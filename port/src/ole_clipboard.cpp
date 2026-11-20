// ole_clipboard.cpp - the OLE clipboard, and objects made from data.
//
// OleSetClipboard puts a data object on the clipboard: its formats go on
// the clipboard to be rendered when asked for (a storage or stream as
// global memory), and the object is kept until the clipboard changes
// hands or OleFlushClipboard renders everything. OleGetClipboard hands
// out a data object over the clipboard: the one put there while it is
// still there, otherwise the clipboard's data, with storages and streams
// made again from their memory.
//
// Between programs: the program whose data object is on the clipboard
// says so (com_remote.cpp), and OleGetClipboard in another program hands
// out a proxy for that object. Putting something else there, or nothing,
// makes the program before let its object go.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "ole.h"
#include "com.h"

#include <string>

using namespace winport::ole;

namespace {

IDataObject* g_clip = NULL;  // the data object OleSetClipboard put there
HWND g_clipWnd = NULL;       // the clipboard's owner while it is

CLIPFORMAT Format(const wchar_t* name) { return (CLIPFORMAT)RegisterClipboardFormatW(name); }

// The file named by "FileName" data (the shell's, in the ANSI code page),
// or nothing.
std::wstring FileNameOf(IDataObject* data) {
  FORMATETC f = {Format(L"FileName"), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM m = {};
  std::wstring file;
  if (FAILED(data->GetData(&f, &m))) return file;
  if (m.tymed == TYMED_HGLOBAL) {
    if (const char* p = (const char*)GlobalLock(m.hGlobal)) {
      int n = MultiByteToWideChar(CP_ACP, 0, p, -1, NULL, 0);
      if (n > 1) {
        file.resize(n);
        MultiByteToWideChar(CP_ACP, 0, p, -1, &file[0], n);
        file.resize(n - 1);
      }
      GlobalUnlock(m.hGlobal);
    }
  }
  ReleaseStgMedium(&m);
  return file;
}

// The medium a format takes on the clipboard.
DWORD TymedOf(CLIPFORMAT cf) {
  switch (cf) {
    case CF_METAFILEPICT: return TYMED_MFPICT;
    case CF_ENHMETAFILE: return TYMED_ENHMF;
    case CF_BITMAP: case CF_PALETTE: return TYMED_GDI;
  }
  if (cf == Format(L"Embed Source") || cf == Format(L"Embedded Object") || cf == Format(L"Link Source"))
    return TYMED_ISTORAGE;
  return TYMED_HGLOBAL;
}

// A medium as the clipboard keeps it: one handle.
HANDLE ToClipboard(CLIPFORMAT cf, STGMEDIUM& m) {
  switch (m.tymed) {
    case TYMED_HGLOBAL: return DuplicateData(m.hGlobal, cf, GMEM_MOVEABLE);
    case TYMED_MFPICT: return DuplicateData(m.hMetaFilePict, CF_METAFILEPICT, GMEM_MOVEABLE);
    case TYMED_ENHMF: return CopyEnhMetaFileW(m.hEnhMetaFile, NULL);
    case TYMED_GDI: return DuplicateData(m.hBitmap, cf == CF_PALETTE ? CF_PALETTE : CF_BITMAP, 0);
    case TYMED_ISTORAGE: {
      // A compound file in memory.
      ILockBytes* lb = NULL;
      IStorage* stg = NULL;
      HGLOBAL out = NULL;
      if (SUCCEEDED(CreateILockBytesOnHGlobal(NULL, FALSE, &lb)) &&
          SUCCEEDED(StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg)) &&
          SUCCEEDED(m.pstg->CopyTo(0, NULL, NULL, stg)) && SUCCEEDED(stg->Commit(STGC_DEFAULT))) {
        SafeRelease(stg);
        GetHGlobalFromILockBytes(lb, &out);
      }
      SafeRelease(stg);
      SafeRelease(lb);
      return out;
    }
    case TYMED_ISTREAM: {
      STATSTG st = {};
      m.pstm->Stat(&st, STATFLAG_NONAME);
      HGLOBAL out = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)st.cbSize.QuadPart);
      if (!out) return NULL;
      LARGE_INTEGER zero = {};
      m.pstm->Seek(zero, STREAM_SEEK_SET, NULL);
      ULONG n = 0;
      m.pstm->Read(GlobalLock(out), (ULONG)st.cbSize.QuadPart, &n);
      GlobalUnlock(out);
      return out;
    }
  }
  return NULL;
}

