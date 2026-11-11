// comctl_bars.cpp - the status bar, the toolbar and tooltips.
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

template <class T>
T* StateOf(HWND h) {
  return (T*)GetWindowLongPtrW(h, 0);
}

int TextHeight(HDC dc) {
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  return tm.tmHeight;
}

// Places a bar along its parent's top or bottom edge, as the CCS_ styles say.
void AlignToParent(HWND h, int height) {
  DWORD style = GetWindowLongW(h, GWL_STYLE);
  if (style & CCS_NORESIZE) return;
  // Moving the bar sends it WM_SIZE, which would align it again.
  static thread_local HWND busy = NULL;
  if (busy == h) return;
  struct Guard {
    HWND prev;
    explicit Guard(HWND h) : prev(busy) { busy = h; }
    ~Guard() { busy = prev; }
  } guard(h);
  HWND parent = GetParent(h);
  RECT pr, wr;
  if (!parent) return;
  GetClientRect(parent, &pr);
  GetWindowRect(h, &wr);
  MapWindowPoints(NULL, parent, (LPPOINT)&wr, 2);
  int x = 0, y, cx = pr.right;
  bool bottom = (style & CCS_BOTTOM) == CCS_BOTTOM;
  y = bottom ? pr.bottom - height : 0;
  if (style & CCS_NOPARENTALIGN) {
    x = wr.left;
    y = wr.top;
    cx = wr.right - wr.left;
  } else if (style & CCS_NOMOVEY) {
    y = wr.top;
  }
  SetWindowPos(h, NULL, x, y, cx, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

// =====================================================================
// Tooltips

struct Tool {
  UINT flags = 0;
  HWND hwnd = NULL;
  UINT_PTR id = 0;
  RECT rect = {0, 0, 0, 0};
  HINSTANCE inst = NULL;
  std::wstring text;
  bool callback = false;
  UINT resource = 0;  // a string resource of inst, when lpszText named one
  LPARAM lParam = 0;
};

enum { kTimerShow = 1, kTimerPop = 2, kTimerLeave = 3 };

struct Tip {
  HWND hwnd = NULL;
  std::vector<Tool> tools;
  bool active = true;
  int current = -1;  // the tool under the mouse
  bool shown = false, suppressed = false;
  DWORD hiddenAt = 0;
  int initial = 500, autopop = 5000, reshow = 100;
  int maxWidth = -1;
  RECT margin = {0, 0, 0, 0};
  COLORREF bk = CLR_DEFAULT, fg = CLR_DEFAULT;
  HFONT font = NULL;
  std::wstring text, title;
  bool tracking = false;
  POINT trackPos = {0, 0};
};

bool SameTool(const Tool& t, HWND hwnd, UINT_PTR id) { return t.hwnd == hwnd && t.id == id; }

int FindTool(Tip* tp, HWND hwnd, UINT_PTR id) {
  for (size_t i = 0; i < tp->tools.size(); i++)
    if (SameTool(tp->tools[i], hwnd, id)) return (int)i;
  return -1;
}

// The tool at a point in a window's client coordinates.
int HitTool(Tip* tp, HWND w, POINT pt) {
  for (size_t i = 0; i < tp->tools.size(); i++) {
    const Tool& t = tp->tools[i];
    if (t.flags & TTF_IDISHWND) {
      if ((HWND)t.id == w) return (int)i;
    } else if (t.hwnd == w && PtInRect(&t.rect, pt)) {
      return (int)i;
    }
  }
  return -1;
}

void ToolFromInfo(Tool* t, const TTTOOLINFOW* ti, bool unicode, bool setText) {
  t->flags = ti->uFlags;
  t->hwnd = ti->hwnd;
  t->id = ti->uId;
  t->rect = ti->rect;
  t->inst = ti->hinst;
  if (ti->cbSize >= offsetof(TTTOOLINFOW, lParam) + sizeof(LPARAM)) t->lParam = ti->lParam;
  if (!setText) return;
  t->callback = IsCallback(ti->lpszText, unicode);
  t->resource = 0;
  t->text.clear();
  if (!t->callback && ti->lpszText && IS_INTRESOURCE(ti->lpszText))
    t->resource = (UINT)(UINT_PTR)ti->lpszText;
  else if (!t->callback)
    t->text = TextIn(ti->lpszText, unicode);
}

void InfoFromTool(const Tool& t, TTTOOLINFOW* ti, bool unicode) {
  ti->uFlags = t.flags;
  ti->hwnd = t.hwnd;
  ti->uId = t.id;
  ti->rect = t.rect;
  ti->hinst = t.inst;
  if (ti->cbSize >= offsetof(TTTOOLINFOW, lParam) + sizeof(LPARAM)) ti->lParam = t.lParam;
  if (t.callback) {
    ti->lpszText = unicode ? LPSTR_TEXTCALLBACKW : (LPWSTR)LPSTR_TEXTCALLBACKA;
  } else if (t.resource) {
    ti->lpszText = (LPWSTR)MAKEINTRESOURCEW(t.resource);
  } else if (ti->lpszText) {
    CopyOut(t.text, ti->lpszText, 80, unicode);
  }
}

// A tool's text, asking its window when it is a callback.
std::wstring ToolText(Tip* tp, int i) {
  Tool& t = tp->tools[i];
  if (t.resource) {
    wchar_t buf[512] = L"";
    LoadStringW(t.inst, t.resource, buf, 512);
    return buf;
  }
  if (!t.callback) return t.text;
  bool unicode = NotifyUnicode(tp->hwnd, t.hwnd);
  std::wstring out;
  if (unicode) {
    NMTTDISPINFOW di = {};
    di.hdr.hwndFrom = tp->hwnd;
    di.hdr.idFrom = t.id;
    di.hdr.code = TTN_GETDISPINFOW;
    di.uFlags = t.flags & TTF_IDISHWND;
    di.lParam = t.lParam;
    SendMessageW(t.hwnd, WM_NOTIFY, t.id, (LPARAM)&di);
    if (di.lpszText && IS_INTRESOURCE(di.lpszText)) {
      wchar_t buf[512] = L"";
      LoadStringW(di.hinst, (UINT)(UINT_PTR)di.lpszText, buf, 512);
      out = buf;
    } else if (di.lpszText && di.lpszText != LPSTR_TEXTCALLBACKW) {
      out = di.lpszText;
    } else {
      out = di.szText;
    }
    if (di.uFlags & TTF_DI_SETITEM) {
      t.callback = false;
      t.text = out;
    }
  } else {
    NMTTDISPINFOA di = {};
    di.hdr.hwndFrom = tp->hwnd;
    di.hdr.idFrom = t.id;
    di.hdr.code = TTN_GETDISPINFOA;
    di.uFlags = t.flags & TTF_IDISHWND;
    di.lParam = t.lParam;
    SendMessageW(t.hwnd, WM_NOTIFY, t.id, (LPARAM)&di);
    if (di.lpszText && IS_INTRESOURCE(di.lpszText)) {
      wchar_t buf[512] = L"";
      LoadStringW(di.hinst, (UINT)(UINT_PTR)di.lpszText, buf, 512);
      out = buf;
    } else if (di.lpszText && di.lpszText != LPSTR_TEXTCALLBACKA) {
      out = Widen(di.lpszText);
    } else {
      out = Widen(di.szText);
    }
    if (di.uFlags & TTF_DI_SETITEM) {
      t.callback = false;
      t.text = out;
    }
  }
  return out;
}

UINT TipTextFlags(Tip* tp) {
  UINT f = DT_LEFT | DT_TOP;
  if (GetWindowLongW(tp->hwnd, GWL_STYLE) & TTS_NOPREFIX) f |= DT_NOPREFIX;
  f |= tp->maxWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE;
  return f;
}

const int kTipPad = 2;

SIZE TipSize(Tip* tp, const std::wstring& text) {
  HDC dc = GetDC(tp->hwnd);
  HGDIOBJ old = SelectObject(dc, tp->font ? tp->font : ControlFont());
  RECT r = {0, 0, tp->maxWidth > 0 ? tp->maxWidth : 0, 0};
  std::wstring s = text.empty() ? L" " : text;
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, TipTextFlags(tp) | DT_CALCRECT);
  int titleH = 0, titleW = 0;
  if (!tp->title.empty()) {
    RECT t = {0, 0, 0, 0};
    DrawTextW(dc, tp->title.c_str(), (int)tp->title.size(), &t, DT_SINGLELINE | DT_CALCRECT);
    titleH = t.bottom + 2;
    titleW = t.right;
  }
  SelectObject(dc, old);
  ReleaseDC(tp->hwnd, dc);
  SIZE s2 = {std::max((int)r.right, titleW) + 2 * kTipPad + tp->margin.left + tp->margin.right + 2,
             r.bottom + titleH + 2 * kTipPad + tp->margin.top + tp->margin.bottom + 2};
  return s2;
}

void Hide(Tip* tp) {
  KillTimer(tp->hwnd, kTimerPop);
  KillTimer(tp->hwnd, kTimerLeave);
  if (!tp->shown) return;
  tp->shown = false;
  tp->hiddenAt = GetTickCount();
  ShowWindow(tp->hwnd, SW_HIDE);
  if (tp->current >= 0 && tp->current < (int)tp->tools.size()) {
    Tool& t = tp->tools[tp->current];
    NMHDR nm = {tp->hwnd, t.id, (UINT)TTN_POP};
    SendMessageW(t.hwnd, WM_NOTIFY, t.id, (LPARAM)&nm);
  }
}

void Show(Tip* tp, int i) {
  if (i < 0 || i >= (int)tp->tools.size()) return;
  tp->current = i;
  std::wstring text = ToolText(tp, i);
  if (i >= (int)tp->tools.size()) return;
  Tool& t = tp->tools[i];
  if (text.empty()) {
    Hide(tp);
    return;
  }
  tp->text = text;
  SIZE s = TipSize(tp, text);
  SetWindowPos(tp->hwnd, NULL, 0, 0, s.cx, s.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  NMHDR nm = {tp->hwnd, t.id, (UINT)TTN_SHOW};
  bool placed = SendMessageW(t.hwnd, WM_NOTIFY, t.id, (LPARAM)&nm) != 0;
  if (!placed) {
    POINT p;
    if (tp->tracking) {
      p = tp->trackPos;
      if (!(t.flags & TTF_ABSOLUTE)) {
        p.x += 4;
        p.y += 4;
      }
    } else if (t.flags & TTF_CENTERTIP) {
      RECT r = t.rect;
      if (t.flags & TTF_IDISHWND) GetClientRect((HWND)t.id, &r);
      HWND owner = (t.flags & TTF_IDISHWND) ? (HWND)t.id : t.hwnd;
      MapWindowPoints(owner, NULL, (LPPOINT)&r, 2);
      p.x = (r.left + r.right - s.cx) / 2;
      p.y = r.bottom + 2;
    } else {
      GetCursorPos(&p);
      p.y += GetSystemMetrics(SM_CYCURSOR) * 3 / 4;
    }
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    p.x = std::max(0, std::min((int)p.x, sw - s.cx));
    if (p.y + s.cy > sh) p.y = std::max(0, (int)p.y - s.cy - GetSystemMetrics(SM_CYCURSOR));
    SetWindowPos(tp->hwnd, HWND_TOPMOST, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
  }
  SetWindowPos(tp->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(tp->hwnd, NULL, TRUE);
  tp->shown = true;
  if (!tp->tracking) {
    SetTimer(tp->hwnd, kTimerPop, tp->autopop, NULL);
    SetTimer(tp->hwnd, kTimerLeave, 100, NULL);
  }
}

void Relay(Tip* tp, const MSG* m) {
  if (!m || !tp->active || tp->tracking) return;
  switch (m->message) {
    case WM_MOUSEMOVE: {
      POINT pt = {(short)LOWORD(m->lParam), (short)HIWORD(m->lParam)};
      int hit = HitTool(tp, m->hwnd, pt);
      if (hit == tp->current) return;
      bool was = tp->shown;
      Hide(tp);
      tp->current = hit;
      tp->suppressed = false;
      KillTimer(tp->hwnd, kTimerShow);
      if (hit >= 0) {
        bool quick = was || GetTickCount() - tp->hiddenAt < (DWORD)tp->reshow * 4;
        SetTimer(tp->hwnd, kTimerShow, quick ? tp->reshow : tp->initial, NULL);
        SetTimer(tp->hwnd, kTimerLeave, 100, NULL);
      }
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
      KillTimer(tp->hwnd, kTimerShow);
      Hide(tp);
      tp->suppressed = true;
      break;
  }
}

// Relays the mouse messages of a TTF_SUBCLASS tool's window.
LRESULT CALLBACK RelayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
  if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_NCMOUSEMOVE) {
    MSG m = {h, msg, wp, lp, (DWORD)GetMessageTime(), {0, 0}};
    if (IsWindow((HWND)id)) SendMessageW((HWND)id, TTM_RELAYEVENT, 0, (LPARAM)&m);
  }
  return DefSubclassProc(h, msg, wp, lp);
}

void Unsubclass(Tip* tp, HWND w, int except) {
  if (!w) return;
  for (int i = 0; i < (int)tp->tools.size(); i++)
    if (i != except && (tp->tools[i].flags & TTF_SUBCLASS)) {
      HWND o = (tp->tools[i].flags & TTF_IDISHWND) ? (HWND)tp->tools[i].id : tp->tools[i].hwnd;
      if (o == w) return;
    }
  RemoveWindowSubclass(w, RelayProc, (UINT_PTR)tp->hwnd);
}

LRESULT TipMessage(Tip* tp, UINT msg, WPARAM wp, LPARAM lp) {
  HWND h = tp->hwnd;
  bool unicode = true;
  switch (msg) {
    case TTM_ADDTOOLA:
    case TTM_DELTOOLA:
    case TTM_NEWTOOLRECTA:
    case TTM_GETTOOLINFOA:
    case TTM_SETTOOLINFOA:
    case TTM_GETTEXTA:
    case TTM_UPDATETIPTEXTA:
    case TTM_ENUMTOOLSA:
    case TTM_GETCURRENTTOOLA:
    case TTM_HITTESTA:
    case TTM_SETTITLEA:
      unicode = false;
      break;
  }
  switch (msg) {
    case TTM_ACTIVATE:
      tp->active = wp != 0;
      if (!tp->active) Hide(tp);
      return 0;
    case TTM_ADDTOOLA:
    case TTM_ADDTOOLW: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      if (!ti) return FALSE;
      Tool t;
      ToolFromInfo(&t, ti, unicode, true);
      int at = FindTool(tp, t.hwnd, t.id);
      if (at >= 0)
        tp->tools[at] = t;
      else
        tp->tools.push_back(t);
      if (t.flags & TTF_SUBCLASS) {
        HWND w = (t.flags & TTF_IDISHWND) ? (HWND)t.id : t.hwnd;
        if (w) SetWindowSubclass(w, RelayProc, (UINT_PTR)h, 0);
      }
      return TRUE;
    }
    case TTM_DELTOOLA:
    case TTM_DELTOOLW: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      if (!ti) return 0;
      int at = FindTool(tp, ti->hwnd, ti->uId);
      if (at < 0) return 0;
      if (at == tp->current) {
        Hide(tp);
        tp->current = -1;
      } else if (at < tp->current) {
        tp->current--;
      }
      Tool t = tp->tools[at];
      if (t.flags & TTF_SUBCLASS) Unsubclass(tp, (t.flags & TTF_IDISHWND) ? (HWND)t.id : t.hwnd, at);
      tp->tools.erase(tp->tools.begin() + at);
      return 0;
    }
    case TTM_NEWTOOLRECTA:
    case TTM_NEWTOOLRECTW: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at >= 0) tp->tools[at].rect = ti->rect;
      return 0;
    }
    case TTM_GETTOOLINFOA:
    case TTM_GETTOOLINFOW: {
      TTTOOLINFOW* ti = (TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at < 0) return FALSE;
      InfoFromTool(tp->tools[at], ti, unicode);
      return TRUE;
    }
    case TTM_SETTOOLINFOA:
    case TTM_SETTOOLINFOW: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at >= 0) ToolFromInfo(&tp->tools[at], ti, unicode, true);
      if (at >= 0 && at == tp->current && tp->shown) Show(tp, at);
      return 0;
    }
    case TTM_HITTESTA:
    case TTM_HITTESTW: {
      TTHITTESTINFOW* hi = (TTHITTESTINFOW*)lp;
      if (!hi) return FALSE;
      int at = HitTool(tp, hi->hwnd, hi->pt);
      if (at < 0) return FALSE;
      hi->ti.cbSize = sizeof(TTTOOLINFOW);
      LPWSTR keep = hi->ti.lpszText;
      InfoFromTool(tp->tools[at], &hi->ti, unicode);
      if (!tp->tools[at].callback && !tp->tools[at].resource) hi->ti.lpszText = keep;
      return TRUE;
    }
    case TTM_GETTEXTA:
    case TTM_GETTEXTW: {
      TTTOOLINFOW* ti = (TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at < 0 || !ti->lpszText) return 0;
      CopyOut(ToolText(tp, at), ti->lpszText, wp ? (int)wp : 80, unicode);
      return 0;
    }
    case TTM_UPDATETIPTEXTA:
    case TTM_UPDATETIPTEXTW: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at < 0) return 0;
      Tool& t = tp->tools[at];
      t.inst = ti->hinst;
      t.callback = IsCallback(ti->lpszText, unicode);
      t.resource = (!t.callback && ti->lpszText && IS_INTRESOURCE(ti->lpszText)) ? (UINT)(UINT_PTR)ti->lpszText : 0;
      t.text = (t.callback || t.resource) ? L"" : TextIn(ti->lpszText, unicode);
      if (at == tp->current && tp->shown) Show(tp, at);
      return 0;
    }
    case TTM_GETTOOLCOUNT:
      return (LRESULT)tp->tools.size();
    case TTM_ENUMTOOLSA:
    case TTM_ENUMTOOLSW: {
      TTTOOLINFOW* ti = (TTTOOLINFOW*)lp;
      if (wp >= tp->tools.size()) return FALSE;
      if (ti) InfoFromTool(tp->tools[wp], ti, unicode);
      return TRUE;
    }
    case TTM_GETCURRENTTOOLA:
    case TTM_GETCURRENTTOOLW: {
      if (tp->current < 0 || !tp->shown) return FALSE;
      if (lp) InfoFromTool(tp->tools[tp->current], (TTTOOLINFOW*)lp, unicode);
      return TRUE;
    }
    case TTM_WINDOWFROMPOINT:
      return lp ? (LRESULT)WindowFromPoint(*(POINT*)lp) : 0;
    case TTM_RELAYEVENT:
      Relay(tp, (const MSG*)lp);
      return 0;
    case TTM_SETDELAYTIME: {
      int v = (short)LOWORD(lp);
      switch (wp) {
        case TTDT_AUTOMATIC:
          if (v < 0) v = 500;
          tp->initial = v;
          tp->autopop = v * 10;
          tp->reshow = v / 5;
          break;
        case TTDT_INITIAL: tp->initial = v < 0 ? 500 : v; break;
        case TTDT_AUTOPOP: tp->autopop = v < 0 ? 5000 : v; break;
        case TTDT_RESHOW: tp->reshow = v < 0 ? 100 : v; break;
      }
      return 0;
    }
    case TTM_GETDELAYTIME:
      switch (wp) {
        case TTDT_INITIAL: return tp->initial;
        case TTDT_AUTOPOP: return tp->autopop;
        case TTDT_RESHOW: return tp->reshow;
      }
      return tp->initial;
    case TTM_SETMAXTIPWIDTH: {
      int old = tp->maxWidth;
      tp->maxWidth = (int)lp;
      return old;
    }
    case TTM_GETMAXTIPWIDTH:
      return tp->maxWidth;
    case TTM_SETMARGIN:
      if (lp) tp->margin = *(const RECT*)lp;
      return 0;
    case TTM_GETMARGIN:
      if (lp) *(RECT*)lp = tp->margin;
      return 0;
    case TTM_SETTIPBKCOLOR:
      tp->bk = (COLORREF)wp;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case TTM_SETTIPTEXTCOLOR:
      tp->fg = (COLORREF)wp;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case TTM_GETTIPBKCOLOR:
      return tp->bk == CLR_DEFAULT ? GetSysColor(COLOR_INFOBK) : tp->bk;
    case TTM_GETTIPTEXTCOLOR:
      return tp->fg == CLR_DEFAULT ? GetSysColor(COLOR_INFOTEXT) : tp->fg;
    case TTM_POP:
      KillTimer(h, kTimerShow);
      Hide(tp);
      return 0;
    case TTM_POPUP: {
      POINT p;
      GetCursorPos(&p);
      HWND w = WindowFromPoint(p);
      ScreenToClient(w, &p);
      int at = HitTool(tp, w, p);
      if (at >= 0) Show(tp, at);
      return 0;
    }
    case TTM_UPDATE:
      if (tp->shown && tp->current >= 0) Show(tp, tp->current);
      return 0;
    case TTM_TRACKACTIVATE: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      if (wp && ti) {
        int at = FindTool(tp, ti->hwnd, ti->uId);
        if (at < 0) return 0;
        tp->tracking = true;
        Show(tp, at);
      } else {
        Hide(tp);
        tp->tracking = false;
      }
      return 0;
    }
    case TTM_TRACKPOSITION:
      tp->trackPos = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (tp->tracking && tp->shown) Show(tp, tp->current);
      return 0;
    case TTM_SETTITLEA:
    case TTM_SETTITLEW:
      tp->title = TextIn((const void*)lp, unicode);
      return TRUE;
    case TTM_GETBUBBLESIZE: {
      const TTTOOLINFOW* ti = (const TTTOOLINFOW*)lp;
      int at = ti ? FindTool(tp, ti->hwnd, ti->uId) : -1;
      if (at < 0) return 0;
      SIZE s = TipSize(tp, ToolText(tp, at));
      return MAKELONG(s.cx, s.cy);
    }
    case TTM_ADJUSTRECT: {
      RECT* r = (RECT*)lp;
      if (!r) return FALSE;
      int dl = kTipPad + 1 + tp->margin.left, dt = kTipPad + 1 + tp->margin.top;
      int dr = kTipPad + 1 + tp->margin.right, db = kTipPad + 1 + tp->margin.bottom;
      if (wp) {
        r->left -= dl;
        r->top -= dt;
        r->right += dr;
        r->bottom += db;
      } else {
        r->left += dl;
        r->top += dt;
        r->right -= dr;
        r->bottom -= db;
      }
      return TRUE;
    }
  }
  return -1;
}

