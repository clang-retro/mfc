// string.h - the string functions of the C library, without index and
// rindex. The C library of Windows does not have these two names, and
// Windows programs use them for their own variables and functions (a
// global array named index, for example). The string.h of older Mac OS X
// versions declares them itself; see also strings.h.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#define index __winport_bsd_index
#define rindex __winport_bsd_rindex
#include_next <string.h>
#undef index
#undef rindex
