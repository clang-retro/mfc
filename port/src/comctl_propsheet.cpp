// comctl_propsheet.cpp - property sheets and wizards: a dialog that
// holds pages, either behind tabs with OK, Cancel and Apply, or one
// after another with Back, Next and Finish.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <prsht.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

// HPROPSHEETPAGE points at one of these: a copy of the page's description.
struct PageData : HPROPSHEETPAGE__ {
  std::vector<BYTE> bytes;  // the PROPSHEETPAGE, at least the full size
  bool unicode = true;
  PROPSHEETPAGEW* psp() { return (PROPSHEETPAGEW*)bytes.data(); }
};

namespace {

// The sheet's own controls.
const int kTab = 0x3020, kApply = 0x3021, kBack = 0x3023, kNext = 0x3024, kFinish = 0x3025, kLine = 0x3026;
const wchar_t kSheetProp[] = L"winport.propsheet";

PageData* NewPage(const void* psp, bool unicode) {
  const PROPSHEETPAGEW* in = (const PROPSHEETPAGEW*)psp;
  if (!in || in->dwSize < offsetof(PROPSHEETPAGEW, pcRefParent)) return NULL;
  PageData* p = new PageData;
  p->unicode = unicode;
  p->bytes.assign(std::max((size_t)in->dwSize, sizeof(PROPSHEETPAGEW)), 0);
  memcpy(p->bytes.data(), in, in->dwSize);
  if (in->dwSize < sizeof(PROPSHEETPAGEW)) {
    // The short form has no header strings.
    p->psp()->pszHeaderTitle = NULL;
    p->psp()->pszHeaderSubTitle = NULL;
  }
  if ((p->psp()->dwFlags & PSP_USEREFPARENT) && p->psp()->pcRefParent) ++*p->psp()->pcRefParent;
  return p;
}

void FreePage(PageData* p) {
  if (!p) return;
  PROPSHEETPAGEW* psp = p->psp();
  if ((psp->dwFlags & PSP_USECALLBACK) && psp->pfnCallback) {
    if (p->unicode)
      psp->pfnCallback(NULL, PSPCB_RELEASE, psp);
    else
      ((LPFNPSPCALLBACKA)psp->pfnCallback)(NULL, PSPCB_RELEASE, (PROPSHEETPAGEA*)psp);
  }
  if ((psp->dwFlags & PSP_USEREFPARENT) && psp->pcRefParent) --*psp->pcRefParent;
  delete p;
}

std::wstring ResString(HINSTANCE inst, const void* s, bool unicode) {
  if (!s) return L"";
  if (IS_INTRESOURCE(s)) {
    wchar_t buf[256] = L"";
    LoadStringW(inst, (UINT)(UINT_PTR)s, buf, 256);
    return buf;
  }
  return unicode ? std::wstring((const wchar_t*)s) : Widen((const char*)s);
}

const void* PageTemplate(PageData* p) {
  PROPSHEETPAGEW* psp = p->psp();
  if (psp->dwFlags & PSP_DLGINDIRECT) return psp->pResource;
  std::wstring name;
  LPCWSTR id = psp->pszTemplate;
  if (!p->unicode && !IS_INTRESOURCE(id)) {
    name = Widen((const char*)id);
    id = name.c_str();
  }
  HRSRC r = FindResourceW(psp->hInstance, id, (LPCWSTR)RT_DIALOG);
  return r ? LockResource(LoadResource(psp->hInstance, r)) : NULL;
}

// The parts of a dialog template's header the sheet needs.
struct Header {
  bool ex = false;
  DWORD style = 0, exStyle = 0;
  short cx = 0, cy = 0;
  std::wstring title;
  bool hasFont = false;
  WORD pointSize = 8, weight = FW_NORMAL;
  BYTE italic = 0, charset = DEFAULT_CHARSET;
  std::wstring face;
};

const WORD* SkipSzOrOrd(const WORD* p, std::wstring* out) {
  if (out) out->clear();
  if (*p == 0xFFFF) return p + 2;
  while (*p) {
    if (out) out->push_back((wchar_t)*p);
    p++;
  }
  return p + 1;
}

Header ReadHeader(const void* tmpl) {
  Header h;
  if (!tmpl) return h;
  const WORD* p = (const WORD*)tmpl;
  h.ex = p[0] == 1 && p[1] == 0xFFFF;
  const BYTE* b = (const BYTE*)tmpl;
  if (h.ex) {
    memcpy(&h.exStyle, b + 8, 4);
    memcpy(&h.style, b + 12, 4);
    memcpy(&h.cx, b + 22, 2);
    memcpy(&h.cy, b + 24, 2);
    p = (const WORD*)(b + 26);
  } else {
    memcpy(&h.style, b, 4);
    memcpy(&h.exStyle, b + 4, 4);
    memcpy(&h.cx, b + 14, 2);
    memcpy(&h.cy, b + 16, 2);
    p = (const WORD*)(b + 18);
  }
  p = SkipSzOrOrd(p, NULL);  // the menu
  p = SkipSzOrOrd(p, NULL);  // the class
  p = SkipSzOrOrd(p, &h.title);
  if (h.style & DS_SETFONT) {
    h.hasFont = true;
    h.pointSize = *p++;
    if (h.ex) {
      h.weight = *p++;
      h.italic = *(const BYTE*)p;
      h.charset = *((const BYTE*)p + 1);
      p++;
    }
    SkipSzOrOrd(p, &h.face);
  }
  return h;
}

struct Page {
  PageData* data = NULL;
  HWND hwnd = NULL;
  std::wstring title;
  short cx = 0, cy = 0;  // in dialog units
};

struct Sheet {
  HWND hwnd = NULL, tab = NULL, owner = NULL;
  bool unicode = true, wizard = false, modeless = false, ended = false;
  DWORD flags = 0;
  HINSTANCE inst = NULL;
  std::wstring caption;
  std::vector<Page> pages;
  int cur = -1, start = 0;
  std::vector<HWND> changed;
  bool applied = false, cancelToClose = false;
  INT_PTR result = 0;
  int restart = 0;  // ID_PSRESTARTWINDOWS or ID_PSREBOOTSYSTEM
  PFNPROPSHEETCALLBACK callback = NULL;
  RECT pageRect = {0, 0, 0, 0};  // where pages go, in the sheet's client
};

Sheet* SheetOf(HWND h) { return (Sheet*)GetPropW(h, kSheetProp); }

PROPSHEETPAGEW* Psp(Sheet* s, int i) { return s->pages[i].data->psp(); }

LRESULT NotifyPage(Sheet* s, int i, UINT code, LPARAM lp = 0) {
  if (i < 0 || i >= (int)s->pages.size() || !s->pages[i].hwnd) return 0;
  PSHNOTIFY nm = {};
  nm.lParam = lp;
  return SendNotify(s->hwnd, s->pages[i].hwnd, code, &nm.hdr);
}

void AddPageInfo(Sheet* s, int at, PageData* data) {
  Page pg;
  pg.data = data;
  PROPSHEETPAGEW* psp = data->psp();
  Header h = ReadHeader(PageTemplate(data));
  pg.cx = h.cx;
  pg.cy = h.cy;
  pg.title = (psp->dwFlags & PSP_USETITLE) ? ResString(psp->hInstance, psp->pszTitle, data->unicode) : h.title;
  at = std::max(0, std::min(at, (int)s->pages.size()));
  s->pages.insert(s->pages.begin() + at, pg);
  if (s->cur >= at) s->cur++;
  if (s->tab) {
    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)pg.title.c_str();
    SendMessageW(s->tab, TCM_INSERTITEMW, at, (LPARAM)&ti);
  }
}

