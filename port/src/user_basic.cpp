// user_basic.cpp - the parts of user32 that need no windows: rectangles,
// string resources, registered messages and formats, system colors and
// metrics, and message boxes before the window system is up.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "theme.h"
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__APPLE__) && !(defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && \
                               __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1030)
// kqueue arrived in Mac OS X 10.3. Before it, the theme file is not watched.
#define WINPORT_KQUEUE 1
#include <sys/event.h>
#endif

using namespace winport;

extern "C" {

// Rectangles
BOOL WINAPI SetRect(LPRECT r, int left, int top, int right, int bottom) {
  if (r == NULL) return FALSE;
  r->left = left;
  r->top = top;
  r->right = right;
  r->bottom = bottom;
  return TRUE;
}
BOOL WINAPI SetRectEmpty(LPRECT r) { return SetRect(r, 0, 0, 0, 0); }
BOOL WINAPI CopyRect(LPRECT dst, const RECT* src) {
  if (dst == NULL || src == NULL) return FALSE;
  *dst = *src;
  return TRUE;
}
BOOL WINAPI IsRectEmpty(const RECT* r) { return r == NULL || r->left >= r->right || r->top >= r->bottom; }
BOOL WINAPI EqualRect(const RECT* a, const RECT* b) {
  if (a == NULL || b == NULL) return FALSE;
  return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
BOOL WINAPI InflateRect(LPRECT r, int dx, int dy) {
  if (r == NULL) return FALSE;
  r->left -= dx;
  r->top -= dy;
  r->right += dx;
  r->bottom += dy;
  return TRUE;
}
BOOL WINAPI OffsetRect(LPRECT r, int dx, int dy) {
  if (r == NULL) return FALSE;
  r->left += dx;
  r->top += dy;
  r->right += dx;
  r->bottom += dy;
  return TRUE;
}
BOOL WINAPI PtInRect(const RECT* r, POINT pt) {
  return r != NULL && pt.x >= r->left && pt.x < r->right && pt.y >= r->top && pt.y < r->bottom;
}
BOOL WINAPI IntersectRect(LPRECT dst, const RECT* a, const RECT* b) {
  if (dst == NULL || a == NULL || b == NULL) return FALSE;
  RECT r = {a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
            a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom};
  if (IsRectEmpty(&r)) {
    SetRectEmpty(dst);
    return FALSE;
  }
  *dst = r;
  return TRUE;
}
BOOL WINAPI UnionRect(LPRECT dst, const RECT* a, const RECT* b) {
  if (dst == NULL || a == NULL || b == NULL) return FALSE;
  if (IsRectEmpty(a)) {
    if (IsRectEmpty(b)) {
      SetRectEmpty(dst);
      return FALSE;
    }
    *dst = *b;
    return TRUE;
  }
  if (IsRectEmpty(b)) {
    *dst = *a;
    return TRUE;
  }
  RECT r = {a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
            a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom};
  *dst = r;
  return TRUE;
}
BOOL WINAPI SubtractRect(LPRECT dst, const RECT* a, const RECT* b) {
  if (dst == NULL || a == NULL || b == NULL) return FALSE;
  RECT r = *a, i;
  // Only a cut across the whole of one side changes the rectangle.
  if (IntersectRect(&i, a, b)) {
    if (i.top == r.top && i.bottom == r.bottom) {
      if (i.left == r.left) r.left = i.right;
      else if (i.right == r.right) r.right = i.left;
    } else if (i.left == r.left && i.right == r.right) {
      if (i.top == r.top) r.top = i.bottom;
      else if (i.bottom == r.bottom) r.bottom = i.top;
    }
  }
  *dst = r;
  return !IsRectEmpty(dst);
}

int WINAPI MulDiv(int number, int numerator, int denominator) {
  if (denominator == 0) return -1;
  long long v = (long long)number * numerator;
  // Rounds half away from zero.
  long long half = (denominator < 0 ? -(long long)denominator : denominator) / 2;
  long long q = ((v < 0) != (denominator < 0)) ? (v - (v < 0 ? half : -half)) / denominator
                                                 : (v + (v < 0 ? -half : half)) / denominator;
  if (q > 0x7fffffffLL || q < -0x80000000LL) return -1;
  return (int)q;
}

}  // extern "C"

// String resources: blocks of 16 strings, each a count of UTF-16 units
// and the units, in RT_STRING resource (id / 16 + 1).
namespace {

bool FindString(HINSTANCE h, UINT id, std::wstring* out) {
  HRSRC r = FindResourceW(h, MAKEINTRESOURCEW(id / 16 + 1), (LPCWSTR)RT_STRING);
  if (r == NULL) return false;
  const BYTE* p = (const BYTE*)LockResource(LoadResource(h, r));
  const BYTE* end = p + SizeofResource(h, r);
  if (p == NULL) return false;
  // The numbers are in the order of the processor (NativeResourceData).
  auto at = [](const BYTE* q) {
    WORD v;
    memcpy(&v, q, 2);
    return (UINT)v;
  };
  for (UINT i = 0; i < 16 && p + 2 <= end; i++) {
    UINT n = at(p);
    p += 2;
    if (i == id % 16) {
      if (n == 0 || p + 2 * n > end) return false;
      std::vector<WCHAR> u;
      // UTF-16 to wchar_t, joining surrogate pairs.
      out->clear();
      for (UINT k = 0; k < n; k++) {
        UINT c = at(p + 2 * k);
        if (c >= 0xd800 && c < 0xdc00 && k + 1 < n) {
          UINT d = at(p + 2 * k + 2);
          if (d >= 0xdc00 && d < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
            k++;
          }
        }
        out->push_back((wchar_t)c);
      }
      return true;
    }
    p += 2 * n;
  }
  return false;
}

}  // namespace

extern "C" {

int WINAPI LoadStringW(HINSTANCE h, UINT id, LPWSTR buf, int cch) {
  std::wstring s;
  if (!FindString(h, id, &s)) {
    if (buf != NULL && cch > 0) buf[0] = 0;
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return 0;
  }
  if (cch == 0) {
    // A pointer to the string itself, which stays put: these strings are
    // converted once and kept.
    static std::mutex lock;
    static std::map<std::pair<HINSTANCE, UINT>, std::wstring> kept;
    std::lock_guard<std::mutex> g(lock);
    std::wstring& k = kept[std::make_pair(h, id)];
    k = s;
    if (buf != NULL) *(LPCWSTR*)buf = k.c_str();
    return (int)k.size();
  }
  if (buf == NULL) return 0;
  int n = (int)s.size() < cch - 1 ? (int)s.size() : cch - 1;
  wmemcpy(buf, s.data(), n);
  buf[n] = 0;
  return n;
}

int WINAPI LoadStringA(HINSTANCE h, UINT id, LPSTR buf, int cch) {
  std::wstring s;
  if (buf == NULL || cch <= 0) return 0;
  if (!FindString(h, id, &s)) {
    buf[0] = 0;
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return 0;
  }
  std::string a = Narrow(s.c_str(), (int)s.size());
  int n = (int)a.size() < cch - 1 ? (int)a.size() : cch - 1;
  // Not in the middle of a UTF-8 sequence.
  while (n > 0 && n < (int)a.size() && ((BYTE)a[n] & 0xc0) == 0x80) n--;
  memcpy(buf, a.data(), n);
  buf[n] = 0;
  return n;
}

// Registered messages and clipboard formats share the string atoms.
UINT WINAPI RegisterWindowMessageA(LPCSTR name) { return name && *name ? GlobalAddAtomA(name) : 0; }
UINT WINAPI RegisterWindowMessageW(LPCWSTR name) { return name && *name ? GlobalAddAtomW(name) : 0; }
UINT WINAPI RegisterClipboardFormatA(LPCSTR name) { return name && *name ? GlobalAddAtomA(name) : 0; }
UINT WINAPI RegisterClipboardFormatW(LPCWSTR name) { return name && *name ? GlobalAddAtomW(name) : 0; }

// ---------------------------------------------------------------------
// The look: the system colors, the caption gradient, and the sizes and
// fonts of the window frame and menus. Each is the classic Windows one
// unless the user's registry says otherwise, where Windows keeps it:
//
//   HKEY_CURRENT_USER\Control Panel\Colors       "ActiveTitle"="0 0 128"
//   HKEY_CURRENT_USER\Control Panel\Desktop      "GradientCaptions"=dword:1
//                                                (or UserPreferencesMask's bit 0x10)
//   ...\Control Panel\Desktop\WindowMetrics      "CaptionHeight"="18"
//                                                "CaptionFont"="Tahoma,8,bold"
//
// A size is in pixels, or negative in twips (-15 to the pixel), as Windows
// writes them; a font is "face,points" with ",bold" or ",italic" after it,
// or a LOGFONTW in REG_BINARY as Windows keeps it. The registry is read
// when the look is first asked for; SetSysColors and SystemParametersInfo
// change it for the session (SPIF_UPDATEINIFILE saving the sizes, fonts
// and gradient). The registry file is watched, so a change made there,
// by hand or by another program, reaches the windows open as it would
// on Windows: what changed is applied, with WM_SYSCOLORCHANGE or
// WM_SETTINGCHANGE.

}  // extern "C"

namespace {

const COLORREF kClassicColors[] = {
    RGB(192, 192, 192),  // COLOR_SCROLLBAR
    RGB(0, 128, 128),    // COLOR_BACKGROUND
    RGB(0, 0, 128),      // COLOR_ACTIVECAPTION
    RGB(128, 128, 128),  // COLOR_INACTIVECAPTION
    RGB(192, 192, 192),  // COLOR_MENU
    RGB(255, 255, 255),  // COLOR_WINDOW
    RGB(0, 0, 0),        // COLOR_WINDOWFRAME
    RGB(0, 0, 0),        // COLOR_MENUTEXT
    RGB(0, 0, 0),        // COLOR_WINDOWTEXT
    RGB(255, 255, 255),  // COLOR_CAPTIONTEXT
    RGB(192, 192, 192),  // COLOR_ACTIVEBORDER
    RGB(192, 192, 192),  // COLOR_INACTIVEBORDER
    RGB(128, 128, 128),  // COLOR_APPWORKSPACE
    RGB(0, 0, 128),      // COLOR_HIGHLIGHT
    RGB(255, 255, 255),  // COLOR_HIGHLIGHTTEXT
    RGB(192, 192, 192),  // COLOR_BTNFACE
    RGB(128, 128, 128),  // COLOR_BTNSHADOW
    RGB(128, 128, 128),  // COLOR_GRAYTEXT
    RGB(0, 0, 0),        // COLOR_BTNTEXT
    RGB(192, 192, 192),  // COLOR_INACTIVECAPTIONTEXT
    RGB(255, 255, 255),  // COLOR_BTNHIGHLIGHT
    RGB(64, 64, 64),     // COLOR_3DDKSHADOW
    RGB(223, 223, 223),  // COLOR_3DLIGHT
    RGB(0, 0, 0),        // COLOR_INFOTEXT
    RGB(255, 255, 225),  // COLOR_INFOBK
    RGB(180, 180, 180),  // 25, unused
    RGB(0, 0, 255),      // COLOR_HOTLIGHT
    RGB(16, 132, 208),   // COLOR_GRADIENTACTIVECAPTION
    RGB(181, 181, 181),  // COLOR_GRADIENTINACTIVECAPTION
    RGB(0, 0, 128),      // COLOR_MENUHILIGHT
    RGB(192, 192, 192),  // COLOR_MENUBAR
};
const int kNumColors = (int)(sizeof(kClassicColors) / sizeof(kClassicColors[0]));

// The registry's name for each color, by index.
const wchar_t* const kColorNames[] = {
    L"Scrollbar",      L"Background",     L"ActiveTitle",   L"InactiveTitle",  L"Menu",
    L"Window",         L"WindowFrame",    L"MenuText",      L"WindowText",     L"TitleText",
    L"ActiveBorder",   L"InactiveBorder", L"AppWorkspace",  L"Hilight",        L"HilightText",
    L"ButtonFace",     L"ButtonShadow",   L"GrayText",      L"ButtonText",     L"InactiveTitleText",
    L"ButtonHilight",  L"ButtonDkShadow", L"ButtonLight",   L"InfoText",       L"InfoWindow",
    L"ButtonAlternateFace", L"HotTrackingColor", L"GradientActiveTitle", L"GradientInactiveTitle",
    L"MenuHilight",    L"MenuBar"};
static_assert(sizeof(kColorNames) / sizeof(kColorNames[0]) == sizeof(kClassicColors) / sizeof(kClassicColors[0]),
              "a name for each color");

// The classic fonts: 8-point MS Sans Serif, bold in captions.
LOGFONTW ClassicFont(bool bold) {
  LOGFONTW lf = {};
  lf.lfHeight = -MulDiv(8, SystemDpi(), 72);
  lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  wcscpy(lf.lfFaceName, L"MS Sans Serif");
  return lf;
}

struct Theme {
  COLORREF colors[kNumColors];
  BOOL gradient = FALSE;
  // The frame's sizes, in pixels, as NONCLIENTMETRICS has them: the
  // caption buttons' and the menu bar's height one less than the bar's.
  // They grow with the screen's DPI, as on Windows.
  int border = 1;  // to which the frame's 3-pixel edge adds
  int captionWidth = Scale96(18), captionHeight = Scale96(18);
  int smCaptionWidth = Scale96(14), smCaptionHeight = Scale96(14);
  int menuWidth = Scale96(18), menuHeight = Scale96(18);
  int scrollWidth = Scale96(16), scrollHeight = Scale96(16);
  LOGFONTW captionFont = ClassicFont(true), smCaptionFont = ClassicFont(true), menuFont = ClassicFont(false),
           statusFont = ClassicFont(false), messageFont = ClassicFont(false), iconFont = ClassicFont(false);
  Theme() {
    // With visual styles, the flat look's faces and borders (theme.cpp).
    for (int i = 0; i < kNumColors; i++) colors[i] = winport::theme::SysColor(i, kClassicColors[i]);
  }
};

std::wstring RegText(HKEY k, const wchar_t* name, DWORD* type, std::vector<BYTE>* raw) {
  BYTE buf[512];
  DWORD cb = sizeof buf - sizeof(WCHAR);
  *type = REG_NONE;
  if (RegQueryValueExW(k, name, NULL, type, buf, &cb) != ERROR_SUCCESS) return std::wstring();
  if (raw) raw->assign(buf, buf + cb);
  if (*type != REG_SZ) return std::wstring();
  memset(buf + cb, 0, sizeof(WCHAR));
  return std::wstring((const WCHAR*)buf);
}

// A size: pixels at 96 dots per inch, or negative twips (as Windows
// writes it), at the screen's DPI.
void ReadSize(HKEY k, const wchar_t* name, int* out) {
  DWORD type;
  std::vector<BYTE> raw;
  std::wstring t = RegText(k, name, &type, &raw);
  long v;
  if (type == REG_SZ && !t.empty()) v = wcstol(t.c_str(), NULL, 10);
  else if (type == REG_DWORD && raw.size() == 4) v = (long)*(const int32_t*)raw.data();
  else return;
  if (v < -3000 || v > 200) return;
  *out = v < 0 ? MulDiv((int)-v, SystemDpi(), 1440) : Scale96((int)v);
}

// A font: "face,points[,bold][,italic]", or a Windows LOGFONTW (its
// characters 16 bits wide).
void ReadFont(HKEY k, const wchar_t* name, LOGFONTW* out) {
  DWORD type;
  std::vector<BYTE> raw;
  std::wstring t = RegText(k, name, &type, &raw);
  if (type == REG_BINARY && raw.size() >= 28 + 2) {
    LOGFONTW lf = {};
    memcpy(&lf, raw.data(), 28);  // the numbers, which the layouts share
    for (size_t i = 0; i < LF_FACESIZE - 1 && 28 + 2 * i + 1 < raw.size(); i++) {
      WCHAR c = raw[28 + 2 * i] | (raw[28 + 2 * i + 1] << 8);
      if (!c) break;
      lf.lfFaceName[i] = c;
    }
    if (lf.lfFaceName[0]) *out = lf;
    return;
  }
  if (type != REG_SZ || t.empty()) return;
  LOGFONTW lf = *out;
  std::vector<std::wstring> parts;
  size_t start = 0;
  for (;;) {
    size_t comma = t.find(L',', start);
    std::wstring p = t.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
    while (!p.empty() && p[0] == L' ') p.erase(0, 1);
    while (!p.empty() && p.back() == L' ') p.pop_back();
    parts.push_back(p);
    if (comma == std::wstring::npos) break;
    start = comma + 1;
  }
  if (parts[0].empty() || parts[0].size() >= LF_FACESIZE) return;
  wcscpy(lf.lfFaceName, parts[0].c_str());
  lf.lfWeight = FW_NORMAL;
  lf.lfItalic = FALSE;
  for (size_t i = 1; i < parts.size(); i++) {
    if (_wcsicmp(parts[i].c_str(), L"bold") == 0) lf.lfWeight = FW_BOLD;
    else if (_wcsicmp(parts[i].c_str(), L"italic") == 0) lf.lfItalic = TRUE;
    else if (int pt = (int)wcstol(parts[i].c_str(), NULL, 10)) lf.lfHeight = -MulDiv(pt, SystemDpi(), 72);
  }
  *out = lf;
}

void ReadTheme(Theme* t) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Colors", 0, KEY_READ, &k) == ERROR_SUCCESS) {
    for (int i = 0; i < kNumColors; i++) {
      DWORD type;
      std::wstring text = RegText(k, kColorNames[i], &type, NULL);
      unsigned r, g, b;
      if (type == REG_SZ && swscanf(text.c_str(), L"%u %u %u", &r, &g, &b) == 3 && r < 256 && g < 256 && b < 256)
        t->colors[i] = RGB(r, g, b);
    }
    RegCloseKey(k);
  }
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &k) == ERROR_SUCCESS) {
    DWORD type;
    std::vector<BYTE> raw;
    RegText(k, L"UserPreferencesMask", &type, &raw);
    if (type == REG_BINARY && !raw.empty()) t->gradient = (raw[0] & 0x10) != 0;
    std::wstring text = RegText(k, L"GradientCaptions", &type, &raw);
    if (type == REG_SZ) t->gradient = wcstol(text.c_str(), NULL, 10) != 0;
    else if (type == REG_DWORD && raw.size() == 4) t->gradient = *(const DWORD*)raw.data() != 0;
    RegCloseKey(k);
  }
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ, &k) == ERROR_SUCCESS) {
    ReadSize(k, L"BorderWidth", &t->border);
    ReadSize(k, L"CaptionWidth", &t->captionWidth);
    ReadSize(k, L"CaptionHeight", &t->captionHeight);
    ReadSize(k, L"SmCaptionWidth", &t->smCaptionWidth);
    ReadSize(k, L"SmCaptionHeight", &t->smCaptionHeight);
    ReadSize(k, L"MenuWidth", &t->menuWidth);
    ReadSize(k, L"MenuHeight", &t->menuHeight);
    ReadSize(k, L"ScrollWidth", &t->scrollWidth);
    ReadSize(k, L"ScrollHeight", &t->scrollHeight);
    ReadFont(k, L"CaptionFont", &t->captionFont);
    ReadFont(k, L"SmCaptionFont", &t->smCaptionFont);
    ReadFont(k, L"MenuFont", &t->menuFont);
    ReadFont(k, L"StatusFont", &t->statusFont);
    ReadFont(k, L"MessageFont", &t->messageFont);
    ReadFont(k, L"IconFont", &t->iconFont);
    RegCloseKey(k);
  }
  // Sizes that leave no room are kept out.
  t->captionHeight = std::max(t->captionHeight, 8);
  t->smCaptionHeight = std::max(t->smCaptionHeight, 8);
  t->menuHeight = std::max(t->menuHeight, 8);
  t->scrollWidth = std::max(t->scrollWidth, 8);
  t->scrollHeight = std::max(t->scrollHeight, 8);
  t->border = std::min(t->border, 50);
}

