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
#include <atlmem.h>
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

using ::IAtlStringMgr;

// A string manager over a memory manager (CWin32Heap, CCRTHeap, ...).
// Strings keep using their own buffers; the manager's are for code
// that asks it for them.
class CAtlStringMgr : public IAtlStringMgr {
public:
  explicit CAtlStringMgr(IAtlMemMgr* pMemMgr = NULL) : m_pMemMgr(pMemMgr) {
    m_nil.hdr.nRefs = 2;
    m_nil.hdr.nDataLength = 0;
    m_nil.hdr.nAllocLength = 0;
    m_nil.zero[0] = m_nil.zero[1] = 0;
  }
  virtual ~CAtlStringMgr() {}
  void SetMemoryManager(IAtlMemMgr* pMemMgr) { m_pMemMgr = pMemMgr; }
  IAtlMemMgr* GetMemoryManager() const { return m_pMemMgr; }

  CStringData* Allocate(int nChars, int nCharSize) override {
    if (m_pMemMgr == NULL || nChars < 0) return NULL;
    int nAlloc = nChars < 64 ? (nChars + 16) & ~15 : (nChars + 64) & ~63;
    CStringData* p =
        (CStringData*)m_pMemMgr->Allocate(sizeof(CStringData) + (size_t)(nAlloc + 1) * nCharSize);
    if (p == NULL) return NULL;
    p->nRefs = 1;
    p->nDataLength = 0;
    p->nAllocLength = nAlloc;
    ::memset(p->data(), 0, nCharSize);
    return p;
  }
  void Free(CStringData* pData) override {
    if (pData != NULL && pData != &m_nil.hdr && m_pMemMgr != NULL) m_pMemMgr->Free(pData);
  }
  CStringData* Reallocate(CStringData* pData, int nChars, int nCharSize) override {
    if (pData == NULL || pData == &m_nil.hdr) return Allocate(nChars, nCharSize);
    if (m_pMemMgr == NULL || nChars < 0) return NULL;
    int nAlloc = nChars < 64 ? (nChars + 16) & ~15 : (nChars + 64) & ~63;
    CStringData* p = (CStringData*)m_pMemMgr->Reallocate(
        pData, sizeof(CStringData) + (size_t)(nAlloc + 1) * nCharSize);
    if (p == NULL) return NULL;
    p->nAllocLength = nAlloc;
    if (p->nDataLength > nAlloc) p->nDataLength = nAlloc;
    return p;
  }
  CStringData* GetNilString() override { return &m_nil.hdr; }
  IAtlStringMgr* Clone() override { return this; }

protected:
  IAtlMemMgr* m_pMemMgr;
  _AtlStrNil m_nil;
};

// A string with room for t_nChars characters from the start, so that it
// does not reallocate until it grows past them.
template <class StringType, int t_nChars>
class CFixedStringT : public StringType {
public:
  typedef typename StringType::XCHAR XCHAR;
  typedef typename StringType::PCXSTR PCXSTR;
  typedef typename StringType::PCYSTR PCYSTR;

  CFixedStringT() { _Fix(); }
  explicit CFixedStringT(IAtlStringMgr* /*pStringMgr*/) { _Fix(); }
  CFixedStringT(const CFixedStringT& s) : StringType() { _Fix(); _Set(s); }
  CFixedStringT(const StringType& s) : StringType() { _Fix(); _Set(s); }
  CFixedStringT(PCXSTR psz) : StringType(psz) { _Fix(); }
  CFixedStringT(PCYSTR psz) : StringType(psz) { _Fix(); }
  CFixedStringT(PCXSTR pch, int nLength) : StringType(pch, nLength) { _Fix(); }
  CFixedStringT(PCYSTR pch, int nLength) : StringType(pch, nLength) { _Fix(); }
  ~CFixedStringT() {}

  using StringType::operator=;
  CFixedStringT& operator=(const CFixedStringT& s) {
    _Set(s);
    return *this;
  }
  CFixedStringT& operator=(const StringType& s) {
    _Set(s);
    return *this;
  }

private:
  void _Fix() {
    int n = this->GetLength();
    this->Preallocate(n > t_nChars ? n : t_nChars);
  }
  // The characters copied into the buffer kept, not shared.
  void _Set(const StringType& s) {
    if (this->GetString() == s.GetString()) return;
    int n = s.GetLength();
    XCHAR* p = this->GetBuffer(n > t_nChars ? n : t_nChars);
    ::memcpy(p, s.GetString(), (size_t)n * sizeof(XCHAR));
    this->ReleaseBufferSetLength(n);
  }
};

}  // namespace ATL

#ifndef __AFX_H__
#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using ATL::CString;
using ATL::CStringA;
using ATL::CStringW;
#endif
#endif

#endif  // __ATLSTR_H__
