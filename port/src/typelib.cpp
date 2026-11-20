// typelib.cpp - type libraries.
//
// A library is made with CreateTypeLib2 (ICreateTypeLib2 and, for each
// of its types, ICreateTypeInfo2) and saved in the layer's own format;
// LoadTypeLib reads that back from a file or from a module's TYPELIB
// resource, and stdole2.tlb is built in (IUnknown and IDispatch). Then
// registration (RegisterTypeLib, LoadRegTypeLib), and ITypeInfo::Invoke,
// which calls a dual or custom interface through its vtable with
// DispCallFunc, as IDispatch implementations such as ATL's and
// CreateStdDispatch's do.
//
// A dual interface is one type seen two ways, as in Windows: its
// dispinterface (what the library hands out) and its vtable interface
// (GetRefTypeOfImplType(-1)).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <oleauto.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "com_wire.h"

using winport::com::Reader;
using winport::com::Writer;

namespace {

const HREFTYPE kSelfInterface = 0xfffffffe;  // a dual's vtable view
const GUID kStdOleLib = {0x00020430, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const CLSID kUniversalMarshaler = {
    0x00020424, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
// What tells one of this file's ITypeInfos from others.
const IID kIID_LayerTypeInfo = {
    0x5f8a2c10, 0x7b3e, 0x4c61, {0x9d, 0x24, 0x31, 0xe7, 0x0a, 0x6b, 0xc2, 0x58}};
const char kMagic[4] = {'W', 'P', 'T', 'L'};
const uint32_t kFormat = 1;

bool SameName(const std::wstring& a, LPCOLESTR b) {
  if (b == NULL) return false;
  size_t n = wcslen(b);
  if (n != a.size()) return false;
  for (size_t i = 0; i < n; i++)
    if (towlower(a[i]) != towlower(b[i])) return false;
  return true;
}

BSTR Bstr(const std::wstring& s) { return SysAllocStringLen(s.data(), (UINT)s.size()); }
std::wstring Str(LPCOLESTR s) { return s ? std::wstring(s) : std::wstring(); }

// A VARIANT that copies and clears itself.
struct Value {
  VARIANT v;
  Value() { VariantInit(&v); }
  Value(const Value& o) {
    VariantInit(&v);
    VariantCopy(&v, &o.v);
  }
  Value& operator=(const Value& o) {
    if (this != &o) VariantCopy(&v, &o.v);
    return *this;
  }
  ~Value() { VariantClear(&v); }
};

// A TYPEDESC that owns what it points at.
struct TDesc {
  VARTYPE vt = VT_EMPTY;
  HREFTYPE href = 0;
  std::shared_ptr<TDesc> inner;  // VT_PTR, VT_SAFEARRAY, VT_CARRAY's elements
  std::vector<SAFEARRAYBOUND> bounds;
};

TDesc FromTypeDesc(const TYPEDESC& t) {
  TDesc d;
  d.vt = t.vt;
  if ((t.vt == VT_PTR || t.vt == VT_SAFEARRAY) && t.lptdesc) {
    d.inner = std::make_shared<TDesc>(FromTypeDesc(*t.lptdesc));
  } else if (t.vt == VT_CARRAY && t.lpadesc) {
    d.inner = std::make_shared<TDesc>(FromTypeDesc(t.lpadesc->tdescElem));
    d.bounds.assign(t.lpadesc->rgbounds, t.lpadesc->rgbounds + t.lpadesc->cDims);
  } else if (t.vt == VT_USERDEFINED) {
    d.href = t.hreftype;
  }
  return d;
}

struct Elem {
  TDesc type;
  USHORT flags = 0;
  bool hasDefault = false;
  Value def;
};

Elem FromElemDesc(const ELEMDESC& e, bool param) {
  Elem el;
  el.type = FromTypeDesc(e.tdesc);
  if (param) {
    el.flags = e.paramdesc.wParamFlags;
    if ((el.flags & PARAMFLAG_FHASDEFAULT) && e.paramdesc.pparamdescex) {
      el.hasDefault = true;
      VariantCopy(&el.def.v, &e.paramdesc.pparamdescex->varDefaultValue);
    } else {
      el.flags &= ~PARAMFLAG_FHASDEFAULT;
    }
  } else {
    el.flags = e.idldesc.wIDLFlags;
  }
  return el;
}

struct Func {
  MEMBERID memid = 0;
  FUNCKIND funckind = FUNC_PUREVIRTUAL;
  INVOKEKIND invkind = INVOKE_FUNC;
  CALLCONV cc = CC_STDCALL;
  SHORT oVft = 0, cParamsOpt = 0;
  WORD flags = 0;
  Elem ret;
  std::vector<Elem> params;
  std::vector<std::wstring> names;  // the function's, then its parameters'
  std::wstring doc;
  DWORD help = 0;
};

struct Var {
  MEMBERID memid = 0;
  VARKIND kind = VAR_PERINSTANCE;
  ULONG oInst = 0;
  Value value;  // VAR_CONST
  Elem type;
  WORD flags = 0;
  std::wstring name, doc;
  DWORD help = 0;
};

struct Impl {
  HREFTYPE href;
  INT flags;
};

struct Type {
  std::wstring name, doc;
  DWORD help = 0;
  GUID guid = IID_NULL;
  TYPEKIND kind = TKIND_INTERFACE;
  WORD flags = 0, maj = 0, min = 0, align = 4, cbSizeVft = 0;
  ULONG cbSizeInstance = 0;
  TDesc alias;
  std::vector<Impl> impls;
  std::vector<Func> funcs;
  std::vector<Var> vars;
};

// A type another one names: one of the library's own, or one of another
// library (kept by GUIDs, and the ITypeInfo itself while it is alive).
struct Ref {
  bool local = true;
  uint32_t index = 0;
  GUID type = IID_NULL, lib = IID_NULL;
  WORD maj = 0, min = 0;
  LCID lcid = 0;
  ITypeInfo* live = NULL;
};

// What GetFuncDesc, GetVarDesc and GetTypeAttr give out, with the
// storage the descriptions point into.
struct Owner {
  std::deque<TYPEDESC> tds;
  std::deque<std::vector<BYTE>> arrays;
  std::vector<ELEMDESC> elems;
  std::deque<PARAMDESCEX> defaults;
  std::deque<VARIANT> values;
  ~Owner() {
    for (PARAMDESCEX& d : defaults) VariantClear(&d.varDefaultValue);
    for (VARIANT& v : values) VariantClear(&v);
  }
  void Build(const TDesc& d, TYPEDESC* out) {
    memset(out, 0, sizeof *out);
    out->vt = d.vt;
    if ((d.vt == VT_PTR || d.vt == VT_SAFEARRAY) && d.inner) {
      tds.emplace_back();
      Build(*d.inner, &tds.back());
      out->lptdesc = &tds.back();
    } else if (d.vt == VT_CARRAY && d.inner) {
      size_t n = d.bounds.size() ? d.bounds.size() : 1;
      arrays.emplace_back(offsetof(ARRAYDESC, rgbounds) + n * sizeof(SAFEARRAYBOUND));
      ARRAYDESC* a = (ARRAYDESC*)arrays.back().data();
      Build(*d.inner, &a->tdescElem);
      a->cDims = (USHORT)d.bounds.size();
      for (size_t i = 0; i < d.bounds.size(); i++) a->rgbounds[i] = d.bounds[i];
      out->lpadesc = a;
    } else if (d.vt == VT_USERDEFINED) {
      out->hreftype = d.href;
    }
  }
  void Build(const Elem& e, ELEMDESC* out, bool param) {
    memset(out, 0, sizeof *out);
    Build(e.type, &out->tdesc);
    if (!param) {
      out->idldesc.wIDLFlags = e.flags;
      return;
    }
    out->paramdesc.wParamFlags = e.flags;
    if (e.hasDefault) {
      defaults.emplace_back();
      PARAMDESCEX& x = defaults.back();
      x.cBytes = sizeof x;
      VariantInit(&x.varDefaultValue);
      VariantCopy(&x.varDefaultValue, &e.def.v);
      out->paramdesc.pparamdescex = &x;
    }
  }
};
template <class T>
struct Block {
  T head;  // what the caller sees, first
  Owner* owner;
};
template <class T>
T* NewBlock(Owner** owner) {
  Block<T>* b = new Block<T>;
  memset(&b->head, 0, sizeof b->head);
  b->owner = *owner = new Owner;
  return &b->head;
}
template <class T>
void FreeBlock(T* p) {
  if (p == NULL) return;
  Block<T>* b = reinterpret_cast<Block<T>*>(p);
  delete b->owner;
  delete b;
}

// ---------------------------------------------------------------------
// The library and its types.

class Info;

class Lib final : public ITypeLib, public ICreateTypeLib2 {
public:
  std::atomic<ULONG> refs{1};
  bool creating = false;  // made by CreateTypeLib2, not loaded
  bool builtin = false;
  std::wstring file;
  GUID guid = IID_NULL;
  LCID lcid = 0;
  SYSKIND syskind = SYS_WIN32;
  WORD maj = 0, min = 0, libFlags = 0;
  std::wstring name, doc, helpFile;
  DWORD help = 0;
  std::vector<Type> types;
  std::vector<Ref> refs_;

  ~Lib() {
    for (Ref& r : refs_)
      if (r.live) r.live->Release();
  }

  HRESULT Resolve(HREFTYPE href, ITypeInfo** out);
  HREFTYPE Intern(const Ref& r) {
    for (size_t i = 0; i < refs_.size(); i++) {
      const Ref& o = refs_[i];
      if (o.local == r.local && (r.local ? o.index == r.index : IsEqualGUID(o.type, r.type))) {
        if (r.live) r.live->Release();
        return (HREFTYPE)(i * 4);
      }
    }
    refs_.push_back(r);
    return (HREFTYPE)((refs_.size() - 1) * 4);
  }
  ITypeInfo* NewInfo(size_t index, bool ifaceView);
  int Find(const GUID& g) {
    for (size_t i = 0; i < types.size(); i++)
      if (IsEqualGUID(types[i].guid, g)) return (int)i;
    return -1;
  }
  void Save(Writer& w);
  bool Load(Reader& r);

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITypeLib)) {
      *out = static_cast<ITypeLib*>(this);
    } else if (creating && (IsEqualIID(riid, IID_ICreateTypeLib) || IsEqualIID(riid, IID_ICreateTypeLib2))) {
      *out = static_cast<ICreateTypeLib2*>(this);
    } else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs;
    if (n == 0 && !builtin) delete this;
    return n;
  }

