
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
 *
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

#include "common.h"


/* like lockf, but lock always from the beginning of the file */
off_t plockf(int fd, int cmd, off_t offset, off_t len)
{
	struct flock fl;
	int op;

	if (cmd == F_LOCK) {
		fl.l_type = F_WRLCK;
		op = F_SETLKW;
	} else if (cmd == F_ULOCK) {
		fl.l_type = F_UNLCK;
		op = F_SETLKW;
	} else if (cmd == F_TLOCK) {
		fl.l_type = F_WRLCK;
		op = F_SETLK;
	} else
		return 0;

	fl.l_whence = SEEK_SET;
	fl.l_start = offset;
	fl.l_len = len;

	return fcntl(fd, op, &fl);
}

/* like pread but either fails, or return a complete read; if we return less
 * than count is because EOF was reached */
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

/* like spread() but for pwrite() */
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

/* build the journal directory name out of the filename */
int get_jdir(const char *filename, char *jdir)
{
	char *base, *baset;
	char *dir, *dirt;

	baset = strdup(filename);
	if (baset == NULL)
		return 0;
	base = basename(baset);

	dirt = strdup(filename);
	if (dirt == NULL)
		return 0;
	dir = dirname(dirt);

	snprintf(jdir, PATH_MAX, "%s/.%s.jio", dir, base);

	free(baset);
	free(dirt);

	return 1;
}

/* build the filename of a given transaction */
int get_jtfile(const char *filename, int tid, char *jtfile)
{
	char *base, *baset;
	char *dir, *dirt;

	baset = strdup(filename);
	if (baset == NULL)
		return 0;
	base = basename(baset);

	dirt = strdup(filename);
	if (dirt == NULL)
		return 0;
	dir = dirname(dirt);

	snprintf(jtfile, PATH_MAX, "%s/.%s.jio/%d", dir, base, tid);

	free(baset);
	free(dirt);

	return 1;
}


