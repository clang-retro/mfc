// user_core.cpp - windows: classes, creation and destruction, the window
// tree, position and size, showing and hiding, and a window's data.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <string.h>

#include <algorithm>
#include <unordered_map>

using namespace winport;

namespace winport {

std::recursive_mutex& UserLock() {
  static std::recursive_mutex* m = new std::recursive_mutex;
  return *m;
}

void Surface::Resize(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w == width && h == height) return;
  std::vector<DWORD> n((size_t)w * h, 0xC0C0C0);
  for (int y = 0; y < h && y < height; y++)
    memcpy(&n[(size_t)y * w], &pixels[(size_t)y * width], sizeof(DWORD) * (w < width ? w : width));
  pixels.swap(n);
  width = w;
  height = h;
}

namespace {

// Handles are small numbers that are never reused, so a stale handle
// is never mistaken for a new window.
std::unordered_map<UINT_PTR, Window*>& Handles() {
  static auto* m = new std::unordered_map<UINT_PTR, Window*>;
  return *m;
}
UINT_PTR g_nextHandle = 0x10020;

Window* g_desktop = NULL;

std::vector<Class*>& Classes() {
  static auto* v = new std::vector<Class*>;
  return *v;
}

std::wstring Lower(std::wstring s) {
  for (wchar_t& c : s)
    if (c >= 'A' && c <= 'Z') c = (wchar_t)(c - 'A' + 'a');
  return s;
}

// A class or window name given as a string or as an atom.
std::wstring NameOf(LPCWSTR p) {
  if (IS_INTRESOURCE(p)) {
    wchar_t buf[256];
    if (GlobalGetAtomNameW((ATOM)(UINT_PTR)p, buf, 256)) return buf;
    return L"#" + std::to_wstring((UINT_PTR)p);
  }
  return p;
}
std::wstring NameOfA(LPCSTR p) {
  if (IS_INTRESOURCE(p)) return NameOf((LPCWSTR)p);
  return Widen(p);
}

Window* NewWindow() {
  Window* w = new Window;
  w->hwnd = (HWND)g_nextHandle;
  g_nextHandle += 4;
  Handles()[(UINT_PTR)w->hwnd] = w;
  return w;
}

std::vector<Window*>& Siblings(Window* w) { return w->parent ? w->parent->children : TopLevels(); }

void Unlink(Window* w) {
  std::vector<Window*>& v = Siblings(w);
  v.erase(std::remove(v.begin(), v.end(), w), v.end());
}

// Puts w in its siblings' z-order after `after` (HWND_TOP, HWND_BOTTOM,
// a sibling, or HWND_TOPMOST / HWND_NOTOPMOST for top-level windows).
void LinkAfter(Window* w, HWND after) {
  std::vector<Window*>& v = Siblings(w);
  v.erase(std::remove(v.begin(), v.end(), w), v.end());
  size_t pos = 0;
  if (after == HWND_BOTTOM) {
    pos = v.size();
  } else if (after == HWND_TOP || after == HWND_TOPMOST || after == HWND_NOTOPMOST) {
    pos = 0;
    // Without WS_EX_TOPMOST a top-level window goes below the topmost ones.
    if (!w->parent && !(w->exStyle & WS_EX_TOPMOST))
      while (pos < v.size() && (v[pos]->exStyle & WS_EX_TOPMOST)) pos++;
  } else {
    Window* a = GetWindow(after);
    auto it = std::find(v.begin(), v.end(), a);
    pos = it == v.end() ? 0 : (size_t)(it - v.begin()) + 1;
  }
  v.insert(v.begin() + pos, w);
}

}  // namespace

std::vector<Window*>& TopLevels() {
  static auto* v = new std::vector<Window*>;
  return *v;
}

Window* GetWindow(HWND h) {
  if (h == NULL) return NULL;
  UserGuard g(UserLock());
  auto it = Handles().find((UINT_PTR)h);
  return it == Handles().end() ? NULL : it->second;
}

Window* GetWindowOrFail(HWND h) {
  Window* w = GetWindow(h);
  if (!w) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
  return w;
}

Window* Desktop() {
  UserGuard g(UserLock());
  if (!g_desktop) {
    g_desktop = NewWindow();
    g_desktop->style = WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN;
    SIZE s = GetBackend()->ScreenSize();
    g_desktop->rect = MakeRect(0, 0, s.cx, s.cy);
    g_desktop->client = g_desktop->rect;
    g_desktop->text = L"";
    g_desktop->proc = DefWindowProcW;
  }
  return g_desktop;
}

RECT ScreenRect(Window* w) {
  RECT r = w->rect;
  for (Window* p = w->parent; p; p = p->parent) {
    RECT c = p->client;
    r = Offset(r, p->rect.left + c.left, p->rect.top + c.top);
  }
  return r;
}

RECT ScreenClient(Window* w) {
  RECT r = ScreenRect(w);
  return Offset(w->client, r.left, r.top);
}

POINT SurfaceOrigin(Window* w) {
  RECT s = ScreenRect(w), t = ScreenRect(w->TopLevel());
  POINT p = {s.left - t.left, s.top - t.top};
  return p;
}

bool ReallyVisible(Window* w) {
  for (; w; w = w->parent)
    if (!w->Visible()) return false;
  return true;
}

Region VisibleRegion(Window* w, bool client) {
  if (!ReallyVisible(w)) return Region();
  Window* top = w->TopLevel();
  RECT topScreen = ScreenRect(top);
  auto toSurface = [&](RECT r) { return Offset(r, -topScreen.left, -topScreen.top); };
  Region rgn(toSurface(client ? ScreenClient(w) : ScreenRect(w)));
  // Clipped by each parent's client area, and by siblings above when
  // WS_CLIPSIBLINGS is set.
  for (Window* c = w; c->parent; c = c->parent) {
    Window* p = c->parent;
    rgn.Intersect(toSurface(ScreenClient(p)));
    if (c->style & WS_CLIPSIBLINGS) {
      for (Window* s : p->children) {
        if (s == c) break;
        if (s->Visible()) rgn.Subtract(toSurface(ScreenRect(s)));
      }
    }
  }
  if (w->style & WS_CLIPCHILDREN)
    for (Window* c : w->children)
      if (c->Visible()) rgn.Subtract(toSurface(ScreenRect(c)));
  return rgn;
}

// ---------------------------------------------------------------------
// Classes

Class* FindClass(const wchar_t* name, HINSTANCE inst) {
  UserGuard g(UserLock());
  RegisterSystemClasses();
  std::wstring n = Lower(NameOf(name));
  Class* global = NULL;
  for (Class* c : Classes()) {
    if (Lower(c->name) != n) continue;
    if (!c->global && (inst == NULL || c->wc.hInstance == inst || c->wc.hInstance == NULL)) return c;
    if (c->global) global = c;
  }
  if (global) return global;
  // A local class of another module of the program is found too.
  for (Class* c : Classes())
    if (Lower(c->name) == n) return c;
  return NULL;
}

Class* FindClassAtom(ATOM atom) {
  UserGuard g(UserLock());
  for (Class* c : Classes())
    if (c->atom == atom) return c;
  return NULL;
}

}  // namespace winport

namespace {

ATOM Register(const WNDCLASSEXW* wc, bool unicode, const std::wstring& menuName, bool system) {
  if (!wc || !wc->lpszClassName || !wc->lpfnWndProc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  UserGuard g(UserLock());
  std::wstring name = NameOf(wc->lpszClassName);
  for (Class* c : Classes())
    if (Lower(c->name) == Lower(name) && c->wc.hInstance == wc->hInstance && !c->global) {
      SetLastError(ERROR_CLASS_ALREADY_EXISTS);
      return 0;
    }
  Class* c = new Class;
  c->name = name;
  c->wc = *wc;
  c->wc.cbSize = sizeof(WNDCLASSEXW);
  c->unicode = unicode;
  c->global = system || (wc->style & CS_GLOBALCLASS);
  c->menuName = menuName;
  c->extra.resize(wc->cbClsExtra > 0 ? wc->cbClsExtra : 0);
  c->atom = IS_INTRESOURCE(wc->lpszClassName) ? (ATOM)(UINT_PTR)wc->lpszClassName
                                              : GlobalAddAtomW(name.c_str());
  c->wc.lpszClassName = c->name.c_str();
  if (!IS_INTRESOURCE(wc->lpszMenuName)) c->wc.lpszMenuName = c->menuName.c_str();
  if (!c->wc.hIconSm) c->wc.hIconSm = c->wc.hIcon;
  Classes().push_back(c);
  return c->atom;
}

}  // namespace

// Registers a class for the window manager's own controls and windows.
namespace winport {
ATOM RegisterSystemClass(LPCWSTR name, WNDPROC proc, UINT style, int extra, HBRUSH brush, LPCWSTR cursor) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof wc;
  wc.style = style | CS_GLOBALCLASS;
  wc.lpfnWndProc = proc;
  wc.cbWndExtra = extra;
  wc.hbrBackground = brush;
  wc.hCursor = cursor ? LoadCursorW(NULL, cursor) : NULL;
  wc.lpszClassName = name;
  return Register(&wc, true, L"", true);
}
}  // namespace winport

