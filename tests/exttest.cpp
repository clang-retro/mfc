// exttest.cpp - an MFC extension DLL (exttest-dll), with the program and
// the DLL both using the shared library (mfc.dll):
//   - the DLL's document and view classes make the program's document
//     template, and the DLL's message map takes the frame's command;
//   - CRuntimeClass::FromName and CArchive find the DLL's serializable
//     class;
//   - strings and a dialog template come from the DLL's resources,
//     through AfxFindResourceHandle, and the program's own from the
//     program;
//   - AfxGetApp in the DLL is the program's application;
//   - an exception thrown in one module is caught in the other;
//   - objects and memory made in one module are freed in the other, and
//     a CStringArray grows in both.
#include <afxwin.h>
#include <afxext.h>
#include "exttest.h"
#include "testutil.h"

static void ThrowFile() { AfxThrowFileException(CFileException::accessDenied); }

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep;
  CExtDialog m_dlg;
  CMainFrame() : m_nStep(0) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  void CheckDocument() {
    CExtDoc* pDoc = (CExtDoc*)GetActiveDocument();
    CHECK(pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(CExtDoc)));
    CView* pView = GetActiveView();
    CHECK(pView != NULL && pView->IsKindOf(RUNTIME_CLASS(CExtView)));
    if (pDoc == NULL || pView == NULL) return;
    CHECK(pDoc->m_strText == _T("A document of the DLL"));
    // The array grows in the program and in the DLL.
    pDoc->m_lines.Add(_T("from the program"));
    ExtAddLine(pDoc, _T("from the DLL again"));
    CHECK(pDoc->m_lines.GetSize() == 3);
    if (pDoc->m_lines.GetSize() == 3) CHECK(pDoc->m_lines[2] == _T("from the DLL again"));
    pView->Invalidate();
    // The frame routes the command to the view's message map, in the DLL.
    SendMessage(WM_COMMAND, ID_EXT_COMMAND);
    CHECK(((CExtView*)pView)->m_nCommands == 1);
  }

  void CheckDialog() {
    // The template is in the DLL's resources.
    HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(IDD_EXTDLG), RT_DIALOG);
    CHECK(hInst == ExtGetModule());
    CHECK(m_dlg.Create(this));
    if (m_dlg.GetSafeHwnd() == NULL) return;
    CHECK(m_dlg.m_strText == _T("The DLL's dialog"));
    m_dlg.SetWindowPos(NULL, 10, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    m_dlg.ShowWindow(SW_SHOW);
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    switch (m_nStep++) {
      case 0:
        CheckDocument();
        CheckDialog();
        break;
      case 1:
        TestSaveWindowBitmap(m_hWnd, _T("exttest.bmp"));
        m_dlg.DestroyWindow();
        break;
      default:
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

static void CheckModules() {
  CHECK(ExtGetModule() != NULL && ExtGetModule() != AfxGetInstanceHandle());
  CHECK(ExtGetApp() == AfxGetApp());

  // Classes. (FromName knows the serializable ones.)
  CHECK(CRuntimeClass::FromName("CExtItem") == RUNTIME_CLASS(CExtItem));
  CHECK(RUNTIME_CLASS(CExtView)->IsDerivedFrom(RUNTIME_CLASS(CView)));
  CMemFile file;
  {
    CArchive ar(&file, CArchive::store);
    CExtItem item;
    item.m_n = 7;
    ar << &item;
  }
  file.SeekToBegin();
  {
    CArchive ar(&file, CArchive::load);
    CObject* pOb = NULL;
    TRY { ar >> pOb; }
    CATCH_ALL(e) {
      TestLog("reading the DLL's object failed");
      g_nFailures++;
    }
    END_CATCH_ALL
    CHECK(pOb != NULL && pOb->IsKindOf(RUNTIME_CLASS(CExtItem)));
    if (pOb != NULL && pOb->IsKindOf(RUNTIME_CLASS(CExtItem))) CHECK(((CExtItem*)pOb)->m_n == 7);
    delete pOb;
  }

  // Strings: the DLL's, from the program and from the DLL, and the
  // program's, from the DLL.
  CString str;
  CHECK(str.LoadString(IDS_EXTSTRING) && str == _T("From the DLL"));
  CHECK(ExtLoadString(IDS_EXTSTRING) == _T("From the DLL"));
  CHECK(ExtLoadString(IDS_APPSTRING) == _T("From the program"));
  CHECK(AfxFindStringResourceHandle(IDS_APPSTRING) == AfxGetInstanceHandle());
  CHECK(AfxFindStringResourceHandle(IDS_EXTSTRING) == ExtGetModule());

  // Exceptions.
  BOOL bCaught = FALSE;
  try {
    ExtThrowUser();
  } catch (CUserException* e) {
    bCaught = TRUE;
    e->Delete();
  }
  CHECK(bCaught);
  CHECK(ExtCatchFile(ThrowFile) == CFileException::accessDenied);

  // Memory.
  CObject* pOb = ExtNewObject();
  CHECK(pOb != NULL && pOb->IsKindOf(RUNTIME_CLASS(CObList)));
  delete pOb;
  ExtDeleteObject(new CStringList);
  char* psz = ExtNewChars("abc");
  CHECK(strcmp(psz, "abc") == 0);
  delete[] psz;
  ExtDeleteChars(new char[8]);
}

class CExtApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("exttest.log"));
    CheckModules();
    CSingleDocTemplate* pTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME, RUNTIME_CLASS(CExtDoc), RUNTIME_CLASS(CMainFrame), RUNTIME_CLASS(CExtView));
    AddDocTemplate(pTemplate);
    OnFileNew();
    if (m_pMainWnd == NULL) {
      TestLog("OnFileNew failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->SetWindowPos(NULL, 0, 0, 400, 260, SWP_NOMOVE | SWP_NOZORDER);
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CExtApp theApp;
