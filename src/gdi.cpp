// gdi.cpp - CGdiObject and friends, CDC and its window DCs, CMenu.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

IMPLEMENT_DYNCREATE(CGdiObject, CObject)
IMPLEMENT_DYNAMIC(CPen, CGdiObject)
IMPLEMENT_DYNAMIC(CBrush, CGdiObject)
IMPLEMENT_DYNAMIC(CFont, CGdiObject)
IMPLEMENT_DYNAMIC(CBitmap, CGdiObject)
IMPLEMENT_DYNAMIC(CPalette, CGdiObject)
IMPLEMENT_DYNAMIC(CRgn, CGdiObject)
IMPLEMENT_DYNCREATE(CDC, CObject)
IMPLEMENT_DYNAMIC(CClientDC, CDC)
IMPLEMENT_DYNAMIC(CWindowDC, CDC)
IMPLEMENT_DYNAMIC(CPaintDC, CDC)
IMPLEMENT_DYNAMIC(CMetaFileDC, CDC)
IMPLEMENT_DYNCREATE(CMenu, CObject)

// ---------------------------------------------------------------------
// CGdiObject

CGdiObject* PASCAL CGdiObject::FromHandle(HGDIOBJ hObject) {
  return (CGdiObject*)afxMapHGDIOBJ(TRUE)->FromHandle(hObject);
}

void PASCAL CGdiObject::DeleteTempMap() {
  CHandleMap* pMap = afxMapHGDIOBJ();
  if (pMap != NULL) pMap->DeleteTemp();
}

BOOL CGdiObject::Attach(HGDIOBJ hObject) {
  if (hObject == NULL) return FALSE;
  m_hObject = hObject;
  afxMapHGDIOBJ(TRUE)->SetPermanent(m_hObject, this);
  return TRUE;
}

HGDIOBJ CGdiObject::Detach() {
  HGDIOBJ hObject = m_hObject;
  if (hObject != NULL) {
    CHandleMap* pMap = afxMapHGDIOBJ();
    if (pMap != NULL && pMap->LookupPermanent(hObject) == this) pMap->RemoveHandle(hObject);
  }
  m_hObject = NULL;
  return hObject;
}

CGdiObject::~CGdiObject() { DeleteObject(); }

BOOL CGdiObject::DeleteObject() {
  if (m_hObject == NULL) return FALSE;
  return ::DeleteObject(Detach());
}

CBrush::CBrush(CBitmap* pBitmap) {
  if (!Attach(::CreatePatternBrush((HBITMAP)pBitmap->GetSafeHandle())))
    AfxThrowResourceException();
}

BOOL CBrush::CreatePatternBrush(CBitmap* pBitmap) {
  return Attach(::CreatePatternBrush((HBITMAP)pBitmap->GetSafeHandle()));
}

BOOL CFont::CreatePointFontIndirect(const LOGFONT* lpLogFont, CDC* pDC) {
  // lfHeight is in tenths of a point; convert it to logical units.
  HDC hDC = pDC != NULL ? pDC->m_hAttribDC : ::GetDC(NULL);
  LOGFONT logFont = *lpLogFont;
  POINT pt;
  pt.y = ::MulDiv(::GetDeviceCaps(hDC, LOGPIXELSY), logFont.lfHeight, 720);
  pt.x = 0;
  ::DPtoLP(hDC, &pt, 1);
  POINT ptOrg = {0, 0};
  ::DPtoLP(hDC, &ptOrg, 1);
  logFont.lfHeight = -abs(pt.y - ptOrg.y);
  if (pDC == NULL) ::ReleaseDC(NULL, hDC);
  return CreateFontIndirect(&logFont);
}

BOOL CFont::CreatePointFont(int nPointSize, LPCTSTR lpszFaceName, CDC* pDC) {
  LOGFONT logFont;
  memset(&logFont, 0, sizeof(logFont));
  logFont.lfCharSet = DEFAULT_CHARSET;
  logFont.lfHeight = nPointSize;
  lstrcpyn(logFont.lfFaceName, lpszFaceName, LF_FACESIZE);
  return CreatePointFontIndirect(&logFont, pDC);
}

