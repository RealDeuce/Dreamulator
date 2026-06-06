// POSIX poll.h shim for Windows
#ifndef COMPAT_POLL_H
#define COMPAT_POLL_H

#include <winsock2.h>

// MSVC's winsock2.h defines WSAPOLLFD and may typedef it as pollfd.
// Use WSAPOLLFD directly and just provide the poll() function name.
#ifndef POLLIN
#define POLLIN  POLLRDNORM
#endif
#ifndef POLLOUT
#define POLLOUT POLLWRNORM
#endif

#define poll(fds, nfds, timeout) WSAPoll(fds, nfds, timeout)

#endif
