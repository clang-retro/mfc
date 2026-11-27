// afxbasetabctrl.h - CMFCBaseTabCtrl, what the Feature Pack's tab
// windows share: a list of tabs, each with a window, a label, an image
// and colors; one active, its window shown in the window area; tabs on
// the top or bottom; swapping tabs by dragging; and the notifications
// AFX_WM_CHANGING_ACTIVE_TAB and AFX_WM_CHANGE_ACTIVE_TAB to the parent.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXBASETABCTRL_H__
#define __AFXBASETABCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxtempl.h>

// Sent to the parent: wParam the tab, lParam the tab window.
extern UINT AFX_WM_CHANGING_ACTIVE_TAB;
extern UINT AFX_WM_CHANGE_ACTIVE_TAB;
extern UINT AFX_WM_ON_MOVETABCOMPLETE;
extern UINT AFX_WM_ON_RENAME_TAB;
extern UINT AFX_WM_ON_BEFOREMOVE_TAB;

// One tab.
class CMFCTabInfo : public CObject {
public:
  CMFCTabInfo(const CString& strText, UINT uiIcon, CWnd* pWnd, int iTabID, BOOL bDetachable)
      : m_strText(strText), m_uiIcon(uiIcon), m_hIcon(NULL), m_bIconOnly(FALSE), m_pWnd(pWnd), m_iTabID(iTabID),
        m_bVisible(TRUE), m_bDetachable(bDetachable), m_clrText((COLORREF)-1), m_clrBack((COLORREF)-1),
        m_rect(0, 0, 0, 0), m_nFullWidth(0) {}

  CString m_strText;
  UINT m_uiIcon;
  HICON m_hIcon;
  BOOL m_bIconOnly;
  CWnd* m_pWnd;
  int m_iTabID;
  BOOL m_bVisible, m_bDetachable;
  COLORREF m_clrText, m_clrBack;
  CRect m_rect;
  int m_nFullWidth;
};

class CMFCBaseTabCtrl : public CWnd {
  DECLARE_DYNAMIC(CMFCBaseTabCtrl)
public:
  enum Location { LOCATION_BOTTOM = 0, LOCATION_TOP = 1 };

  CMFCBaseTabCtrl();
  virtual ~CMFCBaseTabCtrl();

  // ----- The tabs.
  virtual void AddTab(CWnd* pTabWnd, LPCTSTR lpszTabLabel, UINT uiImageId = (UINT)-1, BOOL bDetachable = TRUE);
  virtual void AddTab(CWnd* pTabWnd, UINT uiResTabLabel, UINT uiImageId = (UINT)-1, BOOL bDetachable = TRUE);
  virtual void InsertTab(CWnd* pNewWnd, LPCTSTR lpszTabLabel, int nInsertAt, UINT uiImageId = (UINT)-1,
                         BOOL bDetachable = TRUE);
  virtual BOOL RemoveTab(int iTab, BOOL bRecalcLayout = TRUE);
  virtual void RemoveAllTabs();
  int GetTabsNum() const { return (int)m_arTabs.GetSize(); }
  virtual int GetVisibleTabsNum() const;
  virtual BOOL ShowTab(int iTab, BOOL bShow = TRUE, BOOL bRecalcLayout = TRUE, BOOL bActivate = FALSE);
  BOOL IsTabVisible(int iTab) const;
  virtual BOOL MoveTab(int nSource, int nDest);

  // ----- The active one.
  virtual BOOL SetActiveTab(int iTab);
  int GetActiveTab() const { return m_iActiveTab; }
  CWnd* GetActiveWnd() const;
  int GetTabFromHwnd(HWND hwnd) const;
  CWnd* GetTabWnd(int iTab) const;
  CWnd* GetTabWndNoWrapper(int iTab) const { return GetTabWnd(iTab); }
  virtual int GetTabFromPoint(CPoint& pt) const;
  int GetTabID(int iTab) const;
  int GetTabByID(int id) const;

