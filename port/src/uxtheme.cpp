// uxtheme.cpp - visual styles. A program that asks for them (see
// theme.h) gets the built-in theme: OpenThemeData opens its classes, and
// the drawing and measuring calls answer from theme.cpp. Otherwise the
// controls draw the classic look, no theme opens and the calls that need
// one fail with E_HANDLE, as on Windows with visual styles off. What works
// without a theme does: the system colors, sizes and fonts, and a
// parent's background drawn behind a control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <wchar.h>

#include "theme.h"

using winport::theme::Class;

namespace {
std::atomic<DWORD> g_appProperties{STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS | STAP_ALLOW_WEBCONTENT};

// An open theme: its class and the window it was opened for.
struct ThemeData {
  DWORD magic = 0x54484D45;  // "THME"
  Class cls;
  HWND hwnd;
};

// The open themes, and each window's last one.
std::mutex g_lock;
std::set<ThemeData*> g_open;
std::map<HWND, ThemeData*> g_byWindow;
// Windows whose theme SetWindowTheme turned off.
std::set<HWND> g_off;

ThemeData* DataOf(HTHEME t) {
  std::lock_guard<std::mutex> g(g_lock);
  ThemeData* d = (ThemeData*)t;
  return g_open.count(d) ? d : NULL;
}

}  // namespace

namespace winport {
namespace theme {

bool Themed(HWND h) {
  if (!AppThemed() || !(g_appProperties & STAP_ALLOW_CONTROLS)) return false;
  std::lock_guard<std::mutex> g(g_lock);
  return !g_off.count(h);
}

}  // namespace theme
}  // namespace winport

extern "C" {

HTHEME WINAPI OpenThemeData(HWND h, LPCWSTR classes) {
  if (!winport::theme::AppThemed() || !(g_appProperties & STAP_ALLOW_CONTROLS) || !classes) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return NULL;
  }
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (h && g_off.count(h)) {
      SetLastError(ERROR_NOT_SUPPORTED);
      return NULL;
    }
  }
  // The first class of the list (separated by semicolons) that the
  // theme has.
  std::wstring list(classes);
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find(L';', start);
    std::wstring name = list.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    while (!name.empty() && name[0] == L' ') name.erase(0, 1);
    while (!name.empty() && name.back() == L' ') name.pop_back();
    Class c = winport::theme::ClassOf(name.c_str());
    if (c != winport::theme::kNoClass) {
      ThemeData* d = new ThemeData;
      d->cls = c;
      d->hwnd = h;
      std::lock_guard<std::mutex> g(g_lock);
      g_open.insert(d);
      if (h) g_byWindow[h] = d;
      return (HTHEME)d;
    }
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  SetLastError(ERROR_NOT_FOUND);
  return NULL;
}
HTHEME WINAPI OpenThemeDataEx(HWND h, LPCWSTR classes, DWORD) { return OpenThemeData(h, classes); }
HTHEME WINAPI OpenThemeDataForDpi(HWND h, LPCWSTR classes, UINT) { return OpenThemeData(h, classes); }
HRESULT WINAPI CloseThemeData(HTHEME t) {
  if (!t) return S_OK;
  std::lock_guard<std::mutex> g(g_lock);
  ThemeData* d = (ThemeData*)t;
  if (!g_open.erase(d)) return E_HANDLE;
  for (auto it = g_byWindow.begin(); it != g_byWindow.end();)
    if (it->second == d)
      it = g_byWindow.erase(it);
    else
      ++it;
  delete d;
  return S_OK;
}
HTHEME WINAPI GetWindowTheme(HWND h) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = g_byWindow.find(h);
  return it == g_byWindow.end() ? NULL : (HTHEME)it->second;
}
// An empty application name and class list turn the window's theme off;
// anything else turns it back on. The window hears WM_THEMECHANGED.
HRESULT WINAPI SetWindowTheme(HWND h, LPCWSTR app, LPCWSTR classes) {
  if (!IsWindow(h)) return E_HANDLE;
  bool off = app && classes && (!*app || !wcscmp(app, L" ")) && (!*classes || !wcscmp(classes, L" "));
  {
    std::lock_guard<std::mutex> g(g_lock);
    if (off)
      g_off.insert(h);
    else
      g_off.erase(h);
  }
  SendMessageW(h, WM_THEMECHANGED, 0, 0);
  InvalidateRect(h, NULL, TRUE);
  return S_OK;
}
HRESULT WINAPI EnableThemeDialogTexture(HWND h, DWORD) { return IsWindow(h) ? S_OK : E_HANDLE; }
BOOL WINAPI IsThemeDialogTextureEnabled(HWND) { return FALSE; }
BOOL WINAPI IsThemeActive(void) { return winport::theme::AppThemed(); }
BOOL WINAPI IsAppThemed(void) { return winport::theme::AppThemed() && (g_appProperties & STAP_ALLOW_CONTROLS); }
DWORD WINAPI GetThemeAppProperties(void) { return g_appProperties; }
void WINAPI SetThemeAppProperties(DWORD f) { g_appProperties = f; }

