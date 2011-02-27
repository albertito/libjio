
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
struct jtrans *jtrans_new(struct jfs *fs, unsigned int flags)
{
	pthread_mutexattr_t attr;
	struct jtrans *ts;

	ts = malloc(sizeof(struct jtrans));
	if (ts == NULL)
		return NULL;

	ts->fs = fs;
	ts->id = 0;
	ts->flags = fs->flags | flags;
	ts->op = NULL;
	ts->numops_r = 0;
	ts->numops_w = 0;
	ts->len_w = 0;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init(&(ts->lock), &attr);
	pthread_mutexattr_destroy(&attr);

	return ts;
}

/* Free the contents of a transaction structure */
void jtrans_free(struct jtrans *ts)
{
	struct operation *tmpop;

	ts->fs = NULL;

	while (ts->op != NULL) {
		tmpop = ts->op->next;

		if (ts->op->buf && ts->op->direction == D_WRITE)
			free(ts->op->buf);
		if (ts->op->pdata)
			free(ts->op->pdata);
		free(ts->op);

		ts->op = tmpop;
	}
	pthread_mutex_destroy(&(ts->lock));

	free(ts);
}

/** Lock/unlock the ranges of the file covered by the transaction. mode must
 * be either F_LOCKW or F_UNLOCK. Returns 0 on success, -1 on error. */
static int lock_file_ranges(struct jtrans *ts, int mode)
{
	unsigned int nops;
	off_t lr, min_offset;
	struct operation *op, *start_op;

	if (ts->flags & J_NOLOCK)
		return 0;

	/* Lock/unlock always in the same order to avoid deadlocks. We will
	 * begin with the operation that has the smallest start offset, and go
	 * from there.
	 * Note that this is O(n^2), but n is usually (very) small, and we're
	 * about to do synchronous I/O, so it's not really worrying. It has a
	 * small optimization to help when the operations tend to be in the
	 * right order. */
	nops = 0;
	min_offset = 0;
	start_op = ts->op;
	while (nops < ts->numops_r + ts->numops_w) {
		for (op = start_op; op != NULL; op = op->next) {
			if (min_offset < op->offset)
				continue;
			min_offset = op->offset;
			start_op = op->next;

			if (mode == F_LOCKW) {
				lr = plockf(ts->fs->fd, F_LOCKW, op->offset, op->len);
				if (lr == -1)
					goto error;
				op->locked = 1;
			} else if (mode == F_UNLOCK && op->locked) {
				lr = plockf(ts->fs->fd, F_UNLOCK, op->offset,
						op->len);
				if (lr == -1)
					goto error;
				op->locked = 0;
			}
		}

		nops++;
	}

	return 0;

error:
	return -1;
}

/** Read the previous information from the disk into the given operation
 * structure. Returns 0 on success, -1 on error. */
static int operation_read_prev(struct jtrans *ts, struct operation *op)
{
	ssize_t rv;

	op->pdata = malloc(op->len);
	if (op->pdata == NULL)
		return -1;

	rv = spread(ts->fs->fd, op->pdata, op->len,
			op->offset);
	if (rv < 0) {
		free(op->pdata);
		op->pdata = NULL;
		return -1;
	}

	op->plen = op->len;
	if (rv < op->len) {
		/* we are extending the file! */
		/* ftruncate(ts->fs->fd, op->offset + op->len); */
		op->plen = rv;
	}

	return 0;
}

