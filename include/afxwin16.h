// afxwin16.h - the Win32 names that the library uses, for 16-bit Windows
// (clang-retro's i8086-pc-win16 target with win16-sdk). afx.h includes it
// after <windows.h> when _AFX_WIN16 is set.
//
// The library is written for the Win32 API. On 16-bit Windows, this file
// gives the Win32 types, and declares the KERNEL32 functions that the core
// classes call. src/win16.cpp implements those functions over the 16-bit
// KERNEL and the C library. As in MFC 2.5, the counts (INT_PTR) are int,
// and text is ANSI only.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXWIN16_H__
#define __AFXWIN16_H__

#include <windows.h>
#include <stdarg.h>
// OLE 2's COMPOBJ header has WCHAR, LARGE_INTEGER, FILETIME, HRESULT and
// the other types that the 16-bit OLE DLLs share with Win32.
#include <compobj.h>

#ifdef _UNICODE
#error 16-bit Windows has no Unicode: build without _UNICODE
#endif

// The calling convention of the library's functions (AFXAPI, which is
// __stdcall) is pascal, as in MFC 2.5.
#ifndef __stdcall
#define __stdcall __pascal
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

// ---------------------------------------------------------------------
// Types

typedef int INT;
typedef int INT_PTR, *PINT_PTR;
typedef unsigned int UINT_PTR, *PUINT_PTR;
typedef long LONG_PTR, *PLONG_PTR;
typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef unsigned long DWORD_PTR, *PDWORD_PTR;
typedef unsigned int SIZE_T, *PSIZE_T;
typedef int SSIZE_T;
typedef long long LONGLONG;
#ifndef __int64
#define __int64 long long
#endif
typedef unsigned long long ULONGLONG, DWORDLONG;
typedef long long __time64_t;
typedef long __time32_t;
typedef unsigned char UCHAR;
typedef char CCHAR;
typedef float FLOAT;
typedef WCHAR *PWCHAR, *PWSTR;
typedef WCHAR FAR *LPWSTR;
typedef const WCHAR FAR *LPCWSTR, *PCWSTR;
typedef const char *PCSTR;
typedef char TCHAR, _TCHAR_W16;
typedef LPSTR LPTSTR, PTSTR;
typedef LPCSTR LPCTSTR, PCTSTR;
typedef BYTE BOOLEAN;
typedef DWORD LCID;
typedef WORD LANGID;
typedef void *PVOID;
typedef const void *PCVOID;
typedef LONG *PLONG_W16;
typedef ULONG *PULONG;
typedef short *PSHORT;
typedef USHORT *PUSHORT;
typedef LONGLONG *PLONGLONG;
typedef ULONGLONG *PULONGLONG;
typedef DWORD ACCESS_MASK;

#ifndef TEXT
#define TEXT(x) x
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFUL
#endif
#ifndef CP_ACP
#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF8 65001
#endif
#ifndef IS_INTRESOURCE
// MAKEINTRESOURCE puts the ID in the offset of a far pointer to segment 0.
#define IS_INTRESOURCE(r) ((((DWORD_PTR)(r)) >> 16) == 0)
#endif
#ifndef MAKEINTRESOURCEA
#define MAKEINTRESOURCEA MAKEINTRESOURCE
#endif

// FILETIME (compobj.h) is time in 100-nanosecond units since 1 January
// 1601, UTC or local.
typedef FILETIME *PFILETIME;
typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, FAR *LPSYSTEMTIME;
typedef struct _TIME_ZONE_INFORMATION {
  LONG Bias;
  WCHAR StandardName[32];
  SYSTEMTIME StandardDate;
  LONG StandardBias;
  WCHAR DaylightName[32];
  SYSTEMTIME DaylightDate;
  LONG DaylightBias;
} TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, FAR *LPTIME_ZONE_INFORMATION;
#define TIME_ZONE_ID_INVALID 0xFFFFFFFFUL
#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2

typedef struct _WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *PWIN32_FIND_DATA, FAR *LPWIN32_FIND_DATA;

typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, FAR *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED OVERLAPPED, FAR *LPOVERLAPPED;

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_FLAG_WRITE_THROUGH 0x80000000UL
#define FILE_FLAG_RANDOM_ACCESS 0x10000000UL
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000UL
#define FILE_FLAG_NO_BUFFERING 0x20000000UL
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define DUPLICATE_SAME_ACCESS 0x00000002

