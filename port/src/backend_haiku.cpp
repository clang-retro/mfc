// backend_haiku.cpp - the window manager's half of the Haiku backend,
// which shows top-level windows on Haiku (see haiku.h and
// backend_haiku_be.cpp).
//
// Keys reach the program as they would on Windows, by their place on the
// keyboard: the key marked Ctrl is Control, the key marked Alt (Haiku's
// Command key) is Alt, and the Windows key (Haiku's Option key) is the
// Windows key. Letters and digits follow the keyboard layout.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(__HAIKU__)

#include "haiku.h"

#include <algorithm>
#include <set>

namespace winport {

namespace {

struct Native {
  Window* w = NULL;
  haiku::Win* win = NULL;
  bool popup = false;
  std::vector<DWORD> composed;  // a translucent window's look, made opaque
};

std::set<Native*> g_natives;
unsigned g_buttons;  // MK_ flags of the mouse buttons down

// The native window of an owner pointer from an event, or NULL when that
// window is gone.
Native* Valid(void* owner) {
  Native* n = (Native*)owner;
  return g_natives.count(n) ? n : NULL;
}

// The virtual keys by Haiku's key code (the place of the key on the
// keyboard), from 0x00 to 0x6B.
const BYTE kHaikuKeys[0x6C] = {
    0, VK_ESCAPE, VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6,                                    // 0x00
    VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12, VK_SNAPSHOT, VK_SCROLL,                       // 0x08
    VK_PAUSE, VK_OEM_3, '1', '2', '3', '4', '5', '6',                                          // 0x10
    '7', '8', '9', '0', VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK, VK_INSERT,                         // 0x18
    VK_HOME, VK_PRIOR, VK_NUMLOCK, VK_DIVIDE, VK_MULTIPLY, VK_SUBTRACT, VK_TAB, 'Q',           // 0x20
    'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O',                                                    // 0x28
    'P', VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_DELETE, VK_END, VK_NEXT, VK_NUMPAD7,                 // 0x30
    VK_NUMPAD8, VK_NUMPAD9, VK_ADD, VK_CAPITAL, 'A', 'S', 'D', 'F',                            // 0x38
    'G', 'H', 'J', 'K', 'L', VK_OEM_1, VK_OEM_7, VK_RETURN,                                    // 0x40
    VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_LSHIFT, 'Z', 'X', 'C', 'V',                         // 0x48
    'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2, VK_RSHIFT, VK_UP,                    // 0x50
    VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_RETURN, VK_LCONTROL, VK_LMENU, VK_SPACE, VK_RMENU,  // 0x58
    VK_RCONTROL, VK_LEFT, VK_DOWN, VK_RIGHT, VK_NUMPAD0, VK_DECIMAL, VK_LWIN, VK_RWIN,         // 0x60
    VK_APPS, VK_OEM_102, 0, 0,                                                                 // 0x68
};

// Tells if a key is one of the extended keys of a PC keyboard (bit 24
// of a key message's lParam): the arrows and the keys above them, the
// right-hand modifiers, keypad Enter and keypad /.
bool Extended(unsigned key) {
  switch (key) {
    case 0x1f: case 0x20: case 0x21: case 0x34: case 0x35: case 0x36:  // Insert to Page Down
    case 0x57: case 0x61: case 0x62: case 0x63:                        // the arrows
    case 0x5b: case 0x23: case 0x5f: case 0x60: case 0x0e:             // keypad Enter and /, right Alt and Ctrl
    case 0x66: case 0x67: case 0x68:                                   // Windows and Menu keys
      return true;
  }
  return false;
}

// Tells if a key types no character on Windows: the arrows, the editing
// keys above them, the function keys and the modifiers.
bool TypesNothing(UINT vk) {
  switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_SNAPSHOT: case VK_SCROLL:
    case VK_PAUSE: case VK_NUMLOCK: case VK_CAPITAL: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
    case VK_APPS:
      return true;
  }
  return vk >= VK_F1 && vk <= VK_F24;
}

// The MK_ flags of the buttons and the modifiers held.
WPARAM Keys(unsigned mods) {
  WPARAM k = g_buttons;
  if (mods & haiku::kShift) k |= MK_SHIFT;
  if (mods & haiku::kControl) k |= MK_CONTROL;
  return k;
}

// Gives a key to the program, as WM_KEYDOWN or WM_KEYUP and the
// character it types.
void Key(Native* n, const haiku::Event& e) {
  unsigned code = e.key;
  UINT vk = code < sizeof kHaikuKeys ? kHaikuKeys[code] : 0;
  // Letters and digits follow the keyboard's layout.
  uint32_t plain = e.raw;
  if (plain >= 'a' && plain <= 'z') vk = plain - 'a' + 'A';
  else if (plain >= 'A' && plain <= 'Z') vk = plain;
  else if (plain >= '0' && plain <= '9' && !(vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)) vk = plain;
  bool down = e.kind == haiku::Event::kKeyDown;
  LPARAM lp = 1 | ((LPARAM)(code & 0xFF) << 16) | (Extended(code) ? (1 << 24) : 0);
  wchar_t ch = 0;
  if (down && !TypesNothing(vk)) {
    if (vk == VK_RETURN) ch = '\r';
    else if (vk == VK_BACK) ch = '\b';
    else if (vk == VK_TAB) ch = '\t';
    else if (vk == VK_ESCAPE) ch = 0x1B;
    else if ((e.mods & haiku::kControl) && !(e.mods & haiku::kAlt))
      // Control with a letter types the control character.
      ch = (vk >= 'A' && vk <= 'Z') ? (wchar_t)(vk - 'A' + 1) : 0;
    else if (e.mods & haiku::kAlt)
      // Alt with a letter is a menu's mnemonic: the plain letter.
      ch = (plain >= 0x20 && plain < 0x7F) ? (wchar_t)plain : 0;
    else if (e.ch >= 0x20 && e.ch != 0x7F)
      ch = (wchar_t)e.ch;
  }
  if (ch) lp |= 0x02000000;  // the character comes from here, not TranslateMessage
  if (vk) KeyInput(n->w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
  if (ch) CharInput(n->w, ch);
}

// The window that keys go to: the event's window, or the program's
// active window when the event's window is a popup or gone.
Native* KeyNative(void* owner) {
  Native* n = Valid(owner);
  if (!n || n->popup) {
    HWND active = GetActiveWindow();
    for (Native* c : g_natives)
      if (c->w->hwnd == active) return c;
  }
  return n;
}

// Gives one event from the queue to the window manager.
void Handle(const haiku::Event& e) {
  using haiku::Event;
  if (e.kind == Event::kQuit) {
    // Quit from the Deskbar: the active window is asked to close, as
    // Alt+F4 would; with no window, the program ends.
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
    return;
  }
  if (e.kind == Event::kKeyDown || e.kind == Event::kKeyUp) {
    if (Native* n = KeyNative(e.owner)) Key(n, e);
    return;
  }
  Native* n = Valid(e.owner);
  if (!n) return;
  switch (e.kind) {
    case Event::kMouseDown:
    case Event::kMouseUp:
    case Event::kMouseMove: {
      static const UINT down[] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
      static const UINT up[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
      static const unsigned flag[] = {MK_LBUTTON, MK_RBUTTON, MK_MBUTTON};
      UINT msg = WM_MOUSEMOVE;
      if (e.kind == Event::kMouseDown) {
        msg = down[e.button];
        g_buttons |= flag[e.button];
      } else if (e.kind == Event::kMouseUp) {
        msg = up[e.button];
        g_buttons &= ~flag[e.button];
      }
      MouseInput(n->w, msg, e.x, e.y, Keys(e.mods));
      break;
    }
    case Event::kWheel: {
      // Haiku's wheel moves down for a positive delta; Windows' moves up.
      if (e.wheelY)
        MouseInput(n->w, WM_MOUSEWHEEL, e.x, e.y, MAKEWPARAM(Keys(e.mods), (WORD)(short)(-e.wheelY * WHEEL_DELTA)));
      if (e.wheelX)
        MouseInput(n->w, WM_MOUSEHWHEEL, e.x, e.y, MAKEWPARAM(Keys(e.mods), (WORD)(short)(e.wheelX * WHEEL_DELTA)));
      break;
    }
    case Event::kActivated:
      if (e.active && !n->popup) {
        HWND h = n->w->hwnd;
        if (GetActiveWindow() != h && IsWindowEnabled(h)) SetActiveWindow(h);
      }
      break;
    case Event::kClose:
      CloseRequested(n->w);
      break;
    default:
      break;
  }
}

// UTF-8 text of a window's title.
std::string Utf8(const std::wstring& w) { return Narrow(w.c_str(), (int)w.size()); }

struct HaikuBackend : Backend {
  const char* Name() override { return "haiku"; }

  SIZE ScreenSize() override {
    int w, h;
    haiku::ScreenSize(&w, &h);
    return SIZE{w, h};
  }

  // Menus, drop-downs and tooltips float; windows with a caption, or
  // shown in the Deskbar, are ordinary windows.
  static bool IsPopup(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return false;
    if (w->exStyle & WS_EX_APPWINDOW) return false;
    return (w->style & WS_POPUP) != 0;
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
      n->win = haiku::Create(n, r.left, r.top, r.right - r.left, r.bottom - r.top, n->popup);
      w->native = n;
      haiku::SetTitle(n->win, Utf8(w->text));
      Put(n, RECT{0, 0, r.right - r.left, r.bottom - r.top});
    }
    haiku::Show(n->win, show, show && !n->popup && !(w->style & WS_DISABLED));
  }

  void Destroy(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    haiku::Destroy(n->win);
    g_natives.erase(n);
    w->native = NULL;
    delete n;
  }

  void Moved(Window* w) override {
    Native* n = (Native*)w->native;
    if (!n) return;
    RECT r = w->rect;
    haiku::SetFrame(n->win, r.left, r.top, r.right - r.left, r.bottom - r.top);
  }

  void TitleChanged(Window* w) override {
    if (Native* n = (Native*)w->native) haiku::SetTitle(n->win, Utf8(w->text));
  }

  void TranslucencyChanged(Window* w) override {
    if (Native* n = (Native*)w->native)
      if (w->surface) Put(n, RECT{0, 0, w->surface->width, w->surface->height});
  }

  // Copies a part of the window's surface to its bitmap. Haiku has no
  // translucent windows: a translucent one is drawn over the panel color.
  void Put(Native* n, const RECT& r) {
    Surface* s = n->w->surface;
    if (!s || s->width <= 0 || s->height <= 0) return;
    const DWORD* px = s->pixels.data();
    if (Translucent(n->w)) {
      n->composed.resize((size_t)s->width * s->height);
      Composite(n->w, RECT{0, 0, s->width, s->height}, n->composed.data(), s->width);
      for (DWORD& p : n->composed) {
        unsigned a = p >> 24, back = 255 - a;
        unsigned red = ((p >> 16) & 0xFF) + 0xD8 * back / 255;
        unsigned green = ((p >> 8) & 0xFF) + 0xD8 * back / 255;
        unsigned blue = (p & 0xFF) + 0xD8 * back / 255;
        p = (std::min(red, 255u) << 16) | (std::min(green, 255u) << 8) | std::min(blue, 255u);
      }
      px = n->composed.data();
    }
    haiku::Update(n->win, (const uint32_t*)px, s->width, s->height, s->width, r.left, r.top, r.right - r.left,
                  r.bottom - r.top);
  }

  void Present(Window* w, const RECT& r) override {
    if (Native* n = (Native*)w->native) Put(n, r);
  }

  void SetCursor(HCURSOR h) override {
    CursorPicture p;
    bool have = h && CursorImage(h, &p);
    if (!have) p.systemId = 32512;
    haiku::SetCursor(!h, p.systemId, (const uint32_t*)p.argb.data(), p.width, p.height, p.hotspot.x, p.hotspot.y);
  }

  long clipboardCount = -1;  // the system clipboard's, when this program last set it

  bool ClipboardForeign(DWORD* generation) override {
    long count = haiku::ClipboardCount();
    if (count == clipboardCount) return false;
    *generation = (DWORD)count;
    return true;
  }

  bool ClipboardFetch(std::wstring* text) override {
    std::string u;
    if (!haiku::ClipboardText(&u)) return false;
    *text = Widen(u.c_str(), (int)u.size());
    return true;
  }

  void ClipboardOwn(const std::wstring& text) override { clipboardCount = haiku::SetClipboardText(Utf8(text)); }

  void Wait(DWORD timeout) override {
    for (const haiku::Event& e : haiku::Take(timeout == INFINITE ? -1 : (long)timeout)) Handle(e);
  }
  void Wake() override { haiku::Wake(); }
};

}  // namespace

Backend* NewHaikuBackend() {
  if (!haiku::Start()) return NULL;
  return new HaikuBackend;
}

}  // namespace winport

#else

namespace winport {
Backend* NewHaikuBackend() { return NULL; }
}  // namespace winport

#endif
