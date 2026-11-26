// win16ctl.cpp - the progress bar, trackbar, up-down and tab control
// windows for 16-bit Windows. cmnctls.cpp has the members of the MFC
// classes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// 16-bit Windows has no comctl32.dll. This file registers four window
// classes and does the messages of the common controls of the same
// kind, in ANSI and with the 16-bit packing of WM_HSCROLL and
// WM_VSCROLL. The windows draw in the 3D style of the system colors. The
// classes have names of their own (afxwin16u.h), and the file also
// registers them with the names of comctl32.dll, for the dialog
// templates that use those names.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxcmn.h>
#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

namespace afx16 {

/// Fills a rectangle with a color.
void FillColor(HDC hdc, const RECT& rc, COLORREF clr) {
  HBRUSH hbr = ::CreateSolidBrush(clr);
  ::FillRect(hdc, &rc, hbr);
  ::DeleteObject(hbr);
}

/// Draws a line from (x1, y1) to (x2, y2) in a color. The last point is
/// not drawn.
void DrawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF clr) {
  HPEN hPen = ::CreatePen(PS_SOLID, 1, clr);
  HGDIOBJ hOld = ::SelectObject(hdc, hPen);
  ::MoveTo(hdc, x1, y1);
  ::LineTo(hdc, x2, y2);
  ::SelectObject(hdc, hOld);
  ::DeleteObject(hPen);
}

/// Fills a polygon with a color, without an outline.
void FillPolygon(HDC hdc, const POINT* pts, int n, COLORREF clr) {
  HBRUSH hbr = ::CreateSolidBrush(clr);
  HGDIOBJ hOldBrush = ::SelectObject(hdc, hbr);
  HGDIOBJ hOldPen = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
  ::Polygon(hdc, pts, n);
  ::SelectObject(hdc, hOldPen);
  ::SelectObject(hdc, hOldBrush);
  ::DeleteObject(hbr);
}

/// Sends WM_NOTIFY with a notification header to the parent of a
/// control. Gives the result of the parent.
LRESULT SendNotify(HWND hWnd, UINT nCode, NMHDR* pHdr) {
  pHdr->hwndFrom = hWnd;
  pHdr->idFrom = (UINT)::GetDlgCtrlID(hWnd);
  pHdr->code = nCode;
  return ::SendMessage(::GetParent(hWnd), WM_NOTIFY, (WPARAM)pHdr->idFrom, (LPARAM)pHdr);
}

/// Sends WM_HSCROLL or WM_VSCROLL to the parent of a control, packed as
/// 16-bit Windows packs it: the code in wParam, the position in the low
/// word and the control in the high word of lParam.
void SendScroll(HWND hWnd, BOOL bVertical, UINT nCode, int nPos) {
  ::SendMessage(::GetParent(hWnd), bVertical ? WM_VSCROLL : WM_HSCROLL, nCode,
                MAKELPARAM((WORD)nPos, (WORD)(UINT)hWnd));
}

/// Gives the brush of the background of a control: the brush that the
/// parent gives for WM_CTLCOLOR, or the face color.
HBRUSH ParentBrush(HWND hWnd, HDC hdc) {
  HBRUSH hbr = (HBRUSH)::SendMessage(::GetParent(hWnd), WM_CTLCOLOR, (WPARAM)hdc,
                                     MAKELPARAM((WORD)(UINT)hWnd, CTLCOLOR_DLG));
  return hbr != NULL ? hbr : ::GetSysColorBrush(COLOR_BTNFACE);
}

/// Draws a raised button face in a rectangle: the face color, a
/// highlight edge on the top and left, and a shadow edge on the bottom
/// and right. A pushed face has the sunken edge.
void DrawButtonFace(HDC hdc, RECT rc, BOOL bPushed) {
  FillColor(hdc, rc, ::GetSysColor(COLOR_BTNFACE));
  ::DrawEdge(hdc, &rc, bPushed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
}

/// Draws a small triangle that points in a direction (0 up, 1 down,
/// 2 left, 3 right) in the middle of a rectangle. A disabled triangle is
/// embossed.
void DrawArrow(HDC hdc, const RECT& rc, int nDir, BOOL bEnabled, BOOL bPushed) {
  int cx = rc.right - rc.left, cy = rc.bottom - rc.top;
  int n = (int)MaxL(2, MinL(cx, cy) / 4);  // the half width of the triangle
  int x = rc.left + cx / 2 + (bPushed ? 1 : 0), y = rc.top + cy / 2 + (bPushed ? 1 : 0);
  POINT pts[3];
  switch (nDir) {
    case 0:
      pts[0].x = x - n, pts[0].y = y + n / 2;
      pts[1].x = x + n, pts[1].y = y + n / 2;
      pts[2].x = x, pts[2].y = y - n / 2 - (n & 1);
      break;
    case 1:
      pts[0].x = x - n, pts[0].y = y - n / 2;
      pts[1].x = x + n, pts[1].y = y - n / 2;
      pts[2].x = x, pts[2].y = y + n / 2 + (n & 1);
      break;
    case 2:
      pts[0].x = x + n / 2, pts[0].y = y - n;
      pts[1].x = x + n / 2, pts[1].y = y + n;
      pts[2].x = x - n / 2 - (n & 1), pts[2].y = y;
      break;
    default:
      pts[0].x = x - n / 2, pts[0].y = y - n;
      pts[1].x = x - n / 2, pts[1].y = y + n;
      pts[2].x = x + n / 2 + (n & 1), pts[2].y = y;
      break;
  }
  if (!bEnabled) {
    POINT ptsLight[3];
    for (int i = 0; i < 3; i++) {
      ptsLight[i].x = pts[i].x + 1;
      ptsLight[i].y = pts[i].y + 1;
    }
    FillPolygon(hdc, ptsLight, 3, ::GetSysColor(COLOR_BTNHIGHLIGHT));
    FillPolygon(hdc, pts, 3, ::GetSysColor(COLOR_BTNSHADOW));
    return;
  }
  FillPolygon(hdc, pts, 3, ::GetSysColor(COLOR_BTNTEXT));
}

/// Gives the height of the characters of a font.
int FontHeight(HWND hWnd, HFONT hFont) {
  HDC hdc = ::GetDC(hWnd);
  HGDIOBJ hOld = ::SelectObject(hdc, hFont != NULL ? hFont : ::GetStockObject(ANSI_VAR_FONT));
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(hWnd, hdc);
  return tm.tmHeight;
}

/// Registers one control class of the module with four bytes of window
/// data. Gives TRUE when the class is there.
BOOL RegisterCtlClass(LPCSTR lpszClass, WNDPROC pfn) {
  WNDCLASS wc;
  HINSTANCE hInst = AfxGetInstanceHandle();
  if (::GetClassInfo(hInst, lpszClass, &wc) && wc.lpfnWndProc == pfn) return TRUE;
  memset(&wc, 0, sizeof(wc));
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = pfn;
  wc.cbWndExtra = sizeof(LONG);
  wc.hInstance = hInst;
  wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = lpszClass;
  return ::RegisterClass(&wc) != 0;
}

/// Registers a control class with the name of the library and with the
/// name of comctl32.dll. Only the first must succeed.
BOOL RegisterCtlClasses(LPCSTR lpszName, LPCSTR lpszComctlName, WNDPROC pfn) {
  if (!RegisterCtlClass(lpszName, pfn)) return FALSE;
  RegisterCtlClass(lpszComctlName, pfn);
  return TRUE;
}

}  // namespace afx16

