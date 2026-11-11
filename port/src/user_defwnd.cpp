// user_defwnd.cpp - what a window does by default: DefWindowProc, the
// window frame (borders, caption, scroll bars) and its hit testing, 3-D
// edges, window scroll bars and the caret.
//
// The frame is drawn in the classic Windows style into the window's
// surface; a backend that shows windows with its own decorations can ask
// for frameless top-level windows instead.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <stdlib.h>
#include <string.h>

using namespace winport;

namespace {

bool HasCaption(DWORD style) { return (style & WS_CAPTION) == WS_CAPTION; }

int BorderWidth(DWORD style, DWORD exStyle) {
  if (style & WS_THICKFRAME) return GetSystemMetrics(SM_CXFRAME);
  if ((style & WS_DLGFRAME) || (exStyle & WS_EX_DLGMODALFRAME)) return GetSystemMetrics(SM_CXDLGFRAME);
  if (style & WS_BORDER) return GetSystemMetrics(SM_CXBORDER);
  return 0;
}

int EdgeWidth(DWORD exStyle) {
  if (exStyle & WS_EX_CLIENTEDGE) return GetSystemMetrics(SM_CXEDGE);
  if (exStyle & WS_EX_STATICEDGE) return GetSystemMetrics(SM_CXBORDER);
  return 0;
}

int CaptionHeight(DWORD exStyle) {
  return GetSystemMetrics((exStyle & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION);
}

// Where each part of a window's frame lies, in window coordinates.
struct Frame {
  RECT window, caption, close, max, min, sysmenu, menu, vscroll, hscroll, grip, client;
  int border;
};

Frame Layout(Window* w) {
  Frame f = {};
  int cx = w->rect.right - w->rect.left, cy = w->rect.bottom - w->rect.top;
  f.window = MakeRect(0, 0, cx, cy);
  f.border = BorderWidth(w->style, w->exStyle);
  int b = f.border;
  int top = b;
  if (HasCaption(w->style)) {
    int ch = CaptionHeight(w->exStyle);
    f.caption = MakeRect(b, b, cx - b, b + ch - 1);
    int bh = ch - 5, bw = bh + 2;
    int right = f.caption.right - 2;
    if (w->style & WS_SYSMENU) {
      f.close = MakeRect(right - bw, f.caption.top + 2, right, f.caption.top + 2 + bh);
      right = f.close.left - 2;
      if (w->style & (WS_MAXIMIZEBOX | WS_MINIMIZEBOX)) {
        f.max = MakeRect(right - bw, f.close.top, right, f.close.bottom);
        f.min = MakeRect(f.max.left - bw, f.close.top, f.max.left, f.close.bottom);
      }
      if (!(w->exStyle & WS_EX_TOOLWINDOW) && !(w->exStyle & WS_EX_DLGMODALFRAME))
        f.sysmenu = MakeRect(f.caption.left + 2, f.caption.top + 1, f.caption.left + 2 + ch - 3, f.caption.bottom - 1);
    }
    top += ch;
  }
  if (!w->Child() && w->menu) {
    int mh = GetSystemMetrics(SM_CYMENU);
    f.menu = MakeRect(b, top, cx - b, top + mh);
  }
  f.client = w->client;
  int sx = GetSystemMetrics(SM_CXVSCROLL), sy = GetSystemMetrics(SM_CYHSCROLL);
  if (w->style & WS_VSCROLL) f.vscroll = MakeRect(f.client.right, f.client.top, f.client.right + sx, f.client.bottom);
  if (w->style & WS_HSCROLL) f.hscroll = MakeRect(f.client.left, f.client.bottom, f.client.right, f.client.bottom + sy);
  if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL))
    f.grip = MakeRect(f.client.right, f.client.bottom, f.client.right + sx, f.client.bottom + sy);
  return f;
}

