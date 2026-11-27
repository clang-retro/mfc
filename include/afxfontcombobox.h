// afxfontcombobox.h - CMFCFontComboBox, a combo box of the system's fonts
// (of the types, character set and pitch Setup asks for), each drawn with
// a mark of its type and (m_bDrawUsingFont) in its own face; CMFCFontInfo
// describes one.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXFONTCOMBOBOX_H__
#define __AFXFONTCOMBOBOX_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

class CMFCFontInfo : public CObject {
public:
  CMFCFontInfo(LPCTSTR lpszName, LPCTSTR lpszScript, BYTE nCharSet, BYTE nPitchAndFamily, int nType)
      : m_strName(lpszName), m_strScript(lpszScript), m_nCharSet(nCharSet),
        m_nPitchAndFamily(nPitchAndFamily), m_nType(nType) {}
  CMFCFontInfo(const CMFCFontInfo& src)
      : CObject(), m_strName(src.m_strName), m_strScript(src.m_strScript), m_nCharSet(src.m_nCharSet),
        m_nPitchAndFamily(src.m_nPitchAndFamily), m_nType(src.m_nType) {}
  // The name with the script in parentheses, when it has one.
  CString GetFullName() const {
    return m_strScript.IsEmpty() ? m_strName : m_strName + _T(" (") + m_strScript + _T(")");
  }

  const CString m_strName;
  const CString m_strScript;
  const BYTE m_nCharSet;
  const BYTE m_nPitchAndFamily;
  const int m_nType;
};

class CMFCFontComboBox : public CComboBox {
  DECLARE_DYNAMIC(CMFCFontComboBox)
public:
  CMFCFontComboBox();
  virtual ~CMFCFontComboBox();

  BOOL Setup(int nFontType = DEVICE_FONTTYPE | RASTER_FONTTYPE | TRUETYPE_FONTTYPE,
             BYTE nCharSet = DEFAULT_CHARSET, BYTE nPitchAndFamily = DEFAULT_PITCH);
  BOOL SelectFont(CMFCFontInfo* pDesc);
  BOOL SelectFont(LPCTSTR lpszName, BYTE nCharSet = DEFAULT_CHARSET);
  CMFCFontInfo* GetSelFont() const;

  static BOOL m_bDrawUsingFont;

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
  virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMIS);
  virtual int CompareItem(LPCOMPAREITEMSTRUCT lpCIS);
  virtual void DeleteItem(LPDELETEITEMSTRUCT lpDIS);

protected:
  virtual void PreSubclassWindow();
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  void _AddFont(const LOGFONT* plf, int nType, LPCTSTR lpszScript);
  static int CALLBACK _EnumProc(const LOGFONT* plf, const TEXTMETRIC* ptm, DWORD nType, LPARAM lParam);
  void _Clear();

  int m_nFontType;
  BYTE m_nCharSet, m_nPitchAndFamily;
  int m_nImageHeight;
};

#endif  // __AFXFONTCOMBOBOX_H__
