
/* Header to provide fallbacks for compatibility purposes */

#ifndef _COMPAT_H
#define _COMPAT_H

#include <sys/types.h>		/* off_t, size_t */


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
#define LACK_POSIX_FADVISE 1
#define posix_fadvise(fd, offset, len, advise)
#endif


/* fdatasync() is super standard, but some BSDs (FreeBSD, DragonflyBSD at
 * least) do not have it. Since there is no reliable way to test for it, we
 * have to resort to OS detection. */
#if ! ( (defined __linux__) || (defined (__SVR4) && defined (__sun)) )
#define LACK_FDATASYNC 1
int fdatasync(int fd);
#endif


/* Some platforms do not have clock_gettime() so we define an alternative for
 * them, in compat.c. We should check for _POSIX_TIMERS, but some platforms do
 * not have it yet they do have clock_gettime() (DragonflyBSD), so we just
 * check for CLOCK_REALTIME. */
#include <time.h>
#ifndef CLOCK_REALTIME
#define LACK_CLOCK_GETTIME 1
#define CLOCK_REALTIME 0
#endif

#endif

