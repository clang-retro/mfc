// user_extra.cpp - the display as one monitor (MonitorFromWindow and the
// rest), PrintWindow, and the cursor's state. (Layered windows are in
// user_layered.cpp.)
//
// There is one monitor, the screen the backend reports, with the work
// area SystemParametersInfo gives.
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
