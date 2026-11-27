// user_extra.cpp - the monitors (MonitorFromWindow, EnumDisplayMonitors,
// EnumDisplayDevices and the display settings), PrintWindow, and the
// cursor's state. (Layered windows are in user_layered.cpp.)
//
// The monitors are the backend's: \\.\DISPLAY1 and on, the primary one
// first. The display modes are what each one shows now; they do not
// change.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

using namespace winport;

namespace {

typedef Backend::Monitor Monitor;

// A monitor's handle: "MON1" for the first, and on.
const UINT_PTR kMonitorBase = 0x4D4F4E30;
HMONITOR MonitorHandle(size_t i) { return (HMONITOR)(kMonitorBase + i + 1); }

std::vector<Monitor> Monitors() {
  std::vector<Monitor> m = GetBackend()->Monitors();
  if (m.empty()) {
    SIZE s = GetBackend()->ScreenSize();
    m.push_back(Monitor{RECT{0, 0, s.cx, s.cy}, GetBackend()->WorkArea(), GetBackend()->DesktopScale()});
  }
  return m;
}

// The monitor of a handle, by its number; -1 for none.
int IndexOf(HMONITOR h, const std::vector<Monitor>& m) {
  UINT_PTR v = (UINT_PTR)h;
  if (v <= kMonitorBase || v > kMonitorBase + m.size()) return -1;
  return (int)(v - kMonitorBase - 1);
}

// The distance from a point to a rectangle, squared.
long long Distance(const RECT& r, POINT p) {
  long long dx = p.x < r.left ? r.left - p.x : p.x >= r.right ? p.x - r.right + 1 : 0;
  long long dy = p.y < r.top ? r.top - p.y : p.y >= r.bottom ? p.y - r.bottom + 1 : 0;
  return dx * dx + dy * dy;
}

// The monitor a rectangle meets most; else by flags, the nearest or the
// primary one.
HMONITOR ForRect(const RECT& r, DWORD flags) {
  std::vector<Monitor> m = Monitors();
  bool point = r.left == r.right && r.top == r.bottom;
  long long best = 0;
  int found = -1;
  for (size_t i = 0; i < m.size(); i++) {
    if (point) {
      if (r.left >= m[i].rect.left && r.left < m[i].rect.right && r.top >= m[i].rect.top && r.top < m[i].rect.bottom)
        return MonitorHandle(i);
      continue;
    }
    RECT x;
    if (!IntersectRect(&x, &m[i].rect, &r)) continue;
    long long area = (long long)(x.right - x.left) * (x.bottom - x.top);
    if (area > best) {
      best = area;
      found = (int)i;
    }
  }
  if (found >= 0) return MonitorHandle(found);
  if (flags == MONITOR_DEFAULTTONULL) return NULL;
  if (flags == MONITOR_DEFAULTTOPRIMARY) return MonitorHandle(0);
  POINT c = {r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2};
  long long d = -1;
  for (size_t i = 0; i < m.size(); i++) {
    long long di = Distance(m[i].rect, c);
    if (d < 0 || di < d) {
      d = di;
      found = (int)i;
    }
  }
  return MonitorHandle(found < 0 ? 0 : found);
}

std::wstring DeviceName(size_t i) { return L"\\\\.\\DISPLAY" + std::to_wstring(i + 1); }

// The monitor a display device names (\\.\DISPLAYn); the primary one for
// none. -1 when there is no such device.
int DeviceIndex(const wchar_t* name, size_t count) {
  if (!name || !*name) return 0;
  for (size_t i = 0; i < count; i++)
    if (_wcsicmp(name, DeviceName(i).c_str()) == 0) return (int)i;
  return -1;
}

void Copy(const std::wstring& s, WCHAR* out, size_t size) {
  size_t n = std::min(s.size(), size - 1);
  for (size_t i = 0; i < n; i++) out[i] = (WCHAR)s[i];
  out[n] = 0;
}
void Copy(const std::wstring& s, CHAR* out, size_t size) {
  std::string a = Narrow(s.c_str(), (int)s.size());
  size_t n = std::min(a.size(), size - 1);
  memcpy(out, a.data(), n);
  out[n] = 0;
}

bool FillInfo(HMONITOR h, LPMONITORINFO mi, bool wide) {
  std::vector<Monitor> m = Monitors();
  int i = IndexOf(h, m);
  if (i < 0 || !mi || mi->cbSize < sizeof(MONITORINFO)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  mi->rcMonitor = m[i].rect;
  mi->rcWork = m[i].work;
  // The primary monitor's work area is SystemParametersInfo's.
  if (i == 0 && !SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi->rcWork, 0)) mi->rcWork = m[0].work;
  mi->dwFlags = i == 0 ? MONITORINFOF_PRIMARY : 0;
  if (wide && mi->cbSize >= sizeof(MONITORINFOEXW))
    Copy(DeviceName(i), ((MONITORINFOEXW*)mi)->szDevice, CCHDEVICENAME);
  else if (!wide && mi->cbSize >= sizeof(MONITORINFOEXA))
    Copy(DeviceName(i), ((MONITORINFOEXA*)mi)->szDevice, CCHDEVICENAME);
  return true;
}

// A display device: an adapter for each monitor, \\.\DISPLAYn, or with
// a device named, the monitor on it, \\.\DISPLAYn\Monitor0.
template <class D>
bool FillDevice(const wchar_t* device, DWORD index, D* dd) {
  if (!dd || dd->cb < sizeof(DWORD)) return false;
  size_t count = Monitors().size();
  DWORD cb = dd->cb;
  memset((char*)dd + sizeof(DWORD), 0, cb - sizeof(DWORD) < sizeof *dd - sizeof(DWORD) ? cb - sizeof(DWORD)
                                                                                         : sizeof *dd - sizeof(DWORD));
  D full;
  memset(&full, 0, sizeof full);
  if (!device || !*device) {
    if (index >= count) return false;
    Copy(DeviceName(index), full.DeviceName, 32);
    Copy(L"winport display", full.DeviceString, 128);
    full.StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | (index == 0 ? DISPLAY_DEVICE_PRIMARY_DEVICE : 0);
    Copy(L"WINPORT\\DISPLAY", full.DeviceID, 128);
  } else {
    int i = DeviceIndex(device, count);
    if (i < 0 || index != 0) return false;
    Copy(DeviceName(i) + L"\\Monitor0", full.DeviceName, 32);
    Copy(L"Generic monitor", full.DeviceString, 128);
    full.StateFlags = DISPLAY_DEVICE_ACTIVE | DISPLAY_DEVICE_ATTACHED;
    Copy(L"MONITOR\\Default_Monitor", full.DeviceID, 128);
  }
  full.cb = cb;
  memcpy(dd, &full, cb < sizeof full ? cb : sizeof full);
  return true;
}

// A monitor's one mode: its size, 32 bits a pixel and 60 Hz, at its place.
bool FillMode(const wchar_t* device, DWORD mode, DEVMODEW* dm) {
  if (!dm || (mode != ENUM_CURRENT_SETTINGS && mode != ENUM_REGISTRY_SETTINGS && mode != 0)) return false;
  std::vector<Monitor> m = Monitors();
  int i = DeviceIndex(device, m.size());
  if (i < 0) return false;
  WORD extra = dm->dmDriverExtra;
  memset(dm, 0, sizeof *dm);
  dm->dmSize = sizeof *dm;
  dm->dmDriverExtra = extra;
  Copy(DeviceName(i), dm->dmDeviceName, CCHDEVICENAME);
  dm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_POSITION;
  dm->dmBitsPerPel = 32;
  dm->dmPelsWidth = (DWORD)(m[i].rect.right - m[i].rect.left);
  dm->dmPelsHeight = (DWORD)(m[i].rect.bottom - m[i].rect.top);
  dm->dmPosition.x = m[i].rect.left;
  dm->dmPosition.y = m[i].rect.top;
  dm->dmDisplayFrequency = 60;
  return true;
}

void ModeToA(const DEVMODEW& w, DEVMODEA* a) {
  WORD extra = a->dmDriverExtra;
  memset(a, 0, sizeof *a);
  a->dmSize = sizeof *a;
  a->dmDriverExtra = extra;
  std::wstring name((const wchar_t*)w.dmDeviceName);
  Copy(name, (CHAR*)a->dmDeviceName, CCHDEVICENAME);
  a->dmFields = w.dmFields;
  a->dmBitsPerPel = w.dmBitsPerPel;
  a->dmPelsWidth = w.dmPelsWidth;
  a->dmPelsHeight = w.dmPelsHeight;
  a->dmPosition = w.dmPosition;
  a->dmDisplayFrequency = w.dmDisplayFrequency;
}

}  // namespace

