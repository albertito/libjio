
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
 *
 * UNIX API wrappers
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "libjio.h"
#include "common.h"


/* read() family wrappers */

/* read wrapper */
ssize_t jread(struct jfs *fs, void *buf, size_t count)
{
	int rv;
	off_t pos;

	pthread_mutex_lock(&(fs->lock));

	pos = lseek(fs->fd, 0, SEEK_CUR);

	plockf(fs->fd, F_LOCK, pos, count);
	rv = spread(fs->fd, buf, count, pos);
	plockf(fs->fd, F_ULOCK, pos, count);

	if (rv == count) {
		/* if success, advance the file pointer */
		lseek(fs->fd, count, SEEK_CUR);
	}

	pthread_mutex_unlock(&(fs->lock));

	return rv;
}

/* pread wrapper */
ssize_t jpread(struct jfs *fs, void *buf, size_t count, off_t offset)
{
	int rv;

	plockf(fs->fd, F_LOCK, offset, count);
	rv = spread(fs->fd, buf, count, offset);
	plockf(fs->fd, F_ULOCK, offset, count);

	return rv;
}

/* readv wrapper */
ssize_t jreadv(struct jfs *fs, struct iovec *vector, int count)
{
	int rv, i;
	size_t sum;
	off_t pos;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += vector[i].iov_len;

	pthread_mutex_lock(&(fs->lock));
	pos = lseek(fs->fd, 0, SEEK_CUR);
	plockf(fs->fd, F_LOCK, pos, count);
	rv = readv(fs->fd, vector, count);
	plockf(fs->fd, F_ULOCK, pos, count);
	pthread_mutex_unlock(&(fs->lock));

	return rv;
}


/* write family wrappers */

/* write wrapper */
ssize_t jwrite(struct jfs *fs, const void *buf, size_t count)
{
	int rv;
	off_t pos;
	struct jtrans ts;

	pthread_mutex_lock(&(fs->lock));

	jtrans_init(fs, &ts);
	pos = lseek(fs->fd, 0, SEEK_CUR);
	ts.offset = pos;

	ts.buf = buf;
	ts.len = count;

	rv = jtrans_commit(&ts);

	if (rv >= 0) {
		/* if success, advance the file pointer */
		lseek(fs->fd, count, SEEK_CUR);
	}

	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(&ts);

	return rv;
}

/* pwrite wrapper */
ssize_t jpwrite(struct jfs *fs, const void *buf, size_t count, off_t offset)
{
	int rv;
	struct jtrans ts;

	jtrans_init(fs, &ts);
	ts.offset = offset;

	ts.buf = buf;
	ts.len = count;

	rv = jtrans_commit(&ts);

	jtrans_free(&ts);

	return rv;
}

/* writev wrapper */
ssize_t jwritev(struct jfs *fs, const struct iovec *vector, int count)
{
	int rv, i, bufp;
	ssize_t sum;
	char *buf;
	off_t pos;
	struct jtrans ts;

	sum = 0;
	for (i = 0; i < count; i++)
		sum += vector[i].iov_len;

	/* unify the buffers into one big chunk to commit */
	/* FIXME: can't we do this more efficient? It ruins the whole purpose
	 * of using writev()! maybe we should do one transaction per vector */
	buf = malloc(sum);
	if (buf == NULL)
		return -1;
	bufp = 0;

	for (i = 0; i < count; i++) {
		memcpy(buf + bufp, vector[i].iov_base, vector[i].iov_len);
		bufp += vector[i].iov_len;
	}

	pthread_mutex_lock(&(fs->lock));

	jtrans_init(fs, &ts);
	pos = lseek(fs->fd, 0, SEEK_CUR);
	ts.offset = pos;

	ts.buf = buf;
	ts.len = sum;

	rv = jtrans_commit(&ts);

	if (rv >= 0) {
		/* if success, advance the file pointer */
		lseek(fs->fd, count, SEEK_CUR);
	}

	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(&ts);

	return rv;

}

/* truncate a file - be careful with this */
int jtruncate(struct jfs *fs, off_t lenght)
{
	int rv;

	/* lock from lenght to the end of file */
	plockf(fs->fd, F_LOCK, lenght, 0);
	rv = ftruncate(fs->fd, lenght);
	plockf(fs->fd, F_ULOCK, lenght, 0);

	return rv;
}

