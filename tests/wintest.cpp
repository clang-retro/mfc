// wintest.cpp - console tests for windows and drawing below the MFC
// classes: window classes and windows, the window tree and coordinates,
// sending and posting, the message loop, timers, messages between
// threads, hooks, ANSI and Unicode window procedures, focus, painting,
// device contexts, bitmaps and DIBs, raster operations, regions,
// clipping, mapping modes and scrolling; and a CWnd with a message map
// on top.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <stdio.h>
#include <string.h>
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
  TestCWnd();
  printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
