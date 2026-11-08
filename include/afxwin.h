// afxwin.h - windows, GDI, menus, dialogs, controls, document/view and
// the application classes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXWIN_H__
#define __AFXWIN_H__

#ifndef __AFX_H__
#include <afx.h>
#endif
#include <afxres.h>
#include <afxcoll.h>
#include <atltypes.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <ocidl.h>

// Resource types MFC adds
#ifndef RT_DLGINIT
#define RT_DLGINIT MAKEINTRESOURCE(240)
#endif
#ifndef RT_TOOLBAR
#define RT_TOOLBAR MAKEINTRESOURCE(241)
#endif

// Names that windows.h defines as macros but MFC uses as member names.
#undef GetNextWindow

// ---------------------------------------------------------------------
// Forward declarations

class CSize;
class CPoint;
class CRect;
class CGdiObject;
class CPen;
class CBrush;
class CFont;
class CBitmap;
class CPalette;
class CRgn;
class CDC;
class CClientDC;
class CWindowDC;
class CPaintDC;
class CMenu;
class CCmdTarget;
class CCmdUI;
class COleDropTarget;
class COleMessageFilter;
class COleControlContainer;
class COleControlSite;
struct _AFX_OCC_DIALOG_INFO;
class COleDataObject;
typedef DWORD DROPEFFECT;
class CWnd;
class CDialog;
class CStatic;
class CButton;
class CListBox;
class CCheckListBox;
class CComboBox;
class CEdit;
class CScrollBar;
class CFrameWnd;
class CMDIFrameWnd;
class CMDIChildWnd;
class CMiniFrameWnd;
class CMiniDockFrameWnd;
class CDockBar;
class CView;
class CScrollView;
class CDocument;
class CDocTemplate;
class COleFrameHook;
class CSingleDocTemplate;
class CMultiDocTemplate;
class CWinThread;
class CWinApp;
class CDataExchange;
class CCommandLineInfo;
class CControlBar;
class CSplitterWnd;
class CToolBar;
class CStatusBar;
class CDockState;
class CRecentFileList;
class CDocManager;
class CStatusBar;
class CToolBar;
class CDockState;
class CRecentFileList;
class CPrintInfo;
struct CPrintPreviewState;
class CPreviewView;
class CPreviewDC;
class CPrintDialog;
class CDocManager;
struct CCreateContext;

// ---------------------------------------------------------------------
// GDI objects

class CGdiObject : public CObject {
  DECLARE_DYNCREATE(CGdiObject)
public:
  HGDIOBJ m_hObject;
  operator HGDIOBJ() const { return AFX_IS_NULL_THIS() ? NULL : m_hObject; }
  HGDIOBJ GetSafeHandle() const { return AFX_IS_NULL_THIS() ? NULL : m_hObject; }

  static CGdiObject* PASCAL FromHandle(HGDIOBJ hObject);
  static void PASCAL DeleteTempMap();
  BOOL Attach(HGDIOBJ hObject);
  HGDIOBJ Detach();

  CGdiObject() : m_hObject(NULL) {}
  virtual ~CGdiObject();
  BOOL DeleteObject();

  int GetObject(int nCount, LPVOID lpObject) const {
    return ::GetObject(m_hObject, nCount, lpObject);
  }
  UINT GetObjectType() const { return (UINT)::GetObjectType(m_hObject); }
  BOOL CreateStockObject(int nIndex) { return Attach(::GetStockObject(nIndex)); }
  BOOL UnrealizeObject() { return ::UnrealizeObject(m_hObject); }
  BOOL operator==(const CGdiObject& obj) const { return obj.m_hObject == m_hObject; }
  BOOL operator!=(const CGdiObject& obj) const { return obj.m_hObject != m_hObject; }
};

