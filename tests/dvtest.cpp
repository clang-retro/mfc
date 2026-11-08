// dvtest.cpp - a CDaoRecordView over the in-memory DAO stand-in: the
// form shows the current record, the record commands move through the
// recordset and write edits back, null fields show as empty, and the
// commands are enabled at the right ends.
#include <afxwin.h>
#include <afxext.h>
#include <afxdao.h>
#include "daomock.h"
#include "dvtest.h"
#include "testutil.h"

static const LPCTSTR kFile = _T("C:\\dvtest.mdb");

static void SetUpTable() {
  CDaoDatabase db;
  db.Create(kFile);
  CDaoTableDef td(&db);
  td.Create(_T("people"));
  td.CreateField(_T("ID"), dbLong, 4, dbAutoIncrField);
  td.CreateField(_T("NAME"), dbText, 30, dbVariableField);
  td.CreateField(_T("AGE"), dbLong, 4);
  td.CreateField(_T("ACTIVE"), dbBoolean, 1);
  td.Append();
  td.Close();
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE], [ACTIVE]) VALUES ('Ada', 36, TRUE)"));
  db.Execute(_T("INSERT INTO [people] ([NAME], [ACTIVE]) VALUES ('Brian', FALSE)"));
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE]) VALUES ('Carol', 29)"));
  db.Close();
}

// A cell of the table, as the engine holds it.
static const COleVariant& Cell(int nRow, int nCol) {
  return daomock::G().Find(kFile)->Find(_T("people"))->rows[nRow].v[nCol];
}

static CString CellText(int nRow, int nCol) { return daomock::VarString(Cell(nRow, nCol)); }

class CPeopleSet : public CDaoRecordset {
public:
  long m_ID;
  CString m_Name;
  long m_Age;
  BOOL m_Active;
  CPeopleSet() : CDaoRecordset(NULL), m_ID(0), m_Age(0), m_Active(FALSE) {
    m_nFields = 4;
    m_nDefaultType = dbOpenDynaset;
  }
  virtual CString GetDefaultDBName() { return kFile; }
  virtual CString GetDefaultSQL() { return _T("[people]"); }
  virtual void DoFieldExchange(CDaoFieldExchange* pFX) {
    pFX->SetFieldType(CDaoFieldExchange::outputColumn);
    DFX_Long(pFX, _T("[ID]"), m_ID);
    DFX_Text(pFX, _T("[NAME]"), m_Name);
    DFX_Long(pFX, _T("[AGE]"), m_Age);
    DFX_Bool(pFX, _T("[ACTIVE]"), m_Active);
  }
};

class CDvDoc : public CDocument {
  DECLARE_DYNCREATE(CDvDoc)
public:
  CPeopleSet m_set;
};
IMPLEMENT_DYNCREATE(CDvDoc, CDocument)

class CPeopleView : public CDaoRecordView {
  DECLARE_DYNCREATE(CPeopleView)
public:
  CPeopleSet* m_pSet;
  CPeopleView() : CDaoRecordView(IDD_RECORD), m_pSet(NULL) {}
  virtual CDaoRecordset* OnGetRecordset() {
    if (m_pSet == NULL) m_pSet = &((CDvDoc*)GetDocument())->m_set;
    return m_pSet;
  }
  virtual void DoDataExchange(CDataExchange* pDX) {
    CDaoRecordView::DoDataExchange(pDX);
    DDX_FieldText(pDX, IDC_ID, m_pSet->m_ID, m_pSet);
    DDX_FieldText(pDX, IDC_NAME, m_pSet->m_Name, m_pSet);
    DDX_FieldText(pDX, IDC_AGE, m_pSet->m_Age, m_pSet);
    DDX_FieldCheck(pDX, IDC_ACTIVE, m_pSet->m_Active, m_pSet);
  }
  virtual void OnInitialUpdate() {
    OnGetRecordset();
    CDaoRecordView::OnInitialUpdate();
  }
};
IMPLEMENT_DYNCREATE(CPeopleView, CDaoRecordView)

