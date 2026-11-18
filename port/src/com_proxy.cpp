// com_proxy.cpp - the proxies and stubs of the interfaces COM between
// processes (com_remote.cpp) can call: for each, the proxy writes a
// method's [in] arguments and reads its [out] ones, and the stub reads
// the [in] ones, calls the object and writes the [out] ones. Methods
// are numbered by their place in the vtable.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "com_wire.h"

#include <oaidl.h>
#include <ole2.h>
#include <oleauto.h>

#include <vector>

namespace winport {
namespace com {

// ---------------------------------------------------------------------
// Strings

void Writer::Bstr(BSTR s) {
  if (s == NULL) {
    U32(0xffffffffu);
    return;
  }
  Bytes(s, SysStringByteLen(s));
}

std::wstring Reader::Str(bool* isNull) {
  uint32_t k = U32();
  if (isNull) *isNull = k == 0xffffffffu;
  if (k == 0xffffffffu || bad) return std::wstring();
  if (n - pos < k) {
    bad = true;
    return std::wstring();
  }
  std::wstring s((const OLECHAR*)(p + pos), k / sizeof(OLECHAR));
  pos += k;
  return s;
}

LPOLESTR Reader::TaskStr() {
  bool isNull;
  std::wstring s = Str(&isNull);
  if (isNull || bad) return NULL;
  LPOLESTR out = (LPOLESTR)CoTaskMemAlloc((s.size() + 1) * sizeof(OLECHAR));
  if (out) memcpy(out, s.c_str(), (s.size() + 1) * sizeof(OLECHAR));
  return out;
}

BSTR Reader::Bstr() {
  uint32_t k = U32();
  if (k == 0xffffffffu || bad) return NULL;
  if (n - pos < k) {
    bad = true;
    return NULL;
  }
  BSTR s = SysAllocStringByteLen((LPCSTR)(p + pos), k);
  pos += k;
  return s;
}

// ---------------------------------------------------------------------
// VARIANTs

namespace {

// The size of a value of a plain type; 0 for one sent another way.
size_t PlainSize(VARTYPE vt) {
  switch (vt) {
    case VT_I1:
    case VT_UI1:
      return 1;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
      return 2;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_R4:
    case VT_ERROR:
      return 4;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8:
      return 8;
  }
  return 0;
}

// One element of type vt at p.
bool PutElement(Writer& w, VARTYPE vt, const void* p, DWORD to) {
  if (size_t k = PlainSize(vt)) {
    w.Put(p, k);
    return true;
  }
  switch (vt) {
    case VT_BSTR:
      w.Bstr(*(BSTR*)p);
      return true;
    case VT_UNKNOWN:
      PutInterface(w, *(IUnknown**)p, IID_IUnknown, to);
      return true;
    case VT_DISPATCH:
      PutInterface(w, *(IDispatch**)p, IID_IDispatch, to);
      return true;
    case VT_VARIANT:
      return PutVariant(w, (const VARIANT*)p, to);
    case VT_DECIMAL:
      w.Put(p, sizeof(DECIMAL));
      return true;
  }
  return false;
}

HRESULT GetElement(Reader& r, VARTYPE vt, void* p) {
  if (size_t k = PlainSize(vt)) {
    r.Get(p, k);
    return r.bad ? E_FAIL : S_OK;
  }
  switch (vt) {
    case VT_BSTR:
      *(BSTR*)p = r.Bstr();
      return r.bad ? E_FAIL : S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
      return GetInterface(r, (void**)p);
    case VT_VARIANT:
      return GetVariant(r, (VARIANT*)p);
    case VT_DECIMAL:
      r.Get(p, sizeof(DECIMAL));
      return r.bad ? E_FAIL : S_OK;
  }
  return DISP_E_BADVARTYPE;
}

bool PutArray(Writer& w, VARTYPE vt, SAFEARRAY* sa, DWORD to) {
  if (sa == NULL) {
    w.U32(0xffffffffu);
    return true;
  }
  w.U32(sa->cDims);
  ULONG count = 1;
  for (USHORT d = 0; d < sa->cDims; d++) {
    w.U32(sa->rgsabound[d].cElements);
    w.U32((uint32_t)sa->rgsabound[d].lLbound);
    count *= sa->rgsabound[d].cElements;
  }
  const BYTE* data = (const BYTE*)sa->pvData;
  for (ULONG i = 0; i < count; i++)
    if (!PutElement(w, vt, data + i * sa->cbElements, to)) return false;
  return true;
}

HRESULT GetArray(Reader& r, VARTYPE vt, SAFEARRAY** out) {
  *out = NULL;
  uint32_t dims = r.U32();
  if (dims == 0xffffffffu) return S_OK;
  if (r.bad || dims == 0 || dims > 64) return E_FAIL;
  std::vector<SAFEARRAYBOUND> b(dims);
  ULONG count = 1;
  for (uint32_t d = 0; d < dims; d++) {
    b[d].cElements = r.U32();
    b[d].lLbound = (LONG)r.U32();
    count *= b[d].cElements;
  }
  if (r.bad) return E_FAIL;
  SAFEARRAY* sa = SafeArrayCreate(vt, dims, b.data());
  if (sa == NULL) return E_OUTOFMEMORY;
  BYTE* data = (BYTE*)sa->pvData;
  for (ULONG i = 0; i < count; i++) {
    HRESULT hr = GetElement(r, vt, data + i * sa->cbElements);
    if (FAILED(hr)) {
      SafeArrayDestroy(sa);
      return hr;
    }
  }
  *out = sa;
  return S_OK;
}

}  // namespace

bool PutVariant(Writer& w, const VARIANT* v, DWORD to) {
  VARTYPE vt = v->vt;
  w.U16(vt);
  if (vt & VT_BYREF) {
    // A reference inside a VARIANT goes as its value.
    VARTYPE base = vt & ~VT_BYREF;
    if (base & VT_ARRAY) return PutArray(w, base & VT_TYPEMASK, *v->pparray, to);
    if (base == VT_VARIANT) return PutVariant(w, v->pvarVal, to);
    return PutElement(w, base, v->byref, to);
  }
  if (vt == VT_EMPTY || vt == VT_NULL) return true;
  if (vt & VT_ARRAY) return PutArray(w, vt & VT_TYPEMASK, v->parray, to);
  if (vt == VT_DECIMAL) return PutElement(w, vt, &v->decVal, to);
  return PutElement(w, vt, &v->llVal, to);
}

HRESULT GetVariant(Reader& r, VARIANT* v) {
  VariantInit(v);
  VARTYPE vt = r.U16();
  if (r.bad) return E_FAIL;
  VARTYPE base = vt & ~VT_BYREF;  // a reference arrives as its value
  HRESULT hr = S_OK;
  if (base == VT_EMPTY || base == VT_NULL) {
  } else if (base & VT_ARRAY) {
    hr = GetArray(r, base & VT_TYPEMASK, &v->parray);
  } else if (base == VT_VARIANT) {
    VARIANT inner;
    hr = GetVariant(r, &inner);
    if (SUCCEEDED(hr)) {
      *v = inner;
      return S_OK;
    }
  } else if (base == VT_DECIMAL) {
    hr = GetElement(r, base, &v->decVal);
  } else {
    hr = GetElement(r, base, &v->llVal);
  }
  if (FAILED(hr)) {
    VariantInit(v);
    return hr;
  }
  v->vt = base;
  return S_OK;
}

namespace {

// ---------------------------------------------------------------------
// IClassFactory

struct ClassFactoryProxy : ProxyBase, IClassFactory {
  explicit ClassFactoryProxy(ProxyManager* p) : ProxyBase(p) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override { return pm->QueryInterface(riid, out); }
  STDMETHOD_(ULONG, AddRef)() override { return pm->AddRef(); }
  STDMETHOD_(ULONG, Release)() override { return pm->Release(); }
  STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    if (outer != NULL) return CLASS_E_NOAGGREGATION;
    Writer w;
    w.Guid(riid);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IClassFactory, 3, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    HRESULT hr2 = GetInterface(r, out);
    return FAILED(hr2) ? hr2 : *out ? hr : E_NOINTERFACE;
  }
  STDMETHOD(LockServer)(BOOL lock) override {
    Writer w;
    w.U32(lock ? 1 : 0);
    return pm->Invoke(IID_IClassFactory, 4, w, NULL);
  }
};

HRESULT ClassFactoryStub(StubCall& c) {
  IClassFactory* cf = (IClassFactory*)c.itf;
  switch (c.method) {
    case 3: {
      IID iid = c.in->Guid();
      void* p = NULL;
      HRESULT hr = cf->CreateInstance(NULL, iid, &p);
      PutInterfaceOut(*c.out, hr, (IUnknown*)p, iid, c.peer);
      return hr;
    }
    case 4:
      return cf->LockServer(c.in->U32() != 0);
  }
  return E_NOTIMPL;
}

// ---------------------------------------------------------------------
// IDispatch

// The value a VT_BYREF argument points to, as a VARIANT of its type.
bool PutReferenced(Writer& w, const VARIANT* v, DWORD to) {
  VARIANT tmp;
  VARTYPE base = v->vt & ~VT_BYREF;
  if (base == VT_VARIANT) return PutVariant(w, v->pvarVal, to);
  VariantInit(&tmp);
  tmp.vt = base;
  if (base & VT_ARRAY)
    tmp.parray = *v->pparray;
  else if (base == VT_DECIMAL)
    tmp.decVal = *v->pdecVal;
  else if (size_t k = PlainSize(base))
    memcpy(&tmp.llVal, v->byref, k);
  else if (base == VT_BSTR || base == VT_UNKNOWN || base == VT_DISPATCH)
    tmp.byref = *(void**)v->byref;
  else
    return false;
  return PutVariant(w, &tmp, to);
}

// Puts a returned value where a VT_BYREF argument points, freeing what
// was there.
void SetReferenced(VARIANT* v, VARIANT* value) {
  VARTYPE base = v->vt & ~VT_BYREF;
  if (base == VT_VARIANT) {
    VariantClear(v->pvarVal);
    *v->pvarVal = *value;
    return;
  }
  if (value->vt != base) {
    VariantClear(value);
    return;
  }
  if (base & VT_ARRAY) {
    if (*v->pparray) SafeArrayDestroy(*v->pparray);
    *v->pparray = value->parray;
  } else if (base == VT_DECIMAL) {
    *v->pdecVal = value->decVal;
  } else if (size_t k = PlainSize(base)) {
    memcpy(v->byref, &value->llVal, k);
  } else if (base == VT_BSTR) {
    SysFreeString(*v->pbstrVal);
    *v->pbstrVal = value->bstrVal;
  } else if (base == VT_UNKNOWN || base == VT_DISPATCH) {
    if (*v->ppunkVal) (*v->ppunkVal)->Release();
    *v->ppunkVal = value->punkVal;
  }
}

struct DispatchProxy : ProxyBase, IDispatch {
  explicit DispatchProxy(ProxyManager* p) : ProxyBase(p) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override { return pm->QueryInterface(riid, out); }
  STDMETHOD_(ULONG, AddRef)() override { return pm->AddRef(); }
  STDMETHOD_(ULONG, Release)() override { return pm->Release(); }
  STDMETHOD(GetTypeInfoCount)(UINT* n) override {
    if (n == NULL) return E_POINTER;
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDispatch, 3, Writer(), &reply);
    Reader r(reply);
    *n = SUCCEEDED(hr) ? r.U32() : 0;
    return hr;
  }
  // Type information does not cross: there is no proxy for ITypeInfo.
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* names, UINT count, LCID lcid, DISPID* ids) override {
    if ((names == NULL || ids == NULL) && count) return E_POINTER;
    Writer w;
    w.U32(count);
    for (UINT i = 0; i < count; i++) w.Str(names[i]);
    w.U32(lcid);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDispatch, 5, w, &reply);
    Reader r(reply);
    for (UINT i = 0; i < count; i++) {
      ids[i] = (DISPID)r.U32();
      if (r.bad) ids[i] = DISPID_UNKNOWN;
    }
    return hr;
  }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID lcid, WORD flags, DISPPARAMS* params, VARIANT* result,
                    EXCEPINFO* excep, UINT* argErr) override {
    DISPPARAMS none = {NULL, NULL, 0, 0};
    if (params == NULL) params = &none;
    Writer w;
    w.U32((uint32_t)id);
    w.U32(lcid);
    w.U16(flags);
    w.U32(params->cArgs);
    for (UINT i = 0; i < params->cArgs; i++) {
      VARIANT* a = &params->rgvarg[i];
      w.U16(a->vt);
      bool ok = (a->vt & VT_BYREF) ? PutReferenced(w, a, pm->owner) : PutVariant(w, a, pm->owner);
      if (!ok) {
        if (argErr) *argErr = i;
        return DISP_E_BADVARTYPE;
      }
    }
    w.U32(params->cNamedArgs);
    for (UINT i = 0; i < params->cNamedArgs; i++) w.U32((uint32_t)params->rgdispidNamedArgs[i]);
    w.U32((result ? 1 : 0) | (excep ? 2 : 0) | (argErr ? 4 : 0));
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDispatch, 6, w, &reply);
    if (reply.empty()) return hr;
    Reader r(reply);
    VARIANT v;
    if (result) {
      VariantInit(result);
      if (SUCCEEDED(GetVariant(r, &v))) *result = v;
    }
    if (excep) {
      memset(excep, 0, sizeof *excep);
      excep->wCode = r.U16();
      excep->scode = (SCODE)r.U32();
      excep->dwHelpContext = r.U32();
      excep->bstrSource = r.Bstr();
      excep->bstrDescription = r.Bstr();
      excep->bstrHelpFile = r.Bstr();
    }
    UINT err = r.U32();
    if (argErr) *argErr = err;
    for (UINT i = 0; i < params->cArgs; i++) {
      VARIANT* a = &params->rgvarg[i];
      if (!(a->vt & VT_BYREF)) continue;
      if (FAILED(GetVariant(r, &v))) break;
      SetReferenced(a, &v);
    }
    return hr;
  }
};

