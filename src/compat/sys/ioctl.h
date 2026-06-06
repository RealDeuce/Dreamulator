// POSIX sys/ioctl.h shim for Windows/mingw-w64
#ifndef COMPAT_SYS_IOCTL_H
#define COMPAT_SYS_IOCTL_H

#include <windows.h>

#define TIOCMGET  0x5415
#define TIOCMSET  0x5418

#define TIOCM_DTR 0x002
#define TIOCM_RTS 0x004
#define TIOCM_CTS 0x020
#define TIOCM_DSR 0x100

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
