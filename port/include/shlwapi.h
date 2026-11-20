// shlwapi.h - the shell's light-weight utilities: paths and strings.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SHLWAPI_H
#define _WINPORT_SHLWAPI_H

#include <windows.h>

// PathCompactPathEx's flags (none are defined), and StrToIntEx's.
#define STIF_DEFAULT 0x00000000
#define STIF_SUPPORT_HEX 0x00000001

extern "C" {

LPSTR WINAPI PathAddBackslashA(LPSTR);
LPWSTR WINAPI PathAddBackslashW(LPWSTR);
BOOL WINAPI PathAddExtensionA(LPSTR, LPCSTR);
BOOL WINAPI PathAddExtensionW(LPWSTR, LPCWSTR);
BOOL WINAPI PathAppendA(LPSTR, LPCSTR);
BOOL WINAPI PathAppendW(LPWSTR, LPCWSTR);
LPSTR WINAPI PathBuildRootA(LPSTR, int);
LPWSTR WINAPI PathBuildRootW(LPWSTR, int);
BOOL WINAPI PathCanonicalizeA(LPSTR, LPCSTR);
BOOL WINAPI PathCanonicalizeW(LPWSTR, LPCWSTR);
LPSTR WINAPI PathCombineA(LPSTR, LPCSTR, LPCSTR);
LPWSTR WINAPI PathCombineW(LPWSTR, LPCWSTR, LPCWSTR);
int WINAPI PathCommonPrefixA(LPCSTR, LPCSTR, LPSTR);
int WINAPI PathCommonPrefixW(LPCWSTR, LPCWSTR, LPWSTR);
BOOL WINAPI PathCompactPathA(HDC, LPSTR, UINT);
BOOL WINAPI PathCompactPathW(HDC, LPWSTR, UINT);
BOOL WINAPI PathCompactPathExA(LPSTR, LPCSTR, UINT, DWORD);
BOOL WINAPI PathCompactPathExW(LPWSTR, LPCWSTR, UINT, DWORD);
BOOL WINAPI PathFileExistsA(LPCSTR);
BOOL WINAPI PathFileExistsW(LPCWSTR);
LPSTR WINAPI PathFindExtensionA(LPCSTR);
LPWSTR WINAPI PathFindExtensionW(LPCWSTR);
LPSTR WINAPI PathFindFileNameA(LPCSTR);
LPWSTR WINAPI PathFindFileNameW(LPCWSTR);
LPSTR WINAPI PathFindNextComponentA(LPCSTR);
LPWSTR WINAPI PathFindNextComponentW(LPCWSTR);
int WINAPI PathGetDriveNumberA(LPCSTR);
int WINAPI PathGetDriveNumberW(LPCWSTR);
BOOL WINAPI PathIsDirectoryA(LPCSTR);
BOOL WINAPI PathIsDirectoryW(LPCWSTR);
BOOL WINAPI PathIsFileSpecA(LPCSTR);
BOOL WINAPI PathIsFileSpecW(LPCWSTR);
BOOL WINAPI PathIsPrefixA(LPCSTR, LPCSTR);
BOOL WINAPI PathIsPrefixW(LPCWSTR, LPCWSTR);
BOOL WINAPI PathIsRelativeA(LPCSTR);
BOOL WINAPI PathIsRelativeW(LPCWSTR);
BOOL WINAPI PathIsRootA(LPCSTR);
BOOL WINAPI PathIsRootW(LPCWSTR);
BOOL WINAPI PathIsSameRootA(LPCSTR, LPCSTR);
BOOL WINAPI PathIsSameRootW(LPCWSTR, LPCWSTR);
BOOL WINAPI PathIsUNCA(LPCSTR);
BOOL WINAPI PathIsUNCW(LPCWSTR);
BOOL WINAPI PathIsUNCServerA(LPCSTR);
BOOL WINAPI PathIsUNCServerW(LPCWSTR);
BOOL WINAPI PathIsUNCServerShareA(LPCSTR);
BOOL WINAPI PathIsUNCServerShareW(LPCWSTR);
BOOL WINAPI PathIsURLA(LPCSTR);
BOOL WINAPI PathIsURLW(LPCWSTR);
BOOL WINAPI PathMakePrettyA(LPSTR);
BOOL WINAPI PathMakePrettyW(LPWSTR);
BOOL WINAPI PathMatchSpecA(LPCSTR, LPCSTR);
BOOL WINAPI PathMatchSpecW(LPCWSTR, LPCWSTR);
BOOL WINAPI PathQuoteSpacesA(LPSTR);
BOOL WINAPI PathQuoteSpacesW(LPWSTR);
BOOL WINAPI PathRelativePathToA(LPSTR, LPCSTR, DWORD, LPCSTR, DWORD);
BOOL WINAPI PathRelativePathToW(LPWSTR, LPCWSTR, DWORD, LPCWSTR, DWORD);
void WINAPI PathRemoveArgsA(LPSTR);
void WINAPI PathRemoveArgsW(LPWSTR);
LPSTR WINAPI PathRemoveBackslashA(LPSTR);
LPWSTR WINAPI PathRemoveBackslashW(LPWSTR);
void WINAPI PathRemoveBlanksA(LPSTR);
void WINAPI PathRemoveBlanksW(LPWSTR);
void WINAPI PathRemoveExtensionA(LPSTR);
void WINAPI PathRemoveExtensionW(LPWSTR);
BOOL WINAPI PathRemoveFileSpecA(LPSTR);
BOOL WINAPI PathRemoveFileSpecW(LPWSTR);
BOOL WINAPI PathRenameExtensionA(LPSTR, LPCSTR);
BOOL WINAPI PathRenameExtensionW(LPWSTR, LPCWSTR);
LPSTR WINAPI PathSkipRootA(LPCSTR);
LPWSTR WINAPI PathSkipRootW(LPCWSTR);
void WINAPI PathStripPathA(LPSTR);
void WINAPI PathStripPathW(LPWSTR);
BOOL WINAPI PathStripToRootA(LPSTR);
BOOL WINAPI PathStripToRootW(LPWSTR);
void WINAPI PathUnquoteSpacesA(LPSTR);
void WINAPI PathUnquoteSpacesW(LPWSTR);
LPSTR WINAPI StrChrA(LPCSTR, WORD);
LPWSTR WINAPI StrChrW(LPCWSTR, WCHAR);
LPSTR WINAPI StrChrIA(LPCSTR, WORD);
LPWSTR WINAPI StrChrIW(LPCWSTR, WCHAR);
LPSTR WINAPI StrRChrA(LPCSTR, LPCSTR, WORD);
LPWSTR WINAPI StrRChrW(LPCWSTR, LPCWSTR, WCHAR);
LPSTR WINAPI StrStrA(LPCSTR, LPCSTR);
LPWSTR WINAPI StrStrW(LPCWSTR, LPCWSTR);
LPSTR WINAPI StrStrIA(LPCSTR, LPCSTR);
LPWSTR WINAPI StrStrIW(LPCWSTR, LPCWSTR);
int WINAPI StrCmpNA(LPCSTR, LPCSTR, int);
int WINAPI StrCmpNW(LPCWSTR, LPCWSTR, int);
int WINAPI StrCmpNIA(LPCSTR, LPCSTR, int);
int WINAPI StrCmpNIW(LPCWSTR, LPCWSTR, int);
int WINAPI StrCmpIA(LPCSTR, LPCSTR);
int WINAPI StrCmpIW(LPCWSTR, LPCWSTR);
int WINAPI StrCmpW(LPCWSTR, LPCWSTR);
LPSTR WINAPI StrDupA(LPCSTR);
LPWSTR WINAPI StrDupW(LPCWSTR);
int WINAPI StrToIntA(LPCSTR);
int WINAPI StrToIntW(LPCWSTR);
BOOL WINAPI StrToIntExA(LPCSTR, DWORD, int*);
BOOL WINAPI StrToIntExW(LPCWSTR, DWORD, int*);
BOOL WINAPI StrTrimA(LPSTR, LPCSTR);
BOOL WINAPI StrTrimW(LPWSTR, LPCWSTR);
LPSTR WINAPI StrCpyNA(LPSTR, LPCSTR, int);
LPWSTR WINAPI StrCpyNW(LPWSTR, LPCWSTR, int);
LPSTR WINAPI StrCatBuffA(LPSTR, LPCSTR, int);
LPWSTR WINAPI StrCatBuffW(LPWSTR, LPCWSTR, int);
LPSTR WINAPI StrFormatByteSize64A(LONGLONG, LPSTR, UINT);
LPWSTR WINAPI StrFormatByteSize64W(LONGLONG, LPWSTR, UINT);

}  // extern "C"