bool In(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

bool IsActive(Window* w) {
  // An MDI child is never the active window; its frame tells it with WM_NCACTIVATE.
  if (w->exStyle & WS_EX_MDICHILD) return w->ncActive;
  Window* t = w->TopLevel();
  return QueueFor(t->tid)->active == t->hwnd;
}

void Fill(HDC dc, int l, int t, int r, int b, int color) {
  RECT rc = {l, t, r, b};
  FillRect(dc, &rc, GetSysColorBrush(color));
}

// Scroll bars

ScrollBar* BarOf(Window* w, int bar) { return bar == SB_HORZ ? &w->hscroll : &w->vscroll; }

int MaxPos(const SCROLLINFO& si) {
  int m = si.nMax - (si.nPage ? (int)si.nPage - 1 : 0);
  return m < si.nMin ? si.nMin : m;
}

// The thumb's place along a scroll bar's trough: offset and length.
void ThumbOf(const SCROLLINFO& si, int trough, int* pos, int* len) {
  int range = si.nMax - si.nMin + 1;
  int minThumb = GetSystemMetrics(SM_CXHTHUMB) / 2;
  if (range <= 0 || trough <= 0) {
    *pos = *len = 0;
    return;
  }
  int l = si.nPage ? (int)((long long)trough * si.nPage / range) : GetSystemMetrics(SM_CXHTHUMB);
  if (l < minThumb) l = minThumb;
  if (l > trough) l = trough;
  int span = MaxPos(si) - si.nMin;
  int p = span > 0 ? (int)((long long)(trough - l) * (si.nPos - si.nMin) / span) : 0;
  *pos = p;
  *len = l;
}

void DrawArrowBox(HDC dc, RECT r, int dir, bool enabled) {
  DrawEdge(dc, &r, EDGE_RAISED, BF_RECT | BF_MIDDLE);
  // A small triangle pointing dir: 0 up, 1 down, 2 left, 3 right.
  int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  HBRUSH br = GetSysColorBrush(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
  for (int i = 0; i < 4; i++) {
    RECT line;
    switch (dir) {
      case 0: line = MakeRect(cx - i, cy - 2 + i, cx + i + 1, cy - 1 + i); break;
      case 1: line = MakeRect(cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i); break;
      case 2: line = MakeRect(cx - 2 + i, cy - i, cx - 1 + i, cy + i + 1); break;
      default: line = MakeRect(cx - 1 + i, cy - 3 + i, cx + i, cy + 4 - i); break;
    }
    FillRect(dc, &line, br);
  }
}

}  // namespace

namespace winport {

void PaintScrollBar(HDC dc, const RECT& r, bool vertical, const ScrollBar* sb) {
  if (RectEmpty(r)) return;
  int a = vertical ? r.right - r.left : r.bottom - r.top;  // arrow box size
  int length = vertical ? r.bottom - r.top : r.right - r.left;
  if (a * 2 > length) a = length / 2;
  RECT first = vertical ? MakeRect(r.left, r.top, r.right, r.top + a) : MakeRect(r.left, r.top, r.left + a, r.bottom);
  RECT last = vertical ? MakeRect(r.left, r.bottom - a, r.right, r.bottom) : MakeRect(r.right - a, r.top, r.right, r.bottom);
  RECT trough = vertical ? MakeRect(r.left, first.bottom, r.right, last.top) : MakeRect(first.right, r.top, last.left, r.bottom);
  // The trough is a fine checker of face and highlight.
  FillRect(dc, &trough, GetSysColorBrush(COLOR_SCROLLBAR));
  DrawArrowBox(dc, first, vertical ? 0 : 2, sb->enabled);
  DrawArrowBox(dc, last, vertical ? 1 : 3, sb->enabled);
  int tl = vertical ? trough.bottom - trough.top : trough.right - trough.left;
  int pos, len;
  ThumbOf(sb->info, tl, &pos, &len);
  if (sb->enabled && len > 0 && sb->info.nMax > sb->info.nMin) {
    RECT thumb = vertical ? MakeRect(r.left, trough.top + pos, r.right, trough.top + pos + len)
                          : MakeRect(trough.left + pos, r.top, trough.left + pos + len, r.bottom);
    DrawEdge(dc, &thumb, EDGE_RAISED, BF_RECT | BF_MIDDLE);
  }
}

}  // namespace winport

namespace {

void DrawScrollBar(Window* w, HDC dc, const RECT& r, bool vertical) {
  PaintScrollBar(dc, r, vertical, BarOf(w, vertical ? SB_VERT : SB_HORZ));
}

// Carets: one at a time, as on Windows.
struct Caret {
  HWND hwnd = NULL;
  HBITMAP bitmap = NULL;
  int width = 0, height = 0;
  POINT pos = {0, 0};
  int hidden = 1;  // ShowCaret calls still needed
  bool on = false;
  UINT_PTR timer = 0;
  UINT blink = 530;
} g_caret;

void ToggleCaret() {
  Caret& c = g_caret;
  if (!c.hwnd || !IsWindow(c.hwnd)) return;
  HDC dc = GetDC(c.hwnd);
  // A gray caret is a checker, but inverting does for now.
  PatBlt(dc, c.pos.x, c.pos.y, c.width, c.height, DSTINVERT);
  ReleaseDC(c.hwnd, dc);
  c.on = !c.on;
}

void CALLBACK CaretBlink(HWND, UINT, UINT_PTR, DWORD) {
  if (g_caret.hwnd && g_caret.hidden == 0) ToggleCaret();
}

void CaretOff() {
  if (g_caret.on) ToggleCaret();
}

void StopCaretTimer() {
  if (g_caret.timer) {
    KillTimer(NULL, g_caret.timer);
    g_caret.timer = 0;
  }
}

// Tracking a click on a window scroll bar: the part clicked, and the
// scroll code it sends.
}  // namespace

namespace winport {

// A click on a scroll bar at r, in w's window coordinates: sends the
// scroll messages to notify, with ctl as lParam, and drags the thumb.
void ClickScrollBar(Window* w, ScrollBar* sb, RECT r, bool vertical, POINT screen, HWND notify, HWND ctl) {
  RECT wr = ScreenRect(w);
  POINT p = {screen.x - wr.left, screen.y - wr.top};
  int a = vertical ? r.right - r.left : r.bottom - r.top;
  int length = vertical ? r.bottom - r.top : r.right - r.left;
  if (a * 2 > length) a = length / 2;
  int along = vertical ? p.y - r.top : p.x - r.left;
  UINT msg = vertical ? WM_VSCROLL : WM_HSCROLL;
  if (!sb->enabled) return;
  auto send = [&](WPARAM wp) { SendMessageW(notify, msg, wp, (LPARAM)ctl); };
  int code;
  if (along < a) {
    code = SB_LINEUP;
  } else if (along >= length - a) {
    code = SB_LINEDOWN;
  } else {
    int pos, len;
    ThumbOf(sb->info, length - 2 * a, &pos, &len);
    int t = along - a;
    if (t < pos) {
      code = SB_PAGEUP;
    } else if (t >= pos + len) {
      code = SB_PAGEDOWN;
    } else {
      // Drag the thumb until the button comes up.
      HWND h = w->hwnd;
      SetCapture(h);
      int start = vertical ? screen.y : screen.x;
      int startPos = sb->info.nPos;
      int trough = length - 2 * a - len;
      int span = MaxPos(sb->info) - sb->info.nMin;
      MSG m;
      int track = startPos;
      while (GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_MOUSEMOVE || m.message == WM_LBUTTONUP) {
          POINT now;
          GetCursorPos(&now);
          int d = (vertical ? now.y : now.x) - start;
          track = startPos + (trough > 0 ? (int)((long long)d * span / trough) : 0);
          if (track < sb->info.nMin) track = sb->info.nMin;
          if (track > MaxPos(sb->info)) track = MaxPos(sb->info);
          sb->info.nTrackPos = track;
          if (m.message == WM_LBUTTONUP) break;
          send(MAKEWPARAM(SB_THUMBTRACK, track));
          continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) break;
        TranslateMessage(&m);
        DispatchMessageW(&m);
        if (!IsWindow(h)) return;
      }
      ReleaseCapture();
      if (!IsWindow(h)) return;
      send(MAKEWPARAM(SB_THUMBPOSITION, track));
      if (IsWindow(notify)) send(SB_ENDSCROLL);
      return;
    }
  }
  send(code);
  if (IsWindow(notify)) send(SB_ENDSCROLL);
}

}  // namespace winport

