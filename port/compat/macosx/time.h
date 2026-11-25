// time.h - the system's, with clock_gettime, which Mac OS X has from
// 10.12. The function is in port/src/compat_macosx.cpp: CLOCK_REALTIME
// reads gettimeofday, and CLOCK_MONOTONIC reads mach_absolute_time.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_TIME_H
#define WINPORT_MACOSX_TIME_H
#include_next <time.h>

#ifndef CLOCK_REALTIME
typedef int clockid_t;
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 6

#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clock, struct timespec* ts);
#ifdef __cplusplus
}
#endif
#endif

#endif
