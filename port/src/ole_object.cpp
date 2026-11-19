// ole_object.cpp - OLE objects in a container: the default handler and
// its presentation cache, the advise holders, and OleCreate, OleLoad,
// OleSave, OleRun and their friends.
//
// A container holds an object through the default handler. The handler
// stands for the object while it is only loaded: it keeps the object's
// storage, and its cache keeps the object's pictures (saved in the
// storage's "\2OlePres000" streams), so that the container can draw it
// and ask its extent without the server. Running it (OleRun, DoVerb)
// creates the server's object (CoCreateInstance: a class object the
// program registered, or an InprocServer32 library), hands it the
// storage and client site, and connects the handler to it: the cache
// follows its data, and the container's advise sinks get its news.
//
// There is no second process here: a server is in the program, as the
// program registered it, or in a library.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "ole.h"

#include <wctype.h>

#include <map>
#include <string>
#include <vector>

namespace winport {
namespace ole {

// ---------------------------------------------------------------------
// Copying data

HANDLE DuplicateData(HANDLE h, CLIPFORMAT cf, UINT flags) {
  if (!h) return NULL;
  if (!flags) flags = GMEM_MOVEABLE;
  switch (cf) {
    case CF_METAFILEPICT: {
      METAFILEPICT* src = (METAFILEPICT*)GlobalLock(h);
      if (!src) return NULL;
      HGLOBAL out = GlobalAlloc(flags, sizeof(METAFILEPICT));
      if (out) {
        METAFILEPICT* dst = (METAFILEPICT*)GlobalLock(out);
        *dst = *src;
        dst->hMF = CopyMetaFileW(src->hMF, NULL);
        GlobalUnlock(out);
      }
      GlobalUnlock(h);
      return out;
    }
    case CF_ENHMETAFILE: return CopyEnhMetaFileW((HENHMETAFILE)h, NULL);
    case CF_BITMAP: return CopyImage(h, IMAGE_BITMAP, 0, 0, 0);
    case CF_PALETTE: {
      WORD n = 0;
      if (!GetObjectW(h, sizeof n, &n) || n == 0) return NULL;
      std::vector<BYTE> buf(sizeof(LOGPALETTE) + n * sizeof(PALETTEENTRY));
      LOGPALETTE* lp = (LOGPALETTE*)buf.data();
      lp->palVersion = 0x300;
      lp->palNumEntries = n;
      GetPaletteEntries((HPALETTE)h, 0, n, lp->palPalEntry);
      return CreatePalette(lp);
    }
  }
  SIZE_T size = GlobalSize(h);
  HGLOBAL out = GlobalAlloc(flags, size);
  if (!out) return NULL;
  void* s = GlobalLock(h);
  void* d = GlobalLock(out);
  if (s && d) memcpy(d, s, size);
  GlobalUnlock(out);
  GlobalUnlock(h);
  return out;
}

// A copy of a medium the copy owns.
HRESULT CopyMedium(STGMEDIUM* dst, const STGMEDIUM* src, CLIPFORMAT cf) {
  memset(dst, 0, sizeof *dst);
  dst->tymed = src->tymed;
  switch (src->tymed) {
    case TYMED_NULL: return S_OK;
    case TYMED_HGLOBAL: dst->hGlobal = DuplicateData(src->hGlobal, cf == CF_METAFILEPICT ? 0 : cf, 0); break;
    case TYMED_MFPICT: dst->hMetaFilePict = DuplicateData(src->hMetaFilePict, CF_METAFILEPICT, 0); break;
    case TYMED_ENHMF: dst->hEnhMetaFile = CopyEnhMetaFileW(src->hEnhMetaFile, NULL); break;
    case TYMED_GDI: dst->hBitmap = (HBITMAP)DuplicateData(src->hBitmap, cf == CF_PALETTE ? CF_PALETTE : CF_BITMAP, 0); break;
    case TYMED_ISTREAM:
      dst->pstm = src->pstm;
      if (dst->pstm) dst->pstm->AddRef();
      return S_OK;
    case TYMED_ISTORAGE:
      dst->pstg = src->pstg;
      if (dst->pstg) dst->pstg->AddRef();
      return S_OK;
    default: dst->tymed = TYMED_NULL; return DV_E_TYMED;
  }
  return dst->hGlobal ? S_OK : E_OUTOFMEMORY;
}

}  // namespace ole
}  // namespace winport

namespace {

using namespace winport::ole;

// ---------------------------------------------------------------------
// Advise holders

class OleAdviseHolder final : public IOleAdviseHolder {
 public:
  ~OleAdviseHolder() {
    for (IAdviseSink*& s : sinks_) SafeRelease(s);
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleAdviseHolder)) {
      *out = static_cast<IOleAdviseHolder*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IOleAdviseHolder)
  STDMETHOD(Advise)(IAdviseSink* sink, DWORD* conn) override {
    if (!conn) return E_POINTER;
    *conn = 0;
    if (!sink) return E_INVALIDARG;
    sink->AddRef();
    for (size_t i = 0; i < sinks_.size(); i++)
      if (!sinks_[i]) {
        sinks_[i] = sink;
        *conn = (DWORD)i + 1;
        return S_OK;
      }
    sinks_.push_back(sink);
    *conn = (DWORD)sinks_.size();
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD conn) override {
    if (conn == 0 || conn > sinks_.size() || !sinks_[conn - 1]) return OLE_E_NOCONNECTION;
    SafeRelease(sinks_[conn - 1]);
    return S_OK;
  }
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** out) override {
    if (!out) return E_POINTER;
    std::vector<STATDATA> v;
    for (size_t i = 0; i < sinks_.size(); i++)
      if (sinks_[i]) {
        STATDATA s = {};
        s.pAdvSink = sinks_[i];
        s.dwConnection = (DWORD)i + 1;
        v.push_back(s);
      }
    *out = new StatDataEnum(v);
    return S_OK;
  }
  STDMETHOD(SendOnRename)(IMoniker* mk) override {
    for (IAdviseSink* s : Snapshot()) s->OnRename(mk), s->Release();
    return S_OK;
  }
  STDMETHOD(SendOnSave)() override {
    for (IAdviseSink* s : Snapshot()) s->OnSave(), s->Release();
    return S_OK;
  }
  STDMETHOD(SendOnClose)() override {
    for (IAdviseSink* s : Snapshot()) s->OnClose(), s->Release();
    return S_OK;
  }

 private:
  // The sinks, held, so that one may unadvise while told.
  std::vector<IAdviseSink*> Snapshot() {
    std::vector<IAdviseSink*> v;
    for (IAdviseSink* s : sinks_)
      if (s) s->AddRef(), v.push_back(s);
    return v;
  }
  std::vector<IAdviseSink*> sinks_;
};

class DataAdviseHolder final : public IDataAdviseHolder {
 public:
  ~DataAdviseHolder() {
    for (STATDATA& s : entries_) SafeRelease(s.pAdvSink);
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataAdviseHolder)) {
      *out = static_cast<IDataAdviseHolder*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IDataAdviseHolder)
  STDMETHOD(Advise)(IDataObject* data, FORMATETC* fe, DWORD advf, IAdviseSink* sink, DWORD* conn) override {
    if (conn) *conn = 0;
    if (!fe || !sink || !conn) return E_INVALIDARG;
    STATDATA s = {};
    s.formatetc = *fe;
    s.formatetc.ptd = NULL;
    s.advf = advf;
    s.pAdvSink = sink;
    s.dwConnection = next_++;
    sink->AddRef();
    entries_.push_back(s);
    *conn = s.dwConnection;
    if ((advf & ADVF_PRIMEFIRST) && data) {
      Send(data, s);
      if (advf & ADVF_ONLYONCE) Unadvise(s.dwConnection);
    }
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD conn) override {
    for (size_t i = 0; i < entries_.size(); i++)
      if (entries_[i].dwConnection == conn) {
        IAdviseSink* s = entries_[i].pAdvSink;
        entries_.erase(entries_.begin() + i);
        if (s) s->Release();
        return S_OK;
      }
    return OLE_E_NOCONNECTION;
  }
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** out) override {
    if (!out) return E_POINTER;
    *out = new StatDataEnum(entries_);
    return S_OK;
  }
  STDMETHOD(SendOnDataChange)(IDataObject* data, DWORD, DWORD advf) override {
    std::vector<STATDATA> now = entries_;
    for (STATDATA& s : now) s.pAdvSink->AddRef();
    for (STATDATA& s : now) {
      STATDATA t = s;
      t.advf |= advf;
      Send(data, t);
      if (s.advf & ADVF_ONLYONCE) Unadvise(s.dwConnection);
    }
    for (STATDATA& s : now) s.pAdvSink->Release();
    return S_OK;
  }

 private:
  static void Send(IDataObject* data, const STATDATA& s) {
    FORMATETC fe = s.formatetc;
    STGMEDIUM m = {};
    if (!(s.advf & ADVF_NODATA) && data) {
      if (FAILED(data->GetData(&fe, &m))) memset(&m, 0, sizeof m);
    }
    s.pAdvSink->OnDataChange(&fe, &m);
    if (m.tymed != TYMED_NULL) ReleaseStgMedium(&m);
  }
  std::vector<STATDATA> entries_;
  DWORD next_ = 1;
};

// ---------------------------------------------------------------------
// Streams of a storage, whole

HRESULT ReadStream(IStorage* stg, const wchar_t* name, std::vector<BYTE>* out) {
  IStream* s = NULL;
  HRESULT hr = stg->OpenStream(name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &s);
  if (FAILED(hr)) return hr;
  out->clear();
  BYTE buf[8192];
  ULONG n = 0;
  while (SUCCEEDED(s->Read(buf, sizeof buf, &n)) && n > 0) out->insert(out->end(), buf, buf + n);
  s->Release();
  return S_OK;
}

HRESULT WriteStream(IStorage* stg, const wchar_t* name, const std::vector<BYTE>& v) {
  IStream* s = NULL;
  HRESULT hr = stg->CreateStream(name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &s);
  if (FAILED(hr)) return hr;
  ULONG n = 0;
  hr = v.empty() ? S_OK : s->Write(v.data(), (ULONG)v.size(), &n);
  s->Release();
  return hr;
}

void PutD(std::vector<BYTE>& v, DWORD d) {
  for (int i = 0; i < 4; i++) v.push_back((BYTE)(d >> (8 * i)));
}
DWORD GetD(const std::vector<BYTE>& v, size_t* pos, bool* ok) {
  if (*pos + 4 > v.size()) {
    *ok = false;
    return 0;
  }
  DWORD d = v[*pos] | (v[*pos + 1] << 8) | (v[*pos + 2] << 16) | ((DWORD)v[*pos + 3] << 24);
  *pos += 4;
  return d;
}

// ---------------------------------------------------------------------
// The registry's word on a class (HKEY_CLASSES_ROOT\CLSID\{...})

std::wstring ClassKey(REFCLSID clsid, const wchar_t* sub) {
  WCHAR guid[40];
  StringFromGUID2(clsid, guid, 40);
  std::wstring k = std::wstring(L"CLSID\\") + guid;
  if (sub && *sub) k += std::wstring(L"\\") + sub;
  return k;
}

