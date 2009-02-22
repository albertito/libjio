
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertito@blitiri.com.ar)
 *
 * Core transaction API and recovery functions
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>

#include "libjio.h"
#include "common.h"


/*
 * helper functions
 */

/* gets a new transaction id */
static unsigned int get_tid(struct jfs *fs)
{
	unsigned int curid, rv;

	/* lock the whole file */
	plockf(fs->jfd, F_LOCKW, 0, 0);

	/* read the current max. curid */
	curid = *(fs->jmap);

	/* increment it and handle overflows */
	rv = curid + 1;
	if (rv == 0)
		goto exit;

	/* write to the file descriptor */
	*(fs->jmap) = rv;

exit:
	plockf(fs->jfd, F_UNLOCK, 0, 0);
	return rv;
}

/* frees a transaction id */
static void free_tid(struct jfs *fs, unsigned int tid)
{
	unsigned int curid, i;
	char name[PATH_MAX];

	/* lock the whole file */
	plockf(fs->jfd, F_LOCKW, 0, 0);

	/* read the current max. curid */
	curid = *(fs->jmap);

	/* if we're the max tid, scan the directory looking up for the new
	 * max; the detailed description can be found in the "doc/" dir */
	if (tid == curid) {
		/* look up the new max. */
		for (i = curid - 1; i > 0; i--) {
			get_jtfile(fs, i, name);
			if (access(name, R_OK | W_OK) == 0) {
				break;
			} else if (errno != EACCES) {
				/* Real error, stop looking for a new max. It
				 * doesn't hurt us because it's ok if the max
				 * is higher than it could be */
				break;
			}
		}

		/* and save it */
		*(fs->jmap) = i;
	}

	plockf(fs->jfd, F_UNLOCK, 0, 0);
	return;
}


/*
 * transaction functions
 */

/* initialize a transaction structure */
void jtrans_init(struct jfs *fs, struct jtrans *ts)
{
	pthread_mutexattr_t attr;

	ts->fs = fs;
	ts->name = NULL;
	ts->id = 0;
	ts->flags = fs->flags;
	ts->op = NULL;
	ts->numops = 0;
	ts->len = 0;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init( &(ts->lock), &attr);
	pthread_mutexattr_destroy(&attr);
}


/* free the contents of a transaction structure */
void jtrans_free(struct jtrans *ts)
{
	struct joper *tmpop;

	ts->fs = NULL;

	if (ts->name)
		free(ts->name);

	while (ts->op != NULL) {
		tmpop = ts->op->next;

		if (ts->op->buf)
			free(ts->op->buf);
		if (ts->op->pdata)
			free(ts->op->pdata);
		free(ts->op);

		ts->op = tmpop;
	}
	pthread_mutex_destroy(&(ts->lock));
}


int jtrans_add(struct jtrans *ts, const void *buf, size_t count, off_t offset)
{
	struct joper *jop, *tmpop;

	pthread_mutex_lock(&(ts->lock));

	/* fail for read-only accesses */
	if (ts->flags & J_RDONLY) {
		pthread_mutex_unlock(&(ts->lock));
		return 0;
	}

	if ((long long) ts->len + count > MAX_TSIZE) {
		pthread_mutex_unlock(&(ts->lock));
		return 0;
	}

	/* find the last operation in the transaction and create a new one at
	 * the end */
	if (ts->op == NULL) {
		ts->op = malloc(sizeof(struct joper));
		if (ts->op == NULL) {
			pthread_mutex_unlock(&(ts->lock));
			return 0;
		}
		jop = ts->op;
		jop->prev = NULL;
	} else {
		for (tmpop = ts->op; tmpop->next != NULL; tmpop = tmpop->next)
			;
		tmpop->next = malloc(sizeof(struct joper));
		if (tmpop->next == NULL) {
			pthread_mutex_unlock(&(ts->lock));
			return 0;
		}
		tmpop->next->prev = tmpop;
		jop = tmpop->next;
	}

	jop->buf = malloc(count);
	if (jop->buf == NULL) {
		/* remove from the list and fail */
		if (jop->prev == NULL) {
			ts->op = NULL;
		} else {
			jop->prev->next = jop->next;
		}
		free(jop);
		pthread_mutex_unlock(&(ts->lock));
		return 0;
	}

	ts->numops++;
	ts->len += count;
	pthread_mutex_unlock(&(ts->lock));

	/* we copy the buffer because then the caller can reuse it */
	memcpy(jop->buf, buf, count);
	jop->len = count;
	jop->offset = offset;
	jop->next = NULL;
	jop->plen = 0;
	jop->pdata = NULL;
	jop->locked = 0;

	return 1;
}