extern "C" {

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* wc) {
  if (!wc) return 0;
  std::wstring menu = wc->lpszMenuName && !IS_INTRESOURCE(wc->lpszMenuName) ? wc->lpszMenuName : L"";
  return Register(wc, true, menu, false);
}

ATOM WINAPI RegisterClassExA(const WNDCLASSEXA* wca) {
  if (!wca) return 0;
  WNDCLASSEXW wc;
  memcpy(&wc, wca, sizeof wc);
  std::wstring name = NameOfA(wca->lpszClassName);
  std::wstring menu = wca->lpszMenuName && !IS_INTRESOURCE(wca->lpszMenuName) ? Widen(wca->lpszMenuName) : L"";
  wc.lpszClassName = IS_INTRESOURCE(wca->lpszClassName) ? (LPCWSTR)wca->lpszClassName : name.c_str();
  wc.lpszMenuName = IS_INTRESOURCE(wca->lpszMenuName) ? (LPCWSTR)wca->lpszMenuName : menu.c_str();
  return Register(&wc, false, menu, false);
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* w) {
  if (!w) return 0;
  WNDCLASSEXW wc = {sizeof wc, w->style, w->lpfnWndProc, w->cbClsExtra, w->cbWndExtra, w->hInstance, w->hIcon,
                    w->hCursor, w->hbrBackground, w->lpszMenuName, w->lpszClassName, NULL};
  return RegisterClassExW(&wc);
}

ATOM WINAPI RegisterClassA(const WNDCLASSA* w) {
  if (!w) return 0;
  WNDCLASSEXA wc = {sizeof wc, w->style, w->lpfnWndProc, w->cbClsExtra, w->cbWndExtra, w->hInstance, w->hIcon,
                    w->hCursor, w->hbrBackground, w->lpszMenuName, w->lpszClassName, NULL};
  return RegisterClassExA(&wc);
}

BOOL WINAPI UnregisterClassW(LPCWSTR name, HINSTANCE inst) {
  UserGuard g(UserLock());
  std::wstring n = Lower(NameOf(name));
  for (size_t i = 0; i < Classes().size(); i++) {
    Class* c = Classes()[i];
    if (Lower(c->name) == n && !c->global && (inst == NULL || c->wc.hInstance == inst)) {
      Classes().erase(Classes().begin() + i);
      return TRUE;
    }
  }
  SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
  return FALSE;
}

BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE inst) {
  return UnregisterClassW(IS_INTRESOURCE(name) ? (LPCWSTR)name : Widen(name).c_str(), inst);
}

BOOL WINAPI GetClassInfoExW(HINSTANCE inst, LPCWSTR name, WNDCLASSEXW* out) {
  Class* c = FindClass(name, inst);
  if (!c || !out) {
    SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
    return FALSE;
  }
  *out = c->wc;
  out->lpfnWndProc = ProcHandle(c->wc.lpfnWndProc, c->unicode, true);
  if (c->global && c->wc.hInstance == NULL) out->hInstance = inst;
  return c->atom;
}

BOOL WINAPI GetClassInfoW(HINSTANCE inst, LPCWSTR name, WNDCLASSW* out) {
  WNDCLASSEXW wc;
  if (!out || !GetClassInfoExW(inst, name, &wc)) return FALSE;
  WNDCLASSW w = {wc.style, wc.lpfnWndProc, wc.cbClsExtra, wc.cbWndExtra, wc.hInstance, wc.hIcon,
                 wc.hCursor, wc.hbrBackground, wc.lpszMenuName, wc.lpszClassName};
  *out = w;
  return TRUE;
}

BOOL WINAPI GetClassInfoExA(HINSTANCE inst, LPCSTR name, WNDCLASSEXA* out) {
  std::wstring n = NameOfA(name);
  Class* c = FindClass(IS_INTRESOURCE(name) ? (LPCWSTR)name : n.c_str(), inst);
  if (!c || !out) {
    SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
    return FALSE;
  }
  // The strings stay with the class, in UTF-8.
  static thread_local std::string s_name, s_menu;
  memcpy(out, &c->wc, sizeof *out);
  out->lpfnWndProc = ProcHandle(c->wc.lpfnWndProc, c->unicode, false);
  s_name = Narrow(c->name.c_str());
  out->lpszClassName = IS_INTRESOURCE(name) ? name : s_name.c_str();
  if (!IS_INTRESOURCE(c->wc.lpszMenuName)) {
    s_menu = Narrow(c->menuName.c_str());
    out->lpszMenuName = s_menu.c_str();
  }
  if (c->global && c->wc.hInstance == NULL) out->hInstance = inst;
  return c->atom;
}

BOOL WINAPI GetClassInfoA(HINSTANCE inst, LPCSTR name, WNDCLASSA* out) {
  WNDCLASSEXA wc;
  if (!out || !GetClassInfoExA(inst, name, &wc)) return FALSE;
  WNDCLASSA w = {wc.style, wc.lpfnWndProc, wc.cbClsExtra, wc.cbWndExtra, wc.hInstance, wc.hIcon,
                 wc.hCursor, wc.hbrBackground, wc.lpszMenuName, wc.lpszClassName};
  *out = w;
  return TRUE;
}

int WINAPI GetClassNameW(HWND h, LPWSTR buf, int cch) {
  Window* w = GetWindowOrFail(h);
  if (!w || !buf || cch <= 0) return 0;
  std::wstring n = w->cls ? w->cls->name : (w == Desktop() ? L"#32769" : L"");
  int len = (int)n.size() < cch - 1 ? (int)n.size() : cch - 1;
  wmemcpy(buf, n.c_str(), len);
  buf[len] = 0;
  return len;
}

int WINAPI GetClassNameA(HWND h, LPSTR buf, int cch) {
  wchar_t w[256];
  if (!GetClassNameW(h, w, 256) || !buf || cch <= 0) return 0;
  std::string s = Narrow(w);
  int len = (int)s.size() < cch - 1 ? (int)s.size() : cch - 1;
  memcpy(buf, s.c_str(), len);
  buf[len] = 0;
  return len;
}

static ULONG_PTR ClassLong(Window* w, int index, bool set, ULONG_PTR value, bool unicode) {
  Class* c = w->cls;
  if (!c) return 0;
  UserGuard g(UserLock());
  ULONG_PTR old = 0;
#define FIELD(f, type)              \
  old = (ULONG_PTR)c->wc.f;         \
  if (set) c->wc.f = (type)value;   \
  return old;
  switch (index) {
    case GCLP_HBRBACKGROUND: FIELD(hbrBackground, HBRUSH)
    case GCLP_HCURSOR: FIELD(hCursor, HCURSOR)
    case GCLP_HICON: FIELD(hIcon, HICON)
    case GCLP_HICONSM: FIELD(hIconSm, HICON)
    case GCLP_HMODULE: FIELD(hInstance, HINSTANCE)
    case GCLP_WNDPROC:
      old = (ULONG_PTR)ProcHandle(c->wc.lpfnWndProc, c->unicode, unicode);
      if (set) {
        bool u = unicode;
        c->wc.lpfnWndProc = ResolveProc((WNDPROC)value, &u);
        c->unicode = u;
      }
      return old;
    case GCL_STYLE: FIELD(style, UINT)
    case GCL_CBWNDEXTRA: return (ULONG_PTR)c->wc.cbWndExtra;
    case GCL_CBCLSEXTRA: return (ULONG_PTR)c->wc.cbClsExtra;
    case GCW_ATOM: return c->atom;
    case GCLP_MENUNAME: return (ULONG_PTR)c->wc.lpszMenuName;
  }
#undef FIELD
  if (index >= 0 && (size_t)index + sizeof(ULONG_PTR) <= c->extra.size()) {
    memcpy(&old, &c->extra[index], sizeof old);
    if (set) memcpy(&c->extra[index], &value, sizeof value);
    return old;
  }
  if (index >= 0 && (size_t)index + sizeof(DWORD) <= c->extra.size()) {
    DWORD d;
    memcpy(&d, &c->extra[index], sizeof d);
    if (set) {
      DWORD v = (DWORD)value;
      memcpy(&c->extra[index], &v, sizeof v);
    }
    return d;
  }
  SetLastError(ERROR_INVALID_INDEX);
  return 0;
}

