// uxtheme.cpp - visual styles, off: the controls draw the classic look,
// so no theme opens and the calls that need one fail with E_HANDLE, as
// on Windows with visual styles turned off. What works without a theme
// does: the system colors, sizes and fonts, and a parent's background
// drawn behind a control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <uxtheme.h>

#include <atomic>

namespace {
std::atomic<DWORD> g_appProperties{STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT};
}

extern "C" {

HTHEME WINAPI OpenThemeData(HWND, LPCWSTR) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return NULL;
}
HTHEME WINAPI OpenThemeDataEx(HWND h, LPCWSTR classes, DWORD) { return OpenThemeData(h, classes); }
HRESULT WINAPI CloseThemeData(HTHEME t) { return t ? E_HANDLE : S_OK; }
HTHEME WINAPI GetWindowTheme(HWND) { return NULL; }
HRESULT WINAPI SetWindowTheme(HWND h, LPCWSTR, LPCWSTR) { return IsWindow(h) ? S_OK : E_HANDLE; }
HRESULT WINAPI EnableThemeDialogTexture(HWND h, DWORD) { return IsWindow(h) ? S_OK : E_HANDLE; }
BOOL WINAPI IsThemeDialogTextureEnabled(HWND) { return FALSE; }
BOOL WINAPI IsThemeActive(void) { return FALSE; }
BOOL WINAPI IsAppThemed(void) { return FALSE; }
DWORD WINAPI GetThemeAppProperties(void) { return g_appProperties; }
void WINAPI SetThemeAppProperties(DWORD f) { g_appProperties = f; }

BOOL WINAPI IsThemePartDefined(HTHEME, int, int) { return FALSE; }
BOOL WINAPI IsThemeBackgroundPartiallyTransparent(HTHEME, int, int) { return FALSE; }
HRESULT WINAPI DrawThemeBackground(HTHEME, HDC, int, int, LPCRECT, LPCRECT) { return E_HANDLE; }
HRESULT WINAPI DrawThemeBackgroundEx(HTHEME, HDC, int, int, LPCRECT, const DTBGOPTS*) { return E_HANDLE; }
HRESULT WINAPI DrawThemeText(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, LPCRECT) { return E_HANDLE; }
HRESULT WINAPI DrawThemeEdge(HTHEME, HDC, int, int, LPCRECT, UINT, UINT, LPRECT) { return E_HANDLE; }
HRESULT WINAPI DrawThemeIcon(HTHEME, HDC, int, int, LPCRECT, HIMAGELIST, int) { return E_HANDLE; }
HRESULT WINAPI GetThemeBackgroundContentRect(HTHEME, HDC, int, int, LPCRECT, LPRECT) { return E_HANDLE; }
HRESULT WINAPI GetThemeBackgroundExtent(HTHEME, HDC, int, int, LPCRECT, LPRECT) { return E_HANDLE; }
HRESULT WINAPI GetThemePartSize(HTHEME, HDC, int, int, LPCRECT, THEME_SIZE, SIZE*) { return E_HANDLE; }
HRESULT WINAPI GetThemeTextExtent(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPCRECT, LPRECT) { return E_HANDLE; }
HRESULT WINAPI GetThemeColor(HTHEME, int, int, int, COLORREF*) { return E_HANDLE; }
HRESULT WINAPI GetThemeMetric(HTHEME, HDC, int, int, int, int*) { return E_HANDLE; }
HRESULT WINAPI GetThemeMargins(HTHEME, HDC, int, int, int, LPCRECT, MARGINS*) { return E_HANDLE; }
HRESULT WINAPI GetThemeInt(HTHEME, int, int, int, int*) { return E_HANDLE; }
HRESULT WINAPI GetThemeBool(HTHEME, int, int, int, BOOL*) { return E_HANDLE; }
HRESULT WINAPI GetThemeFont(HTHEME, HDC, int, int, int, LOGFONTW*) { return E_HANDLE; }
HRESULT WINAPI GetThemeRect(HTHEME, int, int, int, LPRECT) { return E_HANDLE; }
HRESULT WINAPI GetThemeString(HTHEME, int, int, int, LPWSTR, int) { return E_HANDLE; }
HRESULT WINAPI GetThemeSysInt(HTHEME, int, int*) { return E_HANDLE; }
HRESULT WINAPI HitTestThemeBackground(HTHEME, HDC, int, int, DWORD, LPCRECT, HRGN, POINT, WORD*) {
  return E_HANDLE;
}

// With no theme, the system's own values.
COLORREF WINAPI GetThemeSysColor(HTHEME, int i) { return GetSysColor(i >= TMT_SCROLLBAR ? i - TMT_SCROLLBAR : i); }
HBRUSH WINAPI GetThemeSysColorBrush(HTHEME, int i) {
  return GetSysColorBrush(i >= TMT_SCROLLBAR ? i - TMT_SCROLLBAR : i);
}
int WINAPI GetThemeSysSize(HTHEME, int i) { return GetSystemMetrics(i); }
BOOL WINAPI GetThemeSysBool(HTHEME, int i) {
  BOOL b = FALSE;
  if (i == TMT_FLATMENUS) SystemParametersInfoW(SPI_GETFLATMENU, 0, &b, 0);
  return b;
}
HRESULT WINAPI GetThemeSysFont(HTHEME, int i, LOGFONTW* lf) {
  if (!lf) return E_POINTER;
  if (i == TMT_ICONTITLEFONT) return SystemParametersInfoW(SPI_GETICONTITLELOGFONT, sizeof *lf, lf, 0) ? S_OK : E_FAIL;
  NONCLIENTMETRICSW m = {sizeof m};
  if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) return E_FAIL;
  switch (i) {
    case TMT_CAPTIONFONT: *lf = m.lfCaptionFont; return S_OK;
    case TMT_SMALLCAPTIONFONT: *lf = m.lfSmCaptionFont; return S_OK;
    case TMT_MENUFONT: *lf = m.lfMenuFont; return S_OK;
    case TMT_STATUSFONT: *lf = m.lfStatusFont; return S_OK;
    case TMT_MSGBOXFONT: *lf = m.lfMessageFont; return S_OK;
  }
  return E_INVALIDARG;
}

// The parent's background and content behind the child's rectangle, so
// a control can look see-through.
HRESULT WINAPI DrawThemeParentBackgroundEx(HWND h, HDC dc, DWORD, const RECT* rc) {
  HWND parent = GetParent(h);
  if (!parent || !dc) return E_HANDLE;
  POINT p = {0, 0};
  MapWindowPoints(h, parent, &p, 1);
  POINT old;
  OffsetViewportOrgEx(dc, -p.x, -p.y, &old);
  int saved = SaveDC(dc);
  if (rc) {
    RECT r = *rc;
    OffsetRect(&r, p.x, p.y);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
  }
  SendMessageW(parent, WM_ERASEBKGND, (WPARAM)dc, 0);
  SendMessageW(parent, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
  RestoreDC(dc, saved);
  SetViewportOrgEx(dc, old.x, old.y, NULL);
  return S_OK;
}
HRESULT WINAPI DrawThemeParentBackground(HWND h, HDC dc, const RECT* rc) {
  return DrawThemeParentBackgroundEx(h, dc, 0, rc);
}

HRESULT WINAPI BufferedPaintInit(void) { return S_OK; }
HRESULT WINAPI BufferedPaintUnInit(void) { return S_OK; }

}  // extern "C"
