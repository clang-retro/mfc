// fpdock.cpp - the Feature Pack's panes and their docking: CBasePane,
// CPane, CDockingManager, CDockablePane, CBaseTabbedPane and CTabbedPane,
// CPaneFrameWnd and CMFCAutoHideBar, and the visual manager's drawing of
// pane captions, dividers and auto-hide buttons.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdockingmanager.h>
#include <afxdockablepane.h>
#include <afxtabbedpane.h>
#include <afxpaneframewnd.h>
#include <afxautohidebar.h>
#include <afxmenubar.h>
#include <afxframewndex.h>
#include <afxmdiframewndex.h>
#include <afxvisualmanager.h>

static const int kDivider = 4;       // the dockable pane's resizing edge
static const int kMinDockSize = 24;  // the least a resizable pane shrinks to
enum { kSlideTimer = 0xA51D };

static LPCTSTR _AfxPaneClass() {
  return AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), NULL);
}

// The side a dock bar ID stands for.
static DWORD _AfxAlignFromDockBarID(UINT nID) {
  switch (nID) {
    case AFX_IDW_DOCKBAR_TOP: return CBRS_ALIGN_TOP;
    case AFX_IDW_DOCKBAR_BOTTOM: return CBRS_ALIGN_BOTTOM;
    case AFX_IDW_DOCKBAR_LEFT: return CBRS_ALIGN_LEFT;
    case AFX_IDW_DOCKBAR_RIGHT: return CBRS_ALIGN_RIGHT;
  }
  return 0;
}

// The first side named in a CBRS_ALIGN_ mask.
static DWORD _AfxFirstAlign(DWORD dw) {
  if (dw & CBRS_ALIGN_TOP) return CBRS_ALIGN_TOP;
  if (dw & CBRS_ALIGN_LEFT) return CBRS_ALIGN_LEFT;
  if (dw & CBRS_ALIGN_RIGHT) return CBRS_ALIGN_RIGHT;
  if (dw & CBRS_ALIGN_BOTTOM) return CBRS_ALIGN_BOTTOM;
  return CBRS_ALIGN_TOP;
}

static int _AfxAutoHideIndex(DWORD dwAlign) {
  switch (dwAlign & CBRS_ALIGN_ANY) {
    case CBRS_ALIGN_LEFT: return 0;
    case CBRS_ALIGN_TOP: return 1;
    case CBRS_ALIGN_RIGHT: return 2;
  }
  return 3;
}

// ---------------------------------------------------------------------
// The visual manager's panes.

COLORREF CMFCVisualManager::OnDrawPaneCaption(CDC* pDC, CDockablePane*, BOOL bActive, CRect rectCaption,
                                              CRect) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  if (bActive) {
    pDC->FillSolidRect(rectCaption, m_bFlat ? g.clrHilite : g.clrActiveCaption);
    return m_bFlat ? g.clrTextHilite : g.clrCaptionText;
  }
  pDC->FillSolidRect(rectCaption, m_bFlat ? m_clrFace : g.clrBtnFace);
  CRect r = rectCaption;
  r.DeflateRect(1, 1);
  pDC->Draw3dRect(r, m_bFlat ? g.clrBtnShadow : g.clrBtnHilite, g.clrBtnShadow);
  return g.clrBtnText;
}

void CMFCVisualManager::OnDrawCaptionButton(CDC* pDC, CMFCCaptionButton* pButton, BOOL bActive, BOOL,
                                            BOOL bMaximized, BOOL bDisabled, int) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  CRect r = pButton->GetRect();
  if (pButton->IsPushed())
    pDC->Draw3dRect(r, g.clrBtnShadow, g.clrBtnHilite);
  else if (pButton->IsFocused())
    pDC->Draw3dRect(r, g.clrBtnHilite, g.clrBtnShadow);
  COLORREF clr = bDisabled ? g.clrGrayedText : bActive ? (m_bFlat ? g.clrTextHilite : g.clrCaptionText) : g.clrBtnText;
  CPen pen(PS_SOLID, 1, clr);
  CPen* pOld = pDC->SelectObject(&pen);
  int cx = r.left + r.Width() / 2, cy = r.top + r.Height() / 2;
  if (pButton->IsPushed()) cx++, cy++;
  if (pButton->GetHit() == AFX_HTCLOSE) {
    for (int d = 0; d < 2; d++) {  // a two pixel X
      pDC->MoveTo(cx - 4 + d, cy - 3);
      pDC->LineTo(cx + 3 + d, cy + 4);
      pDC->MoveTo(cx + 2 + d, cy - 3);
      pDC->LineTo(cx - 5 + d, cy + 4);
    }
  } else {
    // The pin: upright when the pane stays, on its side when it hides.
    if (!bMaximized) {
      pDC->MoveTo(cx - 2, cy - 4);
      pDC->LineTo(cx - 2, cy + 1);
      pDC->LineTo(cx + 2, cy + 1);
      pDC->LineTo(cx + 2, cy - 4);
      pDC->LineTo(cx - 2, cy - 4);
      pDC->MoveTo(cx + 1, cy - 4);
      pDC->LineTo(cx + 1, cy + 1);
      pDC->MoveTo(cx - 4, cy + 1);
      pDC->LineTo(cx + 5, cy + 1);
      pDC->MoveTo(cx, cy + 1);
      pDC->LineTo(cx, cy + 5);
    } else {
      pDC->MoveTo(cx - 1, cy - 2);
      pDC->LineTo(cx + 4, cy - 2);
      pDC->LineTo(cx + 4, cy + 2);
      pDC->LineTo(cx - 1, cy + 2);
      pDC->LineTo(cx - 1, cy - 2);
      pDC->MoveTo(cx - 1, cy + 1);
      pDC->LineTo(cx + 4, cy + 1);
      pDC->MoveTo(cx - 1, cy - 4);
      pDC->LineTo(cx - 1, cy + 5);
      pDC->MoveTo(cx - 5, cy);
      pDC->LineTo(cx - 1, cy);
    }
  }
  pDC->SelectObject(pOld);
}

void CMFCVisualManager::OnDrawPaneDivider(CDC* pDC, CBasePane*, CRect rect, BOOL) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  pDC->FillSolidRect(rect, m_bFlat ? m_clrFace : g.clrBtnFace);
  if (!m_bFlat) pDC->Draw3dRect(rect, g.clrBtnHilite, g.clrBtnShadow);
}

void CMFCVisualManager::OnDrawAutoHideButtonBorder(CDC* pDC, CRect rectBounds, CRect, CBasePane*) {
  pDC->Draw3dRect(rectBounds, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
}

// ---------------------------------------------------------------------
// CBasePane

IMPLEMENT_DYNAMIC(CBasePane, CWnd)

BEGIN_MESSAGE_MAP(CBasePane, CWnd)
  ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
  ON_WM_ERASEBKGND()
  ON_WM_PAINT()
  ON_WM_DESTROY()
END_MESSAGE_MAP()

CBasePane::CBasePane()
    : m_dwStyle(0), m_dwControlBarStyle(0), m_dwEnabledAlignment(0), m_pDockSite(NULL), m_pMiniFrame(NULL),
      m_pTabbedParent(NULL), m_bVisibleState(TRUE), m_bSameRowAsPrev(FALSE), m_sizeDock(200, 200) {}

CBasePane::~CBasePane() {}

BOOL CBasePane::PreCreateWindow(CREATESTRUCT& cs) { return CWnd::PreCreateWindow(cs); }

BOOL CBasePane::CreateEx(DWORD dwStyleEx, LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                         const RECT& rect, CWnd* pParentWnd, UINT nID, DWORD dwControlBarStyle,
                         CCreateContext* pContext) {
  m_dwStyle = dwStyle & CBRS_ALL;
  m_dwControlBarStyle = dwControlBarStyle;
  m_pDockSite = DYNAMIC_DOWNCAST(CFrameWnd, pParentWnd);
  if (m_pDockSite == NULL && pParentWnd != NULL) m_pDockSite = pParentWnd->GetParentFrame();
  DWORD dwWndStyle = (dwStyle & ~CBRS_ALL) | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
  if (lpszClassName == NULL) lpszClassName = _AfxPaneClass();
  CRect r(rect);
  if (!CWnd::CreateEx(dwStyleEx, lpszClassName, lpszWindowName, dwWndStyle, r, pParentWnd, nID, pContext))
    return FALSE;
  if (r.Width() > 0 && r.Height() > 0) m_sizeDock = r.Size();
  m_bVisibleState = (dwStyle & WS_VISIBLE) != 0;
  CDockingManager* pManager = GetDockingManagerOf(pParentWnd);
  if (pManager != NULL) pManager->AddPane(this);
  return TRUE;
}

CDockingManager* AFXAPI CBasePane::GetDockingManagerOf(CWnd* pFrame) {
  if (pFrame == NULL) return NULL;
  if (pFrame->IsKindOf(RUNTIME_CLASS(CFrameWndEx))) return ((CFrameWndEx*)pFrame)->GetDockingManager();
  if (pFrame->IsKindOf(RUNTIME_CLASS(CMDIFrameWndEx))) return ((CMDIFrameWndEx*)pFrame)->GetDockingManager();
  return NULL;
}

CDockingManager* CBasePane::GetDockingManager() const { return GetDockingManagerOf(m_pDockSite); }

void CBasePane::SetPaneAlignment(DWORD dwAlignment) {
  m_dwStyle = (m_dwStyle & ~CBRS_ALIGN_ANY) | (dwAlignment & CBRS_ALIGN_ANY);
}

BOOL CBasePane::IsDocked() const {
  if (m_pTabbedParent != NULL) return ((CBasePane*)m_pTabbedParent)->IsDocked();
  CDockingManager* pManager = GetDockingManager();
  return pManager != NULL && pManager->IsPaneDocked(this);
}

BOOL CBasePane::IsTabbed() const { return m_pTabbedParent != NULL; }

BOOL CBasePane::IsVisible() const {
  if (m_hWnd == NULL) return FALSE;
  if (m_pTabbedParent != NULL) {
    CBaseTabbedPane* pTabbed = (CBaseTabbedPane*)m_pTabbedParent;
    CMFCBaseTabCtrl* pTabs = pTabbed->GetTabWnd();
    int iTab = pTabs != NULL ? pTabs->GetTabFromHwnd(m_hWnd) : -1;
    return iTab >= 0 && pTabs->IsTabVisible(iTab) && pTabbed->IsVisible();
  }
  if (m_pMiniFrame != NULL) return ((CWnd*)m_pMiniFrame)->IsWindowVisible() && (GetStyle() & WS_VISIBLE) != 0;
  return (GetStyle() & WS_VISIBLE) != 0;
}

void CBasePane::ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate) {
  if (m_hWnd == NULL) return;
  m_bVisibleState = bShow;
  ShowWindow(bShow ? SW_SHOWNOACTIVATE : SW_HIDE);
  if (m_pMiniFrame != NULL) ((CWnd*)m_pMiniFrame)->ShowWindow(bShow ? SW_SHOWNOACTIVATE : SW_HIDE);
  if (m_pDockSite != NULL) {
    if (bDelay)
      m_pDockSite->DelayRecalcLayout();
    else
      m_pDockSite->RecalcLayout();
  }
  if (bShow && bActivate) SetFocus();
}

CSize CBasePane::CalcFixedLayout(BOOL, BOOL) {
  CRect r;
  GetWindowRect(r);
  return r.Size();
}

void CBasePane::AdjustDockingLayout(HDWP hdwp) {
  CDockingManager* pManager = GetDockingManager();
  if (pManager != NULL) pManager->AdjustDockingLayout(hdwp);
}

void CBasePane::DoPaint(CDC* pDC) {
  CRect r;
  GetClientRect(r);
  CMFCVisualManager::GetInstance()->OnFillBarBackground(pDC, this, r, r);
}

BOOL CBasePane::OnEraseBkgnd(CDC*) { return TRUE; }

void CBasePane::OnPaint() {
  CPaintDC dc(this);
  DoPaint(&dc);
}

LRESULT CBasePane::OnIdleUpdateCmdUI(WPARAM wParam, LPARAM) {
  if (GetStyle() & WS_VISIBLE) {
    CFrameWnd* pTarget = DYNAMIC_DOWNCAST(CFrameWnd, GetOwner());
    if (pTarget == NULL) pTarget = m_pDockSite;
    if (pTarget == NULL) pTarget = GetParentFrame();
    if (pTarget != NULL) OnUpdateCmdUI(pTarget, (BOOL)wParam);
  }
  return 0;
}