ULONG_PTR WINAPI GetClassLongPtrW(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? ClassLong(w, i, false, 0, true) : 0;
}
ULONG_PTR WINAPI GetClassLongPtrA(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? ClassLong(w, i, false, 0, false) : 0;
}
ULONG_PTR WINAPI SetClassLongPtrW(HWND h, int i, LONG_PTR v) {
  Window* w = GetWindowOrFail(h);
  return w ? ClassLong(w, i, true, (ULONG_PTR)v, true) : 0;
}
ULONG_PTR WINAPI SetClassLongPtrA(HWND h, int i, LONG_PTR v) {
  Window* w = GetWindowOrFail(h);
  return w ? ClassLong(w, i, true, (ULONG_PTR)v, false) : 0;
}
DWORD WINAPI GetClassLongW(HWND h, int i) { return (DWORD)GetClassLongPtrW(h, i); }
DWORD WINAPI GetClassLongA(HWND h, int i) { return (DWORD)GetClassLongPtrA(h, i); }
DWORD WINAPI SetClassLongW(HWND h, int i, LONG v) { return (DWORD)SetClassLongPtrW(h, i, v); }
DWORD WINAPI SetClassLongA(HWND h, int i, LONG v) { return (DWORD)SetClassLongPtrA(h, i, v); }
WORD WINAPI GetClassWord(HWND h, int i) { return (WORD)GetClassLongPtrW(h, i); }

}  // extern "C"

// ---------------------------------------------------------------------
// Creation

namespace {

// Where CW_USEDEFAULT puts the next overlapped window.
POINT NextDefaultPosition() {
  static int n = 0;
  SIZE s = GetBackend()->ScreenSize();
  int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
  POINT p = {step * (n % 8), step * (n % 8)};
  n++;
  (void)s;
  return p;
}

void RecalcClient(Window* w) {
  NCCALCSIZE_PARAMS p = {};
  p.rgrc[0] = w->rect;
  RECT before = w->rect;
  SendTo(w, WM_NCCALCSIZE, FALSE, (LPARAM)&p.rgrc[0]);
  RECT c = p.rgrc[0];
  if (c.right < c.left) c.right = c.left;
  if (c.bottom < c.top) c.bottom = c.top;
  w->client = Offset(c, -before.left, -before.top);
}

void ResizeSurface(Window* w) {
  if (w->parent) return;
  if (!w->surface) w->surface = new Surface;
  w->surface->Resize(w->rect.right - w->rect.left, w->rect.bottom - w->rect.top);
}

}  // namespace

static HWND CreateWindowCore(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y,
                             int cx, int cy, HWND hParent, HMENU menu, HINSTANCE inst, LPVOID param, bool unicode,
                             LPCSTR classNameA, LPCSTR windowNameA) {
  Class* cls = FindClass(className, inst);
  if (!cls) {
    SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
    return NULL;
  }
  Window* parent = NULL;
  Window* owner = NULL;
  bool messageOnly = hParent == HWND_MESSAGE;
  if (hParent && !messageOnly) {
    Window* p = GetWindowOrFail(hParent);
    if (!p) return NULL;
    if (p == Desktop()) p = NULL;
    if (style & WS_CHILD) {
      parent = p;
      if (!parent) style &= ~WS_CHILD;
    } else {
      owner = p ? p->TopLevel() : NULL;
    }
  } else if (style & WS_CHILD) {
    SetLastError(ERROR_TLW_WITH_WSCHILD);
    return NULL;
  }
  // An overlapped window always has a caption and a border.
  if (!(style & (WS_CHILD | WS_POPUP))) style |= WS_CAPTION | WS_CLIPSIBLINGS;
  if ((style & WS_CAPTION) == WS_CAPTION) exStyle |= WS_EX_WINDOWEDGE;
  if ((exStyle & WS_EX_DLGMODALFRAME) || (style & WS_DLGFRAME)) exStyle |= WS_EX_WINDOWEDGE;

  if (!(style & WS_CHILD)) {
    if (x == CW_USEDEFAULT) {
      POINT p = NextDefaultPosition();
      x = p.x;
      if (!(style & WS_POPUP)) y = p.y;
    }
    if (cx == CW_USEDEFAULT) {
      SIZE s = GetBackend()->ScreenSize();
      cx = s.cx * 3 / 4;
      cy = s.cy * 3 / 4;
    }
  } else {
    if (x == CW_USEDEFAULT) x = y = 0;
    if (cx == CW_USEDEFAULT) cx = cy = 0;
  }

  Window* w;
  {
    UserGuard g(UserLock());
    w = NewWindow();
    w->cls = cls;
    w->proc = cls->wc.lpfnWndProc;
    w->unicode = cls->unicode;
    w->style = style & ~WS_VISIBLE;
    w->exStyle = exStyle;
    w->parent = parent;
    w->owner = owner;
    w->instance = inst;
    w->menu = menu;
    w->tid = GetCurrentThreadId();
    w->extra.resize(cls->wc.cbWndExtra > 0 ? cls->wc.cbWndExtra : 0);
    w->rect = MakeRect(x, y, x + cx, y + cy);
    w->client = MakeRect(0, 0, cx, cy);
    // A child goes below its siblings, so that their order is the order
    // they were made in (a dialog's tab order); a top-level window on top.
    if (!messageOnly) LinkAfter(w, (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP);
    ResizeSurface(w);
    // A top-level window takes its class's menu when it names none.
    if (!(style & WS_CHILD) && !menu && cls->wc.lpszMenuName) {
      if (IS_INTRESOURCE(cls->wc.lpszMenuName))
        w->menu = LoadMenuW(cls->wc.hInstance, cls->wc.lpszMenuName);
      else
        w->menu = LoadMenuW(cls->wc.hInstance, cls->menuName.c_str());
    }
  }
  // The creation parameters, in the form the window procedure takes.
  CREATESTRUCTW cs = {param, inst, w->menu, hParent == HWND_MESSAGE ? NULL : hParent, cy, cx, y, x,
                      (LONG)style, windowName, className, exStyle};
  CREATESTRUCTA csa;
  memcpy(&csa, &cs, sizeof csa);
  csa.lpszName = windowNameA;
  csa.lpszClass = classNameA;
  LPARAM lpcs = unicode ? (LPARAM)&cs : (LPARAM)&csa;

  // The CBT hook sees the window before its procedure does.
  if (HaveHooks(WH_CBT)) {
    CBT_CREATEWNDW cbt = {&cs, HWND_TOP};
    CBT_CREATEWNDA cbta = {&csa, HWND_TOP};
    if (CallHooks(WH_CBT, HCBT_CREATEWND, (WPARAM)w->hwnd, unicode ? (LPARAM)&cbt : (LPARAM)&cbta)) {
      DestroyWindow(w->hwnd);
      return NULL;
    }
  }

  if (!(style & (WS_CHILD | WS_POPUP)) || (style & WS_THICKFRAME)) {
    MINMAXINFO mmi = {};
    SIZE s = GetBackend()->ScreenSize();
    mmi.ptMaxSize.x = s.cx;
    mmi.ptMaxSize.y = s.cy;
    mmi.ptMinTrackSize.x = GetSystemMetrics(SM_CXMINTRACK);
    mmi.ptMinTrackSize.y = GetSystemMetrics(SM_CYMINTRACK);
    mmi.ptMaxTrackSize.x = s.cx + 8;
    mmi.ptMaxTrackSize.y = s.cy + 8;
    SendTo(w, WM_GETMINMAXINFO, 0, (LPARAM)&mmi);
  }

  if (!CallProc(w->proc, w->unicode, w->hwnd, WM_NCCREATE, 0, lpcs, unicode)) {
    DestroyWindow(w->hwnd);
    SetLastError(ERROR_CANNOT_FIND_WND_CLASS);
    return NULL;
  }
  if (!GetWindow(w->hwnd)) return NULL;
  RecalcClient(w);
  if (CallProc(w->proc, w->unicode, w->hwnd, WM_CREATE, 0, lpcs, unicode) == -1) {
    DestroyWindow(w->hwnd);
    return NULL;
  }
  if (!GetWindow(w->hwnd)) return NULL;
  w->created = true;
  // The size and position, unless the window has heard of them already.
  SendTo(w, WM_SIZE, SIZE_RESTORED,
         MAKELPARAM(w->client.right - w->client.left, w->client.bottom - w->client.top));
  SendTo(w, WM_MOVE, 0, MAKELPARAM(w->rect.left + w->client.left, w->rect.top + w->client.top));
  if (w->parent && !(exStyle & WS_EX_NOPARENTNOTIFY)) {
    for (Window* p = w->parent; p; p = p->parent) {
      SendTo(p, WM_PARENTNOTIFY, MAKEWPARAM(WM_CREATE, (UINT_PTR)w->menu), (LPARAM)w->hwnd);
      if (p->exStyle & WS_EX_NOPARENTNOTIFY || !p->Child()) break;
    }
  }
  if (style & WS_VISIBLE) {
    int cmd = SW_SHOW;
    if (style & WS_MAXIMIZE) cmd = SW_SHOWMAXIMIZED;
    if (style & WS_MINIMIZE) cmd = SW_SHOWMINIMIZED;
    ShowWindow(w->hwnd, cmd);
  }
  return GetWindow(w->hwnd) ? w->hwnd : NULL;
}

extern "C" {

HWND WINAPI CreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y,
                            int cx, int cy, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  std::string cA = IS_INTRESOURCE(className) ? std::string() : Narrow(className);
  std::string nA = windowName ? Narrow(windowName) : std::string();
  return CreateWindowCore(exStyle, className, windowName ? windowName : L"", style, x, y, cx, cy, parent, menu,
                          inst, param, true, IS_INTRESOURCE(className) ? (LPCSTR)className : cA.c_str(),
                          nA.c_str());
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int cx,
                            int cy, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
  std::wstring c = IS_INTRESOURCE(className) ? std::wstring() : Widen(className);
  std::wstring n = windowName ? Widen(windowName) : std::wstring();
  return CreateWindowCore(exStyle, IS_INTRESOURCE(className) ? (LPCWSTR)className : c.c_str(), n.c_str(), style,
                          x, y, cx, cy, parent, menu, inst, param, false, className, windowName ? windowName : "");
}

}  // extern "C"

