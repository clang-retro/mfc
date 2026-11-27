// oleauto.h - for 16-bit Windows: OLE Automation is in dispatch.h of the
// win16-sdk. This header adds what MFC's automation classes use from the
// Win32 header: VT_TYPEMASK, the byte-length BSTR functions (a 16-bit BSTR
// is ANSI, so its byte length is its length), and forms of the safe array
// functions that take a far pointer in place of a huge one.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ole2.h>
#include <dispatch.h>

#ifndef VT_TYPEMASK
#define VT_TYPEMASK 0x0FFF
#endif
// The type description codes of type libraries, as in Win32. 16-bit
// TYPELIB uses them in TYPEDESC; dispatch.h does not name them.
#ifndef VT_PTR
#define VT_PTR 26
#endif
#ifndef VT_SAFEARRAY
#define VT_SAFEARRAY 27
#endif
#ifndef VT_CARRAY
#define VT_CARRAY 28
#endif
#ifndef VT_USERDEFINED
#define VT_USERDEFINED 29
#endif

/// Gives the length of an ANSI BSTR in bytes.
inline UINT SysStringByteLen(BSTR bstr) { return ::SysStringLen(bstr); }

/// Makes a BSTR of the first cb bytes of psz.
inline BSTR SysAllocStringByteLen(LPCSTR psz, UINT cb) { return ::SysAllocStringLen(psz, cb); }

/// Gives a far pointer to the data of a safe array.
inline HRESULT SafeArrayAccessData(SAFEARRAY FAR *psa, void FAR *FAR *ppvData) {
  return ::SafeArrayAccessData(psa, (void HUGEP * FAR *)(void FAR *)ppvData);
}

/// Gives a far pointer to one element of a safe array.
inline HRESULT SafeArrayPtrOfIndex(SAFEARRAY FAR *psa, long FAR *rgIndices,
                                   void FAR *FAR *ppvData) {
  return ::SafeArrayPtrOfIndex(psa, rgIndices, (void HUGEP * FAR *)(void FAR *)ppvData);
}
