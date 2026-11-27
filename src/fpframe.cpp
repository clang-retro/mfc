// fpframe.cpp - the Feature Pack's frames: CFrameWndEx and
// CMDIFrameWndEx (the docking manager's layout around the view or the MDI
// client, the menu bar in place of the window's menu, the panes' menu and
// their commands, user toolbars, full screen), the MDI tabs, and
// CMDIChildWndEx; and CMFCToolBarsCustomizeDialog.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>
#include <afxcommandmanager.h>
#include <afxglobals.h>
#include <afxframewndex.h>
#include <afxmdiframewndex.h>
#include <afxmdichildwndex.h>
#include <afxmenubar.h>
#include <afxtoolbar.h>
#include <afxautohidebar.h>
#include <afxtoolbarscustomizedialog.h>

enum { kMDITabsID = 0xE9F0 };

// The frame's own client area shows only where the bars' rows leave gaps:
// those take the bars' face, not the class background, around the children.
static void _AfxFillFrameGaps(CWnd* pFrame, CDC* pDC) {
  CRect rect;
  pFrame->GetClientRect(rect);
  int nSaved = pDC->SaveDC();
  for (HWND h = ::GetWindow(pFrame->m_hWnd, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT)) {
    if (!::IsWindowVisible(h)) continue;
    CRect rc;
    ::GetWindowRect(h, rc);
    pFrame->ScreenToClient(rc);
    pDC->ExcludeClipRect(rc);
  }
  pDC->FillRect(rect, &GetGlobalData()->brBarFace);
  pDC->RestoreDC(nSaved);
}

// The pane a command ID names, for the View menu's show and hide.
static CBasePane* _AfxPaneForCommand(CDockingManager& manager, UINT nID) {
  if (nID == 0 || nID == AFX_IDW_PANE_FIRST) return NULL;
  CBasePane* pPane = manager.FindPaneByID(nID);
  if (pPane == NULL || pPane->IsKindOf(RUNTIME_CLASS(CMFCAutoHideBar))) return NULL;
  return pPane;
}

// Pane commands: the check shows the pane is visible, the command shows
// or hides it.
static BOOL _AfxPaneCmdMsg(CFrameWnd* pFrame, CDockingManager& manager, UINT nID, int nCode, void* pExtra,
                           AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (nCode != CN_COMMAND && nCode != (int)CN_UPDATE_COMMAND_UI) return FALSE;
  CBasePane* pPane = _AfxPaneForCommand(manager, nID);
  if (pPane == NULL) return FALSE;
  if (nCode == (int)CN_UPDATE_COMMAND_UI) {
    CCmdUI* pCmdUI = (CCmdUI*)pExtra;
    if (pCmdUI == NULL) return FALSE;
    pCmdUI->Enable(TRUE);
    pCmdUI->SetCheck(pPane->IsVisible());
    return TRUE;
  }
  if (pHandlerInfo != NULL) {
    pHandlerInfo->pTarget = pFrame;
    pHandlerInfo->pmf = NULL;
    return TRUE;
  }
  pPane->ShowPane(!pPane->IsVisible(), FALSE, TRUE);
  return TRUE;
}

// The View menu's placeholder becomes the list of panes.
static void _AfxExpandPaneMenu(CDockingManager& manager, CMenu* pMenu, UINT nPlaceholderID, BOOL bToolbarsOnly) {
  if (pMenu == NULL || nPlaceholderID == 0) return;
  int nCount = pMenu->GetMenuItemCount();
  for (int i = 0; i < nCount; i++) {
    if (pMenu->GetMenuItemID(i) != nPlaceholderID) continue;
    CMenu menuPanes;
    if (!menuPanes.CreatePopupMenu()) return;
    manager.BuildPanesMenu(menuPanes, bToolbarsOnly);
    int nPanes = menuPanes.GetMenuItemCount();
    if (nPanes == 0) return;  // keep the placeholder until there are panes
    pMenu->DeleteMenu(i, MF_BYPOSITION);
    for (int j = 0; j < nPanes; j++) {
      UINT nID = menuPanes.GetMenuItemID(j);
      if (nID == 0) {
        pMenu->InsertMenu(i + j, MF_BYPOSITION | MF_SEPARATOR);
        continue;
      }
      CString strText;
      menuPanes.GetMenuString(j, strText, MF_BYPOSITION);
      pMenu->InsertMenu(i + j, MF_BYPOSITION | MF_STRING, nID, strText);
    }
    return;
  }
}

// A user toolbar, for AFX_WM_CREATETOOLBAR.
static CMFCToolBar* _AfxCreateUserToolbar(CFrameWnd* pFrame, CDockingManager& manager, CObList& lstUserToolbars,
                                          UINT uiFirst, UINT uiLast, LPCTSTR lpszName) {
  if (uiFirst == 0 || uiLast < uiFirst) return NULL;
  UINT nID = 0;
  for (UINT id = uiFirst; id <= uiLast; id++)
    if (manager.FindPaneByID(id) == NULL) {
      nID = id;
      break;
    }
  if (nID == 0) return NULL;
  CMFCToolBar* pBar = new CMFCToolBar;
  if (!pBar->CreateEx(pFrame, TBSTYLE_FLAT, AFX_DEFAULT_TOOLBAR_STYLE | CBRS_TOOLTIPS | CBRS_FLYBY, CRect(1, 1, 1, 1),
                      nID)) {
    delete pBar;
    return NULL;
  }
  pBar->SetUserDefined(TRUE);
  pBar->SetWindowText(lpszName != NULL && *lpszName ? lpszName : _T("Custom"));
  pBar->EnableDocking(CBRS_ALIGN_ANY);
  lstUserToolbars.AddTail(pBar);
  manager.DockPane(pBar);
  return pBar;
}

static void _AfxDeleteUserToolbars(CObList& lst) {
  while (!lst.IsEmpty()) {
    CMFCToolBar* pBar = (CMFCToolBar*)lst.RemoveHead();
    if (pBar->GetSafeHwnd() != NULL) pBar->DestroyWindow();
    delete pBar;
  }
}

static BOOL _AfxMenuBarKeys(CMFCMenuBar* pMenuBar, MSG* pMsg) {
  if (pMenuBar == NULL || pMenuBar->GetSafeHwnd() == NULL || !pMenuBar->IsWindowVisible()) return FALSE;
  // Alt with an item's letter, and F10, open the menus.
  if (pMsg->message == WM_SYSCHAR && (HIWORD(pMsg->lParam) & KF_ALTDOWN))
    return pMenuBar->TranslateChar((UINT)pMsg->wParam);
  if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_F10 && !(::GetKeyState(VK_SHIFT) & 0x8000) &&
      !(::GetKeyState(VK_CONTROL) & 0x8000))
    return pMenuBar->OpenItem(0);
  return FALSE;
}

// The frame's window menu moves to its menu bar.
static void _AfxMoveMenuToBar(CFrameWnd* pFrame, CMFCMenuBar* pMenuBar, HMENU hMenuDefault) {
  if (pMenuBar == NULL || pMenuBar->GetSafeHwnd() == NULL || pFrame->GetSafeHwnd() == NULL) return;
  HMENU hMenu = ::GetMenu(pFrame->m_hWnd);
  if (hMenu == NULL) return;
  pMenuBar->CreateFromMenu(hMenu, hMenu == hMenuDefault);
  ::SetMenu(pFrame->m_hWnd, NULL);
}

