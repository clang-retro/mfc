// dcprev.cpp - CPreviewDC, the device context print preview draws with.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxpriv.h>

IMPLEMENT_DYNAMIC(CPreviewDC, CDC)

CPreviewDC::CPreviewDC() : m_nScaleNum(1), m_nScaleDen(1), m_sizeTopLeft(0, 0), m_nSaveDCIndex(0) {
  m_bPrinting = TRUE;
}

CPreviewDC::~CPreviewDC() {
  // Neither DC belongs to this object.
  m_hDC = NULL;
  m_hAttribDC = NULL;
}

void CPreviewDC::SetAttribDC(HDC hDC) {
  m_hAttribDC = hDC;
  MirrorMappingMode(TRUE);
}

void CPreviewDC::SetOutputDC(HDC hDC) {
  m_hDC = hDC;
  if (hDC == NULL) return;
  m_nSaveDCIndex = ::SaveDC(hDC);
  MirrorAttributes();
  MirrorMappingMode(TRUE);
}

void CPreviewDC::ReleaseOutputDC() {
  if (m_hDC != NULL && m_nSaveDCIndex != 0) ::RestoreDC(m_hDC, m_nSaveDCIndex);
  m_nSaveDCIndex = 0;
  m_hDC = NULL;
}

void CPreviewDC::SetScaleRatio(int nNumerator, int nDenominator) {
  m_nScaleNum = nNumerator;
  m_nScaleDen = nDenominator > 0 ? nDenominator : 1;
  MirrorMappingMode(TRUE);
}

void CPreviewDC::SetTopLeftOffset(CSize sizeTop) {
  m_sizeTopLeft = sizeTop;
  MirrorMappingMode(TRUE);
}

void CPreviewDC::PrinterDPtoScreenDP(LPPOINT lpPoint) const {
  lpPoint->x = ::MulDiv(lpPoint->x, m_nScaleNum, m_nScaleDen) + m_sizeTopLeft.cx;
  lpPoint->y = ::MulDiv(lpPoint->y, m_nScaleNum, m_nScaleDen) + m_sizeTopLeft.cy;
}

void CPreviewDC::ClipToPage() {
  if (m_hDC == NULL || m_hAttribDC == NULL) return;
  CPoint ptBottomRight(::GetDeviceCaps(m_hAttribDC, HORZRES), ::GetDeviceCaps(m_hAttribDC, VERTRES));
  PrinterDPtoScreenDP(&ptBottomRight);
  // Regions are in device units, whatever the mapping.
  HRGN hRgn = ::CreateRectRgn(m_sizeTopLeft.cx, m_sizeTopLeft.cy, ptBottomRight.x, ptBottomRight.y);
  ::ExtSelectClipRgn(m_hDC, hRgn, RGN_AND);
  ::DeleteObject(hRgn);
}

// Copies what the attribute DC has selected and set to the output DC.
void CPreviewDC::MirrorAttributes() {
  if (m_hDC == NULL || m_hAttribDC == NULL) return;
  static const UINT types[] = {OBJ_FONT, OBJ_PEN, OBJ_BRUSH};
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    HGDIOBJ hObj = ::GetCurrentObject(m_hAttribDC, types[i]);
    if (hObj != NULL) ::SelectObject(m_hDC, hObj);
  }
  ::SetTextColor(m_hDC, ::GetTextColor(m_hAttribDC));
  ::SetBkColor(m_hDC, ::GetBkColor(m_hAttribDC));
  ::SetBkMode(m_hDC, ::GetBkMode(m_hAttribDC));
  ::SetTextAlign(m_hDC, ::GetTextAlign(m_hAttribDC));
  ::SetROP2(m_hDC, ::GetROP2(m_hAttribDC));
  ::SetPolyFillMode(m_hDC, ::GetPolyFillMode(m_hAttribDC));
  ::SetStretchBltMode(m_hDC, ::GetStretchBltMode(m_hAttribDC));
}

