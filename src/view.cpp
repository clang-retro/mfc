// view.cpp - CView and CScrollView.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CView, CWnd)

BEGIN_MESSAGE_MAP(CView, CWnd)
  ON_WM_PAINT()
  ON_WM_MOUSEACTIVATE()
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_MESSAGE_VOID(WM_INITIALUPDATE, OnInitialUpdate)
  ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

CView::CView() : m_pDocument(NULL) {}

CView::~CView() {
  if (m_pDocument != NULL) m_pDocument->RemoveView(this);
}

BOOL CView::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WNDFRAMEORVIEW_REG)) return FALSE;
    cs.lpszClass = _afxWndFrameOrView;
  }
  // A plain border becomes the sunken client edge.
  if (cs.style & WS_BORDER) {
    cs.dwExStyle |= WS_EX_CLIENTEDGE;
    cs.style &= ~WS_BORDER;
  }
  return TRUE;
}

int CView::OnCreate(LPCREATESTRUCT lpcs) {
  if (CWnd::OnCreate(lpcs) == -1) return -1;
  CCreateContext* pContext = (CCreateContext*)lpcs->lpCreateParams;
  if (pContext != NULL && pContext->m_pCurrentDoc != NULL) pContext->m_pCurrentDoc->AddView(this);
  return 0;
}

void CView::OnDestroy() {
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL && pFrame->GetActiveView() == this) pFrame->SetActiveView(NULL);
  CWnd::OnDestroy();
}

void CView::PostNcDestroy() { delete this; }

void CView::OnPaint() {
  CPaintDC dc(this);
  OnPrepareDC(&dc);
  OnDraw(&dc);
}

LRESULT CView::OnPrintClient(WPARAM wParam, LPARAM) {
  CDC* pDC = CDC::FromHandle((HDC)wParam);
  OnPrepareDC(pDC);
  OnDraw(pDC);
  return 0;
}

void CView::OnInitialUpdate() { OnUpdate(NULL, 0, NULL); }

void CView::OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) {
  (void)pSender;
  (void)lHint;
  (void)pHint;
  Invalidate(TRUE);
}

BOOL CView::IsSelected(const CObject* pDocItem) const {
  (void)pDocItem;
  return FALSE;
}

void CView::OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView) {
  (void)pDeactiveView;
  if (bActivate && pActivateView == this) {
    // Take the focus if our frame is the active window.
    CFrameWnd* pTopFrame = GetTopLevelFrame();
    if (pTopFrame == NULL || ::GetActiveWindow() == pTopFrame->m_hWnd) {
      HWND hWndFocus = ::GetFocus();
      if (hWndFocus != m_hWnd && !::IsChild(m_hWnd, hWndFocus)) SetFocus();
    }
  }
}

void CView::OnActivateFrame(UINT nState, CFrameWnd* pFrameWnd) {
  (void)nState;
  (void)pFrameWnd;
}

int CView::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) {
  int nResult = CWnd::OnMouseActivate(pDesktopWnd, nHitTest, message);
  if (nResult == MA_NOACTIVATE || nResult == MA_NOACTIVATEANDEAT) return nResult;

  // Clicking a view makes it the active view of its frame.
  CFrameWnd* pParentFrame = GetParentFrame();
  if (pParentFrame != NULL) {
    CView* pView = pParentFrame->GetActiveView();
    HWND hWndFocus = ::GetFocus();
    if (pView == this && m_hWnd != hWndFocus && !::IsChild(m_hWnd, hWndFocus))
      OnActivateView(TRUE, this, this);
    else
      pParentFrame->SetActiveView(this);
  }
  return nResult;
}

BOOL CView::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  if (m_pDocument != NULL) {
    CPushRoutingView push(this);
    return m_pDocument->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
  }
  return FALSE;
}

void CView::CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType) {
  ::AdjustWindowRectEx(lpClientRect, 0, FALSE, GetExStyle());
  if (nAdjustType == adjustOutside) {
    DWORD dwStyle = GetStyle();
    if (dwStyle & WS_VSCROLL) lpClientRect->right += ::GetSystemMetrics(SM_CXVSCROLL);
    if (dwStyle & WS_HSCROLL) lpClientRect->bottom += ::GetSystemMetrics(SM_CYHSCROLL);
  }
}

