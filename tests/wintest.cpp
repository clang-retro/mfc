// wintest.cpp - console tests for windows and drawing below the MFC
// classes: window classes and windows, the window tree and coordinates,
// sending and posting, the message loop, timers, messages between
// threads, hooks, ANSI and Unicode window procedures, focus, painting,
// device contexts, bitmaps and DIBs, raster operations, regions,
// clipping, mapping modes and scrolling, lines, shapes, paths and
// text, icons, accelerators and menus, dialogs and the standard
// controls, MDI, image lists, the common controls, the list view, the
// tree view and property sheets; printing and the print dialogs; window
// shapes (SetWindowRgn) and dropped files (WM_DROPFILES); a CWnd with a
// message map on top; and, here, monitors, layered windows, gradients and
// the shell namespace with its folder browser.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <prsht.h>
#include <shlobj.h>
#ifdef _WINPORT
#include <uxtheme.h>
#include <unistd.h>
#include <olectl.h>
#include <urlmon.h>
#endif
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

CWinApp theApp;

// ---------------------------------------------------------------------
// A window procedure that records what it is sent.

static std::vector<UINT> g_msgs;
static int g_paints = 0;
static int g_timers = 0;
static HWND g_focusFrom = NULL;
static BOOL g_inSend = FALSE;

static bool Seen(UINT msg) {
  for (UINT m : g_msgs)
    if (m == msg) return true;
  return false;
}

static int IndexOf(UINT msg) {
  for (size_t i = 0; i < g_msgs.size(); i++)
    if (g_msgs[i] == msg) return (int)i;
  return -1;
}

static LRESULT CALLBACK RecordProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  g_msgs.push_back(msg);
  switch (msg) {
    case WM_PAINT: {
      g_paints++;
      PAINTSTRUCT ps;
      HDC dc = ::BeginPaint(h, &ps);
      HBRUSH br = ::CreateSolidBrush(RGB(255, 0, 0));
      RECT rc;
      ::GetClientRect(h, &rc);
      ::FillRect(dc, &rc, br);
      ::DeleteObject(br);
      ::EndPaint(h, &ps);
      return 0;
    }
    case WM_TIMER:
      g_timers++;
      return 0;
    case WM_SETFOCUS:
      g_focusFrom = (HWND)wp;
      return 0;
    case WM_APP:
      g_inSend = ::InSendMessage();
      return wp + lp;
    case WM_APP + 1:
      return ::GetCurrentThreadId();
  }
  return ::DefWindowProcW(h, msg, wp, lp);
}

// An ANSI procedure for checking text conversion.
static char g_ansiText[64];
static LRESULT CALLBACK AnsiProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_SETTEXT && lp) lstrcpynA(g_ansiText, (LPCSTR)lp, sizeof g_ansiText);
  return ::DefWindowProcA(h, msg, wp, lp);
}

static const wchar_t kClass[] = L"WinTestClass";

static void Pump() {
  MSG m;
  while (::PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&m);
    ::DispatchMessageW(&m);
  }
}

static HWND NewTop(DWORD style, int x, int y, int cx, int cy) {
  return ::CreateWindowExW(0, kClass, L"Test", style, x, y, cx, cy, NULL, NULL, ::GetModuleHandleW(NULL), NULL);
}

// ---------------------------------------------------------------------

static void TestClassesAndCreation() {
  WNDCLASSW wc = {};
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = RecordProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kClass;
  wc.cbWndExtra = 2 * sizeof(LONG_PTR);
  CHECK(::RegisterClassW(&wc) != 0);
  CHECK(::RegisterClassW(&wc) == 0 && ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
  WNDCLASSW got;
  CHECK(::GetClassInfoW(::GetModuleHandleW(NULL), kClass, &got) && got.cbWndExtra == 2 * sizeof(LONG_PTR));

  g_msgs.clear();
  HWND h = NewTop(WS_OVERLAPPEDWINDOW, 10, 20, 300, 200);
  CHECK(h != NULL);
  // The creation messages, in order.
  CHECK(IndexOf(WM_GETMINMAXINFO) >= 0 && IndexOf(WM_GETMINMAXINFO) < IndexOf(WM_NCCREATE));
  CHECK(IndexOf(WM_NCCREATE) < IndexOf(WM_NCCALCSIZE) && IndexOf(WM_NCCALCSIZE) < IndexOf(WM_CREATE));
  CHECK(!::IsWindowVisible(h));
  wchar_t text[16];
  CHECK(::GetWindowTextW(h, text, 16) == 4 && lstrcmpW(text, L"Test") == 0);
  CHECK(::GetWindowTextLengthW(h) == 4);
  char textA[16];
  CHECK(::GetWindowTextA(h, textA, 16) == 4 && lstrcmpA(textA, "Test") == 0);
  wchar_t cls[32];
  CHECK(::GetClassNameW(h, cls, 32) && lstrcmpiW(cls, kClass) == 0);
  CHECK(::IsWindowUnicode(h));

  RECT wr, cr;
  ::GetWindowRect(h, &wr);
  ::GetClientRect(h, &cr);
  CHECK(wr.left == 10 && wr.top == 20 && wr.right == 310 && wr.bottom == 220);
  // The frame AdjustWindowRectEx adds is what the client area lost.
  RECT adj = {0, 0, cr.right, cr.bottom};
  ::AdjustWindowRectEx(&adj, WS_OVERLAPPEDWINDOW, FALSE, 0);
  CHECK(adj.right - adj.left == 300 && adj.bottom - adj.top == 200);

  // Extra bytes and user data.
  ::SetWindowLongPtrW(h, sizeof(LONG_PTR), 1234);
  CHECK(::GetWindowLongPtrW(h, sizeof(LONG_PTR)) == 1234);
  ::SetWindowLongPtrW(h, GWLP_USERDATA, 99);
  CHECK(::GetWindowLongPtrW(h, GWLP_USERDATA) == 99);
  CHECK((::GetWindowLongW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);

  // Properties.
  CHECK(::SetPropW(h, L"Prop", (HANDLE)42));
  CHECK(::GetPropW(h, L"Prop") == (HANDLE)42);
  CHECK(::RemovePropW(h, L"Prop") == (HANDLE)42 && ::GetPropW(h, L"Prop") == NULL);

  g_msgs.clear();
  CHECK(::DestroyWindow(h));
  CHECK(Seen(WM_DESTROY) && Seen(WM_NCDESTROY) && IndexOf(WM_DESTROY) < IndexOf(WM_NCDESTROY));
  CHECK(!::IsWindow(h));
}

static void TestTree() {
  HWND top = NewTop(WS_OVERLAPPEDWINDOW, 50, 60, 400, 300);
  HWND a = ::CreateWindowExW(0, kClass, L"a", WS_CHILD | WS_VISIBLE, 10, 10, 50, 40, top, (HMENU)101,
                             ::GetModuleHandleW(NULL), NULL);
  HWND b = ::CreateWindowExW(0, kClass, L"b", WS_CHILD | WS_VISIBLE, 70, 10, 50, 40, top, (HMENU)102,
                             ::GetModuleHandleW(NULL), NULL);
  CHECK(a && b);
  CHECK(::GetParent(a) == top && ::IsChild(top, a) && !::IsChild(a, top));
  CHECK(::GetDlgItem(top, 102) == b && ::GetDlgCtrlID(a) == 101);
  // Children stay in the order they were made.
  CHECK(::GetWindow(top, GW_CHILD) == a && ::GetWindow(a, GW_HWNDNEXT) == b);
  ::SetWindowPos(b, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  CHECK(::GetWindow(top, GW_CHILD) == b);

  POINT p = {0, 0};
  ::ClientToScreen(a, &p);
  POINT q = {0, 0};
  ::ClientToScreen(top, &q);
  CHECK(p.x == q.x + 10 && p.y == q.y + 10);
  POINT r = {5, 5};
  ::MapWindowPoints(a, b, &r, 1);
  CHECK(r.x == 5 - 60 && r.y == 5);
  POINT inside = {75, 15};
  CHECK(::ChildWindowFromPoint(top, inside) == b);

  ::MoveWindow(a, 20, 30, 60, 50, TRUE);
  RECT rc;
  ::GetWindowRect(a, &rc);
  CHECK(rc.left == q.x + 20 && rc.top == q.y + 30 && rc.right - rc.left == 60);
  ::GetClientRect(a, &rc);
  CHECK(rc.right == 60 && rc.bottom == 50);

  // Destroying the parent destroys the children.
  ::DestroyWindow(top);
  CHECK(!::IsWindow(a) && !::IsWindow(b));
}

static void TestMessages() {
  HWND h = NewTop(WS_OVERLAPPEDWINDOW, 0, 0, 200, 100);
  CHECK(::SendMessageW(h, WM_APP, 2, 3) == 5);
  CHECK(!g_inSend);

  // Posted messages come back in order, and filters pick among them.
  Pump();
  ::PostMessageW(h, WM_APP + 10, 1, 0);
  ::PostMessageW(h, WM_APP + 11, 2, 0);
  ::PostMessageW(NULL, WM_APP + 12, 3, 0);
  MSG m;
  CHECK(::PeekMessageW(&m, NULL, WM_APP + 11, WM_APP + 11, PM_REMOVE) && m.wParam == 2);
  CHECK(::PeekMessageW(&m, (HWND)-1, 0, 0, PM_REMOVE) && m.message == WM_APP + 12 && m.hwnd == NULL);
  CHECK(::PeekMessageW(&m, h, 0, 0, PM_NOREMOVE) && m.message == WM_APP + 10);
  CHECK(::PeekMessageW(&m, h, WM_APP, WM_APP + 20, PM_REMOVE) && m.message == WM_APP + 10);
  CHECK(!::PeekMessageW(&m, h, WM_APP, WM_APP + 20, PM_REMOVE));

  // WM_QUIT ends GetMessage.
  ::PostThreadMessageW(::GetCurrentThreadId(), WM_APP + 13, 0, 0);
  ::PostQuitMessage(7);
  CHECK(::GetMessageW(&m, NULL, 0, 0) == TRUE && m.message == WM_APP + 13);
  CHECK(::GetMessageW(&m, NULL, 0, 0) == FALSE && m.message == WM_QUIT && m.wParam == 7);

  // TranslateMessage makes WM_CHAR from a key.
  ::PostMessageW(h, WM_KEYDOWN, 'A', 1);
  CHECK(::PeekMessageW(&m, h, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
  ::TranslateMessage(&m);
  CHECK(::PeekMessageW(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE) && (m.wParam == 'a' || m.wParam == 'A'));

  ::DestroyWindow(h);
}

static int g_procTimers = 0;
static void CALLBACK OnThreadTimer(HWND, UINT, UINT_PTR, DWORD) { g_procTimers++; }

static void TestTimers() {
  HWND h = NewTop(WS_OVERLAPPEDWINDOW, 0, 0, 200, 100);
  g_timers = 0;
  CHECK(::SetTimer(h, 1, 20, NULL) == 1);
  UINT_PTR t = ::SetTimer(NULL, 0, 20, OnThreadTimer);
  CHECK(t != 0);
  DWORD start = ::GetTickCount();
  MSG m;
  while ((g_timers < 2 || g_procTimers < 2) && ::GetTickCount() - start < 3000) {
    if (::GetMessageW(&m, NULL, 0, 0) <= 0) break;
    ::DispatchMessageW(&m);
  }
  CHECK(g_timers >= 2 && g_procTimers >= 2);
  CHECK(::KillTimer(h, 1));
  CHECK(::KillTimer(NULL, t));
  CHECK(!::KillTimer(h, 1));
  ::DestroyWindow(h);
}

struct SendArgs {
  HWND hwnd;
  LRESULT result;
  LRESULT tid;
};
static DWORD WINAPI SendFromThread(LPVOID p) {
  SendArgs* a = (SendArgs*)p;
  a->result = ::SendMessageW(a->hwnd, WM_APP, 20, 22);
  a->tid = ::SendMessageW(a->hwnd, WM_APP + 1, 0, 0);
  ::PostMessageW(a->hwnd, WM_APP + 2, 0, 0);
  return 0;
}

static void TestThreads() {
  HWND h = NewTop(WS_OVERLAPPEDWINDOW, 0, 0, 200, 100);
  SendArgs a = {h, 0, 0};
  g_inSend = FALSE;
  HANDLE t = ::CreateThread(NULL, 0, SendFromThread, &a, 0, NULL);
  // The window's thread handles the sent messages while it waits.
  MSG m;
  DWORD start = ::GetTickCount();
  bool posted = false;
  while (!posted && ::GetTickCount() - start < 5000) {
    if (::MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0) {
      while (::PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_APP + 2) posted = true;
        ::DispatchMessageW(&m);
      }
    }
  }
  ::WaitForSingleObject(t, 5000);
  ::CloseHandle(t);
  CHECK(posted);
  CHECK(a.result == 42);
  CHECK(g_inSend);
  CHECK(a.tid == (LRESULT)::GetCurrentThreadId());
  ::DestroyWindow(h);
}

static HWND g_cbtCreated = NULL;
static bool g_cbtBeforeNcCreate = false;
static LRESULT CALLBACK CbtHook(int code, WPARAM wp, LPARAM lp) {
  // Only this test's class counts; Wine makes windows of its own.
  wchar_t cls[64] = L"";
  if (code == HCBT_CREATEWND) ::GetClassNameW((HWND)wp, cls, 64);
  if (code == HCBT_CREATEWND && !g_cbtCreated && lstrcmpW(cls, kClass) == 0) {
    g_cbtCreated = (HWND)wp;
    g_cbtBeforeNcCreate = !Seen(WM_NCCREATE);
  }
  return ::CallNextHookEx(NULL, code, wp, lp);
}

static void TestHooksAndAnsi() {
  HHOOK hook = ::SetWindowsHookExW(WH_CBT, CbtHook, NULL, ::GetCurrentThreadId());
  CHECK(hook != NULL);
  g_msgs.clear();
  g_cbtCreated = NULL;
  HWND h = NewTop(WS_OVERLAPPEDWINDOW, 0, 0, 200, 100);
  CHECK(g_cbtCreated == h && g_cbtBeforeNcCreate);
  CHECK(::UnhookWindowsHookEx(hook));

  // An ANSI procedure gets text as ANSI, and Unicode callers still see it.
  WNDPROC old = (WNDPROC)::SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)AnsiProc);
  CHECK(!::IsWindowUnicode(h));
  CHECK(::SetWindowTextW(h, L"Hello"));
  CHECK(lstrcmpA(g_ansiText, "Hello") == 0);
  wchar_t buf[16];
  CHECK(::GetWindowTextW(h, buf, 16) == 5 && lstrcmpW(buf, L"Hello") == 0);
  // The old procedure, asked for by an ANSI caller, is a handle that
  // CallWindowProc knows.
  CHECK(::CallWindowProcA(old, h, WM_APP, 1, 1) == 2);
  ::SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)old);
  CHECK(::IsWindowUnicode(h));
  CHECK(::SendMessageW(h, WM_APP, 3, 4) == 7);
  ::DestroyWindow(h);
}

static void TestFocusAndPaint() {
  HWND top = NewTop(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 200);
  HWND child = ::CreateWindowExW(0, kClass, L"c", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, top, (HMENU)1,
                                 ::GetModuleHandleW(NULL), NULL);
  ::SetFocus(top);
  ::SetFocus(child);
  CHECK(::GetFocus() == child);
  CHECK(g_focusFrom == top);

  g_paints = 0;
  ::UpdateWindow(top);
  Pump();
  CHECK(g_paints >= 1);
  RECT rc;
  CHECK(!::GetUpdateRect(top, &rc, FALSE));
  ::InvalidateRect(top, NULL, TRUE);
  CHECK(::GetUpdateRect(top, &rc, FALSE));
  ::ValidateRect(top, NULL);
  CHECK(!::GetUpdateRect(top, &rc, FALSE));

  // What WM_PAINT drew is on the window.
  ::InvalidateRect(top, NULL, TRUE);
  ::UpdateWindow(top);
  HDC dc = ::GetDC(top);
  CHECK(::GetPixel(dc, 100, 100) == RGB(255, 0, 0));
  ::ReleaseDC(top, dc);

  // Scrolling moves what is drawn.
  dc = ::GetDC(top);
  HBRUSH blue = ::CreateSolidBrush(RGB(0, 0, 255));
  RECT left = {0, 0, 150, 100};
  ::FillRect(dc, &left, blue);
  ::DeleteObject(blue);
  ::ReleaseDC(top, dc);
  RECT area = {0, 0, 280, 100};
  ::ScrollWindowEx(top, 20, 0, &area, &area, NULL, NULL, 0);
  dc = ::GetDC(top);
  CHECK(::GetPixel(dc, 160, 50) == RGB(0, 0, 255));
  CHECK(::GetPixel(dc, 175, 50) == RGB(255, 0, 0));
  ::ReleaseDC(top, dc);

  ::DestroyWindow(top);
}

// ---------------------------------------------------------------------
// Drawing in memory

static void TestMemoryDC() {
  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  HBITMAP bm = ::CreateCompatibleBitmap(screen, 40, 30);
  HGDIOBJ old = ::SelectObject(mem, bm);
  CHECK(::GetObjectType(mem) == OBJ_MEMDC);
  CHECK(::GetCurrentObject(mem, OBJ_BITMAP) == bm);
  BITMAP info;
  CHECK(::GetObjectW(bm, sizeof info, &info) && info.bmWidth == 40 && info.bmHeight == 30);

  RECT all = {0, 0, 40, 30};
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  HBRUSH green = ::CreateSolidBrush(RGB(0, 255, 0));
  RECT part = {5, 5, 15, 10};
  ::FillRect(mem, &part, green);
  CHECK(::GetPixel(mem, 5, 5) == RGB(0, 255, 0));
  CHECK(::GetPixel(mem, 14, 9) == RGB(0, 255, 0));
  CHECK(::GetPixel(mem, 15, 10) == RGB(255, 255, 255));
  ::SetPixel(mem, 1, 1, RGB(10, 20, 30));
  CHECK(::GetPixel(mem, 1, 1) == RGB(10, 20, 30));

  // Raster operations.
  ::PatBlt(mem, 0, 0, 2, 2, DSTINVERT);
  CHECK(::GetPixel(mem, 1, 1) == RGB(245, 235, 225));
  CHECK(::GetPixel(mem, 0, 0) == RGB(0, 0, 0));

  // Copies, stretched and not.
  HDC mem2 = ::CreateCompatibleDC(screen);
  HBITMAP bm2 = ::CreateCompatibleBitmap(screen, 80, 60);
  HGDIOBJ old2 = ::SelectObject(mem2, bm2);
  ::BitBlt(mem2, 0, 0, 40, 30, mem, 0, 0, SRCCOPY);
  CHECK(::GetPixel(mem2, 10, 7) == RGB(0, 255, 0));
  ::StretchBlt(mem2, 0, 0, 80, 60, mem, 0, 0, 40, 30, SRCCOPY);
  CHECK(::GetPixel(mem2, 20, 14) == RGB(0, 255, 0));
  CHECK(::GetPixel(mem2, 32, 22) == RGB(255, 255, 255));

  // A monochrome bitmap: the source's background color becomes white,
  // and going back, white is the destination's background color.
  HDC monoDC = ::CreateCompatibleDC(screen);
  HBITMAP mono = ::CreateBitmap(40, 30, 1, 1, NULL);
  HGDIOBJ oldMono = ::SelectObject(monoDC, mono);
  ::SetBkColor(mem, RGB(255, 255, 255));
  ::BitBlt(monoDC, 0, 0, 40, 30, mem, 0, 0, SRCCOPY);
  CHECK(::GetPixel(monoDC, 20, 20) == RGB(255, 255, 255));
  CHECK(::GetPixel(monoDC, 6, 6) == RGB(0, 0, 0));
  ::SetBkColor(mem2, RGB(1, 2, 3));
  ::SetTextColor(mem2, RGB(4, 5, 6));
  ::BitBlt(mem2, 0, 0, 40, 30, monoDC, 0, 0, SRCCOPY);
  CHECK(::GetPixel(mem2, 20, 20) == RGB(1, 2, 3));
  CHECK(::GetPixel(mem2, 6, 6) == RGB(4, 5, 6));
  ::SelectObject(monoDC, oldMono);
  ::DeleteObject(mono);
  ::DeleteDC(monoDC);

  // GetDIBits as 24 bits, bottom row first.
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = 40;
  bi.bmiHeader.biHeight = 30;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  std::vector<BYTE> bits(((40 * 3 + 3) & ~3) * 30);
  ::SelectObject(mem, old);
  CHECK(::GetDIBits(screen, bm, 0, 30, &bits[0], &bi, DIB_RGB_COLORS) == 30);
  int stride = (40 * 3 + 3) & ~3;
  BYTE* px = &bits[(29 - 5) * stride + 5 * 3];  // (5, 5)
  CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0);

  ::SelectObject(mem2, old2);
  ::DeleteObject(bm2);
  ::DeleteDC(mem2);
  ::DeleteObject(green);
  ::DeleteObject(bm);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);
}

static void TestDIBSections() {
  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  // 32 bits, top-down: the bits are the pixels.
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = 8;
  bi.bmiHeader.biHeight = -4;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* p = NULL;
  HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &p, NULL, 0);
  CHECK(dib && p);
  DWORD* px = (DWORD*)p;
  px[1 * 8 + 2] = 0x00336699;  // (2, 1)
  HGDIOBJ old = ::SelectObject(mem, dib);
  CHECK(::GetPixel(mem, 2, 1) == RGB(0x33, 0x66, 0x99));
  ::SetPixel(mem, 3, 2, RGB(1, 2, 3));
  ::GdiFlush();
  CHECK((px[2 * 8 + 3] & 0xFFFFFF) == 0x010203);
  DIBSECTION ds;
  CHECK(::GetObjectW(dib, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBits == p && ds.dsBmih.biBitCount == 32);
  ::SelectObject(mem, old);
  ::DeleteObject(dib);

  // 8 bits with a color table, bottom-up.
  struct {
    BITMAPINFOHEADER h;
    RGBQUAD colors[2];
  } b8 = {};
  b8.h.biSize = sizeof(BITMAPINFOHEADER);
  b8.h.biWidth = 4;
  b8.h.biHeight = 2;
  b8.h.biPlanes = 1;
  b8.h.biBitCount = 8;
  b8.h.biClrUsed = 2;
  b8.colors[1].rgbRed = 200;
  dib = ::CreateDIBSection(screen, (BITMAPINFO*)&b8, DIB_RGB_COLORS, &p, NULL, 0);
  CHECK(dib && p);
  ((BYTE*)p)[0] = 1;  // the bottom row's first pixel
  old = ::SelectObject(mem, dib);
  CHECK(::GetPixel(mem, 0, 1) == RGB(200, 0, 0));
  CHECK(::GetPixel(mem, 0, 0) == RGB(0, 0, 0));
  ::SelectObject(mem, old);
  ::DeleteObject(dib);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);
}

static void TestRegionsAndClipping() {
  HRGN a = ::CreateRectRgn(0, 0, 10, 10);
  HRGN b = ::CreateRectRgn(5, 5, 15, 15);
  HRGN c = ::CreateRectRgn(0, 0, 0, 0);
  CHECK(::CombineRgn(c, a, b, RGN_AND) == SIMPLEREGION);
  RECT box;
  ::GetRgnBox(c, &box);
  CHECK(box.left == 5 && box.top == 5 && box.right == 10 && box.bottom == 10);
  CHECK(::CombineRgn(c, a, b, RGN_OR) == COMPLEXREGION);
  CHECK(::PtInRegion(c, 12, 12) && !::PtInRegion(c, 12, 2));
  CHECK(::CombineRgn(c, a, b, RGN_DIFF) == COMPLEXREGION && !::PtInRegion(c, 7, 7));
  ::OffsetRgn(a, 100, 0);
  CHECK(::PtInRegion(a, 105, 5));
  HRGN e = ::CreateEllipticRgn(0, 0, 20, 20);
  CHECK(::PtInRegion(e, 10, 10) && !::PtInRegion(e, 1, 1));

  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  HBITMAP bm = ::CreateCompatibleBitmap(screen, 20, 20);
  HGDIOBJ old = ::SelectObject(mem, bm);
  RECT all = {0, 0, 20, 20};
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  HRGN clip = ::CreateRectRgn(5, 5, 10, 10);
  ::SelectClipRgn(mem, clip);
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(::GetPixel(mem, 6, 6) == RGB(255, 255, 255));
  CHECK(::GetPixel(mem, 2, 2) == RGB(0, 0, 0));
  ::GetClipBox(mem, &box);
  CHECK(box.left == 5 && box.right == 10);
  ::ExcludeClipRect(mem, 5, 5, 7, 7);
  CHECK(!::PtVisible(mem, 6, 6) && ::PtVisible(mem, 8, 8));
  ::SelectClipRgn(mem, NULL);
  CHECK(::PtVisible(mem, 1, 1));

  // SaveDC and RestoreDC bring back the attributes.
  int saved = ::SaveDC(mem);
  ::SetTextColor(mem, RGB(1, 1, 1));
  ::IntersectClipRect(mem, 0, 0, 2, 2);
  ::RestoreDC(mem, saved);
  CHECK(::GetTextColor(mem) == RGB(0, 0, 0));
  CHECK(::PtVisible(mem, 15, 15));

  // An anisotropic mapping doubles everything.
  ::SetMapMode(mem, MM_ANISOTROPIC);
  ::SetWindowExtEx(mem, 10, 10, NULL);
  ::SetViewportExtEx(mem, 20, 20, NULL);
  POINT pt = {3, 4};
  ::LPtoDP(mem, &pt, 1);
  CHECK(pt.x == 6 && pt.y == 8);
  ::SetWindowOrgEx(mem, 1, 1, NULL);
  RECT r = {1, 1, 3, 3};
  ::FillRect(mem, &r, (HBRUSH)::GetStockObject(GRAY_BRUSH));
  ::SetMapMode(mem, MM_TEXT);
  ::SetWindowOrgEx(mem, 0, 0, NULL);  // the origin outlives the mapping mode
  CHECK(::GetPixel(mem, 3, 3) == RGB(128, 128, 128));
  CHECK(::GetPixel(mem, 4, 4) == RGB(0, 0, 0));

  ::SelectObject(mem, old);
  ::DeleteObject(bm);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);
  ::DeleteObject(a);
  ::DeleteObject(b);
  ::DeleteObject(c);
  ::DeleteObject(e);
  ::DeleteObject(clip);
}

// ---------------------------------------------------------------------
// Lines, shapes, paths and text

// A 32-bit top-down DIB section in a memory DC, filled white.
struct Canvas {
  HDC dc;
  HBITMAP bm;
  HGDIOBJ old;
  DWORD* bits;
  int w, h;
  Canvas(int cx, int cy) : w(cx), h(cy) {
    dc = ::CreateCompatibleDC(NULL);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* p = NULL;
    bm = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, NULL, 0);
    bits = (DWORD*)p;
    old = ::SelectObject(dc, bm);
    RECT all = {0, 0, cx, cy};
    ::FillRect(dc, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  }
  ~Canvas() {
    ::SelectObject(dc, old);
    ::DeleteObject(bm);
    ::DeleteDC(dc);
  }
  DWORD At(int x, int y) {
    ::GdiFlush();
    return bits[y * w + x] & 0xFFFFFF;
  }
  // Whether any pixel in the rectangle differs from white.
  bool Inked(int l, int t, int r, int b) {
    ::GdiFlush();
    for (int y = t; y < b; y++)
      for (int x = l; x < r; x++)
        if (x >= 0 && y >= 0 && x < w && y < h && (bits[y * w + x] & 0xFFFFFF) != 0xFFFFFF) return true;
    return false;
  }
};

