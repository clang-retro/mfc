// comctl_rebar.cpp - the rebar, the pager and the SysLink control.
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

DWORD StyleOf(HWND h) { return (DWORD)GetWindowLongW(h, GWL_STYLE); }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

int FontHeightOf(HWND h, HFONT f) {
  HDC dc = GetDC(h);
  HGDIOBJ old = SelectObject(dc, f ? f : ControlFont());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return tm.tmHeight;
}

int TextWidth(HWND h, HFONT f, const std::wstring& s) {
  if (s.empty()) return 0;
  HDC dc = GetDC(h);
  HGDIOBJ old = SelectObject(dc, f ? f : ControlFont());
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return sz.cx;
}

// =====================================================================
// Rebar

const int kGripper = 9;   // the gripper's width with its margins
const int kBandGap = 2;   // around each band's contents
const int kRowBorder = 2;  // between rows, with RBS_BANDBORDERS

struct Band {
  UINT style = 0;
  COLORREF fore = CLR_DEFAULT, back = CLR_DEFAULT;
  std::wstring text;
  int image = -1;
  HWND child = NULL;
  UINT cxMinChild = 0, cyMinChild = 0, cx = 0;
  HBITMAP bmp = NULL;
  UINT id = 0;
  UINT cyChild = 0, cyMaxChild = 0, cyIntegral = 0, cxIdeal = 0;
  LPARAM lParam = 0;
  int cxHeader = -1;  // -1: worked out from the gripper and text
  // Layout
  RECT rect = {0, 0, 0, 0};  // the band, in client coordinates (horizontal)
  int row = 0;
};

struct Rebar {
  HWND hwnd = NULL;
  HWND notify = NULL;  // RB_SETPARENT
  HWND tips = NULL;
  HIMAGELIST himl = NULL;
  HFONT font = NULL;
  COLORREF back = CLR_DEFAULT, fore = CLR_DEFAULT;
  std::vector<Band> bands;
  std::vector<int> rowHeights;
  int height = 0;
  // Dragging a gripper: the band, and where the drag started.
  int drag = -1, dragX = 0, dragCx = 0;
};

Rebar* RebarOf(HWND h) { return (Rebar*)GetWindowLongPtrW(h, 0); }
HWND Target(Rebar* r) { return r->notify ? r->notify : GetParent(r->hwnd); }
bool Vertical(Rebar* r) { return (StyleOf(r->hwnd) & CCS_VERT) != 0; }

bool Visible(const Band& b) { return !(b.style & RBBS_HIDDEN); }

bool HasGripper(Rebar* r, const Band& b) {
  if (b.style & RBBS_NOGRIPPER) return false;
  if (b.style & RBBS_GRIPPERALWAYS) return true;
  if (b.style & RBBS_FIXEDSIZE) return false;
  int n = 0;
  for (const Band& o : r->bands)
    if (Visible(o)) n++;
  return n > 1;
}

int HeaderWidth(Rebar* r, const Band& b) {
  if (b.cxHeader >= 0) return b.cxHeader;
  int w = HasGripper(r, b) ? kGripper : kBandGap;
  if (r->himl && b.image >= 0) {
    int cx = 0, cy = 0;
    ImageList_GetIconSize(r->himl, &cx, &cy);
    w += cx + 3;
  }
  if (!b.text.empty()) w += TextWidth(r->hwnd, r->font, b.text) + 4;
  return w;
}

int BandHeight(Rebar* r, const Band& b) {
  int h = b.cyMinChild;
  if ((b.style & RBBS_VARIABLEHEIGHT) && b.cyChild > h) h = b.cyChild;
  if (!b.text.empty()) h = std::max(h, FontHeightOf(r->hwnd, r->font));
  if (r->himl && b.image >= 0) {
    int cx = 0, cy = 0;
    ImageList_GetIconSize(r->himl, &cx, &cy);
    h = std::max(h, cy);
  }
  return h + 2 * kBandGap;
}

// The rebar's length along its bands, and the band rectangles as if it
// were horizontal; a vertical rebar swaps them when it places them.
int Length(Rebar* r) {
  RECT c;
  GetClientRect(r->hwnd, &c);
  return Vertical(r) ? c.bottom : c.right;
}

RECT Oriented(Rebar* r, const RECT& h) {
  if (!Vertical(r)) return h;
  RECT v = {h.top, h.left, h.bottom, h.right};
  return v;
}

