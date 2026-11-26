// backend_macos.cpp - the window manager's half of the classic Mac OS
// backend, which shows top-level windows with the Toolbox (see
// macos_toolbox.h and macos_toolbox.cpp).
//
// The layer draws each window whole, frame and caption too, and the
// Toolbox shows it as a plain box. Keys reach the program as they would
// on Windows: Command works as Control, so that Command+C copies, and
// Option works as Alt. A mouse with one button gives the right button
// when Control is held, as the Mac OS 8 contextual menus do.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(macintosh)

#include "macos_toolbox.h"

#include <set>

namespace winport {

namespace {

struct Native {
  Window* w = NULL;
  void* win = NULL;
  bool popup = false;
  std::vector<DWORD> composed;  // a translucent window's look, while it is drawn
};

std::set<Native*> g_natives;

// Gives the window an owner names, or NULL when it is gone.
Native* Valid(void* owner) {
  Native* n = (Native*)owner;
  return g_natives.count(n) ? n : NULL;
}

// The keys by their Mac virtual key code, which names a place on the
// keyboard rather than what the key types (as in backend_cocoa.cpp).
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

// Tells whether a key is one of Windows' extended keys.
bool Extended(unsigned code) {
  switch (code) {
    case 0x36: case 0x3D: case 0x3E: case 0x4B: case 0x4C: case 0x6E: case 0x72: case 0x73: case 0x74:
    case 0x75: case 0x77: case 0x79: case 0x7B: case 0x7C: case 0x7D: case 0x7E:
      return true;
  }
  return false;
}

unsigned g_buttons;     // MK_ flags of the buttons down
UINT g_upMessage;       // the message for the button that is down
volatile bool g_woken;  // set by Wake

// Gives the MK_ flags for the buttons and the modifier keys.
WPARAM Keys(unsigned mods) {
  WPARAM k = g_buttons;
  if (mods & mactb::kShift) k |= MK_SHIFT;
  if (mods & (mactb::kControl | mactb::kCommand)) k |= MK_CONTROL;
  return k;
}

// Gives the native window of the program's active window, or NULL.
Native* ActiveNative() {
  HWND active = GetActiveWindow();
  for (Native* c : g_natives)
    if (c->w->hwnd == active) return c;
  return NULL;
}

// Gives a character as Mac Roman, or '?' when Mac Roman has none.
char ToMacRoman(wchar_t c) {
  if (c < 0x80) return (char)c;
  for (unsigned i = 0x80; i < 0x100; i++)
    if (mactb::FromMacRoman((unsigned char)i) == c) return (char)i;
  return '?';
}

// Copies the rectangle r of a window's surface to its Toolbox window.
// The caller holds the lock.
void DrawSurface(Native* n, const RECT& r) {
  Surface* s = n->w->surface;
  if (!s || s->width <= 0 || s->height <= 0) return;
  RECT c = r;
  if (c.left < 0) c.left = 0;
  if (c.top < 0) c.top = 0;
  if (c.right > s->width) c.right = s->width;
  if (c.bottom > s->height) c.bottom = s->height;
  if (c.right <= c.left || c.bottom <= c.top) return;
  const DWORD* pixels = s->pixels.data();
  if (Translucent(n->w)) {
    // The Toolbox has no translucent windows: the look goes over black.
    n->composed.resize((size_t)s->width * s->height);
    Composite(n->w, RECT{0, 0, s->width, s->height}, n->composed.data(), s->width);
    pixels = n->composed.data();
  }
  mactb::Draw(n->win, (const uint32_t*)pixels, s->width, s->height, c.left, c.top, c.right - c.left,
              c.bottom - c.top);
}

struct MacBackend : Backend {
  // Gives the backend's name.
  const char* Name() override { return "macos"; }

  // Gives the size of the whole screen, with the menu bar.
  SIZE ScreenSize() override {
    int w, h;
    mactb::ScreenSize(&w, &h);
    return SIZE{w, h};
  }

