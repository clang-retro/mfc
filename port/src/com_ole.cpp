// com_ole.cpp - proxies and stubs for the interfaces of OLE documents
// between processes (see com_proxy.cpp): IOleObject, IDataObject,
// IPersistStorage, IOleClientSite and IAdviseSink.
//
// Storages and streams go by value: an IStorage argument is sent as the
// compound file it holds, and a storage the object saves into is one
// made for the call, whose contents come back and are copied into the
// caller's. Enumerators come back as copies of what they enumerate.
// Clipboard formats a program registered go by name, since each program
// numbers its own. Monikers and containers do not cross.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "com_wire.h"
#include "ole.h"

#include <ole2.h>
#include <oleidl.h>

#include <vector>

namespace winport {
namespace com {
namespace {

using winport::ole::SafeRelease;

#define PROXY_UNKNOWN                                                                                    \
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override { return pm->QueryInterface(riid, out); } \
  STDMETHOD_(ULONG, AddRef)() override { return pm->AddRef(); }                                          \
  STDMETHOD_(ULONG, Release)() override { return pm->Release(); }

// ---------------------------------------------------------------------
// Values

void PutFormat(Writer& w, CLIPFORMAT cf) {
  w.U16(cf);
  if (cf >= 0xC000) {
    WCHAR name[256];
    int n = GetClipboardFormatNameW(cf, name, 256);
    name[n > 0 ? n : 0] = 0;
    w.Str(name);
  }
}

CLIPFORMAT GetFormat(Reader& r) {
  CLIPFORMAT cf = r.U16();
  if (cf >= 0xC000) {
    std::wstring name = r.Str();
    cf = name.empty() ? 0 : (CLIPFORMAT)RegisterClipboardFormatW(name.c_str());
  }
  return cf;
}

void PutFormatEtc(Writer& w, const FORMATETC* fe) {
  if (fe == NULL) {
    w.U32(0);
    return;
  }
  w.U32(1);
  PutFormat(w, fe->cfFormat);
  if (fe->ptd)
    w.Bytes(fe->ptd, fe->ptd->tdSize);
  else
    w.U32(0xffffffffu);
  w.U32(fe->dwAspect);
  w.U32((uint32_t)fe->lindex);
  w.U32(fe->tymed);
}

// The target device, if there is one, is in task memory.
bool GetFormatEtc(Reader& r, FORMATETC* fe) {
  memset(fe, 0, sizeof *fe);
  if (r.U32() == 0) return false;
  fe->cfFormat = GetFormat(r);
  uint32_t k = r.U32();
  if (k != 0xffffffffu && !r.bad && k >= sizeof(DWORD) && r.n - r.pos >= k) {
    fe->ptd = (DVTARGETDEVICE*)CoTaskMemAlloc(k);
    r.Get(fe->ptd, k);
  } else if (k != 0xffffffffu) {
    r.bad = true;
  }
  fe->dwAspect = r.U32();
  fe->lindex = (LONG)r.U32();
  fe->tymed = r.U32();
  return !r.bad;
}

void FreeFormatEtc(FORMATETC* fe) {
  CoTaskMemFree(fe->ptd);
  fe->ptd = NULL;
}

std::vector<BYTE> GlobalBytes(HGLOBAL h) {
  std::vector<BYTE> v;
  if (h == NULL) return v;
  SIZE_T n = GlobalSize(h);
  const BYTE* p = (const BYTE*)GlobalLock(h);
  if (p) v.assign(p, p + n);
  GlobalUnlock(h);
  return v;
}

HGLOBAL NewGlobal(const std::vector<BYTE>& v) {
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, v.size() ? v.size() : 1);
  if (h == NULL) return NULL;
  if (!v.empty()) {
    memcpy(GlobalLock(h), v.data(), v.size());
    GlobalUnlock(h);
  }
  return h;
}

// A storage as the compound file it is.
bool StorageBytes(IStorage* stg, std::vector<BYTE>* out) {
  out->clear();
  if (stg == NULL) return false;
  ILockBytes* lb = NULL;
  if (FAILED(CreateILockBytesOnHGlobal(NULL, TRUE, &lb))) return false;
  IStorage* copy = NULL;
  bool ok = false;
  if (SUCCEEDED(StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &copy))) {
    ok = SUCCEEDED(stg->CopyTo(0, NULL, NULL, copy)) && SUCCEEDED(copy->Commit(STGC_DEFAULT));
    STATSTG st;
    if (SUCCEEDED(stg->Stat(&st, STATFLAG_NONAME))) copy->SetClass(st.clsid);
    copy->Release();
  }
  STATSTG ls;
  HGLOBAL h = NULL;
  if (ok && SUCCEEDED(lb->Stat(&ls, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromILockBytes(lb, &h))) {
    std::vector<BYTE> all = GlobalBytes(h);
    size_t n = (size_t)ls.cbSize.QuadPart;
    out->assign(all.begin(), all.begin() + (n < all.size() ? n : all.size()));
  }
  lb->Release();
  return ok;
}

// A storage of its own over a copy of those bytes; an empty one for none.
IStorage* StorageOf(const std::vector<BYTE>& bytes) {
  ILockBytes* lb = NULL;
  IStorage* stg = NULL;
  if (bytes.empty()) {
    if (SUCCEEDED(CreateILockBytesOnHGlobal(NULL, TRUE, &lb)))
      StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
  } else {
    HGLOBAL h = NewGlobal(bytes);
    if (h && SUCCEEDED(CreateILockBytesOnHGlobal(h, TRUE, &lb)))
      StgOpenStorageOnILockBytes(lb, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &stg);
  }
  if (lb) lb->Release();
  return stg;
}

void PutStorage(Writer& w, IStorage* stg) {
  std::vector<BYTE> v;
  if (stg == NULL || !StorageBytes(stg, &v)) {
    w.U32(0xffffffffu);
    return;
  }
  w.Bytes(v.data(), (uint32_t)v.size());
}

// NULL for none (or a NULL storage sent).
IStorage* GetStorage(Reader& r) {
  uint32_t k = r.U32();
  if (k == 0xffffffffu || r.bad) return NULL;
  r.pos -= sizeof(uint32_t);
  std::vector<BYTE> v = r.Bytes();
  if (r.bad) return NULL;
  return StorageOf(v);
}

// The contents of a stream, leaving its seek pointer where it was.
std::vector<BYTE> StreamBytes(IStream* s, ULARGE_INTEGER* at) {
  std::vector<BYTE> v;
  LARGE_INTEGER zero = {};
  ULARGE_INTEGER pos = {};
  s->Seek(zero, STREAM_SEEK_CUR, &pos);
  if (at) *at = pos;
  STATSTG st;
  if (FAILED(s->Stat(&st, STATFLAG_NONAME))) return v;
  v.resize((size_t)st.cbSize.QuadPart);
  s->Seek(zero, STREAM_SEEK_SET, NULL);
  ULONG got = 0;
  if (!v.empty()) s->Read(v.data(), (ULONG)v.size(), &got);
  v.resize(got);
  LARGE_INTEGER back;
  back.QuadPart = (LONGLONG)pos.QuadPart;
  s->Seek(back, STREAM_SEEK_SET, NULL);
  return v;
}

// A medium by value; false for one that cannot go (a GDI bitmap).
bool PutMedium(Writer& w, const STGMEDIUM* m, CLIPFORMAT cf) {
  if (m == NULL) {
    w.U32(TYMED_NULL);
    return true;
  }
  w.U32(m->tymed);
  switch (m->tymed) {
    case TYMED_NULL:
      return true;
    case TYMED_HGLOBAL: {
      std::vector<BYTE> v = GlobalBytes(m->hGlobal);
      w.Bytes(v.data(), (uint32_t)v.size());
      return true;
    }
    case TYMED_MFPICT: {
      METAFILEPICT mp = {};
      std::vector<BYTE> bits;
      if (m->hMetaFilePict) {
        if (const METAFILEPICT* p = (const METAFILEPICT*)GlobalLock(m->hMetaFilePict)) {
          mp = *p;
          GlobalUnlock(m->hMetaFilePict);
        }
      }
      if (mp.hMF) {
        UINT n = GetMetaFileBitsEx(mp.hMF, 0, NULL);
        bits.resize(n);
        if (n) GetMetaFileBitsEx(mp.hMF, n, bits.data());
      }
      w.U32((uint32_t)mp.mm);
      w.U32((uint32_t)mp.xExt);
      w.U32((uint32_t)mp.yExt);
      w.Bytes(bits.data(), (uint32_t)bits.size());
      return true;
    }
    case TYMED_ENHMF: {
      std::vector<BYTE> bits;
      if (m->hEnhMetaFile) {
        UINT n = GetEnhMetaFileBits(m->hEnhMetaFile, 0, NULL);
        bits.resize(n);
        if (n) GetEnhMetaFileBits(m->hEnhMetaFile, n, bits.data());
      }
      w.Bytes(bits.data(), (uint32_t)bits.size());
      return true;
    }
    case TYMED_FILE:
      w.Str(m->lpszFileName);
      return true;
    case TYMED_ISTREAM: {
      ULARGE_INTEGER at = {};
      std::vector<BYTE> v;
      if (m->pstm) v = StreamBytes(m->pstm, &at);
      w.Bytes(v.data(), (uint32_t)v.size());
      w.U64(at.QuadPart);
      return true;
    }
    case TYMED_ISTORAGE:
      PutStorage(w, m->pstg);
      return true;
  }
  (void)cf;
  return false;
}

// The medium is the receiver's to release.
HRESULT GetMedium(Reader& r, STGMEDIUM* m) {
  memset(m, 0, sizeof *m);
  DWORD tymed = r.U32();
  if (r.bad) return E_FAIL;
  m->tymed = tymed;
  switch (tymed) {
    case TYMED_NULL:
      return S_OK;
    case TYMED_HGLOBAL: {
      std::vector<BYTE> v = r.Bytes();
      m->hGlobal = NewGlobal(v);
      break;
    }
    case TYMED_MFPICT: {
      METAFILEPICT mp;
      mp.mm = (LONG)r.U32();
      mp.xExt = (LONG)r.U32();
      mp.yExt = (LONG)r.U32();
      std::vector<BYTE> bits = r.Bytes();
      mp.hMF = bits.empty() ? NULL : SetMetaFileBitsEx((UINT)bits.size(), bits.data());
      HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof mp);
      if (h) {
        *(METAFILEPICT*)GlobalLock(h) = mp;
        GlobalUnlock(h);
      }
      m->hMetaFilePict = h;
      break;
    }
    case TYMED_ENHMF: {
      std::vector<BYTE> bits = r.Bytes();
      m->hEnhMetaFile = bits.empty() ? NULL : SetEnhMetaFileBits((UINT)bits.size(), bits.data());
      break;
    }
    case TYMED_FILE:
      m->lpszFileName = r.TaskStr();
      break;
    case TYMED_ISTREAM: {
      std::vector<BYTE> v = r.Bytes();
      uint64_t at = r.U64();
      IStream* s = NULL;
      if (SUCCEEDED(CreateStreamOnHGlobal(NewGlobal(v), TRUE, &s))) {
        ULARGE_INTEGER size;
        size.QuadPart = v.size();
        s->SetSize(size);
        LARGE_INTEGER pos;
        pos.QuadPart = (LONGLONG)at;
        s->Seek(pos, STREAM_SEEK_SET, NULL);
      }
      m->pstm = s;
      break;
    }
    case TYMED_ISTORAGE:
      m->pstg = GetStorage(r);
      break;
    default:
      return DV_E_TYMED;
  }
  if (r.bad) {
    ReleaseStgMedium(m);
    memset(m, 0, sizeof *m);
    return E_FAIL;
  }
  return S_OK;
}

// Puts what came back for GetDataHere into the caller's own medium.
HRESULT FillMedium(STGMEDIUM* dst, STGMEDIUM* got) {
  HRESULT hr = S_OK;
  if (got->tymed != dst->tymed) {
    hr = DV_E_TYMED;
  } else if (dst->tymed == TYMED_HGLOBAL) {
    std::vector<BYTE> v = GlobalBytes(got->hGlobal);
    if (GlobalSize(dst->hGlobal) < v.size()) {
      hr = STG_E_MEDIUMFULL;
    } else if (!v.empty()) {
      memcpy(GlobalLock(dst->hGlobal), v.data(), v.size());
      GlobalUnlock(dst->hGlobal);
    }
  } else if (dst->tymed == TYMED_ISTORAGE) {
    hr = got->pstg && dst->pstg ? got->pstg->CopyTo(0, NULL, NULL, dst->pstg) : E_FAIL;
  } else if (dst->tymed == TYMED_ISTREAM) {
    std::vector<BYTE> v = got->pstm ? StreamBytes(got->pstm, NULL) : std::vector<BYTE>();
    hr = dst->pstm ? dst->pstm->Write(v.data(), (ULONG)v.size(), NULL) : E_FAIL;
  } else {
    hr = DV_E_TYMED;
  }
  ReleaseStgMedium(got);
  return hr;
}

// A medium of the type the caller's is, made for GetDataHere.
HRESULT BlankMedium(DWORD tymed, SIZE_T size, STGMEDIUM* m) {
  memset(m, 0, sizeof *m);
  m->tymed = tymed;
  switch (tymed) {
    case TYMED_HGLOBAL:
      m->hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size ? size : 1);
      return m->hGlobal ? S_OK : E_OUTOFMEMORY;
    case TYMED_ISTREAM:
      return CreateStreamOnHGlobal(NULL, TRUE, &m->pstm);
    case TYMED_ISTORAGE:
      m->pstg = StorageOf(std::vector<BYTE>());
      return m->pstg ? S_OK : E_OUTOFMEMORY;
  }
  return DV_E_TYMED;
}

