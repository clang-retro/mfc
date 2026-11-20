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
typedef ITEMIDLIST* PIDLIST_RELATIVE;
typedef const ITEMIDLIST* PCIDLIST_RELATIVE;
typedef const ITEMIDLIST* PCUIDLIST_RELATIVE;
typedef ITEMIDLIST* PITEMID_CHILD;
typedef const ITEMIDLIST* PCITEMID_CHILD;
typedef const ITEMIDLIST* PCUITEMID_CHILD;
typedef const ITEMIDLIST** PCUITEMID_CHILD_ARRAY;
#pragma pack(pop)

// A name handed back by a shell folder: a wide string from the task
// allocator, an offset into the item ID, or a short ANSI string.
#define STRRET_WSTR 0x0000
#define STRRET_OFFSET 0x0001
#define STRRET_CSTR 0x0002
typedef struct _STRRET {
  UINT uType;
  union {
    LPWSTR pOleStr;
    UINT uOffset;
    char cStr[MAX_PATH];
  };
} STRRET, *LPSTRRET;

#endif