void CBasePane::OnDestroy() {
  CDockingManager* pManager = GetDockingManager();
  if (pManager != NULL) pManager->RemovePaneFromDockManager(this, FALSE, FALSE, FALSE, NULL);
  CWnd::OnDestroy();
}

// ---------------------------------------------------------------------
// CPane

IMPLEMENT_DYNCREATE(CPane, CBasePane)

int CPane::m_cyTopBorder = 2;
int CPane::m_cyBottomBorder = 2;
int CPane::m_cxLeftBorder = 2;
int CPane::m_cxRightBorder = 2;
BOOL CPane::m_bHandleMinSize = FALSE;

CPane::CPane()
    : m_sizeMin(0, 0), m_rectBorders(0, 0, 0, 0), m_rectFloat(0, 0, 0, 0), m_dwRecentAlignment(CBRS_ALIGN_TOP),
      m_bExclusiveRow(FALSE) {}

CPane::~CPane() {}

BOOL CPane::Create(LPCTSTR lpszClassName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID,
                   DWORD dwControlBarStyle, CCreateContext* pContext) {
  return CreateEx(0, lpszClassName, dwStyle, rect, pParentWnd, nID, dwControlBarStyle, pContext);
}

BOOL CPane::CreateEx(DWORD dwStyleEx, LPCTSTR lpszClassName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                     UINT nID, DWORD dwControlBarStyle, CCreateContext* pContext) {
  if (!CBasePane::CreateEx(dwStyleEx, lpszClassName, NULL, dwStyle, rect, pParentWnd, nID, dwControlBarStyle,
                           pContext))
    return FALSE;
  if (GetCurrentAlignment() != 0) m_dwRecentAlignment = GetCurrentAlignment();
  return TRUE;
}

BOOL CPane::DockToFrameWindow(DWORD dwAlignment, LPCRECT lpRect, DWORD, CBasePane*, int, BOOL) {
  CDockingManager* pManager = GetDockingManager();
  if (pManager == NULL) return FALSE;
  UINT nID = 0;
  switch (dwAlignment & CBRS_ALIGN_ANY) {
    case CBRS_ALIGN_TOP: nID = AFX_IDW_DOCKBAR_TOP; break;
    case CBRS_ALIGN_BOTTOM: nID = AFX_IDW_DOCKBAR_BOTTOM; break;
    case CBRS_ALIGN_LEFT: nID = AFX_IDW_DOCKBAR_LEFT; break;
    case CBRS_ALIGN_RIGHT: nID = AFX_IDW_DOCKBAR_RIGHT; break;
  }
  pManager->DockPane(this, nID, lpRect);
  return TRUE;
}

BOOL CPane::DockPane(CBasePane* pDockBar, LPCRECT lpRect, AFX_DOCK_METHOD) {
  DWORD dwAlign = pDockBar != NULL ? pDockBar->GetCurrentAlignment() : m_dwRecentAlignment;
  return DockToFrameWindow(dwAlign, lpRect);
}

