// afxtabctrl.h - CMFCTabCtrl, the Feature Pack's tab window: tabs in a
// 3D, flat, OneNote, VS 2005 or rounded style, on the top or bottom,
// with scroll buttons when they do not fit, each tab showing its window.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTABCTRL_H__
#define __AFXTABCTRL_H__

#include <afxbasetabctrl.h>

class CMFCTabCtrl : public CMFCBaseTabCtrl {
  DECLARE_DYNCREATE(CMFCTabCtrl)
public:
  enum Style {
    STYLE_3D = 0,
    STYLE_FLAT = 1,
    STYLE_FLAT_SHARED_HORZ_SCROLL = 2,
    STYLE_3D_SCROLLED = 3,
    STYLE_3D_ONENOTE = 4,
    STYLE_3D_VS2005 = 5,
    STYLE_3D_ROUNDED = 6,
    STYLE_3D_ROUNDED_SCROLL = 7,
  };

  CMFCTabCtrl();
  virtual ~CMFCTabCtrl();

  BOOL Create(Style style, const RECT& rect, CWnd* pParentWnd, UINT nID, Location location = LOCATION_BOTTOM,
              BOOL bCloseBtn = FALSE);
  Style GetStyle() const { return m_style; }
  void ModifyTabStyle(Style style);

  virtual BOOL IsFlatTab() const { return m_style == STYLE_FLAT || m_style == STYLE_FLAT_SHARED_HORZ_SCROLL; }
  virtual BOOL IsVS2005Style() const { return m_style == STYLE_3D_VS2005; }
  virtual BOOL IsOneNoteStyle() const { return m_style == STYLE_3D_ONENOTE; }
  virtual BOOL IsLeftRightRounded() const {
    return m_style == STYLE_3D_ROUNDED || m_style == STYLE_3D_ROUNDED_SCROLL;
  }

  // The first tab drawn, when they do not all fit.
  int GetFirstVisibleTabNum() const { return m_nFirstVisibleTab; }
  void SetFirstVisibleTab(int iTab);
  BOOL EnsureVisible(int iTab);
  BOOL HasScrollButtons() const { return m_bScroll; }

  void EnableTabDocumentsMenu(BOOL bEnable = TRUE) { m_bTabDocumentsMenu = bEnable; }
  BOOL IsTabDocumentsMenu() const { return m_bTabDocumentsMenu; }
  void SetActiveTabBoldFont(BOOL bIsBold = TRUE) { m_bIsActiveTabBold = bIsBold; }
  BOOL IsActiveTabBoldFont() const { return m_bIsActiveTabBold; }

  virtual BOOL SetActiveTab(int iTab);
  virtual void OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive);
  virtual void OnDraw(CDC* pDC);
  virtual void RecalcLayout();

protected:
  virtual void _LayoutTabs(const CRect& rectTabs);
  CRect _ScrollRect(int nButton) const;  // 0 left, 1 right

  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  DECLARE_MESSAGE_MAP()

  Style m_style;
  int m_nFirstVisibleTab;
  BOOL m_bScroll, m_bTabDocumentsMenu, m_bIsActiveTabBold;
};

#endif  // __AFXTABCTRL_H__
