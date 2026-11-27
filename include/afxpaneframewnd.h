// afxpaneframewnd.h - CPaneFrameWnd, the small tool window a floating
// pane sits in: its caption shows the pane's title, closing it hides the
// pane, and a double click on the caption docks the pane again.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXPANEFRAMEWND_H__
#define __AFXPANEFRAMEWND_H__

#include <afxpane.h>

class CPaneFrameWnd : public CWnd {
  DECLARE_SERIAL(CPaneFrameWnd)
public:
  CPaneFrameWnd();
  virtual ~CPaneFrameWnd();

  virtual BOOL Create(LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                      CCreateContext* pContext = NULL);
  virtual BOOL CreateEx(DWORD dwStyleEx, LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
                        CWnd* pParentWnd, CCreateContext* pContext = NULL);

  virtual void AddPane(CBasePane* pWnd);
  virtual void RemovePane(CBasePane* pWnd, BOOL bDestroy = FALSE, BOOL bNoDelayedDestroy = FALSE);
  virtual CWnd* GetPane() const { return m_pPane; }
  virtual CWnd* GetFirstVisiblePane() const { return m_pPane; }
  virtual int GetPaneCount() const { return m_pPane != NULL ? 1 : 0; }
  virtual CFrameWnd* GetDockSiteFrameWnd() const { return m_pDockSite; }
  virtual void SetDockingManager(CDockingManager* pManager) { m_pDockManager = pManager; }
  CDockingManager* GetDockingManager() const { return m_pDockManager; }
  virtual void OnPaneRecalcLayout();
  virtual BOOL DockPane(BOOL& bWasDocked);
  virtual void SetCaptionButtons(DWORD) {}

protected:
  virtual void PostNcDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnClose();
  afx_msg void OnNcLButtonDblClk(UINT nHitTest, CPoint point);
  afx_msg void OnMove(int x, int y);
  DECLARE_MESSAGE_MAP()

  CBasePane* m_pPane;
  CFrameWnd* m_pDockSite;
  CDockingManager* m_pDockManager;
};

typedef CPaneFrameWnd CMultiPaneFrameWnd;

#endif  // __AFXPANEFRAMEWND_H__
