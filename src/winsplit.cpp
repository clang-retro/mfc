// winsplit.cpp - CSplitterWnd: panes in rows and columns, separated by
// bars the user can drag.
//
// Each pane is a view with its own sunken edge; the bars between them are
// m_cxSplitterGap - 2 * m_cxBorder pixels of button face.
//
// With WS_HSCROLL or WS_VSCROLL the splitter owns the scroll bars: one
// per column along the bottom (AFX_IDW_HSCROLL_FIRST + col) and one per
// row down the right (AFX_IDW_VSCROLL_FIRST + row), shared by the panes
// in that column or row, so they scroll together. A dynamic splitter
// that can still split has a split box at the end of the first scroll
// bar; dragging it out splits, and dragging a bar to the edge unsplits.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxext.h>

IMPLEMENT_DYNAMIC(CSplitterWnd, CWnd)

BOOL AFXAPI _AfxIsSplitterWnd(CWnd* pWnd) {
  return pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CSplitterWnd));
}

BEGIN_MESSAGE_MAP(CSplitterWnd, CWnd)
  ON_WM_SETCURSOR()
  ON_WM_MOUSEMOVE()
  ON_WM_PAINT()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_LBUTTONUP()
  ON_WM_CANCELMODE()
  ON_WM_KEYDOWN()
  ON_WM_SIZE()
  ON_WM_HSCROLL()
  ON_WM_VSCROLL()
  ON_WM_MOUSEWHEEL()
  ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

CSplitterWnd::CSplitterWnd() {
  m_pDynamicViewClass = NULL;
  m_nMaxRows = m_nMaxCols = 0;
  m_nRows = m_nCols = 0;
  m_cxBorder = m_cyBorder = 2;
  m_cxSplitterGap = m_cySplitterGap = 7;
  m_cxSplitter = m_cySplitter = 7;
  m_cxBorderShare = m_cyBorderShare = 0;
  m_bHasHScroll = m_bHasVScroll = FALSE;
  m_pColInfo = m_pRowInfo = NULL;
  m_bTracking = m_bTracking2 = FALSE;
  m_htTrack = noHit;
}

CSplitterWnd::~CSplitterWnd() {
  delete[] m_pRowInfo;
  delete[] m_pColInfo;
}

BOOL CSplitterWnd::CreateCommon(CWnd* pParentWnd, SIZE sizeMin, DWORD dwStyle, UINT nID) {
  delete[] m_pRowInfo;
  delete[] m_pColInfo;
  m_pRowInfo = new CRowColInfo[m_nMaxRows];
  m_pColInfo = new CRowColInfo[m_nMaxCols];
  for (int i = 0; i < m_nMaxRows; i++) {
    m_pRowInfo[i].nMinSize = sizeMin.cy;
    m_pRowInfo[i].nIdealSize = sizeMin.cy;
    m_pRowInfo[i].nCurSize = -1;
  }
  for (int i = 0; i < m_nMaxCols; i++) {
    m_pColInfo[i].nMinSize = sizeMin.cx;
    m_pColInfo[i].nIdealSize = sizeMin.cx;
    m_pColInfo[i].nCurSize = -1;
  }

  // The panes have their own scroll bars.
  m_bHasHScroll = (dwStyle & WS_HSCROLL) != 0;
  m_bHasVScroll = (dwStyle & WS_VSCROLL) != 0;
  dwStyle &= ~(WS_HSCROLL | WS_VSCROLL | SPLS_DYNAMIC_SPLIT | SPLS_INVERT_TRACKER);
  dwStyle |= WS_CHILD;

  // Created empty: the frame's layout gives the splitter its size after
  // the panes exist, and the WM_SIZE that brings lays them out.
  if (!AfxDeferRegisterClass(AFX_WND_REG)) return FALSE;
  if (!CreateEx(0, _afxWnd, NULL, dwStyle, CRect(0, 0, 0, 0), pParentWnd, nID, NULL))
    return FALSE;

  // Each pane draws its own sunken edge, so the frame does not need one.
  if (pParentWnd->GetExStyle() & WS_EX_CLIENTEDGE)
    pParentWnd->ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
  return TRUE;
}

BOOL CSplitterWnd::Create(CWnd* pParentWnd, int nMaxRows, int nMaxCols, SIZE sizeMin,
                          CCreateContext* pContext, DWORD dwStyle, UINT nID) {
  if (pContext == NULL || pContext->m_pNewViewClass == NULL) return FALSE;
  if (nMaxRows < 1 || nMaxRows > 2 || nMaxCols < 1 || nMaxCols > 2) return FALSE;
  m_nMaxRows = nMaxRows;
  m_nMaxCols = nMaxCols;
  m_nRows = m_nCols = 1;
  m_pDynamicViewClass = pContext->m_pNewViewClass;
  if (!CreateCommon(pParentWnd, sizeMin, dwStyle, nID)) return FALSE;
  if (!CreateView(0, 0, m_pDynamicViewClass, sizeMin, pContext)) {
    DestroyWindow();
    return FALSE;
  }
  m_pColInfo[0].nIdealSize = sizeMin.cx;
  m_pRowInfo[0].nIdealSize = sizeMin.cy;
  return TRUE;
}

BOOL CSplitterWnd::CreateStatic(CWnd* pParentWnd, int nRows, int nCols, DWORD dwStyle, UINT nID) {
  if (nRows < 1 || nRows > 16 || nCols < 1 || nCols > 16) return FALSE;
  m_nRows = m_nMaxRows = nRows;
  m_nCols = m_nMaxCols = nCols;
  return CreateCommon(pParentWnd, CSize(0, 0), dwStyle, nID);
}

