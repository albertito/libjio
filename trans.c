
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
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

#include "libjio.h"
#include "common.h"


/*
 * helper functions
 */

/* gets a new transaction id */
static unsigned int get_tid(struct jfs *fs)
{
	unsigned int curid;
	int r, rv;

	/* lock the whole file */
	plockf(fs->jfd, F_LOCKW, 0, 0);

	/* read the current max. curid */
	r = spread(fs->jfd, &curid, sizeof(curid), 0);
	if (r != sizeof(curid)) {
		rv = 0;
		goto exit;
	}

	/* increment it and handle overflows */
	rv = curid + 1;
	if (rv == 0)
		rv = 1;

	/* write to the file descriptor */
	r = spwrite(fs->jfd, &rv, sizeof(rv), 0);
	if (r != sizeof(curid)) {
		rv = 0;
		goto exit;
	}

exit:
	plockf(fs->jfd, F_UNLOCK, 0, 0);
	return rv;
}

/* frees a transaction id */
static void free_tid(struct jfs *fs, unsigned int tid)
{
	unsigned int curid, i;
	int r;
	char name[PATH_MAX];

	/* lock the whole file */
	plockf(fs->jfd, F_LOCKW, 0, 0);

	/* read the current max. curid */
	r = spread(fs->jfd, &curid, sizeof(curid), 0);
	if (r != sizeof(curid)) {
		goto exit;
	}

	if (tid < curid) {
		/* we're not freeing the max. curid, so we just return */
		goto exit;
	} else {
		/* look up the new max. */
		for (i = curid - 1; i > 0; i--) {
			/* this can fail if we're low on mem, but we don't
			 * care checking here because the problem will come
			 * out later and we can fail more properly */
			get_jtfile(fs->name, i, name);
			if (access(name, R_OK | W_OK) == 0) {
				curid = i;
				break;
			}
		}

		/* and save it */
		r = spwrite(fs->jfd, &i, sizeof(i), 0);
		if (r != sizeof(curid)) {
			goto exit;
		}
	}

exit:
	plockf(fs->jfd, F_UNLOCK, 0, 0);
	return;
}


/*
 * transaction functions
 */

/* initialize a transaction structure */
void jtrans_init(struct jfs *fs, struct jtrans *ts)
{
	ts->fs = fs;
	ts->name = NULL;
	ts->id = 0;
	ts->flags = fs->flags;
	ts->op = NULL;
	ts->numops = 0;
	pthread_mutex_init( &(ts->lock), NULL);
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
}


int jtrans_add(struct jtrans *ts, const void *buf, size_t count, off_t offset)
{
	struct joper *jop, *tmpop;

	/* find the last operation in the transaction and create a new one at
	 * the end */
	pthread_mutex_lock(&(ts->lock));
	if (ts->op == NULL) {
		ts->op = malloc(sizeof(struct joper));
		jop = ts->op;
		jop->prev = NULL;
	} else {
		for (tmpop = ts->op; tmpop->next != NULL; tmpop = tmpop->next)
			;
		tmpop->next = malloc(sizeof(struct joper));
		tmpop->next->prev = tmpop;
		jop = tmpop->next;
	}
	pthread_mutex_unlock(&(ts->lock));

	if (jop == NULL) {
		/* malloc() failed */
		return 0;
	}

	jop->buf = malloc(count);
	if (jop->buf == NULL) {
		free(jop);
		return 0;
	}

	/* we copy the buffer because then the caller can reuse it */
	memcpy(jop->buf, buf, count);
	jop->len = count;
	jop->offset = offset;
	jop->next = NULL;
	jop->plen = 0;
	jop->pdata = NULL;
	jop->locked = 0;

	ts->numops++;

	return 1;
}