static void _AfxToggleFullScreen(CFrameWnd* pFrame, CDockingManager& manager, BOOL& bFullScreen, CRect& rectOld,
                                 BOOL bMainMenu) {
  if (!bFullScreen) {
    pFrame->GetWindowRect(rectOld);
    manager.ShowPanes(FALSE);
    CMFCMenuBar* pMenuBar = manager.GetMenuBar();
    if (bMainMenu && pMenuBar != NULL) pMenuBar->ShowPane(TRUE, FALSE, FALSE);
    // The client area covers the screen.
    CRect rect(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
    ::AdjustWindowRectEx(rect, pFrame->GetStyle(), FALSE, pFrame->GetExStyle());
    bFullScreen = TRUE;
    CDockingManager::m_bFullScreenMode = TRUE;
    pFrame->SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOZORDER);
  } else {
    bFullScreen = FALSE;
    CDockingManager::m_bFullScreenMode = FALSE;
    manager.ShowPanes(TRUE);
    pFrame->SetWindowPos(NULL, rectOld.left, rectOld.top, rectOld.Width(), rectOld.Height(), SWP_NOZORDER);
  }
  pFrame->RecalcLayout();
}

// ---------------------------------------------------------------------
// CFrameWndEx

IMPLEMENT_DYNCREATE(CFrameWndEx, CFrameWnd)

BOOL CFrameWndEx::OnEraseBkgnd(CDC* pDC) {
  _AfxFillFrameGaps(this, pDC);
  return TRUE;
}

BEGIN_MESSAGE_MAP(CFrameWndEx, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_ERASEBKGND()
  ON_WM_SIZE()
  ON_WM_CONTEXTMENU()
  ON_WM_DESTROY()
  ON_WM_INITMENUPOPUP()
  ON_REGISTERED_MESSAGE(AFX_WM_CREATETOOLBAR, OnToolbarCreateNew)
END_MESSAGE_MAP()

CFrameWndEx::CFrameWndEx()
    : m_uiFullScreenCmd(0), m_uiViewToolbarsMenuEntryID(0), m_uiUserToolbarFirst(0), m_uiUserToolbarLast(0),
      m_bFullScreen(FALSE), m_bFullScreenMainMenu(TRUE), m_bLoadDockState(TRUE), m_rectFullScreenOld(0, 0, 0, 0),
      m_bViewMenuShowsToolbarsOnly(FALSE) {}

CFrameWndEx::~CFrameWndEx() { _AfxDeleteUserToolbars(m_lstUserToolbars); }

int CFrameWndEx::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  m_dockManager.Create(this);
  return CFrameWnd::OnCreate(lpCreateStruct);
}

void CFrameWndEx::OnDestroy() {
  m_dockManager.LockUpdate(TRUE);
  CFrameWnd::OnDestroy();
}

BOOL CFrameWndEx::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd, CCreateContext* pContext) {
  if (!CFrameWnd::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext)) return FALSE;
  CMFCMenuBar* pMenuBar = GetMenuBar();
  if (pMenuBar != NULL && m_hMenuDefault == NULL) m_hMenuDefault = pMenuBar->GetDefaultMenu();
  _AfxMoveMenuToBar(this, pMenuBar, m_hMenuDefault);
  return TRUE;
}

BOOL CFrameWndEx::EnableDocking(DWORD dwDockStyle) { return m_dockManager.EnableDocking(dwDockStyle); }

BOOL CFrameWndEx::EnableAutoHidePanes(DWORD dwDockStyle) { return m_dockManager.EnableAutoHidePanes(dwDockStyle); }

void CFrameWndEx::DockPane(CBasePane* pBar, UINT nDockBarID, LPCRECT lpRect) {
  m_dockManager.DockPane(pBar, nDockBarID, lpRect);
}

BOOL CFrameWndEx::DockPaneLeftOf(CPane* pBar, CPane* pLeftOf) { return m_dockManager.DockPaneLeftOf(pBar, pLeftOf); }

BOOL CFrameWndEx::AddPane(CBasePane* pControlBar, BOOL bTail) { return m_dockManager.AddPane(pControlBar, bTail); }

BOOL CFrameWndEx::InsertPane(CBasePane* pControlBar, CBasePane* pTarget, BOOL bAfter) {
  return m_dockManager.InsertPane(pControlBar, pTarget, bAfter);
}

void CFrameWndEx::RemovePaneFromDockManager(CBasePane* pControlBar, BOOL bDestroy, BOOL bAdjustLayout,
                                            BOOL bAutoHide, CBasePane* pBarReplacement) {
  m_dockManager.RemovePaneFromDockManager(pControlBar, bDestroy, bAdjustLayout, bAutoHide, pBarReplacement);
}

void CFrameWndEx::ShowPane(CBasePane* pBar, BOOL bShow, BOOL bDelay, BOOL bActivate) {
  if (pBar != NULL) pBar->ShowPane(bShow, bDelay, bActivate);
}

CBasePane* CFrameWndEx::GetPane(UINT nID) { return m_dockManager.FindPaneByID(nID, TRUE); }

void CFrameWndEx::EnablePaneMenu(BOOL bEnable, UINT uiCustomizeCmd, const CString& strCustomizeLabel,
                                 UINT uiViewToolbarsMenuEntryID, BOOL bContextMenuShowsToolbarsOnly,
                                 BOOL bViewMenuShowsToolbarsOnly) {
  m_dockManager.EnablePaneContextMenu(bEnable, uiCustomizeCmd, strCustomizeLabel, bContextMenuShowsToolbarsOnly);
  m_uiViewToolbarsMenuEntryID = uiViewToolbarsMenuEntryID;
  m_bViewMenuShowsToolbarsOnly = bViewMenuShowsToolbarsOnly;
}

BOOL CFrameWndEx::InitUserToolbars(LPCTSTR, UINT uiUserToolbarFirst, UINT uiUserToolbarLast) {
  m_uiUserToolbarFirst = uiUserToolbarFirst;
  m_uiUserToolbarLast = uiUserToolbarLast;
  return uiUserToolbarLast >= uiUserToolbarFirst;
}

CMFCToolBar* CFrameWndEx::GetUserToolBarByIndex(int iIndex) const {
  POSITION pos = iIndex >= 0 ? m_lstUserToolbars.FindIndex(iIndex) : NULL;
  return pos != NULL ? (CMFCToolBar*)m_lstUserToolbars.GetAt(pos) : NULL;
}

void CFrameWndEx::ShowFullScreen() {
  _AfxToggleFullScreen(this, m_dockManager, m_bFullScreen, m_rectFullScreenOld, m_bFullScreenMainMenu);
}

void CFrameWndEx::RecalcLayout(BOOL) {
  if (m_bInRecalcLayout) return;
  m_bInRecalcLayout = TRUE;
  m_dockManager.AdjustDockingLayout();
  // The classic bars, then the view, in what the panes leave.
  CRect rect = m_dockManager.GetClientAreaBounds();
  if (m_dockManager.GetDockSiteFrameWnd() == NULL) GetClientRect(rect);
  RepositionBars(0, 0xFFFF, AFX_IDW_PANE_FIRST, reposDefault, NULL, &rect);
  m_bInRecalcLayout = FALSE;
#ifndef _AFX_WIN16
  if (m_pNotifyHook != NULL) m_pNotifyHook->OnRecalcLayout();
#endif
}