bool ClassValue(REFCLSID clsid, const wchar_t* sub, std::wstring* out) {
  WCHAR buf[1024];
  LONG cb = sizeof buf;
  std::wstring key = ClassKey(clsid, sub);
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), buf, &cb) != ERROR_SUCCESS) return false;
  *out = buf;
  return true;
}

}  // namespace

// ---------------------------------------------------------------------
// The default handler

namespace {

class DefaultHandler;

// The handler's ears on the running object: its IOleObject news, and the
// data its cache follows.
class HandlerSink final : public IAdviseSink {
 public:
  explicit HandlerSink(DefaultHandler* h) : h_(h) {}
  void Detach() { h_ = NULL; }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAdviseSink)) {
      *out = static_cast<IAdviseSink*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IAdviseSink)
  STDMETHOD_(void, OnDataChange)(FORMATETC* fe, STGMEDIUM* m) override;
  STDMETHOD_(void, OnViewChange)(DWORD aspect, LONG lindex) override;
  STDMETHOD_(void, OnRename)(IMoniker* mk) override;
  STDMETHOD_(void, OnSave)() override;
  STDMETHOD_(void, OnClose)() override;

 private:
  DefaultHandler* h_;
};

// The classes of static pictures.
const CLSID kStaticMetafile = {0x00000315, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const CLSID kStaticDib = {0x00000316, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const CLSID kStaticEnhMetafile = {0x00000319, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
// The class of a link's storage.
const CLSID kStdOleLink = {0x00000300, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
bool IsStaticClass(REFCLSID c) {
  return IsEqualCLSID(c, kStaticMetafile) || IsEqualCLSID(c, kStaticDib) || IsEqualCLSID(c, kStaticEnhMetafile);
}

struct CacheEntry {
  FORMATETC fe = {};
  DWORD advf = 0;
  DWORD conn = 0;        // the container's connection
  DWORD serverConn = 0;  // ours on the running object
  STGMEDIUM data = {};   // tymed TYMED_NULL while empty
  SIZEL extent = {0, 0};
};

class DefaultHandler final : public IOleObject,
                             public IDataObject,
                             public IPersistStorage,
                             public IViewObject2,
                             public IRunnableObject,
                             public IOleLink {
 public:
  explicit DefaultHandler(REFCLSID clsid, bool link = false) : clsid_(clsid), cache_(this), link_(link) {
    sink_ = new HandlerSink(this);
    CreateOleAdviseHolder(&oleAdvise_);
  }
  ~DefaultHandler() {
    Stop();
    sink_->Detach();
    sink_->Release();
    for (CacheEntry& e : entries_)
      if (e.data.tymed != TYMED_NULL) ReleaseStgMedium(&e.data);
    for (STATDATA& s : dataAdvise_) SafeRelease(s.pAdvSink);
    SafeRelease(viewSink_);
    SafeRelease(site_);
    SafeRelease(storage_);
    SafeRelease(oleAdvise_);
    SafeRelease(source_);
  }

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleObject))
      *out = static_cast<IOleObject*>(this);
    else if (IsEqualIID(riid, IID_IDataObject))
      *out = static_cast<IDataObject*>(this);
    else if (IsEqualIID(riid, IID_IPersistStorage) || IsEqualIID(riid, IID_IPersist))
      *out = static_cast<IPersistStorage*>(this);
    else if (IsEqualIID(riid, IID_IViewObject) || IsEqualIID(riid, IID_IViewObject2))
      *out = static_cast<IViewObject2*>(this);
    else if (IsEqualIID(riid, IID_IRunnableObject))
      *out = static_cast<IRunnableObject*>(this);
    else if (IsEqualIID(riid, IID_IOleCache) || IsEqualIID(riid, IID_IOleCache2))
      *out = static_cast<IOleCache2*>(&cache_);
    else if (link_ && IsEqualIID(riid, IID_IOleLink))
      *out = static_cast<IOleLink*>(this);
    if (*out) {
      AddRef();
      return S_OK;
    }
    // Anything else is the running object's (in-place activation, its
    // documents and items).
    if (server_) return server_->QueryInterface(riid, out);
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  // IOleObject
  STDMETHOD(SetClientSite)(IOleClientSite* site) override {
    Assign(site_, site);
    // A link's source has its own (it is not the container's).
    if (object_ && !link_) return object_->SetClientSite(site);
    return S_OK;
  }
  STDMETHOD(GetClientSite)(IOleClientSite** out) override {
    if (!out) return E_POINTER;
    *out = site_;
    if (site_) site_->AddRef();
    return S_OK;
  }
  STDMETHOD(SetHostNames)(LPCOLESTR app, LPCOLESTR obj) override {
    hostApp_ = app ? app : L"";
    hostObj_ = obj ? obj : L"";
    haveHostNames_ = true;
    if (object_ && !link_) return object_->SetHostNames(app, obj);
    return S_OK;
  }
  STDMETHOD(Close)(DWORD saveOption) override {
    if (!object_) return S_OK;
    if (link_) {
      // A link lets its source go; the source stays open for its own.
      Stop();
      return S_OK;
    }
    HRESULT hr = object_->Close(saveOption);
    Stop();
    return hr;
  }
  STDMETHOD(SetMoniker)(DWORD which, IMoniker* mk) override {
    if (object_) return object_->SetMoniker(which, mk);
    return S_OK;
  }
  STDMETHOD(GetMoniker)(DWORD assign, DWORD which, IMoniker** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (object_ && !link_) return object_->GetMoniker(assign, which, out);
    if (site_) return site_->GetMoniker(assign, which, out);
    return E_FAIL;
  }
  STDMETHOD(InitFromData)(IDataObject* data, BOOL creation, DWORD reserved) override {
    if (!object_) return OLE_E_NOTRUNNING;
    return object_->InitFromData(data, creation, reserved);
  }
  STDMETHOD(GetClipboardData)(DWORD reserved, IDataObject** out) override {
    if (!object_) return OLE_E_NOTRUNNING;
    return object_->GetClipboardData(reserved, out);
  }
  STDMETHOD(DoVerb)(LONG verb, LPMSG msg, IOleClientSite* site, LONG lindex, HWND parent, LPCRECT rc) override {
    HRESULT hr = Run(NULL);
    if (FAILED(hr)) return hr;
    return object_->DoVerb(verb, msg, site, lindex, parent, rc);
  }
  STDMETHOD(EnumVerbs)(IEnumOLEVERB** out) override {
    if (IsStaticClass(clsid_)) return OLEOBJ_E_NOVERBS;
    if (object_) {
      HRESULT hr = object_->EnumVerbs(out);
      if (hr != OLE_S_USEREG) return hr;
    }
    return OleRegEnumVerbs(clsid_, out);
  }
  STDMETHOD(Update)() override {
    if (link_) return Update((IBindCtx*)NULL);
    if (!object_) return OLE_E_NOTRUNNING;
    HRESULT hr = object_->Update();
    if (SUCCEEDED(hr)) cache_.UpdateCache(data_, UPDFCACHE_ALL, NULL);
    return hr;
  }
  STDMETHOD(IsUpToDate)() override { return object_ ? object_->IsUpToDate() : OLE_E_NOTRUNNING; }
  STDMETHOD(GetUserClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    if (object_) return object_->GetUserClassID(out);
    *out = clsid_;
    return S_OK;
  }
  STDMETHOD(GetUserType)(DWORD form, LPOLESTR* out) override {
    if (IsStaticClass(clsid_) && out) {
      *out = CoStrDup(IsEqualCLSID(clsid_, kStaticDib) ? L"Picture (Device Independent Bitmap)"
                      : IsEqualCLSID(clsid_, kStaticEnhMetafile) ? L"Picture (Enhanced Metafile)"
                                                                 : L"Picture (Metafile)");
      return S_OK;
    }
    if (object_) {
      HRESULT hr = object_->GetUserType(form, out);
      if (hr != OLE_S_USEREG) return hr;
    }
    return OleRegGetUserType(clsid_, form, out);
  }
  STDMETHOD(SetExtent)(DWORD aspect, SIZEL* size) override {
    if (!object_) return OLE_E_NOTRUNNING;
    return object_->SetExtent(aspect, size);
  }
  STDMETHOD(GetExtent)(DWORD aspect, SIZEL* size) override {
    if (!size) return E_POINTER;
    if (object_ && SUCCEEDED(object_->GetExtent(aspect, size))) return S_OK;
    return GetExtent(aspect, -1, NULL, size);
  }
  STDMETHOD(Advise)(IAdviseSink* sink, DWORD* conn) override { return oleAdvise_->Advise(sink, conn); }
  STDMETHOD(Unadvise)(DWORD conn) override { return oleAdvise_->Unadvise(conn); }
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** out) override { return oleAdvise_->EnumAdvise(out); }
  STDMETHOD(GetMiscStatus)(DWORD aspect, DWORD* out) override {
    if (!out) return E_POINTER;
    if (IsStaticClass(clsid_)) {
      *out = OLEMISC_STATIC | OLEMISC_CANTLINKINSIDE;
      return S_OK;
    }
    if (object_) {
      HRESULT hr = object_->GetMiscStatus(aspect, out);
      if (hr != OLE_S_USEREG) return hr;
    }
    return OleRegGetMiscStatus(clsid_, aspect, out);
  }
  STDMETHOD(SetColorScheme)(LOGPALETTE* pal) override {
    return object_ ? object_->SetColorScheme(pal) : OLE_E_NOTRUNNING;
  }

  // IDataObject: the running object's, or the cache's
  STDMETHOD(GetData)(FORMATETC* fe, STGMEDIUM* m) override {
    if (!fe || !m) return E_INVALIDARG;
    if (data_ && SUCCEEDED(data_->GetData(fe, m))) return S_OK;
    return CacheGetData(fe, m);
  }
  STDMETHOD(GetDataHere)(FORMATETC* fe, STGMEDIUM* m) override {
    if (data_) return data_->GetDataHere(fe, m);
    return OLE_E_NOTRUNNING;
  }
  STDMETHOD(QueryGetData)(FORMATETC* fe) override {
    if (!fe) return E_INVALIDARG;
    if (data_ && data_->QueryGetData(fe) == S_OK) return S_OK;
    return FindData(*fe) ? S_OK : S_FALSE;
  }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* in, FORMATETC* out) override {
    if (data_) return data_->GetCanonicalFormatEtc(in, out);
    if (!in || !out) return E_INVALIDARG;
    *out = *in;
    out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
  }
  STDMETHOD(SetData)(FORMATETC* fe, STGMEDIUM* m, BOOL release) override {
    if (!data_) return OLE_E_NOTRUNNING;
    return data_->SetData(fe, m, release);
  }
  STDMETHOD(EnumFormatEtc)(DWORD dir, IEnumFORMATETC** out) override {
    if (data_) {
      HRESULT hr = data_->EnumFormatEtc(dir, out);
      if (hr != OLE_S_USEREG) return hr;
    }
    if (!out) return E_POINTER;
    std::vector<FORMATETC> v;
    for (CacheEntry& e : entries_) v.push_back(e.fe);
    *out = new FormatEnum(v);
    return S_OK;
  }
  STDMETHOD(DAdvise)(FORMATETC* fe, DWORD advf, IAdviseSink* sink, DWORD* conn) override {
    if (!fe || !sink || !conn) return E_INVALIDARG;
    STATDATA s = {};
    s.formatetc = *fe;
    s.formatetc.ptd = NULL;
    s.advf = advf;
    s.pAdvSink = sink;
    s.dwConnection = nextData_++;
    sink->AddRef();
    if (data_) data_->DAdvise(&s.formatetc, advf, sink, &serverData_[s.dwConnection]);
    dataAdvise_.push_back(s);
    *conn = s.dwConnection;
    return S_OK;
  }
  STDMETHOD(DUnadvise)(DWORD conn) override {
    for (size_t i = 0; i < dataAdvise_.size(); i++)
      if (dataAdvise_[i].dwConnection == conn) {
        if (data_ && serverData_[conn]) data_->DUnadvise(serverData_[conn]);
        serverData_.erase(conn);
        SafeRelease(dataAdvise_[i].pAdvSink);
        dataAdvise_.erase(dataAdvise_.begin() + i);
        return S_OK;
      }
    return OLE_E_NOCONNECTION;
  }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA** out) override {
    if (!out) return E_POINTER;
    *out = new StatDataEnum(dataAdvise_);
    return S_OK;
  }

  // IPersistStorage
  STDMETHOD(GetClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    *out = link_ ? kStdOleLink : clsid_;
    return S_OK;
  }
  STDMETHOD(IsDirty)() override {
    if (persist_) return persist_->IsDirty();
    return dirty_ ? S_OK : S_FALSE;
  }
  STDMETHOD(InitNew)(IStorage* stg) override {
    if (!stg) return E_INVALIDARG;
    Assign(storage_, stg);
    loaded_ = false;
    dirty_ = true;
    if (persist_) return persist_->InitNew(stg);
    return S_OK;
  }
  STDMETHOD(Load)(IStorage* stg) override {
    if (!stg) return E_INVALIDARG;
    Assign(storage_, stg);
    loaded_ = true;
    dirty_ = false;
    LoadCache(stg);
    if (link_) return ReadLinkStream(stg);
    if (persist_) return persist_->Load(stg);
    return S_OK;
  }
  STDMETHOD(Save)(IStorage* stg, BOOL sameAsLoad) override {
    if (!stg) return E_INVALIDARG;
    HRESULT hr = S_OK;
    if (persist_) {
      hr = persist_->Save(stg, sameAsLoad);
      if (FAILED(hr)) return hr;
    } else if (!sameAsLoad && storage_ && storage_ != stg) {
      // Not running: what the object saved before, as it is.
      hr = storage_->CopyTo(0, NULL, NULL, stg);
      if (FAILED(hr)) return hr;
    }
    if (link_) {
      hr = WriteLinkStream(stg);
      if (FAILED(hr)) return hr;
    } else {
      WriteOleStream(stg);
    }
    SaveCache(stg);
    return S_OK;
  }
  STDMETHOD(SaveCompleted)(IStorage* stg) override {
    if (stg) Assign(storage_, stg);
    dirty_ = false;
    if (persist_) return persist_->SaveCompleted(stg);
    return S_OK;
  }
  STDMETHOD(HandsOffStorage)() override {
    SafeRelease(storage_);
    if (persist_) return persist_->HandsOffStorage();
    return S_OK;
  }

  // IViewObject2: drawn from the cache
  STDMETHOD(Draw)(DWORD aspect, LONG lindex, void*, DVTARGETDEVICE*, HDC, HDC hdc, LPCRECTL bounds, LPCRECTL,
                  BOOL(STDMETHODCALLTYPE*)(ULONG_PTR), ULONG_PTR) override {
    if (!hdc || !bounds) return E_INVALIDARG;
    // A running object that draws itself does.
    if (server_) {
      IViewObject* v = NULL;
      if (SUCCEEDED(server_->QueryInterface(IID_IViewObject, (void**)&v)) && v != static_cast<IViewObject*>(this)) {
        HRESULT hr = v->Draw(aspect, lindex, NULL, NULL, NULL, hdc, bounds, NULL, NULL, 0);
        v->Release();
        if (SUCCEEDED(hr)) return hr;
      } else if (v) {
        v->Release();
      }
    }
    CacheEntry* e = Presentation(aspect);
    if (!e) return OLE_E_BLANK;
    RECT rc = {bounds->left, bounds->top, bounds->right, bounds->bottom};
    return DrawMedium(hdc, rc, e->fe.cfFormat, e->data) ? S_OK : VIEW_E_DRAW;
  }
  STDMETHOD(GetColorSet)(DWORD, LONG, void*, DVTARGETDEVICE*, HDC, LOGPALETTE** out) override {
    if (out) *out = NULL;
    return S_FALSE;
  }
  STDMETHOD(Freeze)(DWORD, LONG, void*, DWORD* out) override {
    if (out) *out = 0;
    return E_NOTIMPL;
  }
  STDMETHOD(Unfreeze)(DWORD) override { return E_NOTIMPL; }
  STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, IAdviseSink* sink) override {
    Assign(viewSink_, sink);
    viewAspects_ = aspects;
    viewAdvf_ = advf;
    if (sink && (advf & ADVF_PRIMEFIRST)) sink->OnViewChange(aspects, -1);
    return S_OK;
  }
  STDMETHOD(GetAdvise)(DWORD* aspects, DWORD* advf, IAdviseSink** sink) override {
    if (aspects) *aspects = viewAspects_;
    if (advf) *advf = viewAdvf_;
    if (sink) {
      *sink = viewSink_;
      if (viewSink_) viewSink_->AddRef();
    }
    return S_OK;
  }
  STDMETHOD(GetExtent)(DWORD aspect, LONG, DVTARGETDEVICE*, LPSIZEL size) override {
    if (!size) return E_POINTER;
    CacheEntry* e = Presentation(aspect);
    if (!e) return OLE_E_BLANK;
    *size = e->extent;
    return S_OK;
  }

  // IRunnableObject
  STDMETHOD(GetRunningClass)(LPCLSID out) override {
    if (!out) return E_INVALIDARG;
    *out = clsid_;
    return S_OK;
  }
  STDMETHOD(Run)(LPBINDCTX bc) override {
    if (server_) return S_OK;
    if (link_) {
      // A link runs its source: whatever its moniker binds to.
      if (!source_) return OLE_E_CANT_BINDTOSOURCE;
      IBindCtx* own = NULL;
      if (!bc && FAILED(CreateBindCtx(0, &own))) return E_OUTOFMEMORY;
      IUnknown* unk = NULL;
      HRESULT hr = source_->BindToObject(bc ? bc : own, NULL, IID_IUnknown, (void**)&unk);
      SafeRelease(own);
      if (FAILED(hr)) return hr;
      return Connect(unk);
    }
    if (IsStaticClass(clsid_)) return OLE_E_STATIC;
    IUnknown* unk = NULL;
    HRESULT hr = CoCreateInstance(clsid_, NULL, CLSCTX_SERVER, IID_IUnknown, (void**)&unk);
    if (FAILED(hr)) return hr;
    return Connect(unk);
  }
  STDMETHOD_(BOOL, IsRunning)() override { return server_ != NULL; }
  STDMETHOD(LockRunning)(BOOL lock, BOOL lastUnlockCloses) override {
    if (lock) {
      locks_++;
      return S_OK;
    }
    if (locks_ > 0 && --locks_ == 0 && lastUnlockCloses && object_) return Close(OLECLOSE_SAVEIFDIRTY);
    return S_OK;
  }
  STDMETHOD(SetContainedObject)(BOOL contained) override {
    contained_ = contained != FALSE;
    return S_OK;
  }

  // IOleLink
  STDMETHOD(SetUpdateOptions)(DWORD opt) override {
    if (opt != OLEUPDATE_ALWAYS && opt != OLEUPDATE_ONCALL) return E_INVALIDARG;
    updateOpt_ = opt;
    dirty_ = true;
    return S_OK;
  }
  STDMETHOD(GetUpdateOptions)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = updateOpt_;
    return S_OK;
  }
  STDMETHOD(SetSourceMoniker)(IMoniker* mk, REFCLSID clsid) override {
    if (server_) Stop();
    Assign(source_, mk);
    if (!IsEqualCLSID(clsid, CLSID_NULL)) clsid_ = clsid;
    dirty_ = true;
    return S_OK;
  }
  STDMETHOD(GetSourceMoniker)(IMoniker** out) override {
    if (!out) return E_POINTER;
    *out = source_;
    if (!source_) return MK_E_UNAVAILABLE;
    source_->AddRef();
    return S_OK;
  }
  STDMETHOD(SetSourceDisplayName)(LPCOLESTR name) override {
    if (!name) return E_INVALIDARG;
    IBindCtx* bc = NULL;
    HRESULT hr = CreateBindCtx(0, &bc);
    if (FAILED(hr)) return hr;
    ULONG eaten = 0;
    IMoniker* mk = NULL;
    hr = MkParseDisplayName(bc, name, &eaten, &mk);
    bc->Release();
    if (FAILED(hr)) return hr;
    hr = SetSourceMoniker(mk, CLSID_NULL);
    mk->Release();
    return hr;
  }
  STDMETHOD(GetSourceDisplayName)(LPOLESTR* out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!source_) return MK_E_UNAVAILABLE;
    IBindCtx* bc = NULL;
    HRESULT hr = CreateBindCtx(0, &bc);
    if (FAILED(hr)) return hr;
    hr = source_->GetDisplayName(bc, NULL, out);
    bc->Release();
    return hr;
  }
  STDMETHOD(BindToSource)(DWORD, IBindCtx* bc) override { return Run(bc); }
  STDMETHOD(BindIfRunning)() override {
    if (server_) return S_OK;
    if (!source_) return OLE_E_CANT_BINDTOSOURCE;
    IBindCtx* bc = NULL;
    HRESULT hr = CreateBindCtx(0, &bc);
    if (FAILED(hr)) return hr;
    hr = source_->IsRunning(bc, NULL, NULL) == S_OK ? Run(bc) : S_FALSE;
    bc->Release();
    return hr;
  }
  STDMETHOD(GetBoundSource)(IUnknown** out) override {
    if (!out) return E_POINTER;
    *out = server_;
    if (!server_) return E_FAIL;
    server_->AddRef();
    return S_OK;
  }
  STDMETHOD(UnbindSource)() override {
    Stop();
    return S_OK;
  }
  STDMETHOD(Update)(IBindCtx* bc) override {
    HRESULT hr = Run(bc);
    if (FAILED(hr)) return hr;
    cache_.UpdateCache(data_, UPDFCACHE_ALL, NULL);
    return S_OK;
  }

  // Takes an object already made (it is running) as the one it handles.
  HRESULT Connect(IUnknown* unk) {
    server_ = unk;
    unk->QueryInterface(IID_IOleObject, (void**)&object_);
    unk->QueryInterface(IID_IDataObject, (void**)&data_);
    if (link_) {
      // The source keeps itself: only its news and its data.
      CLSID c;
      if (object_ && SUCCEEDED(object_->GetUserClassID(&c))) clsid_ = c;
      if (object_) object_->Advise(sink_, &oleConn_);
      if (data_) {
        for (STATDATA& s : dataAdvise_)
          data_->DAdvise(&s.formatetc, s.advf, s.pAdvSink, &serverData_[s.dwConnection]);
        for (CacheEntry& e : entries_) AdviseCache(e);
      }
      return S_OK;
    }
    unk->QueryInterface(IID_IPersistStorage, (void**)&persist_);
    HRESULT hr = S_OK;
    DWORD misc = 0;
    if (object_) object_->GetMiscStatus(DVASPECT_CONTENT, &misc);
    if (object_ && site_ && (misc & OLEMISC_SETCLIENTSITEFIRST)) object_->SetClientSite(site_);
    if (persist_ && storage_) {
      hr = loaded_ ? persist_->Load(storage_) : persist_->InitNew(storage_);
      if (FAILED(hr)) {
        Stop();
        return hr;
      }
    }
    if (object_) {
      if (site_ && !(misc & OLEMISC_SETCLIENTSITEFIRST)) object_->SetClientSite(site_);
      if (haveHostNames_) object_->SetHostNames(hostApp_.c_str(), hostObj_.c_str());
      object_->Advise(sink_, &oleConn_);
    }
    if (data_) {
      for (STATDATA& s : dataAdvise_) data_->DAdvise(&s.formatetc, s.advf, s.pAdvSink, &serverData_[s.dwConnection]);
      for (CacheEntry& e : entries_) AdviseCache(e);
    }
    return S_OK;
  }

  // The running object goes (it closed, or was told to).
  void Stop() {
    if (!server_) return;
    if (data_) {
      for (CacheEntry& e : entries_)
        if (e.serverConn) data_->DUnadvise(e.serverConn), e.serverConn = 0;
      for (auto& c : serverData_)
        if (c.second) data_->DUnadvise(c.second);
    }
    serverData_.clear();
    if (object_ && oleConn_) object_->Unadvise(oleConn_);
    oleConn_ = 0;
    if (object_ && !link_) object_->SetClientSite(NULL);
    SafeRelease(persist_);
    SafeRelease(data_);
    SafeRelease(object_);
    SafeRelease(server_);
  }

  // The running object's news, from the sink.
  void DataChanged(FORMATETC* fe, STGMEDIUM* m) {
    if (!fe || !m || m->tymed == TYMED_NULL) return;
    for (CacheEntry& e : entries_)
      if (SameFormat(e.fe, *fe) || (e.fe.cfFormat == 0 && e.fe.dwAspect == fe->dwAspect)) {
        StoreData(e, fe->cfFormat, m);
        dirty_ = true;
        NotifyView(e.fe.dwAspect);
        break;
      }
  }
  void Saved() { oleAdvise_->SendOnSave(); }
  void Closed() {
    AddRef();
    Stop();
    oleAdvise_->SendOnClose();
    Release();
  }
  void Renamed(IMoniker* mk) { oleAdvise_->SendOnRename(mk); }
  void ViewChanged(DWORD aspect, LONG lindex) {
    if (viewSink_ && (viewAspects_ & aspect)) viewSink_->OnViewChange(aspect, lindex);
  }

 private:
  // The cache, as a part of its own (its SetData is not the object's).
  class CachePart final : public IOleCache2 {
   public:
    explicit CachePart(DefaultHandler* h) : h_(h) {}
    STDMETHOD(QueryInterface)(REFIID riid, void** out) override { return h_->QueryInterface(riid, out); }
    STDMETHOD_(ULONG, AddRef)() override { return h_->AddRef(); }
    STDMETHOD_(ULONG, Release)() override { return h_->Release(); }
    STDMETHOD(Cache)(FORMATETC* fe, DWORD advf, DWORD* conn) override { return h_->AddCache(fe, advf, conn); }
    STDMETHOD(Uncache)(DWORD conn) override { return h_->RemoveCache(conn); }
    STDMETHOD(EnumCache)(IEnumSTATDATA** out) override {
      if (!out) return E_POINTER;
      std::vector<STATDATA> v;
      for (CacheEntry& e : h_->entries_) {
        STATDATA s = {};
        s.formatetc = e.fe;
        s.advf = e.advf;
        s.dwConnection = e.conn;
        v.push_back(s);
      }
      *out = new StatDataEnum(v);
      return S_OK;
    }
    STDMETHOD(InitCache)(IDataObject* data) override { return UpdateCache(data, UPDFCACHE_ALL, NULL); }
    STDMETHOD(SetData)(FORMATETC* fe, STGMEDIUM* m, BOOL release) override {
      if (!fe || !m) return E_INVALIDARG;
      for (CacheEntry& e : h_->entries_)
        if (SameFormat(e.fe, *fe) || (e.fe.cfFormat == 0 && e.fe.dwAspect == fe->dwAspect)) {
          h_->StoreData(e, fe->cfFormat, m);
          if (release) ReleaseStgMedium(m);
          h_->dirty_ = true;
          return S_OK;
        }
      return OLE_E_BLANK;
    }
    STDMETHOD(UpdateCache)(LPDATAOBJECT data, DWORD, LPVOID) override {
      if (!data) return OLE_E_NOTRUNNING;
      bool any = false;
      for (CacheEntry& e : h_->entries_) {
        FORMATETC fe = h_->Requested(e);
        STGMEDIUM m = {};
        if (SUCCEEDED(data->GetData(&fe, &m))) {
          h_->StoreData(e, fe.cfFormat, &m);
          ReleaseStgMedium(&m);
          any = true;
          h_->NotifyView(e.fe.dwAspect);
        }
      }
      if (any) h_->dirty_ = true;
      return any || h_->entries_.empty() ? S_OK : CACHE_E_NOCACHE_UPDATED;
    }
    STDMETHOD(DiscardCache)(DWORD) override { return S_OK; }

   private:
    DefaultHandler* h_;
  };
  friend class CachePart;

  HRESULT AddCache(FORMATETC* fe, DWORD advf, DWORD* conn) {
    if (!fe) return E_INVALIDARG;
    if (conn) *conn = 0;
    for (CacheEntry& e : entries_)
      if (SameFormat(e.fe, *fe)) {
        if (conn) *conn = e.conn;
        return CACHE_S_SAMECACHE;
      }
    CacheEntry e;
    e.fe = *fe;
    e.fe.ptd = NULL;
    if (e.fe.dwAspect == DVASPECT_ICON && e.fe.cfFormat == 0) e.fe.cfFormat = CF_METAFILEPICT;
    e.advf = advf;
    e.conn = nextCache_++;
    entries_.push_back(e);
    dirty_ = true;
    if (conn) *conn = e.conn;
    if (e.fe.dwAspect == DVASPECT_ICON) {
      // An icon comes from the class.
      HGLOBAL icon = OleGetIconOfClass(clsid_, NULL, TRUE);
      if (icon) {
        STGMEDIUM m = {};
        m.tymed = TYMED_MFPICT;
        m.hMetaFilePict = icon;
        StoreData(entries_.back(), CF_METAFILEPICT, &m);
        ReleaseStgMedium(&m);
      }
    } else if (data_) {
      AdviseCache(entries_.back());
    }
    return S_OK;
  }

  HRESULT RemoveCache(DWORD conn) {
    for (size_t i = 0; i < entries_.size(); i++)
      if (entries_[i].conn == conn) {
        CacheEntry& e = entries_[i];
        if (data_ && e.serverConn) data_->DUnadvise(e.serverConn);
        if (e.data.tymed != TYMED_NULL) ReleaseStgMedium(&e.data);
        entries_.erase(entries_.begin() + i);
        dirty_ = true;
        return S_OK;
      }
    return OLE_E_NOCONNECTION;
  }

  // What to ask the running object for, for an entry.
  FORMATETC Requested(const CacheEntry& e) {
    FORMATETC fe = e.fe;
    if (fe.cfFormat == 0) fe.cfFormat = CF_METAFILEPICT;
    switch (fe.cfFormat) {
      case CF_METAFILEPICT: fe.tymed = TYMED_MFPICT; break;
      case CF_ENHMETAFILE: fe.tymed = TYMED_ENHMF; break;
      case CF_BITMAP: case CF_PALETTE: fe.tymed = TYMED_GDI; break;
      default: fe.tymed = TYMED_HGLOBAL; break;
    }
    return fe;
  }

  void AdviseCache(CacheEntry& e) {
    if (!data_ || e.serverConn || e.fe.dwAspect == DVASPECT_ICON) return;
    FORMATETC fe = Requested(e);
    DWORD advf = ADVF_PRIMEFIRST | (e.advf & (ADVF_NODATA | ADVF_ONLYONCE));
    if (FAILED(data_->DAdvise(&fe, advf, sink_, &e.serverConn))) {
      // No advise: what it has now.
      STGMEDIUM m = {};
      if (SUCCEEDED(data_->GetData(&fe, &m))) {
        StoreData(e, fe.cfFormat, &m);
        ReleaseStgMedium(&m);
      }
    }
  }

  void StoreData(CacheEntry& e, CLIPFORMAT cf, STGMEDIUM* m) {
    STGMEDIUM copy;
    if (FAILED(CopyMedium(&copy, m, cf))) return;
    if (e.data.tymed != TYMED_NULL) ReleaseStgMedium(&e.data);
    e.data = copy;
    if (e.fe.cfFormat == 0) e.fe.cfFormat = cf;
    e.fe.tymed = copy.tymed;
    e.extent = MediumExtent(cf, copy);
  }

  void NotifyView(DWORD aspect) {
    if (viewSink_ && (viewAspects_ & aspect)) {
      viewSink_->OnViewChange(aspect, -1);
      if (viewAdvf_ & ADVF_ONLYONCE) SafeRelease(viewSink_);
    }
  }

  // The size of a picture, in HIMETRIC.
  static SIZEL MediumExtent(CLIPFORMAT cf, const STGMEDIUM& m) {
    SIZEL s = {0, 0};
    if (cf == CF_METAFILEPICT && m.hMetaFilePict) {
      METAFILEPICT* p = (METAFILEPICT*)GlobalLock(m.hMetaFilePict);
      if (p) {
        s.cx = p->xExt;
        s.cy = p->yExt;
        GlobalUnlock(m.hMetaFilePict);
      }
    } else if (cf == CF_ENHMETAFILE && m.hEnhMetaFile) {
      ENHMETAHEADER h;
      if (GetEnhMetaFileHeader(m.hEnhMetaFile, sizeof h, &h)) {
        s.cx = h.rclFrame.right - h.rclFrame.left;
        s.cy = h.rclFrame.bottom - h.rclFrame.top;
      }
    } else if (cf == CF_DIB && m.hGlobal) {
      BITMAPINFOHEADER* bh = (BITMAPINFOHEADER*)GlobalLock(m.hGlobal);
      if (bh) {
        s.cx = MulDiv(bh->biWidth, 2540, 96);
        s.cy = MulDiv(bh->biHeight < 0 ? -bh->biHeight : bh->biHeight, 2540, 96);
        GlobalUnlock(m.hGlobal);
      }
    } else if (cf == CF_BITMAP && m.hBitmap) {
      BITMAP b;
      if (GetObjectW(m.hBitmap, sizeof b, &b)) {
        s.cx = MulDiv(b.bmWidth, 2540, 96);
        s.cy = MulDiv(b.bmHeight, 2540, 96);
      }
    }
    return s;
  }

  CacheEntry* FindData(const FORMATETC& fe) {
    for (CacheEntry& e : entries_)
      if (e.data.tymed != TYMED_NULL && e.fe.cfFormat == fe.cfFormat && e.fe.dwAspect == fe.dwAspect) return &e;
    return NULL;
  }

  // The picture to draw an aspect with: a metafile before a bitmap.
  CacheEntry* Presentation(DWORD aspect) {
    static const CLIPFORMAT order[] = {CF_METAFILEPICT, CF_ENHMETAFILE, CF_DIB, CF_BITMAP};
    for (CLIPFORMAT cf : order)
      for (CacheEntry& e : entries_)
        if (e.fe.dwAspect == aspect && e.fe.cfFormat == cf && e.data.tymed != TYMED_NULL) return &e;
    return NULL;
  }

  HRESULT CacheGetData(FORMATETC* fe, STGMEDIUM* m) {
    CacheEntry* e = FindData(*fe);
    if (!e) return OLE_E_NOTRUNNING;
    if (!(fe->tymed & e->data.tymed)) return DV_E_TYMED;
    HRESULT hr = CopyMedium(m, &e->data, e->fe.cfFormat);
    m->pUnkForRelease = NULL;
    return hr;
  }

  static bool DrawMedium(HDC hdc, RECT rc, CLIPFORMAT cf, const STGMEDIUM& m) {
    if (cf == CF_METAFILEPICT && m.hMetaFilePict) {
      METAFILEPICT* p = (METAFILEPICT*)GlobalLock(m.hMetaFilePict);
      if (!p) return false;
      // The rectangle in device units, and the picture fitted to it.
      POINT pts[2] = {{rc.left, rc.top}, {rc.right, rc.bottom}};
      LPtoDP(hdc, pts, 2);
      int saved = SaveDC(hdc);
      SetMapMode(hdc, MM_ANISOTROPIC);
      SetWindowOrgEx(hdc, 0, 0, NULL);
      SetWindowExtEx(hdc, p->xExt > 0 ? p->xExt : pts[1].x - pts[0].x, p->yExt > 0 ? p->yExt : pts[1].y - pts[0].y,
                     NULL);
      SetViewportOrgEx(hdc, pts[0].x, pts[0].y, NULL);
      SetViewportExtEx(hdc, pts[1].x - pts[0].x, pts[1].y - pts[0].y, NULL);
      BOOL ok = PlayMetaFile(hdc, p->hMF);
      RestoreDC(hdc, saved);
      GlobalUnlock(m.hMetaFilePict);
      return ok != FALSE;
    }
    if (cf == CF_ENHMETAFILE && m.hEnhMetaFile) return PlayEnhMetaFile(hdc, m.hEnhMetaFile, &rc) != FALSE;
    if (cf == CF_DIB && m.hGlobal) {
      BITMAPINFO* bi = (BITMAPINFO*)GlobalLock(m.hGlobal);
      if (!bi) return false;
      const BITMAPINFOHEADER& bh = bi->bmiHeader;
      DWORD colors = bh.biClrUsed ? bh.biClrUsed : (bh.biBitCount <= 8 ? 1u << bh.biBitCount : 0);
      if (bh.biCompression == BI_BITFIELDS) colors = 3;
      const BYTE* bits = (const BYTE*)bi + bh.biSize + colors * sizeof(RGBQUAD);
      int h = bh.biHeight < 0 ? -bh.biHeight : bh.biHeight;
      int r = StretchDIBits(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0, 0, bh.biWidth, h, bits,
                            bi, DIB_RGB_COLORS, SRCCOPY);
      GlobalUnlock(m.hGlobal);
      return r != 0;
    }
    if (cf == CF_BITMAP && m.hBitmap) {
      HDC mem = CreateCompatibleDC(hdc);
      HGDIOBJ old = SelectObject(mem, m.hBitmap);
      BITMAP b;
      GetObjectW(m.hBitmap, sizeof b, &b);
      BOOL ok = StretchBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mem, 0, 0, b.bmWidth,
                           b.bmHeight, SRCCOPY);
      SelectObject(mem, old);
      DeleteDC(mem);
      return ok != FALSE;
    }
    return false;
  }

  // --- Saving the cache: "\2OlePresNNN", one stream a picture.

  static std::wstring PresName(int i) {
    wchar_t buf[32];
    swprintf(buf, 32, L"\2OlePres%03d", i);
    return buf;
  }

  void SaveCache(IStorage* stg) {
    int i = 0;
    for (CacheEntry& e : entries_) {
      std::vector<BYTE> v;
      CLIPFORMAT cf = e.fe.cfFormat;
      if (cf != 0 && cf < 0xC000) {
        PutD(v, 0xFFFFFFFF);
        PutD(v, cf);
      } else if (cf != 0) {
        char name[256];
        int n = GetClipboardFormatNameA(cf, name, sizeof name);
        PutD(v, (DWORD)n + 1);
        v.insert(v.end(), name, name + n);
        v.push_back(0);
      } else {
        PutD(v, 0);
      }
      PutD(v, 4);  // no target device
      PutD(v, e.fe.dwAspect);
      PutD(v, (DWORD)e.fe.lindex);
      PutD(v, e.advf);
      PutD(v, 0);
      PutD(v, (DWORD)e.extent.cx);
      PutD(v, (DWORD)e.extent.cy);
      std::vector<BYTE> bits = MediumBits(cf, e.data);
      PutD(v, (DWORD)bits.size());
      v.insert(v.end(), bits.begin(), bits.end());
      WriteStream(stg, PresName(i++).c_str(), v);
    }
    // Pictures there were before and are not now.
    for (;; i++) {
      std::wstring n = PresName(i);
      if (FAILED(stg->DestroyElement(n.c_str()))) break;
    }
  }

  static std::vector<BYTE> MediumBits(CLIPFORMAT cf, const STGMEDIUM& m) {
    std::vector<BYTE> v;
    if (m.tymed == TYMED_NULL) return v;
    if (cf == CF_METAFILEPICT && m.hMetaFilePict) {
      METAFILEPICT* p = (METAFILEPICT*)GlobalLock(m.hMetaFilePict);
      if (p) {
        UINT n = GetMetaFileBitsEx(p->hMF, 0, NULL);
        v.resize(n);
        if (n) GetMetaFileBitsEx(p->hMF, n, v.data());
        GlobalUnlock(m.hMetaFilePict);
      }
    } else if (cf == CF_ENHMETAFILE && m.hEnhMetaFile) {
      UINT n = GetEnhMetaFileBits(m.hEnhMetaFile, 0, NULL);
      v.resize(n);
      if (n) GetEnhMetaFileBits(m.hEnhMetaFile, n, v.data());
    } else if (m.tymed == TYMED_HGLOBAL && m.hGlobal) {
      SIZE_T n = GlobalSize(m.hGlobal);
      BYTE* p = (BYTE*)GlobalLock(m.hGlobal);
      if (p) v.assign(p, p + n);
      GlobalUnlock(m.hGlobal);
    }
    return v;
  }

  void LoadCache(IStorage* stg) {
    for (CacheEntry& e : entries_)
      if (e.data.tymed != TYMED_NULL) ReleaseStgMedium(&e.data);
    entries_.clear();
    for (int i = 0;; i++) {
      std::vector<BYTE> v;
      if (FAILED(ReadStream(stg, PresName(i).c_str(), &v))) break;
      size_t pos = 0;
      bool ok = true;
      CacheEntry e;
      DWORD marker = GetD(v, &pos, &ok);
      if (marker == 0xFFFFFFFF || marker == 0xFFFFFFFE) {
        e.fe.cfFormat = (CLIPFORMAT)GetD(v, &pos, &ok);
      } else if (marker > 0 && pos + marker <= v.size()) {
        std::string name((const char*)&v[pos], marker - 1);
        pos += marker;
        e.fe.cfFormat = (CLIPFORMAT)RegisterClipboardFormatA(name.c_str());
      }
      DWORD td = GetD(v, &pos, &ok);
      if (td >= 4) pos += td - 4;
      e.fe.dwAspect = GetD(v, &pos, &ok);
      e.fe.lindex = (LONG)GetD(v, &pos, &ok);
      e.advf = GetD(v, &pos, &ok);
      GetD(v, &pos, &ok);
      e.extent.cx = (LONG)GetD(v, &pos, &ok);
      e.extent.cy = (LONG)GetD(v, &pos, &ok);
      DWORD size = GetD(v, &pos, &ok);
      if (!ok || pos + size > v.size()) continue;
      e.conn = nextCache_++;
      const BYTE* bits = size ? &v[pos] : NULL;
      if (size) {
        if (e.fe.cfFormat == CF_METAFILEPICT) {
          HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(METAFILEPICT));
          METAFILEPICT* p = (METAFILEPICT*)GlobalLock(h);
          p->mm = MM_ANISOTROPIC;
          p->xExt = e.extent.cx;
          p->yExt = e.extent.cy;
          p->hMF = SetMetaFileBitsEx(size, bits);
          GlobalUnlock(h);
          e.data.tymed = TYMED_MFPICT;
          e.data.hMetaFilePict = h;
        } else if (e.fe.cfFormat == CF_ENHMETAFILE) {
          e.data.tymed = TYMED_ENHMF;
          e.data.hEnhMetaFile = SetEnhMetaFileBits(size, bits);
        } else {
          HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
          memcpy(GlobalLock(h), bits, size);
          GlobalUnlock(h);
          e.data.tymed = TYMED_HGLOBAL;
          e.data.hGlobal = h;
        }
      }
      e.fe.tymed = e.data.tymed;
      entries_.push_back(e);
    }
  }

  // "\1Ole": an embedded object's (version, flags, update option,
  // reserved, no moniker).
  static void WriteOleStream(IStorage* stg) {
    std::vector<BYTE> v;
    if (SUCCEEDED(ReadStream(stg, L"\1Ole", &v)) && v.size() >= 20) return;
    v.clear();
    PutD(v, 0x02000001);
    PutD(v, 0);
    PutD(v, 0);
    PutD(v, 0);
    PutD(v, 0);
    WriteStream(stg, L"\1Ole", v);
  }

  // "\1Ole" of a link: as an embedded object's with the link flag, then
  // the source's moniker (absolute; no relative one), its class and
  // three times nobody keeps.
  HRESULT WriteLinkStream(IStorage* stg) {
    std::vector<BYTE> mk;
    if (source_) {
      IStream* s = NULL;
      HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &s);
      if (FAILED(hr)) return hr;
      hr = OleSaveToStream(source_, s);
      if (SUCCEEDED(hr)) {
        HGLOBAL g = NULL;
        GetHGlobalFromStream(s, &g);
        ULARGE_INTEGER end = {};
        LARGE_INTEGER zero = {};
        s->Seek(zero, STREAM_SEEK_CUR, &end);
        BYTE* p = (BYTE*)GlobalLock(g);
        if (p) mk.assign(p, p + (size_t)end.QuadPart);
        GlobalUnlock(g);
      }
      s->Release();
      if (FAILED(hr)) return hr;
    }
    std::vector<BYTE> v;
    PutD(v, 0x02000001);
    PutD(v, 1);  // a link
    PutD(v, updateOpt_);
    PutD(v, 0);
    PutD(v, 0);  // no reserved moniker
    PutD(v, 0);  // no relative moniker
    PutD(v, (DWORD)mk.size());
    v.insert(v.end(), mk.begin(), mk.end());
    PutD(v, 0xFFFFFFFF);
    const BYTE* c = (const BYTE*)&clsid_;
    v.insert(v.end(), c, c + sizeof(CLSID));
    PutD(v, 0);  // no display name
    PutD(v, 0);
    for (int i = 0; i < 6; i++) PutD(v, 0);
    return WriteStream(stg, L"\1Ole", v);
  }

  HRESULT ReadLinkStream(IStorage* stg) {
    std::vector<BYTE> v;
    HRESULT hr = ReadStream(stg, L"\1Ole", &v);
    if (FAILED(hr)) return hr;
    size_t pos = 0;
    bool ok = true;
    GetD(v, &pos, &ok);
    GetD(v, &pos, &ok);
    DWORD opt = GetD(v, &pos, &ok);
    if (ok) updateOpt_ = opt == OLEUPDATE_ONCALL ? OLEUPDATE_ONCALL : OLEUPDATE_ALWAYS;
    GetD(v, &pos, &ok);
    for (int i = 0; i < 2 && ok; i++) {
      DWORD n = GetD(v, &pos, &ok);
      pos += n;
    }
    DWORD n = GetD(v, &pos, &ok);
    if (!ok || pos + n > v.size()) return STG_E_READFAULT;
    if (n) {
      HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
      if (!g) return E_OUTOFMEMORY;
      memcpy(GlobalLock(g), &v[pos], n);
      GlobalUnlock(g);
      IStream* s = NULL;
      hr = CreateStreamOnHGlobal(g, TRUE, &s);
      if (FAILED(hr)) {
        GlobalFree(g);
        return hr;
      }
      IMoniker* mk = NULL;
      hr = OleLoadFromStream(s, IID_IMoniker, (void**)&mk);
      s->Release();
      if (FAILED(hr)) return hr;
      SafeRelease(source_);
      source_ = mk;
    }
    pos += n;
    if (GetD(v, &pos, &ok) == 0xFFFFFFFF && ok && pos + sizeof(CLSID) <= v.size()) memcpy(&clsid_, &v[pos], sizeof(CLSID));
    return S_OK;
  }

  ULONG refs_ = 1;
  CLSID clsid_;
  CachePart cache_;
  HandlerSink* sink_;
  IOleAdviseHolder* oleAdvise_ = NULL;
  IOleClientSite* site_ = NULL;
  IStorage* storage_ = NULL;
  bool loaded_ = false;
  bool dirty_ = false;
  bool contained_ = false;
  bool haveHostNames_ = false;
  std::wstring hostApp_, hostObj_;
  int locks_ = 0;
  // The running object.
  IUnknown* server_ = NULL;
  IOleObject* object_ = NULL;
  IDataObject* data_ = NULL;
  IPersistStorage* persist_ = NULL;
  DWORD oleConn_ = 0;
  // The container's data advises, and their connections on the object.
  std::vector<STATDATA> dataAdvise_;
  std::map<DWORD, DWORD> serverData_;
  DWORD nextData_ = 1;
  // The cache.
  std::vector<CacheEntry> entries_;
  DWORD nextCache_ = 1;
  IAdviseSink* viewSink_ = NULL;
  DWORD viewAspects_ = 0, viewAdvf_ = 0;
  // A link's source.
  bool link_;
  IMoniker* source_ = NULL;
  DWORD updateOpt_ = OLEUPDATE_ALWAYS;
};