BOOL CSplitterWnd::CreateView(int row, int col, CRuntimeClass* pViewClass, SIZE sizeInit,
                              CCreateContext* pContext) {
  if (row < 0 || row >= m_nRows || col < 0 || col >= m_nCols) return FALSE;
  if (GetDlgItem(IdFromRowCol(row, col)) != NULL) return FALSE;  // already there

  m_pColInfo[col].nIdealSize = sizeInit.cx;
  m_pRowInfo[row].nIdealSize = sizeInit.cy;

  // Without a context, the new view shows the active pane's document.
  BOOL bSendInitialUpdate = FALSE;
  CCreateContext contextT;
  if (pContext == NULL) {
    CView* pOldView = (CView*)GetActivePane();
    if (pOldView != NULL && pOldView->IsKindOf(RUNTIME_CLASS(CView))) {
      contextT.m_pLastView = pOldView;
      contextT.m_pCurrentDoc = pOldView->GetDocument();
      if (contextT.m_pCurrentDoc != NULL)
        contextT.m_pNewDocTemplate = contextT.m_pCurrentDoc->GetDocTemplate();
    }
    contextT.m_pCurrentFrame = GetParentFrame();
    bSendInitialUpdate = TRUE;
    pContext = &contextT;
  }

  // The view uses the splitter's scroll bars, so they come first.
  _SyncScrollBars();
  CWnd* pWnd = (CWnd*)pViewClass->CreateObject();
  if (pWnd == NULL) return FALSE;
  DWORD dwStyle = AFX_WS_DEFAULT_VIEW;
  CRect rect(CPoint(0, 0), sizeInit);
  if (!pWnd->Create(NULL, NULL, dwStyle, rect, this, IdFromRowCol(row, col), pContext))
    return FALSE;  // the view deletes itself
  if (bSendInitialUpdate) pWnd->SendMessage(WM_INITIALUPDATE);
  return TRUE;
}

// ---------------------------------------------------------------------
// Panes

CWnd* CSplitterWnd::GetPane(int row, int col) const { return GetDlgItem(IdFromRowCol(row, col)); }

BOOL CSplitterWnd::IsChildPane(CWnd* pWnd, int* pRow, int* pCol) {
  if (pWnd == NULL || ::GetParent(pWnd->m_hWnd) != m_hWnd) return FALSE;
  int nID = pWnd->GetDlgCtrlID();
  if (nID < AFX_IDW_PANE_FIRST || nID > AFX_IDW_PANE_LAST) return FALSE;
  int row = (nID - AFX_IDW_PANE_FIRST) / 16;
  int col = (nID - AFX_IDW_PANE_FIRST) % 16;
  if (row >= m_nRows || col >= m_nCols) return FALSE;
  if (pRow != NULL) *pRow = row;
  if (pCol != NULL) *pCol = col;
  return TRUE;
}

void CSplitterWnd::GetRowInfo(int row, int& cyCur, int& cyMin) const {
  cyCur = m_pRowInfo[row].nCurSize;
  cyMin = m_pRowInfo[row].nMinSize;
}

void CSplitterWnd::SetRowInfo(int row, int cyIdeal, int cyMin) {
  m_pRowInfo[row].nIdealSize = cyIdeal;
  m_pRowInfo[row].nMinSize = cyMin;
}

void CSplitterWnd::GetColumnInfo(int col, int& cxCur, int& cxMin) const {
  cxCur = m_pColInfo[col].nCurSize;
  cxMin = m_pColInfo[col].nMinSize;
}

void CSplitterWnd::SetColumnInfo(int col, int cxIdeal, int cxMin) {
  m_pColInfo[col].nIdealSize = cxIdeal;
  m_pColInfo[col].nMinSize = cxMin;
}

DWORD CSplitterWnd::GetScrollStyle() const {
  return (m_bHasHScroll ? WS_HSCROLL : 0) | (m_bHasVScroll ? WS_VSCROLL : 0);
}

void CSplitterWnd::SetScrollStyle(DWORD dwStyle) {
  m_bHasHScroll = (dwStyle & WS_HSCROLL) != 0;
  m_bHasVScroll = (dwStyle & WS_VSCROLL) != 0;
  RecalcLayout();
}

CWnd* CSplitterWnd::GetActivePane(int* pRow, int* pCol) {
  CWnd* pView = NULL;
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL) pView = pFrame->GetActiveView();
  if (pView == NULL) pView = GetFocus();
  if (pView != NULL && !IsChildPane(pView, pRow, pCol)) pView = NULL;
  return pView;
}

void CSplitterWnd::SetActivePane(int row, int col, CWnd* pWnd) {
  CWnd* pPane = pWnd != NULL ? pWnd : GetPane(row, col);
  if (pPane == NULL) return;
  CFrameWnd* pFrame = GetParentFrame();
  if (pPane->IsKindOf(RUNTIME_CLASS(CView)) && pFrame != NULL)
    pFrame->SetActiveView((CView*)pPane);
  else
    pPane->SetFocus();
}

BOOL CSplitterWnd::CanActivateNext(BOOL bPrev) {
  (void)bPrev;
  return m_nRows * m_nCols > 1 && GetActivePane() != NULL;
}

void CSplitterWnd::ActivateNext(BOOL bPrev) {
  int row = 0, col = 0;
  if (GetActivePane(&row, &col) == NULL) return;
  int n = row * m_nCols + col;
  int nTotal = m_nRows * m_nCols;
  n = (n + (bPrev ? nTotal - 1 : 1)) % nTotal;
  SetActivePane(n / m_nCols, n % m_nCols);
}

// ---------------------------------------------------------------------
// Layout

// The scroll bar strips: a dynamic splitter has one for its split box
// even without scroll bars.
BOOL CSplitterWnd::_HasHStrip() const {
  return m_bHasHScroll || (m_pDynamicViewClass != NULL && m_nMaxCols > 1);
}

BOOL CSplitterWnd::_HasVStrip() const {
  return m_bHasVScroll || (m_pDynamicViewClass != NULL && m_nMaxRows > 1);
}

// The box on the vertical strip splits rows; the one on the horizontal
// strip splits columns.
BOOL CSplitterWnd::_ShowVBox() const {
  return m_pDynamicViewClass != NULL && m_nRows < m_nMaxRows;
}

BOOL CSplitterWnd::_ShowHBox() const {
  return m_pDynamicViewClass != NULL && m_nCols < m_nMaxCols;
}

void CSplitterWnd::GetInsideRect(CRect& rect) const {
  GetClientRect(&rect);
  if (_HasVStrip()) rect.right -= ::GetSystemMetrics(SM_CXVSCROLL);
  if (_HasHStrip()) rect.bottom -= ::GetSystemMetrics(SM_CYHSCROLL);
  if (rect.right < rect.left) rect.right = rect.left;
  if (rect.bottom < rect.top) rect.bottom = rect.top;
}