BOOL CBitmap::LoadBitmap(LPCTSTR lpszResourceName) {
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_BITMAP);
  return Attach(::LoadBitmap(hInst, lpszResourceName));
}

BOOL CBitmap::LoadMappedBitmap(UINT nIDBitmap, UINT nFlags, LPCOLORMAP lpColorMap, int nMapSize) {
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(nIDBitmap), RT_BITMAP);
  return Attach(::CreateMappedBitmap(hInst, (INT_PTR)nIDBitmap, nFlags, lpColorMap, nMapSize));
}

BOOL CBitmap::CreateCompatibleBitmap(CDC* pDC, int nWidth, int nHeight) {
  return Attach(::CreateCompatibleBitmap(pDC->m_hDC, nWidth, nHeight));
}

BOOL CBitmap::CreateDiscardableBitmap(CDC* pDC, int nWidth, int nHeight) {
  return Attach(::CreateDiscardableBitmap(pDC->m_hDC, nWidth, nHeight));
}

BOOL CPalette::CreateHalftonePalette(CDC* pDC) {
  return Attach(::CreateHalftonePalette(pDC->GetSafeHdc()));
}

BOOL CRgn::CreateFromPath(CDC* pDC) { return Attach(::PathToRegion(pDC->m_hDC)); }

// ---------------------------------------------------------------------
// CDC

CDC::CDC() : m_hDC(NULL), m_hAttribDC(NULL), m_bPrinting(FALSE) {}

CDC::~CDC() {
  if (m_hDC != NULL) ::DeleteDC(Detach());
}

CDC* PASCAL CDC::FromHandle(HDC hDC) { return (CDC*)afxMapHDC(TRUE)->FromHandle(hDC); }

void PASCAL CDC::DeleteTempMap() {
  CHandleMap* pMap = afxMapHDC();
  if (pMap != NULL) pMap->DeleteTemp();
}

BOOL CDC::Attach(HDC hDC) {
  if (hDC == NULL) return FALSE;
  m_hDC = hDC;
  afxMapHDC(TRUE)->SetPermanent(m_hDC, this);
  SetAttribDC(m_hDC);
  return TRUE;
}

HDC CDC::Detach() {
  HDC hDC = m_hDC;
  if (hDC != NULL) {
    CHandleMap* pMap = afxMapHDC();
    if (pMap != NULL && pMap->LookupPermanent(hDC) == this) pMap->RemoveHandle(hDC);
  }
  ReleaseAttribDC();
  ReleaseOutputDC();
  return hDC;
}

BOOL CDC::DeleteDC() {
  if (m_hDC == NULL) return FALSE;
  return ::DeleteDC(Detach());
}

CWnd* CDC::GetWindow() const { return CWnd::FromHandle(::WindowFromDC(m_hDC)); }

// With two DCs, both are saved; the number returned is the attribute DC's,
// and restoring by number pops the output DC by the same number of levels.
int CDC::SaveDC() {
  int nRetVal = 0;
  if (m_hAttribDC != NULL) nRetVal = ::SaveDC(m_hAttribDC);
  if (m_hDC != m_hAttribDC) {
    int nOut = ::SaveDC(m_hDC);
    if (m_hAttribDC == NULL) nRetVal = nOut;
  }
  return nRetVal;
}

BOOL CDC::RestoreDC(int nSavedDC) {
  BOOL bRetVal = TRUE;
  if (m_hDC != m_hAttribDC && m_hDC != NULL) {
    int nLevels = 1;
    if (nSavedDC > 0 && m_hAttribDC != NULL) {
      // How many levels the attribute DC is above the one asked for.
      int nCur = ::SaveDC(m_hAttribDC);
      ::RestoreDC(m_hAttribDC, -1);
      nLevels = nCur - nSavedDC;
    } else if (nSavedDC < 0) {
      nLevels = -nSavedDC;
    }
    if (m_hAttribDC == NULL)
      bRetVal = ::RestoreDC(m_hDC, nSavedDC);
    else if (nLevels > 0)
      bRetVal = ::RestoreDC(m_hDC, -nLevels);
  }
  if (m_hAttribDC != NULL) bRetVal = ::RestoreDC(m_hAttribDC, nSavedDC);
  return bRetVal;
}

