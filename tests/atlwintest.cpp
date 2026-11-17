// atlwintest.cpp - ATL's windows, without MFC:
//   - a CWindowImpl frame with its own window class: its message map
//     (MESSAGE_HANDLER, COMMAND_ and NOTIFY_ handlers, bHandled FALSE
//     going on to DefWindowProc, a chained map), GetCurrentMessage, the
//     style from its traits, OnFinalMessage once destroyed;
//   - a CContainedWindow edit control whose messages go to an alternate
//     map of its parent;
//   - a superclassed button (DECLARE_WND_SUPERCLASS) whose notifications
//     its parent reflects back to it;
//   - SubclassWindow and UnsubclassWindow on a static control;
//   - CWindow's wrappers;
//   - CDialogImpl, modal (DoModal and EndDialog, the dialog result
//     through DWLP_MSGRESULT) and modeless, and CSimpleDialog.
// Writes atlwintest.log in MFC_TEST_OUT; exits with 1 on a failure.
#include <atlbase.h>
#include <atlwin.h>
#include <stdio.h>
#include <stdarg.h>

#define IDD_ATLDLG 200
#define IDD_ATLSIMPLE 201
#define IDC_NAME 1001

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

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      Log("FAIL %s:%d: %s", __FILE__, __LINE__, #c);      \
      g_nFailures++;                                      \
    }                                                     \
  } while (0)

static const UINT WM_TEST_HANDLED = WM_APP + 1;
static const UINT WM_TEST_UNHANDLED = WM_APP + 2;
static const UINT WM_TEST_CHAINED = WM_APP + 3;
static const UINT WM_TEST_CURRENT = WM_APP + 4;
static const UINT WM_TEST_RANGE = WM_APP + 10;

// A map of its own, chained into the frame's.
class CChained {
public:
  int m_nCalls = 0;
  BEGIN_MSG_MAP(CChained)
    MESSAGE_HANDLER(WM_TEST_CHAINED, OnChained)
  END_MSG_MAP()
  LRESULT OnChained(UINT, WPARAM wParam, LPARAM, BOOL&) {
    m_nCalls++;
    return (LRESULT)wParam * 2;
  }
};

// A button whose own map handles the clicks its parent reflects.
class CReflectButton : public CWindowImpl<CReflectButton> {
public:
  int m_nClicks = 0;
  DECLARE_WND_SUPERCLASS(_T("AtlTestButton"), _T("BUTTON"))
  BEGIN_MSG_MAP(CReflectButton)
    MESSAGE_HANDLER(OCM_COMMAND, OnReflectedCommand)
    DEFAULT_REFLECTION_HANDLER()
  END_MSG_MAP()
  LRESULT OnReflectedCommand(UINT, WPARAM wParam, LPARAM, BOOL&) {
    if (HIWORD(wParam) == BN_CLICKED) m_nClicks++;
    return 0;
  }
};

class CFrame : public CWindowImpl<CFrame, CWindow, CFrameWinTraits>, public CChained {
public:
  int m_nCreate = 0, m_nCommand = 0, m_nNotify = 0, m_nRange = 0, m_nEditChars = 0;
  int m_nFinal = 0, m_nParentCommands = 0;
  HWND m_hWndFinal = NULL;
  UINT m_uCurrent = 0;
  CContainedWindow m_edit;
  CReflectButton m_button;

  CFrame() : m_edit(_T("EDIT"), this, 1) {}

  DECLARE_WND_CLASS(_T("AtlTestFrame"))

  BEGIN_MSG_MAP(CFrame)
    MESSAGE_HANDLER(WM_CREATE, OnCreate)
    MESSAGE_HANDLER(WM_TEST_HANDLED, OnHandled)
    MESSAGE_HANDLER(WM_TEST_UNHANDLED, OnUnhandled)
    MESSAGE_HANDLER(WM_TEST_CURRENT, OnCurrent)
    MESSAGE_RANGE_HANDLER(WM_TEST_RANGE, WM_TEST_RANGE + 5, OnRange)
    COMMAND_ID_HANDLER(100, OnCommand100)
    COMMAND_HANDLER(101, 7, OnCommand101)
    NOTIFY_CODE_HANDLER(NM_CLICK, OnClick)
    COMMAND_CODE_HANDLER(BN_CLICKED, OnParentClicked)
    CHAIN_MSG_MAP(CChained)
    REFLECT_NOTIFICATIONS()
  ALT_MSG_MAP(1)
    MESSAGE_HANDLER(WM_CHAR, OnEditChar)
  END_MSG_MAP()