// Makes the shared scroll bars match the rows and columns.
void CSplitterWnd::_SyncScrollBars() {
  if (m_hWnd == NULL) return;
  for (int i = 0; i < 16; i++) {
    HWND hWnd = ::GetDlgItem(m_hWnd, AFX_IDW_HSCROLL_FIRST + i);
    BOOL bWant = m_bHasHScroll && i < m_nCols;
    if (bWant && hWnd == NULL)
      CreateScrollBarCtrl(SBS_HORZ | WS_DISABLED, AFX_IDW_HSCROLL_FIRST + i);
    else if (!bWant && hWnd != NULL)
      ::DestroyWindow(hWnd);
    hWnd = ::GetDlgItem(m_hWnd, AFX_IDW_VSCROLL_FIRST + i);
    bWant = m_bHasVScroll && i < m_nRows;
    if (bWant && hWnd == NULL)
      CreateScrollBarCtrl(SBS_VERT | WS_DISABLED, AFX_IDW_VSCROLL_FIRST + i);
    else if (!bWant && hWnd != NULL)
      ::DestroyWindow(hWnd);
  }
}

// Computes each row's or column's size: the panes keep their ideal
// sizes and the last one takes what is left.
static void _AfxLayoutRowCol(CSplitterWnd::CRowColInfo* pInfo, int nCount, int nSize,
                             int nBorder2, int nGap) {
  int nLeft = nSize;
  for (int i = 0; i < nCount; i++) {
    int nOuter;
    if (i == nCount - 1) {
      nOuter = nLeft;
    } else {
      nOuter = pInfo[i].nIdealSize + nBorder2;
      // Leave at least the borders of the panes that follow.
      int nRest = (nCount - 1 - i) * (nBorder2 + nGap);
      if (nOuter > nLeft - nRest) nOuter = nLeft - nRest;
    }
    if (nOuter < nBorder2) nOuter = nBorder2;
    pInfo[i].nCurSize = nOuter - nBorder2;
    nLeft -= nOuter + nGap;
  }
}

void CSplitterWnd::RecalcLayout() {
  if (m_hWnd == NULL || m_pRowInfo == NULL) return;
  _SyncScrollBars();
  CRect rectInside, rectClient;
  GetInsideRect(rectInside);
  GetClientRect(&rectClient);
  int cxGap = m_cxSplitterGap - 2 * m_cxBorder;
  int cyGap = m_cySplitterGap - 2 * m_cyBorder;
  _AfxLayoutRowCol(m_pColInfo, m_nCols, rectInside.Width(), 2 * m_cxBorder, cxGap);
  _AfxLayoutRowCol(m_pRowInfo, m_nRows, rectInside.Height(), 2 * m_cyBorder, cyGap);

  AFX_SIZEPARENTPARAMS layout;
  memset(&layout, 0, sizeof(layout));
  layout.hDWP = ::BeginDeferWindowPos(m_nRows * m_nCols + m_nRows + m_nCols);
  int y = rectInside.top;
  for (int row = 0; row < m_nRows; row++) {
    int cy = m_pRowInfo[row].nCurSize + 2 * m_cyBorder;
    int x = rectInside.left;
    for (int col = 0; col < m_nCols; col++) {
      int cx = m_pColInfo[col].nCurSize + 2 * m_cxBorder;
      CWnd* pPane = GetPane(row, col);
      if (pPane != NULL) {
        CRect rect(x, y, x + cx, y + cy);
        AfxRepositionWindow(&layout, pPane->m_hWnd, &rect);
      }
      // Each column's scroll bar runs under its panes.
      HWND hBar;
      if (row == 0 && (hBar = ::GetDlgItem(m_hWnd, AFX_IDW_HSCROLL_FIRST + col)) != NULL) {
        CRect rect(x, rectInside.bottom, x + cx, rectClient.bottom);
        if (col == 0 && _ShowHBox()) rect.left += m_cxSplitter;
        if (rect.right < rect.left) rect.right = rect.left;
        AfxRepositionWindow(&layout, hBar, &rect);
      }
      x += cx + cxGap;
    }
    // ...and each row's down the right.
    HWND hBar = ::GetDlgItem(m_hWnd, AFX_IDW_VSCROLL_FIRST + row);
    if (hBar != NULL) {
      CRect rect(rectInside.right, y, rectClient.right, y + cy);
      if (row == 0 && _ShowVBox()) rect.top += m_cySplitter;
      if (rect.bottom < rect.top) rect.bottom = rect.top;
      AfxRepositionWindow(&layout, hBar, &rect);
    }
    y += cy + cyGap;
  }
  if (layout.hDWP != NULL) ::EndDeferWindowPos(layout.hDWP);
  Invalidate();
}

void CSplitterWnd::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  if (nType != SIZE_MINIMIZED && cx > 0 && cy > 0) RecalcLayout();
}

// The rectangle of a bar, in client coordinates.
void CSplitterWnd::GetHitRect(int ht, CRect& rect) {
  CRect rectInside;
  GetInsideRect(rectInside);
  int cxGap = m_cxSplitterGap - 2 * m_cxBorder;
  int cyGap = m_cySplitterGap - 2 * m_cyBorder;
  // A bar also crosses the scroll bar strip.
  GetClientRect(&rect);
  if (ht == vSplitterBox) {
    CRect rectClient = rect;
    rect.SetRect(rectInside.right, rectInside.top, rectClient.right, rectInside.top + m_cySplitter);
  } else if (ht == hSplitterBox) {
    CRect rectClient = rect;
    rect.SetRect(rectInside.left, rectInside.bottom, rectInside.left + m_cxSplitter,
                 rectClient.bottom);
  } else if (ht >= vSplitterBar1 && ht < vSplitterBar1 + 15) {
    int row = ht - vSplitterBar1;
    int y = rectInside.top;
    for (int i = 0; i <= row; i++) y += m_pRowInfo[i].nCurSize + 2 * m_cyBorder;
    rect.top = y;
    rect.bottom = y + cyGap;
  } else if (ht >= hSplitterBar1 && ht < hSplitterBar1 + 15) {
    int col = ht - hSplitterBar1;
    int x = rectInside.left;
    for (int i = 0; i <= col; i++) x += m_pColInfo[i].nCurSize + 2 * m_cxBorder;
    rect.left = x;
    rect.right = x + cxGap;
  } else if (ht >= splitterIntersection1 && ht < splitterIntersection1 + 225) {
    int n = ht - splitterIntersection1;
    CRect rectRow, rectCol;
    GetHitRect(vSplitterBar1 + n / 15, rectRow);
    GetHitRect(hSplitterBar1 + n % 15, rectCol);
    rect.SetRect(rectCol.left, rectRow.top, rectCol.right, rectRow.bottom);
  } else {
    rect.SetRectEmpty();
  }
}

