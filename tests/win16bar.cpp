// win16bar.cpp - the toolbar and the status bar on 16-bit Windows: a
// CToolBar with buttons and a separator, a click that sends a command,
// the idle update that disables a button and checks another, docking at
// the left edge, floating, and a CStatusBar with indicator panes and the
// message pane. It prints ALL PASSED when each step worked.
#include <afxwin.h>
#include <afxext.h>
#include <afxpriv.h>
#include <stdio.h>
#include "win16bar.h"

static int g_failures = 0;
static BOOL g_bDone = FALSE;  // the last step ran
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

static const UINT s_buttons[] = {ID_BAR_ONE, ID_SEPARATOR, ID_BAR_TWO, ID_BAR_THREE, ID_BAR_CHECK};
static const UINT s_indicators[] = {ID_SEPARATOR, ID_INDICATOR_TEST};

/// Makes an image well of nCount images of 16 by 15 pixels, each in its
/// own color.
static HBITMAP MakeImages(int nCount) {
  CClientDC dcScreen(NULL);
  CDC dc;
  dc.CreateCompatibleDC(&dcScreen);
  CBitmap bm;
  bm.CreateCompatibleBitmap(&dcScreen, 16 * nCount, 15);
  CBitmap* pOld = dc.SelectObject(&bm);
  dc.FillSolidRect(0, 0, 16 * nCount, 15, ::GetSysColor(COLOR_BTNFACE));
  static const COLORREF colors[] = {RGB(255, 0, 0), RGB(0, 128, 0), RGB(0, 0, 255), RGB(0, 0, 0)};
  for (int i = 0; i < nCount; i++) dc.FillSolidRect(i * 16 + 3, 3, 10, 9, colors[i % 4]);
  dc.SelectObject(pOld);
  return (HBITMAP)bm.Detach();
}

class CMainFrame : public CFrameWnd {
public:
  CToolBar m_wndToolBar;
  CStatusBar m_wndStatusBar;
  int m_nOne;
  int m_nTwo;
  int m_nStep;

  CMainFrame() : m_nOne(0), m_nTwo(0), m_nStep(0) {}

  /// Gives the center of the button nID in the toolbar's client area.
  CPoint ButtonCenter(UINT nID) {
    CRect rect;
    m_wndToolBar.GetItemRect(m_wndToolBar.CommandToIndex(nID), &rect);
    return rect.CenterPoint();
  }

  /// Clicks the button nID as the mouse does.
  void Click(UINT nID) {
    CPoint pt = ButtonCenter(nID);
    m_wndToolBar.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(pt.x, pt.y));
    m_wndToolBar.SendMessage(WM_LBUTTONUP, 0, MAKELONG(pt.x, pt.y));
  }

  /// Runs the idle update of the bars.
  void IdleUpdate() { SendMessageToDescendants(WM_IDLEUPDATECMDUI, TRUE, 0, TRUE, TRUE); }

