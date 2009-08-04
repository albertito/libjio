
/*
 * UNIX API wrappers
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "libjio.h"
#include "common.h"
#include "trans.h"


/*
 * read() family wrappers
 */

/* read() wrapper */
ssize_t jread(struct jfs *fs, void *buf, size_t count)
{
	ssize_t rv;
	off_t pos;

	pthread_mutex_lock(&(fs->lock));

	pos = lseek(fs->fd, 0, SEEK_CUR);

	plockf(fs->fd, F_LOCKR, pos, count);
	rv = spread(fs->fd, buf, count, pos);
	plockf(fs->fd, F_UNLOCK, pos, count);

	if (rv > 0)
		lseek(fs->fd, rv, SEEK_CUR);

	pthread_mutex_unlock(&(fs->lock));

	return rv;
}

/* pread() wrapper */
ssize_t jpread(struct jfs *fs, void *buf, size_t count, off_t offset)
{
	ssize_t rv;

	plockf(fs->fd, F_LOCKR, offset, count);
	rv = spread(fs->fd, buf, count, offset);
	plockf(fs->fd, F_UNLOCK, offset, count);

	return rv;
}

/* readv() wrapper */
ssize_t jreadv(struct jfs *fs, const struct iovec *vector, int count)
{
	ssize_t rv;
	off_t pos;

	pthread_mutex_lock(&(fs->lock));
	pos = lseek(fs->fd, 0, SEEK_CUR);
	if (pos < 0)
		return -1;

	plockf(fs->fd, F_LOCKR, pos, count);
	rv = readv(fs->fd, vector, count);
	plockf(fs->fd, F_UNLOCK, pos, count);

	pthread_mutex_unlock(&(fs->lock));

	return rv;
}


/*
 * write() family wrappers
 */

/* write() wrapper */
ssize_t jwrite(struct jfs *fs, const void *buf, size_t count)
{
	ssize_t rv;
	off_t pos;
	struct jtrans *ts;

	ts = jtrans_new(fs, 0);
	if (ts == NULL)
		return -1;

	pthread_mutex_lock(&(fs->lock));

	if (fs->open_flags & O_APPEND)
		pos = lseek(fs->fd, 0, SEEK_END);
	else
		pos = lseek(fs->fd, 0, SEEK_CUR);

	rv = jtrans_add_w(ts, buf, count, pos);
	if (rv < 0)
		goto exit;

	rv = jtrans_commit(ts);

	if (rv >= 0)
		lseek(fs->fd, count, SEEK_CUR);

exit:

	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(ts);

	return (rv >= 0) ? count : rv;
}

/* pwrite() wrapper */
ssize_t jpwrite(struct jfs *fs, const void *buf, size_t count, off_t offset)
{
	ssize_t rv;
	struct jtrans *ts;

	ts = jtrans_new(fs, 0);
	if (ts == NULL)
		return -1;

	rv = jtrans_add_w(ts, buf, count, offset);
	if (rv < 0)
		goto exit;

	rv = jtrans_commit(ts);

exit:
	jtrans_free(ts);

	return (rv >= 0) ? count : rv;
}

/* writev() wrapper */
ssize_t jwritev(struct jfs *fs, const struct iovec *vector, int count)
{
	int i;
	size_t sum;
	ssize_t rv;
	off_t ipos, t;
	struct jtrans *ts;

	ts = jtrans_new(fs, 0);
	if (ts == NULL)
		return -1;

	pthread_mutex_lock(&(fs->lock));

	if (fs->open_flags & O_APPEND)
		ipos = lseek(fs->fd, 0, SEEK_END);
	else
		ipos = lseek(fs->fd, 0, SEEK_CUR);

	t = ipos;

	sum = 0;
	for (i = 0; i < count; i++) {
		rv = jtrans_add_w(ts, vector[i].iov_base,
				vector[i].iov_len, t);
		if (rv < 0)
			goto exit;

		sum += vector[i].iov_len;
		t += vector[i].iov_len;
	}

	rv = jtrans_commit(ts);

	if (rv >= 0)
		lseek(fs->fd, sum, SEEK_CUR);

exit:
	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(ts);

	return (rv >= 0) ? sum : rv;
}

/* Truncate a file. Be careful with this */
int jtruncate(struct jfs *fs, off_t length)
{
	int rv;

	/* lock from length to the end of file */
	plockf(fs->fd, F_LOCKW, length, 0);
	rv = ftruncate(fs->fd, length);
	plockf(fs->fd, F_UNLOCK, length, 0);

	return rv;
}

/* lseek() wrapper */
off_t jlseek(struct jfs *fs, off_t offset, int whence)
{
	off_t rv;

	pthread_mutex_lock(&(fs->lock));
	rv = lseek(fs->fd, offset, whence);
	pthread_mutex_unlock(&(fs->lock));

	return rv;
}