// A file handle is the DOS handle (an HFILE) in a HANDLE.
#define INVALID_HANDLE_VALUE ((HANDLE)(UINT)-1)

#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_DRIVE 15L
#define ERROR_CURRENT_DIRECTORY 16L
#define ERROR_NOT_SAME_DEVICE 17L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_BAD_UNIT 20L
#define ERROR_NOT_READY 21L
#define ERROR_BAD_COMMAND 22L
#define ERROR_CRC 23L
#define ERROR_BAD_LENGTH 24L
#define ERROR_SEEK 25L
#define ERROR_NOT_DOS_DISK 26L
#define ERROR_SECTOR_NOT_FOUND 27L
#define ERROR_WRITE_FAULT 29L
#define ERROR_READ_FAULT 30L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_WRONG_DISK 34L
#define ERROR_SHARING_BUFFER_EXCEEDED 36L
#define ERROR_HANDLE_EOF 38L
#define ERROR_HANDLE_DISK_FULL 39L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_DUP_NAME 52L
#define ERROR_BAD_NETPATH 53L
#define ERROR_NETWORK_BUSY 54L
#define ERROR_DEV_NOT_EXIST 55L
#define ERROR_ADAP_HDW_ERR 57L
#define ERROR_BAD_NET_RESP 58L
#define ERROR_UNEXP_NET_ERR 59L
#define ERROR_BAD_REM_ADAP 60L
#define ERROR_NO_SPOOL_SPACE 62L
#define ERROR_NETNAME_DELETED 64L
#define ERROR_NETWORK_ACCESS_DENIED 65L
#define ERROR_BAD_DEV_TYPE 66L
#define ERROR_BAD_NET_NAME 67L
#define ERROR_TOO_MANY_NAMES 68L
#define ERROR_SHARING_PAUSED 70L
#define ERROR_REQ_NOT_ACCEP 71L
#define ERROR_FILE_EXISTS 80L
#define ERROR_CANNOT_MAKE 82L
#define ERROR_INVALID_PASSWORD 86L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_NET_WRITE_FAULT 88L
#define ERROR_DRIVE_LOCKED 108L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_OPEN_FAILED 110L
#define ERROR_BUFFER_OVERFLOW 111L
#define ERROR_DISK_FULL 112L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_INVALID_LEVEL 124L
#define ERROR_NEGATIVE_SEEK 131L
#define ERROR_SEEK_ON_DEVICE 132L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_LOCK_FAILED 167L
#define ERROR_BAD_PATHNAME 161L
#define ERROR_BUSY 170L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_FILENAME_EXCED_RANGE 206L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_GEN_FAILURE 31L
#define ERROR_IO_DEVICE 1117L
#define ERROR_INVALID_ADDRESS 487L
#define ERROR_ARITHMETIC_OVERFLOW 534L
#define ERROR_NOT_FOUND 1168L

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY 0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK 0x000000FF
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#ifndef MAKELANGID
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#endif

#define LMEM_FIXED_W32 0

// ---------------------------------------------------------------------
// The Win32 names of 16-bit functions

#define lstrlenA lstrlen
#define lstrcmpA lstrcmp
#define lstrcmpiA lstrcmpi
#define lstrcpyA lstrcpy
#define lstrcpynA lstrcpyn
#define lstrcatA lstrcat
#define LoadStringA LoadString
#define wsprintfA wsprintf
#define wvsprintfA wvsprintf
#define CharUpperBuffA AnsiUpperBuff
#define CharLowerBuffA AnsiLowerBuff
#define CharUpperBuff AnsiUpperBuff
#define CharLowerBuff AnsiLowerBuff
#define CharUpperA AnsiUpper
#define CharLowerA AnsiLower
#define CharUpper AnsiUpper
#define CharLower AnsiLower
#define CharNextA AnsiNext
#define CharPrevA AnsiPrev
#define CharNext AnsiNext
#define CharPrev AnsiPrev
#define CharToOemA AnsiToOem
#define OemToCharA OemToAnsi
#define CharToOem AnsiToOem
#define OemToChar OemToAnsi
#define GetModuleFileNameA GetModuleFileName
#define GetModuleHandleA GetModuleHandle
#define LoadLibraryA LoadLibrary
#define OutputDebugStringA OutputDebugString
#define MessageBoxA MessageBox