BOOL CPane::FloatPane(CRect rectFloat, AFX_DOCK_METHOD, bool bShow) {
  if (m_hWnd == NULL || m_pDockSite == NULL) return FALSE;
  if (m_pMiniFrame != NULL) {
    if (!rectFloat.IsRectEmpty())
      ((CWnd*)m_pMiniFrame)->SetWindowPos(NULL, rectFloat.left, rectFloat.top, 0, 0,
                                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
  }
  if (GetCurrentAlignment() != 0) m_dwRecentAlignment = GetCurrentAlignment();
  CDockingManager* pManager = GetDockingManager();
  if (pManager != NULL) pManager->_Remove(this);

  // The pane's size floating: a bar lies across, a resizable pane keeps
  // its size.
  CRect rectWnd;
  GetWindowRect(rectWnd);
  SetPaneAlignment(CBRS_ALIGN_TOP);
  CSize size = IsResizable() ? (rectWnd.Width() > 0 ? rectWnd.Size() : m_sizeDock) : CalcFixedLayout(FALSE, TRUE);
  if (IsResizable() && !rectFloat.IsRectEmpty()) size = rectFloat.Size();
  CPoint pt = rectFloat.IsRectEmpty() ? (m_rectFloat.IsRectEmpty() ? rectWnd.TopLeft() + CPoint(16, 16)
                                                                   : m_rectFloat.TopLeft())
                                      : rectFloat.TopLeft();
  DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | (IsResizable() ? WS_THICKFRAME : 0);
  CRect rectFrame(0, 0, size.cx, size.cy);
  ::AdjustWindowRectEx(rectFrame, dwStyle, FALSE, WS_EX_TOOLWINDOW);
  rectFrame.OffsetRect(pt - rectFrame.TopLeft());

  CString strTitle;
  GetWindowText(strTitle);
  CPaneFrameWnd* pMiniFrame = new CPaneFrameWnd;
  if (!pMiniFrame->CreateEx(WS_EX_TOOLWINDOW, strTitle, dwStyle, rectFrame, m_pDockSite)) {
    delete pMiniFrame;
    if (pManager != NULL) pManager->DockPane(this);
    return FALSE;
  }
  pMiniFrame->AddPane(this);
  m_rectFloat = rectFrame;
  if (bShow && (GetStyle() & WS_VISIBLE)) pMiniFrame->ShowWindow(SW_SHOWNOACTIVATE);
  m_pDockSite->RecalcLayout();
  return TRUE;
}

void CPane::UndockPane(BOOL bDelay) {
  if (m_pMiniFrame != NULL) {
    m_pMiniFrame->RemovePane(this);
    return;
  }
  CDockingManager* pManager = GetDockingManager();
  if (pManager != NULL) pManager->_Remove(this);
  if (m_pDockSite != NULL) {
    if (bDelay)
      m_pDockSite->DelayRecalcLayout();
    else
      m_pDockSite->RecalcLayout();
  }
}

BOOL CPane::DockPaneStandard(BOOL& bWasDocked) {
  bWasDocked = DockToFrameWindow(m_dwRecentAlignment != 0 ? m_dwRecentAlignment : CBRS_ALIGN_TOP);
  return bWasDocked;
}

void CPane::EnableGripper(BOOL bEnable) {
  if (bEnable)
    m_dwStyle |= CBRS_GRIPPER;
  else
    m_dwStyle &= ~CBRS_GRIPPER;
}

CSize CPane::CalcFixedLayout(BOOL, BOOL bHorz) {
  CSize size = CalcSize(!bHorz);
  if (size.cx < m_sizeMin.cx) size.cx = m_sizeMin.cx;
  if (size.cy < m_sizeMin.cy) size.cy = m_sizeMin.cy;
  return size;
}

CSize CPane::CalcSize(BOOL) {
  CRect r;
  GetWindowRect(r);
  return r.Size();
}

// ---------------------------------------------------------------------
// CDockingManager

AFX_DOCK_TYPE CDockingManager::m_dockModeGlobal = DT_IMMEDIATE;
BOOL CDockingManager::m_bGlobalDockSiteMenu = FALSE;
UINT CDockingManager::m_nTimeOutBeforeToolBarDock = 200;
UINT CDockingManager::m_nTimeOutBeforeDockingBarDock = 220;
BOOL CDockingManager::m_bRestoringDockState = FALSE;
BOOL CDockingManager::m_bHideDockingBarsInContainerMode = TRUE;
BOOL CDockingManager::m_bDisableRecalcLayout = FALSE;
BOOL CDockingManager::m_bFullScreenMode = FALSE;

CDockingManager::CDockingManager()
    : m_pParentWnd(NULL), m_pMenuBar(NULL), m_dwEnabledDockingAlignment(0), m_dwEnabledSlideAlignment(0),
      m_rectClientAreaBounds(0, 0, 0, 0), m_bDockSiteMenu(FALSE), m_bPaneContextMenu(FALSE),
      m_bPaneContextMenuToolbarsOnly(FALSE), m_bDisableRestoreDockState(FALSE), m_bIsPrintPreviewMode(FALSE),
      m_bLockUpdate(FALSE), m_bInLayout(FALSE), m_uiCustomizeCmd(0) {
  for (int i = 0; i < 4; i++) m_pAutoHideBars[i] = NULL;
}

CDockingManager::~CDockingManager() {
  for (int i = 0; i < 4; i++) delete m_pAutoHideBars[i];
}

BOOL CDockingManager::Create(CFrameWnd* pParentWnd) {
  m_pParentWnd = pParentWnd;
  return pParentWnd != NULL;
}

void AFXAPI CDockingManager::SetDockingMode(AFX_DOCK_TYPE dockMode, AFX_SMARTDOCK_THEME) {
  m_dockModeGlobal = dockMode;
}

BOOL CDockingManager::EnableDocking(DWORD dwStyle) {
  m_dwEnabledDockingAlignment |= dwStyle & CBRS_ALIGN_ANY;
  return TRUE;
}

BOOL CDockingManager::EnableAutoHidePanes(DWORD dwStyle) {
  m_dwEnabledSlideAlignment |= dwStyle & CBRS_ALIGN_ANY;
  return TRUE;
}

void CDockingManager::_RecalcFrame() {
  if (m_pParentWnd != NULL && m_pParentWnd->GetSafeHwnd() != NULL && !m_bLockUpdate) m_pParentWnd->RecalcLayout();
}

BOOL CDockingManager::AddPane(CBasePane* pWnd, BOOL bTail, BOOL, BOOL) {
  if (pWnd == NULL || pWnd->IsKindOf(RUNTIME_CLASS(CMFCAutoHideBar))) return FALSE;
  if (m_lstPanes.Find(pWnd) != NULL) return TRUE;
  m_lstPanes.AddTail(pWnd);
  if (pWnd->IsKindOf(RUNTIME_CLASS(CMFCMenuBar))) m_pMenuBar = (CMFCMenuBar*)pWnd;
  if (pWnd->m_pMiniFrame == NULL && pWnd->m_pTabbedParent == NULL) {
    if (pWnd->GetCurrentAlignment() == 0) pWnd->SetPaneAlignment(CBRS_ALIGN_TOP);
    pWnd->m_bSameRowAsPrev = FALSE;
    if (bTail)
      m_lstDocked.AddTail(pWnd);
    else
      m_lstDocked.AddHead(pWnd);
  }
  return TRUE;
}

BOOL CDockingManager::InsertPane(CBasePane* pControlBar, CBasePane* pTarget, BOOL bAfter) {
  if (pControlBar == NULL) return FALSE;
  if (m_lstPanes.Find(pControlBar) == NULL) m_lstPanes.AddTail(pControlBar);
  if (pTarget == NULL || m_lstDocked.Find(pTarget) == NULL) return AddPane(pControlBar);
  return _InsertDocked(pControlBar, pTarget, FALSE, !bAfter);
}

POSITION CDockingManager::_FindRowEnd(POSITION pos) {
  // The last position of the row that starts at pos.
  POSITION posLast = pos;
  m_lstDocked.GetNext(pos);
  while (pos != NULL) {
    CBasePane* p = (CBasePane*)m_lstDocked.GetAt(pos);
    if (!p->m_bSameRowAsPrev) break;
    posLast = pos;
    m_lstDocked.GetNext(pos);
  }
  return posLast;
}

void CDockingManager::_Remove(CBasePane* pBar) {
  POSITION pos = m_lstDocked.Find(pBar);
  if (pos == NULL) return;
  POSITION posNext = pos;
  m_lstDocked.GetNext(posNext);
  if (posNext != NULL && !pBar->m_bSameRowAsPrev) {
    CBasePane* pNext = (CBasePane*)m_lstDocked.GetAt(posNext);
    pNext->m_bSameRowAsPrev = FALSE;  // the next in the row now leads it
  }
  m_lstDocked.RemoveAt(pos);
  pBar->m_bSameRowAsPrev = FALSE;
}

BOOL CDockingManager::_InsertDocked(CBasePane* pBar, CBasePane* pTarget, BOOL bSameRow, BOOL bBefore) {
  if (pBar == pTarget) return FALSE;
  _Remove(pBar);
  POSITION pos = m_lstDocked.Find(pTarget);
  if (pos == NULL) return FALSE;
  if (m_lstPanes.Find(pBar) == NULL) m_lstPanes.AddTail(pBar);
  pBar->SetPaneAlignment(pTarget->GetCurrentAlignment());
  if (bSameRow) {
    if (bBefore) {
      pBar->m_bSameRowAsPrev = pTarget->m_bSameRowAsPrev;
      pTarget->m_bSameRowAsPrev = TRUE;
      m_lstDocked.InsertBefore(pos, pBar);
    } else {
      pBar->m_bSameRowAsPrev = TRUE;
      m_lstDocked.InsertAfter(pos, pBar);
    }
  } else {
    // A row of its own, outside the target's row or inside it.
    pBar->m_bSameRowAsPrev = FALSE;
    if (bBefore) {
      while (pos != NULL && ((CBasePane*)m_lstDocked.GetAt(pos))->m_bSameRowAsPrev) m_lstDocked.GetPrev(pos);
      if (pos == NULL)
        m_lstDocked.AddHead(pBar);
      else
        m_lstDocked.InsertBefore(pos, pBar);
    } else {
      m_lstDocked.InsertAfter(_FindRowEnd(pos), pBar);
    }
  }
  return TRUE;
}

void CDockingManager::_ReplaceDocked(CBasePane* pOld, CBasePane* pNew) {
  POSITION pos = m_lstDocked.Find(pOld);
  if (m_lstPanes.Find(pNew) == NULL) m_lstPanes.AddTail(pNew);
  if (pos == NULL) return;
  POSITION posNew = m_lstDocked.Find(pNew);
  if (posNew != NULL) m_lstDocked.RemoveAt(posNew);
  m_lstDocked.SetAt(pos, pNew);
  pNew->m_bSameRowAsPrev = pOld->m_bSameRowAsPrev;
  pNew->SetPaneAlignment(pOld->GetCurrentAlignment());
  pOld->m_bSameRowAsPrev = FALSE;
}

void CDockingManager::RemovePaneFromDockManager(CBasePane* pWnd, BOOL bDestroy, BOOL bAdjustLayout, BOOL,
                                                CBasePane* pBarReplacement) {
  if (pWnd == NULL) return;
  if (pBarReplacement != NULL) _ReplaceDocked(pWnd, pBarReplacement);
  _Remove(pWnd);
  POSITION pos = m_lstPanes.Find(pWnd);
  if (pos != NULL) m_lstPanes.RemoveAt(pos);
  pos = m_lstHiddenPanes.Find(pWnd);
  if (pos != NULL) m_lstHiddenPanes.RemoveAt(pos);
  if (m_pMenuBar == pWnd) m_pMenuBar = NULL;
  CDockablePane* pDockable = DYNAMIC_DOWNCAST(CDockablePane, pWnd);
  if (pDockable != NULL && pDockable->m_pAutoHideBar != NULL) {
    pDockable->m_pAutoHideBar->RemoveAutoHidePane(pDockable);
    pDockable->m_pAutoHideBar = NULL;
  }
  if (bDestroy && pWnd->GetSafeHwnd() != NULL) pWnd->DestroyWindow();
  if (bAdjustLayout) _RecalcFrame();
}

void CDockingManager::DockPane(CBasePane* pBar, UINT nDockBarID, LPCRECT) {
  if (pBar == NULL || pBar->GetSafeHwnd() == NULL) return;
  DWORD dwAlign = _AfxAlignFromDockBarID(nDockBarID);
  if (dwAlign == 0) dwAlign = pBar->GetCurrentAlignment();
  if (dwAlign == 0) {
    CPane* pPane = DYNAMIC_DOWNCAST(CPane, pBar);
    dwAlign = pPane != NULL ? pPane->m_dwRecentAlignment : CBRS_ALIGN_TOP;
  }
  // Only to a side both the pane and the frame allow, when they say.
  DWORD dwAllowed = pBar->GetEnabledAlignment();
  if (m_dwEnabledDockingAlignment != 0 && dwAllowed != 0) dwAllowed &= m_dwEnabledDockingAlignment;
  if (dwAllowed != 0 && (dwAllowed & dwAlign) == 0) dwAlign = _AfxFirstAlign(dwAllowed);

  CDockablePane* pDockable = DYNAMIC_DOWNCAST(CDockablePane, pBar);
  if (pDockable != NULL && pDockable->m_pTabbedParent != NULL) {
    CBaseTabbedPane* pTabbed = (CBaseTabbedPane*)pDockable->m_pTabbedParent;
    pTabbed->DetachPane(pDockable, TRUE);
    pDockable->ShowWindow(SW_SHOWNOACTIVATE);  // off the tab, onto its own edge
  }
  if (pDockable != NULL && pDockable->m_pAutoHideBar != NULL) {
    if (pDockable->m_bSlidOut) pDockable->Slide(FALSE, FALSE);
    pDockable->m_pAutoHideBar->RemoveAutoHidePane(pDockable);
    pDockable->m_pAutoHideBar = NULL;
    pDockable->ShowWindow(SW_SHOWNOACTIVATE);
  }
  if (pBar->m_pMiniFrame != NULL) pBar->m_pMiniFrame->RemovePane(pBar);

  _Remove(pBar);
  if (m_lstPanes.Find(pBar) == NULL) m_lstPanes.AddTail(pBar);
  if (pBar->IsKindOf(RUNTIME_CLASS(CMFCMenuBar))) m_pMenuBar = (CMFCMenuBar*)pBar;
  pBar->SetPaneAlignment(dwAlign);
  CPane* pPane = DYNAMIC_DOWNCAST(CPane, pBar);
  if (pPane != NULL) pPane->m_dwRecentAlignment = dwAlign;
  pBar->m_bSameRowAsPrev = FALSE;
  m_lstDocked.AddTail(pBar);
  if (pDockable != NULL) pDockable->_UpdateNonClient();
  _RecalcFrame();
}

BOOL CDockingManager::DockPaneLeftOf(CPane* pBar, CPane* pLeftOf) {
  if (pBar == NULL || pLeftOf == NULL || m_lstDocked.Find(pLeftOf) == NULL) return FALSE;
  if (pBar->m_pMiniFrame != NULL) pBar->m_pMiniFrame->RemovePane(pBar);
  BOOL bOK = _InsertDocked(pBar, pLeftOf, TRUE, TRUE);
  _RecalcFrame();
  return bOK;
}

CBasePane* CDockingManager::FindPaneByID(UINT uBarID, BOOL) {
  for (POSITION pos = m_lstPanes.GetHeadPosition(); pos != NULL;) {
    CBasePane* p = (CBasePane*)m_lstPanes.GetNext(pos);
    if (p->GetSafeHwnd() != NULL && (UINT)p->GetDlgCtrlID() == uBarID) return p;
  }
  return NULL;
}

void CDockingManager::GetPaneList(CObList& lstBars, BOOL bIncludeAutohide, CRuntimeClass* pRTCFilter,
                                  BOOL bIncludeTabs) {
  for (POSITION pos = m_lstPanes.GetHeadPosition(); pos != NULL;) {
    CBasePane* p = (CBasePane*)m_lstPanes.GetNext(pos);
    if (pRTCFilter != NULL && !p->IsKindOf(pRTCFilter)) continue;
    if (!bIncludeTabs && p->IsTabbed()) continue;
    if (!bIncludeAutohide && p->IsAutoHideMode()) continue;
    lstBars.AddTail(p);
  }
  if (bIncludeAutohide)
    for (int i = 0; i < 4; i++)
      if (m_pAutoHideBars[i] != NULL && (pRTCFilter == NULL || m_pAutoHideBars[i]->IsKindOf(pRTCFilter)))
        lstBars.AddTail(m_pAutoHideBars[i]);
}

void CDockingManager::ShowPanes(BOOL bShow) {
  if (!bShow) {
    for (POSITION pos = m_lstPanes.GetHeadPosition(); pos != NULL;) {
      CBasePane* p = (CBasePane*)m_lstPanes.GetNext(pos);
      if (p->IsVisible() && m_lstHiddenPanes.Find(p) == NULL) {
        m_lstHiddenPanes.AddTail(p);
        p->ShowPane(FALSE, TRUE, FALSE);
      }
    }
  } else {
    while (!m_lstHiddenPanes.IsEmpty()) {
      CBasePane* p = (CBasePane*)m_lstHiddenPanes.RemoveHead();
      if (p->GetSafeHwnd() != NULL) p->ShowPane(TRUE, TRUE, FALSE);
    }
  }
  _RecalcFrame();
}

void CDockingManager::SetPrintPreviewMode(BOOL bPreview, CPrintPreviewState*) {
  if (bPreview == m_bIsPrintPreviewMode) return;
  m_bIsPrintPreviewMode = bPreview;
  ShowPanes(!bPreview);
}

DWORD CDockingManager::GetPaneAlignment(const CBasePane* pBar) const {
  return IsPaneDocked(pBar) ? pBar->GetCurrentAlignment() : 0;
}

CMFCAutoHideBar* CDockingManager::GetAutoHideBar(DWORD dwAlignment, BOOL bCreate) {
  int i = _AfxAutoHideIndex(dwAlignment);
  if (m_pAutoHideBars[i] == NULL && bCreate && m_pParentWnd != NULL) {
    CMFCAutoHideBar* pBar = new CMFCAutoHideBar;
    if (!pBar->Create(m_pParentWnd, dwAlignment & CBRS_ALIGN_ANY)) {
      delete pBar;
      return NULL;
    }
    m_pAutoHideBars[i] = pBar;
  }
  return m_pAutoHideBars[i];
}

void CDockingManager::HideAutoHidePanes(CDockablePane* pBarToExclude) {
  for (int i = 0; i < 4; i++) {
    CMFCAutoHideBar* pBar = m_pAutoHideBars[i];
    if (pBar == NULL) continue;
    for (int j = 0; j < pBar->GetCount(); j++) {
      CDockablePane* p = pBar->GetPane(j);
      if (p != pBarToExclude && p->IsSlidOut()) p->Slide(FALSE, FALSE);
    }
  }
}

void CDockingManager::EnablePaneContextMenu(BOOL bEnable, UINT uiCustomizeCmd, const CString& strCustomizeText,
                                            BOOL bToolbarsOnly) {
  m_bPaneContextMenu = bEnable;
  m_uiCustomizeCmd = uiCustomizeCmd;
  m_strCustomizeText = strCustomizeText;
  m_bPaneContextMenuToolbarsOnly = bToolbarsOnly;
}

void CDockingManager::BuildPanesMenu(CMenu& menu, BOOL bToolbarsOnly) {
  for (POSITION pos = m_lstPanes.GetHeadPosition(); pos != NULL;) {
    CBasePane* p = (CBasePane*)m_lstPanes.GetNext(pos);
    if (p->GetSafeHwnd() == NULL || !p->AllowShowOnPaneMenu()) continue;
    if (bToolbarsOnly && !p->IsKindOf(RUNTIME_CLASS(CMFCToolBar))) continue;
    if (p->IsKindOf(RUNTIME_CLASS(CBaseTabbedPane))) continue;
    CString strName;
    p->GetPaneName(strName);
    if (strName.IsEmpty()) continue;
    menu.AppendMenu(MF_STRING | (p->IsVisible() ? MF_CHECKED : MF_UNCHECKED), (UINT)p->GetDlgCtrlID(), strName);
  }
  if (m_uiCustomizeCmd != 0 && !m_strCustomizeText.IsEmpty()) {
    if (menu.GetMenuItemCount() > 0) menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, m_uiCustomizeCmd, m_strCustomizeText);
  }
}

BOOL CDockingManager::OnPaneContextMenu(CPoint point) {
  if (!m_bPaneContextMenu || m_pParentWnd == NULL) return FALSE;
  CMenu menu;
  if (!menu.CreatePopupMenu()) return FALSE;
  BuildPanesMenu(menu, m_bPaneContextMenuToolbarsOnly);
  if (menu.GetMenuItemCount() == 0) return FALSE;
  menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, m_pParentWnd);
  return TRUE;
}

