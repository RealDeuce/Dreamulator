// POSIX unistd.h shim for Windows/mingw-w64
#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H

#include <io.h>
#include <process.h>
#include <stdint.h>
#include <fcntl.h>

#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0800
#endif
#ifndef O_NOCTTY
#define O_NOCTTY   0x0100
#endif
#ifndef F_GETFL
#define F_GETFL    3
#endif
#ifndef F_SETFL
#define F_SETFL    4
#endif


#ifdef __cplusplus
extern "C" {
#endif

static inline int compat_close(int fd);
static inline ssize_t compat_read(int fd, void *buf, size_t count);
static inline ssize_t compat_write(int fd, const void *buf, size_t count);

int  compat_is_socket(int fd);
int  fcntl(int fd, int cmd, ...);

#define close   compat_close
#define read    compat_read
#define write   compat_write

static inline int ftruncate(int fd, off_t length)
{
    return _chsize(fd, (long)length);
}

static inline unsigned int sleep(unsigned int seconds)
{
    _sleep(seconds * 1000);
    return 0;
}

static inline void usleep(unsigned long usec)
{
    _sleep((unsigned long)(usec / 1000));
}

#ifdef __cplusplus
}
#endif

#include <winsock2.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int compat_close(int fd)
{
    if (compat_is_socket(fd))
        return closesocket((SOCKET)(intptr_t)fd);
    return _close(fd);
}

static inline ssize_t compat_read(int fd, void *buf, size_t count)
{
    if (compat_is_socket(fd))
        return recv((SOCKET)(intptr_t)fd, (char *)buf, (int)count, 0);
    return _read(fd, buf, (unsigned int)count);
}

static inline ssize_t compat_write(int fd, const void *buf, size_t count)
{
    if (compat_is_socket(fd))
        return send((SOCKET)(intptr_t)fd, (const char *)buf, (int)count, 0);
    return _write(fd, buf, (unsigned int)count);
}

#ifdef __cplusplus
}
#endif

#endif
