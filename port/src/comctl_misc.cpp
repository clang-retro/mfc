// comctl_misc.cpp - the small parts of the common controls: setting
// them up, subclassing a window, _TrackMouseEvent, status bar and menu
// help functions, drag lists, and the helpers the controls share.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <string.h>

#include <algorithm>
#include <vector>

using namespace winport;

namespace winport {

LRESULT SendNotify(HWND from, HWND to, UINT code, NMHDR* nm) {
  NMHDR local;
  if (!nm) nm = &local;
  if (!to) to = GetParent(from);
  nm->hwndFrom = from;
  nm->idFrom = (UINT_PTR)GetDlgCtrlID(from);
  nm->code = code;
  if (!to || !IsWindow(to)) return 0;
  return SendMessageW(to, WM_NOTIFY, nm->idFrom, (LPARAM)nm);
}

bool NotifyUnicode(HWND ctl, HWND to) {
  if (!to) to = GetParent(ctl);
  if (!to) return true;
  return SendMessageW(to, WM_NOTIFYFORMAT, (WPARAM)ctl, NF_QUERY) != NFR_ANSI;
}

std::wstring TextIn(const void* p, bool unicode) {
  if (!p || IsCallback(p, unicode)) return L"";
  return unicode ? std::wstring((const wchar_t*)p) : Widen((const char*)p);
}

int CopyOut(const std::wstring& s, void* buf, int max, bool unicode) {
  if (!buf || max <= 0) return 0;
  if (unicode) {
    int n = std::min((int)s.size(), max - 1);
    memcpy(buf, s.data(), n * sizeof(wchar_t));
    ((wchar_t*)buf)[n] = 0;
    return n;
  }
  std::string a = Narrow(s.c_str());
  int n = std::min((int)a.size(), max - 1);
  memcpy(buf, a.data(), n);
  ((char*)buf)[n] = 0;
  return n;
}

HFONT ControlFont() { return (HFONT)GetStockObject(DEFAULT_GUI_FONT); }

void RegisterCommonControls() {
  RegisterBarClasses();
  RegisterSimpleClasses();
  RegisterListView();
}

}  // namespace winport

// ---------------------------------------------------------------------
// Subclassing: a chain of procedures in front of the window's own,
// kept in a property, each one passing on with DefSubclassProc.

namespace {

const wchar_t kSubclassProp[] = L"winport.subclass";

struct SubclassChain {
  struct Link {
    SUBCLASSPROC proc;
    UINT_PTR id;
    DWORD_PTR ref;
    bool removed;
  };
  WNDPROC original = NULL;
  std::vector<Link> links;  // the last added runs first
  size_t next = 0;          // the link DefSubclassProc calls: links[next - 1]
  int depth = 0;
};

SubclassChain* ChainOf(HWND h) { return (SubclassChain*)GetPropW(h, kSubclassProp); }

LRESULT CallNext(SubclassChain* c, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  while (c->next > 0 && c->links[c->next - 1].removed) c->next--;
  if (c->next == 0) return CallWindowProcW(c->original, h, msg, wp, lp);
  size_t i = --c->next;
  SubclassChain::Link l = c->links[i];
  LRESULT r = l.proc(h, msg, wp, lp, l.id, l.ref);
  c->next = i + 1;
  return r;
}

void Unhook(HWND h, SubclassChain* c) {
  SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)c->original);
  RemovePropW(h, kSubclassProp);
  delete c;
}

void Compact(HWND h, SubclassChain* c) {
  if (c->depth) return;
  c->links.erase(std::remove_if(c->links.begin(), c->links.end(), [](const SubclassChain::Link& l) { return l.removed; }),
                 c->links.end());
  if (c->links.empty()) Unhook(h, c);
}

LRESULT CALLBACK ChainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  SubclassChain* c = ChainOf(h);
  if (!c) return DefWindowProcW(h, msg, wp, lp);
  size_t saved = c->next;
  c->next = c->links.size();
  c->depth++;
  LRESULT r = CallNext(c, h, msg, wp, lp);
  c->depth--;
  c->next = saved;
  if (msg == WM_NCDESTROY && c->depth == 0) {
    Unhook(h, c);
  } else if (c->depth == 0) {
    bool any = false;
    for (auto& l : c->links) any |= l.removed;
    if (any) Compact(h, c);
  }
  return r;
}

}  // namespace

extern "C" {

void WINAPI InitCommonControls(void) {}

BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX* icc) { return icc != NULL; }

BOOL WINAPI _TrackMouseEvent(LPTRACKMOUSEEVENT t) { return TrackMouseEvent(t); }

BOOL WINAPI SetWindowSubclass(HWND h, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR ref) {
  if (!h || !proc || !IsWindow(h)) return FALSE;
  SubclassChain* c = ChainOf(h);
  if (!c) {
    c = new SubclassChain;
    c->original = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    SetPropW(h, kSubclassProp, c);
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)ChainProc);
  }
  for (auto& l : c->links)
    if (l.proc == proc && l.id == id && !l.removed) {
      l.ref = ref;
      return TRUE;
    }
  c->links.push_back({proc, id, ref, false});
  return TRUE;
}

