// wintest.cpp - console tests for windows and drawing below the MFC
// classes: window classes and windows, the window tree and coordinates,
// sending and posting, the message loop, timers, messages between
// threads, hooks, ANSI and Unicode window procedures, focus, painting,
// device contexts, bitmaps and DIBs, raster operations, regions,
// clipping, mapping modes and scrolling, lines, shapes, paths and
// text, icons, accelerators and menus, dialogs and the standard
// controls; and a CWnd with a message map
// on top.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
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

int main() {
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
  TestCWnd();
  printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
