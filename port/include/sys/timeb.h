// sys/timeb.h - the system's ftime and struct timeb, also under the C
// runtime's names _ftime and struct _timeb.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SYS_TIMEB_H
#define _WINPORT_SYS_TIMEB_H
#include_next <sys/timeb.h>
#define _timeb timeb
#define _ftime ftime
#endif
