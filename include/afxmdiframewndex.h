// afxmdiframewndex.h - CMDIFrameWndEx, the Feature Pack's MDI frame: the
// panes of CFrameWndEx around the MDI client, and MDI tabs, a tab for
// each child window above the client with the active child maximized.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMDIFRAMEWNDEX_H__
#define __AFXMDIFRAMEWNDEX_H__

#include <afxdockingmanager.h>
#include <afxpane.h>
class CMFCToolBarButton;
class CMFCToolBarMenuButton;
#include <afxtabctrl.h>

class CMFCMenuBar;
class CMFCToolBar;
class CMFCPopupMenu;
class CDockablePane;
class CMDIChildWndEx;

// MDI tabs' look (EnableMDITabbedGroups).
class CMDITabInfo {
public:
  CMDITabInfo()
      : m_tabLocation(CMFCTabCtrl::LOCATION_TOP), m_style(CMFCTabCtrl::STYLE_3D_SCROLLED), m_bTabCloseButton(TRUE),
        m_bTabCustomTooltips(FALSE), m_bTabIcons(FALSE), m_bAutoColor(FALSE), m_bDocumentMenu(FALSE),
        m_bEnableTabSwap(TRUE), m_nTabBorderSize(1), m_bActiveTabCloseButton(FALSE), m_bFlatFrame(TRUE) {}
  void Serialize(CArchive&) {}

  CMFCTabCtrl::Location m_tabLocation;
  CMFCTabCtrl::Style m_style;
  BOOL m_bTabCloseButton, m_bTabCustomTooltips, m_bTabIcons, m_bAutoColor, m_bDocumentMenu, m_bEnableTabSwap;
  int m_nTabBorderSize;
  BOOL m_bActiveTabCloseButton, m_bFlatFrame;
};

class CMDIFrameWndEx : public CMDIFrameWnd {
  DECLARE_DYNCREATE(CMDIFrameWndEx)
  friend class CMDIChildWndEx;

public:
  CMDIFrameWndEx();
  virtual ~CMDIFrameWndEx();

  // ----- The panes (as CFrameWndEx).
  BOOL EnableDocking(DWORD dwDockStyle);
  BOOL EnableAutoHidePanes(DWORD dwDockStyle);
  void DockPane(CBasePane* pBar, UINT nDockBarID = 0, LPCRECT lpRect = NULL);
  BOOL DockPaneLeftOf(CPane* pBar, CPane* pLeftOf);
  BOOL AddPane(CBasePane* pControlBar, BOOL bTail = TRUE);
  BOOL InsertPane(CBasePane* pControlBar, CBasePane* pTarget, BOOL bAfter = TRUE);
  void RemovePaneFromDockManager(CBasePane* pControlBar, BOOL bDestroy, BOOL bAdjustLayout, BOOL bAutoHide,
                                 CBasePane* pBarReplacement);
  void ShowPane(CBasePane* pBar, BOOL bShow, BOOL bDelay, BOOL bActivate);
  CBasePane* GetPane(UINT nID);
  CDockingManager* GetDockingManager() { return &m_dockManager; }
  const CMFCMenuBar* GetMenuBar() const { return m_dockManager.GetMenuBar(); }
  CMFCMenuBar* GetMenuBar() { return m_dockManager.GetMenuBar(); }
  void EnablePaneMenu(BOOL bEnable, UINT uiCustomizeCmd, const CString& strCustomizeLabel,
                      UINT uiViewToolbarsMenuEntryID, BOOL bContextMenuShowsToolbarsOnly = FALSE,
                      BOOL bViewMenuShowsToolbarsOnly = FALSE);
  BOOL IsMenuBarAvailable() const { return m_dockManager.GetMenuBar() != NULL; }

  // ----- MDI tabs.
  void EnableMDITabs(BOOL bEnable = TRUE, BOOL bIcons = TRUE,
                     CMFCTabCtrl::Location tabLocation = CMFCTabCtrl::LOCATION_TOP, BOOL bTabCloseButton = FALSE,
                     CMFCTabCtrl::Style style = CMFCTabCtrl::STYLE_3D_SCROLLED, BOOL bTabCustomTooltips = FALSE,
                     BOOL bActiveTabCloseButton = FALSE);
  void EnableMDITabbedGroups(BOOL bEnable, const CMDITabInfo& params);
  BOOL AreMDITabs(int* pnMDITabsType = NULL) const;
  BOOL IsMDITabbedGroup() const { return m_bMDITabbedGroups; }
  CMFCTabCtrl& GetMDITabs() { return m_wndMDITabs; }
  void UpdateMDITabbedBarsIcons() {}
  void MDITabMoveToNextGroup(BOOL = TRUE) {}
  void MDITabNewGroup(BOOL = TRUE) {}
  void SetPrintPreviewFrame(CFrameWnd*) {}
  void EnableMDITabsLastActiveActivation(BOOL = TRUE) {}