void Render(CLIPFORMAT cf) {
  if (!g_clip) return;
  FORMATETC fe = {cf, NULL, DVASPECT_CONTENT, -1, TymedOf(cf)};
  STGMEDIUM m = {};
  HRESULT hr = g_clip->GetData(&fe, &m);
  if (FAILED(hr) && fe.tymed == TYMED_ISTORAGE) {
    fe.tymed = TYMED_HGLOBAL;
    hr = g_clip->GetData(&fe, &m);
  }
  if (FAILED(hr)) return;
  HANDLE h = ToClipboard(cf, m);
  ReleaseStgMedium(&m);
  if (h) SetClipboardData(cf, h);
}

LRESULT CALLBACK ClipWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_RENDERFORMAT: Render((CLIPFORMAT)wp); return 0;
    case WM_RENDERALLFORMATS:
      if (OpenClipboard(hwnd)) {
        for (UINT cf = EnumClipboardFormats(0); cf; cf = EnumClipboardFormats(cf)) Render((CLIPFORMAT)cf);
        CloseClipboard();
      }
      return 0;
    case WM_DESTROYCLIPBOARD: SafeRelease(g_clip); return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND ClipWindow() {
  if (g_clipWnd && IsWindow(g_clipWnd)) return g_clipWnd;
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = ClipWndProc;
    wc.lpszClassName = L"CLIPBRDWNDCLASS";
    RegisterClassW(&wc);
    registered = true;
  }
  g_clipWnd = CreateWindowExW(0, L"CLIPBRDWNDCLASS", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
  return g_clipWnd;
}

bool OursOnClipboard() { return g_clip && g_clipWnd && GetClipboardOwner() == g_clipWnd; }