class CPen : public CGdiObject {
  DECLARE_DYNAMIC(CPen)
public:
  static CPen* PASCAL FromHandle(HPEN hPen) { return (CPen*)CGdiObject::FromHandle(hPen); }
  CPen() {}
  CPen(int nPenStyle, int nWidth, COLORREF crColor) {
    if (!Attach(::CreatePen(nPenStyle, nWidth, crColor))) AfxThrowResourceException();
  }
  CPen(int nPenStyle, int nWidth, const LOGBRUSH* pLogBrush, int nStyleCount = 0,
       const DWORD* lpStyle = NULL) {
    if (!Attach(::ExtCreatePen(nPenStyle, nWidth, pLogBrush, nStyleCount, lpStyle)))
      AfxThrowResourceException();
  }
  BOOL CreatePen(int nPenStyle, int nWidth, COLORREF crColor) {
    return Attach(::CreatePen(nPenStyle, nWidth, crColor));
  }
  BOOL CreatePen(int nPenStyle, int nWidth, const LOGBRUSH* pLogBrush, int nStyleCount = 0,
                 const DWORD* lpStyle = NULL) {
    return Attach(::ExtCreatePen(nPenStyle, nWidth, pLogBrush, nStyleCount, lpStyle));
  }
  BOOL CreatePenIndirect(LPLOGPEN lpLogPen) { return Attach(::CreatePenIndirect(lpLogPen)); }
  operator HPEN() const { return (HPEN)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  int GetLogPen(LOGPEN* pLogPen) { return ::GetObject(m_hObject, sizeof(LOGPEN), pLogPen); }
  int GetExtLogPen(EXTLOGPEN* pLogPen) { return ::GetObject(m_hObject, sizeof(EXTLOGPEN), pLogPen); }
};

class CBrush : public CGdiObject {
  DECLARE_DYNAMIC(CBrush)
public:
  static CBrush* PASCAL FromHandle(HBRUSH hBrush) { return (CBrush*)CGdiObject::FromHandle(hBrush); }
  CBrush() {}
  CBrush(COLORREF crColor) {
    if (!Attach(::CreateSolidBrush(crColor))) AfxThrowResourceException();
  }
  CBrush(int nIndex, COLORREF crColor) {
    if (!Attach(::CreateHatchBrush(nIndex, crColor))) AfxThrowResourceException();
  }
  CBrush(CBitmap* pBitmap);
  BOOL CreateSolidBrush(COLORREF crColor) { return Attach(::CreateSolidBrush(crColor)); }
  BOOL CreateHatchBrush(int nIndex, COLORREF crColor) {
    return Attach(::CreateHatchBrush(nIndex, crColor));
  }
  BOOL CreateBrushIndirect(const LOGBRUSH* lpLogBrush) {
    return Attach(::CreateBrushIndirect(lpLogBrush));
  }
  BOOL CreatePatternBrush(CBitmap* pBitmap);
  BOOL CreateDIBPatternBrush(HGLOBAL hPackedDIB, UINT nUsage) {
    return Attach(::CreateDIBPatternBrush(hPackedDIB, nUsage));
  }
  BOOL CreateDIBPatternBrush(const void* lpPackedDIB, UINT nUsage) {
    return Attach(::CreateDIBPatternBrushPt(lpPackedDIB, nUsage));
  }
  BOOL CreateSysColorBrush(int nIndex) { return Attach(::GetSysColorBrush(nIndex)); }
  operator HBRUSH() const { return (HBRUSH)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  int GetLogBrush(LOGBRUSH* pLogBrush) { return ::GetObject(m_hObject, sizeof(LOGBRUSH), pLogBrush); }
};

class CFont : public CGdiObject {
  DECLARE_DYNAMIC(CFont)
public:
  static CFont* PASCAL FromHandle(HFONT hFont) { return (CFont*)CGdiObject::FromHandle(hFont); }
  CFont() {}
  BOOL CreateFontIndirect(const LOGFONT* lpLogFont) { return Attach(::CreateFontIndirect(lpLogFont)); }
  BOOL CreateFont(int nHeight, int nWidth, int nEscapement, int nOrientation, int nWeight,
                  BYTE bItalic, BYTE bUnderline, BYTE cStrikeOut, BYTE nCharSet,
                  BYTE nOutPrecision, BYTE nClipPrecision, BYTE nQuality,
                  BYTE nPitchAndFamily, LPCTSTR lpszFacename) {
    return Attach(::CreateFont(nHeight, nWidth, nEscapement, nOrientation, nWeight, bItalic,
                               bUnderline, cStrikeOut, nCharSet, nOutPrecision, nClipPrecision,
                               nQuality, nPitchAndFamily, lpszFacename));
  }
  BOOL CreatePointFont(int nPointSize, LPCTSTR lpszFaceName, CDC* pDC = NULL);
  BOOL CreatePointFontIndirect(const LOGFONT* lpLogFont, CDC* pDC = NULL);
  operator HFONT() const { return (HFONT)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  int GetLogFont(LOGFONT* pLogFont) { return ::GetObject(m_hObject, sizeof(LOGFONT), pLogFont); }
};

class CBitmap : public CGdiObject {
  DECLARE_DYNAMIC(CBitmap)
public:
  static CBitmap* PASCAL FromHandle(HBITMAP hBitmap) { return (CBitmap*)CGdiObject::FromHandle(hBitmap); }
  CBitmap() {}
  BOOL LoadBitmap(LPCTSTR lpszResourceName);
  BOOL LoadBitmap(UINT nIDResource) { return LoadBitmap(MAKEINTRESOURCE(nIDResource)); }
  BOOL LoadOEMBitmap(UINT nIDBitmap) { return Attach(::LoadBitmap(NULL, MAKEINTRESOURCE(nIDBitmap))); }
  BOOL LoadMappedBitmap(UINT nIDBitmap, UINT nFlags = 0, LPCOLORMAP lpColorMap = NULL,
                        int nMapSize = 0);
  BOOL CreateBitmap(int nWidth, int nHeight, UINT nPlanes, UINT nBitcount, const void* lpBits) {
    return Attach(::CreateBitmap(nWidth, nHeight, nPlanes, nBitcount, lpBits));
  }
  BOOL CreateBitmapIndirect(LPBITMAP lpBitmap) { return Attach(::CreateBitmapIndirect(lpBitmap)); }
  BOOL CreateCompatibleBitmap(CDC* pDC, int nWidth, int nHeight);
  BOOL CreateDiscardableBitmap(CDC* pDC, int nWidth, int nHeight);
  operator HBITMAP() const { return (HBITMAP)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  int GetBitmap(BITMAP* pBitMap) { return ::GetObject(m_hObject, sizeof(BITMAP), pBitMap); }
  DWORD SetBitmapBits(DWORD dwCount, const void* lpBits) {
    return (DWORD)::SetBitmapBits((HBITMAP)m_hObject, dwCount, lpBits);
  }
  DWORD GetBitmapBits(DWORD dwCount, LPVOID lpBits) const {
    return (DWORD)::GetBitmapBits((HBITMAP)m_hObject, dwCount, lpBits);
  }
  CSize SetBitmapDimension(int nWidth, int nHeight) {
    SIZE size;
    ::SetBitmapDimensionEx((HBITMAP)m_hObject, nWidth, nHeight, &size);
    return size;
  }
  CSize GetBitmapDimension() const {
    SIZE size;
    ::GetBitmapDimensionEx((HBITMAP)m_hObject, &size);
    return size;
  }
};

class CPalette : public CGdiObject {
  DECLARE_DYNAMIC(CPalette)
public:
  static CPalette* PASCAL FromHandle(HPALETTE hPalette) { return (CPalette*)CGdiObject::FromHandle(hPalette); }
  CPalette() {}
  BOOL CreatePalette(LPLOGPALETTE lpLogPalette) { return Attach(::CreatePalette(lpLogPalette)); }
  BOOL CreateHalftonePalette(CDC* pDC);
  operator HPALETTE() const { return (HPALETTE)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  int GetEntryCount() {
    WORD nEntries = 0;
    ::GetObject(m_hObject, sizeof(WORD), &nEntries);
    return (int)nEntries;
  }
  UINT GetPaletteEntries(UINT nStartIndex, UINT nNumEntries, LPPALETTEENTRY lpPaletteColors) const {
    return ::GetPaletteEntries((HPALETTE)m_hObject, nStartIndex, nNumEntries, lpPaletteColors);
  }
  UINT SetPaletteEntries(UINT nStartIndex, UINT nNumEntries, LPPALETTEENTRY lpPaletteColors) {
    return ::SetPaletteEntries((HPALETTE)m_hObject, nStartIndex, nNumEntries, lpPaletteColors);
  }
  void AnimatePalette(UINT nStartIndex, UINT nNumEntries, LPPALETTEENTRY lpPaletteColors) {
    ::AnimatePalette((HPALETTE)m_hObject, nStartIndex, nNumEntries, lpPaletteColors);
  }
  UINT GetNearestPaletteIndex(COLORREF crColor) const {
    return ::GetNearestPaletteIndex((HPALETTE)m_hObject, crColor);
  }
  BOOL ResizePalette(UINT nNumEntries) { return ::ResizePalette((HPALETTE)m_hObject, nNumEntries); }
};

class CRgn : public CGdiObject {
  DECLARE_DYNAMIC(CRgn)
public:
  static CRgn* PASCAL FromHandle(HRGN hRgn) { return (CRgn*)CGdiObject::FromHandle(hRgn); }
  operator HRGN() const { return (HRGN)(AFX_IS_NULL_THIS() ? NULL : m_hObject); }
  CRgn() {}
  BOOL CreateRectRgn(int x1, int y1, int x2, int y2) { return Attach(::CreateRectRgn(x1, y1, x2, y2)); }
  BOOL CreateRectRgnIndirect(LPCRECT lpRect) { return Attach(::CreateRectRgnIndirect(lpRect)); }
  BOOL CreateEllipticRgn(int x1, int y1, int x2, int y2) { return Attach(::CreateEllipticRgn(x1, y1, x2, y2)); }
  BOOL CreateEllipticRgnIndirect(LPCRECT lpRect) { return Attach(::CreateEllipticRgnIndirect(lpRect)); }
  BOOL CreatePolygonRgn(LPPOINT lpPoints, int nCount, int nMode) {
    return Attach(::CreatePolygonRgn(lpPoints, nCount, nMode));
  }
  BOOL CreatePolyPolygonRgn(LPPOINT lpPoints, LPINT lpPolyCounts, int nCount, int nPolyFillMode) {
    return Attach(::CreatePolyPolygonRgn(lpPoints, lpPolyCounts, nCount, nPolyFillMode));
  }
  BOOL CreateRoundRectRgn(int x1, int y1, int x2, int y2, int x3, int y3) {
    return Attach(::CreateRoundRectRgn(x1, y1, x2, y2, x3, y3));
  }
  BOOL CreateFromPath(CDC* pDC);
  BOOL CreateFromData(const XFORM* lpXForm, int nCount, const RGNDATA* pRgnData) {
    return Attach(::ExtCreateRegion(lpXForm, nCount, pRgnData));
  }
  void SetRectRgn(int x1, int y1, int x2, int y2) { ::SetRectRgn((HRGN)m_hObject, x1, y1, x2, y2); }
  void SetRectRgn(LPCRECT lpRect) {
    ::SetRectRgn((HRGN)m_hObject, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  int CombineRgn(const CRgn* pRgn1, const CRgn* pRgn2, int nCombineMode) {
    return ::CombineRgn((HRGN)m_hObject, (HRGN)pRgn1->GetSafeHandle(),
                        (HRGN)pRgn2->GetSafeHandle(), nCombineMode);
  }
  int CopyRgn(const CRgn* pRgnSrc) {
    return ::CombineRgn((HRGN)m_hObject, (HRGN)pRgnSrc->GetSafeHandle(), NULL, RGN_COPY);
  }
  BOOL EqualRgn(const CRgn* pRgn) const {
    return ::EqualRgn((HRGN)m_hObject, (HRGN)pRgn->GetSafeHandle());
  }
  int OffsetRgn(int x, int y) { return ::OffsetRgn((HRGN)m_hObject, x, y); }
  int OffsetRgn(POINT point) { return ::OffsetRgn((HRGN)m_hObject, point.x, point.y); }
  int GetRgnBox(LPRECT lpRect) const { return ::GetRgnBox((HRGN)m_hObject, lpRect); }
  BOOL PtInRegion(int x, int y) const { return ::PtInRegion((HRGN)m_hObject, x, y); }
  BOOL PtInRegion(POINT point) const { return ::PtInRegion((HRGN)m_hObject, point.x, point.y); }
  BOOL RectInRegion(LPCRECT lpRect) const { return ::RectInRegion((HRGN)m_hObject, lpRect); }
  int GetRegionData(LPRGNDATA lpRgnData, int nCount) const {
    return (int)::GetRegionData((HRGN)m_hObject, nCount, lpRgnData);
  }
};

// ---------------------------------------------------------------------
// Device contexts

class CDC : public CObject {
  DECLARE_DYNCREATE(CDC)
public:
  HDC m_hDC;
  HDC m_hAttribDC;
  operator HDC() const { return AFX_IS_NULL_THIS() ? NULL : m_hDC; }
  HDC GetSafeHdc() const { return AFX_IS_NULL_THIS() ? NULL : m_hDC; }
  CWnd* GetWindow() const;

  static CDC* PASCAL FromHandle(HDC hDC);
  static void PASCAL DeleteTempMap();
  BOOL Attach(HDC hDC);
  virtual HDC Detach();
  virtual void SetAttribDC(HDC hDC) { m_hAttribDC = hDC; }
  virtual void SetOutputDC(HDC hDC) { m_hDC = hDC; }
  virtual void ReleaseAttribDC() { m_hAttribDC = NULL; }
  virtual void ReleaseOutputDC() { m_hDC = NULL; }

  BOOL IsPrinting() const { return m_bPrinting; }

  CDC();
  virtual ~CDC();
  BOOL CreateDC(LPCTSTR lpszDriverName, LPCTSTR lpszDeviceName, LPCTSTR lpszOutput,
                const void* lpInitData) {
    return Attach(::CreateDC(lpszDriverName, lpszDeviceName, lpszOutput, (const DEVMODE*)lpInitData));
  }
  BOOL CreateIC(LPCTSTR lpszDriverName, LPCTSTR lpszDeviceName, LPCTSTR lpszOutput,
                const void* lpInitData) {
    return Attach(::CreateIC(lpszDriverName, lpszDeviceName, lpszOutput, (const DEVMODE*)lpInitData));
  }
  BOOL CreateCompatibleDC(CDC* pDC) { return Attach(::CreateCompatibleDC(pDC->GetSafeHdc())); }
  BOOL DeleteDC();

  // Device context state
  virtual int SaveDC();
  virtual BOOL RestoreDC(int nSavedDC);
  int GetDeviceCaps(int nIndex) const { return ::GetDeviceCaps(m_hAttribDC, nIndex); }
  UINT SetBoundsRect(LPCRECT lpRectBounds, UINT flags) { return ::SetBoundsRect(m_hDC, lpRectBounds, flags); }
  UINT GetBoundsRect(LPRECT lpRectBounds, UINT flags) { return ::GetBoundsRect(m_hAttribDC, lpRectBounds, flags); }
  BOOL ResetDC(const DEVMODE* lpDevMode) { return ::ResetDC(m_hAttribDC, lpDevMode) != NULL; }

  // Drawing tools
  CPoint GetBrushOrg() const { POINT p; ::GetBrushOrgEx(m_hDC, &p); return p; }
  CPoint SetBrushOrg(int x, int y) { POINT p; ::SetBrushOrgEx(m_hDC, x, y, &p); return p; }
  CPoint SetBrushOrg(POINT point) { return SetBrushOrg(point.x, point.y); }
  int EnumObjects(int nObjectType, int(CALLBACK* lpfn)(LPVOID, LPARAM), LPARAM lpData) {
    return ::EnumObjects(m_hAttribDC, nObjectType, (GOBJENUMPROC)lpfn, lpData);
  }

  CGdiObject* SelectStockObject(int nIndex);
  CPen* SelectObject(CPen* pPen);
  CBrush* SelectObject(CBrush* pBrush);
  virtual CFont* SelectObject(CFont* pFont);
  CBitmap* SelectObject(CBitmap* pBitmap);
  int SelectObject(CRgn* pRgn);
  CGdiObject* SelectObject(CGdiObject* pObject);
  HGDIOBJ SelectObject(HGDIOBJ hObject);

  // Color and color palette
  COLORREF GetNearestColor(COLORREF crColor) const { return ::GetNearestColor(m_hAttribDC, crColor); }
  CPalette* SelectPalette(CPalette* pPalette, BOOL bForceBackground);
  UINT RealizePalette() { return ::RealizePalette(m_hDC); }
  void UpdateColors() { ::UpdateColors(m_hDC); }

  // Drawing attributes
  COLORREF GetBkColor() const { return ::GetBkColor(m_hAttribDC); }
  int GetBkMode() const { return ::GetBkMode(m_hAttribDC); }
  int GetPolyFillMode() const { return ::GetPolyFillMode(m_hAttribDC); }
  int GetROP2() const { return ::GetROP2(m_hAttribDC); }
  int GetStretchBltMode() const { return ::GetStretchBltMode(m_hAttribDC); }
  COLORREF GetTextColor() const { return ::GetTextColor(m_hAttribDC); }
  virtual COLORREF SetBkColor(COLORREF crColor);
  int SetBkMode(int nBkMode);
  int SetPolyFillMode(int nPolyFillMode);
  int SetROP2(int nDrawMode);
  int SetStretchBltMode(int nStretchMode);
  virtual COLORREF SetTextColor(COLORREF crColor);
  COLORREF GetDCBrushColor() const { return ::GetDCBrushColor(m_hAttribDC); }
  COLORREF SetDCBrushColor(COLORREF crColor) { return ::SetDCBrushColor(m_hDC, crColor); }
  COLORREF GetDCPenColor() const { return ::GetDCPenColor(m_hAttribDC); }
  COLORREF SetDCPenColor(COLORREF crColor) { return ::SetDCPenColor(m_hDC, crColor); }
  BOOL GetColorAdjustment(LPCOLORADJUSTMENT lpColorAdjust) const {
    return ::GetColorAdjustment(m_hAttribDC, lpColorAdjust);
  }
  BOOL SetColorAdjustment(const COLORADJUSTMENT* lpColorAdjust) {
    return ::SetColorAdjustment(m_hDC, lpColorAdjust);
  }

  // Mapping
  int GetMapMode() const { return ::GetMapMode(m_hAttribDC); }
  CPoint GetViewportOrg() const { POINT p; ::GetViewportOrgEx(m_hAttribDC, &p); return p; }
  virtual int SetMapMode(int nMapMode);
  virtual CPoint SetViewportOrg(int x, int y);
  CPoint SetViewportOrg(POINT point) { return SetViewportOrg(point.x, point.y); }
  virtual CPoint OffsetViewportOrg(int nWidth, int nHeight);
  CSize GetViewportExt() const { SIZE s; ::GetViewportExtEx(m_hAttribDC, &s); return s; }
  virtual CSize SetViewportExt(int cx, int cy);
  CSize SetViewportExt(SIZE size) { return SetViewportExt(size.cx, size.cy); }
  virtual CSize ScaleViewportExt(int xNum, int xDenom, int yNum, int yDenom);
  CPoint GetWindowOrg() const { POINT p; ::GetWindowOrgEx(m_hAttribDC, &p); return p; }
  CPoint SetWindowOrg(int x, int y);
  CPoint SetWindowOrg(POINT point) { return SetWindowOrg(point.x, point.y); }
  CPoint OffsetWindowOrg(int nWidth, int nHeight);
  CSize GetWindowExt() const { SIZE s; ::GetWindowExtEx(m_hAttribDC, &s); return s; }
  virtual CSize SetWindowExt(int cx, int cy);
  CSize SetWindowExt(SIZE size) { return SetWindowExt(size.cx, size.cy); }
  virtual CSize ScaleWindowExt(int xNum, int xDenom, int yNum, int yDenom);

  void DPtoLP(LPPOINT lpPoints, int nCount = 1) const { ::DPtoLP(m_hAttribDC, lpPoints, nCount); }
  void DPtoLP(LPRECT lpRect) const { ::DPtoLP(m_hAttribDC, (LPPOINT)lpRect, 2); }
  void DPtoLP(LPSIZE lpSize) const;
  void LPtoDP(LPPOINT lpPoints, int nCount = 1) const { ::LPtoDP(m_hAttribDC, lpPoints, nCount); }
  void LPtoDP(LPRECT lpRect) const { ::LPtoDP(m_hAttribDC, (LPPOINT)lpRect, 2); }
  void LPtoDP(LPSIZE lpSize) const;
  void DPtoHIMETRIC(LPSIZE lpSize) const;
  void LPtoHIMETRIC(LPSIZE lpSize) const;
  void HIMETRICtoDP(LPSIZE lpSize) const;
  void HIMETRICtoLP(LPSIZE lpSize) const;

  // Region functions
  BOOL FillRgn(CRgn* pRgn, CBrush* pBrush) {
    return ::FillRgn(m_hDC, (HRGN)pRgn->GetSafeHandle(), (HBRUSH)pBrush->GetSafeHandle());
  }
  BOOL FrameRgn(CRgn* pRgn, CBrush* pBrush, int nWidth, int nHeight) {
    return ::FrameRgn(m_hDC, (HRGN)pRgn->GetSafeHandle(), (HBRUSH)pBrush->GetSafeHandle(), nWidth, nHeight);
  }
  BOOL InvertRgn(CRgn* pRgn) { return ::InvertRgn(m_hDC, (HRGN)pRgn->GetSafeHandle()); }
  BOOL PaintRgn(CRgn* pRgn) { return ::PaintRgn(m_hDC, (HRGN)pRgn->GetSafeHandle()); }

  // Clipping
  virtual int GetClipBox(LPRECT lpRect) const { return ::GetClipBox(m_hDC, lpRect); }
  virtual BOOL PtVisible(int x, int y) const { return ::PtVisible(m_hDC, x, y); }
  BOOL PtVisible(POINT point) const { return PtVisible(point.x, point.y); }
  virtual BOOL RectVisible(LPCRECT lpRect) const { return ::RectVisible(m_hDC, lpRect); }
  int SelectClipRgn(CRgn* pRgn) { return ::SelectClipRgn(m_hDC, (HRGN)pRgn->GetSafeHandle()); }
  int ExcludeClipRect(int x1, int y1, int x2, int y2) { return ::ExcludeClipRect(m_hDC, x1, y1, x2, y2); }
  int ExcludeClipRect(LPCRECT lpRect) {
    return ::ExcludeClipRect(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  int ExcludeUpdateRgn(CWnd* pWnd);
  int IntersectClipRect(int x1, int y1, int x2, int y2) { return ::IntersectClipRect(m_hDC, x1, y1, x2, y2); }
  int IntersectClipRect(LPCRECT lpRect) {
    return ::IntersectClipRect(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  int OffsetClipRgn(int x, int y) { return ::OffsetClipRgn(m_hDC, x, y); }
  int OffsetClipRgn(SIZE size) { return ::OffsetClipRgn(m_hDC, size.cx, size.cy); }
  int SelectClipRgn(CRgn* pRgn, int nMode) {
    return ::ExtSelectClipRgn(m_hDC, (HRGN)pRgn->GetSafeHandle(), nMode);
  }

  // Line output
  CPoint GetCurrentPosition() const { POINT p; ::GetCurrentPositionEx(m_hAttribDC, &p); return p; }
  CPoint MoveTo(int x, int y) { POINT p; ::MoveToEx(m_hDC, x, y, &p); return p; }
  CPoint MoveTo(POINT point) { return MoveTo(point.x, point.y); }
  BOOL LineTo(int x, int y) { return ::LineTo(m_hDC, x, y); }
  BOOL LineTo(POINT point) { return LineTo(point.x, point.y); }
  BOOL Arc(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    return ::Arc(m_hDC, x1, y1, x2, y2, x3, y3, x4, y4);
  }
  BOOL Arc(LPCRECT lpRect, POINT ptStart, POINT ptEnd) {
    return ::Arc(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, ptStart.x,
                 ptStart.y, ptEnd.x, ptEnd.y);
  }
  BOOL Polyline(const POINT* lpPoints, int nCount) { return ::Polyline(m_hDC, lpPoints, nCount); }
  BOOL AngleArc(int x, int y, int nRadius, float fStartAngle, float fSweepAngle) {
    return ::AngleArc(m_hDC, x, y, nRadius, fStartAngle, fSweepAngle);
  }
  BOOL ArcTo(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    return ::ArcTo(m_hDC, x1, y1, x2, y2, x3, y3, x4, y4);
  }
  BOOL ArcTo(LPCRECT lpRect, POINT ptStart, POINT ptEnd) {
    return ArcTo(lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, ptStart.x, ptStart.y,
                 ptEnd.x, ptEnd.y);
  }
  int GetArcDirection() const { return ::GetArcDirection(m_hAttribDC); }
  int SetArcDirection(int nArcDirection) { return ::SetArcDirection(m_hDC, nArcDirection); }
  BOOL PolyDraw(const POINT* lpPoints, const BYTE* lpTypes, int nCount) {
    return ::PolyDraw(m_hDC, lpPoints, lpTypes, nCount);
  }
  BOOL PolylineTo(const POINT* lpPoints, int nCount) { return ::PolylineTo(m_hDC, lpPoints, nCount); }
  BOOL PolyPolyline(const POINT* lpPoints, const DWORD* lpPolyPoints, int nCount) {
    return ::PolyPolyline(m_hDC, lpPoints, lpPolyPoints, nCount);
  }
  BOOL PolyBezier(const POINT* lpPoints, int nCount) { return ::PolyBezier(m_hDC, lpPoints, nCount); }
  BOOL PolyBezierTo(const POINT* lpPoints, int nCount) { return ::PolyBezierTo(m_hDC, lpPoints, nCount); }

  // Simple drawing
  void FillRect(LPCRECT lpRect, CBrush* pBrush) {
    ::FillRect(m_hDC, lpRect, (HBRUSH)pBrush->GetSafeHandle());
  }
  void FrameRect(LPCRECT lpRect, CBrush* pBrush) {
    ::FrameRect(m_hDC, lpRect, (HBRUSH)pBrush->GetSafeHandle());
  }
  void InvertRect(LPCRECT lpRect) { ::InvertRect(m_hDC, lpRect); }
  BOOL DrawIcon(int x, int y, HICON hIcon) { return ::DrawIcon(m_hDC, x, y, hIcon); }
  BOOL DrawIcon(POINT point, HICON hIcon) { return ::DrawIcon(m_hDC, point.x, point.y, hIcon); }
  BOOL DrawState(CPoint pt, CSize size, HBITMAP hBitmap, UINT nFlags, HBRUSH hBrush = NULL) {
    return ::DrawState(m_hDC, hBrush, NULL, (LPARAM)hBitmap, 0, pt.x, pt.y, size.cx, size.cy,
                       nFlags | DST_BITMAP);
  }
  BOOL DrawState(CPoint pt, CSize size, HICON hIcon, UINT nFlags, HBRUSH hBrush = NULL) {
    return ::DrawState(m_hDC, hBrush, NULL, (LPARAM)hIcon, 0, pt.x, pt.y, size.cx, size.cy,
                       nFlags | DST_ICON);
  }
  BOOL DrawState(CPoint pt, CSize size, LPCTSTR lpszText, UINT nFlags, BOOL bPrefixText = TRUE,
                 int nTextLen = 0, HBRUSH hBrush = NULL) {
    return ::DrawState(m_hDC, hBrush, NULL, (LPARAM)lpszText, (WPARAM)nTextLen, pt.x, pt.y,
                       size.cx, size.cy, nFlags | (bPrefixText ? DST_PREFIXTEXT : DST_TEXT));
  }
  void FillSolidRect(LPCRECT lpRect, COLORREF clr);
  void FillSolidRect(int x, int y, int cx, int cy, COLORREF clr);
  void Draw3dRect(LPCRECT lpRect, COLORREF clrTopLeft, COLORREF clrBottomRight);
  void Draw3dRect(int x, int y, int cx, int cy, COLORREF clrTopLeft, COLORREF clrBottomRight);
  BOOL DrawEdge(LPRECT lpRect, UINT nEdge, UINT nFlags) { return ::DrawEdge(m_hDC, lpRect, nEdge, nFlags); }
  BOOL DrawFrameControl(LPRECT lpRect, UINT nType, UINT nState) {
    return ::DrawFrameControl(m_hDC, lpRect, nType, nState);
  }
  void DrawFocusRect(LPCRECT lpRect) { ::DrawFocusRect(m_hDC, lpRect); }
  void DrawDragRect(LPCRECT lpRect, SIZE size, LPCRECT lpRectLast, SIZE sizeLast,
                    CBrush* pBrush = NULL, CBrush* pBrushLast = NULL);

  // Ellipse and polygon
  BOOL Chord(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    return ::Chord(m_hDC, x1, y1, x2, y2, x3, y3, x4, y4);
  }
  BOOL Chord(LPCRECT lpRect, POINT ptStart, POINT ptEnd) {
    return ::Chord(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, ptStart.x,
                   ptStart.y, ptEnd.x, ptEnd.y);
  }
  BOOL Ellipse(int x1, int y1, int x2, int y2) { return ::Ellipse(m_hDC, x1, y1, x2, y2); }
  BOOL Ellipse(LPCRECT lpRect) {
    return ::Ellipse(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  BOOL Pie(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4) {
    return ::Pie(m_hDC, x1, y1, x2, y2, x3, y3, x4, y4);
  }
  BOOL Pie(LPCRECT lpRect, POINT ptStart, POINT ptEnd) {
    return ::Pie(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, ptStart.x,
                 ptStart.y, ptEnd.x, ptEnd.y);
  }
  BOOL Polygon(const POINT* lpPoints, int nCount) { return ::Polygon(m_hDC, lpPoints, nCount); }
  BOOL PolyPolygon(const POINT* lpPoints, const INT* lpPolyCounts, int nCount) {
    return ::PolyPolygon(m_hDC, lpPoints, lpPolyCounts, nCount);
  }
  BOOL Rectangle(int x1, int y1, int x2, int y2) { return ::Rectangle(m_hDC, x1, y1, x2, y2); }
  BOOL Rectangle(LPCRECT lpRect) {
    return ::Rectangle(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  BOOL RoundRect(int x1, int y1, int x2, int y2, int x3, int y3) {
    return ::RoundRect(m_hDC, x1, y1, x2, y2, x3, y3);
  }
  BOOL RoundRect(LPCRECT lpRect, POINT point) {
    return ::RoundRect(m_hDC, lpRect->left, lpRect->top, lpRect->right, lpRect->bottom, point.x,
                       point.y);
  }

  // Bitmaps
  BOOL PatBlt(int x, int y, int nWidth, int nHeight, DWORD dwRop) {
    return ::PatBlt(m_hDC, x, y, nWidth, nHeight, dwRop);
  }
  BOOL BitBlt(int x, int y, int nWidth, int nHeight, CDC* pSrcDC, int xSrc, int ySrc, DWORD dwRop) {
    return ::BitBlt(m_hDC, x, y, nWidth, nHeight, pSrcDC->GetSafeHdc(), xSrc, ySrc, dwRop);
  }
  BOOL StretchBlt(int x, int y, int nWidth, int nHeight, CDC* pSrcDC, int xSrc, int ySrc,
                  int nSrcWidth, int nSrcHeight, DWORD dwRop) {
    return ::StretchBlt(m_hDC, x, y, nWidth, nHeight, pSrcDC->GetSafeHdc(), xSrc, ySrc,
                        nSrcWidth, nSrcHeight, dwRop);
  }
  COLORREF GetPixel(int x, int y) const { return ::GetPixel(m_hDC, x, y); }
  COLORREF GetPixel(POINT point) const { return ::GetPixel(m_hDC, point.x, point.y); }
  COLORREF SetPixel(int x, int y, COLORREF crColor) { return ::SetPixel(m_hDC, x, y, crColor); }
  COLORREF SetPixel(POINT point, COLORREF crColor) { return ::SetPixel(m_hDC, point.x, point.y, crColor); }
  BOOL FloodFill(int x, int y, COLORREF crColor) { return ::FloodFill(m_hDC, x, y, crColor); }
  BOOL ExtFloodFill(int x, int y, COLORREF crColor, UINT nFillType) {
    return ::ExtFloodFill(m_hDC, x, y, crColor, nFillType);
  }
  BOOL MaskBlt(int x, int y, int nWidth, int nHeight, CDC* pSrcDC, int xSrc, int ySrc,
               CBitmap& maskBitmap, int xMask, int yMask, DWORD dwRop) {
    return ::MaskBlt(m_hDC, x, y, nWidth, nHeight, pSrcDC->GetSafeHdc(), xSrc, ySrc,
                     (HBITMAP)maskBitmap.m_hObject, xMask, yMask, dwRop);
  }
  BOOL PlgBlt(LPPOINT lpPoint, CDC* pSrcDC, int xSrc, int ySrc, int nWidth, int nHeight,
              CBitmap& maskBitmap, int xMask, int yMask) {
    return ::PlgBlt(m_hDC, lpPoint, pSrcDC->GetSafeHdc(), xSrc, ySrc, nWidth, nHeight,
                    (HBITMAP)maskBitmap.m_hObject, xMask, yMask);
  }
  BOOL SetPixelV(int x, int y, COLORREF crColor) { return ::SetPixelV(m_hDC, x, y, crColor); }
  BOOL SetPixelV(POINT point, COLORREF crColor) { return ::SetPixelV(m_hDC, point.x, point.y, crColor); }
  BOOL TransparentBlt(int xDest, int yDest, int nDestWidth, int nDestHeight, CDC* pSrcDC,
                      int xSrc, int ySrc, int nSrcWidth, int nSrcHeight, UINT clrTransparent);

  // Text
  virtual BOOL TextOut(int x, int y, LPCTSTR lpszString, int nCount) {
    return ::TextOut(m_hDC, x, y, lpszString, nCount);
  }
  BOOL TextOut(int x, int y, const CString& str) {
    return TextOut(x, y, (LPCTSTR)str, str.GetLength());
  }
  virtual BOOL ExtTextOut(int x, int y, UINT nOptions, LPCRECT lpRect, LPCTSTR lpszString,
                          UINT nCount, LPINT lpDxWidths) {
    return ::ExtTextOut(m_hDC, x, y, nOptions, lpRect, lpszString, nCount, lpDxWidths);
  }
  BOOL ExtTextOut(int x, int y, UINT nOptions, LPCRECT lpRect, const CString& str,
                  LPINT lpDxWidths) {
    return ExtTextOut(x, y, nOptions, lpRect, (LPCTSTR)str, str.GetLength(), lpDxWidths);
  }
  virtual CSize TabbedTextOut(int x, int y, LPCTSTR lpszString, int nCount, int nTabPositions,
                              LPINT lpnTabStopPositions, int nTabOrigin) {
    return CSize((DWORD)::TabbedTextOut(m_hDC, x, y, lpszString, nCount, nTabPositions,
                                        lpnTabStopPositions, nTabOrigin));
  }
  CSize TabbedTextOut(int x, int y, const CString& str, int nTabPositions,
                      LPINT lpnTabStopPositions, int nTabOrigin) {
    return TabbedTextOut(x, y, (LPCTSTR)str, str.GetLength(), nTabPositions,
                         lpnTabStopPositions, nTabOrigin);
  }
  virtual int DrawText(LPCTSTR lpszString, int nCount, LPRECT lpRect, UINT nFormat) {
    return ::DrawText(m_hDC, lpszString, nCount, lpRect, nFormat);
  }
  int DrawText(const CString& str, LPRECT lpRect, UINT nFormat) {
    return DrawText((LPCTSTR)str, str.GetLength(), lpRect, nFormat);
  }
  virtual int DrawTextEx(LPTSTR lpszString, int nCount, LPRECT lpRect, UINT nFormat,
                         LPDRAWTEXTPARAMS lpDTParams) {
    return ::DrawTextEx(m_hDC, lpszString, nCount, lpRect, nFormat, lpDTParams);
  }
  CSize GetTextExtent(LPCTSTR lpszString, int nCount) const {
    SIZE size;
    ::GetTextExtentPoint32(m_hAttribDC, lpszString, nCount, &size);
    return size;
  }
  CSize GetTextExtent(const CString& str) const { return GetTextExtent(str, str.GetLength()); }
  CSize GetOutputTextExtent(LPCTSTR lpszString, int nCount) const {
    SIZE size;
    ::GetTextExtentPoint32(m_hDC, lpszString, nCount, &size);
    return size;
  }
  CSize GetOutputTextExtent(const CString& str) const { return GetOutputTextExtent(str, str.GetLength()); }
  CSize GetTabbedTextExtent(LPCTSTR lpszString, int nCount, int nTabPositions,
                            LPINT lpnTabStopPositions) const {
    return CSize((DWORD)::GetTabbedTextExtent(m_hAttribDC, lpszString, nCount, nTabPositions,
                                              lpnTabStopPositions));
  }
  CSize GetTabbedTextExtent(const CString& str, int nTabPositions, LPINT lpnTabStopPositions) const {
    return GetTabbedTextExtent(str, str.GetLength(), nTabPositions, lpnTabStopPositions);
  }
  virtual BOOL GrayString(CBrush* pBrush, BOOL(CALLBACK* lpfnOutput)(HDC, LPARAM, int),
                          LPARAM lpData, int nCount, int x, int y, int nWidth, int nHeight) {
    return ::GrayString(m_hDC, (HBRUSH)pBrush->GetSafeHandle(), (GRAYSTRINGPROC)lpfnOutput,
                        lpData, nCount, x, y, nWidth, nHeight);
  }
  UINT GetTextAlign() const { return ::GetTextAlign(m_hAttribDC); }
  UINT SetTextAlign(UINT nFlags);
  int GetTextFace(int nCount, LPTSTR lpszFacename) const {
    return ::GetTextFace(m_hAttribDC, nCount, lpszFacename);
  }
  int GetTextFace(CString& rString) const;
  BOOL GetTextMetrics(LPTEXTMETRIC lpMetrics) const { return ::GetTextMetrics(m_hAttribDC, lpMetrics); }
  BOOL GetOutputTextMetrics(LPTEXTMETRIC lpMetrics) const { return ::GetTextMetrics(m_hDC, lpMetrics); }
  int SetTextJustification(int nBreakExtra, int nBreakCount) {
    return ::SetTextJustification(m_hDC, nBreakExtra, nBreakCount);
  }
  int GetTextCharacterExtra() const { return ::GetTextCharacterExtra(m_hAttribDC); }
  int SetTextCharacterExtra(int nCharExtra) { return ::SetTextCharacterExtra(m_hDC, nCharExtra); }
  BOOL GetCharWidth(UINT nFirstChar, UINT nLastChar, LPINT lpBuffer) const {
    return ::GetCharWidth(m_hAttribDC, nFirstChar, nLastChar, lpBuffer);
  }
  BOOL GetOutputCharWidth(UINT nFirstChar, UINT nLastChar, LPINT lpBuffer) const {
    return ::GetCharWidth(m_hDC, nFirstChar, nLastChar, lpBuffer);
  }
  DWORD GetFontData(DWORD dwTable, DWORD dwOffset, LPVOID lpData, DWORD cbData) const {
    return ::GetFontData(m_hDC, dwTable, dwOffset, lpData, cbData);
  }
  int GetKerningPairs(int nPairs, LPKERNINGPAIR lpkrnpair) const {
    return (int)::GetKerningPairs(m_hAttribDC, nPairs, lpkrnpair);
  }
  UINT GetOutlineTextMetrics(UINT cbData, LPOUTLINETEXTMETRIC lpotm) const {
    return ::GetOutlineTextMetrics(m_hAttribDC, cbData, lpotm);
  }
  DWORD GetGlyphOutline(UINT nChar, UINT nFormat, LPGLYPHMETRICS lpgm, DWORD cbBuffer,
                        LPVOID lpBuffer, const MAT2* lpmat2) const {
    return ::GetGlyphOutline(m_hAttribDC, nChar, nFormat, lpgm, cbBuffer, lpBuffer, lpmat2);
  }
  BOOL GetCharABCWidths(UINT nFirstChar, UINT nLastChar, LPABC lpabc) const {
    return ::GetCharABCWidths(m_hAttribDC, nFirstChar, nLastChar, lpabc);
  }
  DWORD GetFontLanguageInfo() const { return ::GetFontLanguageInfo(m_hDC); }
  DWORD SetMapperFlags(DWORD dwFlag) { return ::SetMapperFlags(m_hDC, dwFlag); }
  CSize GetAspectRatioFilter() const { SIZE s; ::GetAspectRatioFilterEx(m_hAttribDC, &s); return s; }

  // Scrolling
  BOOL ScrollDC(int dx, int dy, LPCRECT lpRectScroll, LPCRECT lpRectClip, CRgn* pRgnUpdate,
                LPRECT lpRectUpdate) {
    return ::ScrollDC(m_hDC, dx, dy, lpRectScroll, lpRectClip, (HRGN)pRgnUpdate->GetSafeHandle(),
                      lpRectUpdate);
  }

  // Printer escapes
  int Escape(int nEscape, int nCount, LPCSTR lpszInData, LPVOID lpOutData) {
    return ::Escape(m_hDC, nEscape, nCount, lpszInData, lpOutData);
  }
  int StartDoc(LPDOCINFO lpDocInfo) { return ::StartDoc(m_hDC, lpDocInfo); }
  int StartDoc(LPCTSTR lpszDocName) {
    DOCINFO di;
    memset(&di, 0, sizeof di);
    di.cbSize = sizeof di;
    di.lpszDocName = lpszDocName;
    return StartDoc(&di);
  }
  int StartPage() { return ::StartPage(m_hDC); }
  int EndPage() { return ::EndPage(m_hDC); }
  int SetAbortProc(BOOL(CALLBACK* lpfn)(HDC, int)) { return ::SetAbortProc(m_hDC, (ABORTPROC)lpfn); }
  int AbortDoc() { return ::AbortDoc(m_hDC); }
  int EndDoc() { return ::EndDoc(m_hDC); }

  // Paths
  BOOL AbortPath() { return ::AbortPath(m_hDC); }
  BOOL BeginPath() { return ::BeginPath(m_hDC); }
  BOOL CloseFigure() { return ::CloseFigure(m_hDC); }
  BOOL EndPath() { return ::EndPath(m_hDC); }
  BOOL FillPath() { return ::FillPath(m_hDC); }
  BOOL FlattenPath() { return ::FlattenPath(m_hDC); }
  BOOL StrokeAndFillPath() { return ::StrokeAndFillPath(m_hDC); }
  BOOL StrokePath() { return ::StrokePath(m_hDC); }
  BOOL WidenPath() { return ::WidenPath(m_hDC); }
  float GetMiterLimit() const { float f; ::GetMiterLimit(m_hDC, &f); return f; }
  BOOL SetMiterLimit(float fMiterLimit) { return ::SetMiterLimit(m_hDC, fMiterLimit, NULL); }
  int GetPath(LPPOINT lpPoints, LPBYTE lpTypes, int nCount) const {
    return ::GetPath(m_hDC, lpPoints, lpTypes, nCount);
  }
  BOOL SelectClipPath(int nMode) { return ::SelectClipPath(m_hDC, nMode); }

  // Metafiles
  BOOL PlayMetaFile(HMETAFILE hMF) { return ::PlayMetaFile(m_hDC, hMF); }
  BOOL PlayMetaFile(HENHMETAFILE hEnhMetaFile, LPCRECT lpBounds) {
    return ::PlayEnhMetaFile(m_hDC, hEnhMetaFile, lpBounds);
  }

  // Graphics mode and world transforms
  int SetGraphicsMode(int iMode) { return ::SetGraphicsMode(m_hDC, iMode); }
  int GetGraphicsMode() const { return ::GetGraphicsMode(m_hDC); }
  BOOL SetWorldTransform(const XFORM* pXform) { return ::SetWorldTransform(m_hDC, pXform); }
  BOOL ModifyWorldTransform(const XFORM* pXform, DWORD iMode) {
    return ::ModifyWorldTransform(m_hDC, pXform, iMode);
  }
  BOOL GetWorldTransform(XFORM* pXform) const { return ::GetWorldTransform(m_hDC, pXform); }

  CPen* GetCurrentPen() const;
  CBrush* GetCurrentBrush() const;
  CPalette* GetCurrentPalette() const;
  CFont* GetCurrentFont() const;
  CBitmap* GetCurrentBitmap() const;

  static CBrush* PASCAL GetHalftoneBrush();

protected:
  BOOL m_bPrinting;
  friend class CView;
  friend class CDocObjectServer;
  friend class CPreviewDC;
  friend class CPreviewView;
};

class CClientDC : public CDC {
  DECLARE_DYNAMIC(CClientDC)
public:
  explicit CClientDC(CWnd* pWnd);
  virtual ~CClientDC();

protected:
  HWND m_hWnd;
};

class CWindowDC : public CDC {
  DECLARE_DYNAMIC(CWindowDC)
public:
  explicit CWindowDC(CWnd* pWnd);
  virtual ~CWindowDC();

protected:
  HWND m_hWnd;
};

class CPaintDC : public CDC {
  DECLARE_DYNAMIC(CPaintDC)
public:
  explicit CPaintDC(CWnd* pWnd);
  virtual ~CPaintDC();
  PAINTSTRUCT m_ps;

protected:
  HWND m_hWnd;
};

class CMetaFileDC : public CDC {
  DECLARE_DYNAMIC(CMetaFileDC)
public:
  CMetaFileDC() {}
  BOOL Create(LPCTSTR lpszFilename = NULL) { return Attach(::CreateMetaFile(lpszFilename)); }
  BOOL CreateEnhanced(CDC* pDCRef, LPCTSTR lpszFileName, LPCRECT lpBounds, LPCTSTR lpszDescription) {
    return Attach(::CreateEnhMetaFile(pDCRef->GetSafeHdc(), lpszFileName, lpBounds, lpszDescription));
  }
  HMETAFILE Close() { return ::CloseMetaFile(Detach()); }
  HENHMETAFILE CloseEnhanced() { return ::CloseEnhMetaFile(Detach()); }
};

// ---------------------------------------------------------------------
// CImageList lives in afxcmn.h; menus here.

class CMenu : public CObject {
  DECLARE_DYNCREATE(CMenu)
public:
  CMenu() : m_hMenu(NULL) {}
  virtual ~CMenu();

  BOOL CreateMenu() { return Attach(::CreateMenu()); }
  BOOL CreatePopupMenu() { return Attach(::CreatePopupMenu()); }
  BOOL LoadMenu(LPCTSTR lpszResourceName);
  BOOL LoadMenu(UINT nIDResource) { return LoadMenu(MAKEINTRESOURCE(nIDResource)); }
  BOOL LoadMenuIndirect(const void* lpMenuTemplate) {
    return Attach(::LoadMenuIndirect((const MENUTEMPLATE*)lpMenuTemplate));
  }
  BOOL DestroyMenu();

  HMENU m_hMenu;
  HMENU GetSafeHmenu() const { return AFX_IS_NULL_THIS() ? NULL : m_hMenu; }
  operator HMENU() const { return AFX_IS_NULL_THIS() ? NULL : m_hMenu; }

  static CMenu* PASCAL FromHandle(HMENU hMenu);
  static void PASCAL DeleteTempMap();
  BOOL Attach(HMENU hMenu);
  HMENU Detach();

  BOOL DeleteMenu(UINT nPosition, UINT nFlags) { return ::DeleteMenu(m_hMenu, nPosition, nFlags); }
  BOOL TrackPopupMenu(UINT nFlags, int x, int y, CWnd* pWnd, LPCRECT lpRect = 0);
  BOOL TrackPopupMenuEx(UINT fuFlags, int x, int y, CWnd* pWnd, LPTPMPARAMS lptpm);
  BOOL operator==(const CMenu& menu) const { return m_hMenu == menu.m_hMenu; }
  BOOL operator!=(const CMenu& menu) const { return m_hMenu != menu.m_hMenu; }

  BOOL AppendMenu(UINT nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL) {
    return ::AppendMenu(m_hMenu, nFlags, nIDNewItem, lpszNewItem);
  }
  BOOL AppendMenu(UINT nFlags, UINT_PTR nIDNewItem, const CBitmap* pBmp) {
    return ::AppendMenu(m_hMenu, nFlags | MF_BITMAP, nIDNewItem, (LPCTSTR)pBmp->GetSafeHandle());
  }
  UINT CheckMenuItem(UINT nIDCheckItem, UINT nCheck) {
    return (UINT)::CheckMenuItem(m_hMenu, nIDCheckItem, nCheck);
  }
  UINT EnableMenuItem(UINT nIDEnableItem, UINT nEnable) {
    return ::EnableMenuItem(m_hMenu, nIDEnableItem, nEnable);
  }
  UINT GetMenuItemCount() const { return (UINT)::GetMenuItemCount(m_hMenu); }
  UINT GetMenuItemID(int nPos) const { return ::GetMenuItemID(m_hMenu, nPos); }
  UINT GetMenuState(UINT nID, UINT nFlags) const { return ::GetMenuState(m_hMenu, nID, nFlags); }
  int GetMenuString(UINT nIDItem, LPTSTR lpString, int nMaxCount, UINT nFlags) const {
    return ::GetMenuString(m_hMenu, nIDItem, lpString, nMaxCount, nFlags);
  }
  int GetMenuString(UINT nIDItem, CString& rString, UINT nFlags) const;
  BOOL GetMenuItemInfo(UINT uItem, LPMENUITEMINFO lpMenuItemInfo, BOOL fByPos = FALSE) {
    return ::GetMenuItemInfo(m_hMenu, uItem, fByPos, lpMenuItemInfo);
  }
  BOOL SetMenuItemInfo(UINT uItem, LPMENUITEMINFO lpMenuItemInfo, BOOL fByPos = FALSE) {
    return ::SetMenuItemInfo(m_hMenu, uItem, fByPos, lpMenuItemInfo);
  }
  CMenu* GetSubMenu(int nPos) const { return CMenu::FromHandle(::GetSubMenu(m_hMenu, nPos)); }
  BOOL InsertMenu(UINT nPosition, UINT nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL) {
    return ::InsertMenu(m_hMenu, nPosition, nFlags, nIDNewItem, lpszNewItem);
  }
  BOOL InsertMenu(UINT nPosition, UINT nFlags, UINT_PTR nIDNewItem, const CBitmap* pBmp) {
    return ::InsertMenu(m_hMenu, nPosition, nFlags | MF_BITMAP, nIDNewItem,
                        (LPCTSTR)pBmp->GetSafeHandle());
  }
  BOOL InsertMenuItem(UINT uItem, LPMENUITEMINFO lpMenuItemInfo, BOOL fByPos = FALSE) {
    return ::InsertMenuItem(m_hMenu, uItem, fByPos, lpMenuItemInfo);
  }
  BOOL ModifyMenu(UINT nPosition, UINT nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = NULL) {
    return ::ModifyMenu(m_hMenu, nPosition, nFlags, nIDNewItem, lpszNewItem);
  }
  BOOL ModifyMenu(UINT nPosition, UINT nFlags, UINT_PTR nIDNewItem, const CBitmap* pBmp) {
    return ::ModifyMenu(m_hMenu, nPosition, nFlags | MF_BITMAP, nIDNewItem,
                        (LPCTSTR)pBmp->GetSafeHandle());
  }
  BOOL RemoveMenu(UINT nPosition, UINT nFlags) { return ::RemoveMenu(m_hMenu, nPosition, nFlags); }
  BOOL SetMenuItemBitmaps(UINT nPosition, UINT nFlags, const CBitmap* pBmpUnchecked,
                          const CBitmap* pBmpChecked) {
    return ::SetMenuItemBitmaps(m_hMenu, nPosition, nFlags,
                                (HBITMAP)pBmpUnchecked->GetSafeHandle(),
                                (HBITMAP)pBmpChecked->GetSafeHandle());
  }
  BOOL CheckMenuRadioItem(UINT nIDFirst, UINT nIDLast, UINT nIDItem, UINT nFlags) {
    return ::CheckMenuRadioItem(m_hMenu, nIDFirst, nIDLast, nIDItem, nFlags);
  }
  BOOL SetDefaultItem(UINT uItem, BOOL fByPos = FALSE) {
    return ::SetMenuDefaultItem(m_hMenu, uItem, fByPos);
  }
  UINT GetDefaultItem(UINT gmdiFlags, BOOL fByPos = FALSE) {
    return ::GetMenuDefaultItem(m_hMenu, fByPos, gmdiFlags);
  }
  DWORD GetMenuContextHelpId() const { return ::GetMenuContextHelpId(m_hMenu); }
  BOOL SetMenuContextHelpId(DWORD dwContextHelpId) {
    return ::SetMenuContextHelpId(m_hMenu, dwContextHelpId);
  }

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
};

// ---------------------------------------------------------------------
// Message maps

#include <afxmsg_.h>

// ---------------------------------------------------------------------
// CCmdTarget

// ---------------------------------------------------------------------
// COM support in CCmdTarget: interface maps, the IDispatch that a
// dispatch map drives, and aggregation.

struct AFX_DISPMAP_ENTRY;
struct AFX_DISPMAP {
  const AFX_DISPMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_DISPMAP_ENTRY* lpEntries;
};

struct AFX_INTERFACEMAP_ENTRY {
  const void* piid;  // NULL for an aggregate (nOffset is then the LPUNKNOWN member)
  size_t nOffset;    // (size_t)-1 ends the map
};
struct AFX_INTERFACEMAP {
  const AFX_INTERFACEMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_INTERFACEMAP_ENTRY* pEntry;
};

// offsetof of a member of a class with virtual functions, which the
// interface maps need, works with clang and GCC but warns.
#define _AFX_NO_OFFSETOF_WARNING_BEGIN \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")
#define _AFX_NO_OFFSETOF_WARNING_END _Pragma("GCC diagnostic pop")

#define DECLARE_INTERFACE_MAP() \
protected: \
  static const AFX_INTERFACEMAP* PASCAL GetThisInterfaceMap(); \
  virtual const AFX_INTERFACEMAP* GetInterfaceMap() const;

#define BEGIN_INTERFACE_MAP(theClass, theBase) \
  const AFX_INTERFACEMAP* theClass::GetInterfaceMap() const { return GetThisInterfaceMap(); } \
  const AFX_INTERFACEMAP* PASCAL theClass::GetThisInterfaceMap() { \
    typedef theBase TheBaseClass; \
    _AFX_NO_OFFSETOF_WARNING_BEGIN \
    static const AFX_INTERFACEMAP_ENTRY _interfaceEntries[] = {

#define INTERFACE_PART(theClass, iid, localClass) {&iid, offsetof(theClass, m_x##localClass)},
#define INTERFACE_AGGREGATE(theClass, theAggr) {NULL, offsetof(theClass, theAggr)},

#define END_INTERFACE_MAP() \
      {NULL, (size_t)-1}}; \
    _AFX_NO_OFFSETOF_WARNING_END \
    static const AFX_INTERFACEMAP interfaceMap = {&TheBaseClass::GetThisInterfaceMap, \
                                                  &_interfaceEntries[0]}; \
    return &interfaceMap; \
  }

// A nested class that implements one interface. Its methods find the
// object with METHOD_PROLOGUE and forward IUnknown to it.
#define BEGIN_INTERFACE_PART(localClass, baseClass) \
  class X##localClass : public baseClass { \
  public: \
    STDMETHOD_(ULONG, AddRef)(); \
    STDMETHOD_(ULONG, Release)(); \
    STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppvObj);
#define BEGIN_INTERFACE_PART_DERIVE(localClass, baseClass) \
  class X##localClass : public baseClass { \
  public:
#define INIT_INTERFACE_PART(theClass, localClass)
#define INIT_INTERFACE_PART_DERIVE(theClass, localClass)
#define END_INTERFACE_PART(localClass) \
  } m_x##localClass; \
  friend class X##localClass;
#define END_INTERFACE_PART_OPTIONAL(localClass) END_INTERFACE_PART(localClass)
#define END_INTERFACE_PART_STATIC END_INTERFACE_PART

#define METHOD_PROLOGUE(theClass, localClass) \
  _AFX_NO_OFFSETOF_WARNING_BEGIN \
  theClass* pThis = (theClass*)((BYTE*)this - offsetof(theClass, m_x##localClass)); \
  _AFX_NO_OFFSETOF_WARNING_END \
  (void)pThis;
#define METHOD_PROLOGUE_EX(theClass, localClass) METHOD_PROLOGUE(theClass, localClass)
#define METHOD_PROLOGUE_EX_(theClass, localClass) METHOD_PROLOGUE(theClass, localClass)
#define METHOD_MANAGE_STATE(theClass, localClass) METHOD_PROLOGUE(theClass, localClass)

#define DECLARE_DISPATCH_MAP() \
protected: \
  static const AFX_DISPMAP* PASCAL GetThisDispatchMap(); \
  virtual const AFX_DISPMAP* GetDispatchMap() const;

struct AFX_DISPARGS;
class _AfxDispatchImpl : public IDispatch {
public:
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppvObj);
  STDMETHOD_(ULONG, AddRef)();
  STDMETHOD_(ULONG, Release)();
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo);
  STDMETHOD(GetTypeInfo)(UINT itinfo, LCID lcid, ITypeInfo** pptinfo);
  STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid,
                           DISPID* rgdispid);
  STDMETHOD(Invoke)(DISPID dispid, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdispparams,
                    VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr);
};

// Connection maps: the connection points (outgoing interfaces) of an
// object, each a CConnectionPoint member made by BEGIN_CONNECTION_PART.
struct AFX_CONNECTIONMAP_ENTRY {
  const void* piid;  // NULL ends the map
  size_t nOffset;
};
struct AFX_CONNECTIONMAP {
  const AFX_CONNECTIONMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_CONNECTIONMAP_ENTRY* pEntry;
};

#define DECLARE_CONNECTION_MAP() \
protected: \
  static const AFX_CONNECTIONMAP* PASCAL GetThisConnectionMap(); \
  virtual const AFX_CONNECTIONMAP* GetConnectionMap() const;

#define BEGIN_CONNECTION_MAP(theClass, theBase) \
  const AFX_CONNECTIONMAP* theClass::GetConnectionMap() const { return GetThisConnectionMap(); } \
  const AFX_CONNECTIONMAP* PASCAL theClass::GetThisConnectionMap() { \
    typedef theBase TheBaseClass; \
    _AFX_NO_OFFSETOF_WARNING_BEGIN \
    static const AFX_CONNECTIONMAP_ENTRY _connectionEntries[] = {

#define CONNECTION_PART(theClass, iid, localClass) {&iid, offsetof(theClass, m_x##localClass)},

#define END_CONNECTION_MAP() \
      {NULL, (size_t)-1}}; \
    _AFX_NO_OFFSETOF_WARNING_END \
    static const AFX_CONNECTIONMAP connectionMap = {&TheBaseClass::GetThisConnectionMap, \
                                                    &_connectionEntries[0]}; \
    return &connectionMap; \
  }

// The IConnectionPointContainer of every CCmdTarget, answered once
// EnableConnections has been called.
class _AfxConnPtContainerImpl : public IConnectionPointContainer {
public:
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppvObj);
  STDMETHOD_(ULONG, AddRef)();
  STDMETHOD_(ULONG, Release)();
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** ppEnum);
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** ppCP);
};

class CTypeLibCache;
class CConnectionPoint;

// An event, a property change or a request to change a property, from an
// ActiveX control to its container's event sink map (CN_EVENT).
// A data source control's notification (ON_DSCNOTIFY): the step of
// the change, and what the change is.
enum DSCSTATE {
  dscNoState = 0,
  dscOKToDo,
  dscCancelled,
  dscSyncBefore,
  dscAboutToDo,
  dscFailedToDo,
  dscSyncAfter,
  dscDidEvent
};
enum DSCREASON {
  dscNoReason = 0,
  dscClose,
  dscCommit,
  dscDelete,
  dscEdit,
  dscInsert,
  dscModify,
  dscMove
};

struct AFX_EVENT {
  enum { event, propRequest, propChanged, propDSCNotify };
  AFX_EVENT(int eventKind)
      : m_eventKind(eventKind),
        m_dispid(DISPID_UNKNOWN),
        m_pDispParams(NULL),
        m_pExcepInfo(NULL),
        m_puArgError(NULL),
        m_bPropChanged(FALSE),
        m_hResult(S_OK),
        m_nDSCState(dscNoState),
        m_nDSCReason(dscNoReason) {}
  AFX_EVENT(int eventKind, DISPID dispid, DISPPARAMS* pDispParams = NULL,
            EXCEPINFO* pExcepInfo = NULL, UINT* puArgError = NULL)
      : m_eventKind(eventKind),
        m_dispid(dispid),
        m_pDispParams(pDispParams),
        m_pExcepInfo(pExcepInfo),
        m_puArgError(puArgError),
        m_bPropChanged(FALSE),
        m_hResult(S_OK),
        m_nDSCState(dscNoState),
        m_nDSCReason(dscNoReason) {}
  int m_eventKind;
  DISPID m_dispid;
  DISPPARAMS* m_pDispParams;
  EXCEPINFO* m_pExcepInfo;
  UINT* m_puArgError;
  BOOL m_bPropChanged;
  HRESULT m_hResult;
  DSCSTATE m_nDSCState;
  DSCREASON m_nDSCReason;
};

// Event sink maps, filled by the ON_EVENT macros of afxdisp.h.
struct AFX_EVENTSINKMAP_ENTRY;
struct AFX_EVENTSINKMAP {
  const AFX_EVENTSINKMAP* (PASCAL* pfnGetBaseMap)();
  const AFX_EVENTSINKMAP_ENTRY* lpEntries;
};

#define DECLARE_EVENTSINK_MAP() \
protected: \
  static const AFX_EVENTSINKMAP* PASCAL GetThisEventSinkMap(); \
  virtual const AFX_EVENTSINKMAP* GetEventSinkMap() const;

class _AfxInnerUnknownImpl : public IUnknown {
public:
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppvObj);
  STDMETHOD_(ULONG, AddRef)();
  STDMETHOD_(ULONG, Release)();
};

class CCmdTarget : public CObject {
  DECLARE_DYNAMIC(CCmdTarget)
public:
  CCmdTarget();
  virtual ~CCmdTarget();

  void BeginWaitCursor();
  void EndWaitCursor();
  void RestoreWaitCursor();

  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual BOOL IsInvokeAllowed(DISPID) { return TRUE; }

  // Automation: after EnableAutomation, GetIDispatch returns an IDispatch
  // that the class's dispatch map implements.
  void EnableAutomation();
  void EnableConnections();
  void EnableTypeLib() {}
  LPDISPATCH GetIDispatch(BOOL bAddRef);
  static CCmdTarget* PASCAL FromIDispatch(LPDISPATCH lpDispatch);
  BOOL IsResultExpected() { return m_bResultExpected; }
  virtual BOOL GetDispatchIID(IID*) { return FALSE; }
  // For DISP_PROPERTY_EX entries of read-only and write-only properties.
  void GetNotSupported();
  void SetNotSupported();
  // Type information, from the type library of IMPLEMENT_OLETYPELIB.
  virtual UINT GetTypeInfoCount() { return 0; }
  virtual HRESULT GetTypeLib(LCID, LPTYPELIB* ppTypeLib) {
    *ppTypeLib = NULL;
    return TYPE_E_CANTLOADLIBRARY;
  }
  virtual CTypeLibCache* GetTypeLibCache() { return NULL; }
  virtual HRESULT GetTypeInfoOfGuid(LCID lcid, REFGUID guid, LPTYPEINFO* ppTypeInfo);

  // Connection points: those of the connection map, then the hook's and
  // the extra ones.
  virtual LPCONNECTIONPOINT GetConnectionHook(REFIID) { return NULL; }
  virtual BOOL GetExtraConnectionPoints(CPtrArray*) { return FALSE; }
  CConnectionPoint* FindConnection(REFIID iid);

  // An event from a control, through the event sink map (CN_EVENT).
  virtual BOOL OnEvent(UINT idCtrl, AFX_EVENT* pEvent, AFX_CMDHANDLERINFO* pHandlerInfo);

  // Reference counting and interfaces. The External functions go to the
  // controlling unknown when the object is aggregated.
  virtual void OnFinalRelease();
  DWORD InternalAddRef();
  DWORD InternalRelease();
  DWORD InternalQueryInterface(const void* iid, LPVOID* ppvObj);
  DWORD ExternalAddRef();
  DWORD ExternalRelease();
  DWORD ExternalQueryInterface(const void* iid, LPVOID* ppvObj);
  LPUNKNOWN GetControllingUnknown();
  LPUNKNOWN GetInterface(const void* iid);  // no AddRef
  LPUNKNOWN QueryAggregates(const void* iid);
  void EnableAggregation();
  virtual BOOL OnCreateAggregates() { return TRUE; }
  virtual LPUNKNOWN GetInterfaceHook(const void*) { return NULL; }

  long m_dwRef;
  LPUNKNOWN m_pOuterUnknown;
  _AfxInnerUnknownImpl m_xInnerUnknown;
  _AfxDispatchImpl m_xDispatch;
  _AfxConnPtContainerImpl m_xConnPtContainer;
  BOOL m_bAutomation;
  BOOL m_bConnections;
  BOOL m_bAggregation;
  BOOL m_bResultExpected;

  // Searches a message map for an entry. Used by CWnd and CWinThread.
  static const AFX_MSGMAP_ENTRY* PASCAL FindMessageEntry(const AFX_MSGMAP_ENTRY* lpEntry,
                                                         UINT nMsg, UINT nCode, UINT nID);

  // Used by the IDispatch implementation.
  const AFX_DISPMAP_ENTRY* GetDispEntry(DISPID dispid);
  HRESULT InvokeEntry(const AFX_DISPMAP_ENTRY* pEntry, WORD wFlags, DISPPARAMS* pDispParams,
                      VARIANT* pvarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr);

  friend class _AfxDispatchImpl;
  friend class _AfxConnPtContainerImpl;
  DECLARE_MESSAGE_MAP()
  DECLARE_DISPATCH_MAP()
  DECLARE_INTERFACE_MAP()
  DECLARE_CONNECTION_MAP()
  DECLARE_EVENTSINK_MAP()
};

class CCmdUI {
public:
  UINT m_nID;
  UINT m_nIndex;
  CMenu* m_pMenu;
  CMenu* m_pSubMenu;
  CWnd* m_pOther;
  CMenu* m_pParentMenu;
  BOOL m_bEnableChanged;
  BOOL m_bContinueRouting;
  UINT m_nIndexMax;

  CCmdUI();
  virtual ~CCmdUI() {}
  virtual void Enable(BOOL bOn = TRUE);
  virtual void SetCheck(int nCheck = 1);
  virtual void SetRadio(BOOL bOn = TRUE);
  virtual void SetText(LPCTSTR lpszText);
  void ContinueRouting() { m_bContinueRouting = TRUE; }
  BOOL DoUpdate(CCmdTarget* pTarget, BOOL bDisableIfNoHndler);
};

// Puts the hourglass cursor up while the object exists.
class CWaitCursor {
public:
  CWaitCursor() { AfxGetAppWait(TRUE); }
  ~CWaitCursor() { AfxGetAppWait(FALSE); }
  void Restore() { AfxGetAppWait(2); }
  static void AfxGetAppWait(int n);
};

// ---------------------------------------------------------------------
// CWnd

struct AFX_SIZEPARENTPARAMS {
  HDWP hDWP;
  RECT rect;
  SIZE sizeTotal;
  BOOL bStretch;
};

class CWnd : public CCmdTarget {
  DECLARE_DYNCREATE(CWnd)
protected:
  static const MSG* PASCAL GetCurrentMessage();

public:
  HWND m_hWnd;
  COleDropTarget* m_pDropTarget;  // revoked when the window goes
  static const CWnd wndTop;
  static const CWnd wndBottom;
  static const CWnd wndTopMost;
  static const CWnd wndNoTopMost;

  HWND GetSafeHwnd() const { return AFX_IS_NULL_THIS() ? NULL : m_hWnd; }
  operator HWND() const { return AFX_IS_NULL_THIS() ? NULL : m_hWnd; }
  BOOL operator==(const CWnd& wnd) const { return wnd.m_hWnd == m_hWnd; }
  BOOL operator!=(const CWnd& wnd) const { return wnd.m_hWnd != m_hWnd; }

  // These go to the control, for the window object of an ActiveX
  // control (winocc.cpp).
  DWORD GetStyle() const;
  DWORD GetExStyle() const;
  BOOL ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0);
  BOOL ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0);
  static BOOL PASCAL ModifyStyle(HWND hWnd, DWORD dwRemove, DWORD dwAdd, UINT nFlags);
  static BOOL PASCAL ModifyStyleEx(HWND hWnd, DWORD dwRemove, DWORD dwAdd, UINT nFlags);

  CWnd* GetOwner() const;
  void SetOwner(CWnd* pOwnerWnd) { m_hWndOwner = pOwnerWnd->GetSafeHwnd(); }

  BOOL SendChildNotifyLastMsg(LRESULT* pResult = NULL);

  // Creation
  CWnd();
  explicit CWnd(HWND hWnd);
  virtual ~CWnd();

  static CWnd* PASCAL FromHandle(HWND hWnd);
  static CWnd* PASCAL FromHandlePermanent(HWND hWnd);
  static void PASCAL DeleteTempMap();
  BOOL Attach(HWND hWndNew);
  HWND Detach();

  virtual void PreSubclassWindow();
  BOOL SubclassWindow(HWND hWnd);
  BOOL SubclassDlgItem(UINT nID, CWnd* pParent);
  HWND UnsubclassWindow();

  // Creates this window from a dialog template (dialog bars, form views).
  BOOL CreateDlg(LPCTSTR lpszTemplateName, CWnd* pParentWnd);
  BOOL CreateDlgIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd, HINSTANCE hInst);
  BOOL ExecuteDlgInit(LPCTSTR lpszResourceName);
  BOOL ExecuteDlgInit(LPVOID lpResource);