static void TestShapes() {
  Canvas c(120, 100);
  const DWORD kWhite = 0xFFFFFF, kBlack = 0, kRed = 0xFF0000;
  HBRUSH red = ::CreateSolidBrush(RGB(255, 0, 0));
  HGDIOBJ oldBrush = ::SelectObject(c.dc, red);
  // A rectangle leaves out its right and bottom edges; the pen draws the
  // border and the brush the inside.
  CHECK(::Rectangle(c.dc, 0, 0, 10, 10));
  CHECK(c.At(0, 0) == kBlack && c.At(9, 9) == kBlack && c.At(9, 0) == kBlack);
  CHECK(c.At(5, 5) == kRed && c.At(10, 10) == kWhite && c.At(10, 5) == kWhite);
  // With no pen the filled area is a pixel smaller each way.
  HGDIOBJ oldPen = ::SelectObject(c.dc, ::GetStockObject(NULL_PEN));
  ::Rectangle(c.dc, 20, 0, 30, 10);
  CHECK(c.At(20, 0) == kRed && c.At(28, 8) == kRed && c.At(29, 9) == kWhite);
  ::SelectObject(c.dc, ::GetStockObject(BLACK_PEN));
  // An ellipse: filled in the middle, not in the corners of its box.
  ::Ellipse(c.dc, 40, 0, 60, 20);
  CHECK(c.At(50, 10) == kRed && c.At(40, 0) == kWhite && c.At(59, 19) == kWhite);
  CHECK(c.At(40, 10) == kBlack || c.At(40, 9) == kBlack);
  // LineTo leaves out the last pixel and moves the current position.
  ::MoveToEx(c.dc, 0, 30, NULL);
  CHECK(::LineTo(c.dc, 10, 30));
  CHECK(c.At(0, 30) == kBlack && c.At(9, 30) == kBlack && c.At(10, 30) == kWhite);
  POINT cur;
  CHECK(::GetCurrentPositionEx(c.dc, &cur) && cur.x == 10 && cur.y == 30);
  // Drawn twice with XOR, a line leaves nothing.
  int oldRop = ::SetROP2(c.dc, R2_XORPEN);
  HPEN white = ::CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
  ::SelectObject(c.dc, white);
  ::MoveToEx(c.dc, 0, 35, NULL);
  ::LineTo(c.dc, 20, 45);
  CHECK(c.At(0, 35) == kBlack);
  ::MoveToEx(c.dc, 0, 35, NULL);
  ::LineTo(c.dc, 20, 45);
  CHECK(!c.Inked(0, 34, 21, 46));
  ::SetROP2(c.dc, oldRop);
  // A wide pen is centered on the line.
  HPEN wide = ::CreatePen(PS_SOLID, 5, RGB(0, 0, 0));
  ::SelectObject(c.dc, wide);
  ::MoveToEx(c.dc, 0, 55, NULL);
  ::LineTo(c.dc, 30, 55);
  CHECK(c.At(15, 53) == kBlack && c.At(15, 57) == kBlack && c.At(15, 50) == kWhite && c.At(15, 60) == kWhite);
  ::SelectObject(c.dc, ::GetStockObject(BLACK_PEN));
  // The fill mode decides whether a star's middle is filled.
  POINT star[] = {{80, 30}, {92, 70}, {60, 45}, {100, 45}, {68, 70}};
  ::SetPolyFillMode(c.dc, ALTERNATE);
  ::Polygon(c.dc, star, 5);
  CHECK(c.At(80, 52) == kWhite && c.At(80, 40) == kRed);
  ::SetPolyFillMode(c.dc, WINDING);
  ::Polygon(c.dc, star, 5);
  CHECK(c.At(80, 52) == kRed);
  // Flood fill stops at the border color.
  ::SelectObject(c.dc, ::GetStockObject(NULL_BRUSH));
  ::Rectangle(c.dc, 0, 70, 20, 90);
  HBRUSH blue = ::CreateSolidBrush(RGB(0, 0, 255));
  ::SelectObject(c.dc, blue);
  CHECK(::FloodFill(c.dc, 10, 80, RGB(0, 0, 0)));
  CHECK(c.At(10, 80) == 0x0000FF && c.At(1, 71) == 0x0000FF && c.At(0, 70) == kBlack && c.At(25, 80) == kWhite);
  ::SelectObject(c.dc, oldBrush);
  ::SelectObject(c.dc, oldPen);
  ::DeleteObject(red);
  ::DeleteObject(blue);
  ::DeleteObject(white);
  ::DeleteObject(wide);

  // Paths
  Canvas p(60, 60);
  CHECK(::BeginPath(p.dc));
  ::MoveToEx(p.dc, 10, 10, NULL);
  ::LineTo(p.dc, 40, 10);
  ::LineTo(p.dc, 40, 40);
  CHECK(::CloseFigure(p.dc));
  CHECK(::EndPath(p.dc));
  CHECK(!p.Inked(0, 0, 60, 60));
  int n = ::GetPath(p.dc, NULL, NULL, 0);
  CHECK(n == 3);
  POINT pts[3];
  BYTE types[3];
  CHECK(::GetPath(p.dc, pts, types, 3) == 3);
  CHECK(types[0] == PT_MOVETO && types[1] == PT_LINETO && types[2] == (PT_LINETO | PT_CLOSEFIGURE));
  CHECK(pts[2].x == 40 && pts[2].y == 40);
  HRGN rgn = ::PathToRegion(p.dc);
  CHECK(rgn != NULL);
  RECT box;
  CHECK(::GetRgnBox(rgn, &box) != ERROR && box.left >= 10 && box.right <= 41 && box.top >= 10 && box.bottom <= 41);
  CHECK(::PtInRegion(rgn, 35, 15) && !::PtInRegion(rgn, 15, 35));
  ::DeleteObject(rgn);
  CHECK(::GetPath(p.dc, NULL, NULL, 0) == -1);  // PathToRegion used the path up
  ::BeginPath(p.dc);
  ::Rectangle(p.dc, 5, 5, 25, 25);
  ::EndPath(p.dc);
  ::SelectObject(p.dc, ::GetStockObject(BLACK_BRUSH));
  CHECK(::FillPath(p.dc));
  CHECK(p.At(15, 15) == kBlack && p.At(30, 30) == kWhite);
}

static void TestText() {
  Canvas c(300, 120);
  HFONT font = ::CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, L"Arial");
  HGDIOBJ oldFont = ::SelectObject(c.dc, font);
  TEXTMETRICW tm;
  CHECK(::GetTextMetricsW(c.dc, &tm));
  CHECK(tm.tmHeight > 8 && tm.tmHeight < 30 && tm.tmAscent + tm.tmDescent == tm.tmHeight);
  CHECK(tm.tmAveCharWidth > 0 && tm.tmMaxCharWidth >= tm.tmAveCharWidth);
  TEXTMETRICA tma;
  CHECK(::GetTextMetricsA(c.dc, &tma) && tma.tmHeight == tm.tmHeight);
  wchar_t face[LF_FACESIZE];
  CHECK(::GetTextFaceW(c.dc, LF_FACESIZE, face) > 0);

  SIZE sz;
  CHECK(::GetTextExtentPoint32W(c.dc, L"Hello", 5, &sz));
  CHECK(sz.cx > 10 && sz.cy == tm.tmHeight);
  SIZE sza;
  CHECK(::GetTextExtentPoint32A(c.dc, "Hello", 5, &sza) && sza.cx == sz.cx);
  SIZE wide, narrow;
  ::GetTextExtentPoint32W(c.dc, L"WWWW", 4, &wide);
  ::GetTextExtentPoint32W(c.dc, L"iiii", 4, &narrow);
  CHECK(wide.cx >= narrow.cx);
  int widths[5];
  CHECK(::GetCharWidth32W(c.dc, 'a', 'e', widths) && widths[0] > 0);
  int fit = 0, dx[5];
  SIZE ext5;
  CHECK(::GetTextExtentExPointW(c.dc, L"Hello", 5, sz.cx - 1, &fit, dx, &ext5));
  CHECK(fit == 4 && dx[4] == sz.cx && dx[0] > 0 && dx[0] < dx[4]);

  // Text draws inside its box, from the top-left by default.
  CHECK(::TextOutW(c.dc, 10, 10, L"Hello", 5));
  CHECK(c.Inked(10, 10, 10 + sz.cx, 10 + sz.cy));
  CHECK(!c.Inked(0, 0, 300, 9) && !c.Inked(12 + sz.cx, 0, 300, 120) && !c.Inked(0, 11 + sz.cy, 300, 120));
  // TA_UPDATECP moves the current position past the text.
  ::SetTextAlign(c.dc, TA_UPDATECP);
  ::MoveToEx(c.dc, 100, 10, NULL);
  ::TextOutW(c.dc, 0, 0, L"Hello", 5);
  POINT cur;
  ::GetCurrentPositionEx(c.dc, &cur);
  CHECK(cur.x == 100 + sz.cx && cur.y == 10);
  ::SetTextAlign(c.dc, TA_LEFT | TA_TOP);
  // An opaque background fills the text's box in the background color.
  ::SetBkColor(c.dc, RGB(0, 255, 0));
  ::SetBkMode(c.dc, OPAQUE);
  ::TextOutW(c.dc, 200, 10, L" ", 1);
  CHECK(c.At(200, 10) == 0x00FF00);
  ::SetBkMode(c.dc, TRANSPARENT);

  // DrawText measures one line, or breaks words to fit a width.
  RECT r = {0, 0, 0, 0};
  int h = ::DrawTextW(c.dc, L"Hello", -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
  CHECK(h == tm.tmHeight && r.right == sz.cx && r.bottom == tm.tmHeight);
  RECT wrap = {0, 0, sz.cx + 5, 0};
  h = ::DrawTextW(c.dc, L"Hello Hello Hello", -1, &wrap, DT_CALCRECT | DT_WORDBREAK);
  CHECK(h == 3 * tm.tmHeight && wrap.bottom == h && wrap.right <= sz.cx + 5);
  // A prefix character is not drawn.
  RECT pr = {0, 0, 0, 0};
  ::DrawTextW(c.dc, L"&Hello", -1, &pr, DT_CALCRECT | DT_SINGLELINE);
  CHECK(pr.right == sz.cx);
  // Centered in a box, vertically too.
  RECT box = {0, 50, 300, 110};
  ::DrawTextW(c.dc, L"Hello", -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  CHECK(c.Inked(140 - sz.cx / 2, 50, 160 + sz.cx / 2, 110) && !c.Inked(0, 50, 145 - sz.cx / 2, 110));
  // An ellipsis replaces what does not fit, and DT_MODIFYSTRING says so.
  wchar_t buf[64] = L"Hello Hello Hello";
  RECT el = {0, 0, sz.cx, 20};
  ::DrawTextExW(c.dc, buf, -1, &el, DT_SINGLELINE | DT_END_ELLIPSIS | DT_MODIFYSTRING, NULL);
  size_t len = wcslen(buf);
  CHECK(len < 17 && len >= 3 && wcscmp(buf + len - 3, L"...") == 0);
  // Tabs
  DWORD ext = ::GetTabbedTextExtentW(c.dc, L"a\tb", 3, 0, NULL);
  CHECK(HIWORD(ext) == tm.tmHeight && LOWORD(ext) >= 8 * tm.tmAveCharWidth);

  // There is at least one font family to enumerate.
  struct Counter {
    static int CALLBACK Proc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM n) {
      if (lf->lfFaceName[0]) ++*(int*)n;
      return 1;
    }
  };
  int families = 0;
  LOGFONTW want = {};
  want.lfCharSet = DEFAULT_CHARSET;
  ::EnumFontFamiliesExW(c.dc, &want, Counter::Proc, (LPARAM)&families, 0);
  CHECK(families > 0);
  ::SelectObject(c.dc, oldFont);
  ::DeleteObject(font);
}

// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// Icons, cursors and accelerators

static WPARAM g_lastCommand = 0;
static std::vector<UINT> g_menuMsgs;
static int g_clickItem = -1;  // a popup item to click at the next WM_ENTERIDLE
static HMENU g_clickMenu = NULL;

static LRESULT CALLBACK MenuProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_ENTERMENULOOP:
    case WM_EXITMENULOOP:
    case WM_INITMENU:
    case WM_INITMENUPOPUP:
    case WM_UNINITMENUPOPUP:
    case WM_MENUSELECT:
      g_menuMsgs.push_back(msg);
      break;
    case WM_ENTERIDLE:
      if (wp == MSGF_MENU && g_clickItem >= 0) {
        RECT r;
        if (::GetMenuItemRect(h, g_clickMenu, g_clickItem, &r)) {
          POINT p = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
          ::ScreenToClient(h, &p);
          ::PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
          ::PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
          ::PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
        }
        g_clickItem = -1;
      }
      return 0;
    case WM_COMMAND:
      g_lastCommand = wp;
      return 0;
  }
  return ::DefWindowProcW(h, msg, wp, lp);
}

static HWND NewMenuWindow(HMENU menu) {
  static ATOM atom = 0;
  if (!atom) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MenuProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = L"WinTestMenu";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    atom = ::RegisterClassW(&wc);
  }
  return ::CreateWindowExW(0, L"WinTestMenu", L"Menus", WS_OVERLAPPEDWINDOW, 20, 20, 320, 240, NULL, menu,
                           ::GetModuleHandleW(NULL), NULL);
}

static void TestIconsAndAccelerators() {
  HICON app = ::LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
  CHECK(app != NULL);
  ICONINFO ii;
  CHECK(::GetIconInfo(app, &ii) && ii.fIcon && ii.hbmMask != NULL);
  if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
  if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
  CHECK(::LoadCursorW(NULL, (LPCWSTR)IDC_ARROW) != NULL);

  // An icon made from two bitmaps draws its color where the mask is clear.
  HDC screen = ::GetDC(NULL);
  HBITMAP color = ::CreateCompatibleBitmap(screen, 16, 16);
  HBITMAP mask = ::CreateBitmap(16, 16, 1, 1, NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  HGDIOBJ old = ::SelectObject(mem, color);
  RECT all = {0, 0, 16, 16};
  HBRUSH red = ::CreateSolidBrush(RGB(255, 0, 0));
  ::FillRect(mem, &all, red);
  ::SelectObject(mem, mask);
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  ::SelectObject(mem, old);
  ICONINFO make = {TRUE, 0, 0, mask, color};
  HICON icon = ::CreateIconIndirect(&make);
  CHECK(icon != NULL);
  HBITMAP target = ::CreateCompatibleBitmap(screen, 16, 16);
  old = ::SelectObject(mem, target);
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(::DrawIconEx(mem, 0, 0, icon, 16, 16, 0, NULL, DI_NORMAL));
  CHECK(::GetPixel(mem, 8, 8) == RGB(255, 0, 0));
  ::SelectObject(mem, old);
  CHECK(::DestroyIcon(icon));
  ::DeleteObject(target);
  ::DeleteObject(color);
  ::DeleteObject(mask);
  ::DeleteObject(red);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);

  ACCEL keys[2] = {{FVIRTKEY | FCONTROL, 'O', 100}, {FVIRTKEY, VK_F5, 101}};
  HACCEL accel = ::CreateAcceleratorTableW(keys, 2);
  CHECK(accel != NULL && ::CopyAcceleratorTableW(accel, NULL, 0) == 2);
  HWND h = NewMenuWindow(NULL);
  g_lastCommand = 0;
  MSG m = {h, WM_KEYDOWN, VK_F5, 1};
  CHECK(::TranslateAcceleratorW(h, accel, &m));
  CHECK(LOWORD(g_lastCommand) == 101 && HIWORD(g_lastCommand) == 1);
  m.wParam = 'O';
  CHECK(!::TranslateAcceleratorW(h, accel, &m));  // Ctrl is not down
  CHECK(::DestroyAcceleratorTable(accel));
  ::DestroyWindow(h);
}

// ---------------------------------------------------------------------
// Menus

static void TestMenus() {
  HMENU bar = ::CreateMenu();
  HMENU file = ::CreatePopupMenu();
  CHECK(::IsMenu(bar) && ::IsMenu(file) && !::IsMenu(NULL));
  CHECK(::AppendMenuW(file, MF_STRING, 100, L"&Open\tCtrl+O"));
  CHECK(::AppendMenuW(file, MF_SEPARATOR, 0, NULL));
  CHECK(::AppendMenuA(file, MF_STRING | MF_GRAYED, 101, "E&xit"));
  CHECK(::AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File"));
  CHECK(::GetMenuItemCount(bar) == 1 && ::GetMenuItemCount(file) == 3);
  CHECK(::GetSubMenu(bar, 0) == file);
  CHECK(::GetMenuItemID(bar, 0) == (UINT)-1 && ::GetMenuItemID(file, 2) == 101);
  CHECK(::GetMenuState(file, 1, MF_BYPOSITION) & MF_SEPARATOR);
  // A search by command looks into submenus.
  CHECK(::GetMenuState(bar, 101, MF_BYCOMMAND) & MF_GRAYED);
  CHECK((::GetMenuState(bar, 0, MF_BYPOSITION) >> 8) == 3);
  CHECK(::GetMenuState(bar, 999, MF_BYCOMMAND) == (UINT)-1);
  wchar_t buf[64];
  CHECK(::GetMenuStringW(bar, 100, buf, 64, MF_BYCOMMAND) == 12 && wcscmp(buf, L"&Open\tCtrl+O") == 0);
  CHECK(::GetMenuStringW(file, 100, NULL, 0, MF_BYCOMMAND) == 12);
  char abuf[8];
  CHECK(::GetMenuStringA(file, 101, abuf, sizeof abuf, MF_BYCOMMAND) == 5 && strcmp(abuf, "E&xit") == 0);
  CHECK(::EnableMenuItem(bar, 101, MF_BYCOMMAND | MF_ENABLED) == MF_GRAYED);
  CHECK(::CheckMenuItem(file, 100, MF_BYCOMMAND | MF_CHECKED) == MF_UNCHECKED);
  CHECK(::CheckMenuItem(file, 100, MF_BYCOMMAND | MF_UNCHECKED) == MF_CHECKED);
  CHECK(::CheckMenuItem(file, 999, MF_BYCOMMAND) == (DWORD)-1);
  CHECK(::ModifyMenuW(file, 100, MF_BYCOMMAND | MF_STRING, 102, L"&New"));
  CHECK(::GetMenuItemID(file, 0) == 102);
  CHECK(::InsertMenuW(file, 0, MF_BYPOSITION | MF_STRING, 103, L"First"));
  CHECK(::GetMenuItemID(file, 0) == 103 && ::GetMenuItemCount(file) == 4);
  CHECK(::DeleteMenu(file, 103, MF_BYCOMMAND) && ::GetMenuItemCount(file) == 3);
  CHECK(!::RemoveMenu(file, 999, MF_BYCOMMAND));

  MENUITEMINFOW mi = {sizeof mi};
  mi.fMask = MIIM_STRING | MIIM_ID;
  mi.dwTypeData = buf;
  mi.cch = 64;
  CHECK(::GetMenuItemInfoW(file, 0, TRUE, &mi));
  CHECK(mi.wID == 102 && mi.cch == 4 && wcscmp(buf, L"&New") == 0);
  mi.fMask = MIIM_DATA | MIIM_STATE;
  mi.dwItemData = 1234;
  mi.fState = MFS_CHECKED | MFS_DEFAULT;
  CHECK(::SetMenuItemInfoW(file, 102, FALSE, &mi));
  CHECK(::GetMenuDefaultItem(file, FALSE, 0) == 102 && ::GetMenuDefaultItem(file, TRUE, 0) == 0);
  memset(&mi, 0, sizeof mi);
  mi.cbSize = sizeof mi;
  mi.fMask = MIIM_DATA | MIIM_STATE;
  CHECK(::GetMenuItemInfoW(file, 102, FALSE, &mi) && mi.dwItemData == 1234 && (mi.fState & MFS_CHECKED));
  ::CheckMenuItem(file, 102, MF_BYCOMMAND | MF_UNCHECKED);

  HMENU view = ::CreatePopupMenu();
  for (UINT i = 0; i < 3; i++) ::AppendMenuW(view, MF_STRING, 200 + i, L"Mode");
  CHECK(::CheckMenuRadioItem(view, 200, 202, 201, MF_BYCOMMAND));
  CHECK(!(::GetMenuState(view, 200, MF_BYCOMMAND) & MF_CHECKED));
  CHECK(::GetMenuState(view, 201, MF_BYCOMMAND) & MF_CHECKED);
  MENUITEMINFOW ri = {sizeof ri};
  ri.fMask = MIIM_FTYPE;
  CHECK(::GetMenuItemInfoW(view, 201, FALSE, &ri) && (ri.fType & MFT_RADIOCHECK));
  CHECK(::InsertMenuW(bar, (UINT)-1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)view, L"&View"));
  CHECK(::GetMenuItemCount(bar) == 2);
  MENUINFO info = {sizeof info};
  info.fMask = MIM_MENUDATA;
  info.dwMenuData = 77;
  CHECK(::SetMenuInfo(view, &info));
  info.dwMenuData = 0;
  CHECK(::GetMenuInfo(view, &info) && info.dwMenuData == 77);

  // Templates: the standard form and the extended one.
  static const WORD tmpl[] = {0, 0, MF_POPUP | MF_END, '&', 'F', 0, 0, 1, 'A', 0, MF_END, 2, 'B', 0};
  HMENU loaded = ::LoadMenuIndirectW(tmpl);
  CHECK(loaded && ::GetMenuItemCount(loaded) == 1);
  HMENU sub = ::GetSubMenu(loaded, 0);
  CHECK(sub && ::GetMenuItemCount(sub) == 2 && ::GetMenuItemID(sub, 1) == 2);
  CHECK(::DestroyMenu(loaded) && !::IsMenu(sub));
  BYTE ex[56] = {};
  auto put16 = [&](int at, WORD v) { memcpy(ex + at, &v, 2); };
  auto put32 = [&](int at, DWORD v) { memcpy(ex + at, &v, 4); };
  // The header: version 1, then the offset past its help ID.
  put16(0, 1);
  put16(2, 4);
  // A popup that is the last item: type, state, ID, flags, text, padding
  // to a DWORD, and the submenu's help ID.
  put16(20, 0x81);
  put16(22, 'F');
  // Its one item, checked, with ID 5.
  put32(36, MFS_CHECKED);
  put32(40, 5);
  put16(44, 0x80);
  put16(46, 'X');
  loaded = ::LoadMenuIndirectW(ex);
  CHECK(loaded && ::GetMenuItemCount(loaded) == 1);
  sub = loaded ? ::GetSubMenu(loaded, 0) : NULL;
  CHECK(sub && ::GetMenuItemID(sub, 0) == 5 && (::GetMenuState(sub, 0, MF_BYPOSITION) & MF_CHECKED));
  if (loaded) ::DestroyMenu(loaded);

  // A window's menu bar takes room from the client area.
  HWND h = NewMenuWindow(bar);
  CHECK(h && ::GetMenu(h) == bar);
  RECT withBar, without;
  ::GetClientRect(h, &withBar);
  CHECK(::SetMenu(h, NULL) && ::GetMenu(h) == NULL);
  ::GetClientRect(h, &without);
  CHECK(without.bottom - withBar.bottom == ::GetSystemMetrics(SM_CYMENU));
  CHECK(::SetMenu(h, bar));
  RECT item, wr;
  ::GetWindowRect(h, &wr);
  CHECK(::GetMenuItemRect(h, bar, 0, &item));
  CHECK(item.left >= wr.left && item.top > wr.top && item.bottom < wr.bottom && item.right > item.left);
  POINT mid = {(item.left + item.right) / 2, (item.top + item.bottom) / 2};
  CHECK(::MenuItemFromPoint(h, bar, mid) == 0);

  HMENU sys = ::GetSystemMenu(h, FALSE);
  CHECK(sys && ::GetMenuItemCount(sys) >= 5);
  CHECK(::GetMenuState(sys, SC_CLOSE, MF_BYCOMMAND) != (UINT)-1);
  CHECK(::GetSystemMenu(h, TRUE) == NULL);

#ifdef _WINPORT
  // Tracking, driven by posted input.
  ::ShowWindow(h, SW_SHOW);
  Pump();
  g_menuMsgs.clear();
  ::PostMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
  ::PostMessageW(h, WM_KEYDOWN, VK_RETURN, 0);
  CHECK(::TrackPopupMenu(file, TPM_RETURNCMD, 50, 50, 0, h, NULL) == 102);
  CHECK(!g_menuMsgs.empty() && g_menuMsgs.front() == WM_ENTERMENULOOP && g_menuMsgs.back() == WM_EXITMENULOOP);
  bool init = false, select = false;
  for (UINT msg : g_menuMsgs) {
    init |= msg == WM_INITMENUPOPUP;
    select |= msg == WM_MENUSELECT;
  }
  CHECK(init && select);
  ::PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
  CHECK(::TrackPopupMenu(file, TPM_RETURNCMD, 50, 50, 0, h, NULL) == 0);
  // A mnemonic chooses its item, and the command is posted.
  g_lastCommand = 0;
  ::PostMessageW(h, WM_CHAR, 'x', 0);
  CHECK(::TrackPopupMenu(file, 0, 50, 50, 0, h, NULL));
  Pump();
  CHECK(LOWORD(g_lastCommand) == 101);
  // The mouse: a click on an item.
  g_clickMenu = file;
  g_clickItem = 0;
  CHECK(::TrackPopupMenu(file, TPM_RETURNCMD, 50, 50, 0, h, NULL) == 102);
  // The menu bar from the keyboard, as Alt+V then Down twice and Enter.
  g_lastCommand = 0;
  ::PostMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
  ::PostMessageW(h, WM_KEYDOWN, VK_DOWN, 0);
  ::PostMessageW(h, WM_KEYDOWN, VK_RETURN, 0);
  ::SendMessageW(h, WM_SYSCOMMAND, SC_KEYMENU, 'v');
  Pump();
  CHECK(LOWORD(g_lastCommand) == 202);
  // The bar is drawn in the menu color past its items.
  HDC dc = ::GetWindowDC(h);
  RECT last;
  ::GetMenuItemRect(h, bar, 1, &last);
  CHECK(::GetPixel(dc, last.right - wr.left + 8, (last.top + last.bottom) / 2 - wr.top) == ::GetSysColor(COLOR_MENU));
  ::ReleaseDC(h, dc);
#endif

  // The window's menu goes with it.
  ::DestroyWindow(h);
  CHECK(!::IsMenu(bar) && !::IsMenu(file) && !::IsMenu(view));
}

// ---------------------------------------------------------------------
// Dialogs from an in-memory template, and the standard controls in them.

// Builds a DLGTEMPLATE with its items, in UTF-16 words.
struct DlgBuilder {
  std::vector<WORD> w;
  WORD* count = NULL;
  void Dword(DWORD v) {
    w.push_back(LOWORD(v));
    w.push_back(HIWORD(v));
  }
  void Str(const char* s) {
    while (*s) w.push_back((WORD)*s++);
    w.push_back(0);
  }
  void Align() {
    if (w.size() % 2) w.push_back(0);
  }
  DlgBuilder(DWORD style, short cx, short cy, const char* title) {
    Dword(style | DS_SETFONT);
    Dword(0);
    w.push_back(0);  // the item count, filled in by Item
    for (short v : {(short)10, (short)10, cx, cy}) w.push_back((WORD)v);
    w.push_back(0);  // no menu
    w.push_back(0);  // the dialog class
    Str(title);
    w.push_back(8);
    Str("MS Shell Dlg");
  }
  void Item(WORD cls, DWORD style, short x, short y, short cx, short cy, WORD id, const char* text) {
    Align();
    w[4]++;
    Dword(style | WS_CHILD | WS_VISIBLE);
    Dword(0);
    for (short v : {x, y, cx, cy}) w.push_back((WORD)v);
    w.push_back(id);
    w.push_back(0xFFFF);
    w.push_back(cls);
    Str(text);
    w.push_back(0);  // no creation data
  }
  LPCDLGTEMPLATEW Get() { return (LPCDLGTEMPLATEW)w.data(); }
};

static LPARAM g_initParam = 0;
static std::vector<WPARAM> g_dlgCommands;

static INT_PTR CALLBACK TestDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_INITDIALOG:
      g_initParam = lp;
      if (lp == 2) ::PostMessageW(h, WM_COMMAND, IDOK, 0);
      return TRUE;
    case WM_COMMAND:
      g_dlgCommands.push_back(wp);
      if (LOWORD(wp) == IDOK && g_initParam == 2) ::EndDialog(h, 7);
      return TRUE;
  }
  return FALSE;
}

static bool SawCommand(WORD id, WORD code) {
  for (WPARAM wp : g_dlgCommands)
    if (LOWORD(wp) == id && HIWORD(wp) == code) return true;
  return false;
}

static std::wstring DlgText(HWND dlg, int id) {
  wchar_t buf[128] = {0};
  ::GetDlgItemTextW(dlg, id, buf, 128);
  return buf;
}