// ---------------------------------------------------------------------
// IAdviseSink (the methods return nothing: the calls are not waited for)

struct AdviseSinkProxy : ProxyBase, IAdviseSink {
  explicit AdviseSinkProxy(ProxyManager* p) : ProxyBase(p) {}
  PROXY_UNKNOWN
  STDMETHOD_(void, OnDataChange)(FORMATETC* fe, STGMEDIUM* m) override {
    Writer w;
    PutFormatEtc(w, fe);
    if (!PutMedium(w, m, fe ? fe->cfFormat : 0)) {
      w.b.resize(w.b.size() - sizeof(uint32_t));
      w.U32(TYMED_NULL);
    }
    pm->Invoke(IID_IAdviseSink, 3, w, NULL);
  }
  STDMETHOD_(void, OnViewChange)(DWORD aspect, LONG lindex) override {
    Writer w;
    w.U32(aspect);
    w.U32((uint32_t)lindex);
    pm->Invoke(IID_IAdviseSink, 4, w, NULL);
  }
  STDMETHOD_(void, OnRename)(IMoniker*) override { pm->Invoke(IID_IAdviseSink, 5, Writer(), NULL); }
  STDMETHOD_(void, OnSave)() override { pm->Invoke(IID_IAdviseSink, 6, Writer(), NULL); }
  STDMETHOD_(void, OnClose)() override { pm->Invoke(IID_IAdviseSink, 7, Writer(), NULL); }
};

