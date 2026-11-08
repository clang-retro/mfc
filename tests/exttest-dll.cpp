// exttest-dll.cpp - exttest's MFC extension DLL: classes the program
// uses as its own, resources it finds through the library, and memory
// and exceptions that cross from one module to the other.
#define _AFXEXT
#include <afxwin.h>
#include <afxext.h>
#include "exttest.h"

static AFX_EXTENSION_MODULE extDLL = {NULL, NULL};

extern "C" int APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
  if (dwReason == DLL_PROCESS_ATTACH) {
    if (!AfxInitExtensionModule(extDLL, hInstance)) return 0;
    new CDynLinkLibrary(extDLL);
  } else if (dwReason == DLL_PROCESS_DETACH) {
    AfxTermExtensionModule(extDLL);
  }
  return 1;
}

IMPLEMENT_DYNCREATE(CExtDoc, CDocument)

BOOL CExtDoc::OnNewDocument() {
  if (!CDocument::OnNewDocument()) return FALSE;
  m_strText.LoadString(IDS_EXTDOCTEXT);
  m_lines.RemoveAll();
  m_lines.Add(_T("from the DLL"));
  return TRUE;
}

IMPLEMENT_DYNCREATE(CExtView, CView)
BEGIN_MESSAGE_MAP(CExtView, CView)
  ON_COMMAND(ID_EXT_COMMAND, OnExtCommand)
END_MESSAGE_MAP()

CExtView::CExtView() : m_nCommands(0) {}

void CExtView::OnDraw(CDC* pDC) {
  CExtDoc* pDoc = (CExtDoc*)GetDocument();
  pDC->TextOut(10, 10, pDoc->m_strText);
  for (INT_PTR i = 0; i < pDoc->m_lines.GetSize(); i++)
    pDC->TextOut(10, 30 + 20 * (int)i, pDoc->m_lines[i]);
}

void CExtView::OnExtCommand() { m_nCommands++; }

CExtDialog::CExtDialog() : CDialog() {}

BOOL CExtDialog::Create(CWnd* pParent) { return CDialog::Create(IDD, pParent); }

BOOL CExtDialog::OnInitDialog() {
  CDialog::OnInitDialog();
  GetDlgItemText(IDC_EXTTEXT, m_strText);
  return TRUE;
}

IMPLEMENT_SERIAL(CExtItem, CObject, 1)

void CExtItem::Serialize(CArchive& ar) {
  if (ar.IsStoring())
    ar << (LONG)m_n;
  else {
    LONG n;
    ar >> n;
    m_n = (int)n;
  }
}

HMODULE ExtGetModule() { return extDLL.hModule; }
CWinApp* ExtGetApp() { return AfxGetApp(); }

CString ExtLoadString(UINT nID) {
  CString str;
  str.LoadString(nID);
  return str;
}

void ExtThrowUser() { AfxThrowUserException(); }

int ExtCatchFile(void (*pfn)()) {
  int nCause = -1;
  try {
    pfn();
  } catch (CFileException* e) {
    nCause = e->m_cause;
    e->Delete();
  }
  return nCause;
}

CObject* ExtNewObject() { return new CObList; }
void ExtDeleteObject(CObject* p) { delete p; }

char* ExtNewChars(const char* psz) {
  char* p = new char[strlen(psz) + 1];
  strcpy(p, psz);
  return p;
}

void ExtDeleteChars(char* p) { delete[] p; }

void ExtAddLine(CExtDoc* pDoc, LPCTSTR lpsz) { pDoc->m_lines.Add(lpsz); }