static void TestDialogsAndControls() {
  DlgBuilder b(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 220, 140, "Dialog");
  b.Item(0x0080, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 160, 5, 50, 14, IDOK, "OK");
  b.Item(0x0082, SS_LEFT, 5, 5, 30, 8, 16, "&Name:");
  b.Item(0x0081, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 40, 5, 100, 12, 10, "");
  b.Item(0x0080, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 5, 22, 60, 10, 11, "&Check");
  b.Item(0x0080, BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 5, 34, 60, 10, 12, "One");
  b.Item(0x0080, BS_AUTORADIOBUTTON, 5, 46, 60, 10, 13, "Two");
  b.Item(0x0083, LBS_STANDARD | WS_TABSTOP | WS_GROUP, 80, 22, 60, 50, 14, "");
  b.Item(0x0085, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 150, 22, 60, 60, 15, "");

  g_dlgCommands.clear();
  HWND dlg = ::CreateDialogIndirectParamW(::GetModuleHandleW(NULL), b.Get(), NULL, TestDlgProc, 1);
  CHECK(dlg && g_initParam == 1);
  wchar_t cls[32];
  ::GetClassNameW(dlg, cls, 32);
  CHECK(wcscmp(cls, L"#32770") == 0);
  HWND edit = ::GetDlgItem(dlg, 10);
  CHECK(edit && ::GetDlgCtrlID(edit) == 10);
  ::GetClassNameW(edit, cls, 32);
  CHECK(_wcsicmp(cls, L"Edit") == 0);
  CHECK(LOWORD(::SendMessageW(dlg, DM_GETDEFID, 0, 0)) == IDOK);
  RECT units = {0, 0, 4, 8};
  CHECK(::MapDialogRect(dlg, &units) && units.right > 0 && units.bottom > 0);

  // Text and numbers through the dialog helpers.
  CHECK(::SetDlgItemTextW(dlg, 10, L"hello world"));
  CHECK(DlgText(dlg, 10) == L"hello world");
  CHECK(::SetDlgItemInt(dlg, 10, (UINT)-42, TRUE));
  BOOL ok = FALSE;
  CHECK((int)::GetDlgItemInt(dlg, 10, &ok, TRUE) == -42 && ok);
  ::SetDlgItemTextW(dlg, 10, L"abc");
  ::GetDlgItemInt(dlg, 10, &ok, TRUE);
  CHECK(!ok);

  // The edit control: selection, replacement and undo.
  ::SetWindowTextW(edit, L"hello world");
  ::SendMessageW(edit, EM_SETSEL, 0, 5);
  DWORD s0 = 0, s1 = 0;
  ::SendMessageW(edit, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
  CHECK(s0 == 0 && s1 == 5);
  ::SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L"HELLO");
  CHECK(DlgText(dlg, 10) == L"HELLO world");
  CHECK(::SendMessageW(edit, EM_CANUNDO, 0, 0));
  ::SendMessageW(edit, EM_UNDO, 0, 0);
  CHECK(DlgText(dlg, 10) == L"hello world");
  ::SendMessageW(edit, EM_LIMITTEXT, 5, 0);
  ::SendMessageW(edit, EM_SETSEL, 0, -1);
  ::SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)L"123456789");
  CHECK(DlgText(dlg, 10) == L"12345");
  ::SendMessageW(edit, EM_LIMITTEXT, 0, 0);
  CHECK(SawCommand(10, EN_CHANGE));

  // Copy and paste through the clipboard, which converts text.
  ::SetWindowTextW(edit, L"copy me");
  ::SendMessageW(edit, EM_SETSEL, 0, 4);
  ::SendMessageW(edit, WM_COPY, 0, 0);
  CHECK(::IsClipboardFormatAvailable(CF_UNICODETEXT) && ::IsClipboardFormatAvailable(CF_TEXT));
  if (::OpenClipboard(dlg)) {
    HANDLE ht = ::GetClipboardData(CF_TEXT);
    const char* a = ht ? (const char*)::GlobalLock(ht) : NULL;
    CHECK(a && strcmp(a, "copy") == 0);
    if (a) ::GlobalUnlock(ht);
    ::CloseClipboard();
  }
  ::SendMessageW(edit, EM_SETSEL, 7, 7);
  ::SendMessageW(edit, WM_PASTE, 0, 0);
  CHECK(DlgText(dlg, 10) == L"copy mecopy");

  // Buttons: check boxes and a radio group.
  CHECK(::IsDlgButtonChecked(dlg, 11) == BST_UNCHECKED);
  ::SendDlgItemMessageW(dlg, 11, BM_CLICK, 0, 0);
  CHECK(::IsDlgButtonChecked(dlg, 11) == BST_CHECKED);
  CHECK(SawCommand(11, BN_CLICKED));
  CHECK(::CheckDlgButton(dlg, 11, BST_UNCHECKED) && !::IsDlgButtonChecked(dlg, 11));
  CHECK(::CheckRadioButton(dlg, 12, 13, 12));
  CHECK(::IsDlgButtonChecked(dlg, 12) && !::IsDlgButtonChecked(dlg, 13));
  ::SendDlgItemMessageW(dlg, 13, BM_CLICK, 0, 0);
  CHECK(!::IsDlgButtonChecked(dlg, 12) && ::IsDlgButtonChecked(dlg, 13));
  CHECK(::GetNextDlgGroupItem(dlg, ::GetDlgItem(dlg, 12), FALSE) == ::GetDlgItem(dlg, 13));

  // The list box, sorted.
  HWND list = ::GetDlgItem(dlg, 14);
  for (const wchar_t* s : {L"pear", L"apple", L"fig"}) ::SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)s);
  CHECK(::SendMessageW(list, LB_GETCOUNT, 0, 0) == 3);
  wchar_t buf[64];
  ::SendMessageW(list, LB_GETTEXT, 0, (LPARAM)buf);
  CHECK(wcscmp(buf, L"apple") == 0);
  CHECK(::SendMessageW(list, LB_FINDSTRING, (WPARAM)-1, (LPARAM)L"pe") == 2);
  CHECK(::SendMessageW(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)L"pe") == LB_ERR);
  ::SendMessageW(list, LB_SETCURSEL, 1, 0);
  CHECK(::SendMessageW(list, LB_GETCURSEL, 0, 0) == 1);
  ::SendMessageW(list, LB_SETITEMDATA, 1, 1234);
  CHECK(::SendMessageW(list, LB_GETITEMDATA, 1, 0) == 1234);
  CHECK(::SendMessageW(list, LB_DELETESTRING, 0, 0) == 2);
  CHECK(::SendMessageW(list, LB_GETCURSEL, 0, 0) == 0);

  // The drop-down list combo box.
  HWND combo = ::GetDlgItem(dlg, 15);
  for (const wchar_t* s : {L"red", L"green", L"blue"}) ::SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)s);
  CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
  ::SendMessageW(combo, CB_SETCURSEL, 1, 0);
  CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);
  CHECK(DlgText(dlg, 15) == L"green");
  CHECK(::SendMessageW(combo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)L"bl") == 2);
  CHECK(DlgText(dlg, 15) == L"blue");
  COMBOBOXINFO ci = {sizeof ci};
  CHECK(::GetComboBoxInfo(combo, &ci) && ci.hwndList && ci.hwndCombo == combo);
  CHECK(!::SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0));

  // Keyboard navigation through IsDialogMessage.
  ::ShowWindow(dlg, SW_SHOW);
  ::SetFocus(edit);
  MSG m = {};
  m.hwnd = edit;
  m.message = WM_KEYDOWN;
  m.wParam = VK_TAB;
  CHECK(::IsDialogMessageW(dlg, &m));
  CHECK(::GetFocus() == ::GetDlgItem(dlg, 11));
  CHECK(::GetNextDlgTabItem(dlg, edit, FALSE) == ::GetDlgItem(dlg, 11));
  CHECK(::GetNextDlgTabItem(dlg, edit, TRUE) == ::GetDlgItem(dlg, IDOK));
  m.hwnd = ::GetFocus();
  m.wParam = VK_RETURN;
  g_dlgCommands.clear();
  CHECK(::IsDialogMessageW(dlg, &m));
  Pump();
  CHECK(SawCommand(IDOK, BN_CLICKED));
  ::DestroyWindow(dlg);
  CHECK(!::IsWindow(dlg) && !::IsWindow(edit));

  // A modal dialog, ended from its own procedure.
  CHECK(::DialogBoxIndirectParamW(::GetModuleHandleW(NULL), b.Get(), NULL, TestDlgProc, 2) == 7);
  CHECK(g_initParam == 2);
}

// ---------------------------------------------------------------------
// MDI: a frame, its client and children, and the window menu.

static HWND g_mdiClient = NULL;

static LRESULT CALLBACK MdiFrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return ::DefFrameProcW(h, g_mdiClient, msg, wp, lp);
}

static int g_mdiActivations = 0;
static LRESULT CALLBACK MdiChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_MDIACTIVATE && (HWND)lp == h) g_mdiActivations++;
  return ::DefMDIChildProcW(h, msg, wp, lp);
}

static void TestMdi() {
  HINSTANCE inst = ::GetModuleHandleW(NULL);
  WNDCLASSW wc = {};
  wc.lpfnWndProc = MdiFrameProc;
  wc.hInstance = inst;
  wc.lpszClassName = L"MdiFrame";
  ::RegisterClassW(&wc);
  wc.lpfnWndProc = MdiChildProc;
  wc.lpszClassName = L"MdiChild";
  ::RegisterClassW(&wc);

  HMENU bar = ::CreateMenu(), win = ::CreatePopupMenu();
  ::AppendMenuW(win, MF_STRING, 300, L"&Cascade");
  ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)win, L"&Window");
  HWND frame = ::CreateWindowExW(0, L"MdiFrame", L"Frame", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 600, 400,
                                 NULL, bar, inst, NULL);
  CLIENTCREATESTRUCT ccs = {win, 0xFF00};
  g_mdiClient = ::CreateWindowExW(0, L"MDICLIENT", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, frame,
                                  NULL, inst, &ccs);
  CHECK(g_mdiClient != NULL);
  RECT fc, cc;
  ::GetClientRect(frame, &fc);
  ::SendMessageW(frame, WM_SIZE, SIZE_RESTORED, MAKELPARAM(fc.right, fc.bottom));
  ::GetClientRect(g_mdiClient, &cc);
  CHECK(cc.right == fc.right && cc.bottom == fc.bottom);

  MDICREATESTRUCTW mcs = {L"MdiChild", L"One", inst, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          WS_VISIBLE, 0};
  HWND one = (HWND)::SendMessageW(g_mdiClient, WM_MDICREATE, 0, (LPARAM)&mcs);
  mcs.szTitle = L"Two";
  HWND two = (HWND)::SendMessageW(g_mdiClient, WM_MDICREATE, 0, (LPARAM)&mcs);
  CHECK(one && two && ::GetParent(one) == g_mdiClient);
  CHECK(::GetWindowLongW(one, GWL_EXSTYLE) & WS_EX_MDICHILD);
  BOOL maxed = TRUE;
  CHECK((HWND)::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, (LPARAM)&maxed) == two && !maxed);
  CHECK(g_mdiActivations == 2);
  // The window menu lists the children, the active one checked.
  CHECK(::GetMenuItemCount(win) == 4);
  CHECK(::GetMenuState(win, 0xFF01, MF_BYCOMMAND) & MF_CHECKED);
  wchar_t text[64];
  ::GetMenuStringW(win, 0xFF00, text, 64, MF_BYCOMMAND);
  CHECK(wcscmp(text, L"&1 One") == 0);
  // Choosing one from the menu activates it.
  ::SendMessageW(frame, WM_COMMAND, 0xFF00, 0);
  CHECK((HWND)::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, 0) == one);
  ::SendMessageW(g_mdiClient, WM_MDINEXT, 0, 0);
  CHECK((HWND)::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, 0) == two);

  // Maximized: the caption goes, the buttons and title go to the frame.
  int items = ::GetMenuItemCount(bar);
  ::SendMessageW(g_mdiClient, WM_MDIMAXIMIZE, (WPARAM)two, 0);
  CHECK(::IsZoomed(two));
  CHECK((HWND)::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, (LPARAM)&maxed) == two && maxed);
  CHECK(::GetMenuItemCount(bar) == items + 4);
  ::GetWindowTextW(frame, text, 64);
  CHECK(wcscmp(text, L"Frame - [Two]") == 0);
  RECT kc;
  ::GetClientRect(two, &kc);
  CHECK(kc.right == cc.right && kc.bottom == cc.bottom);
  // Activating another keeps the maximized state.
  ::SendMessageW(g_mdiClient, WM_MDIACTIVATE, (WPARAM)one, 0);
  CHECK(::IsZoomed(one) && !::IsZoomed(two));
  ::GetWindowTextW(frame, text, 64);
  CHECK(wcscmp(text, L"Frame - [One]") == 0);
  // The restore button, through the frame.
  ::SendMessageW(frame, WM_COMMAND, SC_RESTORE, 0);
  CHECK(!::IsZoomed(one) && ::GetMenuItemCount(bar) == items);
  ::GetWindowTextW(frame, text, 64);
  CHECK(wcscmp(text, L"Frame") == 0);

  // Tiling side by side.
  ::SendMessageW(g_mdiClient, WM_MDITILE, MDITILE_VERTICAL, 0);
  RECT r1, r2;
  ::GetWindowRect(one, &r1);
  ::GetWindowRect(two, &r2);
  CHECK(r1.top == r2.top && (r1.right == r2.left || r2.right == r1.left));

  // Closing the active one activates the other and renumbers it.
  ::SendMessageW(one, WM_CLOSE, 0, 0);
  CHECK(!::IsWindow(one));
  CHECK((HWND)::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, 0) == two);
  CHECK(::GetDlgCtrlID(two) == 0xFF00 && ::GetMenuItemCount(win) == 3);
  ::SendMessageW(g_mdiClient, WM_MDIDESTROY, (WPARAM)two, 0);
  CHECK(!::IsWindow(two) && ::SendMessageW(g_mdiClient, WM_MDIGETACTIVE, 0, 0) == 0);
  CHECK(::GetMenuItemCount(win) == 1);
  ::DestroyWindow(frame);
  g_mdiClient = NULL;
}

// ---------------------------------------------------------------------
// Image lists.

static void TestImageLists() {
  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  // Two 8x8 images: a red square and a blue one, each on magenta with a
  // 2-pixel border.
  HBITMAP strip = ::CreateCompatibleBitmap(screen, 16, 8);
  HGDIOBJ old = ::SelectObject(mem, strip);
  RECT r = {0, 0, 16, 8};
  HBRUSH magenta = ::CreateSolidBrush(RGB(255, 0, 255)), red = ::CreateSolidBrush(RGB(255, 0, 0)),
         blue = ::CreateSolidBrush(RGB(0, 0, 255));
  ::FillRect(mem, &r, magenta);
  RECT a = {2, 2, 6, 6}, b = {10, 2, 14, 6};
  ::FillRect(mem, &a, red);
  ::FillRect(mem, &b, blue);
  ::SelectObject(mem, old);

  HIMAGELIST il = ImageList_Create(8, 8, ILC_MASK | ILC_COLOR24, 2, 2);
  CHECK(il && ImageList_GetImageCount(il) == 0);
  CHECK(ImageList_AddMasked(il, strip, RGB(255, 0, 255)) == 0);
  CHECK(ImageList_GetImageCount(il) == 2);
  int cx = 0, cy = 0;
  CHECK(ImageList_GetIconSize(il, &cx, &cy) && cx == 8 && cy == 8);

  // Drawn transparently on white, and over a background color.
  HBITMAP canvas = ::CreateCompatibleBitmap(screen, 32, 8);
  old = ::SelectObject(mem, canvas);
  RECT all = {0, 0, 32, 8};
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(ImageList_Draw(il, 0, mem, 0, 0, ILD_TRANSPARENT));
  CHECK(::GetPixel(mem, 3, 3) == RGB(255, 0, 0) && ::GetPixel(mem, 0, 0) == RGB(255, 255, 255));
  CHECK(ImageList_DrawEx(il, 1, mem, 8, 0, 0, 0, RGB(0, 255, 0), CLR_NONE, ILD_NORMAL));
  CHECK(::GetPixel(mem, 11, 3) == RGB(0, 0, 255) && ::GetPixel(mem, 8, 0) == RGB(0, 255, 0));
  // The mask alone, and a blend toward the highlight color.
  CHECK(ImageList_Draw(il, 0, mem, 16, 0, ILD_MASK));
  CHECK(::GetPixel(mem, 19, 3) == RGB(0, 0, 0) && ::GetPixel(mem, 16, 0) == RGB(255, 255, 255));
  // Windows may dither the blend rather than mix each pixel, so a 2x2
  // block is averaged.
  ImageList_DrawEx(il, 0, mem, 24, 0, 0, 0, CLR_NONE, RGB(0, 0, 255), ILD_BLEND50);
  int rsum = 0, bsum = 0;
  for (int y = 2; y < 4; y++)
    for (int x = 26; x < 28; x++) {
      rsum += GetRValue(::GetPixel(mem, x, y));
      bsum += GetBValue(::GetPixel(mem, x, y));
    }
  CHECK(rsum / 4 > 100 && rsum / 4 < 155 && bsum / 4 > 100);
  // An overlay drawn on top.
  CHECK(ImageList_SetOverlayImage(il, 1, 1));
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  ImageList_Draw(il, 0, mem, 0, 0, ILD_TRANSPARENT | INDEXTOOVERLAYMASK(1));
  CHECK(::GetPixel(mem, 3, 3) == RGB(0, 0, 255));
  ::SelectObject(mem, old);

  // Icons in and out.
  HICON icon = ImageList_GetIcon(il, 0, ILD_NORMAL);
  CHECK(icon != NULL);
  CHECK(ImageList_AddIcon(il, icon) == 2);
  ::SelectObject(mem, canvas);
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  ImageList_Draw(il, 2, mem, 0, 0, ILD_TRANSPARENT);
  CHECK(::GetPixel(mem, 3, 3) == RGB(255, 0, 0) && ::GetPixel(mem, 0, 0) == RGB(255, 255, 255));
  ::SelectObject(mem, old);
  ::DestroyIcon(icon);

  IMAGEINFO info;
  CHECK(ImageList_GetImageInfo(il, 1, &info) && info.hbmImage && info.hbmMask && info.rcImage.left == 8);
  HIMAGELIST copy = ImageList_Duplicate(il);
  CHECK(ImageList_GetImageCount(copy) == 3);
  HIMAGELIST merged = ImageList_Merge(il, 0, il, 1, 4, 0);
  CHECK(merged && ImageList_GetIconSize(merged, &cx, &cy) && cx == 12 && cy == 8);
  CHECK(ImageList_Remove(il, 0) && ImageList_GetImageCount(il) == 2);
  CHECK(ImageList_RemoveAll(il) && ImageList_GetImageCount(il) == 0);
  CHECK(ImageList_Destroy(il) && ImageList_Destroy(copy) && ImageList_Destroy(merged));
  CHECK(!ImageList_Destroy(il));

  ::DeleteObject(strip);
  ::DeleteObject(canvas);
  ::DeleteObject(magenta);
  ::DeleteObject(red);
  ::DeleteObject(blue);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);
}

// A CWnd with a message map

class CTestWnd : public CWnd {
public:
  int m_nCreate = 0, m_nSize = 0, m_nTimer = 0, m_nApp = 0;
  bool m_bPostNcDestroy = false;
  CSize m_size;

protected:
  afx_msg int OnCreate(LPCREATESTRUCT cs) {
    m_nCreate++;
    return CWnd::OnCreate(cs);
  }
  afx_msg void OnSize(UINT type, int cx, int cy) {
    m_nSize++;
    m_size = CSize(cx, cy);
    CWnd::OnSize(type, cx, cy);
  }
  afx_msg void OnTimer(UINT_PTR id) {
    if (id == 7) m_nTimer++;
  }
  afx_msg LRESULT OnApp(WPARAM wp, LPARAM) {
    m_nApp++;
    return wp * 2;
  }
  virtual void PostNcDestroy() { m_bPostNcDestroy = true; }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CTestWnd, CWnd)
  ON_WM_CREATE()
  ON_WM_SIZE()
  ON_WM_TIMER()
  ON_MESSAGE(WM_APP + 5, OnApp)
END_MESSAGE_MAP()

// A parent that records what its common controls tell it.
static std::vector<UINT> g_notifies;
static int g_hscrolls = 0, g_vscrolls = 0;
static LRESULT CALLBACK CtlHostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NOTIFY) g_notifies.push_back(((NMHDR*)lp)->code);
  if (msg == WM_HSCROLL) g_hscrolls++;
  if (msg == WM_VSCROLL) g_vscrolls++;
  return ::DefWindowProcW(h, msg, wp, lp);
}

static bool Notified(UINT code) {
  for (UINT c : g_notifies)
    if (c == code) return true;
  return false;
}

static void TestCommonControls() {
  INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
  CHECK(::InitCommonControlsEx(&icc));
  WNDCLASSW wc = {};
  wc.lpfnWndProc = CtlHostProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"CtlHost";
  ::RegisterClassW(&wc);
  HWND host = ::CreateWindowW(L"CtlHost", L"Controls", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 420, 320,
                              NULL, NULL, wc.hInstance, NULL);
  CHECK(host != NULL);
  HINSTANCE inst = wc.hInstance;
  DWORD child = WS_CHILD | WS_VISIBLE;

  // Status bar with parts.
  HWND status = ::CreateWindowW(STATUSCLASSNAMEW, L"Ready", child | SBARS_SIZEGRIP, 0, 0, 0, 0, host,
                                (HMENU)1, inst, NULL);
  CHECK(status != NULL);
  int parts[] = {200, 300, -1};
  CHECK(::SendMessageW(status, SB_SETPARTS, 3, (LPARAM)parts));
  CHECK(::SendMessageW(status, SB_GETPARTS, 0, 0) == 3);
  ::SendMessageW(status, SB_SETTEXTW, 1, (LPARAM)L"NUM");
  wchar_t buf[64] = L"";
  CHECK(LOWORD(::SendMessageW(status, SB_GETTEXTW, 1, (LPARAM)buf)) == 3 && wcscmp(buf, L"NUM") == 0);

  // Progress.
  HWND prog = ::CreateWindowW(PROGRESS_CLASSW, L"", child, 10, 10, 180, 16, host, (HMENU)2, inst, NULL);
  CHECK(prog != NULL);
  ::SendMessageW(prog, PBM_SETRANGE32, 0, 200);
  CHECK(::SendMessageW(prog, PBM_GETRANGE, FALSE, 0) == 200);
  ::SendMessageW(prog, PBM_SETPOS, 50, 0);
  CHECK(::SendMessageW(prog, PBM_DELTAPOS, 25, 0) == 50);
  CHECK(::SendMessageW(prog, PBM_GETPOS, 0, 0) == 75);
  ::SendMessageW(prog, PBM_SETPOS, 500, 0);
  CHECK(::SendMessageW(prog, PBM_GETPOS, 0, 0) == 200);
  ::SendMessageW(prog, PBM_SETPOS, 100, 0);

  // Trackbar: range, position clamping, keys scroll the parent.
  HWND track = ::CreateWindowW(TRACKBAR_CLASSW, L"", child | WS_TABSTOP | TBS_AUTOTICKS, 10, 34, 180, 30, host,
                               (HMENU)3, inst, NULL);
  CHECK(track != NULL);
  ::SendMessageW(track, TBM_SETRANGE, TRUE, MAKELONG(0, 10));
  CHECK(::SendMessageW(track, TBM_GETRANGEMIN, 0, 0) == 0 && ::SendMessageW(track, TBM_GETRANGEMAX, 0, 0) == 10);
  ::SendMessageW(track, TBM_SETPOS, TRUE, 4);
  CHECK(::SendMessageW(track, TBM_GETPOS, 0, 0) == 4);
  ::SendMessageW(track, TBM_SETPOS, TRUE, 99);
  CHECK(::SendMessageW(track, TBM_GETPOS, 0, 0) == 10);
  ::SendMessageW(track, TBM_SETPOS, TRUE, 4);
  g_hscrolls = 0;
  ::SendMessageW(track, WM_KEYDOWN, VK_RIGHT, 0);
  CHECK(::SendMessageW(track, TBM_GETPOS, 0, 0) == 5 && g_hscrolls > 0);
  RECT thumb = {}, chan = {};
  ::SendMessageW(track, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
  ::SendMessageW(track, TBM_GETCHANNELRECT, 0, (LPARAM)&chan);
  CHECK(thumb.right > thumb.left && chan.right > chan.left);

  // Up-down with an edit buddy.
  HWND edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", child | ES_NUMBER, 200, 10, 60, 20, host,
                                (HMENU)4, inst, NULL);
  HWND spin = ::CreateWindowW(UPDOWN_CLASSW, L"", child | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS, 0, 0,
                              0, 0, host, (HMENU)5, inst, NULL);
  CHECK(spin != NULL);
  ::SendMessageW(spin, UDM_SETBUDDY, (WPARAM)edit, 0);
  CHECK((HWND)::SendMessageW(spin, UDM_GETBUDDY, 0, 0) == edit);
  ::SendMessageW(spin, UDM_SETRANGE32, 0, 20);
  ::SendMessageW(spin, UDM_SETPOS32, 0, 7);
  CHECK(::SendMessageW(spin, UDM_GETPOS32, 0, NULL) == 7);
  ::GetWindowTextW(edit, buf, 64);
  CHECK(wcscmp(buf, L"7") == 0);
  g_notifies.clear();
  g_vscrolls = 0;
  ::SendMessageW(edit, WM_KEYDOWN, VK_UP, 0);
  CHECK(::SendMessageW(spin, UDM_GETPOS32, 0, NULL) == 8 && Notified(UDN_DELTAPOS) && g_vscrolls > 0);
  ::GetWindowTextW(edit, buf, 64);
  CHECK(wcscmp(buf, L"8") == 0);

  // Header: items, text, layout and hit testing.
  HWND header = ::CreateWindowW(WC_HEADERW, L"", child | HDS_BUTTONS, 10, 70, 300, 20, host, (HMENU)6, inst, NULL);
  CHECK(header != NULL);
  HDITEMW hi = {};
  hi.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
  hi.fmt = HDF_LEFT | HDF_STRING;
  hi.cxy = 100;
  hi.pszText = (LPWSTR)L"Name";
  CHECK(::SendMessageW(header, HDM_INSERTITEMW, 0, (LPARAM)&hi) == 0);
  hi.pszText = (LPWSTR)L"Size";
  hi.cxy = 60;
  CHECK(::SendMessageW(header, HDM_INSERTITEMW, 1, (LPARAM)&hi) == 1);
  CHECK(::SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 2);
  RECT ir = {};
  CHECK(::SendMessageW(header, HDM_GETITEMRECT, 1, (LPARAM)&ir) && ir.left == 100 && ir.right == 160);
  HDITEMW got = {};
  got.mask = HDI_TEXT | HDI_WIDTH;
  got.pszText = buf;
  got.cchTextMax = 64;
  CHECK(::SendMessageW(header, HDM_GETITEMW, 1, (LPARAM)&got) && wcscmp(buf, L"Size") == 0 && got.cxy == 60);
  HDHITTESTINFO ht = {};
  ht.pt.x = 120;
  ht.pt.y = 5;
  CHECK(::SendMessageW(header, HDM_HITTEST, 0, (LPARAM)&ht) == 1 && (ht.flags & HHT_ONHEADER));
  int order[2] = {};
  CHECK(::SendMessageW(header, HDM_GETORDERARRAY, 2, (LPARAM)order) && order[0] == 0 && order[1] == 1);

  // Tabs: items, selection by mouse notifies the parent.
  HWND tabs = ::CreateWindowW(WC_TABCONTROLW, L"", child | WS_CLIPSIBLINGS, 10, 100, 300, 120, host, (HMENU)7,
                              inst, NULL);
  CHECK(tabs != NULL);
  TCITEMW ti = {};
  ti.mask = TCIF_TEXT | TCIF_PARAM;
  ti.pszText = (LPWSTR)L"General";
  ti.lParam = 11;
  CHECK(::SendMessageW(tabs, TCM_INSERTITEMW, 0, (LPARAM)&ti) == 0);
  ti.pszText = (LPWSTR)L"Advanced";
  ti.lParam = 22;
  CHECK(::SendMessageW(tabs, TCM_INSERTITEMW, 1, (LPARAM)&ti) == 1);
  CHECK(::SendMessageW(tabs, TCM_GETITEMCOUNT, 0, 0) == 2 && ::SendMessageW(tabs, TCM_GETCURSEL, 0, 0) == 0);
  CHECK(TabCtrl_GetItemCount(tabs) == 2 && TabCtrl_GetCurSel(tabs) == 0);
  TCITEMW tg = {};
  tg.mask = TCIF_TEXT | TCIF_PARAM;
  tg.pszText = buf;
  tg.cchTextMax = 64;
  CHECK(::SendMessageW(tabs, TCM_GETITEMW, 1, (LPARAM)&tg) && wcscmp(buf, L"Advanced") == 0 && tg.lParam == 22);
  RECT tr = {};
  CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, 1, (LPARAM)&tr) && tr.right > tr.left);
  g_notifies.clear();
  ::SendMessageW(tabs, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((tr.left + tr.right) / 2, (tr.top + tr.bottom) / 2));
  ::SendMessageW(tabs, WM_LBUTTONUP, 0, MAKELPARAM((tr.left + tr.right) / 2, (tr.top + tr.bottom) / 2));
  CHECK(::SendMessageW(tabs, TCM_GETCURSEL, 0, 0) == 1 && Notified(TCN_SELCHANGING) && Notified(TCN_SELCHANGE));
  RECT page = {0, 0, 300, 120};
  ::SendMessageW(tabs, TCM_ADJUSTRECT, FALSE, (LPARAM)&page);
  CHECK(page.top > 0 && page.bottom < 120 && page.left > 0);

  // Hotkey.
  HWND hot = ::CreateWindowW(HOTKEY_CLASSW, L"", child | WS_BORDER, 200, 34, 120, 20, host, (HMENU)8, inst, NULL);
  CHECK(hot != NULL);
  ::SendMessageW(hot, HKM_SETHOTKEY, MAKEWORD('K', HOTKEYF_CONTROL), 0);
  CHECK(::SendMessageW(hot, HKM_GETHOTKEY, 0, 0) == MAKEWORD('K', HOTKEYF_CONTROL));

  ::ShowWindow(host, SW_SHOW);
  ::UpdateWindow(host);
  Pump();
  ::DestroyWindow(host);
}

static std::vector<UINT> g_commands;  // WM_COMMAND notification codes