HRESULT AdviseSinkStub(StubCall& c) {
  IAdviseSink* s = (IAdviseSink*)c.itf;
  Reader& r = *c.in;
  switch (c.method) {
    case 3: {
      FORMATETC fe;
      bool haveFe = GetFormatEtc(r, &fe);
      STGMEDIUM m;
      if (FAILED(GetMedium(r, &m))) memset(&m, 0, sizeof m);
      s->OnDataChange(haveFe ? &fe : NULL, &m);
      ReleaseStgMedium(&m);
      FreeFormatEtc(&fe);
      return S_OK;
    }
    case 4: {
      DWORD aspect = r.U32();
      LONG lindex = (LONG)r.U32();
      s->OnViewChange(aspect, lindex);
      return S_OK;
    }
    case 5:
      s->OnRename(NULL);
      return S_OK;
    case 6:
      s->OnSave();
      return S_OK;
    case 7:
      s->OnClose();
      return S_OK;
  }
  return E_NOTIMPL;
}

// ---------------------------------------------------------------------
// IOleClientSite

struct ClientSiteProxy : ProxyBase, IOleClientSite {
  explicit ClientSiteProxy(ProxyManager* p) : ProxyBase(p) {}
  PROXY_UNKNOWN
  STDMETHOD(SaveObject)() override { return pm->Invoke(IID_IOleClientSite, 3, Writer(), NULL); }
  STDMETHOD(GetMoniker)(DWORD, DWORD, IMoniker** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;  // monikers do not cross
  }
  STDMETHOD(GetContainer)(IOleContainer** out) override {
    if (out) *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD(ShowObject)() override { return pm->Invoke(IID_IOleClientSite, 6, Writer(), NULL); }
  STDMETHOD(OnShowWindow)(BOOL show) override {
    Writer w;
    w.U32(show ? 1 : 0);
    return pm->Invoke(IID_IOleClientSite, 7, w, NULL);
  }
  STDMETHOD(RequestNewObjectLayout)() override { return pm->Invoke(IID_IOleClientSite, 8, Writer(), NULL); }
};

HRESULT ClientSiteStub(StubCall& c) {
  IOleClientSite* s = (IOleClientSite*)c.itf;
  switch (c.method) {
    case 3:
      return s->SaveObject();
    case 6:
      return s->ShowObject();
    case 7:
      return s->OnShowWindow(c.in->U32() != 0);
    case 8:
      return s->RequestNewObjectLayout();
  }
  return E_NOTIMPL;
}

// ---------------------------------------------------------------------
// IPersistStorage

struct PersistStorageProxy : ProxyBase, IPersistStorage {
  explicit PersistStorageProxy(ProxyManager* p) : ProxyBase(p) {}
  PROXY_UNKNOWN
  STDMETHOD(GetClassID)(CLSID* out) override {
    if (out == NULL) return E_POINTER;
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IPersistStorage, 3, Writer(), &reply);
    Reader r(reply);
    *out = SUCCEEDED(hr) ? r.Guid() : CLSID_NULL;
    return hr;
  }
  STDMETHOD(IsDirty)() override { return pm->Invoke(IID_IPersistStorage, 4, Writer(), NULL); }
  STDMETHOD(InitNew)(IStorage* stg) override { return WithStorage(5, stg); }
  STDMETHOD(Load)(IStorage* stg) override { return WithStorage(6, stg); }
  STDMETHOD(Save)(IStorage* stg, BOOL same) override {
    if (stg == NULL) return E_INVALIDARG;
    Writer w;
    w.U32(same ? 1 : 0);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IPersistStorage, 7, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    IStorage* saved = GetStorage(r);
    if (saved == NULL) return E_FAIL;
    // The caller's storage keeps its class (OleSave wrote it) unless the
    // object set one.
    STATSTG before, after;
    bool haveBefore = SUCCEEDED(stg->Stat(&before, STATFLAG_NONAME));
    HRESULT hr2 = saved->CopyTo(0, NULL, NULL, stg);
    if (SUCCEEDED(hr2) && haveBefore && SUCCEEDED(saved->Stat(&after, STATFLAG_NONAME)) &&
        IsEqualCLSID(after.clsid, CLSID_NULL))
      stg->SetClass(before.clsid);
    saved->Release();
    return FAILED(hr2) ? hr2 : hr;
  }
  STDMETHOD(SaveCompleted)(IStorage* stg) override { return WithStorage(8, stg); }
  STDMETHOD(HandsOffStorage)() override { return pm->Invoke(IID_IPersistStorage, 9, Writer(), NULL); }