bool CreatePage(Sheet* s, int i) {
  Page& pg = s->pages[i];
  if (pg.hwnd) return true;
  PROPSHEETPAGEW* psp = pg.data->psp();
  const void* tmpl = PageTemplate(pg.data);
  size_t size = DialogTemplateSize(tmpl);
  if (!tmpl || !size) return false;
  // A page is a control inside the sheet, whatever its template says.
  std::vector<BYTE> copy((const BYTE*)tmpl, (const BYTE*)tmpl + size);
  Header h = ReadHeader(tmpl);
  DWORD style = (h.style & ~(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | WS_VISIBLE |
                             WS_DISABLED | DS_CENTER | DS_SYSMODAL)) |
                WS_CHILD | DS_CONTROL | DS_3DLOOK;
  DWORD exStyle = (h.exStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)) | WS_EX_CONTROLPARENT;
  memcpy(copy.data() + (h.ex ? 12 : 0), &style, 4);
  memcpy(copy.data() + (h.ex ? 8 : 4), &exStyle, 4);
  if ((psp->dwFlags & PSP_USECALLBACK) && psp->pfnCallback) {
    UINT ok = pg.data->unicode ? psp->pfnCallback(NULL, PSPCB_CREATE, psp)
                               : ((LPFNPSPCALLBACKA)psp->pfnCallback)(NULL, PSPCB_CREATE, (PROPSHEETPAGEA*)psp);
    if (!ok) return false;
  }
  HWND h2 = pg.data->unicode
                ? CreateDialogIndirectParamW(psp->hInstance, (LPCDLGTEMPLATEW)copy.data(), s->hwnd, psp->pfnDlgProc,
                                             (LPARAM)psp)
                : CreateDialogIndirectParamA(psp->hInstance, (LPCDLGTEMPLATEA)copy.data(), s->hwnd, psp->pfnDlgProc,
                                             (LPARAM)psp);
  if (!h2) return false;
  if (i >= (int)s->pages.size() || s->pages[i].data->psp() != psp) return false;
  s->pages[i].hwnd = h2;
  SetWindowPos(h2, HWND_TOP, s->pageRect.left, s->pageRect.top, s->pageRect.right - s->pageRect.left,
               s->pageRect.bottom - s->pageRect.top, SWP_NOACTIVATE);
  return true;
}

