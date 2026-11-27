// afxmaskededit.h - CMFCMaskedEdit, an edit control that takes input in a
// template: where the template has the template character (often '_'),
// the mask's character there says what may be typed:
//   D a digit            d a digit or space
//   + '+', '-' or space  C a letter           c a letter or space
//   A a letter or digit  a a letter, digit or space
//   * any printable character
// Other template characters are fixed. GetWindowText gives the typed
// characters only, unless EnableGetMaskedCharsOnly(FALSE).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMASKEDEDIT_H__
#define __AFXMASKEDEDIT_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

class CMFCMaskedEdit : public CEdit {
  DECLARE_DYNAMIC(CMFCMaskedEdit)
public:
  CMFCMaskedEdit();
  virtual ~CMFCMaskedEdit();

  void EnableMask(LPCTSTR lpszMask, LPCTSTR lpszInputTemplate, TCHAR chMaskInputTemplate = _T('_'),
                  LPCTSTR lpszValid = NULL);
  void DisableMask();
  void SetValidChars(LPCTSTR lpszValid = NULL);
  void EnableGetMaskedCharsOnly(BOOL bEnable = TRUE) { m_bGetMaskedCharsOnly = bEnable; }
  void EnableSetMaskedCharsOnly(BOOL bEnable = TRUE) { m_bSetMaskedCharsOnly = bEnable; }
  void EnableSelectByGroup(BOOL bEnable = TRUE) { m_bSelectByGroup = bEnable; }

  // The text: all of it, or (by default) the typed characters only. The
  // set takes the whole text, or with EnableSetMaskedCharsOnly the typed
  // characters only; FALSE when it does not fit the mask.
  void GetWindowText(CString& rString) const;
  int GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const;
  BOOL SetWindowText(LPCTSTR lpszString);

  const CString& GetMask() const { return m_strMask; }
  const CString& GetInputTemplate() const { return m_strInputTemplate; }

protected:
  virtual BOOL IsMaskedChar(TCHAR chChar, TCHAR chMaskChar) const;
  BOOL _IsEditable(int nPos) const;
  int _NextEditable(int nPos, int nStep) const;
  CString _GetMaskedValue(const CString& strFull) const;
  BOOL _SetValue(LPCTSTR lpszString, BOOL bMaskedOnly);
  void _Show(int nCaret);
  void _Clear(int nStart, int nEnd);

  afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg LRESULT OnCut(WPARAM, LPARAM);
  afx_msg LRESULT OnClear(WPARAM, LPARAM);
  afx_msg LRESULT OnPaste(WPARAM, LPARAM);
  afx_msg LRESULT OnSetTextMsg(WPARAM, LPARAM);
  afx_msg LRESULT OnGetTextMsg(WPARAM, LPARAM);
  afx_msg LRESULT OnGetTextLength(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()

  CString m_strMask;
  CString m_strInputTemplate;
  TCHAR m_chMaskInputTemplate;
  CString m_strValid;
  CString m_str;  // the whole text as shown
  BOOL m_bGetMaskedCharsOnly, m_bSetMaskedCharsOnly, m_bSelectByGroup, m_bInternal;
};

#endif  // __AFXMASKEDEDIT_H__