namespace {

void ScrollBarClick(Window* w, int bar, POINT screen) {
  Frame f = Layout(w);
  ClickScrollBar(w, BarOf(w, bar), bar == SB_VERT ? f.vscroll : f.hscroll, bar == SB_VERT, screen, w->hwnd, NULL);
}

// Shows or hides a window scroll bar by its style; the frame changes.
void SetBarShown(Window* w, int bar, bool show) {
  DWORD bits = bar == SB_HORZ ? WS_HSCROLL : bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL | WS_VSCROLL;
  DWORD style = show ? w->style | bits : w->style & ~bits;
  if (style == w->style) return;
  w->style = style;
  SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void RedrawBars(Window* w) {
  if (!ReallyVisible(w)) return;
  HDC dc = GetWindowDC(w->hwnd);
  Frame f = Layout(w);
  DrawScrollBar(w, dc, f.vscroll, true);
  DrawScrollBar(w, dc, f.hscroll, false);
  ReleaseDC(w->hwnd, dc);
}

LRESULT HitTest(Window* w, POINT screen) {
  RECT wr = ScreenRect(w);
  POINT p = {screen.x - wr.left, screen.y - wr.top};
  Frame f = Layout(w);
  if (!In(f.window, p)) return HTNOWHERE;
  if (In(f.client, p)) return HTCLIENT;
  if (w->style & WS_MINIMIZE) return HTCAPTION;
  int b = f.border;
  if (b && (w->style & WS_THICKFRAME) && !(w->style & WS_MAXIMIZE)) {
    int corner = GetSystemMetrics(SM_CYCAPTION);
    int cx = f.window.right, cy = f.window.bottom;
    bool left = p.x < b, right = p.x >= cx - b, top = p.y < b, bottom = p.y >= cy - b;
    if (left || right || top || bottom) {
      if (top) return p.x < corner ? HTTOPLEFT : p.x >= cx - corner ? HTTOPRIGHT : HTTOP;
      if (bottom) return p.x < corner ? HTBOTTOMLEFT : p.x >= cx - corner ? HTBOTTOMRIGHT : HTBOTTOM;
      if (left) return p.y < corner ? HTTOPLEFT : p.y >= cy - corner ? HTBOTTOMLEFT : HTLEFT;
      return p.y < corner ? HTTOPRIGHT : p.y >= cy - corner ? HTBOTTOMRIGHT : HTRIGHT;
    }
  }
  if (In(f.caption, p)) {
    if (In(f.close, p)) return HTCLOSE;
    if (In(f.max, p)) return HTMAXBUTTON;
    if (In(f.min, p)) return HTMINBUTTON;
    if (In(f.sysmenu, p)) return HTSYSMENU;
    return HTCAPTION;
  }
  if (In(f.menu, p)) return HTMENU;
  if (In(f.vscroll, p)) return HTVSCROLL;
  if (In(f.hscroll, p)) return HTHSCROLL;
  if (In(f.grip, p)) return (w->style & WS_THICKFRAME) ? HTGROWBOX : HTBORDER;
  return HTBORDER;
}

void DrawCaptionBox(HDC dc, const RECT& r, int glyph) {
  RECT box = r;
  DrawEdge(dc, &box, EDGE_RAISED, BF_RECT | BF_MIDDLE);
  HBRUSH br = GetSysColorBrush(COLOR_BTNTEXT);
  int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  switch (glyph) {
    case 0:  // close: an X
      for (int i = -3; i <= 3; i++) {
        RECT a = MakeRect(cx + i - 1, cy + i, cx + i + 1, cy + i + 1);
        RECT b = MakeRect(cx - i - 1, cy + i, cx - i + 1, cy + i + 1);
        FillRect(dc, &a, br);
        FillRect(dc, &b, br);
      }
      break;
    case 1: {  // maximize: a box
      RECT o = MakeRect(cx - 4, cy - 4, cx + 5, cy + 4);
      FrameRect(dc, &o, br);
      RECT t = MakeRect(o.left, o.top + 1, o.right, o.top + 2);
      FillRect(dc, &t, br);
      break;
    }
    case 2: {  // restore: two boxes
      RECT a = MakeRect(cx - 2, cy - 5, cx + 5, cy + 1);
      RECT b = MakeRect(cx - 5, cy - 2, cx + 2, cy + 4);
      FrameRect(dc, &a, br);
      FillRect(dc, &b, GetSysColorBrush(COLOR_BTNFACE));
      FrameRect(dc, &b, br);
      break;
    }
    default: {  // minimize: a bar
      RECT a = MakeRect(cx - 4, cy + 2, cx + 3, cy + 4);
      FillRect(dc, &a, br);
      break;
    }
  }
}

}  // namespace

namespace winport {

RECT FrameInsets(DWORD style, DWORD exStyle, bool menu) {
  int b = BorderWidth(style, exStyle) + EdgeWidth(exStyle);
  RECT r = {b, b, b, b};
  if (HasCaption(style)) r.top += CaptionHeight(exStyle);
  if (menu) r.top += GetSystemMetrics(SM_CYMENU);
  return r;
}

RECT MenuBarRect(Window* w) { return Layout(w).menu; }
RECT SysMenuRect(Window* w) { return Layout(w).sysmenu; }

void DrawFrame(Window* w) {
  if (!ReallyVisible(w)) return;
  HWND h = w->hwnd;
  HDC dc = GetWindowDC(h);
  Frame f = Layout(w);
  RECT r = f.window;
  bool active = IsActive(w);
  if (w->style & (WS_THICKFRAME | WS_DLGFRAME) || (w->exStyle & WS_EX_DLGMODALFRAME)) {
    RECT e = r;
    DrawEdge(dc, &e, EDGE_RAISED, BF_RECT | BF_ADJUST);
    int color = (w->style & WS_THICKFRAME) ? (active ? COLOR_ACTIVEBORDER : COLOR_INACTIVEBORDER) : COLOR_BTNFACE;
    for (int i = 2; i < f.border; i++) {
      RECT ring = MakeRect(r.left + i, r.top + i, r.right - i, r.bottom - i);
      FrameRect(dc, &ring, GetSysColorBrush(color));
    }
  } else if (w->style & WS_BORDER) {
    FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
  }
  if (HasCaption(w->style)) {
    Fill(dc, f.caption.left, f.caption.top, f.caption.right, f.caption.bottom,
         active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    Fill(dc, f.caption.left, f.caption.bottom, f.caption.right, f.caption.bottom + 1, COLOR_BTNFACE);
    RECT text = f.caption;
    text.left += RectEmpty(f.sysmenu) ? 4 : f.sysmenu.right - f.caption.left + 4;
    if (!RectEmpty(f.min)) text.right = f.min.left - 2;
    else if (!RectEmpty(f.close)) text.right = f.close.left - 2;
    if (!w->text.empty()) {
      int mode = SetBkMode(dc, TRANSPARENT);
      COLORREF old = SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT : COLOR_INACTIVECAPTIONTEXT));
      NONCLIENTMETRICSW ncm = {sizeof ncm};
      HFONT font = NULL;
      if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
        font = CreateFontIndirectW(&ncm.lfCaptionFont);
      HGDIOBJ oldFont = font ? SelectObject(dc, font) : NULL;
      DrawTextW(dc, w->text.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
      if (oldFont) SelectObject(dc, oldFont);
      if (font) DeleteObject(font);
      SetTextColor(dc, old);
      SetBkMode(dc, mode);
    }
    if (!RectEmpty(f.close)) DrawCaptionBox(dc, f.close, 0);
    if (!RectEmpty(f.max)) DrawCaptionBox(dc, f.max, (w->style & WS_MAXIMIZE) ? 2 : 1);
    if (!RectEmpty(f.min)) DrawCaptionBox(dc, f.min, 3);
  }
  if (!RectEmpty(f.menu)) DrawMenuBarIn(w, dc, f.menu);
  int e = EdgeWidth(w->exStyle);
  if (e) {
    RECT c = f.client;
    if (w->style & WS_VSCROLL) c.right += GetSystemMetrics(SM_CXVSCROLL);
    if (w->style & WS_HSCROLL) c.bottom += GetSystemMetrics(SM_CYHSCROLL);
    InflateRect(&c, e, e);
    DrawEdge(dc, &c, (w->exStyle & WS_EX_CLIENTEDGE) ? EDGE_SUNKEN : BDR_SUNKENOUTER, BF_RECT);
  }
  DrawScrollBar(w, dc, f.vscroll, true);
  DrawScrollBar(w, dc, f.hscroll, false);
  if (!RectEmpty(f.grip)) Fill(dc, f.grip.left, f.grip.top, f.grip.right, f.grip.bottom, COLOR_BTNFACE);
  ReleaseDC(h, dc);
}

}  // namespace winport


// Moving and sizing a window with the mouse: SC_MOVE and SC_SIZE. The
// window follows the pointer until the button comes up; Escape puts it
// back. hit is the part of the frame grabbed, an HT value.
static void MoveSizeLoop(Window* w, bool unicode, LRESULT hit) {
  HWND h = w->hwnd;
  if (!(GetKeyState(VK_LBUTTON) & 0x8000)) return;
  bool move = hit == HTCAPTION;
  if (!move && !(w->style & WS_THICKFRAME)) return;
  if (w->style & (WS_MAXIMIZE | WS_MINIMIZE) && !(move && (w->style & WS_MINIMIZE))) return;
  // Some displays move and size windows themselves.
  if (!w->parent && GetBackend()->BeginMoveSize(w, hit)) return;
  HWND parent = w->parent ? w->parent->hwnd : NULL;
  RECT start;
  GetWindowRect(h, &start);
  if (parent) MapWindowPoints(NULL, parent, (LPPOINT)&start, 2);
  POINT from;
  GetCursorPos(&from);
  SendTo(w, WM_ENTERSIZEMOVE, 0, 0, unicode);
  SetCapture(h);
  RECT cur = start;
  bool cancelled = false;
  MSG m;
  while (GetCapture() == h) {
    if (!GetMessageW(&m, NULL, 0, 0)) {
      PostQuitMessage((int)m.wParam);
      break;
    }
    if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
      cancelled = true;
      break;
    }
    if (m.message == WM_LBUTTONUP || m.message == WM_NCLBUTTONUP) break;
    if (m.message != WM_MOUSEMOVE && m.message != WM_NCMOUSEMOVE) {
      if (m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) continue;
      TranslateMessage(&m);
      DispatchMessageW(&m);
      continue;
    }
    POINT p;
    GetCursorPos(&p);
    int dx = p.x - from.x, dy = p.y - from.y;
    RECT r = start;
    if (move) {
      OffsetRect(&r, dx, dy);
    } else {
      if (hit == HTLEFT || hit == HTTOPLEFT || hit == HTBOTTOMLEFT) r.left += dx;
      if (hit == HTRIGHT || hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT) r.right += dx;
      if (hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT) r.top += dy;
      if (hit == HTBOTTOM || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT) r.bottom += dy;
      int edge = 0;
      switch (hit) {
        case HTLEFT: edge = WMSZ_LEFT; break;
        case HTRIGHT: edge = WMSZ_RIGHT; break;
        case HTTOP: edge = WMSZ_TOP; break;
        case HTTOPLEFT: edge = WMSZ_TOPLEFT; break;
        case HTTOPRIGHT: edge = WMSZ_TOPRIGHT; break;
        case HTBOTTOM: edge = WMSZ_BOTTOM; break;
        case HTBOTTOMLEFT: edge = WMSZ_BOTTOMLEFT; break;
        case HTBOTTOMRIGHT: edge = WMSZ_BOTTOMRIGHT; break;
      }
      SendTo(w, WM_SIZING, edge, (LPARAM)&r, unicode);
    }
    if (move) SendTo(w, WM_MOVING, 0, (LPARAM)&r, unicode);
    if (EqualRect(&r, &cur)) continue;
    cur = r;
    SetWindowPos(h, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | (move ? SWP_NOSIZE : 0));
  }
  if (GetCapture() == h) ReleaseCapture();
  if (cancelled)
    SetWindowPos(h, NULL, start.left, start.top, start.right - start.left, start.bottom - start.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  if (GetWindow(h)) SendTo(w, WM_EXITSIZEMOVE, 0, 0, unicode);
}

static bool g_altAlone;

static LRESULT DefCore(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode) {
  Window* w = GetWindow(h);
  if (!w) return 0;
  switch (msg) {
    case WM_NCCREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs && cs->lpszName) {
        if (unicode)
          w->text = cs->lpszName;
        else
          w->text = Widen((LPCSTR)((CREATESTRUCTA*)lp)->lpszName);
      }
      // Scroll bars that the style asks for start with a range of 0 to 100.
      return TRUE;
    }
    case WM_NCCALCSIZE: {
      RECT* r = (RECT*)lp;
      if (!r) return 0;
      RECT in = FrameInsets(w->style, w->exStyle, !w->Child() && w->menu);
      r->left += in.left;
      r->top += in.top;
      r->right -= in.right;
      r->bottom -= in.bottom;
      if (w->style & WS_VSCROLL) r->right -= GetSystemMetrics(SM_CXVSCROLL);
      if (w->style & WS_HSCROLL) r->bottom -= GetSystemMetrics(SM_CYHSCROLL);
      if (r->right < r->left) r->right = r->left;
      if (r->bottom < r->top) r->bottom = r->top;
      return 0;
    }
    case WM_NCHITTEST: return HitTest(w, POINT{(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)});
    case WM_NCPAINT: DrawFrame(w); return 0;
    case WM_NCACTIVATE:
      w->ncActive = wp != 0;
      if (ReallyVisible(w) && HasCaption(w->style)) DrawFrame(w);
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND: {
      HBRUSH br = w->cls ? w->cls->wc.hbrBackground : NULL;
      if (!br) return 0;
      RECT r;
      GetClientRect(h, &r);
      HDC dc = (HDC)wp;
      // In the DC's logical units.
      DPtoLP(dc, (LPPOINT)&r, 2);
      FillRect(dc, &r, br);
      return 1;
    }
    case WM_SETTEXT:
      if (lp)
        w->text = unicode ? std::wstring((LPCWSTR)lp) : Widen((LPCSTR)lp);
      else
        w->text.clear();
      if (!w->parent) GetBackend()->TitleChanged(w);
      if (HasCaption(w->style) && ReallyVisible(w)) DrawFrame(w);
      return TRUE;
    case WM_GETTEXT:
      if (!lp || wp == 0) return 0;
      if (unicode) {
        size_t n = w->text.size() < wp - 1 ? w->text.size() : wp - 1;
        memcpy((LPWSTR)lp, w->text.c_str(), n * sizeof(wchar_t));
        ((LPWSTR)lp)[n] = 0;
        return n;
      } else {
        std::string s = Narrow(w->text.c_str());
        size_t n = s.size() < wp - 1 ? s.size() : wp - 1;
        while (n > 0 && n < s.size() && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        memcpy((LPSTR)lp, s.c_str(), n);
        ((LPSTR)lp)[n] = 0;
        return n;
      }
    case WM_GETTEXTLENGTH: return unicode ? w->text.size() : Narrow(w->text.c_str()).size();
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_SYSCOMMAND: {
      if (HaveHooks(WH_CBT) && CallHooks(WH_CBT, HCBT_SYSCOMMAND, wp, lp)) return 0;
      switch (wp & 0xFFF0) {
        case SC_CLOSE: SendTo(w, WM_CLOSE, 0, 0, unicode); return 0;
        case SC_MINIMIZE: ShowWindow(h, SW_MINIMIZE); return 0;
        case SC_MAXIMIZE: ShowWindow(h, SW_MAXIMIZE); return 0;
        case SC_RESTORE: ShowWindow(h, SW_RESTORE); return 0;
        case SC_VSCROLL: case SC_HSCROLL:
          ScrollBarClick(w, (wp & 0xFFF0) == SC_VSCROLL ? SB_VERT : SB_HORZ, POINT{(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)});
          return 0;
        case SC_KEYMENU:
        case SC_MOUSEMENU:
          MenuCommand(w, wp, lp);
          return 0;
        case SC_MOVE:
          MoveSizeLoop(w, unicode, HTCAPTION);
          return 0;
        case SC_SIZE: {
          // The low bits say which edge: WMSZ_ values, as HTLEFT onwards.
          int edge = (int)(wp & 0x0F);
          if (edge >= WMSZ_LEFT && edge <= WMSZ_BOTTOMRIGHT) MoveSizeLoop(w, unicode, HTLEFT + edge - WMSZ_LEFT);
          return 0;
        }
        case SC_CONTEXTHELP:
        default:
          return 0;
      }
    }
    case WM_NCLBUTTONDOWN:
      switch (wp) {
        case HTCAPTION:
          SetActiveWindow(w->TopLevel()->hwnd);
          if (!(w->style & WS_MAXIMIZE)) SendTo(w, WM_SYSCOMMAND, SC_MOVE + HTCAPTION, lp, unicode);
          return 0;
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOM:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
          SetActiveWindow(w->TopLevel()->hwnd);
          SendTo(w, WM_SYSCOMMAND, SC_SIZE + (wp - HTLEFT + WMSZ_LEFT), lp, unicode);
          return 0;
        case HTSYSMENU:
          SetActiveWindow(w->TopLevel()->hwnd);
          if (w->style & WS_SYSMENU) SendTo(w, WM_SYSCOMMAND, SC_MOUSEMENU + HTSYSMENU, lp, unicode);
          return 0;
        case HTCLOSE: PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, lp); return 0;
        case HTMINBUTTON: PostMessageW(h, WM_SYSCOMMAND, SC_MINIMIZE, lp); return 0;
        case HTMAXBUTTON:
          PostMessageW(h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE, lp);
          return 0;
        case HTVSCROLL: SendTo(w, WM_SYSCOMMAND, SC_VSCROLL + HTVSCROLL, lp, unicode); return 0;
        case HTHSCROLL: SendTo(w, WM_SYSCOMMAND, SC_HSCROLL + HTHSCROLL, lp, unicode); return 0;
        case HTMENU: SendTo(w, WM_SYSCOMMAND, SC_MOUSEMENU + HTMENU, lp, unicode); return 0;
      }
      return 0;
    case WM_NCLBUTTONDBLCLK:
      if (wp == HTCAPTION && (w->style & WS_MAXIMIZEBOX))
        PostMessageW(h, WM_SYSCOMMAND, (w->style & WS_MAXIMIZE) ? SC_RESTORE : SC_MAXIMIZE, lp);
      else if (wp == HTSYSMENU)
        PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, lp);
      else if (wp == HTVSCROLL || wp == HTHSCROLL)
        return DefCore(h, WM_NCLBUTTONDOWN, wp, lp, unicode);
      return 0;
    case WM_NCRBUTTONUP:
      if (wp == HTCAPTION || wp == HTSYSMENU) SendTo(w, WM_CONTEXTMENU, (WPARAM)h, lp, unicode);
      return 0;
    case WM_RBUTTONUP: {
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      ClientToScreen(h, &p);
      SendTo(w, WM_CONTEXTMENU, (WPARAM)h, MAKELPARAM(p.x, p.y), unicode);
      return 0;
    }
    case WM_CONTEXTMENU:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_APPCOMMAND:
    case WM_HELP:
      // Children pass these up.
      if (w->Child() && w->parent) return SendTo(w->parent, msg, wp, lp, unicode);
      return 0;
    case WM_MOUSEACTIVATE:
      if (w->Child() && w->parent) {
        LRESULT r = SendTo(w->parent, msg, wp, lp, unicode);
        if (r) return r;
      }
      return MA_ACTIVATE;
    case WM_SETCURSOR: {
      if (w->Child() && w->parent && SendTo(w->parent, msg, wp, lp, unicode)) return TRUE;
      // A parent asked about a child leaves the child its own cursor.
      if ((HWND)wp != h) return FALSE;
      LPCWSTR shape = (LPCWSTR)IDC_ARROW;
      switch ((short)LOWORD(lp)) {
        case HTCLIENT: {
          HCURSOR c = w->cls ? w->cls->wc.hCursor : NULL;
          if (c) {
            SetCursor(c);
            return TRUE;
          }
          return FALSE;
        }
        case HTLEFT: case HTRIGHT: shape = (LPCWSTR)IDC_SIZEWE; break;
        case HTTOP: case HTBOTTOM: shape = (LPCWSTR)IDC_SIZENS; break;
        case HTTOPLEFT: case HTBOTTOMRIGHT: shape = (LPCWSTR)IDC_SIZENWSE; break;
        case HTTOPRIGHT: case HTBOTTOMLEFT: shape = (LPCWSTR)IDC_SIZENESW; break;
      }
      SetCursor(LoadCursorW(NULL, shape));
      return FALSE;
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && !(w->style & WS_MINIMIZE)) SetFocus(h);
      return 0;
    case WM_WINDOWPOSCHANGING: {
      // Top-level windows keep within their tracking size.
      WINDOWPOS* p = (WINDOWPOS*)lp;
      if (p && !(p->flags & SWP_NOSIZE) && !w->Child() && (w->style & WS_THICKFRAME) &&
          !(w->style & (WS_MINIMIZE | WS_MAXIMIZE))) {
        MINMAXINFO mmi = {};
        SIZE s = GetBackend()->ScreenSize();
        mmi.ptMaxSize.x = s.cx;
        mmi.ptMaxSize.y = s.cy;
        mmi.ptMinTrackSize.x = GetSystemMetrics(SM_CXMINTRACK);
        mmi.ptMinTrackSize.y = GetSystemMetrics(SM_CYMINTRACK);
        mmi.ptMaxTrackSize.x = 0x7FFF;
        mmi.ptMaxTrackSize.y = 0x7FFF;
        SendTo(w, WM_GETMINMAXINFO, 0, (LPARAM)&mmi, unicode);
        if (p->cx < mmi.ptMinTrackSize.x) p->cx = mmi.ptMinTrackSize.x;
        if (p->cy < mmi.ptMinTrackSize.y) p->cy = mmi.ptMinTrackSize.y;
        if (p->cx > mmi.ptMaxTrackSize.x) p->cx = mmi.ptMaxTrackSize.x;
        if (p->cy > mmi.ptMaxTrackSize.y) p->cy = mmi.ptMaxTrackSize.y;
      }
      return 0;
    }
    case WM_WINDOWPOSCHANGED: {
      WINDOWPOS* p = (WINDOWPOS*)lp;
      if (!p) return 0;
      if (!(p->flags & SWP_NOMOVE)) {
        RECT c = w->Child() ? MakeRect(w->rect.left + w->client.left, w->rect.top + w->client.top, 0, 0)
                            : ScreenClient(w);
        SendTo(w, WM_MOVE, 0, MAKELPARAM(c.left, c.top), unicode);
      }
      if (!(p->flags & SWP_NOSIZE) || (p->flags & SWP_FRAMECHANGED)) {
        WPARAM kind = (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : (w->style & WS_MINIMIZE) ? SIZE_MINIMIZED : SIZE_RESTORED;
        SendTo(w, WM_SIZE, kind,
               MAKELPARAM(w->client.right - w->client.left, w->client.bottom - w->client.top), unicode);
      }
      return 0;
    }
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      bool window = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
      if (msg == WM_CTLCOLORSCROLLBAR) {
        SetTextColor(dc, GetSysColor(COLOR_BTNFACE));
        SetBkColor(dc, GetSysColor(COLOR_3DHILIGHT));
        return (LRESULT)GetSysColorBrush(COLOR_SCROLLBAR);
      }
      SetTextColor(dc, GetSysColor(window ? COLOR_WINDOWTEXT : COLOR_BTNTEXT));
      SetBkColor(dc, GetSysColor(window ? COLOR_WINDOW : COLOR_BTNFACE));
      return (LRESULT)GetSysColorBrush(window ? COLOR_WINDOW : COLOR_BTNFACE);
    }
    case WM_KEYDOWN:
      g_altAlone = false;
      if (wp == VK_F10) PostMessageW(h, WM_SYSCOMMAND, SC_KEYMENU, 0);
      return 0;
    case WM_SYSKEYUP:
      // Alt pressed and let go with no key between enters the menu bar.
      if (wp == VK_MENU && g_altAlone) PostMessageW(w->TopLevel()->hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
      g_altAlone = false;
      return 0;
    case WM_SYSKEYDOWN:
      g_altAlone = wp == VK_MENU && !(HIWORD(lp) & KF_REPEAT);
      if (wp == VK_F10) {
        PostMessageW(w->TopLevel()->hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
        return 0;
      }
      if (wp == VK_F4 && (HIWORD(lp) & KF_ALTDOWN)) {
        PostMessageW(w->TopLevel()->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        return 0;
      }
      return 0;
    case WM_SYSCHAR:
      if (wp == VK_SPACE || (HIWORD(lp) & KF_ALTDOWN))
        PostMessageW(w->TopLevel()->hwnd, WM_SYSCOMMAND, SC_KEYMENU, wp == VK_SPACE ? ' ' : wp);
      return 0;
    case WM_SETREDRAW:
      // As on Windows, turning drawing off clears WS_VISIBLE without hiding.
      if (wp)
        w->style |= WS_VISIBLE;
      else
        w->style &= ~WS_VISIBLE;
      return 0;
    case WM_SHOWWINDOW:
      return 0;
    case WM_CANCELMODE:
      EndMenu();
      if (GetCapture() == h) ReleaseCapture();
      return 0;
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
      return -1;
    case WM_QUERYOPEN:
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_NOTIFYFORMAT:
      return w->unicode ? NFR_UNICODE : NFR_ANSI;
    case WM_GETICON:
      return (LRESULT)(wp == ICON_SMALL || wp == ICON_SMALL2 ? (w->iconSmall ? w->iconSmall : w->iconBig) : w->iconBig);
    case WM_SETICON: {
      HICON old;
      if (wp == ICON_SMALL) {
        old = w->iconSmall;
        w->iconSmall = (HICON)lp;
      } else {
        old = w->iconBig;
        w->iconBig = (HICON)lp;
      }
      return (LRESULT)old;
    }
    case WM_QUERYDRAGICON:
      return (LRESULT)(w->cls ? w->cls->wc.hIcon : NULL);
    case WM_PRINT:
      if ((lp & PRF_CHECKVISIBLE) && !IsWindowVisible(h)) return 0;
      if (lp & PRF_ERASEBKGND) SendTo(w, WM_ERASEBKGND, wp, 0, unicode);
      if (lp & PRF_CLIENT) SendTo(w, WM_PRINTCLIENT, wp, lp, unicode);
      return 0;
  }
  return 0;
}

extern "C" {

LRESULT WINAPI DefWindowProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefCore(h, msg, wp, lp, true); }
LRESULT WINAPI DefWindowProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefCore(h, msg, wp, lp, false); }