std::mutex& ThemeLock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::atomic<unsigned> g_themeGeneration{0};
std::atomic<bool> g_themePending{false};
Theme* g_fileTheme = NULL;  // the look as the file last had it

// The registry file changed: once it has been quiet a moment (an editor
// may write it in steps), every thread looking for messages is woken to
// apply it.
void FileChanged() {
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  g_themePending = true;
  WakeAllQueues();
}

// Watches the directory the registry file is in, since an editor saving
// it may put a new file in its place.
void WatchFile() {
  std::string file = RegistryPath();
  size_t slash = file.rfind('/');
  std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : file.substr(0, slash);
  std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
  mkdir(dir.c_str(), 0700);
#if defined(__linux__)
  int fd = inotify_init1(IN_CLOEXEC);
  if (fd < 0) return;
  if (inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE) < 0) {
    close(fd);
    return;
  }
  std::thread([fd, base] {
    alignas(inotify_event) char buf[4096];
    for (;;) {
      ssize_t n = read(fd, buf, sizeof buf);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return;
      bool ours = false;
      for (char* p = buf; p < buf + n;) {
        inotify_event* e = (inotify_event*)p;
        if (e->len && base == e->name) ours = true;
        p += sizeof *e + e->len;
      }
      if (ours) FileChanged();
    }
  }).detach();