namespace {

using namespace afx16;

// =====================================================================
// The progress bar

// The data of a progress bar.
struct W16Progress {
  HWND hwnd;
  long nLower, nUpper, nPos, nStep;
  COLORREF clrBar, clrBk;
  int nMarquee;  // the offset of the marquee block
  /// Starts with the range 0 to 100 and the step 10.
  W16Progress()
      : hwnd(NULL), nLower(0), nUpper(100), nPos(0), nStep(10), clrBar(CLR_DEFAULT),
        clrBk(CLR_DEFAULT), nMarquee(0) {}
};

/// Draws a progress bar: a sunken frame, and blocks (or one smooth bar)
/// in the highlight color for the part that is done.
void PaintProgress(W16Progress* p, HDC hdc) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  DWORD dwStyle = StyleOf(p->hwnd);
  FillColor(hdc, rc, p->clrBk == CLR_DEFAULT ? ::GetSysColor(COLOR_BTNFACE) : p->clrBk);
  ::DrawEdge(hdc, &rc, BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
  ::InflateRect(&rc, -1, -1);
  BOOL bVertical = (dwStyle & PBS_VERTICAL) != 0;
  long nLen = bVertical ? rc.bottom - rc.top : rc.right - rc.left;
  if (nLen <= 0) return;
  long nSpan = MaxL(1, p->nUpper - p->nLower);
  long nStart = 0;
  long nFill = (ClampL(p->nPos, p->nLower, p->nUpper) - p->nLower) * nLen / nSpan;
  if (dwStyle & PBS_MARQUEE) {
    nStart = p->nMarquee % nLen;
    nFill = nLen / 4;
  }
  COLORREF clr = p->clrBar == CLR_DEFAULT ? ::GetSysColor(COLOR_HIGHLIGHT) : p->clrBar;
  BOOL bSmooth = (dwStyle & PBS_SMOOTH) != 0;
  long nThick = bVertical ? rc.right - rc.left : rc.bottom - rc.top;
  long nChunk = bSmooth ? nLen : MaxL(2, nThick * 2 / 3), nGap = bSmooth ? 0 : 2;
  for (long nAt = nStart; nAt < nStart + nFill; nAt += nChunk + nGap) {
    long a = nAt % nLen;
    long b = MinL(MinL(a + nChunk, nStart + nFill - (nAt - a)), nLen);
    if (b <= a) continue;
    RECT rcChunk;
    if (bVertical)
      ::SetRect(&rcChunk, rc.left, (int)(rc.bottom - b), rc.right, (int)(rc.bottom - a));
    else
      ::SetRect(&rcChunk, (int)(rc.left + a), rc.top, (int)(rc.left + b), rc.bottom);
    FillColor(hdc, rcChunk, clr);
  }
}

/// Sets the position of a progress bar in its range and redraws it.
void SetProgressPos(W16Progress* p, long nPos) {
  nPos = ClampL(nPos, p->nLower, p->nUpper);
  if (nPos == p->nPos) return;
  p->nPos = nPos;
  ::InvalidateRect(p->hwnd, NULL, FALSE);
}

/// Does the PBM_ messages and the painting of a progress bar.
LRESULT ProgressMessage(W16Progress* p, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintProgress(p, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_TIMER:
      p->nMarquee += 4;
      ::InvalidateRect(hWnd, NULL, FALSE);
      return 0;
    case PBM_SETRANGE:
    case PBM_SETRANGE32: {
      LRESULT lOld = MAKELONG((WORD)p->nLower, (WORD)p->nUpper);
      if (nMsg == PBM_SETRANGE) {
        p->nLower = (short)LOWORD(lParam);
        p->nUpper = (short)HIWORD(lParam);
      } else {
        p->nLower = (int)wParam;
        p->nUpper = (long)lParam;
      }
      p->nPos = ClampL(p->nPos, p->nLower, p->nUpper);
      ::InvalidateRect(hWnd, NULL, FALSE);
      return lOld;
    }
    case PBM_GETRANGE: {
      PBRANGE FAR* pRange = (PBRANGE FAR*)lParam;
      if (pRange != NULL) {
        pRange->iLow = (int)p->nLower;
        pRange->iHigh = (int)p->nUpper;
      }
      return wParam ? p->nLower : p->nUpper;
    }
    case PBM_SETPOS: {
      long nOld = p->nPos;
      SetProgressPos(p, (int)wParam);
      return nOld;
    }
    case PBM_DELTAPOS: {
      long nOld = p->nPos;
      SetProgressPos(p, p->nPos + (int)wParam);
      return nOld;
    }
    case PBM_GETPOS:
      return p->nPos;
    case PBM_SETSTEP: {
      long nOld = p->nStep;
      p->nStep = (int)wParam;
      return nOld;
    }
    case PBM_STEPIT: {
      long nOld = p->nPos, nNext = p->nPos + p->nStep;
      // Past the end, the position starts again from the other end.
      if (nNext > p->nUpper) nNext = p->nLower + (nNext - p->nUpper);
      if (nNext < p->nLower) nNext = p->nUpper - (p->nLower - nNext);
      SetProgressPos(p, nNext);
      return nOld;
    }
    case PBM_SETBARCOLOR:
    case PBM_SETBKCOLOR: {
      COLORREF& clr = nMsg == PBM_SETBARCOLOR ? p->clrBar : p->clrBk;
      COLORREF clrOld = clr;
      clr = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, FALSE);
      return (LRESULT)clrOld;
    }
    case PBM_SETMARQUEE:
      if (wParam)
        ::SetTimer(hWnd, 1, lParam != 0 ? (UINT)lParam : 30, NULL);
      else
        ::KillTimer(hWnd, 1);
      return TRUE;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the progress bar class.
LRESULT CALLBACK W16ProgressProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Progress* p = StateOf<W16Progress>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Progress>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, p);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return ProgressMessage(p, hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The trackbar

const int kThumbAlong = 11;   // the size of the thumb along the channel
const int kThumbAcross = 20;  // the size of the thumb across the channel
const int kTrackRepeat = 2;   // the timer of a page click that repeats

// The data of a trackbar.
struct W16Track {
  HWND hwnd;
  long nMin, nMax, nPos, nLine, nPage, nSelStart, nSelEnd;
  int nFreq;
  int nThumbLength;      // TBM_SETTHUMBLENGTH, for TBS_FIXEDLENGTH
  CDWordArray tics;      // the tick marks of TBM_SETTIC
  BOOL bDragging;        // the user drags the thumb
  int nGrab;             // where in the thumb the drag started
  int nPageDir;          // -1 or 1 while a page click repeats, else 0
  BOOL bFocus;
  /// Starts with the range 0 to 100, the line 1 and the page 20.
  W16Track()
      : hwnd(NULL), nMin(0), nMax(100), nPos(0), nLine(1), nPage(20), nSelStart(0), nSelEnd(0),
        nFreq(1), nThumbLength(0), bDragging(FALSE), nGrab(0), nPageDir(0), bFocus(FALSE) {}
};

/// Gives TRUE for a vertical trackbar.
BOOL TrackVertical(W16Track* t) { return (StyleOf(t->hwnd) & TBS_VERT) != 0; }

/// Gives the size of the thumb across the channel.
int TrackThumbAcross(W16Track* t) {
  if ((StyleOf(t->hwnd) & TBS_FIXEDLENGTH) && t->nThumbLength > 0) return t->nThumbLength;
  return kThumbAcross;
}

/// Gives the length of the control along the channel and its size
/// across.
void TrackSize(W16Track* t, int* pnAlong, int* pnAcross) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  BOOL bVertical = TrackVertical(t);
  *pnAlong = bVertical ? rc.bottom : rc.right;
  *pnAcross = bVertical ? rc.right : rc.bottom;
}

/// Gives where the thumb starts across the control. The ticks take 4
/// pixels on each side that has them.
int TrackThumbTop(W16Track* t) {
  DWORD dwStyle = StyleOf(t->hwnd);
  int nAlong, nAcross;
  TrackSize(t, &nAlong, &nAcross);
  if (dwStyle & TBS_NOTICKS) return (int)MaxL(0, (nAcross - TrackThumbAcross(t)) / 2);
  if (dwStyle & (TBS_BOTH | TBS_TOP)) return 6;
  return 2;
}

/// Gives the first and the last pixel of the channel where the middle
/// of the thumb can be.
void TrackSpan(W16Track* t, int* pnFirst, int* pnLast) {
  int nAlong, nAcross;
  TrackSize(t, &nAlong, &nAcross);
  *pnFirst = kThumbAlong / 2 + 2;
  *pnLast = (int)MaxL(*pnFirst, nAlong - kThumbAlong / 2 - 3);
}

/// Gives the pixel along the channel of a position.
int TrackPixelOf(W16Track* t, long nPos) {
  int nFirst, nLast;
  TrackSpan(t, &nFirst, &nLast);
  long nRange = MaxL(1, t->nMax - t->nMin);
  long nOffset = (ClampL(nPos, t->nMin, t->nMax) - t->nMin) * (nLast - nFirst) / nRange;
  if (StyleOf(t->hwnd) & TBS_REVERSED) return (int)(nLast - nOffset);
  return (int)(nFirst + nOffset);
}

/// Gives the position of a pixel along the channel, rounded to the
/// nearest position.
long TrackPosOf(W16Track* t, int nPixel) {
  int nFirst, nLast;
  TrackSpan(t, &nFirst, &nLast);
  long nWidth = MaxL(1, nLast - nFirst);
  long nOffset = ClampL(nPixel - nFirst, 0, nWidth);
  if (StyleOf(t->hwnd) & TBS_REVERSED) nOffset = nWidth - nOffset;
  return t->nMin + (nOffset * (t->nMax - t->nMin) + nWidth / 2) / nWidth;
}

/// Makes a rectangle from positions along and across the channel.
RECT TrackRect(W16Track* t, int nAlong1, int nAcross1, int nAlong2, int nAcross2) {
  RECT rc;
  if (TrackVertical(t))
    ::SetRect(&rc, nAcross1, nAlong1, nAcross2, nAlong2);
  else
    ::SetRect(&rc, nAlong1, nAcross1, nAlong2, nAcross2);
  return rc;
}

/// Gives the rectangle of the thumb.
RECT TrackThumbRect(W16Track* t) {
  int x = TrackPixelOf(t, t->nPos), nTop = TrackThumbTop(t);
  return TrackRect(t, x - kThumbAlong / 2, nTop, x - kThumbAlong / 2 + kThumbAlong,
                   nTop + TrackThumbAcross(t));
}

/// Gives the rectangle of the channel: 4 pixels across, in the middle
/// of the thumb.
RECT TrackChannelRect(W16Track* t) {
  int nFirst, nLast;
  TrackSpan(t, &nFirst, &nLast);
  int nMid = TrackThumbTop(t) + TrackThumbAcross(t) / 2;
  return TrackRect(t, nFirst - kThumbAlong / 2, nMid - 2, nLast + kThumbAlong / 2 + 1, nMid + 2);
}

/// Draws one tick mark at a pixel along the channel, on the sides that
/// have ticks.
void TrackDrawTick(W16Track* t, HDC hdc, int x, int nLen) {
  DWORD dwStyle = StyleOf(t->hwnd);
  COLORREF clr = ::GetSysColor(COLOR_BTNTEXT);
  int nTop = TrackThumbTop(t), nBottom = nTop + TrackThumbAcross(t);
  BOOL bVertical = TrackVertical(t);
  if (dwStyle & (TBS_BOTH | TBS_TOP)) {
    if (bVertical)
      DrawLine(hdc, nTop - 2 - nLen, x, nTop - 2, x, clr);
    else
      DrawLine(hdc, x, nTop - 2 - nLen, x, nTop - 2, clr);
  }
  if ((dwStyle & TBS_BOTH) || !(dwStyle & TBS_TOP)) {
    if (bVertical)
      DrawLine(hdc, nBottom + 1, x, nBottom + 1 + nLen, x, clr);
    else
      DrawLine(hdc, x, nBottom + 1, x, nBottom + 1 + nLen, clr);
  }
}

/// Draws the tick marks: the ends, the ticks of TBM_SETTIC, and with
/// TBS_AUTOTICKS one tick for each nFreq positions.
void TrackDrawTicks(W16Track* t, HDC hdc) {
  DWORD dwStyle = StyleOf(t->hwnd);
  if (dwStyle & TBS_NOTICKS) return;
  TrackDrawTick(t, hdc, TrackPixelOf(t, t->nMin), 3);
  TrackDrawTick(t, hdc, TrackPixelOf(t, t->nMax), 3);
  if ((dwStyle & TBS_AUTOTICKS) && t->nFreq > 0) {
    for (long n = t->nMin + t->nFreq; n < t->nMax; n += t->nFreq)
      TrackDrawTick(t, hdc, TrackPixelOf(t, n), 2);
  }
  for (int i = 0; i < t->tics.GetSize(); i++)
    TrackDrawTick(t, hdc, TrackPixelOf(t, (long)t->tics[i]), 2);
}

/// Gives the point at a position along and across the channel.
POINT TrackPoint(W16Track* t, int nAlong, int nAcross) {
  POINT pt;
  if (TrackVertical(t)) {
    pt.x = nAcross;
    pt.y = nAlong;
  } else {
    pt.x = nAlong;
    pt.y = nAcross;
  }
  return pt;
}

/// Draws the thumb. Without TBS_BOTH, the thumb points to the ticks.
/// The edges that face left (or up, along a vertical channel) are light,
/// the others are dark.
void TrackDrawThumb(W16Track* t, HDC hdc) {
  DWORD dwStyle = StyleOf(t->hwnd);
  int x = TrackPixelOf(t, t->nPos);
  int nLeft = x - kThumbAlong / 2, nRight = nLeft + kThumbAlong - 1;
  int nTop = TrackThumbTop(t), nBottom = nTop + TrackThumbAcross(t) - 1;
  int nPoint = kThumbAlong / 2;
  POINT pts[6];
  int n = 0;
  if (dwStyle & TBS_BOTH) {
    pts[n++] = TrackPoint(t, nLeft, nTop);
    pts[n++] = TrackPoint(t, nRight, nTop);
    pts[n++] = TrackPoint(t, nRight, nBottom);
    pts[n++] = TrackPoint(t, nLeft, nBottom);
  } else if (dwStyle & TBS_TOP) {
    pts[n++] = TrackPoint(t, x, nTop);
    pts[n++] = TrackPoint(t, nRight, nTop + nPoint);
    pts[n++] = TrackPoint(t, nRight, nBottom);
    pts[n++] = TrackPoint(t, nLeft, nBottom);
    pts[n++] = TrackPoint(t, nLeft, nTop + nPoint);
  } else {
    pts[n++] = TrackPoint(t, nLeft, nTop);
    pts[n++] = TrackPoint(t, nRight, nTop);
    pts[n++] = TrackPoint(t, nRight, nBottom - nPoint);
    pts[n++] = TrackPoint(t, x, nBottom);
    pts[n++] = TrackPoint(t, nLeft, nBottom - nPoint);
  }
  BOOL bVertical = TrackVertical(t);
  BOOL bEnabled = ::IsWindowEnabled(t->hwnd);
  FillPolygon(hdc, pts, n, ::GetSysColor(t->bDragging || !bEnabled ? COLOR_BTNHIGHLIGHT
                                                                      : COLOR_BTNFACE));
  // The points go clockwise on a horizontal trackbar. Swapping x and y
  // for a vertical trackbar makes them go the other way.
  int nSign = bVertical ? -1 : 1;
  for (int i = 0; i < n; i++) {
    POINT p = pts[i], q = pts[(i + 1) % n];
    int nx = nSign * (q.y - p.y), ny = -nSign * (q.x - p.x);  // the outer normal
    BOOL bLight = bVertical ? (ny < 0 || (ny == 0 && nx < 0)) : (nx < 0 || (nx == 0 && ny < 0));
    if (bLight) {
      DrawLine(hdc, p.x, p.y, q.x, q.y, ::GetSysColor(COLOR_BTNHIGHLIGHT));
    } else {
      DrawLine(hdc, p.x, p.y, q.x, q.y, ::GetSysColor(COLOR_WINDOWFRAME));
      int dx = nx > 0 ? -1 : (nx < 0 ? 1 : 0), dy = ny > 0 ? -1 : (ny < 0 ? 1 : 0);
      DrawLine(hdc, p.x + dx, p.y + dy, q.x + dx, q.y + dy, ::GetSysColor(COLOR_BTNSHADOW));
    }
  }
}

/// Draws a trackbar: the background, the channel, the selection, the
/// ticks, the thumb and the focus rectangle.
void PaintTrack(W16Track* t, HDC hdc) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  ::FillRect(hdc, &rc, ParentBrush(t->hwnd, hdc));
  RECT rcChannel = TrackChannelRect(t);
  FillColor(hdc, rcChannel, ::GetSysColor(COLOR_WINDOW));
  ::DrawEdge(hdc, &rcChannel, EDGE_SUNKEN, BF_RECT);
  if ((StyleOf(t->hwnd) & TBS_ENABLESELRANGE) && t->nSelEnd > t->nSelStart) {
    int a = TrackPixelOf(t, t->nSelStart), b = TrackPixelOf(t, t->nSelEnd);
    if (a > b) {
      int c = a;
      a = b;
      b = c;
    }
    RECT rcSel = rcChannel;
    if (TrackVertical(t)) {
      rcSel.top = a;
      rcSel.bottom = b + 1;
    } else {
      rcSel.left = a;
      rcSel.right = b + 1;
    }
    ::InflateRect(&rcSel, TrackVertical(t) ? -2 : 0, TrackVertical(t) ? 0 : -2);
    FillColor(hdc, rcSel, ::GetSysColor(COLOR_HIGHLIGHT));
  }
  TrackDrawTicks(t, hdc);
  TrackDrawThumb(t, hdc);
  if (t->bFocus) ::DrawFocusRect(hdc, &rc);
}

