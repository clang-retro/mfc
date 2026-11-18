// propframe.cpp - the property frame (OleCreatePropertyFrame): a modal
// dialog with a tab for each property page, and OK, Cancel and Apply.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <commctrl.h>
#include <ocidl.h>
#include <olectl.h>

#include <atomic>
#include <string>
#include <vector>

#include "dlgtemplate.h"

namespace {

enum { kApply = 0x3021, kTab = 0x3020 };
const int kMargin = 7, kButtonWidth = 75, kButtonHeight = 23;

struct Frame;

// The site each page reports to.
class Site final : public IPropertyPageSite {
 public:
  Site(Frame* f, LCID lcid) : refs_(1), frame_(f), lcid_(lcid) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPropertyPageSite)) {
      *out = static_cast<IPropertyPageSite*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(OnStatusChange)(DWORD flags) override;
  STDMETHOD(GetLocaleID)(LCID* p) override {
    if (p == NULL) return E_POINTER;
    *p = lcid_;
    return S_OK;
  }
  STDMETHOD(GetPageContainer)(IUnknown** p) override {
    if (p) *p = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(TranslateAccelerator)(MSG*) override { return S_FALSE; }
  void Detach() { frame_ = NULL; }

 private:
  std::atomic<ULONG> refs_;
  Frame* frame_;
  LCID lcid_;
};

struct Page {
  IPropertyPage* page = NULL;
  Site* site = NULL;
  std::wstring title;
  SIZE size = {0, 0};
  bool active = false;
};

struct Frame {
  std::wstring caption;
  std::vector<Page> pages;
  HWND hwnd = NULL, tab = NULL;
  int current = -1;
};

HRESULT Site::OnStatusChange(DWORD flags) {
  if (frame_ && frame_->hwnd && (flags & PROPPAGESTATUS_DIRTY))
    EnableWindow(GetDlgItem(frame_->hwnd, kApply), TRUE);
  return S_OK;
}

void ApplyAll(Frame* f) {
  for (Page& p : f->pages)
    if (p.active && p.page->IsPageDirty() == S_OK) p.page->Apply();
  EnableWindow(GetDlgItem(f->hwnd, kApply), FALSE);
}

void Select(Frame* f, int i) {
  if (i == f->current || i < 0 || i >= (int)f->pages.size()) return;
  if (f->current >= 0 && f->pages[f->current].active) f->pages[f->current].page->Show(SW_HIDE);
  f->current = i;
  if (f->pages[i].active) f->pages[i].page->Show(SW_SHOWNORMAL);
}

HWND Child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int cx, int cy,
           int id, HFONT font) {
  HWND h = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, cx, cy, parent,
                           (HMENU)(INT_PTR)id, NULL, NULL);
  if (h && font) SendMessageW(h, WM_SETFONT, (WPARAM)font, FALSE);
  return h;
}

void Init(HWND h, Frame* f) {
  f->hwnd = h;
  SetWindowTextW(h, f->caption.c_str());
  HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);

  // The page area is the largest page.
  int cx = 0, cy = 0;
  for (const Page& p : f->pages) {
    if (p.size.cx > cx) cx = p.size.cx;
    if (p.size.cy > cy) cy = p.size.cy;
  }
  if (cx < 3 * kButtonWidth + 2 * kMargin) cx = 3 * kButtonWidth + 2 * kMargin;

  f->tab = Child(h, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS | WS_TABSTOP, kMargin, kMargin, cx, cy, kTab, font);
  for (size_t i = 0; i < f->pages.size(); i++) {
    TCITEMW item = {};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(f->pages[i].title.c_str());
    SendMessageW(f->tab, TCM_INSERTITEMW, i, (LPARAM)&item);
  }
  // Grow the tab control until its display area holds the pages.
  RECT disp = {0, 0, cx, cy};
  SendMessageW(f->tab, TCM_ADJUSTRECT, TRUE, (LPARAM)&disp);
  int tabW = disp.right - disp.left, tabH = disp.bottom - disp.top;
  SetWindowPos(f->tab, NULL, 0, 0, tabW, tabH, SWP_NOMOVE | SWP_NOZORDER);
  RECT area = {0, 0, tabW, tabH};
  SendMessageW(f->tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&area);
  OffsetRect(&area, kMargin, kMargin);

  int by = kMargin + tabH + kMargin;
  int bx = kMargin + tabW - 3 * kButtonWidth - 2 * 6;
  Child(h, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, bx, by, kButtonWidth, kButtonHeight, IDOK, font);
  Child(h, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, bx + kButtonWidth + 6, by, kButtonWidth,
        kButtonHeight, IDCANCEL, font);
  HWND apply = Child(h, L"BUTTON", L"&Apply", BS_PUSHBUTTON | WS_TABSTOP, bx + 2 * (kButtonWidth + 6), by,
                     kButtonWidth, kButtonHeight, kApply, font);
  EnableWindow(apply, FALSE);

  // Size the frame around its client area.
  RECT win = {0, 0, kMargin + tabW + kMargin, by + kButtonHeight + kMargin};
  AdjustWindowRectEx(&win, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
  SetWindowPos(h, NULL, 0, 0, win.right - win.left, win.bottom - win.top, SWP_NOMOVE | SWP_NOZORDER);
  // Center on the owner, or the screen.
  RECT outer, self;
  HWND owner = GetWindow(h, GW_OWNER);
  if (owner) GetWindowRect(owner, &outer);
  else SetRect(&outer, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
  GetWindowRect(h, &self);
  int x = outer.left + ((outer.right - outer.left) - (self.right - self.left)) / 2;
  int y = outer.top + ((outer.bottom - outer.top) - (self.bottom - self.top)) / 2;
  SetWindowPos(h, NULL, x < 0 ? 0 : x, y < 0 ? 0 : y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

  // Every page stays active for the frame's life; the tab shows one.
  for (Page& p : f->pages) {
    p.active = SUCCEEDED(p.page->Activate(h, &area, TRUE));
    if (p.active) p.page->Show(SW_HIDE);
  }
  Select(f, 0);
}

void Deactivate(Frame* f) {
  for (Page& p : f->pages) {
    if (p.active) p.page->Deactivate();
    p.active = false;
  }
}

INT_PTR CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Frame* f = (Frame*)GetWindowLongPtrW(h, DWLP_USER);
  switch (msg) {
    case WM_INITDIALOG:
      SetWindowLongPtrW(h, DWLP_USER, lp);
      Init(h, (Frame*)lp);
      return TRUE;
    case WM_NOTIFY: {
      NMHDR* nm = (NMHDR*)lp;
      if (f && nm->hwndFrom == f->tab && nm->code == TCN_SELCHANGE)
        Select(f, (int)SendMessageW(f->tab, TCM_GETCURSEL, 0, 0));
      return FALSE;
    }
    case WM_COMMAND:
      if (f == NULL) return FALSE;
      switch (LOWORD(wp)) {
        case IDOK:
          ApplyAll(f);
          Deactivate(f);
          EndDialog(h, IDOK);
          return TRUE;
        case IDCANCEL:
          Deactivate(f);
          EndDialog(h, IDCANCEL);
          return TRUE;
        case kApply:
          ApplyAll(f);
          return TRUE;
      }
      return FALSE;
    case WM_DESTROY:
      if (f) {
        Deactivate(f);
        f->hwnd = NULL;
      }
      return FALSE;
  }
  return FALSE;
}

}  // namespace