static LRESULT CALLBACK MoreHostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NOTIFY) g_notifies.push_back(((NMHDR*)lp)->code);
  if (msg == WM_COMMAND && lp) g_commands.push_back(HIWORD(wp));
  return ::DefWindowProcW(h, msg, wp, lp);
}

static bool Commanded(UINT code) {
  for (UINT c : g_commands)
    if (c == code) return true;
  return false;
}

// Pumps messages for up to half a second until the code comes.
static bool WaitCommand(UINT code) {
  for (int i = 0; i < 50 && !Commanded(code); i++) {
    Pump();
    if (!Commanded(code)) ::Sleep(10);
  }
  return Commanded(code);
}

static bool SameDate(const SYSTEMTIME& a, const SYSTEMTIME& b) {
  return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

// A two-frame, 16x16, 8-bit uncompressed AVI file.
static std::vector<BYTE> MakeAvi() {
  std::vector<BYTE> v;
  auto put32 = [&](DWORD d) {
    for (int i = 0; i < 4; i++) v.push_back((BYTE)(d >> (8 * i)));
  };
  auto put16 = [&](WORD w) {
    v.push_back((BYTE)w);
    v.push_back((BYTE)(w >> 8));
  };
  auto fcc = [&](const char* s) { v.insert(v.end(), s, s + 4); };
  auto patch = [&](size_t at) {  // the size of the chunk whose size field is at `at`
    DWORD n = (DWORD)(v.size() - at - 4);
    for (int i = 0; i < 4; i++) v[at + i] = (BYTE)(n >> (8 * i));
  };
  const int w = 16, h = 16, frames = 2;
  fcc("RIFF");
  size_t riff = v.size();
  put32(0);
  fcc("AVI ");
  fcc("LIST");
  size_t hdrl = v.size();
  put32(0);
  fcc("hdrl");
  fcc("avih");
  put32(56);
  put32(100000);         // microseconds per frame
  put32(0);              // max bytes per second
  put32(0);              // padding
  put32(0x10);           // AVIF_HASINDEX
  put32(frames);         // total frames
  put32(0);              // initial frames
  put32(1);              // streams
  put32(w * h);          // suggested buffer size
  put32(w);
  put32(h);
  for (int i = 0; i < 4; i++) put32(0);
  fcc("LIST");
  size_t strl = v.size();
  put32(0);
  fcc("strl");
  fcc("strh");
  put32(56);
  fcc("vids");
  fcc("DIB ");
  put32(0);              // flags
  put16(0);              // priority
  put16(0);              // language
  put32(0);              // initial frames
  put32(1);              // scale
  put32(10);             // rate
  put32(0);              // start
  put32(frames);         // length
  put32(w * h);          // suggested buffer size
  put32((DWORD)-1);      // quality
  put32(0);              // sample size
  put16(0);
  put16(0);
  put16(w);
  put16(h);
  fcc("strf");
  put32(40 + 2 * 4);
  put32(40);
  put32(w);
  put32(h);
  put16(1);
  put16(8);
  put32(BI_RGB);
  put32(w * h);
  put32(0);
  put32(0);
  put32(2);              // colors used
  put32(0);
  put32(0x00ff0000);     // blue... (RGBQUAD order: blue, green, red)
  put32(0x000000ff);
  patch(strl);
  patch(hdrl);
  fcc("LIST");
  size_t movi = v.size();
  put32(0);
  fcc("movi");
  DWORD offsets[frames];
  for (int f = 0; f < frames; f++) {
    offsets[f] = (DWORD)(v.size() - (movi + 4));
    fcc("00db");
    put32(w * h);
    for (int i = 0; i < w * h; i++) v.push_back((BYTE)((i / w < h / 2) == (f == 0)));
  }
  patch(movi);
  fcc("idx1");
  put32(16 * frames);
  for (int f = 0; f < frames; f++) {
    fcc("00db");
    put32(0x10);  // AVIIF_KEYFRAME
    put32(offsets[f]);
    put32(w * h);
  }
  patch(riff);
  return v;
}

// The rest of the common controls: IP address, ComboBoxEx, month
// calendar, date and time picker, animation.
static void TestMoreControls() {
  INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_INTERNET_CLASSES | ICC_USEREX_CLASSES | ICC_DATE_CLASSES |
                                              ICC_ANIMATE_CLASS};
  CHECK(::InitCommonControlsEx(&icc));
  WNDCLASSW wc = {};
  wc.lpfnWndProc = MoreHostProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"MoreHost";
  ::RegisterClassW(&wc);
  HWND host = ::CreateWindowW(L"MoreHost", L"More controls", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 520,
                              400, NULL, NULL, wc.hInstance, NULL);
  CHECK(host != NULL);
  HINSTANCE inst = wc.hInstance;
  DWORD child = WS_CHILD | WS_VISIBLE;
  wchar_t buf[64] = L"";

  // IP address: the address in and out, as text too, and a blank field.
  HWND ip = ::CreateWindowW(WC_IPADDRESSW, L"", child | WS_TABSTOP, 10, 10, 150, 22, host, (HMENU)1, inst, NULL);
  CHECK(ip != NULL);
  CHECK(::SendMessageW(ip, IPM_ISBLANK, 0, 0));
  ::SendMessageW(ip, IPM_SETADDRESS, 0, MAKEIPADDRESS(192, 168, 1, 20));
  DWORD addr = 0;
  CHECK(::SendMessageW(ip, IPM_GETADDRESS, 0, (LPARAM)&addr) == 4 && addr == MAKEIPADDRESS(192, 168, 1, 20));
  CHECK(!::SendMessageW(ip, IPM_ISBLANK, 0, 0));
  ::GetWindowTextW(ip, buf, 64);
  CHECK(wcscmp(buf, L"192.168.1.20") == 0);
  ::SendMessageW(ip, IPM_CLEARADDRESS, 0, 0);
  CHECK(::SendMessageW(ip, IPM_ISBLANK, 0, 0));
  CHECK(::SendMessageW(ip, IPM_GETADDRESS, 0, (LPARAM)&addr) == 0);

  // ComboBoxEx: items with text and data through the inner combo box.
  HWND cbx = ::CreateWindowW(WC_COMBOBOXEXW, L"", child | CBS_DROPDOWNLIST, 180, 10, 160, 200, host, (HMENU)2,
                             inst, NULL);
  CHECK(cbx != NULL);
  HWND inner = (HWND)::SendMessageW(cbx, CBEM_GETCOMBOCONTROL, 0, 0);
  CHECK(inner != NULL && ::GetParent(inner) == cbx);
  COMBOBOXEXITEMW cbi = {};
  cbi.mask = CBEIF_TEXT | CBEIF_LPARAM | CBEIF_INDENT;
  cbi.iItem = -1;
  cbi.pszText = (LPWSTR)L"Alpha";
  cbi.lParam = 101;
  CHECK(::SendMessageW(cbx, CBEM_INSERTITEMW, 0, (LPARAM)&cbi) == 0);
  cbi.pszText = (LPWSTR)L"Beta";
  cbi.lParam = 102;
  cbi.iIndent = 1;
  CHECK(::SendMessageW(cbx, CBEM_INSERTITEMW, 0, (LPARAM)&cbi) == 1);
  cbi.iItem = 0;
  cbi.pszText = (LPWSTR)L"First";
  cbi.lParam = 100;
  cbi.iIndent = 0;
  CHECK(::SendMessageW(cbx, CBEM_INSERTITEMW, 0, (LPARAM)&cbi) == 0);
  CHECK(::SendMessageW(cbx, CB_GETCOUNT, 0, 0) == 3);
  COMBOBOXEXITEMW gi = {};
  gi.mask = CBEIF_TEXT | CBEIF_LPARAM | CBEIF_INDENT;
  gi.iItem = 2;
  gi.pszText = buf;
  gi.cchTextMax = 64;
  CHECK(::SendMessageW(cbx, CBEM_GETITEMW, 0, (LPARAM)&gi) && wcscmp(buf, L"Beta") == 0 && gi.lParam == 102 &&
        gi.iIndent == 1);
  ::SendMessageW(cbx, CB_SETCURSEL, 1, 0);
  CHECK(::SendMessageW(cbx, CB_GETCURSEL, 0, 0) == 1 && ::SendMessageW(cbx, CB_GETITEMDATA, 1, 0) == 101);
  CHECK(::SendMessageW(cbx, CBEM_DELETEITEM, 0, 0) == 2 && ::SendMessageW(cbx, CB_GETCOUNT, 0, 0) == 2);

  // Month calendar: the selection, the range and the first day.
  HWND cal = ::CreateWindowW(MONTHCAL_CLASSW, L"", child, 10, 50, 220, 160, host, (HMENU)3, inst, NULL);
  CHECK(cal != NULL);
  SYSTEMTIME day = {};
  day.wYear = 2026;
  day.wMonth = 11;
  day.wDay = 17;
  day.wDayOfWeek = 2;
  CHECK(::SendMessageW(cal, MCM_SETCURSEL, 0, (LPARAM)&day));
  SYSTEMTIME got = {};
  CHECK(::SendMessageW(cal, MCM_GETCURSEL, 0, (LPARAM)&got) && SameDate(got, day));
  RECT need = {};
  CHECK(::SendMessageW(cal, MCM_GETMINREQRECT, 0, (LPARAM)&need) && need.right > 100 && need.bottom > 80);
  SYSTEMTIME range[2] = {};
  range[0].wYear = 2026;
  range[0].wMonth = 1;
  range[0].wDay = 1;
  range[1].wYear = 2026;
  range[1].wMonth = 12;
  range[1].wDay = 31;
  CHECK(::SendMessageW(cal, MCM_SETRANGE, GDTR_MIN | GDTR_MAX, (LPARAM)range));
  SYSTEMTIME gotRange[2] = {};
  CHECK(::SendMessageW(cal, MCM_GETRANGE, 0, (LPARAM)gotRange) == (GDTR_MIN | GDTR_MAX) &&
        SameDate(gotRange[0], range[0]) && SameDate(gotRange[1], range[1]));
  SYSTEMTIME outside = day;
  outside.wYear = 2027;
  CHECK(!::SendMessageW(cal, MCM_SETCURSEL, 0, (LPARAM)&outside));
  CHECK(::SendMessageW(cal, MCM_GETCURSEL, 0, (LPARAM)&got) && SameDate(got, day));
  ::SendMessageW(cal, MCM_SETFIRSTDAYOFWEEK, 0, 6);  // Sunday
  CHECK(LOWORD(::SendMessageW(cal, MCM_GETFIRSTDAYOFWEEK, 0, 0)) == 6);
  ::SendMessageW(cal, MCM_SETCOLOR, MCSC_TITLEBK, RGB(0, 0, 128));
  CHECK(::SendMessageW(cal, MCM_GETCOLOR, MCSC_TITLEBK, 0) == RGB(0, 0, 128));

  // Date and time picker: the date in and out, none, and the format.
  HWND dtp = ::CreateWindowW(DATETIMEPICK_CLASSW, L"", child | WS_TABSTOP | DTS_SHOWNONE, 250, 50, 160, 22, host,
                             (HMENU)4, inst, NULL);
  CHECK(dtp != NULL);
  CHECK(::SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&day));
  memset(&got, 0, sizeof got);
  CHECK(::SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&got) == GDT_VALID && SameDate(got, day));
  CHECK(::SendMessageW(dtp, DTM_SETFORMATW, 0, (LPARAM)L"yyyy'-'MM'-'dd"));
  CHECK(::SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_NONE, (LPARAM)&day));
  CHECK(::SendMessageW(dtp, DTM_GETSYSTEMTIME, 0, (LPARAM)&got) == GDT_NONE);
  CHECK(::SendMessageW(dtp, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&day));
  ::SendMessageW(dtp, DTM_SETMCCOLOR, MCSC_TITLEBK, RGB(0, 128, 0));
  CHECK(::SendMessageW(dtp, DTM_GETMCCOLOR, MCSC_TITLEBK, 0) == RGB(0, 128, 0));
  ::ShowWindow(host, SW_SHOW);

  // Animation: a clip from a file, its first frame on show, playing and
  // stopping.
  std::vector<BYTE> avi = MakeAvi();
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  ::GetTempPathW(MAX_PATH, dir);
  ::GetTempFileNameW(dir, L"avi", 0, path);
  HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  DWORD wrote = 0;
  CHECK(file != INVALID_HANDLE_VALUE && ::WriteFile(file, avi.data(), (DWORD)avi.size(), &wrote, NULL));
  ::CloseHandle(file);
  HWND anim = ::CreateWindowW(ANIMATE_CLASSW, L"", child, 10, 230, 100, 100, host, (HMENU)5, inst, NULL);
  CHECK(anim != NULL);
  CHECK(!::SendMessageW(anim, ACM_OPENW, 0, (LPARAM)L"no such clip.avi"));
  CHECK(::SendMessageW(anim, ACM_OPENW, 0, (LPARAM)path));
  RECT ar = {};
  ::GetClientRect(anim, &ar);
  CHECK(ar.right == 16 && ar.bottom == 16);
  CHECK(::SendMessageW(anim, ACM_PLAY, 1, MAKELPARAM(0, 0)));
  Pump();
  ::UpdateWindow(host);
  HDC screen = ::GetDC(anim);
  CHECK(::GetPixel(screen, 2, 2) == RGB(255, 0, 0) && ::GetPixel(screen, 2, 13) == RGB(0, 0, 255));
  ::ReleaseDC(anim, screen);
  // The notifications may come posted.
  g_commands.clear();
  CHECK(::SendMessageW(anim, ACM_PLAY, (WPARAM)-1, MAKELPARAM(0, -1)));
  CHECK(WaitCommand(ACN_START));
  CHECK(::SendMessageW(anim, ACM_STOP, 0, 0));
  CHECK(WaitCommand(ACN_STOP));
  CHECK(::SendMessageW(anim, ACM_OPENW, 0, 0));
  ::DestroyWindow(anim);
  ::DeleteFileW(path);

  Pump();
  ::DestroyWindow(host);
}

static int CALLBACK CompareDown(LPARAM a, LPARAM b, LPARAM) { return (int)(b - a); }

static void TestListView() {
  HINSTANCE inst = ::GetModuleHandleW(NULL);
  HWND host = ::CreateWindowW(L"CtlHost", L"List", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 420, 320, NULL,
                              NULL, inst, NULL);
  HWND lv = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_EDITLABELS,
                              10, 10, 380, 250, host, (HMENU)20, inst, NULL);
  CHECK(lv != NULL);
  HWND header = (HWND)::SendMessageW(lv, LVM_GETHEADER, 0, 0);
  CHECK(header != NULL);

  // Columns go to the header.
  LVCOLUMNW col = {};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
  col.fmt = LVCFMT_LEFT;
  col.cx = 150;
  col.pszText = (LPWSTR)L"Name";
  CHECK(::SendMessageW(lv, LVM_INSERTCOLUMNW, 0, (LPARAM)&col) == 0);
  col.fmt = LVCFMT_RIGHT;
  col.cx = 80;
  col.pszText = (LPWSTR)L"Size";
  CHECK(::SendMessageW(lv, LVM_INSERTCOLUMNW, 1, (LPARAM)&col) == 1);
  CHECK(::SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 2);
  CHECK(::SendMessageW(lv, LVM_GETCOLUMNWIDTH, 1, 0) == 80);
  wchar_t buf[64] = L"";
  LVCOLUMNW gc = {};
  gc.mask = LVCF_TEXT | LVCF_FMT;
  gc.pszText = buf;
  gc.cchTextMax = 64;
  CHECK(::SendMessageW(lv, LVM_GETCOLUMNW, 1, (LPARAM)&gc) && wcscmp(buf, L"Size") == 0 && gc.fmt == LVCFMT_RIGHT);

  // Items and subitems.
  g_notifies.clear();
  const wchar_t* names[] = {L"apple", L"banana", L"cherry", L"date"};
  for (int i = 0; i < 4; i++) {
    LVITEMW it = {};
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = i;
    it.pszText = (LPWSTR)names[i];
    it.lParam = i * 10;
    CHECK(::SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it) == i);
    wchar_t size[16];
    swprintf(size, 16, L"%d KB", (i + 1) * 3);
    it.mask = LVIF_TEXT;
    it.iSubItem = 1;
    it.pszText = size;
    CHECK(::SendMessageW(lv, LVM_SETITEMTEXTW, i, (LPARAM)&it));
  }
  CHECK(Notified(LVN_INSERTITEM));
  CHECK(::SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0) == 4);
  // The same through commctrl.h's helper macros.
  CHECK(ListView_GetItemCount(lv) == 4 && ListView_GetHeader(lv) == header);
  CHECK(ListView_GetColumnWidth(lv, 1) == 80);
  ListView_SetItemState(lv, 2, LVIS_SELECTED, LVIS_SELECTED);
  CHECK(ListView_GetItemState(lv, 2, LVIS_SELECTED) == LVIS_SELECTED && ListView_GetSelectedCount(lv) == 1);
  CHECK(ListView_GetNextItem(lv, -1, LVNI_SELECTED) == 2);
  ListView_SetItemState(lv, 2, 0, LVIS_SELECTED);
  LVITEMW get = {};
  get.iSubItem = 1;
  get.pszText = buf;
  get.cchTextMax = 64;
  CHECK(::SendMessageW(lv, LVM_GETITEMTEXTW, 2, (LPARAM)&get) == 4 && wcscmp(buf, L"9 KB") == 0);
  get.mask = LVIF_TEXT | LVIF_PARAM;
  get.iItem = 1;
  get.iSubItem = 0;
  CHECK(::SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&get) && wcscmp(buf, L"banana") == 0 && get.lParam == 10);
  LVFINDINFOW fi = {};
  fi.flags = LVFI_STRING;
  fi.psz = L"Cherry";
  CHECK(::SendMessageW(lv, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&fi) == 2);
  fi.flags = LVFI_PARAM;
  fi.lParam = 30;
  CHECK(::SendMessageW(lv, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&fi) == 3);

  // Selection through state, with notifications.
  g_notifies.clear();
  LVITEMW st = {};
  st.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
  st.state = LVIS_SELECTED | LVIS_FOCUSED;
  CHECK(::SendMessageW(lv, LVM_SETITEMSTATE, 1, (LPARAM)&st));
  CHECK(Notified(LVN_ITEMCHANGING) && Notified(LVN_ITEMCHANGED));
  CHECK(::SendMessageW(lv, LVM_GETSELECTEDCOUNT, 0, 0) == 1);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == 1);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED) == 1);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, 1, LVNI_BELOW) == 2);

  // Geometry and hit testing.
  RECT ir = {LVIR_BOUNDS};
  CHECK(::SendMessageW(lv, LVM_GETITEMRECT, 2, (LPARAM)&ir) && ir.right - ir.left == 230 && ir.bottom > ir.top);
  LVHITTESTINFO ht = {};
  ht.pt.x = ir.left + 20;
  ht.pt.y = (ir.top + ir.bottom) / 2;
  CHECK(::SendMessageW(lv, LVM_HITTEST, 0, (LPARAM)&ht) == 2 && (ht.flags & LVHT_ONITEM));
  ht.pt.x = ir.left + 190;
  CHECK(::SendMessageW(lv, LVM_SUBITEMHITTEST, 0, (LPARAM)&ht) == 2 && ht.iSubItem == 1);

  // The keyboard moves the selection; a click selects and notifies.
  ::SetFocus(lv);
  ::SendMessageW(lv, WM_KEYDOWN, VK_DOWN, 0);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == 2);
  CHECK(::SendMessageW(lv, LVM_GETSELECTEDCOUNT, 0, 0) == 1);
  g_notifies.clear();
  RECT r0 = {LVIR_LABEL};
  ::SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&r0);
  LPARAM at = MAKELPARAM(r0.left + 5, (r0.top + r0.bottom) / 2);
  ::SendMessageW(lv, WM_LBUTTONDOWN, MK_LBUTTON, at);
  ::SendMessageW(lv, WM_LBUTTONUP, 0, at);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == 0 && Notified(NM_CLICK));
  ::SendMessageW(lv, WM_KEYDOWN, VK_END, 0);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED) == 3);
  g_notifies.clear();
  ::SendMessageW(lv, WM_CHAR, 'b', 1);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED) == 1);

  // Label editing ends with the parent's say.
  HWND edit = (HWND)::SendMessageW(lv, LVM_EDITLABELW, 1, 0);
  CHECK(edit != NULL && Notified(LVN_BEGINLABELEDITW));
  if (edit) {
    ::SetWindowTextW(edit, L"blueberry");
    ::SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(Notified(LVN_ENDLABELEDITW) && ::SendMessageW(lv, LVM_GETEDITCONTROL, 0, 0) == 0);
  }

  // Sorting by lParam, descending.
  CHECK(::SendMessageW(lv, LVM_SORTITEMS, 0, (LPARAM)CompareDown));
  get.mask = LVIF_PARAM;
  get.iItem = 0;
  CHECK(::SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&get) && get.lParam == 30);
  CHECK(::SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_FOCUSED) == 2);

  // Check boxes are state images 1 and 2.
  ::SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT,
                 LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  CHECK(::SendMessageW(lv, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0) == (LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT));
  CHECK((::SendMessageW(lv, LVM_GETITEMSTATE, 0, LVIS_STATEIMAGEMASK) >> 12) == 1);
  st.stateMask = LVIS_STATEIMAGEMASK;
  st.state = INDEXTOSTATEIMAGEMASK(2);
  ::SendMessageW(lv, LVM_SETITEMSTATE, 0, (LPARAM)&st);
  CHECK((::SendMessageW(lv, LVM_GETITEMSTATE, 0, LVIS_STATEIMAGEMASK) >> 12) == 2);

  // Deleting.
  g_notifies.clear();
  CHECK(::SendMessageW(lv, LVM_DELETEITEM, 3, 0) && Notified(LVN_DELETEITEM));
  CHECK(::SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0) == 3);

  // The other views lay the items out on their own.
  ::SetWindowLongW(lv, GWL_STYLE, (::GetWindowLongW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | LVS_ICON);
  RECT a = {LVIR_BOUNDS}, b = {LVIR_BOUNDS};
  ::SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&a);
  ::SendMessageW(lv, LVM_GETITEMRECT, 1, (LPARAM)&b);
  CHECK(b.left > a.left && b.top == a.top && !(::GetWindowLongW(header, GWL_STYLE) & WS_VISIBLE));
  ::SetWindowLongW(lv, GWL_STYLE, (::GetWindowLongW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | LVS_LIST);
  ::SendMessageW(lv, LVM_GETITEMRECT, 0, (LPARAM)&a);
  // A row of text each (Wine keeps the second item's icon place).
  CHECK(a.left == 0 && a.top == 0 && a.bottom - a.top < 30);
  ::SetWindowLongW(lv, GWL_STYLE, (::GetWindowLongW(lv, GWL_STYLE) & ~LVS_TYPEMASK) | LVS_REPORT);
  CHECK(::GetWindowLongW(header, GWL_STYLE) & WS_VISIBLE);

  // Owner data: the list view keeps only the count and the states.
  HWND od = ::CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_OWNERDATA, 0, 0, 100, 100, host,
                            (HMENU)21, inst, NULL);
  ::SendMessageW(od, LVM_SETITEMCOUNT, 1000, 0);
  CHECK(::SendMessageW(od, LVM_GETITEMCOUNT, 0, 0) == 1000);
  st.stateMask = LVIS_SELECTED;
  st.state = LVIS_SELECTED;
  ::SendMessageW(od, LVM_SETITEMSTATE, 500, (LPARAM)&st);
  CHECK(::SendMessageW(od, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED) == 500);

  g_notifies.clear();
  CHECK(::SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0) && Notified(LVN_DELETEALLITEMS));
  CHECK(::SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0) == 0);
  ::DestroyWindow(host);
}

static HTREEITEM TreeAdd(HWND tv, HTREEITEM parent, const wchar_t* text, LPARAM data,
                         HTREEITEM after = TVI_LAST) {
  TVINSERTSTRUCTW ins = {};
  ins.hParent = parent;
  ins.hInsertAfter = after;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM;
  ins.item.pszText = (LPWSTR)text;
  ins.item.lParam = data;
  return (HTREEITEM)::SendMessageW(tv, TVM_INSERTITEMW, 0, (LPARAM)&ins);
}

static HTREEITEM TreeNext(HWND tv, HTREEITEM h, UINT code) {
  return (HTREEITEM)::SendMessageW(tv, TVM_GETNEXTITEM, code, (LPARAM)h);
}

static std::wstring TreeText(HWND tv, HTREEITEM h) {
  wchar_t buf[64] = L"";
  TVITEMW it = {};
  it.mask = TVIF_TEXT;
  it.hItem = h;
  it.pszText = buf;
  it.cchTextMax = 64;
  ::SendMessageW(tv, TVM_GETITEMW, 0, (LPARAM)&it);
  return buf;
}

static int CALLBACK CompareParamDown(LPARAM a, LPARAM b, LPARAM) { return (int)(b - a); }

static void TestTreeView() {
  HINSTANCE inst = ::GetModuleHandleW(NULL);
  HWND host = ::CreateWindowW(L"CtlHost", L"Tree", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 300, 320, NULL,
                              NULL, inst, NULL);
  HWND tv = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT |
                                  TVS_EDITLABELS | TVS_SHOWSELALWAYS,
                              10, 10, 260, 260, host, (HMENU)30, inst, NULL);
  CHECK(tv != NULL);

  HTREEITEM fruit = TreeAdd(tv, TVI_ROOT, L"Fruit", 1);
  HTREEITEM veg = TreeAdd(tv, TVI_ROOT, L"Vegetables", 2);
  HTREEITEM pear = TreeAdd(tv, fruit, L"Pear", 13);
  HTREEITEM apple = TreeAdd(tv, fruit, L"Apple", 11, TVI_FIRST);
  HTREEITEM fig = TreeAdd(tv, fruit, L"Fig", 12, TVI_SORT);
  HTREEITEM leek = TreeAdd(tv, veg, L"Leek", 21);
  CHECK(fruit && veg && pear && apple && fig && leek);
  CHECK(::SendMessageW(tv, TVM_GETCOUNT, 0, 0) == 6);
  CHECK(TreeNext(tv, NULL, TVGN_ROOT) == fruit && TreeNext(tv, fruit, TVGN_NEXT) == veg);
  CHECK(TreeNext(tv, fruit, TVGN_CHILD) == apple && TreeNext(tv, apple, TVGN_NEXT) == fig &&
        TreeNext(tv, fig, TVGN_NEXT) == pear);
  CHECK(TreeNext(tv, pear, TVGN_PARENT) == fruit && TreeNext(tv, fruit, TVGN_PARENT) == NULL);
  CHECK(TreeText(tv, fig) == L"Fig");

  // Closed parents hide their children.
  CHECK(TreeNext(tv, fruit, TVGN_NEXTVISIBLE) == veg);
  RECT r;
  *(HTREEITEM*)&r = apple;
  CHECK(!::SendMessageW(tv, TVM_GETITEMRECT, TRUE, (LPARAM)&r));
  g_notifies.clear();
  CHECK(::SendMessageW(tv, TVM_EXPAND, TVE_EXPAND, (LPARAM)fruit));
  CHECK(Notified(TVN_ITEMEXPANDINGW) && Notified(TVN_ITEMEXPANDEDW));
  CHECK(::SendMessageW(tv, TVM_GETITEMSTATE, (WPARAM)fruit, TVIS_EXPANDED) == TVIS_EXPANDED);
  CHECK(TreeNext(tv, fruit, TVGN_NEXTVISIBLE) == apple);
  *(HTREEITEM*)&r = apple;
  CHECK(::SendMessageW(tv, TVM_GETITEMRECT, TRUE, (LPARAM)&r) && r.right > r.left);
  // Only the first TVM_EXPAND tells the parent.
  ::SendMessageW(tv, TVM_EXPAND, TVE_COLLAPSE, (LPARAM)fruit);
  g_notifies.clear();
  ::SendMessageW(tv, TVM_EXPAND, TVE_EXPAND, (LPARAM)fruit);
  CHECK(!Notified(TVN_ITEMEXPANDINGW));

  // Selection.
  g_notifies.clear();
  CHECK(::SendMessageW(tv, TVM_SELECTITEM, TVGN_CARET, (LPARAM)fig));
  CHECK(Notified(TVN_SELCHANGINGW) && Notified(TVN_SELCHANGEDW));
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == fig);
  CHECK(::SendMessageW(tv, TVM_GETITEMSTATE, (WPARAM)fig, TVIS_SELECTED) == TVIS_SELECTED);
  // Selecting a hidden item opens its parent.
  ::SendMessageW(tv, TVM_SELECTITEM, TVGN_CARET, (LPARAM)leek);
  CHECK(::SendMessageW(tv, TVM_GETITEMSTATE, (WPARAM)veg, TVIS_EXPANDED) == TVIS_EXPANDED);

  // The keyboard.
  ::SetFocus(tv);
  ::SendMessageW(tv, WM_KEYDOWN, VK_UP, 0);
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == veg);
  ::SendMessageW(tv, WM_KEYDOWN, VK_LEFT, 0);
  CHECK(::SendMessageW(tv, TVM_GETITEMSTATE, (WPARAM)veg, TVIS_EXPANDED) == 0);
  ::SendMessageW(tv, WM_KEYDOWN, VK_HOME, 0);
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == fruit);
  ::SendMessageW(tv, WM_KEYDOWN, VK_RIGHT, 0);
  ::SendMessageW(tv, WM_KEYDOWN, VK_RIGHT, 0);
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == apple);
  ::SendMessageW(tv, WM_CHAR, 'p', 1);
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == pear);

  // Hit testing and a click on the button.
  *(HTREEITEM*)&r = veg;
  ::SendMessageW(tv, TVM_GETITEMRECT, TRUE, (LPARAM)&r);
  TVHITTESTINFO ht = {};
  ht.pt.x = r.left + 3;
  ht.pt.y = (r.top + r.bottom) / 2;
  CHECK((HTREEITEM)::SendMessageW(tv, TVM_HITTEST, 0, (LPARAM)&ht) == veg && (ht.flags & TVHT_ONITEMLABEL));
  ht.pt.x = r.left - 10;
  CHECK((HTREEITEM)::SendMessageW(tv, TVM_HITTEST, 0, (LPARAM)&ht) == veg && (ht.flags & TVHT_ONITEMBUTTON));
  ::SendMessageW(tv, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ht.pt.x, ht.pt.y));
  ::SendMessageW(tv, WM_LBUTTONUP, 0, MAKELPARAM(ht.pt.x, ht.pt.y));
  CHECK(::SendMessageW(tv, TVM_GETITEMSTATE, (WPARAM)veg, TVIS_EXPANDED) == TVIS_EXPANDED);
  g_notifies.clear();
  // The release is waiting in the queue: Windows watches for a drag
  // until it comes.
  ::PostMessageW(tv, WM_LBUTTONUP, 0, MAKELPARAM(r.left + 3, ht.pt.y));
  ::SendMessageW(tv, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(r.left + 3, ht.pt.y));
  Pump();
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == veg && Notified(NM_CLICK));

  // Editing a label.
  g_notifies.clear();
  HWND edit = (HWND)::SendMessageW(tv, TVM_EDITLABELW, 0, (LPARAM)leek);
  CHECK(edit && Notified(TVN_BEGINLABELEDITW));
  if (edit) {
    ::SetWindowTextW(edit, L"Onion");
    CHECK(::SendMessageW(tv, TVM_ENDEDITLABELNOW, FALSE, 0));
    CHECK(Notified(TVN_ENDLABELEDITW) && ::SendMessageW(tv, TVM_GETEDITCONTROL, 0, 0) == 0);
  }

  // Sorting.
  TVSORTCB sort = {fruit, CompareParamDown, 0};
  CHECK(::SendMessageW(tv, TVM_SORTCHILDRENCB, 0, (LPARAM)&sort));
  CHECK(TreeNext(tv, fruit, TVGN_CHILD) == pear && TreeNext(tv, pear, TVGN_NEXT) == fig);
  CHECK(::SendMessageW(tv, TVM_SORTCHILDREN, 0, (LPARAM)fruit));
  CHECK(TreeNext(tv, fruit, TVGN_CHILD) == apple);

  // Deleting a branch with the caret in it moves the caret.
  ::SendMessageW(tv, TVM_SELECTITEM, TVGN_CARET, (LPARAM)fig);
  g_notifies.clear();
  CHECK(::SendMessageW(tv, TVM_DELETEITEM, 0, (LPARAM)fruit));
  CHECK(Notified(TVN_DELETEITEMW) && ::SendMessageW(tv, TVM_GETCOUNT, 0, 0) == 2);
  CHECK(TreeNext(tv, NULL, TVGN_CARET) == veg);
  CHECK(::SendMessageW(tv, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT) && ::SendMessageW(tv, TVM_GETCOUNT, 0, 0) == 0);
  ::DestroyWindow(host);
}

