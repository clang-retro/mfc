// comctl_listview.cpp - the list view: items with subitems shown as
// large icons, small icons, a list or a report with a header, with
// selection, keyboard and mouse handling, label editing, sorting,
// callbacks and owner data.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

const UINT_PTR kEditTimer = 1;
const UINT_PTR kEditSubclass = 0x4C56;
const int kTextPad = 6;  // between a column's edge and its text

struct SubItem {
  std::wstring text;
  bool callback = false;
  int image = I_IMAGENONE;
};

struct Item {
  std::wstring text;
  bool callback = false;  // the text comes from LVN_GETDISPINFO
  int image = I_IMAGENONE;
  LPARAM param = 0;
  UINT state = 0;
  int indent = 0;
  std::vector<SubItem> subs;  // subitem n is subs[n - 1]
  POINT pos = {0, 0};         // in the icon views, once placed
  bool placed = false;
};

struct Column {
  int sub = 0;  // the subitem shown
  int fmt = LVCFMT_LEFT;
};

struct ListView {
  HWND hwnd = NULL, header = NULL, edit = NULL, tooltips = NULL;
  bool ownerData = false;
  std::vector<Item> items;
  std::vector<UINT> odState;  // owner data: only the state is ours
  std::vector<Column> cols;
  HIMAGELIST images[3] = {};
  COLORREF bk = CLR_DEFAULT, text = CLR_DEFAULT, textBk = CLR_DEFAULT;
  DWORD ex = 0;
  UINT callbackMask = 0;
  int focus = -1, mark = -1, hot = -1;
  HFONT font = NULL;
  POINT origin = {0, 0};  // the view's scroll position, in pixels
  bool unicodeNotify = true;
  int listColW = 0;            // a list view column's width, 0 to work it out
  SIZE spacing = {0, 0};       // icon view cells, 0 for the default
  int rowHeight = 0;           // from WM_MEASUREITEM for owner draw
  bool measured = false;
  // The mouse.
  int pressItem = -1;
  POINT pressPt = {0, 0};
  bool pressRight = false, pressWasSelected = false, pressOnLabel = false, tracking = false;
  int pendingEdit = -1;
  // Typing to find an item.
  std::wstring search;
  DWORD searchTime = 0;
  int editItem = -1;
  bool ending = false;
  DWORD hoverTime = (DWORD)-1;
  HCURSOR hotCursor = NULL;
};

ListView* StateOf(HWND h) { return (ListView*)GetWindowLongPtrW(h, 0); }
DWORD StyleOf(HWND h) { return (DWORD)GetWindowLongW(h, GWL_STYLE); }
int View(ListView* lv) { return StyleOf(lv->hwnd) & LVS_TYPEMASK; }
int Count(ListView* lv) { return lv->ownerData ? (int)lv->odState.size() : (int)lv->items.size(); }
bool Valid(ListView* lv, int i) { return i >= 0 && i < Count(lv); }
UINT& StateRef(ListView* lv, int i) { return lv->ownerData ? lv->odState[i] : lv->items[i].state; }
HFONT FontOf(ListView* lv) { return lv->font ? lv->font : ControlFont(); }
bool HasFocus(ListView* lv) { return GetFocus() == lv->hwnd; }

COLORREF BkColor(ListView* lv) { return lv->bk == CLR_DEFAULT ? GetSysColor(COLOR_WINDOW) : lv->bk; }
COLORREF TextColor(ListView* lv) { return lv->text == CLR_DEFAULT ? GetSysColor(COLOR_WINDOWTEXT) : lv->text; }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

int TextWidth(ListView* lv, const std::wstring& s) {
  HDC dc = GetDC(lv->hwnd);
  HGDIOBJ old = SelectObject(dc, FontOf(lv));
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  SelectObject(dc, old);
  ReleaseDC(lv->hwnd, dc);
  return sz.cx;
}

int FontHeight(ListView* lv) {
  HDC dc = GetDC(lv->hwnd);
  HGDIOBJ old = SelectObject(dc, FontOf(lv));
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(lv->hwnd, dc);
  return tm.tmHeight;
}

SIZE ImageSize(HIMAGELIST il, int cx, int cy) {
  SIZE s = {cx, cy};
  int w, h;
  if (il && ImageList_GetIconSize(il, &w, &h)) s = SIZE{w, h};
  return s;
}

// The parts of the layout every item shares.
struct Metrics {
  int fh;        // font height
  int row;       // a row in the report, list and small icon views
  SIZE small;    // the small icon, 0 wide without an image list
  SIZE large;    // the large icon
  SIZE state;    // the state icon or check box, 0 wide without one
  int headerH;   // the report's header
  SIZE cell;     // a cell in the icon and small icon views
  RECT client;
};

Metrics GetMetrics(ListView* lv);

// ---------------------------------------------------------------------
// Notifications

LRESULT Notify(ListView* lv, UINT code, NMHDR* nm = NULL) { return SendNotify(lv->hwnd, NULL, code, nm); }

LRESULT NotifyItem(ListView* lv, UINT code, int item, int sub = 0, UINT newState = 0, UINT oldState = 0,
                   UINT changed = 0, POINT pt = POINT{0, 0}) {
  NMLISTVIEW nm = {};
  nm.iItem = item;
  nm.iSubItem = sub;
  nm.uNewState = newState;
  nm.uOldState = oldState;
  nm.uChanged = changed;
  nm.ptAction = pt;
  if (Valid(lv, item) && !lv->ownerData) nm.lParam = lv->items[item].param;
  return Notify(lv, code, &nm.hdr);
}

LRESULT NotifyActivate(ListView* lv, UINT code, int item, int sub, POINT pt) {
  NMITEMACTIVATE nm = {};
  nm.iItem = item;
  nm.iSubItem = sub;
  nm.ptAction = pt;
  if (Valid(lv, item) && !lv->ownerData) nm.lParam = lv->items[item].param;
  if (GetKeyState(VK_SHIFT) & 0x8000) nm.uKeyFlags |= LVKF_SHIFT;
  if (GetKeyState(VK_CONTROL) & 0x8000) nm.uKeyFlags |= LVKF_CONTROL;
  if (GetKeyState(VK_MENU) & 0x8000) nm.uKeyFlags |= LVKF_ALT;
  return Notify(lv, code, &nm.hdr);
}

// What an item shows, with the parts the parent keeps asked for.
struct Shown {
  std::wstring text;
  int image = I_IMAGENONE;
  UINT state = 0;
  int indent = 0;
  LPARAM param = 0;
};

Shown Resolve(ListView* lv, int i, int sub, UINT want) {
  Shown s;
  UINT ask = 0;
  if (lv->ownerData) {
    s.state = lv->odState[i];
    ask = want & (LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT | LVIF_PARAM);
    if (sub) ask &= LVIF_TEXT | LVIF_IMAGE;
    if ((want & LVIF_STATE) && lv->callbackMask && !sub) ask |= LVIF_STATE;
  } else {
    Item& it = lv->items[i];
    s.param = it.param;
    if (sub == 0) {
      s.text = it.text;
      s.image = it.image;
      s.state = it.state;
      s.indent = it.indent;
      if ((want & LVIF_TEXT) && it.callback) ask |= LVIF_TEXT;
      if ((want & LVIF_IMAGE) && it.image == I_IMAGECALLBACK) ask |= LVIF_IMAGE;
      if ((want & LVIF_INDENT) && it.indent == I_INDENTCALLBACK) ask |= LVIF_INDENT;
      if ((want & LVIF_STATE) && lv->callbackMask) ask |= LVIF_STATE;
    } else if (sub - 1 < (int)it.subs.size()) {
      const SubItem& si = it.subs[sub - 1];
      s.text = si.text;
      s.image = si.image;
      if ((want & LVIF_TEXT) && si.callback) ask |= LVIF_TEXT;
      if ((want & LVIF_IMAGE) && si.image == I_IMAGECALLBACK) ask |= LVIF_IMAGE;
    }
  }
  if (!ask) return s;

  bool u = lv->unicodeNotify;
  NMLVDISPINFOW di = {};
  wchar_t wbuf[1024];
  char abuf[1024];
  di.item.mask = ask;
  di.item.iItem = i;
  di.item.iSubItem = sub;
  di.item.lParam = s.param;
  di.item.stateMask = lv->callbackMask;
  if (ask & LVIF_TEXT) {
    wbuf[0] = 0;
    abuf[0] = 0;
    di.item.pszText = u ? wbuf : (LPWSTR)abuf;
    di.item.cchTextMax = 1024;
  }
  Notify(lv, u ? LVN_GETDISPINFOW : LVN_GETDISPINFOA, &di.hdr);
  if (ask & LVIF_TEXT) s.text = TextIn(di.item.pszText, u);
  if (ask & LVIF_IMAGE) s.image = di.item.iImage;
  if (ask & LVIF_INDENT) s.indent = di.item.iIndent;
  if (ask & LVIF_PARAM) s.param = di.item.lParam;
  if (ask & LVIF_STATE) s.state = (s.state & ~lv->callbackMask) | (di.item.state & lv->callbackMask);
  // The parent may ask us to keep what it answered.
  if ((di.item.mask & LVIF_DI_SETITEM) && !lv->ownerData) {
    Item& it = lv->items[i];
    if (sub == 0) {
      if (ask & LVIF_TEXT) {
        it.text = s.text;
        it.callback = false;
      }
      if (ask & LVIF_IMAGE) it.image = s.image;
      if (ask & LVIF_INDENT) it.indent = s.indent;
    } else if (sub - 1 < (int)it.subs.size()) {
      if (ask & LVIF_TEXT) {
        it.subs[sub - 1].text = s.text;
        it.subs[sub - 1].callback = false;
      }
      if (ask & LVIF_IMAGE) it.subs[sub - 1].image = s.image;
    }
  }
  return s;
}

std::wstring ItemText(ListView* lv, int i, int sub) { return Resolve(lv, i, sub, LVIF_TEXT).text; }

// ---------------------------------------------------------------------
// Layout

int ColumnCount(ListView* lv) { return lv->header ? (int)SendMessageW(lv->header, HDM_GETITEMCOUNT, 0, 0) : 0; }

int HeaderHeight(ListView* lv) {
  if (!lv->header || !(StyleOf(lv->header) & WS_VISIBLE)) return 0;
  RECT r;
  GetWindowRect(lv->header, &r);
  return r.bottom - r.top;
}

int ColumnWidth(ListView* lv, int c) {
  HDITEMW hi = {};
  hi.mask = HDI_WIDTH;
  SendMessageW(lv->header, HDM_GETITEMW, c, (LPARAM)&hi);
  return hi.cxy;
}

int TotalWidth(ListView* lv) {
  int w = 0;
  for (int c = 0, n = ColumnCount(lv); c < n; c++) w += ColumnWidth(lv, c);
  return w;
}

// A column's span across the client, with the header's order.
RECT ColumnRect(ListView* lv, int c) {
  RECT r = {0, 0, 0, 0};
  if (lv->header) SendMessageW(lv->header, HDM_GETITEMRECT, c, (LPARAM)&r);
  OffsetRect(&r, -lv->origin.x, 0);
  return r;
}

// The column that shows a subitem.
int ColumnOf(ListView* lv, int sub) {
  for (size_t c = 0; c < lv->cols.size(); c++)
    if (lv->cols[c].sub == sub) return (int)c;
  return sub < (int)lv->cols.size() ? sub : -1;
}

void Measure(ListView* lv) {
  if (lv->measured) return;
  lv->measured = true;
  if (!(StyleOf(lv->hwnd) & LVS_OWNERDRAWFIXED) || View(lv) != LVS_REPORT) return;
  MEASUREITEMSTRUCT mi = {};
  mi.CtlType = ODT_LISTVIEW;
  mi.CtlID = (UINT)GetDlgCtrlID(lv->hwnd);
  mi.itemHeight = (UINT)FontHeight(lv) + 2;
  SendMessageW(GetParent(lv->hwnd), WM_MEASUREITEM, mi.CtlID, (LPARAM)&mi);
  lv->rowHeight = (int)mi.itemHeight;
}

int MaxLabelWidth(ListView* lv) {
  int w = 0;
  for (int i = 0, n = std::min(Count(lv), 2000); i < n; i++) w = std::max(w, TextWidth(lv, ItemText(lv, i, 0)));
  return w;
}

