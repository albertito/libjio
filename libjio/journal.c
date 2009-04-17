
/*
 * Internal journal
 */

#include <sys/types.h>		/* [s]size_t */
#include <sys/stat.h>		/* open() */
#include <fcntl.h>		/* open() */
#include <unistd.h>		/* f[data]sync(), close() */
#include <stdlib.h>		/* malloc() and friends */
#include <limits.h>		/* MAX_PATH */
#include <string.h>		/* memcpy() */
#include <libgen.h>		/* basename(), dirname() */
#include <stdio.h>		/* fprintf() */
#include <dirent.h>		/* readdir() and friends */
#include <errno.h>		/* errno */
#include <sys/mman.h>		/* mmap() */

#include "libjio.h"
#include "common.h"
#include "compat.h"
#include "journal.h"
#include "trans.h"


/*
 * helper functions
 */

/** Get a new transaction id */
static unsigned int get_tid(struct jfs *fs)
{
	unsigned int curid, rv;

	/* lock the whole file */
	plockf(fs->jfd, F_LOCKW, 0, 0);

	/* read the current max. curid */
	curid = *(fs->jmap);

	fiu_do_on("jio/get_tid/overflow", curid = -1);

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

/** Free a transaction id */
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


static int already_warned_about_sync = 0;

/** fsync() a directory */
static int fsync_dir(int fd)
{
	int rv;

	rv = fsync(fd);

	if (rv != 0 && (errno == EINVAL || errno == EBADF)) {
		/* it seems to be legal that fsync() on directories is not
		 * implemented, so if this fails with EINVAL or EBADF, just
		 * call a global sync(); which is awful (and might still
		 * return before metadata is done) but it seems to be the
		 * saner choice; otherwise we just fail */
		sync();
		rv = 0;

		if (!already_warned_about_sync) {
			fprintf(stderr, "libjio warning: falling back on " \
					"sync() for directory syncing\n");
			already_warned_about_sync = 1;
		}
	}

	return rv;
}


/*
 * Journal functions
 */

/** Create a new transaction in the journal. Returns a pointer to an opaque
 * jop_t (that is freed using journal_free), or NULL if there was an error. */
struct journal_op *journal_new(struct jtrans *ts)
{
	int fd, id;
	ssize_t rv;
	char *name = NULL;
	unsigned char buf_init[J_DISKHEADSIZE];
	unsigned char *bufp;
	struct journal_op *jop = NULL;

	jop = malloc(sizeof(struct journal_op));
	if (jop == NULL)
		goto error;

	name = (char *) malloc(PATH_MAX);
	if (name == NULL)
		goto error;

	id = get_tid(ts->fs);
	if (id == 0)
		goto error;

	/* open the transaction file */
	get_jtfile(ts->fs, id, name);
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		goto error;

	jop->id = id;
	jop->fd = fd;
	jop->name = name;
	jop->curpos = 0;
	jop->ts = ts;
	jop->fs = ts->fs;

	fiu_exit_on("jio/commit/created_tf");

	/* and lock it, just in case */
	plockf(fd, F_LOCKW, 0, 0);

	ts->id = id;

	/* save the header */
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
		goto unlink_error;
	}

	jop->curpos = J_DISKHEADSIZE;

	fiu_exit_on("jio/commit/tf_header");

	return jop;

unlink_error:
	unlink(name);
	free_tid(ts->fs, ts->id);
	close(fd);

error:
	if (name)
		free(name);
	if (jop)
		free(jop);

	return NULL;
}

/** Save the given transaction in the journal */
int journal_save(struct journal_op *jop)
{
	ssize_t rv;
	uint32_t csum;
	struct joper *op;
	unsigned char hdr[J_DISKOPHEADSIZE];
	unsigned char *hdrp;
	const struct jtrans *ts = jop->ts;

	/* save each transacion in the file */
	for (op = ts->op; op != NULL; op = op->next) {
		/* read the current content only if the transaction is not
		 * marked as NOROLLBACK, and if the data is not there yet,
		 * which is the normal case, but for rollbacking we fill it
		 * ourselves */
		if (!(ts->flags & J_NOROLLBACK) && (op->pdata == NULL)) {
			op->pdata = malloc(op->len);
			if (op->pdata == NULL)
				goto error;

			op->plen = op->len;

			rv = spread(ts->fs->fd, op->pdata, op->len,
					op->offset);
			if (rv < 0)
				goto error;
			if (rv < op->len) {
				/* we are extending the file! */
				/* ftruncate(ts->fs->fd, op->offset + op->len); */
				op->plen = rv;
			}
		}

		/* save the operation's header */
		hdrp = hdr;

		memcpy(hdrp, (void *) &(op->len), 4);
		hdrp += 4;

		memcpy(hdrp, (void *) &(op->plen), 4);
		hdrp += 4;

		memcpy(hdrp, (void *) &(op->offset), 8);
		hdrp += 8;

		rv = spwrite(jop->fd, hdr, J_DISKOPHEADSIZE, jop->curpos);
		if (rv != J_DISKOPHEADSIZE)
			goto error;

		fiu_exit_on("jio/commit/tf_ophdr");

		jop->curpos += J_DISKOPHEADSIZE;

		/* and save it to the disk */
		rv = spwrite(jop->fd, op->buf, op->len, jop->curpos);
		if (rv != op->len)
			goto error;

		jop->curpos += op->len;

		fiu_exit_on("jio/commit/tf_opdata");
	}

	fiu_exit_on("jio/commit/tf_data");

	/* compute and save the checksum (curpos is always small, so there's
	 * no overflow possibility when we convert to size_t) */
	if (!checksum(jop->fd, jop->curpos, &csum))
		goto error;

	rv = spwrite(jop->fd, &csum, sizeof(uint32_t), jop->curpos);
	if (rv != sizeof(uint32_t))
		goto error;
	jop->curpos += sizeof(uint32_t);

	/* this is a simple but efficient optimization: instead of doing
	 * everything O_SYNC, we sync at this point only, this way we avoid
	 * doing a lot of very small writes; in case of a crash the
	 * transaction file is only useful if it's complete (ie. after this
	 * point) so we only flush here (both data and metadata) */
	if (fsync(jop->fd) != 0)
		goto error;
	if (fsync_dir(ts->fs->jdirfd) != 0)
		goto error;

	fiu_exit_on("jio/commit/tf_sync");

	return 0;

error:
	return -1;
}

/** Free a journal operation.
 * NOTE: It can't assume the save completed successfuly, so we can call it
 * when journal_save() fails.  */
int journal_free(struct journal_op *jop)
{
	int rv;

	rv = -1;

	if (unlink(jop->name)) {
		/* we do not want to leave a possibly complete transaction
		 * file around when the transaction was not commited and the
		 * unlink failed, so we attempt to truncate it, and if that
		 * fails we corrupt the checksum as a last resort */
		if (ftruncate(jop->fd, 0) != 0) {
			if (pwrite(jop->fd, "\0\0\0\0", 4, jop->curpos - 4)
					!= 4)
				goto exit;
			if (fdatasync(jop->fd) != 0)
				goto exit;
		}
	}

	if (fsync_dir(jop->fs->jdirfd) != 0)
		goto exit;

	fiu_exit_on("jio/commit/pre_ok_free_tid");
	free_tid(jop->fs, jop->id);

	rv = 0;

exit:
	close(jop->fd);

	if (jop->name)
		free(jop->name);

	free(jop);

	return rv;
}


