
/*
 * Internal journal
 */

#include <sys/types.h>		/* [s]size_t */
#include <sys/stat.h>		/* open() */
#include <fcntl.h>		/* open() */
#include <unistd.h>		/* f[data]sync(), close() */
#include <stdlib.h>		/* malloc() and friends */
#include <limits.h>		/* PATH_MAX */
#include <string.h>		/* memcpy() */
#include <stdio.h>		/* fprintf() */
#include <errno.h>		/* errno */
#include <stdint.h>		/* uintX_t */
#include <arpa/inet.h>		/* htonl() and friends */
#include <netinet/in.h>		/* htonl() and friends (on some platforms) */

#include "libjio.h"
#include "common.h"
#include "compat.h"
#include "journal.h"
#include "trans.h"


/*
 * On-disk structures
 *
 * Each transaction will be stored on disk as a single file, composed of a
 * header, operation information, and a trailer. The operation information is
 * composed of repeated operation headers followed by their corresponding
 * data, one for each operation. A special operation header containing all 0s
 * marks the end of the operations.
 * 
 * Visually, something like this:
 * 
 *  +--------+---------+----------+---------+----------+-----+-----+---------+
 *  | header | op1 hdr | op1 data | op2 hdr | op2 data | ... | eoo | trailer |
 *  +--------+---------+----------+---------+----------+-----+-----+---------+
 *             \                                             /
 *              +--------------- operations ----------------+ 
 *
 * The details of each part can be seen on the following structures. All
 * integers are stored in network byte order.
 */

/** Transaction file header */
struct on_disk_hdr {
	uint16_t ver;
	uint16_t flags;
	uint32_t trans_id;
} __attribute__((packed));

/** Transaction file operation header */
struct on_disk_ophdr {
	uint32_t len;
	uint64_t offset;
} __attribute__((packed));

/** Transaction file trailer */
struct on_disk_trailer {
	uint32_t numops;
	uint32_t checksum;
} __attribute__((packed));


/* Convert structs to/from host to network (disk) endian */

static void hdr_hton(struct on_disk_hdr *hdr)
{
	hdr->ver = htons(hdr->ver);
	hdr->flags = htons(hdr->flags);
	hdr->trans_id = htonl(hdr->trans_id);
}

static void hdr_ntoh(struct on_disk_hdr *hdr)
{
	hdr->ver = ntohs(hdr->ver);
	hdr->flags = ntohs(hdr->flags);
	hdr->trans_id = ntohl(hdr->trans_id);
}

static void ophdr_hton(struct on_disk_ophdr *ophdr)
{
	ophdr->len = htonl(ophdr->len);
	ophdr->offset = htonll(ophdr->offset);
}

static void ophdr_ntoh(struct on_disk_ophdr *ophdr)
{
	ophdr->len = ntohl(ophdr->len);
	ophdr->offset = ntohll(ophdr->offset);
}

static void trailer_hton(struct on_disk_trailer *trailer) {
	trailer->numops = htonl(trailer->numops);
	trailer->checksum = htonl(trailer->checksum);
}

static void trailer_ntoh(struct on_disk_trailer *trailer) {
	trailer->numops = ntohl(trailer->numops);
	trailer->checksum = ntohl(trailer->checksum);
}


/*
 * Helper functions
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

/** Corrupt a journal file. Used as a last resource to prevent an applied
 * transaction file laying around */
static int corrupt_journal_file(struct journal_op *jop)
{
	off_t pos;
	struct on_disk_trailer trailer;

	/* We set the number of operations to 0, and the checksum to
	 * 0xffffffff, so there is no chance it's considered valid after a new
	 * transaction overwrites this one */
	trailer.numops = 0;
	trailer.checksum = 0xffffffff;

	pos = lseek(jop->fd, 0, SEEK_END);
	if (pos == (off_t) -1)
		return -1;

	if (pwrite(jop->fd, (void *) &trailer, sizeof(trailer), pos)
			!= sizeof(trailer))
		return -1;

	if (fdatasync(jop->fd) != 0)
		return -1;

	return 0;
}

/** Mark the journal as broken. To do so, we just create a file named "broken"
 * inside the journal directory. Used internally to mark severe journal errors
 * that should prevent further journal use to avoid potential corruption, like
 * failures to remove transaction files. The mark is removed by jfsck(). */