  LRESULT OnCreate(UINT, WPARAM, LPARAM lParam, BOOL&) {
    m_nCreate++;
    CHECK(((LPCREATESTRUCT)lParam)->lpCreateParams == (void*)0x1234);
    RECT rc = {10, 10, 200, 40};
    CHECK(m_edit.Create(m_hWnd, rc, _T(""), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 50) != NULL);
    RECT rcButton = {10, 50, 120, 80};
    CHECK(m_button.Create(m_hWnd, rcButton, _T("Button"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,
                          60) != NULL);
    return 0;
  }
  LRESULT OnHandled(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
    return (LRESULT)(wParam + lParam);
  }
  LRESULT OnUnhandled(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    bHandled = FALSE;  // on to DefWindowProc, which answers 0
    return 99;
  }
  LRESULT OnCurrent(UINT uMsg, WPARAM, LPARAM, BOOL&) {
    const _ATL_MSG* pMsg = GetCurrentMessage();
    m_uCurrent = pMsg != NULL && pMsg->hwnd == m_hWnd ? pMsg->message : 0;
    return uMsg;
  }
  LRESULT OnRange(UINT uMsg, WPARAM, LPARAM, BOOL&) {
    m_nRange++;
    return uMsg - WM_TEST_RANGE;
  }
  LRESULT OnCommand100(WORD wNotifyCode, WORD wID, HWND, BOOL&) {
    m_nCommand += wID == 100 && wNotifyCode == 3;
    return 0;
  }
  LRESULT OnCommand101(WORD, WORD, HWND, BOOL&) {
    m_nCommand += 10;
    return 0;
  }
  LRESULT OnClick(int idCtrl, LPNMHDR pnmh, BOOL&) {
    m_nNotify += idCtrl == 77 && pnmh->idFrom == 77;
    return 5;
  }
  // The parent sees a button's click only when it lets it go on to be
  // reflected.
  LRESULT OnParentClicked(WORD, WORD, HWND, BOOL& bHandled) {
    m_nParentCommands++;
    bHandled = FALSE;
    return 0;
  }
  // The edit's characters: 'x' is swallowed, the rest go on to the edit.
  LRESULT OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
    m_nEditChars++;
    if (wParam != 'x') bHandled = FALSE;
    return 0;
  }

  void OnFinalMessage(HWND hWnd) override {
    m_nFinal++;
    m_hWndFinal = hWnd;
  }
};

// A static control subclassed for a while.
class CSubclassed : public CWindowImpl<CSubclassed> {
public:
  int m_nCalls = 0;
  BEGIN_MSG_MAP(CSubclassed)
    MESSAGE_HANDLER(WM_SETTEXT, OnSetText)
  END_MSG_MAP()
  LRESULT OnSetText(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    m_nCalls++;
    bHandled = FALSE;  // the static control still takes the text
    return 0;
  }
};

class CTestDialog : public CDialogImpl<CTestDialog> {
public:
  enum { IDD = IDD_ATLDLG };
  int m_nInit = 0;
  LPARAM m_initParam = 0;
  TCHAR m_szName[64] = {0};
  BOOL m_bModeless = FALSE;
  int m_nFinal = 0;

  BEGIN_MSG_MAP(CTestDialog)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    MESSAGE_HANDLER(WM_APP, OnApp)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM lParam, BOOL&) {
    m_nInit++;
    m_initParam = lParam;
    SetDlgItemText(IDC_NAME, _T("atl"));
    CenterWindow();
    if (!m_bModeless) PostMessage(WM_COMMAND, IDOK);
    return TRUE;
  }
  // A dialog's result goes through DWLP_MSGRESULT.
  LRESULT OnApp(UINT, WPARAM wParam, LPARAM, BOOL&) { return (LRESULT)wParam + 1; }
  LRESULT OnOK(WORD, WORD, HWND, BOOL&) {
    GetDlgItemText(IDC_NAME, m_szName, 64);
    LRESULT r = ::SendMessage(m_hWnd, WM_APP, 41, 0);
    EndDialog(r == 42 ? 42 : 1);
    return 0;
  }
  LRESULT OnCancel(WORD, WORD, HWND, BOOL&) {
    EndDialog(IDCANCEL);
    return 0;
  }
  void OnFinalMessage(HWND) override { m_nFinal++; }
};