LRESULT CALLBACK TipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Tip* tp = StateOf<Tip>(h);
  switch (msg) {
    case WM_NCCREATE: {
      tp = new Tip;
      tp->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)tp);
      // Always a top-most popup with a border.
      SetWindowLongW(h, GWL_STYLE, (GetWindowLongW(h, GWL_STYLE) & ~WS_CHILD) | WS_POPUP | WS_BORDER);
      SetWindowLongW(h, GWL_EXSTYLE, GetWindowLongW(h, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
      break;
    }
    case WM_NCDESTROY:
      if (tp) {
        for (int i = (int)tp->tools.size() - 1; i >= 0; i--)
          if (tp->tools[i].flags & TTF_SUBCLASS)
            RemoveWindowSubclass((tp->tools[i].flags & TTF_IDISHWND) ? (HWND)tp->tools[i].id : tp->tools[i].hwnd,
                                 RelayProc, (UINT_PTR)h);
        delete tp;
        SetWindowLongPtrW(h, 0, 0);
      }
      return DefWindowProcW(h, msg, wp, lp);
  }
  if (!tp) return DefWindowProcW(h, msg, wp, lp);
  if (msg >= WM_USER && msg < WM_USER + 0x100) {
    LRESULT r = TipMessage(tp, msg, wp, lp);
    if (r != -1 || msg == TTM_GETMAXTIPWIDTH) return r;
    return 0;
  }
  switch (msg) {
    case WM_SETFONT:
      tp->font = (HFONT)wp;
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)tp->font;
    case WM_TIMER:
      if (wp == kTimerShow) {
        KillTimer(h, kTimerShow);
        if (tp->current >= 0 && !tp->suppressed) Show(tp, tp->current);
      } else if (wp == kTimerPop) {
        Hide(tp);
        tp->suppressed = true;
      } else if (wp == kTimerLeave) {
        // The mouse left the tool without a message to say so.
        if (tp->current < 0 || tp->current >= (int)tp->tools.size()) {
          KillTimer(h, kTimerLeave);
          break;
        }
        Tool& t = tp->tools[tp->current];
        POINT p;
        GetCursorPos(&p);
        HWND w = (t.flags & TTF_IDISHWND) ? (HWND)t.id : t.hwnd;
        HWND under = WindowFromPoint(p);
        bool inside = under == w || under == h;
        if (inside && !(t.flags & TTF_IDISHWND)) {
          ScreenToClient(w, &p);
          inside = PtInRect(&t.rect, p) != 0;
        }
        if (!inside) {
          KillTimer(h, kTimerShow);
          Hide(tp);
          tp->current = -1;
          KillTimer(h, kTimerLeave);
        }
      }
      return 0;
    case WM_NCHITTEST:
      if (tp->current >= 0 && tp->current < (int)tp->tools.size() &&
          (tp->tools[tp->current].flags & TTF_TRANSPARENT))
        return HTTRANSPARENT;
      break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
      Hide(tp);
      return 0;
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(h, &r);
      HBRUSH b = CreateSolidBrush(tp->bk == CLR_DEFAULT ? GetSysColor(COLOR_INFOBK) : tp->bk);
      FillRect((HDC)wp, &r, b);
      DeleteObject(b);
      return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      RECT r;
      GetClientRect(h, &r);
      HGDIOBJ old = SelectObject(dc, tp->font ? tp->font : ControlFont());
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, tp->fg == CLR_DEFAULT ? GetSysColor(COLOR_INFOTEXT) : tp->fg);
      r.left += kTipPad + tp->margin.left;
      r.top += kTipPad + tp->margin.top;
      r.right -= kTipPad + tp->margin.right;
      r.bottom -= kTipPad + tp->margin.bottom;
      if (!tp->title.empty()) {
        RECT t = r;
        DrawTextW(dc, tp->title.c_str(), (int)tp->title.size(), &t, DT_SINGLELINE | DT_CALCRECT);
        DrawTextW(dc, tp->title.c_str(), (int)tp->title.size(), &t, DT_SINGLELINE);
        r.top = t.bottom + 2;
      }
      DrawTextW(dc, tp->text.c_str(), (int)tp->text.size(), &r, TipTextFlags(tp));
      SelectObject(dc, old);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETTEXT:
      return CopyOut(tp->text, (void*)lp, (int)wp, true);
    case WM_GETTEXTLENGTH:
      return (LRESULT)tp->text.size();
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Status bar