/** Common function to add an operation to a transaction */
static int jtrans_add_common(struct jtrans *ts, const void *buf, size_t count,
		off_t offset, enum op_direction direction)
{
	struct operation *op, *tmpop;

	op = tmpop = NULL;

	pthread_mutex_lock(&(ts->lock));

	/* Writes are not allowed in read-only mode, they fail early */
	if ((ts->flags & J_RDONLY) && direction == D_WRITE)
		goto error;

	if (count == 0)
		goto error;

	if ((long long) ts->len_w + count > MAX_TSIZE)
		goto error;

	op = malloc(sizeof(struct operation));
	if (op == NULL)
		goto error;

	if (direction == D_WRITE) {
		op->buf = malloc(count);
		if (op->buf == NULL)
			goto error;

		ts->numops_w++;
	} else {
		ts->numops_r++;
	}

	/* add op to the end of the linked list */
	op->next = NULL;
	if (ts->op == NULL) {
		ts->op = op;
		op->prev = NULL;
	} else {
		for (tmpop = ts->op; tmpop->next != NULL; tmpop = tmpop->next)
			;
		tmpop->next = op;
		op->prev = tmpop;
	}

	pthread_mutex_unlock(&(ts->lock));

	op->len = count;
	op->offset = offset;
	op->plen = 0;
	op->pdata = NULL;
	op->locked = 0;
	op->direction = direction;

	if (direction == D_WRITE) {
		memcpy(op->buf, buf, count);

		if (!(ts->flags & J_NOROLLBACK)) {
			/* jtrans_commit() will want to read the current data,
			 * so we tell the kernel about that */
			posix_fadvise(ts->fs->fd, offset, count,
					POSIX_FADV_WILLNEED);
		}
	} else {
		/* this casts the const away, which is ugly but let us have a
		 * common read/write path and avoid useless code repetition
		 * just to handle it */
		op->buf = (void *) buf;

		/* if there are no overlapping writes, jtrans_commit() will
		 * want to read the data from the disk; and if there are we
		 * will already have submitted a request and one more won't
		 * hurt */
		posix_fadvise(ts->fs->fd, offset, count, POSIX_FADV_WILLNEED);
	}

	return 0;

error:
	pthread_mutex_unlock(&(ts->lock));

	if (op && direction == D_WRITE)
		free(op->buf);
	free(op);

	return -1;
}

int jtrans_add_r(struct jtrans *ts, void *buf, size_t count, off_t offset)
{
	return jtrans_add_common(ts, buf, count, offset, D_READ);
}

int jtrans_add_w(struct jtrans *ts, const void *buf, size_t count,
		off_t offset)
{
	return jtrans_add_common(ts, buf, count, offset, D_WRITE);
}


