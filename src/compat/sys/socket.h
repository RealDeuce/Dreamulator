// POSIX sys/socket.h shim for Windows/mingw-w64
#ifndef COMPAT_SYS_SOCKET_H
#define COMPAT_SYS_SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>

void compat_register_socket(int fd);
void compat_unregister_socket(int fd);

static inline int compat_socket(int domain, int type, int protocol)
{
    SOCKET s = socket(domain, type, protocol);
    if (s == INVALID_SOCKET) return -1;
    int fd = (int)s;
    compat_register_socket(fd);
    return fd;
}

static inline int compat_accept(int sockfd, struct sockaddr *addr, int *addrlen)
{
    SOCKET s = accept((SOCKET)(intptr_t)sockfd, addr, addrlen);
    if (s == INVALID_SOCKET) return -1;
    int fd = (int)s;
    compat_register_socket(fd);
    return fd;
}

#define socket  compat_socket
#define accept  compat_accept

static inline int compat_setsockopt(int fd, int level, int optname,
                                    const void *optval, int optlen)
{
    return setsockopt((SOCKET)(intptr_t)fd, level, optname,
                      (const char *)optval, optlen);
}
#define setsockopt compat_setsockopt

typedef int socklen_t;

#endif
