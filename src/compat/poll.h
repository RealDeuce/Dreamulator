// POSIX poll.h shim for Windows/mingw-w64
#ifndef COMPAT_POLL_H
#define COMPAT_POLL_H

#include <winsock2.h>

#ifndef POLLIN
#define POLLIN   0x0100
#define POLLOUT  0x0010
#define POLLERR  0x0001
#define POLLHUP  0x0002
#define POLLNVAL 0x0004
#endif

struct pollfd {
    int   fd;
    short events;
    short revents;
};

static inline int poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
    WSAPOLLFD wfds[16];
    if (nfds > 16) nfds = 16;
    for (unsigned long i = 0; i < nfds; i++) {
        wfds[i].fd = (SOCKET)(intptr_t)fds[i].fd;
        wfds[i].events = fds[i].events;
        wfds[i].revents = 0;
    }
    int ret = WSAPoll(wfds, nfds, timeout);
    for (unsigned long i = 0; i < nfds; i++)
        fds[i].revents = wfds[i].revents;
    return ret;
}

#endif