void CDockingManager::SendIdleUpdateToMiniFrames() {
  for (POSITION pos = m_lstPanes.GetHeadPosition(); pos != NULL;) {
    CBasePane* p = (CBasePane*)m_lstPanes.GetNext(pos);
    if (p->m_pMiniFrame != NULL && p->GetSafeHwnd() != NULL && p->IsWindowVisible()) {
      p->SendMessage(WM_IDLEUPDATECMDUI, (WPARAM)TRUE);
      p->SendMessageToDescendants(WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0, TRUE, TRUE);
    }
  }
}

void CDockingManager::AdjustDockingLayout(HDWP) {
  if (m_bInLayout || m_bDisableRecalcLayout || m_pParentWnd == NULL || m_pParentWnd->GetSafeHwnd() == NULL) return;
  m_bInLayout = TRUE;
  CRect rect;
  m_pParentWnd->GetClientRect(rect);

  // Two passes over the docked panes, outermost first: the bars that
  // keep their size, then (inside the auto-hide strips) the resizable
  // panes. A row is a pane and those docked beside it.
  for (int nPass = 0; nPass < 2; nPass++) {
    if (nPass == 1) {
      for (int i = 0; i < 4; i++) {
        CMFCAutoHideBar* pBar = m_pAutoHideBars[i];
        if (pBar == NULL || pBar->GetSafeHwnd() == NULL) continue;
        if (pBar->GetCount() == 0 || m_bIsPrintPreviewMode) {
          if (pBar->IsWindowVisible()) pBar->ShowWindow(SW_HIDE);
          continue;
        }
        DWORD dwAlign = pBar->GetCurrentAlignment();
        BOOL bHorz = (dwAlign & CBRS_ORIENT_HORZ) != 0;
        CSize size = pBar->CalcFixedLayout(FALSE, bHorz);
        CRect r = rect;
        switch (dwAlign) {
          case CBRS_ALIGN_TOP: r.bottom = rect.top += size.cy; break;
          case CBRS_ALIGN_BOTTOM: r.top = rect.bottom -= size.cy; break;
          case CBRS_ALIGN_LEFT: r.right = rect.left += size.cx; break;
          default: r.left = rect.right -= size.cx; break;
        }
        pBar->SetWindowPos(NULL, r.left, r.top, r.Width(), r.Height(),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      }
    }
    POSITION pos = m_lstDocked.GetHeadPosition();
    while (pos != NULL) {
      // The row's visible panes.
      CBasePane* aRow[64];
      int n = 0;
      CBasePane* pHead = (CBasePane*)m_lstDocked.GetNext(pos);
      if (pHead->GetSafeHwnd() != NULL && (pHead->GetStyle() & WS_VISIBLE)) aRow[n++] = pHead;
      while (pos != NULL && ((CBasePane*)m_lstDocked.GetAt(pos))->m_bSameRowAsPrev) {
        CBasePane* p = (CBasePane*)m_lstDocked.GetNext(pos);
        if (n < 64 && p->GetSafeHwnd() != NULL && (p->GetStyle() & WS_VISIBLE)) aRow[n++] = p;
      }
      if ((pHead->IsResizable() ? 1 : 0) != nPass || n == 0) continue;
      DWORD dwAlign = pHead->GetCurrentAlignment();
      BOOL bHorz = (dwAlign & CBRS_ORIENT_HORZ) != 0;

      // The row's depth: its thickest pane.
      int nDepth = 0;
      CSize aSize[64];
      for (int i = 0; i < n; i++) {
        aSize[i] = aRow[i]->IsResizable() ? aRow[i]->m_sizeDock : aRow[i]->CalcFixedLayout(FALSE, bHorz);
        int d = bHorz ? aSize[i].cy : aSize[i].cx;
        if (d > nDepth) nDepth = d;
      }
      int nRoom = bHorz ? rect.Height() : rect.Width();
      if (nPass == 1 && nDepth > nRoom - kMinDockSize) nDepth = nRoom - kMinDockSize;
      if (nDepth > nRoom) nDepth = nRoom;
      if (nDepth < 0) nDepth = 0;
      CRect rectRow = rect;
      switch (dwAlign) {
        case CBRS_ALIGN_TOP: rectRow.bottom = rect.top += nDepth; break;
        case CBRS_ALIGN_BOTTOM: rectRow.top = rect.bottom -= nDepth; break;
        case CBRS_ALIGN_LEFT: rectRow.right = rect.left += nDepth; break;
        default: rectRow.left = rect.right -= nDepth; break;
      }

      // Along the row: the bars at their length (the last takes what is
      // left when it stretches), the resizable panes share it.
      int nLength = bHorz ? rectRow.Width() : rectRow.Height();
      int x = bHorz ? rectRow.left : rectRow.top;
      int nEnd = x + nLength;
      for (int i = 0; i < n; i++) {
        int cx;
        if (aRow[i]->IsResizable()) {
          cx = (nEnd - x) / (n - i);
        } else {
          CSize sizeStretch = aRow[i]->CalcFixedLayout(TRUE, bHorz);
          cx = bHorz ? sizeStretch.cx : sizeStretch.cy;
          if (i == n - 1 && cx > nEnd - x) cx = nEnd - x;
          if (x + cx > nEnd) cx = nEnd - x > 0 ? nEnd - x : 0;
        }
        CRect r = bHorz ? CRect(x, rectRow.top, x + cx, rectRow.bottom) : CRect(rectRow.left, x, rectRow.right, x + cx);
        aRow[i]->SetWindowPos(NULL, r.left, r.top, r.Width(), r.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
        x += cx;
      }
    }
  }
  if (rect.right < rect.left) rect.right = rect.left;
  if (rect.bottom < rect.top) rect.bottom = rect.top;
  m_rectClientAreaBounds = rect;

  // The auto-hidden panes out on show, over the client area.
  for (int i = 0; i < 4; i++) {
    CMFCAutoHideBar* pBar = m_pAutoHideBars[i];
    if (pBar == NULL) continue;
    for (int j = 0; j < pBar->GetCount(); j++)
      if (pBar->GetPane(j)->IsSlidOut()) pBar->GetPane(j)->_PlaceSlidOut();
  }
  m_bInLayout = FALSE;
}

// ---------------------------------------------------------------------
// CDockablePane

IMPLEMENT_SERIAL(CDockablePane, CPane, VERSIONABLE_SCHEMA | 2)

int CDockablePane::m_nTimeOutBeforeAutoHide = 700;
int CDockablePane::m_nSlideSteps = 12;
BOOL CDockablePane::m_bHideInAutoHideMode = FALSE;
BOOL CDockablePane::m_bDisableAnimation = FALSE;

BEGIN_MESSAGE_MAP(CDockablePane, CPane)
  ON_WM_CREATE()
  ON_WM_NCCALCSIZE()
  ON_WM_NCPAINT()
  ON_WM_NCHITTEST()
  ON_WM_NCLBUTTONDOWN()
  ON_WM_NCLBUTTONDBLCLK()
  ON_WM_NCMOUSEMOVE()
  ON_WM_MOUSEMOVE()
  ON_WM_LBUTTONUP()
  ON_WM_CANCELMODE()
  ON_WM_SETCURSOR()
  ON_WM_SIZE()
  ON_WM_SETFOCUS()
  ON_WM_CONTEXTMENU()
  ON_WM_TIMER()
  ON_MESSAGE(WM_SETTEXT, OnSetText)
  ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
END_MESSAGE_MAP()

CDockablePane::CDockablePane()
    : m_bHasGripper(TRUE), m_bSlidOut(FALSE), m_bTrackingDivider(FALSE), m_bTrackingButton(FALSE),
      m_bCaptionDown(FALSE), m_bWasActive(FALSE), m_dwTabbedStyle(AFX_CBRS_REGULAR_TABS), m_pAutoHideBar(NULL),
      m_pPressedButton(NULL), m_ptTrackStart(0, 0), m_sizeTrackStart(0, 0), m_hIconBig(NULL), m_hIconSmall(NULL) {
  m_lstCaptionButtons.AddTail(new CMFCCaptionButton(AFX_HTCLOSE));
  m_lstCaptionButtons.AddTail(new CMFCCaptionButton(AFX_HTMAXBUTTON));
}

CDockablePane::~CDockablePane() {
  while (!m_lstCaptionButtons.IsEmpty()) delete (CMFCCaptionButton*)m_lstCaptionButtons.RemoveHead();
}

BOOL CDockablePane::Create(LPCTSTR lpszCaption, CWnd* pParentWnd, const RECT& rect, BOOL bHasGripper, UINT nID,
                           DWORD dwStyle, DWORD dwTabbedStyle, DWORD dwControlBarStyle, CCreateContext* pContext) {
  return CreateEx(0, lpszCaption, pParentWnd, rect, bHasGripper, nID, dwStyle, dwTabbedStyle, dwControlBarStyle,
                  pContext);
}

BOOL CDockablePane::Create(LPCTSTR lpszWindowName, CWnd* pParentWnd, CSize sizeDefault, BOOL bHasGripper, UINT nID,
                           DWORD dwStyle, DWORD dwTabbedStyle, DWORD dwControlBarStyle) {
  CRect rect(0, 0, sizeDefault.cx, sizeDefault.cy);
  return CreateEx(0, lpszWindowName, pParentWnd, rect, bHasGripper, nID, dwStyle, dwTabbedStyle,
                  dwControlBarStyle, NULL);
}

BOOL CDockablePane::CreateEx(DWORD dwStyleEx, LPCTSTR lpszCaption, CWnd* pParentWnd, const RECT& rect,
                             BOOL bHasGripper, UINT nID, DWORD dwStyle, DWORD dwTabbedStyle,
                             DWORD dwControlBarStyle, CCreateContext* pContext) {
  m_bHasGripper = bHasGripper;
  m_dwTabbedStyle = dwTabbedStyle;
  if ((dwStyle & CBRS_ALIGN_ANY) == 0) dwStyle |= CBRS_ALIGN_LEFT;
  if (!CBasePane::CreateEx(dwStyleEx, NULL, lpszCaption, dwStyle, rect, pParentWnd, nID, dwControlBarStyle,
                           pContext))
    return FALSE;
  m_dwRecentAlignment = GetCurrentAlignment();
  _UpdateNonClient();
  return TRUE;
}

int CDockablePane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CPane::OnCreate(lpCreateStruct) == -1) return -1;
  return 0;
}

void CDockablePane::EnableGripper(BOOL bEnable) {
  m_bHasGripper = bEnable;
  _UpdateNonClient();
}

int CDockablePane::GetCaptionHeight() const {
  if (!m_bHasGripper || m_pMiniFrame != NULL || m_pTabbedParent != NULL) return 0;
  return afxGlobalData.GetTextHeight() + 4 + CMFCVisualManager::GetInstance()->GetDockingPaneCaptionExtraHeight();
}

DWORD CDockablePane::_DividerSide() const {
  if (m_pMiniFrame != NULL || m_pTabbedParent != NULL) return 0;
  if (m_pAutoHideBar == NULL && !const_cast<CDockablePane*>(this)->IsDocked()) return 0;
  switch (GetCurrentAlignment()) {
    case CBRS_ALIGN_LEFT: return CBRS_ALIGN_RIGHT;
    case CBRS_ALIGN_RIGHT: return CBRS_ALIGN_LEFT;
    case CBRS_ALIGN_TOP: return CBRS_ALIGN_BOTTOM;
    case CBRS_ALIGN_BOTTOM: return CBRS_ALIGN_TOP;
  }
  return 0;
}

CRect CDockablePane::_DividerRect() const {
  CRect r;
  GetWindowRect(r);
  r.OffsetRect(-r.left, -r.top);
  switch (_DividerSide()) {
    case CBRS_ALIGN_RIGHT: r.left = r.right - kDivider; break;
    case CBRS_ALIGN_LEFT: r.right = r.left + kDivider; break;
    case CBRS_ALIGN_BOTTOM: r.top = r.bottom - kDivider; break;
    case CBRS_ALIGN_TOP: r.bottom = r.top + kDivider; break;
    default: r.SetRectEmpty(); break;
  }
  return r;
}

CRect CDockablePane::_CaptionRect() const {
  CRect r;
  GetWindowRect(r);
  r.OffsetRect(-r.left, -r.top);
  switch (_DividerSide()) {
    case CBRS_ALIGN_RIGHT: r.right -= kDivider; break;
    case CBRS_ALIGN_LEFT: r.left += kDivider; break;
    case CBRS_ALIGN_TOP: r.top += kDivider; break;
    case CBRS_ALIGN_BOTTOM: r.bottom -= kDivider; break;
  }
  r.bottom = r.top + GetCaptionHeight();
  return r;
}

