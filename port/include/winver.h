// winver.h - version resources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINVER_H
#define _WINPORT_WINVER_H

#include <winbase.h>

extern "C" {

#define VS_FILE_INFO RT_VERSION
#define VS_VERSION_INFO 1
#define VS_FFI_SIGNATURE 0xFEEF04BDL
#define VS_FFI_STRUCVERSION 0x00010000L
#define VS_FFI_FILEFLAGSMASK 0x0000003FL
#define VS_FF_DEBUG 0x00000001L
#define VS_FF_PRERELEASE 0x00000002L
#define VOS_NT_WINDOWS32 0x00040004L
#define VOS__WINDOWS32 0x00000004L
#define VFT_UNKNOWN 0x00000000L
#define VFT_APP 0x00000001L
#define VFT_DLL 0x00000002L
#define VFT2_UNKNOWN 0x00000000L
typedef struct tagVS_FIXEDFILEINFO {
  DWORD dwSignature;
  DWORD dwStrucVersion;
  DWORD dwFileVersionMS;
  DWORD dwFileVersionLS;
  DWORD dwProductVersionMS;
  DWORD dwProductVersionLS;
  DWORD dwFileFlagsMask;
  DWORD dwFileFlags;
  DWORD dwFileOS;
  DWORD dwFileType;
  DWORD dwFileSubtype;
  DWORD dwFileDateMS;
  DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;
DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR, LPDWORD);
DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR, LPDWORD);
BOOL WINAPI GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID);
BOOL WINAPI GetFileVersionInfoW(LPCWSTR, DWORD, DWORD, LPVOID);
BOOL WINAPI VerQueryValueA(LPCVOID, LPCSTR, LPVOID*, PUINT);
BOOL WINAPI VerQueryValueW(LPCVOID, LPCWSTR, LPVOID*, PUINT);
#define GetFileVersionInfoSize __WINPORT_AW(GetFileVersionInfoSize)
#define GetFileVersionInfo __WINPORT_AW(GetFileVersionInfo)
#define VerQueryValue __WINPORT_AW(VerQueryValue)

}  // extern "C"

#endif