int IndexOfId(Sheet* s, LRESULT id) {
  for (size_t i = 0; i < s->pages.size(); i++) {
    PROPSHEETPAGEW* psp = Psp(s, (int)i);
    if ((psp->dwFlags & PSP_DLGINDIRECT) ? (LRESULT)psp->pResource == id : (LRESULT)psp->pszTemplate == id)
      return (int)i;
  }
  return -1;
}

void ShowButton(HWND dlg, int id, bool show) {
  HWND b = GetDlgItem(dlg, id);
  if (b) ShowWindow(b, show ? SW_SHOWNA : SW_HIDE);
}

void SetDefault(Sheet* s, int id) {
  for (int b : {IDOK, IDCANCEL, kApply, kBack, kNext, kFinish, IDHELP}) {
    HWND w = GetDlgItem(s->hwnd, b);
    if (w) SendMessageW(w, BM_SETSTYLE, b == id ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
  }
  SendMessageW(s->hwnd, DM_SETDEFID, id, 0);
}

// Brings a page forward. dir is +1 or -1 in a wizard, for pages that
// ask to be skipped.
bool Activate(Sheet* s, int i, int dir) {
  for (int guard = 0; guard <= (int)s->pages.size(); guard++) {
    if (i < 0 || i >= (int)s->pages.size()) return false;
    if (!CreatePage(s, i)) {
      if (!s->wizard) return false;
      i += dir ? dir : 1;
      continue;
    }
    LRESULT r = NotifyPage(s, i, PSN_SETACTIVE);
    if (!s->pages[i].hwnd) return false;
    if (r == -1 && s->wizard) {
      i += dir ? dir : 1;
      continue;
    }
    if (r != 0 && r != -1) {
      int j = IndexOfId(s, r);
      if (j >= 0 && j != i) {
        i = j;
        continue;
      }
    }
    break;
  }
  if (i < 0 || i >= (int)s->pages.size() || !s->pages[i].hwnd) return false;
  HWND focus = GetFocus();
  bool focusInPage = s->cur >= 0 && s->cur < (int)s->pages.size() && s->pages[s->cur].hwnd &&
                     (focus == s->pages[s->cur].hwnd || IsChild(s->pages[s->cur].hwnd, focus));
  int old = s->cur;
  s->cur = i;
  HWND page = s->pages[i].hwnd;
  SetWindowPos(page, HWND_TOP, s->pageRect.left, s->pageRect.top, s->pageRect.right - s->pageRect.left,
               s->pageRect.bottom - s->pageRect.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (old >= 0 && old < (int)s->pages.size() && old != i && s->pages[old].hwnd) ShowWindow(s->pages[old].hwnd, SW_HIDE);
  if (s->tab) SendMessageW(s->tab, TCM_SETCURSEL, i, 0);
  if (s->flags & PSH_HASHELP) EnableWindow(GetDlgItem(s->hwnd, IDHELP), (Psp(s, i)->dwFlags & PSP_HASHELP) != 0);
  if (focusInPage || !focus || focus == s->hwnd || s->wizard) {
    HWND first = GetNextDlgTabItem(page, NULL, FALSE);
    if (first) SetFocus(first);
  }
  return true;
}

bool KillActive(Sheet* s) { return s->cur < 0 || NotifyPage(s, s->cur, PSN_KILLACTIVE) == 0; }

void End(Sheet* s, INT_PTR result) {
  s->ended = true;
  s->result = result;
  if (s->modeless && s->cur >= 0 && s->cur < (int)s->pages.size() && s->pages[s->cur].hwnd)
    ShowWindow(s->pages[s->cur].hwnd, SW_HIDE);
}

void SetApply(Sheet* s) {
  HWND b = GetDlgItem(s->hwnd, kApply);
  if (b) EnableWindow(b, !s->changed.empty());
}

bool Apply(Sheet* s, bool ok) {
  if (!KillActive(s)) return false;
  for (int i = 0; i < (int)s->pages.size(); i++) {
    if (!s->pages[i].hwnd) continue;
    LRESULT r = NotifyPage(s, i, PSN_APPLY, ok);
    if (r == PSNRET_INVALID || r == PSNRET_INVALID_NOCHANGEPAGE) {
      if (r == PSNRET_INVALID && i != s->cur) Activate(s, i, 0);
      return false;
    }
  }
  s->changed.clear();
  s->applied = true;
  SetApply(s);
  return true;
}

void Cancel(Sheet* s) {
  if (s->cur >= 0 && NotifyPage(s, s->cur, PSN_QUERYCANCEL)) return;
  for (int i = 0; i < (int)s->pages.size(); i++)
    if (s->pages[i].hwnd) NotifyPage(s, i, PSN_RESET);
  End(s, IDCANCEL);
}

void WizardMove(Sheet* s, int dir) {
  if (s->cur < 0) return;
  if (dir > 0 && !KillActive(s)) return;
  LRESULT r = NotifyPage(s, s->cur, dir > 0 ? PSN_WIZNEXT : PSN_WIZBACK);
  if (r == -1) return;
  int to = r ? IndexOfId(s, r) : s->cur + dir;
  if (to >= 0 && to < (int)s->pages.size()) Activate(s, to, dir);
}

void WizardFinish(Sheet* s) {
  if (s->cur < 0) return;
  if (!KillActive(s)) return;
  if (NotifyPage(s, s->cur, PSN_WIZFINISH)) return;
  s->applied = true;
  End(s, kFinish);
}

void SetWizButtons(Sheet* s, DWORD f) {
  EnableWindow(GetDlgItem(s->hwnd, kBack), (f & PSWIZB_BACK) != 0);
  EnableWindow(GetDlgItem(s->hwnd, kNext), (f & PSWIZB_NEXT) != 0);
  bool finish = (f & (PSWIZB_FINISH | PSWIZB_DISABLEDFINISH)) != 0;
  ShowButton(s->hwnd, kFinish, finish);
  ShowButton(s->hwnd, kNext, !finish);
  EnableWindow(GetDlgItem(s->hwnd, kFinish), (f & PSWIZB_FINISH) != 0);
  SetDefault(s, finish ? kFinish : (f & PSWIZB_NEXT) ? kNext : (f & PSWIZB_BACK) ? kBack : IDCANCEL);
}

void SetTitle(Sheet* s, DWORD style, const void* text, bool unicode) {
  std::wstring t = ResString(s->inst, text, unicode);
  s->caption = t;
  if (style & PSH_PROPTITLE) t = L"Properties for " + t;
  SetWindowTextW(s->hwnd, t.c_str());
}

void RemovePage(Sheet* s, int i) {
  if (i < 0 || i >= (int)s->pages.size()) return;
  if (i == s->cur) {
    int next = i + 1 < (int)s->pages.size() ? i + 1 : i - 1;
    if (next >= 0) Activate(s, next, 0);
    if (s->cur == i) s->cur = -1;
  }
  Page pg = s->pages[i];
  s->pages.erase(s->pages.begin() + i);
  if (s->cur > i) s->cur--;
  if (s->tab) SendMessageW(s->tab, TCM_DELETEITEM, i, 0);
  if (s->tab && s->cur >= 0) SendMessageW(s->tab, TCM_SETCURSEL, s->cur, 0);
  if (pg.hwnd) {
    s->changed.erase(std::remove(s->changed.begin(), s->changed.end(), pg.hwnd), s->changed.end());
    DestroyWindow(pg.hwnd);
  }
  FreePage(pg.data);
  SetApply(s);
}

int PageIndex(Sheet* s, WPARAM index, LPARAM page) {
  if (page) {
    for (size_t i = 0; i < s->pages.size(); i++)
      if ((LPARAM)s->pages[i].data == page) return (int)i;
    return -1;
  }
  return (int)index;
}

// Lays the sheet out around its largest page.
void Layout(Sheet* s) {
  HWND h = s->hwnd;
  int maxCx = 0, maxCy = 0;
  for (Page& pg : s->pages) {
    maxCx = std::max(maxCx, (int)pg.cx);
    maxCy = std::max(maxCy, (int)pg.cy);
  }
  if (!maxCx) maxCx = s->wizard ? WIZ_CXDLG : PROP_MED_CXDLG;
  if (!maxCy) maxCy = s->wizard ? WIZ_CYDLG : PROP_MED_CYDLG;
  RECT du = {7, 7, 7 + 50, 7 + 14};
  MapDialogRect(h, &du);
  int margin = du.left, bw = du.right - du.left, bh = du.bottom - du.top;
  RECT gap = {4, 0, 0, 0};
  MapDialogRect(h, &gap);
  RECT page = {0, 0, maxCx, maxCy};
  MapDialogRect(h, &page);
  int right, buttonsY;
  if (s->wizard) {
    s->pageRect = RECT{0, 0, page.right, page.bottom};
    int lineY = page.bottom + margin / 2;
    SetWindowPos(GetDlgItem(h, kLine), NULL, margin, lineY, page.right - 2 * margin, 2, SWP_NOZORDER | SWP_NOACTIVATE);
    right = page.right - margin;
    buttonsY = lineY + margin;
  } else {
    RECT tr = {0, 0, page.right, page.bottom};
    SendMessageW(s->tab, TCM_ADJUSTRECT, TRUE, (LPARAM)&tr);
    OffsetRect(&tr, margin - tr.left, margin - tr.top);
    SetWindowPos(s->tab, HWND_BOTTOM, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, SWP_NOACTIVATE);
    RECT disp = {0, 0, tr.right - tr.left, tr.bottom - tr.top};
    SendMessageW(s->tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&disp);
    OffsetRect(&disp, tr.left, tr.top);
    s->pageRect = disp;
    right = tr.right;
    buttonsY = tr.bottom + margin;
  }
  // The buttons line up from the right.
  std::vector<int> order;
  if (s->wizard)
    order = {kBack, kNext, IDCANCEL, IDHELP};
  else
    order = {IDOK, IDCANCEL, kApply, IDHELP};
  std::vector<HWND> shown;
  for (int id : order) {
    HWND b = GetDlgItem(h, id);
    if (b && (GetWindowLongW(b, GWL_STYLE) & WS_VISIBLE)) shown.push_back(b);
  }
  int x = right;
  for (int k = (int)shown.size() - 1; k >= 0; k--) {
    x -= bw;
    SetWindowPos(shown[k], NULL, x, buttonsY, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    int id = GetDlgCtrlID(shown[k]);
    if (id == kNext) SetWindowPos(GetDlgItem(h, kFinish), NULL, x, buttonsY, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
    // Back and Next sit together.
    x -= (id == kNext) ? 0 : gap.left;
  }
  RECT client = {0, 0, right + margin, buttonsY + bh + margin};
  if (s->wizard) client.right = std::max((int)client.right, (int)page.right);
  RECT wr = client;
  AdjustWindowRectEx(&wr, GetWindowLongW(h, GWL_STYLE), FALSE, GetWindowLongW(h, GWL_EXSTYLE));
  int w = wr.right - wr.left, hgt = wr.bottom - wr.top;
  RECT area;
  if (s->owner && IsWindow(s->owner))
    GetWindowRect(s->owner, &area);
  else
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  int px = area.left + (area.right - area.left - w) / 2, py = area.top + (area.bottom - area.top - hgt) / 2;
  SetWindowPos(h, NULL, std::max(0, px), std::max(0, py), w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND MakeButton(Sheet* s, int id, const wchar_t* text, DWORD extra = 0) {
  HWND b = CreateWindowExW(0, L"Button", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra, 0, 0, 10, 10,
                           s->hwnd, (HMENU)(INT_PTR)id, s->inst, NULL);
  SendMessageW(b, WM_SETFONT, SendMessageW(s->hwnd, WM_GETFONT, 0, 0), FALSE);
  return b;
}

void InitSheet(Sheet* s) {
  HWND h = s->hwnd;
  HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
  s->tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | (s->wizard ? 0 : WS_VISIBLE),
                           0, 0, 100, 100, h, (HMENU)(INT_PTR)kTab, s->inst, NULL);
  SendMessageW(s->tab, WM_SETFONT, (WPARAM)font, FALSE);
  for (int i = 0; i < (int)s->pages.size(); i++) {
    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)s->pages[i].title.c_str();
    SendMessageW(s->tab, TCM_INSERTITEMW, i, (LPARAM)&ti);
  }
  if (s->wizard) {
    CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 0, 0, 10, 2, h, (HMENU)(INT_PTR)kLine,
                    s->inst, NULL);
    MakeButton(s, kBack, L"< &Back");
    MakeButton(s, kNext, L"&Next >", BS_DEFPUSHBUTTON);
    HWND fin = MakeButton(s, kFinish, L"Finish");
    ShowWindow(fin, SW_HIDE);
    MakeButton(s, IDCANCEL, L"Cancel");
  } else {
    MakeButton(s, IDOK, L"OK", BS_DEFPUSHBUTTON);
    MakeButton(s, IDCANCEL, L"Cancel");
    if (!(s->flags & PSH_NOAPPLYNOW)) EnableWindow(MakeButton(s, kApply, L"&Apply"), FALSE);
  }
  if (s->flags & PSH_HASHELP) MakeButton(s, IDHELP, L"Help");
  Layout(s);
  SetTitle(s, s->flags & PSH_PROPTITLE, s->caption.c_str(), true);
  if (s->wizard) SetWizButtons(s, PSWIZB_BACK | PSWIZB_NEXT);
  SetDefault(s, s->wizard ? kNext : IDOK);
  int start = std::max(0, std::min(s->start, (int)s->pages.size() - 1));
  if (!s->pages.empty() && !Activate(s, start, 1)) {
    for (int i = 0; i < (int)s->pages.size() && s->cur < 0; i++) Activate(s, i, 1);
  }
}

bool SheetKeys(Sheet* s, MSG* m) {
  if (!m || s->ended) return false;
  if (m->hwnd != s->hwnd && !IsChild(s->hwnd, m->hwnd)) return false;
  if (m->message == WM_KEYDOWN && !s->wizard && (GetKeyState(VK_CONTROL) & 0x8000)) {
    int step = 0;
    if (m->wParam == VK_TAB) step = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
    if (m->wParam == VK_PRIOR) step = -1;
    if (m->wParam == VK_NEXT) step = 1;
    if (step && !s->pages.empty()) {
      int n = (int)s->pages.size(), to = ((s->cur + step) % n + n) % n;
      if (to != s->cur && KillActive(s)) Activate(s, to, 0);
      return true;
    }
  }
  return IsDialogMessageW(s->hwnd, m) != FALSE;
}

INT_PTR CALLBACK SheetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Sheet* s = SheetOf(h);
  if (msg == WM_INITDIALOG) {
    s = (Sheet*)lp;
    s->hwnd = h;
    SetPropW(h, kSheetProp, (HANDLE)s);
    InitSheet(s);
    if (s->callback) s->callback(h, PSCB_INITIALIZED, 0);
    return FALSE;  // the page has the focus
  }
  if (!s) return FALSE;
  auto result = [&](LRESULT r) {
    SetWindowLongPtrW(h, DWLP_MSGRESULT, r);
    return TRUE;
  };
  switch (msg) {
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (HIWORD(wp) != BN_CLICKED && lp) return FALSE;
      switch (id) {
        case IDOK:
          if (s->cancelToClose || Apply(s, true)) End(s, IDOK);
          return TRUE;
        case IDCANCEL:
          if (s->cancelToClose && !IsWindowEnabled(GetDlgItem(h, IDCANCEL)) && lp == 0) {
            End(s, IDOK);
            return TRUE;
          }
          Cancel(s);
          return TRUE;
        case kApply:
          Apply(s, false);
          return TRUE;
        case kBack: WizardMove(s, -1); return TRUE;
        case kNext: WizardMove(s, 1); return TRUE;
        case kFinish: WizardFinish(s); return TRUE;
        case IDHELP:
          NotifyPage(s, s->cur, PSN_HELP);
          return TRUE;
      }
      return FALSE;
    }
    case WM_CLOSE:
      if (IsWindowEnabled(GetDlgItem(h, IDCANCEL)) || !GetDlgItem(h, IDCANCEL))
        Cancel(s);
      else
        End(s, IDOK);
      return TRUE;
    case WM_NOTIFY: {
      NMHDR* nm = (NMHDR*)lp;
      if (nm->hwndFrom == s->tab) {
        if (nm->code == TCN_SELCHANGING) return result(KillActive(s) ? FALSE : TRUE);
        if (nm->code == TCN_SELCHANGE) {
          int to = (int)SendMessageW(s->tab, TCM_GETCURSEL, 0, 0);
          if (!Activate(s, to, 0) && s->cur >= 0) SendMessageW(s->tab, TCM_SETCURSEL, s->cur, 0);
          return TRUE;
        }
      }
      return FALSE;
    }
    case WM_DESTROY:
      for (Page& pg : s->pages) {
        FreePage(pg.data);
        pg.data = NULL;
      }
      s->pages.clear();
      RemovePropW(h, kSheetProp);
      if (s->modeless) delete s;
      return FALSE;

    case PSM_SETCURSEL: {
      int i = PageIndex(s, wp, lp);
      if (i < 0 || i >= (int)s->pages.size()) return result(FALSE);
      if (i == s->cur) return result(TRUE);
      if (!KillActive(s)) return result(FALSE);
      return result(Activate(s, i, 0));
    }
    case PSM_SETCURSELID: {
      int i = IndexOfId(s, lp);
      if (i < 0) return result(FALSE);
      if (i != s->cur && !KillActive(s)) return result(FALSE);
      return result(Activate(s, i, 0));
    }
    case PSM_REMOVEPAGE:
      RemovePage(s, PageIndex(s, wp, lp));
      return result(TRUE);
    case PSM_ADDPAGE:
    case PSM_INSERTPAGE: {
      PageData* p = (PageData*)lp;
      if (!p) return result(FALSE);
      int at = (int)s->pages.size();
      if (msg == PSM_INSERTPAGE) {
        if (IS_INTRESOURCE(wp))
          at = (int)wp;
        else
          at = PageIndex(s, 0, (LPARAM)wp) + 1;
      }
      AddPageInfo(s, at, p);
      if (s->cur < 0) Activate(s, at, 0);
      return result(TRUE);
    }
    case PSM_CHANGED:
      if (std::find(s->changed.begin(), s->changed.end(), (HWND)wp) == s->changed.end())
        s->changed.push_back((HWND)wp);
      SetApply(s);
      return result(0);
    case PSM_UNCHANGED:
      s->changed.erase(std::remove(s->changed.begin(), s->changed.end(), (HWND)wp), s->changed.end());
      SetApply(s);
      return result(0);
    case PSM_CANCELTOCLOSE:
      s->cancelToClose = true;
      EnableWindow(GetDlgItem(h, IDCANCEL), FALSE);
      SetDlgItemTextW(h, IDOK, L"Close");
      return result(0);
    case PSM_QUERYSIBLINGS:
      for (Page& pg : s->pages) {
        if (!pg.hwnd) continue;
        LRESULT r = SendMessageW(pg.hwnd, PSM_QUERYSIBLINGS, wp, lp);
        if (r) return result(r);
      }
      return result(0);
    case PSM_APPLY: return result(Apply(s, false));
    case PSM_RESTARTWINDOWS:
      s->restart |= ID_PSRESTARTWINDOWS;
      return result(0);
    case PSM_REBOOTSYSTEM:
      s->restart |= ID_PSREBOOTSYSTEM;
      return result(0);
    case PSM_SETTITLEA:
    case PSM_SETTITLEW:
      SetTitle(s, (DWORD)wp, (const void*)lp, msg == PSM_SETTITLEW);
      return result(0);
    case PSM_SETWIZBUTTONS:
      if (s->wizard) SetWizButtons(s, (DWORD)lp);
      return result(0);
    case PSM_SETFINISHTEXTA:
    case PSM_SETFINISHTEXTW: {
      std::wstring t = ResString(s->inst, (const void*)lp, msg == PSM_SETFINISHTEXTW);
      SetDlgItemTextW(h, kFinish, t.c_str());
      ShowButton(h, kFinish, true);
      EnableWindow(GetDlgItem(h, kFinish), TRUE);
      ShowButton(h, kBack, false);
      ShowButton(h, kNext, false);
      SetDefault(s, kFinish);
      return result(0);
    }
    case PSM_PRESSBUTTON: {
      static const int ids[] = {kBack, kNext, kFinish, IDOK, kApply, IDCANCEL, IDHELP};
      if (wp > PSBTN_MAX) return result(FALSE);
      int id = ids[wp];
      HWND b = GetDlgItem(h, id);
      if (!b || !IsWindowEnabled(b)) return result(FALSE);
      SendMessageW(h, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)b);
      return result(TRUE);
    }
    case PSM_GETTABCONTROL: return result((LRESULT)s->tab);
    case PSM_ISDIALOGMESSAGE: return result(SheetKeys(s, (MSG*)lp));
    case PSM_GETCURRENTPAGEHWND:
      return result(!s->ended && s->cur >= 0 && s->cur < (int)s->pages.size() ? (LRESULT)s->pages[s->cur].hwnd : 0);
    case PSM_HWNDTOINDEX:
      for (size_t i = 0; i < s->pages.size(); i++)
        if (s->pages[i].hwnd == (HWND)wp) return result((LRESULT)i);
      return result(-1);
    case PSM_INDEXTOHWND:
      return result(wp < s->pages.size() ? (LRESULT)s->pages[wp].hwnd : 0);
    case PSM_PAGETOINDEX: return result(PageIndex(s, 0, lp));
    case PSM_INDEXTOPAGE: return result(wp < s->pages.size() ? (LRESULT)s->pages[wp].data : 0);
    case PSM_IDTOINDEX: return result(IndexOfId(s, lp));
    case PSM_INDEXTOID: {
      if (wp >= s->pages.size()) return result(0);
      PROPSHEETPAGEW* psp = Psp(s, (int)wp);
      return result((psp->dwFlags & PSP_DLGINDIRECT) ? (LRESULT)psp->pResource : (LRESULT)psp->pszTemplate);
    }
    case PSM_GETRESULT: return result(s->ended ? s->result : -1);
    case PSM_RECALCPAGESIZES: return result(TRUE);
    case PSM_SETHEADERTITLEA:
    case PSM_SETHEADERTITLEW:
    case PSM_SETHEADERSUBTITLEA:
    case PSM_SETHEADERSUBTITLEW: return result(0);
  }
  return FALSE;
}

