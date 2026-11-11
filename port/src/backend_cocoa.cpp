// backend_cocoa.cpp - the window manager's half of the Cocoa backend,
// which shows top-level windows on macOS (see cocoa.h and
// backend_cocoa.mm).
//
// Keys reach the program as they would on Windows: Command works as
// Control, so that Command+C copies, and Option works as Alt.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(__APPLE__)

#include "cocoa.h"

#include <algorithm>
#include <set>

namespace winport {

namespace {

struct Native {
  Window* w = NULL;
  cocoa::Win* win = NULL;
  bool popup = false;
};

std::set<Native*> g_natives;

Native* Valid(void* owner) {
  Native* n = (Native*)owner;
  return g_natives.count(n) ? n : NULL;
}

// The keys by their Mac virtual key code, which names a place on the
// keyboard rather than what the key types.
const BYTE kMacKeys[128] = {
    'A', 'S', 'D', 'F', 'H', 'G', 'Z', 'X',                                            // 0x00
    'C', 'V', VK_OEM_102, 'B', 'Q', 'W', 'E', 'R',                                     // 0x08
    'Y', 'T', '1', '2', '3', '4', '6', '5',                                            // 0x10
    VK_OEM_PLUS, '9', '7', VK_OEM_MINUS, '8', '0', VK_OEM_6, 'O',                      // 0x18
    'U', VK_OEM_4, 'I', 'P', VK_RETURN, 'L', 'J', VK_OEM_7,                            // 0x20
    'K', VK_OEM_1, VK_OEM_5, VK_OEM_COMMA, VK_OEM_2, 'N', 'M', VK_OEM_PERIOD,          // 0x28
    VK_TAB, VK_SPACE, VK_OEM_3, VK_BACK, 0, VK_ESCAPE, VK_RCONTROL, VK_LCONTROL,       // 0x30: Command is Control
    VK_LSHIFT, VK_CAPITAL, VK_LMENU, VK_LCONTROL, VK_RSHIFT, VK_RMENU, VK_RCONTROL, 0,  // 0x38
    VK_F17, VK_DECIMAL, 0, VK_MULTIPLY, 0, VK_ADD, 0, VK_NUMLOCK,                      // 0x40: Clear is Num Lock
    0, 0, 0, VK_DIVIDE, VK_RETURN, 0, VK_SUBTRACT, VK_F18,                             // 0x48
    VK_F19, 0, VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5,  // 0x50
    VK_NUMPAD6, VK_NUMPAD7, VK_F20, VK_NUMPAD8, VK_NUMPAD9, 0, 0, 0,                   // 0x58
    VK_F5, VK_F6, VK_F7, VK_F3, VK_F8, VK_F9, 0, VK_F11,                               // 0x60
    0, VK_F13, VK_F16, VK_F14, 0, VK_F10, VK_APPS, VK_F12,                             // 0x68
    0, VK_F15, VK_INSERT, VK_HOME, VK_PRIOR, VK_DELETE, VK_F4, VK_END,                 // 0x70: Help is Insert
    VK_F2, VK_NEXT, VK_F1, VK_LEFT, VK_RIGHT, VK_DOWN, VK_UP, 0,                       // 0x78
};

bool Extended(unsigned code) {
  switch (code) {
    case 0x36: case 0x3D: case 0x3E: case 0x4B: case 0x4C: case 0x6E: case 0x72: case 0x73: case 0x74:
    case 0x75: case 0x77: case 0x79: case 0x7B: case 0x7C: case 0x7D: case 0x7E:
      return true;
  }
  return false;
}

// The modifier a modifier key changes, and Caps Lock (which Cocoa
// reports only as a state).
unsigned ModifierOf(unsigned code) {
  switch (code) {
    case 0x38: case 0x3C: return cocoa::kShift;
    case 0x3B: case 0x3E: return cocoa::kControl;
    case 0x3A: case 0x3D: return cocoa::kOption;
    case 0x37: case 0x36: return cocoa::kCommand;
  }
  return 0;
}

unsigned g_buttons;  // MK_ flags of the buttons down

WPARAM Keys(unsigned mods) {
  WPARAM k = g_buttons;
  if (mods & cocoa::kShift) k |= MK_SHIFT;
  if (mods & (cocoa::kControl | cocoa::kCommand)) k |= MK_CONTROL;
  return k;
}

struct CocoaBackend : Backend {
  const char* Name() override { return "cocoa"; }