  // ActiveX controls: this object becomes a window for a control that
  // pParentWnd hosts. The control's own window does the work, and its
  // properties and methods are reached through InvokeHelper.
  BOOL CreateControl(REFCLSID clsid, LPCTSTR pszWindowName, DWORD dwStyle, const RECT& rect,
                     CWnd* pParentWnd, UINT nID, CFile* pPersist = NULL, BOOL bStorage = FALSE,
                     BSTR bstrLicKey = NULL);
  BOOL CreateControl(LPCTSTR pszClass, LPCTSTR pszWindowName, DWORD dwStyle, const RECT& rect,
                     CWnd* pParentWnd, UINT nID, CFile* pPersist = NULL, BOOL bStorage = FALSE,
                     BSTR bstrLicKey = NULL);
  BOOL CreateControl(REFCLSID clsid, LPCTSTR pszWindowName, DWORD dwStyle, const POINT* ppt,
                     const SIZE* psize, CWnd* pParentWnd, UINT nID, CFile* pPersist = NULL,
                     BOOL bStorage = FALSE, BSTR bstrLicKey = NULL);
  LPUNKNOWN GetControlUnknown();
  void AFX_CDECL InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                              const BYTE* pbParamInfo, ...);
  void AFX_CDECL SetProperty(DISPID dwDispID, VARTYPE vtProp, ...);
  void GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const;
  // Data binding (afxocc.h): this control's property dwDispID follows the
  // field szFieldName of the data source control pDSCWnd's current row,
  // and the field follows the property; BindProperty gives the property
  // the data source itself. A NULL data source control unbinds.
  void BindDefaultProperty(DISPID dwDispID, VARTYPE vtProp, LPCTSTR szFieldName, CWnd* pDSCWnd);
  void BindProperty(DISPID dwDispId, CWnd* pWndDSC);
  // A data source control's rowset, which the window keeps.
  IUnknown* GetDSCCursor();
  // Ambient properties for the controls this window hosts; FALSE for the
  // container's defaults.
  virtual BOOL OnAmbientProperty(COleControlSite* pSite, DISPID dispid, VARIANT* pvar);
  BOOL InitControlContainer();
  // Makes the site of a control this window is about to host; *ppSite
  // left NULL means the usual COleControlSite.
  virtual BOOL CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite,
                                 UINT nID, REFCLSID clsid);

