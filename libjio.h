
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertogli@telpin.com.ar)
 */

#ifndef _LIBJIO_H
#define _LIBJIO_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif


/* logical structures */
struct jfs {
	int fd;			/* main file descriptor */
	char *name;		/* and its name */
	int jfd;		/* journal's lock file descriptor */
	int flags;		/* journal mode options used in jopen() */
	pthread_mutex_t lock;	/* a soft lock used in some operations */
};

struct jtrans {
	struct jfs *fs;		/* journal file structure to operate on */
	char *name;		/* name of the transaction file */
	int id;			/* transaction id */
	int flags;		/* misc flags */
	void *buf;		/* buffer */
	size_t len;		/* buffer lenght */
	off_t offset;		/* file offset to operate on */
	void *udata;		/* user-supplied data */
	size_t ulen;		/* udata lenght */
	void *pdata;		/* previous data, for rollback */
	size_t plen;		/* pdata lenght */
};

struct jfsck_result {
	int total;		/* total transactions files we looked at */
	int invalid;		/* invalid files in the journal directory */
	int in_progress;	/* transactions in progress */
	int broken_head;	/* transactions broken (header missing) */
	int broken_body;	/* transactions broken (body missing) */
	int load_error;		/* errors loading the transaction */
	int apply_error;	/* errors applying the transaction */
	int rollbacked;		/* transactions that were rollbacked */
};

/* on-disk structure */
struct disk_trans {
	
	/* header (fixed lenght, defined below) */
	uint32_t id;		/* id */
	uint32_t flags;		/* flags about this transaction */
	uint32_t len;		/* data lenght */
	uint32_t plen;		/* previous data lenght */
	uint32_t ulen;		/* user-supplied information lenght */
	uint64_t offset;	/* offset relative to the BOF */
	
	/* payload (variable lenght) */
	char *udata;		/* user-supplied data */
	char *prevdata;		/* previous data for rollback */
};


/* basic operations */
int jopen(struct jfs *fs, const char *name, int flags, int mode, int jflags);
ssize_t jread(struct jfs *fs, void *buf, size_t count);
ssize_t jpread(struct jfs *fs, void *buf, size_t count, off_t offset);
ssize_t jreadv(struct jfs *fs, struct iovec *vector, int count);
ssize_t jwrite(struct jfs *fs, void *buf, size_t count);
ssize_t jpwrite(struct jfs *fs, void *buf, size_t count, off_t offset);
ssize_t jwritev(struct jfs *fs, struct iovec *vector, int count);
int jtruncate(struct jfs *fs, off_t lenght);
int jclose(struct jfs *fs);

/* transaction operations */
void jtrans_init(struct jfs *fs, struct jtrans *ts);
int jtrans_commit(struct jtrans *ts);
int jtrans_rollback(struct jtrans *ts);
void jtrans_free(struct jtrans *ts);

/* journal checker */
int jfsck(char *name, struct jfsck_result *res);


/* jfs constants */
#define J_NOLOCK	1	/* don't lock the file before operating on it */

/* jtrans constants */
#define J_COMMITED	1	/* mark a transaction as commited */
#define J_ROLLBACKED	2	/* mark a transaction as rollbacked */

/* disk_trans constants */
#define J_DISKTFIXSIZE	 28	/* lenght of disk_trans' header */ 

/* jfsck constants (return values) */
#define J_ESUCCESS	0	/* success - shouldn't be used */
#define J_ENOENT	1	/* no such file */
#define J_ENOJOURNAL	2	/* no journal associated */
#define J_ENOMEM	3	/* no enough free memory */


#ifdef __cplusplus
} /* from extern "C" avobe */
#endif

#endif

