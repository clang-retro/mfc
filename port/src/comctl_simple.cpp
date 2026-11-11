// comctl_simple.cpp - the smaller common controls: the progress bar,
// the trackbar, the up-down control, the header, the tab control and the
// hot key control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <stdio.h>
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

template <class T>
T* Attach(HWND h) {
  T* s = new T;
  s->hwnd = h;
  SetWindowLongPtrW(h, 0, (LONG_PTR)s);
  return s;
}

template <class T>
void Detach(HWND h, T* s) {
  delete s;
  SetWindowLongPtrW(h, 0, 0);
}

DWORD StyleOf(HWND h) { return (DWORD)GetWindowLongW(h, GWL_STYLE); }

int FontHeight(HWND h, HFONT f) {
  HDC dc = GetDC(h);
  HGDIOBJ old = SelectObject(dc, f ? f : ControlFont());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return tm.tmHeight;
}

HBRUSH ParentBrush(HWND h, HDC dc) {
  HBRUSH b = (HBRUSH)SendMessageW(GetParent(h), WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)h);
  return b ? b : GetSysColorBrush(COLOR_BTNFACE);
}

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

// =====================================================================
// Progress bar

struct Progress {
  HWND hwnd = NULL;
  int lo = 0, hi = 100, pos = 0, step = 10;
  COLORREF bar = CLR_DEFAULT, bk = CLR_DEFAULT;
  int marquee = 0;  // the marquee's offset
};

