// malloc.h - the C runtime's memory header: malloc and the rest from
// <stdlib.h>, alloca, and _alloca, _msize-free names that Win32 programs
// use. Linux has its own malloc.h, which comes first.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_MALLOC_H
#define _WINPORT_MALLOC_H

#if defined(__linux__) && __has_include_next(<malloc.h>)
#include_next <malloc.h>
#endif
#include <alloca.h>
#include <stdlib.h>

#ifndef _alloca
#define _alloca alloca
#endif

#endif
