
/*
 * libjio C preloader
 * Alberto Bertogli (albertito@blitiri.com.ar)
 *
 * This generates a shared object that, when prelinked, can be used to make an
 * existing application to use libjio for UNIX I/O.
 * It's not nice or pretty, and does some nasty tricks to work both with and
 * without LFS. I don't think it builds or works without glibc.
 */


#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <dlfcn.h>

/* we don't build this with LFS, however, it's essential that the proper
 * environment is set for libjio's loading; otherwise we would mess the ABI
 * up */
typedef long long off64_t;
#define _FILE_OFFSET_BITS 64
#define off_t off64_t
#include <libjio.h>
#undef off_t
#undef _FILE_OFFSET_BITS


/* maximum number of simultaneous open file descriptors we support */
#define MAXFD (4096 * 2)

/* recursion counter, per-thread */
static int __thread called = 0;


/* C library functions, filled via the dynamic loader */
static void *libc;

static int (*c_open)(const char *pathname, int flags, mode_t mode);
static int (*c_open64)(const char *pathname, int flags, mode_t mode);
static int (*c_close)(int fd);
static int (*c_unlink)(const char *pathname);
static ssize_t (*c_read)(int fd, void *buf, size_t count);
static ssize_t (*c_pread)(int fd, void *buf, size_t count, off_t offset);
static ssize_t (*c_pread64)(int fd, void *buf, size_t count, off64_t offset);
static ssize_t (*c_readv)(int fd, const struct iovec *vector, int count);
static ssize_t (*c_write)(int fd, const void *buf, size_t count);
static ssize_t (*c_pwrite)(int fd, const void *buf, size_t count, off_t offset);
static ssize_t (*c_pwrite64)(int fd, const void *buf, size_t count, off64_t offset);
static ssize_t (*c_writev)(int fd, const struct iovec *vector, int count);
static int (*c_ftruncate)(int fd, off_t length);
static int (*c_ftruncate64)(int fd, off64_t length);
static off_t (*c_lseek)(int fd, off_t offset, int whence);
static off64_t (*c_lseek64)(int fd, off64_t offset, int whence);
static int (*c_fsync)(int fd);
static int (*c_dup)(int oldfd);
static int (*c_dup2)(int oldfd, int newfd);


/* file descriptor table, to translate fds to jfs */
struct fd_entry {
	int fd;
	unsigned int *refcount;
	jfs_t *fs;
	pthread_mutex_t lock;
};
static struct fd_entry fd_table[MAXFD];

/* useful macros, mostly for debugging purposes */
#if 1
	#define rec_inc() do { called++; } while(0)
	#define rec_dec() do { called--; } while(0)
	#define printd(...) do { } while(0)

#else
	/* debug variants */

	#define rec_inc()				\
		do {					\
			called++;			\
			fprintf(stderr, "I: %d\n", called); \
			fflush(stderr);			\
		} while (0)

	#define rec_dec()				\
		do {					\
			called--;			\
			fprintf(stderr, "D: %d\n", called); \
			fflush(stderr);			\
		} while (0)

	#define printd(...)				\
		do {					\
			if (called)			\
				fprintf(stderr, "\t");	\
			called++;			\
			fprintf(stderr, "%5.5d ", getpid()); \
			fprintf(stderr, "%s(): ", __FUNCTION__ ); \
			fprintf(stderr, __VA_ARGS__);	\
			fflush(stderr);			\
			called--;			\
		} while(0)
#endif


/* functions used to lock fds from the table; they do boundary checks so we
 * catch out of bounds accesses */
static inline int fd_lock(int fd)
{
	int r;

	if (fd < 0 || fd >= MAXFD) {
		printd("locking out of bounds fd %d\n", fd);
		return 0;
	}
	//printd("L %d\n", fd);
	r = pthread_mutex_lock(&(fd_table[fd].lock));
	//printd("OK %d\n", fd);
	return !r;
}

