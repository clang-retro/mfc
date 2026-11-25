// strings.h - the BSD string functions of the C library, without index
// and rindex. The C library of Windows does not have these two names, and
// Windows programs use them for their own variables and functions (a
// global array named index, for example). string.h of Linux and Mac OS X
// includes this file. string.h hides the two names in the same way.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if __has_include_next(<strings.h>)
#define index __winport_bsd_index
#define rindex __winport_bsd_rindex
#include_next <strings.h>
#undef index
#undef rindex
#endif
