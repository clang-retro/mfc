// win16u.cpp - the Win32 USER, GDI and KERNEL functions of
// include/afxwin16u.h, for 16-bit Windows. Each one does its work with
// the 16-bit function that is nearest to it, or fails as Win32 fails
// when the system does not support the call. The file is empty in the
// other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <olenls.h>

// ---------------------------------------------------------------------
// GDI objects

/// Gives the type of a GDI object (OBJ_PEN, OBJ_BRUSH and so on), or 0.
/// The 16-bit IsGDIObject gives the type in its own numbers.
DWORD GetObjectType(HGDIOBJ h) {
  static const BYTE s_types[] = {0,        OBJ_PEN,    OBJ_BRUSH,  OBJ_FONT,
                                 OBJ_PAL,  OBJ_BITMAP, OBJ_REGION, OBJ_DC,
                                 OBJ_DC,   OBJ_METADC, OBJ_METAFILE};
  UINT nType = (UINT)::IsGDIObject(h);
  return nType < sizeof(s_types) ? s_types[nType] : 0;
}

/// Gives the object of a type that is selected into a device context.
/// 16-bit Windows has no call for it: the function selects a stock
/// object, takes the old object that SelectObject gives, and selects the
/// old object again.
HGDIOBJ GetCurrentObject(HDC hdc, UINT type) {
  HGDIOBJ hOld = NULL;
  switch (type) {
    case OBJ_PEN:
      hOld = ::SelectObject(hdc, ::GetStockObject(BLACK_PEN));
      break;
    case OBJ_BRUSH:
      hOld = ::SelectObject(hdc, ::GetStockObject(WHITE_BRUSH));
      break;
    case OBJ_FONT:
      hOld = ::SelectObject(hdc, ::GetStockObject(SYSTEM_FONT));
      break;
    case OBJ_PAL: {
      HPALETTE hPal = ::SelectPalette(hdc, (HPALETTE)::GetStockObject(DEFAULT_PALETTE), FALSE);
      if (hPal != NULL) ::SelectPalette(hdc, hPal, FALSE);
      return (HGDIOBJ)hPal;
    }
    case OBJ_BITMAP: {
      // Only a memory device context has a bitmap.
      HBITMAP hTemp = ::CreateBitmap(1, 1, 1, 1, NULL);
      if (hTemp == NULL) return NULL;
      hOld = ::SelectObject(hdc, hTemp);
      if (hOld != NULL) ::SelectObject(hdc, hOld);
      ::DeleteObject(hTemp);
      return hOld;
    }
    default:
      return NULL;
  }
  if (hOld != NULL) ::SelectObject(hdc, hOld);
  return hOld;
}

/// Makes a pen. 16-bit Windows has only cosmetic pens of one color: a
/// pen with a solid brush becomes such a pen, and other pens fail.
HPEN ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH FAR *plbrush, DWORD cStyle,
                  const DWORD FAR *pstyle) {
  (void)cStyle;
  (void)pstyle;
  if (plbrush == NULL || plbrush->lbStyle != BS_SOLID) return NULL;
  return ::CreatePen((int)(iPenStyle & 0x0F), (int)cWidth, plbrush->lbColor);
}

/// Makes a pattern brush from a packed device-independent bitmap. The
/// 16-bit call needs the bitmap in a global memory block; the function
/// copies it there.
HBRUSH CreateDIBPatternBrushPt(LPCVOID lpPackedDIB, UINT iUsage) {
  const BITMAPINFOHEADER FAR *pbih = (const BITMAPINFOHEADER FAR *)lpPackedDIB;
  if (pbih == NULL) return NULL;
  DWORD nColors = pbih->biClrUsed;
  if (nColors == 0 && pbih->biBitCount <= 8) nColors = 1UL << pbih->biBitCount;
  DWORD cbColor = iUsage == DIB_PAL_COLORS ? sizeof(WORD) : sizeof(RGBQUAD);
  DWORD cbRow = ((DWORD)pbih->biWidth * pbih->biBitCount + 31) / 32 * 4;
  LONG nHeight = pbih->biHeight < 0 ? -pbih->biHeight : pbih->biHeight;
  DWORD cbTotal = pbih->biSize + nColors * cbColor + cbRow * (DWORD)nHeight;
  HGLOBAL hDIB = ::GlobalAlloc(GMEM_MOVEABLE, cbTotal);
  if (hDIB == NULL) return NULL;
  BYTE FAR *pDst = (BYTE FAR *)::GlobalLock(hDIB);
  const BYTE FAR *pSrc = (const BYTE FAR *)lpPackedDIB;
  for (DWORD i = 0; i < cbTotal; i++) pDst[i] = pSrc[i];
  ::GlobalUnlock(hDIB);
  HBRUSH hBrush = ::CreateDIBPatternBrush(hDIB, iUsage);
  // GDI keeps a copy of the bitmap.
  ::GlobalFree(hDIB);
  return hBrush;
}

/// Gives a solid brush of a system color. The brushes are made on the
/// first call and stay until the program ends, as system brushes do.
HBRUSH GetSysColorBrush(int nIndex) {
  static HBRUSH s_brushes[COLOR_BTNHIGHLIGHT + 1];
  if (nIndex < 0 || nIndex > COLOR_BTNHIGHLIGHT) return NULL;
  if (s_brushes[nIndex] == NULL) s_brushes[nIndex] = ::CreateSolidBrush(::GetSysColor(nIndex));
  return s_brushes[nIndex];
}

/// Fails: 16-bit Windows does not give the rectangles of a region.
DWORD GetRegionData(HRGN hrgn, DWORD nCount, LPRGNDATA lpRgnData) {
  (void)hrgn;
  (void)nCount;
  (void)lpRgnData;
  return 0;
}

/// Fails: 16-bit Windows cannot make a region from rectangles.
HRGN ExtCreateRegion(const XFORM FAR *lpx, DWORD nCount, const RGNDATA FAR *lpData) {
  (void)lpx;
  (void)nCount;
  (void)lpData;
  return NULL;
}

/// Sets the clipping region. 16-bit Windows can only replace it
/// (RGN_COPY); the other modes fail with ERROR.
int ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode) {
  if (mode != RGN_COPY) return ERROR;
  return ::SelectClipRgn(hdc, hrgn);
}

/// Sets the brush origin and gives the old one, with SetBrushOrg.
BOOL SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt) {
  DWORD dwOld = ::SetBrushOrg(hdc, x, y);
  if (lppt != NULL) {
    lppt->x = (short)LOWORD(dwOld);
    lppt->y = (short)HIWORD(dwOld);
  }
  return TRUE;
}

/// Fails: 16-bit Windows has no DC brush.
COLORREF GetDCBrushColor(HDC hdc) {
  (void)hdc;
  return CLR_INVALID;
}

/// Fails: 16-bit Windows has no DC brush.
COLORREF SetDCBrushColor(HDC hdc, COLORREF color) {
  (void)hdc;
  (void)color;
  return CLR_INVALID;
}

