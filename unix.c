
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
	jtrans_add(&ts, buf, count, pos);

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
	jtrans_add(&ts, buf, count, offset);

	rv = jtrans_commit(&ts);

	jtrans_free(&ts);

	return rv;
}

/* writev wrapper */
ssize_t jwritev(struct jfs *fs, const struct iovec *vector, int count)
{
	int rv, i;
	size_t sum;
	off_t ipos, t;
	struct jtrans ts;

	pthread_mutex_lock(&(fs->lock));

	jtrans_init(fs, &ts);
	ipos = lseek(fs->fd, 0, SEEK_CUR);
	t = ipos;

	sum = 0;
	for (i = 0; i < count; i++) {
		jtrans_add(&ts, vector[i].iov_base, vector[i].iov_len, t);
		sum += vector[i].iov_len;
		t += vector[i].iov_len;
	}

	rv = jtrans_commit(&ts);

	if (rv >= 0) {
		/* if success, advance the file pointer */
		lseek(fs->fd, sum, SEEK_CUR);
	}

	pthread_mutex_unlock(&(fs->lock));

	jtrans_free(&ts);

	return rv;

}

/* truncate a file - be careful with this */
int jtruncate(struct jfs *fs, off_t length)
{
	int rv;

	/* lock from length to the end of file */
	plockf(fs->fd, F_LOCK, length, 0);
	rv = ftruncate(fs->fd, length);
	plockf(fs->fd, F_ULOCK, length, 0);

	return rv;
}