  COleControlContainer* m_pCtrlCont;  // the controls this window hosts
  COleControlSite* m_pCtrlSite;       // the site, when this is a control's window
  _AFX_OCC_DIALOG_INFO* m_pOccDialogInfo;  // controls of a dialog template still to make

  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                      const RECT& rect, CWnd* pParentWnd, UINT nID,
                      CCreateContext* pContext = NULL);
  virtual BOOL CreateEx(DWORD dwExStyle, LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                        DWORD dwStyle, int x, int y, int nWidth, int nHeight,
                        HWND hWndParent, HMENU nIDorHMenu, LPVOID lpParam = NULL);
  virtual BOOL CreateEx(DWORD dwExStyle, LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                        DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID,
                        LPVOID lpParam = NULL);
  virtual BOOL DestroyWindow();

  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType = adjustBorder);
  enum AdjustType { adjustBorder = 0, adjustOutside = 1 };

  // Window tree
  CWnd* GetDlgItem(int nID) const;
  void GetDlgItem(int nID, HWND* phWnd) const { *phWnd = ::GetDlgItem(m_hWnd, nID); }
  int GetDlgCtrlID() const;
  int SetDlgCtrlID(int nID);
  CWnd* GetDescendantWindow(int nID, BOOL bOnlyPerm = FALSE) const;
  void SendMessageToDescendants(UINT message, WPARAM wParam = 0, LPARAM lParam = 0,
                                BOOL bDeep = TRUE, BOOL bOnlyPerm = FALSE);
  CFrameWnd* GetParentFrame() const;
  CFrameWnd* EnsureParentFrame() const;
  CWnd* GetTopLevelParent() const;
  CWnd* EnsureTopLevelParent() const;
  CWnd* GetTopLevelOwner() const;
  CWnd* GetParentOwner() const;
  CFrameWnd* GetTopLevelFrame() const;
  static CWnd* PASCAL GetSafeOwner(CWnd* pParent = NULL, HWND* pWndTop = NULL);

  // Message functions
  LRESULT SendMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) const {
    return ::SendMessage(m_hWnd, message, wParam, lParam);
  }
  BOOL PostMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) const {
    return ::PostMessage(m_hWnd, message, wParam, lParam);
  }
  BOOL SendNotifyMessage(UINT message, WPARAM wParam, LPARAM lParam) const {
    return ::SendNotifyMessage(m_hWnd, message, wParam, lParam);
  }
  BOOL SendChildNotifyLastMsg(LRESULT* pResult, int) { return SendChildNotifyLastMsg(pResult); }

  // Text and fonts
  void SetWindowText(LPCTSTR lpszString);
  int GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const;
  void GetWindowText(CString& rString) const;
  int GetWindowTextLength() const;
  void SetFont(CFont* pFont, BOOL bRedraw = TRUE) {
    ::SendMessage(m_hWnd, WM_SETFONT, (WPARAM)pFont->GetSafeHandle(), bRedraw);
  }
  CFont* GetFont() const {
    return CFont::FromHandle((HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0));
  }

  // Menus
  CMenu* GetMenu() const { return CMenu::FromHandle(::GetMenu(m_hWnd)); }
  BOOL SetMenu(CMenu* pMenu) { return ::SetMenu(m_hWnd, pMenu->GetSafeHmenu()); }
  void DrawMenuBar() { ::DrawMenuBar(m_hWnd); }
  CMenu* GetSystemMenu(BOOL bRevert) const {
    return CMenu::FromHandle(::GetSystemMenu(m_hWnd, bRevert));
  }
  BOOL HiliteMenuItem(CMenu* pMenu, UINT nIDHiliteItem, UINT nHilite) {
    return ::HiliteMenuItem(m_hWnd, pMenu->m_hMenu, nIDHiliteItem, nHilite);
  }

  // Window size and position
  BOOL IsIconic() const { return ::IsIconic(m_hWnd); }
  BOOL IsZoomed() const { return ::IsZoomed(m_hWnd); }
  void MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint = TRUE);
  void MoveWindow(LPCRECT lpRect, BOOL bRepaint = TRUE) {
    MoveWindow(lpRect->left, lpRect->top, lpRect->right - lpRect->left,
               lpRect->bottom - lpRect->top, bRepaint);
  }
  int SetWindowRgn(HRGN hRgn, BOOL bRedraw) { return ::SetWindowRgn(m_hWnd, hRgn, bRedraw); }
  int GetWindowRgn(HRGN hRgn) const { return ::GetWindowRgn(m_hWnd, hRgn); }
  BOOL SetWindowPos(const CWnd* pWndInsertAfter, int x, int y, int cx, int cy, UINT nFlags);
  UINT ArrangeIconicWindows() { return ::ArrangeIconicWindows(m_hWnd); }
  void BringWindowToTop() { ::BringWindowToTop(m_hWnd); }
  void GetWindowRect(LPRECT lpRect) const { ::GetWindowRect(m_hWnd, lpRect); }
  void GetClientRect(LPRECT lpRect) const { ::GetClientRect(m_hWnd, lpRect); }
  BOOL GetWindowPlacement(WINDOWPLACEMENT* lpwndpl) const {
    lpwndpl->length = sizeof(WINDOWPLACEMENT);
    return ::GetWindowPlacement(m_hWnd, lpwndpl);
  }
  BOOL SetWindowPlacement(const WINDOWPLACEMENT* lpwndpl) {
    ((WINDOWPLACEMENT*)lpwndpl)->length = sizeof(WINDOWPLACEMENT);
    return ::SetWindowPlacement(m_hWnd, lpwndpl);
  }

  // Coordinate mapping
  void ClientToScreen(LPPOINT lpPoint) const { ::ClientToScreen(m_hWnd, lpPoint); }
  void ClientToScreen(LPRECT lpRect) const {
    ::ClientToScreen(m_hWnd, (LPPOINT)lpRect);
    ::ClientToScreen(m_hWnd, ((LPPOINT)lpRect) + 1);
  }
  void ScreenToClient(LPPOINT lpPoint) const { ::ScreenToClient(m_hWnd, lpPoint); }
  void ScreenToClient(LPRECT lpRect) const {
    ::ScreenToClient(m_hWnd, (LPPOINT)lpRect);
    ::ScreenToClient(m_hWnd, ((LPPOINT)lpRect) + 1);
  }
  void MapWindowPoints(CWnd* pwndTo, LPPOINT lpPoint, UINT nCount) const {
    ::MapWindowPoints(m_hWnd, pwndTo->GetSafeHwnd(), lpPoint, nCount);
  }
  void MapWindowPoints(CWnd* pwndTo, LPRECT lpRect) const {
    ::MapWindowPoints(m_hWnd, pwndTo->GetSafeHwnd(), (LPPOINT)lpRect, 2);
  }

  // Update and painting
  CDC* BeginPaint(LPPAINTSTRUCT lpPaint) { return CDC::FromHandle(::BeginPaint(m_hWnd, lpPaint)); }
  void EndPaint(LPPAINTSTRUCT lpPaint) { ::EndPaint(m_hWnd, lpPaint); }
  CDC* GetDC() { return CDC::FromHandle(::GetDC(m_hWnd)); }
  CDC* GetWindowDC() { return CDC::FromHandle(::GetWindowDC(m_hWnd)); }
  int ReleaseDC(CDC* pDC) { return ::ReleaseDC(m_hWnd, pDC->m_hDC); }
  void Print(CDC* pDC, DWORD dwFlags) const {
    ::SendMessage(m_hWnd, WM_PRINT, (WPARAM)pDC->GetSafeHdc(), dwFlags);
  }
  void PrintClient(CDC* pDC, DWORD dwFlags) const {
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)pDC->GetSafeHdc(), dwFlags);
  }
  void UpdateWindow() { ::UpdateWindow(m_hWnd); }
  void SetRedraw(BOOL bRedraw = TRUE) { ::SendMessage(m_hWnd, WM_SETREDRAW, bRedraw, 0); }
  BOOL GetUpdateRect(LPRECT lpRect, BOOL bErase = FALSE) {
    return ::GetUpdateRect(m_hWnd, lpRect, bErase);
  }
  int GetUpdateRgn(CRgn* pRgn, BOOL bErase = FALSE) {
    return ::GetUpdateRgn(m_hWnd, (HRGN)pRgn->GetSafeHandle(), bErase);
  }
  void Invalidate(BOOL bErase = TRUE) { ::InvalidateRect(m_hWnd, NULL, bErase); }
  void InvalidateRect(LPCRECT lpRect, BOOL bErase = TRUE) { ::InvalidateRect(m_hWnd, lpRect, bErase); }
  void InvalidateRgn(CRgn* pRgn, BOOL bErase = TRUE) {
    ::InvalidateRgn(m_hWnd, (HRGN)pRgn->GetSafeHandle(), bErase);
  }
  void ValidateRect(LPCRECT lpRect) { ::ValidateRect(m_hWnd, lpRect); }
  void ValidateRgn(CRgn* pRgn) { ::ValidateRgn(m_hWnd, (HRGN)pRgn->GetSafeHandle()); }
  BOOL ShowWindow(int nCmdShow);
  BOOL IsWindowVisible() const { return ::IsWindowVisible(m_hWnd); }
  void ShowOwnedPopups(BOOL bShow = TRUE) { ::ShowOwnedPopups(m_hWnd, bShow); }
  CDC* GetDCEx(CRgn* prgnClip, DWORD flags) {
    return CDC::FromHandle(::GetDCEx(m_hWnd, (HRGN)prgnClip->GetSafeHandle(), flags));
  }
  BOOL LockWindowUpdate() { return ::LockWindowUpdate(m_hWnd); }
  void UnlockWindowUpdate() { ::LockWindowUpdate(NULL); }
  BOOL RedrawWindow(LPCRECT lpRectUpdate = NULL, CRgn* prgnUpdate = NULL,
                    UINT flags = RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE) {
    return ::RedrawWindow(m_hWnd, lpRectUpdate, (HRGN)prgnUpdate->GetSafeHandle(), flags);
  }
  BOOL EnableScrollBar(int nSBFlags, UINT nArrowFlags = ESB_ENABLE_BOTH) {
    return ::EnableScrollBar(m_hWnd, nSBFlags, nArrowFlags);
  }
  BOOL DrawAnimatedRects(int idAni, CONST RECT* lprcFrom, CONST RECT* lprcTo) {
    return ::DrawAnimatedRects(m_hWnd, idAni, lprcFrom, lprcTo);
  }
  BOOL DrawCaption(CDC* pDC, LPCRECT lprc, UINT uFlags) {
    return ::DrawCaption(m_hWnd, pDC->GetSafeHdc(), lprc, uFlags);
  }
  BOOL FlashWindow(BOOL bInvert) { return ::FlashWindow(m_hWnd, bInvert); }

  // Timers
  UINT_PTR SetTimer(UINT_PTR nIDEvent, UINT nElapse,
                    void(CALLBACK* lpfnTimer)(HWND, UINT, UINT_PTR, DWORD) = NULL) {
    return ::SetTimer(m_hWnd, nIDEvent, nElapse, (TIMERPROC)lpfnTimer);
  }
  BOOL KillTimer(UINT_PTR nIDEvent) { return ::KillTimer(m_hWnd, nIDEvent); }

  // State
  BOOL IsWindowEnabled() const;
  BOOL EnableWindow(BOOL bEnable = TRUE);
  static CWnd* PASCAL GetActiveWindow() { return CWnd::FromHandle(::GetActiveWindow()); }
  CWnd* SetActiveWindow() { return CWnd::FromHandle(::SetActiveWindow(m_hWnd)); }
  static CWnd* PASCAL GetForegroundWindow() { return CWnd::FromHandle(::GetForegroundWindow()); }
  BOOL SetForegroundWindow() { return ::SetForegroundWindow(m_hWnd); }
  static CWnd* PASCAL GetCapture() { return CWnd::FromHandle(::GetCapture()); }
  CWnd* SetCapture() { return CWnd::FromHandle(::SetCapture(m_hWnd)); }
  static CWnd* PASCAL GetFocus() { return CWnd::FromHandle(::GetFocus()); }
  CWnd* SetFocus();
  static CWnd* PASCAL GetDesktopWindow() { return CWnd::FromHandle(::GetDesktopWindow()); }

  // Dialog items
  void CheckDlgButton(int nIDButton, UINT nCheck) { ::CheckDlgButton(m_hWnd, nIDButton, nCheck); }
  void CheckRadioButton(int nIDFirstButton, int nIDLastButton, int nIDCheckButton) {
    ::CheckRadioButton(m_hWnd, nIDFirstButton, nIDLastButton, nIDCheckButton);
  }
  int GetCheckedRadioButton(int nIDFirstButton, int nIDLastButton);
  int DlgDirList(LPTSTR lpPathSpec, int nIDListBox, int nIDStaticPath, UINT nFileType) {
    return ::DlgDirList(m_hWnd, lpPathSpec, nIDListBox, nIDStaticPath, nFileType);
  }
  int DlgDirListComboBox(LPTSTR lpPathSpec, int nIDComboBox, int nIDStaticPath, UINT nFileType) {
    return ::DlgDirListComboBox(m_hWnd, lpPathSpec, nIDComboBox, nIDStaticPath, nFileType);
  }
  BOOL DlgDirSelect(LPTSTR lpString, int nCount, int nIDListBox) {
    return ::DlgDirSelectEx(m_hWnd, lpString, nCount, nIDListBox);
  }
  BOOL DlgDirSelectComboBox(LPTSTR lpString, int nCount, int nIDComboBox) {
    return ::DlgDirSelectComboBoxEx(m_hWnd, lpString, nCount, nIDComboBox);
  }
  UINT GetDlgItemInt(int nID, BOOL* lpTrans = NULL, BOOL bSigned = TRUE) const;
  int GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const;
  int GetDlgItemText(int nID, CString& rString) const;
  CWnd* GetNextDlgGroupItem(CWnd* pWndCtl, BOOL bPrevious = FALSE) const {
    return CWnd::FromHandle(::GetNextDlgGroupItem(m_hWnd, pWndCtl->GetSafeHwnd(), bPrevious));
  }
  CWnd* GetNextDlgTabItem(CWnd* pWndCtl, BOOL bPrevious = FALSE) const {
    return CWnd::FromHandle(::GetNextDlgTabItem(m_hWnd, pWndCtl->GetSafeHwnd(), bPrevious));
  }
  UINT IsDlgButtonChecked(int nIDButton) const { return ::IsDlgButtonChecked(m_hWnd, nIDButton); }
  LRESULT SendDlgItemMessage(int nID, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::SendDlgItemMessage(m_hWnd, nID, message, wParam, lParam);
  }
  void SetDlgItemInt(int nID, UINT nValue, BOOL bSigned = TRUE);
  void SetDlgItemText(int nID, LPCTSTR lpszString);

  // Scrolling
  // The position and range calls go to GetScrollBarCtrl's control when
  // there is one (a splitter's shared scroll bars).
  int GetScrollPos(int nBar) const;
  void GetScrollRange(int nBar, LPINT lpMinPos, LPINT lpMaxPos) const;
  void ScrollWindow(int xAmount, int yAmount, LPCRECT lpRect = NULL, LPCRECT lpClipRect = NULL) {
    ::ScrollWindow(m_hWnd, xAmount, yAmount, lpRect, lpClipRect);
  }
  int SetScrollPos(int nBar, int nPos, BOOL bRedraw = TRUE);
  void SetScrollRange(int nBar, int nMinPos, int nMaxPos, BOOL bRedraw = TRUE);
  void ShowScrollBar(UINT nBar, BOOL bShow = TRUE) { ::ShowScrollBar(m_hWnd, nBar, bShow); }
  void EnableScrollBarCtrl(int nBar, BOOL bEnable = TRUE);
  virtual CScrollBar* GetScrollBarCtrl(int nBar) const;
  int ScrollWindowEx(int dx, int dy, LPCRECT lpRectScroll, LPCRECT lpRectClip, CRgn* prgnUpdate,
                     LPRECT lpRectUpdate, UINT flags) {
    return ::ScrollWindowEx(m_hWnd, dx, dy, lpRectScroll, lpRectClip,
                            (HRGN)prgnUpdate->GetSafeHandle(), lpRectUpdate, flags);
  }
  BOOL SetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, BOOL bRedraw = TRUE);
  BOOL GetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, UINT nMask = SIF_ALL);
  int GetScrollLimit(int nBar);

  // Window access
  CWnd* ChildWindowFromPoint(POINT point) const {
    return CWnd::FromHandle(::ChildWindowFromPoint(m_hWnd, point));
  }
  CWnd* ChildWindowFromPoint(POINT point, UINT nFlags) const {
    return CWnd::FromHandle(::ChildWindowFromPointEx(m_hWnd, point, nFlags));
  }
  static CWnd* PASCAL FindWindow(LPCTSTR lpszClassName, LPCTSTR lpszWindowName) {
    return CWnd::FromHandle(::FindWindow(lpszClassName, lpszWindowName));
  }
  static CWnd* FindWindowEx(HWND hwndParent, HWND hwndChildAfter, LPCTSTR lpszClass,
                            LPCTSTR lpszWindow) {
    return CWnd::FromHandle(::FindWindowEx(hwndParent, hwndChildAfter, lpszClass, lpszWindow));
  }
  CWnd* GetNextWindow(UINT nFlag = GW_HWNDNEXT) const {
    return CWnd::FromHandle(::GetWindow(m_hWnd, nFlag));
  }
  CWnd* GetTopWindow() const { return CWnd::FromHandle(::GetTopWindow(m_hWnd)); }
  CWnd* GetWindow(UINT nCmd) const { return CWnd::FromHandle(::GetWindow(m_hWnd, nCmd)); }
  CWnd* GetLastActivePopup() const { return CWnd::FromHandle(::GetLastActivePopup(m_hWnd)); }
  BOOL IsChild(const CWnd* pWnd) const { return ::IsChild(m_hWnd, pWnd->GetSafeHwnd()); }
  CWnd* GetParent() const { return CWnd::FromHandle(::GetParent(m_hWnd)); }
  CWnd* SetParent(CWnd* pWndNewParent) {
    return CWnd::FromHandle(::SetParent(m_hWnd, pWndNewParent->GetSafeHwnd()));
  }
  static CWnd* PASCAL WindowFromPoint(POINT point) { return CWnd::FromHandle(::WindowFromPoint(point)); }

  // Alerts
  BOOL FlashWindowEx(DWORD dwFlags, UINT uCount, DWORD dwTimeout);
  int MessageBox(LPCTSTR lpszText, LPCTSTR lpszCaption = NULL, UINT nType = MB_OK);

  // Clipboard
  BOOL ChangeClipboardChain(HWND hWndNext) { return ::ChangeClipboardChain(m_hWnd, hWndNext); }
  HWND SetClipboardViewer() { return ::SetClipboardViewer(m_hWnd); }
  BOOL OpenClipboard() { return ::OpenClipboard(m_hWnd); }
  static CWnd* PASCAL GetClipboardOwner() { return CWnd::FromHandle(::GetClipboardOwner()); }
  static CWnd* PASCAL GetClipboardViewer() { return CWnd::FromHandle(::GetClipboardViewer()); }
  static CWnd* PASCAL GetOpenClipboardWindow() { return CWnd::FromHandle(::GetOpenClipboardWindow()); }

  // Caret
  void CreateCaret(CBitmap* pBitmap) { ::CreateCaret(m_hWnd, (HBITMAP)pBitmap->GetSafeHandle(), 0, 0); }
  void CreateSolidCaret(int nWidth, int nHeight) { ::CreateCaret(m_hWnd, (HBITMAP)0, nWidth, nHeight); }
  void CreateGrayCaret(int nWidth, int nHeight) { ::CreateCaret(m_hWnd, (HBITMAP)1, nWidth, nHeight); }
  static CPoint PASCAL GetCaretPos() { POINT p; ::GetCaretPos(&p); return p; }
  static void PASCAL SetCaretPos(POINT point) { ::SetCaretPos(point.x, point.y); }
  void HideCaret() { ::HideCaret(m_hWnd); }
  void ShowCaret() { ::ShowCaret(m_hWnd); }

  // Shell
  void DragAcceptFiles(BOOL bAccept = TRUE) { ::DragAcceptFiles(m_hWnd, bAccept); }

  // Icons
  HICON SetIcon(HICON hIcon, BOOL bBigIcon) {
    return (HICON)::SendMessage(m_hWnd, WM_SETICON, bBigIcon, (LPARAM)hIcon);
  }
  HICON GetIcon(BOOL bBigIcon) const { return (HICON)::SendMessage(m_hWnd, WM_GETICON, bBigIcon, 0); }

  // Context help
  BOOL SetWindowContextHelpId(DWORD dwContextHelpId) {
    return ::SetWindowContextHelpId(m_hWnd, dwContextHelpId);
  }
  DWORD GetWindowContextHelpId() const { return ::GetWindowContextHelpId(m_hWnd); }

  // Dialog data
  BOOL UpdateData(BOOL bSaveAndValidate = TRUE);
  virtual void DoDataExchange(CDataExchange* pDX);
  void UpdateDialogControls(CCmdTarget* pTarget, BOOL bDisableIfNoHndler);
  BOOL IsDialogMessage(LPMSG lpMsg) { return ::IsDialogMessage(m_hWnd, lpMsg); }

  // Tooltips (handled by the common controls; not implemented here)
  BOOL EnableToolTips(BOOL bEnable = TRUE) { (void)bEnable; return FALSE; }
  BOOL EnableTrackingToolTips(BOOL bEnable = TRUE) { (void)bEnable; return FALSE; }
  static void PASCAL CancelToolTips(BOOL bKeys = FALSE) { (void)bKeys; }
  void FilterToolTipMessage(MSG*) {}
  virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const;

  // Layout of control bars and the client area
  enum RepositionFlags { reposDefault = 0, reposQuery = 1, reposExtra = 2, reposNoPosLeftOver = 0x8000 };
  void RepositionBars(UINT nIDFirst, UINT nIDLast, UINT nIDLeftOver, UINT nFlag = reposDefault,
                      LPRECT lpRectParam = NULL, LPCRECT lpRectClient = NULL,
                      BOOL bStretch = TRUE);

  void CenterWindow(CWnd* pAlternateOwner = NULL);
  int RunModalLoop(DWORD dwFlags = 0);
  virtual BOOL ContinueModal();
  virtual void EndModalLoop(int nResult);

  // Window procedure and overridables
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual WNDPROC* GetSuperWndProcAddr();
  virtual void WinHelp(DWORD_PTR dwData, UINT nCmd = HELP_CONTEXT);
  virtual void HtmlHelp(DWORD_PTR dwData, UINT nCmd = 0x000F);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
  virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
  virtual void PostNcDestroy();
  virtual BOOL OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual BOOL CheckAutoCenter() { return TRUE; }
  virtual BOOL IsFrameWnd() const { return FALSE; }
  virtual void OnFinalRelease();
  virtual BOOL OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);

  LRESULT Default();
  BOOL ReflectChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  static BOOL PASCAL ReflectLastMsg(HWND hWndChild, LRESULT* pResult = NULL);
  static BOOL PASCAL WalkPreTranslateTree(HWND hWndStop, MSG* pMsg);
  static void PASCAL SendMessageToDescendants(HWND hWnd, UINT message, WPARAM wParam,
                                              LPARAM lParam, BOOL bDeep, BOOL bOnlyPerm);

  // The handlers for every ON_WM_ macro, which do the default processing.