void HandlerSink::OnDataChange(FORMATETC* fe, STGMEDIUM* m) {
  if (h_) h_->DataChanged(fe, m);
}
void HandlerSink::OnViewChange(DWORD aspect, LONG lindex) {
  if (h_) h_->ViewChanged(aspect, lindex);
}
void HandlerSink::OnRename(IMoniker* mk) {
  if (h_) h_->Renamed(mk);
}
void HandlerSink::OnSave() {
  if (h_) h_->Saved();
}
void HandlerSink::OnClose() {
  if (h_) h_->Closed();
}

// Sets an object up in its new storage, as OleCreate and OleLoad do:
// the client site, before or after the storage as the object wants it.
HRESULT SetUpObject(IUnknown* unk, IOleClientSite* site, IStorage* stg, bool load) {
  IOleObject* obj = NULL;
  unk->QueryInterface(IID_IOleObject, (void**)&obj);
  DWORD misc = 0;
  if (obj) obj->GetMiscStatus(DVASPECT_CONTENT, &misc);
  bool siteFirst = (misc & OLEMISC_SETCLIENTSITEFIRST) != 0;
  if (obj && site && siteFirst) obj->SetClientSite(site);
  HRESULT hr = S_OK;
  if (stg) {
    IPersistStorage* ps = NULL;
    hr = unk->QueryInterface(IID_IPersistStorage, (void**)&ps);
    if (SUCCEEDED(hr)) {
      hr = load ? ps->Load(stg) : ps->InitNew(stg);
      ps->Release();
    }
  }
  if (SUCCEEDED(hr) && obj && site && !siteFirst) obj->SetClientSite(site);
  SafeRelease(obj);
  return hr;
}