#ifdef UNICODE
#define PathAddBackslash PathAddBackslashW
#define PathAddExtension PathAddExtensionW
#define PathAppend PathAppendW
#define PathBuildRoot PathBuildRootW
#define PathCanonicalize PathCanonicalizeW
#define PathCombine PathCombineW
#define PathCommonPrefix PathCommonPrefixW
#define PathCompactPath PathCompactPathW
#define PathCompactPathEx PathCompactPathExW
#define PathFileExists PathFileExistsW
#define PathFindExtension PathFindExtensionW
#define PathFindFileName PathFindFileNameW
#define PathFindNextComponent PathFindNextComponentW
#define PathGetDriveNumber PathGetDriveNumberW
#define PathIsDirectory PathIsDirectoryW
#define PathIsFileSpec PathIsFileSpecW
#define PathIsPrefix PathIsPrefixW
#define PathIsRelative PathIsRelativeW
#define PathIsRoot PathIsRootW
#define PathIsSameRoot PathIsSameRootW
#define PathIsUNC PathIsUNCW
#define PathIsUNCServer PathIsUNCServerW
#define PathIsUNCServerShare PathIsUNCServerShareW
#define PathIsURL PathIsURLW
#define PathMakePretty PathMakePrettyW
#define PathMatchSpec PathMatchSpecW
#define PathQuoteSpaces PathQuoteSpacesW
#define PathRelativePathTo PathRelativePathToW
#define PathRemoveArgs PathRemoveArgsW
#define PathRemoveBackslash PathRemoveBackslashW
#define PathRemoveBlanks PathRemoveBlanksW
#define PathRemoveExtension PathRemoveExtensionW
#define PathRemoveFileSpec PathRemoveFileSpecW
#define PathRenameExtension PathRenameExtensionW
#define PathSkipRoot PathSkipRootW
#define PathStripPath PathStripPathW
#define PathStripToRoot PathStripToRootW
#define PathUnquoteSpaces PathUnquoteSpacesW
#define StrChr StrChrW
#define StrChrI StrChrIW
#define StrRChr StrRChrW
#define StrStr StrStrW
#define StrStrI StrStrIW
#define StrCmpN StrCmpNW
#define StrCmpNI StrCmpNIW
#define StrCmpI StrCmpIW
#define StrCmp StrCmpW
#define StrDup StrDupW
#define StrToInt StrToIntW
#define StrToIntEx StrToIntExW
#define StrTrim StrTrimW
#define StrCpyN StrCpyNW
#define StrCatBuff StrCatBuffW
#define StrFormatByteSize64 StrFormatByteSize64W
#else
#define PathAddBackslash PathAddBackslashA
#define PathAddExtension PathAddExtensionA
#define PathAppend PathAppendA
#define PathBuildRoot PathBuildRootA
#define PathCanonicalize PathCanonicalizeA
#define PathCombine PathCombineA
#define PathCommonPrefix PathCommonPrefixA
#define PathCompactPath PathCompactPathA
#define PathCompactPathEx PathCompactPathExA
#define PathFileExists PathFileExistsA
#define PathFindExtension PathFindExtensionA
#define PathFindFileName PathFindFileNameA
#define PathFindNextComponent PathFindNextComponentA
#define PathGetDriveNumber PathGetDriveNumberA
#define PathIsDirectory PathIsDirectoryA
#define PathIsFileSpec PathIsFileSpecA
#define PathIsPrefix PathIsPrefixA
#define PathIsRelative PathIsRelativeA
#define PathIsRoot PathIsRootA
#define PathIsSameRoot PathIsSameRootA
#define PathIsUNC PathIsUNCA
#define PathIsUNCServer PathIsUNCServerA
#define PathIsUNCServerShare PathIsUNCServerShareA
#define PathIsURL PathIsURLA
#define PathMakePretty PathMakePrettyA
#define PathMatchSpec PathMatchSpecA
#define PathQuoteSpaces PathQuoteSpacesA
#define PathRelativePathTo PathRelativePathToA
#define PathRemoveArgs PathRemoveArgsA
#define PathRemoveBackslash PathRemoveBackslashA
#define PathRemoveBlanks PathRemoveBlanksA
#define PathRemoveExtension PathRemoveExtensionA
#define PathRemoveFileSpec PathRemoveFileSpecA
#define PathRenameExtension PathRenameExtensionA
#define PathSkipRoot PathSkipRootA
#define PathStripPath PathStripPathA
#define PathStripToRoot PathStripToRootA
#define PathUnquoteSpaces PathUnquoteSpacesA
#define StrChr StrChrA
#define StrChrI StrChrIA
#define StrRChr StrRChrA
#define StrStr StrStrA
#define StrStrI StrStrIA
#define StrCmpN StrCmpNA
#define StrCmpNI StrCmpNIA
#define StrCmpI StrCmpIA
#define StrDup StrDupA
#define StrToInt StrToIntA
#define StrToIntEx StrToIntExA
#define StrCmp lstrcmpA
#define StrTrim StrTrimA
#define StrCpyN StrCpyNA
#define StrCatBuff StrCatBuffA
#define StrFormatByteSize64 StrFormatByteSize64A
#endif

#endif
