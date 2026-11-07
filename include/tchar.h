// tchar.h - the system's <tchar.h>, or MFC's stand-in where the C library
// has none (picolibc), so that system headers that include it (such as
// oledlg.h) build everywhere.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(__has_include_next) && __has_include_next(<tchar.h>)
#include_next <tchar.h>
#else
#include "afxtchar.h"
#endif
