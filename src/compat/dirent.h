// POSIX dirent.h shim for MSVC
// FLTK defines a minimal struct dirent in FL/platform_types.h (d_name[1]
// as flexible array member). We include that first, then provide opendir/
// readdir/closedir which allocate enough space for the actual filename.
#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H

#ifdef _MSC_VER

#include <FL/platform_types.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    HANDLE           hFind;
    WIN32_FIND_DATAA fdata;
    char             _buf[sizeof(struct dirent) + MAX_PATH];
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
    struct dirent *ent = (struct dirent *)d->_buf;
    strncpy(ent->d_name, d->fdata.cFileName, MAX_PATH - 1);
    ent->d_name[MAX_PATH - 1] = '\0';
    return ent;
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