  HRESULT WithStorage(uint32_t method, IStorage* stg) {
    Writer w;
    PutStorage(w, stg);
    return pm->Invoke(IID_IPersistStorage, method, w, NULL);
  }
};

HRESULT PersistStorageStub(StubCall& c) {
  IPersistStorage* p = (IPersistStorage*)c.itf;
  Reader& r = *c.in;
  switch (c.method) {
    case 3: {
      CLSID clsid = CLSID_NULL;
      HRESULT hr = p->GetClassID(&clsid);
      c.out->Guid(clsid);
      return hr;
    }
    case 4:
      return p->IsDirty();
    case 5:
    case 6:
    case 8: {
      // The object has a storage of its own with the caller's contents.
      IStorage* stg = GetStorage(r);
      if (stg == NULL && c.method != 8) return E_OUTOFMEMORY;
      HRESULT hr = c.method == 5 ? p->InitNew(stg) : c.method == 6 ? p->Load(stg) : p->SaveCompleted(stg);
      if (stg) stg->Release();
      return hr;
    }
    case 7: {
      r.U32();  // same as load: the object saves into a new storage regardless
      IStorage* stg = StorageOf(std::vector<BYTE>());
      if (stg == NULL) return E_OUTOFMEMORY;
      HRESULT hr = p->Save(stg, FALSE);
      if (SUCCEEDED(hr)) stg->Commit(STGC_DEFAULT);
      PutStorage(*c.out, SUCCEEDED(hr) ? stg : NULL);
      stg->Release();
      return hr;
    }
    case 9:
      return p->HandsOffStorage();
  }
  return E_NOTIMPL;
}

