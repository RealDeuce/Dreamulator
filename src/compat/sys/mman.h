// POSIX sys/mman.h shim for Windows/mingw-w64
#ifndef COMPAT_SYS_MMAN_H
#define COMPAT_SYS_MMAN_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04

#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED  ((void *)-1)

#define MS_ASYNC    1
#define MS_SYNC     4

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);
int   mprotect(void *addr, size_t length, int prot);
int   msync(void *addr, size_t length, int flags);

#ifdef __cplusplus
}
#endif

#endif