// ---------------------------------------------------------------------
// 3-D edges and focus rectangles

BOOL WINAPI DrawEdge(HDC hdc, LPRECT rc, UINT edge, UINT flags) {
  if (!rc) return FALSE;
  RECT r = *rc;
  // Light and dark colors for the outer and inner lines.
  int outerTL, outerBR, innerTL, innerBR;
  bool soft = (flags & BF_SOFT) != 0;
  if (edge & BDR_RAISEDOUTER) {
    outerTL = soft ? COLOR_BTNHIGHLIGHT : COLOR_3DLIGHT;
    outerBR = COLOR_3DDKSHADOW;
  } else if (edge & BDR_SUNKENOUTER) {
    outerTL = soft ? COLOR_3DDKSHADOW : COLOR_BTNSHADOW;
    outerBR = COLOR_BTNHIGHLIGHT;
  } else {
    outerTL = outerBR = -1;
  }
  if (edge & BDR_RAISEDINNER) {
    innerTL = soft ? COLOR_3DLIGHT : COLOR_BTNHIGHLIGHT;
    innerBR = COLOR_BTNSHADOW;
  } else if (edge & BDR_SUNKENINNER) {
    innerTL = soft ? COLOR_BTNSHADOW : COLOR_3DDKSHADOW;
    innerBR = COLOR_3DLIGHT;
  } else {
    innerTL = innerBR = -1;
  }
  if (flags & BF_FLAT) {
    if (outerTL >= 0) outerTL = outerBR = COLOR_BTNSHADOW;
    if (innerTL >= 0) innerTL = innerBR = COLOR_BTNFACE;
  }
  if (flags & BF_MONO) {
    if (outerTL >= 0) outerTL = outerBR = COLOR_WINDOWFRAME;
    if (innerTL >= 0) innerTL = innerBR = COLOR_WINDOW;
  }
  int rings[2][2] = {{outerTL, outerBR}, {innerTL, innerBR}};
  for (auto& ring : rings) {
    if (ring[0] < 0) continue;
    if (flags & BF_LEFT) Fill(hdc, r.left, r.top, r.left + 1, r.bottom, ring[0]);
    if (flags & BF_TOP) Fill(hdc, r.left, r.top, r.right, r.top + 1, ring[0]);
    if (flags & BF_RIGHT) Fill(hdc, r.right - 1, r.top, r.right, r.bottom, ring[1]);
    if (flags & BF_BOTTOM) Fill(hdc, r.left, r.bottom - 1, r.right, r.bottom, ring[1]);
    if (flags & BF_LEFT) r.left++;
    if (flags & BF_TOP) r.top++;
    if (flags & BF_RIGHT) r.right--;
    if (flags & BF_BOTTOM) r.bottom--;
  }
  if (flags & BF_MIDDLE) FillRect(hdc, &r, GetSysColorBrush((flags & BF_MONO) ? COLOR_WINDOW : COLOR_BTNFACE));
  if (flags & BF_ADJUST) *rc = r;
  return TRUE;
}

