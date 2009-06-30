
/*
 * Compatibility functions
 */

/* To get sync_file_range() we need to temporarily define _GNU_SOURCE, which
 * is not the nicest thing, but is not worth defining globally */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#define _REMOVE_GNU_SOURCE
#endif

/* Must be down here because otherwise we might try to #include things twice:
 * once with _GNU_SOURCE and one without it */
#include "compat.h"


/*
 * sync_file_range() support through an internal similar API
 */

#include <fcntl.h>		/* sync_range_submit(), if possible */
#include <sys/types.h>		/* off_t, size_t */

#ifdef SYNC_FILE_RANGE_WRITE
const int have_sync_range = 1;

/** Initiate write-out of the dirty pages in the range */
int sync_range_submit(int fd, off_t offset, size_t nbytes)
{
	/* We don't need SYNC_FILE_RANGE_WAIT_BEFORE because we have exclusive
	 * access to the range (guaranteed by the caller) */
	return sync_file_range(fd, offset, nbytes, SYNC_FILE_RANGE_WRITE);
}

/** Wait for completion of the previously-submitted I/O in the given ranges.
 * Does NOT force the submission of any new I/O. */
int sync_range_wait(int fd, off_t offset, size_t nbytes)
{
	return sync_file_range(fd, offset, nbytes, SYNC_FILE_RANGE_WAIT_BEFORE);
}

#else

#warning "Using fdatasync() instead of sync_file_range()"
const int have_sync_range = 0;

int sync_range_submit(int fd, off_t offset, size_t nbytes)
{
	return -1;
}

int sync_range_wait(int fd, off_t offset, size_t nbytes)
{
	return -1;
}

#endif /* defined SYNC_FILE_RANGE_WRITE */

/* It is no longer needed */
#ifdef _REMOVE_GNU_SOURCE
#undef _GNU_SOURCE
#endif


/* When posix_fadvise() is not available, we just show a message since there
 * is no alternative implementation */
#ifdef LACK_POSIX_FADVISE
#warning "Not using posix_fadvise()"
#endif


/*
 * Support for platforms where clock_gettime() is not available.
 */

#ifdef LACK_CLOCK_GETTIME
#warning "Using gettimeofday() instead of clock_gettime()"

#include <sys/time.h>		/* gettimeofday() */

int clock_gettime(int clk_id, struct timespec *tp)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	tp->tv_sec = tv.tv_sec;
	tp->tv_nsec = tv.tv_usec / 1000.0;

	return 0;
}

#endif /* defined LACK_CLOCK_GETTIME */

#ifdef LACK_FDATASYNC
#warning "Using fsync() instead of fdatasync()"

#include <unistd.h>		/* fsync() */

int fdatasync(int fd)
{
	return fsync(fd);
}
#endif /* defined LACK_FDATASYNC */