/* commit a transaction */
ssize_t jtrans_commit(struct jtrans *ts)
{
	int id, fd = -1;
	ssize_t rv;
	uint32_t csum;
	char *name;
	unsigned char *buf_init, *bufp;
	struct joper *op;
	struct jlinger *linger;
	off_t curpos = 0;
	size_t written = 0;

	pthread_mutex_lock(&(ts->lock));

	/* clear the flags */
	ts->flags = ts->flags & ~J_COMMITTED;
	ts->flags = ts->flags & ~J_ROLLBACKED;

	/* fail for read-only accesses */
	if (ts->flags & J_RDONLY)
		goto exit;

	name = (char *) malloc(PATH_MAX);
	if (name == NULL)
		goto exit;

	id = get_tid(ts->fs);
	if (id == 0)
		goto exit;

	/* open the transaction file */
	get_jtfile(ts->fs, id, name);
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		goto exit;

	/* and lock it */
	plockf(fd, F_LOCKW, 0, 0);

	ts->id = id;
	ts->name = name;

	/* save the header */
	buf_init = malloc(J_DISKHEADSIZE);
	if (buf_init == NULL)
		goto unlink_exit;

	bufp = buf_init;

	memcpy(bufp, (void *) &(ts->id), 4);
	bufp += 4;

	memcpy(bufp, (void *) &(ts->flags), 4);
	bufp += 4;

	memcpy(bufp, (void *) &(ts->numops), 4);
	bufp += 4;

	rv = spwrite(fd, buf_init, J_DISKHEADSIZE, 0);
	if (rv != J_DISKHEADSIZE) {
		free(buf_init);
		goto unlink_exit;
	}

	free(buf_init);

	curpos = J_DISKHEADSIZE;

	/* first of all lock all the regions we're going to work with;
	 * otherwise there could be another transaction trying to write the
	 * same spots and we could end up with interleaved writes, that could
	 * break atomicity warantees if we need to rollback */
	if (!(ts->flags & J_NOLOCK)) {
		off_t lr;
		for (op = ts->op; op != NULL; op = op->next) {
			lr = plockf(ts->fs->fd, F_LOCKW, op->offset, op->len);
			if (lr == -1)
				/* note it can fail with EDEADLK */
				goto unlink_exit;
			op->locked = 1;
		}
	}

	/* save each transacion in the file */
	for (op = ts->op; op != NULL; op = op->next) {
		/* read the current content only if the transaction is not
		 * marked as NOROLLBACK, and if the data is not there yet,
		 * which is the normal case, but for rollbacking we fill it
		 * ourselves */
		if (!(ts->flags & J_NOROLLBACK) && (op->pdata == NULL)) {
			op->pdata = malloc(op->len);
			if (op->pdata == NULL)
				goto unlink_exit;

			op->plen = op->len;

			rv = spread(ts->fs->fd, op->pdata, op->len,
					op->offset);
			if (rv < 0)
				goto unlink_exit;
			if (rv < op->len) {
				/* we are extending the file! */
				/* ftruncate(ts->fs->fd, op->offset + op->len); */
				op->plen = rv;
			}
		}

		/* save the operation's header */
		buf_init = malloc(J_DISKOPHEADSIZE);
		if (buf_init == NULL)
			goto unlink_exit;

		bufp = buf_init;

		memcpy(bufp, (void *) &(op->len), 4);
		bufp += 4;

		memcpy(bufp, (void *) &(op->plen), 4);
		bufp += 4;

		memcpy(bufp, (void *) &(op->offset), 8);
		bufp += 8;

		rv = spwrite(fd, buf_init, J_DISKOPHEADSIZE, curpos);
		if (rv != J_DISKOPHEADSIZE) {
			free(buf_init);
			goto unlink_exit;
		}

		free(buf_init);

		curpos += J_DISKOPHEADSIZE;

		/* and save it to the disk */
		rv = spwrite(fd, op->buf, op->len, curpos);
		if (rv != op->len)
			goto unlink_exit;

		curpos += op->len;
	}

	/* compute and save the checksum (curpos is always small, so there's
	 * no overflow possibility when we convert to size_t) */
	if (!checksum(fd, curpos, &csum))
		goto unlink_exit;

	rv = spwrite(fd, &csum, sizeof(uint32_t), curpos);
	if (rv != sizeof(uint32_t))
		goto unlink_exit;
	curpos += sizeof(uint32_t);

	/* this is a simple but efficient optimization: instead of doing
	 * everything O_SYNC, we sync at this point only, this way we avoid
	 * doing a lot of very small writes; in case of a crash the
	 * transaction file is only useful if it's complete (ie. after this
	 * point) so we only flush here (both data and metadata) */
	if (fsync(fd) != 0)
		goto unlink_exit;
	if (fsync(ts->fs->jdirfd) != 0) {
		/* it seems to be legal that fsync() on directories is not
		 * implemented, so if this fails with EINVAL or EBADF, just
		 * call a global sync(); which is awful (and might still
		 * return before metadata is done) but it seems to be the
		 * saner choice; otherwise we just fail */
		if (errno == EINVAL || errno == EBADF) {
			sync();
		} else {
			goto unlink_exit;
		}
	}

	/* now that we have a safe transaction file, let's apply it */
	written = 0;
	for (op = ts->op; op != NULL; op = op->next) {
		rv = spwrite(ts->fs->fd, op->buf, op->len, op->offset);
		if (rv != op->len)
			goto rollback_exit;

		written += rv;
	}

	if (ts->flags & J_LINGER) {
		linger = malloc(sizeof(struct jlinger));
		if (linger == NULL)
			goto rollback_exit;

		linger->id = id;
		linger->name = strdup(name);

		pthread_mutex_lock(&(ts->fs->ltlock));
		linger->next = ts->fs->ltrans;
		ts->fs->ltrans = linger;
		pthread_mutex_unlock(&(ts->fs->ltlock));
	} else {
		/* the transaction has been applied, so we cleanup and remove
		 * it from the disk */
		unlink(name);
		free_tid(ts->fs, ts->id);
	}

	/* mark the transaction as commited, _after_ it was removed */
	ts->flags = ts->flags | J_COMMITTED;


rollback_exit:
	/* If the transaction failed we try to recover by rollbacking it
	 * NOTE: on extreme conditions (ENOSPC/disk failure) this can fail
	 * too! There's nothing much we can do in that case, the caller should
	 * take care of it by itself.
	 * The transaction file might be OK at this point, so the data could
	 * be recovered by a posterior jfsck(); however, that's not what the
	 * user expects (after all, if we return failure, new data should
	 * never appear), so we remove the transaction file.
	 * Transactions that were successfuly recovered by rollbacking them
	 * will have J_ROLLBACKED in their flags, so the caller can verify if
	 * the failure was recovered or not. */
	if (!(ts->flags & J_COMMITTED) && !(ts->flags & J_ROLLBACKING)) {
		rv = ts->flags;
		ts->flags = ts->flags | J_NOLOCK | J_ROLLBACKING;
		if (jtrans_rollback(ts) >= 0) {
			ts->flags = rv | J_ROLLBACKED;
		} else {
			ts->flags = rv;
		}
	}

unlink_exit:
	if (!(ts->flags & J_COMMITTED)) {
		unlink(name);
		free_tid(ts->fs, ts->id);
	}

	close(fd);

	/* always unlock everything at the end; otherwise we could have
	 * half-overlapping transactions applying simultaneously, and if
	 * anything goes wrong it's possible to break consistency */
	if (!(ts->flags & J_NOLOCK)) {
		for (op = ts->op; op != NULL; op = op->next) {
			if (op->locked) {
				plockf(ts->fs->fd, F_UNLOCK,
						op->offset, op->len);
			}
		}
	}

exit:
	pthread_mutex_unlock(&(ts->lock));

	/* return the length only if it was properly commited */
	if (ts->flags & J_COMMITTED)
		return written;
	else
		return -1;

}

