
/*
 * libjio C preloader
 * Alberto Bertogli (albertogli@telpin.com.ar)
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


/* file descriptor table, to translate fds to jfs */
struct fd_entry {
	int fd;
	struct jfs *fs;
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
	if (fd < 0 || fd >= MAXFD) {
		printd("locking out of bounds fd %d\n", fd);
		return 0;
	}
	//printd("L %d\n", fd);
	pthread_mutex_lock(&(fd_table[fd].lock));
	//printd("OK %d\n", fd);
	return 1;
}

static inline int fd_unlock(int fd)
{
	if (fd < 0 || fd >= MAXFD) {
		printd("unlocking out of bounds fd %d\n", fd);
		return 0;
	}
	//printd("U %d\n", fd);
	pthread_mutex_unlock(&(fd_table[fd].lock));
	//printd("OK %d\n", fd);
	return 1;
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

	printd("done\n");
	return 1;
}

/*
 * wrappers
 */

int open(const char *pathname, int flags, ...)
{
	int r, fd;
	struct jfs *fs;
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
	fs = malloc(sizeof(struct jfs));
	if (fs == NULL) {
		rec_dec();
		return -1;
	}
	fd = jopen(fs, pathname, flags, mode, 0);
	if (fd >= MAXFD) {
		printd("too many open fds: %d\n", fd);
		jclose(fs);
		free(fs);
		rec_dec();
		return -1;
	}
	rec_dec();

	if (fd < 0) {
		printd("return %d\n", fd);
		return fd;
	}

	fd_lock(fd);
	fd_table[fd].fd = fd;
	fd_table[fd].fs = fs;
	fd_unlock(fd);

	printd("return %d\n", fd);
	return fd;
}

/* exact copy of open(), but call c_open64 instead of c_open */
int open64(const char *pathname, int flags, ...)
{
	int r, fd;
	struct jfs *fs;
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
	fs = malloc(sizeof(struct jfs));
	if (fs == NULL) {
		rec_dec();
		return -1;
	}
	fd = jopen(fs, pathname, flags, mode, 0);
	if (fd >= MAXFD) {
		printd("too many open fds: %d\n", fd);
		jclose(fs);
		free(fs);
		rec_dec();
		return -1;
	}
	rec_dec();

	if (fd < 0) {
		printd("return %d\n", fd);
		return fd;
	}

	fd_lock(fd);
	fd_table[fd].fd = fd;
	fd_table[fd].fs = fs;
	fd_unlock(fd);

	printd("return %d\n", fd);
	return fd;
}


int close(int fd)
{
	int r;
	struct jfs *fs;

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

	rec_inc();
	r = jclose(fs);
	if (fd_table[fd].fs != NULL) {
		free(fd_table[fd].fs);
		fd_table[fd].fd = -1;
		fd_table[fd].fs = NULL;
	}
	rec_dec();
	fd_unlock(fd);

	printd("return %d\n", r);
	return r;
}


/* the rest of the functions are automagically generated from the following
 * macro. The ugliest. I'm so proud. */

#define mkwrapper(rtype, name, DEF, INVR, INVM)			\
	rtype name DEF						\
	{ 							\
		rtype r;					\
		struct jfs *fs;					\
								\
		if (called) {					\
			printd("orig \n");			\
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