CMFCCaptionButton* CDockablePane::_Button(UINT nHit) {
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    if (p->GetHit() == nHit) return p;
  }
  return NULL;
}

void CDockablePane::_LayoutButtons() {
  CRect rectCaption = _CaptionRect();
  CMFCCaptionButton* pClose = _Button(AFX_HTCLOSE);
  CMFCCaptionButton* pPin = _Button(AFX_HTMAXBUTTON);
  pClose->Hide(!CanBeClosed() || rectCaption.IsRectEmpty());
  pPin->Hide(!CanAutoHide() || rectCaption.IsRectEmpty() || m_pDockSite == NULL ||
             (GetDockingManager() != NULL && m_pAutoHideBar == NULL &&
              (GetDockingManager()->GetEnabledAutoHideAlignment() & GetCurrentAlignment()) == 0));
  int x = rectCaption.right - 2;
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    if (p->IsHidden()) {
      p->SetRect(CRect(0, 0, 0, 0));
      continue;
    }
    CSize size = p->GetSize();
    int y = rectCaption.top + (rectCaption.Height() - size.cy) / 2;
    p->SetRect(CRect(x - size.cx, y, x, y + size.cy));
    x -= size.cx + 1;
  }
}

CMFCCaptionButton* CDockablePane::_ButtonAt(CPoint ptWindow) {
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    if (!p->IsHidden() && p->GetRect().PtInRect(ptWindow)) return p;
  }
  return NULL;
}

void CDockablePane::_UpdateNonClient() {
  if (m_hWnd == NULL) return;
  SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE);
}

void CDockablePane::OnNcCalcSize(BOOL, NCCALCSIZE_PARAMS* lpncsp) {
  CRect r(lpncsp->rgrc[0]);
  switch (_DividerSide()) {
    case CBRS_ALIGN_RIGHT: r.right -= kDivider; break;
    case CBRS_ALIGN_LEFT: r.left += kDivider; break;
    case CBRS_ALIGN_TOP: r.top += kDivider; break;
    case CBRS_ALIGN_BOTTOM: r.bottom -= kDivider; break;
  }
  r.top += GetCaptionHeight();
  if (r.bottom < r.top) r.bottom = r.top;
  if (r.right < r.left) r.right = r.left;
  lpncsp->rgrc[0] = r;
}

void CDockablePane::OnDrawCaption(CDC* pDC, CRect rectCaption) {
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  _LayoutButtons();
  BOOL bActive = IsActive();
  CRect rectButtons(rectCaption.right, rectCaption.top, rectCaption.right, rectCaption.bottom);
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    if (!p->IsHidden() && p->GetRect().left < rectButtons.left) rectButtons.left = p->GetRect().left;
  }
  COLORREF clrText = pVM->OnDrawPaneCaption(pDC, this, bActive, rectCaption, rectButtons);
  CString strTitle;
  GetWindowText(strTitle);
  CRect rectText = rectCaption;
  rectText.left += 4;
  rectText.right = rectButtons.left - 2;
  if (m_hIconSmall != NULL) {
    int y = rectCaption.top + (rectCaption.Height() - 16) / 2;
    ::DrawIconEx(pDC->GetSafeHdc(), rectText.left, y, m_hIconSmall, 16, 16, 0, NULL, DI_NORMAL);
    rectText.left += 18;
  }
  CFont* pOld = pDC->SelectObject(&afxGlobalData.fontRegular);
  pDC->SetBkMode(TRANSPARENT);
  pDC->SetTextColor(clrText);
  pDC->DrawText(strTitle, rectText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  pDC->SelectObject(pOld);
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    if (!p->IsHidden())
      pVM->OnDrawCaptionButton(pDC, p, bActive, TRUE, p->GetHit() == AFX_HTMAXBUTTON && IsAutoHideMode(), FALSE);
  }
}

void CDockablePane::OnNcPaint() {
  CRect rectCaption = _CaptionRect();
  CRect rectDivider = _DividerRect();
  if (rectCaption.IsRectEmpty() && rectDivider.IsRectEmpty()) return;
  CWindowDC dc(this);
  if (!rectDivider.IsRectEmpty())
    CMFCVisualManager::GetInstance()->OnDrawPaneDivider(&dc, this, rectDivider,
                                                        (_DividerSide() & CBRS_ORIENT_HORZ) != 0);
  if (!rectCaption.IsRectEmpty()) OnDrawCaption(&dc, rectCaption);
}

LRESULT CDockablePane::OnNcHitTest(CPoint point) {
  CRect rectWnd;
  GetWindowRect(rectWnd);
  CPoint pt = point - rectWnd.TopLeft();
  CRect rectDivider = _DividerRect();
  if (rectDivider.PtInRect(pt)) {
    switch (_DividerSide()) {
      case CBRS_ALIGN_RIGHT: return HTRIGHT;
      case CBRS_ALIGN_LEFT: return HTLEFT;
      case CBRS_ALIGN_TOP: return HTTOP;
      default: return HTBOTTOM;
    }
  }
  CRect rectCaption = _CaptionRect();
  if (rectCaption.PtInRect(pt)) {
    _LayoutButtons();
    CMFCCaptionButton* pButton = _ButtonAt(pt);
    return pButton != NULL ? (LRESULT)pButton->GetHit() : HTCAPTION;
  }
  return CPane::OnNcHitTest(point);
}

void CDockablePane::OnNcLButtonDown(UINT nHitTest, CPoint point) {
  CRect rectWnd;
  GetWindowRect(rectWnd);
  switch (nHitTest) {
    case HTLEFT:
    case HTRIGHT:
    case HTTOP:
    case HTBOTTOM: {
      m_bTrackingDivider = TRUE;
      m_ptTrackStart = point;
      m_sizeTrackStart = rectWnd.Size();
      SetCapture();
      return;
    }
    case HTCAPTION:
      if (!IsActive()) SetFocus();
      if (CanFloat() && m_pAutoHideBar == NULL) {
        m_bCaptionDown = TRUE;
        m_ptTrackStart = point;
        SetCapture();
      }
      return;
    case AFX_HTCLOSE:
    case AFX_HTMAXBUTTON: {
      CMFCCaptionButton* pButton = _Button(nHitTest);
      if (pButton != NULL) {
        m_pPressedButton = pButton;
        pButton->m_bPushed = TRUE;
        m_bTrackingButton = TRUE;
        SetCapture();
        RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
      }
      return;
    }
  }
  CPane::OnNcLButtonDown(nHitTest, point);
}

void CDockablePane::OnNcLButtonDblClk(UINT nHitTest, CPoint point) {
  if (nHitTest == HTCAPTION && CanFloat() && m_pAutoHideBar == NULL) {
    CRect rectWnd;
    GetWindowRect(rectWnd);
    FloatPane(CRect(rectWnd.TopLeft(), rectWnd.Size()), DM_DBL_CLICK);
    return;
  }
  CPane::OnNcLButtonDblClk(nHitTest, point);
}

void CDockablePane::OnNcMouseMove(UINT nHitTest, CPoint point) {
  // The buttons light up under the mouse.
  CRect rectWnd;
  GetWindowRect(rectWnd);
  _LayoutButtons();
  CMFCCaptionButton* pHot = _ButtonAt(CPoint(point - rectWnd.TopLeft()));
  BOOL bChanged = FALSE;
  for (POSITION pos = m_lstCaptionButtons.GetHeadPosition(); pos != NULL;) {
    CMFCCaptionButton* p = (CMFCCaptionButton*)m_lstCaptionButtons.GetNext(pos);
    BOOL bFocused = p == pHot;
    if (p->m_bFocused != bFocused) p->m_bFocused = bFocused, bChanged = TRUE;
  }
  if (bChanged) {
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
    if (pHot != NULL) {
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE | TME_NONCLIENT, m_hWnd, 0};
      ::TrackMouseEvent(&tme);
    }
  }
  CPane::OnNcMouseMove(nHitTest, point);
}

void CDockablePane::_StartFloatDrag(CPoint ptScreen) {
  CRect rectWnd;
  GetWindowRect(rectWnd);
  CPoint ptOffset = ptScreen - rectWnd.TopLeft();
  if (!FloatPane(CRect(rectWnd.TopLeft(), rectWnd.Size()), DM_MOUSE) || m_pMiniFrame == NULL) return;
  // The floating frame follows the mouse, as if its caption were dragged.
  CWnd* pFrame = (CWnd*)m_pMiniFrame;
  CRect rectFrame;
  pFrame->GetWindowRect(rectFrame);
  pFrame->SetWindowPos(NULL, ptScreen.x - ptOffset.x - 4, ptScreen.y - 8, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  pFrame->SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(ptScreen.x, ptScreen.y));
}

void CDockablePane::OnMouseMove(UINT nFlags, CPoint point) {
  CPoint ptScreen = point;
  ClientToScreen(&ptScreen);
  if (m_bTrackingDivider) {
    CSize d = ptScreen - m_ptTrackStart;
    CSize size = m_sizeDock;
    switch (_DividerSide()) {
      case CBRS_ALIGN_RIGHT: size.cx = m_sizeTrackStart.cx + d.cx; break;
      case CBRS_ALIGN_LEFT: size.cx = m_sizeTrackStart.cx - d.cx; break;
      case CBRS_ALIGN_BOTTOM: size.cy = m_sizeTrackStart.cy + d.cy; break;
      case CBRS_ALIGN_TOP: size.cy = m_sizeTrackStart.cy - d.cy; break;
    }
    int nMinX = m_sizeMin.cx > kMinDockSize ? m_sizeMin.cx : kMinDockSize;
    int nMinY = m_sizeMin.cy > kMinDockSize ? m_sizeMin.cy : kMinDockSize;
    if (size.cx < nMinX) size.cx = nMinX;
    if (size.cy < nMinY) size.cy = nMinY;
    if (size != m_sizeDock) {
      m_sizeDock = size;
      if (m_bSlidOut)
        _PlaceSlidOut();
      else if (m_pDockSite != NULL)
        m_pDockSite->RecalcLayout();
    }
    return;
  }
  if (m_bCaptionDown) {
    CSize d = ptScreen - m_ptTrackStart;
    if (abs(d.cx) > ::GetSystemMetrics(SM_CXDRAG) || abs(d.cy) > ::GetSystemMetrics(SM_CYDRAG)) {
      m_bCaptionDown = FALSE;
      ReleaseCapture();
      _StartFloatDrag(ptScreen);
    }
    return;
  }
  if (m_bTrackingButton && m_pPressedButton != NULL) {
    CRect rectWnd;
    GetWindowRect(rectWnd);
    BOOL bIn = m_pPressedButton->GetRect().PtInRect(CPoint(ptScreen - rectWnd.TopLeft()));
    if (bIn != m_pPressedButton->m_bPushed) {
      m_pPressedButton->m_bPushed = bIn;
      RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
    }
    return;
  }
  CPane::OnMouseMove(nFlags, point);
}

void CDockablePane::OnLButtonUp(UINT nFlags, CPoint point) {
  if (m_bTrackingDivider || m_bCaptionDown) {
    m_bTrackingDivider = m_bCaptionDown = FALSE;
    ReleaseCapture();
    return;
  }
  if (m_bTrackingButton) {
    m_bTrackingButton = FALSE;
    ReleaseCapture();
    CMFCCaptionButton* pButton = m_pPressedButton;
    m_pPressedButton = NULL;
    if (pButton != NULL) {
      BOOL bPushed = pButton->m_bPushed;
      pButton->m_bPushed = FALSE;
      RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
      if (bPushed) {
        UINT nHit = pButton->GetHit();
        if (nHit == AFX_HTCLOSE)
          OnPressCloseButton();
        else if (nHit == AFX_HTMAXBUTTON)
          SetAutoHideMode(!IsAutoHideMode(), GetCurrentAlignment());
        OnPressButtons(nHit);
      }
    }
    return;
  }
  CPane::OnLButtonUp(nFlags, point);
}

