// fpframetest.cpp - the Feature Pack's frames: a CFrameWndEx with a menu
// bar, a toolbar, a status bar and docking panes (docked, tabbed, floating
// and auto-hidden), the panes' commands, and a CMDIFrameWndEx with MDI tabs
// over CMDIChildWndEx windows.
#include <afxwinappex.h>
#include <afxcontrolbars.h>
#include "fpframetest.h"
#include "testutil.h"

static int g_nNewCommands, g_nOpenCommands;

static const UINT kIndicators[] = {ID_SEPARATOR, ID_INDICATOR_CAPS};

// A pane with an edit control in it.
class CEditPane : public CDockablePane {
public:
  CEdit m_wndEdit;
  int m_nSizes;
  CEditPane() : m_nSizes(0) {}

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CDockablePane::OnCreate(lpCreateStruct) == -1) return -1;
    if (!m_wndEdit.Create(WS_CHILD | WS_VISIBLE | ES_MULTILINE, CRect(0, 0, 10, 10), this, 1)) return -1;
    return 0;
  }
  afx_msg void OnSize(UINT nType, int cx, int cy) {
    CDockablePane::OnSize(nType, cx, cy);
    m_nSizes++;
    if (m_wndEdit.GetSafeHwnd() != NULL) m_wndEdit.MoveWindow(0, 0, cx, cy);
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CEditPane, CDockablePane)
ON_WM_CREATE()
ON_WM_SIZE()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------
// The SDI frame, made the way AppWizard's code makes it.

class CMainFrame : public CFrameWndEx {
public:
  CMFCMenuBar m_wndMenuBar;
  CMFCToolBar m_wndToolBar;
  CMFCStatusBar m_wndStatusBar;
  CEditPane m_wndFiles, m_wndProps, m_wndOutput;

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWndEx::OnCreate(lpCreateStruct) == -1) return -1;
    if (!m_wndMenuBar.Create(this)) return -1;
    m_wndMenuBar.SetPaneStyle(m_wndMenuBar.GetPaneStyle() | CBRS_SIZE_DYNAMIC | CBRS_TOOLTIPS | CBRS_FLYBY);
    CMFCPopupMenu::SetForceMenuFocus(FALSE);
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT,
                               WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY |
                                   CBRS_SIZE_DYNAMIC) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME))
      return -1;
    m_wndToolBar.SetWindowText(_T("Standard"));
    m_wndToolBar.EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, _T("Customize..."));
    if (!m_wndStatusBar.Create(this)) return -1;
    m_wndStatusBar.SetIndicators(kIndicators, sizeof(kIndicators) / sizeof(UINT));

    m_wndMenuBar.EnableDocking(CBRS_ALIGN_ANY);
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockPane(&m_wndMenuBar);
    DockPane(&m_wndToolBar);
    EnableAutoHidePanes(CBRS_ALIGN_ANY);

    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_FLOAT_MULTI;
    if (!m_wndFiles.Create(_T("Files"), this, CRect(0, 0, 160, 200), TRUE, ID_PANE_FILES, dwStyle | CBRS_LEFT) ||
        !m_wndProps.Create(_T("Properties"), this, CRect(0, 0, 160, 200), TRUE, ID_PANE_PROPS,
                           dwStyle | CBRS_RIGHT) ||
        !m_wndOutput.Create(_T("Output"), this, CRect(0, 0, 100, 90), TRUE, ID_PANE_OUTPUT,
                            dwStyle | CBRS_BOTTOM))
      return -1;
    m_wndFiles.EnableDocking(CBRS_ALIGN_ANY);
    m_wndProps.EnableDocking(CBRS_ALIGN_ANY);
    m_wndOutput.EnableDocking(CBRS_ALIGN_ANY);
    DockPane(&m_wndFiles);
    DockPane(&m_wndProps);
    DockPane(&m_wndOutput);
    EnablePaneMenu(TRUE, ID_VIEW_CUSTOMIZE, _T("Customize..."), ID_VIEW_TOOLBAR);
    return 0;
  }
  afx_msg void OnFileNew() { g_nNewCommands++; }
  afx_msg void OnFileOpen() { g_nOpenCommands++; }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWndEx)
ON_WM_CREATE()
ON_COMMAND(ID_FILE_NEW, OnFileNew)
ON_COMMAND(ID_FILE_OPEN, OnFileOpen)
END_MESSAGE_MAP()