// A new object of an in-process server's class, or NULL when the class
// has none (its server is the program's own local one).
IUnknown* NewInprocObject(REFCLSID clsid) {
  IUnknown* unk = NULL;
  if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&unk))) return NULL;
  return unk;
}

// A new object of a class, through the handler.
HRESULT NewHandler(REFCLSID clsid, DefaultHandler** out) {
  *out = new DefaultHandler(clsid);
  return S_OK;
}

}  // namespace

// What a new object's cache keeps, as asked for.
void winport::ole::AddRenderCache(IUnknown* obj, DWORD render, LPFORMATETC fe) {
  if (render != OLERENDER_DRAW && !(render == OLERENDER_FORMAT && fe)) return;
  IOleCache* cache = NULL;
  if (FAILED(obj->QueryInterface(IID_IOleCache, (void**)&cache))) return;
  FORMATETC f = {0, NULL, DVASPECT_CONTENT, -1, TYMED_NULL};
  if (fe) f = *fe;
  if (render == OLERENDER_DRAW && f.cfFormat != 0 && f.cfFormat != CF_METAFILEPICT && f.cfFormat != CF_ENHMETAFILE &&
      f.cfFormat != CF_DIB && f.cfFormat != CF_BITMAP)
    f.cfFormat = 0;
  DWORD conn = 0;
  cache->Cache(&f, ADVF_PRIMEFIRST, &conn);
  cache->Release();
}

