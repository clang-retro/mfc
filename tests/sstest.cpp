// sstest.cpp - a screen saver over scrnsave.h, run with /s: the library's
// WinMain makes the window over the whole screen, a timer draws, and a
// key press (sent by the test) closes it through DefScreenSaverProc.
// Linked with libscrnsave.a (libscrnsavw.a for UNICODE).
#include <windows.h>
#include <scrnsave.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_ticks = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// Draws a few lines, and after three ticks presses a key, which must
// close the screen saver.
LRESULT WINAPI ScreenSaverProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      CHECK(!fChildPreview);
      CHECK(hMainInstance != NULL);
      CHECK(lstrcmp(szName, TEXT("Test Saver")) == 0);
      SetTimer(hwnd, 1, 20, NULL);
      return 0;
    }
    case WM_TIMER: {
      RECT r;
      GetClientRect(hwnd, &r);
      if (g_ticks == 0) {
        CHECK(hwnd == hMainWindow);
        CHECK(r.right == GetSystemMetrics(SM_CXSCREEN) && r.bottom == GetSystemMetrics(SM_CYSCREEN));
      }
      HDC dc = GetDC(hwnd);
      HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
      HGDIOBJ old = SelectObject(dc, pen);
      MoveToEx(dc, 0, g_ticks * 10, NULL);
      LineTo(dc, r.right, r.bottom - g_ticks * 10);
      SelectObject(dc, old);
      DeleteObject(pen);
      ReleaseDC(hwnd, dc);
      if (++g_ticks == 3) PostMessage(hwnd, WM_KEYDOWN, VK_SPACE, 0);
      CHECK(g_ticks <= 4);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(hwnd, 1);
      CHECK(g_ticks >= 3);
      printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
      fflush(stdout);
      PostQuitMessage(g_failures ? 1 : 0);
      return 0;
  }
  return DefScreenSaverProc(hwnd, msg, wp, lp);
}

// The configuration dialog: OK closes it.
BOOL WINAPI ScreenSaverConfigureDialog(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
  if (msg == WM_INITDIALOG) return TRUE;
  if (msg == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
    EndDialog(dlg, LOWORD(wp));
    return TRUE;
  }
  return FALSE;
}

// No classes for the configuration dialog.
BOOL WINAPI RegisterDialogClasses(HANDLE) { return TRUE; }
