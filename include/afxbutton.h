// afxbutton.h - CMFCButton, the Feature Pack's button: owner drawn by the
// visual manager, flat or 3D, with an image (normal, hot, disabled and
// checked ones) to the left, right or above the text, a tooltip, a cursor,
// text and face colors, and check box and radio button behavior kept by
// the button itself.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXBUTTON_H__
#define __AFXBUTTON_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxvisualmanager.h>

class CMFCButton : public CButton {
  DECLARE_DYNCREATE(CMFCButton)
public:
  CMFCButton();
  virtual ~CMFCButton();

  enum FlatStyle { BUTTONSTYLE_3D, BUTTONSTYLE_FLAT, BUTTONSTYLE_SEMIFLAT, BUTTONSTYLE_NOBORDERS };
  enum AlignStyle { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER };

  // ----- The look.
  FlatStyle m_nFlatStyle;
  AlignStyle m_nAlignStyle;
  BOOL m_bRightImage;
  BOOL m_bTopImage;
  BOOL m_bTransparent;
  BOOL m_bDrawFocus;
  BOOL m_bHighlightChecked;
  BOOL m_bGrayDisabled;
  BOOL m_bResponseOnButtonDown;
  BOOL m_bDontUseWinXPTheme;
  BOOL m_bWasDblClk;

  // ----- Images: an icon or bitmap for each state (NULL for none).
  void SetImage(HICON hIcon, BOOL bAutoDestroy = TRUE, HICON hIconHot = NULL, HICON hIconDisabled = NULL,
                BOOL bAlphaBlend = FALSE);
  void SetImage(HBITMAP hBitmap, BOOL bAutoDestroy = TRUE, HBITMAP hBitmapHot = NULL, BOOL bMap3dColors = TRUE,
                HBITMAP hBitmapDisabled = NULL);
  void SetImage(UINT uiBmpResId, UINT uiBmpHotResId = 0, UINT uiBmpDsblResID = 0);
  void SetCheckedImage(HICON hIcon, BOOL bAutoDestroy = TRUE, HICON hIconHot = NULL, HICON hIconDisabled = NULL,
                       BOOL bAlphaBlend = FALSE);
  void SetCheckedImage(HBITMAP hBitmap, BOOL bAutoDestroy = TRUE, HBITMAP hBitmapHot = NULL,
                       BOOL bMap3dColors = TRUE, HBITMAP hBitmapDisabled = NULL);
  void SetCheckedImage(UINT uiBmpResId, UINT uiBmpHotResId = 0, UINT uiBmpDsblResID = 0);
  void SetStdImage(UINT /*type*/, UINT /*uiState*/ = 0, CSize /*sizeImage*/ = CSize(0, 0)) {}
  BOOL HasImage() const { return m_Image[0].IsValid() || m_ImageChecked[0].IsValid(); }
  CSize GetImageSize() const;

  // ----- Tooltip, cursor, colors.
  void SetTooltip(LPCTSTR lpszToolTipText);
  CToolTipCtrl& GetToolTipCtrl() { return m_wndToolTip; }
  void SetMouseCursor(HCURSOR hcursor) { m_hCursor = hcursor; }
  void SetMouseCursorHand();
  void SetTextColor(COLORREF clrText) { m_clrRegular = clrText; }
  void SetTextHotColor(COLORREF clrTextHot) { m_clrHover = clrTextHot; }
  void SetFaceColor(COLORREF crFace, BOOL bRepaint = TRUE);
  void EnableMenuFont(BOOL bOn = TRUE, BOOL bRedraw = TRUE);
  void EnableFullTextTooltip(BOOL bOn = TRUE) { m_bFullTextTooltip = bOn; }
  void EnableWindowsTheming(BOOL bEnable = TRUE) { m_bDontUseWinXPTheme = !bEnable; }
  static void AFXAPI EnableWindowsThemingAll(BOOL bEnable = TRUE) { m_bWinXPThemeAll = bEnable; }

  // The size that fits the image and text (and with !bCalcOnly, resize).
  virtual CSize SizeToContent(BOOL bCalcOnly = FALSE);

