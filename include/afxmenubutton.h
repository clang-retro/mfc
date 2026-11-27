// afxmenubutton.h - CMFCMenuButton, a CMFCButton with a drop-down arrow
// that shows a popup menu: the chosen command is in m_nMenuResult when
// the parent gets BN_CLICKED. With m_bDefaultClick the part left of the
// arrow is an ordinary button (m_nMenuResult 0).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMENUBUTTON_H__
#define __AFXMENUBUTTON_H__

#include <afxbutton.h>

class CMFCMenuButton : public CMFCButton {
  DECLARE_DYNAMIC(CMFCMenuButton)
public:
  CMFCMenuButton();
  virtual ~CMFCMenuButton();

  HMENU m_hMenu;
  int m_nMenuResult;
  BOOL m_bOSMenu;
  BOOL m_bRightArrow;
  BOOL m_bStayPressed;
  BOOL m_bDefaultClick;

  virtual CSize SizeToContent(BOOL bCalcOnly = FALSE);
  virtual BOOL PreTranslateMessage(MSG* pMsg);

protected:
  // Shows m_hMenu at the button and sets m_nMenuResult.
  virtual void OnShowMenu();
  virtual void OnDraw(CDC* pDC, const CRect& rect, UINT uiState);
  virtual void OnClicked();
  CRect _ArrowRect() const;

  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  DECLARE_MESSAGE_MAP()

  BOOL m_bMenuIsActive;
  BOOL m_bClickOnMenu;
};

#endif  // __AFXMENUBUTTON_H__
