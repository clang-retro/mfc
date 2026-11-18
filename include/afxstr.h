// afxstr.h - CString, CStringA, CStringW: MFC's strings.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSTR_H__
#define __AFXSTR_H__

#include <cstringt.h>

typedef CStringT<char> CStringA;
typedef CStringT<WCHAR> CStringW;
#ifdef _UNICODE
typedef CStringW CString;
#else
typedef CStringA CString;
#endif
typedef CString CAfxString;

template <typename T> class CSimpleStringT;
typedef CString CSimpleString;

// String conversion classes (CA2W, CT2A, ...) and macros.
#include <atlconv.h>

#endif // __AFXSTR_H__