// A wizard page that records what the sheet tells it and can refuse to
// be left.
static std::vector<UINT> g_pageCodes;
static bool g_pageRefuses = false;
static INT_PTR CALLBACK WizPageProc(HWND h, UINT msg, WPARAM, LPARAM lp) {
  if (msg == WM_NOTIFY) {
    NMHDR* nm = (NMHDR*)lp;
    g_pageCodes.push_back(nm->code);
    LRESULT r = 0;
    if (nm->code == PSN_SETACTIVE) ::PostMessageW(::GetParent(h), PSM_SETWIZBUTTONS, 0, PSWIZB_BACK | PSWIZB_NEXT);
    if (nm->code == PSN_KILLACTIVE && g_pageRefuses) r = TRUE;
    ::SetWindowLongPtrW(h, DWLP_MSGRESULT, r);
    return TRUE;
  }
  return FALSE;
}

static bool PageCode(UINT code) {
  for (UINT c : g_pageCodes)
    if (c == code) return true;
  return false;
}

static void TestPropertySheets() {
  DlgBuilder one(WS_CHILD | WS_CAPTION, 180, 80, "One");
  one.Item(0x0081, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 7, 7, 100, 12, 100, "");
  DlgBuilder two(WS_CHILD | WS_CAPTION, 200, 90, "Two");
  two.Item(0x0080, BS_AUTOCHECKBOX | WS_TABSTOP, 7, 7, 80, 10, 101, "Check");
  PROPSHEETPAGEW pages[2] = {};
  for (int i = 0; i < 2; i++) {
    pages[i].dwSize = sizeof(PROPSHEETPAGEW);
    pages[i].dwFlags = PSP_DLGINDIRECT;
    pages[i].pResource = (LPCDLGTEMPLATE)(i ? two.Get() : one.Get());
    pages[i].pfnDlgProc = WizPageProc;
  }
  HPROPSHEETPAGE extra = ::CreatePropertySheetPageW(&pages[1]);
  CHECK(extra != NULL);

  // A modeless property sheet: tabs, pages and the Apply button.
  PROPSHEETHEADERW psh = {};
  psh.dwSize = sizeof psh;
  psh.dwFlags = PSH_PROPSHEETPAGE | PSH_MODELESS;
  psh.hInstance = ::GetModuleHandleW(NULL);
  psh.pszCaption = L"Sheet";
  psh.nPages = 2;
  psh.ppsp = pages;
  g_pageCodes.clear();
  HWND sheet = (HWND)::PropertySheetW(&psh);
  CHECK(sheet != NULL && ::IsWindow(sheet));
  wchar_t title[64] = L"";
  ::GetWindowTextW(sheet, title, 64);
  CHECK(wcscmp(title, L"Sheet") == 0);
  HWND tab = (HWND)::SendMessageW(sheet, PSM_GETTABCONTROL, 0, 0);
  CHECK(tab && ::SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 2);
  HWND page0 = (HWND)::SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0);
  CHECK(page0 && ::GetParent(page0) == sheet && ::GetDlgItem(page0, 100) && PageCode(PSN_SETACTIVE));
  CHECK(::GetWindowLongW(page0, GWL_STYLE) & WS_CHILD);
  HWND apply = ::GetDlgItem(sheet, 0x3021);
  CHECK(apply && !::IsWindowEnabled(apply));
  ::SendMessageW(sheet, PSM_CHANGED, (WPARAM)page0, 0);
  CHECK(::IsWindowEnabled(apply));
  // A page can keep the focus.
  g_pageRefuses = true;
  CHECK(!::SendMessageW(sheet, PSM_SETCURSEL, 1, 0));
  g_pageRefuses = false;
  g_pageCodes.clear();
  CHECK(::SendMessageW(sheet, PSM_SETCURSEL, 1, 0));
  CHECK(PageCode(PSN_KILLACTIVE) && PageCode(PSN_SETACTIVE));
  CHECK(::SendMessageW(tab, TCM_GETCURSEL, 0, 0) == 1);
  CHECK(::SendMessageW(sheet, PSM_ADDPAGE, 0, (LPARAM)extra) && ::SendMessageW(tab, TCM_GETITEMCOUNT, 0, 0) == 3);
  CHECK(::SendMessageW(sheet, PSM_INDEXTOPAGE, 2, 0) == (LRESULT)extra);
  g_pageCodes.clear();
  ::SendMessageW(sheet, PSM_PRESSBUTTON, PSBTN_APPLYNOW, 0);
  CHECK(PageCode(PSN_APPLY) && !::IsWindowEnabled(apply));
  g_pageCodes.clear();
  ::SendMessageW(sheet, PSM_PRESSBUTTON, PSBTN_OK, 0);
  CHECK(PageCode(PSN_APPLY) && ::SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0) == 0);
  ::DestroyWindow(sheet);

  // A wizard: Next and Back move through the pages.
  psh.dwFlags = PSH_PROPSHEETPAGE | PSH_MODELESS | PSH_WIZARD;
  g_pageCodes.clear();
  sheet = (HWND)::PropertySheetW(&psh);
  CHECK(sheet != NULL);
  Pump();
  HWND next = ::GetDlgItem(sheet, 0x3024), back = ::GetDlgItem(sheet, 0x3023), fin = ::GetDlgItem(sheet, 0x3025);
  CHECK(next && back && fin && ::IsWindowEnabled(next) && !(::GetWindowLongW(fin, GWL_STYLE) & WS_VISIBLE));
  ::SendMessageW(sheet, PSM_PRESSBUTTON, PSBTN_NEXT, 0);
  CHECK(PageCode(PSN_WIZNEXT));
  HWND page1 = (HWND)::SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0);
  CHECK(page1 && ::GetDlgItem(page1, 101) && ::SendMessageW(sheet, PSM_HWNDTOINDEX, (WPARAM)page1, 0) == 1);
  ::SendMessageW(sheet, PSM_SETWIZBUTTONS, 0, PSWIZB_BACK | PSWIZB_FINISH);
  CHECK((::GetWindowLongW(fin, GWL_STYLE) & WS_VISIBLE) && !(::GetWindowLongW(next, GWL_STYLE) & WS_VISIBLE));
  ::SendMessageW(sheet, PSM_PRESSBUTTON, PSBTN_BACK, 0);
  HWND cur = (HWND)::SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0);
  CHECK(PageCode(PSN_WIZBACK) && ::SendMessageW(sheet, PSM_HWNDTOINDEX, (WPARAM)cur, 0) == 0);
  ::SendMessageW(sheet, PSM_SETWIZBUTTONS, 0, PSWIZB_FINISH);
  g_pageCodes.clear();
  ::SendMessageW(sheet, PSM_PRESSBUTTON, PSBTN_FINISH, 0);
  CHECK(PageCode(PSN_WIZFINISH) && ::SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0) == 0);
  ::DestroyWindow(sheet);
}

// ---------------------------------------------------------------------
// Window shapes, and files dropped on a window.

static int g_drops = 0;
static std::vector<std::wstring> g_dropped;
static POINT g_dropPt;
static BOOL g_dropInClient;
static UINT g_dropShort;
static WCHAR g_dropShortText[8];

static LRESULT CALLBACK DropProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg != WM_DROPFILES) return ::DefWindowProcW(h, msg, wp, lp);
  HDROP drop = (HDROP)wp;
  g_dropped.clear();
  UINT n = ::DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
  for (UINT i = 0; i < n; i++) {
    UINT len = ::DragQueryFileW(drop, i, NULL, 0);
    std::vector<WCHAR> buf(len + 1);
    ::DragQueryFileW(drop, i, buf.data(), len + 1);
    g_dropped.push_back(std::wstring(buf.data(), len));
  }
  // A buffer too short takes what fits.
  g_dropShort = ::DragQueryFileW(drop, 0, g_dropShortText, 4);
  g_dropInClient = ::DragQueryPoint(drop, &g_dropPt);
  ::DragFinish(drop);
  g_drops++;
  return 0;
}

// An HDROP as the shell makes one: a DROPFILES and the paths.
static HGLOBAL MakeDrop(const void* paths, size_t bytes, BOOL wide, POINT pt, BOOL nc) {
  HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
  DROPFILES* df = (DROPFILES*)::GlobalLock(h);
  df->pFiles = sizeof(DROPFILES);
  df->pt = pt;
  df->fNC = nc;
  df->fWide = wide;
  memcpy(df + 1, paths, bytes);
  ::GlobalUnlock(h);
  return h;
}

static void TestShapesAndDrops() {
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = DropProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
  wc.lpszClassName = L"WinTestDrop";
  CHECK(::RegisterClassW(&wc) != 0);
  HWND top = ::CreateWindowExW(WS_EX_TOPMOST, L"WinTestDrop", L"drop", WS_POPUP | WS_VISIBLE, 100, 100, 300, 200,
                               NULL, NULL, wc.hInstance, NULL);
  HWND child = ::CreateWindowExW(0, L"WinTestDrop", L"shaped", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, top, NULL,
                                 wc.hInstance, NULL);
  CHECK(top != NULL && child != NULL);
  ::UpdateWindow(top);

  // The child keeps only its right half.
  HRGN rgn = ::CreateRectRgn(0, 0, 0, 0);
  CHECK(::GetWindowRgn(child, rgn) == ERROR);
  CHECK(::SetWindowRgn(child, ::CreateRectRgn(50, 0, 100, 100), TRUE));
  CHECK(::GetWindowRgn(child, rgn) == SIMPLEREGION);
  RECT box;
  ::GetRgnBox(rgn, &box);
  CHECK(box.left == 50 && box.top == 0 && box.right == 100 && box.bottom == 100);
  POINT outside = {110, 110}, inside = {160, 110};
  CHECK(::WindowFromPoint(outside) == top);
  CHECK(::WindowFromPoint(inside) == child);
  HDC dc = ::GetDC(child);
  ::GetClipBox(dc, &box);
  CHECK(box.left == 50 && box.right == 100);
  ::ReleaseDC(child, dc);
  CHECK(::SetWindowRgn(child, NULL, TRUE));
  CHECK(::GetWindowRgn(child, rgn) == ERROR);
  CHECK(::WindowFromPoint(outside) == child);
  ::DeleteObject(rgn);

  // Files dropped on the window.
  ::DragAcceptFiles(top, TRUE);
  CHECK((::GetWindowLongW(top, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0);
  static const WCHAR wide[] = L"C:\\one.txt\0C:\\two words.txt\0";
  POINT pt = {12, 34};
  ::SendMessageW(top, WM_DROPFILES, (WPARAM)MakeDrop(wide, sizeof(wide), TRUE, pt, FALSE), 0);
  CHECK(g_drops == 1);
  CHECK(g_dropped.size() == 2);
  if (g_dropped.size() == 2) {
    CHECK(g_dropped[0] == L"C:\\one.txt");
    CHECK(g_dropped[1] == L"C:\\two words.txt");
  }
  CHECK(g_dropShort == 3 && lstrcmpW(g_dropShortText, L"C:\\") == 0);
  CHECK(g_dropInClient && g_dropPt.x == 12 && g_dropPt.y == 34);
  // The narrow form, read wide; a drop on the frame.
  static const char narrow[] = "/tmp/three\0";
  ::SendMessageW(top, WM_DROPFILES, (WPARAM)MakeDrop(narrow, sizeof(narrow), FALSE, pt, TRUE), 0);
  CHECK(g_drops == 2 && g_dropped.size() == 1 && g_dropped[0] == L"/tmp/three");
  CHECK(!g_dropInClient);
  char a[32] = "";
  HGLOBAL h = MakeDrop(wide, sizeof(wide), TRUE, pt, FALSE);
  CHECK(::DragQueryFileA((HDROP)h, 1, a, sizeof(a)) == 16 && strcmp(a, "C:\\two words.txt") == 0);
  CHECK(::DragQueryFileA((HDROP)h, 2, a, sizeof(a)) == 0);
  ::DragFinish((HDROP)h);
  ::DragAcceptFiles(top, FALSE);
  CHECK((::GetWindowLongW(top, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) == 0);
  ::DestroyWindow(top);
}

// Clicks through a group box: it answers HTTRANSPARENT, so a click on a
// button inside it (beneath it in z-order) reaches the button.
static int g_groupClicks = 0;
static LRESULT CALLBACK GroupParentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_COMMAND && LOWORD(wp) == 101 && HIWORD(wp) == BN_CLICKED) g_groupClicks++;
  return ::DefWindowProcW(h, msg, wp, lp);
}

static void PumpFor(DWORD ms) {
  DWORD t0 = ::GetTickCount();
  MSG m;
  while (::GetTickCount() - t0 < ms) {
    while (::PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&m);
      ::DispatchMessageW(&m);
    }
    ::Sleep(5);
  }
}

static void TestClickThroughGroupBox() {
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = GroupParentProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
  wc.lpszClassName = L"WinTestGroup";
  CHECK(::RegisterClassW(&wc) != 0);
  HWND top = ::CreateWindowExW(WS_EX_TOPMOST, L"WinTestGroup", L"group", WS_POPUP | WS_VISIBLE, 120, 120, 300, 200,
                               NULL, NULL, wc.hInstance, NULL);
  HWND group = ::CreateWindowExW(0, L"BUTTON", L"Group", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 10, 280, 180, top,
                                 (HMENU)100, wc.hInstance, NULL);
  HWND button = ::CreateWindowExW(0, L"BUTTON", L"Push", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 40, 60, 100, 30, top,
                                  (HMENU)101, wc.hInstance, NULL);
  CHECK(top && group && button);
  ::SetForegroundWindow(top);
  ::UpdateWindow(top);
  // The group box, made first, is above the button.
  CHECK(::GetWindow(top, GW_CHILD) == group);
  RECT r;
  ::GetWindowRect(button, &r);
  ::SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
  ::mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
  PumpFor(30);
  ::mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
  PumpFor(30);
  CHECK(g_groupClicks == 1);
  ::DestroyWindow(top);
}

// Focus after a modal dialog: the control that had it in the owner (a
// dialog itself) gets it back when the modal dialog ends.
static HWND g_ownerEdit2 = NULL;
static INT_PTR CALLBACK OwnerDlgProc(HWND h, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_INITDIALOG) {
    ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 10, 100, 20, h, (HMENU)1, NULL, NULL);
    g_ownerEdit2 =
        ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 40, 100, 20, h, (HMENU)2, NULL, NULL);
    return TRUE;
  }
  return FALSE;
}
static INT_PTR CALLBACK ShortModalProc(HWND h, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_INITDIALOG) {
    ::CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 10, 100, 20, h, (HMENU)1, NULL, NULL);
    ::SetTimer(h, 1, 50, NULL);
    return TRUE;
  }
  if (msg == WM_TIMER) {
    ::KillTimer(h, 1);
    ::EndDialog(h, 7);
    return TRUE;
  }
  return FALSE;
}

// Draws text into a fresh bitmap and returns its pixels.
static std::vector<DWORD> InkOf(HFONT font, const char* a, const wchar_t* w) {
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = 64;
  bi.bmiHeader.biHeight = -48;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = NULL;
  HDC dc = ::CreateCompatibleDC(NULL);
  HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HGDIOBJ oldBmp = ::SelectObject(dc, bmp);
  HGDIOBJ oldFont = ::SelectObject(dc, font);
  RECT rc = {0, 0, 64, 48};
  ::FillRect(dc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  ::SetBkMode(dc, TRANSPARENT);
  if (a)
    ::TextOutA(dc, 4, 4, a, (int)strlen(a));
  else
    ::TextOutW(dc, 4, 4, w, (int)wcslen(w));
  ::GdiFlush();
  std::vector<DWORD> px((const DWORD*)bits, (const DWORD*)bits + 64 * 48);
  ::SelectObject(dc, oldFont);
  ::SelectObject(dc, oldBmp);
  ::DeleteObject(bmp);
  ::DeleteDC(dc);
  return px;
}

static bool Inked(const std::vector<DWORD>& px) {
  for (DWORD p : px)
    if ((p & 0xFFFFFF) != 0xFFFFFF) return true;
  return false;
}

// Wingdings is a symbol font: its A text is read byte by byte, and its
// characters draw whether or not Wingdings itself is installed.
static void TestSymbolFont() {
  HFONT wd = ::CreateFontW(-32, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Wingdings");
  HFONT arial = ::CreateFontW(-32, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, L"Arial");
  HDC dc = ::CreateCompatibleDC(NULL);
  HGDIOBJ old = ::SelectObject(dc, wd);
#ifndef _WINPORT
  // Windows draws the font only when it is installed (Wine has none).
  WCHAR face[LF_FACESIZE] = {};
  ::GetTextFaceW(dc, LF_FACESIZE, face);
  if (lstrcmpiW(face, L"Wingdings") != 0) {
    printf("no Wingdings font: symbol fonts not tested\n");
    ::SelectObject(dc, old);
    ::DeleteDC(dc);
    ::DeleteObject(wd);
    ::DeleteObject(arial);
    return;
  }
#endif
  TEXTMETRICW tm = {};
  CHECK(::GetTextMetricsW(dc, &tm));
  CHECK(tm.tmCharSet == SYMBOL_CHARSET);
  // One byte, one character.
  SIZE one = {}, two = {};
  CHECK(::GetTextExtentPoint32A(dc, "\xFC", 1, &one));
  CHECK(::GetTextExtentPoint32A(dc, "\xFC\xFC", 2, &two));
  CHECK(one.cx > 0 && two.cx == 2 * one.cx);
  ::SelectObject(dc, old);
  ::DeleteDC(dc);
  // 0xFC is the check mark: the same from the A form and from U+F0FC,
  // and not the u with diaeresis that Arial has there.
  std::vector<DWORD> byA = InkOf(wd, "\xFC", NULL);
  std::vector<DWORD> byW = InkOf(wd, NULL, L"\xF0FC");
  std::vector<DWORD> latin = InkOf(arial, NULL, L"\x00FC");
  CHECK(Inked(byA));
  CHECK(byA == byW);
  CHECK(byA != latin);
  ::DeleteObject(wd);
  ::DeleteObject(arial);
}

#ifdef _WINPORT
// Pixels of a color in the left of a window's caption, where the system
// menu box is.
static int CaptionPixels(HWND h, COLORREF c) {
  HDC dc = ::GetWindowDC(h);
  int n = 0;
  int bottom = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CYCAPTION);
  for (int y = 0; y < bottom; y++)
    for (int x = 0; x < 40; x++)
      if (::GetPixel(dc, x, y) == c) n++;
  ::ReleaseDC(h, dc);
  return n;
}

// The caption's system menu box shows the window's small icon, or its
// class's, and draws again when the icon changes.
static void TestCaptionIcon() {
  std::vector<DWORD> red(16 * 16, 0x00FF0000), blue(16 * 16, 0x000000FF);
  BYTE zeros[16 * 16 / 8] = {0};
  ICONINFO ii = {TRUE, 0, 0, ::CreateBitmap(16, 16, 1, 1, zeros), ::CreateBitmap(16, 16, 1, 32, red.data())};
  HICON redIcon = ::CreateIconIndirect(&ii);
  ::DeleteObject(ii.hbmColor);
  ii.hbmColor = ::CreateBitmap(16, 16, 1, 32, blue.data());
  HICON blueIcon = ::CreateIconIndirect(&ii);
  ::DeleteObject(ii.hbmColor);
  ::DeleteObject(ii.hbmMask);
  CHECK(redIcon && blueIcon);

  WNDCLASSEXW wc = {sizeof wc};
  wc.lpfnWndProc = ::DefWindowProcW;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"CaptionIconTest";
  wc.hIconSm = blueIcon;
  CHECK(::RegisterClassExW(&wc));
  HWND h = ::CreateWindowExW(0, L"CaptionIconTest", L"Icon", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 200, 120, NULL,
                             NULL, wc.hInstance, NULL);
  CHECK(h != NULL);
  ::UpdateWindow(h);
  int box = ::GetSystemMetrics(SM_CXSMICON) * ::GetSystemMetrics(SM_CYSMICON);
  CHECK(CaptionPixels(h, RGB(0, 0, 255)) >= box);  // the class's
  CHECK(CaptionPixels(h, RGB(255, 0, 0)) == 0);
  ::SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)redIcon);
  CHECK(CaptionPixels(h, RGB(255, 0, 0)) >= box);  // the window's own
  CHECK(CaptionPixels(h, RGB(0, 0, 255)) == 0);
  ::SendMessageW(h, WM_SETICON, ICON_SMALL, 0);
  CHECK(CaptionPixels(h, RGB(0, 0, 255)) >= box);
  ::DestroyWindow(h);
  ::UnregisterClassW(L"CaptionIconTest", wc.hInstance);
  ::DestroyIcon(redIcon);
  ::DestroyIcon(blueIcon);
}
#endif

static void TestModalFocus() {
  // An empty template: the controls are made in WM_INITDIALOG.
  static WORD tmpl[32];
  memset(tmpl, 0, sizeof tmpl);
  DLGTEMPLATE* t = (DLGTEMPLATE*)tmpl;
  t->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
  t->cx = 150;
  t->cy = 80;
  HWND owner = ::CreateDialogIndirectParamW(NULL, t, NULL, OwnerDlgProc, 0);
  CHECK(owner != NULL);
  ::ShowWindow(owner, SW_SHOW);
  ::SetForegroundWindow(owner);
  ::SetFocus(g_ownerEdit2);
  CHECK(::GetFocus() == g_ownerEdit2);
  CHECK(::DialogBoxIndirectParamW(NULL, t, owner, ShortModalProc, 0) == 7);
  CHECK(::GetActiveWindow() == owner);
  CHECK(::GetFocus() == g_ownerEdit2);
  ::DestroyWindow(owner);
}

// What TallBoxTimer saw of the message box.
static bool g_tallBoxSeen, g_tallBoxFits, g_tallBoxScrolls;

// Checks that the tall message box and its OK button are inside the work
// area and that its text scrolls, then closes it.
static void CALLBACK TallBoxTimer(HWND, UINT, UINT_PTR id, DWORD) {
  HWND box = ::FindWindowW(NULL, L"Tall Box");
  if (!box) return;
  ::KillTimer(NULL, id);
  g_tallBoxSeen = true;
  RECT work, wr, br;
  ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  ::GetWindowRect(box, &wr);
  HWND ok = ::GetDlgItem(box, IDOK);
  if (ok) ::GetWindowRect(ok, &br);
  g_tallBoxFits = ok && wr.top >= work.top && wr.bottom <= work.bottom && br.bottom <= work.bottom;
  HWND text = ::GetDlgItem(box, 0xFFFF);
  g_tallBoxScrolls = text && (::GetWindowLongW(text, GWL_STYLE) & WS_VSCROLL) != 0;
  ::PostMessageW(box, WM_COMMAND, IDOK, 0);
}

static void TestTallMessageBox() {
  // A box with more lines than the screen can hold keeps its buttons on
  // the screen; its text scrolls.
  std::wstring text;
  for (int i = 0; i < 300; i++) text += L"A line of the message.\n";
  const char* was = getenv("WINPORT_MESSAGEBOX");
  std::string keep = was ? was : "";
  setenv("WINPORT_MESSAGEBOX", "window", 1);
  UINT_PTR t = ::SetTimer(NULL, 0, 50, TallBoxTimer);
  CHECK(t != 0);
  CHECK(::MessageBoxW(NULL, text.c_str(), L"Tall Box", MB_OK) == IDOK);
  ::KillTimer(NULL, t);
  if (was)
    setenv("WINPORT_MESSAGEBOX", keep.c_str(), 1);
  else
    unsetenv("WINPORT_MESSAGEBOX");
  CHECK(g_tallBoxSeen);
  CHECK(g_tallBoxFits);
  CHECK(g_tallBoxScrolls);
}

static void TestCWnd() {
  CTestWnd wnd;
  CString cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, NULL, (HBRUSH)(COLOR_WINDOW + 1));
  CHECK(wnd.CreateEx(0, cls, _T("MFC window"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 320, 240), NULL, 0));
  CHECK(wnd.m_nCreate == 1);
  CHECK(CWnd::FromHandlePermanent(wnd.m_hWnd) == &wnd);
  CString title;
  wnd.GetWindowText(title);
  CHECK(title == _T("MFC window"));
  CRect rc;
  wnd.GetClientRect(&rc);
  wnd.SetWindowPos(NULL, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER);
  CHECK(wnd.m_nSize >= 1);
  wnd.GetClientRect(&rc);
  CHECK(wnd.m_size == rc.Size());
  CHECK(wnd.SendMessage(WM_APP + 5, 21) == 42 && wnd.m_nApp == 1);
  wnd.SetTimer(7, 20, NULL);
  DWORD start = ::GetTickCount();
  while (wnd.m_nTimer < 1 && ::GetTickCount() - start < 3000) AfxPumpMessage();
  CHECK(wnd.m_nTimer >= 1);
  wnd.KillTimer(7);
  wnd.DestroyWindow();
  CHECK(wnd.m_bPostNcDestroy && wnd.m_hWnd == NULL);
}

// ---------------------------------------------------------------------
// Printing: the default printer, printer DCs and their settings, a job
// written to a file, and the Print and Page Setup dialogs driven by
// their hooks.

static int g_abortCalls = 0;
static BOOL CALLBACK CountingAbortProc(HDC, int) {
  g_abortCalls++;
  return TRUE;
}