  // Gives the screen below the menu bar.
  RECT WorkArea() override {
    int x, y, w, h;
    mactb::WorkArea(&x, &y, &w, &h);
    return RECT{x, y, x + w, y + h};
  }

  // Reads where the pointer is now, from the Event Manager.
  bool ReadCursor(POINT* p) override {
    int x, y;
    mactb::MousePosition(&x, &y);
    p->x = x;
    p->y = y;
    return true;
  }

  // Tells whether a window is a menu, a drop-down or a tooltip, which is
  // shown without becoming the active window.
  static bool IsPopup(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return false;
    if (w->exStyle & WS_EX_APPWINDOW) return false;
    return (w->style & WS_POPUP) != 0;
  }

  // Gives the Toolbox window the window's title, in Mac Roman.
  static void SetTitle(Native* n) {
    std::string t;
    for (wchar_t c : n->w->text) t += ToMacRoman(c);
    mactb::SetTitle(n->win, t.c_str());
  }

  // Shows or hides a window, and makes its Toolbox window the first
  // time it is shown.
  void Show(Window* w, bool show) override {
    Native* n = (Native*)w->native;
    if (!n) {
      if (!show) return;
      n = new Native;
      n->w = w;
      n->popup = IsPopup(w);
      RECT r = w->rect;
      n->win = mactb::Create(n, r.left, r.top, r.right - r.left, r.bottom - r.top);
      if (!n->win) {
        delete n;
        return;
      }
      g_natives.insert(n);
      w->native = n;
      SetTitle(n);
    }
    mactb::Show(n->win, show, (w->style & WS_DISABLED) != 0);
    if (show) DrawSurface(n, RECT{0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top});
  }

  // Disposes of a window's Toolbox window.
  void Destroy(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    mactb::Destroy(n->win);
    g_natives.erase(n);
    w->native = NULL;
    delete n;
  }

  // Moves and sizes the Toolbox window as the window.
  void Moved(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    RECT r = w->rect;
    mactb::SetFrame(n->win, r.left, r.top, r.right - r.left, r.bottom - r.top);
  }

  // Gives the Toolbox window the window's new title.
  void TitleChanged(Window* w) override {
    if (Native* n = (Native*)w->native) SetTitle(n);
  }

  // Shows the whole window again in its new look.
  void TranslucencyChanged(Window* w) override {
    if (Native* n = (Native*)w->native)
      DrawSurface(n, RECT{0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top});
  }

  // Copies the changed part of the surface to the screen at once: the
  // Toolbox keeps no picture of a window.
  void Present(Window* w, const RECT& r) override {
    if (Native* n = (Native*)w->native) DrawSurface(n, r);
  }

  // Handles Toolbox events for up to timeout ms, or until Wake.
  void Wait(DWORD timeout) override {
    if (g_woken) {
      g_woken = false;
      return;
    }
    mactb::Wait(timeout == INFINITE ? -1 : (long)timeout, &g_woken);
    g_woken = false;
  }