static inline int fd_unlock(int fd)
{
	int r;

	if (fd < 0 || fd >= MAXFD) {
		printd("unlocking out of bounds fd %d\n", fd);
		return 0;
	}
	//printd("U %d\n", fd);
	r = pthread_mutex_unlock(&(fd_table[fd].lock));
	//printd("OK %d\n", fd);
	return !r;
}


/*
 * library intialization
 */

static int __attribute__((constructor)) init(void)
{
	int i;
	pthread_mutexattr_t attr;

	printd("starting\n");

	/* initialize fd_table */
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
	for (i = 0; i < MAXFD; i++) {
		fd_table[i].fd = -1;
		fd_table[i].fs = NULL;
		pthread_mutex_init(&(fd_table[i].lock), &attr);
	}
	pthread_mutexattr_destroy(&attr);

	/* dynamically load the C library */
	libc = dlopen("libc.so.6", RTLD_NOW);
	if (libc == NULL) {
		printd("Error loading libc: %s\n", dlerror());
		return 0;
	}

	/* load symbols from the C library */
	#define libc_load(F) c_##F = dlsym(libc, #F)
	libc_load(open);
	libc_load(open64);
	libc_load(close);
	libc_load(unlink);
	libc_load(read);
	libc_load(pread);
	libc_load(pread64);
	libc_load(readv);
	libc_load(write);
	libc_load(pwrite);
	libc_load(pwrite64);
	libc_load(writev);
	libc_load(ftruncate);
	libc_load(ftruncate64);
	libc_load(lseek);
	libc_load(lseek64);
	libc_load(fsync);
	libc_load(dup);
	libc_load(dup2);

	printd("done\n");
	return 1;
}

/*
 * wrappers
 */

int open(const char *pathname, int flags, ...)
{
	int r, fd;
	jfs_t *fs;
	mode_t mode;
	struct stat st;
	va_list l;

	if (flags & O_CREAT) {
		va_start(l, flags);
		mode = va_arg(l, mode_t);
		va_end(l);
	} else {
		/* set it to 0, it's ignored anyway */
		mode = 0;
	}

	if (called) {
		printd("orig (r)\n");
		return (*c_open)(pathname, flags, mode);
	}
	printd("libjio\n");

	/* skip special files */
	r = stat(pathname, &st);
	if (r == 0 && ( S_ISDIR(st.st_mode) \
			|| S_ISCHR(st.st_mode) \
			|| S_ISFIFO(st.st_mode) ) ) {
		printd("orig (s)\n");
		return (*c_open)(pathname, flags, mode);
	}

	/* skip /proc and /sys (not /dev, the problematic files are taken care
	 * of with the stat test above */
	/* FIXME: this breaks with relative paths */
	if ( (strncmp("/proc", pathname, 5) == 0) ||
			(strncmp("/sys", pathname, 4) == 0) ) {
		printd("orig (f)\n");
		return (*c_open)(pathname, flags, mode);
	}

	rec_inc();
	fs = jopen(pathname, flags, mode, 0);
	if (fs == NULL) {
		rec_dec();
		return -1;
	}
	rec_dec();

	fd = jfileno(fs);

	fd_lock(fd);
	fd_table[fd].fd = fd;
	fd_table[fd].refcount = malloc(sizeof(unsigned int));
	*fd_table[fd].refcount = 1;
	fd_table[fd].fs = fs;
	fd_unlock(fd);

	printd("return %d\n", fd);
	return fd;
}