// ---------------------------------------------------------------------
// Destruction

static void SendDestroy(Window* w) {
  SendTo(w, WM_DESTROY, 0, 0);
  std::vector<Window*> kids = w->children;
  for (Window* c : kids)
    if (GetWindow(c->hwnd)) SendDestroy(c);
}

static void FreeTree(Window* w) {
  std::vector<Window*> kids = w->children;
  for (Window* c : kids) FreeTree(c);
  SendTo(w, WM_NCDESTROY, 0, 0);
  UserGuard g(UserLock());
  WindowDestroyed(w);
  Unlink(w);
  Handles().erase((UINT_PTR)w->hwnd);
  if (!w->parent && w->native) GetBackend()->Destroy(w);
  delete w->surface;
  w->surface = NULL;
  // The Window itself stays allocated: a procedure further up the stack
  // may still hold it. Its handle no longer finds it.
  w->children.clear();
}

extern "C" BOOL WINAPI DestroyWindow(HWND h) {
  Window* w = GetWindowOrFail(h);
  if (!w || w == Desktop()) return FALSE;
  if (w->destroying) return TRUE;
  if (HaveHooks(WH_CBT) && CallHooks(WH_CBT, HCBT_DESTROYWND, (WPARAM)h, 0)) return FALSE;
  w->destroying = true;
  if (w->Visible()) {
    SetWindowPos(h, NULL, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  }
  if (!GetWindow(h)) return TRUE;
  // Owned windows go first.
  if (!w->parent) {
    std::vector<Window*> owned;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels())
        if (t->owner == w) owned.push_back(t);
    }
    for (Window* o : owned)
      if (GetWindow(o->hwnd)) DestroyWindow(o->hwnd);
  }
  if (w->parent && !(w->exStyle & WS_EX_NOPARENTNOTIFY))
    SendTo(w->parent, WM_PARENTNOTIFY, MAKEWPARAM(WM_DESTROY, (UINT_PTR)w->menu), (LPARAM)h);
  if (!GetWindow(h)) return TRUE;
  SendDestroy(w);
  if (GetWindow(h)) FreeTree(w);
  return TRUE;
}

// ---------------------------------------------------------------------
// The window tree