/// Fails: 16-bit Windows has no DC pen.
COLORREF GetDCPenColor(HDC hdc) {
  (void)hdc;
  return CLR_INVALID;
}

/// Fails: 16-bit Windows has no DC pen.
COLORREF SetDCPenColor(HDC hdc, COLORREF color) {
  (void)hdc;
  (void)color;
  return CLR_INVALID;
}

/// Sets a pixel, with SetPixel, and tells whether it worked.
BOOL SetPixelV(HDC hdc, int x, int y, COLORREF color) {
  return ::SetPixel(hdc, x, y, color) != (COLORREF)-1;
}

/// Gives the size of a string, with GetTextExtentPoint.
BOOL GetTextExtentPoint32(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl) {
  return ::GetTextExtentPoint(hdc, lpString, c, psizl);
}

/// Gives the size of a string, how many characters fit in nMaxExtent
/// (when lpnFit is not NULL), and the extent of each prefix (when alpDx
/// is not NULL). 16-bit Windows measures one prefix at a time.
BOOL GetTextExtentExPoint(HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent,
                          LPINT lpnFit, LPINT alpDx, LPSIZE lpSize) {
  if (!::GetTextExtentPoint(hdc, lpszString, cchString, lpSize)) return FALSE;
  if (lpnFit == NULL && alpDx == NULL) return TRUE;
  int nFit = 0;
  for (int i = 1; i <= cchString; i++) {
    SIZE size;
    ::GetTextExtentPoint(hdc, lpszString, i, &size);
    if (lpnFit != NULL && size.cx > nMaxExtent) break;
    if (alpDx != NULL) alpDx[i - 1] = size.cx;
    nFit = i;
  }
  if (lpnFit != NULL) *lpnFit = nFit;
  return TRUE;
}

/// Gives 0: the fonts of 16-bit Windows have no language information.
DWORD GetFontLanguageInfo(HDC hdc) {
  (void)hdc;
  return 0;
}

/// Gives GM_COMPATIBLE, the only graphics mode of 16-bit Windows.
int GetGraphicsMode(HDC hdc) {
  (void)hdc;
  return GM_COMPATIBLE;
}

/// Sets the graphics mode. Only GM_COMPATIBLE is possible; for another
/// mode, the function fails and gives 0.
int SetGraphicsMode(HDC hdc, int iMode) {
  (void)hdc;
  return iMode == GM_COMPATIBLE ? GM_COMPATIBLE : 0;
}

/// Fails: 16-bit Windows has no world transform.
BOOL SetWorldTransform(HDC hdc, const XFORM FAR *lpxf) {
  (void)hdc;
  (void)lpxf;
  return FALSE;
}

/// Fails: 16-bit Windows has no world transform.
BOOL ModifyWorldTransform(HDC hdc, const XFORM FAR *lpxf, DWORD mode) {
  (void)hdc;
  (void)lpxf;
  (void)mode;
  return FALSE;
}

/// Fails: 16-bit Windows has no world transform.
BOOL GetWorldTransform(HDC hdc, LPXFORM lpxf) {
  (void)hdc;
  (void)lpxf;
  return FALSE;
}

/// Fails: 16-bit Windows has no color adjustment.
BOOL GetColorAdjustment(HDC hdc, LPCOLORADJUSTMENT lpca) {
  (void)hdc;
  (void)lpca;
  return FALSE;
}

/// Fails: 16-bit Windows has no color adjustment.
BOOL SetColorAdjustment(HDC hdc, const COLORADJUSTMENT FAR *lpca) {
  (void)hdc;
  (void)lpca;
  return FALSE;
}

/// Gives AD_COUNTERCLOCKWISE, the only arc direction of 16-bit Windows.
int GetArcDirection(HDC hdc) {
  (void)hdc;
  return AD_COUNTERCLOCKWISE;
}

/// Sets the arc direction. Only AD_COUNTERCLOCKWISE is possible; for the
/// other direction, the function fails and gives 0.
int SetArcDirection(HDC hdc, int dir) {
  (void)hdc;
  return dir == AD_COUNTERCLOCKWISE ? AD_COUNTERCLOCKWISE : 0;
}

/// Fails: 16-bit Windows has no miter limit.
BOOL GetMiterLimit(HDC hdc, PFLOAT plimit) {
  (void)hdc;
  (void)plimit;
  return FALSE;
}

/// Fails: 16-bit Windows has no miter limit.
BOOL SetMiterLimit(HDC hdc, FLOAT limit, PFLOAT old) {
  (void)hdc;
  (void)limit;
  (void)old;
  return FALSE;
}

/// Fails: 16-bit Windows has no AngleArc.
BOOL AngleArc(HDC hdc, int x, int y, DWORD r, FLOAT StartAngle, FLOAT SweepAngle) {
  (void)hdc;
  (void)x;
  (void)y;
  (void)r;
  (void)StartAngle;
  (void)SweepAngle;
  return FALSE;
}

/// Fails: 16-bit Windows has no ArcTo.
BOOL ArcTo(HDC hdc, int left, int top, int right, int bottom, int xr1, int yr1, int xr2,
           int yr2) {
  (void)hdc;
  (void)left;
  (void)top;
  (void)right;
  (void)bottom;
  (void)xr1;
  (void)yr1;
  (void)xr2;
  (void)yr2;
  return FALSE;
}

/// Fails: 16-bit Windows has no Bezier curves.
BOOL PolyBezier(HDC hdc, const POINT FAR *apt, DWORD cpt) {
  (void)hdc;
  (void)apt;
  (void)cpt;
  return FALSE;
}

/// Fails: 16-bit Windows has no Bezier curves.
BOOL PolyBezierTo(HDC hdc, const POINT FAR *apt, DWORD cpt) {
  (void)hdc;
  (void)apt;
  (void)cpt;
  return FALSE;
}

/// Fails: 16-bit Windows has no PolyDraw.
BOOL PolyDraw(HDC hdc, const POINT FAR *apt, const BYTE FAR *aj, int cpt) {
  (void)hdc;
  (void)apt;
  (void)aj;
  (void)cpt;
  return FALSE;
}

/// Draws a series of polylines, with one Polyline call for each.
BOOL PolyPolyline(HDC hdc, const POINT FAR *apt, const DWORD FAR *asz, DWORD csz) {
  for (DWORD i = 0; i < csz; i++) {
    if (!::Polyline(hdc, apt, (int)asz[i])) return FALSE;
    apt += asz[i];
  }
  return TRUE;
}

/// Draws lines from the current position through the points, with
/// LineTo. The last point becomes the current position.
BOOL PolylineTo(HDC hdc, const POINT FAR *apt, DWORD cpt) {
  for (DWORD i = 0; i < cpt; i++)
    if (!::LineTo(hdc, apt[i].x, apt[i].y)) return FALSE;
  return TRUE;
}

