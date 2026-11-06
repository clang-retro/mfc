// sditest.cpp - a single-document program: a frame with a toolbar and a
// status bar around an edit view, saving and loading through CArchive.
#include <afxwin.h>
#include <afxext.h>
#include "sditest.h"
#include "testutil.h"

class CSdiDoc : public CDocument {
  DECLARE_DYNCREATE(CSdiDoc)
public:
  virtual BOOL OnNewDocument() {
    if (!CDocument::OnNewDocument()) return FALSE;
    POSITION pos = GetFirstViewPosition();
    if (pos != NULL) GetNextView(pos)->SetWindowText(NULL);
    return TRUE;
  }
  virtual void Serialize(CArchive& ar) {
    // The text is the file, as in Notepad.
    ((CEditView*)m_viewList.GetHead())->SerializeRaw(ar);
  }
};
IMPLEMENT_DYNCREATE(CSdiDoc, CDocument)

class CSdiView : public CEditView {
  DECLARE_DYNCREATE(CSdiView)
public:
  CSdiDoc* GetDocument() const { return (CSdiDoc*)m_pDocument; }
};
IMPLEMENT_DYNCREATE(CSdiView, CEditView)

static const UINT indicators[] = {ID_SEPARATOR, ID_INDICATOR_CAPS, ID_INDICATOR_NUM,
                                  ID_INDICATOR_SCRL};

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  CToolBar m_wndToolBar;
  CStatusBar m_wndStatusBar;
  int m_nStep;
  CString m_strPath;

  CMainFrame() : m_nStep(0) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT,
                               WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS |
                                   CBRS_FLYBY | CBRS_SIZE_DYNAMIC) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME)) {
      TestLog("toolbar creation failed");
      return -1;
    }
    if (!m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(UINT))) {
      TestLog("status bar creation failed");
      return -1;
    }
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockControlBar(&m_wndToolBar);
    SetTimer(1, 250, NULL);
    return 0;
  }

  CSdiView* GetView() { return (CSdiView*)GetActiveView(); }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    switch (m_nStep++) {
      case 0: {
        // The document, view and bars are in place.
        CString strTitle;
        GetWindowText(strTitle);
        CHECK(strTitle == _T("Untitled - SdiTest"));
        TestLog("title '%s'", (LPCSTR)CStringA(strTitle));
        CHECK(GetView() != NULL && GetView()->IsKindOf(RUNTIME_CLASS(CSdiView)));
        CHECK(GetActiveDocument() != NULL &&
              GetActiveDocument()->IsKindOf(RUNTIME_CLASS(CSdiDoc)));
        CHECK(GetControlBar(AFX_IDW_TOOLBAR) == &m_wndToolBar);
        CHECK(GetControlBar(AFX_IDW_STATUS_BAR) == &m_wndStatusBar);
        CHECK(m_wndToolBar.GetToolBarCtrl().GetButtonCount() == 8);
        CHECK(m_wndToolBar.CommandToIndex(ID_EDIT_CUT) == 4);
        CHECK(m_wndToolBar.GetItemID(1) == ID_FILE_OPEN);
        CHECK(m_wndStatusBar.GetCount() == 4);
        CHECK(m_wndStatusBar.GetPaneText(0) == _T("Ready"));
        CHECK(m_wndStatusBar.GetItemID(1) == ID_INDICATOR_CAPS);

        // Layout: toolbar on top, status bar at the bottom, the view between.
        CRect rcClient, rcTool, rcStatus, rcView;
        GetClientRect(&rcClient);
        ClientToScreen(&rcClient);
        m_wndToolBar.GetWindowRect(&rcTool);
        m_wndStatusBar.GetWindowRect(&rcStatus);
        GetView()->GetWindowRect(&rcView);
        TestLog("client %ld,%ld-%ld,%ld tool %ld,%ld-%ld,%ld status %ld,%ld-%ld,%ld view %ld,%ld-%ld,%ld",
                rcClient.left, rcClient.top, rcClient.right, rcClient.bottom, rcTool.left,
                rcTool.top, rcTool.right, rcTool.bottom, rcStatus.left, rcStatus.top,
                rcStatus.right, rcStatus.bottom, rcView.left, rcView.top, rcView.right,
                rcView.bottom);
        CHECK(rcTool.top == rcClient.top && rcTool.Height() >= 22);
        CHECK(rcStatus.bottom == rcClient.bottom && rcStatus.Height() >= 16);
        CHECK(rcView.top == rcTool.bottom && rcView.bottom == rcStatus.top);
        CHECK(rcTool.Width() == rcClient.Width());

        // Nothing is selected, so the idle update disabled Cut and Copy.
        int nCut = m_wndToolBar.CommandToIndex(ID_EDIT_CUT);
        CHECK((m_wndToolBar.GetButtonStyle(nCut) & TBBS_DISABLED) != 0);
        CHECK((m_wndToolBar.GetButtonStyle(0) & TBBS_DISABLED) == 0);

        // Tooltip text comes from the string after the newline.
        NMTTDISPINFO ttt;
        memset(&ttt, 0, sizeof(ttt));
        ttt.hdr.hwndFrom = m_wndToolBar.m_hWnd;
        ttt.hdr.idFrom = ID_FILE_OPEN;
        ttt.hdr.code = TTN_NEEDTEXT;
        SendMessage(WM_NOTIFY, ID_FILE_OPEN, (LPARAM)&ttt);
        CHECK(lstrcmp(ttt.szText, _T("Open")) == 0);

        // Typing marks the document modified. (Setting the text of a
        // multiline edit control sends no EN_CHANGE, so this replaces the
        // selection, as typing does.)
        GetView()->GetEditCtrl().ReplaceSel(_T("Hello, world.\r\nSecond line."));
        GetView()->GetEditCtrl().SetSel(0, 5);
        CHECK(GetActiveDocument()->IsModified());
        break;
      }
      case 1: {
        // With a selection, Cut is enabled after the idle update.
        int nCut = m_wndToolBar.CommandToIndex(ID_EDIT_CUT);
        CHECK((m_wndToolBar.GetButtonStyle(nCut) & TBBS_DISABLED) == 0);
        CString strSel;
        GetView()->GetSelectedText(strSel);
        CHECK(strSel == _T("Hello"));
        CHECK(GetView()->FindText(_T("second"), TRUE, FALSE));
        GetView()->GetSelectedText(strSel);
        CHECK(strSel == _T("Second"));
        CHECK(!GetView()->FindText(_T("missing")));
        // Fly-by help puts a command's prompt in the status bar.
        SendMessage(WM_SETMESSAGESTRING, ID_FILE_NEW);
        CHECK(m_wndStatusBar.GetPaneText(0) == _T("Create a new document"));
        SendMessage(WM_SETMESSAGESTRING, AFX_IDS_IDLEMESSAGE);
        TestSaveWindowBitmap(m_hWnd, _T("sditest.bmp"));
        break;
      }
      case 2: {
        // Save to a file and read it back.
        TCHAR szDir[MAX_PATH], szPath[MAX_PATH];
        ::GetTempPath(MAX_PATH, szDir);
        ::GetTempFileName(szDir, _T("sdi"), 0, szPath);
        m_strPath = szPath;
        CDocument* pDoc = GetActiveDocument();
        CHECK(pDoc->OnSaveDocument(m_strPath));
        pDoc->SetPathName(m_strPath);
        CHECK(!pDoc->IsModified());
        CString strTitle;
        GetWindowText(strTitle);
        TestLog("title after save '%s'", (LPCSTR)CStringA(strTitle));
        CHECK(strTitle.Find(_T(" - SdiTest")) > 0);
        CFile file(m_strPath, CFile::modeRead);
        CHECK(file.GetLength() == 27 * sizeof(TCHAR));
        file.Close();

        GetView()->SetWindowText(_T("scratch"));
        CHECK(pDoc->OnOpenDocument(m_strPath));
        CString strText;
        GetView()->GetWindowText(strText);
        CHECK(strText == _T("Hello, world.\r\nSecond line."));
        CHECK(!pDoc->IsModified());
        CFile::Remove(m_strPath);

        // View menu commands hide and show the bars.
        SendMessage(WM_COMMAND, ID_VIEW_STATUS_BAR);
        CHECK(!m_wndStatusBar.IsWindowVisible());
        SendMessage(WM_COMMAND, ID_VIEW_STATUS_BAR);
        CHECK(m_wndStatusBar.IsWindowVisible());
        break;
      }
      case 3:
        KillTimer(1);
        GetActiveDocument()->SetModifiedFlag(FALSE);
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

class CSdiApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("sditest.log"));
    CSingleDocTemplate* pTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME, RUNTIME_CLASS(CSdiDoc), RUNTIME_CLASS(CMainFrame), RUNTIME_CLASS(CSdiView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->SetWindowPos(NULL, 0, 0, 480, 320, SWP_NOMOVE | SWP_NOZORDER);
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CSdiApp, CWinApp)
  ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
  ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
END_MESSAGE_MAP()

CSdiApp theApp;