CScrollBar* CView::GetScrollBarCtrl(int nBar) const {
  (void)nBar;
  return NULL;
}

CSplitterWnd* PASCAL CView::GetParentSplitter(const CWnd* pWnd, BOOL bAnyState) {
  (void)bAnyState;
  if (pWnd == NULL) return NULL;
  CWnd* pParent = pWnd->GetParent();
  if (pParent != NULL && _AfxIsSplitterWnd(pParent)) return (CSplitterWnd*)pParent;
  return NULL;
}

BOOL CView::OnScrollBy(CSize sizeScroll, BOOL bDoScroll) {
  (void)sizeScroll;
  (void)bDoScroll;
  return FALSE;
}

BOOL CView::OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll) {
  (void)nScrollCode;
  (void)nPos;
  (void)bDoScroll;
  return FALSE;
}

// ---------------------------------------------------------------------
// CScrollView

IMPLEMENT_DYNAMIC(CScrollView, CView)

BEGIN_MESSAGE_MAP(CScrollView, CView)
  ON_WM_SIZE()
  ON_WM_HSCROLL()
  ON_WM_VSCROLL()
  ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

#define MM_NONE 0
#define MM_SCALETOFIT (-1)

const SIZE CScrollView::sizeDefault = {0, 0};

CScrollView::CScrollView() {
  m_nMapMode = MM_NONE;
  m_totalLog = CSize(0, 0);
  m_totalDev = CSize(0, 0);
  m_pageDev = CSize(0, 0);
  m_lineDev = CSize(0, 0);
  m_bCenter = FALSE;
  m_bInsideUpdate = FALSE;
}

static void _AfxLPtoDPSize(int nMapMode, CSize& size) {
  if (nMapMode == MM_TEXT) return;
  HDC hDC = ::GetDC(NULL);
  int nOld = ::SetMapMode(hDC, nMapMode);
  ::LPtoDP(hDC, (LPPOINT)&size, 1);
  ::SetMapMode(hDC, nOld);
  ::ReleaseDC(NULL, hDC);
  if (size.cx < 0) size.cx = -size.cx;
  if (size.cy < 0) size.cy = -size.cy;
}

void CScrollView::SetScrollSizes(int nMapMode, SIZE sizeTotal, const SIZE& sizePage,
                                 const SIZE& sizeLine) {
  int nOldMapMode = m_nMapMode;
  m_nMapMode = nMapMode;
  m_totalLog = sizeTotal;

  m_totalDev = m_totalLog;
  _AfxLPtoDPSize(nMapMode, m_totalDev);
  m_pageDev = sizePage;
  _AfxLPtoDPSize(nMapMode, m_pageDev);
  m_lineDev = sizeLine;
  _AfxLPtoDPSize(nMapMode, m_lineDev);

  // A page is a tenth of the document and a line a tenth of a page
  // unless the caller says otherwise.
  if (m_pageDev.cx == 0) m_pageDev.cx = m_totalDev.cx / 10;
  if (m_pageDev.cy == 0) m_pageDev.cy = m_totalDev.cy / 10;
  if (m_lineDev.cx == 0) m_lineDev.cx = m_pageDev.cx / 10;
  if (m_lineDev.cy == 0) m_lineDev.cy = m_pageDev.cy / 10;

  if (m_hWnd != NULL) {
    UpdateBars();
    if (nOldMapMode != m_nMapMode) Invalidate(TRUE);
  }
}

void CScrollView::SetScaleToFitSize(SIZE sizeTotal) {
  m_nMapMode = MM_SCALETOFIT;
  m_totalLog = sizeTotal;
  if (m_hWnd != NULL) {
    CRect rect;
    GetClientRect(&rect);
    m_totalDev = rect.Size();
    SetScrollPos(SB_HORZ, 0);
    SetScrollPos(SB_VERT, 0);
    EnableScrollBarCtrl(SB_BOTH, FALSE);
    Invalidate(TRUE);
  }
}