extern "C" {

HRESULT WINAPI OleCreatePropertyFrameIndirect(LPOCPFIPARAMS params) {
  if (params == NULL || params->cbStructSize < sizeof(OCPFIPARAMS)) return E_INVALIDARG;
  if (params->cObjects > 0 && params->lplpUnk == NULL) return E_POINTER;
  if (params->cPages > 0 && params->lpPages == NULL) return E_POINTER;

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES};
  InitCommonControlsEx(&icc);

  Frame frame;
  frame.caption = params->lpszCaption ? params->lpszCaption : L"";
  frame.caption += frame.caption.empty() ? L"Properties" : L" Properties";
  for (ULONG i = 0; i < params->cPages; i++) {
    IPropertyPage* pp = NULL;
    if (FAILED(CoCreateInstance(params->lpPages[i], NULL, CLSCTX_INPROC_SERVER, IID_IPropertyPage,
                                (void**)&pp)))
      continue;
    Page p;
    p.page = pp;
    p.site = new Site(&frame, params->lcid);
    pp->SetPageSite(p.site);
    PROPPAGEINFO info = {};
    info.cb = sizeof(info);
    if (SUCCEEDED(pp->GetPageInfo(&info))) {
      if (info.pszTitle) p.title = info.pszTitle;
      p.size = info.size;
      CoTaskMemFree(info.pszTitle);
      CoTaskMemFree(info.pszDocString);
      CoTaskMemFree(info.pszHelpFile);
    }
    if (FAILED(pp->SetObjects(params->cObjects, params->lplpUnk))) {
      pp->SetPageSite(NULL);
      p.site->Detach();
      p.site->Release();
      pp->Release();
      continue;
    }
    frame.pages.push_back(p);
  }

  HRESULT hr = S_OK;
  if (!frame.pages.empty()) {
    winport::Template t(0, 100, 50, L"");
    if (DialogBoxIndirectParamW(NULL, t.Get(), params->hWndOwner, Proc, (LPARAM)&frame) == -1) hr = E_FAIL;
  }

  for (Page& p : frame.pages) {
    if (p.active) p.page->Deactivate();
    p.page->SetObjects(0, NULL);
    p.page->SetPageSite(NULL);
    p.site->Detach();
    p.site->Release();
    p.page->Release();
  }
  return hr;
}

HRESULT WINAPI OleCreatePropertyFrame(HWND owner, UINT x, UINT y, LPCOLESTR caption, ULONG cObjects,
                                      LPUNKNOWN* ppUnk, ULONG cPages, LPCLSID pages, LCID lcid, DWORD reserved,
                                      LPVOID pvReserved) {
  (void)reserved;
  (void)pvReserved;
  OCPFIPARAMS p = {};
  p.cbStructSize = sizeof(p);
  p.hWndOwner = owner;
  p.x = (int)x;
  p.y = (int)y;
  p.lpszCaption = caption;
  p.cObjects = cObjects;
  p.lplpUnk = ppUnk;
  p.cPages = cPages;
  p.lpPages = pages;
  p.lcid = lcid;
  p.dispidInitialProperty = DISPID_UNKNOWN;
  return OleCreatePropertyFrameIndirect(&p);
}

}  // extern "C"
