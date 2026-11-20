// user_extra.cpp - the display as one monitor (MonitorFromWindow and the
// rest), layered windows, PrintWindow, and the cursor's state.
//
// There is one monitor, the screen the backend reports, with the work
// area SystemParametersInfo gives. A layered window keeps the attributes
// it is given, but draws opaque: the backends have no per-window alpha.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>

using namespace winport;

namespace {

// The one monitor's handle.
const HMONITOR kMonitor = (HMONITOR)(UINT_PTR)0x4D4F4E31;  // "MON1"

RECT ScreenBounds() {
  RECT r = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  return r;
}

// The monitor for a rectangle: the screen when they meet, else by flags.
HMONITOR ForRect(const RECT& r, DWORD flags) {
  RECT s = ScreenBounds(), x;
  if (IntersectRect(&x, &s, &r) || (r.left == r.right && r.top == r.bottom && r.left >= s.left &&
                                    r.left < s.right && r.top >= s.top && r.top < s.bottom))
    return kMonitor;
  return flags == MONITOR_DEFAULTTONULL ? NULL : kMonitor;
}

bool FillInfo(HMONITOR m, LPMONITORINFO mi, bool wide) {
  if (m != kMonitor || !mi || mi->cbSize < sizeof(MONITORINFO)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  mi->rcMonitor = ScreenBounds();
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi->rcWork, 0)) mi->rcWork = mi->rcMonitor;
  mi->dwFlags = MONITORINFOF_PRIMARY;
  if (wide && mi->cbSize >= sizeof(MONITORINFOEXW)) {
    const char* n = "\\\\.\\DISPLAY1";
    WCHAR* d = ((MONITORINFOEXW*)mi)->szDevice;
    size_t i = 0;
    for (; n[i]; i++) d[i] = (WCHAR)n[i];
    d[i] = 0;
  } else if (!wide && mi->cbSize >= sizeof(MONITORINFOEXA)) {
    strcpy(((MONITORINFOEXA*)mi)->szDevice, "\\\\.\\DISPLAY1");
  }
  return true;
}

// A layered window's attributes, as window properties.
const WCHAR kLayeredKey[] = L"winport.layered.key";
const WCHAR kLayeredAlpha[] = L"winport.layered.alpha";
const WCHAR kLayeredFlags[] = L"winport.layered.flags";

bool Layered(HWND h) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return false;
  }
  if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  return true;
}

}  // namespace

extern "C" {

HMONITOR WINAPI MonitorFromPoint(POINT p, DWORD flags) {
  RECT r = {p.x, p.y, p.x, p.y};
  return ForRect(r, flags);
}

HMONITOR WINAPI MonitorFromRect(LPCRECT r, DWORD flags) {
  if (!r) return flags == MONITOR_DEFAULTTONULL ? NULL : kMonitor;
  return ForRect(*r, flags);
}

HMONITOR WINAPI MonitorFromWindow(HWND h, DWORD flags) {
  RECT r;
  if (!h || !GetWindowRect(h, &r)) return flags == MONITOR_DEFAULTTONULL ? NULL : kMonitor;
  if (IsIconic(h)) return kMonitor;
  return ForRect(r, flags);
}

BOOL WINAPI GetMonitorInfoW(HMONITOR m, LPMONITORINFO mi) { return FillInfo(m, mi, true); }
BOOL WINAPI GetMonitorInfoA(HMONITOR m, LPMONITORINFO mi) { return FillInfo(m, mi, false); }

BOOL WINAPI EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC proc, LPARAM data) {
  if (!proc) return FALSE;
  RECT r = ScreenBounds();
  if (dc) {
    // In the DC's coordinates, within its clipping.
    RECT box;
    if (GetClipBox(dc, &box) == ERROR) return FALSE;
    POINT org;
    GetDCOrgEx(dc, &org);
    OffsetRect(&r, -org.x, -org.y);
    if (!IntersectRect(&r, &r, &box)) return TRUE;
  }
  if (clip && !IntersectRect(&r, &r, clip)) return TRUE;
  proc(kMonitor, dc, &r, data);
  return TRUE;
}

