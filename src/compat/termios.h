// POSIX termios.h shim for Windows/mingw-w64
#ifndef COMPAT_TERMIOS_H
#define COMPAT_TERMIOS_H

#include <windows.h>

typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

#define B1200   1200
#define B2400   2400
#define B4800   4800
#define B9600   9600
#define B19200  19200

#define CLOCAL  0x800
#define CREAD   0x80

#define TCSANOW 0

#ifdef __cplusplus
extern "C" {
#endif

int  tcgetattr(int fd, struct termios *t);
int  tcsetattr(int fd, int action, const struct termios *t);
void cfmakeraw(struct termios *t);
int  cfsetispeed(struct termios *t, speed_t speed);
int  cfsetospeed(struct termios *t, speed_t speed);

int  posix_openpt(int flags);
int  grantpt(int fd);
int  unlockpt(int fd);
char *ptsname(int fd);

#ifdef __cplusplus
}
#endif

#endif
