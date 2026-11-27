// user_dpi.cpp - dots per inch: whether the program is DPI aware, the
// screen's DPI it sees, and the functions that tell them.
//
// A program that is DPI aware draws at the screen's resolution: a screen
// DC's LOGPIXELSX is the desktop's scale times 96, and the system's sizes
// and fonts grow with it, as on Windows. One that is not sees 96 dots per
// inch, and the backend shows its windows scaled up. The awareness comes,
// as on Windows, from the compatibility setting for the program's file
// (HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\
// AppCompatFlags\Layers, "HIGHDPIAWARE" or "DPIUNAWARE"), else from its
// manifest (dpiAware, dpiAwareness), else from SetProcessDPIAware and the
// like called before the first window, else the default. Both are fixed
// the first time anything asks for them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"
#include "gdi.h"

#include <shellscalingapi.h>
#include <string.h>
#include <wctype.h>

#include <mutex>
#include <string>

using namespace winport;

namespace {

enum Awareness { kUnset = -1, kUnaware = 0, kSystemAware = 1, kPerMonitor = 2 };

// What a program not saying otherwise gets.
const Awareness kDefault = kSystemAware;

std::atomic<int> g_asked{kUnset};  // by SetProcessDPIAware and the like
std::atomic<bool> g_fixed{false};
int g_awareness = kSystemAware;
int g_dpi = 96;

std::string Lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

// The text of a manifest element, the first named name (with or without
// a namespace prefix); empty when there is none.
std::string Element(const std::string& xml, const char* name) {
  size_t n = strlen(name);
  for (size_t at = 0; (at = xml.find('<', at)) != std::string::npos; at++) {
    size_t p = at + 1;
    size_t end = p;
    while (end < xml.size() && xml[end] != '>' && xml[end] != ' ' && xml[end] != '\t' && xml[end] != '\r' &&
           xml[end] != '\n' && xml[end] != '/')
      end++;
    std::string tag = xml.substr(p, end - p);
    size_t colon = tag.find(':');
    if (colon != std::string::npos) tag = tag.substr(colon + 1);
    if (tag.size() != n || Lower(tag) != Lower(name)) continue;
    size_t open = xml.find('>', end);
    if (open == std::string::npos || xml[open - 1] == '/') return std::string();
    size_t close = xml.find('<', open);
    if (close == std::string::npos) return std::string();
    std::string text = xml.substr(open + 1, close - open - 1);
    while (!text.empty() && strchr(" \t\r\n", text[0])) text.erase(0, 1);
    while (!text.empty() && strchr(" \t\r\n", text.back())) text.pop_back();
    return Lower(text);
  }
  return std::string();
}

// What the program's manifest asks for.
int FromManifest() {
  HMODULE exe = GetModuleHandleW(NULL);
  for (int id = 1; id <= 3; id++) {
    HRSRC r = FindResourceW(exe, MAKEINTRESOURCEW(id), (LPCWSTR)RT_MANIFEST);
    if (!r) continue;
    HGLOBAL g = LoadResource(exe, r);
    const char* data = g ? (const char*)LockResource(g) : NULL;
    if (!data) continue;
    std::string xml(data, SizeofResource(exe, r));
    // Windows 10's element comes first; it lists choices, best first.
    std::string list = Element(xml, "dpiAwareness");
    size_t start = 0;
    while (!list.empty() && start <= list.size()) {
      size_t comma = list.find(',', start);
      std::string v = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      while (!v.empty() && v[0] == ' ') v.erase(0, 1);
      while (!v.empty() && v.back() == ' ') v.pop_back();
      if (v == "permonitorv2" || v == "permonitor") return kPerMonitor;
      if (v == "system") return kSystemAware;
      if (v == "unaware") return kUnaware;
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    std::string aware = Element(xml, "dpiAware");
    if (aware == "true") return kSystemAware;
    if (aware == "true/pm" || aware == "per monitor" || aware == "permonitor") return kPerMonitor;
    if (aware == "false") return kUnaware;
  }
  return kUnset;
}

// The compatibility setting for the program's file.
int FromCompatFlags() {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
  if (!n || n >= MAX_PATH) return kUnset;
  static const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
  for (HKEY root : roots) {
    HKEY k;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", 0,
                      KEY_READ, &k) != ERROR_SUCCESS)
      continue;
    wchar_t v[512];
    DWORD type, cb = sizeof v - sizeof(wchar_t);
    LONG got = RegQueryValueExW(k, path, NULL, &type, (BYTE*)v, &cb);
    RegCloseKey(k);
    if (got != ERROR_SUCCESS || type != REG_SZ) continue;
    v[cb / sizeof(wchar_t)] = 0;
    std::wstring flags = v;
    for (wchar_t& c : flags) c = (wchar_t)towupper(c);
    if (flags.find(L"DPIUNAWARE") != std::wstring::npos) return kUnaware;
    if (flags.find(L"HIGHDPIAWARE") != std::wstring::npos) return kSystemAware;
  }
  return kUnset;
}