/// Sets the position of a trackbar in its range and redraws it when the
/// position changes.
void TrackSetPos(W16Track* t, long nPos) {
  nPos = ClampL(nPos, t->nMin, t->nMax);
  if (nPos == t->nPos) return;
  t->nPos = nPos;
  ::InvalidateRect(t->hwnd, NULL, TRUE);
}

/// Moves the thumb for a scroll code and tells the parent.
void TrackScroll(W16Track* t, UINT nCode) {
  switch (nCode) {
    case TB_LINEUP:
      TrackSetPos(t, t->nPos - t->nLine);
      break;
    case TB_LINEDOWN:
      TrackSetPos(t, t->nPos + t->nLine);
      break;
    case TB_PAGEUP:
      TrackSetPos(t, t->nPos - t->nPage);
      break;
    case TB_PAGEDOWN:
      TrackSetPos(t, t->nPos + t->nPage);
      break;
    case TB_TOP:
      TrackSetPos(t, t->nMin);
      break;
    case TB_BOTTOM:
      TrackSetPos(t, t->nMax);
      break;
  }
  SendScroll(t->hwnd, TrackVertical(t), nCode, (int)t->nPos);
}

/// Gives the pixel of a mouse point along the channel.
int TrackAlongOf(W16Track* t, LPARAM lParam) {
  return TrackVertical(t) ? (short)HIWORD(lParam) : (short)LOWORD(lParam);
}