BOOL WINAPI DrawFocusRect(HDC hdc, const RECT* r) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !r) return FALSE;
  RECT d = dc->ToDevice(*r);
  DrawScope ds(dc);
  if (!ds.ok) return FALSE;
  // A dotted line that inverts every other pixel.
  auto dot = [&](int x, int y) {
    if (((x + y) & 1) || !ds.clip.Contains(x, y)) return;
    DWORD& p = ds.At(x, y);
    p = ~p & 0xFFFFFF;
  };
  for (int x = d.left; x < d.right; x++) {
    dot(x, d.top);
    if (d.bottom - 1 > d.top) dot(x, d.bottom - 1);
  }
  for (int y = d.top + 1; y < d.bottom - 1; y++) {
    dot(d.left, y);
    if (d.right - 1 > d.left) dot(d.right - 1, y);
  }
  ds.Dirty(d);
  return TRUE;
}

// ---------------------------------------------------------------------
// Frame controls: the parts of buttons, captions, scroll bars and menus

namespace {

// Draws a glyph given as rows of pixels, centered in a box.
void DrawRows(HDC dc, const RECT& box, const char* const* rows, int n, COLORREF color) {
  int w = (int)strlen(rows[0]);
  int x0 = (box.left + box.right - w) / 2, y0 = (box.top + box.bottom - n) / 2;
  HBRUSH br = CreateSolidBrush(color);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < w;) {
      if (rows[y][x] != 'X') {
        x++;
        continue;
      }
      int e = x;
      while (e < w && rows[y][e] == 'X') e++;
      RECT r = MakeRect(x0 + x, y0 + y, x0 + e, y0 + y + 1);
      FillRect(dc, &r, br);
      x = e;
    }
  DeleteObject(br);
}