int CSplitterWnd::HitTest(CPoint pt) const {
  CSplitterWnd* pThis = (CSplitterWnd*)this;
  CRect rectBox;
  if (_ShowVBox() && _HasVStrip()) {
    pThis->GetHitRect(vSplitterBox, rectBox);
    if (rectBox.PtInRect(pt)) return vSplitterBox;
  }
  if (_ShowHBox() && _HasHStrip()) {
    pThis->GetHitRect(hSplitterBox, rectBox);
    if (rectBox.PtInRect(pt)) return hSplitterBox;
  }
  int nRowHit = -1, nColHit = -1;
  for (int row = 0; row < m_nRows - 1; row++) {
    CRect rect;
    pThis->GetHitRect(vSplitterBar1 + row, rect);
    if (pt.y >= rect.top && pt.y < rect.bottom) nRowHit = row;
  }
  for (int col = 0; col < m_nCols - 1; col++) {
    CRect rect;
    pThis->GetHitRect(hSplitterBar1 + col, rect);
    if (pt.x >= rect.left && pt.x < rect.right) nColHit = col;
  }
  if (nRowHit >= 0 && nColHit >= 0) return splitterIntersection1 + nRowHit * 15 + nColHit;
  if (nRowHit >= 0) return vSplitterBar1 + nRowHit;
  if (nColHit >= 0) return hSplitterBar1 + nColHit;
  return noHit;
}

// ---------------------------------------------------------------------
// Drawing

void CSplitterWnd::OnDrawSplitter(CDC* pDC, int nType, const CRect& rect) {
  if (pDC == NULL) {
    RedrawWindow(&rect, NULL, RDW_INVALIDATE | RDW_NOCHILDREN);
    return;
  }
  switch (nType) {
    case splitBorder:
      pDC->Draw3dRect(&rect, ::GetSysColor(COLOR_BTNSHADOW), ::GetSysColor(COLOR_BTNHIGHLIGHT));
      break;
    case splitBox: {
      // A small raised button.
      CRect rectBox = rect;
      pDC->FillSolidRect(&rectBox, ::GetSysColor(COLOR_BTNFACE));
      pDC->Draw3dRect(&rectBox, ::GetSysColor(COLOR_BTNFACE), ::GetSysColor(COLOR_WINDOWFRAME));
      rectBox.DeflateRect(1, 1);
      pDC->Draw3dRect(&rectBox, ::GetSysColor(COLOR_BTNHIGHLIGHT), ::GetSysColor(COLOR_BTNSHADOW));
      break;
    }
    default:
      pDC->FillSolidRect(&rect, ::GetSysColor(COLOR_BTNFACE));
      break;
  }
}

void CSplitterWnd::DrawAllSplitBars(CDC* pDC, int cxInside, int cyInside) {
  (void)cxInside;
  (void)cyInside;
  for (int row = 0; row < m_nRows - 1; row++) {
    CRect rect;
    GetHitRect(vSplitterBar1 + row, rect);
    OnDrawSplitter(pDC, splitBar, rect);
  }
  for (int col = 0; col < m_nCols - 1; col++) {
    CRect rect;
    GetHitRect(hSplitterBar1 + col, rect);
    OnDrawSplitter(pDC, splitBar, rect);
  }
  CRect rect;
  if (_ShowVBox() && _HasVStrip()) {
    GetHitRect(vSplitterBox, rect);
    OnDrawSplitter(pDC, splitBox, rect);
  }
  if (_ShowHBox() && _HasHStrip()) {
    GetHitRect(hSplitterBox, rect);
    OnDrawSplitter(pDC, splitBox, rect);
  }
}

void CSplitterWnd::OnPaint() {
  CPaintDC dc(this);
  CRect rect;
  GetClientRect(&rect);
  // Anything not covered by a pane (a missing pane, or slack) is face.
  dc.FillSolidRect(&rect, ::GetSysColor(COLOR_BTNFACE));
  DrawAllSplitBars(&dc, rect.Width(), rect.Height());
}

void CSplitterWnd::OnInvertTracker(const CRect& rect) {
  CDC* pDC = GetDC();
  CBrush* pBrush = CDC::GetHalftoneBrush();
  CBrush* pOldBrush = pDC->SelectObject(pBrush);
  pDC->PatBlt(rect.left, rect.top, rect.Width(), rect.Height(), PATINVERT);
  pDC->SelectObject(pOldBrush);
  ReleaseDC(pDC);
}

// The bar is a plain window: GetScrollBarCtrl finds it by its ID.
BOOL CSplitterWnd::CreateScrollBarCtrl(DWORD dwStyle, UINT nID) {
  HWND hWnd = ::CreateWindowEx(0, _T("SCROLLBAR"), NULL, dwStyle | WS_VISIBLE | WS_CHILD, 0, 0,
                               1, 1, m_hWnd, (HMENU)(UINT_PTR)nID,
                               AfxGetInstanceHandle(), NULL);
  return hWnd != NULL;
}

// ---------------------------------------------------------------------
// Tracking the bars and boxes with the mouse or the keyboard

// Whether a hit moves a tracker up and down, or left and right.
static BOOL _AfxTracksY(int ht) {
  return ht == CSplitterWnd::vSplitterBox || ht == CSplitterWnd::bothSplitterBox ||
         (ht >= CSplitterWnd::vSplitterBar1 && ht < CSplitterWnd::hSplitterBar1) ||
         ht >= CSplitterWnd::splitterIntersection1;
}

static BOOL _AfxTracksX(int ht) {
  return ht == CSplitterWnd::hSplitterBox || ht == CSplitterWnd::bothSplitterBox ||
         ht >= CSplitterWnd::hSplitterBar1;
}

static BOOL _AfxIsBox(int ht) {
  return ht == CSplitterWnd::vSplitterBox || ht == CSplitterWnd::hSplitterBox ||
         ht == CSplitterWnd::bothSplitterBox;
}