Metrics GetMetrics(ListView* lv) {
  Metrics m;
  Measure(lv);
  m.fh = FontHeight(lv);
  m.small = lv->images[LVSIL_SMALL] ? ImageSize(lv->images[LVSIL_SMALL], 16, 16) : SIZE{0, 0};
  m.large = ImageSize(lv->images[LVSIL_NORMAL], GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
  if (lv->images[LVSIL_STATE])
    m.state = ImageSize(lv->images[LVSIL_STATE], 16, 16);
  else if (lv->ex & LVS_EX_CHECKBOXES)
    m.state = SIZE{16, 16};
  else
    m.state = SIZE{0, 0};
  m.row = std::max(m.fh + 3, (int)std::max(m.small.cy, m.state.cy) + 1);
  if (lv->rowHeight > 0) m.row = lv->rowHeight;
  m.headerH = View(lv) == LVS_REPORT ? HeaderHeight(lv) : 0;
  GetClientRect(lv->hwnd, &m.client);
  if (View(lv) == LVS_ICON) {
    m.cell.cx = lv->spacing.cx > 0 ? lv->spacing.cx : std::max(GetSystemMetrics(SM_CXICONSPACING), (int)m.large.cx + 8);
    m.cell.cy =
        lv->spacing.cy > 0 ? lv->spacing.cy : std::max(GetSystemMetrics(SM_CYICONSPACING), (int)m.large.cy + 2 * m.fh + 12);
  } else if (View(lv) == LVS_SMALLICON) {
    m.cell.cx = m.state.cx + m.small.cx + std::min(MaxLabelWidth(lv), 160) + 12;
    m.cell.cy = m.row;
  } else {
    int w = lv->listColW > 0 ? lv->listColW : m.state.cx + m.small.cx + MaxLabelWidth(lv) + 12;
    m.cell.cx = std::max(w, 16);
    m.cell.cy = m.row;
  }
  return m;
}

// Items per column in the list view, and per row of the icon views.
int PerColumn(ListView* lv, const Metrics& m) { return std::max(1, (int)(m.client.bottom / m.row)); }
int PerRow(ListView* lv, const Metrics& m) { return std::max(1, (int)(m.client.right / m.cell.cx)); }
int PerPage(ListView* lv, const Metrics& m) {
  return std::max(1, (int)((m.client.bottom - m.headerH) / m.row));
}

// Where an item's cell starts in the icon views, in view coordinates.
POINT CellOrigin(ListView* lv, const Metrics& m, int i) {
  if (!lv->ownerData && lv->items[i].placed) return lv->items[i].pos;
  if (StyleOf(lv->hwnd) & LVS_ALIGNLEFT) {
    int per = std::max(1, (int)(m.client.bottom / m.cell.cy));
    return POINT{(i / per) * m.cell.cx, (i % per) * m.cell.cy};
  }
  int per = PerRow(lv, m);
  return POINT{(i % per) * m.cell.cx, (i / per) * m.cell.cy};
}

struct Rects {
  RECT bounds, state, icon, label, select;
};

Rects ItemRects(ListView* lv, const Metrics& m, int i) {
  Rects r;
  int view = View(lv);
  if (view == LVS_REPORT) {
    int top = m.headerH + i * m.row - lv->origin.y;
    int c0 = ColumnOf(lv, 0);
    RECT col = c0 >= 0 ? ColumnRect(lv, c0) : RECT{-lv->origin.x, 0, -lv->origin.x, 0};
    r.bounds = RECT{-lv->origin.x, top, TotalWidth(lv) - lv->origin.x, top + m.row};
    int indent = Resolve(lv, i, 0, LVIF_INDENT).indent;
    int x = col.left + std::max(0, indent) * std::max(16, (int)m.small.cx);
    r.state = RECT{x, top, x + m.state.cx, top + m.row};
    r.icon = RECT{r.state.right, top, r.state.right + m.small.cx, top + m.row};
    r.label = RECT{r.icon.right, top, std::max(r.icon.right, (int)col.right), top + m.row};
    if (lv->ex & LVS_EX_FULLROWSELECT) {
      r.select = r.bounds;
      r.select.left = r.state.right;
    } else {
      r.select = r.icon;
      r.select.right = r.label.right;
    }
    return r;
  }
  POINT p = view == LVS_LIST ? POINT{(i / PerColumn(lv, m)) * m.cell.cx, (i % PerColumn(lv, m)) * m.row}
                             : CellOrigin(lv, m, i);
  p.x -= lv->origin.x;
  p.y -= lv->origin.y;
  if (view == LVS_ICON) {
    int iconX = p.x + (m.cell.cx - m.large.cx) / 2;
    r.icon = RECT{iconX, p.y + 2, iconX + m.large.cx, p.y + 2 + m.large.cy};
    r.state = RECT{r.icon.left - m.state.cx, r.icon.bottom - m.state.cy, r.icon.left, r.icon.bottom};
    // The label wraps under the icon, two lines unless it has the focus.
    std::wstring text = ItemText(lv, i, 0);
    HDC dc = GetDC(lv->hwnd);
    HGDIOBJ old = SelectObject(dc, FontOf(lv));
    RECT t = {0, 0, m.cell.cx - 4, 0};
    DrawTextW(dc, text.c_str(), (int)text.size(), &t, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(dc, old);
    ReleaseDC(lv->hwnd, dc);
    int w = std::min((int)t.right + 4, (int)m.cell.cx), h = t.bottom;
    if (!(StateRef(lv, i) & LVIS_FOCUSED) || !HasFocus(lv)) h = std::min(h, 2 * m.fh);
    int lx = p.x + (m.cell.cx - w) / 2;
    r.label = RECT{lx, r.icon.bottom + 2, lx + w, r.icon.bottom + 2 + h + 2};
    r.bounds = RECT{p.x, p.y, p.x + m.cell.cx, std::max(r.label.bottom, r.icon.bottom)};
    UnionRect(&r.select, &r.icon, &r.label);
    return r;
  }
  r.state = RECT{p.x, p.y, p.x + m.state.cx, p.y + m.row};
  r.icon = RECT{r.state.right, p.y, r.state.right + m.small.cx, p.y + m.row};
  int textW = TextWidth(lv, ItemText(lv, i, 0)) + 6;
  int maxRight = p.x + m.cell.cx - 2;
  r.label = RECT{r.icon.right, p.y, std::min((int)r.icon.right + textW, maxRight), p.y + m.row};
  if (r.label.right < r.label.left) r.label.right = r.label.left;
  r.bounds = RECT{p.x, p.y, std::max(r.label.right, r.icon.right), p.y + m.row};
  r.select = r.icon;
  r.select.right = r.label.right;
  return r;
}

// How big the whole view is, for the scroll bars.
SIZE ContentSize(ListView* lv, const Metrics& m) {
  int n = Count(lv);
  switch (View(lv)) {
    case LVS_REPORT: return SIZE{TotalWidth(lv), m.headerH + n * m.row};
    case LVS_LIST: {
      int per = PerColumn(lv, m);
      return SIZE{((n + per - 1) / per) * m.cell.cx, m.client.bottom};
    }
    default: {
      SIZE s = {0, 0};
      for (int i = 0; i < n; i++) {
        POINT p = CellOrigin(lv, m, i);
        s.cx = std::max(s.cx, p.x + m.cell.cx);
        s.cy = std::max(s.cy, p.y + m.cell.cy);
      }
      return s;
    }
  }
}

void PlaceHeader(ListView* lv) {
  if (!lv->header) return;
  bool show = View(lv) == LVS_REPORT && !(StyleOf(lv->hwnd) & LVS_NOCOLUMNHEADER);
  if (!show) {
    if (StyleOf(lv->header) & WS_VISIBLE) ShowWindow(lv->header, SW_HIDE);
    return;
  }
  RECT c;
  GetClientRect(lv->hwnd, &c);
  WINDOWPOS wp = {};
  HDLAYOUT layout = {&c, &wp};
  SendMessageW(lv->header, HDM_LAYOUT, 0, (LPARAM)&layout);
  int w = std::max((int)wp.cx + lv->origin.x, TotalWidth(lv) + 100);
  SetWindowPos(lv->header, NULL, -lv->origin.x, 0, w, wp.cy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void UpdateScroll(ListView* lv) {
  DWORD style = StyleOf(lv->hwnd);
  if (style & LVS_NOSCROLL) {
    ShowScrollBar(lv->hwnd, SB_BOTH, FALSE);
    return;
  }
  // Showing one bar can take the room that needs the other: twice is enough.
  for (int pass = 0; pass < 2; pass++) {
    Metrics m = GetMetrics(lv);
    SIZE content = ContentSize(lv, m);
    int view = View(lv);
    SCROLLINFO h = {sizeof h, SIF_ALL | SIF_DISABLENOSCROLL}, v = {sizeof v, SIF_ALL};
    h.fMask = v.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (view == LVS_REPORT) {
      int per = PerPage(lv, m);
      v.nMax = Count(lv) - 1;
      v.nPage = per;
      lv->origin.y = std::max(0, std::min(lv->origin.y / m.row, Count(lv) - per)) * m.row;
      v.nPos = lv->origin.y / m.row;
      h.nMax = content.cx - 1;
      h.nPage = m.client.right;
      lv->origin.x = std::max(0, std::min((int)lv->origin.x, (int)content.cx - (int)m.client.right));
      h.nPos = lv->origin.x;
    } else if (view == LVS_LIST) {
      int cols = content.cx / std::max(1, (int)m.cell.cx);
      int vis = std::max(1, (int)(m.client.right / m.cell.cx));
      h.nMax = cols - 1;
      h.nPage = vis;
      int first = std::max(0, std::min((int)(lv->origin.x / m.cell.cx), cols - vis));
      lv->origin.x = first * m.cell.cx;
      lv->origin.y = 0;
      h.nPos = first;
      v.nMax = 0;
      v.nPage = 1;
    } else {
      h.nMax = content.cx - 1;
      h.nPage = m.client.right;
      v.nMax = content.cy - 1;
      v.nPage = m.client.bottom;
      lv->origin.x = std::max(0, std::min((int)lv->origin.x, (int)content.cx - (int)m.client.right));
      lv->origin.y = std::max(0, std::min((int)lv->origin.y, (int)content.cy - (int)m.client.bottom));
      h.nPos = lv->origin.x;
      v.nPos = lv->origin.y;
    }
    SetScrollInfo(lv->hwnd, SB_HORZ, &h, TRUE);
    SetScrollInfo(lv->hwnd, SB_VERT, &v, TRUE);
  }
  PlaceHeader(lv);
}

void Relayout(ListView* lv) {
  UpdateScroll(lv);
  InvalidateRect(lv->hwnd, NULL, TRUE);
}

void InvalidateItem(ListView* lv, int i) {
  if (!Valid(lv, i)) return;
  Metrics m = GetMetrics(lv);
  Rects r = ItemRects(lv, m, i);
  RECT b;
  UnionRect(&b, &r.bounds, &r.select);
  InflateRect(&b, 1, 1);
  if (View(lv) == LVS_ICON) b.bottom += 4 * m.fh;  // a focused label grows
  InvalidateRect(lv->hwnd, &b, TRUE);
}

void ScrollTo(ListView* lv, int x, int y) {
  lv->origin.x = x;
  lv->origin.y = y;
  UpdateScroll(lv);
  InvalidateRect(lv->hwnd, NULL, TRUE);
}

bool EnsureVisible(ListView* lv, int i, bool partial) {
  if (!Valid(lv, i)) return false;
  Metrics m = GetMetrics(lv);
  int view = View(lv);
  if (view == LVS_REPORT) {
    int per = PerPage(lv, m), top = lv->origin.y / m.row;
    if (i < top)
      top = i;
    else if (i >= top + per && !(partial && i == top + per))
      top = i - per + 1;
    if (top * m.row != lv->origin.y) ScrollTo(lv, lv->origin.x, top * m.row);
    return true;
  }
  Rects r = ItemRects(lv, m, i);
  int x = lv->origin.x, y = lv->origin.y;
  if (r.bounds.left < 0)
    x += r.bounds.left;
  else if (r.bounds.right > m.client.right && !partial)
    x += std::min(r.bounds.right - m.client.right, r.bounds.left);
  if (view != LVS_LIST) {
    if (r.bounds.top < 0)
      y += r.bounds.top;
    else if (r.bounds.bottom > m.client.bottom && !partial)
      y += std::min(r.bounds.bottom - m.client.bottom, r.bounds.top);
  }
  if (x != lv->origin.x || y != lv->origin.y) ScrollTo(lv, x, y);
  return true;
}

// ---------------------------------------------------------------------
// Hit testing

int HitTest(ListView* lv, POINT pt, UINT* flags, int* sub, bool subItems) {
  Metrics m = GetMetrics(lv);
  *flags = 0;
  if (sub) *sub = 0;
  if (pt.x < 0) *flags |= LVHT_TOLEFT;
  if (pt.x >= m.client.right) *flags |= LVHT_TORIGHT;
  if (pt.y < 0) *flags |= LVHT_ABOVE;
  if (pt.y >= m.client.bottom) *flags |= LVHT_BELOW;
  if (*flags) return -1;
  int n = Count(lv);
  if (View(lv) == LVS_REPORT) {
    if (pt.y < m.headerH) {
      *flags = LVHT_NOWHERE;
      return -1;
    }
    int i = (pt.y - m.headerH + lv->origin.y) / m.row;
    if (i < 0 || i >= n) {
      *flags = LVHT_NOWHERE;
      return -1;
    }
    Rects r = ItemRects(lv, m, i);
    int col = -1;
    for (int c = 0, nc = ColumnCount(lv); c < nc; c++) {
      RECT cr = ColumnRect(lv, c);
      if (pt.x >= cr.left && pt.x < cr.right) col = c;
    }
    if (sub) *sub = col >= 0 && col < (int)lv->cols.size() ? lv->cols[col].sub : 0;
    if (PtInRect(&r.state, pt))
      *flags = LVHT_ONITEMSTATEICON;
    else if (PtInRect(&r.icon, pt))
      *flags = LVHT_ONITEMICON;
    else if (PtInRect(&r.label, pt))
      *flags = LVHT_ONITEMLABEL;
    else if (col >= 0 && (subItems || (lv->ex & LVS_EX_FULLROWSELECT)))
      *flags = LVHT_ONITEMLABEL;
    else {
      *flags = LVHT_NOWHERE;
      return -1;
    }
    return i;
  }
  // The topmost item wins where they overlap: the last drawn.
  for (int i = n - 1; i >= 0; i--) {
    Rects r = ItemRects(lv, m, i);
    if (PtInRect(&r.state, pt)) {
      *flags = LVHT_ONITEMSTATEICON;
      return i;
    }
    if (PtInRect(&r.icon, pt)) {
      *flags = LVHT_ONITEMICON;
      return i;
    }
    if (PtInRect(&r.label, pt)) {
      *flags = LVHT_ONITEMLABEL;
      return i;
    }
  }
  *flags = LVHT_NOWHERE;
  return -1;
}

// ---------------------------------------------------------------------
// State and selection

bool SetState(ListView* lv, int i, UINT state, UINT mask) {
  if (!Valid(lv, i)) return false;
  UINT& ref = StateRef(lv, i);
  UINT old = ref, now = (old & ~mask) | (state & mask);
  if (now == old) return true;
  if (NotifyItem(lv, LVN_ITEMCHANGING, i, 0, now, old, LVIF_STATE) && !lv->ownerData) return false;
  // Only one item has the focus.
  if ((now & LVIS_FOCUSED) && !(old & LVIS_FOCUSED)) {
    if (lv->focus != i && Valid(lv, lv->focus)) SetState(lv, lv->focus, 0, LVIS_FOCUSED);
    lv->focus = i;
  } else if (!(now & LVIS_FOCUSED) && (old & LVIS_FOCUSED) && lv->focus == i) {
    lv->focus = -1;
  }
  if ((now & LVIS_SELECTED) && !(old & LVIS_SELECTED) && (StyleOf(lv->hwnd) & LVS_SINGLESEL)) {
    for (int j = 0, n = Count(lv); j < n; j++)
      if (j != i && (StateRef(lv, j) & LVIS_SELECTED)) SetState(lv, j, 0, LVIS_SELECTED);
  }
  StateRef(lv, i) = now;
  InvalidateItem(lv, i);
  NotifyItem(lv, LVN_ITEMCHANGED, i, 0, now, old, LVIF_STATE);
  return true;
}

void SelectOnly(ListView* lv, int keep) {
  for (int j = 0, n = Count(lv); j < n; j++)
    if (j != keep && (StateRef(lv, j) & LVIS_SELECTED)) SetState(lv, j, 0, LVIS_SELECTED);
  if (Valid(lv, keep)) SetState(lv, keep, LVIS_SELECTED, LVIS_SELECTED);
}

void SelectRange(ListView* lv, int a, int b, bool keepOthers) {
  if (a > b) std::swap(a, b);
  for (int j = 0, n = Count(lv); j < n; j++) {
    bool in = j >= a && j <= b;
    if (in)
      SetState(lv, j, LVIS_SELECTED, LVIS_SELECTED);
    else if (!keepOthers && (StateRef(lv, j) & LVIS_SELECTED))
      SetState(lv, j, 0, LVIS_SELECTED);
  }
}

int SelectedCount(ListView* lv) {
  int c = 0;
  for (int j = 0, n = Count(lv); j < n; j++)
    if (StateRef(lv, j) & LVIS_SELECTED) c++;
  return c;
}

bool IsChecked(ListView* lv, int i) { return ((StateRef(lv, i) & LVIS_STATEIMAGEMASK) >> 12) == 2; }

void ToggleCheck(ListView* lv, int i) {
  SetState(lv, i, INDEXTOSTATEIMAGEMASK(IsChecked(lv, i) ? 1 : 2), LVIS_STATEIMAGEMASK);
}

// The item next to another in a direction, for the keyboard and LVNI_.
int Neighbor(ListView* lv, int i, UINT dir) {
  int n = Count(lv);
  if (!Valid(lv, i)) return -1;
  Metrics m = GetMetrics(lv);
  int view = View(lv);
  if (view == LVS_REPORT) {
    if (dir == LVNI_ABOVE) return i > 0 ? i - 1 : -1;
    if (dir == LVNI_BELOW) return i + 1 < n ? i + 1 : -1;
    return -1;
  }
  if (view == LVS_LIST) {
    int per = PerColumn(lv, m);
    if (dir == LVNI_ABOVE) return i > 0 ? i - 1 : -1;
    if (dir == LVNI_BELOW) return i + 1 < n ? i + 1 : -1;
    if (dir == LVNI_TOLEFT) return i - per >= 0 ? i - per : -1;
    if (dir == LVNI_TORIGHT) return i + per < n ? i + per : (i / per < (n - 1) / per ? n - 1 : -1);
    return -1;
  }
  // The icon views: the closest item that way, keeping to the same line.
  Rects from = ItemRects(lv, m, i);
  POINT c = {(from.bounds.left + from.bounds.right) / 2, (from.bounds.top + from.bounds.bottom) / 2};
  int best = -1;
  long long bestScore = 0;
  for (int j = 0; j < n; j++) {
    if (j == i) continue;
    Rects r = ItemRects(lv, m, j);
    POINT d = {(r.bounds.left + r.bounds.right) / 2 - c.x, (r.bounds.top + r.bounds.bottom) / 2 - c.y};
    long long along, across;
    switch (dir) {
      case LVNI_ABOVE: along = -d.y, across = d.x; break;
      case LVNI_BELOW: along = d.y, across = d.x; break;
      case LVNI_TOLEFT: along = -d.x, across = d.y; break;
      default: along = d.x, across = d.y; break;
    }
    if (along <= 0) continue;
    long long score = along + 4 * (across < 0 ? -across : across);
    if (best < 0 || score < bestScore) best = j, bestScore = score;
  }
  return best;
}

int NextItem(ListView* lv, int start, UINT flags) {
  UINT want = flags & (LVNI_FOCUSED | LVNI_SELECTED | LVNI_CUT | LVNI_DROPHILITED);
  UINT dir = flags & (LVNI_ABOVE | LVNI_BELOW | LVNI_TOLEFT | LVNI_TORIGHT);
  auto matches = [&](int j) { return (StateRef(lv, j) & want) == want; };
  if (dir) {
    for (int j = Neighbor(lv, start, dir); j >= 0; j = Neighbor(lv, j, dir))
      if (matches(j)) return j;
    return -1;
  }
  if (want == LVNI_FOCUSED && Valid(lv, lv->focus) && lv->focus > start) return lv->focus;
  for (int j = std::max(0, start + 1), n = Count(lv); j < n; j++)
    if (matches(j)) return j;
  return -1;
}

// ---------------------------------------------------------------------
// Items

void ShiftIndices(ListView* lv, int from, int by) {
  for (int* p : {&lv->focus, &lv->mark, &lv->hot, &lv->editItem, &lv->pendingEdit, &lv->pressItem}) {
    if (*p < from) continue;
    if (by < 0 && *p == from)
      *p = -1;
    else
      *p += by;
  }
}

int InsertItem(ListView* lv, const LVITEMW* in, bool u) {
  if (lv->ownerData || !in) return -1;
  Item it;
  if (in->mask & LVIF_TEXT) {
    it.callback = IsCallback(in->pszText, u);
    it.text = TextIn(in->pszText, u);
  }
  if (in->mask & LVIF_IMAGE) it.image = in->iImage;
  if (in->mask & LVIF_PARAM) it.param = in->lParam;
  if (in->mask & LVIF_INDENT) it.indent = in->iIndent;
  UINT state = (in->mask & LVIF_STATE) ? (in->state & in->stateMask) : 0;
  if ((lv->ex & LVS_EX_CHECKBOXES) && !(state & LVIS_STATEIMAGEMASK)) state |= INDEXTOSTATEIMAGEMASK(1);
  it.state = state & ~LVIS_FOCUSED;
  int n = (int)lv->items.size();
  int at = std::max(0, std::min(in->iItem, n));
  DWORD style = StyleOf(lv->hwnd);
  if ((style & (LVS_SORTASCENDING | LVS_SORTDESCENDING)) && !it.callback) {
    bool down = (style & LVS_SORTDESCENDING) != 0;
    at = 0;
    while (at < n) {
      int c = _wcsicmp(it.text.c_str(), lv->items[at].text.c_str());
      if (down ? c > 0 : c < 0) break;
      at++;
    }
  }
  lv->items.insert(lv->items.begin() + at, it);
  ShiftIndices(lv, at, 1);
  if (state & LVIS_FOCUSED) {
    if (Valid(lv, lv->focus)) StateRef(lv, lv->focus) &= ~LVIS_FOCUSED;
    lv->items[at].state |= LVIS_FOCUSED;
    lv->focus = at;
  }
  if ((state & LVIS_SELECTED) && (style & LVS_SINGLESEL)) {
    for (int j = 0; j <= n; j++)
      if (j != at) lv->items[j].state &= ~LVIS_SELECTED;
  }
  NotifyItem(lv, LVN_INSERTITEM, at);
  if (IsWindow(lv->hwnd)) Relayout(lv);
  return at;
}

void EndEdit(ListView* lv, bool save);

bool DeleteItem(ListView* lv, int i) {
  if (!Valid(lv, i) || lv->ownerData) return false;
  if (lv->editItem == i) EndEdit(lv, false);
  NotifyItem(lv, LVN_DELETEITEM, i);
  if (!Valid(lv, i)) return false;
  lv->items.erase(lv->items.begin() + i);
  ShiftIndices(lv, i, -1);
  Relayout(lv);
  return true;
}

bool DeleteAll(ListView* lv) {
  if (lv->editItem >= 0) EndEdit(lv, false);
  if (lv->ownerData) {
    lv->odState.clear();
  } else {
    bool quiet = NotifyItem(lv, LVN_DELETEALLITEMS, -1) != 0;
    if (!quiet)
      for (int i = 0; i < (int)lv->items.size(); i++) NotifyItem(lv, LVN_DELETEITEM, i);
    lv->items.clear();
  }
  lv->focus = lv->mark = lv->hot = lv->pendingEdit = lv->pressItem = -1;
  lv->origin = POINT{0, 0};
  if (IsWindow(lv->hwnd)) Relayout(lv);
  return true;
}

bool SetItem(ListView* lv, const LVITEMW* in, bool u) {
  if (!in || !Valid(lv, in->iItem)) return false;
  int i = in->iItem;
  if (in->iSubItem < 0) return false;
  if (lv->ownerData) {
    if (in->mask & LVIF_STATE) return SetState(lv, i, in->state, in->stateMask);
    return in->iSubItem == 0;
  }
  Item& it = lv->items[i];
  if (in->iSubItem > 0) {
    if (!lv->cols.empty() && in->iSubItem >= (int)lv->cols.size()) return false;
    if ((int)it.subs.size() < in->iSubItem) it.subs.resize(in->iSubItem);
    SubItem& si = it.subs[in->iSubItem - 1];
    if (in->mask & LVIF_TEXT) {
      si.callback = IsCallback(in->pszText, u);
      si.text = TextIn(in->pszText, u);
    }
    if (in->mask & LVIF_IMAGE) si.image = in->iImage;
    InvalidateItem(lv, i);
    return true;
  }
  UINT changed = in->mask & (LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM | LVIF_INDENT);
  if (changed) {
    if (NotifyItem(lv, LVN_ITEMCHANGING, i, 0, 0, 0, changed)) return false;
    InvalidateItem(lv, i);
    if (in->mask & LVIF_TEXT) {
      it.callback = IsCallback(in->pszText, u);
      it.text = TextIn(in->pszText, u);
    }
    if (in->mask & LVIF_IMAGE) it.image = in->iImage;
    if (in->mask & LVIF_PARAM) it.param = in->lParam;
    if (in->mask & LVIF_INDENT) it.indent = in->iIndent;
    InvalidateItem(lv, i);
    NotifyItem(lv, LVN_ITEMCHANGED, i, 0, 0, 0, changed);
    if (in->mask & LVIF_TEXT) UpdateScroll(lv);
  }
  if (in->mask & LVIF_STATE) return SetState(lv, i, in->state, in->stateMask);
  return true;
}

bool GetItem(ListView* lv, LVITEMW* out, bool u, bool textOnly = false) {
  if (!out || !Valid(lv, out->iItem)) return false;
  UINT mask = textOnly ? LVIF_TEXT : out->mask;
  Shown s = Resolve(lv, out->iItem, out->iSubItem, mask);
  if (mask & LVIF_TEXT) CopyOut(s.text, out->pszText, out->cchTextMax, u);
  if (textOnly) return true;
  if (mask & LVIF_IMAGE) out->iImage = s.image;
  if (mask & LVIF_PARAM) out->lParam = lv->ownerData ? s.param : lv->items[out->iItem].param;
  if (mask & LVIF_INDENT) out->iIndent = s.indent;
  if (mask & LVIF_STATE) out->state = s.state & out->stateMask;
  return true;
}

int FindItem(ListView* lv, int start, const LVFINDINFOW* fi, bool u) {
  if (!fi) return -1;
  int n = Count(lv);
  if (lv->ownerData) {
    NMLVFINDITEMW nm = {};
    nm.iStart = start + 1;
    nm.lvfi = *fi;
    return (int)Notify(lv, u ? LVN_ODFINDITEMW : LVN_ODFINDITEMA, &nm.hdr);
  }
  if (fi->flags & LVFI_NEARESTXY) {
    Metrics m = GetMetrics(lv);
    int best = -1;
    long long bestD = 0;
    for (int j = 0; j < n; j++) {
      Rects r = ItemRects(lv, m, j);
      long long dx = r.bounds.left - fi->pt.x, dy = r.bounds.top - fi->pt.y, d = dx * dx + dy * dy;
      if (best < 0 || d < bestD) best = j, bestD = d;
    }
    return best;
  }
  std::wstring want = (fi->flags & (LVFI_STRING | LVFI_PARTIAL)) ? TextIn(fi->psz, u) : L"";
  int first = start + 1;
  for (int k = 0; k < n; k++) {
    int j = first + k;
    if (j >= n) {
      if (!(fi->flags & LVFI_WRAP)) break;
      j -= n;
    }
    if (fi->flags & LVFI_PARAM) {
      if (lv->items[j].param == fi->lParam) return j;
      continue;
    }
    std::wstring t = ItemText(lv, j, 0);
    if (fi->flags & LVFI_PARTIAL) {
      if (t.size() >= want.size() && _wcsnicmp(t.c_str(), want.c_str(), want.size()) == 0) return j;
    } else if (fi->flags & LVFI_STRING) {
      if (_wcsicmp(t.c_str(), want.c_str()) == 0) return j;
    }
  }
  return -1;
}

// Sorting keeps the focus and the selection mark on their items.
void AfterSort(ListView* lv) {
  lv->focus = -1;
  for (int j = 0, n = Count(lv); j < n; j++)
    if (StateRef(lv, j) & LVIS_FOCUSED) lv->focus = j;
  lv->mark = lv->focus;
  for (Item& it : lv->items) it.placed = false;
  Relayout(lv);
}

bool SortItems(ListView* lv, PFNLVCOMPARE cmp, LPARAM data, bool byIndex) {
  if (!cmp || lv->ownerData) return false;
  if (lv->editItem >= 0) EndEdit(lv, false);
  int n = (int)lv->items.size();
  std::vector<int> order(n);
  for (int j = 0; j < n; j++) order[j] = j;
  // The callback may read the items by index, so they stay put until
  // the order is known.
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    if (byIndex) return cmp((LPARAM)a, (LPARAM)b, data) < 0;
    return cmp(lv->items[a].param, lv->items[b].param, data) < 0;
  });
  std::vector<Item> sorted;
  sorted.reserve(n);
  for (int j : order) sorted.push_back(std::move(lv->items[j]));
  lv->items.swap(sorted);
  AfterSort(lv);
  return true;
}

// ---------------------------------------------------------------------
// Columns

bool ColumnToHeader(const LVCOLUMNW* in, HDITEMW* hi, bool u, std::string* narrow) {
  hi->mask = 0;
  if (in->mask & LVCF_WIDTH) {
    hi->mask |= HDI_WIDTH;
    hi->cxy = in->cx;
  }
  if (in->mask & LVCF_TEXT) {
    hi->mask |= HDI_TEXT;
    if (u) {
      hi->pszText = in->pszText;
    } else {
      *narrow = in->pszText ? (const char*)in->pszText : "";
      hi->pszText = (LPWSTR)narrow->c_str();
    }
  }
  if (in->mask & LVCF_FMT) {
    hi->mask |= HDI_FORMAT;
    hi->fmt = (in->fmt & (LVCFMT_JUSTIFYMASK | LVCFMT_IMAGE | LVCFMT_BITMAP_ON_RIGHT)) | HDF_STRING;
  }
  if (in->mask & LVCF_IMAGE) {
    hi->mask |= HDI_IMAGE;
    hi->iImage = in->iImage;
  }
  if (in->mask & LVCF_ORDER) {
    hi->mask |= HDI_ORDER;
    hi->iOrder = in->iOrder;
  }
  return true;
}

int InsertColumn(ListView* lv, int at, const LVCOLUMNW* in, bool u) {
  if (!in || !lv->header) return -1;
  HDITEMW hi = {};
  std::string narrow;
  ColumnToHeader(in, &hi, u, &narrow);
  if (!(hi.mask & HDI_FORMAT)) {
    hi.mask |= HDI_FORMAT;
    hi.fmt = HDF_LEFT | HDF_STRING;
  }
  if (!(hi.mask & HDI_WIDTH)) {
    hi.mask |= HDI_WIDTH;
    hi.cxy = 0;
  }
  int n = ColumnCount(lv);
  at = std::max(0, std::min(at, n));
  int got = (int)SendMessageW(lv->header, u ? HDM_INSERTITEMW : HDM_INSERTITEMA, at, (LPARAM)&hi);
  if (got < 0) return -1;
  Column c;
  c.sub = (in->mask & LVCF_SUBITEM) ? in->iSubItem : got;
  c.fmt = (in->mask & LVCF_FMT) ? in->fmt : LVCFMT_LEFT;
  lv->cols.insert(lv->cols.begin() + got, c);
  Relayout(lv);
  return got;
}

bool SetColumn(ListView* lv, int c, const LVCOLUMNW* in, bool u) {
  if (!in || c < 0 || c >= ColumnCount(lv)) return false;
  HDITEMW hi = {};
  std::string narrow;
  ColumnToHeader(in, &hi, u, &narrow);
  if (in->mask & LVCF_SUBITEM) lv->cols[c].sub = in->iSubItem;
  if (in->mask & LVCF_FMT) lv->cols[c].fmt = in->fmt;
  BOOL ok = hi.mask ? (BOOL)SendMessageW(lv->header, u ? HDM_SETITEMW : HDM_SETITEMA, c, (LPARAM)&hi) : TRUE;
  Relayout(lv);
  return ok != FALSE;
}

bool GetColumn(ListView* lv, int c, LVCOLUMNW* out, bool u) {
  if (!out || c < 0 || c >= ColumnCount(lv)) return false;
  HDITEMW hi = {};
  if (out->mask & LVCF_WIDTH) hi.mask |= HDI_WIDTH;
  if (out->mask & LVCF_TEXT) {
    hi.mask |= HDI_TEXT;
    hi.pszText = out->pszText;
    hi.cchTextMax = out->cchTextMax;
  }
  if (out->mask & LVCF_IMAGE) hi.mask |= HDI_IMAGE;
  if (out->mask & LVCF_ORDER) hi.mask |= HDI_ORDER;
  SendMessageW(lv->header, u ? HDM_GETITEMW : HDM_GETITEMA, c, (LPARAM)&hi);
  if (out->mask & LVCF_WIDTH) out->cx = hi.cxy;
  if (out->mask & LVCF_IMAGE) out->iImage = hi.iImage;
  if (out->mask & LVCF_ORDER) out->iOrder = hi.iOrder;
  if (out->mask & LVCF_FMT) out->fmt = lv->cols[c].fmt;
  if (out->mask & LVCF_SUBITEM) out->iSubItem = lv->cols[c].sub;
  return true;
}

bool DeleteColumn(ListView* lv, int c) {
  if (c < 0 || c >= ColumnCount(lv)) return false;
  int sub = lv->cols[c].sub;
  SendMessageW(lv->header, HDM_DELETEITEM, c, 0);
  lv->cols.erase(lv->cols.begin() + c);
  // The subitem's data goes with its column, and later ones move down.
  if (sub > 0) {
    for (Item& it : lv->items)
      if (sub - 1 < (int)it.subs.size()) it.subs.erase(it.subs.begin() + (sub - 1));
    for (Column& col : lv->cols)
      if (col.sub > sub) col.sub--;
  }
  Relayout(lv);
  return true;
}

bool SetColumnWidth(ListView* lv, int c, int cx) {
  if (View(lv) == LVS_LIST) {
    lv->listColW = cx > 0 ? cx : 0;
    Relayout(lv);
    return true;
  }
  if (View(lv) != LVS_REPORT || c < 0 || c >= ColumnCount(lv)) return false;
  if (cx == LVSCW_AUTOSIZE || cx == LVSCW_AUTOSIZE_USEHEADER) {
    int sub = lv->cols[c].sub, w = 0;
    Metrics m = GetMetrics(lv);
    for (int i = 0, n = std::min(Count(lv), 5000); i < n; i++) w = std::max(w, TextWidth(lv, ItemText(lv, i, sub)));
    w += 2 * kTextPad;
    if (sub == 0) w += m.state.cx + m.small.cx;
    if (cx == LVSCW_AUTOSIZE_USEHEADER) {
      wchar_t buf[260] = L"";
      HDITEMW hi = {};
      hi.mask = HDI_TEXT;
      hi.pszText = buf;
      hi.cchTextMax = 260;
      SendMessageW(lv->header, HDM_GETITEMW, c, (LPARAM)&hi);
      w = std::max(w, TextWidth(lv, buf) + 4 * kTextPad);
      // The last column fills the rest of the window.
      if (c == ColumnCount(lv) - 1) {
        RECT cr = ColumnRect(lv, c);
        w = std::max(w, (int)(m.client.right - cr.left));
      }
    }
    cx = w;
  }
  HDITEMW hi = {};
  hi.mask = HDI_WIDTH;
  hi.cxy = std::max(0, cx);
  SendMessageW(lv->header, HDM_SETITEMW, c, (LPARAM)&hi);
  Relayout(lv);
  return true;
}

// ---------------------------------------------------------------------
// Label editing

LRESULT CALLBACK EditSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
  ListView* lv = (ListView*)data;
  switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_HASSETSEL | DLGC_WANTCHARS;
    case WM_KEYDOWN:
      if (wp == VK_RETURN) {
        EndEdit(lv, true);
        return 0;
      }
      if (wp == VK_ESCAPE) {
        EndEdit(lv, false);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wp == '\r' || wp == 0x1B) return 0;
      break;
    case WM_KILLFOCUS:
      DefSubclassProc(h, msg, wp, lp);
      if (lv->edit == h) EndEdit(lv, true);
      return 0;
  }
  return DefSubclassProc(h, msg, wp, lp);
}