void CFrameWndEx::OnSize(UINT nType, int cx, int cy) { CFrameWnd::OnSize(nType, cx, cy); }

void CFrameWndEx::OnUpdateFrameMenu(HMENU hMenuAlt) {
  CFrameWnd::OnUpdateFrameMenu(hMenuAlt);
  _AfxMoveMenuToBar(this, GetMenuBar(), m_hMenuDefault);
}

BOOL CFrameWndEx::PreTranslateMessage(MSG* pMsg) {
  if (_AfxMenuBarKeys(GetMenuBar(), pMsg)) return TRUE;
  return CFrameWnd::PreTranslateMessage(pMsg);
}

BOOL CFrameWndEx::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (_AfxPaneCmdMsg(this, m_dockManager, nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void CFrameWndEx::OnUpdatePaneMenu(CCmdUI* pCmdUI) {
  CBasePane* pPane = _AfxPaneForCommand(m_dockManager, pCmdUI->m_nID);
  pCmdUI->SetCheck(pPane != NULL && pPane->IsVisible());
}

BOOL CFrameWndEx::OnPaneCheck(UINT nID) {
  CBasePane* pPane = _AfxPaneForCommand(m_dockManager, nID);
  if (pPane == NULL) return FALSE;
  pPane->ShowPane(!pPane->IsVisible(), FALSE, TRUE);
  return TRUE;
}

void CFrameWndEx::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu) {
  if (!bSysMenu)
    _AfxExpandPaneMenu(m_dockManager, pPopupMenu, m_uiViewToolbarsMenuEntryID, m_bViewMenuShowsToolbarsOnly);
  CFrameWnd::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
}

void CFrameWndEx::OnContextMenu(CWnd* pWnd, CPoint point) {
  if (pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CBasePane)) && m_dockManager.OnPaneContextMenu(point)) return;
  CFrameWnd::OnContextMenu(pWnd, point);
}

LRESULT CFrameWndEx::OnToolbarCreateNew(WPARAM, LPARAM lParam) {
  return (LRESULT)_AfxCreateUserToolbar(this, m_dockManager, m_lstUserToolbars, m_uiUserToolbarFirst,
                                        m_uiUserToolbarLast, (LPCTSTR)lParam);
}

void CFrameWndEx::OnIdleUpdateCmdUI() {
  CFrameWnd::OnIdleUpdateCmdUI();
  m_dockManager.SendIdleUpdateToMiniFrames();
}

BOOL CFrameWndEx::OnShowPanes(BOOL bShow) {
  m_dockManager.ShowPanes(bShow);
  return TRUE;
}

void CFrameWndEx::OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pState) {
  if (bPreview) m_dockManager.SetPrintPreviewMode(TRUE, pState);
  CFrameWnd::OnSetPreviewMode(bPreview, pState);
  if (!bPreview) m_dockManager.SetPrintPreviewMode(FALSE, pState);
  RecalcLayout();
}

// ---------------------------------------------------------------------
// The MDI tabs' stand-in windows: one for each child, its close goes to
// the child.

class _AfxMDITabPlaceholder : public CWnd {
public:
  HWND m_hWndChild;
  _AfxMDITabPlaceholder() : m_hWndChild(NULL) {}
  virtual void PostNcDestroy() { delete this; }
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
      if (::IsWindow(m_hWndChild)) ::PostMessage(m_hWndChild, WM_SYSCOMMAND, SC_CLOSE, 0);
      return 0;
    }
    return CWnd::WindowProc(message, wParam, lParam);
  }
};

// ---------------------------------------------------------------------
// CMDIFrameWndEx

IMPLEMENT_DYNCREATE(CMDIFrameWndEx, CMDIFrameWnd)

BOOL CMDIFrameWndEx::OnEraseBkgnd(CDC* pDC) {
  _AfxFillFrameGaps(this, pDC);
  return TRUE;
}

BEGIN_MESSAGE_MAP(CMDIFrameWndEx, CMDIFrameWnd)
  ON_WM_CREATE()
  ON_WM_ERASEBKGND()
  ON_WM_SIZE()
  ON_WM_CONTEXTMENU()
  ON_WM_DESTROY()
  ON_WM_INITMENUPOPUP()
  ON_REGISTERED_MESSAGE(AFX_WM_CREATETOOLBAR, OnToolbarCreateNew)
  ON_REGISTERED_MESSAGE(AFX_WM_CHANGE_ACTIVE_TAB, OnChangeActiveTab)
END_MESSAGE_MAP()

CMDIFrameWndEx::CMDIFrameWndEx()
    : m_bMDITabs(FALSE), m_bMDITabbedGroups(FALSE), m_bInTabUpdate(FALSE), m_bMDITabIcons(FALSE), m_uiFullScreenCmd(0),
      m_uiViewToolbarsMenuEntryID(0), m_uiUserToolbarFirst(0), m_uiUserToolbarLast(0), m_bFullScreen(FALSE),
      m_bFullScreenMainMenu(TRUE), m_bLoadDockState(TRUE), m_rectFullScreenOld(0, 0, 0, 0),
      m_bViewMenuShowsToolbarsOnly(FALSE) {}

CMDIFrameWndEx::~CMDIFrameWndEx() { _AfxDeleteUserToolbars(m_lstUserToolbars); }

int CMDIFrameWndEx::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  m_dockManager.Create(this);
  return CMDIFrameWnd::OnCreate(lpCreateStruct);
}

void CMDIFrameWndEx::OnDestroy() {
  m_dockManager.LockUpdate(TRUE);
  m_bInTabUpdate = TRUE;
  m_mapTabPlaceholders.RemoveAll();  // the windows go with the tabs
  CMDIFrameWnd::OnDestroy();
}

BOOL CMDIFrameWndEx::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                               CCreateContext* pContext) {
  if (!CMDIFrameWnd::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext)) return FALSE;
  CMFCMenuBar* pMenuBar = GetMenuBar();
  if (pMenuBar != NULL && m_hMenuDefault == NULL) m_hMenuDefault = pMenuBar->GetDefaultMenu();
  _AfxMoveMenuToBar(this, pMenuBar, m_hMenuDefault);
  return TRUE;
}

BOOL CMDIFrameWndEx::EnableDocking(DWORD dwDockStyle) { return m_dockManager.EnableDocking(dwDockStyle); }

BOOL CMDIFrameWndEx::EnableAutoHidePanes(DWORD dwDockStyle) {
  return m_dockManager.EnableAutoHidePanes(dwDockStyle);
}

void CMDIFrameWndEx::DockPane(CBasePane* pBar, UINT nDockBarID, LPCRECT lpRect) {
  m_dockManager.DockPane(pBar, nDockBarID, lpRect);
}

BOOL CMDIFrameWndEx::DockPaneLeftOf(CPane* pBar, CPane* pLeftOf) {
  return m_dockManager.DockPaneLeftOf(pBar, pLeftOf);
}

BOOL CMDIFrameWndEx::AddPane(CBasePane* pControlBar, BOOL bTail) { return m_dockManager.AddPane(pControlBar, bTail); }