/// Pages the thumb toward the mouse, while the mouse is not on the
/// thumb.
void TrackPageStep(W16Track* t) {
  POINT pt;
  ::GetCursorPos(&pt);
  ::ScreenToClient(t->hwnd, &pt);
  RECT rcThumb = TrackThumbRect(t);
  if (::PtInRect(&rcThumb, pt)) return;
  int nAlong = TrackVertical(t) ? pt.y : pt.x;
  int nThumb = TrackPixelOf(t, t->nPos);
  int nDir = nAlong < nThumb ? -1 : 1;
  if (StyleOf(t->hwnd) & TBS_REVERSED) nDir = -nDir;
  if (nDir != t->nPageDir) return;
  TrackScroll(t, nDir < 0 ? TB_PAGEUP : TB_PAGEDOWN);
}

/// Does the mouse, the keys and the TBM_ messages of a trackbar.
LRESULT TrackMessage(W16Track* t, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintTrack(t, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_ENABLE:
    case WM_SIZE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      t->bFocus = nMsg == WM_SETFOCUS;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: {
      ::SetFocus(hWnd);
      ::SetCapture(hWnd);
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT rcThumb = TrackThumbRect(t);
      if (::PtInRect(&rcThumb, pt)) {
        t->bDragging = TRUE;
        t->nGrab = TrackAlongOf(t, lParam) - TrackPixelOf(t, t->nPos);
        ::InvalidateRect(hWnd, NULL, TRUE);
        return 0;
      }
      int nDir = TrackAlongOf(t, lParam) < TrackPixelOf(t, t->nPos) ? -1 : 1;
      if (StyleOf(hWnd) & TBS_REVERSED) nDir = -nDir;
      t->nPageDir = nDir;
      TrackScroll(t, nDir < 0 ? TB_PAGEUP : TB_PAGEDOWN);
      ::SetTimer(hWnd, kTrackRepeat, 400, NULL);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (t->bDragging) {
        long nPos = TrackPosOf(t, TrackAlongOf(t, lParam) - t->nGrab);
        if (nPos != t->nPos) {
          TrackSetPos(t, nPos);
          SendScroll(hWnd, TrackVertical(t), TB_THUMBTRACK, (int)t->nPos);
        }
      }
      return 0;
    case WM_TIMER:
      if (wParam == kTrackRepeat && t->nPageDir != 0) {
        ::SetTimer(hWnd, kTrackRepeat, 100, NULL);
        TrackPageStep(t);
      }
      return 0;
    case WM_LBUTTONUP:
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      ::KillTimer(hWnd, kTrackRepeat);
      if (t->bDragging) {
        t->bDragging = FALSE;
        ::InvalidateRect(hWnd, NULL, TRUE);
        SendScroll(hWnd, TrackVertical(t), TB_THUMBPOSITION, (int)t->nPos);
      }
      t->nPageDir = 0;
      SendScroll(hWnd, TrackVertical(t), TB_ENDTRACK, (int)t->nPos);
      return 0;
    case WM_KEYDOWN: {
      BOOL bReversed = (StyleOf(hWnd) & TBS_REVERSED) != 0;
      UINT nCode;
      switch (wParam) {
        case VK_LEFT:
        case VK_UP:
          nCode = bReversed ? TB_LINEDOWN : TB_LINEUP;
          break;
        case VK_RIGHT:
        case VK_DOWN:
          nCode = bReversed ? TB_LINEUP : TB_LINEDOWN;
          break;
        case VK_PRIOR:
          nCode = TB_PAGEUP;
          break;
        case VK_NEXT:
          nCode = TB_PAGEDOWN;
          break;
        case VK_HOME:
          nCode = TB_TOP;
          break;
        case VK_END:
          nCode = TB_BOTTOM;
          break;
        default:
          return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
      }
      TrackScroll(t, nCode);
      return 0;
    }
    case WM_KEYUP:
      switch (wParam) {
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_HOME:
        case VK_END:
          SendScroll(hWnd, TrackVertical(t), TB_ENDTRACK, (int)t->nPos);
          return 0;
      }
      break;
    case TBM_GETPOS:
      return t->nPos;
    case TBM_GETRANGEMIN:
      return t->nMin;
    case TBM_GETRANGEMAX:
      return t->nMax;
    case TBM_SETPOS:
      TrackSetPos(t, (long)lParam);
      return 0;
    case TBM_SETRANGE:
      t->nMin = (short)LOWORD(lParam);
      t->nMax = (short)HIWORD(lParam);
      t->nPos = ClampL(t->nPos, t->nMin, MaxL(t->nMin, t->nMax));
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_SETRANGEMIN:
    case TBM_SETRANGEMAX:
      if (nMsg == TBM_SETRANGEMIN)
        t->nMin = (long)lParam;
      else
        t->nMax = (long)lParam;
      t->nPos = ClampL(t->nPos, t->nMin, MaxL(t->nMin, t->nMax));
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_GETTIC:
      return (int)wParam >= 0 && (int)wParam < t->tics.GetSize() ? (LRESULT)t->tics[(int)wParam]
                                                                  : -1;
    case TBM_SETTIC:
      if ((long)lParam < t->nMin || (long)lParam > t->nMax) return FALSE;
      t->tics.Add((DWORD)lParam);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    case TBM_CLEARTICS:
      t->tics.RemoveAll();
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_GETNUMTICS:
      if (StyleOf(hWnd) & TBS_NOTICKS) return 0;
      if ((StyleOf(hWnd) & TBS_AUTOTICKS) && t->nFreq > 0)
        return (t->nMax - t->nMin + t->nFreq - 1) / t->nFreq + 1;
      return t->tics.GetSize() + 2;
    case TBM_GETTICPOS:
      return (int)wParam >= 0 && (int)wParam < t->tics.GetSize()
                 ? TrackPixelOf(t, (long)t->tics[(int)wParam])
                 : -1;
    case TBM_GETPTICS:
      return 0;
    case TBM_SETTICFREQ:
      t->nFreq = (int)wParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_SETSEL:
      t->nSelStart = (short)LOWORD(lParam);
      t->nSelEnd = (short)HIWORD(lParam);
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_SETSELSTART:
    case TBM_SETSELEND:
      if (nMsg == TBM_SETSELSTART)
        t->nSelStart = (long)lParam;
      else
        t->nSelEnd = (long)lParam;
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_GETSELSTART:
      return t->nSelStart;
    case TBM_GETSELEND:
      return t->nSelEnd;
    case TBM_CLEARSEL:
      t->nSelStart = t->nSelEnd = 0;
      if (wParam) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_SETPAGESIZE: {
      long nOld = t->nPage;
      t->nPage = (long)lParam;
      return nOld;
    }
    case TBM_GETPAGESIZE:
      return t->nPage;
    case TBM_SETLINESIZE: {
      long nOld = t->nLine;
      t->nLine = (long)lParam;
      return nOld;
    }
    case TBM_GETLINESIZE:
      return t->nLine;
    case TBM_GETTHUMBRECT:
      if (lParam != 0) *(RECT FAR*)lParam = TrackThumbRect(t);
      return 0;
    case TBM_GETCHANNELRECT:
      if (lParam != 0) *(RECT FAR*)lParam = TrackChannelRect(t);
      return 0;
    case TBM_SETTHUMBLENGTH:
      t->nThumbLength = (int)wParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TBM_GETTHUMBLENGTH:
      return TrackThumbAcross(t);
    case TBM_SETBUDDY:
    case TBM_GETBUDDY:
    case TBM_SETTOOLTIPS:
    case TBM_GETTOOLTIPS:
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the trackbar class.
LRESULT CALLBACK W16TrackProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Track* t = StateOf<W16Track>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Track>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (t == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, t);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return TrackMessage(t, hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The up-down control

const int kSpinRepeat = 1;  // the timer of an arrow that repeats
const char kSpinProp[] = "AfxUpDown16";  // the property of a buddy with UDS_ARROWKEYS

// The data of an up-down control.
struct W16Spin {
  HWND hwnd;
  long nLower, nUpper, nPos;
  HWND hwndBuddy;
  WNDPROC pfnBuddyProc;  // the procedure of the buddy before UDS_ARROWKEYS
  int nBase;
  CArray<UDACCEL, const UDACCEL&> accel;
  int nPressed;       // 1 for the up (right) arrow, -1 for the down (left), else 0
  DWORD dwPressTime;  // when the arrow was pressed
  /// Starts with the range 100 to 0 and the accelerations of comctl32.
  W16Spin()
      : hwnd(NULL), nLower(100), nUpper(0), nPos(0), hwndBuddy(NULL), pfnBuddyProc(NULL),
        nBase(10), nPressed(0), dwPressTime(0) {
    static const UDACCEL aDefault[3] = {{0, 1}, {2, 5}, {5, 20}};
    for (int i = 0; i < 3; i++) accel.Add(aDefault[i]);
  }
};

/// Gives TRUE for an up-down control with arrows to the left and the
/// right.
BOOL SpinHorizontal(W16Spin* s) { return (StyleOf(s->hwnd) & UDS_HORZ) != 0; }

/// Gives the rectangles of the two arrows: rcUp for the arrow that goes
/// to the upper end (top or right), rcDown for the other.
void SpinArrowRects(W16Spin* s, RECT* prcUp, RECT* prcDown) {
  RECT rc;
  ::GetClientRect(s->hwnd, &rc);
  *prcUp = rc;
  *prcDown = rc;
  if (SpinHorizontal(s)) {
    int x = (rc.left + rc.right) / 2;
    prcDown->right = x;
    prcUp->left = x;
  } else {
    int y = (rc.top + rc.bottom) / 2;
    prcUp->bottom = y;
    prcDown->top = y;
  }
}

/// Draws the two arrow buttons of an up-down control.
void PaintSpin(W16Spin* s, HDC hdc) {
  RECT rcUp, rcDown;
  SpinArrowRects(s, &rcUp, &rcDown);
  BOOL bEnabled = ::IsWindowEnabled(s->hwnd);
  BOOL bHorz = SpinHorizontal(s);
  DrawButtonFace(hdc, rcUp, s->nPressed == 1);
  DrawArrow(hdc, rcUp, bHorz ? 3 : 0, bEnabled, s->nPressed == 1);
  DrawButtonFace(hdc, rcDown, s->nPressed == -1);
  DrawArrow(hdc, rcDown, bHorz ? 2 : 1, bEnabled, s->nPressed == -1);
}

/// Writes the position into the buddy window, with UDS_SETBUDDYINT. In
/// base 10, a comma is put between each 3 digits, unless UDS_NOTHOUSANDS.
void SpinSetBuddyText(W16Spin* s) {
  if (s->hwndBuddy == NULL || !(StyleOf(s->hwnd) & UDS_SETBUDDYINT)) return;
  char szNum[24];
  if (s->nBase == 16) {
    wsprintf(szNum, "0x%lX", s->nPos);
  } else {
    char szDigits[16];
    wsprintf(szDigits, "%ld", s->nPos < 0 ? -s->nPos : s->nPos);
    BOOL bSeparate = !(StyleOf(s->hwnd) & UDS_NOTHOUSANDS);
    int nDigits = lstrlen(szDigits), j = 0;
    if (s->nPos < 0) szNum[j++] = '-';
    for (int i = 0; i < nDigits; i++) {
      if (bSeparate && i > 0 && (nDigits - i) % 3 == 0) szNum[j++] = ',';
      szNum[j++] = szDigits[i];
    }
    szNum[j] = 0;
  }
  ::SetWindowText(s->hwndBuddy, szNum);
}

/// Reads the position from the buddy window, with UDS_SETBUDDYINT. Gives
/// FALSE when the text is not a number.
BOOL SpinReadBuddy(W16Spin* s) {
  if (s->hwndBuddy == NULL || !(StyleOf(s->hwnd) & UDS_SETBUDDYINT)) return TRUE;
  char szText[32];
  ::GetWindowText(s->hwndBuddy, szText, sizeof(szText));
  const char* p = szText;
  while (*p == ' ') p++;
  BOOL bNegative = *p == '-';
  if (bNegative) p++;
  int nBase = s->nBase;
  if (nBase == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  long n = 0;
  BOOL bDigits = FALSE;
  for (; *p != 0; p++) {
    int d;
    if (*p >= '0' && *p <= '9')
      d = *p - '0';
    else if (nBase == 16 && *p >= 'a' && *p <= 'f')
      d = *p - 'a' + 10;
    else if (nBase == 16 && *p >= 'A' && *p <= 'F')
      d = *p - 'A' + 10;
    else if (*p == ',' && nBase == 10)
      continue;
    else
      break;
    n = n * nBase + d;
    bDigits = TRUE;
  }
  if (!bDigits) return FALSE;
  s->nPos = bNegative ? -n : n;
  return TRUE;
}

/// Puts the control next to its buddy, with UDS_ALIGNRIGHT or
/// UDS_ALIGNLEFT: the buddy gets narrower by the width of the control.
void SpinAlign(W16Spin* s) {
  DWORD dwStyle = StyleOf(s->hwnd);
  if (s->hwndBuddy == NULL || !(dwStyle & (UDS_ALIGNRIGHT | UDS_ALIGNLEFT))) return;
  RECT rcBuddy, rcSelf;
  ::GetWindowRect(s->hwndBuddy, &rcBuddy);
  ::GetWindowRect(s->hwnd, &rcSelf);
  HWND hParent = ::GetParent(s->hwnd);
  ::ScreenToClient(hParent, (POINT*)&rcBuddy.left);
  ::ScreenToClient(hParent, (POINT*)&rcBuddy.right);
  int cx = rcSelf.right - rcSelf.left;
  int cy = rcBuddy.bottom - rcBuddy.top;
  if (dwStyle & UDS_ALIGNRIGHT) {
    ::MoveWindow(s->hwndBuddy, rcBuddy.left, rcBuddy.top, rcBuddy.right - rcBuddy.left - cx, cy,
                 TRUE);
    ::MoveWindow(s->hwnd, rcBuddy.right - cx, rcBuddy.top, cx, cy, TRUE);
  } else {
    ::MoveWindow(s->hwndBuddy, rcBuddy.left + cx, rcBuddy.top, rcBuddy.right - rcBuddy.left - cx,
                 cy, TRUE);
    ::MoveWindow(s->hwnd, rcBuddy.left, rcBuddy.top, cx, cy, TRUE);
  }
}

LRESULT CALLBACK SpinBuddyProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

/// Takes the buddy back from the control: removes the procedure of
/// UDS_ARROWKEYS.
void SpinReleaseBuddy(W16Spin* s) {
  if (s->hwndBuddy != NULL && s->pfnBuddyProc != NULL && ::IsWindow(s->hwndBuddy)) {
    ::SetWindowLong(s->hwndBuddy, GWL_WNDPROC, (LONG)s->pfnBuddyProc);
    ::RemoveProp(s->hwndBuddy, kSpinProp);
  }
  s->pfnBuddyProc = NULL;
  s->hwndBuddy = NULL;
}

/// Makes a window the buddy of the control: aligns the control with it,
/// gives it the arrow keys with UDS_ARROWKEYS, and writes the position
/// into it with UDS_SETBUDDYINT.
void SpinSetBuddy(W16Spin* s, HWND hwndBuddy) {
  SpinReleaseBuddy(s);
  s->hwndBuddy = hwndBuddy;
  if (hwndBuddy == NULL) return;
  if (StyleOf(s->hwnd) & UDS_ARROWKEYS) {
    ::SetProp(hwndBuddy, kSpinProp, (HANDLE)(UINT)s->hwnd);
    s->pfnBuddyProc =
        (WNDPROC)::SetWindowLong(hwndBuddy, GWL_WNDPROC, (LONG)(WNDPROC)SpinBuddyProc);
  }
  SpinAlign(s);
  SpinSetBuddyText(s);
}

/// Gives the step for an arrow that the user holds: the acceleration
/// whose time has passed.
long SpinStep(W16Spin* s) {
  DWORD dwSeconds = (::GetTickCount() - s->dwPressTime) / 1000;
  long nStep = 1;
  for (int i = 0; i < s->accel.GetSize(); i++)
    if (dwSeconds >= s->accel[i].nSec) nStep = s->accel[i].nInc;
  return nStep;
}

/// Moves the position one step toward the upper end (nDir 1) or the
/// lower end (nDir -1). The parent gets UDN_DELTAPOS and can refuse the
/// change, then WM_VSCROLL (WM_HSCROLL with UDS_HORZ) with
/// SB_THUMBPOSITION.
void SpinStepPos(W16Spin* s, int nDir) {
  SpinReadBuddy(s);
  long nDelta = SpinStep(s) * nDir;
  if (s->nUpper < s->nLower) nDelta = -nDelta;
  NMUPDOWN nm;
  memset(&nm, 0, sizeof(nm));
  nm.iPos = (int)s->nPos;
  nm.iDelta = (int)nDelta;
  if (SendNotify(s->hwnd, UDN_DELTAPOS, &nm.hdr) != 0) return;
  nDelta = nm.iDelta;
  long nLow = MinL(s->nLower, s->nUpper), nHigh = MaxL(s->nLower, s->nUpper);
  long nPos = s->nPos + nDelta;
  if (nPos < nLow || nPos > nHigh) {
    if (StyleOf(s->hwnd) & UDS_WRAP)
      nPos = nPos < nLow ? nHigh : nLow;
    else
      nPos = ClampL(nPos, nLow, nHigh);
  }
  BOOL bChanged = nPos != s->nPos;
  s->nPos = nPos;
  if (bChanged) SpinSetBuddyText(s);
  SendScroll(s->hwnd, !SpinHorizontal(s), SB_THUMBPOSITION, (int)s->nPos);
}

/// The procedure that a buddy gets with UDS_ARROWKEYS: the up and down
/// keys step the control.
LRESULT CALLBACK SpinBuddyProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  HWND hwndSpin = (HWND)(UINT)::GetProp(hWnd, kSpinProp);
  W16Spin* s = hwndSpin != NULL ? StateOf<W16Spin>(hwndSpin) : NULL;
  if (s == NULL || s->pfnBuddyProc == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  WNDPROC pfnOld = s->pfnBuddyProc;
  if (nMsg == WM_KEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
    SpinStepPos(s, wParam == VK_UP ? 1 : -1);
    return 0;
  }
  if (nMsg == WM_KEYUP && (wParam == VK_UP || wParam == VK_DOWN)) {
    SendScroll(s->hwnd, !SpinHorizontal(s), SB_ENDSCROLL, (int)s->nPos);
    return 0;
  }
  if (nMsg == WM_GETDLGCODE)
    return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam) | DLGC_WANTARROWS;
  if (nMsg == WM_NCDESTROY) {
    ::SetWindowLong(hWnd, GWL_WNDPROC, (LONG)pfnOld);
    ::RemoveProp(hWnd, kSpinProp);
    s->pfnBuddyProc = NULL;
    s->hwndBuddy = NULL;
  }
  return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
}

/// Does the mouse and the UDM_ messages of an up-down control.
LRESULT SpinMessage(W16Spin* s, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_CREATE:
      if (StyleOf(hWnd) & UDS_AUTOBUDDY) SpinSetBuddy(s, ::GetWindow(hWnd, GW_HWNDPREV));
      return 0;
    case WM_DESTROY:
      SpinReleaseBuddy(s);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintSpin(s, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_ENABLE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_LBUTTONDOWN: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      RECT rcUp, rcDown;
      SpinArrowRects(s, &rcUp, &rcDown);
      s->nPressed = ::PtInRect(&rcUp, pt) ? 1 : (::PtInRect(&rcDown, pt) ? -1 : 0);
      if (s->nPressed == 0) return 0;
      if (s->hwndBuddy != NULL) ::SetFocus(s->hwndBuddy);
      ::SetCapture(hWnd);
      s->dwPressTime = ::GetTickCount();
      ::InvalidateRect(hWnd, NULL, FALSE);
      SpinStepPos(s, s->nPressed);
      ::SetTimer(hWnd, kSpinRepeat, 500, NULL);
      return 0;
    }
    case WM_TIMER:
      if (wParam == kSpinRepeat && s->nPressed != 0) {
        ::SetTimer(hWnd, kSpinRepeat, 100, NULL);
        POINT pt;
        ::GetCursorPos(&pt);
        ::ScreenToClient(hWnd, &pt);
        RECT rcUp, rcDown;
        SpinArrowRects(s, &rcUp, &rcDown);
        if (::PtInRect(s->nPressed == 1 ? &rcUp : &rcDown, pt)) SpinStepPos(s, s->nPressed);
      }
      return 0;
    case WM_LBUTTONUP:
      if (s->nPressed == 0) return 0;
      s->nPressed = 0;
      ::KillTimer(hWnd, kSpinRepeat);
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      ::InvalidateRect(hWnd, NULL, FALSE);
      SendScroll(hWnd, !SpinHorizontal(s), SB_ENDSCROLL, (int)s->nPos);
      return 0;
    case UDM_SETRANGE:
      s->nUpper = (short)LOWORD(lParam);
      s->nLower = (short)HIWORD(lParam);
      return 0;
    case UDM_GETRANGE:
      return MAKELONG((WORD)s->nUpper, (WORD)s->nLower);
    case UDM_SETRANGE32:
      s->nLower = (int)wParam;
      s->nUpper = (long)lParam;
      return 0;
    case UDM_GETRANGE32: {
      // A 16-bit wParam holds only the offset of the lower end. The two
      // ends are in the same segment (CSpinButtonCtrl::GetRange32 gives
      // two local variables), so the offset goes with the segment of
      // lParam.
      if (lParam != 0) {
        *(int FAR*)lParam = (int)s->nUpper;
        if (wParam != 0) *(int FAR*)MAKELP(SELECTOROF((LPVOID)lParam), wParam) = (int)s->nLower;
      }
      return 0;
    }
    case UDM_SETPOS:
    case UDM_SETPOS32: {
      long nOld = s->nPos;
      long nPos = nMsg == UDM_SETPOS ? (short)LOWORD(lParam) : (long)lParam;
      s->nPos = ClampL(nPos, MinL(s->nLower, s->nUpper), MaxL(s->nLower, s->nUpper));
      SpinSetBuddyText(s);
      return nMsg == UDM_SETPOS ? (LRESULT)(WORD)nOld : nOld;
    }
    case UDM_GETPOS: {
      BOOL bOK = SpinReadBuddy(s);
      return MAKELONG((WORD)s->nPos, bOK ? 0 : 1);
    }
    case UDM_GETPOS32: {
      BOOL bOK = SpinReadBuddy(s);
      if (lParam != 0) *(BOOL FAR*)lParam = !bOK;
      return s->nPos;
    }
    case UDM_SETBUDDY: {
      HWND hwndOld = s->hwndBuddy;
      SpinSetBuddy(s, (HWND)wParam);
      return (LRESULT)(UINT)hwndOld;
    }
    case UDM_GETBUDDY:
      return (LRESULT)(UINT)s->hwndBuddy;
    case UDM_SETACCEL: {
      const UDACCEL FAR* pAccel = (const UDACCEL FAR*)lParam;
      if (pAccel == NULL || wParam == 0) return FALSE;
      s->accel.RemoveAll();
      for (UINT i = 0; i < wParam; i++) s->accel.Add(pAccel[i]);
      return TRUE;
    }
    case UDM_GETACCEL: {
      UDACCEL FAR* pAccel = (UDACCEL FAR*)lParam;
      UINT n = (UINT)s->accel.GetSize();
      for (UINT i = 0; pAccel != NULL && i < n && i < wParam; i++) pAccel[i] = s->accel[i];
      return n;
    }
    case UDM_SETBASE: {
      if (wParam != 10 && wParam != 16) return 0;
      int nOld = s->nBase;
      s->nBase = (int)wParam;
      SpinSetBuddyText(s);
      return nOld;
    }
    case UDM_GETBASE:
      return s->nBase;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the up-down class.
LRESULT CALLBACK W16SpinProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Spin* s = StateOf<W16Spin>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Spin>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (s == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, s);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return SpinMessage(s, hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The tab control

// One tab.
struct W16TabItem {
  CString strText;
  int iImage;
  LPARAM lParam;
  DWORD dwState;
};

// The data of a tab control.
struct W16Tab {
  HWND hwnd;
  CPtrArray items;                  // the tabs (W16TabItem*)
  CArray<RECT, const RECT&> rects;  // the rectangle of each tab, unselected
  int iSel;                         // the selected tab, or -1
  int iFocus;                       // the tab with the focus, or -1
  BOOL bFocus;
  HFONT hFont;
  SIZE sizeFixed;    // TCM_SETITEMSIZE
  SIZE sizePadding;  // TCM_SETPADDING
  int nMinWidth;     // TCM_SETMINTABWIDTH
  int nRowHeight;    // the height of the row of tabs
  /// Starts with no tabs and the padding of comctl32.
  W16Tab() : hwnd(NULL), iSel(-1), iFocus(-1), bFocus(FALSE), hFont(NULL), nMinWidth(-1),
             nRowHeight(0) {
    sizeFixed.cx = sizeFixed.cy = 0;
    sizePadding.cx = 6;
    sizePadding.cy = 3;
  }
  /// Deletes the tabs.
  ~W16Tab() {
    for (int i = 0; i < items.GetSize(); i++) delete (W16TabItem*)items[i];
  }
};

/// Gives the tab i of a tab control.
W16TabItem& TabItem(W16Tab* t, int i) { return *(W16TabItem*)t->items[i]; }

/// Deletes all the tabs of a tab control.
void TabDeleteAll(W16Tab* t) {
  for (int i = 0; i < t->items.GetSize(); i++) delete &TabItem(t, i);
  t->items.RemoveAll();
}

/// Gives the font of a tab control.
HFONT TabFont(W16Tab* t) {
  return t->hFont != NULL ? t->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Calculates the rectangle of each tab, in one row. The selected tab is
/// 2 pixels larger on each side but the bottom, which TabItemRect adds.
void TabLayout(W16Tab* t) {
  int nCount = (int)t->items.GetSize();
  t->rects.SetSize(nCount);
  int nHeight = t->sizeFixed.cy > 0 ? t->sizeFixed.cy
                                    : FontHeight(t->hwnd, t->hFont) + 2 * t->sizePadding.cy + 2;
  t->nRowHeight = nHeight + 2;
  BOOL bFixed = (StyleOf(t->hwnd) & TCS_FIXEDWIDTH) && t->sizeFixed.cx > 0;
  HDC hdc = ::GetDC(t->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, TabFont(t));
  int x = 2;
  for (int i = 0; i < nCount; i++) {
    int cx;
    if (bFixed) {
      cx = t->sizeFixed.cx;
    } else {
      SIZE size;
      const CString& str = TabItem(t, i).strText;
      ::GetTextExtentPoint32(hdc, str, str.GetLength(), &size);
      cx = size.cx + 2 * t->sizePadding.cx;
      int nMin = t->nMinWidth >= 0 ? t->nMinWidth : 42;
      if (cx < nMin) cx = nMin;
    }
    ::SetRect(&t->rects[i], x, 2, x + cx, 2 + nHeight);
    x += cx;
  }
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(t->hwnd, hdc);
}

/// Gives the rectangle of a tab as it is drawn: the selected tab is
/// larger.
RECT TabItemRect(W16Tab* t, int i) {
  RECT rc = t->rects[i];
  if (i == t->iSel && !(StyleOf(t->hwnd) & TCS_BUTTONS)) {
    ::InflateRect(&rc, 2, 2);
    rc.bottom -= 1;
  }
  return rc;
}

/// Gives the tab at a point, or -1. The selected tab is tested first,
/// because it covers parts of its neighbors.
int TabHitTest(W16Tab* t, POINT pt) {
  if (t->iSel >= 0 && t->iSel < t->items.GetSize()) {
    RECT rc = TabItemRect(t, t->iSel);
    if (::PtInRect(&rc, pt)) return t->iSel;
  }
  for (int i = 0; i < t->rects.GetSize(); i++)
    if (::PtInRect(&t->rects[i], pt)) return i;
  return -1;
}

/// Draws one tab: a frame with cut corners, open at the bottom, and its
/// text. With TCS_BUTTONS, a button.
void TabPaintItem(W16Tab* t, HDC hdc, int i) {
  RECT rc = TabItemRect(t, i);
  BOOL bSelected = i == t->iSel;
  COLORREF clrLight = ::GetSysColor(COLOR_BTNHIGHLIGHT);
  COLORREF clrShadow = ::GetSysColor(COLOR_BTNSHADOW);
  COLORREF clrDark = ::GetSysColor(COLOR_WINDOWFRAME);
  if (StyleOf(t->hwnd) & TCS_BUTTONS) {
    DrawButtonFace(hdc, rc, bSelected);
  } else {
    FillColor(hdc, rc, ::GetSysColor(COLOR_BTNFACE));
    int l = rc.left, r = rc.right - 1, tp = rc.top, b = rc.bottom;
    DrawLine(hdc, l, b, l, tp + 2, clrLight);
    DrawLine(hdc, l, tp + 2, l + 2, tp, clrLight);
    DrawLine(hdc, l + 2, tp, r - 1, tp, clrLight);
    DrawLine(hdc, r - 1, tp + 1, r, tp + 2, clrDark);
    DrawLine(hdc, r, tp + 2, r, b, clrDark);
    DrawLine(hdc, r - 1, tp + 1, r - 1, b, clrShadow);
  }
  RECT rcText = rc;
  if (bSelected && (StyleOf(t->hwnd) & TCS_BUTTONS)) ::OffsetRect(&rcText, 1, 1);
  HGDIOBJ hOld = ::SelectObject(hdc, TabFont(t));
  int nOldMode = ::SetBkMode(hdc, TRANSPARENT);
  COLORREF clrOld = ::SetTextColor(hdc, ::GetSysColor(COLOR_BTNTEXT));
  const CString& str = TabItem(t, i).strText;
  ::DrawText(hdc, str, str.GetLength(), &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  ::SetTextColor(hdc, clrOld);
  ::SetBkMode(hdc, nOldMode);
  ::SelectObject(hdc, hOld);
  if (bSelected && t->bFocus) {
    RECT rcFocus = rc;
    ::InflateRect(&rcFocus, -3, -3);
    ::DrawFocusRect(hdc, &rcFocus);
  }
}

/// Draws a tab control: the background, the frame of the display area
/// with a gap under the selected tab, and the tabs.
void PaintTab(W16Tab* t, HDC hdc) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  ::FillRect(hdc, &rc, ParentBrush(t->hwnd, hdc));
  BOOL bButtons = (StyleOf(t->hwnd) & TCS_BUTTONS) != 0;
  if (!bButtons) {
    RECT rcBody = rc;
    rcBody.top = t->nRowHeight;
    FillColor(hdc, rcBody, ::GetSysColor(COLOR_BTNFACE));
    ::DrawEdge(hdc, &rcBody, EDGE_RAISED, BF_LEFT | BF_RIGHT | BF_BOTTOM);
    COLORREF clrLight = ::GetSysColor(COLOR_BTNHIGHLIGHT);
    if (t->iSel >= 0 && t->iSel < t->items.GetSize()) {
      RECT rcSel = TabItemRect(t, t->iSel);
      DrawLine(hdc, rcBody.left, rcBody.top, rcSel.left, rcBody.top, clrLight);
      DrawLine(hdc, rcSel.right - 1, rcBody.top, rcBody.right - 1, rcBody.top, clrLight);
    } else {
      DrawLine(hdc, rcBody.left, rcBody.top, rcBody.right - 1, rcBody.top, clrLight);
    }
  }
  for (int i = 0; i < t->items.GetSize(); i++)
    if (i != t->iSel) TabPaintItem(t, hdc, i);
  if (t->iSel >= 0 && t->iSel < t->items.GetSize()) TabPaintItem(t, hdc, t->iSel);
}

/// Makes a tab the selected tab, as the user does: the parent gets
/// TCN_SELCHANGING, which can refuse the change, then TCN_SELCHANGE.
void TabSelectByUser(W16Tab* t, int i) {
  if (i < 0 || i >= t->items.GetSize() || i == t->iSel) return;
  NMHDR nm;
  if (SendNotify(t->hwnd, TCN_SELCHANGING, &nm) != 0) return;
  t->iSel = t->iFocus = i;
  ::InvalidateRect(t->hwnd, NULL, TRUE);
  SendNotify(t->hwnd, TCN_SELCHANGE, &nm);
}

/// Copies the fields of a TCITEM that its mask names into a tab.
void TabSetItem(W16TabItem& item, const TCITEMA FAR* pItem) {
  if ((pItem->mask & TCIF_TEXT) && pItem->pszText != NULL) item.strText = pItem->pszText;
  if (pItem->mask & TCIF_IMAGE) item.iImage = pItem->iImage;
  if (pItem->mask & TCIF_PARAM) item.lParam = pItem->lParam;
  if (pItem->mask & TCIF_STATE)
    item.dwState = (item.dwState & ~pItem->dwStateMask) | (pItem->dwState & pItem->dwStateMask);
}

/// Changes a rectangle between the window rectangle of the control and
/// its display area. bLarger: from the display area to the window.
void TabAdjustRect(W16Tab* t, BOOL bLarger, RECT FAR* prc) {
  if (prc == NULL) return;
  if (t->nRowHeight == 0) TabLayout(t);
  if (StyleOf(t->hwnd) & TCS_BUTTONS) {
    if (bLarger)
      prc->top -= t->nRowHeight;
    else
      prc->top += t->nRowHeight;
    return;
  }
  if (bLarger) {
    prc->left -= 4;
    prc->right += 4;
    prc->top -= t->nRowHeight + 4;
    prc->bottom += 4;
  } else {
    prc->left += 4;
    prc->right -= 4;
    prc->top += t->nRowHeight + 4;
    prc->bottom -= 4;
  }
}

/// Does the mouse, the keys and the TCM_ messages of a tab control.
LRESULT TabMessage(W16Tab* t, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_CREATE:
      TabLayout(t);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintTab(t, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      t->hFont = (HFONT)wParam;
      TabLayout(t);
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)t->hFont;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      t->bFocus = nMsg == WM_SETFOCUS;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      int i = TabHitTest(t, pt);
      if (i < 0) return 0;
      if (!(StyleOf(hWnd) & TCS_FOCUSNEVER)) ::SetFocus(hWnd);
      TabSelectByUser(t, i);
      return 0;
    }
    case WM_KEYDOWN:
      if (wParam == VK_LEFT && t->iSel > 0) TabSelectByUser(t, t->iSel - 1);
      if (wParam == VK_RIGHT && t->iSel + 1 < t->items.GetSize()) TabSelectByUser(t, t->iSel + 1);
      return 0;
    case TCM_GETITEMCOUNT:
      return t->items.GetSize();
    case TCM_INSERTITEMA: {
      const TCITEMA FAR* pItem = (const TCITEMA FAR*)lParam;
      if (pItem == NULL) return -1;
      int i = (int)wParam;
      if (i < 0 || i > t->items.GetSize()) i = (int)t->items.GetSize();
      W16TabItem item;
      item.iImage = -1;
      item.lParam = 0;
      item.dwState = 0;
      TabSetItem(item, pItem);
      t->items.InsertAt(i, new W16TabItem(item));
      if (t->iSel < 0)
        t->iSel = t->iFocus = 0;
      else if (i <= t->iSel)
        t->iSel++, t->iFocus = t->iSel;
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return i;
    }
    case TCM_SETITEMA:
    case TCM_GETITEMA: {
      int i = (int)wParam;
      if (i < 0 || i >= t->items.GetSize() || lParam == 0) return FALSE;
      if (nMsg == TCM_SETITEMA) {
        TabSetItem(TabItem(t, i), (const TCITEMA FAR*)lParam);
        TabLayout(t);
        ::InvalidateRect(hWnd, NULL, TRUE);
        return TRUE;
      }
      TCITEMA FAR* pItem = (TCITEMA FAR*)lParam;
      const W16TabItem& item = TabItem(t, i);
      if ((pItem->mask & TCIF_TEXT) && pItem->pszText != NULL && pItem->cchTextMax > 0)
        lstrcpyn(pItem->pszText, item.strText, pItem->cchTextMax);
      if (pItem->mask & TCIF_IMAGE) pItem->iImage = item.iImage;
      if (pItem->mask & TCIF_PARAM) pItem->lParam = item.lParam;
      if (pItem->mask & TCIF_STATE) pItem->dwState = item.dwState & pItem->dwStateMask;
      return TRUE;
    }
    case TCM_DELETEITEM: {
      int i = (int)wParam;
      if (i < 0 || i >= t->items.GetSize()) return FALSE;
      delete &TabItem(t, i);
      t->items.RemoveAt(i);
      if (t->iSel >= t->items.GetSize() || i == t->iSel)
        t->iSel = t->items.GetSize() > 0 ? (int)MinL(t->iSel, t->items.GetSize() - 1) : -1;
      else if (i < t->iSel)
        t->iSel--;
      t->iFocus = t->iSel;
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case TCM_DELETEALLITEMS:
      TabDeleteAll(t);
      t->iSel = t->iFocus = -1;
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    case TCM_GETITEMRECT: {
      int i = (int)wParam;
      if (i < 0 || i >= t->items.GetSize() || lParam == 0) return FALSE;
      *(RECT FAR*)lParam = t->rects[i];
      return TRUE;
    }
    case TCM_GETCURSEL:
      return t->iSel;
    case TCM_SETCURSEL: {
      int nOld = t->iSel;
      int i = (int)wParam;
      if (i < 0 || i >= t->items.GetSize()) return -1;
      t->iSel = t->iFocus = i;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return nOld;
    }
    case TCM_GETCURFOCUS:
      return t->iFocus;
    case TCM_SETCURFOCUS:
      if ((int)wParam >= 0 && (int)wParam < t->items.GetSize()) TabSelectByUser(t, (int)wParam);
      return 0;
    case TCM_HITTEST: {
      TCHITTESTINFO FAR* pInfo = (TCHITTESTINFO FAR*)lParam;
      if (pInfo == NULL) return -1;
      int i = TabHitTest(t, pInfo->pt);
      pInfo->flags = i >= 0 ? TCHT_ONITEMLABEL : TCHT_NOWHERE;
      return i;
    }
    case TCM_ADJUSTRECT:
      TabAdjustRect(t, (BOOL)wParam, (RECT FAR*)lParam);
      return 0;
    case TCM_SETITEMSIZE: {
      LRESULT lOld = MAKELONG(t->sizeFixed.cx, t->sizeFixed.cy);
      t->sizeFixed.cx = (short)LOWORD(lParam);
      t->sizeFixed.cy = (short)HIWORD(lParam);
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return lOld;
    }
    case TCM_SETPADDING:
      t->sizePadding.cx = (short)LOWORD(lParam);
      t->sizePadding.cy = (short)HIWORD(lParam);
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case TCM_SETMINTABWIDTH: {
      int nOld = t->nMinWidth;
      t->nMinWidth = (int)lParam;
      TabLayout(t);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return nOld;
    }
    case TCM_GETROWCOUNT:
      return 1;
    case TCM_SETITEMEXTRA:
      return TRUE;
    case TCM_GETIMAGELIST:
    case TCM_SETIMAGELIST:
    case TCM_GETTOOLTIPS:
    case TCM_SETTOOLTIPS:
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the tab control class.
LRESULT CALLBACK W16TabProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Tab* t = StateOf<W16Tab>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Tab>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (t == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, t);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return TabMessage(t, hWnd, nMsg, wParam, lParam);
}

// =====================================================================
}  // namespace

/// Registers the control classes of the AFX_WNDCOMMCTL_ flags in fReg
/// that the library has on 16-bit Windows: the progress bar, the
/// trackbar (AFX_WNDCOMMCTL_BAR_REG), the up-down control and the tab
/// control. Gives the flags of the classes that are there.
LONG AFXAPI _AfxW16RegisterCtlClasses(LONG fReg) {
  LONG fDone = 0;
  if ((fReg & AFX_WNDCOMMCTL_PROGRESS_REG) &&
      RegisterCtlClasses(PROGRESS_CLASS, "msctls_progress32", W16ProgressProc))
    fDone |= AFX_WNDCOMMCTL_PROGRESS_REG;
  if ((fReg & AFX_WNDCOMMCTL_BAR_REG) &&
      RegisterCtlClasses(TRACKBAR_CLASS, "msctls_trackbar32", W16TrackProc))
    fDone |= AFX_WNDCOMMCTL_BAR_REG;
  if ((fReg & AFX_WNDCOMMCTL_UPDOWN_REG) &&
      RegisterCtlClasses(UPDOWN_CLASS, "msctls_updown32", W16SpinProc))
    fDone |= AFX_WNDCOMMCTL_UPDOWN_REG;
  if ((fReg & AFX_WNDCOMMCTL_TAB_REG) &&
      RegisterCtlClasses(WC_TABCONTROL, "SysTabControl32", W16TabProc))
    fDone |= AFX_WNDCOMMCTL_TAB_REG;
  return fDone;
}

#endif  // _AFX_WIN16
