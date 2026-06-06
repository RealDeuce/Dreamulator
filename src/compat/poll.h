// POSIX poll.h shim for Windows
#ifndef COMPAT_POLL_H
#define COMPAT_POLL_H

#include <winsock2.h>

#ifndef POLLIN
#define POLLIN  0x0100
#define POLLOUT 0x0010
#endif

struct pollfd {
    int   fd;
    short events;
    short revents;
};

static inline int poll(struct pollfd *fds, unsigned long nfds, int timeout)
{
    if (nfds == 0) return 0;
    WSAPOLLFD wfds[4];
    if (nfds > 4) nfds = 4;
    for (unsigned long i = 0; i < nfds; i++) {
        wfds[i].fd = (SOCKET)(uintptr_t)fds[i].fd;
        wfds[i].events = fds[i].events;
        wfds[i].revents = 0;
    }
    int ret = WSAPoll(wfds, nfds, timeout);
    for (unsigned long i = 0; i < nfds; i++)
        fds[i].revents = wfds[i].revents;
    return ret;
}

#endif
