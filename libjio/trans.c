
/*
 * Core transaction API
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
#include "compat.h"
#include "journal.h"
#include "trans.h"


/*
 * Transaction functions
 */

/* Initialize a transaction structure */
struct jtrans *jtrans_new(struct jfs *fs)
{
	pthread_mutexattr_t attr;
	struct jtrans *ts;

	ts = malloc(sizeof(struct jtrans));
	if (ts == NULL)
		return NULL;

	ts->fs = fs;
	ts->id = 0;
	ts->flags = fs->flags;
	ts->op = NULL;
	ts->numops = 0;
	ts->len = 0;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init( &(ts->lock), &attr);
	pthread_mutexattr_destroy(&attr);

	return ts;
}

/* Free the contents of a transaction structure */
void jtrans_free(struct jtrans *ts)
{
	struct joper *tmpop;

	ts->fs = NULL;

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

	free(ts);
}

/* Add an operation to a transaction */
int jtrans_add(struct jtrans *ts, const void *buf, size_t count, off_t offset)
{
	struct joper *jop, *tmpop;

	pthread_mutex_lock(&(ts->lock));

	/* fail for read-only accesses */
	if (ts->flags & J_RDONLY) {
		pthread_mutex_unlock(&(ts->lock));
		return -1;
	}

	if ((long long) ts->len + count > MAX_TSIZE) {
		pthread_mutex_unlock(&(ts->lock));
		return -1;
	}

	/* find the last operation in the transaction and create a new one at
	 * the end */
	if (ts->op == NULL) {
		ts->op = malloc(sizeof(struct joper));
		if (ts->op == NULL) {
			pthread_mutex_unlock(&(ts->lock));
			return -1;
		}
		jop = ts->op;
		jop->prev = NULL;
	} else {
		for (tmpop = ts->op; tmpop->next != NULL; tmpop = tmpop->next)
			;
		tmpop->next = malloc(sizeof(struct joper));
		if (tmpop->next == NULL) {
			pthread_mutex_unlock(&(ts->lock));
			return -1;
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
		return -1;
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

	if (!(ts->flags & J_NOROLLBACK)) {
		/* jtrans_commit() will want to read the current data, so we
		 * tell the kernel about that */
		posix_fadvise(ts->fs->fd, offset, count, POSIX_FADV_WILLNEED);
	}

	return 0;
}

/* Commit a transaction */
ssize_t jtrans_commit(struct jtrans *ts)
{
	ssize_t rv;
	struct joper *op;
	struct jlinger *linger;
	jop_t *jop;
	size_t written = 0;

	pthread_mutex_lock(&(ts->lock));

	/* clear the flags */
	ts->flags = ts->flags & ~J_COMMITTED;
	ts->flags = ts->flags & ~J_ROLLBACKED;

	/* fail for read-only accesses */
	if (ts->flags & J_RDONLY)
		goto exit;

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
				goto unlock_exit;
			op->locked = 1;
		}
	}

	jop = journal_new(ts);
	if (jop == NULL)
		goto unlock_exit;

	rv = journal_save(jop);
	if (rv < 0)
		goto unlink_exit;

	/* now that we have a safe transaction file, let's apply it */
	written = 0;
	for (op = ts->op; op != NULL; op = op->next) {
		rv = spwrite(ts->fs->fd, op->buf, op->len, op->offset);
		if (rv != op->len)
			goto rollback_exit;

		written += rv;

		if (have_sync_range && !(ts->flags & J_LINGER)) {
			rv = sync_range_submit(ts->fs->fd, op->len,
					op->offset);
			if (rv != 0)
				goto rollback_exit;
		}

		fiu_exit_on("jio/commit/wrote_op");
	}

	fiu_exit_on("jio/commit/wrote_all_ops");

	if (ts->flags & J_LINGER) {
		linger = malloc(sizeof(struct jlinger));
		if (linger == NULL)
			goto rollback_exit;

		linger->jop = jop;

		pthread_mutex_lock(&(ts->fs->ltlock));
		linger->next = ts->fs->ltrans;
		ts->fs->ltrans = linger;
		ts->fs->ltrans_len += written;
		autosync_check(ts->fs);
		pthread_mutex_unlock(&(ts->fs->ltlock));
	} else {
		if (have_sync_range) {
			for (op = ts->op; op != NULL; op = op->next) {
				rv = sync_range_wait(ts->fs->fd, op->len,
						op->offset);
				if (rv != 0)
					goto rollback_exit;
			}
		} else {
			if (fdatasync(ts->fs->fd) != 0)
				goto rollback_exit;
		}

		/* the transaction has been applied, so we cleanup and remove
		 * it from the disk */
		rv = journal_free(jop);
		if (rv != 0)
			goto rollback_exit;
	}

	jop = NULL;

	/* mark the transaction as committed, _after_ it was removed */
	ts->flags = ts->flags | J_COMMITTED;


rollback_exit:
	/* If the transaction failed we try to recover by rolling it back.
	 *
	 * NOTE: on extreme conditions (ENOSPC/disk failure) this can fail
	 * too! There's nothing much we can do in that case, the caller should
	 * take care of it by itself.
	 *
	 * The transaction file might be OK at this point, so the data could
	 * be recovered by a posterior jfsck(); however, that's not what the
	 * user expects (after all, if we return failure, new data should
	 * never appear), so we remove the transaction file (see unlink_exit).
	 *
	 * Transactions that were successfuly recovered by rolling them back
	 * will have J_ROLLBACKED in their flags */
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
	if (jop)
		journal_free(jop);

unlock_exit:
	/* always unlock everything at the end; otherwise we could have
	 * half-overlapping transactions applying simultaneously, and if
	 * anything goes wrong it would be possible to break consistency */
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

	/* return the length only if it was properly committed */
	if (ts->flags & J_COMMITTED)
		return written;
	else if (ts->flags & J_ROLLBACKED)
		return -1;
	else
		return -2;
}

