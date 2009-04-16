
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
	int rv, c;

	c = 0;

	while (c < count) {
		rv = pread(fd, (char *) buf + c, count - c, offset + c);

		if (rv == count)
			/* we're done */
			return count;
		else if (rv < 0)
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
	int rv, c;

	c = 0;

	while (c < count) {
		rv = pwrite(fd, (char *) buf + c, count - c, offset + c);

		if (rv == count)
			/* we're done */
			return count;
		else if (rv <= 0)
			/* error/nothing was written */
			return rv;

		/* incomplete write, keep on writing */
		c += rv;
	}

	return count;
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


