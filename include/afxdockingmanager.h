// afxdockingmanager.h - CDockingManager, which keeps a CFrameWndEx-family
// frame's panes: the order they dock in, outermost first, each on a side
// in a row of its own or beside the one before; the panes that float;
// the auto-hide bars on the sides; and the layout that gives each docked
// pane its strip and leaves the rest to the view or the MDI client.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDOCKINGMANAGER_H__
#define __AFXDOCKINGMANAGER_H__

#include <afxbasepane.h>

class CPane;
class CDockablePane;
class CMFCMenuBar;
class CMFCAutoHideBar;

class CDockingManager : public CObject {
  friend class CBasePane;
  friend class CDockablePane;
  friend class CPane;
  friend class CBaseTabbedPane;

public:
  CDockingManager();
  virtual ~CDockingManager();

  BOOL Create(CFrameWnd* pParentWnd);
  CFrameWnd* GetDockSiteFrameWnd() const { return m_pParentWnd; }

  // ----- The sides.
  BOOL EnableDocking(DWORD dwStyle);
  BOOL EnableAutoHidePanes(DWORD dwStyle);
  DWORD GetEnabledAutoHideAlignment() const { return m_dwEnabledSlideAlignment; }
  DWORD GetEnabledDockingAlignment() const { return m_dwEnabledDockingAlignment; }

  // ----- The panes.
  BOOL AddPane(CBasePane* pWnd, BOOL bTail = TRUE, BOOL bAutoHide = FALSE, BOOL bInsertForOuterEdge = FALSE);
  BOOL InsertPane(CBasePane* pControlBar, CBasePane* pTarget, BOOL bAfter = TRUE);
  void RemovePaneFromDockManager(CBasePane* pWnd, BOOL bDestroy = TRUE, BOOL bAdjustLayout = FALSE,
                                 BOOL bAutoHide = FALSE, CBasePane* pBarReplacement = NULL);
  // Docks on a side: nDockBarID an AFX_IDW_DOCKBAR_ ID, or 0 for the
  // pane's own alignment.
  void DockPane(CBasePane* pBar, UINT nDockBarID = 0, LPCRECT lpRect = NULL);
  // Docks pBar beside pLeftOf, in its row, before it.
  BOOL DockPaneLeftOf(CPane* pBar, CPane* pLeftOf);
  CBasePane* FindPaneByID(UINT uBarID, BOOL bSearchMiniFrames = FALSE);
  void GetPaneList(CObList& lstBars, BOOL bIncludeAutohide = FALSE, CRuntimeClass* pRTCFilter = NULL,
                   BOOL bIncludeTabs = FALSE);
  CMFCMenuBar* GetMenuBar() const { return m_pMenuBar; }
  void ShowPanes(BOOL bShow);

  // ----- The layout.
  virtual void AdjustDockingLayout(HDWP hdwp = NULL);
  virtual void AdjustPaneFrames() {}
  CRect GetClientAreaBounds() const { return m_rectClientAreaBounds; }
  void GetClientAreaBounds(CRect& rectClient) const { rectClient = m_rectClientAreaBounds; }
  // The side (CBRS_ALIGN_) a pane is docked on, 0 when not docked.
  DWORD GetPaneAlignment(const CBasePane* pBar) const;
  BOOL IsPaneDocked(const CBasePane* pBar) const { return m_lstDocked.Find((CObject*)pBar) != NULL; }

  // ----- Auto-hide.
  CMFCAutoHideBar* GetAutoHideBar(DWORD dwAlignment, BOOL bCreate);
  void HideAutoHidePanes(CDockablePane* pBarToExclude = NULL);

  // ----- The panes' menu and the rest.
  BOOL IsDockSiteMenu() const { return m_bDockSiteMenu; }
  void EnableDockSiteMenu(BOOL bEnable = TRUE) { m_bDockSiteMenu = bEnable; }
  void EnablePaneContextMenu(BOOL bEnable, UINT uiCustomizeCmd, const CString& strCustomizeText,
                             BOOL bToolbarsOnly = FALSE);
  BOOL IsPaneContextMenuEnabled() const { return m_bPaneContextMenu; }
  // The menu of panes to show or hide, at a screen point.
  BOOL OnPaneContextMenu(CPoint point);
  void BuildPanesMenu(CMenu& menu, BOOL bToolbarsOnly);
  BOOL LoadState(LPCTSTR /*lpszProfileName*/ = NULL, UINT /*uiID*/ = (UINT)-1) { return TRUE; }
  BOOL SaveState(LPCTSTR /*lpszProfileName*/ = NULL, UINT /*uiID*/ = (UINT)-1) { return TRUE; }
  void DisableRestoreDockState(BOOL bDisable = TRUE) { m_bDisableRestoreDockState = bDisable; }
  void SetPrintPreviewMode(BOOL bPreview, CPrintPreviewState* pState);
  BOOL IsPrintPreviewValid() const { return m_bIsPrintPreviewMode; }
  void LockUpdate(BOOL bLock) { m_bLockUpdate = bLock; }

  static void AFXAPI SetDockingMode(AFX_DOCK_TYPE dockMode, AFX_SMARTDOCK_THEME theme = AFX_SDT_DEFAULT);
  static AFX_DOCK_TYPE AFXAPI GetDockingMode() { return m_dockModeGlobal; }
  static void AFXAPI EnableDockSiteMenu(BOOL bEnable, BOOL) { m_bGlobalDockSiteMenu = bEnable; }

  static UINT m_nTimeOutBeforeToolBarDock, m_nTimeOutBeforeDockingBarDock;
  static BOOL m_bRestoringDockState, m_bHideDockingBarsInContainerMode, m_bDisableRecalcLayout;
  static BOOL m_bFullScreenMode;

protected:
  POSITION _FindRowEnd(POSITION pos);
  void _Remove(CBasePane* pBar);
  // Docks pBar beside pTarget: in its row (bSameRow) or in a row of its
  // own next to it, before it (outer side, or earlier in the row) or after.
  BOOL _InsertDocked(CBasePane* pBar, CBasePane* pTarget, BOOL bSameRow, BOOL bBefore);
  // Puts pNew where pOld is docked (pOld stays a known pane).
  void _ReplaceDocked(CBasePane* pOld, CBasePane* pNew);
  void _RecalcFrame();

public:
  // Command UI for the panes that float (their windows are not the
  // frame's children).
  void SendIdleUpdateToMiniFrames();

protected:

  CFrameWnd* m_pParentWnd;
  CObList m_lstPanes;   // every pane the frame knows
  CObList m_lstDocked;  // the docked ones, outermost first
  CMFCMenuBar* m_pMenuBar;
  CMFCAutoHideBar* m_pAutoHideBars[4];  // left, top, right, bottom
  DWORD m_dwEnabledDockingAlignment, m_dwEnabledSlideAlignment;
  CRect m_rectClientAreaBounds;
  BOOL m_bDockSiteMenu, m_bPaneContextMenu, m_bPaneContextMenuToolbarsOnly, m_bDisableRestoreDockState;
  BOOL m_bIsPrintPreviewMode, m_bLockUpdate, m_bInLayout;
  UINT m_uiCustomizeCmd;
  CString m_strCustomizeText;
  CObList m_lstHiddenPanes;  // hidden for print preview or ShowPanes(FALSE)

  static AFX_DOCK_TYPE m_dockModeGlobal;
  static BOOL m_bGlobalDockSiteMenu;
};

#endif  // __AFXDOCKINGMANAGER_H__