// Selecting into both DCs keeps the attribute DC in step with the output DC.
HGDIOBJ CDC::SelectObject(HGDIOBJ hObject) {
  HGDIOBJ hOldObj = NULL;
  if (m_hDC != m_hAttribDC) hOldObj = ::SelectObject(m_hDC, hObject);
  if (m_hAttribDC != NULL) hOldObj = ::SelectObject(m_hAttribDC, hObject);
  return hOldObj;
}

CGdiObject* CDC::SelectStockObject(int nIndex) {
  HGDIOBJ hObject = ::GetStockObject(nIndex);
  return CGdiObject::FromHandle(SelectObject(hObject));
}

CPen* CDC::SelectObject(CPen* pPen) {
  return (CPen*)CGdiObject::FromHandle(SelectObject(pPen->GetSafeHandle()));
}

CBrush* CDC::SelectObject(CBrush* pBrush) {
  return (CBrush*)CGdiObject::FromHandle(SelectObject(pBrush->GetSafeHandle()));
}

CFont* CDC::SelectObject(CFont* pFont) {
  return (CFont*)CGdiObject::FromHandle(SelectObject(pFont->GetSafeHandle()));
}

CBitmap* CDC::SelectObject(CBitmap* pBitmap) {
  return (CBitmap*)CGdiObject::FromHandle(SelectObject(pBitmap->GetSafeHandle()));
}

int CDC::SelectObject(CRgn* pRgn) {
  int nRetVal = GDI_ERROR;
  if (m_hDC != m_hAttribDC) nRetVal = (int)(INT_PTR)::SelectObject(m_hDC, pRgn->GetSafeHandle());
  if (m_hAttribDC != NULL) nRetVal = (int)(INT_PTR)::SelectObject(m_hAttribDC, pRgn->GetSafeHandle());
  return nRetVal;
}

CGdiObject* CDC::SelectObject(CGdiObject* pObject) {
  return CGdiObject::FromHandle(SelectObject(pObject->GetSafeHandle()));
}

CPalette* CDC::SelectPalette(CPalette* pPalette, BOOL bForceBackground) {
  return (CPalette*)CGdiObject::FromHandle(
      ::SelectPalette(m_hDC, (HPALETTE)pPalette->GetSafeHandle(), bForceBackground));
}

CPen* CDC::GetCurrentPen() const {
  return (CPen*)CGdiObject::FromHandle(::GetCurrentObject(m_hDC, OBJ_PEN));
}
CBrush* CDC::GetCurrentBrush() const {
  return (CBrush*)CGdiObject::FromHandle(::GetCurrentObject(m_hDC, OBJ_BRUSH));
}
CPalette* CDC::GetCurrentPalette() const {
  return (CPalette*)CGdiObject::FromHandle(::GetCurrentObject(m_hDC, OBJ_PAL));
}
CFont* CDC::GetCurrentFont() const {
  return (CFont*)CGdiObject::FromHandle(::GetCurrentObject(m_hDC, OBJ_FONT));
}
CBitmap* CDC::GetCurrentBitmap() const {
  return (CBitmap*)CGdiObject::FromHandle(::GetCurrentObject(m_hDC, OBJ_BITMAP));
}

COLORREF CDC::SetBkColor(COLORREF crColor) {
  COLORREF crRetVal = CLR_INVALID;
  if (m_hDC != m_hAttribDC) crRetVal = ::SetBkColor(m_hDC, crColor);
  if (m_hAttribDC != NULL) crRetVal = ::SetBkColor(m_hAttribDC, crColor);
  return crRetVal;
}

COLORREF CDC::SetTextColor(COLORREF crColor) {
  COLORREF crRetVal = CLR_INVALID;
  if (m_hDC != m_hAttribDC) crRetVal = ::SetTextColor(m_hDC, crColor);
  if (m_hAttribDC != NULL) crRetVal = ::SetTextColor(m_hAttribDC, crColor);
  return crRetVal;
}