BOOL CMDIFrameWndEx::InsertPane(CBasePane* pControlBar, CBasePane* pTarget, BOOL bAfter) {
  return m_dockManager.InsertPane(pControlBar, pTarget, bAfter);
}

void CMDIFrameWndEx::RemovePaneFromDockManager(CBasePane* pControlBar, BOOL bDestroy, BOOL bAdjustLayout,
                                               BOOL bAutoHide, CBasePane* pBarReplacement) {
  m_dockManager.RemovePaneFromDockManager(pControlBar, bDestroy, bAdjustLayout, bAutoHide, pBarReplacement);
}

void CMDIFrameWndEx::ShowPane(CBasePane* pBar, BOOL bShow, BOOL bDelay, BOOL bActivate) {
  if (pBar != NULL) pBar->ShowPane(bShow, bDelay, bActivate);
}

CBasePane* CMDIFrameWndEx::GetPane(UINT nID) { return m_dockManager.FindPaneByID(nID, TRUE); }

void CMDIFrameWndEx::EnablePaneMenu(BOOL bEnable, UINT uiCustomizeCmd, const CString& strCustomizeLabel,
                                    UINT uiViewToolbarsMenuEntryID, BOOL bContextMenuShowsToolbarsOnly,
                                    BOOL bViewMenuShowsToolbarsOnly) {
  m_dockManager.EnablePaneContextMenu(bEnable, uiCustomizeCmd, strCustomizeLabel, bContextMenuShowsToolbarsOnly);
  m_uiViewToolbarsMenuEntryID = uiViewToolbarsMenuEntryID;
  m_bViewMenuShowsToolbarsOnly = bViewMenuShowsToolbarsOnly;
}

BOOL CMDIFrameWndEx::InitUserToolbars(LPCTSTR, UINT uiUserToolbarFirst, UINT uiUserToolbarLast) {
  m_uiUserToolbarFirst = uiUserToolbarFirst;
  m_uiUserToolbarLast = uiUserToolbarLast;
  return uiUserToolbarLast >= uiUserToolbarFirst;
}

CMFCToolBar* CMDIFrameWndEx::GetUserToolBarByIndex(int iIndex) const {
  POSITION pos = iIndex >= 0 ? m_lstUserToolbars.FindIndex(iIndex) : NULL;
  return pos != NULL ? (CMFCToolBar*)m_lstUserToolbars.GetAt(pos) : NULL;
}

void CMDIFrameWndEx::ShowFullScreen() {
  _AfxToggleFullScreen(this, m_dockManager, m_bFullScreen, m_rectFullScreenOld, m_bFullScreenMainMenu);
}

void CMDIFrameWndEx::EnableMDITabs(BOOL bEnable, BOOL bIcons, CMFCTabCtrl::Location tabLocation,
                                   BOOL bTabCloseButton, CMFCTabCtrl::Style style, BOOL,
                                   BOOL bActiveTabCloseButton) {
  m_bMDITabs = bEnable;
  if (bEnable && m_wndMDITabs.GetSafeHwnd() == NULL && m_hWnd != NULL) {
    if (!m_wndMDITabs.Create(style, CRect(0, 0, 0, 0), this, kMDITabsID, tabLocation,
                             bTabCloseButton || bActiveTabCloseButton)) {
      m_bMDITabs = FALSE;
      return;
    }
    m_wndMDITabs.SetDrawFrame(FALSE);
    m_wndMDITabs.AutoDestroyWindow(TRUE);
    m_wndMDITabs.SetFlatFrame(TRUE, FALSE);
  } else if (m_wndMDITabs.GetSafeHwnd() != NULL) {
    m_wndMDITabs.SetLocation(tabLocation);
    m_wndMDITabs.ModifyTabStyle(style);
    m_wndMDITabs.EnableActiveTabCloseButton(bTabCloseButton || bActiveTabCloseButton);
  }
  m_bMDITabIcons = bIcons;
  if (!bEnable && m_wndMDITabs.GetSafeHwnd() != NULL) m_wndMDITabs.ShowWindow(SW_HIDE);
  _UpdateMDITabs();
  if (m_hWnd != NULL) RecalcLayout();
}

void CMDIFrameWndEx::EnableMDITabbedGroups(BOOL bEnable, const CMDITabInfo& params) {
  m_bMDITabbedGroups = bEnable;
  EnableMDITabs(bEnable, params.m_bTabIcons, params.m_tabLocation, params.m_bTabCloseButton, params.m_style,
                params.m_bTabCustomTooltips, params.m_bActiveTabCloseButton);
  if (m_wndMDITabs.GetSafeHwnd() != NULL) {
    m_wndMDITabs.EnableAutoColor(params.m_bAutoColor);
    m_wndMDITabs.EnableTabSwap(params.m_bEnableTabSwap);
    m_wndMDITabs.SetTabBorderSize(params.m_nTabBorderSize, FALSE);
    m_wndMDITabs.SetFlatFrame(params.m_bFlatFrame, FALSE);
  }
}

BOOL CMDIFrameWndEx::AreMDITabs(int* pnMDITabsType) const {
  if (pnMDITabsType != NULL) *pnMDITabsType = !m_bMDITabs ? 0 : m_bMDITabbedGroups ? 2 : 1;
  return m_bMDITabs;
}

void CMDIFrameWndEx::_UpdateMDITabs() {
  if (!m_bMDITabs || m_bInTabUpdate || m_wndMDITabs.GetSafeHwnd() == NULL || m_hWndMDIClient == NULL) return;
  m_bInTabUpdate = TRUE;
  int nTabsBefore = m_wndMDITabs.GetTabsNum();

  // The children shown on tabs.
  CArray<HWND, HWND> arChildren;
  for (HWND h = ::GetWindow(m_hWndMDIClient, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT)) {
    if (!::IsWindowVisible(h)) continue;
    CMDIChildWndEx* pChildEx = DYNAMIC_DOWNCAST(CMDIChildWndEx, CWnd::FromHandlePermanent(h));
    if (pChildEx != NULL && !pChildEx->CanShowOnMDITabs()) continue;
    arChildren.Add(h);
  }
  // The tabs of the children that went.
  for (int i = m_wndMDITabs.GetTabsNum() - 1; i >= 0; i--) {
    _AfxMDITabPlaceholder* pPlace = (_AfxMDITabPlaceholder*)m_wndMDITabs.GetTabWnd(i);
    BOOL bKeep = FALSE;
    for (int j = 0; j < arChildren.GetSize() && !bKeep; j++) bKeep = arChildren[j] == pPlace->m_hWndChild;
    if (bKeep) continue;
    m_mapTabPlaceholders.RemoveKey(pPlace->m_hWndChild);
    m_wndMDITabs.RemoveTab(i, FALSE);
    if (pPlace->GetSafeHwnd() != NULL) pPlace->DestroyWindow();
  }
  // New ones at the end, in the order the children came.
  for (int j = (int)arChildren.GetSize() - 1; j >= 0; j--) {
    CWnd* pPlace;
    if (m_mapTabPlaceholders.Lookup(arChildren[j], pPlace)) continue;
    _AfxMDITabPlaceholder* pNew = new _AfxMDITabPlaceholder;
    pNew->m_hWndChild = arChildren[j];
    if (!pNew->CreateEx(0, AfxRegisterWndClass(0), NULL, WS_CHILD, CRect(0, 0, 0, 0), &m_wndMDITabs, 0)) {
      delete pNew;
      continue;
    }
    m_mapTabPlaceholders.SetAt(arChildren[j], pNew);
    m_wndMDITabs.AddTab(pNew, _T(""), (UINT)-1, FALSE);
  }
  // Labels, icons, and the active child's tab.
  HWND hActive = (HWND)::SendMessage(m_hWndMDIClient, WM_MDIGETACTIVE, 0, 0);
  for (int i = 0; i < m_wndMDITabs.GetTabsNum(); i++) {
    _AfxMDITabPlaceholder* pPlace = (_AfxMDITabPlaceholder*)m_wndMDITabs.GetTabWnd(i);
    TCHAR szTitle[256];
    ::GetWindowText(pPlace->m_hWndChild, szTitle, 256);
    CString strOld;
    m_wndMDITabs.GetTabLabel(i, strOld);
    if (strOld != szTitle) m_wndMDITabs.SetTabLabel(i, szTitle);
    if (m_bMDITabIcons) {
      CMDIChildWndEx* pChildEx = DYNAMIC_DOWNCAST(CMDIChildWndEx, CWnd::FromHandlePermanent(pPlace->m_hWndChild));
      HICON hIcon = pChildEx != NULL ? pChildEx->GetFrameIcon() : NULL;
      if (hIcon != m_wndMDITabs.GetTabHicon(i)) m_wndMDITabs.SetTabHicon(i, hIcon);
    }
    if (pPlace->m_hWndChild == hActive && m_wndMDITabs.GetActiveTab() != i) m_wndMDITabs.SetActiveTab(i);
  }
  // On tabs, the active child fills the client area.
  if (hActive != NULL && !::IsZoomed(hActive) && ::IsWindowVisible(hActive))
    ::SendMessage(m_hWndMDIClient, WM_MDIMAXIMIZE, (WPARAM)hActive, 0);
  m_bInTabUpdate = FALSE;
  if ((nTabsBefore == 0) != (m_wndMDITabs.GetTabsNum() == 0)) RecalcLayout();
}

