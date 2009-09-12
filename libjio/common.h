
/*
 * Header for internal functions and definitions
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>	/* for ssize_t and off_t */
#include <stdint.h>	/* for uint*_t */
#include <sys/uio.h>	/* for struct iovec */
#include <pthread.h>	/* pthread_mutex_t */

#include "fiu-local.h"	/* for fault injection functions */

#define _F_READ		0x00001
#define _F_WRITE	0x00010
#define _F_LOCK		0x00100
#define _F_TLOCK	0x01000
#define _F_ULOCK	0x10000

#define F_LOCKR		(_F_LOCK | _F_READ)
#define F_LOCKW		(_F_LOCK | _F_WRITE)
#define F_TLOCKR	(_F_TLOCK | _F_READ)
#define F_TLOCKW	(_F_TLOCK | _F_WRITE)
#define F_UNLOCK	(_F_ULOCK)

#define MAX_TSIZE	(SSIZE_MAX)

/** The main file structure */
struct jfs {
	/** Real file fd */
	int fd;

	/** Real file path */
	char *name;

	/** Journal directory path */
	char *jdir;

	/** Journal directory file descriptor */
	int jdirfd;

	/** Journal's lock file descriptor */
	int jfd;

	/** Journal's lock file mmap */
	unsigned int *jmap;

	/** Journal flags */
	uint32_t flags;

	/** Flags passed to the real open() */
	uint32_t open_flags;

	/** Lingering transactions (linked list) */
	struct jlinger *ltrans;

	/** Length of all the lingered transactions */
	size_t ltrans_len;

	/** Lingering transactions' lock */
	pthread_mutex_t ltlock;

	/** A soft lock used in some operations */
	pthread_mutex_t lock;

	/** Autosync config */
	struct autosync_cfg *as_cfg;
};


off_t plockf(int fd, int cmd, off_t offset, off_t len);
ssize_t spread(int fd, void *buf, size_t count, off_t offset);
ssize_t spwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t swritev(int fd, struct iovec *iov, int iovcnt);
int get_jdir(const char *filename, char *jdir);
void get_jtfile(struct jfs *fs, unsigned int tid, char *jtfile);
uint64_t ntohll(uint64_t x);
uint64_t htonll(uint64_t x);

uint32_t checksum_buf(uint32_t sum, const unsigned char *buf, size_t count);

void autosync_check(struct jfs *fs);

#endif