/* rollback a transaction */
ssize_t jtrans_rollback(struct jtrans *ts)
{
	ssize_t rv;
	struct jtrans newts;
	struct joper *op, *curop, *lop;

	jtrans_init(ts->fs, &newts);
	newts.flags = ts->flags;

	if (ts->op == NULL || ts->flags & J_NOROLLBACK) {
		rv = -1;
		goto exit;
	}

	/* find the last operation */
	for (op = ts->op; op->next != NULL; op = op->next)
		;

	/* and traverse the list backwards */
	for ( ; op != NULL; op = op->prev) {
		/* if we extended the data in the previous transaction, we
		 * should truncate it back */
		/* DANGEROUS: this is one of the main reasons why rollbacking
		 * is dangerous and should only be done with extreme caution:
		 * if for some reason, after the previous transacton, we have
		 * extended the file further, this will cut it back to what it
		 * was; read the docs for more detail */
		if (op->plen < op->len)
			ftruncate(ts->fs->fd, op->offset + op->plen);

		/* manually add the operation to the new transaction */
		curop = malloc(sizeof(struct joper));
		if (curop == NULL) {
			rv = -1;
			goto exit;
		}

		curop->offset = op->offset;
		curop->len = op->plen;
		curop->buf = op->pdata;
		curop->plen = op->plen;
		curop->pdata = op->pdata;
		curop->locked = 0;

		/* add the new transaction to the list */
		if (newts.op == NULL) {
			newts.op = curop;
			curop->prev = NULL;
			curop->next = NULL;
		} else {
			for (lop = newts.op; lop->next != NULL; lop = lop->next)
				;
			lop->next = curop;
			curop->prev = lop;
			curop->next = NULL;
		}
	}

	rv = jtrans_commit(&newts);

exit:
	/* free the transaction */
	for (curop = newts.op; curop != NULL; curop = curop->next) {
		curop->buf = NULL;
		curop->pdata = NULL;
	}
	jtrans_free(&newts);

	return rv;
}