#elif defined(WINPORT_KQUEUE)
  int kq = kqueue();
  int dfd = open(dir.c_str(), O_EVTONLY | O_CLOEXEC);
  if (kq < 0 || dfd < 0) {
    if (kq >= 0) close(kq);
    if (dfd >= 0) close(dfd);
    return;
  }
  std::thread([kq, dfd, file] {
    for (;;) {
      // The directory, for a file put in its place, and the file as it
      // is now, for one written where it is.
      struct kevent ev[2];
      int n = 0;
      EV_SET(&ev[n++], dfd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, NULL);
      int ffd = open(file.c_str(), O_EVTONLY | O_CLOEXEC);
      if (ffd >= 0)
        EV_SET(&ev[n++], ffd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
               NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
      struct kevent got;
      int r = kevent(kq, ev, n, &got, 1, NULL);
      if (ffd >= 0) close(ffd);
      if (r < 0 && errno != EINTR) return;
      if (r > 0) FileChanged();
    }
  }).detach();
#elif defined(__HAIKU__)
  // Haiku's node monitor needs a BLooper; looking at the file four times
  // a second is enough for an editor that saves it.
  std::thread([file] {
    struct stat was;
    bool had = stat(file.c_str(), &was) == 0;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      struct stat now;
      bool has = stat(file.c_str(), &now) == 0;
      if (has != had || (has && (now.st_ino != was.st_ino || now.st_size != was.st_size ||
                                 now.st_mtim.tv_sec != was.st_mtim.tv_sec ||
                                 now.st_mtim.tv_nsec != was.st_mtim.tv_nsec)))
        FileChanged();
      had = has;
      was = now;
    }
  }).detach();