/* commit a transaction */
int jtrans_commit(struct jtrans *ts)
{
	int id, rv, fd = -1;
	char *name;
	unsigned char *buf_init, *bufp;
	struct joper *op;
	off_t curpos = 0;
	size_t written = 0;

	pthread_mutex_lock(&(ts->lock));

	name = (char *) malloc(PATH_MAX);
	if (name == NULL)
		goto exit;

	id = get_tid(ts->fs);
	if (id == 0)
		goto exit;

	/* open the transaction file */
	if (!get_jtfile(ts->fs->name, id, name))
		goto exit;
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, 0600);
	if (fd < 0)
		goto exit;

	/* and lock it */
	plockf(fd, F_LOCKW, 0, 0);

	ts->id = id;
	ts->name = name;

	/* save the header */
	buf_init = malloc(J_DISKHEADSIZE);
	if (buf_init == NULL)
		goto exit;

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
		goto exit;
	}

	free(buf_init);

	curpos = J_DISKHEADSIZE;

	/* first of all lock all the regions we're going to work with;
	 * otherwise there could be another transaction trying to write the
	 * same spots and we could end up with interleaved writes, that could
	 * break atomicity warantees if we need to rollback */
	if (!(ts->flags & J_NOLOCK)) {
		for (op = ts->op; op != NULL; op = op->next) {
			rv = plockf(ts->fs->fd, F_LOCKW, op->offset, op->len);
			if (rv == -1)
				/* note it can fail with EDEADLK */
				goto exit;
			op->locked = 1;
		}
	}

	/* save each transacion in the file */
	for (op = ts->op; op != NULL; op = op->next) {
		/* read the current content only if it's not there yet, which
		 * is the normal case, but for rollbacking we fill it
		 * ourselves */
		if (op->pdata == NULL) {
			op->pdata = malloc(op->len);
			if (op->pdata == NULL)
				goto exit;

			op->plen = op->len;

			rv = spread(ts->fs->fd, op->pdata, op->len,
					op->offset);
			if (rv < 0)
				goto exit;
			if (rv < op->len) {
				/* we are extending the file! */
				/* ftruncate(ts->fs->fd, op->offset + op->len); */
				op->plen = rv;
			}
		}

		/* save the operation's header */
		buf_init = malloc(J_DISKOPHEADSIZE);
		if (buf_init == NULL)
			goto exit;

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
			goto exit;
		}

		free(buf_init);

		curpos += J_DISKOPHEADSIZE;

		/* and save it to the disk */
		rv = spwrite(fd, op->pdata, op->plen, curpos);
		if (rv != op->plen)
			goto exit;

		curpos += op->plen;
	}

	/* this is a simple but efficient optimization: instead of doing
	 * everything O_SYNC, we sync at this point only, this way we avoid
	 * doing a lot of very small writes; in case of a crash the
	 * transaction file is only useful if it's complete (ie. after this
	 * point) so we only flush here (both data and metadata) */
	if (fsync(fd) != 0)
		goto exit;
	if (fsync(ts->fs->jdirfd) != 0) {
		/* it seems to be legal that fsync() on directories is not
		 * implemented, so if this fails with EINVAL or EBADF, just
		 * call a global sync(); which is awful (and might still
		 * return before metadata is done) but it seems to be the
		 * saner choice; otherwise we just fail */
		if (errno == EINVAL || errno == EBADF) {
			sync();
		} else {
			goto exit;
		}
	}

	/* now that we have a safe transaction file, let's apply it */
	written = 0;
	for (op = ts->op; op != NULL; op = op->next) {
		rv = spwrite(ts->fs->fd, op->buf, op->len, op->offset);

		plockf(ts->fs->fd, F_UNLOCK, op->offset, op->len);
		op->locked = 0;

		if (rv != op->len)
			goto exit;

		written += rv;
	}

	/* the transaction has been applied, so we cleanup and remove it from
	 * the disk */
	free_tid(ts->fs, ts->id);
	unlink(name);

	/* mark the transaction as commited, _after_ it was removed */
	ts->flags = ts->flags | J_COMMITED;


exit:
	close(fd);
	for (op = ts->op; op != NULL; op = op->next) {
		if (op->locked)
			plockf(ts->fs->fd, F_UNLOCK, op->offset, op->len);
	}

	pthread_mutex_unlock(&(ts->lock));

	/* return the length only if it was properly commited */
	if (ts->flags & J_COMMITED)
		return written;
	else
		return -1;

}

/* rollback a transaction */
int jtrans_rollback(struct jtrans *ts)
{
	struct jtrans newts;
	struct joper *op, *curop, *lop;

	/* FIXME: this looks like a mess! */

	if (ts->op == NULL) {
		/* we're trying to rollback an empty transaction */
		return 0;
	}

	jtrans_init(ts->fs, &newts);
	newts.flags = ts->flags;

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

	return jtrans_commit(&newts);
}

/*
 * basic operations
 */

/* open a file */
int jopen(struct jfs *fs, const char *name, int flags, int mode, int jflags)
{
	int fd, jfd, rv;
	unsigned int t;
	char jdir[PATH_MAX], jlockfile[PATH_MAX];
	struct stat sinfo;

	/* we always need read and write access, because when we commit a
	 * transaction we read the current contents before applying, and write
	 * access is needed for locking with fcntl */
	flags = flags & ~O_WRONLY;
	flags = flags & ~O_RDONLY;
	flags = flags | O_RDWR;

	fd = open(name, flags, mode);
	if (fd < 0)
		return -1;

	fs->fd = fd;
	fs->name = strdup(name);
	fs->flags = jflags;

	/* Note on fs->lock usage: this lock is used only inside the wrappers,
	 * and exclusively to protect the file pointer. This means that it
	 * must only be held while performing operations that depend or alter
	 * the file pointer (jread, jreadv, jwrite, jwritev), but the others
	 * (jpread, jpwrite) are left unprotected because they can be
	 * performed in paralell as long as they don't affect the same portion
	 * of the file (this is protected by lockf). The lock doesn't slow
	 * things down tho: any threaded app MUST implement this kind of
	 * locking anyways if it wants to prevent data corruption, we only
	 * make it easier for them by taking care of it here. If performance
	 * is essential, the jpread/jpwrite functions should be used, just as
	 * real life. */
	pthread_mutex_init( &(fs->lock), NULL);

	if (!get_jdir(name, jdir))
		return -1;
	rv = mkdir(jdir, 0750);
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode))
		return -1;

	/* open the directory, we will use it to flush transaction files'
	 * metadata in jtrans_commit() */
	fs->jdirfd = open(jdir, O_RDONLY);
	if (fs->jdirfd < 0)
		return -1;

	snprintf(jlockfile, PATH_MAX, "%s/%s", jdir, "lock");
	jfd = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (jfd < 0)
		return -1;

	/* initialize the lock file by writing the first tid to it, but only
	 * if its empty, otherwise there is a race if two processes call
	 * jopen() simultaneously and both initialize the file */
	plockf(jfd, F_LOCKW, 0, 0);
	lstat(jlockfile, &sinfo);
	if (sinfo.st_size == 0) {
		t = 1;
		rv = write(jfd, &t, sizeof(t));
		if (rv != sizeof(t)) {
			plockf(jfd, F_UNLOCK, 0, 0);
			return -1;
		}
	}
	plockf(jfd, F_UNLOCK, 0, 0);

	fs->jfd = jfd;

	return fd;
}

/* close a file */
int jclose(struct jfs *fs)
{
	if (close(fs->fd))
		return -1;
	if (close(fs->jfd))
		return -1;
	if (fs->name)
		/* allocated by strdup() in jopen() */
		free(fs->name);
	return 0;
}