LRESULT CMDIFrameWndEx::OnChangeActiveTab(WPARAM wParam, LPARAM lParam) {
  if ((CWnd*)lParam != &m_wndMDITabs || m_bInTabUpdate) return 0;
  _AfxMDITabPlaceholder* pPlace = (_AfxMDITabPlaceholder*)m_wndMDITabs.GetTabWnd((int)wParam);
  if (pPlace != NULL && ::IsWindow(pPlace->m_hWndChild))
    ::SendMessage(m_hWndMDIClient, WM_MDIACTIVATE, (WPARAM)pPlace->m_hWndChild, 0);
  return 0;
}

void CMDIFrameWndEx::RecalcLayout(BOOL) {
  if (m_bInRecalcLayout) return;
  m_bInRecalcLayout = TRUE;
  m_dockManager.AdjustDockingLayout();
  CRect rect = m_dockManager.GetClientAreaBounds();
  if (m_dockManager.GetDockSiteFrameWnd() == NULL) GetClientRect(rect);
  // The MDI tabs above (or below) the client.
  if (m_wndMDITabs.GetSafeHwnd() != NULL) {
    if (m_bMDITabs && m_wndMDITabs.GetTabsNum() > 0) {
      int cy = m_wndMDITabs.GetTabsHeight() + 2;
      if (cy > rect.Height()) cy = rect.Height();
      CRect r = rect;
      if (m_wndMDITabs.GetLocation() == CMFCTabCtrl::LOCATION_BOTTOM)
        r.top = rect.bottom -= cy;
      else
        r.bottom = rect.top += cy;
      m_wndMDITabs.SetWindowPos(NULL, r.left, r.top, r.Width(), r.Height(),
                                SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (m_wndMDITabs.IsWindowVisible()) {
      m_wndMDITabs.ShowWindow(SW_HIDE);
    }
  }
  RepositionBars(0, 0xFFFF, AFX_IDW_PANE_FIRST, reposDefault, NULL, &rect);
  m_bInRecalcLayout = FALSE;
#ifndef _AFX_WIN16
  if (m_pNotifyHook != NULL) m_pNotifyHook->OnRecalcLayout();
#endif
}

void CMDIFrameWndEx::OnSize(UINT nType, int cx, int cy) { CMDIFrameWnd::OnSize(nType, cx, cy); }

void CMDIFrameWndEx::OnUpdateFrameMenu(HMENU hMenuAlt) {
  CMDIFrameWnd::OnUpdateFrameMenu(hMenuAlt);
  _OnChildMenuChanged();
}

void CMDIFrameWndEx::_OnChildMenuChanged() { _AfxMoveMenuToBar(this, GetMenuBar(), m_hMenuDefault); }

BOOL CMDIFrameWndEx::PreTranslateMessage(MSG* pMsg) {
  if (_AfxMenuBarKeys(GetMenuBar(), pMsg)) return TRUE;
  return CMDIFrameWnd::PreTranslateMessage(pMsg);
}

BOOL CMDIFrameWndEx::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (_AfxPaneCmdMsg(this, m_dockManager, nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  return CMDIFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void CMDIFrameWndEx::OnUpdatePaneMenu(CCmdUI* pCmdUI) {
  CBasePane* pPane = _AfxPaneForCommand(m_dockManager, pCmdUI->m_nID);
  pCmdUI->SetCheck(pPane != NULL && pPane->IsVisible());
}

BOOL CMDIFrameWndEx::OnPaneCheck(UINT nID) {
  CBasePane* pPane = _AfxPaneForCommand(m_dockManager, nID);
  if (pPane == NULL) return FALSE;
  pPane->ShowPane(!pPane->IsVisible(), FALSE, TRUE);
  return TRUE;
}

void CMDIFrameWndEx::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu) {
  if (!bSysMenu)
    _AfxExpandPaneMenu(m_dockManager, pPopupMenu, m_uiViewToolbarsMenuEntryID, m_bViewMenuShowsToolbarsOnly);
  CMDIFrameWnd::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
}

void CMDIFrameWndEx::OnContextMenu(CWnd* pWnd, CPoint point) {
  if (pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CBasePane)) && m_dockManager.OnPaneContextMenu(point)) return;
  CMDIFrameWnd::OnContextMenu(pWnd, point);
}

LRESULT CMDIFrameWndEx::OnToolbarCreateNew(WPARAM, LPARAM lParam) {
  return (LRESULT)_AfxCreateUserToolbar(this, m_dockManager, m_lstUserToolbars, m_uiUserToolbarFirst,
                                        m_uiUserToolbarLast, (LPCTSTR)lParam);
}

void CMDIFrameWndEx::OnIdleUpdateCmdUI() {
  CMDIFrameWnd::OnIdleUpdateCmdUI();
  m_dockManager.SendIdleUpdateToMiniFrames();
  _UpdateMDITabs();
}

BOOL CMDIFrameWndEx::OnShowPanes(BOOL bShow) {
  m_dockManager.ShowPanes(bShow);
  return TRUE;
}

void CMDIFrameWndEx::OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pState) {
  if (bPreview) m_dockManager.SetPrintPreviewMode(TRUE, pState);
  CMDIFrameWnd::OnSetPreviewMode(bPreview, pState);
  if (!bPreview) m_dockManager.SetPrintPreviewMode(FALSE, pState);
  RecalcLayout();
}

