// afxspinbuttonctrl.h - CMFCSpinButtonCtrl, an up-down control drawn by
// the visual manager.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSPINBUTTONCTRL_H__
#define __AFXSPINBUTTONCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxvisualmanager.h>

class CMFCSpinButtonCtrl : public CSpinButtonCtrl {
  DECLARE_DYNAMIC(CMFCSpinButtonCtrl)
public:
  CMFCSpinButtonCtrl();
  virtual ~CMFCSpinButtonCtrl();

protected:
  virtual void OnDraw(CDC* pDC);

  afx_msg void OnPaint();
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnCancelMode();
  DECLARE_MESSAGE_MAP()

  BOOL m_bIsButtonPressedUp, m_bIsButtonPressedDown, m_bIsButtonHighligtedUp, m_bIsButtonHighligtedDown;
  BOOL m_bTracked;
};

#endif  // __AFXSPINBUTTONCTRL_H__
