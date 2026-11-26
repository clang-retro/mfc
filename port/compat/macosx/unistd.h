// unistd.h - the system's. Mac OS X 10.4's sysconf does not count the
// processors, so sysconf here asks sysctl for _SC_NPROCESSORS_ONLN. Before
// 10.2, sysconf also does not know _SC_PAGESIZE, so sysconf here asks
// getpagesize for it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_UNISTD_H
#define WINPORT_MACOSX_UNISTD_H
#include_next <unistd.h>

#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_CONF 0x7f00
#define _SC_NPROCESSORS_ONLN 0x7f01
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 0x7f02
#endif

#ifdef __cplusplus
extern "C" {
#endif
long winport_sysconf(int name);
#ifdef __cplusplus
}
#endif
#define sysconf winport_sysconf
#endif

#endif