void CScrollView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo) {
  if (m_nMapMode == MM_SCALETOFIT) {
    pDC->SetMapMode(MM_ANISOTROPIC);
    pDC->SetWindowExt(m_totalLog);
    pDC->SetViewportExt(m_totalDev);
  } else if (m_nMapMode != MM_NONE) {
    pDC->SetMapMode(m_nMapMode);
  }

  CPoint ptVpOrg(0, 0);
  if (!pDC->IsPrinting()) {
    if (m_nMapMode != MM_SCALETOFIT) ptVpOrg = -GetDeviceScrollPosition();
    if (m_bCenter) {
      CRect rect;
      GetClientRect(&rect);
      if (m_totalDev.cx < rect.Width()) ptVpOrg.x = (rect.Width() - m_totalDev.cx) / 2;
      if (m_totalDev.cy < rect.Height()) ptVpOrg.y = (rect.Height() - m_totalDev.cy) / 2;
    }
  }
  pDC->SetViewportOrg(ptVpOrg);
  CView::OnPrepareDC(pDC, pInfo);
}

CPoint CScrollView::GetDeviceScrollPosition() const {
  return CPoint(GetScrollPos(SB_HORZ), GetScrollPos(SB_VERT));
}

CPoint CScrollView::GetScrollPosition() const {
  if (m_nMapMode == MM_SCALETOFIT) return CPoint(0, 0);
  CPoint pt = GetDeviceScrollPosition();
  if (m_nMapMode != MM_TEXT && m_nMapMode != MM_NONE) {
    HDC hDC = ::GetDC(NULL);
    int nOld = ::SetMapMode(hDC, m_nMapMode);
    ::DPtoLP(hDC, &pt, 1);
    ::SetMapMode(hDC, nOld);
    ::ReleaseDC(NULL, hDC);
  }
  return pt;
}

void CScrollView::GetDeviceScrollSizes(int& nMapMode, SIZE& sizePage, SIZE& sizeLine) const {
  nMapMode = m_nMapMode;
  sizePage = m_pageDev;
  sizeLine = m_lineDev;
}

void CScrollView::ScrollToPosition(POINT pt) {
  if (m_nMapMode != MM_TEXT && m_nMapMode != MM_NONE && m_nMapMode != MM_SCALETOFIT) {
    HDC hDC = ::GetDC(NULL);
    int nOld = ::SetMapMode(hDC, m_nMapMode);
    ::LPtoDP(hDC, &pt, 1);
    ::SetMapMode(hDC, nOld);
    ::ReleaseDC(NULL, hDC);
  }
  int xMax = GetScrollLimit(SB_HORZ);
  int yMax = GetScrollLimit(SB_VERT);
  if (pt.x < 0) pt.x = 0; else if (pt.x > xMax) pt.x = xMax;
  if (pt.y < 0) pt.y = 0; else if (pt.y > yMax) pt.y = yMax;
  ScrollToDevicePosition(pt);
}

void CScrollView::ScrollToDevicePosition(POINT ptDev) {
  int xOrig = GetScrollPos(SB_HORZ);
  SetScrollPos(SB_HORZ, ptDev.x);
  int yOrig = GetScrollPos(SB_VERT);
  SetScrollPos(SB_VERT, ptDev.y);
  int x = GetScrollPos(SB_HORZ);
  int y = GetScrollPos(SB_VERT);
  if (x != xOrig || y != yOrig) ScrollWindow(xOrig - x, yOrig - y);
}

void CScrollView::CenterOnPoint(CPoint ptCenter) {
  CRect rect;
  GetClientRect(&rect);
  int xDesired = ptCenter.x - rect.Width() / 2;
  int yDesired = ptCenter.y - rect.Height() / 2;
  DWORD dwStyle = GetStyle();
  if (!(dwStyle & WS_HSCROLL) || xDesired < 0) {
    xDesired = 0;
  } else {
    int xMax = GetScrollLimit(SB_HORZ);
    if (xDesired > xMax) xDesired = xMax;
  }
  if (!(dwStyle & WS_VSCROLL) || yDesired < 0) {
    yDesired = 0;
  } else {
    int yMax = GetScrollLimit(SB_VERT);
    if (yDesired > yMax) yDesired = yMax;
  }
  ScrollToDevicePosition(CPoint(xDesired, yDesired));
}