static int mark_broken(struct jfs *fs)
{
	char broken_path[PATH_MAX];
	int fd;

	snprintf(broken_path, PATH_MAX, "%s/broken", fs->jdir);
	fd = creat(broken_path, 0600);
	close(fd);

	return fd >= 0;
}

/** Check if the journal is broken */
static int is_broken(struct jfs *fs)
{
	char broken_path[PATH_MAX];

	snprintf(broken_path, PATH_MAX, "%s/broken", fs->jdir);
	return access(broken_path, F_OK) == 0;
}


/*
 * Journal functions
 */

/** Create a new transaction in the journal. Returns a pointer to an opaque
 * jop_t (that is freed using journal_free), or NULL if there was an error. */
struct journal_op *journal_new(struct jfs *fs, unsigned int flags)
{
	int fd, id;
	ssize_t rv;
	char *name = NULL;
	struct journal_op *jop = NULL;
	struct on_disk_hdr hdr;
	struct iovec iov[1];

	if (is_broken(fs))
		goto error;

	jop = malloc(sizeof(struct journal_op));
	if (jop == NULL)
		goto error;

	name = (char *) malloc(PATH_MAX);
	if (name == NULL)
		goto error;

	id = get_tid(fs);
	if (id == 0)
		goto error;

	/* open the transaction file */
	get_jtfile(fs, id, name);
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		goto error;

	if (plockf(fd, F_LOCKW, 0, 0) != 0)
		goto unlink_error;

	jop->id = id;
	jop->fd = fd;
	jop->numops = 0;
	jop->name = name;
	jop->csum = 0;
	jop->fs = fs;

	fiu_exit_on("jio/commit/created_tf");

	/* save the header */
	hdr.ver = 1;
	hdr.trans_id = id;
	hdr.flags = flags;
	hdr_hton(&hdr);

	iov[0].iov_base = (void *) &hdr;
	iov[0].iov_len = sizeof(hdr);
	rv = swritev(fd, iov, 1);
	if (rv != sizeof(hdr))
		goto unlink_error;

	jop->csum = checksum_buf(jop->csum, (unsigned char *) &hdr,
			sizeof(hdr));

	fiu_exit_on("jio/commit/tf_header");

	return jop;

unlink_error:
	unlink(name);
	free_tid(fs, id);
	close(fd);

error:
	free(name);
	free(jop);

	return NULL;
}

/** Save a single operation in the journal file */
int journal_add_op(struct journal_op *jop, unsigned char *buf, size_t len,
		off_t offset)
{
	ssize_t rv;
	struct on_disk_ophdr ophdr;
	struct iovec iov[2];

	ophdr.len = len;
	ophdr.offset = offset;
	ophdr_hton(&ophdr);

	iov[0].iov_base = (void *) &ophdr;
	iov[0].iov_len = sizeof(ophdr);
	jop->csum = checksum_buf(jop->csum, (unsigned char *) &ophdr,
			sizeof(ophdr));

	iov[1].iov_base = (void *) buf;
	iov[1].iov_len = len;
	jop->csum = checksum_buf(jop->csum, buf, len);

	fiu_exit_on("jio/commit/tf_pre_addop");

	rv = swritev(jop->fd, iov, 2);
	if (rv != sizeof(ophdr) + len)
		goto error;

	fiu_exit_on("jio/commit/tf_addop");

	jop->numops++;

	return 0;

error:
	return -1;
}

/** Prepares to commit the operation. Can be omitted. */
void journal_pre_commit(struct journal_op *jop)
{
	/* In an attempt to reduce journal_commit() fsync() waiting time, we
	 * submit the sync here, hoping that at least some of it will be ready
	 * by the time we hit journal_commit() */
	sync_range_submit(jop->fd, 0, 0);
}

/** Commit the journal operation */
int journal_commit(struct journal_op *jop)
{
	ssize_t rv;
	struct on_disk_ophdr ophdr;
	struct on_disk_trailer trailer;
	struct iovec iov[2];

	/* write the empty ophdr to mark there are no more operations, and
	 * then the trailer */
	ophdr.len = 0;
	ophdr.offset = 0;
	ophdr_hton(&ophdr);
	iov[0].iov_base = (void *) &ophdr;
	iov[0].iov_len = sizeof(ophdr);
	jop->csum = checksum_buf(jop->csum, (unsigned char *) &ophdr,
			sizeof(ophdr));

	trailer.checksum = jop->csum;
	trailer.numops = jop->numops;
	trailer_hton(&trailer);
	iov[1].iov_base = (void *) &trailer;
	iov[1].iov_len = sizeof(trailer);

	rv = swritev(jop->fd, iov, 2);
	if (rv != sizeof(ophdr) + sizeof(trailer))
		goto error;

	/* this is a simple but efficient optimization: instead of doing
	 * everything O_SYNC, we sync at this point only, this way we avoid
	 * doing a lot of very small writes; in case of a crash the
	 * transaction file is only useful if it's complete (ie. after this
	 * point) so we only flush here (both data and metadata) */
	if (fsync(jop->fd) != 0)
		goto error;
	if (fsync_dir(jop->fs->jdirfd) != 0)
		goto error;

	fiu_exit_on("jio/commit/tf_sync");

	return 0;

error:
	return -1;
}