// The cache filled from data: the object's own (running), or another's.
void winport::ole::FillCache(IUnknown* obj, IDataObject* src) {
  IOleCache2* c2 = NULL;
  if (FAILED(obj->QueryInterface(IID_IOleCache2, (void**)&c2))) return;
  IDataObject* d = src;
  if (d)
    d->AddRef();
  else
    obj->QueryInterface(IID_IDataObject, (void**)&d);
  c2->UpdateCache(d, UPDFCACHE_ALL, NULL);
  SafeRelease(d);
  c2->Release();
}

namespace {

// An icon's picture as a metafile: the icon (or, without one, a frame)
// with its label under it.
HGLOBAL IconPicture(HICON icon, const std::wstring& label) {
  std::wstring text = label.empty() ? L"Document" : label;
  const int w = 72, hIcon = 32, h = 48;
  HDC dc = CreateMetaFileW(NULL);
  if (!dc) return NULL;
  SetMapMode(dc, MM_ANISOTROPIC);
  SetWindowOrgEx(dc, 0, 0, NULL);
  SetWindowExtEx(dc, w, h, NULL);
  bool drawn = false;
  if (icon) {
    // The icon's pixels, on white.
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = hIcon;
    bi.bmiHeader.biHeight = hIcon;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
      HGDIOBJ old = SelectObject(mem, bmp);
      RECT rc = {0, 0, hIcon, hIcon};
      FillRect(mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
      DrawIconEx(mem, 0, 0, icon, hIcon, hIcon, 0, NULL, DI_NORMAL);
      GdiFlush();
      StretchDIBits(dc, (w - hIcon) / 2, 0, hIcon, hIcon, 0, 0, hIcon, hIcon, bits, &bi, DIB_RGB_COLORS, SRCCOPY);
      SelectObject(mem, old);
      drawn = true;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
  }
  if (!drawn) {
    HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ oldB = SelectObject(dc, b);
    HGDIOBJ oldP = SelectObject(dc, GetStockObject(BLACK_PEN));
    Rectangle(dc, (w - hIcon) / 2, 0, (w + hIcon) / 2, hIcon);
    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(b);
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_CENTER | TA_TOP);
  TextOutW(dc, w / 2, hIcon + 2, text.c_str(), (int)text.size());
  HMETAFILE mf = CloseMetaFile(dc);
  if (!mf) return NULL;
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(METAFILEPICT));
  if (!g) {
    DeleteMetaFile(mf);
    return NULL;
  }
  METAFILEPICT* p = (METAFILEPICT*)GlobalLock(g);
  p->mm = MM_ANISOTROPIC;
  p->xExt = MulDiv(w, 2540, 96);
  p->yExt = MulDiv(h, 2540, 96);
  p->hMF = mf;
  GlobalUnlock(g);
  return g;
}

}  // namespace