void CScrollView::GetScrollBarSizes(CSize& sizeSb) {
  sizeSb.cx = ::GetSystemMetrics(SM_CXVSCROLL);
  sizeSb.cy = ::GetSystemMetrics(SM_CYHSCROLL);
}

BOOL CScrollView::GetTrueClientSize(CSize& size, CSize& sizeSb) {
  CRect rect;
  GetClientRect(&rect);
  size = rect.Size();
  GetScrollBarSizes(sizeSb);
  // Count the scroll bars that are showing as part of the client area.
  DWORD dwStyle = GetStyle();
  if (dwStyle & WS_VSCROLL) size.cx += sizeSb.cx;
  if (dwStyle & WS_HSCROLL) size.cy += sizeSb.cy;
  return size.cx > sizeSb.cx && size.cy > sizeSb.cy;
}

void CScrollView::GetScrollBarState(CSize sizeClient, CSize& needSb, CSize& sizeRange,
                                    CPoint& ptMove, BOOL bInsideClient) {
  CSize sizeSb;
  GetScrollBarSizes(sizeSb);

  sizeRange = m_totalDev - sizeClient;
  ptMove = GetDeviceScrollPosition();

  BOOL bNeedH = sizeRange.cx > 0;
  if (!bNeedH)
    ptMove.x = 0;
  else if (bInsideClient)
    sizeRange.cy += sizeSb.cy;  // the bar takes room from the height

  BOOL bNeedV = sizeRange.cy > 0;
  if (!bNeedV)
    ptMove.y = 0;
  else if (bInsideClient)
    sizeRange.cx += sizeSb.cx;

  // The vertical bar may make the horizontal one necessary after all.
  if (bNeedV && !bNeedH && sizeRange.cx > 0) {
    bNeedH = TRUE;
    sizeRange.cy += sizeSb.cy;
  }

  if (bNeedH && ptMove.x > sizeRange.cx) ptMove.x = sizeRange.cx;
  if (bNeedV && ptMove.y > sizeRange.cy) ptMove.y = sizeRange.cy;
  needSb.cx = bNeedH;
  needSb.cy = bNeedV;
}

void CScrollView::UpdateBars() {
  if (m_bInsideUpdate || m_nMapMode == MM_SCALETOFIT) return;
  m_bInsideUpdate = TRUE;

  CSize sizeClient, sizeSb;
  if (!GetTrueClientSize(sizeClient, sizeSb)) {
    // Too small for scroll bars.
    CRect rect;
    GetClientRect(&rect);
    if (rect.right > 0 && rect.bottom > 0) EnableScrollBarCtrl(SB_BOTH, FALSE);
    m_bInsideUpdate = FALSE;
    return;
  }

  CSize needSb, sizeRange;
  CPoint ptMove;
  GetScrollBarState(sizeClient, needSb, sizeRange, ptMove, TRUE);
  ScrollToDevicePosition(ptMove);

  SCROLLINFO info;
  info.cbSize = sizeof(info);
  info.fMask = SIF_PAGE | SIF_RANGE;
  info.nMin = 0;

  EnableScrollBarCtrl(SB_HORZ, needSb.cx);
  if (needSb.cx) {
    info.nPage = sizeClient.cx - (needSb.cy ? sizeSb.cx : 0);
    info.nMax = m_totalDev.cx - 1;
    SetScrollInfo(SB_HORZ, &info, TRUE);
  }
  EnableScrollBarCtrl(SB_VERT, needSb.cy);
  if (needSb.cy) {
    info.nPage = sizeClient.cy - (needSb.cx ? sizeSb.cy : 0);
    info.nMax = m_totalDev.cy - 1;
    SetScrollInfo(SB_VERT, &info, TRUE);
  }
  m_bInsideUpdate = FALSE;
}