void CDockablePane::OnCancelMode() {
  CPane::OnCancelMode();
  if (m_bTrackingDivider || m_bCaptionDown || m_bTrackingButton) {
    m_bTrackingDivider = m_bCaptionDown = m_bTrackingButton = FALSE;
    if (m_pPressedButton != NULL) m_pPressedButton->m_bPushed = FALSE;
    m_pPressedButton = NULL;
    if (::GetCapture() == m_hWnd) ReleaseCapture();
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
  }
}

BOOL CDockablePane::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (m_bTrackingDivider) {
    DWORD dwSide = _DividerSide();
    ::SetCursor(::LoadCursor(NULL, (dwSide & CBRS_ORIENT_HORZ) ? IDC_SIZENS : IDC_SIZEWE));
    return TRUE;
  }
  return CPane::OnSetCursor(pWnd, nHitTest, message);
}

void CDockablePane::OnSize(UINT nType, int cx, int cy) { CPane::OnSize(nType, cx, cy); }

void CDockablePane::OnSetFocus(CWnd* pOldWnd) {
  CPane::OnSetFocus(pOldWnd);
  // The first child (the pane's content) takes the focus.
  CWnd* pChild = GetWindow(GW_CHILD);
  if (pChild != NULL && pChild->IsWindowVisible()) pChild->SetFocus();
  RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
}

LRESULT CDockablePane::OnSetText(WPARAM, LPARAM lParam) {
  LRESULT lResult = Default();
  RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
  LPCTSTR lpszText = (LPCTSTR)lParam;
  if (m_pMiniFrame != NULL) ((CWnd*)m_pMiniFrame)->SetWindowText(lpszText);
  if (m_pTabbedParent != NULL) {
    CBaseTabbedPane* pTabbed = (CBaseTabbedPane*)m_pTabbedParent;
    CMFCBaseTabCtrl* pTabs = pTabbed->GetTabWnd();
    int iTab = pTabs != NULL ? pTabs->GetTabFromHwnd(m_hWnd) : -1;
    if (iTab >= 0) pTabs->SetTabLabel(iTab, lpszText != NULL ? lpszText : _T(""));
    pTabbed->_UpdateCaption();
  }
  if (m_pAutoHideBar != NULL) {
    m_pAutoHideBar->_Layout();
    if (m_pDockSite != NULL) m_pDockSite->RecalcLayout();
  }
  return lResult;
}

void CDockablePane::OnContextMenu(CWnd* pWnd, CPoint point) { CPane::OnContextMenu(pWnd, point); }

LRESULT CDockablePane::OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam) {
  // The caption shows whether the focus is in the pane.
  BOOL bActive = IsActive();
  if (bActive != m_bWasActive) {
    m_bWasActive = bActive;
    if (GetCaptionHeight() > 0) RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
  }
  return CBasePane::OnIdleUpdateCmdUI(wParam, lParam);
}

BOOL CDockablePane::IsActive() const {
  HWND hFocus = ::GetFocus();
  return m_hWnd != NULL && hFocus != NULL && (hFocus == m_hWnd || ::IsChild(m_hWnd, hFocus));
}

HICON CDockablePane::SetIcon(HICON hIcon, BOOL bBigIcon) {
  HICON hOld = bBigIcon ? m_hIconBig : m_hIconSmall;
  if (bBigIcon)
    m_hIconBig = hIcon;
  else
    m_hIconSmall = hIcon;
  if (m_hWnd != NULL) RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
  return hOld;
}

CSize CDockablePane::CalcFixedLayout(BOOL, BOOL) { return m_sizeDock; }

BOOL CDockablePane::DockToFrameWindow(DWORD dwAlignment, LPCRECT lpRect, DWORD dwDockFlags,
                                      CBasePane* pRelativeBar, int nRelativeIndex, BOOL bOuterEdge) {
  if (lpRect != NULL && !::IsRectEmpty(lpRect)) m_sizeDock = CRect(lpRect).Size();
  return CPane::DockToFrameWindow(dwAlignment, lpRect, dwDockFlags, pRelativeBar, nRelativeIndex, bOuterEdge);
}

BOOL CDockablePane::DockToWindow(CDockablePane* pTargetWindow, DWORD dwAlignment, LPCRECT lpRect) {
  CDockingManager* pManager = GetDockingManager();
  if (pManager == NULL || pTargetWindow == NULL || !pManager->IsPaneDocked(pTargetWindow)) return FALSE;
  if (m_pTabbedParent != NULL) ((CBaseTabbedPane*)m_pTabbedParent)->DetachPane(this, TRUE);
  if (m_pAutoHideBar != NULL) SetAutoHideMode(FALSE, GetCurrentAlignment());
  if (m_pMiniFrame != NULL) m_pMiniFrame->RemovePane(this);
  if (lpRect != NULL && !::IsRectEmpty(lpRect)) m_sizeDock = CRect(lpRect).Size();
  // Along the target's side: in its row; across it: a row of its own.
  BOOL bTargetHorz = (pTargetWindow->GetCurrentAlignment() & CBRS_ORIENT_HORZ) != 0;
  BOOL bAlongHorz = (dwAlignment & (CBRS_ALIGN_LEFT | CBRS_ALIGN_RIGHT)) != 0;
  BOOL bSameRow = bTargetHorz == bAlongHorz;
  BOOL bBefore;
  if (bSameRow) {
    bBefore = (dwAlignment & (CBRS_ALIGN_LEFT | CBRS_ALIGN_TOP)) != 0;
  } else {
    // Before means nearer the frame's edge.
    DWORD dwTarget = pTargetWindow->GetCurrentAlignment();
    bBefore = (dwAlignment & dwTarget) != 0;
  }
  pManager->_InsertDocked(this, pTargetWindow, bSameRow, bBefore);
  m_dwRecentAlignment = GetCurrentAlignment();
  ShowWindow(SW_SHOWNOACTIVATE);
  _UpdateNonClient();
  pManager->_RecalcFrame();
  return TRUE;
}

BOOL CDockablePane::FloatPane(CRect rectFloat, AFX_DOCK_METHOD dockMethod, bool bShow) {
  if (!CanFloat()) return FALSE;
  if (m_pTabbedParent != NULL) ((CBaseTabbedPane*)m_pTabbedParent)->DetachPane(this, TRUE);
  if (m_pAutoHideBar != NULL) SetAutoHideMode(FALSE, GetCurrentAlignment());
  BOOL bOK = CPane::FloatPane(rectFloat, dockMethod, bShow);
  _UpdateNonClient();
  return bOK;
}

CTabbedPane* CDockablePane::GetParentTabbedPane() const {
  return m_pTabbedParent != NULL ? DYNAMIC_DOWNCAST(CTabbedPane, m_pTabbedParent) : NULL;
}

CDockablePane* CDockablePane::AttachToTabWnd(CDockablePane* pTabControlBarAttachTo, AFX_DOCK_METHOD,
                                             BOOL bSetActive, CDockablePane** ppTabbedControlBar) {
  if (pTabControlBarAttachTo == NULL || pTabControlBarAttachTo == this) return NULL;
  CDockingManager* pManager = GetDockingManager();
  CBaseTabbedPane* pTabbed = DYNAMIC_DOWNCAST(CBaseTabbedPane, pTabControlBarAttachTo);
  if (pTabbed == NULL && pTabControlBarAttachTo->m_pTabbedParent != NULL)
    pTabbed = (CBaseTabbedPane*)pTabControlBarAttachTo->m_pTabbedParent;
  if (pTabbed == NULL) {
    // A new tabbed pane where the target is, with the target its first tab.
    CDockablePane* pTarget = pTabControlBarAttachTo;
    if (pTarget->m_pAutoHideBar != NULL) pTarget->SetAutoHideMode(FALSE, pTarget->GetCurrentAlignment());
    CTabbedPane* pNew = new CTabbedPane(TRUE);
    CRect rect;
    pTarget->GetWindowRect(rect);
    CString strTitle;
    pTarget->GetWindowText(strTitle);
    DWORD dwStyle = WS_CHILD | (pTarget->GetStyle() & WS_VISIBLE) | pTarget->GetCurrentAlignment() | CBRS_FLOAT_MULTI;
    if (!pNew->Create(strTitle, m_pDockSite, CRect(0, 0, rect.Width(), rect.Height()), TRUE, (UINT)-1, dwStyle,
                      AFX_CBRS_REGULAR_TABS, pTarget->GetControlBarStyle())) {
      delete pNew;
      return NULL;
    }
    pNew->m_sizeDock = pTarget->m_sizeDock;
    pNew->m_dwRecentAlignment = pTarget->m_dwRecentAlignment;
    if (pTarget->m_pMiniFrame != NULL) {
      CRect rectFrame;
      ((CWnd*)pTarget->m_pMiniFrame)->GetWindowRect(rectFrame);
      pTarget->m_pMiniFrame->RemovePane(pTarget);
      if (pManager != NULL) pManager->_Remove(pNew);
      pNew->CPane::FloatPane(rectFrame, DM_UNKNOWN, true);
    } else if (pManager != NULL) {
      pManager->_ReplaceDocked(pTarget, pNew);
    }
    pNew->AddTab(pTarget, TRUE, TRUE);
    pTabbed = pNew;
  }
  // Out of where this pane is now.
  if (m_pTabbedParent != NULL && m_pTabbedParent != pTabbed)
    ((CBaseTabbedPane*)m_pTabbedParent)->DetachPane(this, TRUE);
  if (m_pAutoHideBar != NULL) SetAutoHideMode(FALSE, GetCurrentAlignment());
  if (m_pMiniFrame != NULL) m_pMiniFrame->RemovePane(this);
  if (pManager != NULL) pManager->_Remove(this);
  if (m_pTabbedParent == NULL) pTabbed->AddTab(this, TRUE, bSetActive);
  if (ppTabbedControlBar != NULL) *ppTabbedControlBar = pTabbed;
  if (pManager != NULL) pManager->_RecalcFrame();
  return this;
}

void CDockablePane::SetAutoHideMode(BOOL bMode, DWORD dwAlignment, CMFCAutoHideBar*, BOOL) {
  CDockingManager* pManager = GetDockingManager();
  if (pManager == NULL || bMode == IsAutoHideMode()) return;
  if (dwAlignment == 0) dwAlignment = GetCurrentAlignment();
  if (bMode) {
    if (!pManager->IsPaneDocked(this)) return;
    CMFCAutoHideBar* pBar = pManager->GetAutoHideBar(dwAlignment, TRUE);
    if (pBar == NULL) return;
    pManager->_Remove(this);
    m_pAutoHideBar = pBar;
    pBar->AddAutoHidePane(this);
    m_bSlidOut = FALSE;
    ShowWindow(SW_HIDE);
    _UpdateNonClient();
    pManager->_RecalcFrame();
  } else {
    if (m_bSlidOut) Slide(FALSE, FALSE);
    CMFCAutoHideBar* pBar = m_pAutoHideBar;
    m_pAutoHideBar = NULL;
    if (pBar != NULL) pBar->RemoveAutoHidePane(this);
    // Back in the frame, innermost on its side.
    SetPaneAlignment(dwAlignment);
    if (pManager->m_lstPanes.Find(this) == NULL) pManager->m_lstPanes.AddTail(this);
    m_bSameRowAsPrev = FALSE;
    pManager->m_lstDocked.AddTail(this);
    ShowWindow(SW_SHOWNOACTIVATE);
    _UpdateNonClient();
    pManager->_RecalcFrame();
  }
}

