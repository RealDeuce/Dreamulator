// POSIX unistd.h shim for Windows/mingw-w64
#ifndef COMPAT_UNISTD_H
#define COMPAT_UNISTD_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <io.h>
#include <process.h>
#include <stdint.h>
#include <fcntl.h>
#include <basetsd.h>

#ifdef _MSC_VER
typedef SSIZE_T ssize_t;
typedef long off_t;
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

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

int  compat_is_socket(SOCKET s);
int  fcntl(SOCKET fd, int cmd, ...);

static inline int compat_close(SOCKET fd)
{
    if (compat_is_socket(fd))
        return closesocket(fd);
    return _close((int)fd);
}

static inline ssize_t compat_read(SOCKET fd, void *buf, size_t count)
{
    if (compat_is_socket(fd))
        return recv(fd, (char *)buf, (int)count, 0);
    return _read((int)fd, buf, (unsigned int)count);
}

static inline ssize_t compat_write(SOCKET fd, const void *buf, size_t count)
{
    if (compat_is_socket(fd))
        return send(fd, (const char *)buf, (int)count, 0);
    return _write((int)fd, buf, (unsigned int)count);
}

#define close   compat_close
#define read    compat_read
#define write   compat_write

static inline int ftruncate(int fd, off_t length)
{
    return _chsize(fd, (long)length);
}

static inline unsigned int sleep(unsigned int seconds)
{
    Sleep(seconds * 1000);
    return 0;
}

static inline void usleep(unsigned long usec)
{
    Sleep((DWORD)(usec / 1000));
}

#ifdef __cplusplus
}
#endif

#endif
