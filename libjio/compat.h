
/* Header to provide fallbacks for compatibility purposes */

#ifndef _COMPAT_H
#define _COMPAT_H

/* sync_file_range() is linux-specific, so we provide an internal similar API,
 * with a constant to be able to check for its presence; the implementation is
 * in compat.c */
extern const int have_sync_range;
int sync_range_submit(int fd, off_t offset, size_t nbytes);
int sync_range_wait(int fd, off_t offset, size_t nbytes);


/* posix_fadvise() was introduced in SUSv3. Because it's the only SUSv3
 * function we rely on so far (everything else is SUSv2), we define a void
 * fallback for systems that do not implement it.
 *
 * The check is based on POSIX_FADV_WILLNEED being defined, which is not very
 * nice, but it's simple, it works and should be reliable. */
#include <fcntl.h>
#ifndef POSIX_FADV_WILLNEED
#define posix_fadvise(fd, offset, len, advise)
#endif

#endif

