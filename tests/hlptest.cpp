// hlptest.cpp - the WinHelp viewer of the portable layer: the help files
// of tests/winhelp opened through WinHelp and CWinApp::WinHelp. A file
// that halibut made (guide.hlp, with its contents file) for the
// contents, the index, the browse sequence, the Help Topics dialog box
// and the macros; and two that mkhlp.py made (feature.hlp, with
// |Phrases, and feature95.hlp, with Hall compression) for [MAP]
// numbers, the region that does not scroll, a table, a picture and its
// hotspot, popups, a macro hotspot and context-sensitive help.
//
// The test drives the viewer through its windows: the main window is of
// the class MS_WINHELP, and the text of each of its topic panes (and of
// a popup) is the plain text of the topic it shows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string>
#include "testutil.h"

// Runs the messages that are waiting, for about ms milliseconds.
static void Pump(DWORD ms = 100) {
  DWORD end = ::GetTickCount() + ms;
  do {
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        ::PostQuitMessage((int)msg.wParam);
        return;
      }
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
    ::Sleep(5);
  } while ((LONG)(::GetTickCount() - end) < 0);
}

// The directory of the test help files.
static std::wstring HelpDir() {
  const char* dir = getenv("MFC_TEST_HLP_DIR");
  return std::wstring(CStringW(dir && *dir ? dir : "tests/winhelp"));
}

// The viewer's main window, or NULL.
static HWND Frame() { return ::FindWindowW(L"MS_WINHELP", NULL); }

// A window's text.
static std::wstring Text(HWND h) {
  if (!h) return std::wstring();
  int n = ::GetWindowTextLengthW(h);
  std::wstring s(n + 1, L'\0');
  ::GetWindowTextW(h, &s[0], n + 1);
  s.resize(wcslen(s.c_str()));
  return s;
}

// The topic panes of the main window: 0 the region that does not
// scroll, 1 the one that does.
static HWND Pane(int which) {
  HWND f = Frame();
  if (!f) return NULL;
  HWND p = ::FindWindowExW(f, NULL, L"WinportHelpTopic", NULL);
  return which == 0 ? p : ::FindWindowExW(f, p, L"WinportHelpTopic", NULL);
}

// Whether the scrolling region's text has a string.
static bool Shows(const wchar_t* s) { return Text(Pane(1)).find(s) != std::wstring::npos; }

// Presses a key in a window.
static void Key(HWND h, UINT vk) {
  ::SendMessageW(h, WM_KEYDOWN, vk, 0);
  Pump(20);
}

// Clicks a button of the button bar by its text.
static bool ClickButton(const wchar_t* text) {
  HWND f = Frame();
  HWND b = f ? ::FindWindowExW(f, NULL, L"BUTTON", text) : NULL;
  if (!b || !::IsWindowEnabled(b)) return false;
  ::SendMessageW(f, WM_COMMAND, MAKEWPARAM(::GetDlgCtrlID(b), BN_CLICKED), (LPARAM)b);
  Pump(20);
  return true;
}

