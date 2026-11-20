// shlobj.h - the shell's objects: here, the files of a drop (CF_HDROP
// and WM_DROPFILES), and the standard folders.
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

// The standard folders, by CSIDL. On Linux they are the XDG folders
// (~/.config, ~/.local/share, ~/.cache and the user directories in
// ~/.config/user-dirs.dirs) and the home folder; on macOS the folders
// under ~/Library and the home folder.
#define CSIDL_DESKTOP 0x0000
#define CSIDL_PROGRAMS 0x0002
#define CSIDL_PERSONAL 0x0005
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_FAVORITES 0x0006
#define CSIDL_STARTUP 0x0007
#define CSIDL_RECENT 0x0008
#define CSIDL_SENDTO 0x0009
#define CSIDL_STARTMENU 0x000b
#define CSIDL_MYMUSIC 0x000d
#define CSIDL_MYVIDEO 0x000e
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_FONTS 0x0014
#define CSIDL_TEMPLATES 0x0015
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_INTERNET_CACHE 0x0020
#define CSIDL_COOKIES 0x0021
#define CSIDL_HISTORY 0x0022
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_WINDOWS 0x0024
#define CSIDL_SYSTEM 0x0025
#define CSIDL_PROGRAM_FILES 0x0026
#define CSIDL_MYPICTURES 0x0027
#define CSIDL_PROFILE 0x0028
#define CSIDL_COMMON_DOCUMENTS 0x002e
#define CSIDL_FLAG_CREATE 0x8000
#define CSIDL_FLAG_DONT_VERIFY 0x4000
#define CSIDL_FLAG_MASK 0xff00
#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1
HRESULT WINAPI SHGetFolderPathA(HWND, int, HANDLE, DWORD, LPSTR);
HRESULT WINAPI SHGetFolderPathW(HWND, int, HANDLE, DWORD, LPWSTR);
BOOL WINAPI SHGetSpecialFolderPathA(HWND, LPSTR, int, BOOL);
BOOL WINAPI SHGetSpecialFolderPathW(HWND, LPWSTR, int, BOOL);
#define SHGetFolderPath __WINPORT_AW(SHGetFolderPath)
#define SHGetSpecialFolderPath __WINPORT_AW(SHGetSpecialFolderPath)

}  // extern "C"

#endif
