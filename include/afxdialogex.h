// afxdialogex.h - CDialogEx, the dialog of MFC 9's Feature Pack: a CDialog
// whose background can be a color or a bitmap (tiled, or in a corner),
// with static text, check boxes and radio buttons drawn over it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDIALOGEX_H__
#define __AFXDIALOGEX_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

class CDialogEx : public CDialog {
  DECLARE_DYNAMIC(CDialogEx)
public:
  enum BackgroundLocation {
    BACKGR_TILE,
    BACKGR_TOPLEFT,
    BACKGR_TOPRIGHT,
    BACKGR_BOTTOMLEFT,
    BACKGR_BOTTOMRIGHT
  };

  CDialogEx();
  CDialogEx(UINT nIDTemplate, CWnd* pParent = NULL);
  CDialogEx(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);
  virtual ~CDialogEx();

  void SetBackgroundColor(COLORREF color, BOOL bRepaint = TRUE);
  void SetBackgroundImage(HBITMAP hBitmap, BackgroundLocation location = BACKGR_TILE,
                          BOOL bAutoDestroy = TRUE, BOOL bRepaint = TRUE);
  BOOL SetBackgroundImage(UINT uiBmpResId, BackgroundLocation location = BACKGR_TILE,
                          BOOL bRepaint = TRUE);

protected:
  HBRUSH m_brBkgr;
  HBITMAP m_hBkgrBitmap;
  CSize m_sizeBkgrBitmap;
  BackgroundLocation m_BkgrLocation;
  BOOL m_bAutoDestroyBmp;

  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

private:
  void _CommonConstruct();
  void _FreeBitmap();
};

#endif  // __AFXDIALOGEX_H__
