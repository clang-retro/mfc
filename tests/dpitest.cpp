// dpitest.cpp - tests for the portable layer's dots per inch: a
// DPI-aware program's LOGPIXELSX, system metrics, fonts and controls at
// the desktop's scale; a program that is not aware; the compatibility
// setting and LogPixels in the registry; and the functions that tell the
// awareness and DPI. The DPI is fixed for a process, so each case runs in
// a child with its own scale (WINPORT_SCALE) and registry.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <shellscalingapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

static void SetText(HKEY root, const wchar_t* key, const wchar_t* name, const wchar_t* value) {
  HKEY k;
  CHECK(::RegCreateKeyExW(root, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) == ERROR_SUCCESS);
  CHECK(::RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t))) ==
        ERROR_SUCCESS);
  ::RegCloseKey(k);
}

static int ScreenDpi() {
  HDC dc = ::GetDC(NULL);
  int dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
  CHECK(::GetDeviceCaps(dc, LOGPIXELSX) == dpi);
  ::ReleaseDC(NULL, dc);
  return dpi;
}

// The white inside of a check box drawn in a window, as wide as it is.
static int CheckBoxInside() {
  HWND parent = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_VISIBLE, 0, 0, 200, 80, NULL, NULL, NULL, NULL);
  HWND box = ::CreateWindowExW(0, L"BUTTON", L"Check", WS_CHILD | WS_VISIBLE | BS_CHECKBOX, 10, 10, 150, 60, parent,
                               NULL, NULL, NULL);
  ::UpdateWindow(parent);
  ::UpdateWindow(box);
  HDC dc = ::GetDC(box);
  // Along the box's middle row: the run of the window color.
  int y = 30, run = 0, best = 0;
  for (int x = 0; x < 60; x++) {
    if (::GetPixel(dc, x, y) == ::GetSysColor(COLOR_WINDOW)) {
      run++;
      if (run > best) best = run;
    } else {
      run = 0;
    }
  }
  ::ReleaseDC(box, dc);
  ::DestroyWindow(parent);
  return best;
}

// A DPI-aware program on a desktop scaled by two.
static void Aware() {
  CHECK(::IsProcessDPIAware());
  CHECK(ScreenDpi() == 192);
  CHECK(::GetDpiForSystem() == 192);
  CHECK(::GetSystemMetrics(SM_CXICON) == 64);
  CHECK(::GetSystemMetrics(SM_CXSMICON) == 32);
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == 37);
  CHECK(::GetSystemMetrics(SM_CXVSCROLL) == 32);
  CHECK(::GetSystemMetrics(SM_CXMENUCHECK) == 26);
  CHECK(::GetSystemMetrics(SM_CXBORDER) == 1);
  CHECK(::GetSystemMetricsForDpi(SM_CXICON, 96) == 32);
  CHECK(::GetSystemMetricsForDpi(SM_CXSCREEN, 96) == ::GetSystemMetrics(SM_CXSCREEN));
  NONCLIENTMETRICSW m = {sizeof m};
  CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0));
  CHECK(m.lfMessageFont.lfHeight == -21);  // 8 points
  CHECK(m.iCaptionHeight == 36);
  CHECK(::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, 96));
  CHECK(m.lfMessageFont.lfHeight == -11 && m.iCaptionHeight == 18);
  // The frame grows with the caption.
  RECT r = {0, 0, 100, 100};
  CHECK(::AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0));
  CHECK(-r.top >= 37);
  RECT s = {0, 0, 100, 100};
  CHECK(::AdjustWindowRectExForDpi(&s, WS_OVERLAPPEDWINDOW, FALSE, 0, 96));
  CHECK(-s.top < -r.top);
  // MFC's point sizes use the screen's DPI.
  CFont f;
  CHECK(f.CreatePointFont(100, _T("Arial")));
  LOGFONT lf;
  f.GetLogFont(&lf);
  CHECK(lf.lfHeight == -27 || lf.lfHeight == 27);  // 10 points at 192
  // A window and its controls.
  HWND h = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
  CHECK(::GetDpiForWindow(h) == 192);
  CHECK(::GetAwarenessFromDpiAwarenessContext(::GetWindowDpiAwarenessContext(h)) == DPI_AWARENESS_SYSTEM_AWARE);
  ::DestroyWindow(h);
  CHECK(CheckBoxInside() == 26 - 4);
  // A dialog's units come from its font, which is twice the size.
  CHECK(LOWORD(::GetDialogBaseUnits()) >= 10);
  // The awareness is fixed: asking for another fails.
  CHECK(::SetProcessDPIAware());
  CHECK(!::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE));
  CHECK(::GetLastError() == ERROR_ACCESS_DENIED);
  CHECK(::SetProcessDpiAwareness(PROCESS_DPI_UNAWARE) == E_ACCESSDENIED);
  PROCESS_DPI_AWARENESS pa;
  CHECK(::GetProcessDpiAwareness(NULL, &pa) == S_OK && pa == PROCESS_SYSTEM_DPI_AWARE);
  UINT x = 0, y = 0;
  CHECK(::GetDpiForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &x, &y) ==
            S_OK &&
        x == 192 && y == 192);
  DEVICE_SCALE_FACTOR sf;
  CHECK(::GetScaleFactorForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &sf) == S_OK &&
        sf == SCALE_200_PERCENT);
}

