// afxtabbedpane.h - CBaseTabbedPane and CTabbedPane: a dockable pane
// that holds other dockable panes as the tabs of a CMFCTabCtrl, the
// active one's window shown and its title in the caption.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTABBEDPANE_H__
#define __AFXTABBEDPANE_H__

#include <afxdockablepane.h>
#include <afxtabctrl.h>

class CBaseTabbedPane : public CDockablePane {
  DECLARE_DYNAMIC(CBaseTabbedPane)
  friend class CDockablePane;
public:
  CBaseTabbedPane(BOOL bAutoDestroy = FALSE);
  virtual ~CBaseTabbedPane();

  virtual BOOL AddTab(CWnd* pNewBar, BOOL bVisible = TRUE, BOOL bSetActive = TRUE, BOOL bDetachable = TRUE);
  // Takes a pane out of the tabs (it floats, or with bHide it hides).
  virtual BOOL RemovePane(CWnd* pBar);
  virtual BOOL DetachPane(CWnd* pBar, BOOL bHide = FALSE);
  virtual CWnd* FindPaneByID(UINT uBarID);
  virtual CWnd* FindBarByTabNumber(int nTabNum, BOOL bGetWrappedBar = FALSE);
  virtual BOOL ShowTab(CWnd* pBar, BOOL bShow, BOOL bDelay, BOOL bActivate);
  virtual int GetTabsNum() const;
  virtual int GetVisibleTabsNum() const;
  virtual BOOL CanFloat() const { return TRUE; }
  virtual CMFCBaseTabCtrl* GetUnderlyingWindow() { return m_pTabWnd; }
  CMFCBaseTabCtrl* GetTabWnd() const { return m_pTabWnd; }
  void EnableSetCaptionTextToTabName(BOOL bEnable) { m_bSetCaptionTextToTabName = bEnable; }
  BOOL CanSetCaptionTextToTabName() const { return m_bSetCaptionTextToTabName; }
  BOOL IsAutoDestroy() const { return m_bAutoDestroy; }
  virtual void ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate);

protected:
  virtual void _UpdateCaption();
  virtual void PostNcDestroy();
  // The last pane left takes the tabbed pane's place.
  void _CollapseIfSingle();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg LRESULT OnChangeActiveTab(WPARAM wParam, LPARAM lParam);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

  CMFCBaseTabCtrl* m_pTabWnd;
  BOOL m_bAutoDestroy, m_bSetCaptionTextToTabName;
};

class CTabbedPane : public CBaseTabbedPane {
  DECLARE_SERIAL(CTabbedPane)
public:
  CTabbedPane(BOOL bAutoDestroy = FALSE);
  virtual ~CTabbedPane();

  CMFCTabCtrl* GetTabCtrl() const { return (CMFCTabCtrl*)m_pTabWnd; }
  static void AFXAPI EnableTabAutoColor(BOOL bEnable = TRUE) { m_bAutoColor = bEnable; }
  static CMFCTabCtrl::Style m_StyleTabWnd;
  static BOOL m_bTabsAlwaysTop, m_bAutoColor;

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXTABBEDPANE_H__