struct Part {
  std::wstring text, tip;
  UINT type = 0;
  LPARAM data = 0;
  HICON icon = NULL;
};

struct Status {
  HWND hwnd = NULL;
  std::vector<int> rights{-1};
  std::vector<Part> parts{Part()};
  Part simplePart;
  bool simple = false;
  int minHeight = 0;
  COLORREF bk = CLR_DEFAULT;
  HFONT font = NULL;
  bool unicodeFormat = true;
};

const int kBorderV = 2, kBetween = 2;

int StatusHeight(Status* s) {
  HDC dc = GetDC(s->hwnd);
  HGDIOBJ old = SelectObject(dc, s->font ? s->font : ControlFont());
  int h = TextHeight(dc);
  SelectObject(dc, old);
  ReleaseDC(s->hwnd, dc);
  h = std::max(h + 2 + 2 * kBorderV, GetSystemMetrics(SM_CYSMICON) + 2 + 2 * kBorderV);
  return std::max(h, s->minHeight + 2 * kBorderV);
}

bool HasGrip(Status* s) {
  if (!(GetWindowLongW(s->hwnd, GWL_STYLE) & SBARS_SIZEGRIP)) return false;
  HWND top = GetAncestor(s->hwnd, GA_ROOT);
  return !top || !IsZoomed(top);
}

bool PartRect(Status* s, int i, RECT* out) {
  RECT c;
  GetClientRect(s->hwnd, &c);
  if (s->simple) {
    if (i != 0 && i != SB_SIMPLEID) return false;
    *out = {0, kBorderV, c.right, c.bottom};
    return true;
  }
  if (i < 0 || i >= (int)s->rights.size()) return false;
  int left = i == 0 ? 0 : (s->rights[i - 1] < 0 ? c.right : s->rights[i - 1] + kBetween);
  int right = s->rights[i] < 0 ? c.right : s->rights[i];
  *out = {left, kBorderV, std::max(left, right), c.bottom};
  return true;
}

void InvalidatePart(Status* s, int i) {
  RECT r;
  if (PartRect(s, i, &r))
    InvalidateRect(s->hwnd, &r, TRUE);
  else
    InvalidateRect(s->hwnd, NULL, TRUE);
}