// Presses a button of each simple dialog as it comes up.
static HHOOK g_hHook;
static WPARAM g_wPress = IDNO;
static LRESULT CALLBACK CbtProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HCBT_ACTIVATE) {
    TCHAR szClass[16];
    if (::GetClassName((HWND)wParam, szClass, 16) && lstrcmp(szClass, _T("#32770")) == 0)
      ::PostMessage((HWND)wParam, WM_COMMAND, g_wPress, 0);
  }
  return ::CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

static void TestFrame() {
  CFrame frame;
  RECT rc = {50, 50, 450, 350};
  HWND hWnd = frame.Create(NULL, rc, _T("ATL frame"), 0, 0, 0U, (LPVOID)0x1234);
  CHECK(hWnd != NULL);
  if (hWnd == NULL) return;
  CHECK(frame.m_hWnd == hWnd && frame.m_nCreate == 1);
  // The class and the style from the traits.
  TCHAR szClass[64];
  ::GetClassName(hWnd, szClass, 64);
  CHECK(lstrcmp(szClass, _T("AtlTestFrame")) == 0);
  CHECK((frame.GetStyle() & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
  CHECK((frame.GetExStyle() & WS_EX_APPWINDOW) != 0);
  frame.ShowWindow(SW_SHOW);

  // The map.
  CHECK(frame.SendMessage(WM_TEST_HANDLED, 3, 4) == 7);
  CHECK(frame.SendMessage(WM_TEST_UNHANDLED) == 0);
  CHECK(frame.SendMessage(WM_TEST_CURRENT) == (LRESULT)WM_TEST_CURRENT &&
        frame.m_uCurrent == WM_TEST_CURRENT);
  CHECK(frame.GetCurrentMessage() == NULL);
  CHECK(frame.SendMessage(WM_TEST_RANGE + 3) == 3 && frame.m_nRange == 1);
  CHECK(frame.SendMessage(WM_TEST_CHAINED, 21) == 42 && frame.m_nCalls == 1);
  frame.SendMessage(WM_COMMAND, MAKEWPARAM(100, 3), 0);
  frame.SendMessage(WM_COMMAND, MAKEWPARAM(101, 7), 0);
  frame.SendMessage(WM_COMMAND, MAKEWPARAM(101, 8), 0);  // no handler
  CHECK(frame.m_nCommand == 11);
  NMHDR nm = {NULL, 77, (UINT)NM_CLICK};
  CHECK(frame.SendMessage(WM_NOTIFY, 77, (LPARAM)&nm) == 5 && frame.m_nNotify == 1);

  // The contained edit: its characters through the frame's ALT map.
  CHECK(frame.m_edit.m_hWnd != NULL);
  ::GetClassName(frame.m_edit, szClass, 64);
  CHECK(lstrcmp(szClass, _T("ATL:EDIT")) == 0);
  frame.m_edit.SendMessage(WM_CHAR, 'x', 1);
  frame.m_edit.SendMessage(WM_CHAR, 'y', 1);
  CHECK(frame.m_nEditChars == 2);
  TCHAR szText[32];
  frame.m_edit.GetWindowText(szText, 32);
  CHECK(lstrcmp(szText, _T("y")) == 0);
  CHECK(frame.m_edit.GetDlgCtrlID() == 50);

  // The superclassed button: a button still, its clicks reflected to it.
  ::GetClassName(frame.m_button, szClass, 64);
  CHECK(lstrcmp(szClass, _T("AtlTestButton")) == 0);
  CHECK(frame.m_button.GetDlgCtrlID() == 60);
  frame.m_button.GetWindowText(szText, 32);
  CHECK(lstrcmp(szText, _T("Button")) == 0);
  frame.m_button.SendMessage(BM_CLICK);
  CHECK(frame.m_nParentCommands == 1);
  CHECK(frame.m_button.m_nClicks == 1);
  BSTR bstr = NULL;
  CHECK(frame.m_button.GetWindowText(&bstr) && bstr != NULL && lstrcmpW(bstr, L"Button") == 0);
  ::SysFreeString(bstr);

  // Subclassing a static control, and giving it back.
  CWindow stat;
  RECT rcStatic = {10, 100, 200, 120};
  stat.Create(_T("STATIC"), frame, rcStatic, _T("one"), WS_CHILD | WS_VISIBLE, 0, 70);
  CHECK(stat.m_hWnd != NULL && frame.GetDlgItem(70) == stat.m_hWnd);
  WNDPROC pOrig = (WNDPROC)stat.GetWindowLongPtr(GWLP_WNDPROC);
  CSubclassed sub;
  CHECK(sub.SubclassWindow(stat));
  stat.SetWindowText(_T("two"));
  CHECK(sub.m_nCalls == 1);
  stat.GetWindowText(szText, 32);
  CHECK(lstrcmp(szText, _T("two")) == 0);
  CHECK(sub.UnsubclassWindow() == stat.m_hWnd);
  CHECK((WNDPROC)stat.GetWindowLongPtr(GWLP_WNDPROC) == pOrig);
  stat.SetWindowText(_T("three"));
  CHECK(sub.m_nCalls == 1);

  // CWindow's wrappers.
  CHECK(frame.SetDlgItemInt(70, 1234) && frame.GetDlgItemInt(70) == 1234);
  CHECK(frame.GetDlgItemTextLength(70) == 4);
  CHECK(stat.GetParent() == frame.m_hWnd && stat.GetTopLevelParent() == frame.m_hWnd);
  CHECK(frame.IsChild(stat));
  CHECK(frame.GetDescendantWindow(60) == frame.m_button.m_hWnd);
  CHECK(stat.ModifyStyle(0, SS_CENTER) && (stat.GetStyle() & SS_CENTER));
  CHECK(!stat.ModifyStyle(0, SS_CENTER));
  CHECK(stat.MoveWindow(5, 6, 70, 20));
  RECT rcGot;
  stat.GetWindowRect(&rcGot);
  frame.ScreenToClient(&rcGot);
  CHECK(rcGot.left == 5 && rcGot.top == 6 && rcGot.right - rcGot.left == 70);
  CHECK(frame.ResizeClient(300, 200));
  frame.GetClientRect(&rcGot);
  CHECK(rcGot.right == 300 && rcGot.bottom == 200);
  CHECK(frame.CenterWindow());
  CHECK(frame.IsWindow() && frame.IsWindowVisible());

  // Destruction: OnFinalMessage once, with the window it had.
  CHECK(frame.DestroyWindow());
  CHECK(frame.m_nFinal == 1 && frame.m_hWndFinal == hWnd && frame.m_hWnd == NULL);
  CHECK(frame.m_edit.m_hWnd == NULL);
  CHECK(!::IsWindow(hWnd));
}

static void TestDialogs() {
  CTestDialog dlg;
  INT_PTR nResult = dlg.DoModal(NULL, 77);
  CHECK(nResult == 42);
  CHECK(dlg.m_nInit == 1 && dlg.m_initParam == 77);
  CHECK(lstrcmp(dlg.m_szName, _T("atl")) == 0);
  CHECK(dlg.m_hWnd == NULL && dlg.m_nFinal == 1);

  // Modeless.
  CTestDialog modeless;
  modeless.m_bModeless = TRUE;
  HWND hWnd = modeless.Create(NULL, 5);
  CHECK(hWnd != NULL && modeless.m_nInit == 1 && modeless.m_initParam == 5);
  if (hWnd != NULL) {
    TCHAR sz[16];
    modeless.GetDlgItemText(IDC_NAME, sz, 16);
    CHECK(lstrcmp(sz, _T("atl")) == 0);
    CHECK(modeless.SendMessage(WM_APP, 9) == 10);
    CHECK(modeless.DestroyWindow());
    CHECK(modeless.m_nFinal == 1 && modeless.m_hWnd == NULL);
  }

  // CSimpleDialog ends with the button pressed.
  g_hHook = ::SetWindowsHookEx(WH_CBT, CbtProc, NULL, ::GetCurrentThreadId());
  CSimpleDialog<IDD_ATLSIMPLE> simple;
  g_wPress = IDNO;
  CHECK(simple.DoModal(NULL) == IDNO);
  CSimpleDialog<IDD_ATLSIMPLE, FALSE> simple2;
  g_wPress = IDOK;
  CHECK(simple2.DoModal(NULL) == IDOK);
  ::UnhookWindowsHookEx(g_hHook);
}

int main() {
  char szPath[MAX_PATH];
  DWORD n = ::GetEnvironmentVariableA("MFC_TEST_OUT", szPath, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 20) lstrcpyA(szPath, ".");
  lstrcatA(szPath, "\\atlwintest.log");
  g_pLog = fopen(szPath, "w");
  CComModule module;
  module.Init(NULL, ::GetModuleHandle(NULL));

  TestFrame();
  TestDialogs();

  module.Term();
  Log("%s: %d failure(s)", g_nFailures ? "FAIL" : "PASS", g_nFailures);
  if (g_pLog != NULL) fclose(g_pLog);
  return g_nFailures ? 1 : 0;
}
