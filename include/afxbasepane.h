// afxbasepane.h - CBasePane, what the Feature Pack's bars and panes
// share: their CBRS_ alignment and style, the sides they may dock to,
// their own AFX_CBRS_ style (float, close, resize, auto-hide), showing
// and hiding, and command UI updates when the application is idle. A
// pane made with a CFrameWndEx-family frame as its parent joins the
// frame's docking manager.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXBASEPANE_H__
#define __AFXBASEPANE_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxext.h>

class CDockingManager;
class CPaneFrameWnd;
class CMFCAutoHideBar;

// The pane's own styles (GetControlBarStyle).
#define AFX_CBRS_FLOAT 0x1
#define AFX_CBRS_AUTOHIDE 0x2
#define AFX_CBRS_RESIZE 0x4
#define AFX_CBRS_CLOSE 0x8
#define AFX_CBRS_AUTO_ROLLUP 0x10
#define AFX_CBRS_REGULAR_TABS 0x20
#define AFX_CBRS_OUTLOOK_TABS 0x40
#define AFX_DEFAULT_DOCKING_PANE_STYLE (AFX_CBRS_FLOAT | AFX_CBRS_CLOSE | AFX_CBRS_RESIZE | AFX_CBRS_AUTOHIDE)
#define AFX_DEFAULT_TOOLBAR_STYLE (WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_HIDE_INPLACE)
#define AFX_DEFAULT_PANE_STYLE (WS_CHILD | WS_VISIBLE | CBRS_TOP)

// How a pane docks: by dragging an outline (standard), at once, or with
// the smart docking markers. Here every mode docks at once.
enum AFX_DOCK_TYPE { DT_UNDEFINED = 0, DT_IMMEDIATE = 1, DT_STANDARD = 2, DT_SMART = 0x80 };
// What made a pane dock or float.
// The smart docking markers' look.
enum AFX_SMARTDOCK_THEME { AFX_SDT_DEFAULT = 0, AFX_SDT_VS2005 = 1, AFX_SDT_VS2008 = 2 };
enum AFX_DOCK_METHOD { DM_UNKNOWN, DM_MOUSE, DM_DBL_CLICK, DM_SHOW, DM_RECT, DM_STANDARD };

class CBasePane : public CWnd {
  DECLARE_DYNAMIC(CBasePane)
  friend class CDockingManager;
  friend class CPaneFrameWnd;
  friend class CBaseTabbedPane;

public:
  CBasePane();
  virtual ~CBasePane();

  virtual BOOL CreateEx(DWORD dwStyleEx, LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                        const RECT& rect, CWnd* pParentWnd, UINT nID, DWORD dwControlBarStyle = 0,
                        CCreateContext* pContext = NULL);

  // ----- Styles.
  virtual void SetPaneStyle(DWORD dwNewStyle) { m_dwStyle = dwNewStyle; }
  virtual DWORD GetPaneStyle() const { return m_dwStyle; }
  virtual DWORD GetCurrentAlignment() const { return m_dwStyle & CBRS_ALIGN_ANY; }
  virtual void SetPaneAlignment(DWORD dwAlignment);
  virtual DWORD GetControlBarStyle() const { return m_dwControlBarStyle; }
  virtual void SetControlBarStyle(DWORD dwNewStyle) { m_dwControlBarStyle = dwNewStyle; }
  virtual DWORD GetEnabledAlignment() const { return m_dwEnabledAlignment; }
  virtual void EnableDocking(DWORD dwAlignment) { m_dwEnabledAlignment = dwAlignment & CBRS_ALIGN_ANY; }
  BOOL IsHorizontal() const { return (GetCurrentAlignment() & CBRS_ORIENT_HORZ) != 0 || GetCurrentAlignment() == 0; }

  // ----- State.
  virtual BOOL IsDocked() const;
  virtual BOOL IsFloating() const { return !IsDocked() && m_pMiniFrame != NULL; }
  virtual BOOL IsTabbed() const;
  virtual BOOL IsVisible() const;
  virtual BOOL IsResizable() const { return FALSE; }
  virtual BOOL CanFloat() const { return (m_dwControlBarStyle & AFX_CBRS_FLOAT) != 0; }
  virtual BOOL CanBeClosed() const { return (m_dwControlBarStyle & AFX_CBRS_CLOSE) != 0; }
  virtual BOOL CanAutoHide() const { return (m_dwControlBarStyle & AFX_CBRS_AUTOHIDE) != 0; }
  virtual BOOL IsAutoHideMode() const { return FALSE; }
  virtual BOOL CanBeDocked(CBasePane* /*pDockBar*/) const { return TRUE; }
  virtual BOOL HideInPrintPreviewMode() const { return TRUE; }
  virtual BOOL IsRestoredFromRegistry() const { return FALSE; }
  virtual BOOL DoesAllowDynInsertBefore() const { return TRUE; }

  // ----- Showing and the layout.
  virtual void ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate);
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual void RecalcLayout() {}
  virtual void AdjustLayout() {}
  virtual void AdjustDockingLayout(HDWP hdwp = NULL);
  virtual void DoPaint(CDC* pDC);
  virtual void OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL /*bDisableIfNoHndler*/) {}
  virtual CFrameWnd* GetDockSiteFrameWnd() const { return m_pDockSite; }
  CPaneFrameWnd* GetParentMiniFrame(BOOL /*bNoAssert*/ = FALSE) const { return m_pMiniFrame; }
  CDockingManager* GetDockingManager() const;
  // The docking manager of a CFrameWndEx-family frame (NULL for others).
  static CDockingManager* AFXAPI GetDockingManagerOf(CWnd* pFrame);
  virtual int GetCaptionHeight() const { return 0; }
  virtual BOOL SaveState(LPCTSTR /*lpszProfileName*/ = NULL, int /*nIndex*/ = -1, UINT /*uiID*/ = (UINT)-1) {
    return TRUE;
  }
  virtual BOOL LoadState(LPCTSTR /*lpszProfileName*/ = NULL, int /*nIndex*/ = -1, UINT /*uiID*/ = (UINT)-1) {
    return TRUE;
  }
  virtual void SetRestoredFromRegistry(BOOL) {}
  virtual BOOL AllowShowOnPaneMenu() const { return TRUE; }
  virtual void GetPaneName(CString& strName) const { GetWindowText(strName); }

protected:
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam);
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnPaint();
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

  DWORD m_dwStyle, m_dwControlBarStyle, m_dwEnabledAlignment;
  CFrameWnd* m_pDockSite;
  CPaneFrameWnd* m_pMiniFrame;  // while floating
  CWnd* m_pTabbedParent;        // while a tab of a tabbed pane
  BOOL m_bVisibleState;         // shown, as far as the layout goes
  BOOL m_bSameRowAsPrev;        // docked beside the pane before it
  CSize m_sizeDock;             // the size docked (resizable panes)
};

#endif  // __AFXBASEPANE_H__