// Counts the pixels of a color in a window.
static int CountColor(HWND h, COLORREF color) {
  ::UpdateWindow(h);
  RECT rc;
  ::GetClientRect(h, &rc);
  int cx = rc.right, cy = rc.bottom;
  if (cx <= 0 || cy <= 0) return 0;
  HDC wdc = ::GetDC(h);
  HDC mdc = ::CreateCompatibleDC(wdc);
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = cx;
  bmi.bmiHeader.biHeight = -cy;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  void* bits = NULL;
  HBITMAP bm = ::CreateDIBSection(wdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ old = ::SelectObject(mdc, bm);
  ::BitBlt(mdc, 0, 0, cx, cy, wdc, 0, 0, SRCCOPY);
  ::GdiFlush();
  int n = 0;
  const DWORD* px = (const DWORD*)bits;
  DWORD want = (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
  for (int i = 0; i < cx * cy; i++)
    if ((px[i] & 0xFFFFFF) == want) n++;
  ::SelectObject(mdc, old);
  ::DeleteObject(bm);
  ::DeleteDC(mdc);
  ::ReleaseDC(h, wdc);
  return n;
}

// The halibut file: contents, index, keywords, browsing, Help Topics.
static void TestGuide() {
  // Named in upper case, as a Windows program might: found anyway.
  std::wstring hlp = HelpDir() + L"/GUIDE.HLP";
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTENTS, 0));
  Pump();
  HWND f = Frame();
  CHECK(f != NULL);
  CHECK(Text(f) == L"The Widget Guide");
  CHECK(Text(Pane(0)).find(L"The Widget Guide") != std::wstring::npos);
  CHECK(Shows(L"This is the help file that the tests of the WinHelp viewer read."));
  // The contents file is there: Help Topics; and the file's own buttons.
  CHECK(f && ::FindWindowExW(f, NULL, L"BUTTON", L"Help &Topics") != NULL);
  CHECK(f && ::FindWindowExW(f, NULL, L"BUTTON", L"&About") != NULL);
  CHECK(!ClickButton(L"&Back"));  // nothing to go back to yet

  // A keyword of one topic: the topic.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_KEY, (ULONG_PTR)L"frame"));
  CHECK(Shows(L"Do these steps:"));
  CHECK(Shows(L"Type a name, and then click OK."));
  // Its jump, by the keyboard: to chapter 2.
  Key(Pane(1), VK_TAB);
  Key(Pane(1), VK_RETURN);
  CHECK(Shows(L"Section 2.1: The widget functions"));
  // Back, twice.
  CHECK(ClickButton(L"&Back"));
  CHECK(Shows(L"Do these steps:"));
  CHECK(ClickButton(L"&Back"));
  CHECK(Shows(L"This is the help file"));
  // A macro: JumpId to a context name; the browse sequence onward.
  CHECK(::WinHelpA(NULL, CStringA(hlp.c_str()), HELP_COMMAND, (ULONG_PTR) "JumpId(`',`t00000003')"));
  CHECK(Shows(L"CreateWidget(name, flags);"));
  CHECK(Shows(L"DestroyWidget removes it."));
  CHECK(ClickButton(L"<<"));
  CHECK(Text(Pane(0)).find(L"Chapter 2: Reference") != std::wstring::npos);

  // The Help Topics dialog box: the contents file's tree.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_FINDER, 0));
  Pump();
  HWND dlg = ::FindWindowW(NULL, L"Help Topics: The Widget Guide");
  CHECK(dlg != NULL);
  if (dlg) {
    CHECK(::SendDlgItemMessageW(dlg, 101, TVM_GETCOUNT, 0, 0) == 7);
    // The Index tab: typing selects the keyword; Display shows its topic.
    HWND tab = ::GetDlgItem(dlg, 100);
    CHECK(::SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 3);
    ::SendMessageW(tab, TCM_SETCURSEL, 1, 0);
    NMHDR nm = {tab, 100, (UINT)TCN_SELCHANGE};
    ::SendMessageW(dlg, WM_NOTIFY, 100, (LPARAM)&nm);
    CHECK(::SendDlgItemMessageW(dlg, 203, LB_GETCOUNT, 0, 0) == 4);
    ::SetDlgItemTextW(dlg, 201, L"fra");
    Pump(20);
    CHECK(::SendDlgItemMessageW(dlg, 203, LB_GETCURSEL, 0, 0) == 1);
    ::SendMessageW(dlg, WM_COMMAND, IDOK, 0);
    Pump();
    CHECK(!::IsWindow(dlg));
    CHECK(Shows(L"Do these steps:"));
  }
  // The Find tab: the topics with all the words typed.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_FINDER, 0));
  Pump();
  dlg = ::FindWindowW(NULL, L"Help Topics: The Widget Guide");
  CHECK(dlg != NULL);
  if (dlg) {
    HWND tab = ::GetDlgItem(dlg, 100);
    ::SendMessageW(tab, TCM_SETCURSEL, 2, 0);
    NMHDR nm = {tab, 100, (UINT)TCN_SELCHANGE};
    ::SendMessageW(dlg, WM_NOTIFY, 100, (LPARAM)&nm);
    ::SetDlgItemTextW(dlg, 301, L"destroywidget removes");
    Pump(20);
    CHECK(::SendDlgItemMessageW(dlg, 303, LB_GETCOUNT, 0, 0) == 1);
    ::SendDlgItemMessageW(dlg, 303, LB_SETCURSEL, 0, 0);
    ::SendMessageW(dlg, WM_COMMAND, IDOK, 0);
    Pump();
    CHECK(Shows(L"CreateWidget makes a widget."));
  }
  TestSaveWindowBitmap(Frame(), _T("guide.bmp"));
  // HELP_QUIT closes the viewer.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_QUIT, 0));
  Pump();
  CHECK(Frame() == NULL);
}