#endif
}

Theme& CurrentTheme() {
  static Theme* t = NULL;
  static std::once_flag once;
  std::call_once(once, [] {
    Theme* n = new Theme;
    ReadTheme(n);
    g_fileTheme = new Theme(*n);
    t = n;
    WatchFile();
  });
  return *t;
}

bool SameFont(const LOGFONTW& a, const LOGFONTW& b) {
  return memcmp(&a, &b, offsetof(LOGFONTW, lfFaceName)) == 0 && wcscmp(a.lfFaceName, b.lfFaceName) == 0;
}

enum { kColorsChanged = 1, kMetricsChanged = 2, kGradientChanged = 4 };

// What the file changed from was to now, put in cur where cur does not
// have it already (a change the program made itself stays until the
// file changes that value); which kinds of thing cur changed.
int Merge(const Theme& was, const Theme& now, Theme* cur) {
  int changed = 0;
  for (int i = 0; i < kNumColors; i++)
    if (was.colors[i] != now.colors[i] && cur->colors[i] != now.colors[i]) {
      cur->colors[i] = now.colors[i];
      changed |= kColorsChanged;
    }
  if (was.gradient != now.gradient && cur->gradient != now.gradient) {
    cur->gradient = now.gradient;
    changed |= kGradientChanged;
  }
  int Theme::*sizes[] = {&Theme::border,          &Theme::captionWidth, &Theme::captionHeight,
                         &Theme::smCaptionWidth,  &Theme::smCaptionHeight, &Theme::menuWidth,
                         &Theme::menuHeight,      &Theme::scrollWidth,  &Theme::scrollHeight};
  for (int Theme::*f : sizes)
    if (was.*f != now.*f && cur->*f != now.*f) {
      cur->*f = now.*f;
      changed |= kMetricsChanged;
    }
  LOGFONTW Theme::*fonts[] = {&Theme::captionFont, &Theme::smCaptionFont, &Theme::menuFont,
                              &Theme::statusFont,  &Theme::messageFont,   &Theme::iconFont};
  for (LOGFONTW Theme::*f : fonts)
    if (!SameFont(was.*f, now.*f) && !SameFont(cur->*f, now.*f)) {
      cur->*f = now.*f;
      changed |= kMetricsChanged;
    }
  return changed;
}