  // ----- State.
  BOOL IsPressed() const { return m_bPushed && m_bHighlighted; }
  BOOL IsPushed() const { return m_bPushed; }
  BOOL IsHighlighted() const { return m_bHighlighted; }
  BOOL IsCheckBox() const { return m_bCheckButton; }
  BOOL IsRadioButton() const { return m_bRadioButton; }
  BOOL IsAutoCheck() const { return m_bAutoCheck; }
  BOOL IsChecked() const { return m_bChecked; }
  BOOL IsWindowsThemingEnabled() const { return !m_bDontUseWinXPTheme && m_bWinXPThemeAll; }

  // The check state of a check box or radio button (BM_SETCHECK and
  // BM_GETCHECK go here too, as the owner-drawn button has none).
  void SetCheck(int nCheck);
  int GetCheck() const { return m_bChecked ? (m_bIndeterminate ? BST_INDETERMINATE : BST_CHECKED) : BST_UNCHECKED; }

  virtual void CleanUp();

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
  virtual BOOL PreTranslateMessage(MSG* pMsg);

protected:
  // One image of a state, an icon or bitmap.
  struct CImageSlot {
    HICON hIcon;
    HBITMAP hBitmap;
    BOOL bAutoDestroy;
    CImageSlot() : hIcon(NULL), hBitmap(NULL), bAutoDestroy(FALSE) {}
    BOOL IsValid() const { return hIcon != NULL || hBitmap != NULL; }
    void Free();
    CSize GetSize() const;
    void Draw(CDC* pDC, const CPoint& pt, BOOL bDisabled) const;
  };

  virtual void PreSubclassWindow();
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void OnDraw(CDC* pDC, const CRect& rect, UINT uiState);
  virtual void OnDrawBorder(CDC* pDC, CRect& rectClient, UINT uiState);
  virtual void OnDrawFocusRect(CDC* pDC, const CRect& rectClient);
  virtual void OnDrawText(CDC* pDC, const CRect& rect, const CString& strText, UINT uiDTFlags, UINT uiState);
  virtual void OnFillBackground(CDC* pDC, const CRect& rectClient);
  virtual void OnDrawParentBackground(CDC* pDC, CRect rectClient);
  virtual CFont* SelectFont(CDC* pDC);
  virtual int GetImageHorzMargin() const { return 4; }
  virtual int GetVertMargin() const { return 2; }
  virtual void OnClicked();
  // The check box mark or radio dot, where the button is one.
  virtual void OnDrawCheckMark(CDC* pDC, CRect& rect, UINT uiState);

  void _InitStyle(DWORD dwStyle);
  void _SetImage(CImageSlot* slots, HICON hIcon, BOOL bAutoDestroy, HICON hIconHot, HICON hIconDisabled);
  void _SetImage(CImageSlot* slots, HBITMAP hBitmap, BOOL bAutoDestroy, HBITMAP hBitmapHot,
                 HBITMAP hBitmapDisabled);
  void _UncheckRadioGroup();

  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  afx_msg void OnCaptureChanged(CWnd* pWnd);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnEnable(BOOL bEnable);
  afx_msg void OnKillFocus(CWnd* pNewWnd);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  afx_msg LRESULT OnSetCheck(WPARAM, LPARAM);
  afx_msg LRESULT OnGetCheck(WPARAM, LPARAM);
  afx_msg LRESULT OnSetStyle(WPARAM, LPARAM);
  afx_msg LRESULT OnSetText(WPARAM, LPARAM);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

  CImageSlot m_Image[3];         // normal, hot, disabled
  CImageSlot m_ImageChecked[3];  // the same when checked
  CToolTipCtrl m_wndToolTip;
  HCURSOR m_hCursor;
  COLORREF m_clrRegular, m_clrHover, m_clrFace;
  HBRUSH m_hbrFace;
  BOOL m_bHighlighted, m_bPushed, m_bClickiedInside, m_bCaptured, m_bKeyDown;
  BOOL m_bChecked, m_bIndeterminate, m_bCheckButton, m_bRadioButton, m_bAutoCheck, m_b3State;
  BOOL m_bDefaultButton, m_bMenuFont, m_bFullTextTooltip, m_bInSetStyle;
  CSize m_sizeImage;

  static BOOL m_bWinXPThemeAll;
};

#endif  // __AFXBUTTON_H__