  // ----- User toolbars and the rest.
  BOOL InitUserToolbars(LPCTSTR lpszRegEntry, UINT uiUserToolbarFirst, UINT uiUserToolbarLast);
  CMFCToolBar* GetUserToolBarByIndex(int iIndex) const;
  void EnableFullScreenMode(UINT uiFullScreenCmd) { m_uiFullScreenCmd = uiFullScreenCmd; }
  void ShowFullScreen();
  BOOL IsFullScreen() const { return m_bFullScreen; }
  void EnableFullScreenMainMenu(BOOL bShowMainMenu) { m_bFullScreenMainMenu = bShowMainMenu; }
  void EnableLoadDockState(BOOL bEnable = TRUE) { m_bLoadDockState = bEnable; }
  void EnableWindowsDialog(UINT, LPCTSTR, BOOL = FALSE, BOOL = FALSE) {}
  void EnableWindowsDialog(UINT, UINT, BOOL = FALSE, BOOL = FALSE) {}
  void ShowWindowsDialog() {}
  CMFCPopupMenu* GetActivePopup() const { return NULL; }

  virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
                         CWnd* pParentWnd = NULL, CCreateContext* pContext = NULL);
  virtual void RecalcLayout(BOOL bNotify = TRUE);
  virtual void OnUpdateFrameMenu(HMENU hMenuAlt);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual BOOL OnShowPopupMenu(CMFCPopupMenu* /*pMenuPopup*/) { return TRUE; }
  virtual BOOL OnShowCustomizePane(CMFCPopupMenu* /*pMenuPane*/, UINT /*uiToolbarID*/) { return TRUE; }
  virtual BOOL OnShowPanes(BOOL bShow);
  virtual BOOL OnShowMDITabContextMenu(CPoint /*point*/, DWORD /*dwAllowedItems*/, BOOL /*bTabDrop*/) {
    return FALSE;
  }
  virtual BOOL OnCloseDockingPane(CDockablePane* /*pPane*/) { return TRUE; }
  virtual BOOL OnCloseMiniFrame(CPaneFrameWnd* /*pWnd*/) { return TRUE; }
  virtual BOOL OnMenuButtonToolHitTest(CMFCToolBarButton*, TOOLINFO*) { return FALSE; }
  virtual BOOL GetToolbarButtonToolTipText(CMFCToolBarButton*, CString&) { return FALSE; }
  virtual BOOL OnDrawMenuImage(CDC*, const CMFCToolBarMenuButton*, const CRect&) { return FALSE; }
  virtual BOOL OnTearOffMenu(CMFCPopupMenu*, CPane*) { return TRUE; }
  virtual CMDIChildWndEx* CreateDocumentWindow(LPCTSTR, CObject*) { return NULL; }
  virtual CMDIChildWndEx* CreateNewWindow(LPCTSTR, CObject*) { return NULL; }
  virtual void OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pState);

  // Keeps the tabs in step with the children (called by CMDIChildWndEx).
  void _UpdateMDITabs();
  void _OnChildMenuChanged();

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  afx_msg LRESULT OnToolbarCreateNew(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnChangeActiveTab(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdatePaneMenu(CCmdUI* pCmdUI);
  afx_msg BOOL OnPaneCheck(UINT nID);
  afx_msg void OnDestroy();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
  virtual void OnIdleUpdateCmdUI();
  DECLARE_MESSAGE_MAP()

  CDockingManager m_dockManager;
  CMFCTabCtrl m_wndMDITabs;
  CMap<HWND, HWND, CWnd*, CWnd*> m_mapTabPlaceholders;  // child window to its tab's stand-in
  BOOL m_bMDITabs, m_bMDITabbedGroups, m_bInTabUpdate, m_bMDITabIcons;
  UINT m_uiFullScreenCmd, m_uiViewToolbarsMenuEntryID, m_uiUserToolbarFirst, m_uiUserToolbarLast;
  BOOL m_bFullScreen, m_bFullScreenMainMenu, m_bLoadDockState;
  CRect m_rectFullScreenOld;
  CObList m_lstUserToolbars;
  BOOL m_bViewMenuShowsToolbarsOnly;
};

#endif  // __AFXMDIFRAMEWNDEX_H__