const char* const kCheckRows[] = {"......X", ".....XX", "X...XXX", "XX.XXX.", "XXXXX..", ".XXX...", "..X...."};
const char* const kBulletRows[] = {".XXXX.", "XXXXXX", "XXXXXX", "XXXXXX", "XXXXXX", ".XXXX."};
const char* const kArrowRows[] = {"X...", "XX..", "XXX.", "XXXX", "XXX.", "XX..", "X..."};

// A square as large as fits, centered.
RECT Square(const RECT& r) {
  int w = r.right - r.left, h = r.bottom - r.top, s = w < h ? w : h;
  int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
  return MakeRect(x, y, x + s, y + s);
}

void DrawCheckBox(HDC dc, RECT r, UINT state) {
  r = Square(r);
  if (state & DFCS_FLAT) {
    FrameRect(dc, &r, GetSysColorBrush(COLOR_BTNSHADOW));
    InflateRect(&r, -1, -1);
  } else if (state & DFCS_MONO) {
    FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
    InflateRect(&r, -1, -1);
  } else {
    DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT | BF_ADJUST);
  }
  bool third = (state & DFCS_BUTTON3STATE) && (state & DFCS_CHECKED);
  int face = (state & (DFCS_PUSHED | DFCS_INACTIVE)) || third ? COLOR_BTNFACE : COLOR_WINDOW;
  FillRect(dc, &r, GetSysColorBrush(face));
  if (state & DFCS_CHECKED)
    DrawRows(dc, r, kCheckRows, 7, GetSysColor(third || (state & DFCS_INACTIVE) ? COLOR_BTNSHADOW : COLOR_WINDOWTEXT));
}

