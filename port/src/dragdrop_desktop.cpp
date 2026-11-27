// dragdrop_desktop.cpp - drags between this program and others. What
// another program drags over a window goes to the drop target under it
// (RegisterDragDrop) as a data object of files and text, or to a window
// that takes files (DragAcceptFiles) as WM_DROPFILES. The backend says
// where the drag is from any thread; the window's thread handles it, as
// an input message of its own, and the answers go back to the backend.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"
#include "ole.h"

#include <shlobj.h>
#include <string.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace {

// The data object over another program's drag: CF_HDROP for its files,
// CF_UNICODETEXT and CF_TEXT for its text, fetched when first asked for.
class OfferData final : public IDataObject {
 public:
  explicit OfferData(const DesktopOfferPtr& offer) : offer_(offer) {}

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
    HRESULT hr = QueryGetData(fe);
    if (hr != S_OK) return hr;
    HGLOBAL h = Render(fe->cfFormat);
    if (!h) return E_FAIL;
    if (fe->tymed & TYMED_HGLOBAL) {
      m->tymed = TYMED_HGLOBAL;
      m->hGlobal = h;
      return S_OK;
    }
    if (FAILED(CreateStreamOnHGlobal(h, TRUE, &m->pstm))) {
      GlobalFree(h);
      return E_OUTOFMEMORY;
    }
    m->tymed = TYMED_ISTREAM;
    return S_OK;
  }
  STDMETHOD(GetDataHere)(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
  STDMETHOD(QueryGetData)(FORMATETC* fe) override {
    if (!fe) return E_INVALIDARG;
    if (!Has(fe->cfFormat)) return DV_E_FORMATETC;
    if (fe->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
    if (!(fe->tymed & (TYMED_HGLOBAL | TYMED_ISTREAM))) return DV_E_TYMED;
    return S_OK;
  }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* in, FORMATETC* out) override {
    if (!in || !out) return E_INVALIDARG;
    *out = *in;
    out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
  }
  STDMETHOD(SetData)(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  STDMETHOD(EnumFormatEtc)(DWORD dir, IEnumFORMATETC** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (dir != DATADIR_GET) return E_NOTIMPL;
    std::vector<FORMATETC> v;
    for (CLIPFORMAT cf : {(CLIPFORMAT)CF_HDROP, (CLIPFORMAT)CF_UNICODETEXT, (CLIPFORMAT)CF_TEXT})
      if (Has(cf)) v.push_back(FORMATETC{cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
    *out = new ole::FormatEnum(v);
    return S_OK;
  }
  STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(DUnadvise)(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

 private:
  bool Has(CLIPFORMAT cf) {
    if (cf == CF_HDROP) return offer_->files;
    if (cf == CF_UNICODETEXT || cf == CF_TEXT) return offer_->text;
    return false;
  }
  // Fetches a kind once.
  bool Bytes(bool files, std::string* out) {
    int i = files ? 0 : 1;
    if (!fetched_[i]) {
      fetched_[i] = true;
      got_[i] = offer_->Fetch(files, &bytes_[i]);
    }
    *out = bytes_[i];
    return got_[i];
  }
  HGLOBAL Render(CLIPFORMAT cf) {
    std::string bytes;
    if (cf == CF_HDROP) {
      if (!Bytes(true, &bytes)) return NULL;
      std::vector<std::wstring> paths = PathsFromUris(bytes);
      if (paths.empty()) return NULL;
      POINT pt = {0, 0};
      return MakeDrop(paths, pt, false);
    }
    if (!Bytes(false, &bytes)) return NULL;
    size_t size;
    std::wstring wide;
    if (cf == CF_UNICODETEXT) {
      wide = Widen(bytes.c_str(), (int)bytes.size());
      size = (wide.size() + 1) * sizeof(wchar_t);
    } else {
      // The ANSI code page is UTF-8.
      size = bytes.size() + 1;
    }
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!h) return NULL;
    char* p = (char*)GlobalLock(h);
    if (cf == CF_UNICODETEXT)
      memcpy(p, wide.c_str(), size);
    else
      memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(h);
    return h;
  }

  DesktopOfferPtr offer_;
  bool fetched_[2] = {false, false};
  bool got_[2] = {false, false};
  std::string bytes_[2];
};

// The drag now over the program's windows.
struct Drag {
  DesktopOfferPtr offer;
  IDataObject* data = NULL;
  IDropTarget* target = NULL;
  HWND window = NULL;  // the target's
};
Drag g_drag;

void ReleaseTarget() {
  if (g_drag.target) g_drag.target->Release();
  g_drag.target = NULL;
  g_drag.window = NULL;
}

void Forget() {
  ReleaseTarget();
  if (g_drag.data) g_drag.data->Release();
  g_drag.data = NULL;
  g_drag.offer.reset();
}

// The effect a window that takes files (and no drop target) gives.
DWORD FilesEffect(const DesktopOfferPtr& offer, POINT screen, DWORD allowed) {
  if (!offer->files || !DropTarget(screen)) return DROPEFFECT_NONE;
  if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
  if (allowed & DROPEFFECT_MOVE) return DROPEFFECT_MOVE;
  return allowed & DROPEFFECT_LINK;
}

// What the backend says, waiting for the window's thread.
enum Kind { kOver, kLeave, kDrop };
struct Event {
  Kind kind;
  DesktopOfferPtr offer;
  POINT screen;
  DWORD keys, allowed;
};
const UINT kDesktopDrag = 0x0299;
std::mutex g_eventsLock;
std::map<LPARAM, Event>& Events() {
  static std::map<LPARAM, Event> m;
  return m;
}
LPARAM g_nextEvent = 1;

void Post(Window* top, const Event& e) {
  LPARAM id;
  {
    std::lock_guard<std::mutex> g(g_eventsLock);
    id = g_nextEvent++;
    Events()[id] = e;
  }
  UserGuard g(UserLock());
  if (!top || !GetWindow(top->hwnd)) {
    std::lock_guard<std::mutex> g2(g_eventsLock);
    Events().erase(id);
    return;
  }
  QueueInput(top, kDesktopDrag, 0, id);
}

}  // namespace

namespace winport {

DWORD DesktopDragOverNow(const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed) {
  if (!offer) return DROPEFFECT_NONE;
  if (offer != g_drag.offer) {
    DesktopDragLeaveNow(g_drag.offer);
    g_drag.offer = offer;
    if (offer->local) {
      g_drag.data = offer->local;
      g_drag.data->AddRef();
    } else {
      g_drag.data = new OfferData(offer);
    }
  }
  HWND where;
  IDropTarget* t = OleDropTargetAt(screen, &where);
  POINTL p = {screen.x, screen.y};
  DWORD e = DROPEFFECT_NONE;
  if (t != g_drag.target || where != g_drag.window) {
    if (g_drag.target) g_drag.target->DragLeave();
    ReleaseTarget();
    if (t) {
      g_drag.target = t;
      g_drag.window = where;
      t->AddRef();
      e = allowed;
      if (FAILED(t->DragEnter(g_drag.data, keys, p, &e))) e = DROPEFFECT_NONE;
    }
  } else if (t) {
    e = allowed;
    if (FAILED(t->DragOver(keys, p, &e))) e = DROPEFFECT_NONE;
  }
  if (!g_drag.target) return FilesEffect(offer, screen, allowed);
  return e & allowed;
}

void DesktopDragLeaveNow(const DesktopOfferPtr& offer) {
  if (!offer || offer != g_drag.offer) return;
  if (g_drag.target) g_drag.target->DragLeave();
  Forget();
}

DWORD DesktopDropNow(const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed) {
  if (!offer) return DROPEFFECT_NONE;
  // The target under the drop, entered if it has not been.
  if (offer != g_drag.offer || !g_drag.target) DesktopDragOverNow(offer, screen, keys, allowed);
  DWORD e = DROPEFFECT_NONE;
  if (g_drag.target) {
    IDropTarget* t = g_drag.target;
    IDataObject* data = g_drag.data;
    t->AddRef();
    data->AddRef();
    Forget();
    POINTL p = {screen.x, screen.y};
    e = allowed;
    if (FAILED(t->Drop(data, keys, p, &e))) e = DROPEFFECT_NONE;
    e &= allowed;
    t->Release();
    data->Release();
    return e;
  }
  Forget();
  e = FilesEffect(offer, screen, allowed);
  if (e == DROPEFFECT_NONE) return e;
  std::string uris;
  if (!offer->Fetch(true, &uris) || !DropFiles(screen, PathsFromUris(uris))) return DROPEFFECT_NONE;
  return e;
}

void DesktopDragOver(Window* top, const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed) {
  Post(top, Event{kOver, offer, screen, keys, allowed});
}
void DesktopDragLeave(Window* top, const DesktopOfferPtr& offer) {
  Post(top, Event{kLeave, offer, POINT{0, 0}, 0, 0});
}
void DesktopDrop(Window* top, const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed) {
  Post(top, Event{kDrop, offer, screen, keys, allowed});
}

bool IsDesktopDrag(UINT msg) { return msg == kDesktopDrag; }

void DeliverDesktopDrag(LPARAM id) {
  Event e;
  {
    std::lock_guard<std::mutex> g(g_eventsLock);
    auto it = Events().find(id);
    if (it == Events().end()) return;
    e = it->second;
    Events().erase(it);
  }
  switch (e.kind) {
    case kOver: e.offer->Answer(DesktopDragOverNow(e.offer, e.screen, e.keys, e.allowed)); break;
    case kLeave: DesktopDragLeaveNow(e.offer); break;
    case kDrop: e.offer->Finished(DesktopDropNow(e.offer, e.screen, e.keys, e.allowed)); break;
  }
}

bool DataFiles(IDataObject* data, std::vector<std::wstring>* paths) {
  FORMATETC f = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM m = {};
  if (!data || FAILED(data->GetData(&f, &m))) return false;
  bool ok = false;
  if (m.tymed == TYMED_HGLOBAL) {
    UINT n = DragQueryFileW((HDROP)m.hGlobal, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < n; i++) {
      UINT len = DragQueryFileW((HDROP)m.hGlobal, i, NULL, 0);
      std::wstring p(len + 1, L'\0');
      DragQueryFileW((HDROP)m.hGlobal, i, &p[0], len + 1);
      p.resize(len);
      paths->push_back(p);
    }
    ok = !paths->empty();
  }
  ReleaseStgMedium(&m);
  return ok;
}

bool DataText(IDataObject* data, std::wstring* text) {
  if (!data) return false;
  for (CLIPFORMAT cf : {(CLIPFORMAT)CF_UNICODETEXT, (CLIPFORMAT)CF_TEXT}) {
    FORMATETC f = {cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM m = {};
    if (FAILED(data->GetData(&f, &m))) continue;
    bool ok = false;
    if (m.tymed == TYMED_HGLOBAL) {
      SIZE_T size = GlobalSize(m.hGlobal);
      if (const char* p = (const char*)GlobalLock(m.hGlobal)) {
        if (cf == CF_UNICODETEXT) {
          const wchar_t* w = (const wchar_t*)p;
          size_t n = 0;
          while (n < size / sizeof(wchar_t) && w[n]) n++;
          text->assign(w, n);
        } else {
          size_t n = 0;
          while (n < size && p[n]) n++;
          *text = Widen(p, (int)n);
        }
        GlobalUnlock(m.hGlobal);
        ok = true;
      }
    }
    ReleaseStgMedium(&m);
    if (ok) return true;
  }
  return false;
}

}  // namespace winport