#define _AFX_BOTH_DC(call)                    \
  int nRetVal = 0;                            \
  if (m_hDC != m_hAttribDC) nRetVal = (int)::call(m_hDC, ARG); \
  if (m_hAttribDC != NULL) nRetVal = (int)::call(m_hAttribDC, ARG); \
  return nRetVal;

int CDC::SetBkMode(int nBkMode) {
#define ARG nBkMode
  _AFX_BOTH_DC(SetBkMode)
#undef ARG
}
int CDC::SetPolyFillMode(int nPolyFillMode) {
#define ARG nPolyFillMode
  _AFX_BOTH_DC(SetPolyFillMode)
#undef ARG
}
int CDC::SetROP2(int nDrawMode) {
#define ARG nDrawMode
  _AFX_BOTH_DC(SetROP2)
#undef ARG
}
int CDC::SetStretchBltMode(int nStretchMode) {
#define ARG nStretchMode
  _AFX_BOTH_DC(SetStretchBltMode)
#undef ARG
}
int CDC::SetMapMode(int nMapMode) {
#define ARG nMapMode
  _AFX_BOTH_DC(SetMapMode)
#undef ARG
}
UINT CDC::SetTextAlign(UINT nFlags) {
  UINT nRetVal = GDI_ERROR;
  if (m_hDC != m_hAttribDC) nRetVal = ::SetTextAlign(m_hDC, nFlags);
  if (m_hAttribDC != NULL) nRetVal = ::SetTextAlign(m_hAttribDC, nFlags);
  return nRetVal;
}

CPoint CDC::SetViewportOrg(int x, int y) {
  POINT point = {0, 0};
  if (m_hDC != m_hAttribDC) ::SetViewportOrgEx(m_hDC, x, y, &point);
  if (m_hAttribDC != NULL) ::SetViewportOrgEx(m_hAttribDC, x, y, &point);
  return point;
}

CPoint CDC::OffsetViewportOrg(int nWidth, int nHeight) {
  POINT point = {0, 0};
  if (m_hDC != m_hAttribDC) ::OffsetViewportOrgEx(m_hDC, nWidth, nHeight, &point);
  if (m_hAttribDC != NULL) ::OffsetViewportOrgEx(m_hAttribDC, nWidth, nHeight, &point);
  return point;
}

CSize CDC::SetViewportExt(int x, int y) {
  SIZE size = {0, 0};
  if (m_hDC != m_hAttribDC) ::SetViewportExtEx(m_hDC, x, y, &size);
  if (m_hAttribDC != NULL) ::SetViewportExtEx(m_hAttribDC, x, y, &size);
  return size;
}

CSize CDC::ScaleViewportExt(int xNum, int xDenom, int yNum, int yDenom) {
  SIZE size = {0, 0};
  if (m_hDC != m_hAttribDC) ::ScaleViewportExtEx(m_hDC, xNum, xDenom, yNum, yDenom, &size);
  if (m_hAttribDC != NULL) ::ScaleViewportExtEx(m_hAttribDC, xNum, xDenom, yNum, yDenom, &size);
  return size;
}

CPoint CDC::SetWindowOrg(int x, int y) {
  POINT point = {0, 0};
  if (m_hDC != m_hAttribDC) ::SetWindowOrgEx(m_hDC, x, y, &point);
  if (m_hAttribDC != NULL) ::SetWindowOrgEx(m_hAttribDC, x, y, &point);
  return point;
}

CPoint CDC::OffsetWindowOrg(int nWidth, int nHeight) {
  POINT point = {0, 0};
  if (m_hDC != m_hAttribDC) ::OffsetWindowOrgEx(m_hDC, nWidth, nHeight, &point);
  if (m_hAttribDC != NULL) ::OffsetWindowOrgEx(m_hAttribDC, nWidth, nHeight, &point);
  return point;
}