void CSplitterWnd::SetSplitCursor(int ht) {
  LPCTSTR lpszCursor = IDC_ARROW;
  BOOL bY = _AfxTracksY(ht), bX = _AfxTracksX(ht);
  if (bX && bY)
    lpszCursor = IDC_SIZEALL;
  else if (bX)
    lpszCursor = IDC_SIZEWE;
  else if (bY)
    lpszCursor = IDC_SIZENS;
  ::SetCursor(::LoadCursor(NULL, lpszCursor));
}

BOOL CSplitterWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (nHitTest == HTCLIENT && pWnd == this && !m_bTracking) return TRUE;  // OnMouseMove sets it
  return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

CWnd* CSplitterWnd::GetSizingParent() { return GetParent(); }

void CSplitterWnd::StartTracking(int ht) {
  if (ht == noHit) return;
  CRect rectClient;
  GetClientRect(&rectClient);
  GetInsideRect(m_rectLimit);
  int cxGap = m_cxSplitterGap - 2 * m_cxBorder;
  int cyGap = m_cySplitterGap - 2 * m_cyBorder;
  m_htTrack = ht;
  m_bTracking2 = FALSE;
  // The first tracker is the one that moves up and down; with two, the
  // second moves left and right.
  CRect rectRow(rectClient.left, m_rectLimit.top, rectClient.right, m_rectLimit.top + cyGap);
  CRect rectCol(m_rectLimit.left, rectClient.top, m_rectLimit.left + cxGap, rectClient.bottom);
  if (ht == vSplitterBox) {
    m_rectTracker = rectRow;
  } else if (ht == hSplitterBox) {
    m_rectTracker = rectCol;
  } else if (ht == bothSplitterBox) {
    m_rectTracker = rectRow;
    m_rectTracker2 = rectCol;
    m_bTracking2 = TRUE;
  } else if (ht >= splitterIntersection1) {
    int n = ht - splitterIntersection1;
    GetHitRect(vSplitterBar1 + n / 15, m_rectTracker);
    GetHitRect(hSplitterBar1 + n % 15, m_rectTracker2);
    m_bTracking2 = TRUE;
  } else {
    GetHitRect(ht, m_rectTracker);
  }
  SetCapture();
  SetFocus();
  m_bTracking = TRUE;
  OnInvertTracker(m_rectTracker);
  if (m_bTracking2) OnInvertTracker(m_rectTracker2);
  SetSplitCursor(ht);
}

// Moves the tracker(s) to centre on pt, keeping them inside the panes.
void CSplitterWnd::_MoveTrackers(CPoint pt) {
  if (_AfxTracksY(m_htTrack)) {
    CRect& rect = m_rectTracker;
    int cy = rect.Height();
    int y = max((int)m_rectLimit.top, min((int)pt.y - cy / 2, (int)m_rectLimit.bottom - cy));
    if (y != rect.top) {
      OnInvertTracker(rect);
      rect.OffsetRect(0, y - rect.top);
      OnInvertTracker(rect);
    }
  }
  if (_AfxTracksX(m_htTrack)) {
    CRect& rect = m_bTracking2 ? m_rectTracker2 : m_rectTracker;
    int cx = rect.Width();
    int x = max((int)m_rectLimit.left, min((int)pt.x - cx / 2, (int)m_rectLimit.right - cx));
    if (x != rect.left) {
      OnInvertTracker(rect);
      rect.OffsetRect(x - rect.left, 0);
      OnInvertTracker(rect);
    }
  }
}

void CSplitterWnd::TrackRowSize(int y, int row) {
  // y is the top of the bar below the row.
  int yTop = 0;
  for (int i = 0; i < row; i++) yTop += m_pRowInfo[i].nCurSize + m_cySplitterGap;
  int cy = y - yTop - 2 * m_cyBorder;
  if (cy < m_pRowInfo[row].nMinSize) cy = 0;  // too small: collapse it
  m_pRowInfo[row].nIdealSize = cy;
}

void CSplitterWnd::TrackColumnSize(int x, int col) {
  int xLeft = 0;
  for (int i = 0; i < col; i++) xLeft += m_pColInfo[i].nCurSize + m_cxSplitterGap;
  int cx = x - xLeft - 2 * m_cxBorder;
  if (cx < m_pColInfo[col].nMinSize) cx = 0;
  m_pColInfo[col].nIdealSize = cx;
}

// The sizes the panes either side of a tracker would get: the row or
// column before it, and the one after it.
static void _AfxSplitSizes(CSplitterWnd::CRowColInfo* pInfo, int n, int nPos, int nStart,
                           int nEnd, int nBorder2, int nGap, int& nBefore, int& nAfter) {
  int nTop = nStart;
  for (int i = 0; i < n; i++) nTop += pInfo[i].nCurSize + nBorder2 + nGap;
  int nBottom = nEnd;
  if (pInfo[n + 1].nCurSize >= 0) nBottom = nTop + pInfo[n].nCurSize + nBorder2 + nGap +
                                            pInfo[n + 1].nCurSize + nBorder2;
  nBefore = nPos - nTop - nBorder2;
  nAfter = nBottom - (nPos + nGap) - nBorder2;
}