void CScrollView::CheckScrollBars(BOOL& bHasHorzBar, BOOL& bHasVertBar) const {
  DWORD dwStyle = GetStyle();
  bHasHorzBar = (dwStyle & WS_HSCROLL) != 0;
  bHasVertBar = (dwStyle & WS_VSCROLL) != 0;
}

void CScrollView::CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType) {
  if (nAdjustType == adjustOutside) {
    CSize sizeClient(lpClientRect->right - lpClientRect->left,
                     lpClientRect->bottom - lpClientRect->top);
    if (m_totalDev.cx > sizeClient.cx) lpClientRect->bottom += ::GetSystemMetrics(SM_CYHSCROLL);
    if (m_totalDev.cy > sizeClient.cy) lpClientRect->right += ::GetSystemMetrics(SM_CXVSCROLL);
  }
  ::AdjustWindowRectEx(lpClientRect, 0, FALSE, GetExStyle());
}

void CScrollView::FillOutsideRect(CDC* pDC, CBrush* pBrush) {
  CRect rect;
  GetClientRect(&rect);
  CPoint ptOrg = pDC->GetViewportOrg();
  CRect rectTotal(ptOrg.x, ptOrg.y, ptOrg.x + m_totalDev.cx, ptOrg.y + m_totalDev.cy);

  int nSaved = pDC->SaveDC();
  pDC->SetMapMode(MM_TEXT);
  pDC->SetViewportOrg(0, 0);
  pDC->SetWindowOrg(0, 0);
  if (rectTotal.right < rect.right)
    pDC->FillRect(CRect(rectTotal.right, rect.top, rect.right, rect.bottom), pBrush);
  if (rectTotal.bottom < rect.bottom)
    pDC->FillRect(CRect(rect.left, rectTotal.bottom, rectTotal.right, rect.bottom), pBrush);
  if (rectTotal.left > rect.left)
    pDC->FillRect(CRect(rect.left, rect.top, rectTotal.left, rect.bottom), pBrush);
  if (rectTotal.top > rect.top)
    pDC->FillRect(CRect(rectTotal.left, rect.top, rectTotal.right, rectTotal.top), pBrush);
  pDC->RestoreDC(nSaved);
}

void CScrollView::ResizeParentToFit(BOOL bShrinkOnly) {
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame == NULL || pFrame->IsIconic() || pFrame->IsZoomed()) return;

  CSize sizeClient, sizeSb;
  GetTrueClientSize(sizeClient, sizeSb);
  CSize sizeWanted = m_totalDev;
  if (bShrinkOnly) {
    if (sizeWanted.cx > sizeClient.cx) sizeWanted.cx = sizeClient.cx;
    if (sizeWanted.cy > sizeClient.cy) sizeWanted.cy = sizeClient.cy;
  }
  CSize delta = sizeWanted - sizeClient;
  if (delta.cx == 0 && delta.cy == 0) return;

  CRect rectFrame;
  pFrame->GetWindowRect(&rectFrame);
  pFrame->SetWindowPos(NULL, 0, 0, rectFrame.Width() + delta.cx, rectFrame.Height() + delta.cy,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CScrollView::OnSize(UINT nType, int cx, int cy) {
  CView::OnSize(nType, cx, cy);
  if (m_nMapMode == MM_SCALETOFIT) {
    m_totalDev = CSize(cx, cy);
    Invalidate(TRUE);
  } else {
    UpdateBars();
  }
}

void CScrollView::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  if (pScrollBar != NULL) return;  // a scroll bar control, not ours
  OnScroll(MAKEWORD(nSBCode, 0xFF), nPos);
}

void CScrollView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  if (pScrollBar != NULL) return;
  OnScroll(MAKEWORD(0xFF, nSBCode), nPos);
}