/*
 * basic operations
 */

/* open a file */
int jopen(struct jfs *fs, const char *name, int flags, int mode, int jflags)
{
	int jfd, rv;
	unsigned int t;
	char jdir[PATH_MAX], jlockfile[PATH_MAX];
	struct stat sinfo;
	pthread_mutexattr_t attr;

	fs->fd = -1;
	fs->jfd = -1;
	fs->jdir = NULL;
	fs->jdirfd = -1;
	fs->jmap = MAP_FAILED;

	/* we provide either read-only or read-write access, because when we
	 * commit a transaction we read the current contents before applying,
	 * and write access is needed for locking with fcntl; the test is done
	 * this way because O_RDONLY is usually 0, so "if (flags & O_RDONLY)"
	 * will fail. */
	if ((flags & O_WRONLY) || (flags & O_RDWR)) {
		flags = flags & ~O_WRONLY;
		flags = flags & ~O_RDONLY;
		flags = flags | O_RDWR;
	} else {
		jflags = jflags | J_RDONLY;
	}

	fs->name = strdup(name);
	fs->flags = jflags;
	fs->ltrans = NULL;

	/* Note on fs->lock usage: this lock is used only to protect the file
	 * pointer. This means that it must only be held while performing
	 * operations that depend or alter the file pointer (jread, jreadv,
	 * jwrite, jwritev), but the others (jpread, jpwrite) are left
	 * unprotected because they can be performed in parallel as long as
	 * they don't affect the same portion of the file (this is protected
	 * by lockf). The lock doesn't slow things down tho: any threaded app
	 * MUST implement this kind of locking anyways if it wants to prevent
	 * data corruption, we only make it easier for them by taking care of
	 * it here. If performance is essential, the jpread/jpwrite functions
	 * should be used, just as real life.
	 * About fs->ltlock, it's used to protect the lingering transactions
	 * list, fs->ltrans. */
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init( &(fs->lock), &attr);
	pthread_mutex_init( &(fs->ltlock), &attr);
	pthread_mutexattr_destroy(&attr);

	fs->fd = open(name, flags, mode);
	if (fs->fd < 0)
		goto error_exit;

	/* nothing else to do for read-only access */
	if (jflags & J_RDONLY) {
		return fs->fd;
	}

	if (!get_jdir(name, jdir))
		goto error_exit;
	rv = mkdir(jdir, 0750);
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode))
		goto error_exit;

	fs->jdir = (char *) malloc(strlen(jdir) + 1);
	if (fs->jdir == NULL)
		goto error_exit;
	strcpy(fs->jdir, jdir);

	/* open the directory, we will use it to flush transaction files'
	 * metadata in jtrans_commit() */
	fs->jdirfd = open(jdir, O_RDONLY);
	if (fs->jdirfd < 0)
		goto error_exit;

	snprintf(jlockfile, PATH_MAX, "%s/%s", jdir, "lock");
	jfd = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (jfd < 0)
		goto error_exit;

	/* initialize the lock file by writing the first tid to it, but only
	 * if its empty, otherwise there is a race if two processes call
	 * jopen() simultaneously and both initialize the file */
	plockf(jfd, F_LOCKW, 0, 0);
	lstat(jlockfile, &sinfo);
	if (sinfo.st_size != sizeof(unsigned int)) {
		t = 0;
		rv = spwrite(jfd, &t, sizeof(t), 0);
		if (rv != sizeof(t)) {
			plockf(jfd, F_UNLOCK, 0, 0);
			goto error_exit;
		}
	}
	plockf(jfd, F_UNLOCK, 0, 0);

	fs->jfd = jfd;

	fs->jmap = (unsigned int *) mmap(NULL, sizeof(unsigned int),
			PROT_READ | PROT_WRITE, MAP_SHARED, jfd, 0);
	if (fs->jmap == MAP_FAILED)
		goto error_exit;

	return fs->fd;