// SPIF_UPDATEINIFILE: the sizes and fonts written back, as Windows keeps
// them (sizes in negative twips).
void SetText(HKEY k, const wchar_t* name, const std::wstring& v) {
  RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(WCHAR)));
}
std::wstring Number(int v) { return Widen(std::to_string(v).c_str()); }
void WriteMetrics(const Theme& t) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k,
                      NULL) != ERROR_SUCCESS)
    return;
  const struct {
    const wchar_t* name;
    int value;
  } sizes[] = {{L"BorderWidth", t.border},         {L"CaptionWidth", t.captionWidth},
               {L"CaptionHeight", t.captionHeight}, {L"SmCaptionWidth", t.smCaptionWidth},
               {L"SmCaptionHeight", t.smCaptionHeight}, {L"MenuWidth", t.menuWidth},
               {L"MenuHeight", t.menuHeight},       {L"ScrollWidth", t.scrollWidth},
               {L"ScrollHeight", t.scrollHeight}};
  for (auto& s : sizes) SetText(k, s.name, Number(-MulDiv(s.value, 1440, SystemDpi())));
  const struct {
    const wchar_t* name;
    const LOGFONTW* font;
  } fonts[] = {{L"CaptionFont", &t.captionFont}, {L"SmCaptionFont", &t.smCaptionFont},
               {L"MenuFont", &t.menuFont},       {L"StatusFont", &t.statusFont},
               {L"MessageFont", &t.messageFont}, {L"IconFont", &t.iconFont}};
  for (auto& f : fonts) {
    int h = f.font->lfHeight < 0 ? -f.font->lfHeight : f.font->lfHeight;
    std::wstring v = f.font->lfFaceName;
    v += L"," + Number(MulDiv(h, 72, SystemDpi()));
    if (f.font->lfWeight >= FW_BOLD) v += L",bold";
    if (f.font->lfItalic) v += L",italic";
    SetText(k, f.name, v);
  }
  RegCloseKey(k);
}
void WriteGradient(BOOL on) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL) !=
      ERROR_SUCCESS)
    return;
  DWORD v = on ? 1 : 0;
  RegSetValueExW(k, L"GradientCaptions", 0, REG_DWORD, (const BYTE*)&v, sizeof v);
  RegCloseKey(k);
}

