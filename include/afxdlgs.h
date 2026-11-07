// afxdlgs.h - common dialogs and property sheets.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDLGS_H__
#define __AFXDLGS_H__

#include "afxwin.h"
#include <commdlg.h>
#include <prsht.h>
#include <richedit.h>

class CCommonDialog : public CDialog {
  DECLARE_DYNAMIC(CCommonDialog)
public:
  explicit CCommonDialog(CWnd* pParentWnd) : CDialog((UINT)0, pParentWnd) {}
protected:
  virtual void OnOK();
  virtual void OnCancel();
};

// ---------------------------------------------------------------------
// File Open and Save As

class CFileDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CFileDialog)
public:
  OPENFILENAME m_ofn;

  explicit CFileDialog(BOOL bOpenFileDialog, LPCTSTR lpszDefExt = NULL,
                       LPCTSTR lpszFileName = NULL,
                       DWORD dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                       LPCTSTR lpszFilter = NULL, CWnd* pParentWnd = NULL, DWORD dwSize = 0,
                       BOOL bVistaStyle = TRUE);
  virtual ~CFileDialog();

  virtual INT_PTR DoModal();

  CString GetPathName() const;
  CString GetFileName() const;
  CString GetFileExt() const;
  CString GetFileTitle() const;
  CString GetFolderPath() const;
  BOOL GetReadOnlyPref() const { return (m_ofn.Flags & OFN_READONLY) != 0; }
  POSITION GetStartPosition() const { return (POSITION)m_ofn.lpstrFile; }
  CString GetNextPathName(POSITION& pos) const;
  void SetDefExt(LPCSTR lpsz);
  OPENFILENAME& GetOFN() { return m_ofn; }
  const OPENFILENAME& GetOFN() const { return m_ofn; }

  // Called from the dialog's hook (OFN_ENABLEHOOK) when one is set.
  virtual UINT OnShareViolation(LPCTSTR lpszPathName);
  virtual BOOL OnFileNameOK();
  virtual void OnInitDone();
  virtual void OnFileNameChange();
  virtual void OnFolderChange();
  virtual void OnTypeChange();

  BOOL m_bOpenFileDialog;
  CString m_strFilter;
  TCHAR m_szFileTitle[64];
  TCHAR m_szFileName[MAX_PATH];
  CString m_strDefExt;
};

// ---------------------------------------------------------------------
// Color, font, print and page setup

class CColorDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CColorDialog)
public:
  CHOOSECOLOR m_cc;
  explicit CColorDialog(COLORREF clrInit = 0, DWORD dwFlags = 0, CWnd* pParentWnd = NULL);
  virtual INT_PTR DoModal();
  COLORREF GetColor() const { return m_cc.rgbResult; }
  void SetCurrentColor(COLORREF clr);
  static COLORREF* PASCAL GetSavedCustomColors();
  static COLORREF clrSavedCustom[16];
  virtual BOOL OnColorOK() { return FALSE; }
};

class CFontDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CFontDialog)
public:
  CHOOSEFONT m_cf;
  explicit CFontDialog(LPLOGFONT lplfInitial = NULL, DWORD dwFlags = CF_EFFECTS | CF_SCREENFONTS,
                       CDC* pdcPrinter = NULL, CWnd* pParentWnd = NULL);
  // For a rich edit selection: the dialog starts from, and gives back,
  // a CHARFORMAT.
  CFontDialog(const CHARFORMAT& charformat, DWORD dwFlags = CF_SCREENFONTS,
              CDC* pdcPrinter = NULL, CWnd* pParentWnd = NULL);
  virtual INT_PTR DoModal();
  void GetCurrentFont(LPLOGFONT lplf);
  void GetCharFormat(CHARFORMAT& cf) const;
  DWORD FillInLogFont(const CHARFORMAT& cf);
  CString GetFaceName() const { return m_lf.lfFaceName; }
  CString GetStyleName() const { return m_szStyleName; }
  int GetSize() const { return m_cf.iPointSize; }
  COLORREF GetColor() const { return m_cf.rgbColors; }
  int GetWeight() const { return (int)m_lf.lfWeight; }
  BOOL IsStrikeOut() const { return m_lf.lfStrikeOut != 0; }
  BOOL IsUnderline() const { return m_lf.lfUnderline != 0; }
  BOOL IsBold() const { return m_lf.lfWeight == FW_BOLD; }
  BOOL IsItalic() const { return m_lf.lfItalic != 0; }

  LOGFONT m_lf;
  TCHAR m_szStyleName[64];
};

class CPrintDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CPrintDialog)
public:
  PRINTDLG& m_pd;
  explicit CPrintDialog(BOOL bPrintSetupOnly,
                        DWORD dwFlags = PD_ALLPAGES | PD_USEDEVMODECOPIES | PD_NOPAGENUMS |
                                        PD_HIDEPRINTTOFILE | PD_NOSELECTION,
                        CWnd* pParentWnd = NULL);
  virtual ~CPrintDialog();

  virtual INT_PTR DoModal();
  BOOL GetDefaults();
  int GetCopies() const;
  BOOL PrintCollate() const { return (m_pd.Flags & PD_COLLATE) != 0; }
  BOOL PrintSelection() const { return (m_pd.Flags & PD_SELECTION) != 0; }
  BOOL PrintAll() const { return !PrintRange() && !PrintSelection(); }
  BOOL PrintRange() const { return (m_pd.Flags & PD_PAGENUMS) != 0; }
  BOOL PrintToFile() const { return (m_pd.Flags & PD_PRINTTOFILE) != 0; }
  int GetFromPage() const { return PrintRange() ? m_pd.nFromPage : -1; }
  int GetToPage() const { return PrintRange() ? m_pd.nToPage : -1; }
  LPDEVMODE GetDevMode() const;
  CString GetDriverName() const;
  CString GetDeviceName() const;
  CString GetPortName() const;
  HDC GetPrinterDC() const;
  HDC CreatePrinterDC();

  PRINTDLG m_pdActual;
};

class CPageSetupDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CPageSetupDialog)
public:
  PAGESETUPDLG m_psd;
  explicit CPageSetupDialog(DWORD dwFlags = PSD_MARGINS | PSD_INWININIINTLMEASURE,
                            CWnd* pParentWnd = NULL);
  virtual ~CPageSetupDialog();
  virtual INT_PTR DoModal();
  LPDEVMODE GetDevMode() const;
  CString GetDriverName() const;
  CString GetDeviceName() const;
  CString GetPortName() const;
  HDC CreatePrinterDC();
  CSize GetPaperSize() const { return CSize(m_psd.ptPaperSize.x, m_psd.ptPaperSize.y); }
  void GetMargins(LPRECT lpRectMargins, LPRECT lpRectMinMargins) const;
};

// ---------------------------------------------------------------------
// Find and Replace (modeless; allocate with new, it deletes itself)

class CFindReplaceDialog : public CCommonDialog {
  DECLARE_DYNAMIC(CFindReplaceDialog)
public:
  FINDREPLACE m_fr;
  CFindReplaceDialog();
  virtual BOOL Create(BOOL bFindDialogOnly, LPCTSTR lpszFindWhat, LPCTSTR lpszReplaceWith = NULL,
                      DWORD dwFlags = FR_DOWN, CWnd* pParentWnd = NULL);
  static CFindReplaceDialog* PASCAL GetNotifier(LPARAM lParam);

  CString GetReplaceString() const { return m_fr.lpstrReplaceWith; }
  CString GetFindString() const { return m_fr.lpstrFindWhat; }
  BOOL SearchDown() const { return (m_fr.Flags & FR_DOWN) != 0; }
  BOOL FindNext() const { return (m_fr.Flags & FR_FINDNEXT) != 0; }
  BOOL MatchCase() const { return (m_fr.Flags & FR_MATCHCASE) != 0; }
  BOOL MatchWholeWord() const { return (m_fr.Flags & FR_WHOLEWORD) != 0; }
  BOOL ReplaceCurrent() const { return (m_fr.Flags & FR_REPLACE) != 0; }
  BOOL ReplaceAll() const { return (m_fr.Flags & FR_REPLACEALL) != 0; }
  BOOL IsTerminating() const { return (m_fr.Flags & FR_DIALOGTERM) != 0; }

protected:
  virtual void PostNcDestroy();
  TCHAR m_szFindWhat[128];
  TCHAR m_szReplaceWith[128];
};