CSize CDC::SetWindowExt(int x, int y) {
  SIZE size = {0, 0};
  if (m_hDC != m_hAttribDC) ::SetWindowExtEx(m_hDC, x, y, &size);
  if (m_hAttribDC != NULL) ::SetWindowExtEx(m_hAttribDC, x, y, &size);
  return size;
}

CSize CDC::ScaleWindowExt(int xNum, int xDenom, int yNum, int yDenom) {
  SIZE size = {0, 0};
  if (m_hDC != m_hAttribDC) ::ScaleWindowExtEx(m_hDC, xNum, xDenom, yNum, yDenom, &size);
  if (m_hAttribDC != NULL) ::ScaleWindowExtEx(m_hAttribDC, xNum, xDenom, yNum, yDenom, &size);
  return size;
}

// Sizes are converted as the distance between two converted points.
void CDC::DPtoLP(LPSIZE lpSize) const {
  CSize sizeWinExt = GetWindowExt();
  CSize sizeVpExt = GetViewportExt();
  lpSize->cx = ::MulDiv(lpSize->cx, abs(sizeWinExt.cx), abs(sizeVpExt.cx));
  lpSize->cy = ::MulDiv(lpSize->cy, abs(sizeWinExt.cy), abs(sizeVpExt.cy));
}

void CDC::LPtoDP(LPSIZE lpSize) const {
  CSize sizeWinExt = GetWindowExt();
  CSize sizeVpExt = GetViewportExt();
  lpSize->cx = ::MulDiv(lpSize->cx, abs(sizeVpExt.cx), abs(sizeWinExt.cx));
  lpSize->cy = ::MulDiv(lpSize->cy, abs(sizeVpExt.cy), abs(sizeWinExt.cy));
}

#define HIMETRIC_INCH 2540

void CDC::DPtoHIMETRIC(LPSIZE lpSize) const {
  int nMapMode = GetMapMode();
  if (nMapMode < MM_ISOTROPIC && nMapMode != MM_TEXT) {
    // A fixed mapping mode: go through MM_HIMETRIC.
    ((CDC*)this)->SetMapMode(MM_HIMETRIC);
    DPtoLP(lpSize);
    ((CDC*)this)->SetMapMode(nMapMode);
  } else {
    int cxPerInch = GetDeviceCaps(LOGPIXELSX);
    int cyPerInch = GetDeviceCaps(LOGPIXELSY);
    lpSize->cx = ::MulDiv(lpSize->cx, HIMETRIC_INCH, cxPerInch);
    lpSize->cy = ::MulDiv(lpSize->cy, HIMETRIC_INCH, cyPerInch);
  }
}

void CDC::HIMETRICtoDP(LPSIZE lpSize) const {
  int nMapMode = GetMapMode();
  if (nMapMode < MM_ISOTROPIC && nMapMode != MM_TEXT) {
    ((CDC*)this)->SetMapMode(MM_HIMETRIC);
    LPtoDP(lpSize);
    ((CDC*)this)->SetMapMode(nMapMode);
  } else {
    int cxPerInch = GetDeviceCaps(LOGPIXELSX);
    int cyPerInch = GetDeviceCaps(LOGPIXELSY);
    lpSize->cx = ::MulDiv(lpSize->cx, cxPerInch, HIMETRIC_INCH);
    lpSize->cy = ::MulDiv(lpSize->cy, cyPerInch, HIMETRIC_INCH);
  }
}

void CDC::LPtoHIMETRIC(LPSIZE lpSize) const {
  LPtoDP(lpSize);
  DPtoHIMETRIC(lpSize);
}

void CDC::HIMETRICtoLP(LPSIZE lpSize) const {
  HIMETRICtoDP(lpSize);
  DPtoLP(lpSize);
}

int CDC::ExcludeUpdateRgn(CWnd* pWnd) { return ::ExcludeUpdateRgn(m_hDC, pWnd->m_hWnd); }

int CDC::GetTextFace(CString& rString) const {
  int nResult = ::GetTextFace(m_hAttribDC, LF_FACESIZE, rString.GetBuffer(LF_FACESIZE));
  rString.ReleaseBuffer();
  return nResult;
}