static LONGLONG _AfxGcd(LONGLONG a, LONGLONG b) {
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  while (b != 0) {
    LONGLONG t = a % b;
    a = b;
    b = t;
  }
  return a == 0 ? 1 : a;
}

// Reduces a window/viewport extent pair to the same ratio in numbers that
// fit the 16-bit coordinates of Windows 9x.
static void _AfxReduceExtents(LONGLONG& w, LONGLONG& v) {
  LONGLONG g = _AfxGcd(w, v);
  w /= g;
  v /= g;
  while (w > 32767 || w < -32767 || v > 32767 || v < -32767) {
    w /= 2;
    v /= 2;
  }
  if (w == 0) w = w < 0 ? -1 : 1;
  if (v == 0) v = 1;
}

// The output DC maps logical units like the printer's DC, then scales the
// printer's pixels to the screen and moves them to the page:
//   screen = (logical - windowOrg) * viewportExt / windowExt * num / den
//            + viewportOrg * num / den + topLeft
void CPreviewDC::MirrorMappingMode(BOOL bCompute) {
  (void)bCompute;
  if (m_hDC == NULL || m_hAttribDC == NULL) return;
  POINT ptWOrg, ptVOrg;
  SIZE sizeWExt, sizeVExt;
  ::GetWindowOrgEx(m_hAttribDC, &ptWOrg);
  ::GetViewportOrgEx(m_hAttribDC, &ptVOrg);
  ::GetWindowExtEx(m_hAttribDC, &sizeWExt);
  ::GetViewportExtEx(m_hAttribDC, &sizeVExt);

  LONGLONG wx = (LONGLONG)sizeWExt.cx * m_nScaleDen, vx = (LONGLONG)sizeVExt.cx * m_nScaleNum;
  LONGLONG wy = (LONGLONG)sizeWExt.cy * m_nScaleDen, vy = (LONGLONG)sizeVExt.cy * m_nScaleNum;
  _AfxReduceExtents(wx, vx);
  _AfxReduceExtents(wy, vy);

  ::SetMapMode(m_hDC, MM_ANISOTROPIC);
  ::SetWindowOrgEx(m_hDC, ptWOrg.x, ptWOrg.y, NULL);
  ::SetWindowExtEx(m_hDC, (int)wx, (int)wy, NULL);
  ::SetViewportExtEx(m_hDC, (int)vx, (int)vy, NULL);
  ::SetViewportOrgEx(m_hDC, ::MulDiv(ptVOrg.x, m_nScaleNum, m_nScaleDen) + m_sizeTopLeft.cx,
                     ::MulDiv(ptVOrg.y, m_nScaleNum, m_nScaleDen) + m_sizeTopLeft.cy, NULL);
}

int CPreviewDC::SaveDC() {
  int nRetVal = ::SaveDC(m_hAttribDC);
  if (m_hDC != NULL) ::SaveDC(m_hDC);
  return nRetVal;
}

BOOL CPreviewDC::RestoreDC(int nSavedDC) {
  if (m_hDC != NULL) {
    // Pop the output DC by as many levels as the attribute DC.
    int nCur = ::SaveDC(m_hAttribDC);
    ::RestoreDC(m_hAttribDC, -1);
    int nLevels = nSavedDC < 0 ? -nSavedDC : nCur - nSavedDC;
    if (nLevels > 0) ::RestoreDC(m_hDC, -nLevels);
  }
  BOOL bRetVal = ::RestoreDC(m_hAttribDC, nSavedDC);
  MirrorMappingMode(TRUE);
  return bRetVal;
}

CFont* CPreviewDC::SelectObject(CFont* pFont) { return CDC::SelectObject(pFont); }

int CPreviewDC::SetMapMode(int nMapMode) {
  int nRetVal = ::SetMapMode(m_hAttribDC, nMapMode);
  MirrorMappingMode(TRUE);
  return nRetVal;
}

CPoint CPreviewDC::SetViewportOrg(int x, int y) {
  POINT pt = {0, 0};
  ::SetViewportOrgEx(m_hAttribDC, x, y, &pt);
  MirrorMappingMode(TRUE);
  return pt;
}