#include <afxwnd_handlers_.h>

  afx_msg LRESULT OnDisplayChange(WPARAM, LPARAM);
  afx_msg LRESULT OnDragList(WPARAM, LPARAM);

  WNDPROC m_pfnSuper;
  HWND m_hWndOwner;
  UINT m_nFlags;
  int m_nModalResult;

  HWND GetSafeInsertAfter() const { return GetSafeHwnd(); }

  friend LRESULT CALLBACK AfxWndProc(HWND, UINT, WPARAM, LPARAM);
  friend class CWinThread;

  DECLARE_MESSAGE_MAP()
};

// m_nFlags
#define WF_TOOLTIPS 0x0001
#define WF_TEMPHIDE 0x0002
#define WF_STAYDISABLED 0x0004
#define WF_MODALLOOP 0x0008
#define WF_CONTINUEMODAL 0x0010
#define WF_OLECTLCONTAINER 0x0100
#define WF_TRACKINGTOOLTIPS 0x0400
#define WF_STAYACTIVE 0x0020
#define WF_NOPOPMSG 0x0040
#define WF_MODALDISABLE 0x0080
#define WF_KEEPMINIACTIVE 0x0200
#define WF_NOWIN32ISDIALOGMSG 0x0800
#define WF_ISWINFORMSVIEWWND 0x1000

// RunModalLoop flags
#define MLF_NOIDLEMSG 0x0001
#define MLF_NOKICKIDLE 0x0002
#define MLF_SHOWONIDLE 0x0004

LRESULT CALLBACK AfxWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT AFXAPI AfxCallWndProc(CWnd* pWnd, HWND hWnd, UINT nMsg, WPARAM wParam = 0,
                              LPARAM lParam = 0);
WNDPROC AFXAPI AfxGetAfxWndProc();
void AFXAPI AfxHookWindowCreate(CWnd* pWnd);
BOOL AFXAPI AfxUnhookWindowCreate();

// ---------------------------------------------------------------------
// Controls