// Applies a finished track: a box splits; a bar sizes, or in a dynamic
// splitter removes the pane it was dragged over.
void CSplitterWnd::_AcceptTrack() {
  int ht = m_htTrack;
  int cxGap = m_cxSplitterGap - 2 * m_cxBorder;
  int cyGap = m_cySplitterGap - 2 * m_cyBorder;
  CRect rectX = m_bTracking2 ? m_rectTracker2 : m_rectTracker;
  if (_AfxIsBox(ht)) {
    // Split only if both new panes would be big enough.
    if (_AfxTracksY(ht) && m_nRows < m_nMaxRows) {
      int cyBefore = m_rectTracker.top - m_rectLimit.top;
      int cyAfter = m_rectLimit.bottom - m_rectTracker.bottom;
      int cyMin = m_pRowInfo[m_nRows - 1].nMinSize + 2 * m_cyBorder;
      if (cyBefore >= cyMin && cyAfter >= cyMin) SplitRow(cyBefore);
    }
    if (_AfxTracksX(ht) && m_nCols < m_nMaxCols) {
      int cxBefore = rectX.left - m_rectLimit.left;
      int cxAfter = m_rectLimit.right - rectX.right;
      int cxMin = m_pColInfo[m_nCols - 1].nMinSize + 2 * m_cxBorder;
      if (cxBefore >= cxMin && cxAfter >= cxMin) SplitColumn(cxBefore);
    }
    return;
  }

  int row = -1, col = -1;
  if (ht >= splitterIntersection1) {
    row = (ht - splitterIntersection1) / 15;
    col = (ht - splitterIntersection1) % 15;
  } else if (ht >= hSplitterBar1) {
    col = ht - hSplitterBar1;
  } else if (ht >= vSplitterBar1) {
    row = ht - vSplitterBar1;
  }
  int rowDelete = -1, colDelete = -1;
  if (row >= 0) {
    int cyBefore, cyAfter;
    _AfxSplitSizes(m_pRowInfo, row, m_rectTracker.top, m_rectLimit.top, m_rectLimit.bottom,
                   2 * m_cyBorder, cyGap, cyBefore, cyAfter);
    if (m_pDynamicViewClass != NULL && cyBefore < m_pRowInfo[row].nMinSize)
      rowDelete = row;
    else if (m_pDynamicViewClass != NULL && cyAfter < m_pRowInfo[row + 1].nMinSize)
      rowDelete = row + 1;
    else
      TrackRowSize(m_rectTracker.top, row);
  }
  if (col >= 0) {
    int cxBefore, cxAfter;
    _AfxSplitSizes(m_pColInfo, col, rectX.left, m_rectLimit.left, m_rectLimit.right,
                   2 * m_cxBorder, cxGap, cxBefore, cxAfter);
    if (m_pDynamicViewClass != NULL && cxBefore < m_pColInfo[col].nMinSize)
      colDelete = col;
    else if (m_pDynamicViewClass != NULL && cxAfter < m_pColInfo[col + 1].nMinSize)
      colDelete = col + 1;
    else
      TrackColumnSize(rectX.left, col);
  }
  if (rowDelete >= 0) DeleteRow(rowDelete);
  if (colDelete >= 0) DeleteColumn(colDelete);
  RecalcLayout();
}

void CSplitterWnd::StopTracking(BOOL bAccept) {
  if (!m_bTracking) return;
  m_bTracking = FALSE;
  OnInvertTracker(m_rectTracker);
  if (m_bTracking2) OnInvertTracker(m_rectTracker2);
  ReleaseCapture();
  if (bAccept) _AcceptTrack();
  // The focus goes back to the active pane (which may be a new one).
  CWnd* pPane = GetActivePane();
  if (pPane == NULL) pPane = GetPane(0, 0);
  if (pPane != NULL) pPane->SetFocus();
  m_htTrack = noHit;
}

void CSplitterWnd::OnLButtonDown(UINT, CPoint pt) {
  if (m_bTracking) return;
  int ht = HitTest(pt);
  StartTracking(ht);
  if (m_bTracking && _AfxIsBox(ht)) _MoveTrackers(pt);
}

void CSplitterWnd::OnMouseMove(UINT, CPoint pt) {
  if (!m_bTracking)
    SetSplitCursor(HitTest(pt));
  else
    _MoveTrackers(pt);
}

void CSplitterWnd::OnLButtonUp(UINT, CPoint) { StopTracking(TRUE); }

void CSplitterWnd::OnLButtonDblClk(UINT, CPoint pt) {
  // In a dynamic splitter, double-clicking a bar removes the split.
  int ht = HitTest(pt);
  if (m_pDynamicViewClass == NULL) return;
  if (ht >= vSplitterBar1 && ht < hSplitterBar1 && m_nRows > 1)
    DeleteRow(1);
  else if (ht >= hSplitterBar1 && ht < splitterIntersection1 && m_nCols > 1)
    DeleteColumn(1);
}

void CSplitterWnd::OnCancelMode() {
  CWnd::OnCancelMode();
  StopTracking(FALSE);
}

void CSplitterWnd::OnCaptureChanged(CWnd* pWnd) {
  if (m_bTracking && pWnd != this) StopTracking(FALSE);
}

// While tracking, the arrow keys move the tracker (Ctrl for one pixel at
// a time), Enter accepts and Esc cancels.
void CSplitterWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (m_bTracking) {
    int nStep = ::GetKeyState(VK_CONTROL) < 0 ? 1 : 8;
    CPoint pt = m_rectTracker.CenterPoint();
    if (m_bTracking2) pt.x = m_rectTracker2.CenterPoint().x;
    switch (nChar) {
      case VK_ESCAPE: StopTracking(FALSE); return;
      case VK_RETURN: StopTracking(TRUE); return;
      case VK_LEFT: pt.x -= nStep; break;
      case VK_RIGHT: pt.x += nStep; break;
      case VK_UP: pt.y -= nStep; break;
      case VK_DOWN: pt.y += nStep; break;
      default: CWnd::OnKeyDown(nChar, nRepCnt, nFlags); return;
    }
    _MoveTrackers(pt);
    // Keep the cursor on the tracker so the mouse carries on from there.
    pt = m_rectTracker.CenterPoint();
    if (m_bTracking2) pt.x = m_rectTracker2.CenterPoint().x;
    ClientToScreen(&pt);
    ::SetCursorPos(pt.x, pt.y);
    return;
  }
  CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

// ---------------------------------------------------------------------
// Dynamic splitting

static CCreateContext _AfxPaneContext(CSplitterWnd* pSplitter, CWnd* pPane) {
  CCreateContext context;
  if (pPane != NULL && pPane->IsKindOf(RUNTIME_CLASS(CView))) {
    CView* pView = (CView*)pPane;
    context.m_pLastView = pView;
    context.m_pCurrentDoc = pView->GetDocument();
    if (context.m_pCurrentDoc != NULL)
      context.m_pNewDocTemplate = context.m_pCurrentDoc->GetDocTemplate();
  }
  context.m_pCurrentFrame = pSplitter->GetParentFrame();
  return context;
}

