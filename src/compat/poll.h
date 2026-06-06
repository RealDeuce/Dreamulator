// POSIX poll.h shim for Windows/mingw-w64
#ifndef COMPAT_POLL_H
#define COMPAT_POLL_H

#include <winsock2.h>

#define poll WSAPoll

#endif