// Builds the sheet's own dialog template: a captioned popup with the
// first page's font, and no controls.
std::vector<WORD> SheetTemplate(Sheet* s) {
  Header font;
  for (Page& pg : s->pages) {
    Header h = ReadHeader(PageTemplate(pg.data));
    if (h.hasFont) {
      font = h;
      break;
    }
  }
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_3DLOOK | DS_SETFONT | WS_CLIPSIBLINGS |
                (s->modeless ? WS_VISIBLE : 0);
  DWORD exStyle = WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME;
  if (s->flags & PSH_WIZARDCONTEXTHELP) style |= DS_CONTEXTHELP;
  std::vector<WORD> t;
  auto dword = [&](DWORD v) {
    t.push_back(LOWORD(v));
    t.push_back(HIWORD(v));
  };
  auto str = [&](const std::wstring& v) {
    for (wchar_t c : v) t.push_back((WORD)c);
    t.push_back(0);
  };
  dword(style);
  dword(exStyle);
  t.push_back(0);  // no controls
  for (int k = 0; k < 4; k++) t.push_back(0);
  t.push_back(0);  // menu
  t.push_back(0);  // class
  str(L"");        // title, set later
  t.push_back(font.hasFont ? font.pointSize : 8);
  str(font.hasFont && !font.face.empty() ? font.face : std::wstring(L"MS Sans Serif"));
  return t;
}

