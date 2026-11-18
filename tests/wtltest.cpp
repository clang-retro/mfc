// wtltest.cpp - the Windows Template Library (WTL 10) built on this ATL:
//   - CAppModule, CMessageLoop with a message filter and an idle handler;
//   - a frame (CFrameWindowImpl) with a command bar, a toolbar in a
//     rebar, a status bar and CUpdateUI, its client a splitter
//     (CSplitterWindow) of a tree view (CTreeViewCtrlEx) and a list view
//     (CListViewCtrl), and the message crackers of atlcrack.h;
//   - a dialog with data exchange (CWinDataExchange) and resizing
//     (CDialogResize), shown modally;
//   - a dialog made in memory (CIndirectDialogImpl);
//   - a modeless property sheet of two pages;
//   - a scrolling window (CScrollWindowImpl);
//   - the GDI and USER wrappers (CDC, CBitmap, CBrush, CPen, CFont,
//     CRgn, CMenu, CResource) and CFindFile.
// WTL is not part of this library: the test builds when WTL's headers are
// on the include path (tools/fetch-wtl.sh downloads them). Writes
// wtltest.log in MFC_TEST_OUT; exits with 1 on a failure.

// WTL 10 needs Windows XP and the common controls of IE 6.
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define _WIN32_IE 0x0600

#include <atlbase.h>
#include <atlstr.h>

// WTL 10 stops unless _MSC_VER says Visual C++ 2005 or later, which clang
// for MinGW does not define (defining it for longer would turn the C and
// C++ runtimes' headers to their Visual C++ forms).
#if !defined(_MSC_VER)
#define _MSC_VER 1400
#define WTLTEST_DEFINED_MSC_VER
#endif
#include <atlapp.h>
#ifdef WTLTEST_DEFINED_MSC_VER
#undef _MSC_VER
#endif

extern CAppModule _Module;

#include <atlwin.h>

// The MinGW-w64 headers name this message only as LM_GETIDEALHEIGHT,
// its Windows XP name.
#ifndef LM_GETIDEALSIZE
#define LM_GETIDEALSIZE LM_GETIDEALHEIGHT
#endif

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlw.h>
#include <atlctrlx.h>
#include <atlmisc.h>
#include <atlcrack.h>
#include <atlddx.h>
#include <atlsplit.h>
#include <atlscrl.h>
#include <atlgdi.h>
#include <atluser.h>
#include <stdio.h>
#include <stdarg.h>
#include "wtltest.h"

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

CAppModule _Module;

static int g_nFailures;
static FILE* g_pLog;

static void Log(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (g_pLog != NULL) {
    vfprintf(g_pLog, fmt, args);
    fputc('\n', g_pLog);
    fflush(g_pLog);
  }
  va_end(args);
}