void PaintPart(Status* s, HDC dc, const Part& p, int index, RECT r) {
  if (p.type & SBT_OWNERDRAW) {
    RECT inner = r;
    if (!(p.type & SBT_NOBORDERS)) InflateRect(&inner, -1, -1);
    DRAWITEMSTRUCT di = {};
    di.CtlID = (UINT)GetDlgCtrlID(s->hwnd);
    di.itemID = (UINT)index;
    di.itemAction = ODA_DRAWENTIRE;
    di.hwndItem = s->hwnd;
    di.hDC = dc;
    di.rcItem = inner;
    di.itemData = (ULONG_PTR)p.data;
    if (!(p.type & SBT_NOBORDERS))
      DrawEdge(dc, &r, (p.type & SBT_POPOUT) ? BDR_RAISEDINNER : BDR_SUNKENOUTER, BF_RECT);
    SendMessageW(GetParent(s->hwnd), WM_DRAWITEM, di.CtlID, (LPARAM)&di);
    return;
  }
  if (p.icon) {
    RECT edge = r;
    if (!(p.type & SBT_NOBORDERS))
      DrawEdge(dc, &edge, (p.type & SBT_POPOUT) ? BDR_RAISEDINNER : BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    DrawIconEx(dc, edge.left + 2, (edge.top + edge.bottom - cy) / 2, p.icon, cx, cy, 0, NULL, DI_NORMAL);
    edge.left += cx + 4;
    DrawStatusTextW(dc, &edge, p.text.c_str(), p.type | SBT_NOBORDERS);
    return;
  }
  DrawStatusTextW(dc, &r, p.text.c_str(), p.type);
}

void PaintStatus(Status* s, HDC dc) {
  RECT c;
  GetClientRect(s->hwnd, &c);
  HBRUSH back = s->bk == CLR_DEFAULT ? GetSysColorBrush(COLOR_BTNFACE) : CreateSolidBrush(s->bk);
  FillRect(dc, &c, back);
  if (s->bk != CLR_DEFAULT) DeleteObject(back);
  HGDIOBJ old = SelectObject(dc, s->font ? s->font : ControlFont());
  SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
  if (s->simple) {
    RECT r;
    PartRect(s, 0, &r);
    PaintPart(s, dc, s->simplePart, SB_SIMPLEID, r);
  } else {
    for (int i = 0; i < (int)s->parts.size(); i++) {
      RECT r;
      if (PartRect(s, i, &r) && r.right > r.left) PaintPart(s, dc, s->parts[i], i, r);
    }
  }
  if (HasGrip(s)) {
    int g = GetSystemMetrics(SM_CXVSCROLL);
    RECT gr = {c.right - g, c.bottom - g, c.right, c.bottom};
    DrawFrameControl(dc, &gr, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
  }
  SelectObject(dc, old);
}

void MouseNotify(Status* s, UINT code, LPARAM lp) {
  NMMOUSE nm = {};
  nm.pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
  nm.dwItemSpec = (DWORD_PTR)-1;
  for (int i = 0; i < (int)s->parts.size(); i++) {
    RECT r;
    if (PartRect(s, i, &r) && PtInRect(&r, nm.pt)) {
      nm.dwItemSpec = s->simple ? SB_SIMPLEID : i;
      break;
    }
  }
  SendNotify(s->hwnd, NULL, code, &nm.hdr);
}

LRESULT CALLBACK StatusProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Status* s = StateOf<Status>(h);
  if (msg == WM_NCCREATE) {
    s = new Status;
    s->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)s);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!s) return DefWindowProcW(h, msg, wp, lp);
  bool unicode = true;
  switch (msg) {
    case WM_CREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs && cs->lpszName && !IS_INTRESOURCE(cs->lpszName)) s->parts[0].text = cs->lpszName;
      AlignToParent(h, StatusHeight(s));
      return 0;
    }
    case WM_NCDESTROY:
      delete s;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_SIZE:
      // The parent passes its own WM_SIZE on.
      if (!(GetWindowLongW(h, GWL_STYLE) & CCS_NORESIZE)) AlignToParent(h, StatusHeight(s));
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintStatus(s, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFONT:
      s->font = (HFONT)wp;
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)s->font;
    case WM_SETTEXT:
      s->parts[0].text = lp ? (const wchar_t*)lp : L"";
      s->parts[0].type = 0;
      InvalidatePart(s, 0);
      return TRUE;
    case WM_GETTEXT:
      return CopyOut(s->parts[0].text, (void*)lp, (int)wp, true);
    case WM_GETTEXTLENGTH:
      return (LRESULT)s->parts[0].text.size();
    case WM_NCHITTEST: {
      if (HasGrip(s)) {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        ScreenToClient(h, &p);
        RECT c;
        GetClientRect(h, &c);
        int g = GetSystemMetrics(SM_CXVSCROLL);
        if (p.x >= c.right - g && p.y >= c.bottom - g) return HTBOTTOMRIGHT;
      }
      break;
    }
    case WM_NCLBUTTONDOWN:
      if (wp == HTBOTTOMRIGHT) {
        HWND top = GetAncestor(h, GA_ROOT);
        if (top) SendMessageW(top, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT, lp);
        return 0;
      }
      break;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTBOTTOMRIGHT) {
        SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32642)));
        return TRUE;
      }
      break;
    case WM_LBUTTONUP: MouseNotify(s, NM_CLICK, lp); return 0;
    case WM_RBUTTONUP: MouseNotify(s, NM_RCLICK, lp); return 0;
    case WM_LBUTTONDBLCLK: MouseNotify(s, NM_DBLCLK, lp); return 0;
    case WM_RBUTTONDBLCLK: MouseNotify(s, NM_RDBLCLK, lp); return 0;

    case SB_SETTEXTA:
      unicode = false;
      // fall through
    case SB_SETTEXTW: {
      int i = LOBYTE(wp);
      Part* p = i == SB_SIMPLEID ? &s->simplePart : (i < (int)s->parts.size() ? &s->parts[i] : NULL);
      if (!p) return FALSE;
      p->type = (UINT)(wp & 0xFF00);
      if (p->type & SBT_OWNERDRAW) {
        p->data = lp;
        p->text.clear();
      } else {
        p->text = TextIn((const void*)lp, unicode);
      }
      if (i == SB_SIMPLEID ? s->simple : !s->simple) InvalidatePart(s, i == SB_SIMPLEID ? 0 : i);
      return TRUE;
    }
    case SB_GETTEXTA:
    case SB_GETTEXTLENGTHA:
      unicode = false;
      // fall through
    case SB_GETTEXTW:
    case SB_GETTEXTLENGTHW: {
      int i = LOBYTE(wp);
      Part* p = i == SB_SIMPLEID ? &s->simplePart : (i < (int)s->parts.size() ? &s->parts[i] : NULL);
      if (!p) return 0;
      if ((p->type & SBT_OWNERDRAW) && (msg == SB_GETTEXTA || msg == SB_GETTEXTW)) return p->data;
      int len = unicode ? (int)p->text.size() : (int)Narrow(p->text.c_str()).size();
      if ((msg == SB_GETTEXTA || msg == SB_GETTEXTW) && lp) CopyOut(p->text, (void*)lp, len + 1, unicode);
      return MAKELONG(len, p->type);
    }
    case SB_SETPARTS: {
      int n = (int)wp;
      const int* r = (const int*)lp;
      if (n <= 0 || n > 256 || !r) return FALSE;
      s->rights.assign(r, r + n);
      s->parts.resize(n);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case SB_GETPARTS: {
      int* r = (int*)lp;
      if (r)
        for (int i = 0; i < (int)wp && i < (int)s->rights.size(); i++) r[i] = s->rights[i];
      return s->simple ? 1 : (LRESULT)s->rights.size();
    }
    case SB_GETBORDERS: {
      int* b = (int*)lp;
      if (!b) return FALSE;
      b[0] = 0;
      b[1] = kBorderV;
      b[2] = kBetween;
      return TRUE;
    }
    case SB_SETMINHEIGHT:
      s->minHeight = (int)wp;
      return 0;
    case SB_SIMPLE:
      if (s->simple != (wp != 0)) {
        s->simple = wp != 0;
        SendNotify(h, NULL, SBN_SIMPLEMODECHANGE);
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;
    case SB_ISSIMPLE:
      return s->simple;
    case SB_GETRECT:
      return lp && PartRect(s, (int)wp, (RECT*)lp);
    case SB_SETICON: {
      int i = (int)wp;
      Part* p = i == -1 || i == SB_SIMPLEID ? &s->simplePart : (i < (int)s->parts.size() ? &s->parts[i] : NULL);
      if (!p) return FALSE;
      p->icon = (HICON)lp;
      InvalidatePart(s, i < 0 ? 0 : i);
      return TRUE;
    }
    case SB_GETICON: {
      int i = (int)wp;
      Part* p = i == -1 || i == SB_SIMPLEID ? &s->simplePart : (i < (int)s->parts.size() ? &s->parts[i] : NULL);
      return p ? (LRESULT)p->icon : 0;
    }
    case SB_SETTIPTEXTA:
    case SB_SETTIPTEXTW:
      if (wp < s->parts.size()) s->parts[wp].tip = TextIn((const void*)lp, msg == SB_SETTIPTEXTW);
      return 0;
    case SB_GETTIPTEXTA:
    case SB_GETTIPTEXTW: {
      int i = LOWORD(wp), n = HIWORD(wp);
      if (i < (int)s->parts.size() && lp) CopyOut(s->parts[i].tip, (void*)lp, n, msg == SB_GETTIPTEXTW);
      return 0;
    }
    case SB_SETBKCOLOR: {
      COLORREF old = s->bk;
      s->bk = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case CCM_SETUNICODEFORMAT: {
      bool old = s->unicodeFormat;
      s->unicodeFormat = wp != 0;
      return old;
    }
    case CCM_GETUNICODEFORMAT:
      return s->unicodeFormat;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Toolbar

struct Button {
  int image = 0;
  int id = 0;
  BYTE state = 0, style = 0;
  DWORD_PTR data = 0;
  INT_PTR string = -1;  // an index into the toolbar's strings, or -1
  std::wstring text;    // the button's own text, given as a string
  bool ownText = false;
  int width = 0;  // from TBIF_SIZE, or a separator's width
  RECT rect = {0, 0, 0, 0};
  bool Separator() const { return (style & TBSTYLE_SEP) != 0; }
};

struct BitmapSource {
  HINSTANCE inst;
  UINT_PTR id;
  int first, count;
};

struct Bar {
  HWND hwnd = NULL;
  std::vector<Button> buttons;
  std::vector<std::wstring> strings;
  std::vector<BitmapSource> sources;
  SIZE bitmap = {16, 15}, button = {23, 22};
  HIMAGELIST own = NULL;  // made for TB_ADDBITMAP
  HIMAGELIST images = NULL, hot = NULL, disabled = NULL;
  HWND notify = NULL;
  HWND tips = NULL;
  DWORD exStyle = 0;
  int rows = 1;
  int hotItem = -1, pressed = -1;
  bool pressedInside = false, mouseTracked = false;
  int indent = 0;
  SIZE padding = {7, 6};
  HFONT font = NULL;
  UINT drawTextFlags = 0, drawTextMask = 0;
  int maxTextRows = 1;
  bool unicodeFormat = true;
  int anchor = 0;
  bool laidOut = false;
};

HWND NotifyTarget(Bar* b) { return b->notify ? b->notify : GetParent(b->hwnd); }

HIMAGELIST Images(Bar* b) { return b->images ? b->images : b->own; }

std::wstring ButtonText(Bar* b, const Button& btn) {
  if (btn.ownText) return btn.text;
  if (btn.string >= 0 && btn.string < (INT_PTR)b->strings.size()) return b->strings[btn.string];
  return L"";
}

bool ShowsText(Bar* b, const Button& btn) {
  if (ButtonText(b, btn).empty()) return false;
  if ((b->exStyle & TBSTYLE_EX_MIXEDBUTTONS) && !(btn.style & BTNS_SHOWTEXT)) return false;
  return true;
}

bool List(Bar* b) { return (GetWindowLongW(b->hwnd, GWL_STYLE) & TBSTYLE_LIST) != 0; }
bool Flat(Bar* b) { return (GetWindowLongW(b->hwnd, GWL_STYLE) & TBSTYLE_FLAT) != 0; }

HFONT BarFont(Bar* b) { return b->font ? b->font : ControlFont(); }

bool DropArrow(Bar* b, const Button& btn) {
  return (btn.style & BTNS_WHOLEDROPDOWN) || ((btn.style & TBSTYLE_DROPDOWN) && (b->exStyle & TBSTYLE_EX_DRAWDDARROWS));
}

const int kArrowWidth = 11;

// Every button's size, then their places in rows.
void Layout(Bar* b) {
  HDC dc = GetDC(b->hwnd);
  HGDIOBJ old = SelectObject(dc, BarFont(b));
  int textH = TextHeight(dc);
  bool anyText = false;
  int maxText = 0;
  for (Button& btn : b->buttons) {
    if (btn.Separator() || !ShowsText(b, btn)) continue;
    std::wstring t = ButtonText(b, btn);
    RECT r = {0, 0, 0, 0};
    DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_SINGLELINE | DT_CALCRECT);
    maxText = std::max(maxText, (int)r.right);
    anyText = true;
  }
  int bw = b->button.cx, bh = b->button.cy;
  if (anyText) {
    if (List(b)) {
      bh = std::max(bh, std::max((int)b->bitmap.cy, textH) + b->padding.cy);
    } else {
      bw = std::max(bw, maxText + b->padding.cx);
      bh = std::max(bh, (int)b->bitmap.cy + textH + b->padding.cy);
    }
  }
  bw = std::max(bw, (int)b->bitmap.cx + b->padding.cx);
  bh = std::max(bh, (int)b->bitmap.cy + b->padding.cy);
  RECT c;
  GetClientRect(b->hwnd, &c);
  bool wrapable = (GetWindowLongW(b->hwnd, GWL_STYLE) & TBSTYLE_WRAPABLE) != 0;
  int x = b->indent, y = 0, rows = 1;
  for (size_t i = 0; i < b->buttons.size(); i++) {
    Button& btn = b->buttons[i];
    if (btn.state & TBSTATE_HIDDEN) {
      btn.rect = {x, y, x, y};
      continue;
    }
    int w;
    if (btn.Separator()) {
      w = btn.width > 0 ? btn.width : (btn.image > 0 ? btn.image : 8);
    } else if (btn.width > 0) {
      w = btn.width;
    } else if ((btn.style & TBSTYLE_AUTOSIZE) || List(b)) {
      std::wstring t = ShowsText(b, btn) ? ButtonText(b, btn) : L"";
      RECT r = {0, 0, 0, 0};
      if (!t.empty()) DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_SINGLELINE | DT_CALCRECT);
      if (List(b))
        w = b->bitmap.cx + (t.empty() ? 0 : r.right + 4) + b->padding.cx;
      else
        w = std::max((int)b->bitmap.cx, (int)r.right) + b->padding.cx;
      if (!(btn.style & TBSTYLE_AUTOSIZE)) w = std::max(w, bw);
    } else {
      w = bw;
    }
    if (DropArrow(b, btn) && !btn.Separator() && btn.width <= 0) w += kArrowWidth;
    if (wrapable && x > b->indent && x + w > c.right && c.right > 0) {
      x = b->indent;
      y += bh;
      rows++;
    }
    btn.rect = {x, y, x + w, y + bh};
    x += w;
    if (btn.state & TBSTATE_WRAP) {
      x = b->indent;
      y += bh;
      if (i + 1 < b->buttons.size()) rows++;
    }
  }
  b->rows = rows;
  SelectObject(dc, old);
  ReleaseDC(b->hwnd, dc);
  b->laidOut = true;
}

void Relayout(Bar* b) {
  Layout(b);
  InvalidateRect(b->hwnd, NULL, TRUE);
}

int Height(Bar* b) {
  if (!b->laidOut) Layout(b);
  int h = 0;
  for (const Button& btn : b->buttons) h = std::max(h, (int)btn.rect.bottom);
  if (!h) h = b->button.cy;
  return h;
}

int IndexOf(Bar* b, int id) {
  for (size_t i = 0; i < b->buttons.size(); i++)
    if (b->buttons[i].id == id) return (int)i;
  return -1;
}

int HitButton(Bar* b, POINT p) {
  if (!b->laidOut) Layout(b);
  for (size_t i = 0; i < b->buttons.size(); i++) {
    const Button& btn = b->buttons[i];
    if (!(btn.state & TBSTATE_HIDDEN) && PtInRect(&btn.rect, p)) return (int)i;
  }
  return -1;
}

void RedrawButton(Bar* b, int i) {
  if (i < 0 || i >= (int)b->buttons.size()) return;
  InvalidateRect(b->hwnd, &b->buttons[i].rect, TRUE);
}

// The images of a bitmap resource or handle, gray as the transparent color.
int AddBitmap(Bar* b, HINSTANCE inst, UINT_PTR id, int count) {
  HBITMAP bmp;
  bool owned = true;
  if (inst == HINST_COMMCTRL) {
    // The standard images are not shipped: blank ones stand in.
    int n = (id == IDB_STD_SMALL_COLOR || id == IDB_STD_LARGE_COLOR) ? 15
            : (id == IDB_VIEW_SMALL_COLOR || id == IDB_VIEW_LARGE_COLOR) ? 12
                                                                           : 5;
    count = n;
    bmp = CreateBitmap(b->bitmap.cx * n, b->bitmap.cy, 1, 32, NULL);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT r = {0, 0, b->bitmap.cx * n, b->bitmap.cy};
    HBRUSH gray = CreateSolidBrush(RGB(192, 192, 192));
    FillRect(dc, &r, gray);
    DeleteObject(gray);
    SelectObject(dc, old);
    DeleteDC(dc);
  } else if (inst) {
    bmp = CreateMappedBitmap(inst, id, 0, NULL, 0);
    if (!bmp) bmp = LoadBitmapW(inst, MAKEINTRESOURCEW(id));
  } else {
    bmp = (HBITMAP)id;
    owned = false;
  }
  if (!bmp) return -1;
  if (!b->own) b->own = ImageList_Create(b->bitmap.cx, b->bitmap.cy, ILC_MASK | ILC_COLOR32, 16, 16);
  int before = ImageList_GetImageCount(b->own);
  int first = ImageList_AddMasked(b->own, bmp, GetSysColor(COLOR_BTNFACE));
  if (owned) DeleteObject(bmp);
  if (first < 0) return -1;
  int added = ImageList_GetImageCount(b->own) - before;
  if (count > 0 && added > count) {
    while (ImageList_GetImageCount(b->own) > before + count) ImageList_Remove(b->own, ImageList_GetImageCount(b->own) - 1);
    added = count;
  }
  b->sources.push_back({inst, id, first, added});
  return first;
}

bool ReplaceBitmap(Bar* b, const TBREPLACEBITMAP* rb) {
  for (BitmapSource& src : b->sources) {
    if (src.inst != rb->hInstOld || src.id != rb->nIDOld) continue;
    HBITMAP bmp = rb->hInstNew ? LoadBitmapW(rb->hInstNew, MAKEINTRESOURCEW(rb->nIDNew)) : (HBITMAP)rb->nIDNew;
    if (!bmp || !b->own) return false;
    // A strip of the new images, put in the old ones' places.
    HIMAGELIST tmp = ImageList_Create(b->bitmap.cx, b->bitmap.cy, ILC_MASK | ILC_COLOR32, 16, 16);
    ImageList_AddMasked(tmp, bmp, GetSysColor(COLOR_BTNFACE));
    if (rb->hInstNew) DeleteObject(bmp);
    int n = std::min(ImageList_GetImageCount(tmp), rb->nButtons > 0 ? rb->nButtons : src.count);
    for (int i = 0; i < n; i++) {
      if (i < src.count)
        ImageList_Copy(b->own, src.first + i, tmp, i, ILCF_MOVE);
    }
    ImageList_Destroy(tmp);
    src.inst = rb->hInstNew;
    src.id = rb->nIDNew;
    InvalidateRect(b->hwnd, NULL, TRUE);
    return true;
  }
  return false;
}

// Draws an image grayed out: its shape in white, then in shadow, one up and left.
void DrawDisabled(HIMAGELIST il, int image, HDC dc, int x, int y) {
  int cx, cy;
  ImageList_GetIconSize(il, &cx, &cy);
  HDC mem = CreateCompatibleDC(dc);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = cx;
  bi.bmiHeader.biHeight = -cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits;
  HBITMAP bm = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ old = SelectObject(mem, bm);
  RECT r = {0, 0, cx, cy};
  FillRect(mem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
  ImageList_Draw(il, image, mem, 0, 0, ILD_TRANSPARENT);
  GdiFlush();
  // Dark pixels of the image make the shape.
  std::vector<bool> shape((size_t)cx * cy);
  DWORD* px = (DWORD*)bits;
  for (int i = 0; i < cx * cy; i++) {
    DWORD p = px[i];
    int lum = ((p >> 16) & 0xFF) * 3 + ((p >> 8) & 0xFF) * 6 + (p & 0xFF);
    shape[i] = lum < 1800;
  }
  COLORREF hi = GetSysColor(COLOR_BTNHIGHLIGHT), sh = GetSysColor(COLOR_BTNSHADOW);
  for (int pass = 0; pass < 2; pass++) {
    COLORREF c = pass ? sh : hi;
    int off = pass ? 0 : 1;
    for (int yy = 0; yy < cy; yy++)
      for (int xx = 0; xx < cx; xx++)
        if (shape[(size_t)yy * cx + xx]) SetPixelV(dc, x + xx + off, y + yy + off, c);
  }
  SelectObject(mem, old);
  DeleteObject(bm);
  DeleteDC(mem);
}

void PaintButton(Bar* b, HDC dc, int i) {
  Button& btn = b->buttons[i];
  RECT r = btn.rect;
  if (btn.state & TBSTATE_HIDDEN || IsRectEmpty(&r)) return;
  bool flat = Flat(b);
  if (btn.Separator()) {
    if (flat && !(btn.state & TBSTATE_WRAP)) {
      int mx = (r.left + r.right) / 2;
      RECT line = {mx - 1, r.top + 2, mx + 1, r.bottom - 2};
      DrawEdge(dc, &line, EDGE_ETCHED, BF_LEFT);
    }
    return;
  }
  bool enabled = (btn.state & TBSTATE_ENABLED) != 0;
  bool checked = (btn.state & TBSTATE_CHECKED) != 0;
  bool pressed = (btn.state & TBSTATE_PRESSED) || (b->pressed == i && b->pressedInside);
  bool indeterminate = (btn.state & TBSTATE_INDETERMINATE) != 0;
  bool hot = b->hotItem == i && enabled;
  bool arrow = DropArrow(b, btn);
  RECT face = r;
  if (arrow && !(btn.style & BTNS_WHOLEDROPDOWN)) face.right -= kArrowWidth;
  if (checked && !pressed) {
    // A checked button's face is dithered with the highlight color.
    WORD pat[8];
    for (int k = 0; k < 8; k++) pat[k] = (k & 1) ? 0x55 : 0xAA;
    HBITMAP pb = CreateBitmap(8, 8, 1, 1, pat);
    HBRUSH br = CreatePatternBrush(pb);
    COLORREF ot = SetTextColor(dc, GetSysColor(COLOR_BTNFACE)), ob = SetBkColor(dc, GetSysColor(COLOR_BTNHIGHLIGHT));
    RECT in = r;
    InflateRect(&in, -1, -1);
    FillRect(dc, &in, br);
    SetTextColor(dc, ot);
    SetBkColor(dc, ob);
    DeleteObject(br);
    DeleteObject(pb);
  }
  if (flat) {
    if (pressed || checked)
      DrawEdge(dc, &face, BDR_SUNKENOUTER, BF_RECT);
    else if (hot)
      DrawEdge(dc, &face, BDR_RAISEDINNER, BF_RECT);
  } else {
    DrawEdge(dc, &face, (pressed || checked) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_SOFT);
  }
  if (arrow) {
    RECT ar = {face.right, r.top, r.right, r.bottom};
    if (btn.style & BTNS_WHOLEDROPDOWN) ar.left = r.right - kArrowWidth;
    if (!(btn.style & BTNS_WHOLEDROPDOWN) && (!flat || hot || pressed))
      DrawEdge(dc, &ar, flat ? BDR_RAISEDINNER : EDGE_RAISED, BF_RECT | BF_SOFT);
    int ax = (ar.left + ar.right) / 2 - 2, ay = (ar.top + ar.bottom) / 2 - 1;
    HBRUSH tb = GetSysColorBrush(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
    for (int k = 0; k < 3; k++) {
      RECT line = {ax + k, ay + k, ax + 5 - k, ay + k + 1};
      FillRect(dc, &line, tb);
    }
  }
  int shift = (pressed || checked) ? 1 : 0;
  HIMAGELIST il = Images(b);
  std::wstring text = ShowsText(b, btn) ? ButtonText(b, btn) : L"";
  int imgX, imgY;
  RECT tr;
  if (List(b)) {
    imgX = face.left + b->padding.cx / 2;
    imgY = (face.top + face.bottom - b->bitmap.cy) / 2;
    tr = {imgX + b->bitmap.cx + 4, face.top, face.right - 2, face.bottom};
  } else {
    imgX = (face.left + face.right - b->bitmap.cx) / 2;
    imgY = face.top + b->padding.cy / 2;
    if (text.empty()) imgY = (face.top + face.bottom - b->bitmap.cy) / 2;
    tr = {face.left + 2, imgY + b->bitmap.cy + 1, face.right - 2, face.bottom - 2};
  }
  imgX += shift;
  imgY += shift;
  OffsetRect(&tr, shift, shift);
  if (il && btn.image >= 0 && btn.image < ImageList_GetImageCount(il) && btn.image != I_IMAGENONE) {
    if (!enabled || indeterminate) {
      if (b->disabled && btn.image < ImageList_GetImageCount(b->disabled))
        ImageList_Draw(b->disabled, btn.image, dc, imgX, imgY, ILD_TRANSPARENT);
      else
        DrawDisabled(il, btn.image, dc, imgX, imgY);
    } else {
      HIMAGELIST use = (hot && b->hot && btn.image < ImageList_GetImageCount(b->hot)) ? b->hot : il;
      ImageList_Draw(use, btn.image, dc, imgX, imgY, ILD_TRANSPARENT);
    }
  }
  if (!text.empty()) {
    SetBkMode(dc, TRANSPARENT);
    UINT f = (List(b) ? DT_LEFT | DT_VCENTER : DT_CENTER | DT_TOP) | DT_SINGLELINE | DT_END_ELLIPSIS;
    if (btn.style & TBSTYLE_NOPREFIX) f |= DT_NOPREFIX;
    f = (f & ~b->drawTextMask) | (b->drawTextFlags & b->drawTextMask);
    if (enabled) {
      SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
      DrawTextW(dc, text.c_str(), (int)text.size(), &tr, f);
    } else {
      RECT t2 = tr;
      OffsetRect(&t2, 1, 1);
      SetTextColor(dc, GetSysColor(COLOR_BTNHIGHLIGHT));
      DrawTextW(dc, text.c_str(), (int)text.size(), &t2, f);
      SetTextColor(dc, GetSysColor(COLOR_BTNSHADOW));
      DrawTextW(dc, text.c_str(), (int)text.size(), &tr, f);
    }
  }
}

void PaintBar(Bar* b, HDC dc) {
  if (!b->laidOut) Layout(b);
  HGDIOBJ old = SelectObject(dc, BarFont(b));
  for (int i = 0; i < (int)b->buttons.size(); i++) PaintButton(b, dc, i);
  SelectObject(dc, old);
}

void ButtonToStruct(const Button& btn, TBBUTTON* out) {
  memset(out, 0, sizeof *out);
  out->iBitmap = btn.image;
  out->idCommand = btn.id;
  out->fsState = btn.state;
  out->fsStyle = btn.style;
  out->dwData = btn.data;
  out->iString = btn.ownText ? (INT_PTR)btn.text.c_str() : btn.string;
}

Button ButtonFromStruct(Bar* b, const TBBUTTON* in, bool unicode) {
  Button btn;
  btn.image = in->iBitmap;
  btn.id = in->idCommand;
  btn.state = in->fsState;
  btn.style = in->fsStyle;
  btn.data = in->dwData;
  if (in->iString >= 0x10000 || in->iString < -1) {
    btn.ownText = true;
    btn.text = TextIn((const void*)in->iString, unicode);
  } else {
    btn.string = in->iString;
  }
  (void)b;
  return btn;
}

void SyncTips(Bar* b) {
  if (!b->tips) return;
  // One tool per button, by command ID, its text asked for.
  while (SendMessageW(b->tips, TTM_GETTOOLCOUNT, 0, 0) > 0) {
    TTTOOLINFOW ti = {sizeof ti};
    if (!SendMessageW(b->tips, TTM_ENUMTOOLSW, 0, (LPARAM)&ti)) break;
    SendMessageW(b->tips, TTM_DELTOOLW, 0, (LPARAM)&ti);
  }
  if (!b->laidOut) Layout(b);
  for (const Button& btn : b->buttons) {
    if (btn.Separator() || (btn.state & TBSTATE_HIDDEN)) continue;
    TTTOOLINFOW ti = {sizeof ti};
    ti.hwnd = b->hwnd;
    ti.uId = (UINT_PTR)btn.id;
    ti.rect = btn.rect;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(b->tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
  }
}

void Changed(Bar* b) {
  Relayout(b);
  SyncTips(b);
}

void SetHot(Bar* b, int i) {
  if (i == b->hotItem) return;
  NMTBHOTITEM nm = {};
  nm.idOld = b->hotItem >= 0 && b->hotItem < (int)b->buttons.size() ? b->buttons[b->hotItem].id : 0;
  nm.idNew = i >= 0 ? b->buttons[i].id : 0;
  nm.dwFlags = (b->hotItem < 0 ? HICF_ENTERING : 0) | (i < 0 ? HICF_LEAVING : 0) | HICF_MOUSE;
  if (SendNotify(b->hwnd, NotifyTarget(b), TBN_HOTITEMCHANGE, &nm.hdr)) return;
  int old = b->hotItem;
  b->hotItem = i;
  RedrawButton(b, old);
  RedrawButton(b, i);
}

void Click(Bar* b, int i) {
  Button& btn = b->buttons[i];
  if (btn.style & TBSTYLE_CHECK) {
    if ((btn.style & TBSTYLE_GROUP) == TBSTYLE_GROUP) {
      // A check group: this one on, the others in its run off.
      int lo = i, hi = i;
      while (lo > 0 && !b->buttons[lo - 1].Separator() && (b->buttons[lo - 1].style & TBSTYLE_CHECKGROUP) == TBSTYLE_CHECKGROUP) lo--;
      while (hi + 1 < (int)b->buttons.size() && !b->buttons[hi + 1].Separator() &&
             (b->buttons[hi + 1].style & TBSTYLE_CHECKGROUP) == TBSTYLE_CHECKGROUP)
        hi++;
      for (int k = lo; k <= hi; k++) {
        bool on = k == i;
        BYTE st = on ? b->buttons[k].state | TBSTATE_CHECKED : b->buttons[k].state & ~TBSTATE_CHECKED;
        if (st != b->buttons[k].state) {
          b->buttons[k].state = st;
          RedrawButton(b, k);
        }
      }
    } else {
      btn.state ^= TBSTATE_CHECKED;
      RedrawButton(b, i);
    }
  }
  SendMessageW(NotifyTarget(b), WM_COMMAND, MAKEWPARAM(btn.id, BN_CLICKED), (LPARAM)b->hwnd);
}

bool OnArrow(Bar* b, int i, POINT p) {
  const Button& btn = b->buttons[i];
  if (btn.style & BTNS_WHOLEDROPDOWN) return true;
  return (btn.style & TBSTYLE_DROPDOWN) && (b->exStyle & TBSTYLE_EX_DRAWDDARROWS) && p.x >= btn.rect.right - kArrowWidth;
}

LRESULT DropDown(Bar* b, int i) {
  NMTOOLBARW nm = {};
  nm.iItem = b->buttons[i].id;
  nm.rcButton = b->buttons[i].rect;
  ButtonToStruct(b->buttons[i], &nm.tbButton);
  b->buttons[i].state |= TBSTATE_PRESSED;
  RedrawButton(b, i);
  UpdateWindow(b->hwnd);
  LRESULT r = SendNotify(b->hwnd, NotifyTarget(b), TBN_DROPDOWN, &nm.hdr);
  if (i < (int)b->buttons.size()) {
    b->buttons[i].state &= ~TBSTATE_PRESSED;
    RedrawButton(b, i);
  }
  return r;
}

void CopyButtonInfo(Bar* b, int i, TBBUTTONINFOW* bi, bool unicode) {
  Button& btn = b->buttons[i];
  if (bi->dwMask & TBIF_COMMAND) bi->idCommand = btn.id;
  if (bi->dwMask & TBIF_IMAGE) bi->iImage = btn.image;
  if (bi->dwMask & TBIF_LPARAM) bi->lParam = btn.data;
  if (bi->dwMask & TBIF_SIZE) bi->cx = (WORD)(btn.rect.right - btn.rect.left);
  if (bi->dwMask & TBIF_STATE) bi->fsState = btn.state;
  if (bi->dwMask & TBIF_STYLE) bi->fsStyle = btn.style;
  if ((bi->dwMask & TBIF_TEXT) && bi->pszText) CopyOut(ButtonText(b, btn), bi->pszText, bi->cchText, unicode);
}

void SetButtonInfo(Bar* b, int i, const TBBUTTONINFOW* bi, bool unicode) {
  Button& btn = b->buttons[i];
  if (bi->dwMask & TBIF_COMMAND) btn.id = bi->idCommand;
  if (bi->dwMask & TBIF_IMAGE) btn.image = bi->iImage;
  if (bi->dwMask & TBIF_LPARAM) btn.data = bi->lParam;
  if (bi->dwMask & TBIF_SIZE) btn.width = bi->cx;
  if (bi->dwMask & TBIF_STATE) btn.state = bi->fsState;
  if (bi->dwMask & TBIF_STYLE) btn.style = bi->fsStyle;
  if (bi->dwMask & TBIF_TEXT) {
    btn.ownText = true;
    btn.text = TextIn(bi->pszText, unicode);
  }
}

// TB_ messages that name a button by command ID: its index, or -1.
int ById(Bar* b, WPARAM id) { return IndexOf(b, (int)id); }

void SetStateBit(Bar* b, int i, BYTE bit, bool on) {
  BYTE st = on ? (b->buttons[i].state | bit) : (b->buttons[i].state & ~bit);
  if (st == b->buttons[i].state) return;
  bool relayout = bit == TBSTATE_HIDDEN || bit == TBSTATE_WRAP;
  b->buttons[i].state = st;
  if (relayout)
    Changed(b);
  else
    RedrawButton(b, i);
}

LRESULT BarMessage(Bar* b, UINT msg, WPARAM wp, LPARAM lp) {
  HWND h = b->hwnd;
  bool unicode = true;
  switch (msg) {
    case TB_ENABLEBUTTON: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_ENABLED, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_CHECKBUTTON: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_CHECKED, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_PRESSBUTTON: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_PRESSED, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_HIDEBUTTON: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_HIDDEN, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_INDETERMINATE: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_INDETERMINATE, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_MARKBUTTON: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      SetStateBit(b, i, TBSTATE_MARKED, LOWORD(lp) != 0);
      return TRUE;
    }
    case TB_ISBUTTONENABLED: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_ENABLED);
    }
    case TB_ISBUTTONCHECKED: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_CHECKED);
    }
    case TB_ISBUTTONPRESSED: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_PRESSED);
    }
    case TB_ISBUTTONHIDDEN: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_HIDDEN);
    }
    case TB_ISBUTTONINDETERMINATE: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_INDETERMINATE);
    }
    case TB_ISBUTTONHIGHLIGHTED: {
      int i = ById(b, wp);
      return i >= 0 && (b->buttons[i].state & TBSTATE_MARKED);
    }
    case TB_SETSTATE: {
      int i = ById(b, wp);
      if (i < 0) return FALSE;
      BYTE old = b->buttons[i].state;
      b->buttons[i].state = (BYTE)LOWORD(lp);
      if ((old ^ b->buttons[i].state) & (TBSTATE_HIDDEN | TBSTATE_WRAP))
        Changed(b);
      else
        RedrawButton(b, i);
      return TRUE;
    }
    case TB_GETSTATE: {
      int i = ById(b, wp);
      return i < 0 ? -1 : b->buttons[i].state;
    }
    case TB_ADDBITMAP: {
      const TBADDBITMAP* ab = (const TBADDBITMAP*)lp;
      if (!ab) return -1;
      int r = AddBitmap(b, ab->hInst, ab->nID, (int)wp);
      InvalidateRect(h, NULL, TRUE);
      return r;
    }
    case TB_REPLACEBITMAP:
      return lp && ReplaceBitmap(b, (const TBREPLACEBITMAP*)lp);
    case TB_LOADIMAGES:
      return AddBitmap(b, (HINSTANCE)lp, wp, 0);
    case TB_ADDBUTTONSA:
      unicode = false;
      // fall through
    case TB_ADDBUTTONSW: {
      const TBBUTTON* in = (const TBBUTTON*)lp;
      if (!in) return FALSE;
      for (UINT k = 0; k < (UINT)wp; k++) b->buttons.push_back(ButtonFromStruct(b, &in[k], unicode));
      Changed(b);
      return TRUE;
    }
    case TB_INSERTBUTTONA:
      unicode = false;
      // fall through
    case TB_INSERTBUTTONW: {
      const TBBUTTON* in = (const TBBUTTON*)lp;
      if (!in) return FALSE;
      int at = (int)wp;
      if (at < 0 || at > (int)b->buttons.size()) at = (int)b->buttons.size();
      b->buttons.insert(b->buttons.begin() + at, ButtonFromStruct(b, in, unicode));
      if (b->hotItem >= at) b->hotItem++;
      Changed(b);
      return TRUE;
    }
    case TB_DELETEBUTTON: {
      int at = (int)wp;
      if (at < 0 || at >= (int)b->buttons.size()) return FALSE;
      TBNOTIFYW nm = {};
      nm.iItem = b->buttons[at].id;
      ButtonToStruct(b->buttons[at], &nm.tbButton);
      SendNotify(h, NotifyTarget(b), TBN_DELETINGBUTTON, &nm.hdr);
      b->buttons.erase(b->buttons.begin() + at);
      if (b->hotItem == at) b->hotItem = -1;
      else if (b->hotItem > at) b->hotItem--;
      if (b->pressed == at) b->pressed = -1;
      Changed(b);
      return TRUE;
    }
    case TB_MOVEBUTTON: {
      int from = (int)wp, to = (int)lp;
      int n = (int)b->buttons.size();
      if (from < 0 || from >= n || to < 0 || to >= n) return FALSE;
      Button btn = b->buttons[from];
      b->buttons.erase(b->buttons.begin() + from);
      b->buttons.insert(b->buttons.begin() + to, btn);
      Changed(b);
      return TRUE;
    }
    case TB_GETBUTTON: {
      int at = (int)wp;
      if (at < 0 || at >= (int)b->buttons.size() || !lp) return FALSE;
      ButtonToStruct(b->buttons[at], (TBBUTTON*)lp);
      return TRUE;
    }
    case TB_BUTTONCOUNT:
      return (LRESULT)b->buttons.size();
    case TB_COMMANDTOINDEX:
      return IndexOf(b, (int)wp);
    case TB_SETCMDID: {
      int at = (int)wp;
      if (at < 0 || at >= (int)b->buttons.size()) return FALSE;
      b->buttons[at].id = (int)lp;
      SyncTips(b);
      return TRUE;
    }
    case TB_GETITEMRECT: {
      int at = (int)wp;
      if (!b->laidOut) Layout(b);
      if (at < 0 || at >= (int)b->buttons.size() || !lp) return FALSE;
      if (b->buttons[at].state & TBSTATE_HIDDEN) return FALSE;
      *(RECT*)lp = b->buttons[at].rect;
      return TRUE;
    }
    case TB_GETRECT: {
      int at = ById(b, wp);
      if (!b->laidOut) Layout(b);
      if (at < 0 || !lp) return FALSE;
      *(RECT*)lp = b->buttons[at].rect;
      return TRUE;
    }
    case TB_HITTEST: {
      if (!lp) return -1;
      POINT p = *(POINT*)lp;
      int at = HitButton(b, p);
      if (at < 0) return -(LRESULT)b->buttons.size() - 1;
      if (b->buttons[at].Separator()) return -at - 1;
      return at;
    }
    case TB_BUTTONSTRUCTSIZE:
      return 0;
    case TB_SETBITMAPSIZE:
      b->bitmap = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (b->bitmap.cx <= 0) b->bitmap.cx = 16;
      if (b->bitmap.cy <= 0) b->bitmap.cy = 15;
      if (b->own && ImageList_GetImageCount(b->own) == 0) ImageList_SetIconSize(b->own, b->bitmap.cx, b->bitmap.cy);
      Changed(b);
      return TRUE;
    case TB_SETBUTTONSIZE:
      b->button = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (b->button.cx <= 0) b->button.cx = 23;
      if (b->button.cy <= 0) b->button.cy = 22;
      Changed(b);
      return TRUE;
    case TB_GETBUTTONSIZE: {
      if (!b->laidOut) Layout(b);
      for (const Button& btn : b->buttons)
        if (!btn.Separator() && !(btn.state & TBSTATE_HIDDEN))
          return MAKELONG(btn.rect.right - btn.rect.left, btn.rect.bottom - btn.rect.top);
      return MAKELONG(b->button.cx, b->button.cy);
    }
    case TB_ADDSTRINGA:
      unicode = false;
      // fall through
    case TB_ADDSTRINGW: {
      int first = (int)b->strings.size();
      if (wp && IS_INTRESOURCE(lp)) {
        // A string resource: its pieces split at its first character.
        wchar_t buf[1024] = L"";
        int n = LoadStringW((HINSTANCE)wp, (UINT)lp, buf, 1024);
        if (n <= 0) return -1;
        wchar_t sep = buf[0];
        std::wstring cur;
        for (int k = 1; k < n; k++) {
          if (buf[k] == sep) {
            b->strings.push_back(cur);
            cur.clear();
          } else {
            cur += buf[k];
          }
        }
        if (!cur.empty()) b->strings.push_back(cur);
      } else if (lp) {
        // Strings one after another, ended by an empty one.
        if (unicode) {
          for (const wchar_t* p = (const wchar_t*)lp; *p; p += wcslen(p) + 1) b->strings.push_back(p);
        } else {
          for (const char* p = (const char*)lp; *p; p += strlen(p) + 1) b->strings.push_back(Widen(p));
        }
      }
      Changed(b);
      return first;
    }
    case TB_GETBUTTONTEXTA:
      unicode = false;
      // fall through
    case TB_GETBUTTONTEXTW: {
      int at = ById(b, wp);
      if (at < 0) return -1;
      std::wstring t = ButtonText(b, b->buttons[at]);
      int len = unicode ? (int)t.size() : (int)Narrow(t.c_str()).size();
      if (lp) CopyOut(t, (void*)lp, len + 1, unicode);
      return len;
    }
    case TB_GETBUTTONINFOA:
      unicode = false;
      // fall through
    case TB_GETBUTTONINFOW: {
      TBBUTTONINFOW* bi = (TBBUTTONINFOW*)lp;
      if (!bi) return -1;
      int at = (bi->dwMask & TBIF_BYINDEX) ? (int)wp : ById(b, wp);
      if (at < 0 || at >= (int)b->buttons.size()) return -1;
      if (!b->laidOut) Layout(b);
      CopyButtonInfo(b, at, bi, unicode);
      return at;
    }
    case TB_SETBUTTONINFOA:
      unicode = false;
      // fall through
    case TB_SETBUTTONINFOW: {
      const TBBUTTONINFOW* bi = (const TBBUTTONINFOW*)lp;
      if (!bi) return FALSE;
      int at = (bi->dwMask & TBIF_BYINDEX) ? (int)wp : ById(b, wp);
      if (at < 0 || at >= (int)b->buttons.size()) return FALSE;
      SetButtonInfo(b, at, bi, unicode);
      Changed(b);
      return TRUE;
    }
    case TB_GETBITMAP: {
      int at = ById(b, wp);
      return at < 0 ? -1 : b->buttons[at].image;
    }
    case TB_CHANGEBITMAP: {
      int at = ById(b, wp);
      if (at < 0) return FALSE;
      b->buttons[at].image = (int)lp;
      RedrawButton(b, at);
      return TRUE;
    }
    case TB_GETBITMAPFLAGS:
      return b->bitmap.cy > 16 ? TBBF_LARGE : 0;
    case TB_AUTOSIZE:
      Layout(b);
      AlignToParent(h, Height(b));
      SyncTips(b);
      return 0;
    case TB_SETROWS: {
      // Wraps the buttons into about the rows asked for.
      int want = std::max(1, (int)LOWORD(wp));
      for (Button& btn : b->buttons) btn.state &= ~TBSTATE_WRAP;
      int n = (int)b->buttons.size(), per = (n + want - 1) / want, k = 0;
      for (int i = 0; i < n; i++)
        if (++k == per && i + 1 < n) {
          b->buttons[i].state |= TBSTATE_WRAP;
          k = 0;
        }
      Changed(b);
      if (lp) {
        RECT* r = (RECT*)lp;
        *r = {0, 0, 0, Height(b)};
        for (const Button& btn : b->buttons) r->right = std::max(r->right, btn.rect.right);
      }
      return 0;
    }
    case TB_GETROWS:
      if (!b->laidOut) Layout(b);
      return b->rows;
    case TB_SETPARENT: {
      HWND old = NotifyTarget(b);
      b->notify = (HWND)wp;
      return (LRESULT)old;
    }
    case TB_GETTOOLTIPS:
      return (LRESULT)b->tips;
    case TB_SETTOOLTIPS:
      b->tips = (HWND)wp;
      SyncTips(b);
      return 0;
    case TB_SETIMAGELIST: {
      HIMAGELIST old = b->images;
      b->images = (HIMAGELIST)lp;
      if (b->images) {
        int cx, cy;
        ImageList_GetIconSize(b->images, &cx, &cy);
        b->bitmap = {cx, cy};
      }
      Changed(b);
      return (LRESULT)old;
    }
    case TB_GETIMAGELIST:
      return (LRESULT)Images(b);
    case TB_SETHOTIMAGELIST: {
      HIMAGELIST old = b->hot;
      b->hot = (HIMAGELIST)lp;
      return (LRESULT)old;
    }
    case TB_GETHOTIMAGELIST:
      return (LRESULT)b->hot;
    case TB_SETDISABLEDIMAGELIST: {
      HIMAGELIST old = b->disabled;
      b->disabled = (HIMAGELIST)lp;
      return (LRESULT)old;
    }
    case TB_GETDISABLEDIMAGELIST:
      return (LRESULT)b->disabled;
    case TB_SETEXTENDEDSTYLE: {
      DWORD old = b->exStyle;
      b->exStyle = (DWORD)lp;
      Changed(b);
      return old;
    }
    case TB_GETEXTENDEDSTYLE:
      return b->exStyle;
    case TB_SETSTYLE:
      SetWindowLongW(h, GWL_STYLE, (LONG)lp);
      Changed(b);
      return 0;
    case TB_GETSTYLE:
      return GetWindowLongW(h, GWL_STYLE);
    case TB_SETINDENT:
      b->indent = (int)wp;
      Changed(b);
      return TRUE;
    case TB_SETPADDING: {
      LRESULT old = MAKELONG(b->padding.cx, b->padding.cy);
      b->padding = {(short)LOWORD(lp), (short)HIWORD(lp)};
      Changed(b);
      return old;
    }
    case TB_GETPADDING:
      return MAKELONG(b->padding.cx, b->padding.cy);
    case TB_GETMAXSIZE: {
      if (!lp) return FALSE;
      if (!b->laidOut) Layout(b);
      SIZE* s = (SIZE*)lp;
      s->cx = s->cy = 0;
      for (const Button& btn : b->buttons) {
        s->cx = std::max(s->cx, btn.rect.right);
        s->cy = std::max(s->cy, btn.rect.bottom);
      }
      return TRUE;
    }
    case TB_GETHOTITEM:
      return b->hotItem;
    case TB_SETHOTITEM: {
      int old = b->hotItem;
      SetHot(b, (int)wp < (int)b->buttons.size() ? (int)wp : -1);
      return old;
    }
    case TB_SETANCHORHIGHLIGHT: {
      int old = b->anchor;
      b->anchor = (int)wp;
      return old;
    }
    case TB_GETANCHORHIGHLIGHT:
      return b->anchor;
    case TB_SETDRAWTEXTFLAGS: {
      UINT old = b->drawTextFlags;
      b->drawTextMask = (UINT)wp;
      b->drawTextFlags = (UINT)lp;
      return old;
    }
    case TB_SETMAXTEXTROWS:
      b->maxTextRows = (int)wp;
      Changed(b);
      return TRUE;
    case TB_GETTEXTROWS:
      return b->maxTextRows;
    case TB_MAPACCELERATORA:
    case TB_MAPACCELERATORW: {
      wchar_t c = (wchar_t)towupper((wint_t)wp);
      for (const Button& btn : b->buttons) {
        std::wstring t = ButtonText(b, btn);
        size_t amp = t.find(L'&');
        if (amp != std::wstring::npos && amp + 1 < t.size() && towupper(t[amp + 1]) == c) {
          if (lp) *(UINT*)lp = (UINT)btn.id;
          return TRUE;
        }
      }
      return FALSE;
    }
    case TB_CUSTOMIZE:
      return 0;
    case TB_SAVERESTOREA:
    case TB_SAVERESTOREW:
      return FALSE;
    case TB_SETINSERTMARK:
    case TB_GETINSERTMARK:
      return 0;
    case CCM_SETUNICODEFORMAT: {
      bool old = b->unicodeFormat;
      b->unicodeFormat = wp != 0;
      return old;
    }
    case CCM_GETUNICODEFORMAT:
      return b->unicodeFormat;
  }
  return -2;  // not a toolbar message
}

