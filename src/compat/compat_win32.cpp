// POSIX compatibility shims for Windows/mingw-w64
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "unistd.h"
#include "sys/mman.h"
#include "sys/ioctl.h"
#include "termios.h"

// --- socket tracking ---

#define MAX_SOCKETS 64
static SOCKET g_socket_fds[MAX_SOCKETS];
static int g_socket_count;

void compat_register_socket(SOCKET s)
{
    if (g_socket_count < MAX_SOCKETS)
        g_socket_fds[g_socket_count++] = s;
}

void compat_unregister_socket(SOCKET s)
{
    for (int i = 0; i < g_socket_count; i++) {
        if (g_socket_fds[i] == s) {
            g_socket_fds[i] = g_socket_fds[--g_socket_count];
            return;
        }
    }
}

extern "C" int compat_is_socket(SOCKET s)
{
    for (int i = 0; i < g_socket_count; i++)
        if (g_socket_fds[i] == s) return 1;
    return 0;
}

// --- mmap ---

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)offset;

    if (flags & MAP_ANON) {
        DWORD flProtect = PAGE_READWRITE;
        if (prot == PROT_READ) flProtect = PAGE_READONLY;
        HANDLE h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, flProtect,
                                     0, (DWORD)length, NULL);
        if (!h) return MAP_FAILED;
        DWORD access = (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
        void *p = MapViewOfFile(h, access, 0, 0, length);
        CloseHandle(h);
        return p ? p : MAP_FAILED;
    }

    HANDLE fh = (HANDLE)_get_osfhandle(fd);
    if (fh == INVALID_HANDLE_VALUE) return MAP_FAILED;

    DWORD flProtect, access;
    if (flags & MAP_PRIVATE) {
        flProtect = PAGE_WRITECOPY;
        access = FILE_MAP_COPY;
    } else {
        flProtect = (prot & PROT_WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        access = (prot & PROT_WRITE) ? FILE_MAP_WRITE : FILE_MAP_READ;
    }

    HANDLE h = CreateFileMapping(fh, NULL, flProtect, 0, (DWORD)length, NULL);
    if (!h) return MAP_FAILED;
    void *p = MapViewOfFile(h, access, 0, 0, length);
    CloseHandle(h);
    return p ? p : MAP_FAILED;
}

int munmap(void *addr, size_t length)
{
    (void)length;
    return UnmapViewOfFile(addr) ? 0 : -1;
}

int mprotect(void *addr, size_t length, int prot)
{
    DWORD newProtect = PAGE_NOACCESS;
    if ((prot & PROT_READ) && (prot & PROT_WRITE))
        newProtect = PAGE_READWRITE;
    else if (prot & PROT_READ)
        newProtect = PAGE_READONLY;

    DWORD old;
    return VirtualProtect(addr, length, newProtect, &old) ? 0 : -1;
}

int msync(void *addr, size_t length, int flags)
{
    (void)flags;
    return FlushViewOfFile(addr, length) ? 0 : -1;
}

// --- fcntl ---

#include "unistd.h"

extern "C" int fcntl(SOCKET fd, int cmd, ...)
{
    if (cmd == F_SETFL) {
        va_list ap;
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        if (compat_is_socket(fd)) {
            u_long mode = (flags & O_NONBLOCK) ? 1 : 0;
            ioctlsocket(fd, FIONBIO, &mode);
        }
        return 0;
    }
    if (cmd == F_GETFL)
        return 0;
    return -1;
}

// --- ioctl (modem control) ---

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);

    HANDLE h = (HANDLE)_get_osfhandle(fd);

    if (request == TIOCMSET) {
        int *bits = va_arg(ap, int *);
        va_end(ap);
        if (*bits & TIOCM_DTR) EscapeCommFunction(h, SETDTR);
        else                   EscapeCommFunction(h, CLRDTR);
        if (*bits & TIOCM_RTS) EscapeCommFunction(h, SETRTS);
        else                   EscapeCommFunction(h, CLRRTS);
        return 0;
    }
    if (request == TIOCMGET) {
        int *bits = va_arg(ap, int *);
        va_end(ap);
        DWORD st = 0;
        GetCommModemStatus(h, &st);
        *bits = 0;
        if (st & MS_CTS_ON) *bits |= TIOCM_CTS;
        if (st & MS_DSR_ON) *bits |= TIOCM_DSR;
        return 0;
    }

    va_end(ap);
    return -1;
}

// --- termios ---

int tcgetattr(int fd, struct termios *t)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DCB dcb;
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) return -1;
    memset(t, 0, sizeof(*t));
    t->c_ispeed = dcb.BaudRate;
    t->c_ospeed = dcb.BaudRate;
    t->c_cflag = CREAD | CLOCAL;
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
    (void)action;
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DCB dcb;
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) return -1;
    dcb.BaudRate = t->c_ospeed;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    return SetCommState(h, &dcb) ? 0 : -1;
}

void cfmakeraw(struct termios *t)
{
    t->c_iflag = 0;
    t->c_oflag = 0;
    t->c_lflag = 0;
}

int cfsetispeed(struct termios *t, speed_t speed)
{
    t->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed)
{
    t->c_ospeed = speed;
    return 0;
}

// PTY stubs — not available on Windows
int posix_openpt(int flags) { (void)flags; return -1; }
int grantpt(int fd) { (void)fd; return -1; }
int unlockpt(int fd) { (void)fd; return -1; }
char *ptsname(int fd) { (void)fd; return NULL; }