// ---------------------------------------------------------------------
// IDataObject

struct DataObjectProxy : ProxyBase, IDataObject {
  explicit DataObjectProxy(ProxyManager* p) : ProxyBase(p) {}
  PROXY_UNKNOWN
  STDMETHOD(GetData)(FORMATETC* fe, STGMEDIUM* m) override {
    if (fe == NULL || m == NULL) return E_INVALIDARG;
    memset(m, 0, sizeof *m);
    Writer w;
    PutFormatEtc(w, fe);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDataObject, 3, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    HRESULT hr2 = GetMedium(r, m);
    return FAILED(hr2) ? hr2 : hr;
  }
  STDMETHOD(GetDataHere)(FORMATETC* fe, STGMEDIUM* m) override {
    if (fe == NULL || m == NULL) return E_INVALIDARG;
    Writer w;
    PutFormatEtc(w, fe);
    w.U32(m->tymed);
    w.U64(m->tymed == TYMED_HGLOBAL && m->hGlobal ? GlobalSize(m->hGlobal) : 0);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDataObject, 4, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    STGMEDIUM got;
    HRESULT hr2 = GetMedium(r, &got);
    if (FAILED(hr2)) return hr2;
    hr2 = FillMedium(m, &got);
    return FAILED(hr2) ? hr2 : hr;
  }
  STDMETHOD(QueryGetData)(FORMATETC* fe) override {
    Writer w;
    PutFormatEtc(w, fe);
    return pm->Invoke(IID_IDataObject, 5, w, NULL);
  }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* in, FORMATETC* out) override {
    if (out == NULL) return E_INVALIDARG;
    Writer w;
    PutFormatEtc(w, in);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDataObject, 6, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) {
      GetFormatEtc(r, out);
    } else {
      memset(out, 0, sizeof *out);
    }
    return hr;
  }
  STDMETHOD(SetData)(FORMATETC* fe, STGMEDIUM* m, BOOL release) override {
    Writer w;
    PutFormatEtc(w, fe);
    if (!PutMedium(w, m, fe ? fe->cfFormat : 0)) return DV_E_TYMED;
    HRESULT hr = pm->Invoke(IID_IDataObject, 7, w, NULL);
    if (SUCCEEDED(hr) && release && m) ReleaseStgMedium(m);  // the object has its copy
    return hr;
  }
  STDMETHOD(EnumFormatEtc)(DWORD dir, IEnumFORMATETC** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    Writer w;
    w.U32(dir);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDataObject, 8, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    uint32_t n = r.U32();
    std::vector<FORMATETC> items;
    for (uint32_t i = 0; i < n && !r.bad; i++) {
      FORMATETC fe;
      GetFormatEtc(r, &fe);
      FreeFormatEtc(&fe);  // the copy keeps no target device
      items.push_back(fe);
    }
    *out = new winport::ole::FormatEnum(items);
    return S_OK;
  }
  STDMETHOD(DAdvise)(FORMATETC* fe, DWORD advf, IAdviseSink* sink, DWORD* conn) override {
    if (conn == NULL) return E_POINTER;
    *conn = 0;
    Writer w;
    PutFormatEtc(w, fe);
    w.U32(advf);
    PutInterface(w, sink, IID_IAdviseSink, pm->owner);
    std::vector<BYTE> reply;
    HRESULT hr = pm->Invoke(IID_IDataObject, 9, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) *conn = r.U32();
    return hr;
  }
  STDMETHOD(DUnadvise)(DWORD conn) override {
    Writer w;
    w.U32(conn);
    return pm->Invoke(IID_IDataObject, 10, w, NULL);
  }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA** out) override {
    if (out) *out = NULL;
    return OLE_E_ADVISENOTSUPPORTED;
  }
};