#define CHECK(c)                                     \
  do {                                               \
    if (!(c)) {                                      \
      Log("FAIL %s:%d: %s", __FILE__, __LINE__, #c); \
      g_nFailures++;                                 \
    }                                                \
  } while (0)

// ---------------------------------------------------------------------
// A dialog with data exchange and resizing.

class CDdxDlg : public CDialogImpl<CDdxDlg>,
                public CWinDataExchange<CDdxDlg>,
                public CDialogResize<CDdxDlg> {
public:
  enum { IDD = IDD_DDX };
  CString m_strName;
  int m_nCount;
  bool m_bFlag;
  bool m_bRan;

  CDdxDlg() : m_strName(_T("first")), m_nCount(7), m_bFlag(true), m_bRan(false) {}

  BEGIN_MSG_MAP(CDdxDlg)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    MESSAGE_HANDLER(WM_APP, OnRun)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    CHAIN_MSG_MAP(CDialogResize<CDdxDlg>)
  END_MSG_MAP()

  BEGIN_DDX_MAP(CDdxDlg)
    DDX_TEXT(IDC_NAME, m_strName)
    DDX_INT_RANGE(IDC_COUNT, m_nCount, 0, 100)
    DDX_CHECK(IDC_FLAG, m_bFlag)
  END_DDX_MAP()

  BEGIN_DLGRESIZE_MAP(CDdxDlg)
    DLGRESIZE_CONTROL(IDOK, DLSZ_MOVE_X)
    DLGRESIZE_CONTROL(IDCANCEL, DLSZ_MOVE_X)
    DLGRESIZE_CONTROL(IDC_GROW, DLSZ_SIZE_X)
  END_DLGRESIZE_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    DoDataExchange(DDX_LOAD);
    DlgResize_Init();
    PostMessage(WM_APP);
    return TRUE;
  }

  LRESULT OnRun(UINT, WPARAM, LPARAM, BOOL&) {
    m_bRan = true;
    // Loaded into the controls.
    TCHAR sz[64];
    GetDlgItemText(IDC_NAME, sz, 64);
    CHECK(lstrcmp(sz, _T("first")) == 0);
    CHECK(GetDlgItemInt(IDC_COUNT, NULL, TRUE) == 7);
    CHECK(IsDlgButtonChecked(IDC_FLAG) == BST_CHECKED);

    // Saved from them.
    SetDlgItemText(IDC_NAME, _T("second"));
    SetDlgItemInt(IDC_COUNT, 42, TRUE);
    CheckDlgButton(IDC_FLAG, BST_UNCHECKED);
    CHECK(DoDataExchange(DDX_SAVE));
    CHECK(m_strName == _T("second"));
    CHECK(m_nCount == 42);
    CHECK(!m_bFlag);

    // Out of range: the exchange fails (WTL has stored the value by then).
    SetDlgItemInt(IDC_COUNT, 500, TRUE);
    CHECK(!DoDataExchange(DDX_SAVE));
    CHECK(m_nCount == 500);

    // Wider: the buttons move right, the edit grows, the rest stays.
    CRect rcOk, rcGrow, rcName, rcOk2, rcGrow2, rcName2, rcWnd;
    GetDlgItem(IDOK).GetWindowRect(&rcOk);
    GetDlgItem(IDC_GROW).GetWindowRect(&rcGrow);
    GetDlgItem(IDC_NAME).GetWindowRect(&rcName);
    GetWindowRect(&rcWnd);
    SetWindowPos(NULL, 0, 0, rcWnd.Width() + 80, rcWnd.Height(), SWP_NOMOVE | SWP_NOZORDER);
    GetDlgItem(IDOK).GetWindowRect(&rcOk2);
    GetDlgItem(IDC_GROW).GetWindowRect(&rcGrow2);
    GetDlgItem(IDC_NAME).GetWindowRect(&rcName2);
    CHECK(rcOk2.left - rcOk.left == 80);
    CHECK(rcOk2.Width() == rcOk.Width());
    CHECK(rcGrow2.Width() - rcGrow.Width() == 80);
    CHECK(rcGrow2.left == rcGrow.left);
    CHECK(rcName2 == rcName);
    EndDialog(IDOK);
    return 0;
  }

  LRESULT OnClose(WORD, WORD wID, HWND, BOOL&) {
    EndDialog(wID);
    return 0;
  }
};

// ---------------------------------------------------------------------
// A dialog template made in memory.

class CMemDlg : public CIndirectDialogImpl<CMemDlg> {
public:
  BEGIN_DIALOG(0, 0, 150, 60)
    DIALOG_CAPTION(_T("Memory"))
    DIALOG_STYLE(DS_SETFONT | WS_POPUP | WS_CAPTION)
    DIALOG_FONT(8, _T("MS Shell Dlg"))
  END_DIALOG()

  BEGIN_CONTROLS_MAP()
    CONTROL_LTEXT(_T("Name"), IDC_STATIC, 5, 5, 40, 8, 0, 0)
    CONTROL_EDITTEXT(IDC_NAME, 50, 5, 90, 12, ES_AUTOHSCROLL | WS_TABSTOP, 0)
    CONTROL_AUTOCHECKBOX(_T("Flag"), IDC_FLAG, 50, 22, 60, 10, WS_TABSTOP, 0)
    CONTROL_DEFPUSHBUTTON(_T("OK"), IDOK, 50, 40, 40, 14, WS_TABSTOP, 0)
  END_CONTROLS_MAP()