extern "C" {

HRESULT WINAPI CreateOleAdviseHolder(LPOLEADVISEHOLDER* out) {
  if (!out) return E_INVALIDARG;
  *out = new OleAdviseHolder;
  return S_OK;
}

HRESULT WINAPI CreateDataAdviseHolder(LPDATAADVISEHOLDER* out) {
  if (!out) return E_INVALIDARG;
  *out = new DataAdviseHolder;
  return S_OK;
}

HANDLE WINAPI OleDuplicateData(HANDLE h, CLIPFORMAT cf, UINT flags) { return DuplicateData(h, cf, flags); }

HRESULT WINAPI OleCreateDefaultHandler(REFCLSID clsid, LPUNKNOWN outer, REFIID riid, LPVOID* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (outer) return CLASS_E_NOAGGREGATION;
  DefaultHandler* h = new DefaultHandler(clsid);
  HRESULT hr = h->QueryInterface(riid, out);
  h->Release();
  return hr;
}

HRESULT WINAPI OleCreate(REFCLSID clsid, REFIID riid, DWORD render, LPFORMATETC fe, LPOLECLIENTSITE site,
                         LPSTORAGE stg, LPVOID* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  // An in-process server's object is used as it is: it draws and caches
  // for itself.
  if (IUnknown* direct = NewInprocObject(clsid)) {
    if (stg) WriteClassStg(stg, clsid);
    HRESULT hr = SetUpObject(direct, site, stg, false);
    if (SUCCEEDED(hr) && (render == OLERENDER_DRAW || (render == OLERENDER_FORMAT && fe))) {
      IOleCache* cache = NULL;
      if (SUCCEEDED(direct->QueryInterface(IID_IOleCache, (void**)&cache))) {
        FORMATETC f = {0, NULL, DVASPECT_CONTENT, -1, TYMED_NULL};
        if (fe) f = *fe;
        DWORD conn = 0;
        cache->Cache(&f, ADVF_PRIMEFIRST, &conn);
        cache->Release();
      }
    }
    if (SUCCEEDED(hr)) hr = direct->QueryInterface(riid, out);
    direct->Release();
    return hr;
  }
  DefaultHandler* h = NULL;
  HRESULT hr = NewHandler(clsid, &h);
  if (FAILED(hr)) return hr;
  if (stg) WriteClassStg(stg, clsid);
  AddRenderCache(static_cast<IOleObject*>(h), render, fe);
  // Set it up, run it (which hands the object its storage and site), and
  // fill the cache from it.
  hr = SetUpObject(static_cast<IOleObject*>(h), site, stg, false);
  if (SUCCEEDED(hr)) hr = h->Run(NULL);
  if (SUCCEEDED(hr) && render != OLERENDER_NONE) FillCache(static_cast<IOleObject*>(h), NULL);
  if (SUCCEEDED(hr)) hr = h->QueryInterface(riid, out);
  if (FAILED(hr)) h->Close(OLECLOSE_NOSAVE);
  h->Release();
  return hr;
}

HRESULT WINAPI OleLoad(LPSTORAGE stg, REFIID riid, LPOLECLIENTSITE site, LPVOID* out) {
  if (!out || !stg) return E_INVALIDARG;
  *out = NULL;
  CLSID clsid = CLSID_NULL;
  HRESULT hr = ReadClassStg(stg, &clsid);
  if (FAILED(hr)) return hr;
  DefaultHandler* h = NULL;
  if (IsEqualCLSID(clsid, kStdOleLink)) {
    h = new DefaultHandler(CLSID_NULL, true);
  } else if (IUnknown* direct = NewInprocObject(clsid)) {
    hr = SetUpObject(direct, site, stg, true);
    if (SUCCEEDED(hr)) hr = direct->QueryInterface(riid, out);
    direct->Release();
    return hr;
  } else {
    hr = NewHandler(clsid, &h);
    if (FAILED(hr)) return hr;
  }
  hr = SetUpObject(static_cast<IOleObject*>(h), site, stg, true);
  if (SUCCEEDED(hr)) hr = h->QueryInterface(riid, out);
  h->Release();
  return hr;
}

// A link to what a moniker names: the handler in link mode, its cache
// filled from the source. A source that was not running before is let go
// again (the cache keeps its picture).
HRESULT WINAPI OleCreateLink(LPMONIKER mk, REFIID riid, DWORD render, LPFORMATETC fe, LPOLECLIENTSITE site,
                             LPSTORAGE stg, LPVOID* out) {
  if (!out || !mk) return E_INVALIDARG;
  *out = NULL;
  IBindCtx* bc = NULL;
  HRESULT hr = CreateBindCtx(0, &bc);
  if (FAILED(hr)) return hr;
  bool wasRunning = mk->IsRunning(bc, NULL, NULL) == S_OK;
  DefaultHandler* h = new DefaultHandler(CLSID_NULL, true);
  h->SetSourceMoniker(mk, CLSID_NULL);
  if (stg) WriteClassStg(stg, kStdOleLink);
  AddRenderCache(static_cast<IOleObject*>(h), render, fe);
  hr = SetUpObject(static_cast<IOleObject*>(h), site, stg, false);
  if (SUCCEEDED(hr)) hr = h->Run(bc);
  bc->Release();
  if (SUCCEEDED(hr) && render != OLERENDER_NONE) FillCache(static_cast<IOleObject*>(h), NULL);
  if (SUCCEEDED(hr) && !wasRunning) h->UnbindSource();
  if (SUCCEEDED(hr)) hr = h->QueryInterface(riid, out);
  if (FAILED(hr)) h->UnbindSource();
  h->Release();
  return hr;
}

HRESULT WINAPI OleCreateLinkToFile(LPCOLESTR file, REFIID riid, DWORD render, LPFORMATETC fe, LPOLECLIENTSITE site,
                                   LPSTORAGE stg, LPVOID* out) {
  if (!out || !file) return E_INVALIDARG;
  *out = NULL;
  IMoniker* mk = NULL;
  HRESULT hr = CreateFileMoniker(file, &mk);
  if (FAILED(hr)) return hr;
  hr = OleCreateLink(mk, riid, render, fe, site, stg, out);
  mk->Release();
  return hr;
}

// An object embedded from a file: the file's own object (found by its
// class) saves itself into the new object's storage, which is loaded;
// its picture comes from the file's object too.
HRESULT WINAPI OleCreateFromFile(REFCLSID, LPCOLESTR file, REFIID riid, DWORD render, LPFORMATETC fe,
                                 LPOLECLIENTSITE site, LPSTORAGE stg, LPVOID* out) {
  if (!out || !file) return E_INVALIDARG;
  *out = NULL;
  if (!stg) return E_INVALIDARG;
  CLSID clsid;
  HRESULT hr = GetClassFile(file, &clsid);
  if (FAILED(hr)) return hr;
  IMoniker* mk = NULL;
  hr = CreateFileMoniker(file, &mk);
  if (FAILED(hr)) return hr;
  IBindCtx* bc = NULL;
  hr = CreateBindCtx(0, &bc);
  IUnknown* unk = NULL;
  if (SUCCEEDED(hr)) hr = mk->BindToObject(bc, NULL, IID_IUnknown, (void**)&unk);
  SafeRelease(bc);
  mk->Release();
  if (FAILED(hr)) return hr;
  IPersistStorage* ps = NULL;
  hr = unk->QueryInterface(IID_IPersistStorage, (void**)&ps);
  if (SUCCEEDED(hr)) {
    hr = WriteClassStg(stg, clsid);
    if (SUCCEEDED(hr)) hr = ps->Save(stg, FALSE);
    if (SUCCEEDED(hr)) ps->SaveCompleted(NULL);
    ps->Release();
  }
  if (SUCCEEDED(hr)) hr = stg->Commit(STGC_DEFAULT);
  IUnknown* obj = NULL;
  if (SUCCEEDED(hr)) hr = OleLoad(stg, IID_IUnknown, site, (void**)&obj);
  if (SUCCEEDED(hr) && render != OLERENDER_NONE) {
    IDataObject* data = NULL;
    unk->QueryInterface(IID_IDataObject, (void**)&data);
    AddRenderCache(obj, render, fe);
    if (data) FillCache(obj, data);
    SafeRelease(data);
  }
  unk->Release();
  if (SUCCEEDED(hr)) hr = obj->QueryInterface(riid, out);
  SafeRelease(obj);
  return hr;
}

// The class an object's class says to become, as its storage's class
// (the object turns into one on its next load).
HRESULT WINAPI OleDoAutoConvert(LPSTORAGE stg, LPCLSID out) {
  if (!stg || !out) return E_INVALIDARG;
  CLSID clsid = CLSID_NULL;
  HRESULT hr = ReadClassStg(stg, &clsid);
  if (FAILED(hr)) return hr;
  *out = clsid;
  std::wstring to;
  CLSID next;
  if (!ClassValue(clsid, L"AutoConvertTo", &to) || FAILED(CLSIDFromString(to.c_str(), &next)))
    return REGDB_E_KEYMISSING;
  CLIPFORMAT cf = 0;
  ReadFmtUserTypeStg(stg, &cf, NULL);
  LPOLESTR type = NULL;
  OleRegGetUserType(next, USERCLASSTYPE_FULL, &type);
  hr = WriteClassStg(stg, next);
  if (SUCCEEDED(hr)) hr = WriteFmtUserTypeStg(stg, cf, type);
  if (SUCCEEDED(hr)) hr = SetConvertStg(stg, TRUE);
  CoTaskMemFree(type);
  if (FAILED(hr)) return hr;
  *out = next;
  return S_OK;
}

HRESULT WINAPI OleSave(LPPERSISTSTORAGE ps, LPSTORAGE stg, BOOL sameAsLoad) {
  if (!ps || !stg) return E_INVALIDARG;
  CLSID clsid;
  HRESULT hr = ps->GetClassID(&clsid);
  if (SUCCEEDED(hr)) hr = WriteClassStg(stg, clsid);
  if (SUCCEEDED(hr)) hr = ps->Save(stg, sameAsLoad);
  if (SUCCEEDED(hr)) stg->Commit(STGC_DEFAULT);
  return hr;
}

HRESULT WINAPI OleRun(LPUNKNOWN unk) {
  if (!unk) return E_INVALIDARG;
  IRunnableObject* r = NULL;
  if (FAILED(unk->QueryInterface(IID_IRunnableObject, (void**)&r))) return S_OK;
  HRESULT hr = r->Run(NULL);
  r->Release();
  return hr;
}

BOOL WINAPI OleIsRunning(LPOLEOBJECT obj) {
  if (!obj) return FALSE;
  IRunnableObject* r = NULL;
  if (FAILED(obj->QueryInterface(IID_IRunnableObject, (void**)&r))) return TRUE;
  BOOL b = r->IsRunning();
  r->Release();
  return b;
}

HRESULT WINAPI OleLockRunning(LPUNKNOWN unk, BOOL lock, BOOL lastUnlockCloses) {
  if (!unk) return E_INVALIDARG;
  IRunnableObject* r = NULL;
  if (FAILED(unk->QueryInterface(IID_IRunnableObject, (void**)&r))) return S_OK;
  HRESULT hr = r->LockRunning(lock, lastUnlockCloses);
  r->Release();
  return hr;
}

HRESULT WINAPI OleNoteObjectVisible(LPUNKNOWN unk, BOOL visible) { return OleLockRunning(unk, visible, FALSE); }

HRESULT WINAPI OleSetContainedObject(LPUNKNOWN unk, BOOL contained) {
  if (!unk) return E_INVALIDARG;
  IRunnableObject* r = NULL;
  if (FAILED(unk->QueryInterface(IID_IRunnableObject, (void**)&r))) return S_OK;
  HRESULT hr = r->SetContainedObject(contained);
  r->Release();
  return hr;
}

HRESULT WINAPI OleDraw(LPUNKNOWN unk, DWORD aspect, HDC hdc, LPCRECT rc) {
  if (!unk || !rc) return E_INVALIDARG;
  IViewObject* v = NULL;
  if (FAILED(unk->QueryInterface(IID_IViewObject, (void**)&v))) return OLE_E_BLANK;
  RECTL r = {rc->left, rc->top, rc->right, rc->bottom};
  HRESULT hr = v->Draw(aspect, -1, NULL, NULL, NULL, hdc, &r, NULL, NULL, 0);
  v->Release();
  return hr;
}

// ---------------------------------------------------------------------
// What the registry says of a class

HRESULT WINAPI OleRegGetUserType(REFCLSID clsid, DWORD form, LPOLESTR* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  std::wstring v;
  wchar_t sub[32];
  swprintf(sub, 32, L"AuxUserType\\%lu", (unsigned long)form);
  if (form == USERCLASSTYPE_FULL || !ClassValue(clsid, sub, &v) || v.empty()) {
    if (!ClassValue(clsid, NULL, &v)) return REGDB_E_CLASSNOTREG;
  }
  *out = CoStrDup(v.c_str());
  return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI OleRegGetMiscStatus(REFCLSID clsid, DWORD aspect, DWORD* out) {
  if (!out) return E_INVALIDARG;
  *out = 0;
  std::wstring v;
  wchar_t sub[32];
  swprintf(sub, 32, L"MiscStatus\\%lu", (unsigned long)aspect);
  if (ClassValue(clsid, sub, &v) || ClassValue(clsid, L"MiscStatus", &v)) {
    *out = (DWORD)wcstoul(v.c_str(), NULL, 10);
    return S_OK;
  }
  std::wstring any;
  return ClassValue(clsid, NULL, &any) ? S_OK : REGDB_E_CLASSNOTREG;
}

// "Verb\N" = "name,menu flags,attributes".
HRESULT WINAPI OleRegEnumVerbs(REFCLSID clsid, LPENUMOLEVERB* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  std::wstring key = ClassKey(clsid, L"Verb");
  HKEY hk;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS) return OLEOBJ_E_NOVERBS;
  std::vector<OLEVERB> verbs;
  std::vector<std::wstring> names;
  for (DWORD i = 0;; i++) {
    WCHAR sub[64];
    LONG r = RegEnumKeyW(hk, i, sub, 64);
    if (r != ERROR_SUCCESS) break;
    WCHAR val[512];
    LONG cb = sizeof val;
    if (RegQueryValueW(hk, sub, val, &cb) != ERROR_SUCCESS) continue;
    std::wstring s = val;
    OLEVERB v = {};
    v.lVerb = wcstol(sub, NULL, 10);
    size_t c1 = s.find(L','), c2 = c1 == std::wstring::npos ? c1 : s.find(L',', c1 + 1);
    names.push_back(s.substr(0, c1));
    if (c1 != std::wstring::npos) v.fuFlags = (DWORD)wcstoul(s.c_str() + c1 + 1, NULL, 10);
    if (c2 != std::wstring::npos) v.grfAttribs = (DWORD)wcstoul(s.c_str() + c2 + 1, NULL, 10);
    verbs.push_back(v);
  }
  RegCloseKey(hk);
  if (verbs.empty()) return OLEOBJ_E_NOVERBS;
  // By number, as the verbs go on a menu.
  std::vector<size_t> idx(verbs.size());
  for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
  std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return verbs[a].lVerb < verbs[b].lVerb; });
  std::vector<OLEVERB> sorted;
  for (size_t i : idx) {
    OLEVERB v = verbs[i];
    v.lpszVerbName = (LPOLESTR)names[i].c_str();  // the enumerator copies it
    sorted.push_back(v);
  }
  *out = new VerbEnum(sorted);
  return S_OK;
}

