// afxframewndex.h - CFrameWndEx, the Feature Pack's SDI frame: a
// docking manager for its menu bar, toolbars, status bar and dockable
// panes; their layout around the view; the menu bar in place of the
// window's menu; and the menu of panes on a right click.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXFRAMEWNDEX_H__
#define __AFXFRAMEWNDEX_H__

#include <afxdockingmanager.h>
#include <afxpane.h>
class CMFCToolBarButton;
class CMFCToolBarMenuButton;

class CMFCMenuBar;
class CMFCToolBar;
class CMFCPopupMenu;
class CDockablePane;

class CFrameWndEx : public CFrameWnd {
  DECLARE_DYNCREATE(CFrameWndEx)
public:
  CFrameWndEx();
  virtual ~CFrameWndEx();

  // ----- The panes.
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
  void UpdateCaption() {}
  BOOL IsMenuBarAvailable() const { return m_dockManager.GetMenuBar() != NULL; }

  // ----- User toolbars and the rest.
  BOOL InitUserToolbars(LPCTSTR lpszRegEntry, UINT uiUserToolbarFirst, UINT uiUserToolbarLast);
  CMFCToolBar* GetUserToolBarByIndex(int iIndex) const;
  void EnableFullScreenMode(UINT uiFullScreenCmd) { m_uiFullScreenCmd = uiFullScreenCmd; }
  void ShowFullScreen();
  BOOL IsFullScreen() const { return m_bFullScreen; }
  void EnableFullScreenMainMenu(BOOL bShowMainMenu) { m_bFullScreenMainMenu = bShowMainMenu; }
  void EnableLoadDockState(BOOL bEnable = TRUE) { m_bLoadDockState = bEnable; }
  void SetupToolbarMenu(CMenu&, const UINT, const UINT) {}
  CMFCPopupMenu* GetActivePopup() const { return NULL; }
  void EnableWindowsDialog(UINT, LPCTSTR, BOOL = FALSE, BOOL = FALSE) {}
  void EnableWindowsDialog(UINT, UINT, BOOL = FALSE, BOOL = FALSE) {}

  virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
                         CWnd* pParentWnd = NULL, CCreateContext* pContext = NULL);
  virtual void RecalcLayout(BOOL bNotify = TRUE);
  virtual void OnUpdateFrameMenu(HMENU hMenuAlt);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual BOOL OnShowPopupMenu(CMFCPopupMenu* /*pMenuPopup*/) { return TRUE; }
  virtual BOOL OnShowCustomizePane(CMFCPopupMenu* /*pMenuPane*/, UINT /*uiToolbarID*/) { return TRUE; }
  virtual BOOL OnShowPanes(BOOL bShow);
  virtual BOOL OnCloseDockingPane(CDockablePane* /*pPane*/) { return TRUE; }
  virtual BOOL OnCloseMiniFrame(CPaneFrameWnd* /*pWnd*/) { return TRUE; }
  virtual BOOL OnMenuButtonToolHitTest(CMFCToolBarButton*, TOOLINFO*) { return FALSE; }
  virtual BOOL GetToolbarButtonToolTipText(CMFCToolBarButton*, CString&) { return FALSE; }
  virtual BOOL OnDrawMenuImage(CDC*, const CMFCToolBarMenuButton*, const CRect&) { return FALSE; }
  virtual void OnDrawMenuLogo(CDC*, CMFCPopupMenu*, const CRect&) {}
  virtual BOOL OnTearOffMenu(CMFCPopupMenu*, CPane*) { return TRUE; }
  virtual void OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pState);

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  afx_msg LRESULT OnToolbarCreateNew(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdatePaneMenu(CCmdUI* pCmdUI);
  afx_msg BOOL OnPaneCheck(UINT nID);
  afx_msg void OnDestroy();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  virtual void OnIdleUpdateCmdUI();
  afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
  DECLARE_MESSAGE_MAP()

  CDockingManager m_dockManager;
  UINT m_uiFullScreenCmd, m_uiViewToolbarsMenuEntryID, m_uiUserToolbarFirst, m_uiUserToolbarLast;
  BOOL m_bFullScreen, m_bFullScreenMainMenu, m_bLoadDockState;
  CRect m_rectFullScreenOld;
  CObList m_lstUserToolbars;
  BOOL m_bViewMenuShowsToolbarsOnly;
};

#endif  // __AFXFRAMEWNDEX_H__