HWND BeginEdit(ListView* lv, int i) {
  if (lv->edit) EndEdit(lv, true);
  if (!Valid(lv, i)) return NULL;
  EnsureVisible(lv, i, false);
  Metrics m = GetMetrics(lv);
  Rects r = ItemRects(lv, m, i);
  std::wstring text = ItemText(lv, i, 0);
  RECT er = r.label;
  if (View(lv) == LVS_ICON) {
    er.bottom = er.top + m.fh + 6;
  } else {
    er.right = std::max((int)er.right, (int)er.left + TextWidth(lv, text) + 16);
    if (View(lv) == LVS_REPORT) {
      int c0 = ColumnOf(lv, 0);
      if (c0 >= 0) er.right = std::max(er.right, ColumnRect(lv, c0).right);
    }
  }
  DWORD style = WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | (View(lv) == LVS_ICON ? ES_CENTER : ES_LEFT);
  HWND e = CreateWindowExW(0, L"Edit", text.c_str(), style, er.left, er.top, er.right - er.left, er.bottom - er.top,
                           lv->hwnd, (HMENU)1, (HINSTANCE)GetWindowLongPtrW(lv->hwnd, GWLP_HINSTANCE), NULL);
  if (!e) return NULL;
  SendMessageW(e, WM_SETFONT, (WPARAM)FontOf(lv), FALSE);
  lv->edit = e;
  lv->editItem = i;
  // The parent sees the edit control and can turn the edit down.
  NMLVDISPINFOW di = {};
  Shown s = Resolve(lv, i, 0, LVIF_IMAGE | LVIF_STATE | LVIF_PARAM);
  std::string narrow = Narrow(text.c_str());
  di.item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE | LVIF_PARAM;
  di.item.iItem = i;
  di.item.pszText = lv->unicodeNotify ? (LPWSTR)text.c_str() : (LPWSTR)narrow.c_str();
  di.item.iImage = s.image;
  di.item.state = s.state;
  di.item.lParam = lv->ownerData ? s.param : lv->items[i].param;
  if (Notify(lv, lv->unicodeNotify ? LVN_BEGINLABELEDITW : LVN_BEGINLABELEDITA, &di.hdr)) {
    lv->edit = NULL;
    lv->editItem = -1;
    DestroyWindow(e);
    return NULL;
  }
  if (!IsWindow(e)) return NULL;
  SetWindowSubclass(e, EditSubclass, kEditSubclass, (DWORD_PTR)lv);
  ShowWindow(e, SW_SHOW);
  SetFocus(e);
  SendMessageW(e, EM_SETSEL, 0, -1);
  return e;
}