void DrawRadio(HDC dc, RECT r, UINT state) {
  r = Square(r);
  if ((state & 0xFF) == DFCS_BUTTONRADIOMASK) {
    // A black disc on white: the mask for drawing an image of the button.
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(BLACK_PEN)), ob = SelectObject(dc, GetStockObject(BLACK_BRUSH));
    Ellipse(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    return;
  }
  // Two rings, dark at the top left and light at the bottom right, like
  // a sunken edge, around a white disc.
  auto ring = [&](RECT b, int tl, int br) {
    HPEN p1 = CreatePen(PS_SOLID, 1, GetSysColor(tl)), p2 = CreatePen(PS_SOLID, 1, GetSysColor(br));
    HGDIOBJ old = SelectObject(dc, p1);
    Arc(dc, b.left, b.top, b.right, b.bottom, b.right, b.top, b.left, b.bottom);
    SelectObject(dc, p2);
    Arc(dc, b.left, b.top, b.right, b.bottom, b.left, b.bottom, b.right, b.top);
    SelectObject(dc, old);
    DeleteObject(p1);
    DeleteObject(p2);
  };
  int face = state & (DFCS_PUSHED | DFCS_INACTIVE) ? COLOR_BTNFACE : COLOR_WINDOW;
  HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN)), ob = SelectObject(dc, GetSysColorBrush(face));
  Ellipse(dc, r.left + 1, r.top + 1, r.right, r.bottom);
  SelectObject(dc, op);
  SelectObject(dc, ob);
  if (state & DFCS_FLAT) {
    ring(r, COLOR_BTNSHADOW, COLOR_BTNSHADOW);
  } else {
    ring(r, COLOR_BTNSHADOW, COLOR_BTNHIGHLIGHT);
    RECT in = r;
    InflateRect(&in, -1, -1);
    ring(in, COLOR_3DDKSHADOW, COLOR_3DLIGHT);
  }
  if (state & DFCS_CHECKED) {
    RECT dot = r;
    int d = (r.right - r.left) / 3;
    InflateRect(&dot, -d, -d);
    COLORREF c = GetSysColor(state & DFCS_INACTIVE ? COLOR_BTNSHADOW : COLOR_WINDOWTEXT);
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    op = SelectObject(dc, pen);
    ob = SelectObject(dc, br);
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(br);
    DeleteObject(pen);
  }
}

void DrawButtonFace(HDC dc, RECT* r, UINT state) {
  UINT edge = (state & (DFCS_PUSHED | DFCS_CHECKED)) ? EDGE_SUNKEN : EDGE_RAISED;
  UINT flags = BF_RECT | BF_MIDDLE | BF_ADJUST;
  if (state & DFCS_FLAT) flags |= BF_FLAT;
  if (state & DFCS_MONO) flags |= BF_MONO;
  if ((state & DFCS_PUSHED) && !(state & DFCS_CHECKED) && !(state & (DFCS_FLAT | DFCS_MONO))) {
    // A pushed button is a dark frame around a shadow line.
    FrameRect(dc, r, GetSysColorBrush(COLOR_3DDKSHADOW));
    InflateRect(r, -1, -1);
    FrameRect(dc, r, GetSysColorBrush(COLOR_BTNSHADOW));
    InflateRect(r, -1, -1);
    FillRect(dc, r, GetSysColorBrush(COLOR_BTNFACE));
    return;
  }
  DrawEdge(dc, r, edge, flags);
}

}  // namespace

