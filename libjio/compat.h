
/* Header to provide fallbacks for compatibility purposes. */

#ifndef _COMPAT_H
#define _COMPAT_H

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