// ---------------------------------------------------------------------
// Property sheets

class CPropertySheet;
class CTabCtrl;

class CPropertyPage : public CDialog {
  DECLARE_DYNAMIC(CPropertyPage)
public:
  PROPSHEETPAGE m_psp;

  CPropertyPage();
  explicit CPropertyPage(UINT nIDTemplate, UINT nIDCaption = 0, DWORD dwSize = 0);
  explicit CPropertyPage(LPCTSTR lpszTemplateName, UINT nIDCaption = 0, DWORD dwSize = 0);
  virtual ~CPropertyPage();
  void Construct(UINT nIDTemplate, UINT nIDCaption = 0);
  void Construct(LPCTSTR lpszTemplateName, UINT nIDCaption = 0);

  PROPSHEETPAGE& GetPSP() { return m_psp; }
  void CancelToClose();
  void SetModified(BOOL bChanged = TRUE);
  LRESULT QuerySiblings(WPARAM wParam, LPARAM lParam);

  virtual BOOL OnApply();
  virtual void OnReset();
  virtual void OnOK();
  virtual void OnCancel();
  virtual BOOL OnSetActive();
  virtual BOOL OnKillActive();
  virtual BOOL OnQueryCancel();
  virtual LRESULT OnWizardBack();
  virtual LRESULT OnWizardNext();
  virtual BOOL OnWizardFinish();
  virtual HWND OnWizardFinishEx();

  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual BOOL PreTranslateMessage(MSG* pMsg);

  CString m_strCaption;
  BOOL m_bFirstSetActive;
  CPropertySheet* GetParentSheet();

  DECLARE_MESSAGE_MAP()
};

class CPropertySheet : public CWnd {
  DECLARE_DYNAMIC(CPropertySheet)
public:
  PROPSHEETHEADER m_psh;

  CPropertySheet();
  explicit CPropertySheet(UINT nIDCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);
  explicit CPropertySheet(LPCTSTR pszCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);
  virtual ~CPropertySheet();
  void Construct(UINT nIDCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);
  void Construct(LPCTSTR pszCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);

  virtual INT_PTR DoModal();
  virtual BOOL Create(CWnd* pParentWnd = NULL, DWORD dwStyle = (DWORD)-1, DWORD dwExStyle = 0);

  void AddPage(CPropertyPage* pPage);
  void RemovePage(CPropertyPage* pPage);
  void RemovePage(int nPage);
  int GetPageCount() const;
  CPropertyPage* GetPage(int nPage) const;
  int GetPageIndex(CPropertyPage* pPage);
  CPropertyPage* GetActivePage() const;
  int GetActiveIndex() const;
  BOOL SetActivePage(int nPage);
  BOOL SetActivePage(CPropertyPage* pPage);
  void SetTitle(LPCTSTR lpszText, UINT nStyle = 0);
  CTabCtrl* GetTabControl() const;
  void SetWizardMode();
  void SetWizardButtons(DWORD dwFlags);
  void SetFinishText(LPCTSTR lpszText);
  void EnableStackedTabs(BOOL bStacked) { m_bStacked = bStacked; }
  void PressButton(int nButton);
  void EndDialog(int nEndID);
  BOOL IsWizard() const { return (m_psh.dwFlags & (PSH_WIZARD | 0x00004000)) != 0; }
  PROPSHEETHEADER& GetPSH() { return m_psh; }

  virtual BOOL OnInitDialog();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL ContinueModal();
  virtual void PostNcDestroy();
  virtual void BuildPropPageArray();

  CPtrList m_pages;
  CString m_strCaption;
  CWnd* m_pParentWnd;
  BOOL m_bStacked;
  BOOL m_bModeless;
  int m_nModalResult;  // the button that closed the modal sheet
  BOOL m_bEndDialogCalled;
  afx_msg LRESULT HandleInitDialog(WPARAM, LPARAM);
  HPROPSHEETPAGE* m_phpage;

  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXDLGS_H__