// The DPI the user set (Windows keeps it there), or 0.
int FromLogPixels() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
  BYTE buf[64];
  DWORD type, cb = sizeof buf - sizeof(wchar_t);
  LONG got = RegQueryValueExW(k, L"LogPixels", NULL, &type, buf, &cb);
  RegCloseKey(k);
  if (got != ERROR_SUCCESS) return 0;
  long v = 0;
  if (type == REG_DWORD && cb == 4) v = (long)*(const DWORD*)buf;
  else if (type == REG_SZ) {
    memset(buf + cb, 0, sizeof(wchar_t));
    v = wcstol((const wchar_t*)buf, NULL, 10);
  }
  return v >= 96 && v <= 960 ? (int)v : 0;
}

std::once_flag g_once;

void Fix() {
  std::call_once(g_once, [] {
    int a = FromCompatFlags();
    if (a == kUnset) a = FromManifest();
    if (a == kUnset) a = g_asked;
    if (a == kUnset) a = kDefault;
    int dpi = 96;
    if (a != kUnaware) {
      dpi = FromLogPixels();
      if (!dpi) {
#if defined(__MSDOS__) || defined(macintosh)
        double s = 1;  // (without starting the screen for a console program)
#else
        double s = GetBackend()->DesktopScale();
#endif
        dpi = s > 1 ? (int)(96 * s + 0.5) : 96;
        if (dpi > 960) dpi = 960;
      }
    }
    g_awareness = a;
    g_dpi = dpi;
    g_fixed = true;
  });
}

// Sets the awareness a program asks for; false, as on Windows, once it
// is fixed at another, or when the compatibility setting or the manifest
// says otherwise.
bool Ask(int a) {
  if (g_fixed) return g_awareness == a;
  int set = FromCompatFlags();
  if (set == kUnset) set = FromManifest();
  if (set != kUnset) return set == a;
  g_asked = a;
  return true;
}

DPI_AWARENESS_CONTEXT ContextOf(int a) {
  switch (a) {
    case kUnaware: return DPI_AWARENESS_CONTEXT_UNAWARE;
    case kPerMonitor: return DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
    default: return DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
  }
}

int AwarenessOf(DPI_AWARENESS_CONTEXT c) {
  if (c == DPI_AWARENESS_CONTEXT_UNAWARE || c == DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) return kUnaware;
  if (c == DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) return kSystemAware;
  if (c == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE || c == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
    return kPerMonitor;
  return kUnset;
}

// The system metrics that are not sizes, which stay the same at any DPI.
bool NotASize(int index) {
  switch (index) {
    case SM_CXSCREEN: case SM_CYSCREEN: case SM_CXFULLSCREEN: case SM_CYFULLSCREEN:
    case SM_CXVIRTUALSCREEN: case SM_CYVIRTUALSCREEN: case SM_CXMAXIMIZED: case SM_CYMAXIMIZED:
    case SM_CXBORDER: case SM_CYBORDER: case SM_CMOUSEBUTTONS: case SM_MOUSEPRESENT:
    case SM_MOUSEWHEELPRESENT: case SM_CMONITORS: case SM_SAMEDISPLAYFORMAT:
    case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN:
      return true;
    default:
      return false;
  }
}

}  // namespace

int winport::SystemDpi() {
  Fix();
  return g_dpi;
}

bool winport::DpiAware() {
  Fix();
  return g_awareness != kUnaware;
}

extern "C" {

BOOL WINAPI SetProcessDPIAware(void) { return Ask(kSystemAware); }

BOOL WINAPI IsProcessDPIAware(void) { return DpiAware(); }

BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) {
  int a = AwarenessOf(c);
  if (a == kUnset) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!Ask(a)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return TRUE;
}

HRESULT WINAPI SetProcessDpiAwareness(PROCESS_DPI_AWARENESS v) {
  if (v < PROCESS_DPI_UNAWARE || v > PROCESS_PER_MONITOR_DPI_AWARE) return E_INVALIDARG;
  return Ask((int)v) ? S_OK : E_ACCESSDENIED;
}

HRESULT WINAPI GetProcessDpiAwareness(HANDLE process, PROCESS_DPI_AWARENESS* out) {
  if (!out) return E_INVALIDARG;
  if (process && process != GetCurrentProcess()) return E_INVALIDARG;  // only this process's
  Fix();
  *out = (PROCESS_DPI_AWARENESS)g_awareness;
  return S_OK;
}