/* exact copy of open(), but call c_open64 instead of c_open */
int open64(const char *pathname, int flags, ...)
{
	int r, fd;
	jfs_t *fs;
	mode_t mode;
	struct stat st;
	va_list l;

	if (flags & O_CREAT) {
		va_start(l, flags);
		mode = va_arg(l, mode_t);
		va_end(l);
	} else {
		/* set it to 0, it's ignored anyway */
		mode = 0;
	}

	if (called) {
		printd("orig (r)\n");
		return (*c_open64)(pathname, flags, mode);
	}
	printd("libjio\n");

	/* skip special files */
	r = stat(pathname, &st);
	if (r == 0 && ( S_ISDIR(st.st_mode) \
			|| S_ISCHR(st.st_mode) \
			|| S_ISFIFO(st.st_mode) ) ) {
		printd("orig (s)\n");
		return (*c_open64)(pathname, flags, mode);
	}

	/* skip /proc and /sys (not /dev, the problematic files are taken care
	 * of with the stat test above */
	/* FIXME: this breaks with relative paths */
	if ( (strncmp("/proc", pathname, 5) == 0) ||
			(strncmp("/sys", pathname, 4) == 0) ) {
		printd("orig (f)\n");
		return (*c_open64)(pathname, flags, mode);
	}

	rec_inc();
	fs = jopen(pathname, flags, mode, 0);
	if (fs == NULL) {
		rec_dec();
		return -1;
	}
	rec_dec();

	fd = jfileno(fs);

	fd_lock(fd);
	fd_table[fd].fd = fd;
	fd_table[fd].refcount = malloc(sizeof(unsigned int));
	*fd_table[fd].refcount = 1;
	fd_table[fd].fs = fs;
	fd_unlock(fd);

	printd("return %d\n", fd);
	return fd;
}

/* close() is split in two functions: unlocked_close() that performs the real
 * actual close and cleanup, and close() which takes care of the locking and
 * calls unlocked_close(); this is because in dup*() we need to close with
 * locks already held to avoid races. */
int unlocked_close(int fd)
{
	int r;

	if (*fd_table[fd].refcount > 1) {
		/* we still have references, don't really close */
		printd("not closing, refcount: %d\n", *fd_table[fd].refcount);
		(*fd_table[fd].refcount)--;
		fd_table[fd].fd = -1;
		fd_table[fd].refcount = NULL;
		fd_table[fd].fs = NULL;
		return 0;
	}

	rec_inc();
	r = jclose(fd_table[fd].fs);
	rec_dec();

	fd_table[fd].fd = -1;
	free(fd_table[fd].refcount);
	fd_table[fd].refcount = NULL;
	fd_table[fd].fs = NULL;

	return r;
}

int close(int fd)
{
	int r;
	jfs_t *fs;

	if (called) {
		printd("orig\n");
		return (*c_close)(fd);
	}

	if (!fd_lock(fd)) {
		printd("out of bounds fd: %d\n", fd);
		return -1;
	}
	fs = fd_table[fd].fs;
	if (fs == NULL) {
		printd("NULL fs, fd %d\n", fd);
		fd_unlock(fd);
		return (*c_close)(fd);
	}
	printd("libjio\n");

	r = unlocked_close(fd);
	fd_unlock(fd);

	printd("return %d\n", r);
	return r;
}


int unlink(const char *pathname)
{
	int r;
	struct jfsck_result res;

	if (called) {
		printd("orig\n");
		return (*c_unlink)(pathname);
	}

	printd("libjio\n");

	rec_inc();
	r = jfsck(pathname, NULL, &res, 0);
	rec_dec();

	r = (*c_unlink)(pathname);
	printd("return %d\n", r);

	return r;
}

int dup(int oldfd)
{
	int r;

	if (called) {
		printd("orig\n");
		return (*c_dup)(oldfd);
	}

	if (fd_table[oldfd].fs == NULL) {
		printd("NULL fs, fd %d\n", oldfd);
		fd_unlock(oldfd);
		return (*c_dup)(oldfd);
	}

	if (!fd_lock(oldfd)) {
		printd("out of bounds fd: %d\n", oldfd);
		return -1;
	}

	printd("libjio\n");

	rec_inc();
	r = (*c_dup)(oldfd);
	rec_dec();

	if (r >= 0) {
		fd_lock(r);
		fd_table[r].fd = r;
		fd_table[r].refcount = fd_table[oldfd].refcount;
		(*fd_table[r].refcount)++;
		fd_table[r].fs = fd_table[oldfd].fs;
		fd_unlock(r);
	}

	fd_unlock(oldfd);
	printd("return %d\n", r);
	return r;
}