/// Fails: 16-bit Windows has no MaskBlt.
BOOL MaskBlt(HDC hdcDest, int xDest, int yDest, int width, int height, HDC hdcSrc, int xSrc,
             int ySrc, HBITMAP hbmMask, int xMask, int yMask, DWORD rop) {
  (void)hdcDest;
  (void)xDest;
  (void)yDest;
  (void)width;
  (void)height;
  (void)hdcSrc;
  (void)xSrc;
  (void)ySrc;
  (void)hbmMask;
  (void)xMask;
  (void)yMask;
  (void)rop;
  return FALSE;
}

/// Fails: 16-bit Windows has no PlgBlt.
BOOL PlgBlt(HDC hdcDest, const POINT FAR *lpPoint, HDC hdcSrc, int xSrc, int ySrc, int width,
            int height, HBITMAP hbmMask, int xMask, int yMask) {
  (void)hdcDest;
  (void)lpPoint;
  (void)hdcSrc;
  (void)xSrc;
  (void)ySrc;
  (void)width;
  (void)height;
  (void)hbmMask;
  (void)xMask;
  (void)yMask;
  return FALSE;
}

// 16-bit Windows has no paths. Each path function fails.

/// Fails: 16-bit Windows has no paths.
BOOL AbortPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL BeginPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL EndPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL CloseFigure(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL FillPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL FlattenPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL StrokePath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL StrokeAndFillPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL WidenPath(HDC hdc) {
  (void)hdc;
  return FALSE;
}

/// Fails: 16-bit Windows has no paths.
BOOL SelectClipPath(HDC hdc, int mode) {
  (void)hdc;
  (void)mode;
  return FALSE;
}

/// Fails with -1: 16-bit Windows has no paths.
int GetPath(HDC hdc, LPPOINT apt, LPBYTE aj, int cpt) {
  (void)hdc;
  (void)apt;
  (void)aj;
  (void)cpt;
  return -1;
}

/// Fails: 16-bit Windows has no paths.
HRGN PathToRegion(HDC hdc) {
  (void)hdc;
  return NULL;
}

/// Fails: 16-bit Windows has no enhanced metafiles.
HDC CreateEnhMetaFile(HDC hdc, LPCSTR lpFilename, const RECT FAR *lprc, LPCSTR lpDesc) {
  (void)hdc;
  (void)lpFilename;
  (void)lprc;
  (void)lpDesc;
  return NULL;
}

/// Fails: 16-bit Windows has no enhanced metafiles.
HENHMETAFILE CloseEnhMetaFile(HDC hdc) {
  (void)hdc;
  return NULL;
}

/// Fails: 16-bit Windows has no enhanced metafiles.
BOOL PlayEnhMetaFile(HDC hdc, HENHMETAFILE hmf, const RECT FAR *lprect) {
  (void)hdc;
  (void)hmf;
  (void)lprect;
  return FALSE;
}

/// Fails: 16-bit Windows has no halftone palette. CPalette then has no
/// palette, and the program uses the default one.
HPALETTE CreateHalftonePalette(HDC hdc) {
  (void)hdc;
  return NULL;
}

// ---------------------------------------------------------------------
// Drawing

/// Draws a line from (x1, y1) to (x2, y2), without the last point, in a
/// system color.
static void _AfxLine(HDC hdc, int x1, int y1, int x2, int y2, int nColor) {
  HPEN hPen = ::CreatePen(PS_SOLID, 1, ::GetSysColor(nColor));
  HGDIOBJ hOld = ::SelectObject(hdc, hPen);
  ::MoveTo(hdc, x1, y1);
  ::LineTo(hdc, x2, y2);
  ::SelectObject(hdc, hOld);
  ::DeleteObject(hPen);
}

/// Draws one line of a 3-D edge: the top and left sides in one color,
/// the bottom and right sides in another, then makes the rectangle 1
/// pixel smaller on each side that it drew.
static void _AfxEdgeLine(HDC hdc, LPRECT r, UINT grfFlags, int nTopLeft, int nBottomRight) {
  if (grfFlags & BF_TOP) _AfxLine(hdc, r->left, r->top, r->right, r->top, nTopLeft);
  if (grfFlags & BF_LEFT) _AfxLine(hdc, r->left, r->top, r->left, r->bottom, nTopLeft);
  if (grfFlags & BF_BOTTOM)
    _AfxLine(hdc, r->left, r->bottom - 1, r->right, r->bottom - 1, nBottomRight);
  if (grfFlags & BF_RIGHT)
    _AfxLine(hdc, r->right - 1, r->top, r->right - 1, r->bottom, nBottomRight);
  if (grfFlags & BF_LEFT) r->left++;
  if (grfFlags & BF_TOP) r->top++;
  if (grfFlags & BF_RIGHT) r->right--;
  if (grfFlags & BF_BOTTOM) r->bottom--;
}

/// Draws a 3-D edge with the button colors: an outer line and an inner
/// line, each raised or sunken. With BF_ADJUST, *qrc becomes the
/// rectangle inside the edge.
BOOL DrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags) {
  RECT r = *qrc;
  if (edge & BDR_RAISEDOUTER)
    _AfxEdgeLine(hdc, &r, grfFlags, COLOR_BTNHIGHLIGHT, COLOR_WINDOWFRAME);
  else if (edge & BDR_SUNKENOUTER)
    _AfxEdgeLine(hdc, &r, grfFlags, COLOR_BTNSHADOW, COLOR_BTNHIGHLIGHT);
  if (edge & BDR_RAISEDINNER)
    _AfxEdgeLine(hdc, &r, grfFlags, COLOR_BTNFACE, COLOR_BTNSHADOW);
  else if (edge & BDR_SUNKENINNER)
    _AfxEdgeLine(hdc, &r, grfFlags, COLOR_WINDOWFRAME, COLOR_BTNFACE);
  if (grfFlags & BF_ADJUST) *qrc = r;
  return TRUE;
}

/// Draws a check box (DFC_BUTTON with DFCS_BUTTONCHECK or
/// DFCS_BUTTON3STATE): a frame, and a cross when DFCS_CHECKED is set.
/// Other controls fail.
BOOL DrawFrameControl(HDC hdc, LPRECT lprc, UINT uType, UINT uState) {
  if (uType != DFC_BUTTON || (uState & (DFCS_BUTTONRADIO | DFCS_BUTTONPUSH)) != 0) return FALSE;
  RECT r = *lprc;
  HBRUSH hFace = ::CreateSolidBrush(
      ::GetSysColor((uState & DFCS_INACTIVE) ? COLOR_BTNFACE : COLOR_WINDOW));
  ::FillRect(hdc, &r, hFace);
  ::DeleteObject(hFace);
  HBRUSH hFrame = ::CreateSolidBrush(::GetSysColor(COLOR_WINDOWFRAME));
  ::FrameRect(hdc, &r, hFrame);
  ::DeleteObject(hFrame);
  if (uState & DFCS_CHECKED) {
    int nColor = (uState & DFCS_INACTIVE) ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT;
    _AfxLine(hdc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2, nColor);
    _AfxLine(hdc, r.right - 3, r.top + 2, r.left + 1, r.bottom - 2, nColor);
  }
  return TRUE;
}

