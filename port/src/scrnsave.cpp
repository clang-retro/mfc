// scrnsave.cpp - the screen saver library: WinMain, which reads the
// command line and runs the configuration dialog, the screen saver or its
// preview, and DefScreenSaverProc. Built two times: libscrnsave.a (ANSI)
// and, with UNICODE, libscrnsavw.a, as on Windows.
//
// The command line is as on Windows: /s (or -s) runs the screen saver
// over the whole screen, /p hwnd (or /l hwnd) runs it in a child of that
// window, /c (with an optional parent window) and no argument show the
// configuration dialog, and /a changes the password (no passwords here).
// On the classic Mac OS, which has no command lines, no argument runs the
// screen saver: the user opens the application to see it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <scrnsave.h>
#include <stdlib.h>

extern "C" {
HINSTANCE hMainInstance;
HWND hMainWindow;
BOOL fChildPreview;
TCHAR szName[TITLEBARNAMELEN];
TCHAR szAppName[APPNAMEBUFFERLEN];
TCHAR szIniFile[MAXFILELEN];
TCHAR szScreenSaver[22];
TCHAR szHelpFile[MAXFILELEN];
TCHAR szNoHelpMemory[BUFFLEN];
UINT MyHelpMessage;
}

namespace {

// The window class of the screen saver, as on Windows.
const TCHAR kClass[] = {'W', 'i', 'n', 'd', 'o', 'w', 's', 'S', 'c', 'r', 'e', 'e', 'n', 'S', 'a',
                        'v', 'e', 'r', 'C', 'l', 'a', 's', 's', 0};

// The mouse can move this many pixels before the screen saver closes.
const int kMouseSlack = 4;

// The first position of the mouse, after the screen saver started.
POINT g_firstMouse;
BOOL g_haveMouse;

// The modes of the command line.
enum Mode { kConfigure, kSave, kPreview, kPassword };

// Reads a decimal window handle from the command line.
HWND ReadWindow(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == ':') p++;
  return (HWND)(UINT_PTR)strtoul(p, NULL, 10);
}

// Reads the mode and its window from the command line.
Mode ReadCommandLine(const char* p, HWND* window) {
  *window = NULL;
  while (*p == ' ' || *p == '\t') p++;
#if defined(macintosh) || defined(__MSDOS__)
  if (!*p) return kSave;
#endif
  if (*p != '/' && *p != '-') return kConfigure;
  char c = p[1];
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  switch (c) {
    case 's':
      return kSave;
    case 'p':
    case 'l':
      *window = ReadWindow(p + 2);
      return *window ? kPreview : kConfigure;
    case 'a':
      *window = ReadWindow(p + 2);
      return kPassword;
    default:
      *window = ReadWindow(p + 2);
      return kConfigure;
  }
}

// Loads the library's strings from the program's resources.
void LoadStrings(HINSTANCE inst) {
  if (!LoadString(inst, IDS_DESCRIPTION, szName, TITLEBARNAMELEN)) szName[0] = 0;
  if (!LoadString(inst, idsAppName, szAppName, APPNAMEBUFFERLEN)) lstrcpyn(szAppName, szName, APPNAMEBUFFERLEN);
  if (!LoadString(inst, idsIniFile, szIniFile, MAXFILELEN)) szIniFile[0] = 0;
  if (!LoadString(inst, idsScreenSaver, szScreenSaver, 22)) szScreenSaver[0] = 0;
  if (!LoadString(inst, idsHelpFile, szHelpFile, MAXFILELEN)) szHelpFile[0] = 0;
  if (!LoadString(inst, idsNoHelpMemory, szNoHelpMemory, BUFFLEN)) szNoHelpMemory[0] = 0;
}

// Shows the configuration dialog, after the program registers its
// classes.
int Configure(HINSTANCE inst, HWND parent) {
  if (!RegisterDialogClasses(inst)) return 0;
  return (int)DialogBox(inst, MAKEINTRESOURCE(DLG_SCRNSAVECONFIGURE), parent,
                        (DLGPROC)ScreenSaverConfigureDialog);
}

// Makes the screen saver's window, over the whole screen or in the
// preview window, and runs its messages until it closes.
int Run(HINSTANCE inst, HWND parent) {
  WNDCLASS wc = {};
  wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = ScreenSaverProc;
  wc.hInstance = inst;
  wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(ID_APP));
  wc.hCursor = NULL;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kClass;
  if (!RegisterClass(&wc)) return 1;
  fChildPreview = parent != NULL;
  if (fChildPreview) {
    RECT r;
    GetClientRect(parent, &r);
    hMainWindow = CreateWindowEx(0, kClass, szName, WS_CHILD | WS_VISIBLE, 0, 0, r.right, r.bottom,
                                 parent, NULL, inst, NULL);
  } else {
    hMainWindow = CreateWindowEx(WS_EX_TOPMOST, kClass, szName, WS_POPUP | WS_VISIBLE, 0, 0,
                                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL,
                                 NULL, inst, NULL);
  }
  if (!hMainWindow) return 1;
  if (!fChildPreview) SetForegroundWindow(hMainWindow);
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return (int)msg.wParam;
}

}  // namespace

extern "C" {

// Closes the screen saver when the user moves the mouse, clicks or
// types, hides the cursor, and ends the program when the window goes.
LRESULT WINAPI DefScreenSaverProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (!fChildPreview) {
    switch (msg) {
      case WM_SETCURSOR:
        SetCursor(NULL);
        return TRUE;
      case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        if (!g_haveMouse) {
          g_firstMouse = pt;
          g_haveMouse = TRUE;
        } else if (abs(pt.x - g_firstMouse.x) > kMouseSlack || abs(pt.y - g_firstMouse.y) > kMouseSlack) {
          PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        break;
      }
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
      case WM_ACTIVATEAPP:
        if (!wp) PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
      case WM_SYSCOMMAND:
        // A second screen saver, or closing by the menu: not while it runs.
        if ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_CLOSE) return FALSE;
        break;
    }
  }
  switch (msg) {
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

// There are no screen saver passwords here: nothing to change.
void WINAPI ScreenSaverChangePassword(HWND) {}

// Reads the command line and runs the configuration dialog, the screen
// saver or its preview.
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int) {
  hMainInstance = inst;
  LoadStrings(inst);
  MyHelpMessage = RegisterWindowMessage(TEXT("commdlg_help"));
  HWND window;
  switch (ReadCommandLine(cmd ? cmd : "", &window)) {
    case kSave:
      return Run(inst, NULL);
    case kPreview:
      return Run(inst, window);
    case kPassword:
      ScreenSaverChangePassword(window);
      return 0;
    case kConfigure:
    default:
      return Configure(inst, window);
  }
}

}  // extern "C"