  BEGIN_MSG_MAP(CMemDlg)
  END_MSG_MAP()
};

// ---------------------------------------------------------------------
// Property pages.

class CPage1 : public CPropertyPageImpl<CPage1> {
public:
  enum { IDD = IDD_PAGE1 };
  bool m_bInit;
  CPage1() : CPropertyPageImpl<CPage1>(_T("One")), m_bInit(false) {}
  BEGIN_MSG_MAP(CPage1)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    CHAIN_MSG_MAP(CPropertyPageImpl<CPage1>)
  END_MSG_MAP()
  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    m_bInit = true;
    SetDlgItemText(IDC_NAME, _T("page one"));
    return TRUE;
  }
};

class CPage2 : public CPropertyPageImpl<CPage2> {
public:
  enum { IDD = IDD_PAGE2 };
  int m_nActive;
  CPage2() : m_nActive(0) {}
  BEGIN_MSG_MAP(CPage2)
    CHAIN_MSG_MAP(CPropertyPageImpl<CPage2>)
  END_MSG_MAP()
  int OnSetActive() {
    m_nActive++;
    return 0;
  }
};

// ---------------------------------------------------------------------
// A scrolling window.

class CScrollTestView : public CScrollWindowImpl<CScrollTestView> {
public:
  int m_nPaints;
  CScrollTestView() : m_nPaints(0) {}
  BEGIN_MSG_MAP(CScrollTestView)
    CHAIN_MSG_MAP(CScrollWindowImpl<CScrollTestView>)
  END_MSG_MAP()
  void DoPaint(CDCHandle /*dc*/) { m_nPaints++; }
};

// ---------------------------------------------------------------------
// The frame.