HRESULT DataObjectStub(StubCall& c) {
  IDataObject* d = (IDataObject*)c.itf;
  Reader& r = *c.in;
  Writer& w = *c.out;
  FORMATETC fe;
  bool haveFe = false;
  HRESULT hr = E_NOTIMPL;
  switch (c.method) {
    case 3: {
      haveFe = GetFormatEtc(r, &fe);
      STGMEDIUM m;
      memset(&m, 0, sizeof m);
      hr = d->GetData(haveFe ? &fe : NULL, &m);
      if (SUCCEEDED(hr)) {
        if (!PutMedium(w, &m, fe.cfFormat)) hr = DV_E_TYMED;
        ReleaseStgMedium(&m);
      }
      break;
    }
    case 4: {
      haveFe = GetFormatEtc(r, &fe);
      DWORD tymed = r.U32();
      SIZE_T size = (SIZE_T)r.U64();
      STGMEDIUM m;
      hr = BlankMedium(tymed, size, &m);
      if (SUCCEEDED(hr)) {
        hr = d->GetDataHere(haveFe ? &fe : NULL, &m);
        if (SUCCEEDED(hr) && m.tymed == TYMED_ISTORAGE && m.pstg) m.pstg->Commit(STGC_DEFAULT);
        if (SUCCEEDED(hr) && !PutMedium(w, &m, fe.cfFormat)) hr = DV_E_TYMED;
        ReleaseStgMedium(&m);
      }
      break;
    }
    case 5:
      haveFe = GetFormatEtc(r, &fe);
      hr = d->QueryGetData(haveFe ? &fe : NULL);
      break;
    case 6: {
      haveFe = GetFormatEtc(r, &fe);
      FORMATETC out;
      memset(&out, 0, sizeof out);
      hr = d->GetCanonicalFormatEtc(haveFe ? &fe : NULL, &out);
      PutFormatEtc(w, &out);
      FreeFormatEtc(&out);
      break;
    }
    case 7: {
      haveFe = GetFormatEtc(r, &fe);
      STGMEDIUM m;
      hr = GetMedium(r, &m);
      if (SUCCEEDED(hr)) {
        hr = d->SetData(haveFe ? &fe : NULL, &m, TRUE);
        if (FAILED(hr)) ReleaseStgMedium(&m);
      }
      break;
    }
    case 8: {
      IEnumFORMATETC* e = NULL;
      hr = d->EnumFormatEtc(r.U32(), &e);
      std::vector<FORMATETC> items;
      if (SUCCEEDED(hr) && e) {
        FORMATETC f;
        while (e->Next(1, &f, NULL) == S_OK) {
          items.push_back(f);
          FreeFormatEtc(&items.back());
        }
        e->Release();
      } else if (SUCCEEDED(hr)) {
        hr = E_FAIL;
      }
      w.U32((uint32_t)items.size());
      for (FORMATETC& f : items) PutFormatEtc(w, &f);
      break;
    }
    case 9: {
      haveFe = GetFormatEtc(r, &fe);
      DWORD advf = r.U32();
      IAdviseSink* sink = NULL;
      hr = GetInterface(r, (void**)&sink);
      DWORD conn = 0;
      if (SUCCEEDED(hr)) hr = d->DAdvise(haveFe ? &fe : NULL, advf, sink, &conn);
      SafeRelease(sink);
      w.U32(conn);
      break;
    }
    case 10:
      hr = d->DUnadvise(r.U32());
      break;
  }
  if (haveFe) FreeFormatEtc(&fe);
  return hr;
}

// ---------------------------------------------------------------------
// IOleObject