BOOL WINAPI SetLayeredWindowAttributes(HWND h, COLORREF key, BYTE alpha, DWORD flags) {
  if (!Layered(h)) return FALSE;
  SetPropW(h, kLayeredKey, (HANDLE)(UINT_PTR)key);
  SetPropW(h, kLayeredAlpha, (HANDLE)(UINT_PTR)alpha);
  SetPropW(h, kLayeredFlags, (HANDLE)(UINT_PTR)(flags | 0x80000000u));
  return TRUE;
}

BOOL WINAPI GetLayeredWindowAttributes(HWND h, COLORREF* key, BYTE* alpha, DWORD* flags) {
  if (!Layered(h)) return FALSE;
  UINT_PTR f = (UINT_PTR)GetPropW(h, kLayeredFlags);
  // Only after SetLayeredWindowAttributes (not UpdateLayeredWindow).
  if (!(f & 0x80000000u)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (key) *key = (COLORREF)(UINT_PTR)GetPropW(h, kLayeredKey);
  if (alpha) *alpha = (BYTE)(UINT_PTR)GetPropW(h, kLayeredAlpha);
  if (flags) *flags = (DWORD)(f & 0x7fffffffu);
  return TRUE;
}

// The window takes the place and size given and shows the source DC's
// image (opaque; see the top).
BOOL WINAPI UpdateLayeredWindow(HWND h, HDC dst, POINT* pos, SIZE* size, HDC src, POINT* srcPos, COLORREF key,
                                BLENDFUNCTION* blend, DWORD flags) {
  (void)dst;
  if (!Layered(h)) return FALSE;
  RECT r;
  GetWindowRect(h, &r);
  UINT swp = SWP_NOZORDER | SWP_NOACTIVATE;
  if (!pos) swp |= SWP_NOMOVE;
  if (!size || (flags & ULW_EX_NORESIZE)) swp |= SWP_NOSIZE;
  int cx = size ? size->cx : r.right - r.left, cy = size ? size->cy : r.bottom - r.top;
  if (!(swp & SWP_NOMOVE) || !(swp & SWP_NOSIZE))
    SetWindowPos(h, NULL, pos ? pos->x : 0, pos ? pos->y : 0, cx, cy, swp);
  SetPropW(h, kLayeredKey, (HANDLE)(UINT_PTR)key);
  SetPropW(h, kLayeredAlpha, (HANDLE)(UINT_PTR)(blend ? blend->SourceConstantAlpha : 255));
  SetPropW(h, kLayeredFlags, (HANDLE)(UINT_PTR)(flags & (ULW_COLORKEY | ULW_ALPHA)));
  if (src) {
    HDC wdc = GetWindowDC(h);
    BitBlt(wdc, 0, 0, cx, cy, src, srcPos ? srcPos->x : 0, srcPos ? srcPos->y : 0, SRCCOPY);
    ReleaseDC(h, wdc);
  }
  return TRUE;
}

// The window as it looks, into dc: the whole window, or with
// PW_CLIENTONLY its client area, at the DC's origin.
BOOL WINAPI PrintWindow(HWND h, HDC dc, UINT flags) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (!dc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  LPARAM what = PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN | PRF_OWNED;
  if (!(flags & PW_CLIENTONLY)) what |= PRF_NONCLIENT;
  SendMessageW(h, WM_PRINT, (WPARAM)dc, what);
  return TRUE;
}

BOOL WINAPI GetCursorInfo(PCURSORINFO ci) {
  if (!ci || ci->cbSize != sizeof(CURSORINFO)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ci->hCursor = GetCursor();
  ci->flags = ci->hCursor ? CURSOR_SHOWING : 0;
  GetCursorPos(&ci->ptScreenPos);
  return TRUE;
}

}  // extern "C"
