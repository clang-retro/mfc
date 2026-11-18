// ole.h - what the OLE parts of the layer share: small COM helpers and
// enumerators, and copying data (ole_object.cpp).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <ole2.h>
#include <oleidl.h>
#include <objidl.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace winport {
namespace ole {

// ---------------------------------------------------------------------
// Small COM helpers

template <class T>
void SafeRelease(T*& p) {
  if (p) {
    T* q = p;
    p = NULL;
    q->Release();
  }
}

template <class T>
void Assign(T*& slot, T* p) {
  if (p) p->AddRef();
  SafeRelease(slot);
  slot = p;
}

inline LPOLESTR CoStrDup(const wchar_t* s) {
  size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
  LPOLESTR out = (LPOLESTR)CoTaskMemAlloc(n);
  if (out) memcpy(out, s, n);
  return out;
}

// A counted object's IUnknown, for the small classes here.
#define REFCOUNTED(Iface)                                   \
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; } \
  STDMETHOD_(ULONG, Release)() override {                  \
    ULONG n = --refs_;                                     \
    if (n == 0) delete this;                               \
    return n;                                              \
  }                                                        \
  ULONG refs_ = 1;

// An enumerator over a copy of some items.
template <class IEnum, class Item, const IID* Iid>
class VectorEnum final : public IEnum {
 public:
  explicit VectorEnum(std::vector<Item> items, size_t pos = 0) : items_(std::move(items)), pos_(pos) {
    for (Item& i : items_) Copied(i);
  }
  ~VectorEnum() {
    for (Item& i : items_) Free(i);
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, *Iid)) {
      *out = static_cast<IEnum*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IEnum)
  STDMETHOD(Next)(ULONG n, Item* out, ULONG* fetched) override {
    if (!out || (n > 1 && !fetched)) return E_POINTER;
    ULONG k = 0;
    while (k < n && pos_ < items_.size()) {
      out[k] = items_[pos_++];
      Copied(out[k]);
      k++;
    }
    if (fetched) *fetched = k;
    return k == n ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG n) override {
    pos_ = std::min(items_.size(), pos_ + n);
    return pos_ < items_.size() ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    pos_ = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnum** out) override {
    if (!out) return E_POINTER;
    *out = new VectorEnum(items_, pos_);
    return S_OK;
  }

 private:
  // What the copy of an item owns.
  static void Copied(STATDATA& s) {
    if (s.pAdvSink) s.pAdvSink->AddRef();
    s.formatetc.ptd = NULL;
  }
  static void Free(STATDATA& s) { SafeRelease(s.pAdvSink); }
  static void Copied(OLEVERB& v) {
    if (v.lpszVerbName) v.lpszVerbName = CoStrDup(v.lpszVerbName);
  }
  static void Free(OLEVERB& v) { CoTaskMemFree(v.lpszVerbName); }
  static void Copied(FORMATETC& f) { f.ptd = NULL; }
  static void Free(FORMATETC&) {}

  std::vector<Item> items_;
  size_t pos_;
};

typedef VectorEnum<IEnumSTATDATA, STATDATA, &IID_IEnumSTATDATA> StatDataEnum;
typedef VectorEnum<IEnumOLEVERB, OLEVERB, &IID_IEnumOLEVERB> VerbEnum;
typedef VectorEnum<IEnumFORMATETC, FORMATETC, &IID_IEnumFORMATETC> FormatEnum;

inline bool SameFormat(const FORMATETC& a, const FORMATETC& b) {
  return a.cfFormat == b.cfFormat && a.dwAspect == b.dwAspect && a.lindex == b.lindex;
}

// A copy of data in a clipboard format (OleDuplicateData), and of a
// medium, which the copy owns.
HANDLE DuplicateData(HANDLE h, CLIPFORMAT cf, UINT flags);
HRESULT CopyMedium(STGMEDIUM* dst, const STGMEDIUM* src, CLIPFORMAT cf);

// The classes the layer serves itself (the standard font and picture),
// for CoGetClassObject when no program or DLL serves them.
HRESULT GetBuiltinClassObject(REFCLSID clsid, REFIID riid, void** out);

}  // namespace ole
}  // namespace winport
