// afxmdichildwndex.h - CMDIChildWndEx, the Feature Pack's MDI child: it
// tells its CMDIFrameWndEx when it comes, goes, activates or changes its
// title, for the MDI tabs and the menu bar.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMDICHILDWNDEX_H__
#define __AFXMDICHILDWNDEX_H__

#include <afxmdiframewndex.h>

class CMDIChildWndEx : public CMDIChildWnd {
  DECLARE_DYNCREATE(CMDIChildWndEx)
public:
  CMDIChildWndEx();
  virtual ~CMDIChildWndEx();

  CMDIFrameWndEx* GetMDIFrameWndEx() const;
  virtual BOOL CanShowOnMDITabs() { return TRUE; }
  virtual BOOL CanShowOnWindowsList() { return TRUE; }
  virtual BOOL IsReadOnly() { return FALSE; }
  virtual LPCTSTR GetDocumentName(CObject** ppObj) {
    if (ppObj != NULL) *ppObj = NULL;
    return NULL;
  }
  virtual HICON GetFrameIcon() const;
  virtual void OnUpdateFrameTitle(BOOL bAddToTitle);
  virtual void OnUpdateFrameMenu(BOOL bActive, CWnd* pActivateWnd, HMENU hMenuAlt);
  virtual BOOL IsTabbedPane() const { return FALSE; }
  virtual BOOL IsTaskbarTabsSupportEnabled() const { return FALSE; }
  virtual void SetTaskbarTabProperties(DWORD) {}
  virtual void SetTaskbarTabOrder(CMDIChildWndEx* = NULL) {}
  void EnableDocking(DWORD) {}
  void DockPane(CBasePane*, UINT = 0, LPCRECT = NULL) {}
  CDockingManager* GetDockingManager() { return NULL; }

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnMDIActivate(BOOL bActivate, CWnd* pActivateWnd, CWnd* pDeactivateWnd);
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXMDICHILDWNDEX_H__