error_exit:
	/* if there was an error, clean up as much as possible so we don't
	 * leak anything, and return failure; jclose just does this cleaning
	 * for us */
	jclose(fs);
	return -1;
}

/* sync a file (makes sense only if using lingering transactions) */
int jsync(struct jfs *fs)
{
	int rv;
	struct jlinger *ltmp;

	if (fs->fd < 0)
		return -1;

	rv = fsync(fs->fd);
	if (rv != 0)
		return rv;

	pthread_mutex_lock(&(fs->ltlock));
	while (fs->ltrans != NULL) {
		free_tid(fs, fs->ltrans->id);
		unlink(fs->ltrans->name);
		free(fs->ltrans->name);

		ltmp = fs->ltrans->next;
		free(fs->ltrans);

		fs->ltrans = ltmp;
	}

	pthread_mutex_unlock(&(fs->ltlock));
	return 0;
}

/* change the location of the journal directory */
int jmove_journal(struct jfs *fs, const char *newpath)
{
	int ret;
	char *oldpath, jlockfile[PATH_MAX];

	/* we try to be sure that all lingering transactions have been
	 * applied, so when we try to remove the journal directory, only the
	 * lockfile is there; however, we do this just to be nice, but the
	 * caller must be sure there are no in-flight transactions or any
	 * other kind of operation around when he calls this function */
	jsync(fs);

	oldpath = fs->jdir;

	fs->jdir = (char *) malloc(strlen(newpath + 1));
	if (fs->jdir == NULL)
		return -1;
	strcpy(fs->jdir, newpath);

	ret = rename(oldpath, newpath);
	if (ret == -1 && (errno == ENOTEMPTY || errno == EEXIST) ) {
		/* rename() failed, the dest. directory is not empty, so we
		 * have to reload everything */

		close(fs->jdirfd);
		fs->jdirfd = open(newpath, O_RDONLY);
		if (fs->jdirfd < 0) {
			ret = -1;
			goto exit;
		}

		close(fs->jfd);
		snprintf(jlockfile, PATH_MAX, "%s/%s", newpath, "lock");
		fs->jfd = open(jlockfile, O_RDWR | O_CREAT, 0600);
		if (fs->jfd < 0)
			goto exit;

		munmap(fs->jmap, sizeof(unsigned int));
		fs->jmap = (unsigned int *) mmap(NULL, sizeof(unsigned int),
			PROT_READ | PROT_WRITE, MAP_SHARED, fs->jfd, 0);
		if (fs->jmap == MAP_FAILED)
			goto exit;

		/* remove the journal directory, if possible */
		snprintf(jlockfile, PATH_MAX, "%s/%s", oldpath, "lock");
		unlink(jlockfile);
		ret = rmdir(oldpath);
		if (ret == -1) {
			/* we couldn't remove it, something went wrong
			 * (possible it had some files left) */
			goto exit;
		}

		ret = 0;
	}

exit:
	free(oldpath);
	return ret;
}

/* close a file */
int jclose(struct jfs *fs)
{
	int ret;

	ret = 0;

	if (! (fs->flags & J_RDONLY)) {
		if (jsync(fs))
			ret = -1;
		if (fs->jfd < 0 || close(fs->jfd))
			ret = -1;
		if (fs->jdirfd < 0 || close(fs->jdirfd))
			ret = -1;
		if (fs->jmap != MAP_FAILED)
			munmap(fs->jmap, sizeof(unsigned int));
	}

	if (fs->fd < 0 || close(fs->fd))
		ret = -1;
	if (fs->name)
		/* allocated by strdup() in jopen() */
		free(fs->name);
	if (fs->jdir)
		free(fs->jdir);
	pthread_mutex_destroy(&(fs->lock));

	return ret;
}

