// exttest.h - what exttest and its extension DLL share: resource IDs and
// the DLL's classes and functions.
#pragma once

#define IDR_MAINFRAME 128
#define IDS_APPSTRING 129    // in the program
#define IDS_EXTSTRING 0x231  // in the DLL; its block of 16 is the DLL's only
#define IDS_EXTDOCTEXT 0x232  // in the DLL
#define IDD_EXTDLG 300       // in the DLL
#define IDC_EXTTEXT 1001
#define ID_EXT_COMMAND 32800

#ifndef RC_INVOKED

// A document, a view and a dialog that live in the DLL.
class AFX_EXT_CLASS CExtDoc : public CDocument {
  DECLARE_DYNCREATE(CExtDoc)
public:
  CString m_strText;
  CStringArray m_lines;  // grown by the DLL and by the program
  virtual BOOL OnNewDocument();
};

class AFX_EXT_CLASS CExtView : public CView {
  DECLARE_DYNCREATE(CExtView)
public:
  int m_nCommands;
  CExtView();
  virtual void OnDraw(CDC* pDC);
  afx_msg void OnExtCommand();
  DECLARE_MESSAGE_MAP()
};

class AFX_EXT_CLASS CExtDialog : public CDialog {
public:
  enum { IDD = IDD_EXTDLG };
  CString m_strText;
  CExtDialog();
  BOOL Create(CWnd* pParent);
  virtual BOOL OnInitDialog();
};

// An object to write to an archive and read back.
class AFX_EXT_CLASS CExtItem : public CObject {
  DECLARE_SERIAL(CExtItem)
public:
  int m_n;
  CExtItem() : m_n(0) {}
  virtual void Serialize(CArchive& ar);
};

AFX_EXT_API HMODULE ExtGetModule();
AFX_EXT_API CWinApp* ExtGetApp();
AFX_EXT_API CString ExtLoadString(UINT nID);
AFX_EXT_API void ExtThrowUser();
// Calls pfn, which throws a CFileException, and returns its m_cause.
AFX_EXT_API int ExtCatchFile(void (*pfn)());
AFX_EXT_API CObject* ExtNewObject();
AFX_EXT_API void ExtDeleteObject(CObject* p);
AFX_EXT_API char* ExtNewChars(const char* psz);
AFX_EXT_API void ExtDeleteChars(char* p);
AFX_EXT_API void ExtAddLine(CExtDoc* pDoc, LPCTSTR lpsz);

#endif