class CStatic : public CWnd {
  DECLARE_DYNAMIC(CStatic)
public:
  CStatic() {}
  virtual BOOL Create(LPCTSTR lpszText, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                      UINT nID = 0xffff);
  HICON SetIcon(HICON hIcon) { return (HICON)::SendMessage(m_hWnd, STM_SETICON, (WPARAM)hIcon, 0); }
  HICON GetIcon() const { return (HICON)::SendMessage(m_hWnd, STM_GETICON, 0, 0); }
  HENHMETAFILE SetEnhMetaFile(HENHMETAFILE hMetaFile) {
    return (HENHMETAFILE)::SendMessage(m_hWnd, STM_SETIMAGE, IMAGE_ENHMETAFILE, (LPARAM)hMetaFile);
  }
  HENHMETAFILE GetEnhMetaFile() const {
    return (HENHMETAFILE)::SendMessage(m_hWnd, STM_GETIMAGE, IMAGE_ENHMETAFILE, 0);
  }
  HBITMAP SetBitmap(HBITMAP hBitmap) {
    return (HBITMAP)::SendMessage(m_hWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBitmap);
  }
  HBITMAP GetBitmap() const { return (HBITMAP)::SendMessage(m_hWnd, STM_GETIMAGE, IMAGE_BITMAP, 0); }
  HCURSOR SetCursor(HCURSOR hCursor) {
    return (HCURSOR)::SendMessage(m_hWnd, STM_SETIMAGE, IMAGE_CURSOR, (LPARAM)hCursor);
  }
  HCURSOR GetCursor() { return (HCURSOR)::SendMessage(m_hWnd, STM_GETIMAGE, IMAGE_CURSOR, 0); }
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CButton : public CWnd {
  DECLARE_DYNAMIC(CButton)
public:
  CButton() {}
  virtual BOOL Create(LPCTSTR lpszCaption, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                      UINT nID);
  UINT GetState() const { return (UINT)::SendMessage(m_hWnd, BM_GETSTATE, 0, 0); }
  void SetState(BOOL bHighlight) { ::SendMessage(m_hWnd, BM_SETSTATE, bHighlight, 0); }
  int GetCheck() const { return (int)::SendMessage(m_hWnd, BM_GETCHECK, 0, 0); }
  void SetCheck(int nCheck) { ::SendMessage(m_hWnd, BM_SETCHECK, nCheck, 0); }
  UINT GetButtonStyle() const { return (UINT)GetWindowLong(m_hWnd, GWL_STYLE) & 0xff; }
  void SetButtonStyle(UINT nStyle, BOOL bRedraw = TRUE) {
    ::SendMessage(m_hWnd, BM_SETSTYLE, nStyle, (LPARAM)bRedraw);
  }
  HICON SetIcon(HICON hIcon) {
    return (HICON)::SendMessage(m_hWnd, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
  }
  HICON GetIcon() const { return (HICON)::SendMessage(m_hWnd, BM_GETIMAGE, IMAGE_ICON, 0); }
  HBITMAP SetBitmap(HBITMAP hBitmap) {
    return (HBITMAP)::SendMessage(m_hWnd, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBitmap);
  }
  HBITMAP GetBitmap() const { return (HBITMAP)::SendMessage(m_hWnd, BM_GETIMAGE, IMAGE_BITMAP, 0); }
  HCURSOR SetCursor(HCURSOR hCursor) {
    return (HCURSOR)::SendMessage(m_hWnd, BM_SETIMAGE, IMAGE_CURSOR, (LPARAM)hCursor);
  }
  HCURSOR GetCursor() { return (HCURSOR)::SendMessage(m_hWnd, BM_GETIMAGE, IMAGE_CURSOR, 0); }
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CListBox : public CWnd {
  DECLARE_DYNAMIC(CListBox)
public:
  CListBox() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  int GetCount() const { return (int)::SendMessage(m_hWnd, LB_GETCOUNT, 0, 0); }
  int GetHorizontalExtent() const { return (int)::SendMessage(m_hWnd, LB_GETHORIZONTALEXTENT, 0, 0); }
  void SetHorizontalExtent(int cxExtent) { ::SendMessage(m_hWnd, LB_SETHORIZONTALEXTENT, cxExtent, 0); }
  int GetTopIndex() const { return (int)::SendMessage(m_hWnd, LB_GETTOPINDEX, 0, 0); }
  int SetTopIndex(int nIndex) { return (int)::SendMessage(m_hWnd, LB_SETTOPINDEX, nIndex, 0); }
  LCID GetLocale() const { return (LCID)::SendMessage(m_hWnd, LB_GETLOCALE, 0, 0); }
  LCID SetLocale(LCID nNewLocale) { return (LCID)::SendMessage(m_hWnd, LB_SETLOCALE, nNewLocale, 0); }
  int InitStorage(int nItems, UINT nBytes) {
    return (int)::SendMessage(m_hWnd, LB_INITSTORAGE, nItems, nBytes);
  }
  UINT ItemFromPoint(CPoint pt, BOOL& bOutside) const {
    DWORD dw = (DWORD)::SendMessage(m_hWnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    bOutside = !!HIWORD(dw);
    return LOWORD(dw);
  }
  int GetCurSel() const { return (int)::SendMessage(m_hWnd, LB_GETCURSEL, 0, 0); }
  int SetCurSel(int nSelect) { return (int)::SendMessage(m_hWnd, LB_SETCURSEL, nSelect, 0); }
  int GetSel(int nIndex) const { return (int)::SendMessage(m_hWnd, LB_GETSEL, nIndex, 0); }
  int SetSel(int nIndex, BOOL bSelect = TRUE) {
    return (int)::SendMessage(m_hWnd, LB_SETSEL, bSelect, nIndex);
  }
  int GetSelCount() const { return (int)::SendMessage(m_hWnd, LB_GETSELCOUNT, 0, 0); }
  int GetSelItems(int nMaxItems, LPINT rgIndex) const {
    return (int)::SendMessage(m_hWnd, LB_GETSELITEMS, nMaxItems, (LPARAM)rgIndex);
  }
  void SetAnchorIndex(int nIndex) { ::SendMessage(m_hWnd, LB_SETANCHORINDEX, nIndex, 0); }
  int GetAnchorIndex() const { return (int)::SendMessage(m_hWnd, LB_GETANCHORINDEX, 0, 0); }
  DWORD_PTR GetItemData(int nIndex) const {
    return (DWORD_PTR)::SendMessage(m_hWnd, LB_GETITEMDATA, nIndex, 0);
  }
  int SetItemData(int nIndex, DWORD_PTR dwItemData) {
    return (int)::SendMessage(m_hWnd, LB_SETITEMDATA, nIndex, (LPARAM)dwItemData);
  }
  void* GetItemDataPtr(int nIndex) const { return (LPVOID)GetItemData(nIndex); }
  int SetItemDataPtr(int nIndex, void* pData) { return SetItemData(nIndex, (DWORD_PTR)pData); }
  int GetItemRect(int nIndex, LPRECT lpRect) const {
    return (int)::SendMessage(m_hWnd, LB_GETITEMRECT, nIndex, (LPARAM)lpRect);
  }
  int GetText(int nIndex, LPTSTR lpszBuffer) const {
    return (int)::SendMessage(m_hWnd, LB_GETTEXT, nIndex, (LPARAM)lpszBuffer);
  }
  void GetText(int nIndex, CString& rString) const;
  int GetTextLen(int nIndex) const { return (int)::SendMessage(m_hWnd, LB_GETTEXTLEN, nIndex, 0); }
  void SetColumnWidth(int cxWidth) { ::SendMessage(m_hWnd, LB_SETCOLUMNWIDTH, cxWidth, 0); }
  BOOL SetTabStops(int nTabStops, LPINT rgTabStops) {
    return (BOOL)::SendMessage(m_hWnd, LB_SETTABSTOPS, nTabStops, (LPARAM)rgTabStops);
  }
  void SetTabStops() { ::SendMessage(m_hWnd, LB_SETTABSTOPS, 0, 0); }
  BOOL SetTabStops(const int& cxEachStop) {
    return (BOOL)::SendMessage(m_hWnd, LB_SETTABSTOPS, 1, (LPARAM)(LPINT)&cxEachStop);
  }
  int SetItemHeight(int nIndex, UINT cyItemHeight) {
    return (int)::SendMessage(m_hWnd, LB_SETITEMHEIGHT, nIndex, MAKELONG(cyItemHeight, 0));
  }
  int GetItemHeight(int nIndex) const { return (int)::SendMessage(m_hWnd, LB_GETITEMHEIGHT, nIndex, 0L); }
  int FindStringExact(int nIndexStart, LPCTSTR lpszFind) const {
    return (int)::SendMessage(m_hWnd, LB_FINDSTRINGEXACT, nIndexStart, (LPARAM)lpszFind);
  }
  int GetCaretIndex() const { return (int)::SendMessage(m_hWnd, LB_GETCARETINDEX, 0, 0L); }
  int SetCaretIndex(int nIndex, BOOL bScroll = TRUE) {
    return (int)::SendMessage(m_hWnd, LB_SETCARETINDEX, nIndex, MAKELONG(bScroll, 0));
  }
  int AddString(LPCTSTR lpszItem) { return (int)::SendMessage(m_hWnd, LB_ADDSTRING, 0, (LPARAM)lpszItem); }
  int DeleteString(UINT nIndex) { return (int)::SendMessage(m_hWnd, LB_DELETESTRING, nIndex, 0); }
  int InsertString(int nIndex, LPCTSTR lpszItem) {
    return (int)::SendMessage(m_hWnd, LB_INSERTSTRING, nIndex, (LPARAM)lpszItem);
  }
  void ResetContent() { ::SendMessage(m_hWnd, LB_RESETCONTENT, 0, 0); }
  int Dir(UINT attr, LPCTSTR lpszWildCard) {
    return (int)::SendMessage(m_hWnd, LB_DIR, attr, (LPARAM)lpszWildCard);
  }
  int FindString(int nStartAfter, LPCTSTR lpszItem) const {
    return (int)::SendMessage(m_hWnd, LB_FINDSTRING, nStartAfter, (LPARAM)lpszItem);
  }
  int SelectString(int nStartAfter, LPCTSTR lpszItem) {
    return (int)::SendMessage(m_hWnd, LB_SELECTSTRING, nStartAfter, (LPARAM)lpszItem);
  }
  int SelItemRange(BOOL bSelect, int nFirstItem, int nLastItem) {
    return bSelect ? (int)::SendMessage(m_hWnd, LB_SELITEMRANGEEX, nFirstItem, nLastItem)
                   : (int)::SendMessage(m_hWnd, LB_SELITEMRANGEEX, nLastItem, nFirstItem);
  }

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
  virtual int CompareItem(LPCOMPAREITEMSTRUCT lpCompareItemStruct);
  virtual void DeleteItem(LPDELETEITEMSTRUCT lpDeleteItemStruct);
  virtual int VKeyToItem(UINT nKey, UINT nIndex);
  virtual int CharToItem(UINT nKey, UINT nIndex);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CComboBox : public CWnd {
  DECLARE_DYNAMIC(CComboBox)
public:
  CComboBox() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  int GetCount() const { return (int)::SendMessage(m_hWnd, CB_GETCOUNT, 0, 0); }
  int GetCurSel() const { return (int)::SendMessage(m_hWnd, CB_GETCURSEL, 0, 0); }
  int SetCurSel(int nSelect) { return (int)::SendMessage(m_hWnd, CB_SETCURSEL, nSelect, 0); }
  LCID GetLocale() const { return (LCID)::SendMessage(m_hWnd, CB_GETLOCALE, 0, 0); }
  LCID SetLocale(LCID nNewLocale) { return (LCID)::SendMessage(m_hWnd, CB_SETLOCALE, nNewLocale, 0); }
  int GetTopIndex() const { return (int)::SendMessage(m_hWnd, CB_GETTOPINDEX, 0, 0); }
  int SetTopIndex(int nIndex) { return (int)::SendMessage(m_hWnd, CB_SETTOPINDEX, nIndex, 0); }
  int InitStorage(int nItems, UINT nBytes) {
    return (int)::SendMessage(m_hWnd, CB_INITSTORAGE, nItems, nBytes);
  }
  void SetHorizontalExtent(UINT nExtent) { ::SendMessage(m_hWnd, CB_SETHORIZONTALEXTENT, nExtent, 0); }
  UINT GetHorizontalExtent() const { return (UINT)::SendMessage(m_hWnd, CB_GETHORIZONTALEXTENT, 0, 0); }
  int SetDroppedWidth(UINT nWidth) { return (int)::SendMessage(m_hWnd, CB_SETDROPPEDWIDTH, nWidth, 0); }
  int GetDroppedWidth() const { return (int)::SendMessage(m_hWnd, CB_GETDROPPEDWIDTH, 0, 0); }
  DWORD GetEditSel() const { return (DWORD)::SendMessage(m_hWnd, CB_GETEDITSEL, 0, 0); }
  BOOL LimitText(int nMaxChars) { return (BOOL)::SendMessage(m_hWnd, CB_LIMITTEXT, nMaxChars, 0); }
  BOOL SetEditSel(int nStartChar, int nEndChar) {
    return (BOOL)::SendMessage(m_hWnd, CB_SETEDITSEL, 0, MAKELONG(nStartChar, nEndChar));
  }
  DWORD_PTR GetItemData(int nIndex) const {
    return (DWORD_PTR)::SendMessage(m_hWnd, CB_GETITEMDATA, nIndex, 0);
  }
  int SetItemData(int nIndex, DWORD_PTR dwItemData) {
    return (int)::SendMessage(m_hWnd, CB_SETITEMDATA, nIndex, (LPARAM)dwItemData);
  }
  void* GetItemDataPtr(int nIndex) const { return (LPVOID)GetItemData(nIndex); }
  int SetItemDataPtr(int nIndex, void* pData) { return SetItemData(nIndex, (DWORD_PTR)pData); }
  int GetLBText(int nIndex, LPTSTR lpszText) const {
    return (int)::SendMessage(m_hWnd, CB_GETLBTEXT, nIndex, (LPARAM)lpszText);
  }
  void GetLBText(int nIndex, CString& rString) const;
  int GetLBTextLen(int nIndex) const { return (int)::SendMessage(m_hWnd, CB_GETLBTEXTLEN, nIndex, 0); }
  int SetItemHeight(int nIndex, UINT cyItemHeight) {
    return (int)::SendMessage(m_hWnd, CB_SETITEMHEIGHT, nIndex, MAKELONG(cyItemHeight, 0));
  }
  int GetItemHeight(int nIndex) const { return (int)::SendMessage(m_hWnd, CB_GETITEMHEIGHT, nIndex, 0L); }
  int FindStringExact(int nIndexStart, LPCTSTR lpszFind) const {
    return (int)::SendMessage(m_hWnd, CB_FINDSTRINGEXACT, nIndexStart, (LPARAM)lpszFind);
  }
  int SetExtendedUI(BOOL bExtended = TRUE) {
    return (int)::SendMessage(m_hWnd, CB_SETEXTENDEDUI, bExtended, 0L);
  }
  BOOL GetExtendedUI() const { return (BOOL)::SendMessage(m_hWnd, CB_GETEXTENDEDUI, 0, 0L); }
  void GetDroppedControlRect(LPRECT lprect) const {
    ::SendMessage(m_hWnd, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)lprect);
  }
  BOOL GetDroppedState() const { return (BOOL)::SendMessage(m_hWnd, CB_GETDROPPEDSTATE, 0, 0L); }
  void ShowDropDown(BOOL bShowIt = TRUE) { ::SendMessage(m_hWnd, CB_SHOWDROPDOWN, bShowIt, 0); }
  int AddString(LPCTSTR lpszString) { return (int)::SendMessage(m_hWnd, CB_ADDSTRING, 0, (LPARAM)lpszString); }
  int DeleteString(UINT nIndex) { return (int)::SendMessage(m_hWnd, CB_DELETESTRING, nIndex, 0); }
  int InsertString(int nIndex, LPCTSTR lpszString) {
    return (int)::SendMessage(m_hWnd, CB_INSERTSTRING, nIndex, (LPARAM)lpszString);
  }
  void ResetContent() { ::SendMessage(m_hWnd, CB_RESETCONTENT, 0, 0); }
  int Dir(UINT attr, LPCTSTR lpszWildCard) {
    return (int)::SendMessage(m_hWnd, CB_DIR, attr, (LPARAM)lpszWildCard);
  }
  int FindString(int nStartAfter, LPCTSTR lpszString) const {
    return (int)::SendMessage(m_hWnd, CB_FINDSTRING, nStartAfter, (LPARAM)lpszString);
  }
  int SelectString(int nStartAfter, LPCTSTR lpszString) {
    return (int)::SendMessage(m_hWnd, CB_SELECTSTRING, nStartAfter, (LPARAM)lpszString);
  }
  void Clear() { ::SendMessage(m_hWnd, WM_CLEAR, 0, 0); }
  void Copy() { ::SendMessage(m_hWnd, WM_COPY, 0, 0); }
  void Cut() { ::SendMessage(m_hWnd, WM_CUT, 0, 0); }
  void Paste() { ::SendMessage(m_hWnd, WM_PASTE, 0, 0); }

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
  virtual int CompareItem(LPCOMPAREITEMSTRUCT lpCompareItemStruct);
  virtual void DeleteItem(LPDELETEITEMSTRUCT lpDeleteItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CEdit : public CWnd {
  DECLARE_DYNAMIC(CEdit)
public:
  CEdit() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  BOOL CanUndo() const { return (BOOL)::SendMessage(m_hWnd, EM_CANUNDO, 0, 0); }
  int GetLineCount() const { return (int)::SendMessage(m_hWnd, EM_GETLINECOUNT, 0, 0); }
  BOOL GetModify() const { return (BOOL)::SendMessage(m_hWnd, EM_GETMODIFY, 0, 0); }
  void SetModify(BOOL bModified = TRUE) { ::SendMessage(m_hWnd, EM_SETMODIFY, bModified, 0); }
  void GetRect(LPRECT lpRect) const { ::SendMessage(m_hWnd, EM_GETRECT, 0, (LPARAM)lpRect); }
  DWORD GetSel() const { return (DWORD)::SendMessage(m_hWnd, EM_GETSEL, 0, 0); }
  void GetSel(int& nStartChar, int& nEndChar) const {
    ::SendMessage(m_hWnd, EM_GETSEL, (WPARAM)&nStartChar, (LPARAM)&nEndChar);
  }
  HLOCAL GetHandle() const { return (HLOCAL)::SendMessage(m_hWnd, EM_GETHANDLE, 0, 0); }
  void SetHandle(HLOCAL hBuffer) { ::SendMessage(m_hWnd, EM_SETHANDLE, (WPARAM)hBuffer, 0); }
  void SetMargins(UINT nLeft, UINT nRight) {
    ::SendMessage(m_hWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(nLeft, nRight));
  }
  DWORD GetMargins() const { return (DWORD)::SendMessage(m_hWnd, EM_GETMARGINS, 0, 0); }
  void SetLimitText(UINT nMax) { ::SendMessage(m_hWnd, EM_SETLIMITTEXT, nMax, 0); }
  UINT GetLimitText() const { return (UINT)::SendMessage(m_hWnd, EM_GETLIMITTEXT, 0, 0); }
  CPoint PosFromChar(UINT nChar) const {
    return CPoint((LPARAM)(DWORD)::SendMessage(m_hWnd, EM_POSFROMCHAR, nChar, 0));
  }
  int CharFromPos(CPoint pt) const {
    return (int)::SendMessage(m_hWnd, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));
  }
  int GetLine(int nIndex, LPTSTR lpszBuffer) const {
    return (int)::SendMessage(m_hWnd, EM_GETLINE, nIndex, (LPARAM)lpszBuffer);
  }
  int GetLine(int nIndex, LPTSTR lpszBuffer, int nMaxLength) const {
    *(LPWORD)lpszBuffer = (WORD)nMaxLength;
    return (int)::SendMessage(m_hWnd, EM_GETLINE, nIndex, (LPARAM)lpszBuffer);
  }
  void EmptyUndoBuffer() { ::SendMessage(m_hWnd, EM_EMPTYUNDOBUFFER, 0, 0); }
  BOOL FmtLines(BOOL bAddEOL) { return (BOOL)::SendMessage(m_hWnd, EM_FMTLINES, bAddEOL, 0); }
  void LimitText(int nChars = 0) { ::SendMessage(m_hWnd, EM_LIMITTEXT, nChars, 0); }
  int LineFromChar(int nIndex = -1) const {
    return (int)::SendMessage(m_hWnd, EM_LINEFROMCHAR, nIndex, 0);
  }
  int LineIndex(int nLine = -1) const { return (int)::SendMessage(m_hWnd, EM_LINEINDEX, nLine, 0); }
  int LineLength(int nLine = -1) const { return (int)::SendMessage(m_hWnd, EM_LINELENGTH, nLine, 0); }
  void LineScroll(int nLines, int nChars = 0) { ::SendMessage(m_hWnd, EM_LINESCROLL, nChars, nLines); }
  void ReplaceSel(LPCTSTR lpszNewText, BOOL bCanUndo = FALSE) {
    ::SendMessage(m_hWnd, EM_REPLACESEL, (WPARAM)bCanUndo, (LPARAM)lpszNewText);
  }
  void SetPasswordChar(TCHAR ch) { ::SendMessage(m_hWnd, EM_SETPASSWORDCHAR, ch, 0); }
  void SetRect(LPCRECT lpRect) { ::SendMessage(m_hWnd, EM_SETRECT, 0, (LPARAM)lpRect); }
  void SetRectNP(LPCRECT lpRect) { ::SendMessage(m_hWnd, EM_SETRECTNP, 0, (LPARAM)lpRect); }
  void SetSel(DWORD dwSelection, BOOL bNoScroll = FALSE) {
    ::SendMessage(m_hWnd, EM_SETSEL, LOWORD(dwSelection), HIWORD(dwSelection));
    if (!bNoScroll) ::SendMessage(m_hWnd, EM_SCROLLCARET, 0, 0);
  }
  void SetSel(int nStartChar, int nEndChar, BOOL bNoScroll = FALSE) {
    ::SendMessage(m_hWnd, EM_SETSEL, nStartChar, nEndChar);
    if (!bNoScroll) ::SendMessage(m_hWnd, EM_SCROLLCARET, 0, 0);
  }
  BOOL SetTabStops(int nTabStops, LPINT rgTabStops) {
    return (BOOL)::SendMessage(m_hWnd, EM_SETTABSTOPS, nTabStops, (LPARAM)rgTabStops);
  }
  void SetTabStops() { ::SendMessage(m_hWnd, EM_SETTABSTOPS, 0, 0); }
  BOOL SetTabStops(const int& cxEachStop) {
    return (BOOL)::SendMessage(m_hWnd, EM_SETTABSTOPS, 1, (LPARAM)(LPINT)&cxEachStop);
  }
  BOOL Undo() { return (BOOL)::SendMessage(m_hWnd, EM_UNDO, 0, 0); }
  void Clear() { ::SendMessage(m_hWnd, WM_CLEAR, 0, 0); }
  void Copy() { ::SendMessage(m_hWnd, WM_COPY, 0, 0); }
  void Cut() { ::SendMessage(m_hWnd, WM_CUT, 0, 0); }
  void Paste() { ::SendMessage(m_hWnd, WM_PASTE, 0, 0); }
  BOOL SetReadOnly(BOOL bReadOnly = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, EM_SETREADONLY, bReadOnly, 0L);
  }
  int GetFirstVisibleLine() const { return (int)::SendMessage(m_hWnd, EM_GETFIRSTVISIBLELINE, 0, 0L); }
  TCHAR GetPasswordChar() const { return (TCHAR)::SendMessage(m_hWnd, EM_GETPASSWORDCHAR, 0, 0L); }
};

class CScrollBar : public CWnd {
  DECLARE_DYNAMIC(CScrollBar)
public:
  CScrollBar() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  int GetScrollPos() const { return ::GetScrollPos(m_hWnd, SB_CTL); }
  int SetScrollPos(int nPos, BOOL bRedraw = TRUE) { return ::SetScrollPos(m_hWnd, SB_CTL, nPos, bRedraw); }
  void GetScrollRange(LPINT lpMinPos, LPINT lpMaxPos) const {
    ::GetScrollRange(m_hWnd, SB_CTL, lpMinPos, lpMaxPos);
  }
  void SetScrollRange(int nMinPos, int nMaxPos, BOOL bRedraw = TRUE) {
    ::SetScrollRange(m_hWnd, SB_CTL, nMinPos, nMaxPos, bRedraw);
  }
  void ShowScrollBar(BOOL bShow = TRUE) { ::ShowScrollBar(m_hWnd, SB_CTL, bShow); }
  BOOL EnableScrollBar(UINT nArrowFlags = ESB_ENABLE_BOTH) {
    return ::EnableScrollBar(m_hWnd, SB_CTL, nArrowFlags);
  }
  BOOL SetScrollInfo(LPSCROLLINFO lpScrollInfo, BOOL bRedraw = TRUE) {
    lpScrollInfo->cbSize = sizeof(*lpScrollInfo);
    ::SetScrollInfo(m_hWnd, SB_CTL, lpScrollInfo, bRedraw);
    return TRUE;
  }
  BOOL GetScrollInfo(LPSCROLLINFO lpScrollInfo, UINT nMask = SIF_ALL) {
    lpScrollInfo->cbSize = sizeof(*lpScrollInfo);
    lpScrollInfo->fMask = nMask;
    return ::GetScrollInfo(m_hWnd, SB_CTL, lpScrollInfo);
  }
  int GetScrollLimit();
};

// ---------------------------------------------------------------------
// Frame windows

struct CCreateContext {
  CRuntimeClass* m_pNewViewClass;
  CDocument* m_pCurrentDoc;
  CDocTemplate* m_pNewDocTemplate;
  CView* m_pLastView;
  CFrameWnd* m_pCurrentFrame;
  CCreateContext() { memset(this, 0, sizeof(*this)); }
};

// Frame window styles that MFC adds
#define FWS_ADDTOTITLE 0x00008000L
#define FWS_PREFIXTITLE 0x00004000L
#define FWS_SNAPTOBARS 0x00002000L

// Style of the view a frame creates for its document
#define AFX_WS_DEFAULT_VIEW (WS_CHILD | WS_VISIBLE | WS_BORDER)

class CFrameWnd : public CWnd {
  DECLARE_DYNCREATE(CFrameWnd)
public:
  static const CRect rectDefault;
  CFrameWnd();
  virtual ~CFrameWnd();

  BOOL LoadAccelTable(LPCTSTR lpszResourceName);
  BOOL LoadAccelTable(UINT nIDResource) { return LoadAccelTable(MAKEINTRESOURCE(nIDResource)); }
  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                      DWORD dwStyle = WS_OVERLAPPEDWINDOW, const RECT& rect = rectDefault,
                      CWnd* pParentWnd = NULL, LPCTSTR lpszMenuName = NULL,
                      DWORD dwExStyle = 0, CCreateContext* pContext = NULL);
  virtual BOOL LoadFrame(UINT nIDResource,
                         DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
                         CWnd* pParentWnd = NULL, CCreateContext* pContext = NULL);
  CWnd* CreateView(CCreateContext* pContext, UINT nID = AFX_IDW_PANE_FIRST);

  virtual CDocument* GetActiveDocument();
  CView* GetActiveView() const { return m_pViewActive; }
  void SetActiveView(CView* pViewNew, BOOL bNotify = TRUE);
  virtual CFrameWnd* GetActiveFrame() { return this; }
  BOOL m_bAutoMenuEnable;
  int m_nWindow;
  BOOL IsTracking() const { return FALSE; }
  virtual void SetMessageText(LPCTSTR lpszText);
  void SetMessageText(UINT nID);
  virtual CWnd* GetMessageBar();
  virtual void GetMessageString(UINT nID, CString& rMessage) const;

  BOOL InModalState() const { return m_cModalStack > 0; }
  virtual void BeginModalState();
  virtual void EndModalState();
  void ShowOwnedWindows(BOOL bShow);

  // Control bars (see afxext.h)
  void EnableDocking(DWORD dwDockStyle);
  void DockControlBar(CControlBar* pBar, UINT nDockBarID = 0, LPCRECT lpRect = NULL);
  void DockControlBar(CControlBar* pBar, CDockBar* pDockBar, LPCRECT lpRect = NULL);
  void ReDockControlBar(CControlBar* pBar, CDockBar* pDockBar, LPCRECT lpRect = NULL);
  void FloatControlBar(CControlBar* pBar, CPoint point, DWORD dwStyle = 0x2000L /*CBRS_ALIGN_TOP*/);
  // The dock style of the frame's dock bar that rect (screen) overlaps.
  DWORD CanDock(CRect rect, DWORD dwDockStyle, CDockBar** ppDockBar = NULL);
  CControlBar* GetControlBar(UINT nID);
  void ShowControlBar(CControlBar* pBar, BOOL bShow, BOOL bDelay);
  void LoadBarState(LPCTSTR lpszProfileName);
  void SaveBarState(LPCTSTR lpszProfileName) const;
  void NotifyFloatingWindows(DWORD dwFlags);
  virtual void RecalcLayout(BOOL bNotify = TRUE);
  virtual void ActivateFrame(int nCmdShow = -1);
  void InitialUpdateFrame(CDocument* pDoc, BOOL bMakeVisible);
  void SetTitle(LPCTSTR lpszTitle) { m_strTitle = lpszTitle; }
  CString GetTitle() const { return m_strTitle; }
  virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
  virtual void OnUpdateFrameMenu(HMENU hMenuAlt);
  virtual HACCEL GetDefaultAccelerator();
  virtual BOOL NegotiateBorderSpace(UINT nBorderCmd, LPRECT lpRectBorder);
  virtual void OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pModeStuff);
  virtual void DelayUpdateFrameMenu(HMENU hMenuAlt);
  void DelayRecalcLayout(BOOL bNotify = TRUE) { m_nIdleFlags |= idleLayout | (bNotify ? idleNotify : 0); }
  void UpdateFrameTitleForDocument(LPCTSTR lpszDocName);
  void AddControlBar(CControlBar* pBar);
  void RemoveControlBar(CControlBar* pBar);
  virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);
  virtual void OnUpdateControlBarMenu(CCmdUI* pCmdUI);
  virtual BOOL OnBarCheck(UINT nID);
  void SetDockState(const CDockState& state);
  void GetDockState(CDockState& state) const;

  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual BOOL IsFrameWnd() const { return TRUE; }
  virtual void PostNcDestroy();
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  HMENU m_hMenuDefault;
  HACCEL m_hAccelTable;
  DWORD m_dwPromptContext;
  BOOL m_bHelpMode;
  CFrameWnd* m_pNextFrameWnd;
  CRect m_rectBorder;
  CPtrList m_listControlBars;
  int m_nShowDelay;
  UINT m_nIDHelp;
  UINT m_nIDTracking;
  UINT m_nIDLastMessage;
  CView* m_pViewActive;
  int m_cModalStack;
  HWND* m_phWndDisable;
  HMENU m_hMenuAlt;
  CString m_strTitle;
  BOOL m_bInRecalcLayout;
  COleFrameHook* m_pNotifyHook;  // while an OLE object is active in place here
  CRuntimeClass* m_pFloatingFrameClass;
  DWORD m_dwDockStyle;  // sides bars may dock to (EnableDocking)
  static const DWORD dwDockBarMap[4][2];  // dock bar IDs and their CBRS_ styles
  CMiniDockFrameWnd* CreateFloatingFrame(DWORD dwStyle);
  void DestroyDockBars();
  void _UpdateFloatingFrames();
  UINT m_nIdleFlags;
  enum IdleFlags { idleMenu = 1, idleTitle = 2, idleNotify = 4, idleLayout = 8 };

  LPCTSTR GetIconWndClass(DWORD dwDefaultStyle, UINT nIDResource);
  virtual void OnIdleUpdateCmdUI();
  int OnCreateHelper(LPCREATESTRUCT lpcs, CCreateContext* pContext);
  void BringToTop(int nCmdShow);

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);