BOOL CSplitterWnd::SplitRow(int cyBefore) {
  if (m_pDynamicViewClass == NULL || m_nRows >= m_nMaxRows) return FALSE;
  int rowNew = m_nRows;
  m_pRowInfo[rowNew - 1].nIdealSize = max(0, cyBefore - 2 * m_cyBorder);
  m_nRows++;
  for (int col = 0; col < m_nCols; col++) {
    CCreateContext context = _AfxPaneContext(this, GetPane(rowNew - 1, col));
    CSize size(m_pColInfo[col].nCurSize, m_pRowInfo[rowNew].nMinSize);
    int cxIdeal = m_pColInfo[col].nIdealSize;
    if (!CreateView(rowNew, col, m_pDynamicViewClass, size, &context)) {
      m_nRows--;
      RecalcLayout();
      return FALSE;
    }
    m_pColInfo[col].nIdealSize = cxIdeal;  // CreateView set it to the pane size
    CWnd* pPane = GetPane(rowNew, col);
    if (pPane != NULL) pPane->SendMessage(WM_INITIALUPDATE);
  }
  m_pRowInfo[rowNew - 1].nIdealSize = max(0, cyBefore - 2 * m_cyBorder);
  RecalcLayout();
  return TRUE;
}

BOOL CSplitterWnd::SplitColumn(int cxBefore) {
  if (m_pDynamicViewClass == NULL || m_nCols >= m_nMaxCols) return FALSE;
  int colNew = m_nCols;
  m_nCols++;
  for (int row = 0; row < m_nRows; row++) {
    CCreateContext context = _AfxPaneContext(this, GetPane(row, colNew - 1));
    CSize size(m_pColInfo[colNew].nMinSize, m_pRowInfo[row].nCurSize);
    int cyIdeal = m_pRowInfo[row].nIdealSize;
    if (!CreateView(row, colNew, m_pDynamicViewClass, size, &context)) {
      m_nCols--;
      RecalcLayout();
      return FALSE;
    }
    m_pRowInfo[row].nIdealSize = cyIdeal;
    CWnd* pPane = GetPane(row, colNew);
    if (pPane != NULL) pPane->SendMessage(WM_INITIALUPDATE);
  }
  m_pColInfo[colNew - 1].nIdealSize = max(0, cxBefore - 2 * m_cxBorder);
  RecalcLayout();
  return TRUE;
}

void CSplitterWnd::DeleteView(int row, int col) {
  CWnd* pPane = GetPane(row, col);
  if (pPane == NULL) return;
  // If the view is active, make another pane active first.
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL && pFrame->GetActiveView() == pPane) {
    CWnd* pOther = NULL;
    for (int r = 0; r < m_nRows && pOther == NULL; r++)
      for (int c = 0; c < m_nCols && pOther == NULL; c++)
        if ((r != row || c != col) && GetPane(r, c) != NULL) pOther = GetPane(r, c);
    if (pOther != NULL && pOther->IsKindOf(RUNTIME_CLASS(CView)))
      pFrame->SetActiveView((CView*)pOther);
    else
      pFrame->SetActiveView(NULL);
  }
  pPane->DestroyWindow();
}

void CSplitterWnd::DeleteRow(int rowDelete) {
  if (m_nRows <= 1 || rowDelete < 0 || rowDelete >= m_nRows) return;
  for (int col = 0; col < m_nCols; col++) DeleteView(rowDelete, col);
  // Renumber the panes below.
  for (int row = rowDelete + 1; row < m_nRows; row++) {
    for (int col = 0; col < m_nCols; col++) {
      CWnd* pPane = GetPane(row, col);
      if (pPane != NULL) pPane->SetDlgCtrlID(IdFromRowCol(row - 1, col));
    }
    m_pRowInfo[row - 1] = m_pRowInfo[row];
  }
  // The row's scroll bar goes, and the ones below move up.
  HWND hBar = ::GetDlgItem(m_hWnd, AFX_IDW_VSCROLL_FIRST + rowDelete);
  if (hBar != NULL) ::DestroyWindow(hBar);
  for (int row = rowDelete + 1; row < m_nRows; row++) {
    hBar = ::GetDlgItem(m_hWnd, AFX_IDW_VSCROLL_FIRST + row);
    if (hBar != NULL) ::SetWindowLongPtr(hBar, GWLP_ID, AFX_IDW_VSCROLL_FIRST + row - 1);
  }
  m_nRows--;
  RecalcLayout();
}

void CSplitterWnd::DeleteColumn(int colDelete) {
  if (m_nCols <= 1 || colDelete < 0 || colDelete >= m_nCols) return;
  for (int row = 0; row < m_nRows; row++) DeleteView(row, colDelete);
  for (int col = colDelete + 1; col < m_nCols; col++) {
    for (int row = 0; row < m_nRows; row++) {
      CWnd* pPane = GetPane(row, col);
      if (pPane != NULL) pPane->SetDlgCtrlID(IdFromRowCol(row, col - 1));
    }
    m_pColInfo[col - 1] = m_pColInfo[col];
  }
  HWND hBar = ::GetDlgItem(m_hWnd, AFX_IDW_HSCROLL_FIRST + colDelete);
  if (hBar != NULL) ::DestroyWindow(hBar);
  for (int col = colDelete + 1; col < m_nCols; col++) {
    hBar = ::GetDlgItem(m_hWnd, AFX_IDW_HSCROLL_FIRST + col);
    if (hBar != NULL) ::SetWindowLongPtr(hBar, GWLP_ID, AFX_IDW_HSCROLL_FIRST + col - 1);
  }
  m_nCols--;
  RecalcLayout();
}

// Window > Split: tracks a split (or the existing bars) from the middle
// of the splitter with the keyboard or the mouse.
BOOL CSplitterWnd::DoKeyboardSplit() {
  if (m_bTracking) return FALSE;
  BOOL bRows = m_pDynamicViewClass != NULL && m_nRows < m_nMaxRows;
  BOOL bCols = m_pDynamicViewClass != NULL && m_nCols < m_nMaxCols;
  int ht;
  if (bRows && bCols)
    ht = bothSplitterBox;
  else if (bRows)
    ht = vSplitterBox;
  else if (bCols)
    ht = hSplitterBox;
  else if (m_nRows > 1 && m_nCols > 1)
    ht = splitterIntersection1;
  else if (m_nRows > 1)
    ht = vSplitterBar1;
  else if (m_nCols > 1)
    ht = hSplitterBar1;
  else
    return FALSE;
  StartTracking(ht);
  if (!m_bTracking) return FALSE;
  // A new split starts in the middle; existing bars stay where they are.
  CPoint pt;
  if (_AfxIsBox(ht)) {
    pt = m_rectLimit.CenterPoint();
    _MoveTrackers(pt);
  }
  pt = m_rectTracker.CenterPoint();
  if (m_bTracking2) pt.x = m_rectTracker2.CenterPoint().x;
  ClientToScreen(&pt);
  ::SetCursorPos(pt.x, pt.y);
  return TRUE;
}