static int _AfxScrollCodeToPos(CWnd* pWnd, int nBar, UINT nCode, int nPos, int nLine, int nPage) {
  switch (nCode) {
    case SB_TOP: return 0;
    case SB_BOTTOM: return INT_MAX;
    case SB_LINEUP: return nPos - nLine;
    case SB_LINEDOWN: return nPos + nLine;
    case SB_PAGEUP: return nPos - nPage;
    case SB_PAGEDOWN: return nPos + nPage;
    case SB_THUMBTRACK: {
      // The message only carries 16 bits of position.
      SCROLLINFO info;
      info.cbSize = sizeof(info);
      info.fMask = SIF_TRACKPOS;
      if (::GetScrollInfo(pWnd->m_hWnd, nBar, &info)) return info.nTrackPos;
      return nPos;
    }
  }
  return nPos;
}

BOOL CScrollView::OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll) {
  (void)nPos;
  int xOrig = GetScrollPos(SB_HORZ);
  int x = _AfxScrollCodeToPos(this, SB_HORZ, LOBYTE(nScrollCode), xOrig, m_lineDev.cx,
                              m_pageDev.cx);
  int yOrig = GetScrollPos(SB_VERT);
  int y = _AfxScrollCodeToPos(this, SB_VERT, HIBYTE(nScrollCode), yOrig, m_lineDev.cy,
                              m_pageDev.cy);

  // INT_MAX - xOrig cannot overflow; clamp before taking the difference.
  int xMax = GetScrollLimit(SB_HORZ);
  int yMax = GetScrollLimit(SB_VERT);
  if (x > xMax) x = xMax;
  if (y > yMax) y = yMax;
  BOOL bResult = OnScrollBy(CSize(x - xOrig, y - yOrig), bDoScroll);
  if (bResult && bDoScroll) UpdateWindow();
  return bResult;
}

BOOL CScrollView::OnScrollBy(CSize sizeScroll, BOOL bDoScroll) {
  DWORD dwStyle = GetStyle();
  if (!(dwStyle & WS_VSCROLL)) sizeScroll.cy = 0;
  if (!(dwStyle & WS_HSCROLL)) sizeScroll.cx = 0;

  int xOrig = GetScrollPos(SB_HORZ);
  int x = xOrig + sizeScroll.cx;
  int xMax = GetScrollLimit(SB_HORZ);
  if (x < 0) x = 0; else if (x > xMax) x = xMax;

  int yOrig = GetScrollPos(SB_VERT);
  int y = yOrig + sizeScroll.cy;
  int yMax = GetScrollLimit(SB_VERT);
  if (y < 0) y = 0; else if (y > yMax) y = yMax;

  if (x == xOrig && y == yOrig) return FALSE;
  if (bDoScroll) {
    ScrollWindow(-(x - xOrig), -(y - yOrig));
    if (x != xOrig) SetScrollPos(SB_HORZ, x);
    if (y != yOrig) SetScrollPos(SB_VERT, y);
  }
  return TRUE;
}

BOOL CScrollView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  return DoMouseWheel(nFlags, zDelta, pt);
}

BOOL CScrollView::DoMouseWheel(UINT fFlags, short zDelta, CPoint point) {
  (void)point;
  if (fFlags & (MK_SHIFT | MK_CONTROL)) return FALSE;
  BOOL bHasHorzBar, bHasVertBar;
  CheckScrollBars(bHasHorzBar, bHasVertBar);
  if (!bHasVertBar && !bHasHorzBar) return FALSE;

  UINT uLines = 3;
  ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &uLines, 0);

  int nLine = bHasVertBar ? m_lineDev.cy : m_lineDev.cx;
  int nPage = bHasVertBar ? m_pageDev.cy : m_pageDev.cx;
  int nDisplacement;
  if (uLines == WHEEL_PAGESCROLL) {
    nDisplacement = zDelta > 0 ? -nPage : nPage;
  } else {
    nDisplacement = -(int)zDelta * (int)uLines * nLine / WHEEL_DELTA;
    if (nDisplacement > nPage) nDisplacement = nPage;
    if (nDisplacement < -nPage) nDisplacement = -nPage;
  }
  BOOL bResult = bHasVertBar ? OnScrollBy(CSize(0, nDisplacement), TRUE)
                             : OnScrollBy(CSize(nDisplacement, 0), TRUE);
  if (bResult) UpdateWindow();
  return bResult;
}
