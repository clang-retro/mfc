// windef.h - the basic Windows types, for the portable Win32 layer.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The layer runs on LP64 systems (64-bit Linux and macOS), where long is
// 64 bits wide, so the 32-bit Windows types are made from int. WCHAR is
// the system's wchar_t (32 bits on these systems), so L"" literals and
// TCHAR work unchanged; the layer converts to UTF-16 only at its edges
// (files written by CArchive, for instance).
#ifndef _WINPORT_WINDEF_H
#define _WINPORT_WINDEF_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifndef _WINPORT
#define _WINPORT 1
#endif

// Calling conventions and decorations mean nothing outside Windows.
#define WINAPI
#define WINAPIV
#define APIENTRY
#define APIPRIVATE
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST const
#define VOID void
#define __stdcall
#define __cdecl
#define _stdcall
#define _cdecl
#define __fastcall
#define __thiscall
#define WINBASEAPI
#define WINUSERAPI
#define WINGDIAPI
#define WINADVAPI
#define WINOLEAPI_(t) extern "C" t
#define WINOLEAPI extern "C" HRESULT
#define WINOLEAUTAPI_(t) extern "C" t
#define WINOLEAUTAPI extern "C" HRESULT
#define WINCOMMCTRLAPI
#define WINSHELLAPI
#define SHSTDAPI extern "C" HRESULT
#define SHSTDAPI_(t) extern "C" t
#define STDAPI extern "C" HRESULT
#define STDAPI_(t) extern "C" t
#define STDAPICALLTYPE
#define STDMETHODCALLTYPE
#define STDMETHODVCALLTYPE
#define DECLSPEC_IMPORT
#define DECLSPEC_EXPORT
#define DECLSPEC_NOVTABLE
#define DECLSPEC_UUID(x)
#define DECLSPEC_SELECTANY __attribute__((weak))
#define DECLSPEC_NORETURN __attribute__((noreturn))
#ifndef __declspec
#define __declspec(x) __winport_declspec_##x
#define __winport_declspec_dllimport
#define __winport_declspec_dllexport
#define __winport_declspec_novtable
#define __winport_declspec_selectany __attribute__((weak))
#define __winport_declspec_noreturn __attribute__((noreturn))
#define __winport_declspec_noinline __attribute__((noinline))
#define __winport_declspec_nothrow
#define __winport_declspec_thread __thread
#endif
#define UNALIGNED
#define RESTRICTED_POINTER
#define IN
#define OUT
#define OPTIONAL

#ifndef NULL
#define NULL 0
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;

typedef char CHAR;
typedef signed char INT8;
typedef unsigned char UCHAR;
typedef unsigned char UINT8;
typedef unsigned char BYTE;
typedef unsigned char BOOLEAN;
typedef short SHORT;
typedef short INT16;
typedef unsigned short USHORT;
typedef unsigned short UINT16;
typedef unsigned short WORD;
typedef int INT;
typedef int INT32;
typedef int LONG;
typedef int LONG32;
typedef int BOOL;
typedef int WINBOOL;
typedef int HFILE;
typedef unsigned int UINT;
typedef unsigned int UINT32;
typedef unsigned int ULONG;
typedef unsigned int ULONG32;
typedef unsigned int DWORD;
typedef unsigned int DWORD32;
// The 64-bit types are long long, as on Windows, so that __int64 (a
// macro, for "unsigned __int64") is the same type as LONGLONG.
#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char
typedef long long INT64;
typedef long long LONG64;
typedef long long LONGLONG;
typedef unsigned long long UINT64;
typedef unsigned long long ULONG64;
typedef unsigned long long DWORD64;
typedef unsigned long long ULONGLONG;
typedef unsigned long long DWORDLONG;
typedef float FLOAT;
typedef double DOUBLE;
typedef wchar_t WCHAR;
typedef WCHAR OLECHAR;

typedef intptr_t INT_PTR;
typedef intptr_t LONG_PTR;
typedef intptr_t SSIZE_T;
typedef uintptr_t UINT_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t SIZE_T;
typedef INT_PTR* PINT_PTR;
typedef UINT_PTR* PUINT_PTR;
typedef LONG_PTR* PLONG_PTR;
typedef ULONG_PTR* PULONG_PTR;
typedef DWORD_PTR* PDWORD_PTR;
typedef SIZE_T* PSIZE_T;
typedef ULONG_PTR KAFFINITY;

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef LONG HRESULT;
typedef WORD ATOM;
typedef DWORD COLORREF;
typedef DWORD LCID;
typedef WORD LANGID;
typedef DWORD LCTYPE;
typedef DWORD LGRPID;
typedef LONG SCODE;
typedef DWORD ACCESS_MASK;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef const BYTE* LPCBYTE;
typedef BOOL* PBOOL;
typedef BOOL* LPBOOL;
typedef BOOLEAN* PBOOLEAN;
typedef CHAR* PCHAR;
typedef UCHAR* PUCHAR;
typedef SHORT* PSHORT;
typedef USHORT* PUSHORT;
typedef WORD* PWORD;
typedef WORD* LPWORD;
typedef INT* PINT;
typedef INT* LPINT;
typedef UINT* PUINT;
typedef UINT* LPUINT;
typedef LONG* PLONG;
typedef LONG* LPLONG;
typedef ULONG* PULONG;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef LONGLONG* PLONGLONG;
typedef ULONGLONG* PULONGLONG;
typedef FLOAT* PFLOAT;
typedef COLORREF* LPCOLORREF;
typedef LCID* PLCID;
typedef SCODE* PSCODE;