void CDC::FillSolidRect(LPCRECT lpRect, COLORREF clr) {
  ::SetBkColor(m_hDC, clr);
  ::ExtTextOut(m_hDC, 0, 0, ETO_OPAQUE, lpRect, NULL, 0, NULL);
}

void CDC::FillSolidRect(int x, int y, int cx, int cy, COLORREF clr) {
  RECT rect = {x, y, x + cx, y + cy};
  FillSolidRect(&rect, clr);
}

void CDC::Draw3dRect(LPCRECT lpRect, COLORREF clrTopLeft, COLORREF clrBottomRight) {
  Draw3dRect(lpRect->left, lpRect->top, lpRect->right - lpRect->left,
             lpRect->bottom - lpRect->top, clrTopLeft, clrBottomRight);
}

void CDC::Draw3dRect(int x, int y, int cx, int cy, COLORREF clrTopLeft, COLORREF clrBottomRight) {
  FillSolidRect(x, y, cx - 1, 1, clrTopLeft);
  FillSolidRect(x, y, 1, cy - 1, clrTopLeft);
  FillSolidRect(x + cx, y, -1, cy, clrBottomRight);
  FillSolidRect(x, y + cy, cx, -1, clrBottomRight);
}

CBrush* PASCAL CDC::GetHalftoneBrush() {
  static CBrush* s_pHalftoneBrush = NULL;
  if (s_pHalftoneBrush == NULL) {
    WORD grayPattern[8];
    for (int i = 0; i < 8; i++) grayPattern[i] = (WORD)(0x5555 << (i & 1));
    HBITMAP grayBitmap = ::CreateBitmap(8, 8, 1, 1, grayPattern);
    if (grayBitmap != NULL) {
      HBRUSH hBrush = ::CreatePatternBrush(grayBitmap);
      ::DeleteObject(grayBitmap);
      if (hBrush != NULL) {
        s_pHalftoneBrush = new CBrush;
        s_pHalftoneBrush->Attach(hBrush);
        // Keep it out of the per-thread map so any thread can use it.
        afxMapHGDIOBJ(TRUE)->RemoveHandle(hBrush);
      }
    }
  }
  return s_pHalftoneBrush;
}

void CDC::DrawDragRect(LPCRECT lpRect, SIZE size, LPCRECT lpRectLast, SIZE sizeLast,
                       CBrush* pBrush, CBrush* pBrushLast) {
  // The frame is drawn with XOR, so drawing it again erases it.
  CRgn rgnNew, rgnOutside, rgnInside;
  rgnOutside.CreateRectRgnIndirect(lpRect);
  CRect rect = *lpRect;
  rect.InflateRect(-size.cx, -size.cy);
  rect.IntersectRect(rect, lpRect);
  rgnInside.CreateRectRgnIndirect(rect);
  rgnNew.CreateRectRgn(0, 0, 0, 0);
  rgnNew.CombineRgn(&rgnOutside, &rgnInside, RGN_XOR);

  CBrush* pBrushOld = NULL;
  if (pBrush == NULL) pBrush = CDC::GetHalftoneBrush();
  if (pBrushLast == NULL) pBrushLast = pBrush;

  CRgn rgnLast, rgnUpdate;
  if (lpRectLast != NULL) {
    rgnLast.CreateRectRgn(0, 0, 0, 0);
    rgnOutside.SetRectRgn(lpRectLast);
    rect = *lpRectLast;
    rect.InflateRect(-sizeLast.cx, -sizeLast.cy);
    rect.IntersectRect(rect, lpRectLast);
    rgnInside.SetRectRgn(rect);
    rgnLast.CombineRgn(&rgnOutside, &rgnInside, RGN_XOR);

    // Only redraw the parts that changed when the brush is the same.
    if (pBrush->m_hObject == pBrushLast->m_hObject) {
      rgnUpdate.CreateRectRgn(0, 0, 0, 0);
      rgnUpdate.CombineRgn(&rgnLast, &rgnNew, RGN_XOR);
    }
  }
  if (pBrush->m_hObject != pBrushLast->m_hObject && lpRectLast != NULL) {
    SelectClipRgn(&rgnLast, RGN_COPY);
    GetClipBox(&rect);
    pBrushOld = SelectObject(pBrushLast);
    PatBlt(rect.left, rect.top, rect.Width(), rect.Height(), PATINVERT);
    SelectObject(pBrushOld);
    pBrushOld = NULL;
  }

  SelectClipRgn(rgnUpdate.m_hObject != NULL ? &rgnUpdate : &rgnNew, RGN_COPY);
  GetClipBox(&rect);
  pBrushOld = SelectObject(pBrush);
  PatBlt(rect.left, rect.top, rect.Width(), rect.Height(), PATINVERT);

  if (pBrushOld != NULL) SelectObject(pBrushOld);
  SelectClipRgn(NULL, RGN_COPY);
}

