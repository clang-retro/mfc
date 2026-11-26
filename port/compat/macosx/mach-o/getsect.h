// mach-o/getsect.h - the system's, with getsectiondata, which Mac OS X
// has from 10.7. The function is in port/src/compat_macosx.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_MACHO_GETSECT_H
#define WINPORT_MACOSX_MACHO_GETSECT_H
#include <stdint.h>
// The 10.4 header has no C++ linkage block.
#ifdef __cplusplus
extern "C" {
#endif
#include_next <mach-o/getsect.h>
#ifdef __cplusplus
}
#endif

#include <AvailabilityMacros.h>
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070 && !defined(__LP64__)
#ifdef __cplusplus
extern "C" {
#endif
uint8_t* getsectiondata(const struct mach_header* mh, const char* segname, const char* sectname,
                        unsigned long* size);
#ifdef __cplusplus
}
#endif
#endif

#endif