BOOL WINAPI GetWindowSubclass(HWND h, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR* ref) {
  SubclassChain* c = h ? ChainOf(h) : NULL;
  if (c)
    for (auto& l : c->links)
      if (l.proc == proc && l.id == id && !l.removed) {
        if (ref) *ref = l.ref;
        return TRUE;
      }
  if (ref) *ref = 0;
  return FALSE;
}

BOOL WINAPI RemoveWindowSubclass(HWND h, SUBCLASSPROC proc, UINT_PTR id) {
  SubclassChain* c = h ? ChainOf(h) : NULL;
  if (!c) return FALSE;
  for (auto& l : c->links)
    if (l.proc == proc && l.id == id && !l.removed) {
      l.removed = true;
      Compact(h, c);
      return TRUE;
    }
  return FALSE;
}

LRESULT WINAPI DefSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  SubclassChain* c = ChainOf(h);
  if (!c) return DefWindowProcW(h, msg, wp, lp);
  return CallNext(c, h, msg, wp, lp);
}

// ---------------------------------------------------------------------
// Status windows and menu help.

void WINAPI DrawStatusTextW(HDC dc, LPCRECT rc, LPCWSTR text, UINT flags) {
  if (!dc || !rc) return;
  RECT r = *rc;
  if (!(flags & SBT_NOBORDERS)) DrawEdge(dc, &r, (flags & SBT_POPOUT) ? BDR_RAISEDINNER : BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
  if (!text || !*text) return;
  int mode = SetBkMode(dc, TRANSPARENT);
  // Tabs split the text: left, then centered, then right.
  std::wstring s = text, parts[3];
  int k = 0;
  for (wchar_t ch : s) {
    if (ch == L'\t' && k < 2) {
      k++;
      continue;
    }
    parts[k] += ch;
  }
  InflateRect(&r, -2, 0);
  UINT align[3] = {DT_LEFT, DT_CENTER, DT_RIGHT};
  for (int i = 0; i < 3; i++)
    if (!parts[i].empty())
      DrawTextW(dc, parts[i].c_str(), (int)parts[i].size(), &r,
                align[i] | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  SetBkMode(dc, mode);
}

void WINAPI DrawStatusTextA(HDC dc, LPCRECT rc, LPCSTR text, UINT flags) {
  std::wstring w = text ? Widen(text) : L"";
  DrawStatusTextW(dc, rc, text ? w.c_str() : NULL, flags);
}

HWND WINAPI CreateStatusWindowW(LONG style, LPCWSTR text, HWND parent, UINT id) {
  return CreateWindowExW(0, STATUSCLASSNAMEW, text, (DWORD)style, 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id,
                         (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
}

HWND WINAPI CreateStatusWindowA(LONG style, LPCSTR text, HWND parent, UINT id) {
  return CreateWindowExA(0, STATUSCLASSNAMEA, text, (DWORD)style, 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id,
                         (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
}

// Shows a menu item's help string, a resource string with the item's ID
// offset by the base in ids[0], in a status window.
void WINAPI MenuHelp(UINT msg, WPARAM wp, LPARAM lp, HMENU mainMenu, HINSTANCE inst, HWND status, UINT* ids) {
  (void)mainMenu;
  if (msg != WM_MENUSELECT || !status) return;
  UINT flags = HIWORD(wp), item = LOWORD(wp);
  if (flags == 0xFFFF && !lp) {
    SendMessageW(status, SB_SIMPLE, FALSE, 0);
    return;
  }
  wchar_t text[256] = L"";
  if (!(flags & (MF_POPUP | MF_SEPARATOR)) && ids) LoadStringW(inst, ids[0] + item, text, 256);
  SendMessageW(status, SB_SIMPLE, TRUE, 0);
  SendMessageW(status, SB_SETTEXTW, SB_SIMPLEID, (LPARAM)text);
}

// lpInfo: pairs of menu command and control ID, after a leading pair.
BOOL WINAPI ShowHideMenuCtl(HWND h, UINT_PTR cmd, LPINT info) {
  if (!h || !info) return FALSE;
  HMENU menu = (HMENU)(INT_PTR)info[1];
  for (LPINT p = info + 2; p[0]; p += 2) {
    if ((UINT_PTR)p[0] != cmd) continue;
    HWND ctl = GetDlgItem(h, p[1]);
    bool shown = menu && (GetMenuState(menu, (UINT)cmd, MF_BYCOMMAND) & MF_CHECKED);
    if (ctl) ShowWindow(ctl, shown ? SW_HIDE : SW_SHOW);
    if (menu) CheckMenuItem(menu, (UINT)cmd, MF_BYCOMMAND | (shown ? MF_UNCHECKED : MF_CHECKED));
    return TRUE;
  }
  return FALSE;
}

// The client area less the visible controls named in lpInfo's pairs.
void WINAPI GetEffectiveClientRect(HWND h, LPRECT out, const INT* info) {
  if (!out) return;
  GetClientRect(h, out);
  if (!info) return;
  for (const INT* p = info + 2; p[0]; p += 2) {
    HWND ctl = GetDlgItem(h, p[1]);
    if (!ctl || !IsWindowVisible(ctl)) continue;
    RECT r;
    GetWindowRect(ctl, &r);
    MapWindowPoints(NULL, h, (LPPOINT)&r, 2);
    SubtractRect(out, out, &r);
  }
}

// ---------------------------------------------------------------------
// Drag lists: a list box whose items can be dragged to a new place.

}  // extern "C"

namespace {

UINT DragListMessage() {
  static UINT m = RegisterWindowMessageW(DRAGLISTMSGSTRINGW);
  return m;
}

const wchar_t kDragProp[] = L"winport.draglist";

LRESULT CALLBACK DragListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
  bool dragging = GetPropW(h, kDragProp) != NULL;
  DRAGLISTINFO dl = {};
  GetCursorPos(&dl.ptCursor);
  dl.hWnd = h;
  HWND parent = GetParent(h);
  int id = GetDlgCtrlID(h);
  switch (msg) {
    case WM_LBUTTONDOWN: {
      LRESULT r = DefSubclassProc(h, msg, wp, lp);
      dl.uNotification = DL_BEGINDRAG;
      if (SendMessageW(parent, DragListMessage(), id, (LPARAM)&dl)) {
        SetPropW(h, kDragProp, (HANDLE)1);
        SetCapture(h);
      }
      return r;
    }
    case WM_MOUSEMOVE:
      if (dragging) {
        dl.uNotification = DL_DRAGGING;
        UINT cursor = (UINT)SendMessageW(parent, DragListMessage(), id, (LPARAM)&dl);
        if (cursor == DL_STOPCURSOR) SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32648)));
        else if (cursor == DL_COPYCURSOR || cursor == DL_MOVECURSOR) SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32512)));
        return 0;
      }
      break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_KEYDOWN:
      if (dragging && (msg != WM_KEYDOWN || wp == VK_ESCAPE)) {
        RemovePropW(h, kDragProp);
        if (msg != WM_CAPTURECHANGED && GetCapture() == h) ReleaseCapture();
        dl.uNotification = msg == WM_LBUTTONUP ? DL_DROPPED : DL_CANCELDRAG;
        SendMessageW(parent, DragListMessage(), id, (LPARAM)&dl);
        return 0;
      }
      break;
    case WM_NCDESTROY:
      RemovePropW(h, kDragProp);
      break;
  }
  return DefSubclassProc(h, msg, wp, lp);
}

}  // namespace