  // Makes Wait return.
  void Wake() override { g_woken = true; }
};

}  // namespace

// Gives the Toolbox backend, or NULL when the Toolbox cannot show windows.
Backend* NewMacBackend() {
  if (!mactb::Start()) return NULL;
  return new MacBackend;
}

namespace mactb {

// Gives the layer a mouse movement or a click in a window.
void OnMouse(void* owner, int kind, int x, int y, unsigned mods) {
  Native* n = Valid(owner);
  if (!n) return;
  UINT msg = WM_MOUSEMOVE;
  if (kind == kDown) {
    bool right = (mods & kControl) != 0;
    msg = right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
    g_upMessage = right ? WM_RBUTTONUP : WM_LBUTTONUP;
    g_buttons |= right ? MK_RBUTTON : MK_LBUTTON;
    mods &= ~(unsigned)kControl;
  } else if (kind == kUp) {
    if (!g_upMessage) return;
    msg = g_upMessage;
    g_upMessage = 0;
    g_buttons = 0;
  }
  MouseInput(n->w, msg, x, y, Keys(mods));
}

// Gives the layer a key that went down, repeated or came up.
void OnKey(void* owner, bool down, unsigned keyCode, unsigned charCode, unsigned mods) {
  Native* n = Valid(owner);
  if (!n) return;
  unsigned code = keyCode & 0x7F;
  UINT vk = kMacKeys[code];
  LPARAM lp = 1 | ((LPARAM)code << 16) | (Extended(code) ? (1 << 24) : 0);
  wchar_t ch = 0;
  if (down) {
    if (mods & kCommand) {
      // Command works as Control: Command+C types Control+C.
      ch = (vk >= 'A' && vk <= 'Z') ? (wchar_t)(vk - 'A' + 1) : 0;
    } else if ((mods & kOption) && vk >= 'A' && vk <= 'Z') {
      // Alt with a letter is a menu's mnemonic, not the letter Option types.
      ch = (wchar_t)((mods & kShift) ? vk : vk - 'A' + 'a');
    } else {
      switch (charCode) {
        case 0x03: ch = '\r'; break;  // Enter
        case 0x08: case 0x09: case 0x0D: case 0x1B: ch = (wchar_t)charCode; break;
        default: ch = charCode >= 0x20 && charCode != 0x7F ? FromMacRoman((unsigned char)charCode) : 0; break;
      }
    }
  }
  if (ch) lp |= 0x02000000;  // the character comes from here, not TranslateMessage
  if (!down) lp |= 0xC0000000;
  if (vk) KeyInput(n->w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
  if (ch) CharInput(n->w, ch);
}

// Gives the layer the modifier keys that went down or came up: the
// Event Manager tells only their state.
void OnModifiers(void* owner, unsigned was, unsigned now) {
  Native* n = Valid(owner);
  if (!n) return;
  static const struct {
    unsigned mask;
    UINT vk;
    unsigned code;
  } kMods[] = {
      {kShift, VK_LSHIFT, 0x38},
      {kControl | kCommand, VK_LCONTROL, 0x3B},
      {kOption, VK_LMENU, 0x3A},
  };
  for (const auto& m : kMods) {
    bool before = (was & m.mask) != 0, after = (now & m.mask) != 0;
    if (before == after) continue;
    LPARAM lp = 1 | ((LPARAM)m.code << 16) | (after ? 0 : 0xC0000000);
    KeyInput(n->w, after ? WM_KEYDOWN : WM_KEYUP, m.vk, lp);
  }
  if ((was ^ now) & kCapsLock) {
    // Caps Lock reports only its state: it is pressed and let go at once.
    LPARAM lp = 1 | (0x39 << 16);
    KeyInput(n->w, WM_KEYDOWN, VK_CAPITAL, lp);
    KeyInput(n->w, WM_KEYUP, VK_CAPITAL, lp | 0xC0000000);
  }
}

// Draws a window again where the Toolbox has lost its picture.
void OnUpdate(void* owner) {
  UserGuard g(UserLock());
  Native* n = Valid(owner);
  if (!n || !GetWindow(n->w->hwnd)) return;
  DrawSurface(n, RECT{0, 0, n->w->rect.right - n->w->rect.left, n->w->rect.bottom - n->w->rect.top});
}

// Makes a window the program's active one when the user brings it to
// the front.
void OnActivate(void* owner, bool active) {
  Native* n = Valid(owner);
  if (!n || n->popup || !active) return;
  HWND h = n->w->hwnd;
  if (GetActiveWindow() != h && IsWindowEnabled(h)) SetActiveWindow(h);
}

// Asks the active window to close, as Alt+F4 would, when the Finder
// asks the program to quit.
void OnQuit() {
  if (Native* n = ActiveNative()) {
    if (!n->popup) {
      CloseRequested(n->w);
      return;
    }
  }
  for (Native* n : g_natives)
    if (!n->popup && n->w->Visible()) {
      CloseRequested(n->w);
      return;
    }
  PostQuitMessage(0);
}

// Gives the window that keys go to: the program's active one.
void* KeyOwner() { return ActiveNative(); }

}  // namespace mactb

}  // namespace winport

#endif
