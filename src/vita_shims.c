/*
 * vita_shims.c
 * Stub implementations of POSIX functions that the Vita libc or
 * third-party libs reference but the VitaSDK does not provide.
 */

#include <errno.h>
#include <sys/stat.h>

/*
 * utimensat — called by libFLAC's metadata_iterators.c (set_file_stats_)
 * to restore file timestamps after writing metadata.  We don't write
 * metadata on the Vita so a no-op is fine.
 */
#ifndef UTIME_NOW
#define UTIME_NOW  (-1)
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT (-2)
#endif

int utimensat(int dirfd, const char *pathname,
              const struct timespec times[2], int flags)
{
    (void)dirfd; (void)pathname; (void)times; (void)flags;
    return 0; /* pretend success */
}