CPoint CPreviewDC::OffsetViewportOrg(int nWidth, int nHeight) {
  POINT pt = {0, 0};
  ::OffsetViewportOrgEx(m_hAttribDC, nWidth, nHeight, &pt);
  MirrorMappingMode(TRUE);
  return pt;
}

CSize CPreviewDC::SetViewportExt(int cx, int cy) {
  SIZE size = {0, 0};
  ::SetViewportExtEx(m_hAttribDC, cx, cy, &size);
  MirrorMappingMode(TRUE);
  return size;
}

CSize CPreviewDC::ScaleViewportExt(int xNum, int xDenom, int yNum, int yDenom) {
  SIZE size = {0, 0};
  ::ScaleViewportExtEx(m_hAttribDC, xNum, xDenom, yNum, yDenom, &size);
  MirrorMappingMode(TRUE);
  return size;
}

CSize CPreviewDC::SetWindowExt(int cx, int cy) {
  SIZE size = {0, 0};
  ::SetWindowExtEx(m_hAttribDC, cx, cy, &size);
  MirrorMappingMode(TRUE);
  return size;
}

CSize CPreviewDC::ScaleWindowExt(int xNum, int xDenom, int yNum, int yDenom) {
  SIZE size = {0, 0};
  ::ScaleWindowExtEx(m_hAttribDC, xNum, xDenom, yNum, yDenom, &size);
  MirrorMappingMode(TRUE);
  return size;
}

BOOL CPreviewDC::TextOut(int x, int y, LPCTSTR lpszString, int nCount) {
  return ExtTextOut(x, y, 0, NULL, lpszString, (UINT)nCount, NULL);
}

BOOL CPreviewDC::ExtTextOut(int x, int y, UINT nOptions, LPCRECT lpRect, LPCTSTR lpszString,
                            UINT nCount, LPINT lpDxWidths) {
  if (m_hDC == NULL) return FALSE;
  if (lpDxWidths != NULL || nCount == 0 || m_hAttribDC == NULL)
    return ::ExtTextOut(m_hDC, x, y, nOptions, lpRect, lpszString, nCount, lpDxWidths);
  // Each character's advance, as the printer measures it.
  int* pnFit = new int[nCount];
  SIZE size;
  BOOL bOK = ::GetTextExtentExPoint(m_hAttribDC, lpszString, (int)nCount, 0, NULL, pnFit, &size);
  if (bOK) {
    for (UINT i = nCount - 1; i > 0; i--) pnFit[i] -= pnFit[i - 1];
    bOK = ::ExtTextOut(m_hDC, x, y, nOptions, lpRect, lpszString, nCount, pnFit);
  } else {
    bOK = ::ExtTextOut(m_hDC, x, y, nOptions, lpRect, lpszString, nCount, NULL);
  }
  delete[] pnFit;
  return bOK;
}

CSize CPreviewDC::TabbedTextOut(int x, int y, LPCTSTR lpszString, int nCount, int nTabPositions,
                                LPINT lpnTabStopPositions, int nTabOrigin) {
  if (m_hAttribDC == NULL || nCount <= 0) return CSize(0, 0);
  // Place each run between tabs where the printer would.
  int nStart = 0;
  for (int i = 0; i <= nCount; i++) {
    if (i == nCount || lpszString[i] == '\t') {
      int xRun = x;
      if (nStart > 0) {
        DWORD dwExt = ::GetTabbedTextExtent(m_hAttribDC, lpszString, nStart, nTabPositions,
                                            lpnTabStopPositions);
        xRun = nTabOrigin + LOWORD(dwExt) + (x - nTabOrigin);
      }
      if (i > nStart) ExtTextOut(xRun, y, 0, NULL, lpszString + nStart, (UINT)(i - nStart), NULL);
      nStart = i + 1;
    }
  }
  DWORD dwExt =
      ::GetTabbedTextExtent(m_hAttribDC, lpszString, nCount, nTabPositions, lpnTabStopPositions);
  return CSize(LOWORD(dwExt), HIWORD(dwExt));
}
