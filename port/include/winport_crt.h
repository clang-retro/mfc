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

// The "secure" forms and other helpers of the Visual C++ runtime.
typedef int errno_t;
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef __min
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef __max
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#include <stdarg.h>
inline int vsprintf_s(char* buf, size_t n, const char* fmt, va_list args) {
  int r = vsnprintf(buf, n, fmt, args);
  if (r < 0 || (size_t)r >= n) {
    if (n) buf[0] = 0;
    return -1;
  }
  return r;
}
inline int sprintf_s(char* buf, size_t n, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int r = vsprintf_s(buf, n, fmt, args);
  va_end(args);
  return r;
}
inline int vswprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, va_list args) {
  int r = vswprintf(buf, n, fmt, args);
  if (r < 0) {
    if (n) buf[0] = 0;
    return -1;
  }
  return r;
}
inline int swprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int r = vswprintf_s(buf, n, fmt, args);
  va_end(args);
  return r;
}
template <size_t N>
int sprintf_s(char (&buf)[N], const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int r = vsprintf_s(buf, N, fmt, args);
  va_end(args);
  return r;
}
template <size_t N>
int swprintf_s(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int r = vswprintf_s(buf, N, fmt, args);
  va_end(args);
  return r;
}

#endif
