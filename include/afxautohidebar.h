// afxautohidebar.h - CMFCAutoHideBar, the strip on a side of the frame
// that holds the auto-hidden panes of that side as buttons with their
// titles; a click on one slides its pane out over the client area, and a
// second click (or another pane) slides it back.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXAUTOHIDEBAR_H__
#define __AFXAUTOHIDEBAR_H__

#include <afxpane.h>
#include <afxtempl.h>

class CDockablePane;

class CMFCAutoHideBar : public CPane {
  DECLARE_DYNCREATE(CMFCAutoHideBar)
  friend class CDockingManager;
  friend class CDockablePane;

public:
  CMFCAutoHideBar();
  virtual ~CMFCAutoHideBar();

  BOOL Create(CWnd* pParentWnd, DWORD dwAlignment);
  void AddAutoHidePane(CDockablePane* pPane);
  BOOL RemoveAutoHidePane(CDockablePane* pPane);
  int GetCount() const { return (int)m_arPanes.GetSize(); }
  CDockablePane* GetPane(int i) const { return i >= 0 && i < GetCount() ? m_arPanes[i] : NULL; }
  CRect GetButtonRect(CDockablePane* pPane) const;
  CDockablePane* HitTest(CPoint ptClient) const;
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual BOOL AllowShowOnPaneMenu() const { return FALSE; }

  static int m_nShowAHWndDelay;

protected:
  void _Layout();
  afx_msg void OnPaint();
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  DECLARE_MESSAGE_MAP()

  CArray<CDockablePane*, CDockablePane*> m_arPanes;
  CArray<CRect, CRect&> m_arRects;
};

#endif  // __AFXAUTOHIDEBAR_H__