// Every top-level window hears of a change of the look, then draws again
// (the frame recalculated when sizes changed).
void ThemeChanged(UINT msg, WPARAM wp, bool frames) {
  g_themeGeneration++;
  std::vector<HWND> tops;
  {
    UserGuard g(UserLock());
    for (Window* w : TopLevels()) tops.push_back(w->hwnd);
  }
  if (msg) for (HWND h : tops) SendMessageW(h, msg, wp, 0);
  for (HWND h : tops) {
    if (frames)
      SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  }
}

}  // namespace

unsigned winport::ThemeGeneration() { return g_themeGeneration; }
bool winport::ThemeFilePending() { return g_themePending; }

// The registry file has changed: what it changed in the look is applied,
// as SetSysColors and SPI_SETNONCLIENTMETRICS would.
void winport::CheckThemeFile() {
  if (!g_themePending || !g_themePending.exchange(false)) return;
  Theme now;
  ReadTheme(&now);
  Theme& cur = CurrentTheme();
  int changed;
  {
    std::lock_guard<std::mutex> g(ThemeLock());
    changed = Merge(*g_fileTheme, now, &cur);
    *g_fileTheme = now;
  }
  if (changed & kColorsChanged) ThemeChanged(WM_SYSCOLORCHANGE, 0, false);
  if (changed & kMetricsChanged) ThemeChanged(WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, true);
  else if (changed & kGradientChanged) ThemeChanged(WM_SETTINGCHANGE, SPI_SETGRADIENTCAPTIONS, false);
}