void EndEdit(ListView* lv, bool save) {
  if (!lv->edit || lv->ending) return;
  lv->ending = true;
  HWND e = lv->edit;
  int i = lv->editItem;
  int len = GetWindowTextLengthW(e);
  std::wstring text(len + 1, L'\0');
  GetWindowTextW(e, &text[0], len + 1);
  text.resize(len);
  lv->edit = NULL;
  lv->editItem = -1;
  bool hadFocus = GetFocus() == e;
  RemoveWindowSubclass(e, EditSubclass, kEditSubclass);
  ShowWindow(e, SW_HIDE);
  if (hadFocus) SetFocus(lv->hwnd);
  if (Valid(lv, i)) {
    NMLVDISPINFOW di = {};
    std::string narrow = Narrow(text.c_str());
    di.item.mask = LVIF_TEXT | LVIF_PARAM;
    di.item.iItem = i;
    di.item.pszText = save ? (lv->unicodeNotify ? (LPWSTR)text.c_str() : (LPWSTR)narrow.c_str()) : NULL;
    di.item.cchTextMax = (int)text.size() + 1;
    di.item.lParam = lv->ownerData ? 0 : lv->items[i].param;
    LRESULT keep = Notify(lv, lv->unicodeNotify ? LVN_ENDLABELEDITW : LVN_ENDLABELEDITA, &di.hdr);
    if (save && keep && Valid(lv, i) && !lv->ownerData && !lv->items[i].callback) {
      LVITEMW set = {};
      set.mask = LVIF_TEXT;
      set.iItem = i;
      set.pszText = (LPWSTR)text.c_str();
      SetItem(lv, &set, true);
    }
    InvalidateItem(lv, i);
  }
  DestroyWindow(e);
  lv->ending = false;
}