void CDockablePane::_PlaceSlidOut() {
  CDockingManager* pManager = GetDockingManager();
  if (pManager == NULL) return;
  CRect rectBounds = pManager->GetClientAreaBounds();
  CRect r = rectBounds;
  int cx = m_sizeDock.cx, cy = m_sizeDock.cy;
  if (cx > rectBounds.Width()) cx = rectBounds.Width();
  if (cy > rectBounds.Height()) cy = rectBounds.Height();
  switch (GetCurrentAlignment()) {
    case CBRS_ALIGN_LEFT: r.right = r.left + cx; break;
    case CBRS_ALIGN_RIGHT: r.left = r.right - cx; break;
    case CBRS_ALIGN_TOP: r.bottom = r.top + cy; break;
    default: r.top = r.bottom - cy; break;
  }
  SetWindowPos(&CWnd::wndTop, r.left, r.top, r.Width(), r.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CDockablePane::Slide(BOOL bSlideOut, BOOL) {
  if (m_pAutoHideBar == NULL || bSlideOut == m_bSlidOut) return;
  CDockingManager* pManager = GetDockingManager();
  if (bSlideOut) {
    if (pManager != NULL) pManager->HideAutoHidePanes(this);
    m_bSlidOut = TRUE;
    _UpdateNonClient();
    _PlaceSlidOut();
    // Over the view: drawn now, caption and all, not after the view paints.
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetTimer(kSlideTimer, 250, NULL);
  } else {
    m_bSlidOut = FALSE;
    KillTimer(kSlideTimer);
    ShowWindow(SW_HIDE);
  }
  m_pAutoHideBar->Invalidate();
}

void CDockablePane::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent != kSlideTimer) {
    CPane::OnTimer(nIDEvent);
    return;
  }
  // Back in when neither the mouse nor the focus is in the pane (or on
  // its button).
  if (!m_bSlidOut || m_bTrackingDivider || m_bTrackingButton) return;
  CPoint pt;
  ::GetCursorPos(&pt);
  CRect rectWnd;
  GetWindowRect(rectWnd);
  if (rectWnd.PtInRect(pt) || IsActive()) return;
  if (m_pAutoHideBar != NULL) {
    CRect rectButton = m_pAutoHideBar->GetButtonRect(this);
    m_pAutoHideBar->ClientToScreen(rectButton);
    if (rectButton.PtInRect(pt)) return;
  }
  Slide(FALSE, FALSE);
}

void CDockablePane::ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate) {
  if (m_pTabbedParent != NULL) {
    ((CBaseTabbedPane*)m_pTabbedParent)->ShowTab(this, bShow, bDelay, bActivate);
    return;
  }
  if (m_pAutoHideBar != NULL) {
    if (bShow) {
      Slide(TRUE, FALSE);
      if (bActivate) SetFocus();
      return;
    }
    SetAutoHideMode(FALSE, GetCurrentAlignment());
  }
  CPane::ShowPane(bShow, bDelay, bActivate);
}

void CDockablePane::OnPressCloseButton() {
  CFrameWndEx* pFrameEx = DYNAMIC_DOWNCAST(CFrameWndEx, m_pDockSite);
  if (pFrameEx != NULL && !pFrameEx->OnCloseDockingPane(this)) return;
  CMDIFrameWndEx* pMDIEx = DYNAMIC_DOWNCAST(CMDIFrameWndEx, m_pDockSite);
  if (pMDIEx != NULL && !pMDIEx->OnCloseDockingPane(this)) return;
  ShowPane(FALSE, FALSE, FALSE);
}

// ---------------------------------------------------------------------
// CBaseTabbedPane and CTabbedPane

IMPLEMENT_DYNAMIC(CBaseTabbedPane, CDockablePane)

BEGIN_MESSAGE_MAP(CBaseTabbedPane, CDockablePane)
  ON_WM_SIZE()
  ON_WM_DESTROY()
  ON_REGISTERED_MESSAGE(AFX_WM_CHANGE_ACTIVE_TAB, OnChangeActiveTab)
END_MESSAGE_MAP()

CBaseTabbedPane::CBaseTabbedPane(BOOL bAutoDestroy)
    : m_pTabWnd(NULL), m_bAutoDestroy(bAutoDestroy), m_bSetCaptionTextToTabName(TRUE) {}

CBaseTabbedPane::~CBaseTabbedPane() {}

void CBaseTabbedPane::PostNcDestroy() {
  if (m_bAutoDestroy) delete this;
}

BOOL CBaseTabbedPane::AddTab(CWnd* pNewBar, BOOL bVisible, BOOL bSetActive, BOOL bDetachable) {
  if (m_pTabWnd == NULL || pNewBar == NULL) return FALSE;
  CString strLabel;
  pNewBar->GetWindowText(strLabel);
  CBasePane* pPane = DYNAMIC_DOWNCAST(CBasePane, pNewBar);
  if (pPane != NULL) {
    CDockingManager* pManager = GetDockingManager();
    if (pManager != NULL) pManager->_Remove(pPane);
    pPane->m_pTabbedParent = this;
    pPane->SetPaneAlignment(GetCurrentAlignment());
  }
  pNewBar->SetParent(m_pTabWnd);
  m_pTabWnd->AddTab(pNewBar, strLabel, (UINT)-1, bDetachable);
  int iTab = m_pTabWnd->GetTabsNum() - 1;
  if (!bVisible) m_pTabWnd->ShowTab(iTab, FALSE);
  if (bSetActive || m_pTabWnd->GetTabsNum() == 1) m_pTabWnd->SetActiveTab(iTab);
  CDockablePane* pDockable = DYNAMIC_DOWNCAST(CDockablePane, pNewBar);
  if (pDockable != NULL) pDockable->_UpdateNonClient();
  _UpdateCaption();
  return TRUE;
}

BOOL CBaseTabbedPane::DetachPane(CWnd* pBar, BOOL bHide) {
  if (m_pTabWnd == NULL || pBar == NULL) return FALSE;
  int iTab = m_pTabWnd->GetTabFromHwnd(pBar->GetSafeHwnd());
  if (iTab < 0) return FALSE;
  m_pTabWnd->RemoveTab(iTab);
  CBasePane* pPane = DYNAMIC_DOWNCAST(CBasePane, pBar);
  if (pPane != NULL) pPane->m_pTabbedParent = NULL;
  pBar->SetParent(m_pDockSite);
  CDockablePane* pDockable = DYNAMIC_DOWNCAST(CDockablePane, pBar);
  if (bHide) {
    pBar->ShowWindow(SW_HIDE);
  } else if (pDockable != NULL) {
    CRect rect;
    GetWindowRect(rect);
    pBar->ShowWindow(SW_SHOWNOACTIVATE);
    pDockable->FloatPane(CRect(rect.TopLeft() + CPoint(24, 24), rect.Size()), DM_UNKNOWN);
  }
  if (pDockable != NULL) pDockable->_UpdateNonClient();
  _CollapseIfSingle();
  return TRUE;
}

BOOL CBaseTabbedPane::RemovePane(CWnd* pBar) { return DetachPane(pBar, FALSE); }

void CBaseTabbedPane::_CollapseIfSingle() {
  if (!m_bAutoDestroy || m_pTabWnd == NULL || m_hWnd == NULL || m_pTabWnd->GetTabsNum() > 1) return;
  CDockingManager* pManager = GetDockingManager();
  if (m_pTabWnd->GetTabsNum() == 1) {
    // The last pane takes this one's place, docked or floating.
    CWnd* pLast = m_pTabWnd->GetTabWnd(0);
    BOOL bVisible = m_pTabWnd->IsTabVisible(0);
    m_pTabWnd->RemoveTab(0);
    CDockablePane* pPane = DYNAMIC_DOWNCAST(CDockablePane, pLast);
    pLast->SetParent(m_pDockSite);
    if (pPane != NULL) {
      pPane->m_pTabbedParent = NULL;
      pPane->m_sizeDock = m_sizeDock;
      if (m_pMiniFrame != NULL) {
        CRect rect;
        ((CWnd*)m_pMiniFrame)->GetWindowRect(rect);
        pPane->ShowWindow(SW_SHOWNOACTIVATE);
        pPane->CPane::FloatPane(rect, DM_UNKNOWN, true);
      } else if (pManager != NULL) {
        pManager->_ReplaceDocked(this, pPane);
      }
      pPane->ShowWindow(bVisible && (GetStyle() & WS_VISIBLE) ? SW_SHOWNOACTIVATE : SW_HIDE);
      pPane->_UpdateNonClient();
    }
  }
  if (m_pMiniFrame != NULL) m_pMiniFrame->RemovePane(this);
  if (pManager != NULL) pManager->RemovePaneFromDockManager(this, FALSE, FALSE, FALSE, NULL);
  DestroyWindow();  // deletes this
  if (pManager != NULL) pManager->_RecalcFrame();
}

CWnd* CBaseTabbedPane::FindPaneByID(UINT uBarID) {
  if (m_pTabWnd == NULL) return NULL;
  for (int i = 0; i < m_pTabWnd->GetTabsNum(); i++) {
    CWnd* p = m_pTabWnd->GetTabWnd(i);
    if (p != NULL && (UINT)p->GetDlgCtrlID() == uBarID) return p;
  }
  return NULL;
}

CWnd* CBaseTabbedPane::FindBarByTabNumber(int nTabNum, BOOL) {
  return m_pTabWnd != NULL ? m_pTabWnd->GetTabWnd(nTabNum) : NULL;
}

BOOL CBaseTabbedPane::ShowTab(CWnd* pBar, BOOL bShow, BOOL bDelay, BOOL bActivate) {
  if (m_pTabWnd == NULL || pBar == NULL) return FALSE;
  int iTab = m_pTabWnd->GetTabFromHwnd(pBar->GetSafeHwnd());
  if (iTab < 0) return FALSE;
  m_pTabWnd->ShowTab(iTab, bShow, TRUE, bShow);
  if (bShow) m_pTabWnd->SetActiveTab(iTab);
  int nVisible = GetVisibleTabsNum();
  if (bShow && !IsVisible())
    CDockablePane::ShowPane(TRUE, bDelay, FALSE);
  else if (nVisible == 0 && IsVisible())
    CDockablePane::ShowPane(FALSE, bDelay, FALSE);
  if (bShow && bActivate) pBar->SetFocus();
  _UpdateCaption();
  return TRUE;
}

void CBaseTabbedPane::ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate) {
  CDockablePane::ShowPane(bShow, bDelay, bActivate);
}

int CBaseTabbedPane::GetTabsNum() const { return m_pTabWnd != NULL ? m_pTabWnd->GetTabsNum() : 0; }

int CBaseTabbedPane::GetVisibleTabsNum() const { return m_pTabWnd != NULL ? m_pTabWnd->GetVisibleTabsNum() : 0; }

void CBaseTabbedPane::_UpdateCaption() {
  if (!m_bSetCaptionTextToTabName || m_pTabWnd == NULL || m_hWnd == NULL) return;
  CString strLabel;
  if (m_pTabWnd->GetTabLabel(m_pTabWnd->GetActiveTab(), strLabel)) {
    CString strOld;
    GetWindowText(strOld);
    if (strOld != strLabel) SetWindowText(strLabel);
  }
}

void CBaseTabbedPane::OnSize(UINT nType, int cx, int cy) {
  CDockablePane::OnSize(nType, cx, cy);
  if (m_pTabWnd != NULL && m_pTabWnd->GetSafeHwnd() != NULL)
    m_pTabWnd->SetWindowPos(NULL, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CBaseTabbedPane::OnChangeActiveTab(WPARAM, LPARAM) {
  _UpdateCaption();
  return 0;
}

void CBaseTabbedPane::OnDestroy() {
  // The panes in the tabs are the application's: give them back to the
  // frame before the tab control goes.
  if (m_pTabWnd != NULL && m_pTabWnd->GetSafeHwnd() != NULL) {
    while (m_pTabWnd->GetTabsNum() > 0) {
      CWnd* p = m_pTabWnd->GetTabWnd(0);
      m_pTabWnd->RemoveTab(0, FALSE);
      CBasePane* pPane = DYNAMIC_DOWNCAST(CBasePane, p);
      if (pPane != NULL) pPane->m_pTabbedParent = NULL;
      // (A frame on its way out is hidden first; its panes go with it.)
      if (p != NULL && p->GetSafeHwnd() != NULL && m_pDockSite != NULL && m_pDockSite->GetSafeHwnd() != NULL &&
          ::IsWindowVisible(m_pDockSite->m_hWnd)) {
        p->ShowWindow(SW_HIDE);
        p->SetParent(m_pDockSite);
      }
    }
  }
  CDockablePane::OnDestroy();
  delete m_pTabWnd;
  m_pTabWnd = NULL;
}

IMPLEMENT_SERIAL(CTabbedPane, CBaseTabbedPane, 1)

CMFCTabCtrl::Style CTabbedPane::m_StyleTabWnd = CMFCTabCtrl::STYLE_3D;
BOOL CTabbedPane::m_bTabsAlwaysTop = FALSE;
BOOL CTabbedPane::m_bAutoColor = FALSE;

BEGIN_MESSAGE_MAP(CTabbedPane, CBaseTabbedPane)
  ON_WM_CREATE()
END_MESSAGE_MAP()

CTabbedPane::CTabbedPane(BOOL bAutoDestroy) : CBaseTabbedPane(bAutoDestroy) {}

CTabbedPane::~CTabbedPane() {}

int CTabbedPane::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CBaseTabbedPane::OnCreate(lpCreateStruct) == -1) return -1;
  CMFCTabCtrl* pTabs = new CMFCTabCtrl;
  CRect rect;
  GetClientRect(rect);
  if (!pTabs->Create(m_StyleTabWnd, rect, this, 101,
                     m_bTabsAlwaysTop ? CMFCTabCtrl::LOCATION_TOP : CMFCTabCtrl::LOCATION_BOTTOM)) {
    delete pTabs;
    return -1;
  }
  pTabs->AutoDestroyWindow(FALSE);
  pTabs->SetFlatFrame(TRUE, FALSE);
  if (m_bAutoColor) pTabs->EnableAutoColor(TRUE);
  m_pTabWnd = pTabs;
  return 0;
}