/* Commit a transaction */
ssize_t jtrans_commit(struct jtrans *ts)
{
	ssize_t r, retval = -1;
	struct operation *op;
	struct jlinger *linger;
	jop_t *jop = NULL;
	size_t written = 0;

	pthread_mutex_lock(&(ts->lock));

	/* clear the flags */
	ts->flags = ts->flags & ~J_COMMITTED;
	ts->flags = ts->flags & ~J_ROLLBACKED;

	if (ts->numops_r + ts->numops_w == 0)
		goto exit;

	/* fail for read-only accesses if we have write operations */
	if (ts->numops_w && (ts->flags & J_RDONLY))
		goto exit;

	/* Lock all the regions we're going to work with; otherwise there
	 * could be another transaction trying to write the same spots and we
	 * could end up with interleaved writes, that could break atomicity
	 * warantees if we need to rollback.
	 * Note we do this before creating a new transaction, so we know it's
	 * not possible to have two overlapping transactions on disk at the
	 * same time. */
	if (lock_file_ranges(ts, F_LOCKW) != 0)
		goto unlock_exit;

	/* create and fill the transaction file only if we have at least one
	 * write operation */
	if (ts->numops_w) {
		jop = journal_new(ts->fs, ts->flags);
		if (jop == NULL)
			goto unlock_exit;
	}

	for (op = ts->op; op != NULL; op = op->next) {
		if (op->direction == D_READ)
			continue;

		r = journal_add_op(jop, op->buf, op->len, op->offset);
		if (r != 0)
			goto unlink_exit;

		fiu_exit_on("jio/commit/tf_opdata");
	}

	if (jop)
		journal_pre_commit(jop);

	fiu_exit_on("jio/commit/tf_data");

	if (!(ts->flags & J_NOROLLBACK)) {
		for (op = ts->op; op != NULL; op = op->next) {
			if (op->direction == D_READ)
				continue;

			 r = operation_read_prev(ts, op);
			 if (r < 0)
				 goto unlink_exit;
		}
	}

	if (jop) {
		r = journal_commit(jop);
		if (r < 0)
			goto unlink_exit;
	}

	/* now that we have a safe transaction file, let's apply it */
	written = 0;
	for (op = ts->op; op != NULL; op = op->next) {
		if (op->direction == D_READ) {
			r = spread(ts->fs->fd, op->buf, op->len, op->offset);
			if (r != op->len)
				goto rollback_exit;

			continue;
		}

		/* from now on, write ops (which are more interesting) */

		r = spwrite(ts->fs->fd, op->buf, op->len, op->offset);
		if (r != op->len)
			goto rollback_exit;

		written += r;

		if (have_sync_range && !(ts->flags & J_LINGER)) {
			r = sync_range_submit(ts->fs->fd, op->len,
					op->offset);
			if (r != 0)
				goto rollback_exit;
		}

		fiu_exit_on("jio/commit/wrote_op");
	}

	fiu_exit_on("jio/commit/wrote_all_ops");

	if (jop && (ts->flags & J_LINGER)) {
		struct jlinger *lp;

		linger = malloc(sizeof(struct jlinger));
		if (linger == NULL)
			goto rollback_exit;

		linger->jop = jop;
		linger->next = NULL;

		pthread_mutex_lock(&(ts->fs->ltlock));

		/* add it to the end of the list so they're in order */
		if (ts->fs->ltrans == NULL) {
			ts->fs->ltrans = linger;
		} else {
			lp = ts->fs->ltrans;
			while (lp->next != NULL)
				lp = lp->next;
			lp->next = linger;
		}

		ts->fs->ltrans_len += written;
		autosync_check(ts->fs);

		pthread_mutex_unlock(&(ts->fs->ltlock));

		/* Leave the journal_free() up to jsync() */
		jop = NULL;
	} else if (jop) {
		if (have_sync_range) {
			for (op = ts->op; op != NULL; op = op->next) {
				if (op->direction == D_READ)
					continue;

				r = sync_range_wait(ts->fs->fd, op->len,
						op->offset);
				if (r != 0)
					goto rollback_exit;
			}
		} else {
			if (fdatasync(ts->fs->fd) != 0)
				goto rollback_exit;
		}
	}

	/* mark the transaction as committed */
	ts->flags = ts->flags | J_COMMITTED;

	retval = 1;

rollback_exit:
	/* If the transaction failed we try to recover by rolling it back.
	 * Only used if it has at least one write operation.
	 *
	 * NOTE: on extreme conditions (ENOSPC/disk failure) this can fail
	 * too! There's nothing much we can do in that case, the caller should
	 * take care of it by itself.
	 *
	 * Transactions that were successfuly recovered by rolling them back
	 * will have J_ROLLBACKED in their flags. */
	if (jop && !(ts->flags & J_COMMITTED) &&
			!(ts->flags & J_ROLLBACKING)) {
		r = ts->flags;
		ts->flags = ts->flags | J_NOLOCK | J_ROLLBACKING;
		if (jtrans_rollback(ts) >= 0) {
			ts->flags = r | J_ROLLBACKED;
			retval = -1;
		} else {
			ts->flags = r;
			retval = -2;
		}
	}

unlink_exit:
	/* If the journal operation is no longer needed, we remove it from the
	 * disk.
	 *
	 * Extreme conditions (filesystem just got read-only, for example) can
	 * cause journal_free() to fail, but there's not much left to do at
	 * that point, and the caller will have to be careful and stop its
	 * operations. In that case, we will return -2, and the transaction
	 * will be marked as J_COMMITTED to indicate that the data was
	 * effectively written to disk. */
	if (jop) {
		/* Note we only unlink if we've written down the real data, or
		 * at least rolled it back properly */
		int data_is_safe = (ts->flags & J_COMMITTED) ||
			(ts->flags & J_ROLLBACKED);
		r = journal_free(jop, data_is_safe ? 1 : 0);
		if (r != 0)
			retval = -2;

		jop = NULL;
	}

unlock_exit:
	/* always unlock everything at the end; otherwise we could have
	 * half-overlapping transactions applying simultaneously, and if
	 * anything goes wrong it would be possible to break consistency */
	lock_file_ranges(ts, F_UNLOCK);

exit:
	pthread_mutex_unlock(&(ts->lock));

	return retval;
}

