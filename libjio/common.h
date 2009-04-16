
/*
 * Header for internal functions and definitions
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <sys/types.h>	/* for ssize_t and off_t */
#include <stdint.h>	/* for uint*_t */

#include "libjio.h"	/* for struct jfs */
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

/* the main file structure */
struct jfs {
	int fd;			/* main file descriptor */
	char *name;		/* and its name */
	char *jdir;		/* journal directory */
	int jdirfd;		/* journal directory file descriptor */
	int jfd;		/* journal's lock file descriptor */
	unsigned int *jmap;	/* journal's lock file mmap area */
	uint32_t flags;		/* journal flags */
	uint32_t open_flags;	/* open() flags */
	struct jlinger *ltrans;	/* lingered transactions */
	size_t ltrans_len;	/* length of all the lingered transactions */
	pthread_mutex_t ltlock;	/* lingered transactions' lock */
	pthread_mutex_t lock;	/* a soft lock used in some operations */
	struct autosync_cfg *as_cfg; /* autosync config */
};


off_t plockf(int fd, int cmd, off_t offset, off_t len);
ssize_t spread(int fd, void *buf, size_t count, off_t offset);
ssize_t spwrite(int fd, const void *buf, size_t count, off_t offset);
int get_jdir(const char *filename, char *jdir);
void get_jtfile(struct jfs *fs, unsigned int tid, char *jtfile);

int checksum(int fd, size_t len, uint32_t *csum);
uint32_t checksum_map(uint8_t *map, size_t count);

void autosync_check(struct jfs *fs);

#endif