INT_PTR RunSheet(const PROPSHEETHEADERW* hdr, bool unicode) {
  if (!hdr || hdr->dwSize < offsetof(PROPSHEETHEADERW, pfnCallback)) return -1;
  Sheet* s = new Sheet;
  s->unicode = unicode;
  s->flags = hdr->dwFlags;
  s->wizard = (hdr->dwFlags & (PSH_WIZARD | PSH_WIZARD97)) != 0;
  s->modeless = (hdr->dwFlags & PSH_MODELESS) != 0;
  s->inst = hdr->hInstance;
  s->caption = ResString(hdr->hInstance, hdr->pszCaption, unicode);
  if ((hdr->dwFlags & PSH_USECALLBACK) && hdr->dwSize >= offsetof(PROPSHEETHEADERW, pfnCallback) + sizeof(void*))
    s->callback = hdr->pfnCallback;
  // The pages, from an array of descriptions or of handles.
  if (hdr->dwFlags & PSH_PROPSHEETPAGE) {
    const BYTE* p = (const BYTE*)hdr->ppsp;
    for (UINT i = 0; i < hdr->nPages && p; i++) {
      PageData* d = NewPage(p, unicode);
      if (!d) break;
      AddPageInfo(s, (int)s->pages.size(), d);
      p += ((const PROPSHEETPAGEW*)p)->dwSize;
    }
  } else {
    for (UINT i = 0; i < hdr->nPages && hdr->phpage; i++)
      if (hdr->phpage[i]) AddPageInfo(s, (int)s->pages.size(), (PageData*)hdr->phpage[i]);
  }
  if (hdr->dwFlags & PSH_USEPSTARTPAGE) {
    std::wstring want = ResString(hdr->hInstance, hdr->pStartPage, unicode);
    for (size_t i = 0; i < s->pages.size(); i++)
      if (s->pages[i].title == want) s->start = (int)i;
  } else {
    s->start = (int)hdr->nStartPage;
  }
  std::vector<WORD> tmpl = SheetTemplate(s);
  if (s->callback) s->callback(NULL, PSCB_PRECREATE, (LPARAM)tmpl.data());
  HWND parent = hdr->hwndParent;
  s->owner = parent ? GetAncestor(parent, GA_ROOT) : NULL;
  HWND h = CreateDialogIndirectParamW(hdr->hInstance, (LPCDLGTEMPLATEW)tmpl.data(), parent, SheetProc, (LPARAM)s);
  if (!h) {
    for (Page& pg : s->pages) FreePage(pg.data);
    delete s;
    return -1;
  }
  if (s->modeless) return (INT_PTR)h;

  // Modal: the owner waits while the sheet runs its own loop.
  bool disabled = false;
  if (s->owner && IsWindowEnabled(s->owner)) {
    EnableWindow(s->owner, FALSE);
    disabled = true;
  }
  if (!s->ended) {
    ShowWindow(h, SW_SHOWNORMAL);
    UpdateWindow(h);
  }
  bool idleSent = false;
  MSG m;
  while (IsWindow(h) && !s->ended) {
    if (!idleSent && !PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE)) {
      idleSent = true;
      if (s->owner) SendMessageW(s->owner, WM_ENTERIDLE, MSGF_DIALOGBOX, (LPARAM)h);
      continue;
    }
    if (!GetMessageW(&m, NULL, 0, 0)) {
      PostQuitMessage((int)m.wParam);
      break;
    }
    idleSent = false;
    if (CallMsgFilterW(&m, MSGF_DIALOGBOX)) continue;
    if (!SendMessageW(h, PSM_ISDIALOGMESSAGE, 0, (LPARAM)&m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  INT_PTR result = s->restart ? s->restart : (s->applied ? 1 : 0);
  if (disabled && IsWindow(s->owner)) EnableWindow(s->owner, TRUE);
  if (IsWindow(h)) DestroyWindow(h);
  if (s->owner && IsWindow(s->owner)) SetActiveWindow(s->owner);
  delete s;
  return result;
}

}  // namespace

HPROPSHEETPAGE WINAPI CreatePropertySheetPageW(LPCPROPSHEETPAGEW psp) { return NewPage(psp, true); }
HPROPSHEETPAGE WINAPI CreatePropertySheetPageA(LPCPROPSHEETPAGEA psp) { return NewPage(psp, false); }

BOOL WINAPI DestroyPropertySheetPage(HPROPSHEETPAGE page) {
  if (!page) return FALSE;
  FreePage((PageData*)page);
  return TRUE;
}

INT_PTR WINAPI PropertySheetW(LPCPROPSHEETHEADERW hdr) { return RunSheet(hdr, true); }

// The ANSI header has the same layout with narrow strings.
INT_PTR WINAPI PropertySheetA(LPCPROPSHEETHEADERA hdr) { return RunSheet((const PROPSHEETHEADERW*)hdr, false); }