/* Rollback a transaction */
ssize_t jtrans_rollback(struct jtrans *ts)
{
	ssize_t rv;
	struct jtrans *newts;
	struct operation *op, *curop, *lop;

	newts = jtrans_new(ts->fs, 0);
	if (newts == NULL)
		return -1;

	newts->flags = ts->flags;
	newts->numops_r = 0;
	newts->numops_w = 0;
	newts->len_w = 0;

	if (ts->op == NULL || ts->flags & J_NOROLLBACK) {
		rv = -1;
		goto exit;
	}

	/* find the last operation */
	for (op = ts->op; op->next != NULL; op = op->next)
		;

	/* and traverse the list backwards, skipping read operations */
	for ( ; op != NULL; op = op->prev) {
		if (op->direction == D_READ)
			continue;

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
		curop = malloc(sizeof(struct operation));
		if (curop == NULL) {
			rv = -1;
			goto exit;
		}

		curop->offset = op->offset;
		curop->len = op->plen;
		curop->buf = op->pdata;
		curop->plen = op->plen;
		curop->pdata = op->pdata;
		curop->direction = op->direction;
		curop->locked = 0;

		newts->numops_w++;
		newts->len_w += curop->len;

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
	/* Free the transaction, taking care to set buf to NULL first since
	 * points to the same address as pdata, which would otherwise make
	 * jtrans_free() attempt to free it twice. We leave the data at
	 * curop->pdata since it is freed unconditionally, while the action
	 * on curop->buf depends on the direction of the transaction. */
	for (curop = newts->op; curop != NULL; curop = curop->next) {
		curop->buf = NULL;
	}
	jtrans_free(newts);

	return rv;
}


/*
 * Basic operations
 */

/* Open a file */
struct jfs *jopen(const char *name, int flags, int mode, unsigned int jflags)
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
	mkdir(jdir, 0750);
	rv = lstat(jdir, &sinfo);
	if (rv < 0 || !S_ISDIR(sinfo.st_mode))
		goto error_exit;

	fs->jdir = malloc(strlen(jdir) + 1);
	if (fs->jdir == NULL)
		goto error_exit;
	strcpy(fs->jdir, jdir);

	/* open the directory, we will use it to flush transaction files'
	 * metadata in jtrans_commit() */
	fs->jdirfd = open(jdir, O_RDONLY);
	if (fs->jdirfd < 0)
		goto error_exit;

	snprintf(jlockfile, PATH_MAX, "%s/lock", jdir);
	jfd = open(jlockfile, O_RDWR | O_CREAT, 0600);
	if (jfd < 0)
		goto error_exit;

	fs->jfd = jfd;

	/* initialize the lock file by writing the first tid to it, but only
	 * if its empty, otherwise there is a race if two processes call
	 * jopen() simultaneously and both initialize the file */
	plockf(jfd, F_LOCKW, 0, 0);
	lstat(jlockfile, &sinfo);
	if (sinfo.st_size != sizeof(unsigned int)) {
		t = 0;
		rv = spwrite(jfd, &t, sizeof(t), 0);
		if (rv != sizeof(t)) {
			goto error_exit;
		}
	}
	plockf(jfd, F_UNLOCK, 0, 0);

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

	/* note the jops will be in order, so if we crash or fail in the
	 * middle of this, there will be no problem applying the remaining
	 * transactions */
	pthread_mutex_lock(&(fs->ltlock));
	while (fs->ltrans != NULL) {
		fiu_exit_on("jio/jsync/pre_unlink");
		if (journal_free(fs->ltrans->jop, 1) != 0) {
			pthread_mutex_unlock(&(fs->ltlock));
			return -1;
		}

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
	char *oldpath, jlockfile[PATH_MAX], oldjlockfile[PATH_MAX];

	/* we try to be sure that all lingering transactions have been
	 * applied, so when we try to remove the journal directory, only the
	 * lockfile is there; however, we do this just to be nice, the caller
	 * must be sure there are no in-flight transactions or any other kind
	 * of operation around when he calls this function */
	jsync(fs);

	oldpath = fs->jdir;
	snprintf(oldjlockfile, PATH_MAX, "%s/lock", fs->jdir);

	fs->jdir = malloc(strlen(newpath) + 1);
	if (fs->jdir == NULL)
		return -1;
	strcpy(fs->jdir, newpath);

	ret = rename(oldpath, newpath);
	if (ret == -1 && (errno == ENOTEMPTY || errno == EEXIST) ) {
		/* rename() failed, the dest. directory is not empty, so we
		 * have to reload everything */

		close(fs->jdirfd);
		fs->jdirfd = open(newpath, O_RDONLY);
		if (fs->jdirfd < 0)
			goto exit;

		snprintf(jlockfile, PATH_MAX, "%s/lock", newpath);
		ret = rename(oldjlockfile, jlockfile);
		if (ret < 0)
			goto exit;

		/* remove the journal directory, if possible */
		unlink(oldjlockfile);
		ret = rmdir(oldpath);
		if (ret == -1) {
			/* we couldn't remove it, something went wrong
			 * (possibly it had some files left) */
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
	pthread_mutex_destroy(&(fs->ltlock));

	free(fs);

	return ret;
}

