
/*
 * libjio - A library for Journaled I/O
 * Alberto Bertogli (albertito@blitiri.com.ar)
 */

#ifndef _LIBJIO_H
#define _LIBJIO_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <pthread.h>
#include <unistd.h>

/* Check if we're using Large File Support - otherwise refuse to build.
 * Otherwise, we would allow applications not using LFS to link with the
 * library (which uses LFS) and that's just begging for problems. There should
 * be a portable way for the C library to do some of this for us, but until I
 * find one, this is the best we can do */
#ifndef _LARGEFILE_SOURCE
#error "You must compile your application with Large File Support"
#endif

/* empty declarations, the API does not expose these */
typedef struct jfs jfs_t;
typedef struct jtrans jtrans_t;

struct jfsck_result {
	int total;		/* total transactions files we looked at */
	int invalid;		/* invalid files in the journal directory */
	int in_progress;	/* transactions in progress */
	int broken;		/* transactions broken */
	int corrupt;		/* corrupt transactions */
	int apply_error;	/* errors applying the transaction */
	int reapplied;		/* transactions that were reapplied */
};


/* core functions */
jfs_t *jopen(const char *name, int flags, int mode, int jflags);
jtrans_t *jtrans_init(jfs_t *fs);
int jtrans_add(jtrans_t *ts, const void *buf, size_t count, off_t offset);
ssize_t jtrans_commit(jtrans_t *ts);
ssize_t jtrans_rollback(jtrans_t *ts);
void jtrans_free(jtrans_t *ts);
int jsync(jfs_t *fs);
int jmove_journal(jfs_t *fs, const char *newpath);
int jclose(jfs_t *fs);

/* autosync */
int jfs_autosync_start(jfs_t *fs, time_t max_sec, size_t max_bytes);
int jfs_autosync_stop(jfs_t *fs);


/* journal checker */
int jfsck(const char *name, const char *jdir, struct jfsck_result *res);
int jfsck_cleanup(const char *name, const char *jdir);

/* UNIX API wrappers */
ssize_t jread(jfs_t *fs, void *buf, size_t count);
ssize_t jpread(jfs_t *fs, void *buf, size_t count, off_t offset);
ssize_t jreadv(jfs_t *fs, const struct iovec *vector, int count);
ssize_t jwrite(jfs_t *fs, const void *buf, size_t count);
ssize_t jpwrite(jfs_t *fs, const void *buf, size_t count, off_t offset);
ssize_t jwritev(jfs_t *fs, const struct iovec *vector, int count);
int jtruncate(jfs_t *fs, off_t length);
off_t jlseek(jfs_t *fs, off_t offset, int whence);

/* ANSI C stdio wrappers */
jfs_t *jfopen(const char *path, const char *mode);
int jfclose(jfs_t *stream);
jfs_t *jfreopen(const char *path, const char *mode, jfs_t *stream);
size_t jfread(void *ptr, size_t size, size_t nmemb, jfs_t *stream);
size_t jfwrite(const void *ptr, size_t size, size_t nmemb, jfs_t *stream);
int jfileno(jfs_t *stream);
int jfeof(jfs_t *stream);
void jclearerr(jfs_t *stream);
int jferror(jfs_t *stream);
int jfseek(jfs_t *stream, long offset, int whence);
long jftell(jfs_t *stream);
void jrewind(jfs_t *stream);
FILE *jfsopen(jfs_t *stream, const char *mode);


/* jfs and jtrans constants */
#define J_NOLOCK	1	/* don't lock the file before operating on it */
#define J_NOROLLBACK	2	/* no need to read rollback information */
#define J_LINGER	4	/* use lingering transactions */
#define J_COMMITTED	8	/* mark a transaction as committed */
#define J_ROLLBACKED	16	/* mark a transaction as rollbacked */
#define J_ROLLBACKING	32	/* mark a transaction as rollbacking */
#define J_RDONLY	64	/* mark a file as read-only */

/* jfsck constants (return values) */
#define J_ESUCCESS	0	/* success - shouldn't be used */
#define J_ENOENT	-1	/* no such file */
#define J_ENOJOURNAL	-2	/* no journal associated */
#define J_ENOMEM	-3	/* no enough free memory */


#endif