  SIZE ScreenSize() override {
    int w, h;
    cocoa::ScreenSize(&w, &h);
    return SIZE{w, h};
  }

  // Menus, drop-downs and tooltips float; windows with a caption, or
  // shown in the Dock, are ordinary windows.
  static bool IsPopup(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return false;
    if (w->exStyle & WS_EX_APPWINDOW) return false;
    return (w->style & WS_POPUP) != 0;
  }

  static void SetTitle(Native* n) {
    std::vector<uint16_t> t(n->w->text.begin(), n->w->text.end());
    cocoa::SetTitle(n->win, t.data(), t.size());
  }

  void Show(Window* w, bool show) override {
    Native* n = (Native*)w->native;
    if (!n) {
      if (!show) return;
      n = new Native;
      n->w = w;
      n->popup = IsPopup(w);
      RECT r = w->rect;
      g_natives.insert(n);
      n->win = cocoa::Create(n, r.left, r.top, r.right - r.left, r.bottom - r.top, n->popup);
      w->native = n;
      SetTitle(n);
    }
    cocoa::Show(n->win, !show ? cocoa::kHide : (n->popup || (w->style & WS_DISABLED)) ? cocoa::kShow : cocoa::kShowKey);
  }

  void Destroy(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    cocoa::Destroy(n->win);
    g_natives.erase(n);
    w->native = NULL;
    delete n;
  }

  void Moved(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    RECT r = w->rect;
    cocoa::SetFrame(n->win, r.left, r.top, r.right - r.left, r.bottom - r.top);
  }

  void TitleChanged(Window* w) override {
    if (Native* n = (Native*)w->native) SetTitle(n);
  }

  void Present(Window* w, const RECT& r) override {
    if (Native* n = (Native*)w->native) cocoa::Invalidate(n->win, r.left, r.top, r.right - r.left, r.bottom - r.top);
  }