struct OleObjectProxy : ProxyBase, IOleObject {
  explicit OleObjectProxy(ProxyManager* p) : ProxyBase(p) {}
  PROXY_UNKNOWN
  HRESULT Call(uint32_t method, const Writer& w, std::vector<BYTE>* reply = NULL) {
    return pm->Invoke(IID_IOleObject, method, w, reply);
  }
  STDMETHOD(SetClientSite)(IOleClientSite* site) override {
    Writer w;
    PutInterface(w, site, IID_IOleClientSite, pm->owner);
    return Call(3, w);
  }
  STDMETHOD(GetClientSite)(IOleClientSite** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    std::vector<BYTE> reply;
    HRESULT hr = Call(4, Writer(), &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    HRESULT hr2 = GetInterface(r, (void**)out);
    return FAILED(hr2) ? hr2 : hr;
  }
  STDMETHOD(SetHostNames)(LPCOLESTR app, LPCOLESTR obj) override {
    Writer w;
    w.Str(app);
    w.Str(obj);
    return Call(5, w);
  }
  STDMETHOD(Close)(DWORD save) override {
    Writer w;
    w.U32(save);
    return Call(6, w);
  }
  STDMETHOD(SetMoniker)(DWORD, IMoniker*) override { return E_NOTIMPL; }  // monikers do not cross
  STDMETHOD(GetMoniker)(DWORD, DWORD, IMoniker** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(InitFromData)(IDataObject* data, BOOL creation, DWORD reserved) override {
    Writer w;
    PutInterface(w, data, IID_IDataObject, pm->owner);
    w.U32(creation ? 1 : 0);
    w.U32(reserved);
    return Call(9, w);
  }
  STDMETHOD(GetClipboardData)(DWORD reserved, IDataObject** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    Writer w;
    w.U32(reserved);
    std::vector<BYTE> reply;
    HRESULT hr = Call(10, w, &reply);
    if (FAILED(hr)) return hr;
    Reader r(reply);
    HRESULT hr2 = GetInterface(r, (void**)out);
    return FAILED(hr2) ? hr2 : hr;
  }
  STDMETHOD(DoVerb)(LONG verb, LPMSG msg, IOleClientSite* site, LONG lindex, HWND parent, LPCRECT rc) override {
    Writer w;
    w.U32((uint32_t)verb);
    w.U32(msg ? 1 : 0);
    if (msg) w.Put(msg, sizeof *msg);
    PutInterface(w, site, IID_IOleClientSite, pm->owner);
    w.U32((uint32_t)lindex);
    w.U64((uint64_t)(uintptr_t)parent);
    w.U32(rc ? 1 : 0);
    if (rc) w.Put(rc, sizeof *rc);
    return Call(11, w);
  }
  STDMETHOD(EnumVerbs)(IEnumOLEVERB** out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    std::vector<BYTE> reply;
    HRESULT hr = Call(12, Writer(), &reply);
    if (hr != S_OK) return hr;  // OLE_S_USEREG: the registry's verbs
    Reader r(reply);
    uint32_t n = r.U32();
    std::vector<OLEVERB> verbs;
    std::vector<std::wstring> names;
    names.reserve(n);
    for (uint32_t i = 0; i < n && !r.bad; i++) {
      OLEVERB v;
      v.lVerb = (LONG)r.U32();
      bool isNull;
      names.push_back(r.Str(&isNull));
      v.lpszVerbName = isNull ? NULL : (LPOLESTR)names.back().c_str();
      v.fuFlags = r.U32();
      v.grfAttribs = r.U32();
      verbs.push_back(v);
    }
    *out = new winport::ole::VerbEnum(verbs);  // copies the names
    return S_OK;
  }
  STDMETHOD(Update)() override { return Call(13, Writer()); }
  STDMETHOD(IsUpToDate)() override { return Call(14, Writer()); }
  STDMETHOD(GetUserClassID)(CLSID* out) override {
    if (out == NULL) return E_POINTER;
    std::vector<BYTE> reply;
    HRESULT hr = Call(15, Writer(), &reply);
    Reader r(reply);
    *out = SUCCEEDED(hr) ? r.Guid() : CLSID_NULL;
    return hr;
  }
  STDMETHOD(GetUserType)(DWORD form, LPOLESTR* out) override {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    Writer w;
    w.U32(form);
    std::vector<BYTE> reply;
    HRESULT hr = Call(16, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) *out = r.TaskStr();
    return hr;
  }
  STDMETHOD(SetExtent)(DWORD aspect, SIZEL* size) override {
    if (size == NULL) return E_INVALIDARG;
    Writer w;
    w.U32(aspect);
    w.Put(size, sizeof *size);
    return Call(17, w);
  }
  STDMETHOD(GetExtent)(DWORD aspect, SIZEL* size) override {
    if (size == NULL) return E_INVALIDARG;
    Writer w;
    w.U32(aspect);
    std::vector<BYTE> reply;
    HRESULT hr = Call(18, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) r.Get(size, sizeof *size);
    return hr;
  }
  STDMETHOD(Advise)(IAdviseSink* sink, DWORD* conn) override {
    if (conn == NULL) return E_POINTER;
    *conn = 0;
    Writer w;
    PutInterface(w, sink, IID_IAdviseSink, pm->owner);
    std::vector<BYTE> reply;
    HRESULT hr = Call(19, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) *conn = r.U32();
    return hr;
  }
  STDMETHOD(Unadvise)(DWORD conn) override {
    Writer w;
    w.U32(conn);
    return Call(20, w);
  }
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetMiscStatus)(DWORD aspect, DWORD* status) override {
    if (status == NULL) return E_POINTER;
    *status = 0;
    Writer w;
    w.U32(aspect);
    std::vector<BYTE> reply;
    HRESULT hr = Call(22, w, &reply);
    Reader r(reply);
    if (SUCCEEDED(hr)) *status = r.U32();
    return hr;
  }
  STDMETHOD(SetColorScheme)(LOGPALETTE* pal) override {
    if (pal == NULL) return E_INVALIDARG;
    Writer w;
    size_t n = sizeof(LOGPALETTE) + (pal->palNumEntries ? pal->palNumEntries - 1 : 0) * sizeof(PALETTEENTRY);
    w.Bytes(pal, (uint32_t)n);
    return Call(23, w);
  }
};

HRESULT OleObjectStub(StubCall& c) {
  IOleObject* o = (IOleObject*)c.itf;
  Reader& r = *c.in;
  Writer& w = *c.out;
  switch (c.method) {
    case 3: {
      IOleClientSite* site = NULL;
      HRESULT hr = GetInterface(r, (void**)&site);
      if (FAILED(hr)) return hr;
      hr = o->SetClientSite(site);
      SafeRelease(site);
      return hr;
    }
    case 4: {
      IOleClientSite* site = NULL;
      HRESULT hr = o->GetClientSite(&site);
      PutInterfaceOut(w, hr, site, IID_IOleClientSite, c.peer);
      return hr;
    }
    case 5: {
      bool nullApp, nullObj;
      std::wstring app = r.Str(&nullApp);
      std::wstring obj = r.Str(&nullObj);
      return o->SetHostNames(nullApp ? NULL : app.c_str(), nullObj ? NULL : obj.c_str());
    }
    case 6:
      return o->Close(r.U32());
    case 9: {
      IDataObject* data = NULL;
      HRESULT hr = GetInterface(r, (void**)&data);
      BOOL creation = r.U32() != 0;
      DWORD reserved = r.U32();
      if (SUCCEEDED(hr)) hr = o->InitFromData(data, creation, reserved);
      SafeRelease(data);
      return hr;
    }
    case 10: {
      IDataObject* data = NULL;
      HRESULT hr = o->GetClipboardData(r.U32(), &data);
      PutInterfaceOut(w, hr, data, IID_IDataObject, c.peer);
      return hr;
    }
    case 11: {
      LONG verb = (LONG)r.U32();
      MSG msg;
      bool haveMsg = r.U32() != 0;
      if (haveMsg) r.Get(&msg, sizeof msg);
      IOleClientSite* site = NULL;
      HRESULT hr = GetInterface(r, (void**)&site);
      LONG lindex = (LONG)r.U32();
      HWND parent = (HWND)(uintptr_t)r.U64();
      RECT rc;
      bool haveRc = r.U32() != 0;
      if (haveRc) r.Get(&rc, sizeof rc);
      if (SUCCEEDED(hr)) hr = o->DoVerb(verb, haveMsg ? &msg : NULL, site, lindex, parent, haveRc ? &rc : NULL);
      SafeRelease(site);
      return hr;
    }
    case 12: {
      IEnumOLEVERB* e = NULL;
      HRESULT hr = o->EnumVerbs(&e);
      std::vector<OLEVERB> verbs;
      if (hr == S_OK && e) {
        OLEVERB v;
        while (e->Next(1, &v, NULL) == S_OK) verbs.push_back(v);
      }
      if (e) e->Release();
      w.U32((uint32_t)verbs.size());
      for (OLEVERB& v : verbs) {
        w.U32((uint32_t)v.lVerb);
        w.Str(v.lpszVerbName);
        w.U32(v.fuFlags);
        w.U32(v.grfAttribs);
        CoTaskMemFree(v.lpszVerbName);
      }
      return hr;
    }
    case 13:
      return o->Update();
    case 14:
      return o->IsUpToDate();
    case 15: {
      CLSID clsid = CLSID_NULL;
      HRESULT hr = o->GetUserClassID(&clsid);
      w.Guid(clsid);
      return hr;
    }
    case 16: {
      LPOLESTR s = NULL;
      HRESULT hr = o->GetUserType(r.U32(), &s);
      w.Str(SUCCEEDED(hr) ? s : NULL);
      CoTaskMemFree(s);
      return hr;
    }
    case 17: {
      DWORD aspect = r.U32();
      SIZEL size;
      r.Get(&size, sizeof size);
      return o->SetExtent(aspect, &size);
    }
    case 18: {
      SIZEL size = {0, 0};
      HRESULT hr = o->GetExtent(r.U32(), &size);
      w.Put(&size, sizeof size);
      return hr;
    }
    case 19: {
      IAdviseSink* sink = NULL;
      HRESULT hr = GetInterface(r, (void**)&sink);
      DWORD conn = 0;
      if (SUCCEEDED(hr)) hr = o->Advise(sink, &conn);
      SafeRelease(sink);
      w.U32(conn);
      return hr;
    }
    case 20:
      return o->Unadvise(r.U32());
    case 22: {
      DWORD status = 0;
      HRESULT hr = o->GetMiscStatus(r.U32(), &status);
      w.U32(status);
      return hr;
    }
    case 23: {
      std::vector<BYTE> pal = r.Bytes();
      if (pal.size() < sizeof(LOGPALETTE)) return E_INVALIDARG;
      return o->SetColorScheme((LOGPALETTE*)pal.data());
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

const ProxyInfo kOleProxies[] = {
    {&IID_IOleObject, Make<OleObjectProxy, IOleObject>, OleObjectStub},
    {&IID_IDataObject, Make<DataObjectProxy, IDataObject>, DataObjectStub},
    {&IID_IPersistStorage, Make<PersistStorageProxy, IPersistStorage>, PersistStorageStub},
    {&IID_IOleClientSite, Make<ClientSiteProxy, IOleClientSite>, ClientSiteStub},
    {&IID_IAdviseSink, Make<AdviseSinkProxy, IAdviseSink>, AdviseSinkStub},
};

}  // namespace

const ProxyInfo* FindOleProxyInfo(REFIID iid) {
  for (const ProxyInfo& p : kOleProxies)
    if (IsEqualIID(*p.iid, iid)) return &p;
  return NULL;
}

}  // namespace com
}  // namespace winport
