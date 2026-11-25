// win16wnd.cpp - the window classes on 16-bit Windows: a CWinApp, a
// CFrameWnd with a message map, a menu command, a button notification,
// WM_CTLCOLOR from a static control, timers, and a modal CDialog from a
// resource with DDX. It prints ALL PASSED when each step worked, because
// Wine gives the exit code 0 whatever a 16-bit program returns.
#include <afxwin.h>
#include <stdio.h>
#include "win16wnd.h"

static int g_failures = 0;
static BOOL g_bDone = FALSE;  // the last step ran
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// The dialog: DDX moves the text of the edit control.
class CTestDialog : public CDialog {
public:
  CString m_strName;
  BOOL m_bInit;
  CTestDialog() : CDialog(IDD_TEST), m_strName("hello"), m_bInit(FALSE) {}

protected:
  /// Moves the name between the edit control and m_strName.
  virtual void DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_NAME, m_strName);
  }

  /// Changes the text of the edit control, then posts the click of OK.
  virtual BOOL OnInitDialog() {
    CDialog::OnInitDialog();
    m_bInit = TRUE;
    SetDlgItemText(IDC_NAME, "changed");
    PostMessage(WM_COMMAND, IDOK, 0);
    return TRUE;
  }
};

class CMainFrame : public CFrameWnd {
public:
  BOOL m_bCreated;
  BOOL m_bCommand;
  BOOL m_bClicked;
  int m_nPaints;
  int m_nCtlColorStatic;
  int m_nStep;
  CButton m_button;
  CStatic m_label;
  CMenu m_menu;

  CMainFrame()
      : m_bCreated(FALSE), m_bCommand(FALSE), m_bClicked(FALSE), m_nPaints(0),
        m_nCtlColorStatic(0), m_nStep(0) {}

protected:
  /// Makes the menu, the button and the label, and starts the timer.
  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    m_bCreated = TRUE;
    m_menu.CreateMenu();
    m_menu.AppendMenu(MF_STRING, ID_TEST_COMMAND, "&Test");
    SetMenu(&m_menu);
    m_button.Create("Push", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, 10, 90, 40), this,
                    IDC_PUSH);
    m_label.Create("Label", WS_CHILD | WS_VISIBLE, CRect(10, 50, 90, 70), this, IDC_LABEL);
    SetTimer(1, 50, NULL);
    return 0;
  }

  /// Counts the paints.
  afx_msg void OnPaint() {
    CPaintDC dc(this);
    dc.TextOut(100, 10, "MFC on 16-bit Windows");
    m_nPaints++;
  }

  /// Counts the WM_CTLCOLOR messages of the static control.
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
    if (nCtlColor == CTLCOLOR_STATIC && pWnd == &m_label) m_nCtlColorStatic++;
    return CFrameWnd::OnCtlColor(pDC, pWnd, nCtlColor);
  }

  /// The menu command.
  afx_msg void OnTestCommand() { m_bCommand = TRUE; }

  /// The button notification.
  afx_msg void OnPush() { m_bClicked = TRUE; }

  /// Does one step of the test each time.
  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    (void)nIDEvent;
    switch (m_nStep++) {
      case 0:
        // A menu command, as the menu sends it.
        PostMessage(WM_COMMAND, ID_TEST_COMMAND, 0);
        break;
      case 1: {
        // A click of the button: the button sends BN_CLICKED.
        m_button.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(5, 5));
        m_button.SendMessage(WM_LBUTTONUP, 0, MAKELONG(5, 5));
        m_label.Invalidate();
        m_label.UpdateWindow();
        break;
      }
      case 2: {
        KillTimer(1);
        CTestDialog dlg;
        INT_PTR nResult = dlg.DoModal();
        CHECK(nResult == IDOK);
        CHECK(dlg.m_bInit);
        CHECK(dlg.m_strName == "changed");
        CHECK(m_bCreated);
        CHECK(m_bCommand);
        CHECK(m_bClicked);
        CHECK(m_nPaints > 0);
        CHECK(m_nCtlColorStatic > 0);
        CHECK(GetActiveFrame() == this);
        // The settings go to the INI file of the program.
        CWinApp* pApp = AfxGetApp();
        CHECK(pApp->WriteProfileInt("Test", "Value", 1234));
        CHECK(pApp->GetProfileInt("Test", "Value", 0) == 1234);
        CHECK(pApp->WriteProfileString("Test", "Name", "win16"));
        CHECK(pApp->GetProfileString("Test", "Name") == "win16");
        g_bDone = TRUE;
        DestroyWindow();
        break;
      }
    }
  }

  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_PAINT()
  ON_WM_CTLCOLOR()
  ON_WM_TIMER()
  ON_COMMAND(ID_TEST_COMMAND, OnTestCommand)
  ON_BN_CLICKED(IDC_PUSH, OnPush)
END_MESSAGE_MAP()

class CTestApp : public CWinApp {
public:
  /// Makes and shows the main frame.
  virtual BOOL InitInstance() {
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, "MFC Win16 test")) {
      printf("FAIL Create\n");
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(m_nCmdShow);
    pFrame->UpdateWindow();
    return TRUE;
  }

  /// Prints the result.
  virtual int ExitInstance() {
    CHECK(g_bDone);
    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILED %d\n", g_failures);
    return CWinApp::ExitInstance();
  }
};

CTestApp theApp;
