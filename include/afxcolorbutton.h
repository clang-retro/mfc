// afxcolorbutton.h - CMFCColorButton, a CMFCButton that shows a color and
// picks another from a popup palette (the standard colors, or a palette
// of the application's, in m_nColumns columns), with an "Automatic" and an
// "Other..." button (the color dialog) when enabled. The parent gets
// BN_CLICKED when the color changes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXCOLORBUTTON_H__
#define __AFXCOLORBUTTON_H__

#include <afxbutton.h>
#include <afxtempl.h>

class CMFCColorPopupWnd_;

class CMFCColorButton : public CMFCButton {
  DECLARE_DYNAMIC(CMFCColorButton)
  friend class CMFCColorPopupWnd_;

public:
  CMFCColorButton();
  virtual ~CMFCColorButton();

  COLORREF GetColor() const { return m_Color; }
  void SetColor(COLORREF color);
  COLORREF GetAutomaticColor() const { return m_ColorAutomatic; }

  void SetPalette(CPalette* pPalette);
  void SetColumnsNumber(int nColumns) { m_nColumns = nColumns; }
  void EnableAutomaticButton(LPCTSTR lpszLabel, COLORREF colorAutomatic, BOOL bEnable = TRUE);
  void EnableOtherButton(LPCTSTR lpszLabel, BOOL bAltColorDlg = TRUE, BOOL bEnable = TRUE);
  void SetDocumentColors(LPCTSTR lpszLabel, CList<COLORREF, COLORREF>& lstColors);

  static void AFXAPI SetColorName(COLORREF color, const CString& strName);
  static BOOL AFXAPI GetColorName(COLORREF color, CString& strName);

  // The palette's colors (the standard 40 when none was set).
  const CArray<COLORREF, COLORREF>& GetColors() const { return m_Colors; }

  BOOL m_bEnabledInCustomizeMode;
  BOOL m_bAutoSetFocus;
  BOOL m_bAltColorDlg;

  virtual CSize SizeToContent(BOOL bCalcOnly = FALSE);

protected:
  virtual void UpdateColor(COLORREF color);
  virtual void OnShowColorPopup();
  virtual void OnDraw(CDC* pDC, const CRect& rect, UINT uiState);
  virtual void OnDrawBorder(CDC* pDC, CRect& rectClient, UINT uiState);
  virtual void OnDrawFocusRect(CDC* pDC, const CRect& rectClient);
  virtual void OnClicked();
  // The color dialog for "Other..."; TRUE when a color was chosen.
  virtual BOOL OnOtherColor(COLORREF& color);
  void _InitColors();

  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  DECLARE_MESSAGE_MAP()

  COLORREF m_Color;
  COLORREF m_ColorAutomatic;
  CArray<COLORREF, COLORREF> m_Colors;
  CList<COLORREF, COLORREF> m_lstDocColors;
  int m_nColumns;
  CString m_strAutoColorText, m_strOtherText, m_strDocColorsText;
  BOOL m_bAutoColor, m_bOtherColor;
  CPalette* m_pPalette;
  CMFCColorPopupWnd_* m_pPopup;
};

#endif  // __AFXCOLORBUTTON_H__