  afx_msg void OnDestroy();
  afx_msg void OnClose();
  afx_msg void OnInitMenuPopup(CMenu*, UINT, BOOL);
  afx_msg void OnMenuSelect(UINT nItemID, UINT nFlags, HMENU hSysMenu);
  afx_msg LRESULT OnPopMessageString(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnSetMessageString(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnIdleUpdateCmdUIMsg(WPARAM, LPARAM);
  afx_msg void OnEnterIdle(UINT nWhy, CWnd* pWho);
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
  afx_msg BOOL OnNcActivate(BOOL bActive);
  afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
  afx_msg BOOL OnQueryEndSession();
  afx_msg void OnEndSession(BOOL bEnding);
  afx_msg void OnDropFiles(HDROP hDropInfo);
  afx_msg LRESULT OnCommandHelp(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnHelpHitTest(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnActivateTopLevel(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdateKeyIndicator(CCmdUI* pCmdUI);
  afx_msg void OnHelp();
  afx_msg void OnUpdateContextHelp(CCmdUI* pCmdUI);
  afx_msg void OnContextHelp();
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdateControlBarMenu_(CCmdUI* pCmdUI) { OnUpdateControlBarMenu(pCmdUI); }
  afx_msg BOOL OnBarCheck_(UINT nID) { return OnBarCheck(nID); }
  afx_msg BOOL OnToolTipText(UINT nID, NMHDR* pNMHDR, LRESULT* pResult);

  friend class CWinThread;
  friend class CWinApp;
  DECLARE_MESSAGE_MAP()
};

class CMDIFrameWnd : public CFrameWnd {
  DECLARE_DYNCREATE(CMDIFrameWnd)
public:
  CMDIFrameWnd();
  void MDIActivate(CWnd* pWndActivate) {
    ::SendMessage(m_hWndMDIClient, WM_MDIACTIVATE, (WPARAM)pWndActivate->GetSafeHwnd(), 0);
  }
  CMDIChildWnd* MDIGetActive(BOOL* pbMaximized = NULL) const;
  void MDIIconArrange() { ::SendMessage(m_hWndMDIClient, WM_MDIICONARRANGE, 0, 0); }
  void MDIMaximize(CWnd* pWnd) { ::SendMessage(m_hWndMDIClient, WM_MDIMAXIMIZE, (WPARAM)pWnd->m_hWnd, 0); }
  void MDINext() { ::SendMessage(m_hWndMDIClient, WM_MDINEXT, 0, 0); }
  void MDIPrev() { ::SendMessage(m_hWndMDIClient, WM_MDINEXT, 0, 1); }
  void MDIRestore(CWnd* pWnd) { ::SendMessage(m_hWndMDIClient, WM_MDIRESTORE, (WPARAM)pWnd->m_hWnd, 0); }
  CMenu* MDISetMenu(CMenu* pFrameMenu, CMenu* pWindowMenu);
  void MDITile() { ::SendMessage(m_hWndMDIClient, WM_MDITILE, 0, 0); }
  void MDICascade() { ::SendMessage(m_hWndMDIClient, WM_MDICASCADE, 0, 0); }
  void MDITile(int nType) { ::SendMessage(m_hWndMDIClient, WM_MDITILE, nType, 0); }
  void MDICascade(int nType) { ::SendMessage(m_hWndMDIClient, WM_MDICASCADE, nType, 0); }
  CMDIChildWnd* CreateNewChild(CRuntimeClass* pClass, UINT nResource, HMENU hMenu = NULL,
                               HACCEL hAccel = NULL);

  virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);
  virtual BOOL CreateClient(LPCREATESTRUCT lpCreateStruct, CMenu* pWindowMenu);
  virtual HMENU GetWindowMenuPopup(HMENU hMenuBar);
  virtual BOOL LoadFrame(UINT nIDResource,
                         DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
                         CWnd* pParentWnd = NULL, CCreateContext* pContext = NULL);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
  virtual void OnUpdateFrameMenu(HMENU hMenuAlt);
  virtual void DelayUpdateFrameMenu(HMENU hMenuAlt);
  virtual CFrameWnd* GetActiveFrame();
  virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

  HWND m_hWndMDIClient;

  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnUpdateMDIWindowCmd(CCmdUI* pCmdUI);
  afx_msg BOOL OnMDIWindowCmd(UINT nID);
  afx_msg void OnWindowNew();
  virtual void OnIdleUpdateCmdUI();
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  DECLARE_MESSAGE_MAP()
};

class CMDIChildWnd : public CFrameWnd {
  DECLARE_DYNCREATE(CMDIChildWnd)
public:
  CMDIChildWnd();
  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                      DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                      const RECT& rect = rectDefault, CMDIFrameWnd* pParentWnd = NULL,
                      CCreateContext* pContext = NULL);
  CMDIFrameWnd* GetMDIFrame();
  void MDIDestroy();
  void MDIActivate();
  void MDIMaximize();
  void MDIRestore();
  void SetHandles(HMENU hMenu, HACCEL hAccel) { m_hMenuShared = hMenu; m_hAccelTable = hAccel; }

  HMENU m_hMenuShared;

  virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                         CCreateContext* pContext = NULL);
  virtual BOOL DestroyWindow();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual void ActivateFrame(int nCmdShow = -1);
  virtual void OnUpdateFrameMenu(BOOL bActive, CWnd* pActivateWnd, HMENU hMenuAlt);
  virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual CFrameWnd* GetActiveFrame();
  virtual void OnUpdateFrameTitle(BOOL bAddToTitle);

  BOOL m_bPseudoInactive;

  afx_msg void OnMDIActivate(BOOL bActivate, CWnd*, CWnd*);
  afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg BOOL OnNcActivate(BOOL bActive);
  afx_msg void OnDestroy();
  afx_msg void OnWindowPosChanging(LPWINDOWPOS lpWndPos);
  afx_msg BOOL OnNcCreate(LPCREATESTRUCT lpCreateStruct);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CMiniFrameWnd: a frame with a small caption, used for floating
// toolbars and palettes. It is a tool window, so the system draws its
// caption the way it draws other palettes.

#define MFS_SYNCACTIVATE 0x00000100L  // caption is active when the owner is
#define MFS_4THICKFRAME 0x00000200L   // a sizing border without the corners
#define MFS_THICKFRAME 0x00000400L    // a sizing border
#define MFS_MOVEFRAME 0x00000800L     // dragging the edge moves the window
#define MFS_BLOCKSYSMENU 0x00001000L  // no system menu on the caption

class CMiniFrameWnd : public CFrameWnd {
  DECLARE_DYNCREATE(CMiniFrameWnd)
public:
  CMiniFrameWnd();
  virtual ~CMiniFrameWnd();

  BOOL Create(LPCTSTR lpClassName, LPCTSTR lpWindowName, DWORD dwStyle, const RECT& rect,
              CWnd* pParentWnd = NULL, UINT nID = 0);
  BOOL CreateEx(DWORD dwExStyle, LPCTSTR lpClassName, LPCTSTR lpWindowName, DWORD dwStyle,
                const RECT& rect, CWnd* pParentWnd = NULL, UINT nID = 0);

  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  static void PASCAL CalcBorders(LPRECT lpClientRect, DWORD dwStyle = 0, DWORD dwExStyle = 0);

  BOOL m_bSysTracking;
  BOOL m_bInSys;
  BOOL m_bActive;
  CString m_strCaption;

  afx_msg BOOL OnNcActivate(BOOL bActive);
  afx_msg LRESULT OnFloatStatus(WPARAM wParam, LPARAM lParam);
  afx_msg void OnGetMinMaxInfo(MINMAXINFO* pMMI);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// Dialogs

class CDialog : public CWnd {
  DECLARE_DYNAMIC(CDialog)
public:
  CDialog();
  explicit CDialog(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);
  explicit CDialog(UINT nIDTemplate, CWnd* pParentWnd = NULL);
  virtual ~CDialog();

  virtual BOOL Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);
  virtual BOOL Create(UINT nIDTemplate, CWnd* pParentWnd = NULL);
  virtual BOOL CreateIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd = NULL,
                              void* lpDialogInit = NULL);
  virtual BOOL CreateIndirect(HGLOBAL hDialogTemplate, CWnd* pParentWnd = NULL);

  BOOL InitModalIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd = NULL,
                         void* lpDialogInit = NULL);
  BOOL InitModalIndirect(HGLOBAL hDialogTemplate, CWnd* pParentWnd = NULL);

  void MapDialogRect(LPRECT lpRect) const { ::MapDialogRect(m_hWnd, lpRect); }
  void SetHelpID(UINT nIDR) { m_nIDHelp = nIDR; }

  virtual INT_PTR DoModal();

  void NextDlgCtrl() const { ::SendMessage(m_hWnd, WM_NEXTDLGCTL, 0, 0); }
  void PrevDlgCtrl() const { ::SendMessage(m_hWnd, WM_NEXTDLGCTL, 1, 0); }
  void GotoDlgCtrl(CWnd* pWndCtrl) {
    ::SendMessage(m_hWnd, WM_NEXTDLGCTL, (WPARAM)pWndCtrl->GetSafeHwnd(), 1L);
  }
  void SetDefID(UINT nID) { ::SendMessage(m_hWnd, DM_SETDEFID, nID, 0); }
  DWORD GetDefID() const { return (DWORD)::SendMessage(m_hWnd, DM_GETDEFID, 0, 0); }
  void EndDialog(int nResult);

  virtual BOOL OnInitDialog();
  virtual void OnSetFont(CFont* pFont) { (void)pFont; }
  virtual void OnOK();
  virtual void OnCancel();

  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual BOOL CheckAutoCenter();

  UINT m_nIDHelp;
  LPCTSTR m_lpszTemplateName;
  HGLOBAL m_hDialogTemplate;
  LPCDLGTEMPLATE m_lpDialogTemplate;
  void* m_lpDialogInit;
  CWnd* m_pParentWnd;
  HWND m_hWndTop;
  BOOL m_bModal;

  virtual void PreInitDialog();
  void CommonConstruct();
  HWND PreModal();
  void PostModal();
  BOOL CreateDlgIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd, HINSTANCE hInst);

  afx_msg LRESULT OnCommandHelp(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnHelpHitTest(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT HandleInitDialog(WPARAM, LPARAM);
  afx_msg LRESULT HandleSetFont(WPARAM, LPARAM);
  afx_msg void OnPaint();
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

  friend INT_PTR CALLBACK AfxDlgProc(HWND, UINT, WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
};

INT_PTR CALLBACK AfxDlgProc(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------
// Dialog data exchange

class CDataExchange {
public:
  BOOL m_bSaveAndValidate;
  CWnd* m_pDlgWnd;
  HWND m_hWndLastControl;
  BOOL m_bEditLastControl;

  CDataExchange(CWnd* pDlgWnd, BOOL bSaveAndValidate);
  HWND PrepareCtrl(int nIDC);
  HWND PrepareEditCtrl(int nIDC);
  void Fail();
  CWnd* PrepareOleCtrl(int nIDC);
};

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, BYTE& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, short& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, int& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, UINT& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, long& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, DWORD& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, LONGLONG& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, ULONGLONG& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, CString& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, LPTSTR value, int nMaxLen);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, float& value);
void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, double& value);
void AFXAPI DDX_Check(CDataExchange* pDX, int nIDC, int& value);
void AFXAPI DDX_Radio(CDataExchange* pDX, int nIDC, int& value);
void AFXAPI DDX_LBString(CDataExchange* pDX, int nIDC, CString& value);
void AFXAPI DDX_CBString(CDataExchange* pDX, int nIDC, CString& value);
void AFXAPI DDX_LBIndex(CDataExchange* pDX, int nIDC, int& index);
void AFXAPI DDX_CBIndex(CDataExchange* pDX, int nIDC, int& index);
void AFXAPI DDX_LBStringExact(CDataExchange* pDX, int nIDC, CString& value);
void AFXAPI DDX_CBStringExact(CDataExchange* pDX, int nIDC, CString& value);
void AFXAPI DDX_Scroll(CDataExchange* pDX, int nIDC, int& value);
void AFXAPI DDX_Slider(CDataExchange* pDX, int nIDC, int& value);
void AFXAPI DDX_Control(CDataExchange* pDX, int nIDC, CWnd& rControl);

void AFXAPI DDV_MinMaxByte(CDataExchange* pDX, BYTE value, BYTE minVal, BYTE maxVal);
void AFXAPI DDV_MinMaxShort(CDataExchange* pDX, short value, short minVal, short maxVal);
void AFXAPI DDV_MinMaxInt(CDataExchange* pDX, int value, int minVal, int maxVal);
void AFXAPI DDV_MinMaxLong(CDataExchange* pDX, long value, long minVal, long maxVal);
void AFXAPI DDV_MinMaxUInt(CDataExchange* pDX, UINT value, UINT minVal, UINT maxVal);
void AFXAPI DDV_MinMaxDWord(CDataExchange* pDX, DWORD value, DWORD minVal, DWORD maxVal);
void AFXAPI DDV_MinMaxLongLong(CDataExchange* pDX, LONGLONG value, LONGLONG minVal, LONGLONG maxVal);
void AFXAPI DDV_MinMaxULongLong(CDataExchange* pDX, ULONGLONG value, ULONGLONG minVal, ULONGLONG maxVal);
void AFXAPI DDV_MinMaxFloat(CDataExchange* pDX, float const& value, float minVal, float maxVal);
void AFXAPI DDV_MinMaxDouble(CDataExchange* pDX, double const& value, double minVal, double maxVal);
void AFXAPI DDV_MaxChars(CDataExchange* pDX, CString const& value, int nChars);
void AFXAPI DDV_MinMaxSlider(CDataExchange* pDX, DWORD value, DWORD minVal, DWORD maxVal);

// ---------------------------------------------------------------------
// Views and documents

class CView : public CWnd {
  DECLARE_DYNAMIC(CView)
protected:
  CView();
public:
  virtual ~CView();
  CDocument* GetDocument() const { return m_pDocument; }

  virtual BOOL IsSelected(const CObject* pDocItem) const;
  virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
  virtual void OnInitialUpdate();
  virtual void OnDraw(CDC* pDC) = 0;
  virtual void OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView);
  virtual void OnActivateFrame(UINT nState, CFrameWnd* pFrameWnd);
  virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint);
  virtual BOOL OnScrollBy(CSize sizeScroll, BOOL bDoScroll = TRUE);
  virtual BOOL OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll = TRUE);
  virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
  virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnPrint(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnEndPrintPreview(CDC* pDC, CPrintInfo* pInfo, POINT point, CPreviewView* pView);

  // Drag and drop, when a COleDropTarget is registered for the view.
  virtual DROPEFFECT OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
  virtual DROPEFFECT OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
  virtual void OnDragLeave();
  virtual BOOL OnDrop(COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);
  virtual DROPEFFECT OnDropEx(COleDataObject* pDataObject, DROPEFFECT dropDefault,
                              DROPEFFECT dropList, CPoint point);
  virtual DROPEFFECT OnDragScroll(DWORD dwKeyState, CPoint point);
  BOOL DoPrintPreview(UINT nIDResource, CView* pPrintView, CRuntimeClass* pPreviewViewClass,
                      CPrintPreviewState* pState);
  BOOL DoPreparePrinting(CPrintInfo* pInfo);

  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void PostNcDestroy();
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual void CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType = adjustBorder);
  virtual CScrollBar* GetScrollBarCtrl(int nBar) const;

  static CSplitterWnd* PASCAL GetParentSplitter(const CWnd* pWnd, BOOL bAnyState);

  CDocument* m_pDocument;

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);
  afx_msg void OnDestroy();
  afx_msg void OnPaint();
  afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
  afx_msg void OnFilePrint();
  afx_msg void OnFilePrintPreview();
  afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdateSplitCmd(CCmdUI* pCmdUI);
  afx_msg BOOL OnSplitCmd(UINT nID);
  afx_msg void OnUpdateNextPaneMenu(CCmdUI* pCmdUI);
  afx_msg BOOL OnNextPaneCmd(UINT nID);

  friend class CDocument;
  friend class CFrameWnd;
  friend class CPreviewView;
  friend class CDocObjectServer;
  DECLARE_MESSAGE_MAP()
};

class CSplitterWnd;

class CScrollView : public CView {
  DECLARE_DYNAMIC(CScrollView)
protected:
  CScrollView();
public:
  static const SIZE sizeDefault;
  CPoint GetScrollPosition() const;
  CSize GetTotalSize() const { return m_totalLog; }
  void GetScrollBarSizes(CSize& sizeSb);
  void GetScrollBarState(CSize sizeClient, CSize& needSb, CSize& sizeRange, CPoint& ptMove,
                         BOOL bInsideClient);
  CPoint GetDeviceScrollPosition() const;
  void GetDeviceScrollSizes(int& nMapMode, SIZE& sizePage, SIZE& sizeLine) const;
  void ScrollToPosition(POINT pt);
  void FillOutsideRect(CDC* pDC, CBrush* pBrush);
  void ResizeParentToFit(BOOL bShrinkOnly = TRUE);
  BOOL DoMouseWheel(UINT fFlags, short zDelta, CPoint point);
  void SetScaleToFitSize(SIZE sizeTotal);
  void SetScrollSizes(int nMapMode, SIZE sizeTotal, const SIZE& sizePage = sizeDefault,
                      const SIZE& sizeLine = sizeDefault);
  void CheckScrollBars(BOOL& bHasHorzBar, BOOL& bHasVertBar) const;

  virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
  virtual void CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType = adjustBorder);
  virtual BOOL OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll = TRUE);
  virtual BOOL OnScrollBy(CSize sizeScroll, BOOL bDoScroll = TRUE);

  int m_nMapMode;
  CSize m_totalLog;
  CSize m_totalDev;
  CSize m_pageDev;
  CSize m_lineDev;
  BOOL m_bCenter;
  BOOL m_bInsideUpdate;

  void CenterOnPoint(CPoint ptCenter);
  void ScrollToDevicePosition(POINT ptDev);
  void UpdateBars();
  BOOL GetTrueClientSize(CSize& size, CSize& sizeSb);

  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  DECLARE_MESSAGE_MAP()
};

// A view that is a control: the window is the control itself (an edit,
// list view, tree view, ...), so it paints itself.
class CCtrlView : public CView {
  DECLARE_DYNAMIC(CCtrlView)
public:
  CCtrlView(LPCTSTR lpszClass, DWORD dwStyle);

  CString m_strClass;
  DWORD m_dwDefaultStyle;

  virtual void OnDraw(CDC* pDC);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

  afx_msg void OnPaint();
  DECLARE_MESSAGE_MAP()
};

class CDocument : public CCmdTarget {
  DECLARE_DYNAMIC(CDocument)
public:
  CDocument();
  virtual ~CDocument();

  const CString& GetTitle() const { return m_strTitle; }
  virtual void SetTitle(LPCTSTR lpszTitle);
  const CString& GetPathName() const { return m_strPathName; }
  virtual void SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU = TRUE);
  void ClearPathName() { m_strPathName.Empty(); }
  CDocTemplate* GetDocTemplate() const { return m_pDocTemplate; }
  virtual BOOL IsModified() { return m_bModified; }
  virtual void SetModifiedFlag(BOOL bModified = TRUE) { m_bModified = bModified; }

  virtual POSITION GetFirstViewPosition() const { return m_viewList.GetHeadPosition(); }
  virtual CView* GetNextView(POSITION& rPosition) const {
    return (CView*)m_viewList.GetNext(rPosition);
  }
  void AddView(CView* pView);
  void RemoveView(CView* pView);
  void UpdateAllViews(CView* pSender, LPARAM lHint = 0L, CObject* pHint = NULL);
  void SendInitialUpdate();

  virtual void DeleteContents();
  virtual BOOL OnNewDocument();
  virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
  virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
  virtual void OnCloseDocument();
  virtual void ReportSaveLoadException(LPCTSTR lpszPathName, CException* e, BOOL bSaving,
                                       UINT nIDPDefault);
  virtual CFile* GetFile(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException* pError);
  virtual void ReleaseFile(CFile* pFile, BOOL bAbort);
  virtual BOOL CanCloseFrame(CFrameWnd* pFrame);
  virtual BOOL SaveModified();
  virtual void PreCloseFrame(CFrameWnd* pFrame);
  virtual void OnChangedViewList();
  virtual BOOL DoSave(LPCTSTR lpszPathName, BOOL bReplace = TRUE);
  virtual BOOL DoFileSave();
  virtual void UpdateFrameCounts();
  virtual HMENU GetDefaultMenu();
  virtual HACCEL GetDefaultAccelerator();
  virtual void OnIdle();
  virtual void OnFinalRelease();
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

  CString m_strTitle;
  CString m_strPathName;
  CDocTemplate* m_pDocTemplate;
  CPtrList m_viewList;
  BOOL m_bModified;
  BOOL m_bAutoDelete;
  BOOL m_bEmbedded;

  afx_msg void OnFileClose();
  afx_msg void OnFileSave();
  afx_msg void OnFileSaveAs();
  afx_msg void OnFileSendMail();
  afx_msg void OnUpdateFileSendMail(CCmdUI* pCmdUI);

  friend class CDocTemplate;
  DECLARE_MESSAGE_MAP()
};

class CDocTemplate : public CCmdTarget {
  DECLARE_DYNAMIC(CDocTemplate)
protected:
  CDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass, CRuntimeClass* pFrameClass,
               CRuntimeClass* pViewClass);
public:
  virtual ~CDocTemplate();

  virtual void LoadTemplate();
  virtual POSITION GetFirstDocPosition() const = 0;
  virtual CDocument* GetNextDoc(POSITION& rPos) const = 0;

  enum DocStringIndex {
    windowTitle, docName, fileNewName, filterName, filterExt, regFileTypeId,
    regFileTypeName, filterMac
  };
  virtual BOOL GetDocString(CString& rString, enum DocStringIndex index) const;

  enum Confidence { noAttempt, maybeAttemptForeign, maybeAttemptNative, yesAttemptForeign,
                    yesAttemptNative, yesAlreadyOpen };
  virtual Confidence MatchDocType(LPCTSTR lpszPathName, CDocument*& rpDocMatch);

  virtual void AddDocument(CDocument* pDoc);
  virtual void RemoveDocument(CDocument* pDoc);
  virtual CDocument* CreateNewDocument();
  virtual CFrameWnd* CreateNewFrame(CDocument* pDoc, CFrameWnd* pOther);
  virtual void InitialUpdateFrame(CFrameWnd* pFrame, CDocument* pDoc, BOOL bMakeVisible = TRUE);
  virtual BOOL SaveAllModified();
  virtual void CloseAllDocuments(BOOL bEndSession);
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible = TRUE) = 0;
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU, BOOL bMakeVisible);
  virtual void SetDefaultTitle(CDocument* pDocument) = 0;
  virtual void OnIdle();
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  void SetContainerInfo(UINT nIDOleInPlaceContainer);
  // The menu and accelerators (nIDOleEmbedding) a server document uses
  // while it edits an embedded object, and those for in-place editing.
  void SetServerInfo(UINT nIDOleEmbedding, UINT nIDOleInPlaceServer = 0,
                     CRuntimeClass* pOleFrameClass = NULL, CRuntimeClass* pOleViewClass = NULL);
  // The in-place frame (of the class SetServerInfo gave) in the
  // container's window, with a new view when bCreateView is TRUE.
  virtual CFrameWnd* CreateOleFrame(CWnd* pParentWnd, CDocument* pDoc, BOOL bCreateView);

  UINT m_nIDResource;
  CRuntimeClass* m_pDocClass;
  CRuntimeClass* m_pFrameClass;
  CRuntimeClass* m_pViewClass;
  CString m_strDocStrings;
  BOOL m_bAutoDelete;
  CObject* m_pAttachedFactory;
  UINT m_nIDServerResource;
  UINT m_nIDEmbeddingResource;
  UINT m_nIDContainerResource;
  HMENU m_hMenuInPlace;
  HACCEL m_hAccelInPlace;
  HMENU m_hMenuEmbedding;
  HACCEL m_hAccelEmbedding;
  HMENU m_hMenuInPlaceServer;
  HACCEL m_hAccelInPlaceServer;
  CRuntimeClass* m_pOleFrameClass;
  CRuntimeClass* m_pOleViewClass;
};