// ---------------------------------------------------------------------
// Painting

UINT DrawStage(ListView* lv, HDC dc, DWORD stage, int item, int sub, const RECT& rc, UINT itemState,
               COLORREF* text, COLORREF* bk) {
  NMLVCUSTOMDRAW cd = {};
  cd.nmcd.dwDrawStage = stage;
  cd.nmcd.hdc = dc;
  cd.nmcd.rc = rc;
  cd.nmcd.dwItemSpec = (DWORD_PTR)item;
  cd.nmcd.uItemState = itemState;
  if (Valid(lv, item) && !lv->ownerData) cd.nmcd.lItemlParam = lv->items[item].param;
  cd.clrText = text ? *text : 0;
  cd.clrTextBk = bk ? *bk : 0;
  cd.iSubItem = sub;
  UINT r = (UINT)Notify(lv, NM_CUSTOMDRAW, &cd.nmcd.hdr);
  if (text) *text = cd.clrText;
  if (bk) *bk = cd.clrTextBk;
  return r;
}

void DrawCheck(HDC dc, const RECT& r, bool checked) {
  RECT b = {r.left + (r.right - r.left - 13) / 2, r.top + (r.bottom - r.top - 13) / 2, 0, 0};
  b.right = b.left + 13;
  b.bottom = b.top + 13;
  DrawFrameControl(dc, &b, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));
}

void DrawStateIcon(ListView* lv, HDC dc, const RECT& r, UINT state) {
  if (r.right <= r.left) return;
  int index = (int)((state & LVIS_STATEIMAGEMASK) >> 12);
  if (!index) return;
  if (lv->images[LVSIL_STATE]) {
    int cx, cy;
    ImageList_GetIconSize(lv->images[LVSIL_STATE], &cx, &cy);
    ImageList_Draw(lv->images[LVSIL_STATE], index - 1, dc, r.left, r.top + (r.bottom - r.top - cy) / 2,
                   ILD_TRANSPARENT);
  } else if (lv->ex & LVS_EX_CHECKBOXES) {
    DrawCheck(dc, r, index == 2);
  }
}

void DrawIcon(ListView* lv, HDC dc, HIMAGELIST il, int image, const RECT& r, UINT state, bool blend) {
  if (!il || image < 0 || r.right <= r.left) return;
  int cx, cy;
  ImageList_GetIconSize(il, &cx, &cy);
  UINT flags = ILD_TRANSPARENT | (state & LVIS_OVERLAYMASK);
  if (blend || (state & LVIS_CUT)) flags |= ILD_BLEND50;
  ImageList_Draw(il, image, dc, r.left + (r.right - r.left - cx) / 2, r.top + (r.bottom - r.top - cy) / 2, flags);
}

void DrawLabel(HDC dc, const std::wstring& s, RECT r, UINT format, COLORREF color) {
  SetTextColor(dc, color);
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, format | DT_NOPREFIX);
}