/// Draws a text, an icon or a bitmap, gray when DSS_DISABLED is set
/// (with GrayString for a text). A complex image (DST_COMPLEX) calls the
/// program's function.
BOOL DrawState(HDC hdc, HBRUSH hbrFore, DRAWSTATEPROC qfnCallBack, LPARAM lData, WPARAM wData,
               int x, int y, int cx, int cy, UINT uFlags) {
  switch (uFlags & 0x000F) {
    case DST_COMPLEX:
      return qfnCallBack != NULL && qfnCallBack(hdc, lData, wData, cx, cy);
    case DST_TEXT:
    case DST_PREFIXTEXT: {
      LPCSTR lpsz = (LPCSTR)lData;
      int nLen = wData != 0 ? (int)wData : lstrlen(lpsz);
      if (uFlags & DSS_DISABLED)
        return ::GrayString(hdc, hbrFore, NULL, lData, nLen, x, y, cx, cy);
      return ::TextOut(hdc, x, y, lpsz, nLen);
    }
    case DST_ICON:
      return ::DrawIcon(hdc, x, y, (HICON)LOWORD(lData));
    case DST_BITMAP: {
      HBITMAP hbm = (HBITMAP)LOWORD(lData);
      BITMAP bm;
      if (::GetObject(hbm, sizeof(bm), &bm) == 0) return FALSE;
      HDC hdcMem = ::CreateCompatibleDC(hdc);
      if (hdcMem == NULL) return FALSE;
      HGDIOBJ hOld = ::SelectObject(hdcMem, hbm);
      BOOL bOK = ::BitBlt(hdc, x, y, cx != 0 ? cx : bm.bmWidth, cy != 0 ? cy : bm.bmHeight,
                          hdcMem, 0, 0, SRCCOPY);
      ::SelectObject(hdcMem, hOld);
      ::DeleteDC(hdcMem);
      return bOK;
    }
    default:
      return FALSE;
  }
}

/// Draws a text with DrawText. The margins and tab length of lpdtp have
/// no effect; uiLengthDrawn becomes the length of the text.
int DrawTextEx(HDC hdc, LPSTR lpchText, int cchText, LPRECT lprc, UINT format,
               LPDRAWTEXTPARAMS lpdtp) {
  int nHeight = ::DrawText(hdc, lpchText, cchText, lprc, format);
  if (lpdtp != NULL) lpdtp->uiLengthDrawn = cchText >= 0 ? (UINT)cchText : lstrlen(lpchText);
  return nHeight;
}

/// Does nothing and succeeds: 16-bit Windows has no animated rectangles.
BOOL DrawAnimatedRects(HWND hwnd, int idAni, const RECT FAR *lprcFrom, const RECT FAR *lprcTo) {
  (void)hwnd;
  (void)idAni;
  (void)lprcFrom;
  (void)lprcTo;
  return TRUE;
}

/// Fails: 16-bit Windows does not draw captions for a program.
BOOL DrawCaption(HWND hwnd, HDC hdc, const RECT FAR *lprect, UINT flags) {
  (void)hwnd;
  (void)hdc;
  (void)lprect;
  (void)flags;
  return FALSE;
}

// ---------------------------------------------------------------------
// Windows

/// Gives the child window at a point, with ChildWindowFromPoint. The
/// flags have no effect.
HWND ChildWindowFromPointEx(HWND hwnd, POINT pt, UINT flags) {
  (void)flags;
  return ::ChildWindowFromPoint(hwnd, pt);
}

/// Tells whether a window has a class name and a text. NULL matches
/// all names.
static BOOL _AfxWindowMatches(HWND hWnd, LPCSTR lpszClass, LPCSTR lpszWindow) {
  char sz[128];
  if (lpszClass != NULL) {
    ::GetClassName(hWnd, sz, sizeof(sz));
    if (lstrcmpi(sz, lpszClass) != 0) return FALSE;
  }
  if (lpszWindow != NULL) {
    ::GetWindowText(hWnd, sz, sizeof(sz));
    if (lstrcmp(sz, lpszWindow) != 0) return FALSE;
  }
  return TRUE;
}

/// Finds a child of a window, after the child hWndChildAfter, by class
/// name and text. Without a parent, it finds a top-level window.
HWND FindWindowEx(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow) {
  if (hWndParent == NULL && hWndChildAfter == NULL) return ::FindWindow(lpszClass, lpszWindow);
  HWND hWnd;
  if (hWndChildAfter != NULL)
    hWnd = ::GetWindow(hWndChildAfter, GW_HWNDNEXT);
  else
    hWnd = ::GetWindow(hWndParent != NULL ? hWndParent : ::GetDesktopWindow(), GW_CHILD);
  for (; hWnd != NULL; hWnd = ::GetWindow(hWnd, GW_HWNDNEXT))
    if (_AfxWindowMatches(hWnd, lpszClass, lpszWindow)) return hWnd;
  return NULL;
}

/// Gives the active window: 16-bit Windows has no other foreground.
HWND GetForegroundWindow() { return ::GetActiveWindow(); }

/// Makes a window the active window.
BOOL SetForegroundWindow(HWND hWnd) {
  ::SetActiveWindow(hWnd);
  return TRUE;
}

// The help context of a window is in two window properties, one for
// each word.
static const char s_szHelpIdLow[] = "AfxHelpIdLo";
static const char s_szHelpIdHigh[] = "AfxHelpIdHi";

/// Gives the help context of a window, or 0.
DWORD GetWindowContextHelpId(HWND hwnd) {
  return MAKELONG((UINT)::GetProp(hwnd, s_szHelpIdLow), (UINT)::GetProp(hwnd, s_szHelpIdHigh));
}

/// Sets the help context of a window. 0 removes it.
BOOL SetWindowContextHelpId(HWND hwnd, DWORD dwContextHelpId) {
  if (dwContextHelpId == 0) {
    ::RemoveProp(hwnd, s_szHelpIdLow);
    ::RemoveProp(hwnd, s_szHelpIdHigh);
    return TRUE;
  }
  return ::SetProp(hwnd, s_szHelpIdLow, (HANDLE)LOWORD(dwContextHelpId)) &&
         ::SetProp(hwnd, s_szHelpIdHigh, (HANDLE)HIWORD(dwContextHelpId));
}

/// Gives 0: a 16-bit menu has no help context.
DWORD GetMenuContextHelpId(HMENU hmenu) {
  (void)hmenu;
  return 0;
}

/// Fails: a 16-bit menu has no help context.
BOOL SetMenuContextHelpId(HMENU hmenu, DWORD dwContextHelpId) {
  (void)hmenu;
  (void)dwContextHelpId;
  return FALSE;
}

/// Gives ERROR: 16-bit Windows has no window regions.
int GetWindowRgn(HWND hWnd, HRGN hRgn) {
  (void)hWnd;
  (void)hRgn;
  return ERROR;
}