BOOL WINAPI DrawFrameControl(HDC dc, LPRECT rc, UINT type, UINT state) {
  if (!rc) return FALSE;
  RECT r = *rc;
  switch (type) {
    case DFC_BUTTON: {
      UINT kind = state & 0xFF;
      if (kind & DFCS_BUTTONPUSH) {
        DrawButtonFace(dc, &r, state);
        if (state & DFCS_ADJUSTRECT) *rc = r;
      } else if (kind & (DFCS_BUTTONRADIO | DFCS_BUTTONRADIOIMAGE | DFCS_BUTTONRADIOMASK)) {
        DrawRadio(dc, r, (state & DFCS_BUTTONRADIOMASK) ? (state & ~0xFF) | DFCS_BUTTONRADIOMASK : state);
      } else {
        DrawCheckBox(dc, r, state);
      }
      return TRUE;
    }
    case DFC_CAPTION: {
      UINT edge = (state & DFCS_PUSHED) ? EDGE_SUNKEN : EDGE_RAISED;
      RECT box = r;
      DrawEdge(dc, &box, edge, BF_RECT | BF_MIDDLE | BF_ADJUST | ((state & DFCS_FLAT) ? BF_FLAT : 0));
      int glyph;
      switch (state & 0xFF) {
        case DFCS_CAPTIONCLOSE: glyph = 0; break;
        case DFCS_CAPTIONMAX: glyph = 1; break;
        case DFCS_CAPTIONRESTORE: glyph = 2; break;
        case DFCS_CAPTIONMIN: glyph = 3; break;
        default: glyph = 4; break;
      }
      if (state & DFCS_PUSHED) OffsetRect(&box, 1, 1);
      COLORREF c = GetSysColor((state & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
      if (glyph == 4) {
        // Help: a question mark.
        static const char* const q[] = {".XXX.", "XX.XX", "...XX", "..XX.", "..XX.", ".....", "..XX."};
        DrawRows(dc, box, q, 7, c);
      } else {
        // The caption buttons' glyphs, drawn the way the frame draws them.
        int mode = glyph;
        HBRUSH br = CreateSolidBrush(c);
        int cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
        if (mode == 0) {
          for (int i = -3; i <= 3; i++) {
            RECT a = MakeRect(cx + i - 1, cy + i, cx + i + 1, cy + i + 1);
            RECT b = MakeRect(cx - i - 1, cy + i, cx - i + 1, cy + i + 1);
            FillRect(dc, &a, br);
            FillRect(dc, &b, br);
          }
        } else if (mode == 1) {
          RECT o = MakeRect(cx - 4, cy - 4, cx + 5, cy + 4);
          FrameRect(dc, &o, br);
          RECT t = MakeRect(o.left, o.top + 1, o.right, o.top + 2);
          FillRect(dc, &t, br);
        } else if (mode == 2) {
          RECT a = MakeRect(cx - 2, cy - 5, cx + 5, cy + 1);
          RECT b = MakeRect(cx - 5, cy - 2, cx + 2, cy + 4);
          FrameRect(dc, &a, br);
          FillRect(dc, &b, GetSysColorBrush(COLOR_BTNFACE));
          FrameRect(dc, &b, br);
        } else {
          RECT a = MakeRect(cx - 4, cy + 2, cx + 3, cy + 4);
          FillRect(dc, &a, br);
        }
        DeleteObject(br);
      }
      return TRUE;
    }
    case DFC_SCROLL: {
      UINT kind = state & 0xFF;
      if (kind == DFCS_SCROLLSIZEGRIP || kind == DFCS_SCROLLSIZEGRIPRIGHT) {
        FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
        // Diagonal lines in the bottom right corner.
        int size = (r.right - r.left) < (r.bottom - r.top) ? r.right - r.left : r.bottom - r.top;
        for (int i = 2; i < size; i += 4) {
          for (int k = 0; k < 2; k++) {
            HPEN p = CreatePen(PS_SOLID, 1, GetSysColor(k == 0 ? COLOR_BTNHIGHLIGHT : COLOR_BTNSHADOW));
            HGDIOBJ old = SelectObject(dc, p);
            MoveToEx(dc, r.right - 1, r.bottom - i - k - 1, NULL);
            LineTo(dc, r.right - i - k - 1, r.bottom - 1);
            SelectObject(dc, old);
            DeleteObject(p);
          }
        }
        return TRUE;
      }
      DrawButtonFace(dc, &r, state & ~DFCS_CHECKED);
      if (state & DFCS_PUSHED) OffsetRect(&r, 1, 1);
      int dir = kind == DFCS_SCROLLUP ? 0 : kind == DFCS_SCROLLLEFT ? 2 : kind == DFCS_SCROLLRIGHT ? 3 : 1;
      HBRUSH br = GetSysColorBrush((state & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
      int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
      for (int i = 0; i < 4; i++) {
        RECT line;
        switch (dir) {
          case 0: line = MakeRect(cx - i, cy - 2 + i, cx + i + 1, cy - 1 + i); break;
          case 1: line = MakeRect(cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i); break;
          case 2: line = MakeRect(cx - 2 + i, cy - i, cx - 1 + i, cy + i + 1); break;
          default: line = MakeRect(cx - 1 + i, cy - 3 + i, cx + i, cy + 4 - i); break;
        }
        FillRect(dc, &line, br);
      }
      return TRUE;
    }
    case DFC_MENU:
    case DFC_POPUPMENU: {
      // Black on white, as Windows draws them, for use as a mask.
      FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
      switch (state & 0xFF) {
        case DFCS_MENUCHECK: DrawRows(dc, r, kCheckRows, 7, RGB(0, 0, 0)); break;
        case DFCS_MENUBULLET: DrawRows(dc, r, kBulletRows, 6, RGB(0, 0, 0)); break;
        default: DrawRows(dc, r, kArrowRows, 7, RGB(0, 0, 0)); break;
      }
      return TRUE;
    }
  }
  return FALSE;
}

// ---------------------------------------------------------------------
// Window scroll bars

int WINAPI SetScrollInfo(HWND h, int bar, LPCSCROLLINFO si, BOOL redraw) {
  if (bar == SB_CTL) return (int)SendMessageW(h, SBM_SETSCROLLINFO, redraw, (LPARAM)si);
  Window* w = GetWindowOrFail(h);
  if (!w || !si || (bar != SB_HORZ && bar != SB_VERT)) return 0;
  ScrollBar* sb = BarOf(w, bar);
  SCROLLINFO& s = sb->info;
  if (si->fMask & SIF_RANGE) {
    s.nMin = si->nMin;
    s.nMax = si->nMax;
  }
  if (si->fMask & SIF_PAGE) s.nPage = si->nPage;
  if (si->fMask & SIF_POS) s.nPos = si->nPos;
  int range = s.nMax - s.nMin + 1;
  if (range < 0) range = 0;
  if ((int)s.nPage > range) s.nPage = (UINT)range;
  if (s.nPos < s.nMin) s.nPos = s.nMin;
  if (s.nPos > MaxPos(s)) s.nPos = MaxPos(s);
  if (si->fMask & (SIF_RANGE | SIF_PAGE)) {
    // A bar shows while there is something to scroll.
    bool needed = s.nMax > s.nMin && (s.nPage == 0 || (int)s.nPage < range);
    if (si->fMask & SIF_DISABLENOSCROLL) {
      sb->enabled = needed;
      SetBarShown(w, bar, true);
    } else {
      sb->enabled = true;
      SetBarShown(w, bar, needed);
    }
  }
  if (redraw) RedrawBars(w);
  return s.nPos;
}

BOOL WINAPI GetScrollInfo(HWND h, int bar, LPSCROLLINFO si) {
  if (bar == SB_CTL) return (BOOL)SendMessageW(h, SBM_GETSCROLLINFO, 0, (LPARAM)si);
  Window* w = GetWindowOrFail(h);
  if (!w || !si || (bar != SB_HORZ && bar != SB_VERT)) return FALSE;
  const SCROLLINFO& s = BarOf(w, bar)->info;
  if (si->fMask & SIF_RANGE) {
    si->nMin = s.nMin;
    si->nMax = s.nMax;
  }
  if (si->fMask & SIF_PAGE) si->nPage = s.nPage;
  if (si->fMask & SIF_POS) si->nPos = s.nPos;
  if (si->fMask & SIF_TRACKPOS) si->nTrackPos = s.nTrackPos;
  return TRUE;
}

int WINAPI SetScrollPos(HWND h, int bar, int pos, BOOL redraw) {
  SCROLLINFO si = {sizeof si, SIF_POS};
  GetScrollInfo(h, bar, &si);
  int old = si.nPos;
  si.fMask = SIF_POS;
  si.nPos = pos;
  SetScrollInfo(h, bar, &si, redraw);
  return old;
}

int WINAPI GetScrollPos(HWND h, int bar) {
  SCROLLINFO si = {sizeof si, SIF_POS};
  return GetScrollInfo(h, bar, &si) ? si.nPos : 0;
}

BOOL WINAPI SetScrollRange(HWND h, int bar, int min, int max, BOOL redraw) {
  SCROLLINFO si = {sizeof si, SIF_RANGE};
  si.nMin = min;
  si.nMax = max;
  SetScrollInfo(h, bar, &si, redraw);
  return TRUE;
}

BOOL WINAPI GetScrollRange(HWND h, int bar, LPINT min, LPINT max) {
  SCROLLINFO si = {sizeof si, SIF_RANGE};
  if (!GetScrollInfo(h, bar, &si)) return FALSE;
  if (min) *min = si.nMin;
  if (max) *max = si.nMax;
  return TRUE;
}

BOOL WINAPI ShowScrollBar(HWND h, int bar, BOOL show) {
  if (bar == SB_CTL) return ShowWindow(h, show ? SW_SHOW : SW_HIDE), TRUE;
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  SetBarShown(w, bar, show != FALSE);
  return TRUE;
}

BOOL WINAPI EnableScrollBar(HWND h, UINT bar, UINT arrows) {
  if (bar == SB_CTL) return EnableWindow(h, arrows != ESB_DISABLE_BOTH), TRUE;
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  bool on = arrows != ESB_DISABLE_BOTH;
  if (bar == SB_HORZ || bar == SB_BOTH) w->hscroll.enabled = on;
  if (bar == SB_VERT || bar == SB_BOTH) w->vscroll.enabled = on;
  RedrawBars(w);
  return TRUE;
}

// ---------------------------------------------------------------------
// The caret

BOOL WINAPI CreateCaret(HWND h, HBITMAP bm, int width, int height) {
  if (!GetWindowOrFail(h)) return FALSE;
  DestroyCaret();
  g_caret.hwnd = h;
  g_caret.bitmap = bm;
  g_caret.width = width ? width : GetSystemMetrics(SM_CXBORDER);
  g_caret.height = height ? height : GetSystemMetrics(SM_CYBORDER);
  if (bm && bm != (HBITMAP)1) {
    BITMAP b;
    if (GetObjectW(bm, sizeof b, &b)) {
      g_caret.width = b.bmWidth;
      g_caret.height = b.bmHeight;
    }
  }
  g_caret.pos.x = g_caret.pos.y = 0;
  g_caret.hidden = 1;
  g_caret.on = false;
  return TRUE;
}

BOOL WINAPI DestroyCaret(void) {
  if (!g_caret.hwnd) return FALSE;
  CaretOff();
  StopCaretTimer();
  g_caret.hwnd = NULL;
  return TRUE;
}

BOOL WINAPI ShowCaret(HWND h) {
  if (!g_caret.hwnd || (h && h != g_caret.hwnd)) return FALSE;
  if (g_caret.hidden > 0 && --g_caret.hidden == 0) {
    ToggleCaret();
    if (!g_caret.timer) g_caret.timer = SetTimer(NULL, 0, g_caret.blink, CaretBlink);
  }
  return TRUE;
}

BOOL WINAPI HideCaret(HWND h) {
  if (!g_caret.hwnd || (h && h != g_caret.hwnd)) return FALSE;
  if (g_caret.hidden++ == 0) {
    CaretOff();
    StopCaretTimer();
  }
  return TRUE;
}

BOOL WINAPI SetCaretPos(int x, int y) {
  if (!g_caret.hwnd) return FALSE;
  bool was = g_caret.on;
  CaretOff();
  g_caret.pos.x = x;
  g_caret.pos.y = y;
  if (was || g_caret.hidden == 0) ToggleCaret();
  return TRUE;
}

BOOL WINAPI GetCaretPos(LPPOINT p) {
  if (!p) return FALSE;
  *p = g_caret.pos;
  return TRUE;
}

UINT WINAPI GetCaretBlinkTime(void) { return g_caret.blink; }
BOOL WINAPI SetCaretBlinkTime(UINT ms) {
  g_caret.blink = ms ? ms : 530;
  return TRUE;
}

}  // extern "C"