// ---------------------------------------------------------------------
// CMDIChildWndEx

IMPLEMENT_DYNCREATE(CMDIChildWndEx, CMDIChildWnd)

BEGIN_MESSAGE_MAP(CMDIChildWndEx, CMDIChildWnd)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_MDIACTIVATE()
  ON_MESSAGE(WM_SETTEXT, OnSetText)
END_MESSAGE_MAP()

CMDIChildWndEx::CMDIChildWndEx() {}

CMDIChildWndEx::~CMDIChildWndEx() {}

CMDIFrameWndEx* CMDIChildWndEx::GetMDIFrameWndEx() const {
  return DYNAMIC_DOWNCAST(CMDIFrameWndEx, const_cast<CMDIChildWndEx*>(this)->GetMDIFrame());
}

HICON CMDIChildWndEx::GetFrameIcon() const {
  HICON hIcon = (HICON)::SendMessage(m_hWnd, WM_GETICON, ICON_SMALL, 0);
  if (hIcon == NULL) hIcon = (HICON)::SendMessage(m_hWnd, WM_GETICON, ICON_BIG, 0);
  if (hIcon == NULL) hIcon = (HICON)::GetClassLongPtr(m_hWnd, GCLP_HICONSM);
  if (hIcon == NULL) hIcon = (HICON)::GetClassLongPtr(m_hWnd, GCLP_HICON);
  return hIcon;
}

int CMDIChildWndEx::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CMDIChildWnd::OnCreate(lpCreateStruct) == -1) return -1;
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL) pFrame->_UpdateMDITabs();
  return 0;
}

void CMDIChildWndEx::OnDestroy() {
  CMDIChildWnd::OnDestroy();
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL) {
    // Hidden by now, so its tab goes.
    ShowWindow(SW_HIDE);
    pFrame->_UpdateMDITabs();
  }
}

void CMDIChildWndEx::OnMDIActivate(BOOL bActivate, CWnd* pActivateWnd, CWnd* pDeactivateWnd) {
  CMDIChildWnd::OnMDIActivate(bActivate, pActivateWnd, pDeactivateWnd);
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL && bActivate) pFrame->_UpdateMDITabs();
}

LRESULT CMDIChildWndEx::OnSetText(WPARAM, LPARAM) {
  LRESULT lResult = Default();
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL) pFrame->_UpdateMDITabs();
  return lResult;
}

void CMDIChildWndEx::OnUpdateFrameTitle(BOOL bAddToTitle) {
  CMDIChildWnd::OnUpdateFrameTitle(bAddToTitle);
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL) pFrame->_UpdateMDITabs();
}

void CMDIChildWndEx::OnUpdateFrameMenu(BOOL bActive, CWnd* pActivateWnd, HMENU hMenuAlt) {
  CMDIChildWnd::OnUpdateFrameMenu(bActive, pActivateWnd, hMenuAlt);
  CMDIFrameWndEx* pFrame = GetMDIFrameWndEx();
  if (pFrame != NULL) pFrame->_OnChildMenuChanged();
}

// ---------------------------------------------------------------------
// CMFCToolBarsCustomizeDialog

IMPLEMENT_DYNAMIC(CMFCToolBarsCustomizeDialog, CDialog)

enum { kListID = 100, kNewID = 101, kRenameID = 102, kDeleteID = 103, kResetID = 104, kLabelsID = 105 };

CMFCToolBarsCustomizeDialog::CMFCToolBarsCustomizeDialog(CFrameWnd* pWndParentFrame, BOOL bAutoSetFromMenus,
                                                         UINT uiFlags, CList<CRuntimeClass*, CRuntimeClass*>*)
    : m_pParentFrame(pWndParentFrame), m_bAutoSetFromMenus(bAutoSetFromMenus), m_bUserDefinedToolbars(FALSE),
      m_uiFlags(uiFlags), m_pList(NULL) {}

CMFCToolBarsCustomizeDialog::~CMFCToolBarsCustomizeDialog() {
  for (POSITION pos = m_mapButtonsByCategory.GetStartPosition(); pos != NULL;) {
    CString strKey;
    CObject* pObj;
    m_mapButtonsByCategory.GetNextAssoc(pos, strKey, pObj);
    CObList* pList = (CObList*)pObj;
    while (!pList->IsEmpty()) delete pList->RemoveHead();
    delete pList;
  }
  delete m_pList;
}

BOOL CMFCToolBarsCustomizeDialog::Create() {
  CAfxDlgTemplate dt(DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER, 236, 150, L"Customize");
  dt.AddItem(0x82, SS_LEFT, 7, 7, 120, 8, (WORD)-1, L"Tool&bars:");
  dt.AddItem(0x83,
             LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER | WS_TABSTOP | LBS_NOINTEGRALHEIGHT,
             7, 18, 150, 106, kListID, L"");
  dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, 164, 18, 65, 14, kNewID, L"&New...");
  dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, 164, 36, 65, 14, kRenameID, L"Rena&me...");
  dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, 164, 54, 65, 14, kDeleteID, L"&Delete");
  dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, 164, 72, 65, 14, kResetID, L"&Reset");
  dt.AddItem(0x80, BS_AUTOCHECKBOX | WS_TABSTOP, 164, 92, 68, 10, kLabelsID, L"&Text labels");
  dt.AddItem(0x80, BS_DEFPUSHBUTTON | WS_TABSTOP, 164, 129, 65, 14, IDCANCEL, L"Close");
  m_template.SetSize(dt.GetSize());
  memcpy(m_template.GetData(), dt.GetTemplate(), (size_t)dt.GetSize());
  return CreateIndirect((LPCDLGTEMPLATE)m_template.GetData(), m_pParentFrame);
}

BOOL CMFCToolBarsCustomizeDialog::OnInitDialog() {
  CDialog::OnInitDialog();
  m_pList = new CCheckListBox;
  m_pList->SubclassDlgItem(kListID, this);
  if (!m_bUserDefinedToolbars) {
    GetDlgItem(kNewID)->EnableWindow(FALSE);
    GetDlgItem(kRenameID)->EnableWindow(FALSE);
  }
  if (!(m_uiFlags & AFX_CUSTOMIZE_TEXT_LABELS)) GetDlgItem(kLabelsID)->EnableWindow(FALSE);
  _FillList();
  return TRUE;
}