typedef BOOL(WINAPI* PFN_TRANSPARENTBLT)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);

BOOL CDC::TransparentBlt(int xDest, int yDest, int nDestWidth, int nDestHeight, CDC* pSrcDC,
                         int xSrc, int ySrc, int nSrcWidth, int nSrcHeight,
                         UINT clrTransparent) {
  // msimg32 is not on Windows 95 or NT 4; fall back to a mask blit.
  static PFN_TRANSPARENTBLT s_pfn = NULL;
  static BOOL s_bLooked = FALSE;
  if (!s_bLooked) {
    HMODULE hMod = ::LoadLibraryA("msimg32.dll");
    if (hMod != NULL) s_pfn = (PFN_TRANSPARENTBLT)(void*)::GetProcAddress(hMod, "TransparentBlt");
    s_bLooked = TRUE;
  }
  if (s_pfn != NULL)
    return s_pfn(m_hDC, xDest, yDest, nDestWidth, nDestHeight, pSrcDC->GetSafeHdc(), xSrc, ySrc,
                 nSrcWidth, nSrcHeight, clrTransparent);

  // Build a monochrome mask where the transparent colour is white.
  HDC hdcSrc = pSrcDC->GetSafeHdc();
  HDC hdcMask = ::CreateCompatibleDC(m_hDC);
  HBITMAP hbmMask = ::CreateBitmap(nSrcWidth, nSrcHeight, 1, 1, NULL);
  HGDIOBJ hOldMask = ::SelectObject(hdcMask, hbmMask);
  COLORREF crOldBk = ::SetBkColor(hdcSrc, clrTransparent);
  ::BitBlt(hdcMask, 0, 0, nSrcWidth, nSrcHeight, hdcSrc, xSrc, ySrc, SRCCOPY);
  ::SetBkColor(hdcSrc, crOldBk);

  COLORREF crOldText = ::SetTextColor(m_hDC, RGB(0, 0, 0));
  crOldBk = ::SetBkColor(m_hDC, RGB(255, 255, 255));
  ::StretchBlt(m_hDC, xDest, yDest, nDestWidth, nDestHeight, hdcSrc, xSrc, ySrc, nSrcWidth,
               nSrcHeight, SRCINVERT);
  ::StretchBlt(m_hDC, xDest, yDest, nDestWidth, nDestHeight, hdcMask, 0, 0, nSrcWidth,
               nSrcHeight, SRCAND);
  ::StretchBlt(m_hDC, xDest, yDest, nDestWidth, nDestHeight, hdcSrc, xSrc, ySrc, nSrcWidth,
               nSrcHeight, SRCINVERT);
  ::SetTextColor(m_hDC, crOldText);
  ::SetBkColor(m_hDC, crOldBk);

  ::SelectObject(hdcMask, hOldMask);
  ::DeleteObject(hbmMask);
  ::DeleteDC(hdcMask);
  return TRUE;
}

// ---------------------------------------------------------------------
// Window DCs

CClientDC::CClientDC(CWnd* pWnd) {
  m_hWnd = pWnd->GetSafeHwnd();
  if (!Attach(::GetDC(m_hWnd))) AfxThrowResourceException();
}

CClientDC::~CClientDC() { ::ReleaseDC(m_hWnd, Detach()); }