// ---------------------------------------------------------------------
// Scrolling: the panes in a column share its horizontal scroll bar and
// the panes in a row its vertical one, so they scroll together.

// pViewFrom has scrolled from ptOld; bring the panes that share its bars
// along.
void CSplitterWnd::_SyncPanes(CView* pViewFrom, CPoint ptOld) {
  int rowFrom, colFrom;
  if (!IsChildPane(pViewFrom, &rowFrom, &colFrom)) return;
  BOOL bSharedH = pViewFrom->GetScrollBarCtrl(SB_HORZ) != NULL;
  BOOL bSharedV = pViewFrom->GetScrollBarCtrl(SB_VERT) != NULL;
  CPoint ptNew(pViewFrom->GetScrollPos(SB_HORZ), pViewFrom->GetScrollPos(SB_VERT));
  for (int row = 0; row < m_nRows; row++) {
    for (int col = 0; col < m_nCols; col++) {
      if (row == rowFrom && col == colFrom) continue;
      CWnd* pPane = GetPane(row, col);
      if (pPane == NULL || !pPane->IsKindOf(RUNTIME_CLASS(CView))) continue;
      CView* pView = (CView*)pPane;
      CSize sizeScroll(0, 0);
      if (col == colFrom && bSharedH && pView->GetScrollBarCtrl(SB_HORZ) != NULL)
        sizeScroll.cx = ptNew.x - ptOld.x;
      if (row == rowFrom && bSharedV && pView->GetScrollBarCtrl(SB_VERT) != NULL)
        sizeScroll.cy = ptNew.y - ptOld.y;
      if (sizeScroll.cx == 0 && sizeScroll.cy == 0) continue;
      // The bar has already moved; the pane is still where it was.
      if (sizeScroll.cx != 0) pView->SetScrollPos(SB_HORZ, ptOld.x, FALSE);
      if (sizeScroll.cy != 0) pView->SetScrollPos(SB_VERT, ptOld.y, FALSE);
      pView->OnScrollBy(sizeScroll, TRUE);
      pView->UpdateWindow();
    }
  }
  // Whatever the other panes did, the bars show where pViewFrom is.
  if (bSharedH) pViewFrom->SetScrollPos(SB_HORZ, ptNew.x);
  if (bSharedV) pViewFrom->SetScrollPos(SB_VERT, ptNew.y);
}

BOOL CSplitterWnd::DoScroll(CView* pViewFrom, UINT nScrollCode, BOOL bDoScroll) {
  if (pViewFrom == NULL) return FALSE;
  CPoint ptOld(pViewFrom->GetScrollPos(SB_HORZ), pViewFrom->GetScrollPos(SB_VERT));
  BOOL bResult = pViewFrom->OnScroll(nScrollCode, 0, bDoScroll);
  if (bResult && bDoScroll) _SyncPanes(pViewFrom, ptOld);
  return bResult;
}

BOOL CSplitterWnd::DoScrollBy(CView* pViewFrom, CSize sizeScroll, BOOL bDoScroll) {
  if (pViewFrom == NULL) return FALSE;
  CPoint ptOld(pViewFrom->GetScrollPos(SB_HORZ), pViewFrom->GetScrollPos(SB_VERT));
  BOOL bResult = pViewFrom->OnScrollBy(sizeScroll, bDoScroll);
  if (bResult && bDoScroll) {
    pViewFrom->UpdateWindow();
    _SyncPanes(pViewFrom, ptOld);
  }
  return bResult;
}

// A shared scroll bar scrolls the active pane if it is in that column
// (or row), otherwise the first one; the others follow.
void CSplitterWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  if (pScrollBar == NULL) {
    CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
    return;
  }
  int col = pScrollBar->GetDlgCtrlID() - AFX_IDW_HSCROLL_FIRST;
  if (col < 0 || col >= m_nCols) return;
  int rowActive = 0, colActive = -1;
  if (GetActivePane(&rowActive, &colActive) == NULL || colActive != col) rowActive = 0;
  CWnd* pPane = GetPane(rowActive, col);
  if (pPane != NULL && pPane->IsKindOf(RUNTIME_CLASS(CView)))
    DoScroll((CView*)pPane, MAKEWORD(nSBCode, 0xFF), TRUE);
}

void CSplitterWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  if (pScrollBar == NULL) {
    CWnd::OnVScroll(nSBCode, nPos, pScrollBar);
    return;
  }
  int row = pScrollBar->GetDlgCtrlID() - AFX_IDW_VSCROLL_FIRST;
  if (row < 0 || row >= m_nRows) return;
  int rowActive = -1, colActive = 0;
  if (GetActivePane(&rowActive, &colActive) == NULL || rowActive != row) colActive = 0;
  CWnd* pPane = GetPane(row, colActive);
  if (pPane != NULL && pPane->IsKindOf(RUNTIME_CLASS(CView)))
    DoScroll((CView*)pPane, MAKEWORD(0xFF, nSBCode), TRUE);
}

BOOL CSplitterWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  // Give the wheel to the active pane.
  CWnd* pPane = GetActivePane();
  if (pPane != NULL)
    return (BOOL)pPane->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(nFlags, zDelta), MAKELPARAM(pt.x, pt.y));
  return FALSE;
}

// Commands and notifications from the panes' controls go on to the frame.
BOOL CSplitterWnd::OnCommand(WPARAM wParam, LPARAM lParam) {
  if (CWnd::OnCommand(wParam, lParam)) return TRUE;
  CFrameWnd* pFrame = GetParentFrame();
  return pFrame != NULL && pFrame->SendMessage(WM_COMMAND, wParam, lParam) != 0;
}

BOOL CSplitterWnd::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (CWnd::OnNotify(wParam, lParam, pResult)) return TRUE;
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame == NULL) return FALSE;
  *pResult = pFrame->SendMessage(WM_NOTIFY, wParam, lParam);
  return TRUE;
}