  // ITypeLib
  STDMETHOD_(UINT, GetTypeInfoCount)() override { return (UINT)types.size(); }
  STDMETHOD(GetTypeInfo)(UINT index, ITypeInfo** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (index >= types.size()) return TYPE_E_ELEMENTNOTFOUND;
    *out = NewInfo(index, false);
    return S_OK;
  }
  STDMETHOD(GetTypeInfoType)(UINT index, TYPEKIND* kind) override {
    if (kind == NULL) return E_INVALIDARG;
    if (index >= types.size()) return TYPE_E_ELEMENTNOTFOUND;
    const Type& t = types[index];
    *kind = (t.kind == TKIND_INTERFACE && (t.flags & TYPEFLAG_FDUAL)) ? TKIND_DISPATCH : t.kind;
    return S_OK;
  }
  STDMETHOD(GetTypeInfoOfGuid)(REFGUID g, ITypeInfo** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    int i = Find(g);
    if (i < 0) return TYPE_E_ELEMENTNOTFOUND;
    *out = NewInfo((size_t)i, false);
    return S_OK;
  }
  STDMETHOD(GetLibAttr)(TLIBATTR** out) override {
    if (out == NULL) return E_INVALIDARG;
    TLIBATTR* a = new TLIBATTR;
    a->guid = guid;
    a->lcid = lcid;
    a->syskind = syskind;
    a->wMajorVerNum = maj;
    a->wMinorVerNum = min;
    a->wLibFlags = libFlags;
    *out = a;
    return S_OK;
  }
  STDMETHOD(GetTypeComp)(ITypeComp** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetDocumentation)(INT index, BSTR* pName, BSTR* pDoc, DWORD* pHelp, BSTR* pFile) override {
    if (index != -1 && (index < 0 || (size_t)index >= types.size())) return TYPE_E_ELEMENTNOTFOUND;
    const std::wstring& n = index == -1 ? name : types[index].name;
    const std::wstring& d = index == -1 ? doc : types[index].doc;
    if (pName) *pName = Bstr(n);
    if (pDoc) *pDoc = d.empty() ? NULL : Bstr(d);
    if (pHelp) *pHelp = index == -1 ? help : types[index].help;
    if (pFile) *pFile = helpFile.empty() ? NULL : Bstr(helpFile);
    return S_OK;
  }
  STDMETHOD(IsName)(LPOLESTR szName, ULONG, BOOL* pf) override {
    if (szName == NULL || pf == NULL) return E_INVALIDARG;
    *pf = FALSE;
    for (const Type& t : types) {
      if (SameName(t.name, szName)) *pf = TRUE;
      for (const Func& f : t.funcs)
        for (const std::wstring& n : f.names)
          if (SameName(n, szName)) *pf = TRUE;
      for (const Var& v : t.vars)
        if (SameName(v.name, szName)) *pf = TRUE;
    }
    return S_OK;
  }
  STDMETHOD(FindName)(LPOLESTR szName, ULONG, ITypeInfo** infos, MEMBERID* ids, USHORT* pc) override {
    if (szName == NULL || pc == NULL || (*pc && (infos == NULL || ids == NULL))) return E_INVALIDARG;
    USHORT n = 0;
    for (size_t i = 0; i < types.size() && n < *pc; i++) {
      const Type& t = types[i];
      MEMBERID id = 0;
      bool hit = SameName(t.name, szName);
      if (hit) id = MEMBERID_NIL;
      for (const Func& f : t.funcs)
        if (!hit && !f.names.empty() && SameName(f.names[0], szName)) {
          hit = true;
          id = f.memid;
        }
      for (const Var& v : t.vars)
        if (!hit && SameName(v.name, szName)) {
          hit = true;
          id = v.memid;
        }
      if (hit) {
        infos[n] = NewInfo(i, false);
        ids[n++] = id;
      }
    }
    *pc = n;
    return S_OK;
  }
  STDMETHOD_(void, ReleaseTLibAttr)(TLIBATTR* a) override { delete a; }

  // ICreateTypeLib2
  STDMETHOD(CreateTypeInfo)(LPOLESTR szName, TYPEKIND kind, ICreateTypeInfo** out) override;
  STDMETHOD(SetName)(LPOLESTR s) override {
    name = Str(s);
    return S_OK;
  }
  STDMETHOD(SetVersion)(WORD a, WORD b) override {
    maj = a;
    min = b;
    return S_OK;
  }
  STDMETHOD(SetGuid)(REFGUID g) override {
    guid = g;
    return S_OK;
  }
  STDMETHOD(SetDocString)(LPOLESTR s) override {
    doc = Str(s);
    return S_OK;
  }
  STDMETHOD(SetHelpFileName)(LPOLESTR s) override {
    helpFile = Str(s);
    return S_OK;
  }
  STDMETHOD(SetHelpContext)(DWORD n) override {
    help = n;
    return S_OK;
  }
  STDMETHOD(SetLcid)(LCID l) override {
    lcid = l;
    return S_OK;
  }
  STDMETHOD(SetLibFlags)(UINT f) override {
    libFlags = (WORD)f;
    return S_OK;
  }
  STDMETHOD(SaveAllChanges)() override;
  STDMETHOD(DeleteTypeInfo)(LPOLESTR s) override {
    for (size_t i = 0; i < types.size(); i++)
      if (SameName(types[i].name, s)) {
        types.erase(types.begin() + i);
        return S_OK;
      }
    return TYPE_E_ELEMENTNOTFOUND;
  }
  STDMETHOD(SetCustData)(REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetHelpStringContext)(ULONG) override { return S_OK; }
  STDMETHOD(SetHelpStringDll)(LPOLESTR) override { return S_OK; }
};

// The ITypeInfo of a type of a library: index is its place there.
class Info final : public ITypeInfo, public ICreateTypeInfo2 {
public:
  Lib* lib;
  size_t index;
  bool ifaceView;

  Info(Lib* l, size_t i, bool iface) : lib(l), index(i), ifaceView(iface) { lib->AddRef(); }
  ~Info() { lib->Release(); }