/// Fails: 16-bit Windows has no window regions. The function deletes
/// the region, as Win32 does when the call works.
int SetWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw) {
  (void)hWnd;
  (void)bRedraw;
  if (hRgn != NULL) ::DeleteObject(hRgn);
  return 0;
}

/// Sends a message and waits for the result, as SendMessage does. In
/// one task, there is no thread that the call does not wait for.
BOOL SendNotifyMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  ::SendMessage(hWnd, Msg, wParam, lParam);
  return TRUE;
}

// ---------------------------------------------------------------------
// Scroll bars

/// Gives the range and position of a scroll bar. A 16-bit scroll bar has
/// no page size: nPage is 0, and nTrackPos is the position.
BOOL GetScrollInfo(HWND hwnd, int nBar, LPSCROLLINFO lpsi) {
  if (lpsi->fMask & SIF_RANGE) ::GetScrollRange(hwnd, nBar, &lpsi->nMin, &lpsi->nMax);
  if (lpsi->fMask & SIF_PAGE) lpsi->nPage = 0;
  if (lpsi->fMask & (SIF_POS | SIF_TRACKPOS)) {
    int nPos = ::GetScrollPos(hwnd, nBar);
    if (lpsi->fMask & SIF_POS) lpsi->nPos = nPos;
    if (lpsi->fMask & SIF_TRACKPOS) lpsi->nTrackPos = nPos;
  }
  return TRUE;
}

/// Sets the range and position of a scroll bar and gives the new
/// position. The page size has no effect.
int SetScrollInfo(HWND hwnd, int nBar, LPCSCROLLINFO lpsi, BOOL redraw) {
  if (lpsi->fMask & SIF_RANGE)
    ::SetScrollRange(hwnd, nBar, lpsi->nMin, lpsi->nMax, redraw && !(lpsi->fMask & SIF_POS));
  if (lpsi->fMask & SIF_POS) ::SetScrollPos(hwnd, nBar, lpsi->nPos, redraw);
  return ::GetScrollPos(hwnd, nBar);
}

// ---------------------------------------------------------------------
// Menus

/// Puts a radio check on one item of a range and takes the check off the
/// others. 16-bit Windows shows the radio check as a normal check mark.
BOOL CheckMenuRadioItem(HMENU hmenu, UINT first, UINT last, UINT check, UINT flags) {
  UINT nBy = flags & MF_BYPOSITION;
  for (UINT i = first; i <= last; i++)
    ::CheckMenuItem(hmenu, i, nBy | (i == check ? MF_CHECKED : MF_UNCHECKED));
  return TRUE;
}

/// Gives (UINT)-1: a 16-bit menu has no default item.
UINT GetMenuDefaultItem(HMENU hMenu, UINT fByPos, UINT gmdiFlags) {
  (void)hMenu;
  (void)fByPos;
  (void)gmdiFlags;
  return (UINT)-1;
}

/// Fails: a 16-bit menu has no default item.
BOOL SetMenuDefaultItem(HMENU hMenu, UINT uItem, UINT fByPos) {
  (void)hMenu;
  (void)uItem;
  (void)fByPos;
  return FALSE;
}

/// Gives the parts of a menu item that 16-bit Windows has: the state,
/// the ID, the pop-up menu, the type and the text.
BOOL GetMenuItemInfo(HMENU hmenu, UINT item, BOOL fByPosition, LPMENUITEMINFO lpmii) {
  UINT nBy = fByPosition ? MF_BYPOSITION : MF_BYCOMMAND;
  UINT nState = ::GetMenuState(hmenu, item, nBy);
  if (nState == (UINT)-1) return FALSE;
  if (lpmii->fMask & MIIM_STATE) lpmii->fState = nState & (MF_GRAYED | MF_DISABLED | MF_CHECKED);
  if (lpmii->fMask & (MIIM_TYPE | MIIM_FTYPE))
    lpmii->fType = nState & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW | MF_MENUBARBREAK |
                             MF_MENUBREAK);
  if (lpmii->fMask & MIIM_SUBMENU)
    lpmii->hSubMenu = fByPosition ? ::GetSubMenu(hmenu, (int)item) : NULL;
  if (lpmii->fMask & MIIM_ID)
    lpmii->wID = fByPosition ? ::GetMenuItemID(hmenu, (int)item) : item;
  if (lpmii->fMask & (MIIM_TYPE | MIIM_STRING)) {
    if (lpmii->dwTypeData != NULL && lpmii->cch > 0)
      lpmii->cch = (UINT)::GetMenuString(hmenu, item, lpmii->dwTypeData, (int)lpmii->cch, nBy);
    else {
      char sz[256];
      lpmii->cch = (UINT)::GetMenuString(hmenu, item, sz, sizeof(sz), nBy);
    }
  }
  if (lpmii->fMask & MIIM_DATA) lpmii->dwItemData = 0;
  return TRUE;
}

/// Gives the ModifyMenu and InsertMenu flags of a MENUITEMINFO.
static UINT _AfxMenuFlags(LPCMENUITEMINFO lpmii) {
  UINT nFlags = 0;
  if (lpmii->fMask & MIIM_STATE) nFlags |= lpmii->fState & (MF_GRAYED | MF_DISABLED | MF_CHECKED);
  if (lpmii->fMask & (MIIM_TYPE | MIIM_FTYPE))
    nFlags |= lpmii->fType & (MF_SEPARATOR | MF_OWNERDRAW | MF_MENUBARBREAK | MF_MENUBREAK);
  if ((lpmii->fMask & MIIM_SUBMENU) && lpmii->hSubMenu != NULL) nFlags |= MF_POPUP;
  return nFlags;
}

/// Changes a menu item: its state with EnableMenuItem and CheckMenuItem,
/// and its text, ID or pop-up menu with ModifyMenu.
BOOL SetMenuItemInfo(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmii) {
  UINT nBy = fByPosition ? MF_BYPOSITION : MF_BYCOMMAND;
  if (::GetMenuState(hmenu, item, nBy) == (UINT)-1) return FALSE;
  if (lpmii->fMask & (MIIM_STRING | MIIM_TYPE | MIIM_ID | MIIM_SUBMENU)) {
    char sz[256];
    LPCSTR lpszText = lpmii->dwTypeData;
    if (!(lpmii->fMask & (MIIM_STRING | MIIM_TYPE))) {
      ::GetMenuString(hmenu, item, sz, sizeof(sz), nBy);
      lpszText = sz;
    }
    UINT nNewID;
    if ((lpmii->fMask & MIIM_SUBMENU) && lpmii->hSubMenu != NULL)
      nNewID = (UINT)lpmii->hSubMenu;
    else if (lpmii->fMask & MIIM_ID)
      nNewID = lpmii->wID;
    else
      nNewID = fByPosition ? ::GetMenuItemID(hmenu, (int)item) : item;
    if (!::ModifyMenu(hmenu, item, nBy | MF_STRING | _AfxMenuFlags(lpmii), nNewID, lpszText))
      return FALSE;
  } else if (lpmii->fMask & MIIM_STATE) {
    ::EnableMenuItem(hmenu, item, nBy | (lpmii->fState & (MF_GRAYED | MF_DISABLED)));
    ::CheckMenuItem(hmenu, item, nBy | (lpmii->fState & MF_CHECKED));
  }
  return TRUE;
}

