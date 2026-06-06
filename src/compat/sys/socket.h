// POSIX sys/socket.h shim for Windows/mingw-w64
#ifndef COMPAT_SYS_SOCKET_H
#define COMPAT_SYS_SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

void compat_register_socket(SOCKET s);
void compat_unregister_socket(SOCKET s);

static inline SOCKET compat_socket(int domain, int type, int protocol)
{
    SOCKET s = socket(domain, type, protocol);
    if (s != INVALID_SOCKET)
        compat_register_socket(s);
    return s;
}

static inline SOCKET compat_accept(SOCKET sockfd, struct sockaddr *addr, int *addrlen)
{
    SOCKET s = accept(sockfd, addr, addrlen);
    if (s != INVALID_SOCKET)
        compat_register_socket(s);
    return s;
}

#define socket  compat_socket
#define accept  compat_accept

static inline int compat_setsockopt(SOCKET s, int level, int optname,
                                    const void *optval, int optlen)
{
    return setsockopt(s, level, optname, (const char *)optval, optlen);
}
#define setsockopt compat_setsockopt

typedef int socklen_t;

#endif