// The formats a class gives (DataFormats\GetSet\N = "format,aspect,
// tymed,direction"), for the direction asked: 1 is get, 2 is set. A
// format is a clipboard format's number or its name.
HRESULT WINAPI OleRegEnumFormatEtc(REFCLSID clsid, DWORD dir, LPENUMFORMATETC* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (dir != DATADIR_GET && dir != DATADIR_SET) return E_INVALIDARG;
  std::wstring key = ClassKey(clsid, L"DataFormats\\GetSet");
  HKEY hk;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS) return REGDB_E_KEYMISSING;
  std::vector<FORMATETC> v;
  for (DWORD i = 0;; i++) {
    WCHAR sub[64];
    if (RegEnumKeyW(hk, i, sub, 64) != ERROR_SUCCESS) break;
    WCHAR val[512];
    LONG cb = sizeof val;
    if (RegQueryValueW(hk, sub, val, &cb) != ERROR_SUCCESS) continue;
    std::wstring s = val;
    std::vector<std::wstring> parts;
    for (size_t start = 0;;) {
      size_t c = s.find(L',', start);
      parts.push_back(s.substr(start, c == std::wstring::npos ? c : c - start));
      if (c == std::wstring::npos) break;
      start = c + 1;
    }
    if (parts.size() < 4) continue;
    DWORD dirs = (DWORD)wcstoul(parts[3].c_str(), NULL, 10);
    if (!(dirs & (dir == DATADIR_GET ? 1u : 2u))) continue;
    std::wstring name = parts[0];
    while (!name.empty() && name[0] == L' ') name.erase(0, 1);
    FORMATETC f = {};
    f.cfFormat = !name.empty() && iswdigit(name[0]) ? (CLIPFORMAT)wcstoul(name.c_str(), NULL, 10)
                                                    : (CLIPFORMAT)RegisterClipboardFormatW(name.c_str());
    f.dwAspect = (DWORD)wcstoul(parts[1].c_str(), NULL, 10);
    f.lindex = -1;
    f.tymed = (DWORD)wcstoul(parts[2].c_str(), NULL, 10);
    if (f.cfFormat) v.push_back(f);
  }
  RegCloseKey(hk);
  *out = new FormatEnum(v);
  return S_OK;
}

