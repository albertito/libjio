
/*
 * Compatibility functions
 */

/* To get sync_file_range() we need to temporarily define _GNU_SOURCE, which
 * is not the nicest thing, but is not worth defining globally */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#define _REMOVE_GNU_SOURCE
#endif

#include <fcntl.h>		/* sync_range_submit(), if possible */
#include "compat.h"


/*
 * sync_file_range() support through an internal similar API
 */

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

#warning "No sync_file_range()"
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

#ifdef _REMOVE_GNU_SOURCE
#undef _GNU_SOURCE
#endif