class CTestCmdUI : public CCmdUI {
public:
  BOOL m_bEnabled;
  CTestCmdUI() : m_bEnabled(-1) {}
  virtual void Enable(BOOL bOn) { m_bEnabled = bOn; }
};

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep;
  BOOL m_bSawMessageBox;
  BOOL m_bBusy;
  CMainFrame() : m_nStep(0), m_bSawMessageBox(FALSE), m_bBusy(FALSE) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  CPeopleView* View() { return (CPeopleView*)GetActiveView(); }

  CString Text(int nID) {
    CString str;
    View()->GetDlgItemText(nID, str);
    return str;
  }

  void SetText(int nID, LPCTSTR psz) { View()->SetDlgItemText(nID, psz); }

  int Check() { return (int)View()->SendDlgItemMessage(IDC_ACTIVE, BM_GETCHECK); }

  // Whether the view enables a command.
  BOOL Enabled(UINT nID) {
    CTestCmdUI ui;
    ui.m_nID = nID;
    ui.DoUpdate(View(), FALSE);
    return ui.m_bEnabled == TRUE;
  }

  void Command(UINT nID) { SendMessage(WM_COMMAND, nID); }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == 2) {
      // Close the message box this thread has up.
      HWND hBox = ::GetLastActivePopup(m_hWnd);
      if (hBox != NULL && hBox != m_hWnd) {
        m_bSawMessageBox = TRUE;
        ::PostMessage(hBox, WM_COMMAND, IDOK, 0);
        KillTimer(2);
      }
      return;
    }
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    // A message box runs its own loop, so a step can be re-entered.
    if (m_bBusy) return;
    m_bBusy = TRUE;
    try {
      Step();
    } catch (CDaoException* e) {
      TCHAR sz[256];
      e->GetErrorMessage(sz, 256);
      TestLog("step %d: DAO exception %s", m_nStep - 1, (LPCSTR)CStringA(sz));
      g_nFailures++;
      e->Delete();
      m_nStep = 100;
    }
    m_bBusy = FALSE;
  }

  void Step() {
    CPeopleView* pView = View();
    CPeopleSet* pSet = pView->m_pSet;
    switch (m_nStep++) {
      case 0: {
        CHECK(pView->IsKindOf(RUNTIME_CLASS(CDaoRecordView)));
        CHECK(pSet != NULL && pSet->IsOpen());
        CHECK(pSet->m_pDatabase != NULL && pSet->m_pDatabase->IsOpen());
        // The first record is on the form.
        CHECK(Text(IDC_ID) == _T("1"));
        CHECK(Text(IDC_NAME) == _T("Ada"));
        CHECK(Text(IDC_AGE) == _T("36"));
        CHECK(Check() == BST_CHECKED);
        CHECK(pView->IsOnFirstRecord());
        CHECK(!pView->IsOnLastRecord());
        CHECK(!Enabled(ID_RECORD_FIRST) && !Enabled(ID_RECORD_PREV));
        CHECK(Enabled(ID_RECORD_NEXT) && Enabled(ID_RECORD_LAST));
        TestSaveWindowBitmap(m_hWnd, _T("dvtest.bmp"));

        // An edit on the form is written when the view moves on.
        SetText(IDC_NAME, _T("Ada L."));
        Command(ID_RECORD_NEXT);
        CHECK(CellText(0, 1) == _T("Ada L."));
        CHECK(Text(IDC_ID) == _T("2"));
        CHECK(Text(IDC_NAME) == _T("Brian"));
        CHECK(Text(IDC_AGE) == _T(""));  // null
        CHECK(Check() == BST_UNCHECKED);
        CHECK(Enabled(ID_RECORD_FIRST) && Enabled(ID_RECORD_PREV));
        break;
      }
      case 1: {
        // Moving on without a change writes nothing.
        int nWrites = daomock::G().fieldWrites;
        Command(ID_RECORD_NEXT);
        CHECK(daomock::G().fieldWrites == nWrites);
        CHECK(Cell(1, 2).vt == VT_NULL);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        CHECK(Check() == BST_INDETERMINATE);  // a null check box
        TestSaveWindowBitmap(m_hWnd, _T("dvtest-carol.bmp"));

        // The view knows the last record from the start, so Next and
        // Last are off here, and a disabled command does nothing.
        CHECK(pView->IsOnLastRecord());
        CHECK(!Enabled(ID_RECORD_NEXT) && !Enabled(ID_RECORD_LAST));
        CHECK(Enabled(ID_RECORD_PREV) && Enabled(ID_RECORD_FIRST));
        Command(ID_RECORD_NEXT);
        CHECK(Text(IDC_NAME) == _T("Carol"));

        // Clearing a field stores null.
        SetText(IDC_AGE, _T(""));
        Command(ID_RECORD_PREV);
        CHECK(Cell(2, 2).vt == VT_NULL);
        CHECK(Text(IDC_NAME) == _T("Brian"));
        CHECK(!pView->IsOnFirstRecord() && !pView->IsOnLastRecord());
        break;
      }
      case 2: {
        // A typed number goes in as a value.
        Command(ID_RECORD_LAST);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        CHECK(Text(IDC_AGE) == _T(""));
        SetText(IDC_AGE, _T("30"));
        Command(ID_RECORD_FIRST);
        CHECK(Cell(2, 2).vt == VT_I4 && Cell(2, 2).lVal == 30);
        CHECK(Text(IDC_NAME) == _T("Ada L."));
        CHECK(pView->IsOnFirstRecord());
        Command(ID_RECORD_LAST);
        CHECK(Text(IDC_AGE) == _T("30"));
        Command(ID_RECORD_PREV);
        CHECK(Text(IDC_NAME) == _T("Brian"));
        // A value that does not parse stops the move, after a message
        // box (which timer 2 closes).
        SetText(IDC_AGE, _T("old"));
        m_bSawMessageBox = FALSE;
        SetTimer(2, 250, NULL);
        Command(ID_RECORD_NEXT);
        CHECK(m_bSawMessageBox);
        CHECK(Text(IDC_NAME) == _T("Brian"));
        CHECK(Cell(1, 2).vt == VT_NULL);
        CHECK(pSet->GetEditMode() == dbEditNone);
        break;
      }
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

class CDvApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("dvtest.log"));
    AfxOleInit();
    daomock::Register();
    try {
      SetUpTable();
    } catch (CDaoException* e) {
      TestLog("could not set up the table");
      g_nFailures++;
      e->Delete();
      return FALSE;
    }
    CSingleDocTemplate* pTemplate =
        new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CDvDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(CPeopleView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CDvApp theApp;