CWindowDC::CWindowDC(CWnd* pWnd) {
  m_hWnd = pWnd->GetSafeHwnd();
  if (!Attach(::GetWindowDC(m_hWnd))) AfxThrowResourceException();
}

CWindowDC::~CWindowDC() { ::ReleaseDC(m_hWnd, Detach()); }

CPaintDC::CPaintDC(CWnd* pWnd) {
  m_hWnd = pWnd->m_hWnd;
  if (!Attach(::BeginPaint(m_hWnd, &m_ps))) AfxThrowResourceException();
}

CPaintDC::~CPaintDC() {
  ::EndPaint(m_hWnd, &m_ps);
  Detach();
}

// ---------------------------------------------------------------------
// CMenu

CMenu::~CMenu() { DestroyMenu(); }

CMenu* PASCAL CMenu::FromHandle(HMENU hMenu) { return (CMenu*)afxMapHMENU(TRUE)->FromHandle(hMenu); }

void PASCAL CMenu::DeleteTempMap() {
  CHandleMap* pMap = afxMapHMENU();
  if (pMap != NULL) pMap->DeleteTemp();
}

BOOL CMenu::Attach(HMENU hMenu) {
  if (hMenu == NULL) return FALSE;
  m_hMenu = hMenu;
  afxMapHMENU(TRUE)->SetPermanent(m_hMenu, this);
  return TRUE;
}

HMENU CMenu::Detach() {
  HMENU hMenu = m_hMenu;
  if (hMenu != NULL) {
    CHandleMap* pMap = afxMapHMENU();
    if (pMap != NULL && pMap->LookupPermanent(hMenu) == this) pMap->RemoveHandle(hMenu);
  }
  m_hMenu = NULL;
  return hMenu;
}

BOOL CMenu::DestroyMenu() {
  if (m_hMenu == NULL) return FALSE;
  return ::DestroyMenu(Detach());
}

BOOL CMenu::LoadMenu(LPCTSTR lpszResourceName) {
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_MENU);
  return Attach(::LoadMenu(hInst, lpszResourceName));
}

BOOL CMenu::TrackPopupMenu(UINT nFlags, int x, int y, CWnd* pWnd, LPCRECT lpRect) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  HWND hWndOld = pState->m_hTrackingWindow;
  HMENU hMenuOld = pState->m_hTrackingMenu;
  pState->m_hTrackingWindow = pWnd->GetSafeHwnd();
  pState->m_hTrackingMenu = m_hMenu;
  BOOL bOK = ::TrackPopupMenu(m_hMenu, nFlags, x, y, 0, pWnd->GetSafeHwnd(), lpRect);
  pState->m_hTrackingWindow = hWndOld;
  pState->m_hTrackingMenu = hMenuOld;
  return bOK;
}

BOOL CMenu::TrackPopupMenuEx(UINT fuFlags, int x, int y, CWnd* pWnd, LPTPMPARAMS lptpm) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  HWND hWndOld = pState->m_hTrackingWindow;
  HMENU hMenuOld = pState->m_hTrackingMenu;
  pState->m_hTrackingWindow = pWnd->GetSafeHwnd();
  pState->m_hTrackingMenu = m_hMenu;
  BOOL bOK = ::TrackPopupMenuEx(m_hMenu, fuFlags, x, y, pWnd->GetSafeHwnd(), lptpm);
  pState->m_hTrackingWindow = hWndOld;
  pState->m_hTrackingMenu = hMenuOld;
  return bOK;
}

int CMenu::GetMenuString(UINT nIDItem, CString& rString, UINT nFlags) const {
  int nLen = ::GetMenuString(m_hMenu, nIDItem, NULL, 0, nFlags);
  if (nLen <= 0) {
    rString.Empty();
    return nLen;
  }
  ::GetMenuString(m_hMenu, nIDItem, rString.GetBufferSetLength(nLen), nLen + 1, nFlags);
  rString.ReleaseBuffer();
  return nLen;
}

void CMenu::DrawItem(LPDRAWITEMSTRUCT) {}
void CMenu::MeasureItem(LPMEASUREITEMSTRUCT) {}