BOOL WINAPI IsThemePartDefined(HTHEME t, int part, int) {
  ThemeData* d = DataOf(t);
  return d && winport::theme::HasPart(d->cls, part);
}
BOOL WINAPI IsThemeBackgroundPartiallyTransparent(HTHEME t, int part, int state) {
  ThemeData* d = DataOf(t);
  return d && winport::theme::PartTransparent(d->cls, part, state);
}
HRESULT WINAPI DrawThemeBackground(HTHEME t, HDC dc, int part, int state, LPCRECT r, LPCRECT clip) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!dc || !r) return E_POINTER;
  return winport::theme::DrawPart(d->cls, dc, part, state, *r, clip) ? S_OK : E_INVALIDARG;
}
HRESULT WINAPI DrawThemeBackgroundEx(HTHEME t, HDC dc, int part, int state, LPCRECT r, const DTBGOPTS* o) {
  const RECT* clip = o && (o->dwFlags & DTBG_CLIPRECT) ? &o->rcClip : NULL;
  if (o && (o->dwFlags & DTBG_OMITCONTENT) && !(o->dwFlags & DTBG_OMITBORDER)) {
    // The border only: draw, then the content's place stays as it was.
    ThemeData* d = DataOf(t);
    if (!d) return E_HANDLE;
    if (!dc || !r) return E_POINTER;
    RECT in = winport::theme::ContentRect(d->cls, part, state, *r);
    int saved = SaveDC(dc);
    ExcludeClipRect(dc, in.left, in.top, in.right, in.bottom);
    HRESULT hr = DrawThemeBackground(t, dc, part, state, r, clip);
    RestoreDC(dc, saved);
    return hr;
  }
  if (o && (o->dwFlags & DTBG_OMITBORDER)) {
    ThemeData* d = DataOf(t);
    if (!d) return E_HANDLE;
    if (!dc || !r) return E_POINTER;
    RECT in = winport::theme::ContentRect(d->cls, part, state, *r);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, in.left, in.top, in.right, in.bottom);
    HRESULT hr = DrawThemeBackground(t, dc, part, state, r, clip);
    RestoreDC(dc, saved);
    return hr;
  }
  return DrawThemeBackground(t, dc, part, state, r, clip);
}
HRESULT WINAPI DrawThemeText(HTHEME t, HDC dc, int part, int state, LPCWSTR text, int n, DWORD flags, DWORD flags2,
                             LPCRECT r) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!dc || !text || !r) return E_POINTER;
  COLORREF c = GetTextColor(dc);
  winport::theme::Color(d->cls, part, state, TMT_TEXTCOLOR, &c);
  if (flags2 & DTT_GRAYED) c = GetSysColor(COLOR_GRAYTEXT);
  COLORREF old = SetTextColor(dc, c);
  int mode = SetBkMode(dc, TRANSPARENT);
  RECT box = *r;
  DrawTextW(dc, text, n, &box, flags);
  SetBkMode(dc, mode);
  SetTextColor(dc, old);
  return S_OK;
}
HRESULT WINAPI DrawThemeEdge(HTHEME t, HDC dc, int, int, LPCRECT r, UINT edge, UINT flags, LPRECT content) {
  if (!DataOf(t)) return E_HANDLE;
  if (!dc || !r) return E_POINTER;
  RECT box = *r;
  DrawEdge(dc, &box, edge, flags | (content ? BF_ADJUST : 0));
  if (content) *content = box;
  return S_OK;
}
HRESULT WINAPI DrawThemeIcon(HTHEME t, HDC dc, int, int, LPCRECT r, HIMAGELIST il, int i) {
  if (!DataOf(t)) return E_HANDLE;
  if (!dc || !r || !il) return E_POINTER;
  int cx = 0, cy = 0;
  ImageList_GetIconSize(il, &cx, &cy);
  return ImageList_Draw(il, i, dc, (r->left + r->right - cx) / 2, (r->top + r->bottom - cy) / 2, ILD_TRANSPARENT)
             ? S_OK
             : E_FAIL;
}
HRESULT WINAPI GetThemeBackgroundContentRect(HTHEME t, HDC, int part, int state, LPCRECT r, LPRECT out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!r || !out) return E_POINTER;
  *out = winport::theme::ContentRect(d->cls, part, state, *r);
  return S_OK;
}
HRESULT WINAPI GetThemeBackgroundExtent(HTHEME t, HDC, int part, int state, LPCRECT content, LPRECT out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!content || !out) return E_POINTER;
  RECT big = {0, 0, 1000, 1000};
  RECT in = winport::theme::ContentRect(d->cls, part, state, big);
  *out = RECT{content->left - in.left, content->top - in.top, content->right + (1000 - in.right),
              content->bottom + (1000 - in.bottom)};
  return S_OK;
}
HRESULT WINAPI GetThemePartSize(HTHEME t, HDC, int part, int state, LPCRECT r, THEME_SIZE which, SIZE* out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!out) return E_POINTER;
  if (winport::theme::PartSize(d->cls, part, state, r, which, out)) return S_OK;
  if (which == TS_DRAW && r) {
    *out = SIZE{r->right - r->left, r->bottom - r->top};
    return S_OK;
  }
  *out = SIZE{0, 0};
  return E_INVALIDARG;
}
HRESULT WINAPI GetThemeTextExtent(HTHEME t, HDC dc, int, int, LPCWSTR text, int n, DWORD flags, LPCRECT bound,
                                  LPRECT out) {
  if (!DataOf(t)) return E_HANDLE;
  if (!dc || !text || !out) return E_POINTER;
  RECT r = bound ? *bound : RECT{0, 0, 0, 0};
  DrawTextW(dc, text, n, &r, flags | DT_CALCRECT);
  *out = r;
  return S_OK;
}
HRESULT WINAPI GetThemeColor(HTHEME t, int part, int state, int prop, COLORREF* out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!out) return E_POINTER;
  return winport::theme::Color(d->cls, part, state, prop, out) ? S_OK : E_PROP_ID_UNSUPPORTED;
}
HRESULT WINAPI GetThemeMetric(HTHEME t, HDC, int part, int state, int prop, int* out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!out) return E_POINTER;
  return winport::theme::Int(d->cls, part, state, prop, out) ? S_OK : E_PROP_ID_UNSUPPORTED;
}
HRESULT WINAPI GetThemeMargins(HTHEME t, HDC, int part, int state, int prop, LPCRECT, MARGINS* out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!out) return E_POINTER;
  return winport::theme::Margins(d->cls, part, state, prop, out) ? S_OK : E_PROP_ID_UNSUPPORTED;
}
HRESULT WINAPI GetThemeInt(HTHEME t, int part, int state, int prop, int* out) {
  ThemeData* d = DataOf(t);
  if (!d) return E_HANDLE;
  if (!out) return E_POINTER;
  return winport::theme::Int(d->cls, part, state, prop, out) ? S_OK : E_PROP_ID_UNSUPPORTED;
}
HRESULT WINAPI GetThemeBool(HTHEME t, int, int, int, BOOL* out) {
  if (!DataOf(t)) return E_HANDLE;
  if (!out) return E_POINTER;
  return E_PROP_ID_UNSUPPORTED;
}
HRESULT WINAPI GetThemeFont(HTHEME t, HDC, int, int, int, LOGFONTW*) {
  // The parts use the font of the control.
  return DataOf(t) ? E_PROP_ID_UNSUPPORTED : E_HANDLE;
}
HRESULT WINAPI GetThemeRect(HTHEME t, int, int, int, LPRECT) { return DataOf(t) ? E_PROP_ID_UNSUPPORTED : E_HANDLE; }
HRESULT WINAPI GetThemeString(HTHEME t, int, int, int, LPWSTR, int) {
  return DataOf(t) ? E_PROP_ID_UNSUPPORTED : E_HANDLE;
}
HRESULT WINAPI GetThemeSysInt(HTHEME t, int, int*) { return DataOf(t) ? E_PROP_ID_UNSUPPORTED : E_HANDLE; }
HRESULT WINAPI HitTestThemeBackground(HTHEME t, HDC, int, int, DWORD, LPCRECT r, HRGN, POINT pt, WORD* out) {
  if (!DataOf(t)) return E_HANDLE;
  if (!r || !out) return E_POINTER;
  *out = PtInRect(r, pt) ? HTCLIENT : HTNOWHERE;
  return S_OK;
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