/** Free a journal operation.
 * NOTE: It can't assume the save completed successfuly, so we can call it
 * when journal_save() fails.  */
int journal_free(struct journal_op *jop, int do_unlink)
{
	int rv;

	if (!do_unlink) {
		rv = 0;
		goto exit;
	}

	rv = -1;

	if (unlink(jop->name)) {
		/* we do not want to leave a possibly complete transaction
		 * file around when the transaction was not commited and the
		 * unlink failed, so we attempt to truncate it, and if that
		 * fails we corrupt it as a last resort. */
		if (ftruncate(jop->fd, 0) != 0) {
			if (corrupt_journal_file(jop) != 0) {
				mark_broken(jop->fs);
				goto exit;
			}
		}
	}

	if (fsync_dir(jop->fs->jdirfd) != 0) {
		mark_broken(jop->fs);
		goto exit;
	}

	fiu_exit_on("jio/commit/pre_ok_free_tid");
	free_tid(jop->fs, jop->id);

	rv = 0;

exit:
	close(jop->fd);

	free(jop->name);
	free(jop);

	return rv;
}

/** Fill a transaction structure from a mmapped transaction file. Useful for
 * checking purposes.
 * @returns 0 on success, -1 if the file was broken, -2 if the checksums didn't
 *	match
 */
int fill_trans(unsigned char *map, off_t len, struct jtrans *ts)
{
	int rv;
	unsigned char *p;
	struct operation *op, *tmp;
	struct on_disk_hdr hdr;
	struct on_disk_ophdr ophdr;
	struct on_disk_trailer trailer;

	rv = -1;

	if (len < sizeof(hdr) + sizeof(ophdr) + sizeof(trailer))
		return -1;

	p = map;

	memcpy(&hdr, p, sizeof(hdr));
	p += sizeof(hdr);

	hdr_ntoh(&hdr);
	if (hdr.ver != 1)
		return -1;

	ts->id = hdr.trans_id;
	ts->flags = hdr.flags;
	ts->numops_r = 0;
	ts->numops_w = 0;
	ts->len_w = 0;

	for (;;) {
		if (p + sizeof(ophdr) > map + len)
			goto error;

		memcpy(&ophdr, p,  sizeof(ophdr));
		p += sizeof(ophdr);

		ophdr_ntoh(&ophdr);

		if (ophdr.len == 0 && ophdr.offset == 0) {
			/* This header marks the end of the operations */
			break;
		}

		if (p + ophdr.len > map + len)
			goto error;

		op = malloc(sizeof(struct operation));
		if (op == NULL)
			goto error;

		op->len = ophdr.len;
		op->offset = ophdr.offset;
		op->direction = D_WRITE;

		op->buf = (void *) p;
		p += op->len;

		op->pdata = NULL;

		if (ts->op == NULL) {
			ts->op = op;
			op->prev = NULL;
			op->next = NULL;
		} else {
			for (tmp = ts->op; tmp->next != NULL; tmp = tmp->next)
				;
			tmp->next = op;
			op->prev = tmp;
			op->next = NULL;
		}

		ts->numops_w++;
		ts->len_w += op->len;
	}

	if (p + sizeof(trailer) > map + len)
		goto error;

	memcpy(&trailer, p, sizeof(trailer));
	p += sizeof(trailer);

	trailer_ntoh(&trailer);

	if (trailer.numops != ts->numops_w)
		goto error;

	if (checksum_buf(0, map, len - sizeof(trailer)) != trailer.checksum) {
		rv = -2;
		goto error;
	}

	return 0;

error:
	while (ts->op != NULL) {
		tmp = ts->op->next;
		free(ts->op);
		ts->op = tmp;
	}
	return rv;
}

