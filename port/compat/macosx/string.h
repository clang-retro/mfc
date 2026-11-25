// string.h - the system's, with strnlen, which Mac OS X has from 10.7.
// The function is in port/src/compat_macosx.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_STRING_H
#define WINPORT_MACOSX_STRING_H
#include_next <string.h>

#ifdef __cplusplus
extern "C" {
#endif
size_t strnlen(const char* s, size_t max);
#ifdef __cplusplus
}
#endif

#endif