// The data object OleGetClipboard hands out.
class ClipboardData final : public IDataObject {
 public:
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject)) {
      *out = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IDataObject)
  STDMETHOD(GetData)(FORMATETC* fe, STGMEDIUM* m) override {
    if (!fe || !m) return E_INVALIDARG;
    memset(m, 0, sizeof *m);
    if (OursOnClipboard()) return g_clip->GetData(fe, m);
    if (!IsClipboardFormatAvailable(fe->cfFormat)) return DV_E_FORMATETC;
    if (!OpenClipboard(NULL)) return CLIPBRD_E_CANT_OPEN;
    HANDLE h = GetClipboardData(fe->cfFormat);
    HRESULT hr = h ? FromClipboard(fe, h, m) : DV_E_FORMATETC;
    CloseClipboard();
    return hr;
  }
  STDMETHOD(GetDataHere)(FORMATETC* fe, STGMEDIUM* m) override {
    if (OursOnClipboard()) return g_clip->GetDataHere(fe, m);
    if (!fe || !m || m->tymed != TYMED_ISTORAGE || !m->pstg) return E_NOTIMPL;
    STGMEDIUM got = {};
    FORMATETC f = *fe;
    f.tymed = TYMED_ISTORAGE;
    HRESULT hr = GetData(&f, &got);
    if (FAILED(hr)) return hr;
    hr = got.pstg->CopyTo(0, NULL, NULL, m->pstg);
    ReleaseStgMedium(&got);
    return hr;
  }
  STDMETHOD(QueryGetData)(FORMATETC* fe) override {
    if (!fe) return E_INVALIDARG;
    if (OursOnClipboard()) return g_clip->QueryGetData(fe);
    if (!IsClipboardFormatAvailable(fe->cfFormat)) return DV_E_FORMATETC;
    return (fe->tymed & Accepts(fe->cfFormat)) ? S_OK : DV_E_TYMED;
  }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* in, FORMATETC* out) override {
    if (!in || !out) return E_INVALIDARG;
    *out = *in;
    out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
  }
  // Data set here goes on the clipboard beside what is there: to the
  // object that put it there, when it is this program's.
  STDMETHOD(SetData)(FORMATETC* fe, STGMEDIUM* m, BOOL release) override {
    if (!fe || !m) return E_INVALIDARG;
    if (OursOnClipboard()) return g_clip->SetData(fe, m, release);
    if (!fe->cfFormat || !(fe->tymed & m->tymed)) return DV_E_FORMATETC;
    if (!OpenClipboard(NULL)) return CLIPBRD_E_CANT_OPEN;
    HANDLE h = ToClipboard(fe->cfFormat, *m);
    bool ok = h && SetClipboardData(fe->cfFormat, h) != NULL;
    CloseClipboard();
    if (!h) return DV_E_TYMED;
    if (!ok) return CLIPBRD_E_CANT_SET;
    if (release) ReleaseStgMedium(m);
    return S_OK;
  }
  STDMETHOD(EnumFormatEtc)(DWORD dir, IEnumFORMATETC** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (dir != DATADIR_GET) return E_NOTIMPL;
    if (OursOnClipboard()) return g_clip->EnumFormatEtc(dir, out);
    std::vector<FORMATETC> v;
    if (OpenClipboard(NULL)) {
      for (UINT cf = EnumClipboardFormats(0); cf; cf = EnumClipboardFormats(cf)) {
        FORMATETC f = {(CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1, TymedOf((CLIPFORMAT)cf)};
        v.push_back(f);
      }
      CloseClipboard();
    }
    *out = new FormatEnum(v);
    return S_OK;
  }
  STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(DUnadvise)(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

 private:
  static DWORD Accepts(CLIPFORMAT cf) {
    DWORD t = TymedOf(cf);
    return t == TYMED_ISTORAGE ? (TYMED_ISTORAGE | TYMED_HGLOBAL | TYMED_ISTREAM)
           : t == TYMED_HGLOBAL ? (TYMED_HGLOBAL | TYMED_ISTREAM)
                                : t;
  }

  static HRESULT FromClipboard(FORMATETC* fe, HANDLE h, STGMEDIUM* m) {
    DWORD t = TymedOf(fe->cfFormat);
    if (t == TYMED_MFPICT && (fe->tymed & TYMED_MFPICT)) {
      m->tymed = TYMED_MFPICT;
      m->hMetaFilePict = DuplicateData(h, CF_METAFILEPICT, GMEM_MOVEABLE);
    } else if (t == TYMED_ENHMF && (fe->tymed & TYMED_ENHMF)) {
      m->tymed = TYMED_ENHMF;
      m->hEnhMetaFile = CopyEnhMetaFileW((HENHMETAFILE)h, NULL);
    } else if (t == TYMED_GDI && (fe->tymed & TYMED_GDI)) {
      m->tymed = TYMED_GDI;
      m->hBitmap = (HBITMAP)DuplicateData(h, fe->cfFormat, 0);
    } else if ((t == TYMED_HGLOBAL || t == TYMED_ISTORAGE) && (fe->tymed & TYMED_ISTORAGE) && t == TYMED_ISTORAGE) {
      HGLOBAL copy = DuplicateData(h, fe->cfFormat, GMEM_MOVEABLE);
      ILockBytes* lb = NULL;
      if (!copy || FAILED(CreateILockBytesOnHGlobal(copy, TRUE, &lb))) return E_OUTOFMEMORY;
      HRESULT hr = StgOpenStorageOnILockBytes(lb, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &m->pstg);
      lb->Release();
      if (FAILED(hr)) return hr;
      m->tymed = TYMED_ISTORAGE;
    } else if ((t == TYMED_HGLOBAL || t == TYMED_ISTORAGE) && (fe->tymed & TYMED_HGLOBAL)) {
      m->tymed = TYMED_HGLOBAL;
      m->hGlobal = DuplicateData(h, fe->cfFormat, GMEM_MOVEABLE);
    } else if ((t == TYMED_HGLOBAL || t == TYMED_ISTORAGE) && (fe->tymed & TYMED_ISTREAM)) {
      HGLOBAL copy = DuplicateData(h, fe->cfFormat, GMEM_MOVEABLE);
      if (!copy || FAILED(CreateStreamOnHGlobal(copy, TRUE, &m->pstm))) return E_OUTOFMEMORY;
      m->tymed = TYMED_ISTREAM;
    } else {
      return DV_E_TYMED;
    }
    m->pUnkForRelease = NULL;
    return m->hGlobal || m->pstg ? S_OK : E_OUTOFMEMORY;
  }
};

bool Offers(IDataObject* d, CLIPFORMAT cf, DWORD tymed) {
  FORMATETC fe = {cf, NULL, DVASPECT_CONTENT, -1, tymed};
  return d->QueryGetData(&fe) == S_OK;
}

}  // namespace