HRESULT DispatchStub(StubCall& c) {
  IDispatch* d = (IDispatch*)c.itf;
  Reader& r = *c.in;
  Writer& w = *c.out;
  switch (c.method) {
    case 3: {
      UINT n = 0;
      HRESULT hr = d->GetTypeInfoCount(&n);
      w.U32(n);
      return hr;
    }
    case 5: {
      UINT count = r.U32();
      if (r.bad || count > 4096) return E_INVALIDARG;
      std::vector<std::wstring> names(count);
      std::vector<LPOLESTR> ptrs(count);
      for (UINT i = 0; i < count; i++) {
        names[i] = r.Str();
        ptrs[i] = (LPOLESTR)names[i].c_str();
      }
      LCID lcid = r.U32();
      std::vector<DISPID> ids(count, DISPID_UNKNOWN);
      HRESULT hr = d->GetIDsOfNames(IID_NULL, ptrs.data(), count, lcid, ids.data());
      for (UINT i = 0; i < count; i++) w.U32((uint32_t)ids[i]);
      return hr;
    }
    case 6: {
      DISPID id = (DISPID)r.U32();
      LCID lcid = r.U32();
      WORD flags = r.U16();
      UINT cArgs = r.U32();
      if (r.bad || cArgs > 4096) return E_INVALIDARG;
      // The arguments, and the values the VT_BYREF ones point to.
      std::vector<VARIANT> args(cArgs), values(cArgs);
      HRESULT hr = S_OK;
      for (UINT i = 0; i < cArgs; i++) {
        VariantInit(&args[i]);
        VariantInit(&values[i]);
        VARTYPE vt = r.U16();
        if (FAILED(hr)) continue;
        hr = GetVariant(r, &values[i]);
        if (FAILED(hr)) continue;
        if (vt & VT_BYREF) {
          args[i].vt = vt;
          VARTYPE base = vt & ~VT_BYREF;
          if (base == VT_VARIANT)
            args[i].pvarVal = &values[i];
          else if (base == VT_DECIMAL)
            args[i].pdecVal = &values[i].decVal;
          else
            args[i].byref = &values[i].llVal;  // every value is at the start of the union
        } else {
          args[i] = values[i];
          VariantInit(&values[i]);
        }
      }
      UINT cNamed = r.U32();
      if (r.bad || cNamed > cArgs) hr = E_INVALIDARG;
      std::vector<DISPID> named(cNamed ? cNamed : 1);
      for (UINT i = 0; i < cNamed && !r.bad; i++) named[i] = (DISPID)r.U32();
      uint32_t want = r.U32();
      VARIANT result;
      VariantInit(&result);
      EXCEPINFO excep;
      memset(&excep, 0, sizeof excep);
      UINT argErr = 0;
      if (SUCCEEDED(hr)) {
        DISPPARAMS params = {args.data(), named.data(), cArgs, cNamed};
        hr = d->Invoke(id, IID_NULL, lcid, flags, &params, (want & 1) ? &result : NULL,
                       (want & 2) ? &excep : NULL, (want & 4) ? &argErr : NULL);
        if (hr == DISP_E_EXCEPTION && excep.pfnDeferredFillIn) excep.pfnDeferredFillIn(&excep);
      }
      if (want & 1) {
        if (!PutVariant(w, &result, c.peer)) {
          w.b.resize(w.b.size() - sizeof(VARTYPE));
          w.U16(VT_EMPTY);
        }
      }
      if (want & 2) {
        w.U16(excep.wCode);
        w.U32((uint32_t)excep.scode);
        w.U32(excep.dwHelpContext);
        w.Bstr(excep.bstrSource);
        w.Bstr(excep.bstrDescription);
        w.Bstr(excep.bstrHelpFile);
      }
      w.U32(argErr);
      for (UINT i = 0; i < cArgs; i++) {
        if (args[i].vt & VT_BYREF) {
          if (!PutVariant(w, &values[i], c.peer)) break;
        }
      }
      VariantClear(&result);
      SysFreeString(excep.bstrSource);
      SysFreeString(excep.bstrDescription);
      SysFreeString(excep.bstrHelpFile);
      for (UINT i = 0; i < cArgs; i++) {
        if (!(args[i].vt & VT_BYREF)) VariantClear(&args[i]);
        VariantClear(&values[i]);
      }
      return hr;
    }
  }
  return E_NOTIMPL;
}

template <class P, class I>
ProxyBase* Make(ProxyManager* pm, IUnknown** itf) {
  P* p = new P(pm);
  *itf = static_cast<I*>(p);
  return p;
}

const ProxyInfo kProxies[] = {
    {&IID_IClassFactory, Make<ClassFactoryProxy, IClassFactory>, ClassFactoryStub},
    {&IID_IDispatch, Make<DispatchProxy, IDispatch>, DispatchStub},
};

}  // namespace

const ProxyInfo* FindProxyInfo(REFIID iid) {
  for (const ProxyInfo& p : kProxies)
    if (IsEqualIID(*p.iid, iid)) return &p;
  return FindOleProxyInfo(iid);
}

}  // namespace com
}  // namespace winport
