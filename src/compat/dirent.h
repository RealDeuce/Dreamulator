// POSIX dirent.h shim for MSVC
#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H

#ifdef _MSC_VER

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string.h>
#include <stdlib.h>

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE           hFind;
    WIN32_FIND_DATAA fdata;
    struct dirent    ent;
    int              first;
} DIR;

static inline DIR *opendir(const char *path)
{
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*", path);
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return NULL;
    d->hFind = FindFirstFileA(pattern, &d->fdata);
    if (d->hFind == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

static inline struct dirent *readdir(DIR *d)
{
    if (d->first) {
        d->first = 0;
    } else {
        if (!FindNextFileA(d->hFind, &d->fdata)) return NULL;
    }
    strncpy(d->ent.d_name, d->fdata.cFileName, MAX_PATH - 1);
    d->ent.d_name[MAX_PATH - 1] = '\0';
    return &d->ent;
}

static inline int closedir(DIR *d)
{
    FindClose(d->hFind);
    free(d);
    return 0;
}

#else
#include_next <dirent.h>
#endif

#endif