// GDI.128, which <windows.h> does not declare.
#ifdef __cplusplus
extern "C" {
#endif
int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator);
#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------
// KERNEL32 functions, in src/win16.cpp

#ifdef __cplusplus
extern "C" {
#endif
// The wide-character functions, for CStringW. 16-bit Windows has none:
// these convert to ANSI (code page 1252, as Latin-1) and back.
int WINAPI lstrlenW(LPCWSTR lpString);
int WINAPI lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2);
int WINAPI lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2);
DWORD WINAPI CharUpperBuffW(LPWSTR lpsz, DWORD cchLength);
DWORD WINAPI CharLowerBuffW(LPWSTR lpsz, DWORD cchLength);
int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax);
DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
                            DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize,
                            va_list FAR *Arguments);

DWORD WINAPI _AfxW16GetLastError(void);
void WINAPI _AfxW16SetLastError(DWORD dwErr);
#define GetLastError _AfxW16GetLastError
#define SetLastError _AfxW16SetLastError

HANDLE WINAPI _AfxW16CreateFile(LPCSTR lpFileName, DWORD dwAccess, DWORD dwShare,
                                LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreation,
                                DWORD dwFlags, HANDLE hTemplate);
BOOL WINAPI _AfxW16ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nToRead, LPDWORD lpRead,
                            LPOVERLAPPED lpOverlapped);
BOOL WINAPI _AfxW16WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nToWrite,
                             LPDWORD lpWritten, LPOVERLAPPED lpOverlapped);
DWORD WINAPI _AfxW16SetFilePointer(HANDLE hFile, LONG lDistance, LONG FAR *lpDistanceHigh,
                                   DWORD dwMethod);
BOOL WINAPI _AfxW16SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistance,
                                    PLARGE_INTEGER lpNewPointer, DWORD dwMethod);
BOOL WINAPI _AfxW16SetEndOfFile(HANDLE hFile);
DWORD WINAPI _AfxW16GetFileSize(HANDLE hFile, LPDWORD lpSizeHigh);
BOOL WINAPI _AfxW16GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpSize);
BOOL WINAPI _AfxW16FlushFileBuffers(HANDLE hFile);
BOOL WINAPI _AfxW16CloseHandle(HANDLE hObject);
BOOL WINAPI _AfxW16LockFile(HANDLE hFile, DWORD dwOffsetLow, DWORD dwOffsetHigh,
                            DWORD nBytesLow, DWORD nBytesHigh);
BOOL WINAPI _AfxW16UnlockFile(HANDLE hFile, DWORD dwOffsetLow, DWORD dwOffsetHigh,
                              DWORD nBytesLow, DWORD nBytesHigh);
BOOL WINAPI _AfxW16DuplicateHandle(HANDLE hSourceProcess, HANDLE hSource,
                                   HANDLE hTargetProcess, LPHANDLE lpTarget,
                                   DWORD dwAccess, BOOL bInherit, DWORD dwOptions);
HANDLE WINAPI _AfxW16GetCurrentProcess(void);
BOOL WINAPI _AfxW16GetFileTime(HANDLE hFile, LPFILETIME lpCreation, LPFILETIME lpAccess,
                               LPFILETIME lpWrite);
BOOL WINAPI _AfxW16SetFileTime(HANDLE hFile, const FILETIME FAR *lpCreation,
                               const FILETIME FAR *lpAccess, const FILETIME FAR *lpWrite);
DWORD WINAPI _AfxW16GetFileAttributes(LPCSTR lpFileName);
BOOL WINAPI _AfxW16SetFileAttributes(LPCSTR lpFileName, DWORD dwAttributes);
BOOL WINAPI _AfxW16DeleteFile(LPCSTR lpFileName);
BOOL WINAPI _AfxW16MoveFile(LPCSTR lpExisting, LPCSTR lpNew);
DWORD WINAPI _AfxW16GetFullPathName(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer,
                                    LPSTR FAR *lpFilePart);
DWORD WINAPI _AfxW16GetTempPath(DWORD nBufferLength, LPSTR lpBuffer);
UINT WINAPI _AfxW16GetTempFileName(LPCSTR lpPath, LPCSTR lpPrefix, UINT uUnique,
                                   LPSTR lpTempFileName);