void PaintProgress(Progress* p, HDC dc) {
  RECT r;
  GetClientRect(p->hwnd, &r);
  DWORD style = StyleOf(p->hwnd);
  FillSolid(dc, r, p->bk == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->bk);
  DrawEdge(dc, &r, BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
  InflateRect(&r, -1, -1);
  bool vertical = (style & PBS_VERTICAL) != 0;
  int len = vertical ? r.bottom - r.top : r.right - r.left;
  int span = std::max(1, p->hi - p->lo);
  int start = 0, fill = (int)((long long)(std::min(std::max(p->pos, p->lo), p->hi) - p->lo) * len / span);
  if (style & PBS_MARQUEE) {
    start = p->marquee % std::max(1, len);
    fill = len / 4;
  }
  COLORREF c = p->bar == CLR_DEFAULT ? GetSysColor(COLOR_HIGHLIGHT) : p->bar;
  bool smooth = (style & PBS_SMOOTH) != 0;
  int thick = vertical ? r.right - r.left : r.bottom - r.top;
  int chunk = smooth ? len : std::max(2, thick * 2 / 3), gap = smooth ? 0 : 2;
  for (int at = start; at < start + fill; at += chunk + gap) {
    int a = at % std::max(1, len), b = std::min(a + chunk, start + fill - (at - a));
    b = std::min(b, len);
    if (b <= a) continue;
    RECT c2 = vertical ? RECT{r.left, r.bottom - b, r.right, r.bottom - a} : RECT{r.left + a, r.top, r.left + b, r.bottom};
    FillSolid(dc, c2, c);
  }
}

void SetProgress(Progress* p, int pos) {
  pos = std::min(std::max(pos, p->lo), p->hi);
  if (pos == p->pos) return;
  p->pos = pos;
  InvalidateRect(p->hwnd, NULL, FALSE);
}

LRESULT CALLBACK ProgressProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Progress* p = StateOf<Progress>(h);
  if (msg == WM_NCCREATE) {
    Attach<Progress>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!p) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      Detach(h, p);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintProgress(p, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_TIMER:
      p->marquee += 4;
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case PBM_SETRANGE: {
      LRESULT old = MAKELONG(p->lo, p->hi);
      p->lo = (short)LOWORD(lp);
      p->hi = (short)HIWORD(lp);
      SetProgress(p, p->pos);
      InvalidateRect(h, NULL, FALSE);
      return old;
    }
    case PBM_SETRANGE32: {
      LRESULT old = MAKELONG(p->lo, p->hi);
      p->lo = (int)wp;
      p->hi = (int)lp;
      SetProgress(p, p->pos);
      InvalidateRect(h, NULL, FALSE);
      return old;
    }
    case PBM_GETRANGE: {
      PBRANGE* r = (PBRANGE*)lp;
      if (r) {
        r->iLow = p->lo;
        r->iHigh = p->hi;
      }
      return wp ? p->lo : p->hi;
    }
    case PBM_SETPOS: {
      int old = p->pos;
      SetProgress(p, (int)wp);
      return old;
    }
    case PBM_DELTAPOS: {
      int old = p->pos;
      SetProgress(p, p->pos + (int)wp);
      return old;
    }
    case PBM_GETPOS:
      return p->pos;
    case PBM_SETSTEP: {
      int old = p->step;
      p->step = (int)wp;
      return old;
    }
    case PBM_STEPIT: {
      int old = p->pos, next = p->pos + p->step;
      // Past the end it starts again from the beginning.
      if (next > p->hi) next = p->lo + (next - p->hi);
      if (next < p->lo) next = p->hi - (p->lo - next);
      SetProgress(p, next);
      return old;
    }
    case PBM_SETBARCOLOR: {
      COLORREF old = p->bar;
      p->bar = (COLORREF)lp;
      InvalidateRect(h, NULL, FALSE);
      return old;
    }
    case PBM_SETBKCOLOR: {
      COLORREF old = p->bk;
      p->bk = (COLORREF)lp;
      InvalidateRect(h, NULL, FALSE);
      return old;
    }
    case PBM_SETMARQUEE:
      if (wp)
        SetTimer(h, 1, lp ? (UINT)lp : 30, NULL);
      else
        KillTimer(h, 1);
      return TRUE;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Trackbar

struct Track {
  HWND hwnd = NULL;
  int lo = 0, hi = 100, pos = 0;
  int line = 1, page = 20;
  int selStart = 0, selEnd = 0;
  int freq = 1;
  std::vector<int> tics;  // the tick marks besides the ends
  int thumbLength = 0;
  HWND buddy[2] = {NULL, NULL};  // left (top) and right (bottom)
  HWND tips = NULL;
  bool dragging = false, paging = false;
  int pageDir = 0;
  int grab = 0;  // where in the thumb the drag began
  bool focus = false;
};

bool Vertical(Track* t) { return (StyleOf(t->hwnd) & TBS_VERT) != 0; }

int ThumbLen(Track* t) {
  if (t->thumbLength > 0 && (StyleOf(t->hwnd) & TBS_FIXEDLENGTH)) return t->thumbLength;
  return 20;
}
const int kThumbWidth = 11;

// The channel the thumb runs in, in client coordinates.
RECT Channel(Track* t) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  DWORD style = StyleOf(t->hwnd);
  int len = ThumbLen(t);
  bool v = Vertical(t);
  int across = v ? c.right : c.bottom;
  int ticks = (style & TBS_NOTICKS) ? 0 : 4;
  int start;  // where the thumb begins across the control
  if (style & TBS_BOTH)
    start = (across - len) / 2;
  else if (style & TBS_TOP)
    start = ticks ? ticks + 4 : 2;
  else
    start = 2;
  int mid = start + len / 2;
  int margin = kThumbWidth / 2 + 4;
  if (v) return RECT{mid - 2, margin, mid + 2, std::max(margin, (int)c.bottom - margin)};
  return RECT{margin, mid - 2, std::max(margin, (int)c.right - margin), mid + 2};
}

int PosToPixel(Track* t, int pos) {
  RECT ch = Channel(t);
  bool v = Vertical(t);
  int a = v ? ch.top : ch.left, b = v ? ch.bottom : ch.right;
  int span = std::max(1, t->hi - t->lo);
  return a + (int)((long long)(pos - t->lo) * (b - a) / span);
}

int PixelToPos(Track* t, int px) {
  RECT ch = Channel(t);
  bool v = Vertical(t);
  int a = v ? ch.top : ch.left, b = v ? ch.bottom : ch.right;
  if (b <= a) return t->lo;
  int span = t->hi - t->lo;
  int pos = t->lo + (int)(((long long)(px - a) * span + (b - a) / 2) / (b - a));
  return std::min(std::max(pos, t->lo), t->hi);
}

RECT Thumb(Track* t) {
  RECT ch = Channel(t);
  int at = PosToPixel(t, t->pos), len = ThumbLen(t);
  if (Vertical(t)) {
    int mid = (ch.left + ch.right) / 2;
    return RECT{mid - len / 2, at - kThumbWidth / 2, mid - len / 2 + len, at - kThumbWidth / 2 + kThumbWidth};
  }
  int mid = (ch.top + ch.bottom) / 2;
  return RECT{at - kThumbWidth / 2, mid - len / 2, at - kThumbWidth / 2 + kThumbWidth, mid - len / 2 + len};
}

std::vector<int> TicPositions(Track* t) {
  std::vector<int> out;
  if (StyleOf(t->hwnd) & TBS_AUTOTICKS)
    for (int p = t->lo + t->freq; p < t->hi; p += std::max(1, t->freq)) out.push_back(p);
  else
    out = t->tics;
  return out;
}

void PaintTrack(Track* t, HDC dc) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  FillRect(dc, &c, ParentBrush(t->hwnd, dc));
  DWORD style = StyleOf(t->hwnd);
  bool v = Vertical(t);
  RECT ch = Channel(t);
  RECT edge = ch;
  DrawEdge(dc, &edge, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
  FillSolid(dc, edge, GetSysColor(COLOR_WINDOW));
  if ((style & TBS_ENABLESELRANGE) && t->selEnd > t->selStart) {
    int a = PosToPixel(t, t->selStart), b = PosToPixel(t, t->selEnd);
    RECT sel = v ? RECT{edge.left, a, edge.right, b} : RECT{a, edge.top, b, edge.bottom};
    FillSolid(dc, sel, GetSysColor(COLOR_HIGHLIGHT));
  }
  RECT th = Thumb(t);
  if (!(style & TBS_NOTICKS)) {
    HBRUSH tb = GetSysColorBrush(COLOR_BTNTEXT);
    std::vector<int> tics = TicPositions(t);
    tics.push_back(t->lo);
    tics.push_back(t->hi);
    for (int p : tics) {
      int at = PosToPixel(t, p), len = (p == t->lo || p == t->hi) ? 4 : 3;
      auto mark = [&](int across) {
        RECT m = v ? RECT{across, at, across + len, at + 1} : RECT{at, across, at + 1, across + len};
        FillRect(dc, &m, tb);
      };
      bool before = (style & TBS_BOTH) || (style & TBS_TOP);
      bool after = (style & TBS_BOTH) || !(style & TBS_TOP);
      if (before) mark((v ? th.left : th.top) - len - 2);
      if (after) mark((v ? th.right : th.bottom) + 2);
    }
  }
  if (!(style & TBS_NOTHUMB)) {
    DrawEdge(dc, &th, EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);
    FillRect(dc, &th, GetSysColorBrush(t->dragging ? COLOR_3DLIGHT : COLOR_BTNFACE));
  }
  if (t->focus && !(GetWindowLongW(t->hwnd, GWL_STYLE) & WS_DISABLED)) {
    RECT f = c;
    DrawFocusRect(dc, &f);
  }
}

void ScrollNotify(Track* t, int code) {
  UINT msg = Vertical(t) ? WM_VSCROLL : WM_HSCROLL;
  WPARAM wp = (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) ? MAKEWPARAM(code, t->pos) : (WPARAM)code;
  SendMessageW(GetParent(t->hwnd), msg, wp, (LPARAM)t->hwnd);
}

void SetTrack(Track* t, int pos, bool redraw) {
  pos = std::min(std::max(pos, t->lo), t->hi);
  if (pos == t->pos) return;
  t->pos = pos;
  if (redraw) InvalidateRect(t->hwnd, NULL, TRUE);
}

void PlaceBuddies(Track* t) {
  RECT r;
  GetWindowRect(t->hwnd, &r);
  HWND parent = GetParent(t->hwnd);
  MapWindowPoints(NULL, parent, (LPPOINT)&r, 2);
  for (int i = 0; i < 2; i++) {
    HWND b = t->buddy[i];
    if (!b || !IsWindow(b)) continue;
    RECT br;
    GetWindowRect(b, &br);
    int w = br.right - br.left, hgt = br.bottom - br.top;
    int x, y;
    if (Vertical(t)) {
      x = (r.left + r.right - w) / 2;
      y = i == 0 ? r.top - hgt : r.bottom;
    } else {
      x = i == 0 ? r.left - w : r.right;
      y = (r.top + r.bottom - hgt) / 2;
    }
    SetWindowPos(b, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void Page(Track* t) {
  RECT th = Thumb(t);
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(t->hwnd, &p);
  int at = Vertical(t) ? p.y : p.x;
  int lo = Vertical(t) ? th.top : th.left, hi = Vertical(t) ? th.bottom : th.right;
  // Stop once the thumb reaches the mouse.
  if (t->pageDir < 0 && at >= lo) return;
  if (t->pageDir > 0 && at < hi) return;
  SetTrack(t, t->pos + t->pageDir * t->page, true);
  ScrollNotify(t, t->pageDir < 0 ? TB_PAGEUP : TB_PAGEDOWN);
}

LRESULT CALLBACK TrackProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Track* t = StateOf<Track>(h);
  if (msg == WM_NCCREATE) {
    Attach<Track>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!t) return DefWindowProcW(h, msg, wp, lp);
  bool redraw = wp != 0;
  switch (msg) {
    case WM_NCDESTROY:
      Detach(h, t);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintTrack(t, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      t->focus = msg == WM_SETFOCUS;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_SIZE:
    case WM_MOVE:
      PlaceBuddies(t);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_LBUTTONDOWN: {
      SetFocus(h);
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      RECT th = Thumb(t);
      SetCapture(h);
      if (PtInRect(&th, p)) {
        t->dragging = true;
        t->grab = Vertical(t) ? p.y - (th.top + th.bottom) / 2 : p.x - (th.left + th.right) / 2;
        InvalidateRect(h, NULL, TRUE);
      } else {
        int at = Vertical(t) ? p.y : p.x, mid = Vertical(t) ? (th.top + th.bottom) / 2 : (th.left + th.right) / 2;
        t->paging = true;
        t->pageDir = at < mid ? -1 : 1;
        Page(t);
        SetTimer(h, 1, 400, NULL);
      }
      return 0;
    }
    case WM_TIMER:
      if (t->paging) {
        SetTimer(h, 1, 100, NULL);
        Page(t);
      }
      return 0;
    case WM_MOUSEMOVE:
      if (t->dragging) {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        int pos = PixelToPos(t, (Vertical(t) ? p.y : p.x) - t->grab);
        if (pos != t->pos) {
          SetTrack(t, pos, true);
          ScrollNotify(t, TB_THUMBTRACK);
        }
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (t->dragging || t->paging) {
        bool drag = t->dragging;
        t->dragging = t->paging = false;
        KillTimer(h, 1);
        if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        if (drag) ScrollNotify(t, TB_THUMBPOSITION);
        ScrollNotify(t, TB_ENDTRACK);
        SendNotify(h, NULL, NM_RELEASEDCAPTURE);
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;
    case WM_KEYDOWN: {
      int code = -1, pos = t->pos;
      switch (wp) {
        case VK_LEFT:
        case VK_UP: code = TB_LINEUP; pos -= t->line; break;
        case VK_RIGHT:
        case VK_DOWN: code = TB_LINEDOWN; pos += t->line; break;
        case VK_PRIOR: code = TB_PAGEUP; pos -= t->page; break;
        case VK_NEXT: code = TB_PAGEDOWN; pos += t->page; break;
        case VK_HOME: code = TB_TOP; pos = t->lo; break;
        case VK_END: code = TB_BOTTOM; pos = t->hi; break;
      }
      if (code < 0) break;
      SetTrack(t, pos, true);
      ScrollNotify(t, code);
      return 0;
    }
    case WM_KEYUP:
      if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT ||
          wp == VK_HOME || wp == VK_END)
        ScrollNotify(t, TB_ENDTRACK);
      return 0;
    case TBM_GETPOS: return t->pos;
    case TBM_GETRANGEMIN: return t->lo;
    case TBM_GETRANGEMAX: return t->hi;
    case TBM_GETTIC: {
      std::vector<int> tics = TicPositions(t);
      return wp < tics.size() ? tics[wp] : -1;
    }
    case TBM_SETTIC:
      if ((int)lp < t->lo || (int)lp > t->hi) return FALSE;
      t->tics.push_back((int)lp);
      std::sort(t->tics.begin(), t->tics.end());
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    case TBM_SETPOS:
      SetTrack(t, (int)lp, redraw);
      return 0;
    case TBM_SETRANGE:
      t->lo = (short)LOWORD(lp);
      t->hi = (short)HIWORD(lp);
      SetTrack(t, t->pos, false);
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETRANGEMIN:
      t->lo = (int)lp;
      SetTrack(t, t->pos, false);
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETRANGEMAX:
      t->hi = (int)lp;
      SetTrack(t, t->pos, false);
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_CLEARTICS:
      t->tics.clear();
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETSEL:
      t->selStart = (short)LOWORD(lp);
      t->selEnd = (short)HIWORD(lp);
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETSELSTART:
      t->selStart = (int)lp;
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETSELEND:
      t->selEnd = (int)lp;
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_GETPTICS: {
      // The ticks the control keeps; the caller does not free them.
      static thread_local std::vector<DWORD> out;
      out.clear();
      for (int p : TicPositions(t)) out.push_back((DWORD)p);
      return out.empty() ? 0 : (LRESULT)out.data();
    }
    case TBM_GETTICPOS: {
      std::vector<int> tics = TicPositions(t);
      return wp < tics.size() ? PosToPixel(t, tics[wp]) : -1;
    }
    case TBM_GETNUMTICS:
      if (StyleOf(h) & TBS_NOTICKS) return 0;
      return (LRESULT)TicPositions(t).size() + 2;
    case TBM_GETSELSTART: return t->selStart;
    case TBM_GETSELEND: return t->selEnd;
    case TBM_CLEARSEL:
      t->selStart = t->selEnd = 0;
      if (redraw) InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETTICFREQ:
      t->freq = std::max(1, (int)wp);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_SETPAGESIZE: {
      int old = t->page;
      t->page = (int)lp;
      return old;
    }
    case TBM_GETPAGESIZE: return t->page;
    case TBM_SETLINESIZE: {
      int old = t->line;
      t->line = (int)lp;
      return old;
    }
    case TBM_GETLINESIZE: return t->line;
    case TBM_GETTHUMBRECT:
      if (lp) *(RECT*)lp = Thumb(t);
      return 0;
    case TBM_GETCHANNELRECT:
      if (lp) {
        // Always as if horizontal, as the original does.
        RECT ch = Channel(t);
        *(RECT*)lp = Vertical(t) ? RECT{ch.top, ch.left, ch.bottom, ch.right} : ch;
      }
      return 0;
    case TBM_SETTHUMBLENGTH:
      t->thumbLength = (int)wp;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case TBM_GETTHUMBLENGTH: return ThumbLen(t);
    case TBM_SETTOOLTIPS: t->tips = (HWND)wp; return 0;
    case TBM_GETTOOLTIPS: return (LRESULT)t->tips;
    case TBM_SETTIPSIDE: return 0;
    case TBM_SETBUDDY: {
      int i = wp ? 0 : 1;
      HWND old = t->buddy[i];
      t->buddy[i] = (HWND)lp;
      PlaceBuddies(t);
      return (LRESULT)old;
    }
    case TBM_GETBUDDY: return (LRESULT)t->buddy[wp ? 0 : 1];
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Up-down control

struct Spin {
  HWND hwnd = NULL;
  HWND buddy = NULL;
  int lo = 100, hi = 0, pos = 0;  // the default range runs backward
  int base = 10;
  std::vector<UDACCEL> accel{{0, 1}, {2, 5}, {5, 20}};
  int pressed = 0;  // 1 for the up arrow, -1 for the down one
  DWORD pressStart = 0;
  bool hovering = false;
};


bool SpinHorz(Spin* s) { return (StyleOf(s->hwnd) & UDS_HORZ) != 0; }

std::wstring FormatPos(Spin* s, int v) {
  wchar_t buf[40];
  if (s->base == 16) {
    swprintf(buf, 40, L"0x%04X", (unsigned)v);
    return buf;
  }
  swprintf(buf, 40, L"%d", v);
  std::wstring t = buf;
  if (!(StyleOf(s->hwnd) & UDS_NOTHOUSANDS)) {
    bool neg = !t.empty() && t[0] == L'-';
    std::wstring digits = neg ? t.substr(1) : t, out;
    for (size_t i = 0; i < digits.size(); i++) {
      if (i && (digits.size() - i) % 3 == 0) out += L',';
      out += digits[i];
    }
    t = (neg ? L"-" : L"") + out;
  }
  return t;
}

// The buddy's number; false if its text is not one.
bool BuddyPos(Spin* s, int* out) {
  if (!s->buddy || !IsWindow(s->buddy)) return false;
  wchar_t buf[64] = L"";
  GetWindowTextW(s->buddy, buf, 64);
  std::wstring t;
  for (wchar_t* p = buf; *p; p++)
    if (*p != L',' && *p != L' ') t += *p;
  if (t.empty()) return false;
  wchar_t* end;
  long v = s->base == 16 ? wcstol(t.c_str(), &end, 16) : wcstol(t.c_str(), &end, 10);
  if (*end) return false;
  *out = (int)v;
  return true;
}

int Clamp(Spin* s, int v) {
  int a = std::min(s->lo, s->hi), b = std::max(s->lo, s->hi);
  return std::min(std::max(v, a), b);
}

void ShowBuddy(Spin* s) {
  if (!(StyleOf(s->hwnd) & UDS_SETBUDDYINT) || !s->buddy || !IsWindow(s->buddy)) return;
  std::wstring t = FormatPos(s, s->pos);
  SetWindowTextW(s->buddy, t.c_str());
}

// Moves by delta arrow steps; true if the position changed.
bool Step(Spin* s, int delta) {
  int cur;
  if ((StyleOf(s->hwnd) & UDS_SETBUDDYINT) && BuddyPos(s, &cur)) s->pos = Clamp(s, cur);
  // Up goes toward the upper limit, whichever way the range runs.
  int dir = s->hi >= s->lo ? 1 : -1;
  NMUPDOWN nm = {};
  nm.iPos = s->pos;
  nm.iDelta = delta * dir;
  if (SendNotify(s->hwnd, NULL, UDN_DELTAPOS, &nm.hdr)) return false;
  int next = s->pos + nm.iDelta;
  int a = std::min(s->lo, s->hi), b = std::max(s->lo, s->hi);
  if (next > b || next < a) {
    if (StyleOf(s->hwnd) & UDS_WRAP)
      next = next > b ? a + (next - b - 1) : b - (a - next - 1);
    else
      next = Clamp(s, next);
  }
  bool changed = next != s->pos;
  s->pos = next;
  ShowBuddy(s);
  SendMessageW(GetParent(s->hwnd), SpinHorz(s) ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (WORD)s->pos),
               (LPARAM)s->hwnd);
  return changed;
}

int AccelStep(Spin* s) {
  DWORD secs = (GetTickCount() - s->pressStart) / 1000;
  int inc = 1;
  for (const UDACCEL& a : s->accel)
    if (secs >= a.nSec) inc = (int)a.nInc;
  return inc;
}

void PlaceOnBuddy(Spin* s) {
  DWORD style = StyleOf(s->hwnd);
  if (!s->buddy || !(style & (UDS_ALIGNLEFT | UDS_ALIGNRIGHT))) return;
  RECT br;
  GetWindowRect(s->buddy, &br);
  HWND parent = GetParent(s->hwnd);
  MapWindowPoints(NULL, parent, (LPPOINT)&br, 2);
  RECT me;
  GetWindowRect(s->hwnd, &me);
  int w = me.right - me.left;
  if (w <= 0) w = GetSystemMetrics(SM_CXVSCROLL);
  // The buddy gives up the room the arrows take.
  int bw = std::max(0, (int)(br.right - br.left) - w + 2);
  if (style & UDS_ALIGNRIGHT) {
    SetWindowPos(s->buddy, NULL, br.left, br.top, bw, br.bottom - br.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(s->hwnd, NULL, br.left + bw - 2, br.top, w, br.bottom - br.top, SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    SetWindowPos(s->buddy, NULL, br.left + w - 2, br.top, bw, br.bottom - br.top, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(s->hwnd, NULL, br.left, br.top, w, br.bottom - br.top, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

// The buddy's arrow keys work the up-down control.
LRESULT CALLBACK BuddyKeys(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
  HWND spin = (HWND)id;
  if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN) && IsWindow(spin)) {
    Spin* s = StateOf<Spin>(spin);
    if (s) {
      Step(s, wp == VK_UP ? 1 : -1);
      return 0;
    }
  }
  if (msg == WM_KEYUP && (wp == VK_UP || wp == VK_DOWN) && IsWindow(spin)) {
    Spin* s = StateOf<Spin>(spin);
    if (s)
      SendMessageW(GetParent(spin), SpinHorz(s) ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, (WORD)s->pos),
                   (LPARAM)spin);
  }
  return DefSubclassProc(h, msg, wp, lp);
}

HWND SetBuddy(Spin* s, HWND buddy) {
  HWND old = s->buddy;
  if (old && IsWindow(old)) RemoveWindowSubclass(old, BuddyKeys, (UINT_PTR)s->hwnd);
  s->buddy = buddy;
  if (buddy && (StyleOf(s->hwnd) & UDS_ARROWKEYS)) SetWindowSubclass(buddy, BuddyKeys, (UINT_PTR)s->hwnd, 0);
  PlaceOnBuddy(s);
  int v;
  if (buddy && (StyleOf(s->hwnd) & UDS_SETBUDDYINT) && BuddyPos(s, &v)) s->pos = Clamp(s, v);
  return old;
}

void PaintSpin(Spin* s, HDC dc) {
  RECT c;
  GetClientRect(s->hwnd, &c);
  bool horz = SpinHorz(s);
  RECT a = c, b = c;
  if (horz) {
    a.right = b.left = (c.left + c.right) / 2;
  } else {
    a.bottom = b.top = (c.top + c.bottom) / 2;
  }
  bool enabled = IsWindowEnabled(s->hwnd) != 0;
  UINT inactive = enabled ? 0 : DFCS_INACTIVE;
  // a: up (or left), b: down (or right).
  DrawFrameControl(dc, &a, DFC_SCROLL, (horz ? DFCS_SCROLLLEFT : DFCS_SCROLLUP) | inactive | (s->pressed == 1 ? DFCS_PUSHED : 0));
  DrawFrameControl(dc, &b, DFC_SCROLL,
                   (horz ? DFCS_SCROLLRIGHT : DFCS_SCROLLDOWN) | inactive | (s->pressed == -1 ? DFCS_PUSHED : 0));
}

LRESULT CALLBACK SpinProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Spin* s = StateOf<Spin>(h);
  if (msg == WM_NCCREATE) {
    Attach<Spin>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!s) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_CREATE:
      if (StyleOf(h) & UDS_AUTOBUDDY) SetBuddy(s, GetWindow(h, GW_HWNDPREV));
      return 0;
    case WM_NCDESTROY:
      if (s->buddy && IsWindow(s->buddy)) RemoveWindowSubclass(s->buddy, BuddyKeys, (UINT_PTR)h);
      Detach(h, s);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintSpin(s, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      RECT c;
      GetClientRect(h, &c);
      bool first = SpinHorz(s) ? p.x < (c.left + c.right) / 2 : p.y < (c.top + c.bottom) / 2;
      // The left arrow of a horizontal one goes down.
      s->pressed = SpinHorz(s) ? (first ? -1 : 1) : (first ? 1 : -1);
      s->pressStart = GetTickCount();
      SetCapture(h);
      if (s->buddy && IsWindow(s->buddy)) SetFocus(s->buddy);
      InvalidateRect(h, NULL, FALSE);
      Step(s, s->pressed);
      SetTimer(h, 1, 400, NULL);
      return 0;
    }
    case WM_TIMER:
      if (s->pressed) {
        SetTimer(h, 1, 100, NULL);
        Step(s, s->pressed * AccelStep(s));
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (s->pressed) {
        s->pressed = 0;
        KillTimer(h, 1);
        if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        InvalidateRect(h, NULL, FALSE);
        SendMessageW(GetParent(h), SpinHorz(s) ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_ENDSCROLL, (WORD)s->pos),
                     (LPARAM)h);
      }
      return 0;
    case WM_ENABLE:
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case UDM_SETRANGE:
      s->hi = (short)LOWORD(lp);
      s->lo = (short)HIWORD(lp);
      return 0;
    case UDM_GETRANGE:
      return MAKELONG(s->hi, s->lo);
    case UDM_SETRANGE32:
      s->lo = (int)wp;
      s->hi = (int)lp;
      return 0;
    case UDM_GETRANGE32:
      if (wp) *(int*)wp = s->lo;
      if (lp) *(int*)lp = s->hi;
      return 0;
    case UDM_SETPOS: {
      int old = s->pos;
      s->pos = Clamp(s, (short)LOWORD(lp));
      ShowBuddy(s);
      return old;
    }
    case UDM_SETPOS32: {
      int old = s->pos;
      s->pos = Clamp(s, (int)lp);
      ShowBuddy(s);
      return old;
    }
    case UDM_GETPOS:
    case UDM_GETPOS32: {
      bool bad = false;
      if (StyleOf(h) & UDS_SETBUDDYINT) {
        int v;
        if (BuddyPos(s, &v))
          s->pos = Clamp(s, v);
        else
          bad = true;
      }
      if (msg == UDM_GETPOS32) {
        if (lp) *(BOOL*)lp = bad;
        return s->pos;
      }
      return MAKELONG(s->pos, bad ? 1 : 0);
    }
    case UDM_SETBUDDY:
      return (LRESULT)SetBuddy(s, (HWND)wp);
    case UDM_GETBUDDY:
      return (LRESULT)s->buddy;
    case UDM_SETACCEL: {
      const UDACCEL* a = (const UDACCEL*)lp;
      if (!a || !wp) return FALSE;
      s->accel.assign(a, a + wp);
      return TRUE;
    }
    case UDM_GETACCEL: {
      UDACCEL* a = (UDACCEL*)lp;
      UINT n = (UINT)std::min<size_t>(wp, s->accel.size());
      if (a)
        for (UINT i = 0; i < n; i++) a[i] = s->accel[i];
      return (LRESULT)s->accel.size();
    }
    case UDM_SETBASE: {
      if (wp != 10 && wp != 16) return 0;
      int old = s->base;
      s->base = (int)wp;
      ShowBuddy(s);
      return old;
    }
    case UDM_GETBASE:
      return s->base;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Header

struct HeadItem {
  int width = 50;
  std::wstring text;
  bool callback = false;
  HBITMAP bitmap = NULL;
  int fmt = HDF_LEFT | HDF_STRING;
  LPARAM lParam = 0;
  int image = -1;
  int order = 0;
};

struct Header {
  HWND hwnd = NULL;
  std::vector<HeadItem> items;
  std::vector<int> order;  // item indexes, left to right
  HIMAGELIST images = NULL;
  HFONT font = NULL;
  int pressed = -1;
  bool pressedInside = false;
  int tracking = -1;  // the item whose divider is dragged
  int trackStart = 0, trackWidth = 0;
  int hot = -1;
  int hotDivider = -1;
  bool unicodeNotify = true;
};

std::vector<RECT> ItemRects(Header* hd) {
  RECT c;
  GetClientRect(hd->hwnd, &c);
  std::vector<RECT> out(hd->items.size());
  int x = 0;
  for (int idx : hd->order) {
    int w = std::max(0, hd->items[idx].width);
    out[idx] = {x, 0, x + w, c.bottom};
    x += w;
  }
  return out;
}

// The item under a point, and whether it is on that item's right divider.
int HeadHit(Header* hd, POINT p, UINT* flags) {
  RECT c;
  GetClientRect(hd->hwnd, &c);
  *flags = 0;
  if (!PtInRect(&c, p)) {
    *flags = p.x < c.left ? HHT_TOLEFT : p.x >= c.right ? HHT_TORIGHT : p.y < c.top ? HHT_ABOVE : HHT_BELOW;
    return -1;
  }
  std::vector<RECT> rs = ItemRects(hd);
  // Dividers first: the right edge of an item, near enough.
  for (int k = (int)hd->order.size() - 1; k >= 0; k--) {
    int idx = hd->order[k];
    int edge = rs[idx].right;
    if (p.x >= edge - 4 && p.x < edge + 4) {
      *flags = HHT_ONDIVIDER;
      return idx;
    }
  }
  for (size_t i = 0; i < rs.size(); i++)
    if (PtInRect(&rs[i], p)) {
      *flags = HHT_ONHEADER;
      return (int)i;
    }
  *flags = HHT_NOWHERE;
  return -1;
}

void HeadItemOut(Header* hd, int i, HDITEMW* out, bool unicode) {
  const HeadItem& it = hd->items[i];
  UINT m = out->mask;
  if (m & HDI_WIDTH) out->cxy = it.width;
  if (m & HDI_FORMAT) out->fmt = it.fmt;
  if (m & HDI_LPARAM) out->lParam = it.lParam;
  if (m & HDI_BITMAP) out->hbm = it.bitmap;
  if (m & HDI_IMAGE) out->iImage = it.image;
  if (m & HDI_ORDER) {
    out->iOrder = 0;
    for (size_t k = 0; k < hd->order.size(); k++)
      if (hd->order[k] == i) out->iOrder = (int)k;
  }
  if (m & HDI_TEXT) {
    if (it.callback)
      out->pszText = unicode ? LPSTR_TEXTCALLBACKW : (LPWSTR)LPSTR_TEXTCALLBACKA;
    else if (out->pszText)
      CopyOut(it.text, out->pszText, out->cchTextMax, unicode);
  }
}

void HeadItemIn(Header* hd, HeadItem* it, const HDITEMW* in, bool unicode) {
  (void)hd;
  UINT m = in->mask;
  if (m & HDI_WIDTH) it->width = in->cxy;
  if (m & HDI_FORMAT) it->fmt = in->fmt;
  if (m & HDI_LPARAM) it->lParam = in->lParam;
  if (m & HDI_BITMAP) it->bitmap = in->hbm;
  if (m & HDI_IMAGE) it->image = in->iImage;
  if (m & HDI_TEXT) {
    it->callback = IsCallback(in->pszText, unicode);
    it->text = it->callback ? L"" : TextIn(in->pszText, unicode);
  }
}

// Sends an HDN_ notification about an item, in the parent's character set.
LRESULT HeadNotify(Header* hd, UINT codeW, UINT codeA, int item, int button, HDITEMW* pitem) {
  NMHEADERW nm = {};
  nm.iItem = item;
  nm.iButton = button;
  nm.pitem = pitem;
  return SendNotify(hd->hwnd, NULL, hd->unicodeNotify ? codeW : codeA, &nm.hdr);
}

std::wstring HeadText(Header* hd, int i) {
  HeadItem& it = hd->items[i];
  if (!it.callback) return it.text;
  NMHDDISPINFOW di = {};
  wchar_t buf[260] = L"";
  di.iItem = i;
  di.mask = HDI_TEXT;
  di.lParam = it.lParam;
  if (hd->unicodeNotify) {
    di.pszText = buf;
    di.cchTextMax = 260;
    SendNotify(hd->hwnd, NULL, HDN_GETDISPINFOW, &di.hdr);
    return di.pszText ? std::wstring(di.pszText) : L"";
  }
  char abuf[260] = "";
  di.pszText = (LPWSTR)abuf;
  di.cchTextMax = 260;
  SendNotify(hd->hwnd, NULL, HDN_GETDISPINFOA, &di.hdr);
  return di.pszText ? Widen((const char*)di.pszText) : L"";
}

void PaintHeader(Header* hd, HDC dc) {
  RECT c;
  GetClientRect(hd->hwnd, &c);
  FillRect(dc, &c, GetSysColorBrush(COLOR_BTNFACE));
  HGDIOBJ old = SelectObject(dc, hd->font ? hd->font : ControlFont());
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
  std::vector<RECT> rs = ItemRects(hd);
  bool buttons = (StyleOf(hd->hwnd) & HDS_BUTTONS) != 0;
  int right = 0;
  for (int idx : hd->order) {
    RECT r = rs[idx];
    right = r.right;
    if (r.right <= r.left) continue;
    HeadItem& it = hd->items[idx];
    bool down = buttons && hd->pressed == idx && hd->pressedInside;
    RECT e = r;
    DrawEdge(dc, &e, down ? BDR_SUNKENOUTER : EDGE_RAISED, BF_RECT | BF_SOFT | BF_ADJUST);
    if (it.fmt & HDF_OWNERDRAW) {
      DRAWITEMSTRUCT di = {};
      di.CtlType = ODT_HEADER;
      di.CtlID = (UINT)GetDlgCtrlID(hd->hwnd);
      di.itemID = (UINT)idx;
      di.itemAction = ODA_DRAWENTIRE;
      di.itemState = down ? ODS_SELECTED : 0;
      di.hwndItem = hd->hwnd;
      di.hDC = dc;
      di.rcItem = r;
      di.itemData = (ULONG_PTR)it.lParam;
      SendMessageW(GetParent(hd->hwnd), WM_DRAWITEM, di.CtlID, (LPARAM)&di);
      continue;
    }
    RECT in = e;
    InflateRect(&in, -4, 0);
    if (down) OffsetRect(&in, 1, 1);
    int cx, cy;
    if ((it.fmt & HDF_IMAGE) && hd->images && it.image >= 0 && ImageList_GetIconSize(hd->images, &cx, &cy)) {
      ImageList_Draw(hd->images, it.image, dc, in.left, (in.top + in.bottom - cy) / 2, ILD_TRANSPARENT);
      in.left += cx + 4;
    }
    if ((it.fmt & HDF_BITMAP) && it.bitmap) {
      BITMAP bm;
      if (GetObjectW(it.bitmap, sizeof bm, &bm)) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ ob = SelectObject(mem, it.bitmap);
        int bx = (it.fmt & HDF_BITMAP_ON_RIGHT) ? in.right - bm.bmWidth : in.left;
        BitBlt(dc, bx, (in.top + in.bottom - bm.bmHeight) / 2, bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteDC(mem);
        if (it.fmt & HDF_BITMAP_ON_RIGHT)
          in.right -= bm.bmWidth + 4;
        else
          in.left += bm.bmWidth + 4;
      }
    }
    if (it.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
      // A small triangle at the right.
      int ax = in.right - 8, ay = (in.top + in.bottom) / 2;
      HBRUSH sb = GetSysColorBrush(COLOR_BTNSHADOW);
      for (int k = 0; k < 4; k++) {
        int y = (it.fmt & HDF_SORTUP) ? ay - 2 + k : ay + 1 - k;
        RECT line = {ax + 3 - k, y, ax + 4 + k, y + 1};
        FillRect(dc, &line, sb);
      }
      in.right -= 12;
    }
    std::wstring text = (it.fmt & HDF_STRING) || !(it.fmt & (HDF_BITMAP | HDF_IMAGE)) ? HeadText(hd, idx) : L"";
    if (!text.empty()) {
      UINT align = (it.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT ? DT_RIGHT
                   : (it.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER
                                                               : DT_LEFT;
      DrawTextW(dc, text.c_str(), (int)text.size(), &in, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
  }
  if (right < c.right) {
    RECT rest = {right, 0, c.right, c.bottom};
    DrawEdge(dc, &rest, EDGE_RAISED, BF_RECT | BF_SOFT);
  }
  SelectObject(dc, old);
}

int HeadInsert(Header* hd, int at, const HDITEMW* in, bool unicode) {
  if (!in) return -1;
  if (at < 0 || at > (int)hd->items.size()) at = (int)hd->items.size();
  HeadItem it;
  HeadItemIn(hd, &it, in, unicode);
  hd->items.insert(hd->items.begin() + at, it);
  for (int& o : hd->order)
    if (o >= at) o++;
  int ord = (in->mask & HDI_ORDER) && in->iOrder >= 0 && in->iOrder <= (int)hd->order.size() ? in->iOrder : at;
  ord = std::min(ord, (int)hd->order.size());
  hd->order.insert(hd->order.begin() + ord, at);
  InvalidateRect(hd->hwnd, NULL, TRUE);
  return at;
}

LRESULT CALLBACK HeaderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Header* hd = StateOf<Header>(h);
  if (msg == WM_NCCREATE) {
    Attach<Header>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!hd) return DefWindowProcW(h, msg, wp, lp);
  bool unicode = true;
  switch (msg) {
    case WM_CREATE:
      hd->unicodeNotify = NotifyUnicode(h);
      return 0;
    case WM_NCDESTROY:
      Detach(h, hd);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintHeader(hd, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFONT:
      hd->font = (HFONT)wp;
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)hd->font;
    case WM_SETCURSOR: {
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(h, &p);
      UINT flags;
      HeadHit(hd, p, &flags);
      if (flags & HHT_ONDIVIDER) {
        SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32644)));
        return TRUE;
      }
      break;
    }
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      UINT flags;
      int i = HeadHit(hd, p, &flags);
      if (i < 0) return 0;
      if (flags & HHT_ONDIVIDER) {
        HDITEMW item = {};
        item.mask = HDI_WIDTH;
        item.cxy = hd->items[i].width;
        if (HeadNotify(hd, HDN_BEGINTRACKW, HDN_BEGINTRACKA, i, 0, &item)) return 0;
        hd->tracking = i;
        hd->trackStart = p.x;
        hd->trackWidth = hd->items[i].width;
        SetCapture(h);
      } else if (StyleOf(h) & HDS_BUTTONS) {
        hd->pressed = i;
        hd->pressedInside = true;
        SetCapture(h);
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (hd->tracking >= 0) {
        int w = std::max(0, hd->trackWidth + p.x - hd->trackStart);
        HDITEMW item = {};
        item.mask = HDI_WIDTH;
        item.cxy = w;
        if (HeadNotify(hd, HDN_TRACKW, HDN_TRACKA, hd->tracking, 0, &item)) return 0;
        if (StyleOf(h) & HDS_FULLDRAG) {
          if (HeadNotify(hd, HDN_ITEMCHANGINGW, HDN_ITEMCHANGINGA, hd->tracking, 0, &item)) return 0;
          hd->items[hd->tracking].width = item.cxy;
          InvalidateRect(h, NULL, TRUE);
          HeadNotify(hd, HDN_ITEMCHANGEDW, HDN_ITEMCHANGEDA, hd->tracking, 0, &item);
        } else {
          hd->items[hd->tracking].width = item.cxy;
          InvalidateRect(h, NULL, TRUE);
        }
      } else if (hd->pressed >= 0) {
        UINT flags;
        bool inside = HeadHit(hd, p, &flags) == hd->pressed && !(flags & HHT_ONDIVIDER);
        if (inside != hd->pressedInside) {
          hd->pressedInside = inside;
          InvalidateRect(h, NULL, TRUE);
        }
      }
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (hd->tracking >= 0) {
        int i = hd->tracking;
        hd->tracking = -1;
        if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        HDITEMW item = {};
        item.mask = HDI_WIDTH;
        item.cxy = hd->items[i].width;
        HeadNotify(hd, HDN_ENDTRACKW, HDN_ENDTRACKA, i, 0, &item);
        if (!(StyleOf(h) & HDS_FULLDRAG)) {
          if (!HeadNotify(hd, HDN_ITEMCHANGINGW, HDN_ITEMCHANGINGA, i, 0, &item)) {
            hd->items[i].width = item.cxy;
            HeadNotify(hd, HDN_ITEMCHANGEDW, HDN_ITEMCHANGEDA, i, 0, &item);
          } else {
            hd->items[i].width = hd->trackWidth;
          }
        }
        InvalidateRect(h, NULL, TRUE);
      } else if (hd->pressed >= 0) {
        int i = hd->pressed;
        bool inside = hd->pressedInside;
        hd->pressed = -1;
        if (msg == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        InvalidateRect(h, NULL, TRUE);
        if (inside && msg == WM_LBUTTONUP) HeadNotify(hd, HDN_ITEMCLICKW, HDN_ITEMCLICKA, i, 0, NULL);
      }
      return 0;
    case WM_LBUTTONDBLCLK: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      UINT flags;
      int i = HeadHit(hd, p, &flags);
      if (i >= 0 && (flags & HHT_ONDIVIDER))
        HeadNotify(hd, HDN_DIVIDERDBLCLICKW, HDN_DIVIDERDBLCLICKA, i, 0, NULL);
      else if (i >= 0)
        HeadNotify(hd, HDN_ITEMDBLCLICKW, HDN_ITEMDBLCLICKA, i, 0, NULL);
      return 0;
    }
    case WM_RBUTTONUP:
      SendNotify(h, NULL, NM_RCLICK);
      return 0;
    case HDM_GETITEMCOUNT:
      return (LRESULT)hd->items.size();
    case HDM_INSERTITEMA:
      unicode = false;
      // fall through
    case HDM_INSERTITEMW:
      return HeadInsert(hd, (int)wp, (const HDITEMW*)lp, unicode);
    case HDM_DELETEITEM: {
      int i = (int)wp;
      if (i < 0 || i >= (int)hd->items.size()) return FALSE;
      hd->items.erase(hd->items.begin() + i);
      hd->order.erase(std::remove(hd->order.begin(), hd->order.end(), i), hd->order.end());
      for (int& o : hd->order)
        if (o > i) o--;
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case HDM_GETITEMA:
      unicode = false;
      // fall through
    case HDM_GETITEMW: {
      int i = (int)wp;
      if (i < 0 || i >= (int)hd->items.size() || !lp) return FALSE;
      HeadItemOut(hd, i, (HDITEMW*)lp, unicode);
      return TRUE;
    }
    case HDM_SETITEMA:
      unicode = false;
      // fall through
    case HDM_SETITEMW: {
      int i = (int)wp;
      HDITEMW* in = (HDITEMW*)lp;
      if (i < 0 || i >= (int)hd->items.size() || !in) return FALSE;
      if (HeadNotify(hd, HDN_ITEMCHANGINGW, HDN_ITEMCHANGINGA, i, 0, in)) return FALSE;
      HeadItemIn(hd, &hd->items[i], in, unicode);
      if ((in->mask & HDI_ORDER) && in->iOrder >= 0 && in->iOrder < (int)hd->order.size()) {
        hd->order.erase(std::remove(hd->order.begin(), hd->order.end(), i), hd->order.end());
        hd->order.insert(hd->order.begin() + in->iOrder, i);
      }
      InvalidateRect(h, NULL, TRUE);
      HeadNotify(hd, HDN_ITEMCHANGEDW, HDN_ITEMCHANGEDA, i, 0, in);
      return TRUE;
    }
    case HDM_LAYOUT: {
      HDLAYOUT* l = (HDLAYOUT*)lp;
      if (!l || !l->prc || !l->pwpos) return FALSE;
      int height = (StyleOf(h) & HDS_HIDDEN) ? 0 : FontHeight(h, hd->font) + 7;
      l->pwpos->hwnd = h;
      l->pwpos->hwndInsertAfter = NULL;
      l->pwpos->x = l->prc->left;
      l->pwpos->y = l->prc->top;
      l->pwpos->cx = l->prc->right - l->prc->left;
      l->pwpos->cy = height;
      l->pwpos->flags = SWP_NOZORDER | SWP_NOACTIVATE;
      l->prc->top += height;
      return TRUE;
    }
    case HDM_HITTEST: {
      HDHITTESTINFO* hi = (HDHITTESTINFO*)lp;
      if (!hi) return -1;
      hi->iItem = HeadHit(hd, hi->pt, &hi->flags);
      return hi->iItem;
    }
    case HDM_GETITEMRECT: {
      int i = (int)wp;
      if (i < 0 || i >= (int)hd->items.size() || !lp) return FALSE;
      *(RECT*)lp = ItemRects(hd)[i];
      return TRUE;
    }
    case HDM_SETIMAGELIST: {
      HIMAGELIST old = hd->images;
      hd->images = (HIMAGELIST)lp;
      InvalidateRect(h, NULL, TRUE);
      return (LRESULT)old;
    }
    case HDM_GETIMAGELIST:
      return (LRESULT)hd->images;
    case HDM_ORDERTOINDEX:
      return wp < hd->order.size() ? hd->order[wp] : (LRESULT)wp;
    case HDM_GETORDERARRAY: {
      int* out = (int*)lp;
      if (!out || wp < hd->order.size()) return FALSE;
      for (size_t k = 0; k < hd->order.size(); k++) out[k] = hd->order[k];
      return TRUE;
    }
    case HDM_SETORDERARRAY: {
      const int* in = (const int*)lp;
      if (!in || wp != hd->order.size()) return FALSE;
      hd->order.assign(in, in + wp);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case HDM_SETHOTDIVIDER: {
      int old = hd->hotDivider;
      hd->hotDivider = (int)lp;
      return old;
    }
    case HDM_CREATEDRAGIMAGE:
      return 0;
    case HDM_SETUNICODEFORMAT: {
      bool old = hd->unicodeNotify;
      hd->unicodeNotify = wp != 0;
      return old;
    }
    case HDM_GETUNICODEFORMAT:
      return hd->unicodeNotify;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Tab control

struct TabItem {
  std::wstring text;
  int image = -1;
  DWORD state = 0;
  std::vector<BYTE> extra;  // TCM_SETITEMEXTRA bytes, the lParam by default
  RECT rect = {0, 0, 0, 0};
  int row = 0;
};

struct Tabs {
  HWND hwnd = NULL;
  std::vector<TabItem> items;
  int sel = -1, focus = -1;
  int extra = sizeof(LPARAM);
  HIMAGELIST images = NULL;
  HFONT font = NULL;
  HWND tips = NULL;
  SIZE fixed = {0, 0};
  SIZE padding = {6, 3};
  int minWidth = -1;
  int first = 0;  // the first tab shown, when they do not fit in one row
  int rows = 1;
  bool focused = false;
  DWORD exStyle = 0;
  bool laidOut = false;
};

int TabHeight(Tabs* t) {
  if (t->fixed.cy > 0) return t->fixed.cy;
  int h = FontHeight(t->hwnd, t->font) + 2 * t->padding.cy;
  int cx, cy;
  if (t->images && ImageList_GetIconSize(t->images, &cx, &cy)) h = std::max(h, cy + 2 * t->padding.cy);
  return h;
}

bool Multi(Tabs* t) { return (StyleOf(t->hwnd) & TCS_MULTILINE) != 0; }
bool Bottom(Tabs* t) { return (StyleOf(t->hwnd) & TCS_BOTTOM) != 0; }
bool Buttons(Tabs* t) { return (StyleOf(t->hwnd) & TCS_BUTTONS) != 0; }

void LayoutTabs(Tabs* t) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  HDC dc = GetDC(t->hwnd);
  HGDIOBJ old = SelectObject(dc, t->font ? t->font : ControlFont());
  int th = TabHeight(t), x = 2, row = 0;
  int imgW = 0, cy;
  if (t->images) ImageList_GetIconSize(t->images, &imgW, &cy);
  int limit = c.right - 2;
  std::vector<int> widths;
  for (TabItem& it : t->items) {
    int w;
    if (t->fixed.cx > 0 && (StyleOf(t->hwnd) & TCS_FIXEDWIDTH)) {
      w = t->fixed.cx;
    } else {
      RECT r = {0, 0, 0, 0};
      if (!it.text.empty()) DrawTextW(dc, it.text.c_str(), (int)it.text.size(), &r, DT_SINGLELINE | DT_CALCRECT);
      w = r.right + 2 * t->padding.cx + (it.image >= 0 && t->images ? imgW + 3 : 0);
      w = std::max(w, t->minWidth >= 0 ? t->minWidth : 42);
    }
    widths.push_back(w);
  }
  // Multiline: rows that wrap; single line: one row from the first shown tab.
  int start = Multi(t) ? 0 : std::min(t->first, std::max(0, (int)t->items.size() - 1));
  for (int i = 0; i < (int)t->items.size(); i++) {
    TabItem& it = t->items[i];
    if (!Multi(t) && i < start) {
      it.rect = {0, 0, 0, 0};
      it.row = -1;
      continue;
    }
    int w = widths[i];
    if (Multi(t) && x > 2 && x + w > limit) {
      x = 2;
      row++;
    }
    it.row = row;
    it.rect = {x, 0, x + w, th};
    x += w + (Buttons(t) ? 3 : 0);
  }
  t->rows = row + 1;
  // The selected tab's row sits next to the body; rows count from it outward.
  int selRow = t->sel >= 0 && t->sel < (int)t->items.size() ? t->items[t->sel].row : 0;
  for (TabItem& it : t->items) {
    if (it.row < 0) continue;
    int fromBody = Multi(t) ? ((it.row - selRow + t->rows) % t->rows) : 0;
    int y = Bottom(t) ? c.bottom - th * (fromBody + 1) - 2 : (t->rows - 1 - fromBody) * th + 2;
    it.rect.top = y;
    it.rect.bottom = y + th;
  }
  SelectObject(dc, old);
  ReleaseDC(t->hwnd, dc);
  t->laidOut = true;
}

int TabArea(Tabs* t) { return t->rows * TabHeight(t) + 2; }

RECT TabBody(Tabs* t) {
  RECT c;
  GetClientRect(t->hwnd, &c);
  if (Buttons(t)) return c;
  if (!t->laidOut) LayoutTabs(t);
  if (Bottom(t))
    c.bottom -= TabArea(t);
  else
    c.top += TabArea(t);
  return c;
}

void PaintTabs(Tabs* t, HDC dc) {
  if (!t->laidOut) LayoutTabs(t);
  RECT c;
  GetClientRect(t->hwnd, &c);
  FillRect(dc, &c, ParentBrush(t->hwnd, dc));
  HGDIOBJ old = SelectObject(dc, t->font ? t->font : ControlFont());
  SetBkMode(dc, TRANSPARENT);
  bool buttons = Buttons(t);
  if (!buttons) {
    RECT body = TabBody(t);
    DrawEdge(dc, &body, EDGE_RAISED, BF_RECT | BF_SOFT);
  }
  for (int i = 0; i < (int)t->items.size(); i++) {
    TabItem& it = t->items[i];
    if (it.row < 0 || IsRectEmpty(&it.rect)) continue;
    RECT r = it.rect;
    bool sel = i == t->sel;
    if (buttons) {
      DrawEdge(dc, &r, sel ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT | BF_SOFT);
    } else {
      if (sel) InflateRect(&r, 2, 0);
      if (sel) {
        if (Bottom(t))
          r.bottom += 2;
        else
          r.top -= 2;
      }
      RECT fill = r;
      if (Bottom(t))
        fill.top -= sel ? 2 : 0;
      else
        fill.bottom += sel ? 2 : 0;
      FillRect(dc, &fill, GetSysColorBrush(COLOR_BTNFACE));
      UINT sides = BF_LEFT | BF_RIGHT | (Bottom(t) ? BF_BOTTOM : BF_TOP) | BF_SOFT;
      DrawEdge(dc, &r, EDGE_RAISED, sides);
      if (sel) {
        // Joins the body: no line between them.
        RECT gap = Bottom(t) ? RECT{r.left + 1, r.top - 2, r.right - 1, r.top} : RECT{r.left + 1, r.bottom, r.right - 1, r.bottom + 2};
        FillRect(dc, &gap, GetSysColorBrush(COLOR_BTNFACE));
      }
    }
    if (StyleOf(t->hwnd) & TCS_OWNERDRAWFIXED) {
      DRAWITEMSTRUCT di = {};
      di.CtlType = ODT_TAB;
      di.CtlID = (UINT)GetDlgCtrlID(t->hwnd);
      di.itemID = (UINT)i;
      di.itemAction = ODA_DRAWENTIRE;
      di.itemState = sel ? ODS_SELECTED : 0;
      di.hwndItem = t->hwnd;
      di.hDC = dc;
      di.rcItem = r;
      LPARAM data = 0;
      memcpy(&data, it.extra.data(), std::min(it.extra.size(), sizeof data));
      di.itemData = (ULONG_PTR)data;
      SendMessageW(GetParent(t->hwnd), WM_DRAWITEM, di.CtlID, (LPARAM)&di);
      continue;
    }
    RECT in = r;
    InflateRect(&in, -t->padding.cx, 0);
    int cx = 0, cy = 0;
    if (it.image >= 0 && t->images && ImageList_GetIconSize(t->images, &cx, &cy)) {
      ImageList_Draw(t->images, it.image, dc, in.left, (in.top + in.bottom - cy) / 2, ILD_TRANSPARENT);
      in.left += cx + 3;
    }
    bool hilite = (it.state & TCIS_HIGHLIGHTED) != 0;
    SetTextColor(dc, GetSysColor(hilite ? COLOR_HIGHLIGHT : (IsWindowEnabled(t->hwnd) ? COLOR_BTNTEXT : COLOR_GRAYTEXT)));
    DrawTextW(dc, it.text.c_str(), (int)it.text.size(), &in, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (t->focused && i == (t->focus >= 0 ? t->focus : t->sel) && !(StyleOf(t->hwnd) & TCS_FOCUSNEVER)) {
      RECT f = r;
      InflateRect(&f, -3, -3);
      DrawFocusRect(dc, &f);
    }
  }
  // Arrows to scroll a single row that does not fit.
  if (!Multi(t) && !buttons && !t->items.empty()) {
    RECT last = t->items.back().rect;
    if (t->first > 0 || last.right > c.right - 2) {
      int s = TabHeight(t) - 2;
      RECT ar = {c.right - 2 * s, Bottom(t) ? c.bottom - s - 2 : 2, c.right - s, (Bottom(t) ? c.bottom - 2 : s + 2)};
      DrawFrameControl(dc, &ar, DFC_SCROLL, DFCS_SCROLLLEFT | (t->first > 0 ? 0 : DFCS_INACTIVE));
      OffsetRect(&ar, s, 0);
      DrawFrameControl(dc, &ar, DFC_SCROLL, DFCS_SCROLLRIGHT | (last.right > c.right - 2 * s ? 0 : DFCS_INACTIVE));
    }
  }
  SelectObject(dc, old);
}

int TabHit(Tabs* t, POINT p, UINT* flags) {
  if (!t->laidOut) LayoutTabs(t);
  for (int i = 0; i < (int)t->items.size(); i++)
    if (t->items[i].row >= 0 && PtInRect(&t->items[i].rect, p)) {
      if (flags) *flags = TCHT_ONITEM;
      return i;
    }
  if (flags) *flags = TCHT_NOWHERE;
  return -1;
}

void Select(Tabs* t, int i, bool notify) {
  if (i == t->sel || i < 0 || i >= (int)t->items.size()) return;
  if (notify && SendNotify(t->hwnd, NULL, TCN_SELCHANGING)) return;
  t->sel = i;
  t->focus = i;
  // A single row scrolls the selection into view.
  if (!Multi(t)) {
    if (i < t->first) t->first = i;
    RECT c;
    GetClientRect(t->hwnd, &c);
    for (int guard = 0; guard < 100; guard++) {
      LayoutTabs(t);
      if (t->items[i].rect.right <= c.right - 2 * TabHeight(t) || t->first >= i) break;
      t->first++;
    }
  }
  LayoutTabs(t);
  InvalidateRect(t->hwnd, NULL, TRUE);
  if (notify) SendNotify(t->hwnd, NULL, TCN_SELCHANGE);
}

void TabOut(Tabs* t, int i, TCITEMW* out, bool unicode) {
  const TabItem& it = t->items[i];
  if (out->mask & TCIF_TEXT) CopyOut(it.text, out->pszText, out->cchTextMax, unicode);
  if (out->mask & TCIF_IMAGE) out->iImage = it.image;
  if (out->mask & TCIF_STATE) out->dwState = it.state & out->dwStateMask;
  if (out->mask & TCIF_PARAM) memcpy(&out->lParam, it.extra.data(), it.extra.size());
}

void TabIn(Tabs* t, TabItem* it, const TCITEMW* in, bool unicode) {
  if (in->mask & TCIF_TEXT) it->text = TextIn(in->pszText, unicode);
  if (in->mask & TCIF_IMAGE) it->image = in->iImage;
  if (in->mask & TCIF_STATE) it->state = (it->state & ~in->dwStateMask) | (in->dwState & in->dwStateMask);
  if (in->mask & TCIF_PARAM) {
    it->extra.assign(t->extra, 0);
    memcpy(it->extra.data(), &in->lParam, t->extra);  // the item data runs on past lParam for TCM_SETITEMEXTRA
  }
}

LRESULT CALLBACK TabProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Tabs* t = StateOf<Tabs>(h);
  if (msg == WM_NCCREATE) {
    Attach<Tabs>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!t) return DefWindowProcW(h, msg, wp, lp);
  bool unicode = true;
  switch (msg) {
    case WM_NCDESTROY:
      Detach(h, t);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintTabs(t, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SIZE:
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      t->font = (HFONT)wp;
      LayoutTabs(t);
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)t->font;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      t->focused = msg == WM_SETFOCUS;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN: {
      NMTCKEYDOWN nm = {};
      nm.wVKey = (WORD)wp;
      nm.flags = (UINT)lp;
      SendNotify(h, NULL, TCN_KEYDOWN, &nm.hdr);
      int n = (int)t->items.size();
      if (!n) return 0;
      int cur = t->sel < 0 ? 0 : t->sel;
      if (wp == VK_LEFT || wp == VK_UP) Select(t, std::max(0, cur - 1), true);
      else if (wp == VK_RIGHT || wp == VK_DOWN) Select(t, std::min(n - 1, cur + 1), true);
      else if (wp == VK_HOME) Select(t, 0, true);
      else if (wp == VK_END) Select(t, n - 1, true);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      RECT c;
      GetClientRect(h, &c);
      // The scroll arrows of a single row.
      if (!Multi(t) && !Buttons(t)) {
        int s = TabHeight(t) - 2;
        bool inBand = Bottom(t) ? p.y >= c.bottom - s - 2 : p.y < s + 2;
        if (inBand && p.x >= c.right - 2 * s && (t->first > 0 || (!t->items.empty() && t->items.back().rect.right > c.right - 2))) {
          if (p.x < c.right - s && t->first > 0)
            t->first--;
          else if (p.x >= c.right - s && t->first + 1 < (int)t->items.size())
            t->first++;
          LayoutTabs(t);
          InvalidateRect(h, NULL, TRUE);
          return 0;
        }
      }
      int i = TabHit(t, p, NULL);
      if (i >= 0) {
        if (!(StyleOf(h) & TCS_FOCUSNEVER)) SetFocus(h);
        Select(t, i, true);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      SendNotify(h, NULL, NM_CLICK);
      return 0;
    case WM_RBUTTONUP:
      SendNotify(h, NULL, NM_RCLICK);
      return 0;
    case TCM_GETIMAGELIST:
      return (LRESULT)t->images;
    case TCM_SETIMAGELIST: {
      HIMAGELIST old = t->images;
      t->images = (HIMAGELIST)lp;
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return (LRESULT)old;
    }
    case TCM_GETITEMCOUNT:
      return (LRESULT)t->items.size();
    case TCM_GETITEMA:
      unicode = false;
      // fall through
    case TCM_GETITEMW: {
      int i = (int)wp;
      if (i < 0 || i >= (int)t->items.size() || !lp) return FALSE;
      TabOut(t, i, (TCITEMW*)lp, unicode);
      return TRUE;
    }
    case TCM_SETITEMA:
      unicode = false;
      // fall through
    case TCM_SETITEMW: {
      int i = (int)wp;
      if (i < 0 || i >= (int)t->items.size() || !lp) return FALSE;
      TabIn(t, &t->items[i], (const TCITEMW*)lp, unicode);
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case TCM_INSERTITEMA:
      unicode = false;
      // fall through
    case TCM_INSERTITEMW: {
      const TCITEMW* in = (const TCITEMW*)lp;
      if (!in) return -1;
      int at = std::min(std::max((int)wp, 0), (int)t->items.size());
      TabItem it;
      it.extra.assign(t->extra, 0);
      TabIn(t, &it, in, unicode);
      t->items.insert(t->items.begin() + at, it);
      if (t->sel >= at) t->sel++;
      if (t->sel < 0) t->sel = 0;
      if (t->focus >= at) t->focus++;
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return at;
    }
    case TCM_DELETEITEM: {
      int i = (int)wp;
      if (i < 0 || i >= (int)t->items.size()) return FALSE;
      t->items.erase(t->items.begin() + i);
      if (t->sel == i) t->sel = -1;
      else if (t->sel > i) t->sel--;
      if (t->focus == i) t->focus = -1;
      else if (t->focus > i) t->focus--;
      t->first = std::min(t->first, std::max(0, (int)t->items.size() - 1));
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case TCM_DELETEALLITEMS:
      t->items.clear();
      t->sel = t->focus = -1;
      t->first = 0;
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    case TCM_GETITEMRECT: {
      int i = (int)wp;
      if (!t->laidOut) LayoutTabs(t);
      if (i < 0 || i >= (int)t->items.size() || !lp) return FALSE;
      *(RECT*)lp = t->items[i].rect;
      return TRUE;
    }
    case TCM_GETCURSEL:
      return t->sel;
    case TCM_SETCURSEL: {
      int old = t->sel;
      if ((int)wp < 0 || (int)wp >= (int)t->items.size()) return -1;
      Select(t, (int)wp, false);
      return old;
    }
    case TCM_HITTEST: {
      TCHITTESTINFO* hi = (TCHITTESTINFO*)lp;
      if (!hi) return -1;
      return TabHit(t, hi->pt, &hi->flags);
    }
    case TCM_SETITEMEXTRA:
      if (!t->items.empty() || (int)wp < 0) return FALSE;
      t->extra = (int)wp;
      return TRUE;
    case TCM_ADJUSTRECT: {
      RECT* r = (RECT*)lp;
      if (!r) return 0;
      if (!t->laidOut) LayoutTabs(t);
      int area = Buttons(t) ? TabArea(t) : TabArea(t);
      if (wp) {
        // Display rectangle to window rectangle.
        InflateRect(r, 4, 4);
        if (Bottom(t))
          r->bottom += area - 2;
        else
          r->top -= area - 2;
      } else {
        if (Bottom(t))
          r->bottom -= area;
        else
          r->top += area;
        InflateRect(r, -4, -4);
        r->top += Bottom(t) ? 0 : -2;
      }
      return 0;
    }
    case TCM_SETITEMSIZE: {
      LRESULT old = MAKELONG(t->fixed.cx, t->fixed.cy);
      t->fixed = {(short)LOWORD(lp), (short)HIWORD(lp)};
      LayoutTabs(t);
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case TCM_REMOVEIMAGE: {
      int img = (int)wp;
      if (t->images) ImageList_Remove(t->images, img);
      for (TabItem& it : t->items)
        if (it.image == img)
          it.image = -1;
        else if (it.image > img)
          it.image--;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    }
    case TCM_SETPADDING:
      t->padding = {(short)LOWORD(lp), (short)HIWORD(lp)};
      LayoutTabs(t);
      return 0;
    case TCM_GETROWCOUNT:
      if (!t->laidOut) LayoutTabs(t);
      return t->rows;
    case TCM_GETTOOLTIPS:
      return (LRESULT)t->tips;
    case TCM_SETTOOLTIPS:
      t->tips = (HWND)wp;
      return 0;
    case TCM_GETCURFOCUS:
      return t->focus >= 0 ? t->focus : t->sel;
    case TCM_SETCURFOCUS:
      if (Buttons(t)) {
        t->focus = (int)wp;
        InvalidateRect(h, NULL, TRUE);
      } else {
        Select(t, (int)wp, true);
      }
      return 0;
    case TCM_SETMINTABWIDTH: {
      int old = t->minWidth;
      t->minWidth = (int)lp;
      LayoutTabs(t);
      return old;
    }
    case TCM_DESELECTALL:
      for (int i = 0; i < (int)t->items.size(); i++)
        if (!wp || i != t->sel) t->items[i].state &= ~TCIS_BUTTONPRESSED;
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case TCM_HIGHLIGHTITEM: {
      int i = (int)wp;
      if (i < 0 || i >= (int)t->items.size()) return FALSE;
      if (LOWORD(lp))
        t->items[i].state |= TCIS_HIGHLIGHTED;
      else
        t->items[i].state &= ~TCIS_HIGHLIGHTED;
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case TCM_SETEXTENDEDSTYLE: {
      DWORD old = t->exStyle;
      t->exStyle = wp ? (t->exStyle & ~(DWORD)wp) | ((DWORD)lp & (DWORD)wp) : (DWORD)lp;
      return old;
    }
    case TCM_GETEXTENDEDSTYLE:
      return t->exStyle;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Hot key control

struct HotKey {
  HWND hwnd = NULL;
  WORD vk = 0, mods = 0;
  WORD invalid = 0, fallback = 0;
  HFONT font = NULL;
  bool focus = false;
};

std::wstring KeyName(UINT vk, bool extended) {
  UINT scan = MapVirtualKeyW(vk, 0);
  wchar_t buf[64] = L"";
  if (GetKeyNameTextW((LONG)((scan << 16) | (extended ? (1 << 24) : 0)), buf, 64) > 0) return buf;
  if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::wstring(1, (wchar_t)vk);
  if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
  swprintf(buf, 64, L"#%u", vk);
  return buf;
}

std::wstring HotKeyText(HotKey* k) {
  std::wstring t;
  if (k->mods & HOTKEYF_CONTROL) t += KeyName(VK_CONTROL, false) + L" + ";
  if (k->mods & HOTKEYF_SHIFT) t += KeyName(VK_SHIFT, false) + L" + ";
  if (k->mods & HOTKEYF_ALT) t += KeyName(VK_MENU, false) + L" + ";
  if (k->vk) t += KeyName(k->vk, (k->mods & HOTKEYF_EXT) != 0);
  if (t.empty()) t = L"None";
  return t;
}

WORD CurrentMods() {
  WORD m = 0;
  if (GetKeyState(VK_CONTROL) & 0x8000) m |= HOTKEYF_CONTROL;
  if (GetKeyState(VK_SHIFT) & 0x8000) m |= HOTKEYF_SHIFT;
  if (GetKeyState(VK_MENU) & 0x8000) m |= HOTKEYF_ALT;
  return m;
}

// The invalid combinations rule: replaced by the fallback modifiers.
WORD ApplyRules(HotKey* k, WORD mods) {
  WORD combo;
  bool c = mods & HOTKEYF_CONTROL, s = mods & HOTKEYF_SHIFT, a = mods & HOTKEYF_ALT;
  if (!c && !s && !a) combo = HKCOMB_NONE;
  else if (s && !c && !a) combo = HKCOMB_S;
  else if (c && !s && !a) combo = HKCOMB_C;
  else if (a && !c && !s) combo = HKCOMB_A;
  else if (s && c && !a) combo = HKCOMB_SC;
  else if (s && a && !c) combo = HKCOMB_SA;
  else if (c && a && !s) combo = HKCOMB_CA;
  else combo = HKCOMB_SCA;
  if (k->invalid & combo) return (WORD)(k->fallback | (mods & HOTKEYF_EXT));
  return mods;
}

LRESULT CALLBACK HotKeyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  HotKey* k = StateOf<HotKey>(h);
  if (msg == WM_NCCREATE) {
    Attach<HotKey>(h);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!k) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      Detach(h, k);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_GETDLGCODE:
      return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_SETFOCUS:
      k->focus = true;
      CreateCaret(h, NULL, 1, FontHeight(h, k->font));
      ShowCaret(h);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_KILLFOCUS:
      k->focus = false;
      DestroyCaret();
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (wp == VK_CONTROL || wp == VK_SHIFT || wp == VK_MENU) {
        k->vk = 0;
        k->mods = ApplyRules(k, CurrentMods());
      } else if ((wp == VK_BACK || wp == VK_DELETE || wp == VK_SPACE) && !CurrentMods()) {
        k->vk = 0;
        k->mods = 0;
      } else if (wp != VK_TAB || CurrentMods()) {
        k->vk = (WORD)wp;
        k->mods = ApplyRules(k, (WORD)(CurrentMods() | ((lp & (1 << 24)) ? HOTKEYF_EXT : 0)));
      } else {
        break;
      }
      InvalidateRect(h, NULL, TRUE);
      SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), EN_CHANGE), (LPARAM)h);
      return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      if (!k->vk && (wp == VK_CONTROL || wp == VK_SHIFT || wp == VK_MENU)) {
        k->mods = ApplyRules(k, CurrentMods());
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(h);
      return 0;
    case WM_SETFONT:
      k->font = (HFONT)wp;
      if (lp) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)k->font;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      RECT c;
      GetClientRect(h, &c);
      FillRect(dc, &c, GetSysColorBrush(IsWindowEnabled(h) ? COLOR_WINDOW : COLOR_BTNFACE));
      HGDIOBJ old = SelectObject(dc, k->font ? k->font : ControlFont());
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, GetSysColor(IsWindowEnabled(h) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
      std::wstring t = HotKeyText(k);
      RECT r = c;
      r.left += 2;
      DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
      if (k->focus) {
        RECT m = {0, 0, 0, 0};
        DrawTextW(dc, t.c_str(), (int)t.size(), &m, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        SetCaretPos(r.left + m.right, (c.bottom - m.bottom) / 2);
      }
      SelectObject(dc, old);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case HKM_SETHOTKEY:
      k->vk = LOBYTE(LOWORD(wp));
      k->mods = HIBYTE(LOWORD(wp));
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case HKM_GETHOTKEY:
      return MAKEWORD(k->vk, k->mods);
    case HKM_SETRULES:
      k->invalid = (WORD)wp;
      k->fallback = LOWORD(lp);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterSimpleClasses() {
  LPCWSTR arrow = MAKEINTRESOURCEW(32512);
  RegisterSystemClass(PROGRESS_CLASSW, ProgressProc, CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL, arrow);
  RegisterSystemClass(TRACKBAR_CLASSW, TrackProc, CS_DBLCLKS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(UPDOWN_CLASSW, SpinProc, CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL, arrow);
  RegisterSystemClass(WC_HEADERW, HeaderProc, CS_DBLCLKS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(WC_TABCONTROLW, TabProc, CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL, arrow);
  RegisterSystemClass(HOTKEY_CLASSW, HotKeyProc, 0, sizeof(void*), NULL, MAKEINTRESOURCEW(32513));
}

}  // namespace winport

extern "C" {

HWND WINAPI CreateUpDownControl(DWORD style, int x, int y, int cx, int cy, HWND parent, int id, HINSTANCE inst,
                                HWND buddy, int upper, int lower, int pos) {
  HWND h = CreateWindowExW(0, UPDOWN_CLASSW, NULL, style, x, y, cx, cy, parent, (HMENU)(INT_PTR)id, inst, NULL);
  if (!h) return NULL;
  if (buddy) SendMessageW(h, UDM_SETBUDDY, (WPARAM)buddy, 0);
  SendMessageW(h, UDM_SETRANGE, 0, MAKELONG(upper, lower));
  SendMessageW(h, UDM_SETPOS, 0, MAKELONG(pos, 0));
  return h;
}

}  // extern "C"