extern "C" {

DWORD WINAPI GetSysColor(int index) {
  if (index < 0 || index >= kNumColors) return 0;
  Theme& t = CurrentTheme();
  std::lock_guard<std::mutex> g(ThemeLock());
  return t.colors[index];
}

// The system color brushes follow the colors, so a change shows once the
// windows draw again; each top-level window hears of it first.
BOOL WINAPI SetSysColors(int n, const INT* indexes, const COLORREF* colors) {
  if (n < 0 || (n && (!indexes || !colors))) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Theme& t = CurrentTheme();
  {
    std::lock_guard<std::mutex> g(ThemeLock());
    for (int i = 0; i < n; i++)
      if (indexes[i] >= 0 && indexes[i] < kNumColors) t.colors[indexes[i]] = colors[i] & 0xFFFFFF;
  }
  ThemeChanged(WM_SYSCOLORCHANGE, 0, false);
  return TRUE;
}

// The metrics, from the look's sizes, on the screen the backend reports.
int WINAPI GetSystemMetrics(int index) {
  Theme& t = CurrentTheme();
  int frame = t.border + 3;
  switch (index) {
    case SM_CXSCREEN: case SM_CXFULLSCREEN: case SM_CXMAXIMIZED:
      return GetBackend()->ScreenSize().cx;
    case SM_CYSCREEN: case SM_CYMAXIMIZED: return GetBackend()->ScreenSize().cy;
    case SM_XVIRTUALSCREEN: return VirtualScreen().left;
    case SM_YVIRTUALSCREEN: return VirtualScreen().top;
    case SM_CXVIRTUALSCREEN: return VirtualScreen().right - VirtualScreen().left;
    case SM_CYVIRTUALSCREEN: return VirtualScreen().bottom - VirtualScreen().top;
    case SM_CMONITORS: return (int)GetBackend()->Monitors().size();
    case SM_CYFULLSCREEN: return GetBackend()->ScreenSize().cy - (t.captionHeight + 1) - 30;
    case SM_CXVSCROLL: case SM_CYVSCROLL: return t.scrollWidth;
    case SM_CYHSCROLL: case SM_CXHSCROLL: return t.scrollHeight;
    case SM_CYVTHUMB: return t.scrollWidth;
    case SM_CXHTHUMB: return t.scrollHeight;
    case SM_CYCAPTION: return t.captionHeight + 1;
    case SM_CXBORDER: case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return 3;
    case SM_CXFRAME: case SM_CYFRAME: return frame;
    case SM_CXICON: case SM_CYICON: case SM_CXCURSOR: case SM_CYCURSOR: return Scale96(32);
    case SM_CXSMICON: case SM_CYSMICON: return Scale96(16);
    case SM_CYMENU: return t.menuHeight + 1;
    case SM_CXMENUSIZE: return t.menuWidth;
    case SM_CYMENUSIZE: return t.menuHeight;
    case SM_CXMIN: case SM_CXMINTRACK: return Scale96(112);
    case SM_CYMIN: case SM_CYMINTRACK: return t.captionHeight + 1 + 2 * frame;
    case SM_CXSIZE: return t.captionWidth;
    case SM_CYSIZE: return t.captionHeight;
    case SM_CXSMSIZE: return t.smCaptionWidth;
    case SM_CYSMSIZE: return t.smCaptionHeight;
    case SM_CYSMCAPTION: return t.smCaptionHeight + 1;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return Scale96(4);
    case SM_CXICONSPACING: case SM_CYICONSPACING: return Scale96(75);
    case SM_CXDRAG: case SM_CYDRAG: return Scale96(4);
    case SM_CXEDGE: case SM_CYEDGE: return 2;
    case SM_CXMENUCHECK: case SM_CYMENUCHECK: return Scale96(13);
    case SM_CMOUSEBUTTONS: return 3;
    case SM_MOUSEPRESENT: case SM_MOUSEWHEELPRESENT: return 1;
    case SM_SAMEDISPLAYFORMAT: return 1;
    default: return 0;
  }
}

static void FontToW(const LOGFONTA& a, LOGFONTW* w) {
  memcpy(w, &a, offsetof(LOGFONTA, lfFaceName));
  memset(w->lfFaceName, 0, sizeof w->lfFaceName);
  std::wstring f = Widen(a.lfFaceName);
  wcsncpy(w->lfFaceName, f.c_str(), LF_FACESIZE - 1);
}

static void FontToA(const LOGFONTW& w, LOGFONTA* a) {
  memcpy(a, &w, offsetof(LOGFONTA, lfFaceName));
  memset(a->lfFaceName, 0, sizeof a->lfFaceName);
  strncpy(a->lfFaceName, Narrow(w.lfFaceName).c_str(), LF_FACESIZE - 1);
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT uiParam, PVOID pv, UINT winIni) {
  switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
      NONCLIENTMETRICSW* m = (NONCLIENTMETRICSW*)pv;
      if (!m) return FALSE;
      UINT size = m->cbSize ? m->cbSize : sizeof *m;
      Theme& t = CurrentTheme();
      std::lock_guard<std::mutex> g(ThemeLock());
      memset(m, 0, sizeof *m);
      m->cbSize = size;
      m->iBorderWidth = t.border;
      m->iScrollWidth = t.scrollWidth;
      m->iScrollHeight = t.scrollHeight;
      m->iCaptionWidth = t.captionWidth;
      m->iCaptionHeight = t.captionHeight;
      m->lfCaptionFont = t.captionFont;
      m->iSmCaptionWidth = t.smCaptionWidth;
      m->iSmCaptionHeight = t.smCaptionHeight;
      m->lfSmCaptionFont = t.smCaptionFont;
      m->iMenuWidth = t.menuWidth;
      m->iMenuHeight = t.menuHeight;
      m->lfMenuFont = t.menuFont;
      m->lfStatusFont = t.statusFont;
      m->lfMessageFont = t.messageFont;
      return TRUE;
    }
    case SPI_SETNONCLIENTMETRICS: {
      const NONCLIENTMETRICSW* m = (const NONCLIENTMETRICSW*)pv;
      if (!m) return FALSE;
      Theme& t = CurrentTheme();
      {
        std::lock_guard<std::mutex> g(ThemeLock());
        t.border = m->iBorderWidth;
        t.scrollWidth = std::max(8, m->iScrollWidth);
        t.scrollHeight = std::max(8, m->iScrollHeight);
        t.captionWidth = m->iCaptionWidth;
        t.captionHeight = std::max(8, m->iCaptionHeight);
        t.captionFont = m->lfCaptionFont;
        t.smCaptionWidth = m->iSmCaptionWidth;
        t.smCaptionHeight = std::max(8, m->iSmCaptionHeight);
        t.smCaptionFont = m->lfSmCaptionFont;
        t.menuWidth = m->iMenuWidth;
        t.menuHeight = std::max(8, m->iMenuHeight);
        t.menuFont = m->lfMenuFont;
        t.statusFont = m->lfStatusFont;
        t.messageFont = m->lfMessageFont;
      }
      if (winIni & SPIF_UPDATEINIFILE) {
        Theme copy;
        {
          std::lock_guard<std::mutex> g(ThemeLock());
          copy = t;
        }
        WriteMetrics(copy);
      }
      ThemeChanged(WM_SETTINGCHANGE, SPI_SETNONCLIENTMETRICS, true);
      return TRUE;
    }
    case SPI_GETICONTITLELOGFONT: {
      if (pv == NULL) return FALSE;
      Theme& t = CurrentTheme();
      std::lock_guard<std::mutex> g(ThemeLock());
      *(LOGFONTW*)pv = t.iconFont;
      return TRUE;
    }
    case SPI_GETWORKAREA:
      if (pv == NULL) return FALSE;
      *(LPRECT)pv = GetBackend()->WorkArea();
      return TRUE;
    case SPI_GETWHEELSCROLLLINES:
      if (pv == NULL) return FALSE;
      *(UINT*)pv = 3;
      return TRUE;
    case SPI_GETGRADIENTCAPTIONS:
      if (pv == NULL) return FALSE;
      *(BOOL*)pv = CurrentTheme().gradient;
      return TRUE;
    case SPI_SETGRADIENTCAPTIONS:
      CurrentTheme().gradient = pv != NULL;
      if (winIni & SPIF_UPDATEINIFILE) WriteGradient(pv != NULL);
      ThemeChanged(WM_SETTINGCHANGE, SPI_SETGRADIENTCAPTIONS, false);
      return TRUE;
    case SPI_GETDRAGFULLWINDOWS:
    case SPI_GETFLATMENU:
    case SPI_GETMENUUNDERLINES:
      if (pv == NULL) return FALSE;
      *(BOOL*)pv = action == SPI_GETMENUUNDERLINES;
      return TRUE;
    default:
      (void)uiParam;
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
  }
}
// The screen's mode does not change (ChangeDisplaySettingsExW).
LONG WINAPI ChangeDisplaySettingsW(DEVMODEW* dm, DWORD flags) {
  return ChangeDisplaySettingsExW(NULL, dm, NULL, flags, NULL);
}
LONG WINAPI ChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags) {
  return ChangeDisplaySettingsExA(NULL, dm, NULL, flags, NULL);
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT uiParam, PVOID pv, UINT winIni) {
  if (action == SPI_GETNONCLIENTMETRICS) {
    NONCLIENTMETRICSA* a = (NONCLIENTMETRICSA*)pv;
    NONCLIENTMETRICSW w = {sizeof w};
    if (!a || !SystemParametersInfoW(action, sizeof w, &w, winIni)) return FALSE;
    UINT size = a->cbSize ? a->cbSize : sizeof *a;
    a->cbSize = size;
    a->iBorderWidth = w.iBorderWidth;
    a->iScrollWidth = w.iScrollWidth;
    a->iScrollHeight = w.iScrollHeight;
    a->iCaptionWidth = w.iCaptionWidth;
    a->iCaptionHeight = w.iCaptionHeight;
    FontToA(w.lfCaptionFont, &a->lfCaptionFont);
    a->iSmCaptionWidth = w.iSmCaptionWidth;
    a->iSmCaptionHeight = w.iSmCaptionHeight;
    FontToA(w.lfSmCaptionFont, &a->lfSmCaptionFont);
    a->iMenuWidth = w.iMenuWidth;
    a->iMenuHeight = w.iMenuHeight;
    FontToA(w.lfMenuFont, &a->lfMenuFont);
    FontToA(w.lfStatusFont, &a->lfStatusFont);
    FontToA(w.lfMessageFont, &a->lfMessageFont);
    return TRUE;
  }
  if (action == SPI_SETNONCLIENTMETRICS) {
    const NONCLIENTMETRICSA* a = (const NONCLIENTMETRICSA*)pv;
    if (!a) return FALSE;
    NONCLIENTMETRICSW w = {sizeof w};
    w.iBorderWidth = a->iBorderWidth;
    w.iScrollWidth = a->iScrollWidth;
    w.iScrollHeight = a->iScrollHeight;
    w.iCaptionWidth = a->iCaptionWidth;
    w.iCaptionHeight = a->iCaptionHeight;
    FontToW(a->lfCaptionFont, &w.lfCaptionFont);
    w.iSmCaptionWidth = a->iSmCaptionWidth;
    w.iSmCaptionHeight = a->iSmCaptionHeight;
    FontToW(a->lfSmCaptionFont, &w.lfSmCaptionFont);
    w.iMenuWidth = a->iMenuWidth;
    w.iMenuHeight = a->iMenuHeight;
    FontToW(a->lfMenuFont, &w.lfMenuFont);
    FontToW(a->lfStatusFont, &w.lfStatusFont);
    FontToW(a->lfMessageFont, &w.lfMessageFont);
    return SystemParametersInfoW(action, sizeof w, &w, winIni);
  }
  if (action == SPI_GETICONTITLELOGFONT) {
    LOGFONTW w;
    if (!pv || !SystemParametersInfoW(action, uiParam, &w, winIni)) return FALSE;
    FontToA(w, (LOGFONTA*)pv);
    return TRUE;
  }
  // The other actions answered here carry no text.
  return SystemParametersInfoW(action, uiParam, pv, winIni);
}

BOOL WINAPI MessageBeep(UINT) { return TRUE; }

}  // extern "C"

// The placeholders in stubs.cpp report here.
extern "C" void _WinportStub(const char* name, bool* reported) {
  if (!*reported && getenv("WINPORT_DEBUG") != NULL) fprintf(stderr, "winport: %s is not implemented\n", name);
  *reported = true;
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}