void CMFCToolBarsCustomizeDialog::_FillList() {
  if (m_pList == NULL) return;
  int iSel = m_pList->GetCurSel();
  m_pList->ResetContent();
  m_lstShownToolBars.RemoveAll();
  CDockingManager* pManager = CBasePane::GetDockingManagerOf(m_pParentFrame);
  if (pManager != NULL) {
    CObList lst;
    pManager->GetPaneList(lst, FALSE, RUNTIME_CLASS(CMFCToolBar), TRUE);
    for (POSITION pos = lst.GetHeadPosition(); pos != NULL;) {
      CMFCToolBar* pBar = (CMFCToolBar*)lst.GetNext(pos);
      if (!pBar->AllowShowOnList() && !pBar->IsMenuBar()) continue;
      CString strName;
      pBar->GetWindowText(strName);
      if (strName.IsEmpty()) continue;
      int i = m_pList->AddString(strName);
      m_pList->SetCheck(i, pBar->IsVisible() ? 1 : 0);
      if (pBar->IsMenuBar()) m_pList->Enable(i, FALSE);
      m_lstShownToolBars.AddTail(pBar);
    }
  }
  if (iSel < 0 && m_pList->GetCount() > 0) iSel = 0;
  if (iSel >= m_pList->GetCount()) iSel = m_pList->GetCount() - 1;
  m_pList->SetCurSel(iSel);
  CMFCToolBar* pBar = _SelectedToolBar();
  if (GetDlgItem(kDeleteID) != NULL) GetDlgItem(kDeleteID)->EnableWindow(pBar != NULL && pBar->IsUserDefined());
  if (GetDlgItem(kRenameID) != NULL)
    GetDlgItem(kRenameID)->EnableWindow(m_bUserDefinedToolbars && pBar != NULL && pBar->IsUserDefined());
  if (GetDlgItem(kResetID) != NULL) GetDlgItem(kResetID)->EnableWindow(pBar != NULL && !pBar->IsUserDefined());
  CheckDlgButton(kLabelsID, pBar != NULL && pBar->AreTextLabels());
}

CMFCToolBar* CMFCToolBarsCustomizeDialog::_SelectedToolBar() const {
  if (m_pList == NULL) return NULL;
  int i = m_pList->GetCurSel();
  POSITION pos = i >= 0 ? m_lstShownToolBars.FindIndex(i) : NULL;
  return pos != NULL ? (CMFCToolBar*)m_lstShownToolBars.GetAt(pos) : NULL;
}

// A one-line text prompt, for a toolbar's name.
static BOOL _AfxAskName(CWnd* pParent, LPCWSTR lpszCaption, CString& strName) {
  CAfxDlgTemplate dt(DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER, 180, 58, lpszCaption);
  dt.AddItem(0x82, SS_LEFT, 7, 7, 166, 8, (WORD)-1, L"Toolbar &name:");
  dt.AddItem(0x81, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 18, 166, 13, 100, L"");
  dt.AddItem(0x80, BS_DEFPUSHBUTTON | WS_TABSTOP, 69, 38, 50, 14, IDOK, L"OK");
  dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, 123, 38, 50, 14, IDCANCEL, L"Cancel");
  class CNameDlg : public CDialog {
  public:
    CString m_str;
    virtual BOOL OnInitDialog() {
      CDialog::OnInitDialog();
      SetDlgItemText(100, m_str);
      return TRUE;
    }
    virtual void OnOK() {
      GetDlgItemText(100, m_str);
      CDialog::OnOK();
    }
  } dlg;
  dlg.m_str = strName;
  if (!dlg.InitModalIndirect(dt.GetTemplate(), pParent) || dlg.DoModal() != IDOK) return FALSE;
  strName = dlg.m_str;
  strName.TrimLeft();
  strName.TrimRight();
  return !strName.IsEmpty();
}

BOOL CMFCToolBarsCustomizeDialog::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam);
  UINT nCode = HIWORD(wParam);
  CMFCToolBar* pBar = _SelectedToolBar();
  switch (nID) {
    case kListID:
      if (nCode == CLBN_CHKCHANGE && m_pList != NULL) {
        int i = m_pList->GetCurSel();
        POSITION pos = i >= 0 ? m_lstShownToolBars.FindIndex(i) : NULL;
        if (pos != NULL) {
          CMFCToolBar* p = (CMFCToolBar*)m_lstShownToolBars.GetAt(pos);
          p->ShowPane(m_pList->GetCheck(i) != 0, FALSE, FALSE);
        }
      } else if (nCode == LBN_SELCHANGE) {
        _FillList();
      }
      return TRUE;
    case kNewID: {
      CString strName = _T("Custom 1");
      if (m_pParentFrame != NULL && _AfxAskName(this, L"New Toolbar", strName)) {
        m_pParentFrame->SendMessage(AFX_WM_CREATETOOLBAR, 0, (LPARAM)(LPCTSTR)strName);
        _FillList();
      }
      return TRUE;
    }
    case kRenameID:
      if (pBar != NULL && pBar->IsUserDefined()) {
        CString strName;
        pBar->GetWindowText(strName);
        if (_AfxAskName(this, L"Rename Toolbar", strName)) {
          pBar->SetWindowText(strName);
          _FillList();
        }
      }
      return TRUE;
    case kDeleteID:
      if (pBar != NULL && pBar->IsUserDefined() && m_pParentFrame != NULL) {
        // The frame may say no; otherwise the toolbar goes.
        if (m_pParentFrame->SendMessage(AFX_WM_DELETETOOLBAR, 0, (LPARAM)pBar) == 0) {
          CDockingManager* pManager = CBasePane::GetDockingManagerOf(m_pParentFrame);
          pBar->ShowPane(FALSE, FALSE, FALSE);
          if (pManager != NULL) pManager->RemovePaneFromDockManager(pBar, FALSE, TRUE, FALSE, NULL);
        }
        _FillList();
      }
      return TRUE;
    case kResetID:
      if (pBar != NULL && m_pParentFrame != NULL) {
        UINT uiResID = pBar->GetDlgCtrlID();
        m_pParentFrame->SendMessage(AFX_WM_RESETTOOLBAR, uiResID, 0);
      }
      return TRUE;
    case kLabelsID:
      if (pBar != NULL) pBar->EnableTextLabels(IsDlgButtonChecked(kLabelsID));
      return TRUE;
  }
  return CDialog::OnCommand(wParam, lParam);
}

void CMFCToolBarsCustomizeDialog::OnOK() { DestroyWindow(); }

void CMFCToolBarsCustomizeDialog::OnCancel() { DestroyWindow(); }

void CMFCToolBarsCustomizeDialog::PostNcDestroy() { delete this; }

CObList* CMFCToolBarsCustomizeDialog::_Category(LPCTSTR lpszCategory, BOOL bCreate) {
  CObject* pObj = NULL;
  if (m_mapButtonsByCategory.Lookup(lpszCategory, pObj)) return (CObList*)pObj;
  if (!bCreate) return NULL;
  CObList* pList = new CObList;
  m_mapButtonsByCategory.SetAt(lpszCategory, pList);
  m_lstCategories.AddTail(lpszCategory);
  return pList;
}

void CMFCToolBarsCustomizeDialog::AddButton(LPCTSTR lpszCategory, const CMFCToolBarButton& button, int iInsertAfter) {
  CObList* pList = _Category(lpszCategory, TRUE);
  CMFCToolBarButton* pCopy = DYNAMIC_DOWNCAST(CMFCToolBarButton, button.GetRuntimeClass()->CreateObject());
  if (pCopy == NULL) return;
  pCopy->CopyFrom(button);
  POSITION pos = iInsertAfter >= 0 ? pList->FindIndex(iInsertAfter) : NULL;
  if (pos != NULL)
    pList->InsertAfter(pos, pCopy);
  else
    pList->AddTail(pCopy);
}

void CMFCToolBarsCustomizeDialog::AddButton(UINT uiCategoryId, const CMFCToolBarButton& button, int iInsertAfter) {
  CString strCategory;
  strCategory.LoadString(uiCategoryId);
  AddButton(strCategory, button, iInsertAfter);
}