HANDLE WINAPI _AfxW16FindFirstFile(LPCSTR lpFileName, LPWIN32_FIND_DATA lpFindData);
BOOL WINAPI _AfxW16FindNextFile(HANDLE hFind, LPWIN32_FIND_DATA lpFindData);
BOOL WINAPI _AfxW16FindClose(HANDLE hFind);
DWORD WINAPI _AfxW16GetEnvironmentVariable(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
DWORD WINAPI _AfxW16FormatMessage(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
                                  DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize,
                                  va_list FAR *Arguments);
int WINAPI _AfxW16MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByte,
                                      int cbMultiByte, LPWSTR lpWideChar, int cchWideChar);
int WINAPI _AfxW16WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideChar,
                                      int cchWideChar, LPSTR lpMultiByte, int cbMultiByte,
                                      LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
void WINAPI _AfxW16GetSystemTime(LPSYSTEMTIME lpSystemTime);
void WINAPI _AfxW16GetLocalTime(LPSYSTEMTIME lpSystemTime);
void WINAPI _AfxW16GetSystemTimeAsFileTime(LPFILETIME lpFileTime);
BOOL WINAPI _AfxW16FileTimeToSystemTime(const FILETIME FAR *lpFileTime,
                                        LPSYSTEMTIME lpSystemTime);
BOOL WINAPI _AfxW16SystemTimeToFileTime(const SYSTEMTIME FAR *lpSystemTime,
                                        LPFILETIME lpFileTime);
BOOL WINAPI _AfxW16FileTimeToLocalFileTime(const FILETIME FAR *lpFileTime,
                                           LPFILETIME lpLocalFileTime);
BOOL WINAPI _AfxW16LocalFileTimeToFileTime(const FILETIME FAR *lpLocalFileTime,
                                           LPFILETIME lpFileTime);
DWORD WINAPI _AfxW16GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZone);
BOOL WINAPI _AfxW16FileTimeToDosDateTime(const FILETIME FAR *lpFileTime, LPWORD lpDate,
                                         LPWORD lpTime);
BOOL WINAPI _AfxW16DosDateTimeToFileTime(WORD wDate, WORD wTime, LPFILETIME lpFileTime);
DWORD WINAPI _AfxW16GetCurrentDirectory(DWORD nBufferLength, LPSTR lpBuffer);
#ifdef __cplusplus
}
#endif

#define CreateFile _AfxW16CreateFile
#define CreateFileA _AfxW16CreateFile
#define ReadFile _AfxW16ReadFile
#define WriteFile _AfxW16WriteFile
#define SetFilePointer _AfxW16SetFilePointer
#define SetFilePointerEx _AfxW16SetFilePointerEx
#define SetEndOfFile _AfxW16SetEndOfFile
#define GetFileSize _AfxW16GetFileSize
#define GetFileSizeEx _AfxW16GetFileSizeEx
#define FlushFileBuffers _AfxW16FlushFileBuffers
#define CloseHandle _AfxW16CloseHandle
#define LockFile _AfxW16LockFile
#define UnlockFile _AfxW16UnlockFile
#define DuplicateHandle _AfxW16DuplicateHandle
#define GetCurrentProcess _AfxW16GetCurrentProcess
#define GetFileTime _AfxW16GetFileTime
#define SetFileTime _AfxW16SetFileTime
#define GetFileAttributes _AfxW16GetFileAttributes
#define GetFileAttributesA _AfxW16GetFileAttributes
#define SetFileAttributes _AfxW16SetFileAttributes
#define SetFileAttributesA _AfxW16SetFileAttributes
#define DeleteFile _AfxW16DeleteFile
#define DeleteFileA _AfxW16DeleteFile
#define MoveFile _AfxW16MoveFile
#define MoveFileA _AfxW16MoveFile
#define GetFullPathName _AfxW16GetFullPathName
#define GetFullPathNameA _AfxW16GetFullPathName
#define GetTempPath _AfxW16GetTempPath
#define GetTempPathA _AfxW16GetTempPath
#define GetTempFileNameA _AfxW16GetTempFileName
#ifdef __cplusplus
/// The Win32 form of GetTempFileName (a directory, not a drive letter),
/// next to the 16-bit one.
inline UINT GetTempFileName(LPCSTR lpPath, LPCSTR lpPrefix, UINT uUnique, LPSTR lpTempFileName) {
  return _AfxW16GetTempFileName(lpPath, lpPrefix, uUnique, lpTempFileName);
}
#endif
#define FindFirstFile _AfxW16FindFirstFile
#define FindFirstFileA _AfxW16FindFirstFile
#define FindNextFile _AfxW16FindNextFile
#define FindNextFileA _AfxW16FindNextFile
#define FindClose _AfxW16FindClose
#define GetEnvironmentVariable _AfxW16GetEnvironmentVariable
#define GetEnvironmentVariableA _AfxW16GetEnvironmentVariable
#define FormatMessage _AfxW16FormatMessage
#define FormatMessageA _AfxW16FormatMessage
#define MultiByteToWideChar _AfxW16MultiByteToWideChar
#define WideCharToMultiByte _AfxW16WideCharToMultiByte
#define GetSystemTime _AfxW16GetSystemTime
#define GetLocalTime _AfxW16GetLocalTime
#define GetSystemTimeAsFileTime _AfxW16GetSystemTimeAsFileTime
#define FileTimeToSystemTime _AfxW16FileTimeToSystemTime
#define SystemTimeToFileTime _AfxW16SystemTimeToFileTime
#define FileTimeToLocalFileTime _AfxW16FileTimeToLocalFileTime
#define LocalFileTimeToFileTime _AfxW16LocalFileTimeToFileTime
#define GetTimeZoneInformation _AfxW16GetTimeZoneInformation
#define FileTimeToDosDateTime _AfxW16FileTimeToDosDateTime
#define DosDateTimeToFileTime _AfxW16DosDateTimeToFileTime
#define GetCurrentDirectory _AfxW16GetCurrentDirectory
#define GetCurrentDirectoryA _AfxW16GetCurrentDirectory

