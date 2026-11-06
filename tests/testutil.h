// testutil.h - helpers for the self-driving GUI tests.
//
// Each test program runs by itself: timers move it through its steps,
// it records CHECK failures in a log file, saves screenshots of its
// windows as .bmp files, and exits with status 1 if anything failed.
// Output goes to the directory named by MFC_TEST_OUT (default ".").
#pragma once
#include <stdio.h>

static int g_nFailures = 0;
static FILE* g_pLog = NULL;

static CString TestOutPath(LPCTSTR lpszName) {
  TCHAR szDir[MAX_PATH];
  DWORD n = ::GetEnvironmentVariable(_T("MFC_TEST_OUT"), szDir, MAX_PATH);
  CString strPath = (n > 0 && n < MAX_PATH) ? szDir : _T(".");
  if (strPath.Right(1) != _T("\\") && strPath.Right(1) != _T("/")) strPath += _T("\\");
  return strPath + lpszName;
}

static void TestOpenLog(LPCTSTR lpszName) {
  CString strPath = TestOutPath(lpszName);
  g_pLog = _tfopen(strPath, _T("w"));
}

static void TestLog(const char* pszFormat, ...) {
  va_list args;
  va_start(args, pszFormat);
  if (g_pLog != NULL) {
    vfprintf(g_pLog, pszFormat, args);
    fputc('\n', g_pLog);
    fflush(g_pLog);
  }
  va_end(args);
}

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      TestLog("FAIL %s:%d: %s", __FILE__, __LINE__, #cond);              \
      g_nFailures++;                                                     \
    }                                                                    \
  } while (0)

static int TestFinish() {
  TestLog("%s: %d failure(s)", g_nFailures ? "FAIL" : "PASS", g_nFailures);
  if (g_pLog != NULL) fclose(g_pLog);
  g_pLog = NULL;
  return g_nFailures ? 1 : 0;
}

// Saves what is on the screen inside a window (frame included) as a
// 24-bit .bmp file.
static BOOL TestSaveWindowBitmap(HWND hWnd, LPCTSTR lpszName) {
  ::UpdateWindow(hWnd);
  RECT rc;
  ::GetWindowRect(hWnd, &rc);
  int cx = rc.right - rc.left, cy = rc.bottom - rc.top;
  if (cx <= 0 || cy <= 0) return FALSE;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
  BITMAPINFO bmi;
  memset(&bmi, 0, sizeof(bmi));
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = cx;
  bmi.bmiHeader.biHeight = cy;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 24;
  void* pBits = NULL;
  HBITMAP hbm = ::CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
  HGDIOBJ hOld = ::SelectObject(hdcMem, hbm);
  ::BitBlt(hdcMem, 0, 0, cx, cy, hdcScreen, rc.left, rc.top, SRCCOPY);
  ::GdiFlush();
  ::SelectObject(hdcMem, hOld);

  BOOL bOK = FALSE;
  int nStride = (cx * 3 + 3) & ~3;
  DWORD cbBits = (DWORD)nStride * cy;
  FILE* f = _tfopen(TestOutPath(lpszName), _T("wb"));
  if (f != NULL) {
    BITMAPFILEHEADER bfh;
    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + cbBits;
    fwrite(&bfh, sizeof(bfh), 1, f);
    fwrite(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
    fwrite(pBits, cbBits, 1, f);
    fclose(f);
    bOK = TRUE;
  }
  ::DeleteObject(hbm);
  ::DeleteDC(hdcMem);
  ::ReleaseDC(NULL, hdcScreen);
  return bOK;
}

// Lets pending messages (paint, idle updates) run.
static void TestPump() {
  MSG msg;
  for (int i = 0; i < 50 && ::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE); i++) {
    if (msg.message == WM_QUIT) break;
    AfxPumpMessage();
  }
  AfxGetApp()->OnIdle(0);
  AfxGetApp()->OnIdle(1);
}
