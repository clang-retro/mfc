// shtypes.h - shell item ID lists: a path in the shell namespace as a run
// of items, each starting with its size, ended by a zero size.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SHTYPES_H
#define _WINPORT_SHTYPES_H

#include <windows.h>

#pragma pack(push, 1)
typedef struct _SHITEMID {
  USHORT cb;
  BYTE abID[1];
} SHITEMID, *LPSHITEMID;
typedef const SHITEMID* LPCSHITEMID;
typedef struct _ITEMIDLIST {
  SHITEMID mkid;
} ITEMIDLIST, *LPITEMIDLIST;
typedef const ITEMIDLIST* LPCITEMIDLIST;
typedef ITEMIDLIST* PIDLIST_ABSOLUTE;
typedef const ITEMIDLIST* PCIDLIST_ABSOLUTE;
#pragma pack(pop)

#endif