class CSingleDocTemplate : public CDocTemplate {
  DECLARE_DYNAMIC(CSingleDocTemplate)
public:
  CSingleDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass, CRuntimeClass* pFrameClass,
                     CRuntimeClass* pViewClass);
  virtual ~CSingleDocTemplate();
  virtual void AddDocument(CDocument* pDoc);
  virtual void RemoveDocument(CDocument* pDoc);
  virtual POSITION GetFirstDocPosition() const;
  virtual CDocument* GetNextDoc(POSITION& rPos) const;
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible = TRUE);
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU, BOOL bMakeVisible);
  virtual void SetDefaultTitle(CDocument* pDocument);

  CDocument* m_pOnlyDoc;
};

class CMultiDocTemplate : public CDocTemplate {
  DECLARE_DYNAMIC(CMultiDocTemplate)
public:
  CMultiDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass, CRuntimeClass* pFrameClass,
                    CRuntimeClass* pViewClass);
  virtual ~CMultiDocTemplate();
  virtual void LoadTemplate();
  virtual void AddDocument(CDocument* pDoc);
  virtual void RemoveDocument(CDocument* pDoc);
  virtual POSITION GetFirstDocPosition() const;
  virtual CDocument* GetNextDoc(POSITION& rPos) const;
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible = TRUE);
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU, BOOL bMakeVisible);
  virtual void SetDefaultTitle(CDocument* pDocument);

  HMENU m_hMenuShared;
  HACCEL m_hAccelTable;
  CPtrList m_docList;
  UINT m_nUntitledCount;
};

// ---------------------------------------------------------------------
// Threads and the application

typedef UINT(AFX_CDECL* AFX_THREADPROC)(LPVOID);

class CWinThread : public CCmdTarget {
  DECLARE_DYNAMIC(CWinThread)
public:
  CWinThread();
  CWinThread(AFX_THREADPROC pfnThreadProc, LPVOID pParam);
  BOOL CreateThread(DWORD dwCreateFlags = 0, UINT nStackSize = 0,
                    LPSECURITY_ATTRIBUTES lpSecurityAttrs = NULL);

  CWnd* m_pMainWnd;
  CWnd* m_pActiveWnd;
  BOOL m_bAutoDelete;
  HANDLE m_hThread;
  operator HANDLE() const { return AFX_IS_NULL_THIS() ? NULL : m_hThread; }
  DWORD m_nThreadID;

  int GetThreadPriority() { return ::GetThreadPriority(m_hThread); }
  BOOL SetThreadPriority(int nPriority) { return ::SetThreadPriority(m_hThread, nPriority); }
  DWORD SuspendThread() { return ::SuspendThread(m_hThread); }
  DWORD ResumeThread() { return ::ResumeThread(m_hThread); }
  BOOL PostThreadMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return ::PostThreadMessage(m_nThreadID, message, wParam, lParam);
  }

  virtual BOOL InitInstance();
  virtual int Run();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL PumpMessage();
  virtual BOOL OnIdle(LONG lCount);
  virtual BOOL IsIdleMessage(MSG* pMsg);
  virtual int ExitInstance();
  virtual LRESULT ProcessWndProcException(CException* e, const MSG* pMsg);
  virtual BOOL ProcessMessageFilter(int code, LPMSG lpMsg);
  virtual CWnd* GetMainWnd();

  virtual ~CWinThread();
  virtual void Delete();

  MSG m_msgCur;
  LPVOID m_pThreadParams;
  AFX_THREADPROC m_pfnThreadProc;
  CPoint m_ptCursorLast;
  UINT m_nMsgLast;
  COleMessageFilter* m_pMessageFilter;  // AfxOleInit's, for this thread

  void CommonConstruct();
  BOOL DispatchThreadMessageEx(MSG* msg);

  DECLARE_MESSAGE_MAP()
};

class CCommandLineInfo : public CObject {
public:
  CCommandLineInfo();
  virtual ~CCommandLineInfo();
  virtual void ParseParam(const TCHAR* pszParam, BOOL bFlag, BOOL bLast);

  BOOL m_bShowSplash;
  BOOL m_bRunEmbedded;
  BOOL m_bRunAutomated;
  BOOL m_bRegisterPerUser;
  enum { FileNew, FileOpen, FilePrint, FilePrintTo, FileDDE, AppRegister, AppUnregister,
         FileNothing = -1 } m_nShellCommand;
  CString m_strFileName;
  CString m_strPrinterName;
  CString m_strDriverName;
  CString m_strPortName;
  CString m_strRestartIdentifier;

protected:
  void ParseParamFlag(const char* pszParam);
  void ParseParamNotFlag(const TCHAR* pszParam);
  void ParseLast(BOOL bLast);
};

class CRecentFileList {
public:
  CRecentFileList(UINT nStart, LPCTSTR lpszSection, LPCTSTR lpszEntryFormat, int nSize,
                  int nMaxDispLen = 30);
  virtual ~CRecentFileList();
  int GetSize() const { return m_nSize; }
  CString& operator[](int nIndex) { return m_arrNames[nIndex]; }
  virtual void Remove(int nIndex);
  virtual void Add(LPCTSTR lpszPathName);
  virtual BOOL GetDisplayName(CString& strName, int nIndex, LPCTSTR lpszCurDir, int nCurDir,
                              BOOL bAtLeastName = TRUE) const;
  virtual void UpdateMenu(CCmdUI* pCmdUI);
  virtual void ReadList();
  virtual void WriteList();

  int m_nSize;
  CString* m_arrNames;
  CString m_strSectionName;
  CString m_strEntryFormat;
  UINT m_nStart;
  int m_nMaxDisplayLength;
  CString m_strOriginal;
};

class CWinApp : public CWinThread {
  DECLARE_DYNAMIC(CWinApp)
public:
  explicit CWinApp(LPCTSTR lpszAppName = NULL);
  virtual ~CWinApp();

  LPCTSTR m_pszAppName;
  HINSTANCE m_hInstance;
  LPTSTR m_lpCmdLine;
  int m_nCmdShow;
  LPCTSTR m_pszExeName;
  LPCTSTR m_pszHelpFilePath;
  LPCTSTR m_pszProfileName;
  LPCTSTR m_pszRegistryKey;
  HINSTANCE m_hLangResourceDLL;
  BOOL m_bHelpMode;
  CDocManager* m_pDocManager;

  HCURSOR LoadCursor(LPCTSTR lpszResourceName) const;
  HCURSOR LoadCursor(UINT nIDResource) const;
  HCURSOR LoadStandardCursor(LPCTSTR lpszCursorName) const { return ::LoadCursor(NULL, lpszCursorName); }
  HCURSOR LoadOEMCursor(UINT nIDCursor) const { return ::LoadCursor(NULL, MAKEINTRESOURCE(nIDCursor)); }
  HICON LoadIcon(LPCTSTR lpszResourceName) const;
  HICON LoadIcon(UINT nIDResource) const;
  HICON LoadStandardIcon(LPCTSTR lpszIconName) const { return ::LoadIcon(NULL, lpszIconName); }
  HICON LoadOEMIcon(UINT nIDIcon) const { return ::LoadIcon(NULL, MAKEINTRESOURCE(nIDIcon)); }

  UINT GetProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault);
  BOOL WriteProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nValue);
  CString GetProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault = NULL);
  BOOL WriteProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue);
  BOOL GetProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes);
  BOOL WriteProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes);
  void SetRegistryKey(LPCTSTR lpszRegistryKey);
  void SetRegistryKey(UINT nIDRegistryKey);
  HKEY GetAppRegistryKey();
  HKEY GetSectionKey(LPCTSTR lpszSection);

  BOOL RunEmbedded() { return FALSE; }
  BOOL RunAutomated() { return FALSE; }
  void ParseCommandLine(CCommandLineInfo& rCmdInfo);
  BOOL ProcessShellCommand(CCommandLineInfo& rCmdInfo);

  virtual BOOL InitApplication();
  virtual BOOL InitInstance();
  virtual int ExitInstance();
  virtual int Run();
  virtual BOOL OnIdle(LONG lCount);
  virtual LRESULT ProcessWndProcException(CException* e, const MSG* pMsg);

  virtual CDocument* OpenDocumentFile(LPCTSTR lpszFileName);
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszFileName, BOOL bAddToMRU);
  virtual void AddToRecentFileList(LPCTSTR lpszPathName);
  virtual BOOL SaveAllModified();
  virtual int DoMessageBox(LPCTSTR lpszPrompt, UINT nType, UINT nIDPrompt);
  virtual void DoWaitCursor(int nCode);
  virtual BOOL OnDDECommand(LPTSTR lpszCommand);
  virtual void WinHelp(DWORD_PTR dwData, UINT nCmd = HELP_CONTEXT);
  virtual void HtmlHelp(DWORD_PTR dwData, UINT nCmd = 0x000F);
  virtual BOOL DoPromptFileName(CString& fileName, UINT nIDSTitle, DWORD lFlags,
                                BOOL bOpenFileDialog, CDocTemplate* pTemplate);

  void AddDocTemplate(CDocTemplate* pTemplate);
  POSITION GetFirstDocTemplatePosition() const;
  CDocTemplate* GetNextDocTemplate(POSITION& pos) const;
  void CloseAllDocuments(BOOL bEndSession);
  void HideApplication();

  void SelectPrinter(HANDLE hDevNames, HANDLE hDevMode, BOOL bFreeOld = TRUE);
  BOOL CreatePrinterDC(CDC& dc);
  BOOL GetPrinterDeviceDefaults(PRINTDLG* pPrintDlg);
  int DoPrintDialog(CPrintDialog* pPD);
  void UpdatePrinterSelection(BOOL bForceDefaults);
  void SaveStdProfileSettings();
  void LoadStdProfileSettings(UINT nMaxMRU = 4);
  void EnableShellOpen();
  void RegisterShellFileTypes(BOOL bCompat = FALSE);
  void UnregisterShellFileTypes();
  void SetDialogBkColor(COLORREF = RGB(192, 192, 192), COLORREF = RGB(0, 0, 0)) {}
  void Enable3dControls() {}
  void Enable3dControlsStatic() {}
  void EnableHtmlHelp() {}
  BOOL Unregister() { return TRUE; }
  BOOL Register() { return TRUE; }
  void SetAppID(LPCTSTR) {}
  void EnableTaskbarInteraction(BOOL = TRUE) {}

  HGLOBAL m_hDevMode;
  HGLOBAL m_hDevNames;
  DWORD m_dwPromptContext;
  int m_nWaitCursorCount;
  HCURSOR m_hcurWaitCursorRestore;
  CRecentFileList* m_pRecentFileList;
  ATOM m_atomApp;
  ATOM m_atomSystemTopic;
  UINT m_nNumPreviewPages;
  void (*m_lpfnDaoTerm)();

  afx_msg void OnAppExit();
  afx_msg void OnFileNew();
  afx_msg void OnFileOpen();
  afx_msg void OnFilePrintSetup();
  afx_msg void OnUpdateRecentFileMenu(CCmdUI* pCmdUI);
  afx_msg BOOL OnOpenRecentFile(UINT nID);
  afx_msg void OnHelp();
  afx_msg void OnHelpIndex();
  afx_msg void OnHelpFinder();
  afx_msg void OnHelpUsing();
  afx_msg void OnContextHelp();

  DECLARE_MESSAGE_MAP()
};

class CDocManager : public CObject {
  DECLARE_DYNAMIC(CDocManager)
public:
  CDocManager();
  virtual ~CDocManager();
  virtual void AddDocTemplate(CDocTemplate* pTemplate);
  virtual POSITION GetFirstDocTemplatePosition() const;
  virtual CDocTemplate* GetNextDocTemplate(POSITION& pos) const;
  // Registers each template's file extension as its document type, which
  // the shell opens (and with bCompat prints) with this program.
  virtual void RegisterShellFileTypes(BOOL bCompat);
  virtual void UnregisterShellFileTypes();
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszFileName);
  virtual CDocument* OpenDocumentFile(LPCTSTR lpszFileName, BOOL bAddToMRU);
  virtual BOOL SaveAllModified();
  virtual void CloseAllDocuments(BOOL bEndSession);
  virtual int GetOpenDocumentCount();
  virtual BOOL DoPromptFileName(CString& fileName, UINT nIDSTitle, DWORD lFlags,
                                BOOL bOpenFileDialog, CDocTemplate* pTemplate);
  virtual BOOL OnDDECommand(LPTSTR lpszCommand) { (void)lpszCommand; return FALSE; }
  virtual void OnFileNew();
  virtual void OnFileOpen();

  CPtrList m_templateList;
  static CPtrList* pStaticList;
  static BOOL bStaticInit;
};

// ---------------------------------------------------------------------
// Global functions

CWinApp* AFXAPI AfxGetApp();
CWinThread* AFXAPI AfxGetThread();
CWnd* AFXAPI AfxGetMainWnd();
BOOL AFXAPI AfxWinInit(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine,
                       int nCmdShow);
void AFXAPI AfxWinTerm();
int AFXAPI AfxWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine,
                      int nCmdShow);
BOOL AFXAPI AfxInitRichEdit();
BOOL AFXAPI AfxInitRichEdit2();
BOOL AFXAPI AfxOleInit();
BOOL AFXAPI AfxDeferRegisterClass(LONG fToRegister);
BOOL AFXAPI AfxRegisterClass(WNDCLASS* lpWndClass);
LPCTSTR AFXAPI AfxRegisterWndClass(UINT nClassStyle, HCURSOR hCursor = 0,
                                   HBRUSH hbrBackground = 0, HICON hIcon = 0);
void AFXAPI AfxPostQuitMessage(int nExitCode);

// The program's count of live OLE objects (afxdisp.h). When it falls to
// zero in a program the user did not start (it was run with /Automation
// or /Embedding), the program closes.
void AFXAPI AfxOleLockApp();
void AFXAPI AfxOleUnlockApp();
BOOL AFXAPI AfxOleCanExitApp();
BOOL AFXAPI AfxOleGetUserCtrl();
void AFXAPI AfxOleSetUserCtrl(BOOL bUserCtrl);
int AFXAPI AfxMessageBox(LPCTSTR lpszText, UINT nType = MB_OK, UINT nIDHelp = 0);
int AFXAPI AfxMessageBox(UINT nIDPrompt, UINT nType = MB_OK, UINT nIDHelp = (UINT)-1);
void AFXAPI AfxFormatString1(CString& rString, UINT nIDS, LPCTSTR lpsz1);
void AFXAPI AfxFormatString2(CString& rString, UINT nIDS, LPCTSTR lpsz1, LPCTSTR lpsz2);
void AFXAPI AfxFormatStrings(CString& rString, UINT nIDS, LPCTSTR const* rglpsz, int nString);
void AFXAPI AfxFormatStrings(CString& rString, LPCTSTR lpszFormat, LPCTSTR const* rglpsz,
                             int nString);
BOOL AFXAPI AfxCheckMemory();
void AFXAPI AfxLockTempMaps();
BOOL AFXAPI AfxUnlockTempMaps(BOOL bDeleteTemps = TRUE);
CWinThread* AFXAPI AfxBeginThread(AFX_THREADPROC pfnThreadProc, LPVOID pParam,
                                  int nPriority = THREAD_PRIORITY_NORMAL, UINT nStackSize = 0,
                                  DWORD dwCreateFlags = 0,
                                  LPSECURITY_ATTRIBUTES lpSecurityAttrs = NULL);
CWinThread* AFXAPI AfxBeginThread(CRuntimeClass* pThreadClass,
                                  int nPriority = THREAD_PRIORITY_NORMAL, UINT nStackSize = 0,
                                  DWORD dwCreateFlags = 0,
                                  LPSECURITY_ATTRIBUTES lpSecurityAttrs = NULL);
void AFXAPI AfxEndThread(UINT nExitCode, BOOL bDelete = TRUE);
BOOL AFXAPI AfxPumpMessage();
void AFXAPI AfxLockGlobals(int nLockType);
void AFXAPI AfxUnlockGlobals(int nLockType);
void AFXAPI AfxRepositionWindow(AFX_SIZEPARENTPARAMS* lpLayout, HWND hWnd, LPCRECT lpRect);
void AFXAPI AfxCancelModes(HWND hWndRcvr);
BOOL AFXAPI AfxIsDescendant(HWND hWndParent, HWND hWndChild);
HWND AFXAPI AfxGetParentOwner(HWND hWnd);
void AFXAPI AfxGlobalFree(HGLOBAL hGlobal);
void AFXAPI AfxDeleteObject(HGDIOBJ* pObject);

// Help context bases
#define HID_BASE_COMMAND 0x00010000UL
#define HID_BASE_RESOURCE 0x00020000UL
#define HID_BASE_PROMPT 0x00030000UL
#define HID_BASE_NCAREAS 0x00040000UL
#define HID_BASE_CONTROL 0x00050000UL
#define HID_BASE_DISPATCH 0x00060000UL

// MFC's private messages (also in afxpriv.h)
#define WM_QUERYAFXWNDPROC 0x0360
#define WM_SIZEPARENT 0x0361
#define WM_SETMESSAGESTRING 0x0362
#define WM_IDLEUPDATECMDUI 0x0363
#define WM_INITIALUPDATE 0x0364
#define WM_COMMANDHELP 0x0365
#define WM_HELPHITTEST 0x0366
#define WM_EXITHELPMODE 0x0367
#define WM_RECALCPARENT 0x0368
#define WM_SIZECHILD 0x0369
#define WM_KICKIDLE 0x036A
#define WM_QUERYCENTERWND 0x036B
#define WM_DISABLEMODAL 0x036C
#define WM_FLOATSTATUS 0x036D
// WM_FLOATSTATUS flags
#define FS_SHOW 0x01
#define FS_HIDE 0x02
#define FS_ACTIVATE 0x04
#define FS_DEACTIVATE 0x08
#define FS_ENABLE 0x10
#define FS_DISABLE 0x20
#define FS_SYNCACTIVE 0x40
#define WM_ACTIVATETOPLEVEL 0x036E
#define WM_RESERVED_036F 0x036F
#define WM_RESERVED_0370 0x0370
#define WM_RESERVED_0371 0x0371
#define WM_RESERVED_0372 0x0372
#define WM_SOCKET_NOTIFY 0x0373
#define WM_SOCKET_DEAD 0x0374
#define WM_POPMESSAGESTRING 0x0375
#define WM_HELPPROMPTADDR 0x0376
#define WM_OCC_LOADFROMSTREAM 0x0376
#define WM_OCC_LOADFROMSTORAGE 0x0377
#define WM_OCC_INITNEW 0x0378
#define WM_QUEUE_SENTINEL 0x0379
#define WM_OCC_LOADFROMSTREAM_EX 0x037A
#define WM_OCC_LOADFROMSTORAGE_EX 0x037B
#define WM_MFC_INITCTRL 0x037C

// Window class names MFC registers (AfxDeferRegisterClass)
#define AFX_WND_REG 0x00001
#define AFX_WNDCONTROLBAR_REG 0x00002
#define AFX_WNDMDIFRAME_REG 0x00004
#define AFX_WNDFRAMEORVIEW_REG 0x00008
#define AFX_WNDCOMMCTLS_REG 0x00010
#define AFX_WNDOLECONTROL_REG 0x00020
#define AFX_WNDCOMMCTL_UPDOWN_REG 0x00040
#define AFX_WNDCOMMCTL_TREEVIEW_REG 0x00080
#define AFX_WNDCOMMCTL_TAB_REG 0x00100
#define AFX_WNDCOMMCTL_PROGRESS_REG 0x00200
#define AFX_WNDCOMMCTL_LISTVIEW_REG 0x00400
#define AFX_WNDCOMMCTL_HOTKEY_REG 0x00800
#define AFX_WNDCOMMCTL_BAR_REG 0x01000
#define AFX_WNDCOMMCTL_ANIMATE_REG 0x02000
#define AFX_WNDCOMMCTL_INTERNET_REG 0x04000
#define AFX_WNDCOMMCTL_COOL_REG 0x08000
#define AFX_WNDCOMMCTL_USEREX_REG 0x10000
#define AFX_WNDCOMMCTL_DATE_REG 0x20000

#define AFX_WND _T("AfxWnd")
#define AFX_WNDCONTROLBAR _T("AfxControlBar")
#define AFX_WNDMDIFRAME _T("AfxMDIFrame")
#define AFX_WNDFRAMEORVIEW _T("AfxFrameOrView")
#define AFX_WNDOLECONTROL _T("AfxOleControl")

// ---------------------------------------------------------------------
// Helpers used by the message map thunks

inline CWnd* _AfxWnd(WPARAM h) { return CWnd::FromHandle((HWND)h); }
inline CWnd* _AfxWnd(LPARAM h) { return CWnd::FromHandle((HWND)h); }
inline CDC* _AfxDC(WPARAM h) { return CDC::FromHandle((HDC)h); }
inline CMenu* _AfxMenu(WPARAM h) { return CMenu::FromHandle((HMENU)h); }
inline CMenu* _AfxMenu(LPARAM h) { return CMenu::FromHandle((HMENU)h); }
inline CPoint _AfxPt(LPARAM l) { return CPoint((short)LOWORD(l), (short)HIWORD(l)); }
inline UINT _AfxCtlType(UINT msg) {
  return msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC ? msg - WM_CTLCOLORMSGBOX
                                                              : CTLCOLOR_DLG;
}

#endif // __AFXWIN_H__