// An icon's picture: the class's user type under a frame, as a metafile.
HGLOBAL WINAPI OleGetIconOfClass(REFCLSID clsid, LPOLESTR label, BOOL useTypeAsLabel) {
  std::wstring text;
  if (label) {
    text = label;
  } else if (useTypeAsLabel) {
    LPOLESTR t = NULL;
    if (SUCCEEDED(OleRegGetUserType(clsid, USERCLASSTYPE_SHORT, &t)) && t) {
      text = t;
      CoTaskMemFree(t);
    }
  }
  return IconPicture(NULL, text);
}

// An icon and its label as a picture. (Where the icon came from is not
// kept in it.)
HGLOBAL WINAPI OleMetafilePictFromIconAndLabel(HICON icon, LPOLESTR label, LPOLESTR, UINT) {
  if (!icon) return NULL;
  return IconPicture(icon, label ? label : L"");
}

// A picture as an object of its own: the handler of a static class, with
// the picture in its cache.
HRESULT WINAPI OleCreateStaticFromData(LPDATAOBJECT src, REFIID riid, DWORD render, LPFORMATETC fe,
                                       LPOLECLIENTSITE site, LPSTORAGE stg, LPVOID* out) {
  if (!out || !src) return E_INVALIDARG;
  *out = NULL;
  static const struct {
    CLIPFORMAT cf;
    DWORD tymed;
    const CLSID* clsid;
  } kinds[] = {{CF_METAFILEPICT, TYMED_MFPICT, &kStaticMetafile},
               {CF_ENHMETAFILE, TYMED_ENHMF, &kStaticEnhMetafile},
               {CF_DIB, TYMED_HGLOBAL, &kStaticDib},
               {CF_BITMAP, TYMED_GDI, &kStaticDib}};
  for (const auto& k : kinds) {
    if (render == OLERENDER_FORMAT && fe && fe->cfFormat != k.cf) continue;
    FORMATETC f = {k.cf, NULL, DVASPECT_CONTENT, -1, k.tymed};
    STGMEDIUM m = {};
    if (FAILED(src->GetData(&f, &m))) continue;
    DefaultHandler* h = new DefaultHandler(*k.clsid);
    if (stg) WriteClassStg(stg, *k.clsid);
    HRESULT hr = SetUpObject(static_cast<IOleObject*>(h), site, stg, false);
    IOleCache* cache = NULL;
    if (SUCCEEDED(hr)) hr = h->QueryInterface(IID_IOleCache, (void**)&cache);
    if (SUCCEEDED(hr)) {
      DWORD conn = 0;
      cache->Cache(&f, ADVF_PRIMEFIRST, &conn);
      hr = cache->SetData(&f, &m, FALSE);
      cache->Release();
    }
    ReleaseStgMedium(&m);
    if (SUCCEEDED(hr)) hr = h->QueryInterface(riid, out);
    h->Release();
    return hr;
  }
  return DV_E_FORMATETC;
}

// ---------------------------------------------------------------------
// The Edit menu's object item (oledlg)

// The item at pos becomes the object's verbs: one item for one verb, a
// popup for more (with Convert); without an object, a gray "&Object".
BOOL WINAPI OleUIAddVerbMenuW(LPOLEOBJECT obj, LPCWSTR shortType, HMENU menu, UINT pos, UINT idMin, UINT idMax,
                              BOOL addConvert, UINT idConvert, HMENU* popupOut) {
  if (popupOut) *popupOut = NULL;
  if (!menu) return FALSE;
  UINT oldId = GetMenuItemID(menu, (int)pos);
  if (!obj) {
    ModifyMenuW(menu, pos, MF_BYPOSITION | MF_STRING | MF_GRAYED, oldId, L"&Object");
    return FALSE;
  }
  std::wstring type = shortType ? shortType : L"";
  if (type.empty()) {
    LPOLESTR t = NULL;
    if (SUCCEEDED(obj->GetUserType(USERCLASSTYPE_SHORT, &t)) && t) type = t;
    CoTaskMemFree(t);
  }
  std::vector<std::pair<LONG, std::wstring>> verbs;
  IEnumOLEVERB* e = NULL;
  if (SUCCEEDED(obj->EnumVerbs(&e)) && e) {
    OLEVERB v;
    ULONG n = 0;
    while (e->Next(1, &v, &n) == S_OK && n == 1) {
      if (v.lVerb >= 0 && (v.grfAttribs & OLEVERBATTRIB_ONCONTAINERMENU) && v.lpszVerbName &&
          (idMax == 0 || idMin + (UINT)v.lVerb <= idMax))
        verbs.push_back({v.lVerb, v.lpszVerbName});
      CoTaskMemFree(v.lpszVerbName);
    }
    e->Release();
  }
  std::wstring label = type.empty() ? L"&Object" : type + L" &Object";
  if (verbs.empty()) {
    ModifyMenuW(menu, pos, MF_BYPOSITION | MF_STRING, idMin, label.c_str());
    return TRUE;
  }
  if (verbs.size() == 1 && !addConvert) {
    std::wstring name;
    for (wchar_t c : verbs[0].second)
      if (c != L'&') name += c;
    std::wstring text = name + L" " + label;
    ModifyMenuW(menu, pos, MF_BYPOSITION | MF_STRING, idMin + (UINT)verbs[0].first, text.c_str());
    return TRUE;
  }
  HMENU popup = CreatePopupMenu();
  for (auto& v : verbs) AppendMenuW(popup, MF_STRING, idMin + (UINT)v.first, v.second.c_str());
  if (addConvert) {
    AppendMenuW(popup, MF_SEPARATOR, 0, NULL);
    AppendMenuW(popup, MF_STRING, idConvert, L"&Convert...");
  }
  ModifyMenuW(menu, pos, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)popup, label.c_str());
  if (popupOut) *popupOut = popup;
  return TRUE;
}

}  // extern "C"