static UINT_PTR CALLBACK PrintPagesHook(HWND h, UINT msg, WPARAM, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    CHECK(((PRINTDLGW*)lp)->lCustData == 77);
    CHECK(::GetDlgItem(h, edt1) != NULL && ::GetDlgItem(h, edt2) != NULL && ::GetDlgItem(h, edt3) != NULL);
    ::CheckRadioButton(h, rad1, rad3, rad3);
    ::SetDlgItemInt(h, edt1, 2, FALSE);
    ::SetDlgItemInt(h, edt2, 3, FALSE);
    ::SetDlgItemInt(h, edt3, 2, FALSE);
    ::PostMessageW(h, WM_COMMAND, IDOK, 0);
    return TRUE;
  }
  return 0;
}

static UINT_PTR CALLBACK PrintCancelHook(HWND h, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_INITDIALOG) {
    ::PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
    return TRUE;
  }
  return 0;
}

static UINT_PTR CALLBACK PageSetupHook(HWND h, UINT msg, WPARAM, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    CHECK(((PAGESETUPDLGW*)lp)->lCustData == 78);
    ::CheckRadioButton(h, rad1, rad2, rad2);
    ::SendMessageW(h, WM_COMMAND, MAKEWPARAM(rad2, BN_CLICKED), (LPARAM)::GetDlgItem(h, rad2));
    ::PostMessageW(h, WM_COMMAND, IDOK, 0);
    return TRUE;
  }
  return 0;
}

static std::wstring DevNamesDevice(HGLOBAL h) {
  DEVNAMES* dn = (DEVNAMES*)::GlobalLock(h);
  std::wstring s = dn ? (const wchar_t*)dn + dn->wDeviceOffset : L"";
  ::GlobalUnlock(h);
  return s;
}

static void TestPrinting() {
  // The default printer, with a DC.
  PRINTDLGW pd;
  memset(&pd, 0, sizeof pd);
  pd.lStructSize = sizeof pd;
  pd.Flags = PD_RETURNDEFAULT | PD_RETURNDC;
  if (!::PrintDlgW(&pd)) {
    printf("note: no printer (%lu), skipping printing\n", (unsigned long)::CommDlgExtendedError());
    return;
  }
  CHECK(pd.hDevNames != NULL && pd.hDevMode != NULL && pd.hDC != NULL);
  std::wstring device = DevNamesDevice(pd.hDevNames);
  DEVMODEW* dm = (DEVMODEW*)::GlobalLock(pd.hDevMode);
  CHECK(dm != NULL && device == dm->dmDeviceName);
  CHECK(dm != NULL && (dm->dmFields & DM_ORIENTATION) && dm->dmOrientation == DMORIENT_PORTRAIT);
  DEVMODEW portrait = *dm;
  ::GlobalUnlock(pd.hDevMode);
  DEVNAMES* dn = (DEVNAMES*)::GlobalLock(pd.hDevNames);
  CHECK(dn != NULL && (dn->wDefault & DN_DEFAULTPRN));
  ::GlobalUnlock(pd.hDevNames);
  HDC hdc = pd.hDC;
  CHECK(::GetDeviceCaps(hdc, TECHNOLOGY) == DT_RASPRINTER);
  int dpi = ::GetDeviceCaps(hdc, LOGPIXELSX), cx = ::GetDeviceCaps(hdc, HORZRES), cy = ::GetDeviceCaps(hdc, VERTRES);
  int pw = ::GetDeviceCaps(hdc, PHYSICALWIDTH), ph = ::GetDeviceCaps(hdc, PHYSICALHEIGHT);
  int ox = ::GetDeviceCaps(hdc, PHYSICALOFFSETX), oy = ::GetDeviceCaps(hdc, PHYSICALOFFSETY);
  printf("printer '%ls': %d dpi, %dx%d of %dx%d at %d,%d\n", device.c_str(), dpi, cx, cy, pw, ph, ox, oy);
  CHECK(dpi >= 72 && cy > cx && cx > dpi * 4);
  CHECK(pw >= cx + ox && ph >= cy + oy);
  CHECK(::GetDeviceCaps(hdc, HORZSIZE) > 100 && ::GetDeviceCaps(hdc, HORZSIZE) < 300);

  // The same printer turned: a landscape DC, and ResetDC back.
  portrait.dmFields = DM_ORIENTATION;
  DEVMODEW landscape = portrait;
  landscape.dmOrientation = DMORIENT_LANDSCAPE;
  HDC hdcLand = ::CreateDCW(L"WINSPOOL", device.c_str(), NULL, &landscape);
  CHECK(hdcLand != NULL);
  if (hdcLand) {
    CHECK(::GetDeviceCaps(hdcLand, PHYSICALWIDTH) == ph && ::GetDeviceCaps(hdcLand, PHYSICALHEIGHT) == pw);
    CHECK(::GetDeviceCaps(hdcLand, HORZRES) > ::GetDeviceCaps(hdcLand, VERTRES));
    CHECK(::ResetDCW(hdcLand, &portrait) == hdcLand);
    CHECK(::GetDeviceCaps(hdcLand, HORZRES) == cx && ::GetDeviceCaps(hdcLand, VERTRES) == cy);
    ::DeleteDC(hdcLand);
  }

  // A two-page job, to a file, on a DC made from the settings.
  CHECK(::DeleteDC(hdc));
  hdc = ::CreateDCW(L"WINSPOOL", device.c_str(), NULL, &portrait);
  CHECK(hdc != NULL);
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  ::GetTempPathW(MAX_PATH, dir);
  ::GetTempFileNameW(dir, L"prt", 0, path);
  g_abortCalls = 0;
  CHECK(::SetAbortProc(hdc, CountingAbortProc) > 0);
  DOCINFOW di;
  memset(&di, 0, sizeof di);
  di.cbSize = sizeof di;
  di.lpszDocName = L"wintest";
  di.lpszOutput = path;
  int job = ::StartDocW(hdc, &di);
  if (job <= 0) printf("StartDoc: error %lu\n", (unsigned long)::GetLastError());
  CHECK(job > 0);
  for (int page = 0; page < 2; page++) {
    CHECK(::StartPage(hdc) > 0);
    RECT r = {dpi, dpi, dpi * 3, dpi * 2};
    HBRUSH br = ::CreateSolidBrush(RGB(0, 0, 255));
    ::FillRect(hdc, &r, br);
    ::DeleteObject(br);
    ::TextOutW(hdc, dpi, dpi * 3, L"Printed by wintest", 18);
    CHECK(::EndPage(hdc) > 0);
  }
  CHECK(::EndDoc(hdc) > 0);
  CHECK(g_abortCalls >= 1);
  HANDLE f = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  CHECK(f != INVALID_HANDLE_VALUE);
  if (f != INVALID_HANDLE_VALUE) {
    char head[8] = {};
    DWORD got = 0;
    ::ReadFile(f, head, 4, &got, NULL);
    DWORD size = ::GetFileSize(f, NULL);
    ::CloseHandle(f);
    printf("print file: %lu bytes, '%.4s'\n", (unsigned long)size, head);
    CHECK(got == 4 && size > 100);
    CHECK(memcmp(head, "%PDF", 4) == 0 || memcmp(head, "%!PS", 4) == 0);
  }
  ::DeleteFileW(path);

  // A job abandoned part way.
  CHECK(::StartDocW(hdc, &di) > 0);
  CHECK(::StartPage(hdc) > 0);
  CHECK(::AbortDoc(hdc) > 0);
  ::DeleteFileW(path);
  CHECK(::DeleteDC(hdc));
  ::GlobalFree(pd.hDevNames);
  ::GlobalFree(pd.hDevMode);

  // Errors: a default asked for with settings given; a wrong size.
  memset(&pd, 0, sizeof pd);
  pd.lStructSize = sizeof pd;
  pd.Flags = PD_RETURNDEFAULT;
  pd.hDevMode = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DEVMODEW));
  CHECK(!::PrintDlgW(&pd) && ::CommDlgExtendedError() == PDERR_RETDEFFAILURE);
  ::GlobalFree(pd.hDevMode);
  pd.hDevMode = NULL;
  pd.lStructSize = 12;
  CHECK(!::PrintDlgW(&pd) && ::CommDlgExtendedError() == CDERR_STRUCTSIZE);

  // The ANSI form.
  PRINTDLGA pda;
  memset(&pda, 0, sizeof pda);
  pda.lStructSize = sizeof pda;
  pda.Flags = PD_RETURNDEFAULT;
  CHECK(::PrintDlgA(&pda));
  if (pda.hDevNames && pda.hDevMode) {
    DEVNAMES* dna = (DEVNAMES*)::GlobalLock(pda.hDevNames);
    DEVMODEA* dma = (DEVMODEA*)::GlobalLock(pda.hDevMode);
    CHECK(strcmp((const char*)dna + dna->wDeviceOffset, (const char*)dma->dmDeviceName) == 0);
    CHECK(*((const char*)dna + dna->wDriverOffset) != 0);
    ::GlobalUnlock(pda.hDevMode);
    ::GlobalUnlock(pda.hDevNames);
  }
  ::GlobalFree(pda.hDevNames);
  ::GlobalFree(pda.hDevMode);

  // The Print dialog: pages 2 to 3, twice.
  memset(&pd, 0, sizeof pd);
  pd.lStructSize = sizeof pd;
  pd.Flags = PD_ENABLEPRINTHOOK | PD_NOSELECTION;
  pd.lpfnPrintHook = PrintPagesHook;
  pd.lCustData = 77;
  pd.nFromPage = pd.nToPage = 1;
  pd.nMinPage = 1;
  pd.nMaxPage = 9;
  pd.nCopies = 1;
  CHECK(::PrintDlgW(&pd));
  CHECK(pd.Flags & PD_PAGENUMS);
  CHECK(pd.nFromPage == 2 && pd.nToPage == 3);
  CHECK(pd.hDevNames != NULL && pd.hDevMode != NULL && pd.hDC == NULL);
  int copies = pd.nCopies;
  if (pd.hDevMode) {
    DEVMODEW* m = (DEVMODEW*)::GlobalLock(pd.hDevMode);
    if (copies == 1 && m) copies = m->dmCopies;
    ::GlobalUnlock(pd.hDevMode);
  }
  CHECK(copies == 2);
  ::GlobalFree(pd.hDevNames);
  ::GlobalFree(pd.hDevMode);

  // Cancel leaves the structure.
  memset(&pd, 0, sizeof pd);
  pd.lStructSize = sizeof pd;
  pd.Flags = PD_ENABLEPRINTHOOK;
  pd.lpfnPrintHook = PrintCancelHook;
  CHECK(!::PrintDlgW(&pd));
  CHECK(pd.hDC == NULL && pd.hDevNames == NULL);

  // Page Setup: the defaults, then landscape.
  PAGESETUPDLGW psd;
  memset(&psd, 0, sizeof psd);
  psd.lStructSize = sizeof psd;
  psd.Flags = PSD_RETURNDEFAULT | PSD_INTHOUSANDTHSOFINCHES;
  CHECK(::PageSetupDlgW(&psd));
  POINT paper = psd.ptPaperSize;
  printf("paper %ldx%ld, margins %ld,%ld,%ld,%ld\n", (long)paper.x, (long)paper.y, (long)psd.rtMargin.left,
         (long)psd.rtMargin.top, (long)psd.rtMargin.right, (long)psd.rtMargin.bottom);
  CHECK(paper.y > paper.x && paper.x > 7000 && paper.x < 20000);
  CHECK(psd.hDevNames != NULL && psd.hDevMode != NULL);
  psd.Flags = PSD_ENABLEPAGESETUPHOOK | PSD_INTHOUSANDTHSOFINCHES;
  psd.lpfnPageSetupHook = PageSetupHook;
  psd.lCustData = 78;
  CHECK(::PageSetupDlgW(&psd));
  CHECK(psd.ptPaperSize.x == paper.y && psd.ptPaperSize.y == paper.x);
  if (psd.hDevMode) {
    DEVMODEW* m = (DEVMODEW*)::GlobalLock(psd.hDevMode);
    CHECK(m && m->dmOrientation == DMORIENT_LANDSCAPE);
    ::GlobalUnlock(psd.hDevMode);
  }
  ::GlobalFree(psd.hDevNames);
  ::GlobalFree(psd.hDevMode);
}

#ifdef _WINPORT
// PlgBlt: a 4x2 bitmap copied as it is, then turned a quarter clockwise,
// then through a mask.
static void TestPlgBlt() {
  HDC screen = ::GetDC(NULL);
  HDC src = ::CreateCompatibleDC(screen), dst = ::CreateCompatibleDC(screen);
  HBITMAP sbm = ::CreateCompatibleBitmap(screen, 4, 2), dbm = ::CreateCompatibleBitmap(screen, 8, 8);
  HGDIOBJ sold = ::SelectObject(src, sbm), dold = ::SelectObject(dst, dbm);
  for (int y = 0; y < 2; y++)
    for (int x = 0; x < 4; x++) ::SetPixel(src, x, y, RGB(x * 60, y * 200, 7));
  RECT all = {0, 0, 8, 8};
  ::FillRect(dst, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  POINT same[3] = {{0, 0}, {4, 0}, {0, 2}};
  CHECK(::PlgBlt(dst, same, src, 0, 0, 4, 2, NULL, 0, 0));
  CHECK(::GetPixel(dst, 3, 1) == RGB(180, 200, 7) && ::GetPixel(dst, 0, 0) == RGB(0, 0, 7));
  CHECK(::GetPixel(dst, 4, 0) == RGB(255, 255, 255));
  // A quarter turn: the upper left goes to (2,0), the upper right down to
  // (2,4), the lower left to (0,0).
  ::FillRect(dst, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  POINT turned[3] = {{2, 0}, {2, 4}, {0, 0}};
  CHECK(::PlgBlt(dst, turned, src, 0, 0, 4, 2, NULL, 0, 0));
  CHECK(::GetPixel(dst, 1, 0) == RGB(0, 0, 7));      // source (0,0)
  CHECK(::GetPixel(dst, 0, 0) == RGB(0, 200, 7));    // source (0,1)
  CHECK(::GetPixel(dst, 1, 3) == RGB(180, 0, 7));    // source (3,0)
  CHECK(::GetPixel(dst, 2, 0) == RGB(255, 255, 255));  // outside
  // A mask with only its left half set.
  HBITMAP mask = ::CreateBitmap(4, 2, 1, 1, NULL);
  HDC mdc = ::CreateCompatibleDC(screen);
  HGDIOBJ mold = ::SelectObject(mdc, mask);
  ::PatBlt(mdc, 0, 0, 4, 2, BLACKNESS);
  ::PatBlt(mdc, 0, 0, 2, 2, WHITENESS);
  ::SelectObject(mdc, mold);
  ::DeleteDC(mdc);
  ::FillRect(dst, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(::PlgBlt(dst, same, src, 0, 0, 4, 2, mask, 0, 0));
  CHECK(::GetPixel(dst, 1, 1) == RGB(60, 200, 7));
  CHECK(::GetPixel(dst, 3, 1) == RGB(255, 255, 255));
  ::DeleteObject(mask);
  ::SelectObject(src, sold);
  ::SelectObject(dst, dold);
  ::DeleteObject(sbm);
  ::DeleteObject(dbm);
  ::DeleteDC(src);
  ::DeleteDC(dst);
  ::ReleaseDC(NULL, screen);
}
#endif

#ifdef _WINPORT
// WinHelp, which opens the help document next to the .hlp file: here
// through WINPORT_HELP, a command that notes what it was given.
static std::string WaitForLine(const std::string& log, size_t n) {
  for (int i = 0; i < 500; i++) {
    std::vector<std::string> lines;
    if (FILE* f = fopen(log.c_str(), "r")) {
      char buf[1024];
      while (fgets(buf, sizeof(buf), f)) lines.push_back(buf);
      fclose(f);
    }
    if (lines.size() >= n) {
      std::string s = lines[n - 1];
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
      return s;
    }
    ::Sleep(10);
  }
  return std::string();
}

static void TestWinHelp() {
  const char* out = getenv("MFC_TEST_OUT");
  std::string dir = out && *out ? out : ".";
  std::string log = dir + "/winhelp.log";
  remove(log.c_str());
  std::string html = dir + "/AppHelp.html";
  if (FILE* f = fopen(html.c_str(), "w")) {
    fputs("<html><body><h1 id=\"131072\">Help</h1></body></html>\n", f);
    fclose(f);
  }
  std::string cmd = "echo >> '" + log + "'";
  setenv("WINPORT_HELP", cmd.c_str(), 1);
  std::wstring hlp = std::wstring(CStringW(dir.c_str())) + L"/AppHelp.HLP";

  // The contents: the document itself.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_FINDER, 0));
  CHECK(WaitForLine(log, 1) == html);
  // A context: the page's anchor; a window after the name is let go.
  std::string hlpA = dir + "/AppHelp.HLP>main";
  CHECK(::WinHelpA(NULL, hlpA.c_str(), HELP_CONTEXT, 131072));
  CHECK(WaitForLine(log, 2) == "file://" + html + "#131072");
  // Through CWnd, with the program's own help file.
  CWinApp* pApp = AfxGetApp();
  LPCTSTR pszOld = pApp->m_pszHelpFilePath;
  CString strHlp(hlp.c_str());
  pApp->m_pszHelpFilePath = strHlp;
  CWnd wnd;
  CHECK(wnd.CreateEx(0, _T("STATIC"), _T("help"), WS_POPUP, CRect(0, 0, 10, 10), NULL, 0));
  wnd.WinHelp(0, HELP_INDEX);
  wnd.DestroyWindow();
  pApp->m_pszHelpFilePath = pszOld;
  CHECK(WaitForLine(log, 3) == html);

  // Nothing to close; no document.
  CHECK(::WinHelpW(NULL, hlp.c_str(), HELP_QUIT, 0));
  ::SetLastError(0);
  CHECK(!::WinHelpW(NULL, L"/nowhere/none.hlp", HELP_CONTENTS, 0));
  CHECK(::GetLastError() == ERROR_FILE_NOT_FOUND);
  unsetenv("WINPORT_HELP");
  remove(html.c_str());
  remove(log.c_str());
}
#endif

#ifdef _WINPORT
// What WTL uses beyond the classic API: one monitor, layered windows,
// PrintWindow, scroll bar and cursor state, flat scroll bars, gradients,
// glyph indices, the description in a metafile, themes (off) and OpenGL
// (absent).
static COLORREF PixelAt(HDC dc, int x, int y) { return ::GetPixel(dc, x, y) & 0xFFFFFF; }

// Draws a red square at (5,5) for WM_PRINTCLIENT; the class brush erases.
static LRESULT CALLBACK PrintProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PRINTCLIENT) {
    RECT r = {5, 5, 15, 15};
    HBRUSH red = ::CreateSolidBrush(RGB(255, 0, 0));
    ::FillRect((HDC)wp, &r, red);
    ::DeleteObject(red);
    return 0;
  }
  return ::DefWindowProcW(h, msg, wp, lp);
}

#ifdef _WINPORT
// Layered windows as the screen shows them, and where they take the
// mouse: fully transparent pixels let it through to the window beneath.
static LRESULT CALLBACK LayerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND) {
    // Red on the left, green on the right.
    RECT r;
    ::GetClientRect(h, &r);
    RECT left = r, right = r;
    left.right = right.left = r.right / 2;
    HBRUSH red = ::CreateSolidBrush(RGB(255, 0, 0)), green = ::CreateSolidBrush(RGB(0, 255, 0));
    ::FillRect((HDC)wp, &left, red);
    ::FillRect((HDC)wp, &right, green);
    ::DeleteObject(red);
    ::DeleteObject(green);
    return 1;
  }
  return ::DefWindowProcW(h, msg, wp, lp);
}

static bool Near(COLORREF a, COLORREF b) {
  return abs(GetRValue(a) - GetRValue(b)) <= 1 && abs(GetGValue(a) - GetGValue(b)) <= 1 &&
         abs(GetBValue(a) - GetBValue(b)) <= 1;
}

static void TestLayeredWindows() {
  WNDCLASSW wc = {0};
  wc.lpfnWndProc = LayerProc;
  wc.lpszClassName = L"WinTestLayer";
  CHECK(::RegisterClassW(&wc));
  HBRUSH blue = ::CreateSolidBrush(RGB(0, 0, 255));
  wc.lpfnWndProc = ::DefWindowProcW;
  wc.hbrBackground = blue;
  wc.lpszClassName = L"WinTestBlue";
  CHECK(::RegisterClassW(&wc));
  HWND under = ::CreateWindowExW(0, L"WinTestBlue", L"under", WS_POPUP | WS_VISIBLE, 500, 300, 100, 100, NULL, NULL,
                                 NULL, NULL);
  HWND top = ::CreateWindowExW(WS_EX_LAYERED, L"WinTestLayer", L"top", WS_POPUP | WS_VISIBLE, 520, 320, 40, 40,
                               NULL, NULL, NULL, NULL);
  CHECK(under && top);
  ::UpdateWindow(under);
  ::UpdateWindow(top);
  HDC screen = ::GetDC(NULL);
  POINT redPt = {525, 325}, greenPt = {555, 325}, corner = {520, 320};
  const COLORREF kBlue = RGB(0, 0, 255);

  // Nothing shows until the window has attributes or a picture.
  CHECK(PixelAt(screen, redPt.x, redPt.y) == kBlue);
  CHECK(::WindowFromPoint(redPt) == under);

  // Half alpha over the blue.
  CHECK(::SetLayeredWindowAttributes(top, 0, 128, LWA_ALPHA));
  CHECK(Near(PixelAt(screen, redPt.x, redPt.y), RGB(128, 0, 127)));
  CHECK(Near(PixelAt(screen, greenPt.x, greenPt.y), RGB(0, 128, 127)));
  CHECK(::WindowFromPoint(redPt) == top);

  // A color key: the red half is a hole, for the eye and the mouse.
  CHECK(::SetLayeredWindowAttributes(top, RGB(255, 0, 0), 255, LWA_COLORKEY));
  CHECK(PixelAt(screen, redPt.x, redPt.y) == kBlue);
  CHECK(PixelAt(screen, greenPt.x, greenPt.y) == RGB(0, 255, 0));
  CHECK(::WindowFromPoint(redPt) == under && ::WindowFromPoint(greenPt) == top);
  // With WS_EX_TRANSPARENT the mouse goes through it everywhere.
  LONG ex = ::GetWindowLongW(top, GWL_EXSTYLE);
  ::SetWindowLongW(top, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
  CHECK(::WindowFromPoint(greenPt) == under);
  ::SetWindowLongW(top, GWL_EXSTYLE, ex);
  CHECK(::WindowFromPoint(greenPt) == top);

  // A 32-bit picture with alpha: white on the left, black at half alpha
  // on the right (premultiplied), a hole in the top left corner.
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = 40;
  bi.bmiHeader.biHeight = -40;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  DWORD* bits = NULL;
  HBITMAP dib = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
  CHECK(dib != NULL);
  for (int y = 0; y < 40; y++)
    for (int x = 0; x < 40; x++) bits[y * 40 + x] = x < 20 ? 0xFFFFFFFFu : 0x80000000u;
  bits[0] = 0;
  HDC mem = ::CreateCompatibleDC(NULL);
  HGDIOBJ old = ::SelectObject(mem, dib);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  POINT zero = {0, 0}, pos = {520, 320};
  SIZE size = {40, 40};
  // Not after SetLayeredWindowAttributes, until the style is set again.
  CHECK(!::UpdateLayeredWindow(top, NULL, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA));
  ::SetWindowLongW(top, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
  ::SetWindowLongW(top, GWL_EXSTYLE, ex);
  CHECK(PixelAt(screen, greenPt.x, greenPt.y) == kBlue);  // nothing shown again
  CHECK(::UpdateLayeredWindow(top, NULL, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA));
  CHECK(!::GetLayeredWindowAttributes(top, NULL, NULL, NULL));
  CHECK(PixelAt(screen, redPt.x, redPt.y) == RGB(255, 255, 255));
  CHECK(Near(PixelAt(screen, greenPt.x, greenPt.y), RGB(0, 0, 127)));
  CHECK(PixelAt(screen, corner.x, corner.y) == kBlue);
  CHECK(::WindowFromPoint(corner) == under && ::WindowFromPoint(redPt) == top);
  // The same picture at no constant alpha: gone, and the mouse goes by.
  bf.SourceConstantAlpha = 0;
  CHECK(::UpdateLayeredWindow(top, NULL, NULL, NULL, NULL, NULL, 0, &bf, ULW_ALPHA));
  CHECK(PixelAt(screen, redPt.x, redPt.y) == kBlue && ::WindowFromPoint(redPt) == under);
  // Moved by it, and the picture taken in full again.
  bf.SourceConstantAlpha = 255;
  pos.x = 530;
  CHECK(::UpdateLayeredWindow(top, NULL, &pos, &size, mem, &zero, 0, &bf, ULW_ALPHA));
  RECT wr;
  ::GetWindowRect(top, &wr);
  CHECK(wr.left == 530 && wr.top == 320 && wr.right == 570);
  CHECK(PixelAt(screen, 535, 325) == RGB(255, 255, 255) && PixelAt(screen, 525, 325) == kBlue);
  ::SelectObject(mem, old);
  ::DeleteDC(mem);
  ::DeleteObject(dib);

  // A window region shows on the screen as well: outside it, what is
  // beneath.
  ::DestroyWindow(top);
  CHECK(::SetWindowRgn(under, ::CreateRectRgn(0, 0, 50, 100), TRUE));
  ::UpdateWindow(under);
  CHECK(PixelAt(screen, 510, 310) == kBlue && PixelAt(screen, 590, 310) != kBlue);
  POINT outside = {590, 310};
  CHECK(::WindowFromPoint(outside) != under);
  ::ReleaseDC(NULL, screen);
  ::DestroyWindow(under);
  ::DeleteObject(blue);
}
#endif

static void TestDisplayAndDrawing() {
  // One monitor: the screen, with its work area.
  POINT origin = {0, 0}, away = {100000, 100000};
  HMONITOR mon = ::MonitorFromPoint(origin, MONITOR_DEFAULTTONULL);
  CHECK(mon != NULL);
  CHECK(::MonitorFromPoint(away, MONITOR_DEFAULTTONULL) == NULL);
  CHECK(::MonitorFromPoint(away, MONITOR_DEFAULTTONEAREST) == mon);
  MONITORINFOEXW mi;
  mi.cbSize = sizeof mi;
  CHECK(::GetMonitorInfoW(mon, &mi));
  CHECK(mi.rcMonitor.right == ::GetSystemMetrics(SM_CXSCREEN));
  CHECK(mi.rcMonitor.bottom == ::GetSystemMetrics(SM_CYSCREEN));
  CHECK(mi.dwFlags & MONITORINFOF_PRIMARY);
  CHECK(wcscmp(mi.szDevice, L"\\\\.\\DISPLAY1") == 0);
  MONITORINFO bad = {0};
  CHECK(!::GetMonitorInfoA(mon, &bad));

  WNDCLASSW wc = {0};
  wc.lpfnWndProc = PrintProc;
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"WinTestPrint";
  CHECK(::RegisterClassW(&wc));
  HWND w = ::CreateWindowExW(WS_EX_LAYERED, L"WinTestPrint", L"layered",
                             WS_POPUP | WS_VISIBLE | WS_VSCROLL | WS_BORDER, 10, 10, 120, 80, NULL, NULL, NULL, NULL);
  CHECK(w != NULL);
  CHECK(::MonitorFromWindow(w, MONITOR_DEFAULTTONULL) == mon);
  CHECK(::SetLayeredWindowAttributes(w, RGB(1, 2, 3), 128, LWA_ALPHA | LWA_COLORKEY));
  COLORREF key = 0;
  BYTE alpha = 0;
  DWORD flags = 0;
  CHECK(::GetLayeredWindowAttributes(w, &key, &alpha, &flags));
  CHECK(key == RGB(1, 2, 3) && alpha == 128 && flags == (LWA_ALPHA | LWA_COLORKEY));
  HWND plain = ::CreateWindowW(L"STATIC", L"plain", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
  CHECK(!::SetLayeredWindowAttributes(plain, 0, 255, LWA_ALPHA));
  ::DestroyWindow(plain);

  // The window's scroll bar: its rectangle on the right, its arrows.
  SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, 99, 10, 0};
  ::SetScrollInfo(w, SB_VERT, &si, FALSE);
  SCROLLBARINFO sbi = {sizeof sbi};
  CHECK(::GetScrollBarInfo(w, OBJID_VSCROLL, &sbi));
  RECT wr;
  ::GetWindowRect(w, &wr);
  CHECK(sbi.rcScrollBar.right == wr.right - 1);  // inside the border
  CHECK(sbi.rcScrollBar.right - sbi.rcScrollBar.left == ::GetSystemMetrics(SM_CXVSCROLL));
  CHECK(sbi.dxyLineButton == ::GetSystemMetrics(SM_CXVSCROLL));
  CHECK(sbi.xyThumbTop == sbi.dxyLineButton);
  CHECK(sbi.xyThumbBottom > sbi.xyThumbTop);
  CHECK(!(sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE));
  CHECK(::GetScrollBarInfo(w, OBJID_HSCROLL, &sbi));
  CHECK(sbi.rgstate[0] & STATE_SYSTEM_INVISIBLE);

  // Flat scroll bars are the window's own.
  CHECK(::InitializeFlatSB(w));
  CHECK(::FlatSB_SetScrollPos(w, SB_VERT, 40, FALSE) == 0);
  CHECK(::GetScrollPos(w, SB_VERT) == 40);
  int prop = 0;
  CHECK(::FlatSB_GetScrollProp(w, WSB_PROP_CXVSCROLL, &prop) && prop == ::GetSystemMetrics(SM_CXVSCROLL));
  CHECK(::FlatSB_SetScrollProp(w, WSB_PROP_VSTYLE, FSB_FLAT_MODE, FALSE));
  CHECK(::FlatSB_GetScrollProp(w, WSB_PROP_VSTYLE, &prop) && prop == FSB_FLAT_MODE);
  CHECK(::UninitializeFlatSB(w) == S_OK);

  // PrintWindow draws the client area through WM_PRINTCLIENT.
  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  HBITMAP bm = ::CreateCompatibleBitmap(screen, 200, 100);
  HGDIOBJ old = ::SelectObject(mem, bm);
  RECT all = {0, 0, 200, 100};
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  CHECK(::PrintWindow(w, mem, PW_CLIENTONLY));
  CHECK(PixelAt(mem, 2, 2) == (::GetSysColor(COLOR_BTNFACE) & 0xFFFFFF));
  CHECK(PixelAt(mem, 10, 10) == RGB(255, 0, 0));
  // The whole window: the border, the client area inside it, and a child
  // in its place.
  HWND kid = ::CreateWindowW(L"WinTestPrint", L"", WS_CHILD | WS_VISIBLE, 40, 20, 30, 30, w, NULL, NULL, NULL);
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  CHECK(::PrintWindow(w, mem, 0));
  CHECK(PixelAt(mem, 0, 0) == (::GetSysColor(COLOR_WINDOWFRAME) & 0xFFFFFF));
  CHECK(PixelAt(mem, 11, 11) == RGB(255, 0, 0));
  CHECK(PixelAt(mem, 1 + 40 + 2, 1 + 20 + 2) == (::GetSysColor(COLOR_BTNFACE) & 0xFFFFFF));
  CHECK(PixelAt(mem, 1 + 40 + 10, 1 + 20 + 10) == RGB(255, 0, 0));
  ::DestroyWindow(kid);
  ::DestroyWindow(w);
  ::UnregisterClassW(L"WinTestPrint", NULL);

  CURSORINFO ci = {sizeof ci};
  CHECK(::GetCursorInfo(&ci));
  POINT pt;
  ::GetCursorPos(&pt);
  CHECK(ci.ptScreenPos.x == pt.x && ci.ptScreenPos.y == pt.y);

  // A gradient across: red at the left, blue at the right.
  TRIVERTEX v[2] = {{0, 0, 0xFF00, 0, 0, 0}, {100, 10, 0, 0, 0xFF00, 0}};
  GRADIENT_RECT gr = {0, 1};
  CHECK(::GradientFill(mem, v, 2, &gr, 1, GRADIENT_FILL_RECT_H));
  CHECK(PixelAt(mem, 0, 5) == RGB(255, 0, 0));
  CHECK(PixelAt(mem, 99, 5) == RGB(0, 0, 255));
  COLORREF mid = PixelAt(mem, 50, 5);
  CHECK(GetRValue(mid) > 100 && GetRValue(mid) < 155 && GetBValue(mid) > 100 && GetBValue(mid) < 155);
  // Down, then a triangle: its corners' colors, nothing outside.
  CHECK(::GradientFill(mem, v, 2, &gr, 1, GRADIENT_FILL_RECT_V));
  CHECK(PixelAt(mem, 50, 0) == RGB(255, 0, 0));
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  TRIVERTEX t[3] = {{0, 0, 0xFF00, 0, 0, 0}, {60, 0, 0, 0xFF00, 0, 0}, {0, 60, 0, 0, 0xFF00, 0}};
  GRADIENT_TRIANGLE tri = {0, 1, 2};
  CHECK(::GradientFill(mem, t, 3, &tri, 1, GRADIENT_FILL_TRIANGLE));
  CHECK(GetRValue(PixelAt(mem, 0, 0)) > 240);
  CHECK(GetGValue(PixelAt(mem, 58, 0)) > 220);
  CHECK(GetBValue(PixelAt(mem, 0, 58)) > 220);
  CHECK(PixelAt(mem, 59, 59) == 0);
  GRADIENT_RECT wrong = {0, 5};
  CHECK(!::GradientFill(mem, v, 2, &wrong, 1, GRADIENT_FILL_RECT_H));

  // Shadowed text draws both.
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  RECT tr = {0, 0, 200, 40};
  CHECK(::DrawShadowText(mem, L"Shadow", 6, &tr, DT_LEFT | DT_TOP, RGB(255, 0, 0), RGB(0, 0, 255), 3, 3) > 0);
  bool red = false, blue = false;
  for (int y = 0; y < 30; y++)
    for (int x = 0; x < 80; x++) {
      COLORREF c = PixelAt(mem, x, y);
      red = red || c == RGB(255, 0, 0);
      blue = blue || c == RGB(0, 0, 255);
    }
  CHECK(red && blue);

  // Glyph indices are the characters; widths match their *I forms.
  WORD g[3] = {0};
  CHECK(::GetGlyphIndicesW(mem, L"AB\xFFFF", 3, g, GGI_MARK_NONEXISTING_GLYPHS) == 3);
  CHECK(g[0] == 'A' && g[1] == 'B');
  SIZE s1, s2;
  CHECK(::GetTextExtentPoint32W(mem, L"AB", 2, &s1));
  CHECK(::GetTextExtentPointI(mem, g, 2, &s2));
  CHECK(s1.cx == s2.cx && s1.cy == s2.cy);
  INT wA = 0, wI = 0;
  FLOAT wF = 0;
  CHECK(::GetCharWidth32W(mem, 'A', 'A', &wA));
  CHECK(::GetCharWidthI(mem, 0, 1, g, &wI) && wI == wA);
  CHECK(::GetCharWidthFloatW(mem, 'A', 'A', &wF) && (int)wF == wA);
  ABCFLOAT abcf;
  CHECK(::GetCharABCWidthsFloatW(mem, 'A', 'A', &abcf));
  CHECK((int)(abcf.abcfA + abcf.abcfB + abcf.abcfC) == wA);
  DWORD need = ::GetFontUnicodeRanges(mem, NULL);
  CHECK(need >= sizeof(GLYPHSET));
  std::vector<BYTE> buf(need);
  GLYPHSET* gs = (GLYPHSET*)buf.data();
  CHECK(::GetFontUnicodeRanges(mem, gs) == need);
  bool hasA = false;
  for (DWORD i = 0; i < gs->cRanges; i++)
    hasA = hasA || ('A' >= gs->ranges[i].wcLow && 'A' < gs->ranges[i].wcLow + gs->ranges[i].cGlyphs);
  CHECK(hasA && gs->cGlyphsSupported >= 26);

  ENUMLOGFONTEXDVW ex;
  memset(&ex, 0, sizeof ex);
  ex.elfEnumLogfontEx.elfLogFont.lfHeight = -20;
  wcscpy(ex.elfEnumLogfontEx.elfLogFont.lfFaceName, L"Arial");
  HFONT f = ::CreateFontIndirectExW(&ex);
  CHECK(f != NULL);
  LOGFONTW lf;
  CHECK(::GetObjectW(f, sizeof lf, &lf) && lf.lfHeight == -20);
  ::DeleteObject(f);

  ::SelectObject(mem, old);
  ::DeleteObject(bm);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);

  // The description saved with a metafile, both parts.
  HDC emf = ::CreateEnhMetaFileW(NULL, NULL, NULL, L"App\0Picture\0");
  ::Rectangle(emf, 0, 0, 10, 10);
  HENHMETAFILE hemf = ::CloseEnhMetaFile(emf);
  CHECK(::GetEnhMetaFileDescriptionW(hemf, 0, NULL) == 13);
  WCHAR desc[20];
  CHECK(::GetEnhMetaFileDescriptionW(hemf, 20, desc) == 13);
  CHECK(wcscmp(desc, L"App") == 0 && wcscmp(desc + 4, L"Picture") == 0);
  char descA[20];
  CHECK(::GetEnhMetaFileDescriptionA(hemf, 20, descA) == 13 && strcmp(descA + 4, "Picture") == 0);
  CHECK(::GetEnhMetaFilePaletteEntries(hemf, 0, NULL) == 0);
  ::DeleteEnhMetaFile(hemf);

  // No visual styles. OpenGL is over CGL on Mac OS X and over the
  // generic implementation (port/softgl) on the other systems: both give
  // the single-buffered format for this request.
  CHECK(!::IsThemeActive() && !::IsAppThemed());
  CHECK(::OpenThemeData(NULL, L"BUTTON") == NULL);
  CHECK(::GetThemeSysColor(NULL, COLOR_WINDOW) == ::GetSysColor(COLOR_WINDOW));
  LOGFONTW tf;
  CHECK(SUCCEEDED(::GetThemeSysFont(NULL, TMT_MENUFONT, &tf)));
  PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL};
  HDC sdc = ::GetDC(NULL);
  CHECK(::ChoosePixelFormat(sdc, &pfd) == 2);
  CHECK(::wglMakeCurrent(NULL, NULL));
  ::ReleaseDC(NULL, sdc);
}

