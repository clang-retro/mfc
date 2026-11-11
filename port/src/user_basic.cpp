// user_basic.cpp - the parts of user32 that need no windows: rectangles,
// string resources, registered messages and formats, system colors and
// metrics, and message boxes before the window system is up.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <mutex>
#include <vector>

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
  for (UINT i = 0; i < 16 && p + 2 <= end; i++) {
    UINT n = p[0] | (p[1] << 8);
    p += 2;
    if (i == id % 16) {
      if (n == 0 || p + 2 * n > end) return false;
      std::vector<WCHAR> u;
      // UTF-16 to wchar_t, joining surrogate pairs.
      out->clear();
      for (UINT k = 0; k < n; k++) {
        UINT c = p[2 * k] | (p[2 * k + 1] << 8);
        if (c >= 0xd800 && c < 0xdc00 && k + 1 < n) {
          UINT d = p[2 * k + 2] | (p[2 * k + 3] << 8);
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

// The classic Windows scheme.
DWORD WINAPI GetSysColor(int index) {
  static const COLORREF colors[] = {
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
  if (index < 0 || index >= (int)(sizeof(colors) / sizeof(colors[0]))) return 0;
  return colors[index];
}

// The classic metrics, on the screen the backend reports.
int WINAPI GetSystemMetrics(int index) {
  switch (index) {
    case SM_CXSCREEN: case SM_CXFULLSCREEN: case SM_CXVIRTUALSCREEN: case SM_CXMAXIMIZED:
      return GetBackend()->ScreenSize().cx;
    case SM_CYSCREEN: case SM_CYVIRTUALSCREEN: case SM_CYMAXIMIZED: return GetBackend()->ScreenSize().cy;
    case SM_CYFULLSCREEN: return GetBackend()->ScreenSize().cy - 19 - 30;
    case SM_CXVSCROLL: case SM_CYHSCROLL: case SM_CXHSCROLL: case SM_CYVSCROLL: return 16;
    case SM_CYCAPTION: return 19;
    case SM_CXBORDER: case SM_CYBORDER: return 1;
    case SM_CXDLGFRAME: case SM_CYDLGFRAME: return 3;
    case SM_CXFRAME: case SM_CYFRAME: return 4;
    case SM_CYVTHUMB: case SM_CXHTHUMB: return 16;
    case SM_CXICON: case SM_CYICON: case SM_CXCURSOR: case SM_CYCURSOR: return 32;
    case SM_CXSMICON: case SM_CYSMICON: return 16;
    case SM_CYMENU: case SM_CYMENUSIZE: case SM_CXMENUSIZE: return 19;
    case SM_CXMIN: return 112;
    case SM_CYMIN: return 27;
    case SM_CXSIZE: case SM_CYSIZE: case SM_CXSMSIZE: case SM_CYSMSIZE: return 18;
    case SM_CYSMCAPTION: return 15;
    case SM_CXMINTRACK: return 112;
    case SM_CYMINTRACK: return 27;
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    case SM_CXICONSPACING: case SM_CYICONSPACING: return 75;
    case SM_CXDRAG: case SM_CYDRAG: return 4;
    case SM_CXEDGE: case SM_CYEDGE: return 2;
    case SM_CXMENUCHECK: case SM_CYMENUCHECK: return 13;
    case SM_CMOUSEBUTTONS: return 3;
    case SM_MOUSEPRESENT: case SM_MOUSEWHEELPRESENT: return 1;
    case SM_CMONITORS: return 1;
    case SM_SAMEDISPLAYFORMAT: return 1;
    default: return 0;
  }
}

// The classic fonts: 8-point MS Sans Serif, bold in captions.
static LOGFONTW ClassicFont(bool bold) {
  LOGFONTW lf = {};
  lf.lfHeight = -11;
  lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  wcscpy(lf.lfFaceName, L"MS Sans Serif");
  return lf;
}

static void FontToA(const LOGFONTW& w, LOGFONTA* a) {
  memcpy(a, &w, offsetof(LOGFONTA, lfFaceName));
  memset(a->lfFaceName, 0, sizeof a->lfFaceName);
  strncpy(a->lfFaceName, Narrow(w.lfFaceName).c_str(), LF_FACESIZE - 1);
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT uiParam, PVOID pv, UINT) {
  switch (action) {
    case SPI_GETNONCLIENTMETRICS: {
      NONCLIENTMETRICSW* m = (NONCLIENTMETRICSW*)pv;
      if (!m) return FALSE;
      UINT size = m->cbSize ? m->cbSize : sizeof *m;
      memset(m, 0, sizeof *m);
      m->cbSize = size;
      m->iBorderWidth = 1;
      m->iScrollWidth = m->iScrollHeight = GetSystemMetrics(SM_CXVSCROLL);
      m->iCaptionWidth = m->iCaptionHeight = GetSystemMetrics(SM_CYCAPTION) - 1;
      m->lfCaptionFont = ClassicFont(true);
      m->iSmCaptionWidth = m->iSmCaptionHeight = GetSystemMetrics(SM_CYSMCAPTION) - 1;
      m->lfSmCaptionFont = ClassicFont(true);
      m->iMenuWidth = m->iMenuHeight = GetSystemMetrics(SM_CYMENU) - 1;
      m->lfMenuFont = ClassicFont(false);
      m->lfStatusFont = ClassicFont(false);
      m->lfMessageFont = ClassicFont(false);
      return TRUE;
    }
    case SPI_GETICONTITLELOGFONT:
      if (pv == NULL) return FALSE;
      *(LOGFONTW*)pv = ClassicFont(false);
      return TRUE;
    case SPI_GETWORKAREA:
      if (pv == NULL) return FALSE;
      SetRect((LPRECT)pv, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
      return TRUE;
    case SPI_GETWHEELSCROLLLINES:
      if (pv == NULL) return FALSE;
      *(UINT*)pv = 3;
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
  if (action == SPI_GETICONTITLELOGFONT) {
    LOGFONTW w;
    if (!pv || !SystemParametersInfoW(action, uiParam, &w, winIni)) return FALSE;
    FontToA(w, (LOGFONTA*)pv);
    return TRUE;
  }
  // The other actions answered here carry no text.
  return SystemParametersInfoW(action, uiParam, pv, winIni);
}

// Message boxes, until the window system can show one: the text goes to
// standard error, and the default button is the answer.
int WINAPI MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type) {
  fprintf(stderr, "%s%s%s\n", caption ? Narrow(caption).c_str() : "", caption ? ": " : "",
          text ? Narrow(text).c_str() : "");
  UINT kind = type & MB_TYPEMASK;
  UINT def = (type & MB_DEFMASK) >> 8;
  static const int answers[][3] = {
      {IDOK, IDOK, IDOK},           {IDOK, IDCANCEL, IDCANCEL},   {IDABORT, IDRETRY, IDIGNORE},
      {IDYES, IDNO, IDCANCEL},      {IDYES, IDNO, IDNO},          {IDRETRY, IDCANCEL, IDCANCEL},
      {IDCANCEL, IDTRYAGAIN, IDCONTINUE},
  };
  if (kind > 6) kind = 0;
  return answers[kind][def < 3 ? def : 0];
}
int WINAPI MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
  return MessageBoxW(hwnd, text ? Widen(text).c_str() : NULL, caption ? Widen(caption).c_str() : NULL, type);
}
BOOL WINAPI MessageBeep(UINT) { return TRUE; }

}  // extern "C"

// The placeholders in stubs.cpp report here.
extern "C" void _WinportStub(const char* name, bool* reported) {
  if (!*reported && getenv("WINPORT_DEBUG") != NULL) fprintf(stderr, "winport: %s is not implemented\n", name);
  *reported = true;
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}