  Type& T() { return lib->types[index]; }
  bool Dual() {
    const Type& t = T();
    return (t.kind == TKIND_INTERFACE || t.kind == TKIND_DISPATCH) && (t.flags & TYPEFLAG_FDUAL);
  }
  TYPEKIND Kind() { return Dual() ? (ifaceView ? TKIND_INTERFACE : TKIND_DISPATCH) : T().kind; }
  // The function for a call: memid's, of a kind wFlags allows.
  int FindFunc(MEMBERID memid, WORD wFlags) {
    const std::vector<Func>& fs = T().funcs;
    for (size_t i = 0; i < fs.size(); i++)
      if (fs[i].memid == memid && (fs[i].invkind & wFlags)) return (int)i;
    return -1;
  }
  HRESULT Base(ITypeInfo** out) {
    *out = NULL;
    if (T().impls.empty() || T().kind == TKIND_COCLASS) return TYPE_E_ELEMENTNOTFOUND;
    return lib->Resolve(T().impls[0].href, out);
  }
  FUNCDESC* MakeFuncDesc(size_t i);
  void LayOutVtable();
  HRESULT CallVtable(void* pv, FUNCDESC* fd, WORD wFlags, DISPPARAMS* dp, VARIANT* result,
                     EXCEPINFO* ei, UINT* argErr);

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITypeInfo)) {
      *out = static_cast<ITypeInfo*>(this);
    } else if (lib->creating &&
               (IsEqualIID(riid, IID_ICreateTypeInfo) || IsEqualIID(riid, IID_ICreateTypeInfo2))) {
      *out = static_cast<ICreateTypeInfo2*>(this);
    } else if (IsEqualIID(riid, kIID_LayerTypeInfo)) {
      *out = this;
    } else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // ITypeInfo
  STDMETHOD(GetTypeAttr)(TYPEATTR** out) override {
    if (out == NULL) return E_INVALIDARG;
    Owner* owner;
    TYPEATTR* a = NewBlock<TYPEATTR>(&owner);
    const Type& t = T();
    a->guid = t.guid;
    a->lcid = lib->lcid;
    a->memidConstructor = a->memidDestructor = MEMBERID_NIL;
    a->typekind = Kind();
    a->cbSizeInstance = t.cbSizeInstance ? t.cbSizeInstance : (ULONG)sizeof(void*);
    a->cFuncs = (WORD)t.funcs.size();
    a->cVars = (WORD)t.vars.size();
    a->cImplTypes = (WORD)t.impls.size();
    a->cbSizeVft = a->typekind == TKIND_DISPATCH ? (WORD)(7 * sizeof(void*)) : t.cbSizeVft;
    a->cbAlignment = t.align;
    a->wTypeFlags = t.flags;
    if (Dual() && !ifaceView) a->wTypeFlags |= TYPEFLAG_FDISPATCHABLE;
    a->wMajorVerNum = t.maj;
    a->wMinorVerNum = t.min;
    if (t.kind == TKIND_ALIAS) owner->Build(t.alias, &a->tdescAlias);
    *out = a;
    return S_OK;
  }
  STDMETHOD(GetTypeComp)(ITypeComp** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetFuncDesc)(UINT i, FUNCDESC** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (i >= T().funcs.size()) return TYPE_E_ELEMENTNOTFOUND;
    *out = MakeFuncDesc(i);
    return S_OK;
  }
  STDMETHOD(GetVarDesc)(UINT i, VARDESC** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (i >= T().vars.size()) return TYPE_E_ELEMENTNOTFOUND;
    const Var& v = T().vars[i];
    Owner* owner;
    VARDESC* d = NewBlock<VARDESC>(&owner);
    d->memid = v.memid;
    d->varkind = v.kind;
    d->wVarFlags = v.flags;
    owner->Build(v.type, &d->elemdescVar, false);
    if (v.kind == VAR_CONST) {
      owner->values.emplace_back();
      VariantInit(&owner->values.back());
      VariantCopy(&owner->values.back(), &v.value.v);
      d->lpvarValue = &owner->values.back();
    } else {
      d->oInst = v.oInst;
    }
    *out = d;
    return S_OK;
  }
  STDMETHOD(GetNames)(MEMBERID memid, BSTR* names, UINT cMax, UINT* pc) override {
    if (pc == NULL || (cMax && names == NULL)) return E_INVALIDARG;
    *pc = 0;
    for (const Func& f : T().funcs)
      if (f.memid == memid) {
        UINT n = 0;
        for (; n < f.names.size() && n < cMax; n++) names[n] = Bstr(f.names[n]);
        *pc = n;
        return S_OK;
      }
    for (const Var& v : T().vars)
      if (v.memid == memid) {
        if (cMax) {
          names[0] = Bstr(v.name);
          *pc = 1;
        }
        return S_OK;
      }
    ITypeInfo* base;
    if (SUCCEEDED(Base(&base))) {
      HRESULT hr = base->GetNames(memid, names, cMax, pc);
      base->Release();
      return hr;
    }
    return TYPE_E_ELEMENTNOTFOUND;
  }
  STDMETHOD(GetRefTypeOfImplType)(UINT i, HREFTYPE* out) override {
    if (out == NULL) return E_INVALIDARG;
    if (i == (UINT)-1) {
      if (Dual() && !ifaceView) {
        *out = kSelfInterface;
        return S_OK;
      }
      return TYPE_E_ELEMENTNOTFOUND;
    }
    if (i >= T().impls.size()) return TYPE_E_ELEMENTNOTFOUND;
    *out = T().impls[i].href;
    return S_OK;
  }
  STDMETHOD(GetImplTypeFlags)(UINT i, INT* out) override {
    if (out == NULL) return E_INVALIDARG;
    if (i >= T().impls.size()) return TYPE_E_ELEMENTNOTFOUND;
    *out = T().impls[i].flags;
    return S_OK;
  }
  STDMETHOD(GetIDsOfNames)(LPOLESTR* names, UINT c, MEMBERID* ids) override {
    if (names == NULL || ids == NULL || c == 0) return E_INVALIDARG;
    for (UINT i = 0; i < c; i++) ids[i] = MEMBERID_NIL;
    for (const Func& f : T().funcs) {
      if (f.names.empty() || !SameName(f.names[0], names[0])) continue;
      ids[0] = f.memid;
      HRESULT hr = S_OK;
      for (UINT k = 1; k < c; k++) {
        for (size_t j = 1; j < f.names.size(); j++)
          if (SameName(f.names[j], names[k])) ids[k] = (MEMBERID)(j - 1);
        if (ids[k] == MEMBERID_NIL) hr = DISP_E_UNKNOWNNAME;
      }
      return hr;
    }
    for (const Var& v : T().vars)
      if (SameName(v.name, names[0])) {
        ids[0] = v.memid;
        return c == 1 ? S_OK : DISP_E_UNKNOWNNAME;
      }
    ITypeInfo* base;
    if (SUCCEEDED(Base(&base))) {
      HRESULT hr = base->GetIDsOfNames(names, c, ids);
      base->Release();
      return hr;
    }
    return DISP_E_UNKNOWNNAME;
  }
  STDMETHOD(Invoke)(PVOID pv, MEMBERID memid, WORD wFlags, DISPPARAMS* dp, VARIANT* result,
                    EXCEPINFO* ei, UINT* argErr) override {
    if (pv == NULL || dp == NULL) return E_INVALIDARG;
    if (Kind() == TKIND_DISPATCH && !Dual())
      return ((IDispatch*)pv)
          ->Invoke(memid, IID_NULL, LOCALE_USER_DEFAULT, wFlags, dp, result, ei, argErr);
    if (Kind() != TKIND_INTERFACE && Kind() != TKIND_DISPATCH) return TYPE_E_WRONGTYPEKIND;
    int i = FindFunc(memid, wFlags);
    if (i < 0) {
      ITypeInfo* base;
      if (SUCCEEDED(Base(&base))) {
        HRESULT hr = base->Invoke(pv, memid, wFlags, dp, result, ei, argErr);
        base->Release();
        return hr;
      }
      return DISP_E_MEMBERNOTFOUND;
    }
    FUNCDESC* fd = MakeFuncDesc((size_t)i);
    HRESULT hr = CallVtable(pv, fd, wFlags, dp, result, ei, argErr);
    FreeBlock(fd);
    return hr;
  }
  STDMETHOD(GetDocumentation)(MEMBERID memid, BSTR* pName, BSTR* pDoc, DWORD* pHelp, BSTR* pFile) override {
    const std::wstring* n = NULL;
    const std::wstring* d = NULL;
    DWORD h = 0;
    const Type& t = T();
    if (memid == MEMBERID_NIL) {
      n = &t.name;
      d = &t.doc;
      h = t.help;
    }
    for (const Func& f : t.funcs)
      if (n == NULL && f.memid == memid && !f.names.empty()) {
        n = &f.names[0];
        d = &f.doc;
        h = f.help;
      }
    for (const Var& v : t.vars)
      if (n == NULL && v.memid == memid) {
        n = &v.name;
        d = &v.doc;
        h = v.help;
      }
    if (n == NULL) {
      ITypeInfo* base;
      if (SUCCEEDED(Base(&base))) {
        HRESULT hr = base->GetDocumentation(memid, pName, pDoc, pHelp, pFile);
        base->Release();
        return hr;
      }
      return TYPE_E_ELEMENTNOTFOUND;
    }
    if (pName) *pName = Bstr(*n);
    if (pDoc) *pDoc = d->empty() ? NULL : Bstr(*d);
    if (pHelp) *pHelp = h;
    if (pFile) *pFile = lib->helpFile.empty() ? NULL : Bstr(lib->helpFile);
    return S_OK;
  }
  STDMETHOD(GetDllEntry)(MEMBERID, INVOKEKIND, BSTR*, BSTR*, WORD*) override { return TYPE_E_BADMODULEKIND; }
  STDMETHOD(GetRefTypeInfo)(HREFTYPE href, ITypeInfo** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (href == kSelfInterface) {
      if (!Dual()) return TYPE_E_ELEMENTNOTFOUND;
      *out = lib->NewInfo(index, true);
      return S_OK;
    }
    return lib->Resolve(href, out);
  }
  STDMETHOD(AddressOfMember)(MEMBERID, INVOKEKIND, PVOID* ppv) override {
    if (ppv) *ppv = NULL;
    return TYPE_E_BADMODULEKIND;
  }
  STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, PVOID* out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (T().kind != TKIND_COCLASS) return TYPE_E_WRONGTYPEKIND;
    return CoCreateInstance(T().guid, outer, CLSCTX_ALL, riid, out);
  }
  STDMETHOD(GetMops)(MEMBERID, BSTR* out) override {
    if (out) *out = NULL;
    return S_OK;
  }
  STDMETHOD(GetContainingTypeLib)(ITypeLib** out, UINT* pIndex) override {
    if (out) {
      *out = lib;
      lib->AddRef();
    }
    if (pIndex) *pIndex = (UINT)index;
    return S_OK;
  }
  STDMETHOD_(void, ReleaseTypeAttr)(TYPEATTR* p) override { FreeBlock(p); }
  STDMETHOD_(void, ReleaseFuncDesc)(FUNCDESC* p) override { FreeBlock(p); }
  STDMETHOD_(void, ReleaseVarDesc)(VARDESC* p) override { FreeBlock(p); }

  // ICreateTypeInfo2
  STDMETHOD(SetGuid)(REFGUID g) override {
    T().guid = g;
    return S_OK;
  }
  STDMETHOD(SetTypeFlags)(UINT f) override {
    T().flags = (WORD)f;
    if ((f & TYPEFLAG_FDUAL) && (T().kind == TKIND_INTERFACE || T().kind == TKIND_DISPATCH))
      T().flags |= TYPEFLAG_FOLEAUTOMATION;
    return S_OK;
  }
  STDMETHOD(SetDocString)(LPOLESTR s) override {
    T().doc = Str(s);
    return S_OK;
  }
  STDMETHOD(SetHelpContext)(DWORD n) override {
    T().help = n;
    return S_OK;
  }
  STDMETHOD(SetVersion)(WORD a, WORD b) override {
    T().maj = a;
    T().min = b;
    return S_OK;
  }
  STDMETHOD(AddRefTypeInfo)(ITypeInfo* p, HREFTYPE* out) override {
    if (p == NULL || out == NULL) return E_INVALIDARG;
    Ref r;
    Info* mine = NULL;
    if (SUCCEEDED(p->QueryInterface(kIID_LayerTypeInfo, (void**)&mine)) && mine) {
      bool local = mine->lib == lib;
      size_t i = mine->index;
      mine->Release();
      if (local) {
        r.local = true;
        r.index = (uint32_t)i;
        *out = lib->Intern(r);
        return S_OK;
      }
    }
    TYPEATTR* ta;
    HRESULT hr = p->GetTypeAttr(&ta);
    if (FAILED(hr)) return hr;
    r.local = false;
    r.type = ta->guid;
    p->ReleaseTypeAttr(ta);
    ITypeLib* tl = NULL;
    UINT idx;
    if (SUCCEEDED(p->GetContainingTypeLib(&tl, &idx)) && tl) {
      TLIBATTR* la;
      if (SUCCEEDED(tl->GetLibAttr(&la))) {
        r.lib = la->guid;
        r.maj = la->wMajorVerNum;
        r.min = la->wMinorVerNum;
        r.lcid = la->lcid;
        tl->ReleaseTLibAttr(la);
      }
      tl->Release();
    }
    r.live = p;
    p->AddRef();
    *out = lib->Intern(r);
    return S_OK;
  }
  STDMETHOD(AddFuncDesc)(UINT i, FUNCDESC* fd) override {
    if (fd == NULL) return E_INVALIDARG;
    std::vector<Func>& fs = T().funcs;
    if (i > fs.size()) return TYPE_E_ELEMENTNOTFOUND;
    if (fd->cParams < 0 || (fd->cParams > 0 && fd->lprgelemdescParam == NULL)) return E_INVALIDARG;
    Func f;
    f.memid = fd->memid;
    f.funckind = fd->funckind;
    f.invkind = fd->invkind;
    f.cc = fd->callconv;
    f.oVft = fd->oVft;
    f.cParamsOpt = fd->cParamsOpt;
    f.flags = fd->wFuncFlags;
    f.ret = FromElemDesc(fd->elemdescFunc, false);
    for (SHORT k = 0; k < fd->cParams; k++) f.params.push_back(FromElemDesc(fd->lprgelemdescParam[k], true));
    fs.insert(fs.begin() + i, f);
    return S_OK;
  }
  STDMETHOD(AddImplType)(UINT i, HREFTYPE href) override {
    std::vector<Impl>& is = T().impls;
    if (i > is.size()) return TYPE_E_ELEMENTNOTFOUND;
    is.insert(is.begin() + i, Impl{href, 0});
    return S_OK;
  }
  STDMETHOD(SetImplTypeFlags)(UINT i, INT f) override {
    if (i >= T().impls.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().impls[i].flags = f;
    return S_OK;
  }
  STDMETHOD(SetAlignment)(WORD a) override {
    T().align = a;
    return S_OK;
  }
  STDMETHOD(SetSchema)(LPOLESTR) override { return S_OK; }
  STDMETHOD(AddVarDesc)(UINT i, VARDESC* vd) override {
    if (vd == NULL) return E_INVALIDARG;
    std::vector<Var>& vs = T().vars;
    if (i > vs.size()) return TYPE_E_ELEMENTNOTFOUND;
    Var v;
    v.memid = vd->memid;
    v.kind = vd->varkind;
    v.flags = vd->wVarFlags;
    v.type = FromElemDesc(vd->elemdescVar, false);
    if (vd->varkind == VAR_CONST) {
      if (vd->lpvarValue) VariantCopy(&v.value.v, vd->lpvarValue);
    } else {
      v.oInst = vd->oInst;
    }
    vs.insert(vs.begin() + i, v);
    return S_OK;
  }
  STDMETHOD(SetFuncAndParamNames)(UINT i, LPOLESTR* names, UINT c) override {
    if (names == NULL || c == 0) return E_INVALIDARG;
    if (i >= T().funcs.size()) return TYPE_E_ELEMENTNOTFOUND;
    std::vector<std::wstring>& ns = T().funcs[i].names;
    ns.clear();
    for (UINT k = 0; k < c; k++) ns.push_back(Str(names[k]));
    return S_OK;
  }
  STDMETHOD(SetVarName)(UINT i, LPOLESTR s) override {
    if (i >= T().vars.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().vars[i].name = Str(s);
    return S_OK;
  }
  STDMETHOD(SetTypeDescAlias)(TYPEDESC* td) override {
    if (td == NULL) return E_INVALIDARG;
    if (T().kind != TKIND_ALIAS) return TYPE_E_WRONGTYPEKIND;
    T().alias = FromTypeDesc(*td);
    return S_OK;
  }
  STDMETHOD(DefineFuncAsDllEntry)(UINT, LPOLESTR, LPOLESTR) override { return TYPE_E_BADMODULEKIND; }
  STDMETHOD(SetFuncDocString)(UINT i, LPOLESTR s) override {
    if (i >= T().funcs.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().funcs[i].doc = Str(s);
    return S_OK;
  }
  STDMETHOD(SetVarDocString)(UINT i, LPOLESTR s) override {
    if (i >= T().vars.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().vars[i].doc = Str(s);
    return S_OK;
  }
  STDMETHOD(SetFuncHelpContext)(UINT i, DWORD n) override {
    if (i >= T().funcs.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().funcs[i].help = n;
    return S_OK;
  }
  STDMETHOD(SetVarHelpContext)(UINT i, DWORD n) override {
    if (i >= T().vars.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().vars[i].help = n;
    return S_OK;
  }
  STDMETHOD(SetMops)(UINT, BSTR) override { return S_OK; }
  STDMETHOD(SetTypeIdldesc)(IDLDESC*) override { return S_OK; }
  STDMETHOD(LayOut)() override {
    LayOutVtable();
    return S_OK;
  }
  STDMETHOD(DeleteFuncDesc)(UINT i) override {
    if (i >= T().funcs.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().funcs.erase(T().funcs.begin() + i);
    return S_OK;
  }
  STDMETHOD(DeleteFuncDescByMemId)(MEMBERID memid, INVOKEKIND kind) override {
    int i = FindFunc(memid, (WORD)kind);
    return i < 0 ? TYPE_E_ELEMENTNOTFOUND : DeleteFuncDesc((UINT)i);
  }
  STDMETHOD(DeleteVarDesc)(UINT i) override {
    if (i >= T().vars.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().vars.erase(T().vars.begin() + i);
    return S_OK;
  }
  STDMETHOD(DeleteVarDescByMemId)(MEMBERID memid) override {
    for (size_t i = 0; i < T().vars.size(); i++)
      if (T().vars[i].memid == memid) return DeleteVarDesc((UINT)i);
    return TYPE_E_ELEMENTNOTFOUND;
  }
  STDMETHOD(DeleteImplType)(UINT i) override {
    if (i >= T().impls.size()) return TYPE_E_ELEMENTNOTFOUND;
    T().impls.erase(T().impls.begin() + i);
    return S_OK;
  }
  STDMETHOD(SetCustData)(REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetFuncCustData)(UINT, REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetParamCustData)(UINT, UINT, REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetVarCustData)(UINT, REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetImplTypeCustData)(UINT, REFGUID, VARIANT*) override { return S_OK; }
  STDMETHOD(SetHelpStringContext)(ULONG) override { return S_OK; }
  STDMETHOD(SetFuncHelpStringContext)(UINT, ULONG) override { return S_OK; }
  STDMETHOD(SetVarHelpStringContext)(UINT, ULONG) override { return S_OK; }
  STDMETHOD(Invalidate)() override { return S_OK; }
  STDMETHOD(SetName)(LPOLESTR s) override {
    T().name = Str(s);
    return S_OK;
  }

private:
  std::atomic<ULONG> refs_{1};
};

ITypeInfo* Lib::NewInfo(size_t i, bool ifaceView) { return static_cast<ITypeInfo*>(new Info(this, i, ifaceView)); }

HRESULT Lib::CreateTypeInfo(LPOLESTR szName, TYPEKIND kind, ICreateTypeInfo** out) {
  if (szName == NULL || out == NULL) return E_INVALIDARG;
  *out = NULL;
  for (const Type& t : types)
    if (SameName(t.name, szName)) return TYPE_E_NAMECONFLICT;
  Type t;
  t.name = szName;
  t.kind = kind;
  if (kind == TKIND_DISPATCH) t.cbSizeVft = (WORD)(7 * sizeof(void*));
  types.push_back(t);
  Info* info = new Info(this, types.size() - 1, kind == TKIND_INTERFACE);
  *out = static_cast<ICreateTypeInfo2*>(info);
  return S_OK;
}

FUNCDESC* Info::MakeFuncDesc(size_t i) {
  const Func& f = T().funcs[i];
  Owner* owner;
  FUNCDESC* d = NewBlock<FUNCDESC>(&owner);
  d->memid = f.memid;
  d->funckind = Kind() == TKIND_DISPATCH ? FUNC_DISPATCH : f.funckind;
  d->invkind = f.invkind;
  d->callconv = f.cc;
  d->cParams = (SHORT)f.params.size();
  d->cParamsOpt = f.cParamsOpt;
  d->oVft = f.oVft;
  d->wFuncFlags = f.flags;
  owner->Build(f.ret, &d->elemdescFunc, false);
  owner->elems.resize(f.params.size());
  for (size_t k = 0; k < f.params.size(); k++) owner->Build(f.params[k], &owner->elems[k], true);
  d->lprgelemdescParam = owner->elems.empty() ? NULL : owner->elems.data();
  return d;
}

// Each function of an interface gets the vtable slot after its base's
// and the ones before it.
void Info::LayOutVtable() {
  Type& t = T();
  if (t.kind != TKIND_INTERFACE) return;
  WORD base = 0;
  bool known = false;
  ITypeInfo* b;
  if (SUCCEEDED(Base(&b))) {
    TYPEATTR* a;
    if (SUCCEEDED(b->GetTypeAttr(&a))) {
      if (a->typekind == TKIND_DISPATCH && (a->wTypeFlags & TYPEFLAG_FDUAL)) {
        // A dual base: its vtable part.
        HREFTYPE h;
        ITypeInfo* v;
        if (SUCCEEDED(b->GetRefTypeOfImplType((UINT)-1, &h)) && SUCCEEDED(b->GetRefTypeInfo(h, &v))) {
          TYPEATTR* va;
          if (SUCCEEDED(v->GetTypeAttr(&va))) {
            base = va->cbSizeVft;
            known = true;
            v->ReleaseTypeAttr(va);
          }
          v->Release();
        }
      } else {
        base = a->cbSizeVft;
        known = true;
      }
      b->ReleaseTypeAttr(a);
    }
    b->Release();
  } else if (t.impls.empty()) {
    known = true;  // IUnknown itself
  }
  WORD top = base;
  for (size_t i = 0; i < t.funcs.size(); i++) {
    Func& f = t.funcs[i];
    if (known) f.oVft = (SHORT)(base + i * sizeof(void*));
    if ((WORD)(f.oVft + sizeof(void*)) > top) top = (WORD)(f.oVft + sizeof(void*));
  }
  t.cbSizeVft = top;
}

// What a parameter of type td is passed as: a VARTYPE, with VT_BYREF for
// a pointer, and the interface's IID for an interface pointer.
VARTYPE ParamType(ITypeInfo* ti, const TYPEDESC* td, IID* iid, int depth = 0) {
  if (depth > 8) return VT_EMPTY;
  if (td->vt == VT_USERDEFINED) {
    ITypeInfo* ref;
    if (FAILED(ti->GetRefTypeInfo(td->hreftype, &ref))) return VT_EMPTY;
    TYPEATTR* a;
    VARTYPE vt = VT_EMPTY;
    if (SUCCEEDED(ref->GetTypeAttr(&a))) {
      switch (a->typekind) {
        case TKIND_ENUM: vt = VT_I4; break;
        case TKIND_ALIAS: vt = ParamType(ref, &a->tdescAlias, iid, depth + 1); break;
        case TKIND_DISPATCH:
          vt = VT_DISPATCH;
          if (iid) *iid = a->guid;
          break;
        case TKIND_INTERFACE:
        case TKIND_COCLASS:
          vt = VT_UNKNOWN;
          if (iid) *iid = a->guid;
          break;
        default: vt = VT_RECORD; break;
      }
      ref->ReleaseTypeAttr(a);
    }
    ref->Release();
    return vt;
  }
  if (td->vt == VT_PTR && td->lptdesc) {
    VARTYPE inner = ParamType(ti, td->lptdesc, iid, depth + 1);
    // IFoo* is the interface pointer itself; IFoo** a pointer to one.
    if (td->lptdesc->vt == VT_USERDEFINED && (inner == VT_UNKNOWN || inner == VT_DISPATCH)) return inner;
    if (inner & VT_BYREF) return VT_EMPTY;
    return (VARTYPE)(inner | VT_BYREF);
  }
  if (td->vt == VT_SAFEARRAY && td->lptdesc) return (VARTYPE)(VT_ARRAY | td->lptdesc->vt);
  return td->vt;
}

HRESULT Info::CallVtable(void* pv, FUNCDESC* fd, WORD wFlags, DISPPARAMS* dp, VARIANT* result,
                         EXCEPINFO* ei, UINT* argErr) {
  UINT n = (UINT)fd->cParams;
  ELEMDESC* ps = fd->lprgelemdescParam;
  bool hasRetval = n > 0 && (ps[n - 1].paramdesc.wParamFlags & PARAMFLAG_FRETVAL);
  UINT nIn = n - (hasRetval ? 1 : 0);
  UINT nNamed = dp->cNamedArgs, nPos = dp->cArgs - nNamed;
  if (dp->cArgs < nNamed || (nNamed && dp->rgdispidNamedArgs == NULL) || (dp->cArgs && dp->rgvarg == NULL))
    return E_INVALIDARG;

  // The argument for each parameter.
  std::vector<VARIANT*> src(n, (VARIANT*)NULL);
  std::vector<UINT> srcIndex(n, 0);
  UINT pos = 0;
  for (UINT i = 0; i < nIn && pos < nPos; i++) {
    if (ps[i].paramdesc.wParamFlags & PARAMFLAG_FLCID) continue;
    srcIndex[i] = dp->cArgs - 1 - pos;
    src[i] = &dp->rgvarg[srcIndex[i]];
    pos++;
  }
  if (pos < nPos) return DISP_E_BADPARAMCOUNT;
  for (UINT k = 0; k < nNamed; k++) {
    DISPID id = dp->rgdispidNamedArgs[k];
    UINT i;
    if (id == DISPID_PROPERTYPUT && (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) && nIn > 0)
      i = nIn - 1;
    else if (id >= 0 && (UINT)id < nIn)
      i = (UINT)id;
    else
      return DISP_E_PARAMNOTFOUND;
    src[i] = &dp->rgvarg[k];
    srcIndex[i] = k;
  }

  std::vector<VARIANT> hold(n), args(n);
  std::vector<VARIANT*> argPtrs(n);
  std::vector<VARTYPE> vts(n);
  std::vector<IUnknown*> qis;
  VARIANT retv, missing;
  VariantInit(&retv);
  VariantInit(&missing);
  missing.vt = VT_ERROR;
  missing.scode = DISP_E_PARAMNOTFOUND;
  for (UINT i = 0; i < n; i++) {
    VariantInit(&hold[i]);
    VariantInit(&args[i]);
    argPtrs[i] = &args[i];
  }
  HRESULT hr = S_OK;
  for (UINT i = 0; i < n && SUCCEEDED(hr); i++) {
    IID iid = IID_NULL;
    VARTYPE bt = ParamType(this, &ps[i].tdesc, &iid);
    USHORT pf = ps[i].paramdesc.wParamFlags;
    if (bt == VT_EMPTY || bt == VT_RECORD || bt == (VT_BYREF | VT_RECORD)) {
      hr = DISP_E_BADVARTYPE;
      break;
    }
    if (hasRetval && i == n - 1) {
      VARTYPE x = (VARTYPE)(bt & ~VT_BYREF);
      vts[i] = (VARTYPE)(VT_BYREF | x);
      if (x == VT_VARIANT) {
        args[i].byref = &retv;
      } else {
        retv.vt = x;
        args[i].byref = &retv.llVal;
      }
      continue;
    }
    if (pf & PARAMFLAG_FLCID) {
      vts[i] = VT_I4;
      args[i].lVal = LOCALE_USER_DEFAULT;
      continue;
    }
    VARIANT* s = src[i];
    if (s == NULL) {
      bool optional = (pf & PARAMFLAG_FOPT) || (fd->cParamsOpt > 0 && i + fd->cParamsOpt >= nIn);
      if (pf & PARAMFLAG_FHASDEFAULT) {
        s = &ps[i].paramdesc.pparamdescex->varDefaultValue;
      } else if (optional && (bt == VT_VARIANT || bt == (VT_BYREF | VT_VARIANT))) {
        s = &missing;
      } else if ((pf & PARAMFLAG_FOUT) && !(pf & PARAMFLAG_FIN) && (bt & VT_BYREF)) {
        s = &missing;  // an [out] the caller does not want
      } else {
        hr = DISP_E_PARAMNOTOPTIONAL;
        break;
      }
    }
    if (bt == VT_VARIANT) {
      vts[i] = VT_VARIANT;
      args[i] = *s;  // a copy the callee does not own
    } else if (bt == (VT_BYREF | VT_VARIANT)) {
      vts[i] = bt;
      args[i].byref = s->vt == (VT_BYREF | VT_VARIANT) ? s->pvarVal : s;
    } else if (bt & VT_BYREF) {
      vts[i] = bt;
      if (s->vt == bt) {
        args[i].byref = s->byref;
      } else {
        VARTYPE x = (VARTYPE)(bt & ~VT_BYREF);
        if (s != &missing && s->vt != VT_EMPTY && FAILED(VariantChangeType(&hold[i], s, 0, x))) {
          if (argErr) *argErr = srcIndex[i];
          hr = DISP_E_TYPEMISMATCH;
          break;
        }
        hold[i].vt = x;
        args[i].byref = x == VT_VARIANT ? (void*)&hold[i] : (void*)&hold[i].llVal;
      }
    } else if (bt == VT_UNKNOWN || bt == VT_DISPATCH) {
      vts[i] = bt;
      IUnknown* p = NULL;
      VARTYPE svt = (VARTYPE)(s->vt & ~VT_BYREF);
      if (svt == VT_UNKNOWN || svt == VT_DISPATCH) {
        p = (s->vt & VT_BYREF) ? *s->ppunkVal : s->punkVal;
      } else if (s->vt != VT_EMPTY && !(s->vt == VT_ERROR && s == &missing)) {
        if (argErr) *argErr = srcIndex[i];
        hr = DISP_E_TYPEMISMATCH;
        break;
      }
      if (p && !IsEqualIID(iid, IID_NULL) && !IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IDispatch)) {
        IUnknown* q = NULL;
        if (FAILED(p->QueryInterface(iid, (void**)&q))) {
          if (argErr) *argErr = srcIndex[i];
          hr = DISP_E_TYPEMISMATCH;
          break;
        }
        qis.push_back(q);
        p = q;
      }
      args[i].punkVal = p;
    } else {
      vts[i] = bt;
      if (FAILED(VariantChangeType(&hold[i], s, 0, bt))) {
        if (argErr) *argErr = srcIndex[i];
        hr = DISP_E_TYPEMISMATCH;
        break;
      }
      args[i] = hold[i];  // shallow: hold keeps ownership
    }
  }

  if (SUCCEEDED(hr)) {
    IID none;
    VARTYPE rt = ParamType(this, &fd->elemdescFunc.tdesc, &none);
    VARIANT ret;
    VariantInit(&ret);
    hr = DispCallFunc(pv, (ULONG_PTR)fd->oVft, fd->callconv, rt, n, n ? vts.data() : NULL,
                      n ? argPtrs.data() : NULL, &ret);
    if (SUCCEEDED(hr)) {
      if (rt == VT_HRESULT) {
        HRESULT r = ret.scode;
        if (FAILED(r)) {
          if (ei) {
            memset(ei, 0, sizeof *ei);
            ei->scode = r;
            IErrorInfo* err = NULL;
            if (GetErrorInfo(0, &err) == S_OK && err) {
              err->GetSource(&ei->bstrSource);
              err->GetDescription(&ei->bstrDescription);
              err->GetHelpFile(&ei->bstrHelpFile);
              err->GetHelpContext(&ei->dwHelpContext);
              err->Release();
            }
          }
          hr = DISP_E_EXCEPTION;
        } else if (result) {
          VariantInit(result);
          if (hasRetval) {
            *result = retv;
            VariantInit(&retv);
          }
        }
      } else if (result) {
        VariantInit(result);
        if (rt != VT_VOID && rt != VT_EMPTY) *result = ret;
      } else if (rt == VT_BSTR) {
        SysFreeString(ret.bstrVal);
      }
    }
  }
  for (UINT i = 0; i < n; i++) VariantClear(&hold[i]);
  for (IUnknown* q : qis) q->Release();
  VariantClear(&retv);
  return hr;
}

// ---------------------------------------------------------------------
// Resolving a reference.

Lib* StdOle();

HRESULT Lib::Resolve(HREFTYPE href, ITypeInfo** out) {
  *out = NULL;
  if (href % 4 != 0 || href / 4 >= refs_.size()) return TYPE_E_ELEMENTNOTFOUND;
  const Ref& r = refs_[href / 4];
  if (r.local) {
    if (r.index >= types.size()) return TYPE_E_ELEMENTNOTFOUND;
    *out = NewInfo(r.index, false);
    return S_OK;
  }
  if (r.live) {
    *out = r.live;
    r.live->AddRef();
    return S_OK;
  }
  ITypeLib* tl = NULL;
  HRESULT hr = LoadRegTypeLib(r.lib, r.maj, r.min, r.lcid, &tl);
  if (FAILED(hr)) return hr;
  hr = tl->GetTypeInfoOfGuid(r.type, out);
  tl->Release();
  return hr;
}

// ---------------------------------------------------------------------
// The built-in stdole2.tlb: IUnknown and IDispatch.

Elem ElemOf(VARTYPE vt, VARTYPE inner = VT_EMPTY, VARTYPE inner2 = VT_EMPTY, USHORT flags = PARAMFLAG_FIN) {
  Elem e;
  e.type.vt = vt;
  if (inner != VT_EMPTY) {
    e.type.inner = std::make_shared<TDesc>();
    e.type.inner->vt = inner;
    if (inner2 != VT_EMPTY) {
      e.type.inner->inner = std::make_shared<TDesc>();
      e.type.inner->inner->vt = inner2;
    }
  }
  e.flags = flags;
  return e;
}

Func Method(MEMBERID memid, int slot, VARTYPE ret, std::vector<std::wstring> names, std::vector<Elem> params) {
  Func f;
  f.memid = memid;
  f.funckind = FUNC_PUREVIRTUAL;
  f.invkind = INVOKE_FUNC;
  f.cc = CC_STDCALL;
  f.oVft = (SHORT)(slot * sizeof(void*));
  f.flags = FUNCFLAG_FRESTRICTED;
  f.ret = ElemOf(ret, VT_EMPTY, VT_EMPTY, 0);
  f.names = names;
  f.params = params;
  return f;
}

Lib* StdOle() {
  static Lib* lib = [] {
    Lib* l = new Lib;
    l->builtin = true;
    l->guid = kStdOleLib;
    l->maj = 2;
    l->name = L"stdole";
    l->doc = L"OLE Automation";
    Type unk;
    unk.name = L"IUnknown";
    unk.guid = IID_IUnknown;
    unk.kind = TKIND_INTERFACE;
    unk.flags = TYPEFLAG_FHIDDEN;
    unk.cbSizeVft = (WORD)(3 * sizeof(void*));
    unk.funcs.push_back(Method(0x60000000, 0, VT_HRESULT, {L"QueryInterface", L"riid", L"ppvObj"},
                               {ElemOf(VT_PTR, VT_VOID), ElemOf(VT_PTR, VT_PTR, VT_VOID, PARAMFLAG_FOUT)}));
    unk.funcs.push_back(Method(0x60000001, 1, VT_UI4, {L"AddRef"}, {}));
    unk.funcs.push_back(Method(0x60000002, 2, VT_UI4, {L"Release"}, {}));
    l->types.push_back(unk);
    Type disp;
    disp.name = L"IDispatch";
    disp.guid = IID_IDispatch;
    disp.kind = TKIND_INTERFACE;
    disp.flags = TYPEFLAG_FHIDDEN;
    disp.cbSizeVft = (WORD)(7 * sizeof(void*));
    Ref r;
    r.local = true;
    r.index = 0;
    disp.impls.push_back(Impl{l->Intern(r), 0});
    disp.funcs.push_back(Method(0x60010000, 3, VT_HRESULT, {L"GetTypeInfoCount", L"pctinfo"},
                                {ElemOf(VT_PTR, VT_UINT, VT_EMPTY, PARAMFLAG_FOUT)}));
    disp.funcs.push_back(Method(0x60010001, 4, VT_HRESULT, {L"GetTypeInfo", L"itinfo", L"lcid", L"pptinfo"},
                                {ElemOf(VT_UINT), ElemOf(VT_UI4),
                                 ElemOf(VT_PTR, VT_PTR, VT_VOID, PARAMFLAG_FOUT)}));
    disp.funcs.push_back(Method(
        0x60010002, 5, VT_HRESULT, {L"GetIDsOfNames", L"riid", L"rgszNames", L"cNames", L"lcid", L"rgdispid"},
        {ElemOf(VT_PTR, VT_VOID), ElemOf(VT_PTR, VT_PTR, VT_I1), ElemOf(VT_UINT), ElemOf(VT_UI4),
         ElemOf(VT_PTR, VT_I4, VT_EMPTY, PARAMFLAG_FOUT)}));
    disp.funcs.push_back(Method(0x60010003, 6, VT_HRESULT,
                                {L"Invoke", L"dispidMember", L"riid", L"lcid", L"wFlags", L"pdispparams",
                                 L"pvarResult", L"pexcepinfo", L"puArgErr"},
                                {ElemOf(VT_I4), ElemOf(VT_PTR, VT_VOID), ElemOf(VT_UI4), ElemOf(VT_UI2),
                                 ElemOf(VT_PTR, VT_VOID), ElemOf(VT_PTR, VT_VARIANT, VT_EMPTY, PARAMFLAG_FOUT),
                                 ElemOf(VT_PTR, VT_VOID, VT_EMPTY, PARAMFLAG_FOUT),
                                 ElemOf(VT_PTR, VT_UINT, VT_EMPTY, PARAMFLAG_FOUT)}));
    l->types.push_back(disp);
    return l;
  }();
  return lib;
}

// ---------------------------------------------------------------------
// The file format: the layer's own, the same on every platform it runs
// on (both ends are the same layer).

void PutValue(Writer& w, const VARIANT& v) {
  w.U16(v.vt);
  if (v.vt == VT_BSTR) {
    w.Bstr(v.bstrVal);
  } else if (!(v.vt & (VT_BYREF | VT_ARRAY)) && v.vt != VT_UNKNOWN && v.vt != VT_DISPATCH &&
             v.vt != VT_VARIANT && v.vt != VT_DECIMAL && v.vt != VT_RECORD) {
    w.U64((uint64_t)v.llVal);
  }
}
void GetValue(Reader& r, VARIANT* v) {
  VariantInit(v);
  VARTYPE vt = r.U16();
  if (vt == VT_BSTR) {
    v->bstrVal = r.Bstr();
    v->vt = VT_BSTR;
  } else if (!(vt & (VT_BYREF | VT_ARRAY)) && vt != VT_UNKNOWN && vt != VT_DISPATCH && vt != VT_VARIANT &&
             vt != VT_DECIMAL && vt != VT_RECORD) {
    v->llVal = (LONGLONG)r.U64();
    v->vt = vt;
  }
}
void PutStr(Writer& w, const std::wstring& s) { w.Str(s.c_str()); }
std::wstring GetStr(Reader& r) { return r.Str(); }

void PutTDesc(Writer& w, const TDesc& d) {
  w.U16(d.vt);
  if (d.vt == VT_USERDEFINED) w.U32(d.href);
  if (d.vt == VT_CARRAY) {
    w.U16((uint16_t)d.bounds.size());
    for (const SAFEARRAYBOUND& b : d.bounds) {
      w.U32(b.cElements);
      w.U32((uint32_t)b.lLbound);
    }
  }
  if (d.vt == VT_PTR || d.vt == VT_SAFEARRAY || d.vt == VT_CARRAY) {
    w.U16(d.inner ? 1 : 0);
    if (d.inner) PutTDesc(w, *d.inner);
  }
}
TDesc GetTDesc(Reader& r, int depth = 0) {
  TDesc d;
  d.vt = r.U16();
  if (d.vt == VT_USERDEFINED) d.href = r.U32();
  if (d.vt == VT_CARRAY) {
    uint16_t n = r.U16();
    for (uint16_t i = 0; i < n && !r.bad; i++) {
      SAFEARRAYBOUND b;
      b.cElements = r.U32();
      b.lLbound = (LONG)r.U32();
      d.bounds.push_back(b);
    }
  }
  if (d.vt == VT_PTR || d.vt == VT_SAFEARRAY || d.vt == VT_CARRAY) {
    if (r.U16() && depth < 32 && !r.bad) d.inner = std::make_shared<TDesc>(GetTDesc(r, depth + 1));
  }
  return d;
}
void PutElem(Writer& w, const Elem& e) {
  PutTDesc(w, e.type);
  w.U16(e.flags);
  w.U16(e.hasDefault ? 1 : 0);
  if (e.hasDefault) PutValue(w, e.def.v);
}
Elem GetElem(Reader& r) {
  Elem e;
  e.type = GetTDesc(r);
  e.flags = r.U16();
  e.hasDefault = r.U16() != 0;
  if (e.hasDefault) GetValue(r, &e.def.v);
  return e;
}

void Lib::Save(Writer& w) {
  w.Put(kMagic, 4);
  w.U32(kFormat);
  w.Guid(guid);
  w.U32(lcid);
  w.U32(syskind);
  w.U16(maj);
  w.U16(min);
  w.U16(libFlags);
  PutStr(w, name);
  PutStr(w, doc);
  PutStr(w, helpFile);
  w.U32(help);
  w.U32((uint32_t)refs_.size());
  for (const Ref& r : refs_) {
    w.U16(r.local ? 1 : 0);
    w.U32(r.index);
    w.Guid(r.type);
    w.Guid(r.lib);
    w.U16(r.maj);
    w.U16(r.min);
    w.U32(r.lcid);
  }
  w.U32((uint32_t)types.size());
  for (const Type& t : types) {
    PutStr(w, t.name);
    PutStr(w, t.doc);
    w.U32(t.help);
    w.Guid(t.guid);
    w.U32(t.kind);
    w.U16(t.flags);
    w.U16(t.maj);
    w.U16(t.min);
    w.U16(t.align);
    w.U16(t.cbSizeVft);
    w.U32(t.cbSizeInstance);
    PutTDesc(w, t.alias);
    w.U32((uint32_t)t.impls.size());
    for (const Impl& i : t.impls) {
      w.U32(i.href);
      w.U32((uint32_t)i.flags);
    }
    w.U32((uint32_t)t.funcs.size());
    for (const Func& f : t.funcs) {
      w.U32((uint32_t)f.memid);
      w.U32(f.funckind);
      w.U32(f.invkind);
      w.U32(f.cc);
      w.U16((uint16_t)f.oVft);
      w.U16((uint16_t)f.cParamsOpt);
      w.U16(f.flags);
      PutElem(w, f.ret);
      w.U32((uint32_t)f.params.size());
      for (const Elem& e : f.params) PutElem(w, e);
      w.U32((uint32_t)f.names.size());
      for (const std::wstring& n : f.names) PutStr(w, n);
      PutStr(w, f.doc);
      w.U32(f.help);
    }
    w.U32((uint32_t)t.vars.size());
    for (const Var& v : t.vars) {
      w.U32((uint32_t)v.memid);
      w.U32(v.kind);
      w.U32(v.oInst);
      PutValue(w, v.value.v);
      PutElem(w, v.type);
      w.U16(v.flags);
      PutStr(w, v.name);
      PutStr(w, v.doc);
      w.U32(v.help);
    }
  }
}

bool Lib::Load(Reader& r) {
  char magic[4];
  if (!r.Get(magic, 4) || memcmp(magic, kMagic, 4) != 0 || r.U32() != kFormat) return false;
  guid = r.Guid();
  lcid = r.U32();
  syskind = (SYSKIND)r.U32();
  maj = r.U16();
  min = r.U16();
  libFlags = r.U16();
  name = GetStr(r);
  doc = GetStr(r);
  helpFile = GetStr(r);
  help = r.U32();
  uint32_t n = r.U32();
  for (uint32_t i = 0; i < n && !r.bad; i++) {
    Ref ref;
    ref.local = r.U16() != 0;
    ref.index = r.U32();
    ref.type = r.Guid();
    ref.lib = r.Guid();
    ref.maj = r.U16();
    ref.min = r.U16();
    ref.lcid = r.U32();
    refs_.push_back(ref);
  }
  n = r.U32();
  for (uint32_t i = 0; i < n && !r.bad; i++) {
    Type t;
    t.name = GetStr(r);
    t.doc = GetStr(r);
    t.help = r.U32();
    t.guid = r.Guid();
    t.kind = (TYPEKIND)r.U32();
    t.flags = r.U16();
    t.maj = r.U16();
    t.min = r.U16();
    t.align = r.U16();
    t.cbSizeVft = r.U16();
    t.cbSizeInstance = r.U32();
    t.alias = GetTDesc(r);
    uint32_t m = r.U32();
    for (uint32_t k = 0; k < m && !r.bad; k++) {
      Impl im;
      im.href = r.U32();
      im.flags = (INT)r.U32();
      t.impls.push_back(im);
    }
    m = r.U32();
    for (uint32_t k = 0; k < m && !r.bad; k++) {
      Func f;
      f.memid = (MEMBERID)r.U32();
      f.funckind = (FUNCKIND)r.U32();
      f.invkind = (INVOKEKIND)r.U32();
      f.cc = (CALLCONV)r.U32();
      f.oVft = (SHORT)r.U16();
      f.cParamsOpt = (SHORT)r.U16();
      f.flags = r.U16();
      f.ret = GetElem(r);
      uint32_t np = r.U32();
      for (uint32_t j = 0; j < np && !r.bad; j++) f.params.push_back(GetElem(r));
      uint32_t nn = r.U32();
      for (uint32_t j = 0; j < nn && !r.bad; j++) f.names.push_back(GetStr(r));
      f.doc = GetStr(r);
      f.help = r.U32();
      t.funcs.push_back(f);
    }
    m = r.U32();
    for (uint32_t k = 0; k < m && !r.bad; k++) {
      Var v;
      v.memid = (MEMBERID)r.U32();
      v.kind = (VARKIND)r.U32();
      v.oInst = r.U32();
      GetValue(r, &v.value.v);
      v.type = GetElem(r);
      v.flags = r.U16();
      v.name = GetStr(r);
      v.doc = GetStr(r);
      v.help = r.U32();
      t.vars.push_back(v);
    }
    types.push_back(t);
  }
  return !r.bad;
}

HRESULT Lib::SaveAllChanges() {
  for (size_t i = 0; i < types.size(); i++) {
    Info info(this, i, true);
    info.LayOutVtable();
  }
  Writer w;
  Save(w);
  HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return TYPE_E_IOERROR;
  DWORD done = 0;
  BOOL ok = WriteFile(h, w.b.data(), (DWORD)w.b.size(), &done, NULL) && done == w.b.size();
  CloseHandle(h);
  return ok ? S_OK : TYPE_E_IOERROR;
}

bool ReadFileBytes(LPCOLESTR path, std::vector<BYTE>* out) {
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD size = GetFileSize(h, NULL), done = 0;
  bool ok = size != INVALID_FILE_SIZE;
  if (ok) {
    out->resize(size);
    ok = size == 0 || (ReadFile(h, out->data(), size, &done, NULL) && done == size);
  }
  CloseHandle(h);
  return ok;
}

HRESULT FromBytes(const std::vector<BYTE>& bytes, const std::wstring& path, ITypeLib** out) {
  if (bytes.size() >= 4 && memcmp(bytes.data(), "MSFT", 4) == 0) return TYPE_E_UNSUPFORMAT;
  Lib* lib = new Lib;
  Reader r(bytes);
  if (!lib->Load(r)) {
    lib->Release();
    return TYPE_E_CANTLOADLIBRARY;
  }
  lib->file = path;
  *out = lib;
  return S_OK;
}

// A module's TYPELIB resource: the program's own, or a library's.
HRESULT FromModule(const std::wstring& path, int index, ITypeLib** out) {
  WCHAR self[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, self, MAX_PATH);
  HMODULE h = (n && SameName(path, self)) ? GetModuleHandleW(NULL) : LoadLibraryW(path.c_str());
  if (h == NULL) return TYPE_E_CANTLOADLIBRARY;
  HRSRC res = FindResourceW(h, MAKEINTRESOURCEW(index), L"TYPELIB");
  HGLOBAL g = res ? LoadResource(h, res) : NULL;
  const BYTE* p = g ? (const BYTE*)LockResource(g) : NULL;
  if (p == NULL) return TYPE_E_CANTLOADLIBRARY;
  std::vector<BYTE> bytes(p, p + SizeofResource(h, res));
  return FromBytes(bytes, path, out);
}

std::wstring GuidText(REFGUID g) {
  WCHAR s[40];
  StringFromGUID2(g, s, 40);
  return s;
}
std::wstring Hex(unsigned v) {
  WCHAR s[16];
  swprintf(s, 16, L"%x", v);
  return s;
}

LONG SetText(HKEY root, const std::wstring& sub, LPCWSTR name, const std::wstring& value) {
  HKEY k;
  LONG l = RegCreateKeyExW(root, sub.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
  if (l != ERROR_SUCCESS) return l;
  l = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value.c_str(), (DWORD)((value.size() + 1) * sizeof(WCHAR)));
  RegCloseKey(k);
  return l;
}

std::wstring GetText(HKEY root, const std::wstring& sub, LPCWSTR name) {
  HKEY k;
  if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return std::wstring();
  WCHAR buf[1024];
  DWORD cb = sizeof buf - sizeof(WCHAR), type = 0;
  std::wstring s;
  if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS && type == REG_SZ) {
    buf[cb / sizeof(WCHAR)] = 0;
    s = buf;
  }
  RegCloseKey(k);
  return s;
}

void DeleteTree(HKEY root, const std::wstring& sub) {
  HKEY k;
  if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return;
  std::vector<std::wstring> kids;
  WCHAR name[256];
  for (DWORD i = 0;; i++) {
    DWORD cch = 256;
    if (RegEnumKeyExW(k, i, name, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
    kids.push_back(name);
  }
  RegCloseKey(k);
  for (const std::wstring& c : kids) DeleteTree(root, sub + L"\\" + c);
  RegDeleteKeyW(root, sub.c_str());
}

bool HasSubkeys(HKEY root, const std::wstring& sub) {
  HKEY k;
  if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
  WCHAR name[256];
  DWORD cch = 256;
  bool any = RegEnumKeyExW(k, 0, name, &cch, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
  RegCloseKey(k);
  return any;
}

// The interfaces a library registers: dispinterfaces, and interfaces
// Automation can marshal.
bool Registers(const TYPEATTR* a) {
  return a->typekind == TKIND_DISPATCH ||
         (a->typekind == TKIND_INTERFACE && (a->wTypeFlags & (TYPEFLAG_FOLEAUTOMATION | TYPEFLAG_FDUAL)));
}

// The standard dispatch object of CreateStdDispatch: IDispatch on the
// object pvThis, by its type information.
class StdDispatch final : public IDispatch {
public:
  StdDispatch(IUnknown* outer, void* pvThis, ITypeInfo* ti) : outer_(outer), this_(pvThis), ti_(ti) {
    ti_->AddRef();
  }
  // The inner IUnknown, which an aggregate keeps.
  class Inner : public IUnknown {
  public:
    StdDispatch* owner;
    STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
      if (out == NULL) return E_POINTER;
      if (IsEqualIID(riid, IID_IUnknown)) {
        *out = this;
      } else if (IsEqualIID(riid, IID_IDispatch)) {
        *out = static_cast<IDispatch*>(owner);
      } else {
        *out = NULL;
        return E_NOINTERFACE;
      }
      ((IUnknown*)*out)->AddRef();
      return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++owner->refs_; }
    STDMETHOD_(ULONG, Release)() override {
      ULONG n = --owner->refs_;
      if (n == 0) delete owner;
      return n;
    }
  } inner;

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    return outer_ ? outer_->QueryInterface(riid, out) : inner.QueryInterface(riid, out);
  }
  STDMETHOD_(ULONG, AddRef)() override { return outer_ ? outer_->AddRef() : inner.AddRef(); }
  STDMETHOD_(ULONG, Release)() override { return outer_ ? outer_->Release() : inner.Release(); }
  STDMETHOD(GetTypeInfoCount)(UINT* p) override {
    if (p == NULL) return E_INVALIDARG;
    *p = 1;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT i, LCID, ITypeInfo** out) override {
    if (out == NULL) return E_INVALIDARG;
    *out = NULL;
    if (i != 0) return DISP_E_BADINDEX;
    *out = ti_;
    ti_->AddRef();
    return S_OK;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* names, UINT c, LCID, DISPID* ids) override {
    return ti_->GetIDsOfNames(names, c, ids);
  }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* dp, VARIANT* res, EXCEPINFO* ei,
                    UINT* argErr) override {
    return ti_->Invoke(this_, id, flags, dp, res, ei, argErr);
  }

private:
  ~StdDispatch() { ti_->Release(); }
  IUnknown* outer_;
  void* this_;
  ITypeInfo* ti_;
  std::atomic<ULONG> refs_{1};
};

}  // namespace


// Type information between processes goes by value: the library it
// comes from, as the layer saves libraries, and which of its types.
namespace winport {
namespace com {

bool WriteTypeInfo(Writer& w, ITypeInfo* ti) {
  Info* info = NULL;
  if (ti == NULL || FAILED(ti->QueryInterface(kIID_LayerTypeInfo, (void**)&info)) || info == NULL) {
    w.U32(0);
    return ti == NULL;
  }
  Lib* lib = info->lib;
  if (lib->builtin) {
    w.U32(2);  // stdole: the receiver has it
  } else {
    if (lib->creating)
      for (size_t i = 0; i < lib->types.size(); i++) {
        Info layout(lib, i, true);
        layout.LayOutVtable();
      }
    Writer b;
    lib->Save(b);
    w.U32(1);
    w.Bytes(b.b.data(), (uint32_t)b.b.size());
    w.Str(lib->file.c_str());
  }
  w.U32((uint32_t)info->index);
  w.U32(info->ifaceView ? 1 : 0);
  info->Release();
  return true;
}

HRESULT ReadTypeInfo(Reader& r, ITypeInfo** out) {
  *out = NULL;
  uint32_t kind = r.U32();
  if (kind == 0) return r.bad ? winport::com::kBadReply : S_OK;
  Lib* lib = NULL;
  if (kind == 2) {
    lib = StdOle();
    lib->AddRef();
  } else {
    std::vector<BYTE> bytes = r.Bytes();
    std::wstring path = r.Str();
    ITypeLib* tl = NULL;
    if (r.bad || FAILED(FromBytes(bytes, path, &tl))) return winport::com::kBadReply;
    lib = static_cast<Lib*>(tl);
  }
  uint32_t index = r.U32();
  bool view = r.U32() != 0;
  HRESULT hr = S_OK;
  if (r.bad || index >= lib->types.size()) hr = winport::com::kBadReply;
  else *out = lib->NewInfo(index, view);
  lib->Release();
  return hr;
}

}  // namespace com
}  // namespace winport

extern "C" {

HRESULT WINAPI CreateTypeLib2(SYSKIND syskind, LPCOLESTR file, ICreateTypeLib2** out) {
  if (file == NULL || out == NULL) return E_INVALIDARG;
  Lib* lib = new Lib;
  lib->creating = true;
  lib->syskind = syskind;
  lib->file = file;
  *out = static_cast<ICreateTypeLib2*>(lib);
  return S_OK;
}

HRESULT WINAPI CreateTypeLib(SYSKIND syskind, LPCOLESTR file, ICreateTypeLib** out) {
  if (out == NULL) return E_INVALIDARG;
  ICreateTypeLib2* p = NULL;
  HRESULT hr = CreateTypeLib2(syskind, file, &p);
  *out = p;
  return hr;
}

HRESULT WINAPI LoadTypeLib(LPCOLESTR file, ITypeLib** out) {
  if (file == NULL || out == NULL) return E_INVALIDARG;
  *out = NULL;
  std::wstring path = file;
  size_t slash = path.find_last_of(L"\\/");
  std::wstring base = slash == std::wstring::npos ? path : path.substr(slash + 1);
  if (SameName(base, L"stdole2.tlb") || SameName(base, L"stdole32.tlb")) {
    *out = StdOle();
    (*out)->AddRef();
    return S_OK;
  }
  std::vector<BYTE> bytes;
  if (ReadFileBytes(file, &bytes)) {
    if (bytes.size() >= 4 && memcmp(bytes.data(), kMagic, 4) == 0) return FromBytes(bytes, path, out);
    return FromModule(path, 1, out);
  }
  // "module\N": the module's resource N.
  if (slash != std::wstring::npos && slash + 1 < path.size()) {
    std::wstring tail = path.substr(slash + 1);
    bool digits = !tail.empty();
    for (wchar_t c : tail) digits = digits && c >= L'0' && c <= L'9';
    if (digits) return FromModule(path.substr(0, slash), (int)wcstol(tail.c_str(), NULL, 10), out);
  }
  return TYPE_E_CANTLOADLIBRARY;
}

HRESULT WINAPI QueryPathOfRegTypeLib(REFGUID guid, USHORT maj, USHORT min, LCID lcid, BSTR* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = NULL;
  std::wstring key = L"TypeLib\\" + GuidText(guid);
  HKEY k;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
    return TYPE_E_LIBNOTREGISTERED;
  // The version: the same major one, and the latest minor one that is
  // at least the one asked for.
  std::wstring best;
  long bestMinor = -1;
  WCHAR name[256];
  for (DWORD i = 0;; i++) {
    DWORD cch = 256;
    if (RegEnumKeyExW(k, i, name, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
    WCHAR* dot = NULL;
    unsigned long a = wcstoul(name, &dot, 16);
    if (dot == NULL || *dot != L'.') continue;
    unsigned long b = wcstoul(dot + 1, NULL, 16);
    if (a == maj && b >= min && (long)b > bestMinor) {
      bestMinor = (long)b;
      best = name;
    }
  }
  RegCloseKey(k);
  if (best.empty()) return TYPE_E_LIBNOTREGISTERED;
  key += L"\\" + best;
  const std::wstring langs[] = {Hex(lcid), Hex(lcid & 0x3ff), L"0"};
  for (const std::wstring& l : langs)
    for (LPCWSTR plat : {L"win64", L"win32"}) {
      std::wstring p = GetText(HKEY_CLASSES_ROOT, key + L"\\" + l + L"\\" + plat, NULL);
      if (!p.empty()) {
        *out = Bstr(p);
        return S_OK;
      }
    }
  return TYPE_E_LIBNOTREGISTERED;
}

HRESULT WINAPI LoadRegTypeLib(REFGUID guid, WORD maj, WORD min, LCID lcid, ITypeLib** out) {
  if (out == NULL) return E_INVALIDARG;
  *out = NULL;
  if (IsEqualGUID(guid, kStdOleLib)) {
    *out = StdOle();
    (*out)->AddRef();
    return S_OK;
  }
  BSTR path = NULL;
  HRESULT hr = QueryPathOfRegTypeLib(guid, maj, min, lcid, &path);
  if (FAILED(hr)) return hr;
  hr = LoadTypeLib(path, out);
  SysFreeString(path);
  return hr;
}

HRESULT WINAPI RegisterTypeLib(ITypeLib* tl, LPCOLESTR path, LPCOLESTR helpDir) {
  if (tl == NULL || path == NULL) return E_INVALIDARG;
  TLIBATTR* la;
  HRESULT hr = tl->GetLibAttr(&la);
  if (FAILED(hr)) return hr;
  BSTR name = NULL, doc = NULL;
  tl->GetDocumentation(-1, &name, &doc, NULL, NULL);
  std::wstring libid = GuidText(la->guid);
  std::wstring ver = Hex(la->wMajorVerNum) + L"." + Hex(la->wMinorVerNum);
  std::wstring key = L"TypeLib\\" + libid + L"\\" + ver;
  std::wstring desc = doc && *doc ? doc : (name ? name : L"");
  LONG l = SetText(HKEY_CLASSES_ROOT, key, NULL, desc);
  if (l == ERROR_SUCCESS)
    l = SetText(HKEY_CLASSES_ROOT, key + L"\\" + Hex(la->lcid) + (la->syskind == SYS_WIN64 ? L"\\win64" : L"\\win32"),
                NULL, path);
  if (l == ERROR_SUCCESS) {
    WCHAR flags[16];
    swprintf(flags, 16, L"%u", (unsigned)la->wLibFlags);
    SetText(HKEY_CLASSES_ROOT, key + L"\\FLAGS", NULL, flags);
    std::wstring dir;
    if (helpDir) {
      dir = helpDir;
    } else {
      dir = path;
      size_t s = dir.find_last_of(L"\\/");
      dir = s == std::wstring::npos ? std::wstring() : dir.substr(0, s);
    }
    SetText(HKEY_CLASSES_ROOT, key + L"\\HELPDIR", NULL, dir);
  }
  // Its interfaces, marshaled by the type library.
  std::wstring marshaler = GuidText(kUniversalMarshaler);
  UINT n = tl->GetTypeInfoCount();
  for (UINT i = 0; i < n && l == ERROR_SUCCESS; i++) {
    ITypeInfo* ti;
    if (FAILED(tl->GetTypeInfo(i, &ti))) continue;
    TYPEATTR* a;
    if (SUCCEEDED(ti->GetTypeAttr(&a))) {
      if (Registers(a)) {
        BSTR tn = NULL;
        ti->GetDocumentation(MEMBERID_NIL, &tn, NULL, NULL, NULL);
        std::wstring ik = L"Interface\\" + GuidText(a->guid);
        SetText(HKEY_CLASSES_ROOT, ik, NULL, tn ? tn : L"");
        SetText(HKEY_CLASSES_ROOT, ik + L"\\ProxyStubClsid", NULL, marshaler);
        SetText(HKEY_CLASSES_ROOT, ik + L"\\ProxyStubClsid32", NULL, marshaler);
        SetText(HKEY_CLASSES_ROOT, ik + L"\\TypeLib", NULL, libid);
        SetText(HKEY_CLASSES_ROOT, ik + L"\\TypeLib", L"Version", ver);
        SysFreeString(tn);
      }
      ti->ReleaseTypeAttr(a);
    }
    ti->Release();
  }
  SysFreeString(name);
  SysFreeString(doc);
  tl->ReleaseTLibAttr(la);
  return l == ERROR_SUCCESS ? S_OK : TYPE_E_REGISTRYACCESS;
}

HRESULT WINAPI UnRegisterTypeLib(REFGUID guid, WORD maj, WORD min, LCID lcid, SYSKIND) {
  std::wstring libid = GuidText(guid);
  std::wstring ver = Hex(maj) + L"." + Hex(min);
  std::wstring key = L"TypeLib\\" + libid;
  HKEY k;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (key + L"\\" + ver).c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS)
    return TYPE_E_REGISTRYACCESS;
  RegCloseKey(k);
  // The interfaces it registered, when it can still be read.
  ITypeLib* tl = NULL;
  if (SUCCEEDED(LoadRegTypeLib(guid, maj, min, lcid, &tl))) {
    UINT n = tl->GetTypeInfoCount();
    for (UINT i = 0; i < n; i++) {
      ITypeInfo* ti;
      if (FAILED(tl->GetTypeInfo(i, &ti))) continue;
      TYPEATTR* a;
      if (SUCCEEDED(ti->GetTypeAttr(&a))) {
        std::wstring ik = L"Interface\\" + GuidText(a->guid);
        if (Registers(a) && SameName(GetText(HKEY_CLASSES_ROOT, ik + L"\\TypeLib", NULL), libid.c_str()))
          DeleteTree(HKEY_CLASSES_ROOT, ik);
        ti->ReleaseTypeAttr(a);
      }
      ti->Release();
    }
    tl->Release();
  }
  DeleteTree(HKEY_CLASSES_ROOT, key + L"\\" + ver);
  if (!HasSubkeys(HKEY_CLASSES_ROOT, key)) DeleteTree(HKEY_CLASSES_ROOT, key);
  return S_OK;
}

HRESULT WINAPI LoadTypeLibEx(LPCOLESTR file, REGKIND kind, ITypeLib** out) {
  HRESULT hr = LoadTypeLib(file, out);
  if (SUCCEEDED(hr) && kind == REGKIND_REGISTER) {
    WCHAR full[MAX_PATH];
    if (!GetFullPathNameW(file, MAX_PATH, full, NULL)) lstrcpynW(full, file, MAX_PATH);
    hr = RegisterTypeLib(*out, full, NULL);
    if (FAILED(hr)) {
      (*out)->Release();
      *out = NULL;
    }
  }
  return hr;
}

ULONG WINAPI LHashValOfNameSys(SYSKIND syskind, LCID lcid, const OLECHAR* name) {
  if (name == NULL) return 0;
  ULONG h = 0x811c9dc5u;
  for (const OLECHAR* p = name; *p; p++) {
    h ^= (ULONG)towupper(*p);
    h *= 0x01000193u;
  }
  // The low 16 bits are the hash; the rest tell the system and language.
  return (h & 0xffff) | ((ULONG)(syskind & 0xff) << 24) | ((lcid & 0xff) << 16);
}

HRESULT WINAPI DispGetParam(DISPPARAMS* dp, UINT position, VARTYPE vt, VARIANT* out, UINT* argErr) {
  if (dp == NULL || out == NULL) return E_INVALIDARG;
  UINT nPos = dp->cArgs - dp->cNamedArgs;
  int index = -1;
  if (position < nPos) {
    index = (int)(dp->cArgs - 1 - position);
  } else {
    for (UINT k = 0; k < dp->cNamedArgs; k++)
      if (dp->rgdispidNamedArgs[k] == (DISPID)position) index = (int)k;
  }
  if (index < 0) return DISP_E_PARAMNOTFOUND;
  VariantInit(out);
  HRESULT hr = VariantChangeType(out, &dp->rgvarg[index], 0, vt);
  if (FAILED(hr)) {
    if (argErr) *argErr = (UINT)index;
    return hr == DISP_E_OVERFLOW ? hr : DISP_E_TYPEMISMATCH;
  }
  return S_OK;
}

HRESULT WINAPI DispGetIDsOfNames(ITypeInfo* ti, OLECHAR** names, UINT c, DISPID* ids) {
  if (ti == NULL) return E_INVALIDARG;
  return ti->GetIDsOfNames(names, c, ids);
}

HRESULT WINAPI DispInvoke(void* pvThis, ITypeInfo* ti, DISPID id, WORD flags, DISPPARAMS* dp, VARIANT* res,
                          EXCEPINFO* ei, UINT* argErr) {
  if (ti == NULL) return E_INVALIDARG;
  return ti->Invoke(pvThis, id, flags, dp, res, ei, argErr);
}

HRESULT WINAPI CreateStdDispatch(IUnknown* outer, void* pvThis, ITypeInfo* ti, IUnknown** out) {
  if (pvThis == NULL || ti == NULL || out == NULL) return E_INVALIDARG;
  StdDispatch* d = new StdDispatch(outer, pvThis, ti);
  d->inner.owner = d;
  *out = &d->inner;
  return S_OK;
}

}  // extern "C"