int CMFCToolBarsCustomizeDialog::RemoveButton(LPCTSTR lpszCategory, UINT uiCmdId) {
  int nCount = 0;
  for (POSITION posCat = m_lstCategories.GetHeadPosition(); posCat != NULL;) {
    CString strCategory = m_lstCategories.GetNext(posCat);
    if (lpszCategory != NULL && strCategory != lpszCategory) continue;
    CObList* pList = _Category(strCategory, FALSE);
    for (POSITION pos = pList != NULL ? pList->GetHeadPosition() : NULL; pos != NULL;) {
      POSITION posThis = pos;
      CMFCToolBarButton* p = (CMFCToolBarButton*)pList->GetNext(pos);
      if (p->m_nID != uiCmdId) continue;
      pList->RemoveAt(posThis);
      delete p;
      nCount++;
    }
  }
  return nCount;
}

int CMFCToolBarsCustomizeDialog::RemoveButton(UINT uiCategoryId, UINT uiCmdId) {
  CString strCategory;
  strCategory.LoadString(uiCategoryId);
  return RemoveButton(strCategory, uiCmdId);
}

void CMFCToolBarsCustomizeDialog::_AddMenuItems(HMENU hMenu, LPCTSTR lpszCategory) {
  int n = ::GetMenuItemCount(hMenu);
  for (int i = 0; i < n; i++) {
    HMENU hSub = ::GetSubMenu(hMenu, i);
    TCHAR szText[256];
    szText[0] = 0;
    ::GetMenuString(hMenu, i, szText, 256, MF_BYPOSITION);
    if (hSub != NULL) {
      // The top items name the categories.
      CString strCategory = lpszCategory != NULL ? CString(lpszCategory) : CString(szText);
      strCategory.Remove(_T('&'));
      _AddMenuItems(hSub, strCategory);
      continue;
    }
    UINT nID = ::GetMenuItemID(hMenu, i);
    if (nID == 0 || nID == (UINT)-1 || lpszCategory == NULL) continue;
    CString strText = szText;
    int iTab = strText.Find(_T('\t'));
    if (iTab >= 0) strText = strText.Left(iTab);
    strText.Remove(_T('&'));
    AddButton(lpszCategory, CMFCToolBarButton(nID, GetCmdMgr()->GetCmdImage(nID, FALSE), strText));
  }
}

BOOL CMFCToolBarsCustomizeDialog::AddMenu(UINT uiMenuResId) {
  CMenu menu;
  if (!menu.LoadMenu(uiMenuResId)) return FALSE;
  _AddMenuItems(menu.GetSafeHmenu(), NULL);
  return TRUE;
}

void CMFCToolBarsCustomizeDialog::AddMenuCommands(const CMenu* pMenu, BOOL, LPCTSTR lpszCategory, LPCTSTR) {
  if (pMenu != NULL) _AddMenuItems(pMenu->GetSafeHmenu(), lpszCategory);
}

BOOL CMFCToolBarsCustomizeDialog::AddToolBar(LPCTSTR lpszCategory, UINT uiToolbarResId) {
  LPCTSTR lpszName = MAKEINTRESOURCE(uiToolbarResId);
  HINSTANCE hInst = AfxFindResourceHandle(lpszName, RT_TOOLBAR);
  HRSRC hRsrc = ::FindResource(hInst, lpszName, RT_TOOLBAR);
  HGLOBAL hGlobal = hRsrc != NULL ? ::LoadResource(hInst, hRsrc) : NULL;
  const WORD* pData = hGlobal != NULL ? (const WORD*)::LockResource(hGlobal) : NULL;
  if (pData == NULL || pData[0] != 1) return FALSE;
  for (int i = 0; i < pData[3]; i++) {
    UINT nID = pData[4 + i];
    if (nID != 0) AddButton(lpszCategory, CMFCToolBarButton(nID, GetCmdMgr()->GetCmdImage(nID, FALSE)));
  }
  return TRUE;
}

BOOL CMFCToolBarsCustomizeDialog::AddToolBar(UINT uiCategoryId, UINT uiToolbarResId) {
  CString strCategory;
  strCategory.LoadString(uiCategoryId);
  return AddToolBar(strCategory, uiToolbarResId);
}

BOOL CMFCToolBarsCustomizeDialog::RenameCategory(LPCTSTR lpszCategoryOld, LPCTSTR lpszCategoryNew) {
  CObject* pObj;
  if (!m_mapButtonsByCategory.Lookup(lpszCategoryOld, pObj)) return FALSE;
  m_mapButtonsByCategory.RemoveKey(lpszCategoryOld);
  m_mapButtonsByCategory.SetAt(lpszCategoryNew, pObj);
  POSITION pos = m_lstCategories.Find(lpszCategoryOld);
  if (pos != NULL) m_lstCategories.SetAt(pos, lpszCategoryNew);
  return TRUE;
}

int CMFCToolBarsCustomizeDialog::ReplaceButton(UINT uiCmd, const CMFCToolBarButton& button) {
  int nCount = 0;
  for (POSITION posCat = m_lstCategories.GetHeadPosition(); posCat != NULL;) {
    CObList* pList = _Category(m_lstCategories.GetNext(posCat), FALSE);
    for (POSITION pos = pList != NULL ? pList->GetHeadPosition() : NULL; pos != NULL;) {
      POSITION posThis = pos;
      CMFCToolBarButton* p = (CMFCToolBarButton*)pList->GetNext(pos);
      if (p->m_nID != uiCmd) continue;
      CMFCToolBarButton* pNew = DYNAMIC_DOWNCAST(CMFCToolBarButton, button.GetRuntimeClass()->CreateObject());
      if (pNew == NULL) continue;
      pNew->CopyFrom(button);
      pList->SetAt(posThis, pNew);
      delete p;
      nCount++;
    }
  }
  return nCount;
}

BOOL CMFCToolBarsCustomizeDialog::SetUserCategory(LPCTSTR lpszCategory) {
  if (_Category(lpszCategory, FALSE) == NULL) return FALSE;
  m_strUserCategory = lpszCategory;
  return TRUE;
}

void CMFCToolBarsCustomizeDialog::GetCategoriesList(CStringList& lstCategories) const {
  lstCategories.RemoveAll();
  lstCategories.AddTail(const_cast<CStringList*>(&m_lstCategories));
}

int CMFCToolBarsCustomizeDialog::GetCountInCategory(LPCTSTR lpszItemName, const CObList& lstCommands) const {
  int nCount = 0;
  for (POSITION pos = lstCommands.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)lstCommands.GetNext(pos);
    if (p->m_strText == lpszItemName) nCount++;
  }
  return nCount;
}

BOOL CMFCToolBarsCustomizeDialog::GetCommandName(UINT uiCmd, CString& strCommandName) const {
  for (POSITION posCat = m_lstCategories.GetHeadPosition(); posCat != NULL;) {
    CObject* pObj;
    if (!m_mapButtonsByCategory.Lookup(m_lstCategories.GetNext(posCat), pObj)) continue;
    CObList* pList = (CObList*)pObj;
    for (POSITION pos = pList->GetHeadPosition(); pos != NULL;) {
      CMFCToolBarButton* p = (CMFCToolBarButton*)pList->GetNext(pos);
      if (p->m_nID == uiCmd) {
        strCommandName = p->m_strText;
        return TRUE;
      }
    }
  }
  return FALSE;
}