// A file of mkhlp.py: [MAP] numbers, phrases, a table, a picture, popups.
static void TestFeature(const wchar_t* name) {
  TestLog("feature file %ls", name);
  std::wstring hlp = HelpDir() + L"/" + name;
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTEXT, 100));
  Pump();
  HWND f = Frame();
  CHECK(f != NULL);
  CHECK(Text(f) == L"Feature Window");  // the caption of its main window
  CHECK(Text(Pane(0)) == L"Feature Test\n");
  // The text, through the phrases; the table's rows, a cell a tab.
  CHECK(Shows(L"This topic is the topic of the tests. Jump to the second topic, see a popup, or run a macro."));
  CHECK(Shows(L"Name\tValue\nAlpha\tOne\n"));
  CHECK(Shows(L"code sample"));
  // The picture: its blue and red halves.
  CHECK(CountColor(Pane(1), RGB(0, 0, 255)) >= 16 * 16);
  CHECK(CountColor(Pane(1), RGB(255, 0, 0)) >= 16 * 16);
  TestSaveWindowBitmap(f, CString(name) + _T(".bmp"));

  // The hotspots, in order: the jump, the popup, the macro, the picture's.
  HWND p = Pane(1);
  Key(p, VK_TAB);
  Key(p, VK_RETURN);
  CHECK(Shows(L"The second topic of the tests."));
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_COMMAND, (ULONG_PTR)L"Back()"));
  CHECK(Shows(L"This topic is the topic"));
  p = Pane(1);
  Key(p, VK_TAB);
  Key(p, VK_TAB);
  Key(p, VK_RETURN);
  HWND pop = ::FindWindowW(L"WinportHelpPopup", NULL);
  CHECK(pop != NULL);
  CHECK(Text(pop) == L"The text of the popup topic.\n");
  if (pop) {
    TestSaveWindowBitmap(pop, CString(name) + _T("-popup.bmp"));
    Key(pop, VK_ESCAPE);
  }
  CHECK(::FindWindowW(L"WinportHelpPopup", NULL) == NULL);
  p = Pane(1);
  Key(p, VK_TAB);
  Key(p, VK_TAB);
  Key(p, VK_TAB);
  Key(p, VK_RETURN);  // JumpId(`',`second')
  CHECK(Shows(L"The second topic of the tests."));
  CHECK(ClickButton(L"&Back"));
  p = Pane(1);
  for (int i = 0; i < 4; i++) Key(p, VK_TAB);
  Key(p, VK_RETURN);  // the picture's hotspot
  CHECK(Shows(L"The second topic of the tests."));
  // The browse sequence: back to the first.
  CHECK(ClickButton(L"<<"));
  CHECK(Shows(L"This topic is the topic"));
  CHECK(!ClickButton(L"<<"));

  // Popups by themselves: HELP_CONTEXTPOPUP and a control's help.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTEXTPOPUP, 300));
  Pump();
  pop = ::FindWindowW(L"WinportHelpPopup", NULL);
  CHECK(Text(pop) == L"The text of the popup topic.\n");
  if (pop) Key(pop, VK_ESCAPE);
  HWND button = ::CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE, 0, 0, 40, 20, f, (HMENU)1234, NULL, NULL);
  static const DWORD ids[] = {1234, 300, 0, 0};
  CHECK(::WinHelpW(button, hlp.c_str(), HELP_WM_HELP, (ULONG_PTR)ids));
  Pump();
  pop = ::FindWindowW(L"WinportHelpPopup", NULL);
  CHECK(pop != NULL);
  if (pop) Key(pop, VK_ESCAPE);
  ::DestroyWindow(button);

  // The contents topic can change.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_SETCONTENTS, 200));
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTENTS, 0));
  CHECK(Shows(L"The second topic of the tests."));

  // A keyword of two topics: the list of them, then the one chosen.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTEXT, 100));
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_KEY, (ULONG_PTR)L"alpha"));
  Pump();
  HWND found = ::FindWindowW(NULL, L"Topics Found");
  CHECK(found != NULL);
  if (found) {
    CHECK(::SendDlgItemMessageW(found, 400, LB_GETCOUNT, 0, 0) == 2);
    ::SendDlgItemMessageW(found, 400, LB_SETCURSEL, 1, 0);
    ::SendMessageW(found, WM_COMMAND, IDOK, 0);
    Pump();
    CHECK(Shows(L"The second topic of the tests."));
  }
  // A keyword of another index letter (here K, by HELP_MULTIKEY).
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_CONTEXT, 100));
  struct {
    DWORD mkSize;
    WCHAR mkKeylist;
    WCHAR szKeyphrase[8];
  } mk = {sizeof(mk), L'K', L"beta"};
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_MULTIKEY, (ULONG_PTR)&mk));
  CHECK(Shows(L"The second topic of the tests."));

  // Through CWinApp, with the program's help file.
  CWinApp* pApp = AfxGetApp();
  LPCTSTR pszOld = pApp->m_pszHelpFilePath;
  CString strHlp(hlp.c_str());
  pApp->m_pszHelpFilePath = strHlp;
  pApp->WinHelp(100, HELP_CONTEXT);
  CHECK(Shows(L"This topic is the topic"));
  pApp->m_pszHelpFilePath = pszOld;
  CHECK(::WinHelpW(NULL, NULL, HELP_QUIT, 0));
  Pump();
  CHECK(Frame() == NULL);
}

// A file that is not a help file: WinHelp falls back to a document, and
// fails without one.
static void TestNotHelp() {
  const char* out = getenv("MFC_TEST_OUT");
  std::string path = std::string(out && *out ? out : ".") + "/nothelp.hlp";
  if (FILE* fp = fopen(path.c_str(), "w")) {
    fputs("This is not a help file.\n", fp);
    fclose(fp);
  }
  ::SetLastError(0);
  CHECK(!::WinHelpA(NULL, path.c_str(), HELP_CONTENTS, 0));
  CHECK(::GetLastError() == ERROR_FILE_NOT_FOUND);
  remove(path.c_str());
}

class CHlpApp : public CWinApp {
 public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("hlptest.log"));
    CFrameWnd* pFrame = new CFrameWnd;
    if (!pFrame->Create(NULL, _T("hlptest"), WS_OVERLAPPEDWINDOW, CRect(20, 20, 300, 200))) {
      TestLog("frame creation failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    TestGuide();
    TestFeature(L"feature.hlp");
    TestFeature(L"feature95.hlp");
    TestNotHelp();
    pFrame->PostMessage(WM_CLOSE);
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CHlpApp theApp;