typedef CHAR* LPSTR;
typedef CHAR* PSTR;
typedef CHAR* NPSTR;
typedef const CHAR* LPCSTR;
typedef const CHAR* PCSTR;
typedef WCHAR* LPWSTR;
typedef WCHAR* PWSTR;
typedef WCHAR* PWCHAR;
typedef WCHAR* NWPSTR;
typedef const WCHAR* LPCWSTR;
typedef const WCHAR* PCWSTR;
typedef const WCHAR* PCWCH;
typedef const CHAR* PCCH;
typedef OLECHAR* LPOLESTR;
typedef const OLECHAR* LPCOLESTR;
typedef OLECHAR* BSTR;
typedef BSTR* LPBSTR;

#ifdef UNICODE
typedef WCHAR TCHAR;
typedef WCHAR TBYTE;
typedef LPWSTR LPTSTR;
typedef LPWSTR PTSTR;
typedef LPCWSTR LPCTSTR;
typedef LPCWSTR PCTSTR;
#define __TEXT(q) L##q
#else
typedef char TCHAR;
typedef unsigned char TBYTE;
typedef LPSTR LPTSTR;
typedef LPSTR PTSTR;
typedef LPCSTR LPCTSTR;
typedef LPCSTR PCTSTR;
#define __TEXT(q) q
#endif
typedef TCHAR* PTCHAR;
typedef TBYTE* PTBYTE;
#define TEXT(q) __TEXT(q)

// Handles: distinct pointer types, as with STRICT.
#define DECLARE_HANDLE(name) \
  struct name##__ { int unused; }; \
  typedef struct name##__* name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HMETAFILE);
DECLARE_HANDLE(HENHMETAFILE);
DECLARE_HANDLE(HHOOK);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HTASK);
DECLARE_HANDLE(HDWP);
DECLARE_HANDLE(HCOLORSPACE);
DECLARE_HANDLE(HSPRITE);
DECLARE_HANDLE(HUMPD);
DECLARE_HANDLE(HDROP);
DECLARE_HANDLE(HIMAGELIST);
DECLARE_HANDLE(HTREEITEM);
DECLARE_HANDLE(HPROPSHEETPAGE);
DECLARE_HANDLE(HMIDI);
typedef HKEY* PHKEY;
typedef HICON HCURSOR;
typedef HANDLE HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef HANDLE GLOBALHANDLE;
typedef HANDLE LOCALHANDLE;
typedef HINSTANCE HMODULE;

typedef INT_PTR(WINAPI* FARPROC)();
typedef INT_PTR(WINAPI* NEARPROC)();
typedef INT_PTR(WINAPI* PROC)();

#define MAKEWORD(a, b) ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))

#define MAX_PATH 260

#include <winport_crt.h>

typedef struct tagRECT {
  LONG left, top, right, bottom;
} RECT, *PRECT, *NPRECT, *LPRECT;
typedef const RECT* LPCRECT;
typedef struct _RECTL {
  LONG left, top, right, bottom;
} RECTL, *PRECTL, *LPRECTL;
typedef const RECTL* LPCRECTL;
typedef struct tagPOINT {
  LONG x, y;
} POINT, *PPOINT, *NPPOINT, *LPPOINT;
typedef struct _POINTL {
  LONG x, y;
} POINTL, *PPOINTL;
typedef struct tagSIZE {
  LONG cx, cy;
} SIZE, *PSIZE, *LPSIZE;
typedef SIZE SIZEL;
typedef SIZE* PSIZEL;
typedef SIZE* LPSIZEL;
typedef struct tagPOINTS {
  SHORT x, y;
} POINTS, *PPOINTS, *LPPOINTS;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

#define DM_UPDATE 1
#define DM_COPY 2
#define DM_PROMPT 4
#define DM_MODIFY 8
#define DM_IN_BUFFER DM_MODIFY
#define DM_IN_PROMPT DM_PROMPT
#define DM_OUT_BUFFER DM_COPY
#define DM_OUT_DEFAULT DM_UPDATE

#define HFILE_ERROR ((HFILE)-1)

#endif