protected:
  /// Makes the toolbar and the status bar, and starts the timer.
  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    CHECK(m_wndToolBar.Create(this, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_FLYBY));
    CHECK(m_wndToolBar.SetButtons(s_buttons, 5));
    CHECK(m_wndToolBar.SetBitmap(MakeImages(4)));
    CHECK(m_wndStatusBar.Create(this));
    CHECK(m_wndStatusBar.SetIndicators(s_indicators, 2));
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockControlBar(&m_wndToolBar);
    SetTimer(1, 50, NULL);
    return 0;
  }

  /// The commands of the buttons.
  afx_msg void OnOne() { m_nOne++; }
  afx_msg void OnTwo() { m_nTwo++; }

  /// Disables the third button and checks the check button.
  afx_msg void OnUpdateThree(CCmdUI* pCmdUI) { pCmdUI->Enable(FALSE); }
  afx_msg void OnUpdateCheck(CCmdUI* pCmdUI) { pCmdUI->SetCheck(1); }
  afx_msg void OnCheck() {}
  afx_msg void OnUpdateIndicator(CCmdUI* pCmdUI) { pCmdUI->SetText("ON"); }

  /// Does one step of the test each time.
  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    (void)nIDEvent;
    switch (m_nStep++) {
      case 0: {
        // The toolbar has its buttons, docked at the top.
        CHECK(::IsWindow(m_wndToolBar.m_hWnd));
        CHECK(m_wndToolBar.IsWindowVisible());
        CHECK(m_wndToolBar.CommandToIndex(ID_BAR_TWO) == 2);
        CHECK(m_wndToolBar.GetItemID(1) == ID_SEPARATOR);
        CRect rectOne, rectSep, rectTwo;
        m_wndToolBar.GetItemRect(0, &rectOne);
        m_wndToolBar.GetItemRect(1, &rectSep);
        m_wndToolBar.GetItemRect(2, &rectTwo);
        CHECK(rectOne.Width() == 23 && rectOne.Height() == 22);
        CHECK(rectSep.Width() == 8);
        CHECK(rectTwo.left == rectSep.right && rectTwo.top == rectOne.top);
        CRect rectBar, rectClient;
        m_wndToolBar.GetWindowRect(&rectBar);
        ScreenToClient(&rectBar);
        GetClientRect(&rectClient);
        CHECK(rectBar.top == rectClient.top || rectBar.top <= 4);
        CHECK(rectBar.Height() >= 22);
        CHECK(!m_wndToolBar.IsFloating());
        // The status bar is at the bottom with two panes.
        CRect rectStatus;
        m_wndStatusBar.GetWindowRect(&rectStatus);
        ScreenToClient(&rectStatus);
        CHECK(rectStatus.bottom == rectClient.bottom);
        CHECK(rectStatus.Height() > 10);
        CRect rectPane0, rectPane1;
        m_wndStatusBar.GetItemRect(0, &rectPane0);
        m_wndStatusBar.GetItemRect(1, &rectPane1);
        CHECK(rectPane1.Width() > 8);
        CHECK(rectPane0.right < rectPane1.left);
        // The message pane stretches to the indicator.
        CHECK(rectPane0.Width() > rectStatus.Width() / 2);
        // The indicator has its string, or the text of an idle update.
        CString strPane = m_wndStatusBar.GetPaneText(1);
        CHECK(strPane == "TEST" || strPane == "ON");
        break;
      }
      case 1: {
        // A click sends the command of the button to the frame.
        Click(ID_BAR_ONE);
        Click(ID_BAR_TWO);
        CHECK(m_nOne == 1);
        CHECK(m_nTwo == 1);
        // A press that leaves the button sends nothing.
        CPoint pt = ButtonCenter(ID_BAR_ONE);
        m_wndToolBar.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELONG(pt.x, pt.y));
        m_wndToolBar.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELONG(pt.x, 200));
        m_wndToolBar.SendMessage(WM_LBUTTONUP, 0, MAKELONG(pt.x, 200));
        CHECK(m_nOne == 1);
        break;
      }
      case 2: {
        // The idle update disables, checks and sets the pane text.
        IdleUpdate();
        int i3 = m_wndToolBar.CommandToIndex(ID_BAR_THREE);
        int iCheck = m_wndToolBar.CommandToIndex(ID_BAR_CHECK);
        CHECK((m_wndToolBar.GetButtonStyle(i3) & TBBS_DISABLED) != 0);
        CHECK((m_wndToolBar.GetButtonStyle(iCheck) & TBBS_CHECKED) != 0);
        CHECK((m_wndToolBar.GetButtonStyle(0) & TBBS_DISABLED) == 0);
        CHECK(m_wndStatusBar.GetPaneText(1) == "ON");
        // A disabled button sends nothing.
        int nTwo = m_nTwo;
        Click(ID_BAR_THREE);
        CHECK(m_nTwo == nTwo);
        // The control has the state that the class set.
        CHECK((m_wndToolBar.DefWindowProc(TB_GETSTATE, ID_BAR_THREE, 0) & TBSTATE_ENABLED) == 0);
        // The message pane.
        SetMessageText("Hello from Win16");
        char sz[64];
        m_wndStatusBar.DefWindowProc(SB_GETTEXT, 0, (LPARAM)(LPSTR)sz);
        CHECK(lstrcmp(sz, "Hello from Win16") == 0);
        CHECK(m_wndStatusBar.GetPaneText(0) == "Hello from Win16");
        m_wndToolBar.UpdateWindow();
        m_wndStatusBar.UpdateWindow();
        break;
      }
      case 3: {
        // Docked at the left edge, the buttons are in one column.
        DockControlBar(&m_wndToolBar, AFX_IDW_DOCKBAR_LEFT);
        RecalcLayout();
        CRect rectOne, rectTwo;
        m_wndToolBar.GetItemRect(0, &rectOne);
        m_wndToolBar.GetItemRect(2, &rectTwo);
        CHECK(rectTwo.top > rectOne.bottom - 1);
        CHECK(rectTwo.left == rectOne.left);
        CRect rectBar;
        m_wndToolBar.GetWindowRect(&rectBar);
        CHECK(rectBar.Height() > rectBar.Width());
        break;
      }
      case 4: {
        // Floating, the bar is in a mini frame, in one row.
        FloatControlBar(&m_wndToolBar, CPoint(300, 200));
        CHECK(m_wndToolBar.IsFloating());
        CFrameWnd* pMini = m_wndToolBar.GetParentFrame();
        CHECK(pMini != NULL && pMini != this);
        CRect rectOne, rectTwo;
        m_wndToolBar.GetItemRect(0, &rectOne);
        m_wndToolBar.GetItemRect(2, &rectTwo);
        CHECK(rectTwo.top == rectOne.top);
        // Docked again at the top.
        DockControlBar(&m_wndToolBar, AFX_IDW_DOCKBAR_TOP);
        CHECK(!m_wndToolBar.IsFloating());
        Click(ID_BAR_ONE);
        CHECK(m_nOne == 2);
        break;
      }
      case 5: {
        // ShowControlBar hides the status bar.
        ShowControlBar(&m_wndStatusBar, FALSE, FALSE);
        CHECK(!m_wndStatusBar.IsWindowVisible());
        ShowControlBar(&m_wndStatusBar, TRUE, FALSE);
        CHECK(m_wndStatusBar.IsWindowVisible());
        KillTimer(1);
        g_bDone = TRUE;
        if (getenv("WIN16BAR_KEEP") == NULL) DestroyWindow();
        break;
      }
    }
  }

  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
  ON_COMMAND(ID_BAR_ONE, OnOne)
  ON_COMMAND(ID_BAR_TWO, OnTwo)
  ON_COMMAND(ID_BAR_CHECK, OnCheck)
  ON_UPDATE_COMMAND_UI(ID_BAR_THREE, OnUpdateThree)
  ON_UPDATE_COMMAND_UI(ID_BAR_CHECK, OnUpdateCheck)
  ON_UPDATE_COMMAND_UI(ID_INDICATOR_TEST, OnUpdateIndicator)
END_MESSAGE_MAP()

class CTestApp : public CWinApp {
public:
  /// Makes and shows the main frame.
  virtual BOOL InitInstance() {
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, "MFC Win16 bars", WS_OVERLAPPEDWINDOW, CRect(20, 20, 420, 320))) {
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