// One task, one thread: a critical section has nothing to do.
typedef struct _CRITICAL_SECTION {
  LONG LockCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION, FAR *LPCRITICAL_SECTION;
/// Sets the lock count of a critical section to 0.
inline void InitializeCriticalSection(LPCRITICAL_SECTION p) { p->LockCount = 0; }
/// Does nothing: the critical section has no resources.
inline void DeleteCriticalSection(LPCRITICAL_SECTION) {}
/// Adds 1 to the lock count of a critical section.
inline void EnterCriticalSection(LPCRITICAL_SECTION p) { p->LockCount++; }
/// Subtracts 1 from the lock count of a critical section.
inline void LeaveCriticalSection(LPCRITICAL_SECTION p) { p->LockCount--; }

#ifdef __cplusplus
extern "C" {
#endif
void WINAPI _AfxW16Sleep(DWORD dwMilliseconds);
void WINAPI _AfxW16ExitProcess(UINT uExitCode);
#ifdef __cplusplus
}
#endif
#define Sleep _AfxW16Sleep
#define ExitProcess _AfxW16ExitProcess

// One task, one thread: thread-local storage is a table of 16 values.
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFUL
#ifdef __cplusplus
extern "C" {
#endif
DWORD WINAPI _AfxW16TlsAlloc(void);
BOOL WINAPI _AfxW16TlsFree(DWORD dwTlsIndex);
LPVOID WINAPI _AfxW16TlsGetValue(DWORD dwTlsIndex);
BOOL WINAPI _AfxW16TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);
#ifdef __cplusplus
}
#endif
#define TlsAlloc _AfxW16TlsAlloc
#define TlsFree _AfxW16TlsFree
#define TlsGetValue _AfxW16TlsGetValue
#define TlsSetValue _AfxW16TlsSetValue

// One task, one thread: the interlocked operations are plain ones.
/// Adds 1 to *p and returns the new value.
inline LONG InterlockedIncrement(LONG volatile FAR *p) { return ++*p; }
/// Subtracts 1 from *p and returns the new value.
inline LONG InterlockedDecrement(LONG volatile FAR *p) { return --*p; }
/// Sets *p to v and returns the old value.
inline LONG InterlockedExchange(LONG volatile FAR *p, LONG v) {
  LONG old = *p;
  *p = v;
  return old;
}
/// Sets *p to v if it is c, and returns the old value.
inline LONG InterlockedCompareExchange(LONG volatile FAR *p, LONG v, LONG c) {
  LONG old = *p;
  if (old == c)
    *p = v;
  return old;
}
/// Adds v to *p and returns the old value.
inline LONG InterlockedExchangeAdd(LONG volatile FAR *p, LONG v) {
  LONG old = *p;
  *p += v;
  return old;
}

#endif // __AFXWIN16_H__
