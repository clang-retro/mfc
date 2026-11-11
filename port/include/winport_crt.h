// winport_crt.h - the Microsoft C runtime names Windows programs use,
// over the system's C library.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_CRT_H
#define _WINPORT_CRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>

typedef long long __time64_t;
typedef int __time32_t;

extern "C" {
__time64_t _time64(__time64_t*);
struct tm* _localtime64(const __time64_t*);
struct tm* _gmtime64(const __time64_t*);
__time64_t _mktime64(struct tm*);
__time64_t _mkgmtime64(struct tm*);
int _localtime64_s(struct tm*, const __time64_t*);
int _gmtime64_s(struct tm*, const __time64_t*);
}

#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _strdup strdup
#define _wcsdup wcsdup

#endif
