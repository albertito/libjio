
/*
 * ANSI C API wrappers
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "libjio.h"
#include "common.h"
#include "trans.h"


/*
 * To avoid completely useless code duplication, this functions rely on the
 * UNIX wrappers from unix.c.
 *
 * The API is not nice, and I wouldn't recommend it for any serious I/O.
 *
 * Note that it is still experimental and it hasn't received too much testing
 * (that's why it's not even documented), so use it at your own risk.
 */


/* fopen() wrapper */
struct jfs *jfopen(const char *path, const char *mode)
{
	int flags;
	int pos_at_the_beginning;
	struct jfs *fs;

	if (strlen(mode) < 1)
		return NULL;

	if (mode[0] == 'r') {
		pos_at_the_beginning = 1;
		if (strlen(mode) > 1 && strchr(mode, '+'))
			flags = O_RDWR;
		else
			flags = O_RDONLY;
	} else if (mode[0] == 'a') {
		if (strlen(mode) > 1 && strchr(mode, '+'))
			pos_at_the_beginning = 1;
		else
			pos_at_the_beginning = 0;
		flags = O_RDWR | O_CREAT | O_APPEND;
	} else if (mode[0] == 'w') {
		pos_at_the_beginning = 1;
		flags = O_RDWR | O_CREAT | O_TRUNC;
	} else {
		return NULL;
	}

	fs = jopen(path, flags, 0666, 0);
	if (fs == NULL)
		return NULL;

	if (pos_at_the_beginning)
		lseek(fs->fd, 0, SEEK_SET);
	else
		lseek(fs->fd, 0, SEEK_END);

	return fs;
}

/* fclose() wrapper */
int jfclose(struct jfs *stream)
{
	int rv;
	rv = jclose(stream);
	free(stream);

	if (rv == 0)
		return 0;
	else
		return EOF;
}

/* freopen() wrapper */
struct jfs *jfreopen(const char *path, const char *mode, struct jfs *stream)
{
	if (stream)
		jfclose(stream);

	stream = jfopen(path, mode);
	return stream;
}

/* fread() wrapper */
size_t jfread(void *ptr, size_t size, size_t nmemb, struct jfs *stream)
{
	int rv;
	rv = jread(stream, ptr, size * nmemb);

	if (rv <= 0)
		return 0;

	return rv / size;
}

/* fwrite() wrapper */
size_t jfwrite(const void *ptr, size_t size, size_t nmemb, struct jfs *stream)
{
	int rv;
	rv = jwrite(stream, ptr, size * nmemb);

	if (rv <= 0)
		return 0;

	return rv / size;
}

/* fileno() wrapper */
int jfileno(struct jfs *stream)
{
	return stream->fd;
}

/* feof() wrapper */
int jfeof(struct jfs *stream)
{
	/* ANSI expects that when an EOF is reached in any operation (like
	 * fread() or fwrite()) some internal flag is set, and this function
	 * can be used to check if it is set or unset.
	 * As we don't do that (it's pointless for this kind of I/O), this
	 * just checks if the file pointer is at the end of the file */

	off_t curpos, endpos;

	pthread_mutex_lock(&(stream->lock));

	curpos = lseek(jfileno(stream), 0, SEEK_CUR);
	endpos = lseek(jfileno(stream), 0, SEEK_END);

	lseek(jfileno(stream), curpos, SEEK_SET);

	pthread_mutex_unlock(&(stream->lock));

	if (curpos >= endpos)
		return 1;
	else
		return 0;
}

/* clearerr() wrapper */
void jclearerr(struct jfs *stream)
{
	/* As we do not carry any kind of error state (like explained in
	 * jfeof()), this function has no effect. */
}

/* ferror() wrapper */
int jferror(struct jfs *stream)
{
	/* The same as the above; however not returning this might have some
	 * side effects on very subtle programs relying on this behaviour */
	return 0;
}

/* fseek() wrapper */
int jfseek(struct jfs *stream, long offset, int whence)
{
	off_t pos;

	pthread_mutex_lock(&(stream->lock));
	pos = lseek(stream->fd, offset, whence);
	pthread_mutex_unlock(&(stream->lock));

	/* fseek returns 0 on success, -1 on error */
	if (pos == -1)
		return 1;

	return 0;
}

/* ftell() wrapper */
long jftell(struct jfs *stream)
{
	/* forced conversion to long to meet the prototype */
	return (long) lseek(stream->fd, 0, SEEK_CUR);
}

/* rewind() wrapper */
void jrewind(struct jfs *stream)
{
	lseek(stream->fd, 0, SEEK_SET);
}

/* convert a struct jfs to a FILE so you can use it with other functions that
 * require a FILE pointer; be aware that you're bypassing the journaling layer
 * and it can cause corruption if you're not extremely careful */
FILE *jfsopen(struct jfs *stream, const char *mode)
{
	return fdopen(stream->fd, mode);
}