void PaintItem(ListView* lv, HDC dc, const Metrics& m, int i, bool focused, UINT cdFlags) {
  Rects r = ItemRects(lv, m, i);
  int view = View(lv);
  Shown s = Resolve(lv, i, 0, LVIF_TEXT | LVIF_IMAGE | LVIF_STATE);
  bool selected = (s.state & LVIS_SELECTED) != 0;
  bool shown = (selected && (focused || (StyleOf(lv->hwnd) & LVS_SHOWSELALWAYS))) || (s.state & LVIS_DROPHILITED);
  bool active = focused || (s.state & LVIS_DROPHILITED);
  COLORREF text = TextColor(lv), bk = lv->textBk;
  if (shown) {
    text = GetSysColor(active ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    bk = GetSysColor(active ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
  }
  UINT itemState = (selected ? CDIS_SELECTED : 0) | ((s.state & LVIS_FOCUSED) && focused ? CDIS_FOCUS : 0);
  UINT itemCd = CDRF_DODEFAULT;
  // The parent may change the colors, or select another font (CDRF_NEWFONT).
  if (cdFlags & CDRF_NOTIFYITEMDRAW) {
    itemCd = DrawStage(lv, dc, CDDS_ITEMPREPAINT, i, 0, r.bounds, itemState, &text, &bk);
    if (itemCd & CDRF_SKIPDEFAULT) return;
  }
  // Subitems outside a full-row selection keep the plain colors.
  COLORREF plainText = shown ? TextColor(lv) : text, plainBk = shown ? lv->textBk : bk;
  bool blend = shown && active && view != LVS_REPORT;

  // The owner draws the whole row.
  if (view == LVS_REPORT && (StyleOf(lv->hwnd) & LVS_OWNERDRAWFIXED)) {
    DRAWITEMSTRUCT di = {};
    di.CtlType = ODT_LISTVIEW;
    di.CtlID = (UINT)GetDlgCtrlID(lv->hwnd);
    di.itemID = (UINT)i;
    di.itemAction = ODA_DRAWENTIRE;
    di.itemState = (selected ? ODS_SELECTED : 0) | ((s.state & LVIS_FOCUSED) && focused ? ODS_FOCUS : 0);
    di.hwndItem = lv->hwnd;
    di.hDC = dc;
    di.rcItem = r.bounds;
    di.itemData = lv->ownerData ? 0 : (ULONG_PTR)lv->items[i].param;
    SendMessageW(GetParent(lv->hwnd), WM_DRAWITEM, di.CtlID, (LPARAM)&di);
    return;
  }

  if (view == LVS_REPORT) {
    bool full = (lv->ex & LVS_EX_FULLROWSELECT) != 0;
    if (shown && full) FillSolid(dc, r.select, bk);
    for (int c = 0, nc = ColumnCount(lv); c < nc; c++) {
      RECT cr = ColumnRect(lv, c);
      cr.top = r.bounds.top;
      cr.bottom = r.bounds.bottom;
      if (cr.right <= cr.left) continue;
      int sub = c < (int)lv->cols.size() ? lv->cols[c].sub : c;
      bool lit = shown && (full || sub == 0);
      COLORREF st = lit ? text : plainText, sb = lit ? bk : plainBk;
      if ((itemCd & CDRF_NOTIFYSUBITEMDRAW) && sub > 0) {
        UINT sc = DrawStage(lv, dc, CDDS_ITEMPREPAINT | CDDS_SUBITEM, i, sub, cr, itemState, &st, &sb);
        if (sc & CDRF_SKIPDEFAULT) continue;
      }
      if (sub == 0) {
        HRGN clip = CreateRectRgnIndirect(&cr);
        SelectClipRgn(dc, clip);
        DrawStateIcon(lv, dc, r.state, s.state);
        DrawIcon(lv, dc, lv->images[LVSIL_SMALL], s.image, r.icon, s.state, shown && active);
        RECT lab = r.label;
        if (shown && !full) {
          RECT hl = lab;
          hl.right = std::min((int)lab.right, (int)lab.left + TextWidth(lv, s.text) + 2 * kTextPad);
          FillSolid(dc, hl, bk);
        } else if (!shown && sb != CLR_DEFAULT && sb != CLR_NONE) {
          FillSolid(dc, lab, sb);
        }
        lab.left += kTextPad / 2 + (m.small.cx ? 2 : 0);
        lab.right -= kTextPad / 2;
        DrawLabel(dc, s.text, lab, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT, st);
        SelectClipRgn(dc, NULL);
        DeleteObject(clip);
        continue;
      }
      Shown ss = Resolve(lv, i, sub, LVIF_TEXT | LVIF_IMAGE);
      RECT tr = cr;
      if (!(shown && full) && sb != CLR_DEFAULT && sb != CLR_NONE) FillSolid(dc, cr, sb);
      if ((lv->ex & LVS_EX_SUBITEMIMAGES) && ss.image >= 0 && lv->images[LVSIL_SMALL]) {
        RECT ir = {tr.left + 2, tr.top, tr.left + 2 + m.small.cx, tr.bottom};
        DrawIcon(lv, dc, lv->images[LVSIL_SMALL], ss.image, ir, 0, false);
        tr.left = ir.right;
      }
      tr.left += kTextPad;
      tr.right -= kTextPad;
      int fmt = c < (int)lv->cols.size() ? lv->cols[c].fmt & LVCFMT_JUSTIFYMASK : LVCFMT_LEFT;
      UINT align = fmt == LVCFMT_RIGHT ? DT_RIGHT : fmt == LVCFMT_CENTER ? DT_CENTER : DT_LEFT;
      DrawLabel(dc, ss.text, tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | align, st);
    }
    if ((s.state & LVIS_FOCUSED) && focused) {
      RECT fr = full ? r.select : r.label;
      if (!full) fr.right = std::min((int)fr.right, (int)fr.left + TextWidth(lv, s.text) + 2 * kTextPad);
      DrawFocusRect(dc, &fr);
    }
  } else if (view == LVS_ICON) {
    DrawStateIcon(lv, dc, r.state, s.state);
    DrawIcon(lv, dc, lv->images[LVSIL_NORMAL], s.image, r.icon, s.state, blend);
    if (shown || (bk != CLR_DEFAULT && bk != CLR_NONE)) FillSolid(dc, r.label, bk);
    RECT t = r.label;
    InflateRect(&t, -2, -1);
    UINT f = DT_CENTER | DT_WORDBREAK | DT_EDITCONTROL;
    if (!(s.state & LVIS_FOCUSED) || !focused) f |= DT_END_ELLIPSIS;
    DrawLabel(dc, s.text, t, f, text);
    if ((s.state & LVIS_FOCUSED) && focused) DrawFocusRect(dc, &r.label);
  } else {
    DrawStateIcon(lv, dc, r.state, s.state);
    DrawIcon(lv, dc, lv->images[LVSIL_SMALL], s.image, r.icon, s.state, blend);
    if (shown || (bk != CLR_DEFAULT && bk != CLR_NONE)) FillSolid(dc, r.label, bk);
    RECT t = r.label;
    t.left += 2;
    DrawLabel(dc, s.text, t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT, text);
    if ((s.state & LVIS_FOCUSED) && focused) DrawFocusRect(dc, &r.label);
  }
  if (itemCd & CDRF_NOTIFYPOSTPAINT) DrawStage(lv, dc, CDDS_ITEMPOSTPAINT, i, 0, r.bounds, itemState, NULL, NULL);
}

void Paint(ListView* lv, HDC dc, const RECT& update) {
  Metrics m = GetMetrics(lv);
  HGDIOBJ oldFont = SelectObject(dc, FontOf(lv));
  SetBkMode(dc, TRANSPARENT);
  RECT all = m.client;
  UINT cd = DrawStage(lv, dc, CDDS_PREPAINT, 0, 0, all, 0, NULL, NULL);
  if (cd & CDRF_SKIPDEFAULT) {
    SelectObject(dc, oldFont);
    return;
  }
  bool focused = HasFocus(lv);
  int n = Count(lv);
  int first = 0, last = n - 1;
  if (View(lv) == LVS_REPORT) {
    first = std::max(0, (int)((update.top - m.headerH + lv->origin.y) / m.row));
    last = std::min(n - 1, (int)((update.bottom - m.headerH + lv->origin.y) / m.row));
  } else if (View(lv) == LVS_LIST) {
    int per = PerColumn(lv, m);
    first = std::max(0, (int)((update.left + lv->origin.x) / m.cell.cx) * per);
    last = std::min(n - 1, (int)((update.right + lv->origin.x) / m.cell.cx + 1) * per - 1);
  }
  if (lv->ownerData && first <= last) {
    NMLVCACHEHINT hint = {};
    hint.iFrom = first;
    hint.iTo = last;
    Notify(lv, LVN_ODCACHEHINT, &hint.hdr);
  }
  for (int i = first; i <= last; i++) {
    if (View(lv) != LVS_REPORT && View(lv) != LVS_LIST) {
      Rects r = ItemRects(lv, m, i);
      RECT b, x;
      UnionRect(&b, &r.bounds, &r.select);
      if (!IntersectRect(&x, &b, &update)) continue;
    }
    PaintItem(lv, dc, m, i, focused, cd);
  }
  if (View(lv) == LVS_REPORT && (lv->ex & LVS_EX_GRIDLINES)) {
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));
    HGDIOBJ old = SelectObject(dc, pen);
    for (int c = 0, nc = ColumnCount(lv); c < nc; c++) {
      RECT cr = ColumnRect(lv, c);
      MoveToEx(dc, cr.right - 1, m.headerH, NULL);
      LineTo(dc, cr.right - 1, m.client.bottom);
    }
    int right = TotalWidth(lv) - lv->origin.x;
    for (int y = m.headerH + m.row - 1 - (lv->origin.y % m.row); y < m.client.bottom; y += m.row) {
      MoveToEx(dc, 0, y, NULL);
      LineTo(dc, right, y);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
  }
  if (cd & CDRF_NOTIFYPOSTPAINT) DrawStage(lv, dc, CDDS_POSTPAINT, 0, 0, all, 0, NULL, NULL);
  SelectObject(dc, oldFont);
}

HIMAGELIST CreateDragImage(ListView* lv, int i, POINT* upLeft) {
  if (!Valid(lv, i)) return NULL;
  Metrics m = GetMetrics(lv);
  Rects r = ItemRects(lv, m, i);
  RECT b;
  UnionRect(&b, &r.icon, &r.label);
  int w = std::max(1, (int)(b.right - b.left)), h = std::max(1, (int)(b.bottom - b.top));
  HDC screen = GetDC(NULL);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
  HGDIOBJ old = SelectObject(mem, bm);
  COLORREF key = RGB(255, 0, 255);
  RECT all = {0, 0, w, h};
  FillSolid(mem, all, key);
  SetViewportOrgEx(mem, -b.left, -b.top, NULL);
  Shown s = Resolve(lv, i, 0, LVIF_TEXT | LVIF_IMAGE);
  HIMAGELIST il = lv->images[View(lv) == LVS_ICON ? LVSIL_NORMAL : LVSIL_SMALL];
  DrawIcon(lv, mem, il, s.image, r.icon, 0, false);
  HGDIOBJ oldFont = SelectObject(mem, FontOf(lv));
  SetBkMode(mem, TRANSPARENT);
  RECT t = r.label;
  t.left += 2;
  DrawLabel(mem, s.text, t,
            View(lv) == LVS_ICON ? DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS
                                 : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
            TextColor(lv));
  SelectObject(mem, oldFont);
  SetViewportOrgEx(mem, 0, 0, NULL);
  SelectObject(mem, old);
  HIMAGELIST drag = ImageList_Create(w, h, ILC_COLOR24 | ILC_MASK, 1, 0);
  ImageList_AddMasked(drag, bm, key);
  DeleteObject(bm);
  DeleteDC(mem);
  ReleaseDC(NULL, screen);
  if (upLeft) *upLeft = POINT{b.left, b.top};
  return drag;
}

// ---------------------------------------------------------------------
// Keyboard and mouse

void MoveFocus(ListView* lv, int to, bool shift, bool ctrl) {
  if (!Valid(lv, to)) return;
  if (ctrl && !shift) {
    SetState(lv, to, LVIS_FOCUSED, LVIS_FOCUSED);
  } else if (shift && !(StyleOf(lv->hwnd) & LVS_SINGLESEL)) {
    int anchor = Valid(lv, lv->mark) ? lv->mark : to;
    SelectRange(lv, anchor, to, ctrl);
    SetState(lv, to, LVIS_FOCUSED, LVIS_FOCUSED);
    lv->mark = anchor;
  } else {
    SelectOnly(lv, to);
    SetState(lv, to, LVIS_FOCUSED, LVIS_FOCUSED);
    lv->mark = to;
  }
  EnsureVisible(lv, to, false);
}

void KeyDown(ListView* lv, WPARAM vk) {
  NMLVKEYDOWN kd = {};
  kd.wVKey = (WORD)vk;
  Notify(lv, LVN_KEYDOWN, &kd.hdr);
  int n = Count(lv);
  if (!n) return;
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  int cur = Valid(lv, lv->focus) ? lv->focus : -1;
  int to = -1;
  Metrics m = GetMetrics(lv);
  switch (vk) {
    case VK_UP: to = cur < 0 ? 0 : Neighbor(lv, cur, LVNI_ABOVE); break;
    case VK_DOWN: to = cur < 0 ? 0 : Neighbor(lv, cur, LVNI_BELOW); break;
    case VK_LEFT:
      if (View(lv) == LVS_REPORT) {
        ScrollTo(lv, lv->origin.x - m.fh, lv->origin.y);
        return;
      }
      to = cur < 0 ? 0 : Neighbor(lv, cur, LVNI_TOLEFT);
      break;
    case VK_RIGHT:
      if (View(lv) == LVS_REPORT) {
        ScrollTo(lv, lv->origin.x + m.fh, lv->origin.y);
        return;
      }
      to = cur < 0 ? 0 : Neighbor(lv, cur, LVNI_TORIGHT);
      break;
    case VK_HOME: to = 0; break;
    case VK_END: to = n - 1; break;
    case VK_PRIOR:
    case VK_NEXT: {
      int step = View(lv) == LVS_REPORT ? PerPage(lv, m) - 1
                 : View(lv) == LVS_LIST ? PerColumn(lv, m) * std::max(1, (int)(m.client.right / m.cell.cx))
                                        : PerRow(lv, m) * std::max(1, (int)(m.client.bottom / m.cell.cy) - 1);
      step = std::max(1, step);
      to = std::max(0, std::min(n - 1, std::max(cur, 0) + (vk == VK_NEXT ? step : -step)));
      break;
    }
    case VK_SPACE:
      if (cur < 0) return;
      if (lv->ex & LVS_EX_CHECKBOXES) {
        ToggleCheck(lv, cur);
      } else if (ctrl) {
        SetState(lv, cur, (StateRef(lv, cur) & LVIS_SELECTED) ? 0 : LVIS_SELECTED, LVIS_SELECTED);
      } else {
        SetState(lv, cur, LVIS_SELECTED, LVIS_SELECTED);
      }
      return;
    case VK_RETURN:
      Notify(lv, NM_RETURN);
      if (cur >= 0) NotifyActivate(lv, LVN_ITEMACTIVATE, cur, 0, POINT{0, 0});
      return;
    case VK_F2:
      if (cur >= 0 && (StyleOf(lv->hwnd) & LVS_EDITLABELS)) BeginEdit(lv, cur);
      return;
    default: return;
  }
  if (to >= 0) MoveFocus(lv, to, shift, ctrl);
}

void TypeAhead(ListView* lv, wchar_t ch) {
  if (ch < L' ') return;
  DWORD now = GetTickCount();
  if (now - lv->searchTime > 1000) lv->search.clear();
  lv->searchTime = now;
  lv->search += ch;
  int n = Count(lv);
  if (!n) return;
  // A repeated first letter cycles through the items that start with it.
  bool cycle = lv->search.size() > 1 && std::all_of(lv->search.begin(), lv->search.end(),
                                                     [&](wchar_t c) { return towlower(c) == towlower(lv->search[0]); });
  std::wstring want = cycle ? lv->search.substr(0, 1) : lv->search;
  int start = Valid(lv, lv->focus) ? lv->focus : -1;
  if (lv->search.size() > 1 && !cycle) start--;
  for (int k = 1; k <= n; k++) {
    int j = ((start + k) % n + n) % n;
    std::wstring t = ItemText(lv, j, 0);
    if (t.size() >= want.size() && _wcsnicmp(t.c_str(), want.c_str(), want.size()) == 0) {
      MoveFocus(lv, j, false, false);
      return;
    }
  }
}

void ButtonDown(ListView* lv, POINT pt, bool right, bool dbl) {
  if (lv->edit) EndEdit(lv, true);
  KillTimer(lv->hwnd, kEditTimer);
  lv->pendingEdit = -1;
  if (GetFocus() != lv->hwnd) SetFocus(lv->hwnd);
  UINT flags;
  int sub;
  int i = HitTest(lv, pt, &flags, &sub, false);
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  if (dbl) {
    NotifyActivate(lv, right ? NM_RDBLCLK : NM_DBLCLK, i, sub, pt);
    if (i >= 0 && !right) {
      if ((flags & LVHT_ONITEMSTATEICON) && (lv->ex & LVS_EX_CHECKBOXES))
        ToggleCheck(lv, i);
      else
        NotifyActivate(lv, LVN_ITEMACTIVATE, i, sub, pt);
    }
    return;
  }
  lv->pressItem = i;
  lv->pressPt = pt;
  lv->pressRight = right;
  lv->pressWasSelected = i >= 0 && (StateRef(lv, i) & LVIS_SELECTED) && lv->focus == i;
  lv->pressOnLabel = (flags & LVHT_ONITEMLABEL) != 0;
  if (i < 0) {
    if (!shift && !ctrl) SelectOnly(lv, -1);
  } else if (!right && (flags & LVHT_ONITEMSTATEICON) && (lv->ex & LVS_EX_CHECKBOXES)) {
    ToggleCheck(lv, i);
  } else if (ctrl && !right) {
    if (StyleOf(lv->hwnd) & LVS_SINGLESEL)
      SelectOnly(lv, i);
    else
      SetState(lv, i, (StateRef(lv, i) & LVIS_SELECTED) ? 0 : LVIS_SELECTED, LVIS_SELECTED);
    SetState(lv, i, LVIS_FOCUSED, LVIS_FOCUSED);
    lv->mark = i;
  } else if (shift && !right) {
    MoveFocus(lv, i, true, false);
  } else if (!(StateRef(lv, i) & LVIS_SELECTED)) {
    SelectOnly(lv, i);
    SetState(lv, i, LVIS_FOCUSED, LVIS_FOCUSED);
    lv->mark = i;
  } else {
    // A click on a selected item keeps the others for a drag; the button
    // coming up drops them.
    SetState(lv, i, LVIS_FOCUSED, LVIS_FOCUSED);
    lv->mark = i;
  }
  lv->tracking = true;
  SetCapture(lv->hwnd);
}

void ButtonUp(ListView* lv, POINT pt, bool right) {
  if (!lv->tracking || lv->pressRight != right) return;
  lv->tracking = false;
  if (GetCapture() == lv->hwnd) ReleaseCapture();
  int i = lv->pressItem;
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  if (Valid(lv, i) && !shift && !ctrl && !right) SelectOnly(lv, i);
  UINT flags;
  int sub;
  int at = HitTest(lv, pt, &flags, &sub, false);
  if (right) {
    if (!NotifyActivate(lv, NM_RCLICK, at, sub, pt)) {
      POINT s = pt;
      ClientToScreen(lv->hwnd, &s);
      SendMessageW(lv->hwnd, WM_CONTEXTMENU, (WPARAM)lv->hwnd, MAKELPARAM(s.x, s.y));
    }
    return;
  }
  NotifyActivate(lv, NM_CLICK, at, sub, pt);
  if (Valid(lv, at) && (lv->ex & LVS_EX_ONECLICKACTIVATE)) NotifyActivate(lv, LVN_ITEMACTIVATE, at, sub, pt);
  // A second click on the focused item's label edits it, unless a double
  // click comes first.
  if (Valid(lv, at) && at == i && lv->pressWasSelected && lv->pressOnLabel && (StyleOf(lv->hwnd) & LVS_EDITLABELS)) {
    lv->pendingEdit = at;
    SetTimer(lv->hwnd, kEditTimer, GetDoubleClickTime(), NULL);
  }
}

void MouseMove(ListView* lv, POINT pt) {
  if (!lv->tracking || !Valid(lv, lv->pressItem)) return;
  if (abs(pt.x - lv->pressPt.x) < GetSystemMetrics(SM_CXDRAG) &&
      abs(pt.y - lv->pressPt.y) < GetSystemMetrics(SM_CYDRAG))
    return;
  lv->tracking = false;
  if (GetCapture() == lv->hwnd) ReleaseCapture();
  NotifyItem(lv, lv->pressRight ? LVN_BEGINRDRAG : LVN_BEGINDRAG, lv->pressItem, 0, 0, 0, 0, lv->pressPt);
}

void Scroll(ListView* lv, int bar, int code) {
  Metrics m = GetMetrics(lv);
  SCROLLINFO si = {sizeof si, SIF_ALL};
  GetScrollInfo(lv->hwnd, bar, &si);
  int pos = si.nPos, line = 1, page = std::max(1, (int)si.nPage);
  int view = View(lv);
  if (view == LVS_REPORT && bar == SB_HORZ) line = m.fh;
  if (view == LVS_ICON || view == LVS_SMALLICON) line = bar == SB_HORZ ? m.cell.cx / 2 : m.cell.cy / 2;
  switch (code) {
    case SB_LINEUP: pos -= line; break;
    case SB_LINEDOWN: pos += line; break;
    case SB_PAGEUP: pos -= page; break;
    case SB_PAGEDOWN: pos += page; break;
    case SB_TOP: pos = si.nMin; break;
    case SB_BOTTOM: pos = si.nMax; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    default: return;
  }
  pos = std::max(si.nMin, std::min(pos, si.nMax - std::max(0, (int)si.nPage - 1)));
  int x = lv->origin.x, y = lv->origin.y;
  if (view == LVS_REPORT) {
    if (bar == SB_VERT)
      y = pos * m.row;
    else
      x = pos;
  } else if (view == LVS_LIST) {
    if (bar == SB_HORZ) x = pos * m.cell.cx;
  } else if (bar == SB_VERT) {
    y = pos;
  } else {
    x = pos;
  }
  ScrollTo(lv, x, y);
}

void Arrange(ListView* lv) {
  for (Item& it : lv->items) it.placed = false;
  Relayout(lv);
}

void SetImageList(ListView* lv, int which, HIMAGELIST il) {
  lv->images[which] = il;
  if (which == LVSIL_SMALL && lv->header) SendMessageW(lv->header, HDM_SETIMAGELIST, 0, (LPARAM)il);
  Relayout(lv);
}

DWORD SetExStyle(ListView* lv, DWORD mask, DWORD style) {
  DWORD old = lv->ex;
  if (!mask) mask = ~0u;
  lv->ex = (lv->ex & ~mask) | (style & mask);
  if ((lv->ex & LVS_EX_CHECKBOXES) && !(old & LVS_EX_CHECKBOXES)) {
    for (int j = 0, n = Count(lv); j < n; j++)
      if (!(StateRef(lv, j) & LVIS_STATEIMAGEMASK)) StateRef(lv, j) |= INDEXTOSTATEIMAGEMASK(1);
  } else if (!(lv->ex & LVS_EX_CHECKBOXES) && (old & LVS_EX_CHECKBOXES) && !lv->images[LVSIL_STATE]) {
    for (int j = 0, n = Count(lv); j < n; j++) StateRef(lv, j) &= ~LVIS_STATEIMAGEMASK;
  }
  if (lv->header) {
    LONG hs = GetWindowLongW(lv->header, GWL_STYLE);
    hs = (lv->ex & LVS_EX_HEADERDRAGDROP) ? (hs | HDS_DRAGDROP) : (hs & ~HDS_DRAGDROP);
    SetWindowLongW(lv->header, GWL_STYLE, hs);
  }
  Relayout(lv);
  return old;
}

// The header tells its parent, the list view, about the columns; the
// list view follows and passes the news on.
LRESULT HeaderNotify(ListView* lv, WPARAM wp, NMHDR* nm) {
  UINT code = nm->code;
  NMHEADERW* hn = (NMHEADERW*)nm;
  if (code == HDN_ITEMCLICKW || code == HDN_ITEMCLICKA) {
    int c = hn->iItem;
    NotifyItem(lv, LVN_COLUMNCLICK, -1, c);
    return 0;
  }
  if (code == HDN_DIVIDERDBLCLICKW || code == HDN_DIVIDERDBLCLICKA) {
    LRESULT r = SendMessageW(GetParent(lv->hwnd), WM_NOTIFY, wp, (LPARAM)nm);
    SetColumnWidth(lv, hn->iItem, LVSCW_AUTOSIZE);
    return r;
  }
  LRESULT r = SendMessageW(GetParent(lv->hwnd), WM_NOTIFY, wp, (LPARAM)nm);
  if (code == HDN_ITEMCHANGEDW || code == HDN_ITEMCHANGEDA || code == HDN_TRACKW || code == HDN_TRACKA ||
      code == HDN_ENDTRACKW || code == HDN_ENDTRACKA || code == HDN_ENDDRAG) {
    if (code == HDN_ENDDRAG) PostMessageW(lv->hwnd, WM_USER + 0x7F00, 0, 0);
    UpdateScroll(lv);
    InvalidateRect(lv->hwnd, NULL, TRUE);
  }
  return r;
}

// ---------------------------------------------------------------------
// The window procedure

LRESULT CALLBACK ListViewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ListView* lv = StateOf(h);
  if (msg == WM_NCCREATE) {
    lv = new ListView;
    lv->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)lv);
    lv->ownerData = (((CREATESTRUCTW*)lp)->style & LVS_OWNERDATA) != 0;
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!lv) return DefWindowProcW(h, msg, wp, lp);
  bool u = true;
  switch (msg) {
    case WM_CREATE: {
      lv->unicodeNotify = NotifyUnicode(h);
      DWORD style = StyleOf(h);
      DWORD hs = WS_CHILD | HDS_HORZ | (style & LVS_NOSORTHEADER ? 0 : HDS_BUTTONS);
      lv->header = CreateWindowExW(0, WC_HEADERW, L"", hs, 0, 0, 0, 0, h, (HMENU)0,
                                   (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE), NULL);
      if (lv->header) SendMessageW(lv->header, WM_SETFONT, (WPARAM)FontOf(lv), FALSE);
      UpdateScroll(lv);
      return 0;
    }
    case WM_DESTROY:
      if (lv->edit) EndEdit(lv, false);
      DeleteAll(lv);
      if (!(StyleOf(h) & LVS_SHAREIMAGELISTS))
        for (HIMAGELIST& il : lv->images)
          if (il) ImageList_Destroy(il), il = NULL;
      return 0;
    case WM_NCDESTROY:
      delete lv;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_NOTIFYFORMAT:
      if (lp == NF_REQUERY) {
        lv->unicodeNotify = NotifyUnicode(h);
        if (lv->header) SendMessageW(lv->header, WM_NOTIFYFORMAT, (WPARAM)h, NF_REQUERY);
      }
      return lv->unicodeNotify ? NFR_UNICODE : NFR_ANSI;
    case WM_NOTIFY: {
      NMHDR* nm = (NMHDR*)lp;
      if (nm && lv->header && nm->hwndFrom == lv->header) return HeaderNotify(lv, wp, nm);
      return 0;
    }
    case WM_STYLECHANGED:
      if (wp == (WPARAM)GWL_STYLE) {
        STYLESTRUCT* ss = (STYLESTRUCT*)lp;
        if ((ss->styleOld ^ ss->styleNew) & LVS_TYPEMASK) {
          lv->origin = POINT{0, 0};
          lv->measured = false;
        }
        if (lv->header) {
          LONG hs = GetWindowLongW(lv->header, GWL_STYLE);
          hs = (ss->styleNew & LVS_NOSORTHEADER) ? (hs & ~HDS_BUTTONS) : (hs | HDS_BUTTONS);
          SetWindowLongW(lv->header, GWL_STYLE, hs);
        }
        Relayout(lv);
      }
      return 0;
    case WM_SIZE:
      if (StyleOf(h) & LVS_AUTOARRANGE) Arrange(lv);
      UpdateScroll(lv);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      lv->font = (HFONT)wp;
      lv->measured = false;
      if (lv->header) SendMessageW(lv->header, WM_SETFONT, wp, lp);
      UpdateScroll(lv);
      if (LOWORD(lp)) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT: return (LRESULT)lv->font;
    case WM_ERASEBKGND: {
      if (lv->bk == CLR_NONE) {
        return DefWindowProcW(h, msg, wp, lp);
      }
      RECT r;
      GetClientRect(h, &r);
      FillSolid((HDC)wp, r, BkColor(lv));
      return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      if (msg == WM_PRINTCLIENT || wp) {
        RECT r;
        GetClientRect(h, &r);
        if (msg == WM_PRINTCLIENT) FillSolid((HDC)wp, r, BkColor(lv));
        Paint(lv, (HDC)wp, r);
        return 0;
      }
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      Paint(lv, dc, ps.rcPaint);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      Notify(lv, msg == WM_SETFOCUS ? NM_SETFOCUS : NM_KILLFOCUS);
      for (int j = 0, n = Count(lv); j < n; j++)
        if (StateRef(lv, j) & (LVIS_SELECTED | LVIS_FOCUSED)) InvalidateItem(lv, j);
      if (msg == WM_SETFOCUS && !Valid(lv, lv->focus) && Count(lv) > 0 && !lv->ownerData)
        SetState(lv, 0, LVIS_FOCUSED, LVIS_FOCUSED);
      return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
      KeyDown(lv, wp);
      return 0;
    case WM_CHAR:
      TypeAhead(lv, (wchar_t)wp);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
      ButtonDown(lv, POINT{(short)LOWORD(lp), (short)HIWORD(lp)}, msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK,
                 msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK);
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
      ButtonUp(lv, POINT{(short)LOWORD(lp), (short)HIWORD(lp)}, msg == WM_RBUTTONUP);
      return 0;
    case WM_MOUSEMOVE:
      MouseMove(lv, POINT{(short)LOWORD(lp), (short)HIWORD(lp)});
      return 0;
    case WM_CAPTURECHANGED:
      if ((HWND)lp != h) lv->tracking = false;
      return 0;
    case WM_TIMER:
      if (wp == kEditTimer) {
        KillTimer(h, kEditTimer);
        int i = lv->pendingEdit;
        lv->pendingEdit = -1;
        if (Valid(lv, i) && i == lv->focus && HasFocus(lv)) BeginEdit(lv, i);
        return 0;
      }
      break;
    case WM_VSCROLL:
    case WM_HSCROLL:
      if (lv->edit) EndEdit(lv, true);
      Scroll(lv, msg == WM_VSCROLL ? SB_VERT : SB_HORZ, LOWORD(wp));
      return 0;
    case WM_MOUSEWHEEL: {
      int lines = -(short)HIWORD(wp) / WHEEL_DELTA * 3;
      bool horz = View(lv) == LVS_LIST;
      for (int k = 0; k < abs(lines); k++)
        Scroll(lv, horz ? SB_HORZ : SB_VERT, lines > 0 ? SB_LINEDOWN : SB_LINEUP);
      return 0;
    }
    case WM_COMMAND:
      return 0;  // the label editor's notifications
    case WM_USER + 0x7F00:  // after the header has moved a column
      InvalidateRect(h, NULL, TRUE);
      return 0;

    case CCM_SETUNICODEFORMAT: {
      bool old = lv->unicodeNotify;
      lv->unicodeNotify = wp != 0;
      return old;
    }
    case CCM_GETUNICODEFORMAT: return lv->unicodeNotify;

    case LVM_GETBKCOLOR: return lv->bk == CLR_DEFAULT ? GetSysColor(COLOR_WINDOW) : lv->bk;
    case LVM_SETBKCOLOR:
      lv->bk = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    case LVM_GETTEXTCOLOR: return TextColor(lv);
    case LVM_SETTEXTCOLOR:
      lv->text = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    case LVM_GETTEXTBKCOLOR: return lv->textBk == CLR_DEFAULT ? BkColor(lv) : lv->textBk;
    case LVM_SETTEXTBKCOLOR:
      lv->textBk = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    case LVM_GETIMAGELIST: return wp < 3 ? (LRESULT)lv->images[wp] : 0;
    case LVM_SETIMAGELIST: {
      if (wp >= 3) return 0;
      HIMAGELIST old = lv->images[wp];
      SetImageList(lv, (int)wp, (HIMAGELIST)lp);
      return (LRESULT)old;
    }
    case LVM_GETITEMCOUNT: return Count(lv);
    case LVM_SETITEMCOUNT:
      if (lv->ownerData) {
        int n = std::max(0, (int)wp);
        lv->odState.resize(n, 0);
        if (lv->focus >= n) lv->focus = -1;
        if (lv->mark >= n) lv->mark = -1;
        Relayout(lv);
      } else {
        lv->items.reserve(wp);
      }
      return TRUE;
    case LVM_GETITEMA: u = false; [[fallthrough]];
    case LVM_GETITEMW: return GetItem(lv, (LVITEMW*)lp, u);
    case LVM_SETITEMA: u = false; [[fallthrough]];
    case LVM_SETITEMW: return SetItem(lv, (LVITEMW*)lp, u);
    case LVM_INSERTITEMA: u = false; [[fallthrough]];
    case LVM_INSERTITEMW: return InsertItem(lv, (LVITEMW*)lp, u);
    case LVM_DELETEITEM: return DeleteItem(lv, (int)wp);
    case LVM_DELETEALLITEMS: return DeleteAll(lv);
    case LVM_GETCALLBACKMASK: return lv->callbackMask;
    case LVM_SETCALLBACKMASK:
      lv->callbackMask = (UINT)wp;
      return TRUE;
    case LVM_GETNEXTITEM: return NextItem(lv, (int)wp, (UINT)LOWORD(lp));
    case LVM_FINDITEMA: u = false; [[fallthrough]];
    case LVM_FINDITEMW: return FindItem(lv, (int)wp, (const LVFINDINFOW*)lp, u);
    case LVM_GETITEMRECT: {
      RECT* out = (RECT*)lp;
      if (!out || !Valid(lv, (int)wp)) return FALSE;
      Metrics m = GetMetrics(lv);
      Rects r = ItemRects(lv, m, (int)wp);
      switch (out->left) {
        case LVIR_ICON: *out = r.icon; break;
        case LVIR_LABEL: *out = r.label; break;
        case LVIR_SELECTBOUNDS: *out = r.select; break;
        default: *out = r.bounds; break;
      }
      return TRUE;
    }
    case LVM_GETSUBITEMRECT: {
      RECT* out = (RECT*)lp;
      if (!out || !Valid(lv, (int)wp) || View(lv) != LVS_REPORT) return FALSE;
      int sub = out->top, code = out->left;
      Metrics m = GetMetrics(lv);
      Rects r = ItemRects(lv, m, (int)wp);
      if (sub == 0) {
        *out = code == LVIR_ICON ? r.icon : code == LVIR_LABEL ? r.label : r.bounds;
        return TRUE;
      }
      int c = ColumnOf(lv, sub);
      if (c < 0) return FALSE;
      RECT cr = ColumnRect(lv, c);
      *out = RECT{cr.left, r.bounds.top, cr.right, r.bounds.bottom};
      if (code == LVIR_ICON) out->right = out->left + m.small.cx;
      return TRUE;
    }
    case LVM_SETITEMPOSITION:
    case LVM_SETITEMPOSITION32: {
      int i = (int)wp;
      if (!Valid(lv, i) || lv->ownerData) return FALSE;
      POINT p = msg == LVM_SETITEMPOSITION ? POINT{(short)LOWORD(lp), (short)HIWORD(lp)} : *(POINT*)lp;
      lv->items[i].pos = POINT{p.x + lv->origin.x, p.y + lv->origin.y};
      lv->items[i].placed = true;
      Relayout(lv);
      return TRUE;
    }
    case LVM_GETITEMPOSITION: {
      POINT* out = (POINT*)lp;
      if (!out || !Valid(lv, (int)wp)) return FALSE;
      Metrics m = GetMetrics(lv);
      Rects r = ItemRects(lv, m, (int)wp);
      *out = POINT{r.bounds.left, r.bounds.top};
      return TRUE;
    }
    case LVM_GETSTRINGWIDTHA: return lp ? TextWidth(lv, Widen((const char*)lp)) : 0;
    case LVM_GETSTRINGWIDTHW: return lp ? TextWidth(lv, (const wchar_t*)lp) : 0;
    case LVM_HITTEST:
    case LVM_SUBITEMHITTEST: {
      LVHITTESTINFO* ht = (LVHITTESTINFO*)lp;
      if (!ht) return -1;
      int sub = 0;
      ht->iItem = HitTest(lv, ht->pt, &ht->flags, &sub, msg == LVM_SUBITEMHITTEST);
      if (msg == LVM_SUBITEMHITTEST) ht->iSubItem = ht->iItem >= 0 ? sub : 0;
      return ht->iItem;
    }
    case LVM_ENSUREVISIBLE: return EnsureVisible(lv, (int)wp, lp != 0);
    case LVM_SCROLL: {
      Metrics m = GetMetrics(lv);
      int dx = (int)wp, dy = (int)lp;
      if (View(lv) == LVS_REPORT) dy = (dy / m.row + (dy % m.row ? (dy > 0 ? 1 : -1) : 0)) * m.row;
      if (View(lv) == LVS_LIST) {
        int cols = dx / m.cell.cx + (dx % m.cell.cx ? (dx > 0 ? 1 : -1) : 0);
        dx = cols * m.cell.cx;
        dy = 0;
      }
      ScrollTo(lv, lv->origin.x + dx, lv->origin.y + dy);
      return TRUE;
    }
    case LVM_REDRAWITEMS:
      for (int i = std::max(0, (int)wp); i <= (int)lp && i < Count(lv); i++) InvalidateItem(lv, i);
      return TRUE;
    case LVM_UPDATE:
      if (StyleOf(h) & LVS_AUTOARRANGE) Arrange(lv);
      UpdateScroll(lv);
      InvalidateItem(lv, (int)wp);
      return TRUE;
    case LVM_ARRANGE:
      Arrange(lv);
      return TRUE;
    case LVM_EDITLABELA:
    case LVM_EDITLABELW:
      if ((int)wp < 0) {
        EndEdit(lv, false);
        return 0;
      }
      return (LRESULT)BeginEdit(lv, (int)wp);
    case LVM_GETEDITCONTROL: return (LRESULT)lv->edit;
    case LVM_GETCOLUMNA: u = false; [[fallthrough]];
    case LVM_GETCOLUMNW: return GetColumn(lv, (int)wp, (LVCOLUMNW*)lp, u);
    case LVM_SETCOLUMNA: u = false; [[fallthrough]];
    case LVM_SETCOLUMNW: return SetColumn(lv, (int)wp, (LVCOLUMNW*)lp, u);
    case LVM_INSERTCOLUMNA: u = false; [[fallthrough]];
    case LVM_INSERTCOLUMNW: return InsertColumn(lv, (int)wp, (LVCOLUMNW*)lp, u);
    case LVM_DELETECOLUMN: return DeleteColumn(lv, (int)wp);
    case LVM_GETCOLUMNWIDTH:
      if (View(lv) == LVS_LIST) return GetMetrics(lv).cell.cx;
      if ((int)wp < 0 || (int)wp >= ColumnCount(lv)) return 0;
      return ColumnWidth(lv, (int)wp);
    case LVM_SETCOLUMNWIDTH: return SetColumnWidth(lv, (int)wp, (short)LOWORD(lp));
    case LVM_GETHEADER: return (LRESULT)lv->header;
    case LVM_SETCOLUMNORDERARRAY: {
      BOOL ok = (BOOL)SendMessageW(lv->header, HDM_SETORDERARRAY, wp, lp);
      Relayout(lv);
      return ok;
    }
    case LVM_GETCOLUMNORDERARRAY: return SendMessageW(lv->header, HDM_GETORDERARRAY, wp, lp);
    case LVM_CREATEDRAGIMAGE: return (LRESULT)CreateDragImage(lv, (int)wp, (POINT*)lp);
    case LVM_GETVIEWRECT: {
      RECT* out = (RECT*)lp;
      if (!out || View(lv) == LVS_REPORT || View(lv) == LVS_LIST) return FALSE;
      Metrics m = GetMetrics(lv);
      SIZE s = ContentSize(lv, m);
      *out = RECT{-lv->origin.x, -lv->origin.y, s.cx - lv->origin.x, s.cy - lv->origin.y};
      return TRUE;
    }
    case LVM_GETORIGIN: {
      POINT* out = (POINT*)lp;
      if (!out) return FALSE;
      if (View(lv) == LVS_REPORT || View(lv) == LVS_LIST) {
        *out = POINT{0, 0};
        return FALSE;
      }
      *out = lv->origin;
      return TRUE;
    }
    case LVM_GETTOPINDEX: {
      Metrics m = GetMetrics(lv);
      if (View(lv) == LVS_REPORT) return lv->origin.y / m.row;
      if (View(lv) == LVS_LIST) return (lv->origin.x / m.cell.cx) * PerColumn(lv, m);
      return 0;
    }
    case LVM_GETCOUNTPERPAGE: {
      Metrics m = GetMetrics(lv);
      if (View(lv) == LVS_REPORT) return PerPage(lv, m);
      if (View(lv) == LVS_LIST) return PerColumn(lv, m) * std::max(1, (int)(m.client.right / m.cell.cx));
      return Count(lv);
    }
    case LVM_SETITEMSTATE: {
      LVITEMW* in = (LVITEMW*)lp;
      if (!in) return FALSE;
      if ((int)wp == -1) {
        for (int j = 0, n = Count(lv); j < n; j++) SetState(lv, j, in->state, in->stateMask);
        return TRUE;
      }
      return SetState(lv, (int)wp, in->state, in->stateMask);
    }
    case LVM_GETITEMSTATE:
      if (!Valid(lv, (int)wp)) return 0;
      return Resolve(lv, (int)wp, 0, LVIF_STATE).state & (UINT)lp;
    case LVM_GETITEMTEXTA: u = false; [[fallthrough]];
    case LVM_GETITEMTEXTW: {
      LVITEMW* out = (LVITEMW*)lp;
      if (!out || !Valid(lv, (int)wp)) return 0;
      std::wstring t = ItemText(lv, (int)wp, out->iSubItem);
      return CopyOut(t, out->pszText, out->cchTextMax, u);
    }
    case LVM_SETITEMTEXTA: u = false; [[fallthrough]];
    case LVM_SETITEMTEXTW: {
      LVITEMW* in = (LVITEMW*)lp;
      if (!in) return FALSE;
      LVITEMW set = *in;
      set.mask = LVIF_TEXT;
      set.iItem = (int)wp;
      return SetItem(lv, &set, u);
    }
    case LVM_SORTITEMS: return SortItems(lv, (PFNLVCOMPARE)lp, (LPARAM)wp, false);
    case LVM_SORTITEMSEX: return SortItems(lv, (PFNLVCOMPARE)lp, (LPARAM)wp, true);
    case LVM_GETSELECTEDCOUNT: return SelectedCount(lv);
    case LVM_GETITEMSPACING: {
      Metrics m = GetMetrics(lv);
      if (wp) return MAKELONG(m.state.cx + m.small.cx + 64, m.row);
      DWORD view = StyleOf(h);
      SetWindowLongW(h, GWL_STYLE, (view & ~LVS_TYPEMASK) | LVS_ICON);
      Metrics im = GetMetrics(lv);
      SetWindowLongW(h, GWL_STYLE, view);
      return MAKELONG(im.cell.cx, im.cell.cy);
    }
    case LVM_SETICONSPACING: {
      Metrics m = GetMetrics(lv);
      LRESULT old = View(lv) == LVS_ICON ? MAKELONG(m.cell.cx, m.cell.cy) : 0;
      if (lp == -1) {
        lv->spacing = SIZE{0, 0};
      } else {
        lv->spacing = SIZE{(short)LOWORD(lp), (short)HIWORD(lp)};
      }
      Relayout(lv);
      return old;
    }
    case LVM_SETEXTENDEDLISTVIEWSTYLE: return SetExStyle(lv, (DWORD)wp, (DWORD)lp);
    case LVM_GETEXTENDEDLISTVIEWSTYLE: return lv->ex;
    case LVM_SETHOTITEM: {
      int old = lv->hot;
      lv->hot = (int)wp;
      return old;
    }
    case LVM_GETHOTITEM: return lv->hot;
    case LVM_SETHOTCURSOR: {
      HCURSOR old = lv->hotCursor;
      lv->hotCursor = (HCURSOR)lp;
      return (LRESULT)old;
    }
    case LVM_GETHOTCURSOR: return (LRESULT)lv->hotCursor;
    case LVM_APPROXIMATEVIEWRECT: {
      Metrics m = GetMetrics(lv);
      int n = (int)wp < 0 ? Count(lv) : (int)wp;
      if (View(lv) == LVS_REPORT) return MAKELONG(TotalWidth(lv), m.headerH + n * m.row);
      int cx = (short)LOWORD(lp) > 0 ? (short)LOWORD(lp) : (int)m.client.right;
      int per = std::max(1, (int)(cx / m.cell.cx));
      if (View(lv) == LVS_LIST) {
        int cy = (short)HIWORD(lp) > 0 ? (short)HIWORD(lp) : (int)m.client.bottom;
        int pc = std::max(1, cy / m.row);
        return MAKELONG(((n + pc - 1) / pc) * m.cell.cx, std::min(n, pc) * m.row);
      }
      return MAKELONG(std::min(n, per) * m.cell.cx, ((n + per - 1) / per) * m.cell.cy);
    }
    case LVM_SETWORKAREAS: return 0;
    case LVM_GETWORKAREAS: return 0;
    case LVM_GETNUMBEROFWORKAREAS:
      if (lp) *(UINT*)lp = 0;
      return 0;
    case LVM_GETSELECTIONMARK: return lv->mark;
    case LVM_SETSELECTIONMARK: {
      int old = lv->mark;
      lv->mark = (int)lp;
      return old;
    }
    case LVM_SETHOVERTIME: {
      DWORD old = lv->hoverTime;
      lv->hoverTime = (DWORD)lp;
      return old;
    }
    case LVM_GETHOVERTIME: return lv->hoverTime;
    case LVM_SETTOOLTIPS: {
      HWND old = lv->tooltips;
      lv->tooltips = (HWND)wp;
      return (LRESULT)old;
    }
    case LVM_GETTOOLTIPS: return (LRESULT)lv->tooltips;
    case LVM_GETISEARCHSTRINGA: u = false; [[fallthrough]];
    case LVM_GETISEARCHSTRINGW:
      if (GetTickCount() - lv->searchTime > 1000) lv->search.clear();
      if (lp) CopyOut(lv->search, (void*)lp, (int)lv->search.size() + 1, u);
      return (LRESULT)lv->search.size();
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterListView() {
  RegisterSystemClass(WC_LISTVIEWW, ListViewProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL,
                      MAKEINTRESOURCEW(32512));
}

}  // namespace winport