LRESULT CALLBACK BarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Bar* b = StateOf<Bar>(h);
  if (msg == WM_NCCREATE) {
    b = new Bar;
    b->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)b);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!b) return DefWindowProcW(h, msg, wp, lp);
  if (msg >= WM_USER && msg < WM_USER + 0x100) {
    LRESULT r = BarMessage(b, msg, wp, lp);
    if (r != -2) return r;
  }
  if (msg == CCM_SETUNICODEFORMAT || msg == CCM_GETUNICODEFORMAT) return BarMessage(b, msg, wp, lp);
  switch (msg) {
    case WM_CREATE:
      if (GetWindowLongW(h, GWL_STYLE) & TBSTYLE_TOOLTIPS) {
        b->tips = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, h, NULL,
                                  (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE), NULL);
        if (b->tips) {
          NMTOOLTIPSCREATED nm = {};
          nm.hwndToolTips = b->tips;
          SendNotify(h, NotifyTarget(b), NM_TOOLTIPSCREATED, &nm.hdr);
        }
      }
      return 0;
    case WM_NCDESTROY:
      if (b->tips && IsWindow(b->tips) && GetWindow(b->tips, GW_OWNER) == h) DestroyWindow(b->tips);
      if (b->own) ImageList_Destroy(b->own);
      delete b;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_NCCALCSIZE:
      if (!(GetWindowLongW(h, GWL_STYLE) & CCS_NODIVIDER) && lp) {
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        ((RECT*)lp)->top += 2;
        return r;
      }
      break;
    case WM_NCPAINT: {
      LRESULT r = DefWindowProcW(h, msg, wp, lp);
      if (!(GetWindowLongW(h, GWL_STYLE) & CCS_NODIVIDER)) {
        HDC dc = GetWindowDC(h);
        RECT wr;
        GetWindowRect(h, &wr);
        RECT line = {0, 0, wr.right - wr.left, 2};
        DrawEdge(dc, &line, EDGE_ETCHED, BF_TOP);
        ReleaseDC(h, dc);
      }
      return r;
    }
    case WM_SIZE:
      if (b->laidOut && (GetWindowLongW(h, GWL_STYLE) & TBSTYLE_WRAPABLE)) Changed(b);
      // A parent's WM_SIZE, passed on: fit its width again.
      AlignToParent(h, Height(b));
      return 0;
    case WM_ERASEBKGND: {
      DWORD style = GetWindowLongW(h, GWL_STYLE);
      if (style & TBSTYLE_TRANSPARENT) {
        // The parent's background shows through.
        HWND parent = GetParent(h);
        POINT o = {0, 0};
        MapWindowPoints(h, parent, &o, 1);
        POINT prev;
        OffsetWindowOrgEx((HDC)wp, o.x, o.y, &prev);
        LRESULT r = SendMessageW(parent, WM_ERASEBKGND, wp, 0);
        SetWindowOrgEx((HDC)wp, prev.x, prev.y, NULL);
        if (r) return 1;
      }
      RECT c;
      GetClientRect(h, &c);
      FillRect((HDC)wp, &c, GetSysColorBrush(COLOR_BTNFACE));
      return 1;
    }
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintBar(b, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFONT:
      b->font = (HFONT)wp;
      Changed(b);
      return 0;
    case WM_GETFONT:
      return (LRESULT)b->font;
    case WM_MOUSEMOVE: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (b->tips) {
        MSG m = {h, msg, wp, lp, (DWORD)GetMessageTime(), {0, 0}};
        SendMessageW(b->tips, TTM_RELAYEVENT, 0, (LPARAM)&m);
      }
      int at = HitButton(b, p);
      if (b->pressed >= 0) {
        bool inside = at == b->pressed;
        if (inside != b->pressedInside) {
          b->pressedInside = inside;
          RedrawButton(b, b->pressed);
        }
        return 0;
      }
      if (Flat(b) || (b->exStyle & TBSTYLE_EX_DRAWDDARROWS)) {
        int hot = (at >= 0 && !b->buttons[at].Separator() && (b->buttons[at].state & TBSTATE_ENABLED)) ? at : -1;
        SetHot(b, hot);
        if (!b->mouseTracked) {
          TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0};
          b->mouseTracked = TrackMouseEvent(&t) != 0;
        }
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      b->mouseTracked = false;
      if (b->pressed < 0) SetHot(b, -1);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (b->tips) {
        MSG m = {h, msg, wp, lp, (DWORD)GetMessageTime(), {0, 0}};
        SendMessageW(b->tips, TTM_RELAYEVENT, 0, (LPARAM)&m);
      }
      int at = HitButton(b, p);
      if (at < 0 || b->buttons[at].Separator() || !(b->buttons[at].state & TBSTATE_ENABLED)) return 0;
      if (OnArrow(b, at, p)) {
        DropDown(b, at);
        return 0;
      }
      b->pressed = at;
      b->pressedInside = true;
      SetCapture(h);
      RedrawButton(b, at);
      return 0;
    }
    case WM_LBUTTONUP: {
      if (b->tips) {
        MSG m = {h, msg, wp, lp, (DWORD)GetMessageTime(), {0, 0}};
        SendMessageW(b->tips, TTM_RELAYEVENT, 0, (LPARAM)&m);
      }
      int at = b->pressed;
      bool inside = b->pressedInside;
      b->pressed = -1;
      b->pressedInside = false;
      if (GetCapture() == h) ReleaseCapture();
      if (at >= 0 && at < (int)b->buttons.size()) {
        RedrawButton(b, at);
        if (inside) Click(b, at);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      if ((HWND)lp != h && b->pressed >= 0) {
        int at = b->pressed;
        b->pressed = -1;
        RedrawButton(b, at);
      }
      return 0;
    case WM_RBUTTONUP: {
      NMMOUSE nm = {};
      nm.pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      int at = HitButton(b, nm.pt);
      nm.dwItemSpec = at >= 0 ? (DWORD_PTR)b->buttons[at].id : (DWORD_PTR)-1;
      if (at >= 0) nm.dwItemData = b->buttons[at].data;
      SendNotify(h, NotifyTarget(b), NM_RCLICK, &nm.hdr);
      return 0;
    }
    case WM_NOTIFY: {
      // The tooltips ask the toolbar; the toolbar asks its owner.
      NMHDR* nm = (NMHDR*)lp;
      if (nm && nm->hwndFrom == b->tips) return SendMessageW(NotifyTarget(b), WM_NOTIFY, wp, lp);
      break;
    }
    case WM_NOTIFYFORMAT:
      if (lp == NF_QUERY) return NotifyUnicode(h, NotifyTarget(b)) ? NFR_UNICODE : NFR_ANSI;
      break;
    case WM_SYSCOLORCHANGE:
      InvalidateRect(h, NULL, TRUE);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterBarClasses() {
  HCURSOR arrow = (HCURSOR)MAKEINTRESOURCEW(32512);
  RegisterSystemClass(STATUSCLASSNAMEW, StatusProc, CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL,
                      (LPCWSTR)arrow);
  RegisterSystemClass(TOOLBARCLASSNAMEW, BarProc, CS_DBLCLKS, sizeof(void*), NULL, (LPCWSTR)arrow);
  RegisterSystemClass(TOOLTIPS_CLASSW, TipProc, CS_SAVEBITS | CS_DBLCLKS, sizeof(void*), NULL, (LPCWSTR)arrow);
}

}  // namespace winport

extern "C" {

HWND WINAPI CreateToolbarEx(HWND parent, DWORD style, UINT id, int bitmaps, HINSTANCE inst, UINT_PTR bmpId,
                            LPCTBBUTTON buttons, int count, int dxButton, int dyButton, int dxBitmap, int dyBitmap,
                            UINT structSize) {
  HWND h = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, style | WS_CHILD, 0, 0, 0, 0, parent, (HMENU)(UINT_PTR)id,
                           (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), NULL);
  if (!h) return NULL;
  SendMessageW(h, TB_BUTTONSTRUCTSIZE, structSize, 0);
  if (dxBitmap && dyBitmap) SendMessageW(h, TB_SETBITMAPSIZE, 0, MAKELONG(dxBitmap, dyBitmap));
  if (dxButton && dyButton) SendMessageW(h, TB_SETBUTTONSIZE, 0, MAKELONG(dxButton, dyButton));
  if (bitmaps > 0) {
    TBADDBITMAP ab = {inst, bmpId};
    SendMessageW(h, TB_ADDBITMAP, bitmaps, (LPARAM)&ab);
  }
  if (buttons && count > 0) SendMessageW(h, TB_ADDBUTTONSW, count, (LPARAM)buttons);
  SendMessageW(h, TB_AUTOSIZE, 0, 0);
  return h;
}

}  // extern "C"