extern "C" {

BOOL WINAPI IsWindow(HWND h) { return GetWindow(h) != NULL; }

HWND WINAPI GetDesktopWindow(void) { return Desktop()->hwnd; }

HWND WINAPI GetParent(HWND h) {
  Window* w = GetWindowOrFail(h);
  if (!w) return NULL;
  if (w->style & WS_CHILD) return w->parent ? w->parent->hwnd : NULL;
  if (w->style & WS_POPUP) return w->owner ? w->owner->hwnd : NULL;
  return NULL;
}

HWND WINAPI GetAncestor(HWND h, UINT flags) {
  Window* w = GetWindowOrFail(h);
  if (!w || w == Desktop()) return NULL;
  switch (flags) {
    case GA_PARENT: return w->parent ? w->parent->hwnd : Desktop()->hwnd;
    case GA_ROOT: return w->TopLevel()->hwnd;
    case GA_ROOTOWNER: {
      Window* r = w->TopLevel();
      while (r->owner) r = r->owner->TopLevel();
      return r->hwnd;
    }
  }
  return NULL;
}

HWND WINAPI SetParent(HWND h, HWND newParent) {
  Window* w = GetWindowOrFail(h);
  if (!w) return NULL;
  Window* p = newParent ? GetWindowOrFail(newParent) : NULL;
  if (newParent && !p) return NULL;
  if (p == Desktop()) p = NULL;
  HWND old = w->parent ? w->parent->hwnd : Desktop()->hwnd;
  bool visible = w->Visible();
  if (visible) ShowWindow(h, SW_HIDE);
  {
    UserGuard g(UserLock());
    RECT screen = ScreenRect(w);
    Unlink(w);
    w->parent = p;
    RECT origin = p ? ScreenClient(p) : MakeRect(0, 0, 0, 0);
    w->rect = Offset(screen, -origin.left, -origin.top);
    if (p) {
      delete w->surface;
      w->surface = NULL;
    } else {
      w->style &= ~WS_CHILD;
      ResizeSurface(w);
    }
    LinkAfter(w, HWND_TOP);
  }
  if (visible) ShowWindow(h, SW_SHOWNA);
  return old;
}

BOOL WINAPI IsChild(HWND parent, HWND h) {
  Window* w = GetWindow(h);
  Window* p = GetWindow(parent);
  if (!w || !p) return FALSE;
  for (Window* c = w; c->Child(); c = c->parent)
    if (c->parent == p) return TRUE;
  return FALSE;
}

HWND WINAPI GetWindow(HWND h, UINT cmd) {
  Window* w = GetWindowOrFail(h);
  if (!w) return NULL;
  UserGuard g(UserLock());
  if (cmd == GW_OWNER) return w->owner ? w->owner->hwnd : NULL;
  if (cmd == GW_CHILD) {
    std::vector<Window*>& v = w == Desktop() ? TopLevels() : w->children;
    return v.empty() ? NULL : v.front()->hwnd;
  }
  if (w == Desktop()) return NULL;
  std::vector<Window*>& v = Siblings(w);
  auto it = std::find(v.begin(), v.end(), w);
  if (it == v.end()) return NULL;
  switch (cmd) {
    case GW_HWNDFIRST: return v.front()->hwnd;
    case GW_HWNDLAST: return v.back()->hwnd;
    case GW_HWNDNEXT: return it + 1 == v.end() ? NULL : (*(it + 1))->hwnd;
    case GW_HWNDPREV: return it == v.begin() ? NULL : (*(it - 1))->hwnd;
    case GW_ENABLEDPOPUP: {
      for (Window* t : TopLevels())
        if (t->owner == w && t->Visible() && !(t->style & WS_DISABLED)) return t->hwnd;
      return NULL;
    }
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return NULL;
}

HWND WINAPI GetTopWindow(HWND h) { return GetWindow(h ? h : GetDesktopWindow(), GW_CHILD); }

HWND WINAPI GetLastActivePopup(HWND h) {
  Window* w = GetWindowOrFail(h);
  if (!w) return NULL;
  Window* a = GetWindow(w->lastActive);
  return a && a->Visible() && !(a->style & WS_DISABLED) ? a->hwnd : h;
}

BOOL WINAPI IsWindowVisible(HWND h) {
  Window* w = GetWindow(h);
  return w && ReallyVisible(w);
}

BOOL WINAPI IsWindowEnabled(HWND h) {
  Window* w = GetWindow(h);
  return w && !(w->style & WS_DISABLED);
}

BOOL WINAPI IsIconic(HWND h) {
  Window* w = GetWindow(h);
  return w && (w->style & WS_MINIMIZE);
}

BOOL WINAPI IsZoomed(HWND h) {
  Window* w = GetWindow(h);
  return w && (w->style & WS_MAXIMIZE);
}

BOOL WINAPI IsWindowUnicode(HWND h) {
  Window* w = GetWindow(h);
  return w && w->unicode;
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  BOOL wasDisabled = (w->style & WS_DISABLED) != 0;
  if (enable && wasDisabled) {
    w->style &= ~WS_DISABLED;
    SendTo(w, WM_ENABLE, TRUE, 0);
  } else if (!enable && !wasDisabled) {
    SendTo(w, WM_CANCELMODE, 0, 0);
    w->style |= WS_DISABLED;
    Queue* q = QueueFor(w->tid);
    Window* f = GetWindow(q->focus);
    if (f && (f == w || IsChild(h, f->hwnd))) SetFocus(NULL);
    SendTo(w, WM_ENABLE, FALSE, 0);
  }
  return wasDisabled;
}

DWORD WINAPI GetWindowThreadProcessId(HWND h, LPDWORD pid) {
  Window* w = GetWindowOrFail(h);
  if (!w) return 0;
  if (pid) *pid = GetCurrentProcessId();
  return w->tid ? w->tid : GetCurrentThreadId();
}

typedef BOOL(CALLBACK* EnumProc)(HWND, LPARAM);

BOOL WINAPI EnumWindows(WNDENUMPROC proc, LPARAM lParam) {
  std::vector<HWND> list;
  {
    UserGuard g(UserLock());
    for (Window* t : TopLevels()) list.push_back(t->hwnd);
  }
  for (HWND h : list)
    if (IsWindow(h) && !proc(h, lParam)) break;
  return TRUE;
}

BOOL WINAPI EnumThreadWindows(DWORD tid, WNDENUMPROC proc, LPARAM lParam) {
  std::vector<HWND> list;
  {
    UserGuard g(UserLock());
    for (Window* t : TopLevels())
      if (t->tid == tid) list.push_back(t->hwnd);
  }
  for (HWND h : list)
    if (IsWindow(h) && !proc(h, lParam)) return FALSE;
  return TRUE;
}

static void CollectChildren(Window* w, std::vector<HWND>& out) {
  for (Window* c : w->children) {
    out.push_back(c->hwnd);
    CollectChildren(c, out);
  }
}

BOOL WINAPI EnumChildWindows(HWND parent, WNDENUMPROC proc, LPARAM lParam) {
  Window* p = parent ? GetWindow(parent) : Desktop();
  if (!p) return FALSE;
  if (p == Desktop()) return EnumWindows(proc, lParam);
  std::vector<HWND> list;
  {
    UserGuard g(UserLock());
    CollectChildren(p, list);
  }
  for (HWND h : list)
    if (IsWindow(h) && !proc(h, lParam)) break;
  return TRUE;
}

HWND WINAPI FindWindowExW(HWND parent, HWND after, LPCWSTR cls, LPCWSTR title) {
  Window* p = parent ? GetWindow(parent) : NULL;
  if (p == Desktop()) p = NULL;
  UserGuard g(UserLock());
  std::vector<Window*>& v = p ? p->children : TopLevels();
  bool started = after == NULL;
  std::wstring c = cls ? Lower(NameOf(cls)) : L"";
  for (Window* w : v) {
    if (!started) {
      started = w->hwnd == after;
      continue;
    }
    if (cls && (!w->cls || Lower(w->cls->name) != c)) continue;
    if (title && w->text != title) continue;
    return w->hwnd;
  }
  return NULL;
}

HWND WINAPI FindWindowExA(HWND parent, HWND after, LPCSTR cls, LPCSTR title) {
  std::wstring c = cls && !IS_INTRESOURCE(cls) ? Widen(cls) : L"";
  std::wstring t = title ? Widen(title) : L"";
  return FindWindowExW(parent, after, IS_INTRESOURCE(cls) ? (LPCWSTR)cls : c.c_str(), title ? t.c_str() : NULL);
}

HWND WINAPI FindWindowW(LPCWSTR cls, LPCWSTR title) { return FindWindowExW(NULL, NULL, cls, title); }
HWND WINAPI FindWindowA(LPCSTR cls, LPCSTR title) { return FindWindowExA(NULL, NULL, cls, title); }

// ---------------------------------------------------------------------
// Coordinates

BOOL WINAPI GetWindowRect(HWND h, LPRECT r) {
  Window* w = GetWindowOrFail(h);
  if (!w || !r) return FALSE;
  UserGuard g(UserLock());
  *r = w == Desktop() ? w->rect : ScreenRect(w);
  return TRUE;
}

BOOL WINAPI GetClientRect(HWND h, LPRECT r) {
  Window* w = GetWindowOrFail(h);
  if (!w || !r) return FALSE;
  UserGuard g(UserLock());
  *r = MakeRect(0, 0, w->client.right - w->client.left, w->client.bottom - w->client.top);
  return TRUE;
}

static POINT ClientOrigin(HWND h) {
  Window* w = GetWindow(h);
  POINT p = {0, 0};
  if (!w || w == Desktop()) return p;
  RECT c = ScreenClient(w);
  p.x = c.left;
  p.y = c.top;
  return p;
}

BOOL WINAPI ClientToScreen(HWND h, LPPOINT pt) {
  if (!GetWindowOrFail(h) || !pt) return FALSE;
  POINT o = ClientOrigin(h);
  pt->x += o.x;
  pt->y += o.y;
  return TRUE;
}

BOOL WINAPI ScreenToClient(HWND h, LPPOINT pt) {
  if (!GetWindowOrFail(h) || !pt) return FALSE;
  POINT o = ClientOrigin(h);
  pt->x -= o.x;
  pt->y -= o.y;
  return TRUE;
}

int WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT n) {
  POINT a = ClientOrigin(from), b = ClientOrigin(to);
  int dx = a.x - b.x, dy = a.y - b.y;
  for (UINT i = 0; i < n; i++) {
    pts[i].x += dx;
    pts[i].y += dy;
  }
  SetLastError(0);
  return MAKELONG((WORD)(SHORT)dx, (WORD)(SHORT)dy);
}

// The deepest visible, enabled-or-not window under a screen point.
static Window* ChildAt(Window* w, POINT screen, UINT skip) {
  for (Window* c : w->children) {
    if ((skip & CWP_SKIPINVISIBLE) && !c->Visible()) continue;
    if (!c->Visible() && !(skip == CWP_ALL)) continue;
    if ((skip & CWP_SKIPDISABLED) && (c->style & WS_DISABLED)) continue;
    if ((skip & CWP_SKIPTRANSPARENT) && (c->exStyle & WS_EX_TRANSPARENT)) continue;
    RECT r = ScreenRect(c);
    if (screen.x >= r.left && screen.x < r.right && screen.y >= r.top && screen.y < r.bottom) return c;
  }
  return NULL;
}

HWND WINAPI ChildWindowFromPointEx(HWND parent, POINT pt, UINT flags) {
  Window* p = GetWindowOrFail(parent);
  if (!p) return NULL;
  UserGuard g(UserLock());
  POINT s = pt;
  RECT c = ScreenClient(p);
  s.x += c.left;
  s.y += c.top;
  RECT r = ScreenRect(p);
  if (p != Desktop() && (s.x < r.left || s.x >= r.right || s.y < r.top || s.y >= r.bottom)) return NULL;
  Window* w = ChildAt(p, s, flags);
  return w ? w->hwnd : parent;
}

HWND WINAPI ChildWindowFromPoint(HWND parent, POINT pt) { return ChildWindowFromPointEx(parent, pt, CWP_ALL); }
HWND WINAPI RealChildWindowFromPoint(HWND parent, POINT pt) {
  return ChildWindowFromPointEx(parent, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
}

HWND WINAPI WindowFromPoint(POINT pt) {
  UserGuard g(UserLock());
  for (Window* t : TopLevels()) {
    if (!t->Visible()) continue;
    RECT r = ScreenRect(t);
    if (pt.x < r.left || pt.x >= r.right || pt.y < r.top || pt.y >= r.bottom) continue;
    Window* w = t;
    for (Window* c; (c = ChildAt(w, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT)) != NULL;) w = c;
    return w->hwnd;
  }
  return Desktop()->hwnd;
}

}  // extern "C"

// ---------------------------------------------------------------------
// Position, size and z-order

namespace {

// Where a window moved from, so that what it uncovered is repainted.
void InvalidateUncovered(Window* w, const RECT& oldScreen) {
  Window* p = w->parent;
  RECT now = ReallyVisible(w) ? ScreenRect(w) : MakeRect(0, 0, 0, 0);
  Region gone(oldScreen);
  gone.Subtract(now);
  if (gone.Empty()) return;
  if (p) {
    RECT pc = ScreenClient(p);
    gone.Offset(-pc.left, -pc.top);
    InvalidateWindow(p, &gone, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  } else {
    // Uncovered parts of the screen: the windows beneath.
    for (Window* t : TopLevels()) {
      if (t == w || !t->Visible()) continue;
      RECT tr = ScreenRect(t);
      if (!gone.Touches(tr)) continue;
      Region part = gone;
      part.Intersect(tr);
      RECT tc = ScreenClient(t);
      part.Offset(-tc.left, -tc.top);
      InvalidateWindow(t, &part, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
  }
}

}  // namespace

static BOOL SetWindowPosCore(Window* w, HWND after, int x, int y, int cx, int cy, UINT flags) {
  HWND h = w->hwnd;
  if (w->parent) flags |= SWP_NOACTIVATE;
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;
  if ((flags & SWP_SHOWWINDOW) && w->Visible()) flags &= ~SWP_SHOWWINDOW;
  if ((flags & SWP_HIDEWINDOW) && !w->Visible()) flags &= ~SWP_HIDEWINDOW;
  WINDOWPOS wp = {h, after, x, y, cx, cy, flags};
  if (flags & SWP_NOMOVE) {
    wp.x = w->rect.left;
    wp.y = w->rect.top;
  }
  if (flags & SWP_NOSIZE) {
    wp.cx = w->rect.right - w->rect.left;
    wp.cy = w->rect.bottom - w->rect.top;
  }
  if (!(flags & SWP_NOSENDCHANGING)) SendTo(w, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wp);
  if (!GetWindow(h)) return FALSE;
  flags = wp.flags;

  RECT oldScreen = ReallyVisible(w) ? ScreenRect(w) : MakeRect(0, 0, 0, 0);
  RECT oldRect = w->rect, oldClient = w->client;
  {
    UserGuard g(UserLock());
    w->rect = MakeRect(wp.x, wp.y, wp.x + wp.cx, wp.y + wp.cy);
    if (!(flags & SWP_NOZORDER)) {
      if (wp.hwndInsertAfter == HWND_TOPMOST) w->exStyle |= WS_EX_TOPMOST;
      if (wp.hwndInsertAfter == HWND_NOTOPMOST) w->exStyle &= ~WS_EX_TOPMOST;
      if (wp.hwndInsertAfter != h) LinkAfter(w, wp.hwndInsertAfter);
    }
    if (flags & SWP_SHOWWINDOW) w->style |= WS_VISIBLE;
    if (flags & SWP_HIDEWINDOW) w->style &= ~WS_VISIBLE;
  }
  bool sized = oldRect.right - oldRect.left != wp.cx || oldRect.bottom - oldRect.top != wp.cy;
  bool moved = oldRect.left != wp.x || oldRect.top != wp.y;
  if (sized || (flags & SWP_FRAMECHANGED)) RecalcClient(w);
  if (!GetWindow(h)) return FALSE;
  {
    UserGuard g(UserLock());
    if (sized) ResizeSurface(w);
  }
  bool clientChanged = w->client.right - w->client.left != oldClient.right - oldClient.left ||
                       w->client.bottom - w->client.top != oldClient.bottom - oldClient.top;

  if (!w->parent) {
    if (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) GetBackend()->Show(w, w->Visible());
    if (sized || moved) GetBackend()->Moved(w);
  }

  // Repaint: the window when it appeared, or changed size or frame; and
  // whatever its old place uncovered.
  if (!(flags & SWP_NOREDRAW)) {
    if (ReallyVisible(w) && ((flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED)) || sized || moved ||
                             !(flags & SWP_NOZORDER)))
      InvalidateWindow(w, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ALLCHILDREN);
    if (!RectEmpty(oldScreen) && (moved || sized || (flags & SWP_HIDEWINDOW))) InvalidateUncovered(w, oldScreen);
  }

  if ((flags & SWP_SHOWWINDOW) && !(flags & SWP_NOACTIVATE) && !w->parent) ActivateWindow(w, false);
  if ((flags & SWP_HIDEWINDOW) && !w->parent) {
    Queue* q = QueueFor(w->tid);
    if (q->active == h) {
      // Activation passes to the owner, or to another top-level window.
      Window* next = w->owner && w->owner->Visible() && !(w->owner->style & WS_DISABLED) ? w->owner : NULL;
      if (!next) {
        UserGuard g(UserLock());
        for (Window* t : TopLevels())
          if (t != w && t->Visible() && !(t->style & WS_DISABLED) && t->tid == w->tid &&
              (t->style & WS_CAPTION) == WS_CAPTION) {
            next = t;
            break;
          }
      }
      if (next)
        ActivateWindow(next, false);
      else
        SetActiveWindow(NULL);
    }
  }
  if (flags & SWP_HIDEWINDOW) {
    Queue* q = QueueFor(w->tid);
    Window* f = GetWindow(q->focus);
    // A hidden child passes the focus to its parent; a hidden top-level
    // window keeps it only if activation stayed with it.
    if (f && (f == w || IsChild(h, f->hwnd))) {
      if (w->parent)
        SetFocus(w->parent->hwnd);
      else if (q->active == h || !q->active)
        SetFocus(NULL);
    }
  }

  wp.x = w->rect.left;
  wp.y = w->rect.top;
  wp.cx = w->rect.right - w->rect.left;
  wp.cy = w->rect.bottom - w->rect.top;
  wp.flags = flags;
  if (!moved) wp.flags |= SWP_NOMOVE;
  if (!sized && !clientChanged) wp.flags |= SWP_NOSIZE;
  SendTo(w, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
  return TRUE;
}

extern "C" {

BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
  Window* w = GetWindowOrFail(h);
  if (!w || w == Desktop()) return FALSE;
  return SetWindowPosCore(w, after, x, y, cx, cy, flags);
}

BOOL WINAPI MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint) {
  return SetWindowPos(h, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

BOOL WINAPI BringWindowToTop(HWND h) { return SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }

// Deferred positioning applies each move in turn.
struct DeferList {
  std::vector<WINDOWPOS> list;
};

HDWP WINAPI BeginDeferWindowPos(int) { return (HDWP) new DeferList; }

HDWP WINAPI DeferWindowPos(HDWP d, HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
  if (!d) return NULL;
  WINDOWPOS wp = {h, after, x, y, cx, cy, flags};
  ((DeferList*)d)->list.push_back(wp);
  return d;
}

BOOL WINAPI EndDeferWindowPos(HDWP d) {
  if (!d) return FALSE;
  DeferList* l = (DeferList*)d;
  for (const WINDOWPOS& wp : l->list) SetWindowPos(wp.hwnd, wp.hwndInsertAfter, wp.x, wp.y, wp.cx, wp.cy, wp.flags);
  delete l;
  return TRUE;
}

BOOL WINAPI ShowWindow(HWND h, int cmd) {
  Window* w = GetWindowOrFail(h);
  if (!w || w == Desktop()) return FALSE;
  BOOL wasVisible = w->Visible();
  UINT swp = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER;
  bool show = true;
  RECT target = w->rect;
  bool resize = false;
  DWORD oldStyle = w->style;
  switch (cmd) {
    case SW_HIDE:
      if (!wasVisible) return FALSE;
      show = false;
      swp |= SWP_HIDEWINDOW | SWP_NOACTIVATE;
      break;
    case SW_SHOWNOACTIVATE:
    case SW_SHOWNA:
    case SW_SHOWMINNOACTIVE:
      swp |= SWP_SHOWWINDOW | SWP_NOACTIVATE;
      break;
    case SW_MINIMIZE:
    case SW_SHOWMINIMIZED:
    case SW_FORCEMINIMIZE:
      swp |= SWP_SHOWWINDOW | (cmd == SW_SHOWMINIMIZED ? 0 : SWP_NOACTIVATE);
      if (!(w->style & WS_MINIMIZE)) {
        if (!(w->style & WS_MAXIMIZE)) w->normalRect = w->rect;
        w->style = (w->style | WS_MINIMIZE) & ~WS_MAXIMIZE;
        int cxm = GetSystemMetrics(SM_CXMINIMIZED) > 0 ? GetSystemMetrics(SM_CXMINIMIZED) : 160;
        target = MakeRect(w->rect.left, w->rect.top, w->rect.left + cxm, w->rect.top + GetSystemMetrics(SM_CYCAPTION) + 6);
        resize = true;
      }
      break;
    case SW_MAXIMIZE:
      swp |= SWP_SHOWWINDOW | SWP_FRAMECHANGED;
      if (!(w->style & WS_MAXIMIZE)) {
        if (!(w->style & WS_MINIMIZE)) w->normalRect = w->rect;
        w->style = (w->style | WS_MAXIMIZE) & ~WS_MINIMIZE;
        if (w->parent) {
          target = MakeRect(0, 0, w->parent->client.right - w->parent->client.left,
                            w->parent->client.bottom - w->parent->client.top);
        } else {
          SIZE s = GetBackend()->ScreenSize();
          target = MakeRect(0, 0, s.cx, s.cy);
        }
        RECT in = FrameInsets(w->style, w->exStyle, false);
        target.left -= in.left;
        target.top -= in.top - (in.top > in.left ? in.top - in.left : 0);
        target.right += in.right;
        target.bottom += in.bottom;
        resize = true;
      }
      break;
    case SW_RESTORE:
    case SW_SHOWNORMAL:
    case SW_SHOWDEFAULT:
      swp |= SWP_SHOWWINDOW;
      if (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) {
        w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
        if (!RectEmpty(w->normalRect)) target = w->normalRect;
        resize = true;
        swp |= SWP_FRAMECHANGED;
      }
      break;
    case SW_SHOW:
    default:
      swp |= SWP_SHOWWINDOW;
      break;
  }
  if (w->parent) swp |= SWP_NOACTIVATE | SWP_NOZORDER;
  if (show != (wasVisible != 0)) {
    SendTo(w, WM_SHOWWINDOW, show, 0);
    if (!GetWindow(h)) return wasVisible;
  }
  if (resize) swp &= ~(SWP_NOSIZE | SWP_NOMOVE);
  if (!w->parent && show && !(swp & SWP_NOACTIVATE)) swp &= ~SWP_NOZORDER;
  SetWindowPosCore(w, HWND_TOP, target.left, target.top, target.right - target.left, target.bottom - target.top,
                   swp);
  if (!GetWindow(h)) return wasVisible;
  if (resize || (oldStyle & (WS_MINIMIZE | WS_MAXIMIZE)) != (w->style & (WS_MINIMIZE | WS_MAXIMIZE))) {
    WPARAM type = (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : (w->style & WS_MINIMIZE) ? SIZE_MINIMIZED : SIZE_RESTORED;
    SendTo(w, WM_SIZE, type, MAKELPARAM(w->client.right - w->client.left, w->client.bottom - w->client.top));
  }
  return wasVisible;
}

BOOL WINAPI ShowWindowAsync(HWND h, int cmd) { return ShowWindow(h, cmd); }

BOOL WINAPI OpenIcon(HWND h) { return ShowWindow(h, SW_RESTORE), TRUE; }
BOOL WINAPI CloseWindow(HWND h) { return ShowWindow(h, SW_MINIMIZE), TRUE; }

BOOL WINAPI ShowOwnedPopups(HWND h, BOOL show) {
  std::vector<HWND> list;
  {
    UserGuard g(UserLock());
    Window* w = GetWindow(h);
    for (Window* t : TopLevels())
      if (t->owner == w && (t->style & WS_POPUP)) list.push_back(t->hwnd);
  }
  for (HWND p : list) ShowWindow(p, show ? SW_SHOWNA : SW_HIDE);
  return TRUE;
}

BOOL WINAPI GetWindowPlacement(HWND h, WINDOWPLACEMENT* wp) {
  Window* w = GetWindowOrFail(h);
  if (!w || !wp) return FALSE;
  wp->length = sizeof *wp;
  wp->flags = 0;
  wp->showCmd = (w->style & WS_MINIMIZE) ? SW_SHOWMINIMIZED : (w->style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
  wp->ptMinPosition.x = wp->ptMinPosition.y = -1;
  wp->ptMaxPosition.x = wp->ptMaxPosition.y = -1;
  wp->rcNormalPosition = (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) && !RectEmpty(w->normalRect) ? w->normalRect : w->rect;
  return TRUE;
}

BOOL WINAPI SetWindowPlacement(HWND h, const WINDOWPLACEMENT* wp) {
  Window* w = GetWindowOrFail(h);
  if (!w || !wp) return FALSE;
  w->normalRect = wp->rcNormalPosition;
  if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE)))
    SetWindowPos(h, NULL, wp->rcNormalPosition.left, wp->rcNormalPosition.top,
                 wp->rcNormalPosition.right - wp->rcNormalPosition.left,
                 wp->rcNormalPosition.bottom - wp->rcNormalPosition.top, SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(h, wp->showCmd);
  return TRUE;
}

BOOL WINAPI AdjustWindowRectEx(LPRECT r, DWORD style, BOOL menu, DWORD exStyle) {
  if (!r) return FALSE;
  if (!(style & (WS_CHILD | WS_POPUP))) style |= WS_CAPTION;
  RECT in = FrameInsets(style, exStyle, menu != FALSE);
  r->left -= in.left;
  r->top -= in.top;
  r->right += in.right;
  r->bottom += in.bottom;
  return TRUE;
}

BOOL WINAPI AdjustWindowRect(LPRECT r, DWORD style, BOOL menu) { return AdjustWindowRectEx(r, style, menu, 0); }

// ---------------------------------------------------------------------
// Window data

static LONG_PTR WindowLong(Window* w, int index, bool set, LONG_PTR value, bool unicode) {
  LONG_PTR old = 0;
  switch (index) {
    case GWL_STYLE: {
      old = (LONG)w->style;
      if (set) {
        STYLESTRUCT ss = {w->style, (DWORD)value};
        SendTo(w, WM_STYLECHANGING, GWL_STYLE, (LPARAM)&ss);
        // WS_VISIBLE changes the flag only; the window is not shown or hidden.
        w->style = ss.styleNew;
        SendTo(w, WM_STYLECHANGED, GWL_STYLE, (LPARAM)&ss);
      }
      return old;
    }
    case GWL_EXSTYLE: {
      old = (LONG)w->exStyle;
      if (set) {
        STYLESTRUCT ss = {w->exStyle, (DWORD)value};
        SendTo(w, WM_STYLECHANGING, GWL_EXSTYLE, (LPARAM)&ss);
        w->exStyle = ss.styleNew;
        SendTo(w, WM_STYLECHANGED, GWL_EXSTYLE, (LPARAM)&ss);
      }
      return old;
    }
    case GWLP_WNDPROC:
      old = (LONG_PTR)ProcHandle(w->proc, w->unicode, unicode);
      if (set) {
        bool u = unicode;
        w->proc = ResolveProc((WNDPROC)value, &u);
        w->unicode = u;
      }
      return old;
    case GWLP_HINSTANCE:
      old = (LONG_PTR)w->instance;
      if (set) w->instance = (HINSTANCE)value;
      return old;
    case GWLP_HWNDPARENT: {
      if (w->Child()) return w->parent ? (LONG_PTR)w->parent->hwnd : 0;
      old = w->owner ? (LONG_PTR)w->owner->hwnd : 0;
      if (set) {
        Window* o = GetWindow((HWND)value);
        w->owner = o ? o->TopLevel() : NULL;
      }
      return old;
    }
    case GWLP_ID:
      old = (LONG_PTR)w->menu;
      if (set) w->menu = (HMENU)value;
      return old;
    case GWLP_USERDATA:
      old = w->userData;
      if (set) w->userData = value;
      return old;
  }
  if (index >= 0 && (size_t)index + sizeof(LONG_PTR) <= w->extra.size()) {
    memcpy(&old, &w->extra[index], sizeof old);
    if (set) memcpy(&w->extra[index], &value, sizeof value);
    return old;
  }
  SetLastError(ERROR_INVALID_INDEX);
  return 0;
}

static LONG WindowLong32(Window* w, int index, bool set, LONG value, bool unicode) {
  if (index < 0) return (LONG)WindowLong(w, index, set, value, unicode);
  if ((size_t)index + sizeof(LONG) > w->extra.size()) {
    SetLastError(ERROR_INVALID_INDEX);
    return 0;
  }
  LONG old;
  memcpy(&old, &w->extra[index], sizeof old);
  if (set) memcpy(&w->extra[index], &value, sizeof value);
  return old;
}

LONG_PTR WINAPI GetWindowLongPtrW(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong(w, i, false, 0, true) : 0;
}
LONG_PTR WINAPI GetWindowLongPtrA(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong(w, i, false, 0, false) : 0;
}
LONG_PTR WINAPI SetWindowLongPtrW(HWND h, int i, LONG_PTR v) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong(w, i, true, v, true) : 0;
}
LONG_PTR WINAPI SetWindowLongPtrA(HWND h, int i, LONG_PTR v) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong(w, i, true, v, false) : 0;
}
LONG WINAPI GetWindowLongW(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong32(w, i, false, 0, true) : 0;
}
LONG WINAPI GetWindowLongA(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong32(w, i, false, 0, false) : 0;
}
LONG WINAPI SetWindowLongW(HWND h, int i, LONG v) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong32(w, i, true, v, true) : 0;
}
LONG WINAPI SetWindowLongA(HWND h, int i, LONG v) {
  Window* w = GetWindowOrFail(h);
  return w ? WindowLong32(w, i, true, v, false) : 0;
}
WORD WINAPI GetWindowWord(HWND h, int i) {
  Window* w = GetWindowOrFail(h);
  if (!w || i < 0 || (size_t)i + 2 > w->extra.size()) return 0;
  WORD v;
  memcpy(&v, &w->extra[i], 2);
  return v;
}
WORD WINAPI SetWindowWord(HWND h, int i, WORD v) {
  Window* w = GetWindowOrFail(h);
  if (!w || i < 0 || (size_t)i + 2 > w->extra.size()) return 0;
  WORD old;
  memcpy(&old, &w->extra[i], 2);
  memcpy(&w->extra[i], &v, 2);
  return old;
}

int WINAPI GetDlgCtrlID(HWND h) {
  Window* w = GetWindowOrFail(h);
  return w && (w->style & WS_CHILD) ? (int)(INT_PTR)w->menu : 0;
}

HWND WINAPI GetDlgItem(HWND dlg, int id) {
  Window* w = GetWindowOrFail(dlg);
  if (!w) return NULL;
  UserGuard g(UserLock());
  for (Window* c : w->children)
    if ((int)(INT_PTR)c->menu == id) return c->hwnd;
  SetLastError(ERROR_CONTROL_ID_NOT_FOUND);
  return NULL;
}

// Text

int WINAPI GetWindowTextW(HWND h, LPWSTR buf, int cch) {
  Window* w = GetWindowOrFail(h);
  if (!w || !buf || cch <= 0) return 0;
  buf[0] = 0;
  return (int)SendTo(w, WM_GETTEXT, cch, (LPARAM)buf, true);
}

int WINAPI GetWindowTextA(HWND h, LPSTR buf, int cch) {
  Window* w = GetWindowOrFail(h);
  if (!w || !buf || cch <= 0) return 0;
  buf[0] = 0;
  return (int)SendTo(w, WM_GETTEXT, cch, (LPARAM)buf, false);
}

int WINAPI GetWindowTextLengthW(HWND h) {
  Window* w = GetWindowOrFail(h);
  return w ? (int)SendTo(w, WM_GETTEXTLENGTH, 0, 0, true) : 0;
}

int WINAPI GetWindowTextLengthA(HWND h) {
  Window* w = GetWindowOrFail(h);
  return w ? (int)SendTo(w, WM_GETTEXTLENGTH, 0, 0, false) : 0;
}

BOOL WINAPI SetWindowTextW(HWND h, LPCWSTR s) {
  Window* w = GetWindowOrFail(h);
  return w ? (BOOL)SendTo(w, WM_SETTEXT, 0, (LPARAM)s, true) : FALSE;
}

BOOL WINAPI SetWindowTextA(HWND h, LPCSTR s) {
  Window* w = GetWindowOrFail(h);
  return w ? (BOOL)SendTo(w, WM_SETTEXT, 0, (LPARAM)s, false) : FALSE;
}

int WINAPI InternalGetWindowText(HWND h, LPWSTR buf, int cch) {
  Window* w = GetWindowOrFail(h);
  if (!w || !buf || cch <= 0) return 0;
  int n = (int)w->text.size() < cch - 1 ? (int)w->text.size() : cch - 1;
  wmemcpy(buf, w->text.c_str(), n);
  buf[n] = 0;
  return n;
}

// Properties

static std::wstring PropName(LPCWSTR s) {
  if (IS_INTRESOURCE(s)) return L"#" + std::to_wstring((UINT_PTR)s);
  // An atom's name finds the same property as the atom.
  ATOM a = GlobalFindAtomW(s);
  if (a) return L"#" + std::to_wstring(a);
  return Lower(s);
}

BOOL WINAPI SetPropW(HWND h, LPCWSTR name, HANDLE data) {
  Window* w = GetWindowOrFail(h);
  if (!w || !name) return FALSE;
  UserGuard g(UserLock());
  std::wstring n = PropName(name);
  for (auto& p : w->props)
    if (p.first == n) {
      p.second = data;
      return TRUE;
    }
  w->props.push_back(std::make_pair(n, data));
  return TRUE;
}

HANDLE WINAPI GetPropW(HWND h, LPCWSTR name) {
  Window* w = GetWindow(h);
  if (!w || !name) return NULL;
  UserGuard g(UserLock());
  std::wstring n = PropName(name);
  for (auto& p : w->props)
    if (p.first == n) return p.second;
  return NULL;
}

HANDLE WINAPI RemovePropW(HWND h, LPCWSTR name) {
  Window* w = GetWindow(h);
  if (!w || !name) return NULL;
  UserGuard g(UserLock());
  std::wstring n = PropName(name);
  for (size_t i = 0; i < w->props.size(); i++)
    if (w->props[i].first == n) {
      HANDLE d = w->props[i].second;
      w->props.erase(w->props.begin() + i);
      return d;
    }
  return NULL;
}

BOOL WINAPI SetPropA(HWND h, LPCSTR name, HANDLE data) {
  return SetPropW(h, IS_INTRESOURCE(name) ? (LPCWSTR)name : Widen(name).c_str(), data);
}
HANDLE WINAPI GetPropA(HWND h, LPCSTR name) {
  return GetPropW(h, IS_INTRESOURCE(name) ? (LPCWSTR)name : Widen(name).c_str());
}
HANDLE WINAPI RemovePropA(HWND h, LPCSTR name) {
  return RemovePropW(h, IS_INTRESOURCE(name) ? (LPCWSTR)name : Widen(name).c_str());
}

int WINAPI EnumPropsW(HWND h, PROPENUMPROCW proc) {
  Window* w = GetWindow(h);
  if (!w) return -1;
  std::vector<std::pair<std::wstring, HANDLE>> props = w->props;
  int r = -1;
  for (auto& p : props) {
    LPCWSTR name = p.first[0] == '#' ? (LPCWSTR)(UINT_PTR)wcstoul(p.first.c_str() + 1, NULL, 10) : p.first.c_str();
    r = proc(h, name, p.second);
    if (!r) break;
  }
  return r;
}

int WINAPI EnumPropsA(HWND h, PROPENUMPROCA proc) {
  Window* w = GetWindow(h);
  if (!w) return -1;
  std::vector<std::pair<std::wstring, HANDLE>> props = w->props;
  int r = -1;
  for (auto& p : props) {
    std::string n = Narrow(p.first.c_str());
    LPCSTR name = p.first[0] == '#' ? (LPCSTR)(UINT_PTR)wcstoul(p.first.c_str() + 1, NULL, 10) : n.c_str();
    r = proc(h, name, p.second);
    if (!r) break;
  }
  return r;
}

BOOL WINAPI SetWindowContextHelpId(HWND h, DWORD id) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  w->contextHelpId = id;
  return TRUE;
}

DWORD WINAPI GetWindowContextHelpId(HWND h) {
  Window* w = GetWindow(h);
  return w ? w->contextHelpId : 0;
}

UINT WINAPI GetDpiForWindow(HWND) { return 96; }

BOOL WINAPI IsHungAppWindow(HWND) { return FALSE; }

BOOL WINAPI FlashWindow(HWND, BOOL) { return FALSE; }
BOOL WINAPI FlashWindowEx(PFLASHWINFO) { return FALSE; }

BOOL WINAPI AnimateWindow(HWND h, DWORD, DWORD flags) {
  return ShowWindow(h, (flags & AW_HIDE) ? SW_HIDE : (flags & AW_ACTIVATE) ? SW_SHOW : SW_SHOWNA), TRUE;
}

}  // extern "C"