static CRect WindowRectIn(CWnd* pWnd, CWnd* pFrame) {
  CRect rect;
  pWnd->GetWindowRect(rect);
  pFrame->ScreenToClient(rect);
  return rect;
}

static void Click(CWnd* pWnd, CPoint pt) {
  pWnd->SendMessage(WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
  pWnd->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
  pWnd->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
}

static void TestSDI() {
  TestLog("SDI frame...");
  g_nNewCommands = g_nOpenCommands = 0;
  CMainFrame* pFrame = new CMainFrame;
  if (!pFrame->LoadFrame(IDR_MAINFRAME, WS_OVERLAPPEDWINDOW)) {
    TestLog("LoadFrame failed");
    g_nFailures++;
    return;
  }
  AfxGetApp()->m_pMainWnd = pFrame;
  pFrame->SetWindowPos(NULL, 0, 0, 720, 480, SWP_NOZORDER | SWP_NOACTIVATE);
  pFrame->ShowWindow(SW_SHOW);
  pFrame->UpdateWindow();
  pFrame->RecalcLayout();
  TestPump();

  // The menu bar holds the menu instead of the window.
  CHECK(::GetMenu(pFrame->m_hWnd) == NULL);
  CHECK(pFrame->m_wndMenuBar.GetCount() == 3);
  CHECK(pFrame->m_wndMenuBar.GetHMenu() != NULL);
  CMFCToolBarMenuButton* pView = DYNAMIC_DOWNCAST(CMFCToolBarMenuButton, pFrame->m_wndMenuBar.GetButton(1));
  CHECK(pView != NULL && pView->m_strText == _T("&View"));
  CHECK(pView != NULL && ::GetMenuItemCount(pView->GetMenu()) == 2);

  // The toolbar's buttons and separators; the status bar's panes.
  CHECK(pFrame->m_wndToolBar.GetCount() >= 8);
  CHECK(pFrame->m_wndToolBar.CommandToIndex(ID_FILE_OPEN) == 1);
  CHECK(pFrame->m_wndToolBar.CommandToIndex(ID_EDIT_PASTE) == 6);
  CHECK(pFrame->m_wndStatusBar.GetCount() == 2);
  CHECK(CMFCToolBar::GetImages()->GetCount() >= 6);

  // The layout: menu bar, then toolbar, at the top; status bar at the
  // bottom; files on the left, properties on the right, output above the
  // status bar.
  CRect rcClient;
  pFrame->GetClientRect(rcClient);
  CRect rcMenu = WindowRectIn(&pFrame->m_wndMenuBar, pFrame);
  CRect rcTool = WindowRectIn(&pFrame->m_wndToolBar, pFrame);
  CRect rcStatus = WindowRectIn(&pFrame->m_wndStatusBar, pFrame);
  CRect rcFiles = WindowRectIn(&pFrame->m_wndFiles, pFrame);
  CRect rcProps = WindowRectIn(&pFrame->m_wndProps, pFrame);
  CRect rcOutput = WindowRectIn(&pFrame->m_wndOutput, pFrame);
  TestLog("menu %d,%d-%d,%d tool %d,%d-%d,%d status %d,%d-%d,%d", rcMenu.left, rcMenu.top, rcMenu.right,
          rcMenu.bottom, rcTool.left, rcTool.top, rcTool.right, rcTool.bottom, rcStatus.left, rcStatus.top,
          rcStatus.right, rcStatus.bottom);
  TestLog("files %d,%d-%d,%d props %d,%d-%d,%d output %d,%d-%d,%d", rcFiles.left, rcFiles.top, rcFiles.right,
          rcFiles.bottom, rcProps.left, rcProps.top, rcProps.right, rcProps.bottom, rcOutput.left, rcOutput.top,
          rcOutput.right, rcOutput.bottom);
  CHECK(rcMenu.top == 0 && rcMenu.left == 0 && rcMenu.right == rcClient.right);
  CHECK(rcTool.top >= rcMenu.bottom);
  CHECK(rcStatus.bottom == rcClient.bottom);
  CHECK(rcFiles.left == 0 && rcFiles.top >= rcTool.bottom && rcFiles.Width() > 50);
  CHECK(rcProps.right == rcClient.right && rcProps.Width() > 50);
  CHECK(rcOutput.bottom <= rcStatus.top && rcOutput.Height() > 30);
  CRect rcBounds = pFrame->GetDockingManager()->GetClientAreaBounds();
  CHECK(rcBounds.left >= rcFiles.right - 1 && rcBounds.right <= rcProps.left + 1);
  CHECK(pFrame->m_wndFiles.IsDocked() && !pFrame->m_wndFiles.IsFloating());
  CHECK(pFrame->m_wndFiles.m_nSizes > 0);
  TestSaveWindowBitmap(pFrame->m_hWnd, _T("fpframe-sdi.bmp"));

  // A click on a toolbar button runs its command on the frame.
  CMFCToolBarButton* pOpen = pFrame->m_wndToolBar.GetButton(1);
  CHECK(pOpen != NULL && pOpen->m_nID == ID_FILE_OPEN);
  if (pOpen != NULL) Click(&pFrame->m_wndToolBar, pOpen->Rect().CenterPoint());
  TestPump();
  CHECK(g_nOpenCommands == 1);
  pFrame->SendMessage(WM_COMMAND, ID_FILE_NEW);
  CHECK(g_nNewCommands == 1);

  // The View menu's commands show and hide the bars and the panes.
  CHECK(pFrame->m_wndToolBar.IsVisible());
  pFrame->SendMessage(WM_COMMAND, ID_VIEW_TOOLBAR);
  CHECK(!pFrame->m_wndToolBar.IsVisible());
  pFrame->SendMessage(WM_COMMAND, ID_VIEW_TOOLBAR);
  CHECK(pFrame->m_wndToolBar.IsVisible());
  pFrame->SendMessage(WM_COMMAND, ID_VIEW_STATUS_BAR);
  CHECK(!pFrame->m_wndStatusBar.IsVisible());
  pFrame->SendMessage(WM_COMMAND, ID_VIEW_STATUS_BAR);
  CHECK(pFrame->m_wndStatusBar.IsVisible());
  pFrame->SendMessage(WM_COMMAND, ID_PANE_FILES);
  CHECK(!pFrame->m_wndFiles.IsVisible());
  pFrame->RecalcLayout();
  CHECK(pFrame->GetDockingManager()->GetClientAreaBounds().left < rcBounds.left);
  pFrame->SendMessage(WM_COMMAND, ID_PANE_FILES);
  CHECK(pFrame->m_wndFiles.IsVisible());
  CHECK(pFrame->GetPane(ID_PANE_PROPS) == &pFrame->m_wndProps);

  // The status bar's panes.
  CHECK(pFrame->m_wndStatusBar.SetPaneText(0, _T("Working")));
  CHECK(pFrame->m_wndStatusBar.GetPaneText(0) == _T("Working"));
  pFrame->m_wndStatusBar.SetWindowText(_T("Ready"));
  CHECK(pFrame->m_wndStatusBar.GetPaneText(0) == _T("Ready"));

  // Properties on a tab beside Files.
  TestLog("tabs...");
  CTabbedPane* pTabbed = NULL;
  CDockablePane* pTabResult =
      pFrame->m_wndProps.AttachToTabWnd(&pFrame->m_wndFiles, DM_SHOW, TRUE, (CDockablePane**)&pTabbed);
  pFrame->RecalcLayout();
  TestPump();
  CHECK(pTabResult != NULL);
  CTabbedPane* pParentTab = pFrame->m_wndFiles.GetParentTabbedPane();
  CHECK(pParentTab != NULL && pParentTab == pFrame->m_wndProps.GetParentTabbedPane());
  if (pParentTab != NULL) {
    CHECK(pParentTab->GetTabsNum() == 2);
    CHECK(pParentTab->IsDocked());
    CRect rcTab = WindowRectIn(pParentTab, pFrame);
    CHECK(rcTab.left == 0 && rcTab.Width() > 50);
  }
  CHECK(pFrame->m_wndProps.IsVisible());
  TestSaveWindowBitmap(pFrame->m_hWnd, _T("fpframe-tabs.bmp"));
  // Back on its own on the right; the tabbed pane with one left goes away.
  pFrame->m_wndProps.DockToFrameWindow(CBRS_ALIGN_RIGHT);
  pFrame->RecalcLayout();
  TestPump();
  CHECK(pFrame->m_wndProps.GetParentTabbedPane() == NULL);
  CHECK(pFrame->m_wndFiles.GetParentTabbedPane() == NULL);
  CHECK(pFrame->m_wndProps.IsDocked() && pFrame->m_wndFiles.IsDocked());
  CRect rcProps2 = WindowRectIn(&pFrame->m_wndProps, pFrame);
  CHECK(rcProps2.right == rcClient.right);

  // Output floats in a mini frame, and docks again.
  TestLog("floating...");
  CHECK(pFrame->m_wndOutput.FloatPane(CRect(100, 100, 400, 260)));
  TestPump();
  CHECK(pFrame->m_wndOutput.IsFloating() && !pFrame->m_wndOutput.IsDocked());
  CWnd* pMini = pFrame->m_wndOutput.GetParent();
  CHECK(pMini != NULL && pMini->IsKindOf(RUNTIME_CLASS(CPaneFrameWnd)));
  CHECK(pFrame->m_wndOutput.IsVisible());
  pFrame->RecalcLayout();
  CHECK(pFrame->GetDockingManager()->GetClientAreaBounds().bottom > rcBounds.bottom);
  pFrame->m_wndOutput.DockToFrameWindow(CBRS_ALIGN_BOTTOM);
  pFrame->RecalcLayout();
  TestPump();
  CHECK(pFrame->m_wndOutput.IsDocked() && !pFrame->m_wndOutput.IsFloating());
  CHECK(pFrame->m_wndOutput.GetParent() == pFrame);

  // Output auto-hidden on the bottom edge, slid out and back.
  TestLog("auto-hide...");
  pFrame->m_wndOutput.SetAutoHideMode(TRUE, CBRS_ALIGN_BOTTOM);
  pFrame->RecalcLayout();
  TestPump();
  CHECK(pFrame->m_wndOutput.IsAutoHideMode());
  CMFCAutoHideBar* pAutoBar = pFrame->m_wndOutput.GetAutoHideToolBar();
  CHECK(pAutoBar != NULL && pAutoBar->IsWindowVisible());
  CHECK(!pFrame->m_wndOutput.IsSlidOut());
  pFrame->m_wndOutput.Slide(TRUE, FALSE);
  CHECK(pFrame->m_wndOutput.IsSlidOut() && pFrame->m_wndOutput.IsWindowVisible());
  CRect rcSlid = WindowRectIn(&pFrame->m_wndOutput, pFrame);
  CHECK(rcSlid.Height() > 30 && rcSlid.bottom <= WindowRectIn(pAutoBar, pFrame).top + 1);
  TestSaveWindowBitmap(pFrame->m_hWnd, _T("fpframe-autohide.bmp"));
  pFrame->m_wndOutput.Slide(FALSE, FALSE);
  CHECK(!pFrame->m_wndOutput.IsSlidOut() && !pFrame->m_wndOutput.IsWindowVisible());
  pFrame->m_wndOutput.SetAutoHideMode(FALSE, CBRS_ALIGN_BOTTOM);
  pFrame->RecalcLayout();
  TestPump();
  CHECK(!pFrame->m_wndOutput.IsAutoHideMode() && pFrame->m_wndOutput.IsDocked());
  CHECK(pFrame->m_wndOutput.IsWindowVisible());

  pFrame->DestroyWindow();
  AfxGetApp()->m_pMainWnd = NULL;
  TestPump();
}

// ---------------------------------------------------------------------
// The MDI frame with tabs.

class CMDIMainFrame : public CMDIFrameWndEx {
public:
  CMFCMenuBar m_wndMenuBar;
  CMFCToolBar m_wndToolBar;

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CMDIFrameWndEx::OnCreate(lpCreateStruct) == -1) return -1;
    CMDITabInfo mdiTabParams;
    mdiTabParams.m_style = CMFCTabCtrl::STYLE_3D_ONENOTE;
    mdiTabParams.m_bActiveTabCloseButton = TRUE;
    mdiTabParams.m_bTabIcons = FALSE;
    mdiTabParams.m_bAutoColor = TRUE;
    mdiTabParams.m_bDocumentMenu = TRUE;
    EnableMDITabbedGroups(TRUE, mdiTabParams);
    if (!m_wndMenuBar.Create(this)) return -1;
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME))
      return -1;
    m_wndMenuBar.EnableDocking(CBRS_ALIGN_ANY);
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockPane(&m_wndMenuBar);
    DockPane(&m_wndToolBar);
    return 0;
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMDIMainFrame, CMDIFrameWndEx)
ON_WM_CREATE()
END_MESSAGE_MAP()