/* Rollback a transaction */
ssize_t jtrans_rollback(struct jtrans *ts)
{
	ssize_t rv;
	struct jtrans *newts;
	struct joper *op, *curop, *lop;

	newts = jtrans_new(ts->fs);
	newts->flags = ts->flags;
	newts->numops = ts->numops;

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
		if (op->plen < op->len) {
			rv = ftruncate(ts->fs->fd, op->offset + op->plen);
			if (rv != 0)
				goto exit;
		}

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
		if (newts->op == NULL) {
			newts->op = curop;
			curop->prev = NULL;
			curop->next = NULL;
		} else {
			for (lop = newts->op; lop->next != NULL; lop = lop->next)
				;
			lop->next = curop;
			curop->prev = lop;
			curop->next = NULL;
		}
	}

	rv = jtrans_commit(newts);

exit:
	/* free the transaction */
	for (curop = newts->op; curop != NULL; curop = curop->next) {
		curop->buf = NULL;
		curop->pdata = NULL;
	}
	jtrans_free(newts);

	return rv;
}


/*
 * Basic operations
 */

/* Open a file */
struct jfs *jopen(const char *name, int flags, int mode, int jflags)
{
	int jfd, rv;
	unsigned int t;
	char jdir[PATH_MAX], jlockfile[PATH_MAX];
	struct stat sinfo;
	pthread_mutexattr_t attr;
	struct jfs *fs;

	fs = malloc(sizeof(struct jfs));
	if (fs == NULL)
		return NULL;

	fs->fd = -1;
	fs->jfd = -1;
	fs->jdir = NULL;
	fs->jdirfd = -1;
	fs->jmap = MAP_FAILED;
	fs->as_cfg = NULL;

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
	fs->open_flags = flags;
	fs->ltrans = NULL;
	fs->ltrans_len = 0;

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
		return fs;
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

	return fs;

error_exit:
	/* if there was an error, clean up as much as possible so we don't
	 * leak anything, and return failure; jclose just does this cleaning
	 * for us */
	jclose(fs);
	return NULL;
}

/* Sync a file */
int jsync(struct jfs *fs)
{
	int rv;
	struct jlinger *ltmp;

	if (fs->fd < 0)
		return -1;

	rv = fdatasync(fs->fd);
	if (rv != 0)
		return rv;

	pthread_mutex_lock(&(fs->ltlock));
	while (fs->ltrans != NULL) {
		fiu_exit_on("jio/jsync/pre_unlink");
		journal_free(fs->ltrans->jop);

		ltmp = fs->ltrans->next;
		free(fs->ltrans);
		fs->ltrans = ltmp;
	}

	fs->ltrans_len = 0;
	pthread_mutex_unlock(&(fs->ltlock));
	return 0;
}

/* Change the location of the journal directory */
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

/* Close a file opened with jopen() */
int jclose(struct jfs *fs)
{
	int ret;

	ret = 0;

	if (jfs_autosync_stop(fs))
		ret = -1;

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

	free(fs);

	return ret;
}