void Layout(Rebar* r, bool notify = true) {
  int len = Length(r);
  bool borders = (StyleOf(r->hwnd) & RBS_BANDBORDERS) != 0;
  // Rows: a new one at RBBS_BREAK, or when the bands' least widths no
  // longer fit.
  std::vector<std::vector<int>> rows;
  int used = 0;
  for (int i = 0; i < (int)r->bands.size(); i++) {
    Band& b = r->bands[i];
    if (!Visible(b)) continue;
    int need = HeaderWidth(r, b) + (int)b.cxMinChild + kBandGap;
    if (rows.empty() || ((b.style & RBBS_BREAK) && !rows.back().empty()) ||
        (!rows.back().empty() && len > 0 && used + need > len)) {
      rows.emplace_back();
      used = 0;
    }
    rows.back().push_back(i);
    used += need;
  }
  r->rowHeights.clear();
  int y = 0;
  for (size_t ri = 0; ri < rows.size(); ri++) {
    std::vector<int>& row = rows[ri];
    int rh = 0;
    for (int i : row) rh = std::max(rh, BandHeight(r, r->bands[i]));
    // Widths: what each band asks for, at least its least; the last band
    // takes the rest; too wide, and the bands give way from the right.
    std::vector<int> w(row.size()), lo(row.size());
    int total = 0;
    for (size_t k = 0; k < row.size(); k++) {
      Band& b = r->bands[row[k]];
      lo[k] = HeaderWidth(r, b) + (int)b.cxMinChild + kBandGap;
      w[k] = std::max(lo[k], (int)b.cx);
      total += w[k];
    }
    if (len > 0) {
      if (total < len) {
        w.back() += len - total;
      } else {
        for (size_t k = row.size(); k-- > 0 && total > len;) {
          int give = std::min(w[k] - lo[k], total - len);
          w[k] -= give;
          total -= give;
        }
      }
    }
    int x = 0;
    for (size_t k = 0; k < row.size(); k++) {
      Band& b = r->bands[row[k]];
      b.rect = RECT{x, y, x + w[k], y + rh};
      b.row = (int)ri;
      x += w[k];
    }
    r->rowHeights.push_back(rh);
    y += rh;
    if (borders && ri + 1 < rows.size()) y += kRowBorder;
  }
  for (Band& b : r->bands)
    if (!Visible(b)) b.rect = RECT{0, 0, 0, 0};
  // The children.
  for (Band& b : r->bands) {
    if (!b.child || !IsWindow(b.child)) continue;
    if (!Visible(b)) {
      ShowWindow(b.child, SW_HIDE);
      continue;
    }
    int hdr = HeaderWidth(r, b);
    int ch = b.cyMinChild;
    if ((b.style & RBBS_VARIABLEHEIGHT) && b.cyChild > (UINT)ch) ch = b.cyChild;
    RECT c = {b.rect.left + hdr, b.rect.top + (b.rect.bottom - b.rect.top - ch) / 2, b.rect.right - kBandGap, 0};
    c.bottom = c.top + ch;
    if (c.right < c.left) c.right = c.left;
    RECT o = Oriented(r, c);
    SetWindowPos(b.child, NULL, o.left, o.top, o.right - o.left, o.bottom - o.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  int old = r->height;
  r->height = y;
  // Without CCS_NORESIZE the rebar sizes itself to its rows.
  DWORD style = StyleOf(r->hwnd);
  if (!(style & CCS_NORESIZE)) {
    RECT wr;
    GetWindowRect(r->hwnd, &wr);
    HWND parent = GetParent(r->hwnd);
    MapWindowPoints(NULL, parent, (POINT*)&wr, 2);
    RECT pc;
    GetClientRect(parent, &pc);
    RECT nc = {0, 0, 0, 0};
    AdjustWindowRectEx(&nc, style, FALSE, GetWindowLongW(r->hwnd, GWL_EXSTYLE));
    int extra = Vertical(r) ? nc.right - nc.left : nc.bottom - nc.top;
    if (Vertical(r)) {
      int cy = (style & CCS_NOPARENTALIGN) ? wr.bottom - wr.top : pc.bottom;
      SetWindowPos(r->hwnd, NULL, wr.left, (style & CCS_NOPARENTALIGN) ? wr.top : 0, y + extra, cy,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      int cx = (style & CCS_NOPARENTALIGN) ? wr.right - wr.left : pc.right;
      SetWindowPos(r->hwnd, NULL, (style & CCS_NOPARENTALIGN) ? wr.left : 0, (style & CCS_NOPARENTALIGN) ? wr.top : 0,
                   cx, y + extra, SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  InvalidateRect(r->hwnd, NULL, TRUE);
  if (notify && old != y) SendNotify(r->hwnd, Target(r), RBN_HEIGHTCHANGE);
}

COLORREF BackOf(Rebar* r, const Band& b) {
  if (b.back != CLR_DEFAULT && b.back != CLR_NONE) return b.back;
  if (r->back != CLR_DEFAULT && r->back != CLR_NONE) return r->back;
  return GetSysColor(COLOR_BTNFACE);
}

void PaintRebar(Rebar* r, HDC dc) {
  RECT c;
  GetClientRect(r->hwnd, &c);
  COLORREF bk = r->back != CLR_DEFAULT && r->back != CLR_NONE ? r->back : GetSysColor(COLOR_BTNFACE);
  FillSolid(dc, c, bk);
  bool borders = (StyleOf(r->hwnd) & RBS_BANDBORDERS) != 0;
  HGDIOBJ oldFont = SelectObject(dc, r->font ? r->font : ControlFont());
  SetBkMode(dc, TRANSPARENT);
  for (Band& b : r->bands) {
    if (!Visible(b)) continue;
    RECT br = Oriented(r, b.rect);
    if (b.bmp) {
      BITMAP bm;
      if (GetObjectW(b.bmp, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ ob = SelectObject(mem, b.bmp);
        for (int y = br.top; y < br.bottom; y += bm.bmHeight)
          for (int x = br.left; x < br.right; x += bm.bmWidth)
            BitBlt(dc, x, y, std::min((int)bm.bmWidth, (int)(br.right - x)), std::min((int)bm.bmHeight, (int)(br.bottom - y)),
                   mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteDC(mem);
      }
    } else {
      FillSolid(dc, br, BackOf(r, b));
    }
    int x = b.rect.left;
    if (HasGripper(r, b) && b.cxHeader < 0) {
      RECT g = {x + 2, b.rect.top + 2, x + 5, b.rect.bottom - 2};
      RECT go = Oriented(r, g);
      DrawEdge(dc, &go, BDR_RAISEDINNER, BF_RECT);
      x += kGripper;
    } else {
      x += kBandGap;
    }
    if (r->himl && b.image >= 0) {
      int cx = 0, cy = 0;
      ImageList_GetIconSize(r->himl, &cx, &cy);
      RECT ir = {x, b.rect.top + (b.rect.bottom - b.rect.top - cy) / 2, x + cx, 0};
      RECT io = Oriented(r, ir);
      ImageList_Draw(r->himl, b.image, dc, io.left, io.top, ILD_TRANSPARENT);
      x += cx + 3;
    }
    if (!b.text.empty()) {
      COLORREF fc = b.fore != CLR_DEFAULT ? b.fore : r->fore != CLR_DEFAULT ? r->fore : GetSysColor(COLOR_BTNTEXT);
      SetTextColor(dc, fc);
      RECT tr = {x, b.rect.top, x + TextWidth(r->hwnd, r->font, b.text) + 4, b.rect.bottom};
      RECT to = Oriented(r, tr);
      DrawTextW(dc, b.text.c_str(), (int)b.text.size(), &to, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    }
    if (borders) {
      RECT e = Oriented(r, b.rect);
      if (b.rect.left > 0) {
        RECT l = Oriented(r, RECT{b.rect.left, b.rect.top, b.rect.left + 2, b.rect.bottom});
        DrawEdge(dc, &l, EDGE_ETCHED, Vertical(r) ? BF_TOP : BF_LEFT);
      }
      if (b.rect.bottom < r->height) {
        RECT bot = Oriented(r, RECT{b.rect.left, b.rect.bottom, b.rect.right, b.rect.bottom + kRowBorder});
        DrawEdge(dc, &bot, EDGE_ETCHED, Vertical(r) ? BF_LEFT : BF_TOP);
      }
      (void)e;
    }
  }
  SelectObject(dc, oldFont);
}

// REBARBANDINFO in, A or W; the fields past cbSize are left alone.
template <class INFO>
void ApplyBand(Rebar* r, Band& b, const INFO* in, bool unicode) {
  UINT size = in->cbSize;
  UINT m = in->fMask;
  auto has = [&](size_t end) { return size >= end; };
  if (m & RBBIM_STYLE) {
    UINT oldStyle = b.style;
    b.style = in->fStyle;
    if (b.child && ((oldStyle ^ b.style) & RBBS_HIDDEN))
      ShowWindow(b.child, (b.style & RBBS_HIDDEN) ? SW_HIDE : SW_SHOWNA);
  }
  if (m & RBBIM_COLORS) {
    b.fore = in->clrFore;
    b.back = in->clrBack;
  }
  if (m & RBBIM_TEXT) b.text = TextIn(in->lpText, unicode);
  if (m & RBBIM_IMAGE) b.image = in->iImage;
  if (m & RBBIM_CHILD) {
    if (b.child && b.child != in->hwndChild && IsWindow(b.child)) ShowWindow(b.child, SW_HIDE);
    b.child = in->hwndChild;
    if (b.child && GetParent(b.child) != r->hwnd) SetParent(b.child, r->hwnd);
  }
  if (m & RBBIM_CHILDSIZE) {
    b.cxMinChild = in->cxMinChild;
    b.cyMinChild = in->cyMinChild;
    if (has(offsetof(INFO, cyIntegral) + sizeof(UINT))) {
      b.cyChild = in->cyChild;
      b.cyMaxChild = in->cyMaxChild;
      b.cyIntegral = in->cyIntegral;
    }
  }
  if (m & RBBIM_SIZE) b.cx = in->cx;
  if (m & RBBIM_BACKGROUND) b.bmp = in->hbmBack;
  if (m & RBBIM_ID) b.id = in->wID;
  if ((m & RBBIM_IDEALSIZE) && has(offsetof(INFO, cxIdeal) + sizeof(UINT))) b.cxIdeal = in->cxIdeal;
  if ((m & RBBIM_LPARAM) && has(offsetof(INFO, lParam) + sizeof(LPARAM))) b.lParam = in->lParam;
  if ((m & RBBIM_HEADERSIZE) && has(offsetof(INFO, cxHeader) + sizeof(UINT))) b.cxHeader = (int)in->cxHeader;
}

template <class INFO>
void ReadBand(Rebar* r, const Band& b, INFO* out, bool unicode) {
  UINT size = out->cbSize;
  UINT m = out->fMask;
  auto has = [&](size_t end) { return size >= end; };
  if (m & RBBIM_STYLE) out->fStyle = b.style;
  if (m & RBBIM_COLORS) {
    out->clrFore = b.fore == CLR_DEFAULT ? GetSysColor(COLOR_BTNTEXT) : b.fore;
    out->clrBack = BackOf(r, b);
  }
  if (m & RBBIM_TEXT) CopyOut(b.text, out->lpText, (int)out->cch, unicode);
  if (m & RBBIM_IMAGE) out->iImage = b.image;
  if (m & RBBIM_CHILD) out->hwndChild = b.child;
  if (m & RBBIM_CHILDSIZE) {
    out->cxMinChild = b.cxMinChild;
    out->cyMinChild = b.cyMinChild;
    if (has(offsetof(INFO, cyIntegral) + sizeof(UINT))) {
      out->cyChild = b.cyChild;
      out->cyMaxChild = b.cyMaxChild;
      out->cyIntegral = b.cyIntegral;
    }
  }
  if (m & RBBIM_SIZE) out->cx = b.rect.right > b.rect.left ? b.rect.right - b.rect.left : b.cx;
  if (m & RBBIM_BACKGROUND) out->hbmBack = b.bmp;
  if (m & RBBIM_ID) out->wID = b.id;
  if ((m & RBBIM_IDEALSIZE) && has(offsetof(INFO, cxIdeal) + sizeof(UINT))) out->cxIdeal = b.cxIdeal;
  if ((m & RBBIM_LPARAM) && has(offsetof(INFO, lParam) + sizeof(LPARAM))) out->lParam = b.lParam;
  if ((m & RBBIM_HEADERSIZE) && has(offsetof(INFO, cxHeader) + sizeof(UINT))) out->cxHeader = HeaderWidth(r, b);
}

template <class INFO>
LRESULT InsertBand(Rebar* r, int at, const INFO* in, bool unicode) {
  if (!in || in->cbSize < REBARBANDINFOW_V3_SIZE - (unicode ? 0 : 0)) return FALSE;
  Band b;
  ApplyBand(r, b, in, unicode);
  if (at < 0 || at > (int)r->bands.size()) at = (int)r->bands.size();
  r->bands.insert(r->bands.begin() + at, b);
  if (b.child && Visible(b)) ShowWindow(b.child, SW_SHOWNA);
  Layout(r);
  return TRUE;
}

int BandAt(Rebar* r, POINT p, UINT* flags) {
  if (Vertical(r)) std::swap(p.x, p.y);
  for (int i = 0; i < (int)r->bands.size(); i++) {
    Band& b = r->bands[i];
    if (!Visible(b) || !PtInRect(&b.rect, p)) continue;
    if (flags) {
      int hdr = HeaderWidth(r, b);
      if (HasGripper(r, b) && p.x < b.rect.left + kGripper)
        *flags = RBHT_GRABBER;
      else if (p.x < b.rect.left + hdr)
        *flags = RBHT_CAPTION;
      else
        *flags = RBHT_CLIENT;
    }
    return i;
  }
  if (flags) *flags = RBHT_NOWHERE;
  return -1;
}

LRESULT CALLBACK RebarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Rebar* r = RebarOf(h);
  if (msg == WM_NCCREATE) {
    r = new Rebar;
    r->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)r);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!r) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      delete r;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE:
      Layout(r, false);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintRebar(r, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_SIZE:
      Layout(r);
      return 0;
    case WM_SETFONT:
      r->font = (HFONT)wp;
      if (LOWORD(lp)) Layout(r);
      return 0;
    case WM_GETFONT:
      return (LRESULT)r->font;
    // The bands' windows talk to the rebar's parent.
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
      return SendMessageW(Target(r), msg, wp, lp);
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      UINT flags;
      int i = BandAt(r, p, &flags);
      if (i > 0 && flags == RBHT_GRABBER && r->bands[i].rect.left > 0) {
        // The gripper resizes the band before it on the row.
        int prev = -1;
        for (int k = i - 1; k >= 0; k--)
          if (Visible(r->bands[k]) && r->bands[k].row == r->bands[i].row) {
            prev = k;
            break;
          }
        if (prev >= 0 && !(StyleOf(h) & RBS_FIXEDORDER)) {
          r->drag = prev;
          r->dragX = Vertical(r) ? p.y : p.x;
          r->dragCx = r->bands[prev].rect.right - r->bands[prev].rect.left;
          SetCapture(h);
          NMREBAR nm = {};
          nm.dwMask = RBNM_ID | RBNM_STYLE | RBNM_LPARAM;
          nm.uBand = i;
          nm.wID = r->bands[i].id;
          nm.fStyle = r->bands[i].style;
          nm.lParam = r->bands[i].lParam;
          SendNotify(h, Target(r), RBN_BEGINDRAG, &nm.hdr);
        }
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (r->drag >= 0 && GetCapture() == h) {
        int at = Vertical(r) ? (short)HIWORD(lp) : (short)LOWORD(lp);
        Band& b = r->bands[r->drag];
        int lo = HeaderWidth(r, b) + (int)b.cxMinChild + kBandGap;
        b.cx = (UINT)std::max(lo, r->dragCx + at - r->dragX);
        Layout(r);
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (r->drag >= 0) {
        r->drag = -1;
        if (msg == WM_LBUTTONUP) ReleaseCapture();
        SendNotify(h, Target(r), RBN_ENDDRAG);
        SendNotify(h, Target(r), RBN_LAYOUTCHANGED);
      }
      return 0;
    case WM_SETCURSOR:
      if ((HWND)wp == h && LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        UINT flags;
        if (BandAt(r, p, &flags) > 0 && flags == RBHT_GRABBER) {
          SetCursor(LoadCursorW(NULL, (LPCWSTR)(Vertical(r) ? IDC_SIZENS : IDC_SIZEWE)));
          return TRUE;
        }
      }
      return DefWindowProcW(h, msg, wp, lp);
    case RB_INSERTBANDA:
      return InsertBand(r, (int)wp, (const REBARBANDINFOA*)lp, false);
    case RB_INSERTBANDW:
      return InsertBand(r, (int)wp, (const REBARBANDINFOW*)lp, true);
    case RB_DELETEBAND: {
      if (wp >= r->bands.size()) return FALSE;
      NMREBAR nm = {};
      nm.dwMask = RBNM_ID | RBNM_STYLE | RBNM_LPARAM;
      nm.uBand = (UINT)wp;
      nm.wID = r->bands[wp].id;
      nm.fStyle = r->bands[wp].style;
      nm.lParam = r->bands[wp].lParam;
      SendNotify(h, Target(r), RBN_DELETINGBAND, &nm.hdr);
      HWND child = r->bands[wp].child;
      r->bands.erase(r->bands.begin() + wp);
      if (child && IsWindow(child)) ShowWindow(child, SW_HIDE);
      SendNotify(h, Target(r), RBN_DELETEDBAND, &nm.hdr);
      Layout(r);
      return TRUE;
    }
    case RB_SETBANDINFOA:
    case RB_SETBANDINFOW:
      if (wp >= r->bands.size() || !lp) return FALSE;
      if (msg == RB_SETBANDINFOW)
        ApplyBand(r, r->bands[wp], (const REBARBANDINFOW*)lp, true);
      else
        ApplyBand(r, r->bands[wp], (const REBARBANDINFOA*)lp, false);
      Layout(r);
      return TRUE;
    case RB_GETBANDINFOA:
    case RB_GETBANDINFOW:
      if (wp >= r->bands.size() || !lp) return FALSE;
      if (msg == RB_GETBANDINFOW)
        ReadBand(r, r->bands[wp], (REBARBANDINFOW*)lp, true);
      else
        ReadBand(r, r->bands[wp], (REBARBANDINFOA*)lp, false);
      return TRUE;
    case RB_GETBARINFO: {
      REBARINFO* ri = (REBARINFO*)lp;
      if (!ri) return FALSE;
      if (ri->fMask & RBIM_IMAGELIST) ri->himl = r->himl;
      return TRUE;
    }
    case RB_SETBARINFO: {
      const REBARINFO* ri = (const REBARINFO*)lp;
      if (!ri) return FALSE;
      if (ri->fMask & RBIM_IMAGELIST) {
        r->himl = ri->himl;
        Layout(r);
      }
      return TRUE;
    }
    case RB_SETPARENT: {
      HWND old = Target(r);
      r->notify = (HWND)wp;
      return (LRESULT)old;
    }
    case RB_GETBANDCOUNT:
      return (LRESULT)r->bands.size();
    case RB_GETROWCOUNT:
      return (LRESULT)r->rowHeights.size();
    case RB_GETROWHEIGHT:
      if (wp < r->bands.size() && Visible(r->bands[wp])) return r->bands[wp].rect.bottom - r->bands[wp].rect.top;
      return 0;
    case RB_GETBARHEIGHT:
      return r->height;
    case RB_IDTOINDEX:
      for (size_t i = 0; i < r->bands.size(); i++)
        if (r->bands[i].id == (UINT)wp) return (LRESULT)i;
      return -1;
    case RB_GETRECT:
      if (wp >= r->bands.size() || !lp) return FALSE;
      *(RECT*)lp = Oriented(r, r->bands[wp].rect);
      return TRUE;
    case RB_GETBANDBORDERS: {
      if (wp >= r->bands.size() || !lp) return 0;
      RECT* rc = (RECT*)lp;
      rc->left = HeaderWidth(r, r->bands[wp]);
      rc->top = kBandGap;
      rc->right = kBandGap;
      rc->bottom = kBandGap;
      return 0;
    }
    case RB_HITTEST: {
      RBHITTESTINFO* hi = (RBHITTESTINFO*)lp;
      if (!hi) return -1;
      hi->iBand = BandAt(r, hi->pt, &hi->flags);
      return hi->iBand;
    }
    case RB_SETBKCOLOR: {
      COLORREF old = r->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : r->back;
      r->back = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case RB_GETBKCOLOR:
      return r->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : r->back;
    case RB_SETTEXTCOLOR: {
      COLORREF old = r->fore == CLR_DEFAULT ? GetSysColor(COLOR_BTNTEXT) : r->fore;
      r->fore = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case RB_GETTEXTCOLOR:
      return r->fore == CLR_DEFAULT ? GetSysColor(COLOR_BTNTEXT) : r->fore;
    case RB_GETTOOLTIPS:
      return (LRESULT)r->tips;
    case RB_SETTOOLTIPS:
      r->tips = (HWND)wp;
      return 0;
    case RB_SHOWBAND:
      if (wp >= r->bands.size()) return FALSE;
      if (lp)
        r->bands[wp].style &= ~RBBS_HIDDEN;
      else
        r->bands[wp].style |= RBBS_HIDDEN;
      if (HWND c = r->bands[wp].child) ShowWindow(c, lp ? SW_SHOWNA : SW_HIDE);
      Layout(r);
      return TRUE;
    case RB_MOVEBAND: {
      size_t from = wp, to = (size_t)lp;
      if (from >= r->bands.size() || to >= r->bands.size()) return FALSE;
      Band b = r->bands[from];
      r->bands.erase(r->bands.begin() + from);
      r->bands.insert(r->bands.begin() + to, b);
      Layout(r);
      return TRUE;
    }
    case RB_MAXIMIZEBAND:
    case RB_MINIMIZEBAND:
      if (wp >= r->bands.size()) return 0;
      if (msg == RB_MINIMIZEBAND)
        r->bands[wp].cx = 0;
      else
        r->bands[wp].cx = lp ? r->bands[wp].cxIdeal + HeaderWidth(r, r->bands[wp]) : 0x7FFF;
      Layout(r);
      return 0;
    case RB_SIZETORECT: {
      const RECT* rc = (const RECT*)lp;
      if (rc) SetWindowPos(h, NULL, 0, 0, rc->right - rc->left, rc->bottom - rc->top, SWP_NOMOVE | SWP_NOZORDER);
      return TRUE;
    }
    case RB_BEGINDRAG:
    case RB_ENDDRAG:
    case RB_DRAGMOVE:
    case RB_PUSHCHEVRON:
      return 0;
    case RB_SETPALETTE:
    case RB_GETPALETTE:
      return 0;
    case CCM_SETCOLORSCHEME:
      return 0;
    case CCM_GETCOLORSCHEME: {
      COLORSCHEME* cs = (COLORSCHEME*)lp;
      if (cs) {
        cs->clrBtnHighlight = GetSysColor(COLOR_BTNHIGHLIGHT);
        cs->clrBtnShadow = GetSysColor(COLOR_BTNSHADOW);
      }
      return cs != NULL;
    }
    case CCM_GETDROPTARGET:
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Pager

struct Pager {
  HWND hwnd = NULL;
  HWND child = NULL;
  int pos = 0;
  int border = 0;
  int button = 12;
  COLORREF back = CLR_DEFAULT;
  int extent = 0;   // the child's length along the scroll
  bool forward = false;
  int pressed = -1;  // the button held down
  bool sizing = false;
};

Pager* PagerOf(HWND h) { return (Pager*)GetWindowLongPtrW(h, 0); }
bool Horz(Pager* p) { return (StyleOf(p->hwnd) & PGS_HORZ) != 0; }

// The length between the borders, with no buttons.
int Inner(Pager* p) {
  RECT w;
  GetWindowRect(p->hwnd, &w);
  return (Horz(p) ? w.right - w.left : w.bottom - w.top) - 2 * p->border;
}

bool ShowsButton(Pager* p, int which, int pos) {
  int room = Inner(p);
  if (p->extent <= room) return false;
  if (which == PGB_TOPORLEFT) return pos > 0;
  // The far button shows while the child goes on past the visible part.
  int visible = room - (pos > 0 ? p->button : 0) - p->button;
  return pos + visible < p->extent;
}

int MaxPos(Pager* p) {
  int room = Inner(p);
  if (p->extent <= room) return 0;
  return std::max(0, p->extent - (room - p->button));
}

void PlaceChild(Pager* p) {
  if (!p->child || !IsWindow(p->child)) return;
  RECT c;
  GetClientRect(p->hwnd, &c);
  if (Horz(p))
    SetWindowPos(p->child, NULL, -p->pos, 0, std::max(p->extent, (int)c.right), c.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  else
    SetWindowPos(p->child, NULL, 0, -p->pos, c.right, std::max(p->extent, (int)c.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetPos(Pager* p, int pos) {
  pos = std::max(0, std::min(pos, MaxPos(p)));
  p->pos = pos;
  // The buttons change the client area.
  if (!p->sizing) {
    p->sizing = true;
    SetWindowPos(p->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    p->sizing = false;
  }
  PlaceChild(p);
}

void Recalc(Pager* p) {
  RECT w;
  GetWindowRect(p->hwnd, &w);
  NMPGCALCSIZE nm = {};
  bool horz = Horz(p);
  nm.dwFlag = horz ? PGF_CALCWIDTH : PGF_CALCHEIGHT;
  RECT cr = {0, 0, 0, 0};
  if (p->child && IsWindow(p->child)) GetWindowRect(p->child, &cr);
  nm.iWidth = cr.right - cr.left;
  nm.iHeight = cr.bottom - cr.top;
  SendNotify(p->hwnd, NULL, PGN_CALCSIZE, &nm.hdr);
  p->extent = horz ? nm.iWidth : nm.iHeight;
  SetPos(p, p->pos);
}

RECT ButtonRect(Pager* p, int which) {
  RECT w;
  GetWindowRect(p->hwnd, &w);
  int cx = w.right - w.left, cy = w.bottom - w.top, b = p->border;
  if (Horz(p))
    return which == PGB_TOPORLEFT ? RECT{b, b, b + p->button, cy - b} : RECT{cx - b - p->button, b, cx - b, cy - b};
  return which == PGB_TOPORLEFT ? RECT{b, b, cx - b, b + p->button} : RECT{b, cy - b - p->button, cx - b, cy - b};
}

void Scroll(Pager* p, int which) {
  NMPGSCROLL nm = {};
  bool horz = Horz(p);
  nm.iDir = which == PGB_TOPORLEFT ? (horz ? PGF_SCROLLLEFT : PGF_SCROLLUP) : (horz ? PGF_SCROLLRIGHT : PGF_SCROLLDOWN);
  GetClientRect(p->hwnd, &nm.rcParent);
  nm.iXpos = horz ? p->pos : 0;
  nm.iYpos = horz ? 0 : p->pos;
  nm.iScroll = std::max(1, (int)(horz ? nm.rcParent.right : nm.rcParent.bottom) / 4);
  if (GetKeyState(VK_SHIFT) < 0) nm.fwKeys |= PGK_SHIFT;
  if (GetKeyState(VK_CONTROL) < 0) nm.fwKeys |= PGK_CONTROL;
  SendNotify(p->hwnd, NULL, PGN_SCROLL, &nm.hdr);
  SetPos(p, p->pos + (which == PGB_TOPORLEFT ? -nm.iScroll : nm.iScroll));
  RedrawWindow(p->hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
}

LRESULT CALLBACK PagerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Pager* p = PagerOf(h);
  if (msg == WM_NCCREATE) {
    p = new Pager;
    p->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)p);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!p) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      delete p;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_NCCALCSIZE: {
      LRESULT r = DefWindowProcW(h, msg, wp, lp);
      RECT* rc = (RECT*)lp;
      if (!rc) return r;
      InflateRect(rc, -p->border, -p->border);
      bool first = ShowsButton(p, PGB_TOPORLEFT, p->pos), last = ShowsButton(p, PGB_BOTTOMORRIGHT, p->pos);
      if (Horz(p)) {
        if (first) rc->left += p->button;
        if (last) rc->right -= p->button;
      } else {
        if (first) rc->top += p->button;
        if (last) rc->bottom -= p->button;
      }
      if (rc->right < rc->left) rc->right = rc->left;
      if (rc->bottom < rc->top) rc->bottom = rc->top;
      return r;
    }
    case WM_NCPAINT: {
      DefWindowProcW(h, msg, wp, lp);
      HDC dc = GetWindowDC(h);
      RECT w;
      GetWindowRect(h, &w);
      RECT all = {0, 0, w.right - w.left, w.bottom - w.top};
      COLORREF bk = p->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->back;
      if (p->border) {
        RECT in = all;
        InflateRect(&in, -p->border, -p->border);
        HRGN out = CreateRectRgnIndirect(&all), hole = CreateRectRgnIndirect(&in);
        CombineRgn(out, out, hole, RGN_DIFF);
        HBRUSH br = CreateSolidBrush(bk);
        FillRgn(dc, out, br);
        DeleteObject(br);
        DeleteObject(out);
        DeleteObject(hole);
      }
      bool horz = Horz(p);
      for (int which = PGB_TOPORLEFT; which <= PGB_BOTTOMORRIGHT; which++) {
        if (!ShowsButton(p, which, p->pos)) continue;
        RECT br = ButtonRect(p, which);
        UINT kind = which == PGB_TOPORLEFT ? (horz ? DFCS_SCROLLLEFT : DFCS_SCROLLUP)
                                           : (horz ? DFCS_SCROLLRIGHT : DFCS_SCROLLDOWN);
        if (p->pressed == which) kind |= DFCS_PUSHED;
        DrawFrameControl(dc, &br, DFC_SCROLL, kind | DFCS_FLAT);
      }
      ReleaseDC(h, dc);
      return 0;
    }
    case WM_NCHITTEST: {
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      RECT w;
      GetWindowRect(h, &w);
      pt.x -= w.left;
      pt.y -= w.top;
      for (int which = PGB_TOPORLEFT; which <= PGB_BOTTOMORRIGHT; which++) {
        RECT br = ButtonRect(p, which);
        if (ShowsButton(p, which, p->pos) && PtInRect(&br, pt)) return HTOBJECT;
      }
      return DefWindowProcW(h, msg, wp, lp);
    }
    case WM_NCLBUTTONDOWN:
      if (wp == HTOBJECT) {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        RECT w;
        GetWindowRect(h, &w);
        pt.x -= w.left;
        pt.y -= w.top;
        for (int which = PGB_TOPORLEFT; which <= PGB_BOTTOMORRIGHT; which++) {
          RECT br = ButtonRect(p, which);
          if (PtInRect(&br, pt)) {
            p->pressed = which;
            Scroll(p, which);
            SetTimer(h, 1, 100, NULL);
            SetCapture(h);
            break;
          }
        }
        return 0;
      }
      return DefWindowProcW(h, msg, wp, lp);
    case WM_TIMER:
      if (wp == 1 && p->pressed >= 0) {
        if (ShowsButton(p, p->pressed, p->pos))
          Scroll(p, p->pressed);
        else
          KillTimer(h, 1);
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (p->pressed >= 0) {
        p->pressed = -1;
        KillTimer(h, 1);
        if (msg == WM_LBUTTONUP) ReleaseCapture();
        RedrawWindow(h, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
      }
      return 0;
    case WM_ERASEBKGND: {
      RECT c;
      GetClientRect(h, &c);
      FillSolid((HDC)wp, c, p->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->back);
      return 1;
    }
    case WM_SIZE:
      if (!p->sizing) Recalc(p);
      return 0;
    case WM_COMMAND:
    case WM_NOTIFY:
      return SendMessageW(GetParent(h), msg, wp, lp);
    case PGM_SETCHILD:
      p->child = (HWND)lp;
      if (p->child && GetParent(p->child) != h) SetParent(p->child, h);
      p->pos = 0;
      Recalc(p);
      return 0;
    case PGM_RECALCSIZE:
      Recalc(p);
      return 0;
    case PGM_FORWARDMOUSE:
      p->forward = wp != 0;
      return 0;
    case PGM_SETBKCOLOR: {
      COLORREF old = p->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->back;
      p->back = (COLORREF)lp;
      RedrawWindow(h, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE);
      return old;
    }
    case PGM_GETBKCOLOR:
      return p->back == CLR_DEFAULT ? GetSysColor(COLOR_BTNFACE) : p->back;
    case PGM_SETBORDER: {
      int old = p->border;
      p->border = std::max(0, (int)lp);
      SetPos(p, p->pos);
      return old;
    }
    case PGM_GETBORDER:
      return p->border;
    case PGM_SETPOS:
      SetPos(p, (int)lp);
      return 0;
    case PGM_GETPOS:
      return p->pos;
    case PGM_SETBUTTONSIZE: {
      int old = p->button;
      p->button = std::max(1, (int)lp);
      SetPos(p, p->pos);
      return old;
    }
    case PGM_GETBUTTONSIZE:
      return p->button;
    case PGM_GETBUTTONSTATE: {
      int which = (int)lp;
      if (!ShowsButton(p, which, p->pos)) return PGF_INVISIBLE;
      UINT s = PGF_NORMAL;
      if (p->pressed == which) s |= PGF_DEPRESSED;
      return s;
    }
    case CCM_GETDROPTARGET:
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// SysLink

struct LinkItem {
  std::wstring id, url, text;
  UINT state = LIS_ENABLED;
};

// A piece of laid-out text: a word or a space, in a link or not.
struct Run {
  std::wstring text;
  int link = -1;
  RECT rect = {0, 0, 0, 0};
};

struct Link {
  HWND hwnd = NULL;
  HFONT font = NULL, underline = NULL;
  std::wstring markup;
  std::vector<LinkItem> items;
  std::vector<Run> runs;
  int focus = -1;
  int pressed = -1;
  int lineHeight = 0;
  int height = 0;
};

Link* LinkOf(HWND h) { return (Link*)GetWindowLongPtrW(h, 0); }

// The value of attr="..." (or '...', or bare) in an opening tag.
bool Attribute(const std::wstring& tag, const wchar_t* name, std::wstring* value) {
  size_t n = wcslen(name);
  for (size_t i = 0; i + n <= tag.size(); i++) {
    if (_wcsnicmp(tag.c_str() + i, name, n) != 0) continue;
    if (i > 0 && !iswspace(tag[i - 1])) continue;
    size_t j = i + n;
    while (j < tag.size() && iswspace(tag[j])) j++;
    if (j >= tag.size() || tag[j] != L'=') continue;
    j++;
    while (j < tag.size() && iswspace(tag[j])) j++;
    if (j < tag.size() && (tag[j] == L'"' || tag[j] == L'\'')) {
      wchar_t q = tag[j++];
      size_t e = tag.find(q, j);
      if (e == std::wstring::npos) e = tag.size();
      *value = tag.substr(j, e - j);
    } else {
      size_t e = j;
      while (e < tag.size() && !iswspace(tag[e])) e++;
      *value = tag.substr(j, e - j);
    }
    return true;
  }
  return false;
}

void Parse(Link* l) {
  // Keep the states of links that stay.
  std::vector<LinkItem> old = l->items;
  l->items.clear();
  l->runs.clear();
  const std::wstring& m = l->markup;
  std::wstring plain;
  int current = -1;
  auto flush = [&]() {
    // Words and spaces become runs of their own.
    size_t i = 0;
    while (i < plain.size()) {
      size_t j = i;
      if (plain[i] == L'\n') {
        j = i + 1;
      } else if (iswspace(plain[i])) {
        while (j < plain.size() && iswspace(plain[j]) && plain[j] != L'\n') j++;
      } else {
        while (j < plain.size() && !iswspace(plain[j])) j++;
      }
      Run r;
      r.text = plain.substr(i, j - i);
      r.link = current;
      l->runs.push_back(r);
      i = j;
    }
    plain.clear();
  };
  size_t i = 0;
  while (i < m.size()) {
    if (m[i] == L'<') {
      size_t e = m.find(L'>', i);
      if (e != std::wstring::npos) {
        std::wstring tag = m.substr(i + 1, e - i - 1);
        if (current < 0 && (tag == L"a" || tag == L"A" ||
                            ((tag[0] == L'a' || tag[0] == L'A') && tag.size() > 1 && iswspace(tag[1])))) {
          flush();
          LinkItem it;
          Attribute(tag, L"href", &it.url);
          Attribute(tag, L"id", &it.id);
          size_t k = l->items.size();
          if (k < old.size()) it.state = old[k].state;
          l->items.push_back(it);
          current = (int)k;
          i = e + 1;
          continue;
        }
        if (current >= 0 && (_wcsicmp(tag.c_str(), L"/a") == 0)) {
          flush();
          current = -1;
          i = e + 1;
          continue;
        }
      }
    }
    plain += m[i];
    if (current >= 0) l->items[current].text += m[i];
    i++;
  }
  flush();
  if (l->focus >= (int)l->items.size()) l->focus = -1;
}

void LayoutLink(Link* l, int width, int* height) {
  HDC dc = GetDC(l->hwnd);
  HGDIOBJ old = SelectObject(dc, l->font ? l->font : ControlFont());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  int lh = tm.tmHeight;
  int x = 0, y = 0;
  for (Run& r : l->runs) {
    if (r.text == L"\n") {
      r.rect = RECT{x, y, x, y + lh};
      x = 0;
      y += lh;
      continue;
    }
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, r.text.c_str(), (int)r.text.size(), &sz);
    bool space = iswspace(r.text[0]) != 0;
    if (!space && x > 0 && width > 0 && x + sz.cx > width) {
      x = 0;
      y += lh;
    }
    if (space && x == 0 && y > 0) sz.cx = 0;  // no space at the start of a line
    r.rect = RECT{x, y, x + sz.cx, y + lh};
    x += sz.cx;
  }
  SelectObject(dc, old);
  ReleaseDC(l->hwnd, dc);
  l->lineHeight = lh;
  *height = l->runs.empty() ? lh : y + lh;
}

void Relayout(Link* l) {
  RECT c;
  GetClientRect(l->hwnd, &c);
  LayoutLink(l, c.right, &l->height);
  InvalidateRect(l->hwnd, NULL, TRUE);
}

HFONT UnderlineFont(Link* l) {
  if (!l->underline) {
    LOGFONTW lf;
    GetObjectW(l->font ? l->font : ControlFont(), sizeof lf, &lf);
    lf.lfUnderline = TRUE;
    l->underline = CreateFontIndirectW(&lf);
  }
  return l->underline;
}

void PaintLink(Link* l, HDC dc) {
  RECT c;
  GetClientRect(l->hwnd, &c);
  if (!(StyleOf(l->hwnd) & LWS_TRANSPARENT)) {
    HBRUSH b = (HBRUSH)SendMessageW(GetParent(l->hwnd), WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)l->hwnd);
    FillRect(dc, &c, b ? b : GetSysColorBrush(COLOR_BTNFACE));
  }
  SetBkMode(dc, TRANSPARENT);
  bool enabled = IsWindowEnabled(l->hwnd) != 0;
  HFONT normal = l->font ? l->font : ControlFont();
  for (const Run& r : l->runs) {
    if (r.text == L"\n") continue;
    COLORREF color = GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
    HFONT f = normal;
    if (r.link >= 0) {
      const LinkItem& it = l->items[r.link];
      if (enabled && (it.state & LIS_ENABLED))
        color = (it.state & LIS_VISITED) ? RGB(128, 0, 128) : GetSysColor(COLOR_HOTLIGHT);
      else
        color = GetSysColor(COLOR_GRAYTEXT);
      f = UnderlineFont(l);
    }
    SetTextColor(dc, color);
    HGDIOBJ old = SelectObject(dc, f);
    TextOutW(dc, r.rect.left, r.rect.top, r.text.c_str(), (int)r.text.size());
    SelectObject(dc, old);
  }
  // The focused link's focus rectangle, around its runs.
  if (GetFocus() == l->hwnd && l->focus >= 0) {
    RECT f = {0, 0, 0, 0};
    for (const Run& r : l->runs)
      if (r.link == l->focus) UnionRect(&f, &f, &r.rect);
    if (!IsRectEmpty(&f)) DrawFocusRect(dc, &f);
  }
}

int LinkAt(Link* l, POINT p) {
  for (const Run& r : l->runs)
    if (r.link >= 0 && PtInRect(&r.rect, p)) return r.link;
  return -1;
}

void FillItem(Link* l, int i, LITEM* it) {
  const LinkItem& li = l->items[i];
  it->iLink = i;
  if (it->mask & LIF_STATE) {
    UINT s = li.state;
    if (l->focus == i) s |= LIS_FOCUSED;
    it->state = s & it->stateMask;
  }
  if (it->mask & LIF_ITEMID) {
    size_t n = std::min(li.id.size(), (size_t)MAX_LINKID_TEXT - 1);
    memcpy(it->szID, li.id.data(), n * sizeof(WCHAR));
    it->szID[n] = 0;
  }
  if (it->mask & LIF_URL) {
    size_t n = std::min(li.url.size(), (size_t)L_MAX_URL_LENGTH - 1);
    memcpy(it->szUrl, li.url.data(), n * sizeof(WCHAR));
    it->szUrl[n] = 0;
  }
}

void Click(Link* l, int i) {
  if (i < 0 || i >= (int)l->items.size() || !(l->items[i].state & LIS_ENABLED)) return;
  NMLINK nm = {};
  nm.item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
  nm.item.stateMask = 0xFFFFFFFF;
  FillItem(l, i, &nm.item);
  SendNotify(l->hwnd, NULL, NM_CLICK, &nm.hdr);
}

LRESULT CALLBACK LinkProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Link* l = LinkOf(h);
  if (msg == WM_NCCREATE) {
    l = new Link;
    l->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)l);
    CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
    if (cs && cs->lpszName) {
      l->markup = cs->lpszName;
      Parse(l);
    }
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!l) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      if (l->underline) DeleteObject(l->underline);
      delete l;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE:
      Relayout(l);
      return 0;
    case WM_SETTEXT:
      DefWindowProcW(h, msg, wp, lp);
      l->markup = lp ? (const wchar_t*)lp : L"";
      Parse(l);
      Relayout(l);
      return TRUE;
    case WM_SIZE:
      Relayout(l);
      return 0;
    case WM_SETFONT:
      l->font = (HFONT)wp;
      if (l->underline) DeleteObject(l->underline);
      l->underline = NULL;
      Relayout(l);
      if (LOWORD(lp)) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)l->font;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintLink(l, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_ENABLE:
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_SETCURSOR:
      if ((HWND)wp == h && LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        int i = LinkAt(l, p);
        if (i >= 0 && (l->items[i].state & LIS_ENABLED)) {
          SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
          return TRUE;
        }
      }
      return DefWindowProcW(h, msg, wp, lp);
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      int i = LinkAt(l, p);
      l->pressed = i;
      if (i >= 0) {
        l->focus = i;
        SetFocus(h);
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      int i = LinkAt(l, p);
      if (i >= 0 && i == l->pressed) Click(l, i);
      l->pressed = -1;
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      if (msg == WM_SETFOCUS && l->focus < 0) {
        for (int i = 0; i < (int)l->items.size(); i++)
          if (l->items[i].state & LIS_ENABLED) {
            l->focus = (GetKeyState(VK_SHIFT) < 0) ? -1 : i;
            if (l->focus >= 0) break;
          }
        if (l->focus < 0)
          for (int i = (int)l->items.size(); i-- > 0;)
            if (l->items[i].state & LIS_ENABLED) {
              l->focus = i;
              break;
            }
      }
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE: {
      LRESULT code = DLGC_WANTCHARS;
      MSG* m = (MSG*)lp;
      if (m && m->message == WM_KEYDOWN) {
        if (m->wParam == VK_RETURN && !(StyleOf(h) & LWS_IGNORERETURN)) code |= DLGC_WANTALLKEYS;
        if (m->wParam == VK_TAB) {
          // Tab moves between the links before leaving the control.
          bool back = GetKeyState(VK_SHIFT) < 0;
          int next = l->focus + (back ? -1 : 1);
          while (next >= 0 && next < (int)l->items.size() && !(l->items[next].state & LIS_ENABLED))
            next += back ? -1 : 1;
          if (next >= 0 && next < (int)l->items.size()) code |= DLGC_WANTTAB;
        }
      }
      return code;
    }
    case WM_KEYDOWN:
      if (wp == VK_TAB) {
        bool back = GetKeyState(VK_SHIFT) < 0;
        int next = l->focus + (back ? -1 : 1);
        while (next >= 0 && next < (int)l->items.size() && !(l->items[next].state & LIS_ENABLED))
          next += back ? -1 : 1;
        if (next >= 0 && next < (int)l->items.size()) {
          l->focus = next;
          InvalidateRect(h, NULL, TRUE);
        }
        return 0;
      }
      if ((wp == VK_RETURN && !(StyleOf(h) & LWS_IGNORERETURN)) || wp == VK_SPACE) {
        if (wp == VK_RETURN) {
          NMLINK nm = {};
          nm.item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
          nm.item.stateMask = 0xFFFFFFFF;
          if (l->focus >= 0) FillItem(l, l->focus, &nm.item);
          if (SendNotify(h, NULL, NM_RETURN, &nm.hdr)) return 0;
        }
        Click(l, l->focus);
        return 0;
      }
      return DefWindowProcW(h, msg, wp, lp);
    case LM_HITTEST: {
      LHITTESTINFO* hi = (LHITTESTINFO*)lp;
      if (!hi) return FALSE;
      int i = LinkAt(l, hi->pt);
      if (i < 0) return FALSE;
      hi->item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
      hi->item.stateMask = 0xFFFFFFFF;
      FillItem(l, i, &hi->item);
      return TRUE;
    }
    case LM_GETIDEALHEIGHT: {
      RECT c;
      GetClientRect(h, &c);
      int width = wp ? (int)wp : c.right, height = 0;
      LayoutLink(l, width, &height);
      if (width != c.right) LayoutLink(l, c.right, &l->height);
      return height;
    }
    case LM_SETITEM: {
      LITEM* it = (LITEM*)lp;
      if (!it || !(it->mask & LIF_ITEMINDEX) || it->iLink < 0 || it->iLink >= (int)l->items.size()) return FALSE;
      LinkItem& li = l->items[it->iLink];
      if (it->mask & LIF_STATE) {
        UINT mask = it->stateMask & ~LIS_FOCUSED;
        li.state = (li.state & ~mask) | (it->state & mask);
        if (it->stateMask & LIS_FOCUSED) {
          if (it->state & LIS_FOCUSED)
            l->focus = it->iLink;
          else if (l->focus == it->iLink)
            l->focus = -1;
        }
      }
      if (it->mask & LIF_ITEMID) li.id = std::wstring(it->szID, wcsnlen(it->szID, MAX_LINKID_TEXT));
      if (it->mask & LIF_URL) li.url = std::wstring(it->szUrl, wcsnlen(it->szUrl, L_MAX_URL_LENGTH));
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case LM_GETITEM: {
      LITEM* it = (LITEM*)lp;
      if (!it || !(it->mask & LIF_ITEMINDEX) || it->iLink < 0 || it->iLink >= (int)l->items.size()) return FALSE;
      FillItem(l, it->iLink, it);
      return TRUE;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterRebarClasses() {
  LPCWSTR arrow = (LPCWSTR)IDC_ARROW;
  RegisterSystemClass(REBARCLASSNAMEW, RebarProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(WC_PAGESCROLLERW, PagerProc, CS_GLOBALCLASS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(WC_LINK, LinkProc, CS_GLOBALCLASS, sizeof(void*), NULL, arrow);
}

}  // namespace winport