// A program that asks not to be aware sees 96 dots per inch.
static void Unaware() {
  CHECK(::SetProcessDpiAwareness(PROCESS_DPI_UNAWARE) == S_OK);
  CHECK(!::IsProcessDPIAware());
  CHECK(ScreenDpi() == 96);
  CHECK(::GetSystemMetrics(SM_CXICON) == 32);
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == 19);
  CHECK(::GetAwarenessFromDpiAwarenessContext(::GetThreadDpiAwarenessContext()) == DPI_AWARENESS_UNAWARE);
  CHECK(CheckBoxInside() == 13 - 4);
}

// The compatibility setting for the program's file wins over what it
// asks for.
static void Compat() {
  wchar_t exe[MAX_PATH];
  ::GetModuleFileNameW(NULL, exe, MAX_PATH);
  SetText(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe,
          L"~ DPIUNAWARE");
  CHECK(!::SetProcessDPIAware());
  CHECK(ScreenDpi() == 96);
  CHECK(!::IsProcessDPIAware());
}

// The DPI in the registry, and sizes given there in twips and pixels.
static void LogPixels() {
  HKEY k;
  CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) ==
        ERROR_SUCCESS);
  DWORD v = 144;
  CHECK(::RegSetValueExW(k, L"LogPixels", 0, REG_DWORD, (const BYTE*)&v, sizeof v) == ERROR_SUCCESS);
  ::RegCloseKey(k);
  const wchar_t* metrics = L"Control Panel\\Desktop\\WindowMetrics";
  SetText(HKEY_CURRENT_USER, metrics, L"CaptionHeight", L"-300");  // twips: 30 pixels at 144
  SetText(HKEY_CURRENT_USER, metrics, L"MenuHeight", L"20");      // pixels at 96
  SetText(HKEY_CURRENT_USER, metrics, L"MenuFont", L"Arial,10");
  CHECK(ScreenDpi() == 144);
  CHECK(::GetSystemMetrics(SM_CYCAPTION) == 31);
  CHECK(::GetSystemMetrics(SM_CYMENU) == 31);
  CHECK(::GetSystemMetrics(SM_CXICON) == 48);
  NONCLIENTMETRICSW m = {sizeof m};
  CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0));
  CHECK(m.lfMenuFont.lfHeight == -20);  // 10 points at 144
  // Saved back as Windows writes them: twips, and points.
  CHECK(::SystemParametersInfoW(SPI_SETNONCLIENTMETRICS, sizeof m, &m, SPIF_UPDATEINIFILE));
  CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, metrics, 0, KEY_READ, &k) == ERROR_SUCCESS);
  wchar_t buf[64];
  DWORD type, cb = sizeof buf;
  CHECK(::RegQueryValueExW(k, L"CaptionHeight", NULL, &type, (BYTE*)buf, &cb) == ERROR_SUCCESS &&
        wcscmp(buf, L"-300") == 0);
  cb = sizeof buf;
  CHECK(::RegQueryValueExW(k, L"MenuFont", NULL, &type, (BYTE*)buf, &cb) == ERROR_SUCCESS &&
        wcscmp(buf, L"Arial,10") == 0);
  ::RegCloseKey(k);
}

// A fractional scale.
static void Fraction() {
  CHECK(ScreenDpi() == 120);
  CHECK(::GetSystemMetrics(SM_CXICON) == 40);
  CHECK(CheckBoxInside() == 16 - 4);
}

static int RunChild(const char* name, const char* scale) {
  char exe[MAX_PATH];
  ::GetModuleFileNameA(NULL, exe, MAX_PATH);
  const char* out = getenv("MFC_TEST_OUT");
  char reg[MAX_PATH];
  snprintf(reg, sizeof reg, "%s/dpi-%s.reg", out ? out : ".", name);
  remove(reg);
  setenv("WINPORT_REGISTRY", reg, 1);
  setenv("WINPORT_SCALE", scale, 1);
  char cmd[MAX_PATH * 2];
  snprintf(cmd, sizeof cmd, "\"%s\" -%s", exe, name);
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (!::CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1000;
  ::WaitForSingleObject(pi.hProcess, 60000);
  DWORD code = 1001;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  printf("%s: %lu failure(s)\n", name, (unsigned long)code);
  return (int)code;
}

int main(int argc, char** argv) {
  if (argc > 1 && argv[1][0] == '-') {
    const char* c = argv[1] + 1;
    if (!strcmp(c, "aware")) Aware();
    else if (!strcmp(c, "unaware")) Unaware();
    else if (!strcmp(c, "compat")) Compat();
    else if (!strcmp(c, "logpixels")) LogPixels();
    else if (!strcmp(c, "fraction")) Fraction();
    else return 99;
    fflush(stdout);
    return g_failures;
  }
  g_failures += RunChild("aware", "2");
  g_failures += RunChild("unaware", "2");
  g_failures += RunChild("compat", "2");
  g_failures += RunChild("logpixels", "1");
  g_failures += RunChild("fraction", "1.25");
  printf(g_failures ? "FAIL: %d failure(s)\n" : "PASS: %d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