int dup2(int oldfd, int newfd)
{
	int r;

	if (called) {
		printd("orig\n");
		return (*c_dup2)(oldfd, newfd);
	}

	if (!fd_lock(oldfd)) {
		printd("out of bounds fd: %d\n", oldfd);
		return -1;
	}

	if (fd_table[oldfd].fs == NULL) {
		printd("NULL fs, fd %d\n", oldfd);
		fd_unlock(oldfd);
		return (*c_dup2)(oldfd, newfd);
	}

	printd("libjio\n");

	rec_inc();
	r = (*c_dup2)(oldfd, newfd);
	rec_dec();

	if (r >= 0) {
		fd_lock(newfd);
		if (fd_table[newfd].fs != NULL) {
			unlocked_close(newfd);
		}
		fd_table[newfd].fd = newfd;
		fd_table[newfd].refcount = fd_table[oldfd].refcount;
		(*fd_table[newfd].refcount)++;
		fd_table[newfd].fs = fd_table[oldfd].fs;
		fd_unlock(newfd);
	}

	fd_unlock(oldfd);
	printd("return %d\n", r);
	return r;
}


/* the rest of the functions are automagically generated from the following
 * macro. The ugliest. I'm so proud. */

#define mkwrapper(rtype, name, DEF, INVR, INVM)			\
	rtype name DEF						\
	{ 							\
		rtype r;					\
		jfs_t *fs;					\
								\
		if (called) {					\
			printd("orig\n");			\
			return (*c_##name) INVR;		\
		}						\
								\
		if (!fd_lock(fd)) {				\
			printd("out of bounds fd: %d\n", fd);	\
			return -1;				\
		}						\
		fs = fd_table[fd].fs;				\
		if (fs == NULL) {				\
			printd("(): NULL fs, fd %d\n", fd); 	\
			fd_unlock(fd);				\
			return (*c_##name) INVR;		\
		}						\
		printd("libjio\n");				\
								\
		rec_inc();					\
		r = j##name INVM;				\
		rec_dec();					\
		fd_unlock(fd);					\
								\
		printd("return %lld\n", (long long) r); 	\
		return r;					\
	}


/* 32-bit versions */
mkwrapper(ssize_t, read, (int fd, void *buf, size_t count),
		(fd, buf, count), (fs, buf, count) );

mkwrapper(ssize_t, pread, (int fd, void *buf, size_t count, off_t offset),
		(fd, buf, count, offset), (fs, buf, count, offset) );

mkwrapper(ssize_t, readv, (int fd, const struct iovec *vector, int count),
		(fd, vector, count), (fs, vector, count) );

mkwrapper(ssize_t, write, (int fd, const void *buf, size_t count),
		(fd, buf, count), (fs, buf, count) );

mkwrapper(ssize_t, pwrite,
		(int fd, const void *buf, size_t count, off_t offset),
		(fd, buf, count, offset), (fs, buf, count, offset) );

mkwrapper(ssize_t, writev, (int fd, const struct iovec *vector, int count),
		(fd, vector, count), (fs, vector, count) );

mkwrapper(off_t, lseek, (int fd, off_t offset, int whence),
		(fd, offset, whence), (fs, offset, whence) );


/* libjio defines jtruncate and jsync, not jftruncate and jfsync, which breaks
 * the macro; so we add a nice #define to unbreak it */
#define jftruncate jtruncate
mkwrapper(int, ftruncate, (int fd, off_t length),
		(fd, length), (fs, length) );

#define jfsync jsync
mkwrapper(int, fsync, (int fd), (fd), (fs) );


/* 64-bit versions */
#define jpread64 jpread
mkwrapper(ssize_t, pread64, (int fd, void *buf, size_t count, off64_t offset),
		(fd, buf, count, offset), (fs, buf, count, offset) );

#define jpwrite64 jpwrite
mkwrapper(ssize_t, pwrite64,
		(int fd, const void *buf, size_t count, off64_t offset),
		(fd, buf, count, offset), (fs, buf, count, offset) );

#define jlseek64 jlseek
mkwrapper(off64_t, lseek64, (int fd, off64_t offset, int whence),
		(fd, offset, whence), (fs, offset, whence) );

#define jftruncate64 jtruncate
mkwrapper(int, ftruncate64, (int fd, off64_t length),
		(fd, length), (fs, length) );

