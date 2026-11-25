// sys/stat.h - the system's, with futimens, which Mac OS X has from
// 10.13. The function is in port/src/compat_macosx.cpp, over futimes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_SYS_STAT_H
#define WINPORT_MACOSX_SYS_STAT_H
#include_next <sys/stat.h>

#ifndef UTIME_OMIT
#define UTIME_NOW -1
#define UTIME_OMIT -2

#ifdef __cplusplus
extern "C" {
#endif
int futimens(int fd, const struct timespec times[2]);
#ifdef __cplusplus
}
#endif
#endif

#endif
