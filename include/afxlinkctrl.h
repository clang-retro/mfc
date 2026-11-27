// afxlinkctrl.h - CMFCLinkCtrl, a CMFCButton that shows its text as a
// hyperlink (underlined when hot, another color once visited) with the
// hand cursor, and opens its URL (the text when none is set, after the
// prefix) with the shell when clicked.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXLINKCTRL_H__
#define __AFXLINKCTRL_H__

#include <afxbutton.h>

class CMFCLinkCtrl : public CMFCButton {
  DECLARE_DYNAMIC(CMFCLinkCtrl)
public:
  CMFCLinkCtrl();
  virtual ~CMFCLinkCtrl();

  void SetURL(LPCTSTR lpszURL);
  void SetURLPrefix(LPCTSTR lpszPrefix);
  CString GetURL() const { return m_strURL; }
  CString GetURLPrefix() const { return m_strPrefix; }

  virtual CSize SizeToContent(BOOL bVCenter = FALSE, BOOL bHCenter = FALSE);

  BOOL m_bMultilineText;
  BOOL m_bAlwaysUnderlineText;
  BOOL m_bDefaultClickProcess;
  BOOL m_bVisited;

protected:
  // Opens the URL (unless m_bDefaultClickProcess is FALSE, when the
  // parent gets BN_CLICKED); TRUE when it opened.
  virtual BOOL OpenLink();
  virtual void OnDraw(CDC* pDC, const CRect& rect, UINT uiState);
  virtual void OnDrawFocusRect(CDC* pDC, const CRect& rectClient);
  virtual void OnFillBackground(CDC* pDC, const CRect& rectClient);
  virtual void OnDrawBorder(CDC*, CRect&, UINT) {}
  virtual void OnClicked();
  virtual void PreSubclassWindow();

  CString m_strURL;
  CString m_strPrefix;
};

#endif  // __AFXLINKCTRL_H__