/// Puts a new item into a menu, with InsertMenu.
BOOL InsertMenuItem(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmii) {
  UINT nBy = fByPosition ? MF_BYPOSITION : MF_BYCOMMAND;
  UINT nFlags = _AfxMenuFlags(lpmii);
  UINT nNewID = (nFlags & MF_POPUP) ? (UINT)lpmii->hSubMenu : lpmii->wID;
  if (nFlags & MF_SEPARATOR) return ::InsertMenu(hmenu, item, nBy | nFlags, 0, NULL);
  return ::InsertMenu(hmenu, item, nBy | MF_STRING | nFlags, nNewID,
                      lpmii->dwTypeData != NULL ? lpmii->dwTypeData : "");
}

/// Shows a pop-up menu, with TrackPopupMenu. The area that the menu
/// must not cover (lptpm) has no effect.
BOOL TrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm) {
  (void)lptpm;
  return ::TrackPopupMenu(hMenu, uFlags, x, y, 0, hWnd, NULL);
}

/// Does nothing and succeeds. A 16-bit accelerator table is a resource,
/// which Windows frees when the program ends.
BOOL DestroyAcceleratorTable(HACCEL hAccel) {
  (void)hAccel;
  return TRUE;
}

/// Loads a bitmap, an icon or a cursor from the resources, with
/// LoadBitmap, LoadIcon or LoadCursor. The size and the flags have no
/// effect, and a file (LR_LOADFROMFILE) fails: the colors of a bitmap
/// are not mapped to the system colors.
HANDLE LoadImage(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad) {
  (void)cx;
  (void)cy;
  if (fuLoad & LR_LOADFROMFILE) return NULL;
  switch (type) {
    case IMAGE_BITMAP:
      return (HANDLE)::LoadBitmap(hInst, name);
    case IMAGE_ICON:
      return (HANDLE)::LoadIcon(hInst, name);
    case IMAGE_CURSOR:
      return (HANDLE)::LoadCursor(hInst, name);
    default:
      return NULL;
  }
}

/// Copies an icon or a cursor, with CopyIcon or CopyCursor. The size
/// and the flags have no effect; other images fail.
HANDLE CopyImage(HANDLE h, UINT type, int cx, int cy, UINT flags) {
  (void)cx;
  (void)cy;
  (void)flags;
  switch (type) {
    case IMAGE_ICON:
      return (HANDLE)::CopyIcon(AfxGetInstanceHandle(), (HICON)h);
    case IMAGE_CURSOR:
      return (HANDLE)::CopyCursor(AfxGetInstanceHandle(), (HCURSOR)h);
    default:
      return NULL;
  }
}

/// Copies a UTF-16 string, at most iMaxLength - 1 characters and a 0.
LPWSTR lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength) {
  if (iMaxLength <= 0) return lpString1;
  int i = 0;
  for (; i < iMaxLength - 1 && lpString2[i] != 0; i++) lpString1[i] = lpString2[i];
  lpString1[i] = 0;
  return lpString1;
}

/// Loads a bitmap from the resources. 16-bit Windows has no color map:
/// the colors of the bitmap are not changed.
extern "C" HBITMAP WINAPI CreateMappedBitmap(HINSTANCE hInstance, INT_PTR idBitmap, UINT wFlags,
                                             LPCOLORMAP lpColorMap, int iNumMaps) {
  (void)wFlags;
  (void)lpColorMap;
  (void)iNumMaps;
  return ::LoadBitmap(hInstance, MAKEINTRESOURCE(idBitmap));
}

// ---------------------------------------------------------------------
// The registry

/// Tells whether a key is in a tree that 16-bit Windows does not have.
static BOOL _AfxOtherTree(HKEY hKey) { return (hKey & 0x80000000UL) != 0; }

/// Tells whether a value name is the name of the value of the key
/// itself: NULL or "".
static BOOL _AfxDefaultValue(LPCSTR lpValueName) {
  return lpValueName == NULL || *lpValueName == '\0';
}

/// Opens a key, with RegOpenKey. The options and access have no effect.
LONG RegOpenKeyEx(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired,
                  PHKEY phkResult) {
  (void)ulOptions;
  (void)samDesired;
  if (_AfxOtherTree(hKey)) return ERROR_BADKEY;
  return ::RegOpenKey(hKey, lpSubKey, phkResult);
}

/// Opens a key, or makes it if it is not there, with RegOpenKey and
/// RegCreateKey. *lpdwDisposition tells which.
LONG RegCreateKeyEx(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
                    REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                    PHKEY phkResult, LPDWORD lpdwDisposition) {
  (void)Reserved;
  (void)lpClass;
  (void)dwOptions;
  (void)samDesired;
  (void)lpSecurityAttributes;
  if (_AfxOtherTree(hKey)) return ERROR_BADKEY;
  if (::RegOpenKey(hKey, lpSubKey, phkResult) == ERROR_SUCCESS) {
    if (lpdwDisposition != NULL) *lpdwDisposition = REG_OPENED_EXISTING_KEY;
    return ERROR_SUCCESS;
  }
  LONG lResult = ::RegCreateKey(hKey, lpSubKey, phkResult);
  if (lResult == ERROR_SUCCESS && lpdwDisposition != NULL)
    *lpdwDisposition = REG_CREATED_NEW_KEY;
  return lResult;
}

/// Gives the value of a key, a string (REG_SZ), with RegQueryValue.
/// Without lpData, the function gives only the size.
LONG RegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
                     LPBYTE lpData, LPDWORD lpcbData) {
  (void)lpReserved;
  if (_AfxOtherTree(hKey) || !_AfxDefaultValue(lpValueName)) return ERROR_BADKEY;
  char sz[256];
  LONG cb = sizeof(sz);
  LONG lResult = ::RegQueryValue(hKey, NULL, sz, &cb);
  if (lResult != ERROR_SUCCESS) return lResult;
  if (lpType != NULL) *lpType = REG_SZ;
  DWORD cbNeeded = (DWORD)lstrlen(sz) + 1;
  if (lpData != NULL) {
    if (lpcbData == NULL || *lpcbData < cbNeeded) {
      if (lpcbData != NULL) *lpcbData = cbNeeded;
      return ERROR_OUTOFMEMORY;
    }
    lstrcpy((LPSTR)lpData, sz);
  }
  if (lpcbData != NULL) *lpcbData = cbNeeded;
  return ERROR_SUCCESS;
}