double winport::MonitorScale(HMONITOR h) {
  std::vector<Monitor> m = Monitors();
  int i = IndexOf(h, m);
  return i < 0 ? 0 : m[i].scale;
}

RECT winport::VirtualScreen() {
  std::vector<Monitor> m = Monitors();
  RECT r = m[0].rect;
  for (const Monitor& x : m) UnionRect(&r, &r, &x.rect);
  return r;
}

extern "C" {

HMONITOR WINAPI MonitorFromPoint(POINT p, DWORD flags) {
  RECT r = {p.x, p.y, p.x, p.y};
  return ForRect(r, flags);
}

HMONITOR WINAPI MonitorFromRect(LPCRECT r, DWORD flags) {
  if (!r) return flags == MONITOR_DEFAULTTONULL ? NULL : MonitorHandle(0);
  return ForRect(*r, flags);
}

HMONITOR WINAPI MonitorFromWindow(HWND h, DWORD flags) {
  RECT r;
  if (!h || !GetWindowRect(h, &r)) return flags == MONITOR_DEFAULTTONULL ? NULL : MonitorHandle(0);
  // A minimized window goes by where it would be restored.
  WINDOWPLACEMENT wp = {sizeof wp};
  if (IsIconic(h) && GetWindowPlacement(h, &wp)) r = wp.rcNormalPosition;
  return ForRect(r, flags);
}

BOOL WINAPI GetMonitorInfoW(HMONITOR m, LPMONITORINFO mi) { return FillInfo(m, mi, true); }
BOOL WINAPI GetMonitorInfoA(HMONITOR m, LPMONITORINFO mi) { return FillInfo(m, mi, false); }

BOOL WINAPI EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC proc, LPARAM data) {
  if (!proc) return FALSE;
  std::vector<Monitor> m = Monitors();
  RECT box;
  POINT org = {0, 0};
  if (dc) {
    // In the DC's coordinates, within its clipping.
    if (GetClipBox(dc, &box) == ERROR) return FALSE;
    GetDCOrgEx(dc, &org);
  }
  for (size_t i = 0; i < m.size(); i++) {
    RECT r = m[i].rect;
    if (dc) {
      OffsetRect(&r, -org.x, -org.y);
      if (!IntersectRect(&r, &r, &box)) continue;
    }
    if (clip && !IntersectRect(&r, &r, clip)) continue;
    if (!proc(MonitorHandle(i), dc, &r, data)) break;
  }
  return TRUE;
}

