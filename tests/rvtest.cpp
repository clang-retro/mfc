// rvtest.cpp - a CRecordView over the in-memory ODBC stand-in: the form
// shows the current record, the record commands move through the
// recordset and write edits back, null fields show as empty, and the
// commands are enabled at the right ends.
#include <afxwin.h>
#include <afxext.h>
#include <afxdb.h>
#include "odbcmock.h"
#include "rvtest.h"
#include "testutil.h"

using mock::Value;

static void SetUpTable() {
  mock::Table people;
  people.name = "people";
  people.cols = {{"ID", SQL_INTEGER, true},
                 {"NAME", SQL_VARCHAR, false},
                 {"AGE", SQL_INTEGER, false},
                 {"ACTIVE", SQL_BIT, false}};
  people.rows = {
      {Value::Int(1), Value::Text("Ada"), Value::Int(36), Value::Int(1)},
      {Value::Int(2), Value::Text("Brian"), Value(), Value::Int(0)},
      {Value::Int(3), Value::Text("Carol"), Value::Int(29), Value()},
  };
  mock::G().tables.push_back(people);
}

static const Value& Cell(int nRow, int nCol) { return mock::G().tables[0].rows[nRow][nCol]; }

class CPeopleSet : public CRecordset {
public:
  long m_ID;
  CString m_Name;
  long m_Age;
  BOOL m_Active;
  CPeopleSet() : CRecordset(NULL), m_ID(0), m_Age(0), m_Active(FALSE) {
    m_nFields = 4;
    m_nDefaultType = dynaset;
  }
  virtual CString GetDefaultConnect() { return _T("ODBC;DSN=Mock"); }
  virtual CString GetDefaultSQL() { return _T("[people]"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Long(pFX, _T("[ID]"), m_ID);
    RFX_Text(pFX, _T("[NAME]"), m_Name);
    RFX_Long(pFX, _T("[AGE]"), m_Age);
    RFX_Bool(pFX, _T("[ACTIVE]"), m_Active);
  }
};

class CRvDoc : public CDocument {
  DECLARE_DYNCREATE(CRvDoc)
public:
  CPeopleSet m_set;
};
IMPLEMENT_DYNCREATE(CRvDoc, CDocument)

class CPeopleView : public CRecordView {
  DECLARE_DYNCREATE(CPeopleView)
public:
  CPeopleSet* m_pSet;
  CPeopleView() : CRecordView(IDD_RECORD), m_pSet(NULL) {}
  virtual CRecordset* OnGetRecordset() {
    if (m_pSet == NULL) m_pSet = &((CRvDoc*)GetDocument())->m_set;
    return m_pSet;
  }
  virtual void DoDataExchange(CDataExchange* pDX) {
    CRecordView::DoDataExchange(pDX);
    DDX_FieldText(pDX, IDC_ID, m_pSet->m_ID, m_pSet);
    DDX_FieldText(pDX, IDC_NAME, m_pSet->m_Name, m_pSet);
    DDX_FieldText(pDX, IDC_AGE, m_pSet->m_Age, m_pSet);
    DDX_FieldCheck(pDX, IDC_ACTIVE, m_pSet->m_Active, m_pSet);
  }
  virtual void OnInitialUpdate() {
    OnGetRecordset();
    CRecordView::OnInitialUpdate();
  }
};
IMPLEMENT_DYNCREATE(CPeopleView, CRecordView)

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
    Step();
    m_bBusy = FALSE;
  }

  void Step() {
    CPeopleView* pView = View();
    CPeopleSet* pSet = pView->m_pSet;
    switch (m_nStep++) {
      case 0: {
        CHECK(pView->IsKindOf(RUNTIME_CLASS(CRecordView)));
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
        TestSaveWindowBitmap(m_hWnd, _T("rvtest.bmp"));

        // An edit on the form is written when the view moves on.
        SetText(IDC_NAME, _T("Ada L."));
        Command(ID_RECORD_NEXT);
        TestLog("name now %s", Cell(0, 1).s.c_str());
        CHECK(Cell(0, 1).s == "Ada L.");
        CHECK(Text(IDC_ID) == _T("2"));
        CHECK(Text(IDC_NAME) == _T("Brian"));
        CHECK(Text(IDC_AGE) == _T(""));  // null
        CHECK(Check() == BST_UNCHECKED);
        CHECK(Enabled(ID_RECORD_FIRST) && Enabled(ID_RECORD_PREV));
        break;
      }
      case 1: {
        // Moving on without a change writes nothing.
        size_t nLog = mock::G().log.size();
        Command(ID_RECORD_NEXT);
        CHECK(mock::G().log.size() == nLog);
        CHECK(Cell(1, 2).kind == mock::kNull);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        CHECK(Check() == BST_INDETERMINATE);  // a null check box
        TestSaveWindowBitmap(m_hWnd, _T("rvtest-carol.bmp"));

        // Clearing a field stores null; past the end stays on the last.
        SetText(IDC_AGE, _T(""));
        Command(ID_RECORD_NEXT);
        CHECK(Cell(2, 2).kind == mock::kNull);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        CHECK(pView->IsOnLastRecord());
        CHECK(!Enabled(ID_RECORD_NEXT) && !Enabled(ID_RECORD_LAST));
        CHECK(Enabled(ID_RECORD_PREV));
        break;
      }
      case 2: {
        // A typed number goes in as a value.
        SetText(IDC_AGE, _T("30"));
        Command(ID_RECORD_FIRST);
        CHECK(Cell(2, 2).kind == mock::kInt && Cell(2, 2).i == 30);
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
        CHECK(Cell(1, 2).kind == mock::kNull);
        CHECK(pSet->m_nEditMode == CRecordset::noMode);
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

class CRvApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("rvtest.log"));
    SetUpTable();
    CSingleDocTemplate* pTemplate =
        new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CRvDoc), RUNTIME_CLASS(CMainFrame),
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

CRvApp theApp;
