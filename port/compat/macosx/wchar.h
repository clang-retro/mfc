// wchar.h - the system's, with the wide string functions that Mac OS X
// has only from 10.7: wcscasecmp, wcsncasecmp, wcsnlen and wcsdup. The
// functions are in port/src/compat_macosx.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_WCHAR_H
#define WINPORT_MACOSX_WCHAR_H
#include_next <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
int wcscasecmp(const wchar_t* a, const wchar_t* b);
int wcsncasecmp(const wchar_t* a, const wchar_t* b, size_t n);
size_t wcsnlen(const wchar_t* s, size_t max);
wchar_t* wcsdup(const wchar_t* s);
#ifdef __cplusplus
}
#endif

#endif