  // ----- Labels, images and colors.
  virtual BOOL GetTabLabel(int iTab, CString& strLabel) const;
  virtual BOOL SetTabLabel(int iTab, const CString& strLabel);
  virtual BOOL GetTabRect(int iTab, CRect& rect) const;
  virtual BOOL SetImageList(UINT uiID, int cx = 15, COLORREF clrTransp = RGB(255, 0, 255));
  virtual BOOL SetImageList(HIMAGELIST hImageList);
  HIMAGELIST GetImageList() const { return m_hImageList; }
  CSize GetImageSize() const { return m_sizeImage; }
  virtual BOOL SetTabIcon(int iTab, UINT uiIcon);
  virtual UINT GetTabIcon(int iTab) const;
  virtual BOOL SetTabHicon(int iTab, HICON hIcon);
  virtual HICON GetTabHicon(int iTab) const;
  virtual BOOL SetTabIconOnly(int iTab, BOOL bIconOnly = TRUE, BOOL bShowTooltipAlways = FALSE);
  virtual BOOL SetTabBkColor(int iTab, COLORREF color = (COLORREF)-1);
  virtual COLORREF GetTabBkColor(int iTab) const;
  virtual BOOL SetTabTextColor(int iTab, COLORREF color = (COLORREF)-1);
  virtual COLORREF GetTabTextColor(int iTab) const;
  void EnableAutoColor(BOOL bEnable = TRUE);
  BOOL IsAutoColor() const { return m_bIsAutoColor; }
  void SetAutoColors(const CArray<COLORREF, COLORREF>& arColors);
  const CArray<COLORREF, COLORREF>& GetAutoColors() const { return m_arAutoColors; }
  virtual COLORREF GetActiveTabColor() const { return m_clrActiveTabBk; }
  virtual COLORREF GetActiveTabTextColor() const { return m_clrActiveTabFg; }
  void SetActiveTabColor(COLORREF clr);
  void SetActiveTabTextColor(COLORREF clr);
  void SetTabBorderSize(int nTabBorderSize, BOOL bRepaint = TRUE);
  int GetTabBorderSize() const { return m_nTabBorderSize; }

  // ----- Look and behavior.
  virtual void SetLocation(Location location);
  Location GetLocation() const { return m_location; }
  void EnableTabSwap(BOOL bEnable) { m_bEnableTabSwap = bEnable; }
  BOOL IsTabSwapEnabled() const { return m_bEnableTabSwap; }
  void EnableInPlaceEdit(BOOL bEnable) { m_bIsInPlaceEdit = bEnable; }
  BOOL IsInPlaceEdit() const { return m_bIsInPlaceEdit; }
  void HideSingleTab(BOOL bHide = TRUE);
  BOOL IsHideSingleTab() const { return m_bHideSingleTab; }
  void EnableActiveTabCloseButton(BOOL bEnable = TRUE);
  BOOL IsActiveTabCloseButton() const { return m_bActiveTabCloseButton; }
  void SetDrawFrame(BOOL bDraw = TRUE) { m_bDrawFrame = bDraw; }
  BOOL IsDrawFrame() const { return m_bDrawFrame; }
  void AutoDestroyWindow(BOOL bAutoDestroy = TRUE) { m_bAutoDestroyWindow = bAutoDestroy; }
  BOOL IsFlatFrame() const { return m_bFlatFrame; }
  void SetFlatFrame(BOOL bFlat = TRUE, BOOL bRepaint = TRUE);
  virtual BOOL IsFlatTab() const { return FALSE; }
  virtual BOOL IsVS2005Style() const { return FALSE; }
  virtual BOOL IsOneNoteStyle() const { return FALSE; }
  virtual BOOL IsLeftRightRounded() const { return FALSE; }

  // ----- The layout.
  virtual void GetTabArea(CRect& rectTabAreaTop, CRect& rectTabAreaBottom) const;
  virtual void GetWndArea(CRect& rect) const;
  virtual int GetTabsHeight() const;
  virtual void RecalcLayout();
  virtual void FireChangeActiveTab(int nNewTab);
  virtual BOOL FireChangingActiveTab(int nNewTab);

  // ----- Drawing, by the visual manager.
  virtual void OnDraw(CDC* pDC);
  virtual void OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive);
  virtual void OnFillBackground(CDC* pDC, CRect rect);

protected:
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  // Each kind of tab window lays its tabs out.
  virtual void _LayoutTabs(const CRect& rectTabs);
  int _TabWidth(CDC* pDC, CMFCTabInfo* pTab) const;
  CMFCTabInfo* _Tab(int iTab) const { return iTab >= 0 && iTab < GetTabsNum() ? m_arTabs[iTab] : NULL; }
  void _UpdateWindows();
  CRect _CloseButtonRect() const;

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg LRESULT OnSetFont(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()

  CArray<CMFCTabInfo*, CMFCTabInfo*> m_arTabs;
  int m_iActiveTab, m_iHighlighted, m_iPressed, m_nNextTabID, m_nTabBorderSize, m_nTabsHeight;
  int m_iTabBeforeDrag;
  Location m_location;
  BOOL m_bEnableTabSwap, m_bIsInPlaceEdit, m_bHideSingleTab, m_bActiveTabCloseButton, m_bDrawFrame;
  BOOL m_bAutoDestroyWindow, m_bFlatFrame, m_bIsAutoColor, m_bDragging, m_bCloseButtonPressed;
  HIMAGELIST m_hImageList;
  BOOL m_bOwnImageList;
  CSize m_sizeImage;
  CArray<COLORREF, COLORREF> m_arAutoColors;
  COLORREF m_clrActiveTabBk, m_clrActiveTabFg;
  CRect m_rectTabsArea, m_rectWndArea;
  HFONT m_hFont;
};

#endif  // __AFXBASETABCTRL_H__
