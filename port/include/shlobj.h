// shlobj.h - the shell's objects: here, the files of a drop (CF_HDROP
// and WM_DROPFILES).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SHLOBJ_H
#define _WINPORT_SHLOBJ_H

#include <windows.h>
#include <shellapi.h>

extern "C" {

// An HDROP's memory: this header, then the paths from pFiles bytes in,
// each ending with a NUL and the list with another; wide (WCHAR) when
// fWide is set.
typedef struct _DROPFILES {
  DWORD pFiles;
  POINT pt;
  BOOL fNC;
  BOOL fWide;
} DROPFILES, *LPDROPFILES;

}  // extern "C"

#endif