BOOL WINAPI EnumDisplayDevicesW(LPCWSTR device, DWORD index, PDISPLAY_DEVICEW dd, DWORD) {
  return FillDevice(device, index, dd);
}

BOOL WINAPI EnumDisplayDevicesA(LPCSTR device, DWORD index, PDISPLAY_DEVICEA dd, DWORD) {
  std::wstring w = device ? Widen(device) : std::wstring();
  return FillDevice(device ? w.c_str() : NULL, index, dd);
}

BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR device, DWORD mode, DEVMODEW* dm, DWORD) {
  return FillMode(device, mode, dm);
}

BOOL WINAPI EnumDisplaySettingsW(LPCWSTR device, DWORD mode, DEVMODEW* dm) { return FillMode(device, mode, dm); }

BOOL WINAPI EnumDisplaySettingsExA(LPCSTR device, DWORD mode, DEVMODEA* dm, DWORD) {
  if (!dm) return FALSE;
  std::wstring name = device ? Widen(device) : std::wstring();
  DEVMODEW w;
  w.dmDriverExtra = 0;
  if (!FillMode(device ? name.c_str() : NULL, mode, &w)) return FALSE;
  ModeToA(w, dm);
  return TRUE;
}

BOOL WINAPI EnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA* dm) {
  return EnumDisplaySettingsExA(device, mode, dm, 0);
}

// A monitor's mode does not change: asking for the one it has succeeds.
LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR device, DEVMODEW* dm, HWND, DWORD flags, LPVOID) {
  if (!dm) return DISP_CHANGE_SUCCESSFUL;
  DEVMODEW now;
  now.dmDriverExtra = 0;
  if (!FillMode(device, ENUM_CURRENT_SETTINGS, &now)) return DISP_CHANGE_BADPARAM;
  if ((dm->dmFields & DM_PELSWIDTH) && dm->dmPelsWidth != now.dmPelsWidth) return DISP_CHANGE_BADMODE;
  if ((dm->dmFields & DM_PELSHEIGHT) && dm->dmPelsHeight != now.dmPelsHeight) return DISP_CHANGE_BADMODE;
  (void)flags;
  return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI ChangeDisplaySettingsExA(LPCSTR device, DEVMODEA* dm, HWND h, DWORD flags, LPVOID param) {
  std::wstring name = device ? Widen(device) : std::wstring();
  if (!dm) return ChangeDisplaySettingsExW(device ? name.c_str() : NULL, NULL, h, flags, param);
  DEVMODEW w;
  memset(&w, 0, sizeof w);
  w.dmFields = dm->dmFields;
  w.dmPelsWidth = dm->dmPelsWidth;
  w.dmPelsHeight = dm->dmPelsHeight;
  return ChangeDisplaySettingsExW(device ? name.c_str() : NULL, &w, h, flags, param);
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
