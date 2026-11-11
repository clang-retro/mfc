// user_listbox.cpp - the ListBox and ComboBox controls, and the
// DlgDir functions that fill them with file names.
//
// A combo box is a selection field (an Edit child, or drawn by the combo
// box itself for CBS_DROPDOWNLIST) and a list of the "ComboLBox" class:
// a child below the field for CBS_SIMPLE, or a popup that drops down.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>
#include <wctype.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

const int kEditId = 1001, kListId = 1000;

// ---------------------------------------------------------------------
// List boxes

struct Item {
  std::wstring text;
  ULONG_PTR data = 0;
  bool sel = false;
  int height = 0;
};

struct List {
  HWND hwnd = NULL;
  HWND combo = NULL;  // the combo box that owns a ComboLBox
  std::vector<Item> items;
  int top = 0;
  int cur = -1;     // the selection of a single-selection list
  int caret = -1;   // the item with the focus rectangle
  int anchor = -1;  // where an extended selection starts
  int itemHeight = 13;
  HFONT font = NULL;
  bool focus = false, tracking = false;
  int extent = 0, xOffset = 0, columnWidth = 0;
  std::vector<int> tabs;
  LCID locale = 0;
  int nodataCount = 0;

  DWORD Style() const { return (DWORD)GetWindowLongW(hwnd, GWL_STYLE); }
  bool OwnerDraw() const { return (Style() & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0; }
  bool Variable() const { return (Style() & LBS_OWNERDRAWVARIABLE) != 0; }
  bool HasStrings() const { return !OwnerDraw() || (Style() & LBS_HASSTRINGS); }
  bool Multi() const { return (Style() & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0; }
  bool NoData() const { return (Style() & LBS_NODATA) != 0; }
  int Count() const { return NoData() ? nodataCount : (int)items.size(); }
};

List* ListOf(HWND h) { return (List*)GetWindowLongPtrW(h, 0); }

HWND NotifyTarget(List* l) { return l->combo ? l->combo : GetParent(l->hwnd); }

void Notify(List* l, UINT code) {
  if (!(l->Style() & LBS_NOTIFY)) return;
  HWND to = NotifyTarget(l);
  if (to) SendMessageW(to, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(l->hwnd), code), (LPARAM)l->hwnd);
}

// Owner draw messages go to the window that owns the control: the
// combo box's parent for a combo box's list.
HWND OwnerOf(List* l) { return l->combo ? GetParent(l->combo) : GetParent(l->hwnd); }

UINT CtlIdOf(List* l) { return (UINT)GetDlgCtrlID(l->combo ? l->combo : l->hwnd); }

int HeightOf(List* l, int i) {
  if (l->Variable() && i >= 0 && i < (int)l->items.size() && l->items[i].height) return l->items[i].height;
  return l->itemHeight;
}

int FullyVisible(List* l) {
  RECT rc;
  GetClientRect(l->hwnd, &rc);
  int h = rc.bottom - rc.top, n = 0;
  for (int i = l->top; i < l->Count(); i++) {
    h -= HeightOf(l, i);
    if (h < 0) break;
    n++;
  }
  if (h > 0) n += h / std::max(1, l->itemHeight);
  return std::max(1, n);
}

RECT ItemRect(List* l, int i) {
  RECT rc;
  GetClientRect(l->hwnd, &rc);
  int y = 0;
  if (i >= l->top)
    for (int k = l->top; k < i; k++) y += HeightOf(l, k);
  else
    for (int k = i; k < l->top; k++) y -= HeightOf(l, k);
  return MakeRect(rc.left - l->xOffset, y, std::max(rc.right, (LONG)l->extent) - l->xOffset, y + HeightOf(l, i));
}

int ItemAt(List* l, POINT p, bool* outside = NULL) {
  RECT rc;
  GetClientRect(l->hwnd, &rc);
  if (outside) *outside = !PtInRect(&rc, p);
  int y = 0;
  int n = l->Count();
  if (p.y < 0) return std::max(0, l->top - 1);
  for (int i = l->top; i < n; i++) {
    int h = HeightOf(l, i);
    if (p.y < y + h) return i;
    y += h;
  }
  if (outside) *outside = true;
  return n - 1;
}

void UpdateScroll(List* l) {
  DWORD s = l->Style();
  if (s & WS_VSCROLL) {
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    if (s & LBS_DISABLENOSCROLL) si.fMask |= SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = std::max(0, l->Count() - 1);
    si.nPage = FullyVisible(l);
    si.nPos = l->top;
    SetScrollInfo(l->hwnd, SB_VERT, &si, TRUE);
  }
  if ((s & WS_HSCROLL) && l->extent) {
    RECT rc;
    GetClientRect(l->hwnd, &rc);
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    if (s & LBS_DISABLENOSCROLL) si.fMask |= SIF_DISABLENOSCROLL;
    si.nMax = l->extent - 1;
    si.nPage = rc.right - rc.left;
    si.nPos = l->xOffset;
    SetScrollInfo(l->hwnd, SB_HORZ, &si, TRUE);
  }
}

void Redraw(List* l) {
  if (l->Style() & LBS_NOREDRAW) return;
  InvalidateRect(l->hwnd, NULL, TRUE);
}

void RedrawItem(List* l, int i) {
  if (i < 0 || (l->Style() & LBS_NOREDRAW)) return;
  RECT r = ItemRect(l, i);
  InvalidateRect(l->hwnd, &r, TRUE);
}

void SetTop(List* l, int top) {
  int maxTop = std::max(0, l->Count() - FullyVisible(l));
  top = std::max(0, std::min(top, maxTop));
  if (top == l->top) return;
  l->top = top;
  UpdateScroll(l);
  Redraw(l);
}

void EnsureVisible(List* l, int i) {
  if (i < 0) return;
  if (i < l->top) {
    SetTop(l, i);
    return;
  }
  int vis = FullyVisible(l);
  if (i >= l->top + vis) SetTop(l, i - vis + 1);
}

void Measure(List* l, int i) {
  if (!l->Variable() || i < 0 || i >= (int)l->items.size()) return;
  MEASUREITEMSTRUCT mis = {l->combo ? (UINT)ODT_COMBOBOX : (UINT)ODT_LISTBOX, CtlIdOf(l), (UINT)i, 0,
                           (UINT)l->itemHeight, l->items[i].data};
  if (HWND o = OwnerOf(l)) SendMessageW(o, WM_MEASUREITEM, mis.CtlID, (LPARAM)&mis);
  l->items[i].height = (int)mis.itemHeight;
}

int Compare(List* l, const Item& a, int bIndex) {
  const Item& b = l->items[bIndex];
  if (l->HasStrings()) return CompareStringW(l->locale, NORM_IGNORECASE, a.text.c_str(), -1, b.text.c_str(), -1) - 2;
  COMPAREITEMSTRUCT cis = {l->combo ? (UINT)ODT_COMBOBOX : (UINT)ODT_LISTBOX, CtlIdOf(l), l->combo ? l->combo : l->hwnd,
                           (UINT)-1, a.data, (UINT)bIndex, b.data, (DWORD)l->locale};
  HWND o = OwnerOf(l);
  return o ? (int)SendMessageW(o, WM_COMPAREITEM, cis.CtlID, (LPARAM)&cis) : 0;
}

int Insert(List* l, int at, const Item& it, bool sorted) {
  if (sorted && (l->Style() & LBS_SORT)) {
    // After the items that sort before or the same.
    int lo = 0, hi = (int)l->items.size();
    while (lo < hi) {
      int mid = (lo + hi) / 2;
      if (Compare(l, it, mid) < 0)
        hi = mid;
      else
        lo = mid + 1;
    }
    at = lo;
  }
  if (at < 0 || at > (int)l->items.size()) at = (int)l->items.size();
  l->items.insert(l->items.begin() + at, it);
  if (l->cur >= at) l->cur++;
  if (l->caret >= at) l->caret++;
  if (l->anchor >= at) l->anchor++;
  Measure(l, at);
  UpdateScroll(l);
  Redraw(l);
  return at;
}

void SendDelete(List* l, int i) {
  if (l->NoData() || i < 0 || i >= (int)l->items.size()) return;
  if (!l->OwnerDraw() && !l->items[i].data) return;
  DELETEITEMSTRUCT dis = {l->combo ? (UINT)ODT_COMBOBOX : (UINT)ODT_LISTBOX, CtlIdOf(l), (UINT)i,
                          l->combo ? l->combo : l->hwnd, l->items[i].data};
  if (HWND o = OwnerOf(l)) SendMessageW(o, WM_DELETEITEM, dis.CtlID, (LPARAM)&dis);
}

void Delete(List* l, int i) {
  SendDelete(l, i);
  l->items.erase(l->items.begin() + i);
  auto fix = [&](int& v) {
    if (v == i) v = -1;
    else if (v > i) v--;
  };
  fix(l->cur);
  if (l->caret > i || (l->caret == i && l->caret >= (int)l->items.size())) l->caret--;
  fix(l->anchor);
  if (l->top > 0 && l->top >= (int)l->items.size()) l->top = std::max(0, (int)l->items.size() - 1);
  UpdateScroll(l);
  Redraw(l);
}

void ResetContent(List* l) {
  for (int i = 0; i < (int)l->items.size(); i++) SendDelete(l, i);
  l->items.clear();
  l->nodataCount = 0;
  l->cur = l->caret = l->anchor = -1;
  l->top = 0;
  UpdateScroll(l);
  Redraw(l);
}

bool IsSelected(List* l, int i) {
  if (i < 0 || i >= l->Count()) return false;
  if (l->Multi()) return !l->NoData() && l->items[i].sel;
  return i == l->cur;
}

// Single selection: makes i the selection.
void SetCur(List* l, int i) {
  if (l->Style() & LBS_NOSEL) return;
  int old = l->cur;
  l->cur = i;
  l->caret = i;
  if (old != i) {
    RedrawItem(l, old);
    RedrawItem(l, i);
  }
}

void SetSel(List* l, int i, bool on) {
  if (l->NoData() || i < 0 || i >= (int)l->items.size()) return;
  if (l->items[i].sel != on) {
    l->items[i].sel = on;
    RedrawItem(l, i);
  }
}

void SetCaret(List* l, int i) {
  int old = l->caret;
  l->caret = i;
  if (old != i) {
    RedrawItem(l, old);
    RedrawItem(l, i);
  }
}

int Find(List* l, int start, const wchar_t* s, bool exact) {
  int n = (int)l->items.size();
  if (n == 0 || !s) return LB_ERR;
  size_t len = wcslen(s);
  for (int k = 1; k <= n; k++) {
    int i = ((start < 0 ? -1 : start) + k) % n;
    const Item& it = l->items[i];
    if (!l->HasStrings()) {
      if (it.data == (ULONG_PTR)s) return i;
      continue;
    }
    if (exact) {
      if (CompareStringW(l->locale, NORM_IGNORECASE, it.text.c_str(), -1, s, -1) == CSTR_EQUAL) return i;
    } else if (it.text.size() >= len &&
               CompareStringW(l->locale, NORM_IGNORECASE, it.text.c_str(), (int)len, s, (int)len) == CSTR_EQUAL) {
      return i;
    }
  }
  return LB_ERR;
}

void Paint(List* l, HDC dc) {
  RECT rc;
  GetClientRect(l->hwnd, &rc);
  HWND parent = NotifyTarget(l);
  bool enabled = IsWindowEnabled(l->hwnd) != FALSE;
  HBRUSH br = NULL;
  HWND colorFrom = l->combo ? GetParent(l->combo) : parent;
  if (colorFrom) br = (HBRUSH)SendMessageW(colorFrom, WM_CTLCOLORLISTBOX, (WPARAM)dc, (LPARAM)l->hwnd);
  if (!br) br = (HBRUSH)DefWindowProcW(l->hwnd, WM_CTLCOLORLISTBOX, (WPARAM)dc, (LPARAM)l->hwnd);
  FillRect(dc, &rc, br);
  HGDIOBJ oldFont = SelectObject(dc, l->font ? (HGDIOBJ)l->font : GetStockObject(SYSTEM_FONT));
  COLORREF normal = enabled ? GetTextColor(dc) : GetSysColor(COLOR_GRAYTEXT);
  int mode = SetBkMode(dc, TRANSPARENT);
  int n = l->Count();
  bool focusShown = false;
  for (int i = l->top; i < n; i++) {
    RECT r = ItemRect(l, i);
    if (r.top >= rc.bottom) break;
    bool sel = IsSelected(l, i);
    bool hasFocus = l->focus && i == l->caret;
    if (l->OwnerDraw()) {
      DRAWITEMSTRUCT dis = {l->combo ? (UINT)ODT_COMBOBOX : (UINT)ODT_LISTBOX, CtlIdOf(l), (UINT)i, ODA_DRAWENTIRE,
                            0, l->hwnd, dc, r, l->NoData() ? 0 : l->items[i].data};
      if (sel) dis.itemState |= ODS_SELECTED;
      if (hasFocus) dis.itemState |= ODS_FOCUS;
      if (!enabled) dis.itemState |= ODS_DISABLED;
      if (HWND o = OwnerOf(l)) SendMessageW(o, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
      focusShown |= hasFocus;
      continue;
    }
    if (sel) FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
    SetTextColor(dc, sel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : normal);
    const std::wstring& t = l->items[i].text;
    if (l->Style() & LBS_USETABSTOPS)
      TabbedTextOutW(dc, r.left + 2, r.top, t.c_str(), (int)t.size(), (int)l->tabs.size(),
                     l->tabs.empty() ? NULL : l->tabs.data(), r.left + 2);
    else
      ExtTextOutW(dc, r.left + 2, r.top, 0, NULL, t.c_str(), (UINT)t.size(), NULL);
    if (hasFocus) {
      DrawFocusRect(dc, &r);
      focusShown = true;
    }
  }
  if (l->focus && !focusShown && !l->combo) {
    RECT r = MakeRect(rc.left, 0, rc.right, l->itemHeight);
    DrawFocusRect(dc, &r);
  }
  SetBkMode(dc, mode);
  SelectObject(dc, oldFont);
}

// A user's choice of item i, from the mouse or the keyboard.
void Choose(List* l, int i, bool shift, bool ctrl, bool toggle) {
  int n = l->Count();
  if (n == 0 || (l->Style() & LBS_NOSEL)) {
    if (n) SetCaret(l, std::max(0, std::min(i, n - 1)));
    return;
  }
  i = std::max(0, std::min(i, n - 1));
  DWORD style = l->Style();
  bool changed = false;
  if (!l->Multi()) {
    changed = l->cur != i;
    SetCur(l, i);
  } else if (style & LBS_EXTENDEDSEL) {
    if (shift) {
      if (l->anchor < 0) l->anchor = i;
      int a = std::min(l->anchor, i), b = std::max(l->anchor, i);
      for (int k = 0; k < n; k++) {
        bool want = (k >= a && k <= b) || (ctrl && l->items[k].sel && (k < a || k > b));
        if (l->items[k].sel != want) changed = true;
        SetSel(l, k, want);
      }
    } else if (ctrl) {
      if (toggle) {
        SetSel(l, i, !l->items[i].sel);
        changed = true;
      }
      l->anchor = i;
    } else {
      for (int k = 0; k < n; k++) {
        bool want = k == i;
        if (l->items[k].sel != want) changed = true;
        SetSel(l, k, want);
      }
      l->anchor = i;
    }
    SetCaret(l, i);
  } else {  // LBS_MULTIPLESEL
    if (toggle) {
      SetSel(l, i, !l->items[i].sel);
      changed = true;
    }
    SetCaret(l, i);
  }
  EnsureVisible(l, i);
  if (changed) Notify(l, LBN_SELCHANGE);
}

void ComboListDone(HWND combo, bool ok);
void ComboListChanged(HWND combo);

void Key(List* l, UINT vk) {
  DWORD style = l->Style();
  if ((style & LBS_WANTKEYBOARDINPUT) && !l->combo) {
    LRESULT r = SendMessageW(GetParent(l->hwnd), WM_VKEYTOITEM, MAKEWPARAM(vk, std::max(0, l->caret)), (LPARAM)l->hwnd);
    if (r == -2) return;
    if (r >= 0) {
      Choose(l, (int)r, false, false, false);
      return;
    }
  }
  int n = l->Count();
  if (n == 0) return;
  int c = l->caret < 0 ? 0 : l->caret;
  int to = -1;
  int page = std::max(1, FullyVisible(l) - 1);
  switch (vk) {
    case VK_UP: to = c - 1; break;
    case VK_DOWN: to = l->caret < 0 ? 0 : c + 1; break;
    case VK_PRIOR: to = c - page; break;
    case VK_NEXT: to = c + page; break;
    case VK_HOME: to = 0; break;
    case VK_END: to = n - 1; break;
    case VK_SPACE:
      if (l->Multi() && !(GetKeyState(VK_SHIFT) & 0x8000)) Choose(l, c, false, true, true);
      return;
    case VK_RETURN:
    case VK_ESCAPE:
      if (l->combo) ComboListDone(l->combo, vk == VK_RETURN);
      return;
    default: return;
  }
  to = std::max(0, std::min(to, n - 1));
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  if ((style & LBS_MULTIPLESEL) || ((style & LBS_EXTENDEDSEL) && ctrl && !shift)) {
    SetCaret(l, to);
    EnsureVisible(l, to);
  } else {
    Choose(l, to, shift && (style & LBS_EXTENDEDSEL), false, false);
  }
  if (l->combo) ComboListChanged(l->combo);
}

void CharKey(List* l, wchar_t ch) {
  DWORD style = l->Style();
  if ((style & LBS_WANTKEYBOARDINPUT) && !l->combo && !l->HasStrings()) {
    LRESULT r = SendMessageW(GetParent(l->hwnd), WM_CHARTOITEM, MAKEWPARAM(ch, std::max(0, l->caret)), (LPARAM)l->hwnd);
    if (r >= 0) Choose(l, (int)r, false, false, false);
    return;
  }
  if (!l->HasStrings() || ch < L' ') return;
  wchar_t s[2] = {ch, 0};
  int i = Find(l, l->caret, s, false);
  if (i != LB_ERR) {
    Choose(l, i, false, false, false);
    if (l->combo) ComboListChanged(l->combo);
  }
}

int GetTextCore(List* l, int i, LPWSTR out) {
  if (i < 0 || i >= l->Count()) return LB_ERR;
  if (!l->HasStrings()) {
    if (out) memcpy(out, &l->items[i].data, sizeof(ULONG_PTR));
    return sizeof(ULONG_PTR);
  }
  const std::wstring& t = l->items[i].text;
  if (out) {
    memcpy(out, t.c_str(), (t.size() + 1) * sizeof(wchar_t));
  }
  return (int)t.size();
}

int Dir(List* l, UINT attrs, LPCWSTR spec) {
  WIN32_FIND_DATAW fd;
  int last = LB_ERR;
  bool exclusive = (attrs & DDL_EXCLUSIVE) != 0;
  HANDLE f = FindFirstFileW(spec && *spec ? spec : L"*", &fd);
  if (f != INVALID_HANDLE_VALUE) {
    do {
      bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (dir) {
        if (!(attrs & DDL_DIRECTORY) || !wcscmp(fd.cFileName, L".")) continue;
      } else {
        if (exclusive && !(fd.dwFileAttributes & attrs & 0x3F)) continue;
        if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) &&
            !(fd.dwFileAttributes & attrs & (DDL_HIDDEN | DDL_SYSTEM)))
          continue;
      }
      Item it;
      it.text = dir ? L"[" + std::wstring(fd.cFileName) + L"]" : fd.cFileName;
      last = Insert(l, -1, it, true);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
  }
  if (attrs & DDL_DRIVES) {
    Item it;
    it.text = L"[-c-]";
    last = Insert(l, -1, it, true);
  }
  return last;
}

LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  List* l = ListOf(h);
  if (!l && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE: {
      l = new List;
      l->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)l);
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs->style & LBS_COMBOBOX) l->combo = (HWND)cs->lpCreateParams;
      HDC dc = CreateCompatibleDC(NULL);
      HGDIOBJ old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
      TEXTMETRICW tm;
      GetTextMetricsW(dc, &tm);
      SelectObject(dc, old);
      DeleteDC(dc);
      l->itemHeight = tm.tmHeight;
      break;
    }
    case WM_CREATE:
      if (l->OwnerDraw() && !l->Variable()) {
        MEASUREITEMSTRUCT mis = {l->combo ? (UINT)ODT_COMBOBOX : (UINT)ODT_LISTBOX, CtlIdOf(l), 0, 0,
                                 (UINT)l->itemHeight, 0};
        if (HWND o = OwnerOf(l)) SendMessageW(o, WM_MEASUREITEM, mis.CtlID, (LPARAM)&mis);
        if (mis.itemHeight) l->itemHeight = (int)mis.itemHeight;
      }
      UpdateScroll(l);
      return 0;
    case WM_NCDESTROY:
      ResetContent(l);
      SetWindowLongPtrW(h, 0, 0);
      delete l;
      return DefWindowProcW(h, msg, wp, lp);
    case WM_SIZE:
      SetTop(l, l->top);
      UpdateScroll(l);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      Paint(l, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFONT: {
      l->font = (HFONT)wp;
      if (!l->OwnerDraw()) {
        HDC dc = CreateCompatibleDC(NULL);
        HGDIOBJ old = SelectObject(dc, l->font ? (HGDIOBJ)l->font : GetStockObject(SYSTEM_FONT));
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        DeleteDC(dc);
        l->itemHeight = tm.tmHeight;
      }
      UpdateScroll(l);
      if (lp) Redraw(l);
      return 0;
    }
    case WM_GETFONT:
      return (LRESULT)l->font;
    case WM_SETFOCUS:
      l->focus = true;
      RedrawItem(l, l->caret < 0 ? 0 : l->caret);
      Notify(l, LBN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      l->focus = false;
      RedrawItem(l, l->caret < 0 ? 0 : l->caret);
      Redraw(l);
      Notify(l, LBN_KILLFOCUS);
      return 0;
    case WM_ENABLE:
      Redraw(l);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      if (l->combo && !IsWindowVisible(h)) return 0;
      if (l->combo && (l->Style() & WS_POPUP)) {
        RECT rc;
        GetWindowRect(h, &rc);
        POINT s = p;
        ClientToScreen(h, &s);
        if (!PtInRect(&rc, s)) {
          // A click outside the dropped list closes it.
          ComboListDone(l->combo, false);
          return 0;
        }
      }
      if (!l->combo && GetFocus() != h) SetFocus(h);
      bool outside;
      int i = ItemAt(l, p, &outside);
      if (outside && !l->combo) return 0;
      if (msg == WM_LBUTTONDBLCLK && !l->combo) {
        Notify(l, LBN_DBLCLK);
        return 0;
      }
      bool shift = (wp & MK_SHIFT) != 0, ctrl = (wp & MK_CONTROL) != 0;
      Choose(l, i, shift, ctrl, true);
      if (l->combo) ComboListChanged(l->combo);
      if (!l->combo) {
        SetCapture(h);
        l->tracking = true;
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      bool dropped = l->combo && (l->Style() & WS_POPUP) && IsWindowVisible(h);
      if (!l->tracking && !dropped) return 0;
      bool outside;
      int i = ItemAt(l, p, &outside);
      if (dropped && outside) return 0;
      if (l->Style() & LBS_MULTIPLESEL) return 0;
      if (i != (l->Multi() ? l->caret : l->cur)) {
        Choose(l, i, (l->Style() & LBS_EXTENDEDSEL) != 0, false, false);
        if (l->combo) ComboListChanged(l->combo);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (l->tracking) {
        l->tracking = false;
        if (GetCapture() == h) ReleaseCapture();
        return 0;
      }
      if (l->combo && (l->Style() & WS_POPUP) && IsWindowVisible(h)) {
        POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
        RECT rc;
        GetClientRect(h, &rc);
        if (PtInRect(&rc, p)) ComboListDone(l->combo, true);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      l->tracking = false;
      return 0;
    case WM_KEYDOWN:
      Key(l, (UINT)wp);
      return 0;
    case WM_CHAR:
      CharKey(l, (wchar_t)wp);
      return 0;
    case WM_MOUSEWHEEL:
      SetTop(l, l->top - 3 * (SHORT)HIWORD(wp) / WHEEL_DELTA);
      return 0;
    case WM_VSCROLL: {
      int page = std::max(1, FullyVisible(l) - 1);
      switch (LOWORD(wp)) {
        case SB_LINEUP: SetTop(l, l->top - 1); break;
        case SB_LINEDOWN: SetTop(l, l->top + 1); break;
        case SB_PAGEUP: SetTop(l, l->top - page); break;
        case SB_PAGEDOWN: SetTop(l, l->top + page); break;
        case SB_TOP: SetTop(l, 0); break;
        case SB_BOTTOM: SetTop(l, l->Count()); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: SetTop(l, (SHORT)HIWORD(wp)); break;
      }
      return 0;
    }
    case WM_HSCROLL: {
      RECT rc;
      GetClientRect(h, &rc);
      int x = l->xOffset;
      switch (LOWORD(wp)) {
        case SB_LINEUP: x -= 8; break;
        case SB_LINEDOWN: x += 8; break;
        case SB_PAGEUP: x -= rc.right; break;
        case SB_PAGEDOWN: x += rc.right; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: x = (SHORT)HIWORD(wp); break;
      }
      x = std::max(0, std::min(x, std::max(0, l->extent - (int)rc.right)));
      if (x != l->xOffset) {
        l->xOffset = x;
        UpdateScroll(l);
        Redraw(l);
      }
      return 0;
    }
    case LB_ADDSTRING:
    case LB_INSERTSTRING: {
      if (l->NoData()) return LB_ERR;
      Item it;
      if (l->HasStrings())
        it.text = lp ? (LPCWSTR)lp : L"";
      else
        it.data = (ULONG_PTR)lp;
      return Insert(l, msg == LB_ADDSTRING ? -1 : (int)(INT_PTR)wp, it, msg == LB_ADDSTRING);
    }
    case LB_DELETESTRING: {
      int i = (int)wp;
      if (i < 0 || i >= (int)l->items.size()) return LB_ERR;
      Delete(l, i);
      return (LRESULT)l->items.size();
    }
    case LB_RESETCONTENT:
      ResetContent(l);
      return 0;
    case LB_GETCOUNT:
      return l->Count();
    case LB_GETTEXT:
      return GetTextCore(l, (int)wp, (LPWSTR)lp);
    case LB_GETTEXTLEN: {
      int i = (int)wp;
      if (i < 0 || i >= l->Count()) return LB_ERR;
      return l->HasStrings() ? (LRESULT)l->items[i].text.size() : (LRESULT)sizeof(ULONG_PTR);
    }
    case LB_SETCURSEL: {
      if (l->Multi()) return LB_ERR;
      int i = (int)(INT_PTR)wp;
      if (i < -1 || i >= l->Count()) i = -1;
      SetCur(l, i);
      EnsureVisible(l, i);
      return i == -1 ? LB_ERR : i;
    }
    case LB_GETCURSEL:
      if (l->Multi()) return l->caret;
      return l->cur;
    case LB_SETSEL: {
      if (!l->Multi()) return LB_ERR;
      int i = (int)(INT_PTR)lp;
      if (i == -1) {
        for (int k = 0; k < (int)l->items.size(); k++) SetSel(l, k, wp != 0);
        return 0;
      }
      if (i < 0 || i >= (int)l->items.size()) return LB_ERR;
      SetSel(l, i, wp != 0);
      if (wp) {
        SetCaret(l, i);
        l->anchor = i;
      }
      return 0;
    }
    case LB_GETSEL: {
      int i = (int)wp;
      if (i < 0 || i >= l->Count()) return LB_ERR;
      return IsSelected(l, i) ? 1 : 0;
    }
    case LB_GETSELCOUNT: {
      if (!l->Multi()) return LB_ERR;
      int n = 0;
      for (const Item& it : l->items) n += it.sel;
      return n;
    }
    case LB_GETSELITEMS: {
      if (!l->Multi()) return LB_ERR;
      int n = 0;
      int* out = (int*)lp;
      for (int i = 0; i < (int)l->items.size() && n < (int)wp; i++)
        if (l->items[i].sel) out[n++] = i;
      return n;
    }
    case LB_SELITEMRANGE:
    case LB_SELITEMRANGEEX: {
      if (!l->Multi()) return LB_ERR;
      int a, b;
      bool on;
      if (msg == LB_SELITEMRANGE) {
        a = LOWORD(lp);
        b = HIWORD(lp);
        on = wp != 0;
      } else {
        a = (int)wp;
        b = (int)lp;
        on = a <= b;
        if (a > b) std::swap(a, b);
      }
      for (int i = std::max(0, a); i <= b && i < (int)l->items.size(); i++) SetSel(l, i, on);
      return 0;
    }
    case LB_GETITEMDATA: {
      int i = (int)wp;
      if (i < 0 || i >= (int)l->items.size()) return LB_ERR;
      return (LRESULT)l->items[i].data;
    }
    case LB_SETITEMDATA: {
      int i = (int)wp;
      if (i < 0 || i >= (int)l->items.size()) return LB_ERR;
      l->items[i].data = (ULONG_PTR)lp;
      return TRUE;
    }
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT:
      return Find(l, (int)(INT_PTR)wp, (const wchar_t*)lp, msg == LB_FINDSTRINGEXACT);
    case LB_SELECTSTRING: {
      int i = Find(l, (int)(INT_PTR)wp, (const wchar_t*)lp, false);
      if (i == LB_ERR) return LB_ERR;
      if (l->Multi()) {
        SetSel(l, i, true);
        SetCaret(l, i);
      } else {
        SetCur(l, i);
      }
      EnsureVisible(l, i);
      return i;
    }
    case LB_GETTOPINDEX:
      return l->top;
    case LB_SETTOPINDEX:
      if ((int)wp < 0 || (int)wp >= std::max(1, l->Count())) return LB_ERR;
      SetTop(l, (int)wp);
      return 0;
    case LB_GETITEMHEIGHT:
      return HeightOf(l, (int)wp);
    case LB_SETITEMHEIGHT: {
      int hgt = LOWORD(lp);
      if (hgt <= 0 || hgt > 255) return LB_ERR;
      if (l->Variable()) {
        if ((int)wp < 0 || (int)wp >= (int)l->items.size()) return LB_ERR;
        l->items[wp].height = hgt;
      } else {
        l->itemHeight = hgt;
      }
      UpdateScroll(l);
      Redraw(l);
      return 0;
    }
    case LB_GETITEMRECT: {
      int i = (int)wp;
      if (!lp || i < 0 || (i >= l->Count() && !(i == 0 && l->Count() == 0))) return LB_ERR;
      *(RECT*)lp = ItemRect(l, i);
      return 1;
    }
    case LB_ITEMFROMPOINT: {
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      if (l->Count() == 0) return MAKELRESULT(0, 1);
      bool outside;
      int i = ItemAt(l, p, &outside);
      return MAKELRESULT(i, outside ? 1 : 0);
    }
    case LB_SETCARETINDEX:
      if ((int)wp < 0 || (int)wp >= l->Count()) return LB_ERR;
      SetCaret(l, (int)wp);
      if (!LOWORD(lp)) EnsureVisible(l, (int)wp);
      return 0;
    case LB_GETCARETINDEX:
      return l->caret < 0 ? 0 : l->caret;
    case LB_SETANCHORINDEX:
      if ((int)wp < -1 || (int)wp >= l->Count()) return LB_ERR;
      l->anchor = (int)wp;
      return 0;
    case LB_GETANCHORINDEX:
      return l->anchor;
    case LB_SETCOLUMNWIDTH:
      l->columnWidth = (int)wp;
      return 0;
    case LB_SETHORIZONTALEXTENT:
      l->extent = (int)wp;
      UpdateScroll(l);
      Redraw(l);
      return 0;
    case LB_GETHORIZONTALEXTENT:
      return l->extent;
    case LB_INITSTORAGE:
      l->items.reserve(l->items.size() + wp);
      return (LRESULT)(l->items.size() + wp);
    case LB_SETCOUNT:
      if (!l->NoData()) return LB_ERR;
      l->nodataCount = (int)wp;
      l->items.resize(wp);
      UpdateScroll(l);
      Redraw(l);
      return 0;
    case LB_SETTABSTOPS: {
      l->tabs.clear();
      HDC dc = CreateCompatibleDC(NULL);
      HGDIOBJ old = SelectObject(dc, l->font ? (HGDIOBJ)l->font : GetStockObject(SYSTEM_FONT));
      TEXTMETRICW tm;
      GetTextMetricsW(dc, &tm);
      SelectObject(dc, old);
      DeleteDC(dc);
      // Tab stops come in dialog units.
      int unit = tm.tmAveCharWidth ? tm.tmAveCharWidth : 6;
      if (wp == 0) {
        l->tabs.push_back(unit * 8);
      } else {
        const INT* t = (const INT*)lp;
        for (WPARAM i = 0; i < wp; i++) l->tabs.push_back(MulDiv(t[i], unit, 4));
      }
      Redraw(l);
      return TRUE;
    }
    case LB_SETLOCALE: {
      LCID old = l->locale;
      l->locale = (LCID)wp;
      return old;
    }
    case LB_GETLOCALE:
      return l->locale;
    case LB_DIR:
      return Dir(l, (UINT)wp, (LPCWSTR)lp);
    case LB_ADDFILE: {
      Item it;
      it.text = lp ? (LPCWSTR)lp : L"";
      return Insert(l, -1, it, true);
    }
    case LB_GETLISTBOXINFO:
      return FullyVisible(l);
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------
// Combo boxes

struct Combo {
  HWND hwnd = NULL, edit = NULL, list = NULL;
  bool dropped = false, focus = false, pressed = false, extendedUI = false;
  int fieldHeight = 0;  // the selection field's height, inside the frame
  int listHeight = 100;
  int droppedWidth = 0;
  HFONT font = NULL;
  int selAtDrop = -1;
  WNDPROC editProc = NULL;

  DWORD Style() const { return (DWORD)GetWindowLongW(hwnd, GWL_STYLE); }
  UINT Kind() const { return Style() & 3; }
};

Combo* ComboOf(HWND h) { return (Combo*)GetWindowLongPtrW(h, 0); }

void ComboNotify(Combo* c, UINT code) {
  HWND parent = GetParent(c->hwnd);
  if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(c->hwnd), code), (LPARAM)c->hwnd);
}

const int kFrame = 2;  // the sunken edge around a combo box

int ButtonWidth() { return GetSystemMetrics(SM_CXVSCROLL); }

RECT FieldRect(Combo* c) {
  RECT rc;
  GetClientRect(c->hwnd, &rc);
  RECT f = MakeRect(rc.left + kFrame, rc.top + kFrame, rc.right - kFrame, rc.top + kFrame + c->fieldHeight);
  if (c->Kind() != CBS_SIMPLE) f.right -= ButtonWidth();
  return f;
}

RECT ButtonRect(Combo* c) {
  RECT rc;
  GetClientRect(c->hwnd, &rc);
  if (c->Kind() == CBS_SIMPLE) return MakeRect(0, 0, 0, 0);
  return MakeRect(rc.right - kFrame - ButtonWidth(), rc.top + kFrame, rc.right - kFrame, rc.bottom - kFrame);
}

int ListItemHeight(Combo* c) { return c->list ? (int)SendMessageW(c->list, LB_GETITEMHEIGHT, 0, 0) : 13; }

void FitFieldHeight(Combo* c) {
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, c->font ? (HGDIOBJ)c->font : GetStockObject(SYSTEM_FONT));
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  DeleteDC(dc);
  c->fieldHeight = tm.tmHeight + 4;
  if (c->Style() & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
    MEASUREITEMSTRUCT mis = {ODT_COMBOBOX, (UINT)GetDlgCtrlID(c->hwnd), (UINT)-1, 0, (UINT)tm.tmHeight, 0};
    SendMessageW(GetParent(c->hwnd), WM_MEASUREITEM, mis.CtlID, (LPARAM)&mis);
    c->fieldHeight = (int)mis.itemHeight + 4;
  }
}

// Places the edit and the list for the combo box's size.
void LayoutCombo(Combo* c) {
  RECT rc;
  GetClientRect(c->hwnd, &rc);
  RECT f = FieldRect(c);
  if (c->edit) SetWindowPos(c->edit, NULL, f.left + 1, f.top + 1, f.right - f.left - 2, f.bottom - f.top - 2,
                            SWP_NOZORDER | SWP_NOACTIVATE);
  if (c->Kind() == CBS_SIMPLE && c->list) {
    int top = f.bottom + kFrame + 1;
    SetWindowPos(c->list, NULL, rc.left, top, rc.right - rc.left, std::max(0, (int)rc.bottom - top),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

int CurSel(Combo* c) { return c->list ? (int)SendMessageW(c->list, LB_GETCURSEL, 0, 0) : -1; }

std::wstring ItemText(Combo* c, int i) {
  if (!c->list || i < 0) return L"";
  int n = (int)SendMessageW(c->list, LB_GETTEXTLEN, i, 0);
  if (n < 0) return L"";
  List* l = ListOf(c->list);
  if (l && !l->HasStrings()) return L"";
  std::wstring s(n + 1, L'\0');
  SendMessageW(c->list, LB_GETTEXT, i, (LPARAM)&s[0]);
  s.resize(n);
  return s;
}

// Shows the selection in the field.
void SelectionToField(Combo* c) {
  int i = CurSel(c);
  if (c->edit) {
    std::wstring t = ItemText(c, i);
    SendMessageW(c->edit, WM_SETTEXT, 0, (LPARAM)t.c_str());
    if (c->focus || GetFocus() == c->edit) SendMessageW(c->edit, EM_SETSEL, 0, -1);
  } else {
    InvalidateRect(c->hwnd, NULL, FALSE);
  }
}

void Drop(Combo* c, bool show) {
  if (c->Kind() == CBS_SIMPLE || show == c->dropped || !c->list) return;
  if (show) {
    ComboNotify(c, CBN_DROPDOWN);
    if (!IsWindow(c->hwnd)) return;
    RECT wr;
    GetWindowRect(c->hwnd, &wr);
    int count = (int)SendMessageW(c->list, LB_GETCOUNT, 0, 0);
    int ih = ListItemHeight(c);
    int h = c->listHeight;
    int want = std::max(1, count) * ih + 2;
    if (!(c->Style() & CBS_NOINTEGRALHEIGHT) || want < h) h = std::min(h, want);
    if (h < ih + 2) h = ih + 2;
    int w = std::max((int)(wr.right - wr.left), c->droppedWidth);
    int y = wr.bottom;
    if (y + h > GetSystemMetrics(SM_CYSCREEN)) y = wr.top - h;
    c->selAtDrop = CurSel(c);
    c->dropped = true;
    SetWindowPos(c->list, HWND_TOPMOST, wr.left, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    int i = CurSel(c);
    if (i >= 0) SendMessageW(c->list, LB_SETTOPINDEX, i, 0);
    UpdateWindow(c->list);
    SetCapture(c->list);
    InvalidateRect(c->hwnd, NULL, FALSE);
  } else {
    c->dropped = false;
    if (GetCapture() == c->list) ReleaseCapture();
    ShowWindow(c->list, SW_HIDE);
    InvalidateRect(c->hwnd, NULL, FALSE);
    ComboNotify(c, CBN_CLOSEUP);
  }
}

void PaintCombo(Combo* c, HDC dc) {
  RECT rc;
  GetClientRect(c->hwnd, &rc);
  bool enabled = IsWindowEnabled(c->hwnd) != FALSE;
  HWND parent = GetParent(c->hwnd);
  RECT frame = rc;
  if (c->Kind() == CBS_SIMPLE) frame.bottom = frame.top + c->fieldHeight + 2 * kFrame;
  FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
  UINT ctl = enabled ? WM_CTLCOLOREDIT : WM_CTLCOLORSTATIC;
  HBRUSH br = parent ? (HBRUSH)SendMessageW(parent, ctl, (WPARAM)dc, (LPARAM)c->hwnd) : NULL;
  if (!br) br = (HBRUSH)DefWindowProcW(c->hwnd, ctl, (WPARAM)dc, (LPARAM)c->hwnd);
  RECT inner = frame;
  DrawEdge(dc, &inner, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
  FillRect(dc, &inner, br);
  if (c->Kind() != CBS_SIMPLE) {
    RECT b = ButtonRect(c);
    DrawFrameControl(dc, &b, DFC_SCROLL,
                     DFCS_SCROLLCOMBOBOX | (c->pressed ? DFCS_PUSHED | DFCS_FLAT : 0) | (enabled ? 0 : DFCS_INACTIVE));
  }
  if (c->edit) return;
  // A drop-down list shows its selection itself.
  RECT f = FieldRect(c);
  InflateRect(&f, -1, -1);
  int i = CurSel(c);
  bool hilite = c->focus && !c->dropped;
  HGDIOBJ oldFont = SelectObject(dc, c->font ? (HGDIOBJ)c->font : GetStockObject(SYSTEM_FONT));
  if (c->Style() & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
    DRAWITEMSTRUCT dis = {ODT_COMBOBOX, (UINT)GetDlgCtrlID(c->hwnd), (UINT)i, ODA_DRAWENTIRE, ODS_COMBOBOXEDIT,
                          c->hwnd, dc, f, i >= 0 ? (ULONG_PTR)SendMessageW(c->list, LB_GETITEMDATA, i, 0) : 0};
    if (hilite) dis.itemState |= ODS_SELECTED | ODS_FOCUS;
    if (!enabled) dis.itemState |= ODS_DISABLED;
    if (parent) SendMessageW(parent, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
  } else {
    if (hilite) FillRect(dc, &f, GetSysColorBrush(COLOR_HIGHLIGHT));
    std::wstring t = ItemText(c, i);
    int mode = SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(!enabled ? COLOR_GRAYTEXT : hilite ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    RECT tr = f;
    tr.left += 2;
    DrawTextW(dc, t.c_str(), (int)t.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SetBkMode(dc, mode);
    if (hilite) DrawFocusRect(dc, &f);
  }
  SelectObject(dc, oldFont);
}

bool ComboKey(Combo* c, UINT vk, bool alt) {
  if ((vk == VK_F4 && !alt) || (alt && (vk == VK_DOWN || vk == VK_UP))) {
    if (c->dropped)
      ComboListDone(c->hwnd, true);
    else
      Drop(c, true);
    return true;
  }
  if (c->dropped && (vk == VK_RETURN || vk == VK_ESCAPE)) {
    ComboListDone(c->hwnd, vk == VK_RETURN);
    return true;
  }
  if (vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
      ((vk == VK_HOME || vk == VK_END) && !c->edit)) {
    if (c->extendedUI && !c->dropped && vk == VK_DOWN) {
      Drop(c, true);
      return true;
    }
    int before = CurSel(c);
    SendMessageW(c->list, WM_KEYDOWN, vk, 0);
    if (!c->dropped && CurSel(c) != before) {
      SelectionToField(c);
      ComboNotify(c, CBN_SELCHANGE);
    }
    return true;
  }
  return false;
}

// The combo box's edit passes the keys that work the list to it.
LRESULT CALLBACK ComboEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  HWND combo = GetParent(h);
  Combo* c = combo ? ComboOf(combo) : NULL;
  if (!c || !c->editProc) return DefWindowProcW(h, msg, wp, lp);
  WNDPROC base = c->editProc;
  switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      bool alt = msg == WM_SYSKEYDOWN;
      if (ComboKey(c, (UINT)wp, alt)) return 0;
      break;
    }
    case WM_CHAR:
      if (c->dropped && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
      break;
    case WM_GETDLGCODE: {
      LRESULT r = CallWindowProcW(base, h, msg, wp, lp);
      MSG* m = (MSG*)lp;
      if (c->dropped && m && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE))
        r |= DLGC_WANTMESSAGE;
      return r;
    }
  }
  return CallWindowProcW(base, h, msg, wp, lp);
}

LRESULT CALLBACK ComboProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Combo* c = ComboOf(h);
  if (!c && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE:
      c = new Combo;
      c->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)c);
      // The scroll bar styles are for the list, not the field.
      SetWindowLongW(h, GWL_STYLE, GetWindowLongW(h, GWL_STYLE) & ~(WS_VSCROLL | WS_HSCROLL));
      break;
    case WM_CREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      DWORD style = cs->style;
      FitFieldHeight(c);
      UINT kind = style & 3;
      DWORD ls = LBS_NOTIFY | LBS_COMBOBOX | WS_VSCROLL | WS_BORDER;
      if (style & WS_HSCROLL) ls |= WS_HSCROLL;
      if (style & CBS_SORT) ls |= LBS_SORT;
      if (style & CBS_OWNERDRAWFIXED) ls |= LBS_OWNERDRAWFIXED;
      if (style & CBS_OWNERDRAWVARIABLE) ls |= LBS_OWNERDRAWVARIABLE;
      if (style & CBS_HASSTRINGS) ls |= LBS_HASSTRINGS;
      if (style & CBS_NOINTEGRALHEIGHT) ls |= LBS_NOINTEGRALHEIGHT;
      if (style & CBS_DISABLENOSCROLL) ls |= LBS_DISABLENOSCROLL;
      int total = kFrame * 2 + c->fieldHeight;
      HINSTANCE inst = cs->hInstance;
      if (kind == CBS_SIMPLE) {
        c->list = CreateWindowExW(WS_EX_CLIENTEDGE, L"ComboLBox", L"", ls | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h,
                                  (HMENU)(UINT_PTR)kListId, inst, (LPVOID)h);
      } else {
        c->listHeight = std::max(cs->cy - total, 0);
        if (c->listHeight < 20) c->listHeight = 100;
        c->list = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"ComboLBox", L"", ls | WS_POPUP, 0, 0, 0, 0, h,
                                  (HMENU)(UINT_PTR)kListId, inst, (LPVOID)h);
        SetWindowPos(h, NULL, 0, 0, cs->cx, total, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      if (kind != CBS_DROPDOWNLIST) {
        DWORD es = WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NOHIDESEL;
        if ((style & CBS_AUTOHSCROLL) || kind == CBS_SIMPLE) es |= ES_AUTOHSCROLL;
        if (style & CBS_UPPERCASE) es |= ES_UPPERCASE;
        if (style & CBS_LOWERCASE) es |= ES_LOWERCASE;
        c->edit = CreateWindowExW(0, L"Edit", L"", es, 0, 0, 0, 0, h, (HMENU)(UINT_PTR)kEditId, inst, NULL);
        if (c->edit) {
          if (Window* ew = winport::GetWindow(c->edit))
            if (Window* cw = winport::GetWindow(h)) ew->ansiCreate = cw->ansiCreate;
          c->editProc = (WNDPROC)SetWindowLongPtrW(c->edit, GWLP_WNDPROC, (LONG_PTR)ComboEditProc);
        }
      }
      if (!c->list) return -1;
      LayoutCombo(c);
      return 0;
    }
    case WM_NCDESTROY:
      if (c->list && IsWindow(c->list) && c->Kind() != CBS_SIMPLE) DestroyWindow(c->list);
      SetWindowLongPtrW(h, 0, 0);
      delete c;
      return DefWindowProcW(h, msg, wp, lp);
    case WM_WINDOWPOSCHANGING: {
      // The height asked of a drop-down combo box is that of its list.
      WINDOWPOS* wpos = (WINDOWPOS*)lp;
      if (c->Kind() != CBS_SIMPLE && !(wpos->flags & SWP_NOSIZE)) {
        int total = kFrame * 2 + c->fieldHeight;
        if (wpos->cy != total) {
          if (wpos->cy > total) c->listHeight = wpos->cy - total;
          wpos->cy = total;
        }
      }
      return 0;
    }
    case WM_SIZE:
      LayoutCombo(c);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintCombo(c, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETDLGCODE: {
      LRESULT r = DLGC_WANTARROWS | DLGC_WANTCHARS;
      MSG* m = (MSG*)lp;
      if (c->dropped && m && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE))
        r |= DLGC_WANTMESSAGE;
      return r;
    }
    case WM_SETFONT:
      c->font = (HFONT)wp;
      if (c->edit) SendMessageW(c->edit, WM_SETFONT, wp, lp);
      if (c->list) SendMessageW(c->list, WM_SETFONT, wp, lp);
      FitFieldHeight(c);
      if (c->Kind() != CBS_SIMPLE) {
        RECT wr;
        GetWindowRect(h, &wr);
        SetWindowPos(h, NULL, 0, 0, wr.right - wr.left, kFrame * 2 + c->fieldHeight,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      LayoutCombo(c);
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)c->font;
    case WM_ENABLE:
      if (c->edit) EnableWindow(c->edit, (BOOL)wp);
      if (c->list) EnableWindow(c->list, (BOOL)wp);
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case WM_SETFOCUS:
      if (c->edit) {
        SetFocus(c->edit);
        SendMessageW(c->edit, EM_SETSEL, 0, -1);
        return 0;
      }
      c->focus = true;
      InvalidateRect(h, NULL, FALSE);
      ComboNotify(c, CBN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS: {
      HWND to = (HWND)wp;
      if (to == c->edit || to == c->list) return 0;
      if (c->dropped) ComboListDone(h, false);
      c->focus = false;
      InvalidateRect(h, NULL, FALSE);
      ComboNotify(c, CBN_KILLFOCUS);
      return 0;
    }
    case WM_COMMAND: {
      HWND from = (HWND)lp;
      if (from == c->edit && c->edit) {
        switch (HIWORD(wp)) {
          case EN_CHANGE: ComboNotify(c, CBN_EDITCHANGE); break;
          case EN_UPDATE: ComboNotify(c, CBN_EDITUPDATE); break;
          case EN_SETFOCUS:
            if (!c->focus) {
              c->focus = true;
              ComboNotify(c, CBN_SETFOCUS);
            }
            break;
          case EN_KILLFOCUS: {
            HWND f = GetFocus();
            if (f != h && f != c->list && c->focus) {
              if (c->dropped) ComboListDone(h, false);
              c->focus = false;
              ComboNotify(c, CBN_KILLFOCUS);
            }
            break;
          }
        }
      } else if (from == c->list && c->list) {
        switch (HIWORD(wp)) {
          case LBN_SELCHANGE:
            if (c->Kind() == CBS_SIMPLE) {
              SelectionToField(c);
              ComboNotify(c, CBN_SELCHANGE);
            }
            break;
          case LBN_DBLCLK:
            ComboNotify(c, CBN_DBLCLK);
            break;
        }
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      HWND target = c->edit ? c->edit : h;
      if (GetFocus() != target) SetFocus(target);
      RECT b = ButtonRect(c);
      if (c->Kind() == CBS_DROPDOWNLIST || PtInRect(&b, p)) {
        if (c->dropped) {
          ComboListDone(h, true);
        } else {
          c->pressed = true;
          Drop(c, true);
          c->pressed = false;
        }
      }
      return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (ComboKey(c, (UINT)wp, msg == WM_SYSKEYDOWN)) return 0;
      break;
    case WM_CHAR:
      if (!c->edit && c->list) {
        int before = CurSel(c);
        SendMessageW(c->list, WM_CHAR, wp, lp);
        if (!c->dropped && CurSel(c) != before) {
          SelectionToField(c);
          ComboNotify(c, CBN_SELCHANGE);
        }
        return 0;
      }
      if (c->edit) return SendMessageW(c->edit, msg, wp, lp);
      return 0;
    case WM_MOUSEWHEEL:
      if (!c->dropped && c->list) {
        ComboKey(c, (SHORT)HIWORD(wp) > 0 ? VK_UP : VK_DOWN, false);
        return 0;
      }
      break;
    case WM_SETTEXT:
      if (c->edit) return SendMessageW(c->edit, WM_SETTEXT, wp, lp);
      {
        int i = (int)SendMessageW(c->list, LB_FINDSTRINGEXACT, (WPARAM)-1, lp);
        if (i < 0) return CB_ERR;
        SendMessageW(c->list, LB_SETCURSEL, i, 0);
        InvalidateRect(h, NULL, FALSE);
        return TRUE;
      }
    case WM_GETTEXT:
      if (c->edit) return SendMessageW(c->edit, WM_GETTEXT, wp, lp);
      {
        if (!wp || !lp) return 0;
        std::wstring t = ItemText(c, CurSel(c));
        size_t n = std::min(t.size(), (size_t)wp - 1);
        memcpy((void*)lp, t.c_str(), n * sizeof(wchar_t));
        ((wchar_t*)lp)[n] = 0;
        return (LRESULT)n;
      }
    case WM_GETTEXTLENGTH:
      if (c->edit) return SendMessageW(c->edit, WM_GETTEXTLENGTH, 0, 0);
      return (LRESULT)ItemText(c, CurSel(c)).size();
    case WM_CUT:
    case WM_COPY:
    case WM_PASTE:
    case WM_CLEAR:
    case WM_UNDO:
      return c->edit ? SendMessageW(c->edit, msg, wp, lp) : 0;

    case CB_ADDSTRING: return SendMessageW(c->list, LB_ADDSTRING, wp, lp);
    case CB_INSERTSTRING: return SendMessageW(c->list, LB_INSERTSTRING, wp, lp);
    case CB_DELETESTRING: {
      LRESULT r = SendMessageW(c->list, LB_DELETESTRING, wp, lp);
      InvalidateRect(h, NULL, FALSE);
      return r;
    }
    case CB_DIR: return SendMessageW(c->list, LB_DIR, wp, lp);
    case CB_FINDSTRING: return SendMessageW(c->list, LB_FINDSTRING, wp, lp);
    case CB_FINDSTRINGEXACT: return SendMessageW(c->list, LB_FINDSTRINGEXACT, wp, lp);
    case CB_GETCOUNT: return SendMessageW(c->list, LB_GETCOUNT, 0, 0);
    case CB_GETCURSEL: return CurSel(c);
    case CB_GETITEMDATA: return SendMessageW(c->list, LB_GETITEMDATA, wp, 0);
    case CB_SETITEMDATA: return SendMessageW(c->list, LB_SETITEMDATA, wp, lp);
    case CB_GETLBTEXT: return SendMessageW(c->list, LB_GETTEXT, wp, lp);
    case CB_GETLBTEXTLEN: return SendMessageW(c->list, LB_GETTEXTLEN, wp, 0);
    case CB_RESETCONTENT:
      SendMessageW(c->list, LB_RESETCONTENT, 0, 0);
      if (c->edit) SendMessageW(c->edit, WM_SETTEXT, 0, (LPARAM)L"");
      InvalidateRect(h, NULL, FALSE);
      return CB_OKAY;
    case CB_SETCURSEL: {
      LRESULT r = SendMessageW(c->list, LB_SETCURSEL, wp, 0);
      SelectionToField(c);
      return r;
    }
    case CB_SELECTSTRING: {
      int i = (int)SendMessageW(c->list, LB_FINDSTRING, wp, lp);
      if (i < 0) return CB_ERR;
      SendMessageW(c->list, LB_SETCURSEL, i, 0);
      SelectionToField(c);
      return i;
    }
    case CB_GETITEMHEIGHT:
      if ((int)wp == -1) return c->fieldHeight - 4;
      return SendMessageW(c->list, LB_GETITEMHEIGHT, wp, 0);
    case CB_SETITEMHEIGHT:
      if ((int)wp == -1) {
        c->fieldHeight = LOWORD(lp) + 4;
        if (c->Kind() != CBS_SIMPLE) {
          RECT wr;
          GetWindowRect(h, &wr);
          SetWindowPos(h, NULL, 0, 0, wr.right - wr.left, kFrame * 2 + c->fieldHeight,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        LayoutCombo(c);
        InvalidateRect(h, NULL, TRUE);
        return 0;
      }
      return SendMessageW(c->list, LB_SETITEMHEIGHT, wp, lp);
    case CB_SHOWDROPDOWN:
      if (wp)
        Drop(c, true);
      else if (c->dropped)
        ComboListDone(h, true);
      return TRUE;
    case CB_GETDROPPEDSTATE:
      return c->dropped;
    case CB_GETDROPPEDCONTROLRECT:
      if (lp) {
        RECT wr;
        GetWindowRect(h, &wr);
        RECT* r = (RECT*)lp;
        *r = wr;
        r->bottom = wr.bottom + c->listHeight;
        r->right = wr.left + std::max((int)(wr.right - wr.left), c->droppedWidth);
      }
      return CB_OKAY;
    case CB_LIMITTEXT:
      return c->edit ? SendMessageW(c->edit, EM_LIMITTEXT, wp, 0), TRUE : CB_ERR;
    case CB_GETEDITSEL:
      return c->edit ? SendMessageW(c->edit, EM_GETSEL, wp, lp) : CB_ERR;
    case CB_SETEDITSEL:
      if (!c->edit) return CB_ERR;
      SendMessageW(c->edit, EM_SETSEL, (WPARAM)(SHORT)LOWORD(lp), (LPARAM)(SHORT)HIWORD(lp));
      return TRUE;
    case CB_SETEXTENDEDUI:
      c->extendedUI = wp != 0;
      return CB_OKAY;
    case CB_GETEXTENDEDUI:
      return c->extendedUI;
    case CB_GETTOPINDEX: return SendMessageW(c->list, LB_GETTOPINDEX, 0, 0);
    case CB_SETTOPINDEX: return SendMessageW(c->list, LB_SETTOPINDEX, wp, 0) == LB_ERR ? CB_ERR : 0;
    case CB_GETHORIZONTALEXTENT: return SendMessageW(c->list, LB_GETHORIZONTALEXTENT, 0, 0);
    case CB_SETHORIZONTALEXTENT: return SendMessageW(c->list, LB_SETHORIZONTALEXTENT, wp, 0);
    case CB_GETDROPPEDWIDTH: {
      RECT wr;
      GetWindowRect(h, &wr);
      return std::max((int)(wr.right - wr.left), c->droppedWidth);
    }
    case CB_SETDROPPEDWIDTH:
      c->droppedWidth = (int)wp;
      return c->droppedWidth;
    case CB_INITSTORAGE: return SendMessageW(c->list, LB_INITSTORAGE, wp, lp);
    case CB_SETLOCALE: return SendMessageW(c->list, LB_SETLOCALE, wp, 0);
    case CB_GETLOCALE: return SendMessageW(c->list, LB_GETLOCALE, 0, 0);
    case CB_GETCOMBOBOXINFO: {
      COMBOBOXINFO* ci = (COMBOBOXINFO*)lp;
      if (!ci || ci->cbSize != sizeof(COMBOBOXINFO)) return FALSE;
      ci->rcItem = FieldRect(c);
      ci->rcButton = ButtonRect(c);
      ci->stateButton = c->Kind() == CBS_SIMPLE ? STATE_SYSTEM_INVISIBLE : (c->pressed ? STATE_SYSTEM_PRESSED : 0);
      ci->hwndCombo = h;
      ci->hwndItem = c->edit ? c->edit : h;
      ci->hwndList = c->list;
      return TRUE;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// The dropped list closes: with the selection taken (ok), or not.
void ComboListDone(HWND combo, bool ok) {
  Combo* c = ComboOf(combo);
  if (!c || !c->dropped) return;
  int sel = CurSel(c);
  ComboNotify(c, ok ? CBN_SELENDOK : CBN_SELENDCANCEL);
  if (!IsWindow(combo)) return;
  if (!ok && c->Kind() == CBS_DROPDOWNLIST && sel != c->selAtDrop) {
    // Escape puts back what was chosen before.
    SendMessageW(c->list, LB_SETCURSEL, c->selAtDrop, 0);
    SelectionToField(c);
    ComboNotify(c, CBN_SELCHANGE);
  }
  Drop(c, false);
}

// The list's selection moved while dropped or from the keyboard.
void ComboListChanged(HWND combo) {
  Combo* c = ComboOf(combo);
  if (!c) return;
  SelectionToField(c);
  ComboNotify(c, CBN_SELCHANGE);
}

}  // namespace

namespace winport {

void RegisterListClasses() {
  static bool done = false;
  if (done) return;
  done = true;
  LPCWSTR arrow = MAKEINTRESOURCEW(32512);
  RegisterSystemClass(L"ListBox", ListProc, CS_DBLCLKS | CS_PARENTDC, sizeof(void*), NULL, arrow);
  RegisterSystemClass(L"ComboLBox", ListProc, CS_DBLCLKS | CS_SAVEBITS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(L"ComboBox", ComboProc, CS_DBLCLKS | CS_PARENTDC | CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL,
                      arrow);
}

}  // namespace winport

// ---------------------------------------------------------------------
// The API

extern "C" {

BOOL WINAPI GetComboBoxInfo(HWND h, PCOMBOBOXINFO ci) {
  return (BOOL)SendMessageW(h, CB_GETCOMBOBOXINFO, 0, (LPARAM)ci);
}

DWORD WINAPI GetListBoxInfo(HWND h) { return (DWORD)SendMessageW(h, LB_GETLISTBOXINFO, 0, 0); }

// The DlgDir functions: a path's directory becomes current, and its
// file part fills the list.
static int DirListCore(HWND dlg, std::wstring& path, int listId, int staticId, UINT attrs, bool combo) {
  std::wstring spec = path.empty() ? L"*" : path;
  size_t slash = spec.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    std::wstring dir = spec.substr(0, slash + 1);
    if (!SetCurrentDirectoryW(dir.c_str())) return FALSE;
    spec = spec.substr(slash + 1);
  } else {
    // A bare directory name moves into it.
    DWORD a = GetFileAttributesW(spec.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
      SetCurrentDirectoryW(spec.c_str());
      spec = L"*";
    }
  }
  if (spec.empty()) spec = L"*";
  if (listId) {
    HWND l = GetDlgItem(dlg, listId);
    if (!l) return FALSE;
    SendMessageW(l, combo ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
    SendMessageW(l, combo ? CB_DIR : LB_DIR, attrs & ~DDL_POSTMSGS, (LPARAM)spec.c_str());
  }
  if (staticId) {
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetDlgItemTextW(dlg, staticId, cwd);
  }
  path = spec;
  return TRUE;
}

int WINAPI DlgDirListW(HWND dlg, LPWSTR path, int listId, int staticId, UINT attrs) {
  std::wstring p = path ? path : L"";
  int r = DirListCore(dlg, p, listId, staticId, attrs, false);
  if (r && path) wcscpy(path, p.c_str());
  return r;
}

int WINAPI DlgDirListA(HWND dlg, LPSTR path, int listId, int staticId, UINT attrs) {
  std::wstring p = path ? Widen(path) : L"";
  int r = DirListCore(dlg, p, listId, staticId, attrs, false);
  if (r && path) strcpy(path, Narrow(p.c_str()).c_str());
  return r;
}

int WINAPI DlgDirListComboBoxW(HWND dlg, LPWSTR path, int listId, int staticId, UINT attrs) {
  std::wstring p = path ? path : L"";
  int r = DirListCore(dlg, p, listId, staticId, attrs, true);
  if (r && path) wcscpy(path, p.c_str());
  return r;
}

int WINAPI DlgDirListComboBoxA(HWND dlg, LPSTR path, int listId, int staticId, UINT attrs) {
  std::wstring p = path ? Widen(path) : L"";
  int r = DirListCore(dlg, p, listId, staticId, attrs, true);
  if (r && path) strcpy(path, Narrow(p.c_str()).c_str());
  return r;
}

// The selected entry: a directory ("[name]") comes back with a trailing
// separator, and the result says it was one.
static BOOL DirSelectCore(HWND dlg, std::wstring* out, int id, bool combo) {
  HWND l = GetDlgItem(dlg, id);
  if (!l) return FALSE;
  int i = (int)SendMessageW(l, combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0);
  if (i < 0) return FALSE;
  int n = (int)SendMessageW(l, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, i, 0);
  std::wstring s(n + 1, L'\0');
  SendMessageW(l, combo ? CB_GETLBTEXT : LB_GETTEXT, i, (LPARAM)&s[0]);
  s.resize(n);
  bool dir = s.size() >= 2 && s.front() == L'[' && s.back() == L']';
  if (dir) {
    s = s.substr(1, s.size() - 2);
    if (s.size() == 3 && s[0] == L'-' && s[2] == L'-')
      s = std::wstring(1, s[1]) + L":";
    else
      s += L"/";
  }
  *out = s;
  return dir;
}

BOOL WINAPI DlgDirSelectExW(HWND dlg, LPWSTR out, int n, int id) {
  std::wstring s;
  BOOL dir = DirSelectCore(dlg, &s, id, false);
  if (out && n > 0) lstrcpynW(out, s.c_str(), n);
  return dir;
}

BOOL WINAPI DlgDirSelectExA(HWND dlg, LPSTR out, int n, int id) {
  std::wstring s;
  BOOL dir = DirSelectCore(dlg, &s, id, false);
  if (out && n > 0) lstrcpynA(out, Narrow(s.c_str()).c_str(), n);
  return dir;
}

BOOL WINAPI DlgDirSelectComboBoxExW(HWND dlg, LPWSTR out, int n, int id) {
  std::wstring s;
  BOOL dir = DirSelectCore(dlg, &s, id, true);
  if (out && n > 0) lstrcpynW(out, s.c_str(), n);
  return dir;
}

BOOL WINAPI DlgDirSelectComboBoxExA(HWND dlg, LPSTR out, int n, int id) {
  std::wstring s;
  BOOL dir = DirSelectCore(dlg, &s, id, true);
  if (out && n > 0) lstrcpynA(out, Narrow(s.c_str()).c_str(), n);
  return dir;
}

}  // extern "C"