// DECIMAL: exact text, rounding half to even, comparison across scales.
static void TestDecimal() {
  DECIMAL a, b;
  CHECK(SUCCEEDED(::VarDecFromStr(L"12.50", 0, 0, &a)));
  CHECK(a.scale == 2 && a.Lo32 == 1250 && a.sign == 0);
  CHECK(SUCCEEDED(::VarDecFromStr(L"12.5", 0, 0, &b)));
  CHECK(::VarDecCmp(&a, &b) == VARCMP_EQ);
  CHECK(SUCCEEDED(::VarDecFromStr(L"-0.001", 0, 0, &b)));
  CHECK(b.sign == DECIMAL_NEG && b.scale == 3 && b.Lo32 == 1);
  CHECK(::VarDecCmp(&b, &a) == VARCMP_LT && ::VarDecCmp(&a, &b) == VARCMP_GT);
  BSTR s = NULL;
  CHECK(SUCCEEDED(::VarBstrFromDec(&b, 0, 0, &s)) && wcscmp(s, L"-0.001") == 0);
  ::SysFreeString(s);
  CHECK(SUCCEEDED(::VarDecFromStr(L"79228162514264337593543950335", 0, 0, &a)));
  CHECK(a.Hi32 == 0xFFFFFFFF && a.Mid32 == 0xFFFFFFFF && a.Lo32 == 0xFFFFFFFF);
  CHECK(::VarDecFromStr(L"79228162514264337593543950336", 0, 0, &a) == DISP_E_OVERFLOW);
  CHECK(::VarDecFromStr(L"12x", 0, 0, &a) == DISP_E_TYPEMISMATCH);
  // Past 28 places, to even.
  CHECK(SUCCEEDED(::VarDecFromStr(L"0.00000000000000000000000000025", 0, 0, &a)));
  CHECK(a.scale == 28 && a.Lo32 == 2);
  CHECK(SUCCEEDED(::VarDecFromStr(L"0.00000000000000000000000000035", 0, 0, &a)));
  CHECK(a.Lo32 == 4);
  CHECK(SUCCEEDED(::VarDecFromStr(L"1.5e3", 0, 0, &a)));
  CHECK(SUCCEEDED(::VarBstrFromDec(&a, 0, 0, &s)) && wcscmp(s, L"1500") == 0);
  ::SysFreeString(s);
  CHECK(SUCCEEDED(::VarDecFromI4(-42, &a)));
  double d = 0;
  CHECK(SUCCEEDED(::VarR8FromDec(&a, &d)) && d == -42.0);
  CHECK(SUCCEEDED(::VarDecFromR8(0.1, &a)));
  CHECK(SUCCEEDED(::VarBstrFromDec(&a, 0, 0, &s)) && wcscmp(s, L"0.1") == 0);
  ::SysFreeString(s);
  CHECK(::VarDecFromR8(1e30, &a) == DISP_E_OVERFLOW);
  DECIMAL z1, z2;
  CHECK(SUCCEEDED(::VarDecFromStr(L"0", 0, 0, &z1)) && SUCCEEDED(::VarDecFromStr(L"-0.00", 0, 0, &z2)));
  CHECK(::VarDecCmp(&z1, &z2) == VARCMP_EQ);
}

// The shell namespace over the file system, and the folder browser.
static std::string g_browseDir;
static int g_browseSelChanges = 0;
static int CALLBACK BrowseCallback(HWND h, UINT msg, LPARAM lp, LPARAM data) {
  if (msg == BFFM_INITIALIZED) {
    CHECK(data == 1234);
    std::string sub = g_browseDir + "/sub";
    ::SendMessageA(h, BFFM_SETSELECTIONA, TRUE, (LPARAM)sub.c_str());
    ::SendMessageW(h, BFFM_SETOKTEXT, 0, (LPARAM)L"Choose");
    WCHAR ok[20];
    ::GetDlgItemTextW(h, IDOK, ok, 20);
    CHECK(wcscmp(ok, L"Choose") == 0);
    ::PostMessageW(h, WM_COMMAND, IDOK, 0);
  } else if (msg == BFFM_SELCHANGED) {
    g_browseSelChanges++;
    char path[MAX_PATH];
    CHECK(::SHGetPathFromIDListA((LPCITEMIDLIST)lp, path));
  }
  return 0;
}

static void TestShellNamespace() {
  char tmpl[] = "/tmp/wintest-shell-XXXXXX";
  CHECK(mkdtemp(tmpl) != NULL);
  std::string dir = tmpl;
  CHECK(::CreateDirectoryA((dir + "/sub").c_str(), NULL));
  CHECK(::CreateDirectoryA((dir + "/sub/deeper").c_str(), NULL));
  FILE* fp = fopen((dir + "/file.txt").c_str(), "w");
  fputs("hello", fp);
  fclose(fp);
  fp = fopen((dir + "/.hidden").c_str(), "w");
  fclose(fp);

  // ID lists: one item a component.
  std::wstring wdir(dir.begin(), dir.end());
  LPITEMIDLIST pidl = ::ILCreateFromPathW(wdir.c_str());
  CHECK(pidl != NULL);
  WCHAR path[MAX_PATH];
  CHECK(::SHGetPathFromIDListW(pidl, path) && wdir == path);
  LPITEMIDLIST copy = ::ILClone(pidl);
  CHECK(::ILIsEqual(pidl, copy) && ::ILGetSize(copy) == ::ILGetSize(pidl));
  CHECK(::ILRemoveLastID(copy));
  CHECK(::ILIsParent(copy, pidl, TRUE));
  CHECK(::SHGetPathFromIDListW(copy, path) && wcscmp(path, L"/tmp") == 0);
  LPCITEMIDLIST last = ::ILFindLastID(pidl);
  LPITEMIDLIST again = ::ILCombine(copy, last);
  CHECK(::ILIsEqual(again, pidl));
  CHECK(::ILFindChild(copy, pidl) == last);
  ::ILFree(again);
  ::ILFree(copy);

  // The desktop is the root; folders bind below it.
  IShellFolder* desktop = NULL;
  CHECK(SUCCEEDED(::SHGetDesktopFolder(&desktop)));
  IShellFolder* folder = NULL;
  CHECK(SUCCEEDED(desktop->BindToObject(pidl, NULL, IID_IShellFolder, (void**)&folder)));
  IEnumIDList* items = NULL;
  CHECK(SUCCEEDED(folder->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items)));
  std::vector<std::string> names;
  LPITEMIDLIST child;
  ULONG got;
  while (items->Next(1, &child, &got) == S_OK) {
    STRRET name;
    CHECK(SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &name)) && name.uType == STRRET_WSTR);
    std::wstring w = name.pOleStr;
    names.push_back(std::string(w.begin(), w.end()));
    ::CoTaskMemFree(name.pOleStr);
    ::ILFree(child);
  }
  items->Release();
  CHECK(names.size() == 2 && names[0] == "file.txt" && names[1] == "sub");
  CHECK(SUCCEEDED(folder->EnumObjects(NULL, SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &items)));
  ULONG n = 0;
  LPITEMIDLIST two[3] = {NULL, NULL, NULL};
  CHECK(items->Next(3, two, &n) == S_FALSE && n == 2);
  ::ILFree(two[0]);
  ::ILFree(two[1]);
  items->Release();

  LPITEMIDLIST sub = NULL, file = NULL;
  ULONG attrs = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSTEM | SFGAO_LINK;
  CHECK(SUCCEEDED(folder->ParseDisplayName(NULL, NULL, (LPWSTR)L"sub", NULL, &sub, &attrs)));
  CHECK(attrs == (SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSTEM));
  attrs = SFGAO_FOLDER | SFGAO_STREAM;
  CHECK(SUCCEEDED(folder->ParseDisplayName(NULL, NULL, (LPWSTR)L"file.txt", NULL, &file, &attrs)));
  CHECK(attrs == SFGAO_STREAM);
  CHECK(folder->ParseDisplayName(NULL, NULL, (LPWSTR)L"missing", NULL, &child, NULL) ==
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
  // Folders before files.
  CHECK((short)HRESULT_CODE(folder->CompareIDs(0, sub, file)) < 0);
  CHECK((short)HRESULT_CODE(folder->CompareIDs(0, sub, sub)) == 0);
  STRRET full;
  CHECK(SUCCEEDED(folder->GetDisplayNameOf(file, SHGDN_FORPARSING, &full)));
  CHECK(std::wstring(full.pOleStr) == wdir + L"/file.txt");
  ::CoTaskMemFree(full.pOleStr);
  IStream* stream = NULL;
  CHECK(SUCCEEDED(folder->BindToStorage(file, NULL, IID_IStream, (void**)&stream)));
  char text[8] = {0};
  ULONG read = 0;
  CHECK(SUCCEEDED(stream->Read(text, sizeof text, &read)) && read == 5 && strcmp(text, "hello") == 0);
  stream->Release();
  void* none = (void*)1;
  CHECK(folder->BindToObject(file, NULL, IID_IShellLinkW, &none) == E_NOINTERFACE && none == NULL);
  LPITEMIDLIST renamed = NULL;
  CHECK(SUCCEEDED(folder->SetNameOf(NULL, file, L"renamed.txt", SHGDN_INFOLDER, &renamed)));
  CHECK(::GetFileAttributesA((dir + "/renamed.txt").c_str()) != INVALID_FILE_ATTRIBUTES);
  ::ILFree(renamed);
  ::ILFree(file);
  ::ILFree(sub);
  folder->Release();
  desktop->Release();

  LPITEMIDLIST root = NULL;
  CHECK(SUCCEEDED(::SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &root)) && ILIsEmpty(root));
  CHECK(::SHGetPathFromIDListA(root, (char*)path) && strcmp((char*)path, "/") == 0);
  ::ILFree(root);
  IMalloc* malloc = NULL;
  CHECK(SUCCEEDED(::SHGetMalloc(&malloc)));
  void* block = malloc->Alloc(10);
  CHECK(block != NULL && malloc->GetSize(block) >= 10);
  malloc->Free(block);

  // The folder browser, from a root, its callback choosing for it.
  g_browseDir = dir;
  char display[MAX_PATH] = "";
  BROWSEINFOA bi;
  memset(&bi, 0, sizeof bi);
  bi.pidlRoot = pidl;
  bi.pszDisplayName = display;
  bi.lpszTitle = "Pick one";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
  bi.lpfn = BrowseCallback;
  bi.lParam = 1234;
  LPITEMIDLIST chosen = ::SHBrowseForFolderA(&bi);
  CHECK(chosen != NULL);
  char chosenPath[MAX_PATH] = "";
  CHECK(chosen && ::SHGetPathFromIDListA(chosen, chosenPath) && dir + "/sub" == chosenPath);
  CHECK(strcmp(display, "sub") == 0);
  CHECK(g_browseSelChanges >= 2);
  ::ILFree(chosen);
  ::ILFree(pidl);

  remove((dir + "/renamed.txt").c_str());
  remove((dir + "/.hidden").c_str());
  rmdir((dir + "/sub/deeper").c_str());
  rmdir((dir + "/sub").c_str());
  rmdir(dir.c_str());
}

// ---------------------------------------------------------------------
// OLE odds and ends: connections enumerated, a frozen picture, a
// binding suspended, names bound in type information, records copied,
// and pictures that are not bitmaps saved and loaded again.

class CPropSink : public IPropertyNotifySink {
public:
  LONG m_nRef = 1;
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IPropertyNotifySink)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_nRef; }
  STDMETHOD_(ULONG, Release)() { return (ULONG)--m_nRef; }
  STDMETHOD(OnChanged)(DISPID) { return S_OK; }
  STDMETHOD(OnRequestEdit)(DISPID) { return S_OK; }
};

class CBindSink : public IBindStatusCallback {
public:
  LONG m_nRef = 1;
  IBinding* m_pBinding = NULL;
  int m_nData = 0;
  BOOL m_bStopped = FALSE;
  BOOL m_bSuspend = TRUE;
  HRESULT m_hrSuspend = E_FAIL;
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IBindStatusCallback)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_nRef; }
  STDMETHOD_(ULONG, Release)() { return (ULONG)--m_nRef; }
  STDMETHOD(OnStartBinding)(DWORD, IBinding* pib) {
    m_pBinding = pib;
    pib->AddRef();
    if (m_bSuspend) m_hrSuspend = pib->Suspend();
    return S_OK;
  }
  STDMETHOD(GetPriority)(LONG*) { return E_NOTIMPL; }
  STDMETHOD(OnLowResource)(DWORD) { return S_OK; }
  STDMETHOD(OnProgress)(ULONG, ULONG, ULONG, LPCWSTR) { return S_OK; }
  STDMETHOD(OnStopBinding)(HRESULT, LPCWSTR) {
    m_bStopped = TRUE;
    return S_OK;
  }
  STDMETHOD(GetBindInfo)(DWORD* pgrf, BINDINFO* pbi) {
    *pgrf = BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE;
    (void)pbi;
    return S_OK;
  }
  STDMETHOD(OnDataAvailable)(DWORD, DWORD, FORMATETC*, STGMEDIUM*) {
    m_nData++;
    return S_OK;
  }
  STDMETHOD(OnObjectAvailable)(REFIID, IUnknown*) { return S_OK; }
};

// A record of one LONG.
class CLongRecord : public IRecordInfo {
public:
  LONG m_nRef = 1;
  int m_nCopies = 0;
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IRecordInfo)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_nRef; }
  STDMETHOD_(ULONG, Release)() { return (ULONG)--m_nRef; }
  STDMETHOD(RecordInit)(PVOID pv) {
    *(LONG*)pv = 0;
    return S_OK;
  }
  STDMETHOD(RecordClear)(PVOID pv) {
    *(LONG*)pv = 0;
    return S_OK;
  }
  STDMETHOD(RecordCopy)(PVOID pvOld, PVOID pvNew) {
    *(LONG*)pvNew = *(LONG*)pvOld;
    return S_OK;
  }
  STDMETHOD(GetGuid)(GUID*) { return E_NOTIMPL; }
  STDMETHOD(GetName)(BSTR*) { return E_NOTIMPL; }
  STDMETHOD(GetSize)(ULONG* pcb) {
    *pcb = sizeof(LONG);
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetField)(PVOID, LPCOLESTR, VARIANT*) { return E_NOTIMPL; }
  STDMETHOD(GetFieldNoCopy)(PVOID, LPCOLESTR, VARIANT*, PVOID*) { return E_NOTIMPL; }
  STDMETHOD(PutField)(ULONG, PVOID, LPCOLESTR, VARIANT*) { return E_NOTIMPL; }
  STDMETHOD(PutFieldNoCopy)(ULONG, PVOID, LPCOLESTR, VARIANT*) { return E_NOTIMPL; }
  STDMETHOD(GetFieldNames)(ULONG*, BSTR*) { return E_NOTIMPL; }
  STDMETHOD_(BOOL, IsMatchingType)(IRecordInfo* p) { return p == this; }
  STDMETHOD_(PVOID, RecordCreate)() { return ::CoTaskMemAlloc(sizeof(LONG)); }
  STDMETHOD(RecordCreateCopy)(PVOID pvSource, PVOID* ppvDest) {
    m_nCopies++;
    *ppvDest = RecordCreate();
    return *ppvDest ? RecordCopy(pvSource, *ppvDest) : E_OUTOFMEMORY;
  }
  STDMETHOD(RecordDestroy)(PVOID pv) {
    ::CoTaskMemFree(pv);
    return S_OK;
  }
};

static HENHMETAFILE MakeEmf(int cx, int cy) {
  RECT rc = {0, 0, cx, cy};  // HIMETRIC
  HDC dc = ::CreateEnhMetaFileW(NULL, NULL, &rc, NULL);
  if (dc == NULL) return NULL;
  ::Rectangle(dc, 0, 0, 50, 50);
  return ::CloseEnhMetaFile(dc);
}

// Saves a picture to a stream and loads it again.
static IPicture* RoundTrip(IPicture* pic, LONG* pcbSaved) {
  *pcbSaved = 0;
  IStream* s = NULL;
  if (FAILED(::CreateStreamOnHGlobal(NULL, TRUE, &s))) return NULL;
  IPicture* out = NULL;
  if (SUCCEEDED(pic->SaveAsFile(s, TRUE, pcbSaved)) && *pcbSaved > 0) {
    LARGE_INTEGER zero = {};
    s->Seek(zero, STREAM_SEEK_SET, NULL);
    ::OleLoadPicture(s, *pcbSaved, FALSE, IID_IPicture, (void**)&out);
  }
  s->Release();
  return out;
}