extern "C" {

BOOL WINAPI MakeDragList(HWND lb) {
  if (!lb || (GetWindowLongW(lb, GWL_STYLE) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL))) return FALSE;
  DragListMessage();
  return SetWindowSubclass(lb, DragListProc, 0, 0);
}

int WINAPI LBItemFromPt(HWND lb, POINT pt, BOOL autoScroll) {
  RECT r;
  GetWindowRect(lb, &r);
  if (!PtInRect(&r, pt)) {
    if (autoScroll) {
      int top = (int)SendMessageW(lb, LB_GETTOPINDEX, 0, 0);
      if (pt.y < r.top && top > 0) SendMessageW(lb, LB_SETTOPINDEX, top - 1, 0);
      if (pt.y >= r.bottom) SendMessageW(lb, LB_SETTOPINDEX, top + 1, 0);
    }
    return -1;
  }
  ScreenToClient(lb, &pt);
  LRESULT hit = SendMessageW(lb, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
  return HIWORD(hit) ? -1 : (int)LOWORD(hit);
}

// Draws an arrow in the parent beside the list box, at an item's top.
void WINAPI DrawInsert(HWND parent, HWND lb, int item) {
  static int last = -1;
  static HWND lastList = NULL;
  auto arrow = [&](HWND list, int at) {
    RECT ir, lr;
    if (SendMessageW(list, LB_GETITEMRECT, at, (LPARAM)&ir) == LB_ERR) return;
    GetWindowRect(list, &lr);
    POINT p = {lr.left - 12, lr.top + ir.top};
    ScreenToClient(parent, &p);
    HDC dc = GetDC(parent);
    for (int i = 0; i < 6; i++) PatBlt(dc, p.x + i, p.y - 5 + i, 1, 11 - 2 * i, DSTINVERT);
    ReleaseDC(parent, dc);
  };
  if (lastList == lb && last == item) return;
  if (lastList && last >= 0 && IsWindow(lastList)) arrow(lastList, last);  // inverting again erases it
  lastList = lb;
  last = item;
  if (item >= 0) arrow(lb, item);
}

}  // extern "C"
