// POSIX strings.h shim for Windows
#ifndef COMPAT_STRINGS_H
#define COMPAT_STRINGS_H

#include <string.h>

#ifdef _MSC_VER
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#endif

#endif