static CMDIChildWndEx* NewChild(CMDIMainFrame* pFrame, LPCTSTR lpszTitle) {
  CMDIChildWndEx* pChild = new CMDIChildWndEx;
  // No view: the class background paints the child.
  LPCTSTR lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW),
                                          (HBRUSH)(COLOR_WINDOW + 1), NULL);
  if (!pChild->Create(lpszClass, lpszTitle, WS_CHILD | WS_VISIBLE | WS_OVERLAPPEDWINDOW, CFrameWnd::rectDefault,
                      pFrame)) {
    TestLog("child Create failed");
    g_nFailures++;
    return NULL;
  }
  pChild->InitialUpdateFrame(NULL, TRUE);
  TestPump();
  return pChild;
}

static void TestMDI() {
  TestLog("MDI frame...");
  CMDIMainFrame* pFrame = new CMDIMainFrame;
  if (!pFrame->LoadFrame(IDR_MAINFRAME, WS_OVERLAPPEDWINDOW)) {
    TestLog("LoadFrame failed");
    g_nFailures++;
    return;
  }
  AfxGetApp()->m_pMainWnd = pFrame;
  pFrame->SetWindowPos(NULL, 0, 0, 720, 480, SWP_NOZORDER | SWP_NOACTIVATE);
  pFrame->ShowWindow(SW_SHOW);
  pFrame->UpdateWindow();
  TestPump();
  CHECK(::GetMenu(pFrame->m_hWnd) == NULL);
  CHECK(pFrame->m_wndMenuBar.GetCount() == 3);
  CHECK(pFrame->AreMDITabs());

  CMDIChildWndEx* pOne = NewChild(pFrame, _T("One"));
  CMDIChildWndEx* pTwo = NewChild(pFrame, _T("Two"));
  if (pOne == NULL || pTwo == NULL) {
    pFrame->DestroyWindow();
    return;
  }
  CHECK(pOne->GetMDIFrameWndEx() == pFrame);
  CMFCTabCtrl& tabs = pFrame->GetMDITabs();
  CHECK(tabs.GetSafeHwnd() != NULL && tabs.IsWindowVisible());
  CHECK(tabs.GetTabsNum() == 2);
  CString strLabel;
  tabs.GetTabLabel(0, strLabel);
  CHECK(strLabel == _T("One"));
  tabs.GetTabLabel(1, strLabel);
  CHECK(strLabel == _T("Two"));
  CHECK(tabs.GetActiveTab() == 1);
  CHECK(pFrame->MDIGetActive() == pTwo);
  CHECK(pTwo->IsZoomed());
  // The MDI client sits below the tabs.
  CRect rcTabs = WindowRectIn(&tabs, pFrame);
  CRect rcClient;
  ::GetWindowRect(pFrame->m_hWndMDIClient, rcClient);
  pFrame->ScreenToClient(rcClient);
  TestLog("tabs %d,%d-%d,%d client %d,%d-%d,%d", rcTabs.left, rcTabs.top, rcTabs.right, rcTabs.bottom,
          rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
  CHECK(rcTabs.bottom <= rcClient.top + 1);
  CHECK(rcTabs.top >= WindowRectIn(&pFrame->m_wndToolBar, pFrame).bottom);
  pFrame->RedrawWindow(NULL, NULL, RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
  TestSaveWindowBitmap(pFrame->m_hWnd, _T("fpframe-mdi.bmp"));

  // A tab picks its child; a new title shows on the tab.
  tabs.SetActiveTab(0);
  TestPump();
  CHECK(pFrame->MDIGetActive() == pOne);
  pFrame->MDIActivate(pTwo);
  TestPump();
  CHECK(tabs.GetActiveTab() == 1);
  pOne->SetWindowText(_T("Renamed"));
  TestPump();
  tabs.GetTabLabel(0, strLabel);
  CHECK(strLabel == _T("Renamed"));

  // A closed child takes its tab with it.
  pTwo->MDIDestroy();
  TestPump();
  CHECK(tabs.GetTabsNum() == 1);
  CHECK(pFrame->MDIGetActive() == pOne);
  pOne->MDIDestroy();
  TestPump();
  CHECK(tabs.GetTabsNum() == 0);

  pFrame->DestroyWindow();
  AfxGetApp()->m_pMainWnd = NULL;
  TestPump();
}

// ---------------------------------------------------------------------

class CTestApp : public CWinAppEx {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("fpframetest.log"));
    CWinAppEx::InitInstance();
    ::InitCommonControls();
    TestSDI();
    TestMDI();
    return FALSE;
  }
  virtual int ExitInstance() {
    CWinAppEx::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