  void Wait(DWORD timeout) override { cocoa::Wait(timeout == INFINITE ? -1 : (long)timeout); }
  void Wake() override { cocoa::Wake(); }
};

}  // namespace

Backend* NewCocoaBackend() {
  if (!cocoa::Start()) return NULL;
  return new CocoaBackend;
}

namespace cocoa {

void OnMouse(void* owner, int kind, int button, int x, int y, unsigned mods) {
  Native* n = Valid(owner);
  if (!n) return;
  static const UINT down[] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
  static const UINT up[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
  static const unsigned flag[] = {MK_LBUTTON, MK_RBUTTON, MK_MBUTTON};
  UINT msg = WM_MOUSEMOVE;
  if (kind == kDown) {
    msg = down[button];
    g_buttons |= flag[button];
  } else if (kind == kUp) {
    msg = up[button];
    g_buttons &= ~flag[button];
  }
  MouseInput(n->w, msg, x - n->w->rect.left, y - n->w->rect.top, Keys(mods));
}

void OnWheel(void* owner, int x, int y, double up, double right, unsigned mods) {
  static double acc[2];
  double d[2] = {up, right};
  for (int i = 0; i < 2; i++) {
    acc[i] += d[i];
    int notches = (int)acc[i];
    acc[i] -= notches;
    Native* n = Valid(owner);
    if (!notches || !n) continue;
    MouseInput(n->w, i == 0 ? WM_MOUSEWHEEL : WM_MOUSEHWHEEL, x - n->w->rect.left, y - n->w->rect.top,
               MAKEWPARAM(Keys(mods), (WORD)(short)(notches * WHEEL_DELTA)));
  }
}

bool OnKey(void* owner, int type, unsigned code, unsigned mods, const uint16_t* text, size_t textLength,
           const uint16_t* plain, size_t plainLength) {
  // The key window, or the program's active window when a popup is in
  // front of it.
  Native* n = Valid(owner);
  if (!n) {
    HWND active = GetActiveWindow();
    for (Native* c : g_natives)
      if (c->w->hwnd == active) n = c;
  }
  if (!n) return false;
  UINT vk = code < 128 ? kMacKeys[code] : 0;
  LPARAM lp = 1 | ((LPARAM)(code & 0xFF) << 16) | (Extended(code) ? (1 << 24) : 0);
  if (type == kModifiers) {
    if (!vk) return true;
    if (code == 0x39) {
      // Caps Lock reports only its state: it is pressed and let go at once.
      KeyInput(n->w, WM_KEYDOWN, vk, lp);
      KeyInput(n->w, WM_KEYUP, vk, lp);
      return true;
    }
    unsigned mod = ModifierOf(code);
    if (mod) KeyInput(n->w, (mods & mod) ? WM_KEYDOWN : WM_KEYUP, vk, lp);
    return true;
  }
  bool down = type == kKeyDown;
  // Letters and digits follow the keyboard's layout.
  if (plainLength == 1) {
    uint16_t c = plain[0];
    if (c >= 'a' && c <= 'z') vk = c - 'a' + 'A';
    else if (c >= 'A' && c <= 'Z') vk = c;
    else if (c >= '0' && c <= '9' && !(code >= 0x52 && code <= 0x5C)) vk = c;
  }
  wchar_t ch = 0;
  if (down) {
    // Alt with a letter is a menu's mnemonic, not the letter Option types.
    if (mods & kOption) {
      text = plain;
      textLength = plainLength;
    }
    if (textLength == 1) ch = text[0];
    if (ch == 0x7F) ch = '\b';
    if (ch == 0x03) ch = '\r';  // Enter
    if (ch == 0x19) ch = '\t';  // Shift+Tab
    if (ch >= 0xF700 && ch <= 0xF8FF) ch = 0;  // arrows, function keys
    if (mods & kCommand) {
      // Command works as Control: Command+C types Control+C.
      ch = (vk >= 'A' && vk <= 'Z') ? (wchar_t)(vk - 'A' + 1) : 0;
    }
  }
  if (ch) lp |= 0x02000000;  // the character comes from here, not TranslateMessage
  if (vk) KeyInput(n->w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
  if (ch) CharInput(n->w, ch);
  return true;
}

void OnBecameKey(void* owner) {
  Native* n = Valid(owner);
  if (!n || n->popup) return;
  HWND h = n->w->hwnd;
  if (GetActiveWindow() != h && IsWindowEnabled(h)) SetActiveWindow(h);
}

void OnQuit() {
  // As Alt+F4 would: the active window is asked to close.
  HWND active = GetActiveWindow();
  for (Native* n : g_natives)
    if (n->w->hwnd == active && !n->popup) {
      CloseRequested(n->w);
      return;
    }
  for (Native* n : g_natives)
    if (!n->popup && n->w->Visible()) {
      CloseRequested(n->w);
      return;
    }
  PostQuitMessage(0);
}

bool BeginDraw(void* owner, const uint32_t** pixels, int* width, int* height) {
  UserLock().lock();
  Native* n = Valid(owner);
  Surface* s = n && GetWindow(n->w->hwnd) ? n->w->surface : NULL;
  if (!s || s->width <= 0 || s->height <= 0) {
    UserLock().unlock();
    return false;
  }
  *pixels = (const uint32_t*)s->pixels.data();
  *width = s->width;
  *height = s->height;
  return true;
}

void EndDraw() { UserLock().unlock(); }

}  // namespace cocoa

}  // namespace winport

#else

namespace winport {
Backend* NewCocoaBackend() { return NULL; }
}  // namespace winport

#endif