/// Sets the value of a key, with RegSetValue. Only a string (REG_SZ)
/// is possible.
LONG RegSetValueEx(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
                   const BYTE FAR *lpData, DWORD cbData) {
  (void)Reserved;
  if (_AfxOtherTree(hKey) || !_AfxDefaultValue(lpValueName)) return ERROR_BADKEY;
  if (dwType != REG_SZ) return ERROR_INVALID_PARAMETER;
  return ::RegSetValue(hKey, NULL, REG_SZ, (LPCSTR)lpData, cbData);
}

/// Clears the value of a key: 16-bit Windows cannot remove it, so the
/// value becomes an empty string.
LONG RegDeleteValue(HKEY hKey, LPCSTR lpValueName) {
  if (_AfxOtherTree(hKey) || !_AfxDefaultValue(lpValueName)) return ERROR_BADKEY;
  return ::RegSetValue(hKey, NULL, REG_SZ, "", 0);
}

// ---------------------------------------------------------------------
// The task

/// Gives the command line as Win32 does: the path of the program, in
/// quotation marks, then the command tail from the program segment
/// prefix of the task.
LPSTR GetCommandLine() {
  static char s_szCmdLine[260 + 3 + 128];
  if (s_szCmdLine[0] != '\0') return s_szCmdLine;
  char szPath[260];
  ::GetModuleFileName(AfxGetInstanceHandle(), szPath, sizeof(szPath));
  wsprintf(s_szCmdLine, "\"%s\"", (LPSTR)szPath);
  // The command tail: a length byte at offset 0x80, then the text.
  const BYTE FAR *pPSP = (const BYTE FAR *)MAKELP(::GetCurrentPDB(), 0);
  UINT nLen = pPSP[0x80];
  if (nLen > 126) nLen = 126;
  LPSTR p = s_szCmdLine + lstrlen(s_szCmdLine);
  for (UINT i = 0; i < nLen; i++) *p++ = (char)pPSP[0x81 + i];
  *p = '\0';
  return s_szCmdLine;
}

/// Gives the ID of the thread: the handle of the task.
DWORD GetCurrentThreadId() { return (DWORD)(UINT)::GetCurrentTask(); }

/// Gives the ID of the thread of a window: the handle of its task. A
/// task has no process ID apart from its task handle.
DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
  DWORD dwTask = (DWORD)(UINT)::GetWindowTask(hWnd);
  if (lpdwProcessId != NULL) *lpdwProcessId = dwTask;
  return dwTask;
}

/// Fails with ERROR_NOT_SUPPORTED: a 16-bit task has one thread.
HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, UINT dwStackSize,
                    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                    DWORD dwCreationFlags, LPDWORD lpThreadId) {
  (void)lpThreadAttributes;
  (void)dwStackSize;
  (void)lpStartAddress;
  (void)lpParameter;
  (void)dwCreationFlags;
  (void)lpThreadId;
  SetLastError(ERROR_NOT_SUPPORTED);
  return NULL;
}

/// Ends the thread. The task has only one, so the task ends.
void ExitThread(DWORD dwExitCode) { ExitProcess((UINT)dwExitCode); }

/// Gives STILL_ACTIVE: the only thread is the one that calls.
BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode) {
  (void)hThread;
  *lpExitCode = STILL_ACTIVE;
  return TRUE;
}

/// Fails with (DWORD)-1: no thread can wait to start.
DWORD ResumeThread(HANDLE hThread) {
  (void)hThread;
  SetLastError(ERROR_NOT_SUPPORTED);
  return (DWORD)-1;
}

/// Fails with (DWORD)-1: the only thread cannot stop itself.
DWORD SuspendThread(HANDLE hThread) {
  (void)hThread;
  SetLastError(ERROR_NOT_SUPPORTED);
  return (DWORD)-1;
}

/// Gives THREAD_PRIORITY_NORMAL: 16-bit tasks have no priorities.
int GetThreadPriority(HANDLE hThread) {
  (void)hThread;
  return THREAD_PRIORITY_NORMAL;
}

/// Does nothing and succeeds: 16-bit tasks have no priorities.
BOOL SetThreadPriority(HANDLE hThread, int nPriority) {
  (void)hThread;
  (void)nPriority;
  return TRUE;
}