class CMainFrame : public CFrameWindowImpl<CMainFrame>,
                   public CUpdateUI<CMainFrame>,
                   public CMessageFilter,
                   public CIdleHandler {
public:
  DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

  CCommandBarCtrl m_CmdBar;
  CSplitterWindow m_splitter;
  CTreeViewCtrlEx m_tree;
  CListViewCtrl m_list;
  HWND m_hWndToolBar2;
  int m_nIdle;
  int m_nFilter;
  int m_nTestOne;
  bool m_bCreated;
  bool m_bTestsDone;
  bool m_bFinishing;

  CMainFrame()
      : m_hWndToolBar2(NULL), m_nIdle(0), m_nFilter(0), m_nTestOne(0), m_bCreated(false),
        m_bTestsDone(false), m_bFinishing(false) {}

  virtual BOOL PreTranslateMessage(MSG* pMsg) {
    m_nFilter++;
    if (CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg)) return TRUE;
    return FALSE;
  }
  // The loop calls this when its queue empties; once the tests have run,
  // the first idle time ends the program.
  virtual BOOL OnIdle() {
    m_nIdle++;
    UIUpdateToolBar();
    if (m_bTestsDone && !m_bFinishing) {
      m_bFinishing = true;
      PostMessage(WM_APP + 1);
    }
    return FALSE;
  }

  BEGIN_UPDATE_UI_MAP(CMainFrame)
    UPDATE_ELEMENT(ID_EDIT_CUT, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
    UPDATE_ELEMENT(ID_EDIT_COPY, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
    UPDATE_ELEMENT(ID_TEST_ONE, UPDUI_MENUPOPUP | UPDUI_TOOLBAR)
    UPDATE_ELEMENT(ID_VIEW_STATUS_BAR, UPDUI_MENUPOPUP)
  END_UPDATE_UI_MAP()

  BEGIN_MSG_MAP_EX(CMainFrame)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MESSAGE_HANDLER_EX(WM_APP, OnRunTests)
    MESSAGE_HANDLER_EX(WM_APP + 1, OnFinish)
    MSG_WM_TIMER(OnTimer)
    COMMAND_ID_HANDLER_EX(ID_TEST_ONE, OnTestOne)
    COMMAND_ID_HANDLER_EX(ID_APP_EXIT, OnFileExit)
    CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
  END_MSG_MAP()

  int OnCreate(LPCREATESTRUCT /*lpcs*/) {
    // The menu as a command bar, with the toolbar's images.
    m_CmdBar.Create(m_hWnd, rcDefault, NULL, ATL_SIMPLE_CMDBAR_PANE_STYLE);
    m_CmdBar.AttachMenu(GetMenu());
    m_CmdBar.LoadImages(IDR_MAINFRAME);
    SetMenu(NULL);

    m_hWndToolBar2 = CreateSimpleToolBarCtrl(m_hWnd, IDR_MAINFRAME, FALSE,
                                             ATL_SIMPLE_TOOLBAR_PANE_STYLE);
    CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
    AddSimpleReBarBand(m_CmdBar);
    AddSimpleReBarBand(m_hWndToolBar2, NULL, TRUE);
    CreateSimpleStatusBar();
    UIAddToolBar(m_hWndToolBar2);

    m_hWndClient = m_splitter.Create(m_hWnd, rcDefault, NULL,
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    m_tree.Create(m_splitter, rcDefault, NULL,
                  WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
                  WS_EX_CLIENTEDGE);
    m_list.Create(m_splitter, rcDefault, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT,
                  WS_EX_CLIENTEDGE);
    m_splitter.SetSplitterPanes(m_tree, m_list);
    UpdateLayout();

    CMessageLoop* pLoop = _Module.GetMessageLoop();
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);
    m_bCreated = true;
    PostMessage(WM_APP);
    return 0;
  }

  void OnDestroy() {
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    pLoop->RemoveMessageFilter(this);
    pLoop->RemoveIdleHandler(this);
    SetMsgHandled(FALSE);
  }

  void OnTestOne(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/) { m_nTestOne++; }
  void OnFileExit(UINT, int, CWindow) { PostMessage(WM_CLOSE); }

  LRESULT OnRunTests(UINT, WPARAM, LPARAM) {
    TestFrame();
    Log("frame: %d failure(s) so far", g_nFailures);
    TestControls();
    Log("controls: %d failure(s) so far", g_nFailures);
    TestDialogs();
    Log("dialogs: %d failure(s) so far", g_nFailures);
    TestScroll();
    Log("scrolling: %d failure(s) so far", g_nFailures);
    TestGdiUser();
    Log("GDI and USER: %d failure(s) so far", g_nFailures);
    // The idle handler finishes; the timer, if it never runs.
    m_bTestsDone = true;
    SetTimer(1, 10000);
    return 0;
  }

  void OnTimer(UINT_PTR /*nIDEvent*/) {
    KillTimer(1);
    Log("the idle handler did not run");
    PostMessage(WM_APP + 1);
  }

  LRESULT OnFinish(UINT, WPARAM, LPARAM) {
    KillTimer(1);
    CHECK(m_nFilter > 0);
    CHECK(m_nIdle > 0);
    PostMessage(WM_CLOSE);
    return 0;
  }

  void TestFrame() {
    CHECK(m_bCreated);
    CHECK(m_hAccel != NULL);
    CHECK(::IsWindow(m_hWndToolBar));  // the rebar
    CHECK(::IsWindow(m_hWndStatusBar));
    CHECK(GetMenu() == NULL);

    // The command bar holds the menu's three popups.
    CHECK(m_CmdBar.IsWindow());
    CHECK(::SendMessage(m_CmdBar, TB_BUTTONCOUNT, 0, 0) == 3);
    CHECK(::GetMenuItemCount(m_CmdBar.GetMenu()) == 3);

    // The toolbar from the RT_TOOLBAR resource: four buttons and a
    // separator, and the tooltips' text from the string table.
    CToolBarCtrl tb(m_hWndToolBar2);
    CHECK(tb.GetButtonCount() == 5);
    TBBUTTON button = {};
    CHECK(tb.GetButton(1, &button) && (button.fsStyle & BTNS_SEP) != 0);
    CHECK(tb.CommandToIndex(ID_TEST_ONE) == 4);

    // CUpdateUI: a disabled command is disabled on the toolbar.
    UIEnable(ID_EDIT_CUT, FALSE);
    UISetCheck(ID_TEST_ONE, 1);
    UIUpdateToolBar(TRUE);
    CHECK(!tb.IsButtonEnabled(ID_EDIT_CUT));
    CHECK(tb.IsButtonEnabled(ID_EDIT_COPY));
    CHECK(tb.IsButtonChecked(ID_TEST_ONE));
    UISetCheck(ID_VIEW_STATUS_BAR, 1);
    CHECK((UIGetState(ID_VIEW_STATUS_BAR) & UPDUI_CHECKED) != 0);
    CHECK((UIGetState(ID_EDIT_CUT) & UPDUI_DISABLED) != 0);
    UIEnable(ID_EDIT_CUT, TRUE);
    UIUpdateToolBar(TRUE);
    CHECK(tb.IsButtonEnabled(ID_EDIT_CUT));

    // A command reaches its cracked handler.
    SendMessage(WM_COMMAND, MAKEWPARAM(ID_TEST_ONE, 0), 0);
    CHECK(m_nTestOne == 1);

    // The status bar.
    ::SetWindowText(m_hWndStatusBar, _T("Ready"));
    TCHAR sz[32] = _T("");
    ::GetWindowText(m_hWndStatusBar, sz, 32);
    CHECK(lstrcmp(sz, _T("Ready")) == 0);

    // The splitter fills the client area between the bars.
    CRect rcClient, rcSplit;
    GetClientRect(&rcClient);
    m_splitter.GetWindowRect(&rcSplit);
    CHECK(rcSplit.Width() == rcClient.Width());
    CHECK(m_splitter.SetSplitterPos(120));
    CHECK(m_splitter.GetSplitterPos() == 120);
    CRect rcTree;
    m_tree.GetWindowRect(&rcTree);
    CHECK(rcTree.Width() == 120);
    CHECK(m_splitter.SetSinglePaneMode(SPLIT_PANE_RIGHT));
    CHECK(m_splitter.GetSinglePaneMode() == SPLIT_PANE_RIGHT);
    CHECK(!m_tree.IsWindowVisible());
    m_splitter.SetSinglePaneMode(SPLIT_PANE_NONE);
    CHECK(m_tree.IsWindowVisible());
  }

  void TestControls() {
    // The tree, through CTreeItem.
    CTreeItem root = m_tree.InsertItem(_T("Root"), TVI_ROOT, TVI_LAST);
    CHECK(!root.IsNull());
    CTreeItem child = root.AddTail(_T("Child"), -1);
    root.AddTail(_T("Second"), -1);
    CHECK(root.HasChildren());
    CTreeItem first = root.GetChild();
    CString str;
    CHECK(first.GetText(str) && str == _T("Child"));
    CHECK(first.GetNextSibling().GetText(str) && str == _T("Second"));
    CHECK(child.GetParent() == root);
    CHECK(m_tree.GetCount() == 3);
    CHECK(root.Expand());
    CHECK(child.Select());
    CHECK(m_tree.GetSelectedItem() == child);

    // The list view.
    CHECK(m_list.InsertColumn(0, _T("Name"), LVCFMT_LEFT, 100, 0) == 0);
    CHECK(m_list.InsertColumn(1, _T("Size"), LVCFMT_RIGHT, 60, 1) == 1);
    CHECK(m_list.InsertItem(0, _T("alpha")) == 0);
    CHECK(m_list.InsertItem(1, _T("beta")) == 1);
    CHECK(m_list.SetItemText(1, 1, _T("42")));
    CHECK(m_list.GetItemCount() == 2);
    CHECK(m_list.GetItemText(1, 1, str) == 2 && str == _T("42"));
    CHECK(m_list.GetItemText(0, 0, str) && str == _T("alpha"));
    CHECK(m_list.SetItemState(1, LVIS_SELECTED, LVIS_SELECTED));
    CHECK(m_list.GetSelectedIndex() == 1);
    CHECK(m_list.DeleteItem(0));
    CHECK(m_list.GetItemCount() == 1);
  }

  void TestDialogs() {
    // Modal, with data exchange and resizing.
    CDdxDlg dlg;
    CHECK(dlg.DoModal(m_hWnd) == IDOK);
    CHECK(dlg.m_bRan);

    // In memory, modeless.
    CMemDlg mem;
    HWND hMem = mem.Create(m_hWnd);
    CHECK(hMem != NULL);
    if (hMem != NULL) {
      CHECK(mem.GetDlgItem(IDC_NAME).IsWindow());
      CHECK(mem.GetDlgItem(IDOK).IsWindow());
      TCHAR szClass[32] = _T("");
      ::GetClassName(mem.GetDlgItem(IDC_FLAG), szClass, 32);
      CHECK(lstrcmpi(szClass, _T("Button")) == 0);
      TCHAR szCaption[32] = _T("");
      mem.GetWindowText(szCaption, 32);
      CHECK(lstrcmp(szCaption, _T("Memory")) == 0);
      int n = 0;
      for (HWND h = ::GetWindow(hMem, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT)) n++;
      CHECK(n == 4);
      mem.DestroyWindow();
    }

    // A modeless property sheet.
    CPage1 page1;
    CPage2 page2;
    CPropertySheet sheet(_T("Sheet"));
    CHECK(sheet.AddPage(page1));
    CHECK(sheet.AddPage(page2));
    CHECK(sheet.GetPageCount() == 2);
    HWND hSheet = sheet.Create(m_hWnd);
    CHECK(hSheet != NULL);
    if (hSheet != NULL) {
      CHECK(page1.m_bInit);
      CHECK(sheet.GetActiveIndex() == 0);
      TCHAR sz[32] = _T("");
      ::GetDlgItemText(page1, IDC_NAME, sz, 32);
      CHECK(lstrcmp(sz, _T("page one")) == 0);
      CHECK(sheet.SetActivePage(1));
      CHECK(sheet.GetActiveIndex() == 1);
      CHECK(page2.m_nActive == 1);
      CHECK(sheet.GetActivePage() != NULL);
      sheet.DestroyWindow();
    }
  }

  void TestScroll() {
    CScrollTestView view;
    CRect rcView(0, 0, 100, 100);
    HWND h = view.Create(m_hWnd, rcView, NULL, WS_CHILD | WS_VISIBLE);
    CHECK(h != NULL);
    if (h == NULL) return;
    view.SetScrollSize(1000, 2000);
    CHECK(view.m_sizeAll.cx == 1000 && view.m_sizeAll.cy == 2000);
    SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE};
    CHECK(view.GetScrollInfo(SB_VERT, &si));
    CHECK(si.nMax == 1999);
    view.SetScrollOffset(50, 60);
    POINT pt = {};
    view.GetScrollOffset(pt);
    CHECK(pt.x == 50 && pt.y == 60);
    CHECK(view.GetScrollPos(SB_VERT) == 60);
    // A line down, through WM_VSCROLL.
    view.SendMessage(WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    view.GetScrollOffset(pt);
    CHECK(pt.y > 60);
    view.UpdateWindow();
    view.Invalidate();
    view.UpdateWindow();
    CHECK(view.m_nPaints > 0);
    view.DestroyWindow();
  }

  void TestGdiUser() {
    // A memory DC, a bitmap, a brush and a pen.
    CDCHandle screen = ::GetDC(NULL);
    CDC dc;
    CHECK(dc.CreateCompatibleDC(screen) != NULL);
    CBitmap bmp;
    CHECK(bmp.CreateCompatibleBitmap(screen, 16, 16) != NULL);
    ::ReleaseDC(NULL, screen);
    HBITMAP hOld = dc.SelectBitmap(bmp);
    CBrush brush;
    CHECK(brush.CreateSolidBrush(RGB(255, 0, 0)) != NULL);
    CRect rc(0, 0, 16, 16);
    dc.FillRect(&rc, brush);
    CHECK(dc.GetPixel(8, 8) == RGB(255, 0, 0));
    CPen pen;
    CHECK(pen.CreatePen(PS_SOLID, 1, RGB(0, 0, 255)) != NULL);
    HPEN hOldPen = dc.SelectPen(pen);
    dc.MoveTo(0, 2);
    dc.LineTo(16, 2);
    CHECK(dc.GetPixel(5, 2) == RGB(0, 0, 255));
    dc.SelectPen(hOldPen);
    BITMAP bm = {};
    CHECK(bmp.GetBitmap(&bm) && bm.bmWidth == 16 && bm.bmHeight == 16);
    CSize size;
    CHECK(bmp.GetSize(size) && size.cx == 16);

    // A font and the text it measures.
    CFont font;
    CHECK(font.CreatePointFont(100, _T("Arial")) != NULL);
    HFONT hOldFont = dc.SelectFont(font);
    CSize ext;
    CHECK(dc.GetTextExtent(_T("WTL"), 3, &ext) && ext.cx > 0 && ext.cy > 0);
    dc.SelectFont(hOldFont);
    LOGFONT lf = {};
    CHECK(font.GetLogFont(&lf));
    CHECK(lstrcmpi(lf.lfFaceName, _T("Arial")) == 0);
    dc.SelectBitmap(hOld);

    // A region.
    CRgn rgn;
    CHECK(rgn.CreateRectRgn(0, 0, 10, 10) != NULL);
    CHECK(rgn.PtInRegion(5, 5));
    CHECK(!rgn.PtInRegion(15, 5));

    // A menu, a string and a resource.
    CMenu menu;
    CHECK(menu.CreatePopupMenu());
    CHECK(menu.AppendMenu(MF_STRING, 1, _T("One")));
    CHECK(menu.AppendMenu(MF_SEPARATOR));
    CHECK(menu.GetMenuItemCount() == 2);
    CString strItem;
    CHECK(menu.GetMenuString(1, strItem, MF_BYCOMMAND) == 3 && strItem == _T("One"));
    CString strHello;
    CHECK(strHello.LoadString(IDS_HELLO) && strHello == _T("Hello from WTL"));
    CResource res;
    CHECK(res.Load(MAKEINTRESOURCE(241), IDR_MAINFRAME));
    CHECK(res.GetSize() == 9 * sizeof(WORD));
    const WORD* pw = (const WORD*)res.Lock();
    CHECK(pw != NULL && pw[0] == 1 && pw[3] == 5);

    // CFindFile over the Windows directory.
    TCHAR szWin[MAX_PATH];
    ::GetWindowsDirectory(szWin, MAX_PATH);
    lstrcat(szWin, _T("\\*.*"));
    CFindFile ff;
    CHECK(ff.FindFile(szWin));
    int nFiles = 0;
    BOOL bMore = TRUE;
    while (bMore && nFiles < 1000) {
      nFiles++;
      bMore = ff.FindNextFile();
    }
    CHECK(nFiles > 2);
    ff.Close();
  }
};

int main() {
  char szPath[MAX_PATH];
  DWORD n = ::GetEnvironmentVariableA("MFC_TEST_OUT", szPath, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 20) lstrcpyA(szPath, ".");
  lstrcatA(szPath, "/wtltest.log");
  g_pLog = fopen(szPath, "w");

  HRESULT hr = ::CoInitialize(NULL);
  CHECK(SUCCEEDED(hr));
  CHECK(AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES));
  hr = _Module.Init(NULL, ::GetModuleHandle(NULL));
  CHECK(SUCCEEDED(hr));
  Log("WTL %x on ATL %x", _WTL_VER, _ATL_VER);

  CMessageLoop theLoop;
  CHECK(_Module.AddMessageLoop(&theLoop));
  CMainFrame wnd;
  CHECK(wnd.CreateEx() != NULL);
  if (wnd.IsWindow()) {
    wnd.ShowWindow(SW_SHOW);
    theLoop.Run();
  }
  _Module.RemoveMessageLoop();
  _Module.Term();
  ::CoUninitialize();

  Log(g_nFailures ? "FAIL: %d failure(s)" : "PASS: %d failure(s)", g_nFailures);
  if (g_pLog != NULL) fclose(g_pLog);
  return g_nFailures ? 1 : 0;
}
