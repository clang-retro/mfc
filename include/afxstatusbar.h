// afxstatusbar.h - CMFCStatusBar, the Feature Pack's status bar: panes
// by indicator ID with text, icons, colors, a progress bar or an
// animation, sized by their text or stretched; drawn by the visual
// manager, updated by command UI, and the frame's message line in pane 0.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSTATUSBAR_H__
#define __AFXSTATUSBAR_H__

#include <afxpane.h>
#include <afxtempl.h>

// Sent to the frame on a double click on a pane (EnablePaneDoubleClick).
extern UINT AFX_WM_ON_STATUSBAR_DBLCLK;

struct CMFCStatusBarPaneInfo {
  UINT nID;
  UINT nStyle;     // SBPS_
  int cxText;      // the width asked for
  CString strText;
  CString strToolTip;
  HIMAGELIST hImages;  // an icon, or an animation's frames
  int nFrameCount, nCurrFrame;
  CSize sizeImage;
  COLORREF clrText, clrBackground;
  long nProgressCurr, nProgressTotal;
  BOOL bProgressText;
  COLORREF clrProgressBar, clrProgressBarDest, clrProgressText;
  CRect rect;      // laid out
  BOOL bOwnImages;
};

class CMFCStatusBar : public CPane {
  DECLARE_DYNAMIC(CMFCStatusBar)
public:
  CMFCStatusBar();
  virtual ~CMFCStatusBar();

  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_BOTTOM,
                      UINT nID = AFX_IDW_STATUS_BAR);
  virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = 0, DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_BOTTOM,
                        UINT nID = AFX_IDW_STATUS_BAR);

  BOOL SetIndicators(const UINT* lpIDArray, int nIDCount);
  int GetCount() const { return (int)m_arPanes.GetSize(); }
  int CommandToIndex(UINT nIDFind) const;
  UINT GetItemID(int nIndex) const;
  void GetItemRect(int nIndex, LPRECT lpRect) const;
  CString GetPaneText(int nIndex) const;
  void GetPaneText(int nIndex, CString& rString) const { rString = GetPaneText(nIndex); }
  virtual BOOL SetPaneText(int nIndex, LPCTSTR lpszNewText, BOOL bUpdate = TRUE);
  void GetPaneInfo(int nIndex, UINT& nID, UINT& nStyle, int& cxWidth) const;
  void SetPaneInfo(int nIndex, UINT nID, UINT nStyle, int cxWidth);
  UINT GetPaneStyle(int nIndex) const;
  void SetPaneStyle(int nIndex, UINT nStyle);
  int GetPaneWidth(int nIndex) const;
  void SetPaneWidth(int nIndex, int cx);
  void SetPaneIcon(int nIndex, HICON hIcon, BOOL bUpdate = TRUE);
  void SetPaneIcon(int nIndex, HBITMAP hBmp, COLORREF clrTransparent = RGB(255, 0, 255), BOOL bUpdate = TRUE);
  void SetPaneAnimation(int nIndex, HIMAGELIST hImageList, UINT nFrameRate = 500, BOOL bUpdate = TRUE);
  void SetPaneTextColor(int nIndex, COLORREF clrText = (COLORREF)-1, BOOL bUpdate = TRUE);
  void SetPaneBackgroundColor(int nIndex, COLORREF clrBackground = (COLORREF)-1, BOOL bUpdate = TRUE);
  void SetTipText(int nIndex, LPCTSTR pszTipText);
  CString GetTipText(int nIndex) const;
  void EnablePaneProgressBar(int nIndex, long nTotal = 100, BOOL bDisplayText = FALSE,
                             COLORREF clrBar = (COLORREF)-1, COLORREF clrBarDest = (COLORREF)-1,
                             COLORREF clrProgressText = (COLORREF)-1);
  void SetPaneProgress(int nIndex, long nCurr, BOOL bUpdate = TRUE);
  long GetPaneProgress(int nIndex) const;
  void InvalidatePaneContent(int nIndex);
  void EnablePaneDoubleClick(BOOL bEnable = TRUE) { m_bPaneDoubleClick = bEnable; }
  CMFCStatusBarPaneInfo* GetPane(int nIndex) const;
  int GetExtendedArea() const { return 0; }
  int HitTest(CPoint point) const;
  BOOL GetDrawExtendedArea() const { return FALSE; }
  void SetDrawExtendedArea(BOOL = TRUE) {}

  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  virtual void DoPaint(CDC* pDC);
  virtual BOOL AllowShowOnPaneMenu() const { return FALSE; }
  virtual void OnDrawPane(CDC* pDC, CMFCStatusBarPaneInfo* pPane);
  virtual void RecalcLayout();

protected:
  void _FreePane(CMFCStatusBarPaneInfo* pPane);
  BOOL _HasSizeBox() const;
  CRect _SizeBoxRect() const;

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg LRESULT OnNcHitTest(CPoint point);
  afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetText(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetTextLength(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnSetFont(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetFont(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()

  CArray<CMFCStatusBarPaneInfo*, CMFCStatusBarPaneInfo*> m_arPanes;
  HFONT m_hFont;
  int m_cyHeight;
  BOOL m_bPaneDoubleClick;
  CToolTipCtrl* m_pToolTip;
};

#endif  // __AFXSTATUSBAR_H__