/// Puts a message into the queue of a task, with PostAppMessage. The
/// thread ID is the task handle.
BOOL PostThreadMessage(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam) {
  return ::PostAppMessage((HTASK)(UINT)idThread, Msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// Events, mutexes and semaphores

namespace {
// The kind of a synchronization object.
enum { kFree, kEvent, kMutex, kSemaphore };

// One synchronization object. For an event, nCount is 1 when it is
// signaled; for a mutex, it is the number of waits that own it; for a
// semaphore, it is the count.
struct SyncSlot {
  BYTE nKind;
  BYTE bManualReset;
  LONG nCount;
  LONG nMaximum;
};

const int kSyncSlots = 32;
// The handles of the objects start here, far above the file handles.
const UINT kSyncBase = 0x7F00;
SyncSlot g_syncSlots[kSyncSlots];
}  // namespace

/// Gives the slot of a handle of a kind (or of all kinds when nKind is
/// kFree), or NULL.
static SyncSlot* _AfxSyncSlot(HANDLE h, int nKind) {
  UINT n = (UINT)h - kSyncBase;
  if ((UINT)h < kSyncBase || n >= (UINT)kSyncSlots) return NULL;
  SyncSlot* p = &g_syncSlots[n];
  if (p->nKind == kFree || (nKind != kFree && p->nKind != nKind)) return NULL;
  return p;
}

/// Makes a synchronization object and gives its handle, or NULL when
/// all the slots are used.
static HANDLE _AfxNewSync(int nKind, BOOL bManualReset, LONG nCount, LONG nMaximum) {
  for (int i = 0; i < kSyncSlots; i++) {
    if (g_syncSlots[i].nKind == kFree) {
      g_syncSlots[i].nKind = (BYTE)nKind;
      g_syncSlots[i].bManualReset = (BYTE)(bManualReset != FALSE);
      g_syncSlots[i].nCount = nCount;
      g_syncSlots[i].nMaximum = nMaximum;
      return (HANDLE)(kSyncBase + i);
    }
  }
  SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  return NULL;
}

/// Releases a synchronization object, if the handle is one. Tells
/// whether it was.
BOOL _AfxW16CloseSyncHandle(HANDLE h) {
  SyncSlot* p = _AfxSyncSlot(h, kFree);
  if (p == NULL) return FALSE;
  p->nKind = kFree;
  return TRUE;
}

/// Makes an event. A name has no effect: the event is of this task.
HANDLE CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                   LPCSTR lpName) {
  (void)lpEventAttributes;
  (void)lpName;
  return _AfxNewSync(kEvent, bManualReset, bInitialState ? 1 : 0, 1);
}

/// Signals an event.
BOOL SetEvent(HANDLE hEvent) {
  SyncSlot* p = _AfxSyncSlot(hEvent, kEvent);
  if (p == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  p->nCount = 1;
  return TRUE;
}

/// Makes an event not signaled.
BOOL ResetEvent(HANDLE hEvent) {
  SyncSlot* p = _AfxSyncSlot(hEvent, kEvent);
  if (p == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  p->nCount = 0;
  return TRUE;
}

/// Signals an event and makes it not signaled again. No thread waits,
/// so only the reset has an effect.
BOOL PulseEvent(HANDLE hEvent) { return ResetEvent(hEvent); }

/// Makes a mutex. The only thread can always own it.
HANDLE CreateMutex(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
  (void)lpMutexAttributes;
  (void)lpName;
  return _AfxNewSync(kMutex, FALSE, bInitialOwner ? 1 : 0, 0x7FFFFFFFL);
}

/// Releases one ownership of a mutex. It fails if the mutex has no owner.
BOOL ReleaseMutex(HANDLE hMutex) {
  SyncSlot* p = _AfxSyncSlot(hMutex, kMutex);
  if (p == NULL || p->nCount == 0) {
    SetLastError(p == NULL ? ERROR_INVALID_HANDLE : ERROR_NOT_OWNER);
    return FALSE;
  }
  p->nCount--;
  return TRUE;
}

/// Makes a semaphore with a count and a maximum count.
HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount,
                       LONG lMaximumCount, LPCSTR lpName) {
  (void)lpSemaphoreAttributes;
  (void)lpName;
  if (lInitialCount < 0 || lMaximumCount <= 0 || lInitialCount > lMaximumCount) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return _AfxNewSync(kSemaphore, FALSE, lInitialCount, lMaximumCount);
}

/// Adds to the count of a semaphore. It fails if the count would go
/// above the maximum.
BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount) {
  SyncSlot* p = _AfxSyncSlot(hSemaphore, kSemaphore);
  if (p == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (lReleaseCount <= 0 || p->nCount + lReleaseCount > p->nMaximum) {
    SetLastError(ERROR_TOO_MANY_POSTS);
    return FALSE;
  }
  if (lpPreviousCount != NULL) *lpPreviousCount = p->nCount;
  p->nCount += lReleaseCount;
  return TRUE;
}

/// Tells whether a wait for an object can end now.
static BOOL _AfxSyncReady(const SyncSlot* p) {
  return p->nKind == kMutex || p->nCount > 0;
}

/// Ends a wait for an object: an auto-reset event becomes not signaled,
/// a mutex gets one more ownership, and the count of a semaphore goes
/// down by 1.
static void _AfxSyncTake(SyncSlot* p) {
  if (p->nKind == kMutex)
    p->nCount++;
  else if (p->nKind == kSemaphore || !p->bManualReset)
    p->nCount--;
}

/// Waits for objects: for one of them, or for all with bWaitAll. When the
/// wait cannot end now, no other thread can make it end: after the time
/// the function gives WAIT_TIMEOUT. An INFINITE wait fails. When
/// dwWakeMask is not 0, a message of those kinds in the queue also ends
/// the wait, and the result is WAIT_OBJECT_0 + nCount.
static DWORD _AfxWait(DWORD nCount, const HANDLE FAR *lpHandles, BOOL bWaitAll,
                      DWORD dwMilliseconds, DWORD dwWakeMask) {
  SyncSlot* slots[kSyncSlots];
  if (nCount > (DWORD)kSyncSlots) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  for (DWORD i = 0; i < nCount; i++) {
    slots[i] = _AfxSyncSlot(lpHandles[i], kFree);
    if (slots[i] == NULL) {
      SetLastError(ERROR_INVALID_HANDLE);
      return WAIT_FAILED;
    }
  }
  DWORD dwStart = ::GetTickCount();
  for (;;) {
    if (bWaitAll) {
      DWORD nReady = 0;
      for (DWORD i = 0; i < nCount; i++)
        if (_AfxSyncReady(slots[i])) nReady++;
      if (nCount > 0 && nReady == nCount) {
        for (DWORD i = 0; i < nCount; i++) _AfxSyncTake(slots[i]);
        return WAIT_OBJECT_0;
      }
    } else {
      for (DWORD i = 0; i < nCount; i++) {
        if (_AfxSyncReady(slots[i])) {
          _AfxSyncTake(slots[i]);
          return WAIT_OBJECT_0 + i;
        }
      }
    }
    if (dwWakeMask != 0 && (HIWORD(::GetQueueStatus((UINT)dwWakeMask)) != 0))
      return WAIT_OBJECT_0 + nCount;
    if (dwMilliseconds != INFINITE && ::GetTickCount() - dwStart >= dwMilliseconds)
      return WAIT_TIMEOUT;
    if (dwWakeMask == 0) {
      if (dwMilliseconds == INFINITE) {
        SetLastError(ERROR_POSSIBLE_DEADLOCK);
        return WAIT_FAILED;
      }
      Sleep(dwMilliseconds - (::GetTickCount() - dwStart));
      return WAIT_TIMEOUT;
    }
    // Let the other tasks run until a message comes.
    if (dwMilliseconds == INFINITE)
      ::WaitMessage();
    else
      ::Yield();
  }
}

/// Waits for one object (see _AfxWait).
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
  return _AfxWait(1, &hHandle, FALSE, dwMilliseconds, 0);
}

/// Waits for one object or all objects (see _AfxWait).
DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE FAR *lpHandles, BOOL bWaitAll,
                             DWORD dwMilliseconds) {
  return _AfxWait(nCount, lpHandles, bWaitAll, dwMilliseconds, 0);
}

/// Waits for objects or for a message (see _AfxWait).
DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE FAR *pHandles, BOOL fWaitAll,
                                DWORD dwMilliseconds, DWORD dwWakeMask) {
  return _AfxWait(nCount, pHandles, fWaitAll, dwMilliseconds, dwWakeMask);
}

// ---------------------------------------------------------------------
// Windows and locales

/// Gives an ancestor of a window. GA_PARENT gives the parent of a child
/// window (not the owner). GA_ROOT goes up the parents to the window that
/// is not a child. GA_ROOTOWNER then goes up the owners. Gives NULL for
/// NULL or for a flag that is not known.
HWND GetAncestor(HWND hWnd, UINT gaFlags) {
  if (hWnd == NULL) return NULL;
  switch (gaFlags) {
    case GA_PARENT:
      return (::GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) ? ::GetParent(hWnd) : NULL;
    case GA_ROOT:
    case GA_ROOTOWNER: {
      while (::GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) {
        HWND hParent = ::GetParent(hWnd);
        if (hParent == NULL) break;
        hWnd = hParent;
      }
      if (gaFlags == GA_ROOT) return hWnd;
      // The 16-bit GetParent of a window that is not a child is its owner.
      for (HWND hOwner = ::GetParent(hWnd); hOwner != NULL; hOwner = ::GetParent(hWnd))
        hWnd = hOwner;
      return hWnd;
    }
  }
  return NULL;
}

/// Gives the default locale of the user, from OLE 2's national language
/// support. 16-bit Windows has no thread locale.
LCID GetThreadLocale() { return ::GetUserDefaultLCID(); }

#endif  // _AFX_WIN16