static void TestOleBits() {
  CHECK(SUCCEEDED(::OleInitialize(NULL)));

  // A font's property-notification connections.
  FONTDESC fd = {sizeof(FONTDESC), (LPOLESTR)L"Arial", {120000, 0}, FW_NORMAL, ANSI_CHARSET, FALSE, FALSE, FALSE};
  IUnknown* pFont = NULL;
  CHECK(::OleCreateFontIndirect(&fd, IID_IUnknown, (void**)&pFont) == S_OK);
  IConnectionPointContainer* pCPC = NULL;
  IConnectionPoint* pCP = NULL;
  if (pFont != NULL) pFont->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
  if (pCPC != NULL) pCPC->FindConnectionPoint(IID_IPropertyNotifySink, &pCP);
  CHECK(pCP != NULL);
  if (pCP != NULL) {
    CPropSink sink;
    DWORD dwCookie = 0;
    CHECK(pCP->Advise(&sink, &dwCookie) == S_OK);
    IEnumConnections* pEnum = NULL;
    CHECK(pCP->EnumConnections(&pEnum) == S_OK && pEnum != NULL);
    if (pEnum != NULL) {
      CONNECTDATA cd[2];
      ULONG n = 0;
      CHECK(pEnum->Next(2, cd, &n) == S_FALSE && n == 1);
      if (n == 1) {
        CHECK(cd[0].dwCookie == dwCookie && cd[0].pUnk == &sink);
        cd[0].pUnk->Release();
      }
      pEnum->Release();
    }
    CHECK(pCP->Unadvise(dwCookie) == S_OK);
    CHECK(sink.m_nRef == 1);
    pCP->Release();
  }
  if (pCPC != NULL) pCPC->Release();
  if (pFont != NULL) pFont->Release();

  // A cached picture, frozen: a new one does not show until it thaws.
  static const CLSID clsidNone = {0x1b2c3d4e, 0x0001, 0x0002, {0, 1, 2, 3, 4, 5, 6, 7}};
  IOleCache* pCache = NULL;
  CHECK(::OleCreateDefaultHandler(clsidNone, NULL, IID_IOleCache, (void**)&pCache) == S_OK);
  if (pCache != NULL) {
    FORMATETC fe = {CF_ENHMETAFILE, NULL, DVASPECT_CONTENT, -1, TYMED_ENHMF};
    DWORD dwConn = 0;
    CHECK(SUCCEEDED(pCache->Cache(&fe, ADVF_PRIMEFIRST, &dwConn)));
    STGMEDIUM m = {};
    m.tymed = TYMED_ENHMF;
    m.hEnhMetaFile = MakeEmf(1000, 500);
    CHECK(pCache->SetData(&fe, &m, TRUE) == S_OK);
    IViewObject* pView = NULL;
    CHECK(pCache->QueryInterface(IID_IViewObject, (void**)&pView) == S_OK);
    IViewObject2* pView2 = NULL;
    if (pView != NULL) pView->QueryInterface(IID_IViewObject2, (void**)&pView2);
    if (pView != NULL && pView2 != NULL) {
      DWORD dwFreeze = 0, dwAgain = 0;
      CHECK(pView->Freeze(DVASPECT_ICON, -1, NULL, &dwFreeze) == OLE_E_BLANK);
      CHECK(pView->Freeze(DVASPECT_CONTENT, -1, NULL, &dwFreeze) == S_OK);
      CHECK(pView->Freeze(DVASPECT_CONTENT, -1, NULL, &dwAgain) == VIEW_S_ALREADY_FROZEN && dwAgain == dwFreeze);
      m.tymed = TYMED_ENHMF;
      m.hEnhMetaFile = MakeEmf(3000, 2000);
      CHECK(pCache->SetData(&fe, &m, TRUE) == S_OK);
      SIZEL size = {0, 0};
      CHECK(pView2->GetExtent(DVASPECT_CONTENT, -1, NULL, &size) == S_OK);
      CHECK(size.cx == 1000 && size.cy == 500);
      CHECK(pView->Unfreeze(dwFreeze) == S_OK);
      CHECK(pView->Unfreeze(dwFreeze) == OLE_E_NOCONNECTION);
      CHECK(pView2->GetExtent(DVASPECT_CONTENT, -1, NULL, &size) == S_OK);
      CHECK(size.cx == 3000 && size.cy == 2000);
    }
    if (pView2 != NULL) pView2->Release();
    if (pView != NULL) pView->Release();
    pCache->Release();
  }

  // A binding suspended as it starts: no data until it resumes.
  char szFile[] = "/tmp/wintest-bind-XXXXXX";
  int fdTemp = mkstemp(szFile);
  CHECK(fdTemp >= 0);
  if (fdTemp >= 0) {
    CHECK(write(fdTemp, "hello", 5) == 5);
    close(fdTemp);
    WCHAR szUrl[64] = L"file://";
    for (int i = 0; szFile[i]; i++) szUrl[7 + i] = szFile[i], szUrl[8 + i] = 0;
    IMoniker* pmk = NULL;
    CHECK(::CreateURLMoniker(NULL, szUrl, &pmk) == S_OK);
    CBindSink bsc;
    IBindCtx* pbc = NULL;
    CHECK(::CreateAsyncBindCtx(0, &bsc, NULL, &pbc) == S_OK);
    if (pmk != NULL && pbc != NULL) {
      IStream* pstm = NULL;
      CHECK(pmk->BindToStorage(pbc, NULL, IID_IStream, (void**)&pstm) == MK_S_ASYNCHRONOUS);
      CHECK(bsc.m_hrSuspend == S_OK);
      MSG msg;
      DWORD dwStart = ::GetTickCount();
      while (::GetTickCount() - dwStart < 200) {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) ::DispatchMessage(&msg);
        ::Sleep(10);
      }
      CHECK(bsc.m_nData == 0 && !bsc.m_bStopped);
      if (bsc.m_pBinding != NULL) {
        CHECK(bsc.m_pBinding->Suspend() == S_FALSE);
        CHECK(bsc.m_pBinding->Resume() == S_OK);
        CHECK(bsc.m_pBinding->Resume() == S_FALSE);
      }
      dwStart = ::GetTickCount();
      while (!bsc.m_bStopped && ::GetTickCount() - dwStart < 2000) {
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) ::DispatchMessage(&msg);
        ::Sleep(10);
      }
      CHECK(bsc.m_nData == 1 && bsc.m_bStopped);
      if (bsc.m_pBinding != NULL) {
        CHECK(bsc.m_pBinding->Resume() == E_FAIL);
        bsc.m_pBinding->Release();
      }
      if (pstm != NULL) pstm->Release();
    }
    if (pbc != NULL) pbc->Release();
    if (pmk != NULL) pmk->Release();
    unlink(szFile);
  }

  // Names bound through a type's ITypeComp and a library's.
  ICreateTypeLib2* pctl = NULL;
  CHECK(::CreateTypeLib2(SYS_WIN32, L"wintest.tlb", &pctl) == S_OK);
  if (pctl != NULL) {
    static const GUID iidThing = {0x51c0ffee, 0x1234, 0x4abc, {1, 2, 3, 4, 5, 6, 7, 8}};
    ICreateTypeInfo* pcti = NULL;
    if (SUCCEEDED(pctl->CreateTypeInfo((LPOLESTR)L"DThing", TKIND_DISPATCH, &pcti))) {
      pcti->SetGuid(iidThing);
      FUNCDESC f = {};
      f.memid = 7;
      f.funckind = FUNC_DISPATCH;
      f.invkind = INVOKE_FUNC;
      f.callconv = CC_STDCALL;
      f.elemdescFunc.tdesc.vt = VT_I4;
      LPOLESTR names[] = {(LPOLESTR)L"Count"};
      pcti->AddFuncDesc(0, &f);
      pcti->SetFuncAndParamNames(0, names, 1);
      pcti->Release();
    }
    if (SUCCEEDED(pctl->CreateTypeInfo((LPOLESTR)L"Colors", TKIND_ENUM, &pcti))) {
      VARDESC v = {};
      VARIANT val;
      val.vt = VT_I4;
      val.lVal = 3;
      v.memid = 0x40000001;
      v.lpvarValue = &val;
      v.varkind = VAR_CONST;
      v.elemdescVar.tdesc.vt = VT_I4;
      pcti->AddVarDesc(0, &v);
      pcti->SetVarName(0, (LPOLESTR)L"Green");
      pcti->Release();
    }
    ITypeLib* ptl = NULL;
    pctl->QueryInterface(IID_ITypeLib, (void**)&ptl);
    ITypeInfo* pti = NULL;
    if (ptl != NULL) ptl->GetTypeInfoOfGuid(iidThing, &pti);
    CHECK(pti != NULL);
    ITypeComp* ptc = NULL;
    if (pti != NULL) CHECK(pti->GetTypeComp(&ptc) == S_OK && ptc != NULL);
    if (ptc != NULL) {
      ITypeInfo* ptiBound = NULL;
      DESCKIND kind = DESCKIND_MAX;
      BINDPTR bp;
      CHECK(ptc->Bind((LPOLESTR)L"count", 0, INVOKE_FUNC, &ptiBound, &kind, &bp) == S_OK);
      CHECK(kind == DESCKIND_FUNCDESC && ptiBound != NULL);
      if (kind == DESCKIND_FUNCDESC && ptiBound != NULL) {
        CHECK(bp.lpfuncdesc->memid == 7);
        ptiBound->ReleaseFuncDesc(bp.lpfuncdesc);
      }
      if (ptiBound != NULL) ptiBound->Release();
      ptiBound = NULL;
      CHECK(ptc->Bind((LPOLESTR)L"Count", 0, INVOKE_PROPERTYPUT, &ptiBound, &kind, &bp) == TYPE_E_TYPEMISMATCH);
      CHECK(ptc->Bind((LPOLESTR)L"Nothing", 0, 0, &ptiBound, &kind, &bp) == S_OK && kind == DESCKIND_NONE &&
            ptiBound == NULL);
      ptc->Release();
    }
    ptc = NULL;
    if (ptl != NULL) CHECK(ptl->GetTypeComp(&ptc) == S_OK && ptc != NULL);
    if (ptc != NULL) {
      ITypeInfo* ptiBound = NULL;
      ITypeComp* ptcBound = NULL;
      CHECK(ptc->BindType((LPOLESTR)L"dthing", 0, &ptiBound, &ptcBound) == S_OK && ptiBound != NULL);
      if (ptiBound != NULL) ptiBound->Release();
      ptiBound = NULL;
      DESCKIND kind = DESCKIND_MAX;
      BINDPTR bp;
      // An enumeration's constants are the library's names.
      CHECK(ptc->Bind((LPOLESTR)L"Green", 0, 0, &ptiBound, &kind, &bp) == S_OK && kind == DESCKIND_VARDESC);
      if (kind == DESCKIND_VARDESC && ptiBound != NULL) {
        CHECK(bp.lpvardesc->lpvarValue && bp.lpvardesc->lpvarValue->lVal == 3);
        ptiBound->ReleaseVarDesc(bp.lpvardesc);
      }
      if (ptiBound != NULL) ptiBound->Release();
      ptiBound = NULL;
      CHECK(ptc->Bind((LPOLESTR)L"Colors", 0, 0, &ptiBound, &kind, &bp) == S_OK && kind == DESCKIND_TYPECOMP);
      if (kind == DESCKIND_TYPECOMP) bp.lptcomp->Release();
      ptc->Release();
    }
    if (pti != NULL) pti->Release();
    if (ptl != NULL) ptl->Release();
    pctl->Release();
  }

  // A record copied with its variant.
  CLongRecord rec;
  LONG lValue = 99;
  VARIANT vRec, vCopy;
  ::VariantInit(&vRec);
  ::VariantInit(&vCopy);
  vRec.vt = VT_RECORD;
  vRec.pvRecord = &lValue;
  vRec.pRecInfo = &rec;
  CHECK(::VariantCopy(&vCopy, &vRec) == S_OK);
  CHECK(vCopy.vt == VT_RECORD && vCopy.pRecInfo == &rec && vCopy.pvRecord != &lValue);
  CHECK(vCopy.pvRecord != NULL && *(LONG*)vCopy.pvRecord == 99 && rec.m_nCopies == 1 && rec.m_nRef == 2);
  PVOID pvCopy = vCopy.pvRecord;
  CHECK(::VariantClear(&vCopy) == S_OK && rec.m_nRef == 1);
  ::CoTaskMemFree(pvCopy);

  // Pictures that are not bitmaps go to files and back.
  PICTDESC pd = {sizeof(PICTDESC), PICTYPE_ICON};
  pd.icon.hicon = ::CopyIcon(::LoadIcon(NULL, IDI_APPLICATION));
  IPicture* pic = NULL;
  CHECK(::OleCreatePictureIndirect(&pd, IID_IPicture, TRUE, (void**)&pic) == S_OK);
  if (pic != NULL) {
    OLE_XSIZE_HIMETRIC cx = 0, cx2 = 0;
    pic->get_Width(&cx);
    LONG cb = 0;
    IPicture* back = RoundTrip(pic, &cb);
    CHECK(back != NULL && cb > 22);
    SHORT type = 0;
    if (back != NULL) {
      CHECK(back->get_Type(&type) == S_OK && type == PICTYPE_ICON);
      back->get_Width(&cx2);
      CHECK(cx2 == cx);
      back->Release();
    }
    pic->Release();
  }
  pd.picType = PICTYPE_ENHMETAFILE;
  pd.emf.hemf = MakeEmf(2540, 1270);
  pic = NULL;
  CHECK(::OleCreatePictureIndirect(&pd, IID_IPicture, TRUE, (void**)&pic) == S_OK);
  if (pic != NULL) {
    LONG cb = 0;
    IPicture* back = RoundTrip(pic, &cb);
    CHECK(back != NULL);
    if (back != NULL) {
      SHORT type = 0;
      OLE_XSIZE_HIMETRIC cx = 0;
      OLE_YSIZE_HIMETRIC cy = 0;
      CHECK(back->get_Type(&type) == S_OK && type == PICTYPE_ENHMETAFILE);
      back->get_Width(&cx);
      back->get_Height(&cy);
      CHECK(cx == 2540 && cy == 1270);
      back->Release();
    }
    pic->Release();
  }
  HDC dcMeta = ::CreateMetaFileW(NULL);
  ::Rectangle(dcMeta, 0, 0, 100, 100);
  pd.picType = PICTYPE_METAFILE;
  pd.wmf.hmeta = ::CloseMetaFile(dcMeta);
  pd.wmf.xExt = 5080;
  pd.wmf.yExt = 2540;
  pic = NULL;
  CHECK(::OleCreatePictureIndirect(&pd, IID_IPicture, TRUE, (void**)&pic) == S_OK);
  if (pic != NULL) {
    LONG cb = 0;
    IPicture* back = RoundTrip(pic, &cb);
    CHECK(back != NULL);
    if (back != NULL) {
      SHORT type = 0;
      OLE_XSIZE_HIMETRIC cx = 0;
      OLE_YSIZE_HIMETRIC cy = 0;
      CHECK(back->get_Type(&type) == S_OK && type == PICTYPE_METAFILE);
      back->get_Width(&cx);
      back->get_Height(&cy);
      CHECK(cx == 5080 && cy == 2540);
      back->Release();
    }
    pic->Release();
  }
  ::OleUninitialize();
}
#endif

#ifdef _WINPORT
static int g_sysColorChanges = 0;
static LRESULT CALLBACK ThemeProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_SYSCOLORCHANGE) g_sysColorChanges++;
  return ::DefWindowProcW(h, m, wp, lp);
}

// The child wintest starts: the colors it found in the registry, put
// back there for the parent.
static int ReportTheme() {
  HKEY k;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WinTest", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) !=
      ERROR_SUCCESS)
    return 1;
  BOOL gradient = FALSE;
  ::SystemParametersInfoW(SPI_GETGRADIENTCAPTIONS, 0, &gradient, 0);
  NONCLIENTMETRICSW ncm = {sizeof ncm};
  ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  DWORD v[7] = {::GetSysColor(COLOR_ACTIVECAPTION),
                ::GetSysColor(COLOR_BTNFACE),
                (DWORD)gradient,
                (DWORD)::GetSystemMetrics(SM_CYCAPTION),
                (DWORD)::GetSystemMetrics(SM_CXFRAME),
                (DWORD)ncm.lfCaptionFont.lfHeight,
                (DWORD)ncm.lfCaptionFont.lfWeight};
  ::RegSetValueExW(k, L"Theme", 0, REG_BINARY, (const BYTE*)v, sizeof v);
  ::RegSetValueExW(k, L"Face", 0, REG_SZ, (const BYTE*)ncm.lfCaptionFont.lfFaceName,
                   (DWORD)((wcslen(ncm.lfCaptionFont.lfFaceName) + 1) * sizeof(WCHAR)));
  ::RegCloseKey(k);
  return 0;
}

// A value of HKCU\subkey, or false.
static bool QueryValue(const wchar_t* sub, const wchar_t* name, DWORD* type, void* data, DWORD cb) {
  HKEY k;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
  bool ok = ::RegQueryValueExW(k, name, NULL, type, (BYTE*)data, &cb) == ERROR_SUCCESS;
  ::RegCloseKey(k);
  return ok;
}

static std::string ReadText(const char* path) {
  std::string t;
  FILE* f = fopen(path, "rb");
  if (!f) return t;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n);
  fclose(f);
  return t;
}

// The classic look: the checker of a scroll bar's trough, a list box's
// scroll bar that shows again when the items no longer fit, and the
// pointed thumb of a trackbar.
static void TestClassicLook() {
  HWND w = ::CreateWindowW(L"STATIC", L"classic", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, NULL, NULL);
  HDC screen = ::GetDC(NULL);
  HDC mem = ::CreateCompatibleDC(screen);
  HBITMAP bm = ::CreateCompatibleBitmap(screen, 200, 60);
  HGDIOBJ old = ::SelectObject(mem, bm);

  // With the standard colors, the trough is a checker of face and
  // highlight.
  HBRUSH trough = (HBRUSH)::DefWindowProcW(w, WM_CTLCOLORSCROLLBAR, (WPARAM)mem, (LPARAM)w);
  RECT r = {0, 0, 8, 8};
  ::FillRect(mem, &r, trough);
  COLORREF face = ::GetSysColor(COLOR_3DFACE) & 0xFFFFFF, hilite = ::GetSysColor(COLOR_3DHILIGHT) & 0xFFFFFF;
  CHECK((PixelAt(mem, 0, 0) == face && PixelAt(mem, 1, 0) == hilite) ||
        (PixelAt(mem, 0, 0) == hilite && PixelAt(mem, 1, 0) == face));
  CHECK(PixelAt(mem, 0, 0) == PixelAt(mem, 1, 1));

  // A list box made empty hides its bar, and shows it again.
  HWND list = ::CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 100, 60, w, NULL, NULL, NULL);
  CHECK(!(::GetWindowLongW(list, GWL_STYLE) & WS_VSCROLL));
  for (int i = 0; i < 20; i++) ::SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"item");
  CHECK(::GetWindowLongW(list, GWL_STYLE) & WS_VSCROLL);
  ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
  CHECK(!(::GetWindowLongW(list, GWL_STYLE) & WS_VSCROLL));
  ::SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"one");
  for (int i = 0; i < 20; i++) ::SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"item");
  CHECK(::GetWindowLongW(list, GWL_STYLE) & WS_VSCROLL);

  // A trackbar's thumb points down to its ticks: the bottom corners of
  // its rectangle are background, and the point is dark.
  HWND track = ::CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, 0, 0, 200, 40, w,
                               NULL, NULL, NULL);
  RECT th = {};
  ::SendMessageW(track, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
  RECT all = {0, 0, 200, 60};
  ::FillRect(mem, &all, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  ::SendMessageW(track, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
  COLORREF dark = ::GetSysColor(COLOR_3DDKSHADOW) & 0xFFFFFF;
  CHECK(PixelAt(mem, th.left, th.bottom - 1) != dark);
  CHECK(PixelAt(mem, th.right - 1, th.bottom - 1) != dark);
  CHECK(PixelAt(mem, (th.left + th.right - 1) / 2, th.bottom - 1) == dark);
  CHECK(PixelAt(mem, th.left, th.top) == hilite);

  ::SelectObject(mem, old);
  ::DeleteObject(bm);
  ::DeleteDC(mem);
  ::ReleaseDC(NULL, screen);
  ::DestroyWindow(w);
}

// The system colors: from HKCU\Control Panel\Colors when a program
// starts, in the registry file as a person writes them; SetSysColors for
// the session; and captions shaded from one color to another.
static void TestTheme() {
  // The registry file keeps strings and numbers readable.
  HKEY k;
  CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) ==
        ERROR_SUCCESS);
  const wchar_t* title = L"10 20 30";
  CHECK(::RegSetValueExW(k, L"ActiveTitle", 0, REG_SZ, (const BYTE*)title, (DWORD)((wcslen(title) + 1) * sizeof(WCHAR))) ==
        ERROR_SUCCESS);
  ::RegCloseKey(k);
  CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) ==
        ERROR_SUCCESS);
  DWORD one = 1;
  CHECK(::RegSetValueExW(k, L"GradientCaptions", 0, REG_DWORD, (const BYTE*)&one, sizeof one) == ERROR_SUCCESS);
  ::RegCloseKey(k);
  // Sizes and fonts: the caption 24 pixels high (in twips, as Windows
  // keeps it), the border 2, and a caption font by name.
  CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k,
                          NULL) == ERROR_SUCCESS);
  const wchar_t* metrics[][2] = {{L"CaptionHeight", L"-360"}, {L"BorderWidth", L"2"}, {L"CaptionFont", L"Tahoma,9,bold"}};
  for (auto& m : metrics)
    CHECK(::RegSetValueExW(k, m[0], 0, REG_SZ, (const BYTE*)m[1], (DWORD)((wcslen(m[1]) + 1) * sizeof(WCHAR))) ==
          ERROR_SUCCESS);
  ::RegCloseKey(k);
  const char* file = getenv("WINPORT_REGISTRY");
  if (file) {
    std::string text = ReadText(file);
    CHECK(text.find("\"ActiveTitle\"=\"10 20 30\"") != std::string::npos);
    CHECK(text.find("\"GradientCaptions\"=dword:00000001") != std::string::npos);
    // A line written by hand is read as a string: the file is replaced, as
    // an editor saves it, and the registry reads it again.
    size_t at = text.find("[HKEY_CURRENT_USER\\Control Panel\\Colors]\n");
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
      text.insert(at + strlen("[HKEY_CURRENT_USER\\Control Panel\\Colors]\n"), "\"ButtonFace\"=\"200 \\\"201\\\" 202\"\n");
      std::string tmp = std::string(file) + ".edit";
      FILE* f = fopen(tmp.c_str(), "wb");
      CHECK(f != NULL);
      if (f) {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
        CHECK(rename(tmp.c_str(), file) == 0);
      }
      WCHAR got[64] = {};
      DWORD type = 0;
      CHECK(QueryValue(L"Control Panel\\Colors", L"ButtonFace", &type, got, sizeof got - sizeof(WCHAR)));
      CHECK(type == REG_SZ && wcscmp(got, L"200 \"201\" 202") == 0);
      // Not a color, so the default would stay; put a real one.
      const wchar_t* face = L"200 201 202";
      CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_ALL_ACCESS, &k) == ERROR_SUCCESS);
      CHECK(::RegSetValueExW(k, L"ButtonFace", 0, REG_SZ, (const BYTE*)face, (DWORD)((wcslen(face) + 1) * sizeof(WCHAR))) ==
            ERROR_SUCCESS);
      ::RegCloseKey(k);
    }
  }

  // A program started now sees them.
  WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16];
  ::GetModuleFileNameW(NULL, exe, MAX_PATH);
  wsprintfW(cmd, L"\"%s\" -theme", exe);
  STARTUPINFOW si = {sizeof si};
  PROCESS_INFORMATION pi = {};
  CHECK(::CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
  if (pi.hProcess) {
    CHECK(::WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    CHECK(code == 0);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    DWORD v[7] = {}, type = 0;
    CHECK(QueryValue(L"Software\\WinTest", L"Theme", &type, v, sizeof v));
    CHECK(v[0] == RGB(10, 20, 30));
    CHECK(v[1] == RGB(200, 201, 202));
    CHECK(v[2] == TRUE);
    CHECK(v[3] == 25);
    CHECK(v[4] == 5);
    CHECK((LONG)v[5] == -12);
    CHECK(v[6] == FW_BOLD);
    WCHAR face[LF_FACESIZE] = {};
    CHECK(QueryValue(L"Software\\WinTest", L"Face", &type, face, sizeof face - sizeof(WCHAR)));
    CHECK(wcscmp(face, L"Tahoma") == 0);
  }
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Colors");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WinTest");

  // SetSysColors: the color, its brush and every top-level window.
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ThemeProc;
  wc.hInstance = ::GetModuleHandleW(NULL);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = L"ThemeTest";
  CHECK(::RegisterClassW(&wc));
  HWND h = ::CreateWindowExW(0, L"ThemeTest", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 100, NULL, NULL,
                             wc.hInstance, NULL);
  CHECK(h != NULL);
  ::SetForegroundWindow(h);
  INT which[2] = {COLOR_ACTIVECAPTION, COLOR_GRADIENTACTIVECAPTION};
  COLORREF was[2] = {::GetSysColor(which[0]), ::GetSysColor(which[1])};
  COLORREF now[2] = {RGB(255, 0, 0), RGB(0, 0, 255)};
  CHECK(::SetSysColors(2, which, now));
  CHECK(g_sysColorChanges == 1);
  CHECK(::GetSysColor(COLOR_ACTIVECAPTION) == RGB(255, 0, 0));
  LOGBRUSH lb = {};
  CHECK(::GetObjectW(::GetSysColorBrush(COLOR_ACTIVECAPTION), sizeof lb, &lb) == sizeof lb);
  CHECK(lb.lbColor == RGB(255, 0, 0));

  // The caption shades from red at the left to blue at the right, or is
  // all red without the gradient.
  BOOL gradient = TRUE;
  CHECK(::SystemParametersInfoW(SPI_GETGRADIENTCAPTIONS, 0, &gradient, 0));
  BOOL before = gradient;
  CHECK(::SystemParametersInfoW(SPI_SETGRADIENTCAPTIONS, 0, (PVOID)TRUE, 0));
  ::UpdateWindow(h);
  int b = ::GetSystemMetrics(SM_CXFRAME), y = b + 2;
  HDC dc = ::GetWindowDC(h);
  COLORREF left = ::GetPixel(dc, b, y), middle = ::GetPixel(dc, 150, y);
  CHECK(GetRValue(left) > 240 && GetBValue(left) < 16);
  CHECK(GetRValue(middle) > 64 && GetBValue(middle) > 64);
  ::ReleaseDC(h, dc);
  CHECK(::SystemParametersInfoW(SPI_SETGRADIENTCAPTIONS, 0, (PVOID)FALSE, 0));
  ::UpdateWindow(h);
  dc = ::GetWindowDC(h);
  CHECK(::GetPixel(dc, 150, y) == RGB(255, 0, 0));
  ::ReleaseDC(h, dc);
  ::SystemParametersInfoW(SPI_SETGRADIENTCAPTIONS, 0, (PVOID)(INT_PTR)before, 0);
  CHECK(::SetSysColors(2, which, was));

  // SPI_SETNONCLIENTMETRICS: a taller caption takes from the client area
  // of a window that is already open.
  NONCLIENTMETRICSW ncm = {sizeof ncm}, saved;
  CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0));
  saved = ncm;
  RECT c0, c1;
  ::GetClientRect(h, &c0);
  int cap = ::GetSystemMetrics(SM_CYCAPTION);
  ncm.iCaptionHeight += 10;
  CHECK(::SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof ncm, &ncm, 0));
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == cap + 10);
  ::GetClientRect(h, &c1);
  CHECK(c1.bottom == c0.bottom - 10);
  CHECK(::SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof saved, &saved, 0));
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == cap);

  // SPIF_UPDATEINIFILE keeps the sizes and fonts in the registry, as
  // Windows writes them.
  ncm.iCaptionHeight = 20;
  CHECK(::SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof ncm, &ncm, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE));
  WCHAR text[64] = {};
  DWORD type = 0;
  CHECK(QueryValue(L"Control Panel\\Desktop\\WindowMetrics", L"CaptionHeight", &type, text, sizeof text - sizeof(WCHAR)));
  CHECK(type == REG_SZ && wcscmp(text, L"-300") == 0);
  memset(text, 0, sizeof text);
  CHECK(QueryValue(L"Control Panel\\Desktop\\WindowMetrics", L"CaptionFont", &type, text, sizeof text - sizeof(WCHAR)));
  CHECK(wcsncmp(text, ncm.lfCaptionFont.lfFaceName, wcslen(ncm.lfCaptionFont.lfFaceName)) == 0);
  CHECK(::SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof saved, &saved, 0));
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop");
  ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel");
  PumpFor(300);
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == cap);

  // The registry file changed while the window is open, as an editor
  // saves it: the window takes the new caption color and height once it
  // looks for messages, and a color the program set itself stays.
  const char* path = getenv("WINPORT_REGISTRY");
  if (path) {
    INT hi = COLOR_HIGHLIGHT;
    COLORREF highlight = ::GetSysColor(COLOR_HIGHLIGHT), mine = RGB(1, 2, 3);
    CHECK(::SetSysColors(1, &hi, &mine));
    int changes = g_sysColorChanges;
    ::GetClientRect(h, &c0);
    std::string edit = ReadText(path);
    edit +=
        "[HKEY_CURRENT_USER\\Control Panel\\Colors]\n\"ActiveTitle\"=\"40 50 60\"\n"
        "[HKEY_CURRENT_USER\\Control Panel\\Desktop\\WindowMetrics]\n\"CaptionHeight\"=\"30\"\n";
    std::string tmp = std::string(path) + ".edit";
    FILE* f = fopen(tmp.c_str(), "wb");
    CHECK(f != NULL);
    if (f) {
      fwrite(edit.data(), 1, edit.size(), f);
      fclose(f);
      CHECK(rename(tmp.c_str(), path) == 0);
    }
    DWORD start = ::GetTickCount();
    while (::GetSystemMetrics(SM_CYCAPTION) != 31 && ::GetTickCount() - start < 5000) PumpFor(20);
    CHECK(::GetSysColor(COLOR_ACTIVECAPTION) == RGB(40, 50, 60));
    CHECK(g_sysColorChanges == changes + 1);
    CHECK(::GetSystemMetrics(SM_CYCAPTION) == 31);
    ::GetClientRect(h, &c1);
    CHECK(c1.bottom == c0.bottom - (31 - cap));
    CHECK(::GetSysColor(COLOR_HIGHLIGHT) == mine);
    // Taken out again, the classic ones come back.
    ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Colors");
    ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics");
    ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel\\Desktop");
    ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Control Panel");
    start = ::GetTickCount();
    while (::GetSystemMetrics(SM_CYCAPTION) != cap && ::GetTickCount() - start < 5000) PumpFor(20);
    CHECK(::GetSystemMetrics(SM_CYCAPTION) == cap);
    CHECK(::GetSysColor(COLOR_ACTIVECAPTION) == was[0]);
    CHECK(::SetSysColors(1, &hi, &highlight));
  }
  ::DestroyWindow(h);
  ::UnregisterClassW(L"ThemeTest", wc.hInstance);
}
#endif

int main() {
#ifdef _WINPORT
  if (strstr(::GetCommandLineA(), " -theme")) return ReportTheme();
#endif
  if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), SW_SHOW)) {
    printf("FAIL: AfxWinInit\n");
    return 1;
  }
  TestClassesAndCreation();
  TestTree();
  TestMessages();
  TestTimers();
  TestThreads();
  TestHooksAndAnsi();
  TestFocusAndPaint();
  TestMemoryDC();
  TestDIBSections();
  TestRegionsAndClipping();
  TestShapes();
  TestText();
  TestIconsAndAccelerators();
  TestMenus();
  TestDialogsAndControls();
  TestMdi();
  TestImageLists();
  TestCommonControls();
  TestMoreControls();
  TestListView();
  TestTreeView();
  TestPropertySheets();
  TestPrinting();
  TestShapesAndDrops();
  TestModalFocus();
  TestTallMessageBox();
  TestSymbolFont();
#ifdef _WINPORT
  TestCaptionIcon();
  TestTheme();
  TestClassicLook();
#endif
  TestCWnd();
#ifdef _WINPORT
  TestClickThroughGroupBox();
  TestWinHelp();
  TestPlgBlt();
  TestDisplayAndDrawing();
#ifdef _WINPORT
  TestLayeredWindows();
#endif
  TestDecimal();
  TestShellNamespace();
  TestOleBits();
#endif
  printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