// Every thread has the process's awareness.
DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(void) {
  Fix();
  return ContextOf(g_awareness);
}

DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) {
  if (AwarenessOf(c) == kUnset) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return GetThreadDpiAwarenessContext();
}

DPI_AWARENESS_CONTEXT WINAPI GetWindowDpiAwarenessContext(HWND h) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  return GetThreadDpiAwarenessContext();
}

DPI_AWARENESS WINAPI GetAwarenessFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) {
  int a = AwarenessOf(c);
  return a == kUnset ? DPI_AWARENESS_INVALID : (DPI_AWARENESS)a;
}

UINT WINAPI GetDpiFromDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) {
  int a = AwarenessOf(c);
  if (a == kUnset || a == kPerMonitor) return 0;
  return a == kUnaware ? 96 : (UINT)SystemDpi();
}

BOOL WINAPI AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT a, DPI_AWARENESS_CONTEXT b) {
  return AwarenessOf(a) != kUnset && AwarenessOf(a) == AwarenessOf(b);
}

BOOL WINAPI IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT c) { return AwarenessOf(c) != kUnset; }

UINT WINAPI GetDpiForSystem(void) { return (UINT)SystemDpi(); }

UINT WINAPI GetDpiForWindow(HWND h) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return 0;
  }
  return (UINT)SystemDpi();
}

HRESULT WINAPI GetDpiForMonitor(HMONITOR m, MONITOR_DPI_TYPE type, UINT* x, UINT* y) {
  if (!m || !x || !y || type < MDT_EFFECTIVE_DPI || type > MDT_RAW_DPI) return E_INVALIDARG;
  double scale = MonitorScale(m);
  if (scale <= 0) return E_INVALIDARG;
  // The program draws at one DPI on every monitor (the system's); the
  // raw DPI is the monitor's own.
  *x = *y = type == MDT_RAW_DPI ? (UINT)(scale * 96 + 0.5) : (UINT)SystemDpi();
  return S_OK;
}

HRESULT WINAPI GetScaleFactorForMonitor(HMONITOR m, DEVICE_SCALE_FACTOR* out) {
  if (!m || !out) return E_INVALIDARG;
  *out = (DEVICE_SCALE_FACTOR)MulDiv(SystemDpi(), 100, 96);
  return S_OK;
}

// A size the system has at its own DPI, at another.
int WINAPI GetSystemMetricsForDpi(int index, UINT dpi) {
  int v = GetSystemMetrics(index);
  if (!dpi || NotASize(index)) return v;
  return MulDiv(v, (int)dpi, SystemDpi());
}

BOOL WINAPI AdjustWindowRectExForDpi(LPRECT r, DWORD style, BOOL menu, DWORD exStyle, UINT dpi) {
  if (!r) return FALSE;
  RECT in = *r;
  if (!AdjustWindowRectEx(r, style, menu, exStyle)) return FALSE;
  int sys = SystemDpi();
  if (dpi && (int)dpi != sys) {
    r->left = in.left - MulDiv(in.left - r->left, (int)dpi, sys);
    r->top = in.top - MulDiv(in.top - r->top, (int)dpi, sys);
    r->right = in.right + MulDiv(r->right - in.right, (int)dpi, sys);
    r->bottom = in.bottom + MulDiv(r->bottom - in.bottom, (int)dpi, sys);
  }
  return TRUE;
}

// The metrics and fonts the SPI_GET actions give, at another DPI.
BOOL WINAPI SystemParametersInfoForDpi(UINT action, UINT uiParam, PVOID pv, UINT winIni, UINT dpi) {
  if (!SystemParametersInfoW(action, uiParam, pv, winIni)) return FALSE;
  int sys = SystemDpi();
  if (!dpi || (int)dpi == sys) return TRUE;
  auto size = [&](int& v) { v = MulDiv(v, (int)dpi, sys); };
  auto font = [&](LOGFONTW& f) { f.lfHeight = MulDiv(f.lfHeight, (int)dpi, sys); };
  switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
      NONCLIENTMETRICSW* m = (NONCLIENTMETRICSW*)pv;
      size(m->iBorderWidth), size(m->iScrollWidth), size(m->iScrollHeight);
      size(m->iCaptionWidth), size(m->iCaptionHeight), size(m->iSmCaptionWidth);
      size(m->iSmCaptionHeight), size(m->iMenuWidth), size(m->iMenuHeight);
      font(m->lfCaptionFont), font(m->lfSmCaptionFont), font(m->lfMenuFont);
      font(m->lfStatusFont), font(m->lfMessageFont);
      break;
    }
    case SPI_GETICONTITLELOGFONT: font(*(LOGFONTW*)pv); break;
    default: break;
  }
  return TRUE;
}

}  // extern "C"