// ---------------------------------------------------------------------
// CPaneFrameWnd

IMPLEMENT_SERIAL(CPaneFrameWnd, CWnd, VERSIONABLE_SCHEMA | 2)

BEGIN_MESSAGE_MAP(CPaneFrameWnd, CWnd)
  ON_WM_SIZE()
  ON_WM_CLOSE()
  ON_WM_NCLBUTTONDBLCLK()
  ON_WM_MOVE()
END_MESSAGE_MAP()

CPaneFrameWnd::CPaneFrameWnd() : m_pPane(NULL), m_pDockSite(NULL), m_pDockManager(NULL) {}

CPaneFrameWnd::~CPaneFrameWnd() {}

BOOL CPaneFrameWnd::Create(LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                           CCreateContext* pContext) {
  return CreateEx(WS_EX_TOOLWINDOW, lpszWindowName, dwStyle, rect, pParentWnd, pContext);
}

BOOL CPaneFrameWnd::CreateEx(DWORD dwStyleEx, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
                             CWnd* pParentWnd, CCreateContext*) {
  m_pDockSite = DYNAMIC_DOWNCAST(CFrameWnd, pParentWnd);
  if (m_pDockSite == NULL && pParentWnd != NULL) m_pDockSite = pParentWnd->GetTopLevelFrame();
  m_pDockManager = CBasePane::GetDockingManagerOf(m_pDockSite);
  LPCTSTR lpszClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1));
  return CWnd::CreateEx(dwStyleEx | WS_EX_TOOLWINDOW, lpszClass, lpszWindowName, (dwStyle | WS_POPUP) & ~WS_CHILD,
                        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                        pParentWnd != NULL ? pParentWnd->GetSafeHwnd() : NULL, NULL);
}

void CPaneFrameWnd::AddPane(CBasePane* pWnd) {
  if (pWnd == NULL) return;
  m_pPane = pWnd;
  pWnd->m_pMiniFrame = this;
  pWnd->SetParent(this);
  CString strTitle;
  pWnd->GetWindowText(strTitle);
  SetWindowText(strTitle);
  OnPaneRecalcLayout();
}

void CPaneFrameWnd::RemovePane(CBasePane* pWnd, BOOL bDestroy, BOOL) {
  if (pWnd == NULL || pWnd != m_pPane) return;
  CRect rect;
  GetWindowRect(rect);
  CPane* pPane = DYNAMIC_DOWNCAST(CPane, pWnd);
  if (pPane != NULL) pPane->m_rectFloat = rect;
  pWnd->m_pMiniFrame = NULL;
  m_pPane = NULL;
  if (pWnd->GetSafeHwnd() != NULL) {
    if (bDestroy)
      pWnd->DestroyWindow();
    else
      pWnd->SetParent(m_pDockSite);
  }
  DestroyWindow();  // deletes this
}

void CPaneFrameWnd::OnPaneRecalcLayout() {
  if (m_pPane == NULL || m_hWnd == NULL) return;
  CRect rectClient;
  GetClientRect(rectClient);
  if (!m_pPane->IsResizable()) {
    // A bar sets the frame's size.
    CSize size = m_pPane->CalcFixedLayout(FALSE, TRUE);
    if (size != rectClient.Size()) {
      CRect rectFrame(0, 0, size.cx, size.cy);
      ::AdjustWindowRectEx(rectFrame, GetStyle(), FALSE, GetExStyle());
      SetWindowPos(NULL, 0, 0, rectFrame.Width(), rectFrame.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      GetClientRect(rectClient);
    }
  }
  m_pPane->SetWindowPos(NULL, 0, 0, rectClient.Width(), rectClient.Height(),
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

BOOL CPaneFrameWnd::DockPane(BOOL& bWasDocked) {
  bWasDocked = FALSE;
  CPane* pPane = DYNAMIC_DOWNCAST(CPane, m_pPane);
  return pPane != NULL && pPane->DockPaneStandard(bWasDocked);
}

void CPaneFrameWnd::PostNcDestroy() { delete this; }

void CPaneFrameWnd::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  if (m_pPane != NULL && m_pPane->GetSafeHwnd() != NULL)
    m_pPane->SetWindowPos(NULL, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CPaneFrameWnd::OnClose() {
  if (m_pPane == NULL) {
    CWnd::OnClose();
    return;
  }
  CFrameWndEx* pFrameEx = DYNAMIC_DOWNCAST(CFrameWndEx, m_pDockSite);
  if (pFrameEx != NULL && !pFrameEx->OnCloseMiniFrame(this)) return;
  CMDIFrameWndEx* pMDIEx = DYNAMIC_DOWNCAST(CMDIFrameWndEx, m_pDockSite);
  if (pMDIEx != NULL && !pMDIEx->OnCloseMiniFrame(this)) return;
  m_pPane->ShowPane(FALSE, FALSE, FALSE);
}

void CPaneFrameWnd::OnNcLButtonDblClk(UINT nHitTest, CPoint point) {
  if (nHitTest == HTCAPTION && m_pPane != NULL) {
    BOOL bWasDocked;
    DockPane(bWasDocked);
    return;
  }
  CWnd::OnNcLButtonDblClk(nHitTest, point);
}

void CPaneFrameWnd::OnMove(int x, int y) {
  CWnd::OnMove(x, y);
  CPane* pPane = DYNAMIC_DOWNCAST(CPane, m_pPane);
  if (pPane != NULL) GetWindowRect(pPane->m_rectFloat);
}

// ---------------------------------------------------------------------
// CMFCAutoHideBar

IMPLEMENT_DYNCREATE(CMFCAutoHideBar, CPane)

int CMFCAutoHideBar::m_nShowAHWndDelay = 400;

BEGIN_MESSAGE_MAP(CMFCAutoHideBar, CPane)
  ON_WM_PAINT()
  ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

CMFCAutoHideBar::CMFCAutoHideBar() {}

CMFCAutoHideBar::~CMFCAutoHideBar() {}

BOOL CMFCAutoHideBar::Create(CWnd* pParentWnd, DWORD dwAlignment) {
  return CPane::CreateEx(0, NULL, WS_CHILD | (dwAlignment & CBRS_ALIGN_ANY), CRect(0, 0, 0, 0), pParentWnd,
                         AFX_IDW_CONTROLBAR_LAST - _AfxAutoHideIndex(dwAlignment), 0);
}

void CMFCAutoHideBar::AddAutoHidePane(CDockablePane* pPane) {
  for (int i = 0; i < GetCount(); i++)
    if (m_arPanes[i] == pPane) return;
  m_arPanes.Add(pPane);
  _Layout();
}

BOOL CMFCAutoHideBar::RemoveAutoHidePane(CDockablePane* pPane) {
  for (int i = 0; i < GetCount(); i++) {
    if (m_arPanes[i] == pPane) {
      m_arPanes.RemoveAt(i);
      _Layout();
      return TRUE;
    }
  }
  return FALSE;
}

void CMFCAutoHideBar::_Layout() {
  m_arRects.SetSize(GetCount());
  if (m_hWnd == NULL) return;
  BOOL bHorz = IsHorizontal();
  CClientDC dc(this);
  CFont* pOld = dc.SelectObject(&afxGlobalData.fontRegular);
  int nDepth = afxGlobalData.GetTextHeight() + 6;
  int x = afxGlobalData.m_nAutoHideToolBarMargin;
  for (int i = 0; i < GetCount(); i++) {
    CString strTitle;
    m_arPanes[i]->GetWindowText(strTitle);
    int cx = dc.GetTextExtent(strTitle).cx + 12 + (m_arPanes[i]->GetIcon(FALSE) != NULL ? 18 : 0);
    m_arRects[i] = bHorz ? CRect(x, 1, x + cx, nDepth - 1) : CRect(1, x, nDepth - 1, x + cx);
    x += cx + afxGlobalData.m_nAutoHideToolBarSpacing;
  }
  dc.SelectObject(pOld);
  Invalidate();
}

CRect CMFCAutoHideBar::GetButtonRect(CDockablePane* pPane) const {
  for (int i = 0; i < GetCount(); i++)
    if (m_arPanes[i] == pPane && i < m_arRects.GetSize()) return m_arRects[i];
  return CRect(0, 0, 0, 0);
}

CDockablePane* CMFCAutoHideBar::HitTest(CPoint ptClient) const {
  for (int i = 0; i < GetCount() && i < m_arRects.GetSize(); i++)
    if (m_arRects[i].PtInRect(ptClient)) return m_arPanes[i];
  return NULL;
}

CSize CMFCAutoHideBar::CalcFixedLayout(BOOL, BOOL bHorz) {
  int nDepth = afxGlobalData.GetTextHeight() + 6;
  int nLength = 0;
  for (int i = 0; i < m_arRects.GetSize(); i++) nLength += bHorz ? m_arRects[i].Width() : m_arRects[i].Height();
  return bHorz ? CSize(nLength, nDepth) : CSize(nDepth, nLength);
}

void CMFCAutoHideBar::OnPaint() {
  CPaintDC dc(this);
  CRect rectClient;
  GetClientRect(rectClient);
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  pVM->OnFillBarBackground(&dc, this, rectClient, rectClient);
  BOOL bHorz = IsHorizontal();
  CFont* pOld = dc.SelectObject(bHorz ? &afxGlobalData.fontRegular : &afxGlobalData.fontVert);
  dc.SetBkMode(TRANSPARENT);
  for (int i = 0; i < GetCount() && i < m_arRects.GetSize(); i++) {
    CDockablePane* pPane = m_arPanes[i];
    CRect r = m_arRects[i];
    if (pPane->IsSlidOut()) dc.FillSolidRect(r, pVM->GetHighlightColor());
    pVM->OnDrawAutoHideButtonBorder(&dc, r, CRect(1, 1, 1, 1), this);
    CString strTitle;
    pPane->GetWindowText(strTitle);
    dc.SetTextColor(afxGlobalData.clrBtnText);
    int nOffset = 6;
    HICON hIcon = pPane->GetIcon(FALSE);
    if (hIcon != NULL) {
      if (bHorz)
        ::DrawIconEx(dc.GetSafeHdc(), r.left + 4, r.top + (r.Height() - 16) / 2, hIcon, 16, 16, 0, NULL, DI_NORMAL);
      else
        ::DrawIconEx(dc.GetSafeHdc(), r.left + (r.Width() - 16) / 2, r.top + 4, hIcon, 16, 16, 0, NULL, DI_NORMAL);
      nOffset += 18;
    }
    if (bHorz) {
      dc.TextOut(r.left + nOffset, r.top + (r.Height() - afxGlobalData.GetTextHeight() + 2) / 2, strTitle);
    } else {
      // The vertical font runs down the button from its right side.
      dc.TextOut(r.right - (r.Width() - afxGlobalData.GetTextHeight() + 2) / 2, r.top + nOffset, strTitle);
    }
  }
  dc.SelectObject(pOld);
}

void CMFCAutoHideBar::OnLButtonDown(UINT nFlags, CPoint point) {
  CDockablePane* pPane = HitTest(point);
  if (pPane != NULL) {
    pPane->Slide(!pPane->IsSlidOut(), TRUE);
    if (pPane->IsSlidOut()) pPane->SetFocus();
    return;
  }
  CPane::OnLButtonDown(nFlags, point);
}
