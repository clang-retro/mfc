// sys/stat.h - the C library's, with the special times of futimens.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_DOS_STAT_H
#define WINPORT_DOS_STAT_H
#include_next <sys/stat.h>

#ifndef UTIME_NOW
#define UTIME_NOW ((1L << 30) - 1L)
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

#endif