extern "C" {

HRESULT WINAPI OleSetClipboard(LPDATAOBJECT data) {
  winport::com::ClearForeignClipboard();
  HWND w = ClipWindow();
  if (!w || !OpenClipboard(w)) return CLIPBRD_E_CANT_OPEN;
  EmptyClipboard();  // lets the object before go
  SafeRelease(g_clip);
  HRESULT hr = S_OK;
  if (data) {
    g_clip = data;
    data->AddRef();
    IEnumFORMATETC* e = NULL;
    if (SUCCEEDED(data->EnumFormatEtc(DATADIR_GET, &e)) && e) {
      FORMATETC fe;
      ULONG n = 0;
      while (e->Next(1, &fe, &n) == S_OK && n == 1) {
        if (fe.ptd) CoTaskMemFree(fe.ptd);
        if (!IsClipboardFormatAvailable(fe.cfFormat)) SetClipboardData(fe.cfFormat, NULL);
      }
      e->Release();
    } else {
      hr = CLIPBRD_E_CANT_SET;
    }
  }
  CloseClipboard();
  winport::com::OwnClipboard(data != NULL && SUCCEEDED(hr));
  return hr;
}

HRESULT WINAPI OleGetClipboard(LPDATAOBJECT* out) {
  if (!out) return E_INVALIDARG;
  if (!OursOnClipboard() && SUCCEEDED(winport::com::ForeignClipboard(out))) return S_OK;
  *out = new ClipboardData;
  return S_OK;
}

HRESULT WINAPI OleIsCurrentClipboard(LPDATAOBJECT data) {
  if (!data) return S_FALSE;
  return OursOnClipboard() && data == g_clip ? S_OK : S_FALSE;
}

HRESULT WINAPI OleFlushClipboard(void) {
  if (!OursOnClipboard()) return S_OK;
  if (!OpenClipboard(g_clipWnd)) return CLIPBRD_E_CANT_OPEN;
  std::vector<UINT> formats;
  for (UINT cf = EnumClipboardFormats(0); cf; cf = EnumClipboardFormats(cf)) formats.push_back(cf);
  for (UINT cf : formats) Render((CLIPFORMAT)cf);
  // The rendered data stays; the object goes.
  IDataObject* d = g_clip;
  g_clip = NULL;
  CloseClipboard();
  SafeRelease(d);
  winport::com::OwnClipboard(false);  // what was rendered stays in this program
  return S_OK;
}

// Whether data makes an object: S_OK for an embedded one, OLE_S_STATIC
// for a picture only.
HRESULT WINAPI OleQueryCreateFromData(LPDATAOBJECT data) {
  if (!data) return E_INVALIDARG;
  if (Offers(data, Format(L"Embedded Object"), TYMED_ISTORAGE) ||
      Offers(data, Format(L"Embed Source"), TYMED_ISTORAGE) || Offers(data, Format(L"FileName"), TYMED_HGLOBAL))
    return S_OK;
  if (Offers(data, CF_METAFILEPICT, TYMED_MFPICT) || Offers(data, CF_ENHMETAFILE, TYMED_ENHMF) ||
      Offers(data, CF_DIB, TYMED_HGLOBAL) || Offers(data, CF_BITMAP, TYMED_GDI))
    return OLE_S_STATIC;
  return S_FALSE;
}

HRESULT WINAPI OleQueryLinkFromData(LPDATAOBJECT data) {
  if (!data) return E_INVALIDARG;
  return Offers(data, Format(L"Link Source"), TYMED_ISTREAM) ? S_OK : S_FALSE;
}

// An embedded object from data: a copy of the object's storage, loaded;
// or, from a picture alone, a static object.
HRESULT WINAPI OleCreateFromData(LPDATAOBJECT data, REFIID riid, DWORD render, LPFORMATETC fe,
                                 LPOLECLIENTSITE site, LPSTORAGE stg, LPVOID* out) {
  if (!data || !out) return E_INVALIDARG;
  *out = NULL;
  const wchar_t* names[] = {L"Embedded Object", L"Embed Source"};
  for (const wchar_t* name : names) {
    FORMATETC f = {Format(name), NULL, DVASPECT_CONTENT, -1, TYMED_ISTORAGE};
    if (data->QueryGetData(&f) != S_OK) continue;
    HRESULT hr = E_FAIL;
    if (stg) {
      STGMEDIUM here = {};
      here.tymed = TYMED_ISTORAGE;
      here.pstg = stg;
      hr = data->GetDataHere(&f, &here);
    }
    if (FAILED(hr)) {
      STGMEDIUM m = {};
      hr = data->GetData(&f, &m);
      if (FAILED(hr)) continue;
      hr = stg ? m.pstg->CopyTo(0, NULL, NULL, stg) : E_INVALIDARG;
      ReleaseStgMedium(&m);
      if (FAILED(hr)) return hr;
    }
    stg->Commit(STGC_DEFAULT);
    IUnknown* obj = NULL;
    hr = OleLoad(stg, IID_IUnknown, site, (void**)&obj);
    if (FAILED(hr)) return hr;
    // The picture asked for, from the data when it has one.
    if (render != OLERENDER_NONE) {
      AddRenderCache(obj, render, fe);
      FillCache(obj, data);
    }
    hr = obj->QueryInterface(riid, out);
    obj->Release();
    return hr;
  }
  // A file the shell dropped or copied: the object it holds.
  std::wstring file = FileNameOf(data);
  if (!file.empty()) return OleCreateFromFile(CLSID_NULL, file.c_str(), riid, render, fe, site, stg, out);
  if (OleQueryCreateFromData(data) == OLE_S_STATIC)
    return OleCreateStaticFromData(data, riid, render, fe, site, stg, out);
  return DV_E_FORMATETC;
}

// A link from data: the source's moniker and class, as "Link Source"
// holds them.
HRESULT WINAPI OleCreateLinkFromData(LPDATAOBJECT data, REFIID riid, DWORD render, LPFORMATETC fe,
                                     LPOLECLIENTSITE site, LPSTORAGE stg, LPVOID* out) {
  if (!data || !out) return E_INVALIDARG;
  *out = NULL;
  FORMATETC f = {Format(L"Link Source"), NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
  STGMEDIUM m = {};
  HRESULT hr = data->GetData(&f, &m);
  if (FAILED(hr)) {
    // Only a file: a link to it.
    std::wstring file = FileNameOf(data);
    if (file.empty()) return hr;
    return OleCreateLinkToFile(file.c_str(), riid, render, fe, site, stg, out);
  }
  if (m.tymed != TYMED_ISTREAM || !m.pstm) {
    ReleaseStgMedium(&m);
    return DV_E_TYMED;
  }
  LARGE_INTEGER zero = {};
  m.pstm->Seek(zero, STREAM_SEEK_SET, NULL);
  IMoniker* mk = NULL;
  hr = OleLoadFromStream(m.pstm, IID_IMoniker, (void**)&mk);
  ReleaseStgMedium(&m);
  if (FAILED(hr)) return hr;
  hr = OleCreateLink(mk, riid, render, fe, site, stg, out);
  mk->Release();
  return hr;
}

}  // extern "C"

HRESULT winport::com::LocalClipboardObject(IDataObject** out) {
  *out = NULL;
  if (!OursOnClipboard()) return S_FALSE;
  *out = g_clip;
  g_clip->AddRef();
  return S_OK;
}
