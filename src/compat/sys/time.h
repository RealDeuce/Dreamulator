// POSIX sys/time.h shim for Windows
#ifndef COMPAT_SYS_TIME_H
#define COMPAT_SYS_TIME_H

#ifdef _MSC_VER

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

static inline int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) |
                            ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    tv->tv_sec  = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10);
    return 0;
}

#else
#include_next <sys/time.h>
#endif

#endif
