
/*
 * Common functions
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>		/* htonl() and friends */

#include "libjio.h"
#include "common.h"


/** Like lockf(), but lock always from the given offset */
off_t plockf(int fd, int cmd, off_t offset, off_t len)
{
	struct flock fl;
	int op;

	op = -1;
	fl.l_type = -1;

	if (cmd & _F_READ) {
		fl.l_type = F_RDLCK;
	} else if (cmd & _F_WRITE) {
		fl.l_type = F_WRLCK;
	}

	if (cmd & _F_LOCK) {
		op = F_SETLKW;
	} else if (cmd & _F_TLOCK) {
		op = F_SETLK;
	} else if (cmd & F_UNLOCK) {
		fl.l_type = F_UNLCK;
		op = F_SETLKW; /* not very relevant */
	}

	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = len;

	return fcntl(fd, op, &fl);
}

/** Like pread() but either fails, or return a complete read. If it returns
 * less than count it's because EOF was reached */
ssize_t spread(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t rv;
	size_t c;

	c = 0;

	while (c < count) {
		rv = pread(fd, (char *) buf + c, count - c, offset + c);

		if (rv < 0)
			/* error */
			return rv;
		else if (rv == 0)
			/* got EOF */
			return c;

		/* incomplete read, keep on reading */
		c += rv;
	}

	return count;
}

/** Like spread() but for pwrite() */
ssize_t spwrite(int fd, const void *buf, size_t count, off_t offset)
{
	ssize_t rv;
	size_t c;

	c = 0;

	while (c < count) {
		rv = pwrite(fd, (char *) buf + c, count - c, offset + c);

		if (rv < 0)
			return rv;

		/* incomplete write, keep on writing */
		c += rv;
	}

	return count;
}

/** Like writev() but either fails, or return a complete write.
 * Note that, as opposed to writev() it WILL MODIFY iov, in particular the
 * iov_len fields. */
ssize_t swritev(int fd, struct iovec *iov, int iovcnt)
{
	int i;
	ssize_t rv;
	size_t c, t, total;

	total = 0;
	for (i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;

	c = 0;
	while (c < total) {
		rv = writev(fd, iov, iovcnt);

		if (rv < 0)
			return rv;

		c += rv;

		/* avoid going into the complex calculations for the common
		 * case of writev() doing a complete write */
		if (c == total)
			break;

		/* incomplete write, advance iov and try again */
		t = 0;
		for (i = 0; i < iovcnt; i++) {
			if (t + iov[i].iov_len > rv) {
				iov[i].iov_base = (char *)
					iov[i].iov_base + rv - t;
				iov[i].iov_len -= rv - t;
				break;
			} else {
				t += iov[i].iov_len;
			}
		}

		iovcnt -= i;
		iov = iov + i;
	}

	return c;
}

/** Store in jdir the default journal directory path of the given filename */
int get_jdir(const char *filename, char *jdir)
{
	char *base, *baset;
	char *dir, *dirt;

	baset = strdup(filename);
	if (baset == NULL)
		return 0;
	base = basename(baset);

	dirt = strdup(filename);
	if (dirt == NULL) {
		free(baset);
		return 0;
	}
	dir = dirname(dirt);

	snprintf(jdir, PATH_MAX, "%s/.%s.jio", dir, base);

	free(baset);
	free(dirt);

	return 1;
}

/** Build the filename of a given transaction. Assumes jtfile can hold at
 * least PATH_MAX bytes. */
void get_jtfile(struct jfs *fs, unsigned int tid, char *jtfile)
{
	snprintf(jtfile, PATH_MAX, "%s/%u", fs->jdir, tid);
}


/* The ntohll() and htonll() functions are not standard, so we define them
 * using an UGLY trick because there is no standard way to check for
 * endianness at runtime. */

/** Convert a 64-bit value between network byte order and host byte order. */
uint64_t ntohll(uint64_t x)
{
	static int endianness = 0;

	/* determine the endianness by checking how htonl() behaves; use -1
	 * for little endian and 1 for big endian */
	if (endianness == 0) {
		if (htonl(1) == 1)
			endianness = 1;
		else
			endianness = -1;
	}

	if (endianness == 1) {
		/* big endian */
		return x;
	}

	/* little endian */
	return ( ntohl( (x >> 32) & 0xFFFFFFFF ) | \
			( (uint64_t) ntohl(x & 0xFFFFFFFF) ) << 32 );
}

/** Convert a 64-bit value between host byte order and network byte order. */
uint64_t htonll(uint64_t x)
{
	static int endianness = 0;

	/* determine the endianness by checking how htonl() behaves; use -1
	 * for little endian and 1 for big endian */
	if (endianness == 0) {
		if (htonl(1) == 1)
			endianness = 1;
		else
			endianness = -1;
	}

	if (endianness == 1) {
		/* big endian */
		return x;
	}

	/* little endian */
	return ( htonl( (x >> 32) & 0xFFFFFFFF ) | \
			( (uint64_t) htonl(x & 0xFFFFFFFF) ) << 32 );
}


