// atlstr.h - ATL's strings: CString, CStringA and CStringW for programs
// without MFC, and CAtlString, CAtlStringA and CAtlStringW for any.
//
// They are the CStringT that MFC's CString is (cstringt.h), over string
// traits that keep everything in the headers: their buffers come from the
// C heap, LoadString reads the module's resources (_AtlBaseModule), the
// formatting follows Microsoft's printf rules, and errors are thrown with
// AtlThrow. In a program with MFC (afx.h included first), CString stays
// MFC's own.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSTR_H__
#define __ATLSTR_H__

#include <atlbase.h>
#include <atlexcept.h>
#include <cstringt.h>
#include <atlstrfmt_.h>
#include <stdlib.h>
#include <string.h>

namespace ATL {

using ::CStringData;
using ::CStringT;

// The characters' operations, formatting in the header.
template <typename BaseType>
struct ChTraitsCRT : ChTraitsMFC<BaseType> {
  static int VFormat(BaseType* buf, int n, const BaseType* f, va_list a) {
    return _AtlStrFmt::VFormat<BaseType>(buf, n, f, a);
  }
  static int StringLength(const BaseType* p) { return ChTraitsMFC<BaseType>::Len(p); }
};
template <typename BaseType>
struct ChTraitsOS : ChTraitsCRT<BaseType> {};

// The buffers: the C heap, and an empty string of this image's own.
struct alignas(8) _AtlStrNil {
  CStringData hdr;
  WCHAR zero[2];
};
inline _AtlStrNil _atlStrNil = {{-1, 0, 0}, {0, 0}};

template <typename BaseType, class StringIterator = ChTraitsCRT<BaseType>>
struct StrTraitATL : StringIterator {
  template <typename C> using Rebind = StrTraitATL<C, ChTraitsCRT<C>>;
  static CStringData* StrNil() { return &_atlStrNil.hdr; }
  static CStringData* StrAlloc(int nChars, int nCharSize) {
    if (nChars < 0) AtlThrow(E_INVALIDARG);
    // Rounded up, so that small changes do not reallocate every time.
    int nAlloc = nChars < 64 ? (nChars + 16) & ~15 : (nChars + 64) & ~63;
    CStringData* p =
        (CStringData*)::malloc(sizeof(CStringData) + (size_t)(nAlloc + 1) * nCharSize);
    if (p == NULL) AtlThrow(E_OUTOFMEMORY);
    p->nRefs = 1;
    p->nDataLength = 0;
    p->nAllocLength = nAlloc;
    ::memset(p->data(), 0, nCharSize);
    return p;
  }
  static void StrRelease(CStringData* p) {
    if (p == StrNil()) return;
    if (p->nRefs < 0) {  // locked: one owner
      ::free(p);
      return;
    }
    if (::InterlockedDecrement(&p->nRefs) <= 0) ::free(p);
  }
  static void ThrowMemory() { AtlThrow(E_OUTOFMEMORY); }
  static void ThrowInvalidArg() { AtlThrow(E_INVALIDARG); }
  static HINSTANCE FindStringResourceInstance(UINT) {
    return _AtlBaseModule.GetResourceInstance();
  }
};

typedef CStringT<WCHAR, StrTraitATL<WCHAR, ChTraitsCRT<WCHAR>>> CAtlStringW;
typedef CStringT<char, StrTraitATL<char, ChTraitsCRT<char>>> CAtlStringA;
#ifdef _UNICODE
typedef CAtlStringW CAtlString;
#else
typedef CAtlStringA CAtlString;
#endif

#ifndef __AFX_H__
typedef CAtlStringA CStringA;
typedef CAtlStringW CStringW;
typedef CAtlString CString;
#endif

}  // namespace ATL

#ifndef __AFX_H__
#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using ATL::CString;
using ATL::CStringA;
using ATL::CStringW;
#endif
#endif

#endif  // __ATLSTR_H__
