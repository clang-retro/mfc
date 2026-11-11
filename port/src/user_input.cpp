// user_input.cpp - focus, activation, mouse capture, the keyboard state,
// the cursor, and the input a backend brings in.
//
// Each thread has its own active window, focus and capture, as on
// Windows; the foreground window is the active window of the thread
// that last took the foreground.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <stdlib.h>

using namespace winport;

namespace {

HWND g_foreground = NULL;
HWND g_capture = NULL;  // the window with the mouse, whichever thread owns it
BYTE g_asyncKeys[256];  // 0x80 down, 0x01 pressed since the last GetAsyncKeyState
HCURSOR g_cursor = NULL;
int g_cursorShown = 0;
RECT g_clipCursor = {0, 0, 0, 0};
UINT g_doubleClickTime = 500;

// The last button press, for double clicks.
struct LastClick {
  HWND hwnd;
  UINT msg;
  DWORD time;
  POINT pt;
} g_lastClick = {};

// TrackMouseEvent: the window that wants WM_MOUSELEAVE.
struct Tracking {
  HWND hwnd;
  DWORD flags;
  DWORD hoverTime;
  DWORD enteredAt;
  bool hovered;
} g_tracking = {};

HWND g_mouseOver = NULL;  // the window under the mouse at the last move

Queue* QueueOf(Window* w) { return QueueFor(w->tid); }

void SetKey(Queue* q, UINT vk, bool down) {
  if (vk >= 256) return;
  BYTE* keys[2] = {q ? q->keys : NULL, g_asyncKeys};
  for (BYTE* k : keys) {
    if (!k) continue;
    if (down) {
      if (!(k[vk] & 0x80)) k[vk] ^= 0x01;  // the toggle flips on each press
      k[vk] |= 0x80;
    } else {
      k[vk] &= ~0x80;
    }
  }
  if (down) g_asyncKeys[vk] |= 0x01;
}

WPARAM MouseKeys(Queue* q) {
  WPARAM k = 0;
  if (q->keys[VK_LBUTTON] & 0x80) k |= MK_LBUTTON;
  if (q->keys[VK_RBUTTON] & 0x80) k |= MK_RBUTTON;
  if (q->keys[VK_MBUTTON] & 0x80) k |= MK_MBUTTON;
  if (q->keys[VK_SHIFT] & 0x80) k |= MK_SHIFT;
  if (q->keys[VK_CONTROL] & 0x80) k |= MK_CONTROL;
  return k;
}

// The window the mouse is over: the deepest visible, enabled child at
// the point, skipping windows that answer HTTRANSPARENT.
Window* WindowAt(Window* top, POINT screen, LRESULT* hit) {
  Window* w = top;
  for (;;) {
    Window* next = NULL;
    {
      UserGuard g(UserLock());
      RECT c = ScreenClient(w);
      if (screen.x >= c.left && screen.x < c.right && screen.y >= c.top && screen.y < c.bottom) {
        for (Window* k : w->children) {
          if (!k->Visible() || (k->exStyle & WS_EX_TRANSPARENT)) continue;
          RECT r = ScreenRect(k);
          if (screen.x >= r.left && screen.x < r.right && screen.y >= r.top && screen.y < r.bottom) {
            next = k;
            break;
          }
        }
      }
    }
    if (!next || (next->style & WS_DISABLED)) break;
    w = next;
  }
  for (;;) {
    *hit = SendTo(w, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
    if (*hit != HTTRANSPARENT || !w->parent) break;
    w = w->parent;
  }
  return w;
}

bool IsButtonDown(UINT msg) {
  return msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN;
}

void SendCaptureChanged(HWND old, HWND now) {
  if (old && old != now)
    if (Window* w = GetWindow(old)) SendTo(w, WM_CAPTURECHANGED, 0, (LPARAM)now);
}

}  // namespace

namespace winport {

void ActivateWindow(Window* w, bool byMouse) {
  if (!w) return;
  w = w->TopLevel();
  HWND h = w->hwnd;
  Queue* q = QueueOf(w);
  HWND old = q->active;
  if (old == h) {
    g_foreground = h;
    return;
  }
  if (HaveHooks(WH_CBT)) {
    CBTACTIVATESTRUCT cbt = {byMouse, old};
    if (CallHooks(WH_CBT, HCBT_ACTIVATE, (WPARAM)h, (LPARAM)&cbt)) return;
  }
  bool appChange = g_foreground == NULL || GetWindow(g_foreground) == NULL ||
                   GetWindow(g_foreground)->tid != w->tid;
  if (Window* o = GetWindow(old)) {
    SendTo(o, WM_NCACTIVATE, FALSE, 0);
    SendTo(o, WM_ACTIVATE, WA_INACTIVE, (LPARAM)h);
  }
  if (!GetWindow(h)) return;
  {
    UserGuard g(UserLock());
    q->active = h;
    g_foreground = h;
    if (w->owner) w->owner->lastActive = h;
    w->lastActive = h;
  }
  SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (appChange) {
    std::vector<HWND> mine;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels())
        if (t->tid == w->tid) mine.push_back(t->hwnd);
    }
    for (HWND t : mine)
      if (Window* tw = GetWindow(t)) SendTo(tw, WM_ACTIVATEAPP, TRUE, 0);
  }
  if (!GetWindow(h)) return;
  SendTo(w, WM_NCACTIVATE, TRUE, 0);
  SendTo(w, WM_ACTIVATE, MAKEWPARAM(byMouse ? WA_CLICKACTIVE : WA_ACTIVE, (w->style & WS_MINIMIZE) != 0),
         (LPARAM)old);
}

// Called with the lock held, after WM_NCDESTROY.
void WindowDestroyed(Window* w) {
  HWND h = w->hwnd;
  Queue* q = QueueOf(w);
  if (q->focus == h) q->focus = NULL;
  if (q->active == h) q->active = NULL;
  if (q->capture == h) q->capture = NULL;
  if (g_capture == h) g_capture = NULL;
  if (g_foreground == h) g_foreground = NULL;
  if (g_mouseOver == h) g_mouseOver = NULL;
  if (g_tracking.hwnd == h) g_tracking.hwnd = NULL;
  if (g_lastClick.hwnd == h) g_lastClick.hwnd = NULL;
  for (Window* t : TopLevels())
    if (t->lastActive == h) t->lastActive = NULL;
  ForgetWindowMessages(w);
}

// Mouse input: x and y are in the top-level window's surface.
void MouseInput(Window* top, UINT msg, int x, int y, WPARAM wParam) {
  if (!GetWindow(top->hwnd)) return;
  POINT screen = {top->rect.left + x, top->rect.top + y};
  CursorPos() = screen;
  Queue* q = QueueOf(top);
  switch (msg) {
    case WM_LBUTTONDOWN: SetKey(q, VK_LBUTTON, true); break;
    case WM_LBUTTONUP: SetKey(q, VK_LBUTTON, false); break;
    case WM_RBUTTONDOWN: SetKey(q, VK_RBUTTON, true); break;
    case WM_RBUTTONUP: SetKey(q, VK_RBUTTON, false); break;
    case WM_MBUTTONDOWN: SetKey(q, VK_MBUTTON, true); break;
    case WM_MBUTTONUP: SetKey(q, VK_MBUTTON, false); break;
  }

  // The wheel goes to the focus.
  if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
    Window* f = GetWindow(q->focus);
    if (!f) f = GetWindow(q->active);
    if (!f) f = top;
    WPARAM wp = MAKEWPARAM(MouseKeys(q), HIWORD(wParam));
    QueueInput(f, msg, wp, MAKELPARAM(screen.x, screen.y));
    return;
  }

  Window* target;
  LRESULT hit = HTCLIENT;
  Window* cap = GetWindow(g_capture);
  if (cap) {
    target = cap;
  } else {
    target = WindowAt(top, screen, &hit);
    if (!GetWindow(target->hwnd)) return;
  }

  // Leaving a window that asked for WM_MOUSELEAVE.
  if (g_tracking.hwnd && g_tracking.hwnd != target->hwnd && (g_tracking.flags & TME_LEAVE)) {
    if (Window* t = GetWindow(g_tracking.hwnd)) QueueInput(t, WM_MOUSELEAVE, 0, 0);
    g_tracking.hwnd = NULL;
  }
  g_mouseOver = target->hwnd;

  if (!cap) {
    UINT mouseMsg = msg;
    SendTo(target, WM_SETCURSOR, (WPARAM)target->hwnd, MAKELPARAM(hit, mouseMsg));
    if (IsButtonDown(msg)) {
      Window* t = target->TopLevel();
      if (QueueOf(t)->active != t->hwnd || g_foreground != t->hwnd) {
        LRESULT ma = SendTo(target, WM_MOUSEACTIVATE, (WPARAM)t->hwnd, MAKELPARAM(hit, mouseMsg));
        if (ma != MA_NOACTIVATE && ma != MA_NOACTIVATEANDEAT) ActivateWindow(t, true);
        if (ma == MA_ACTIVATEANDEAT || ma == MA_NOACTIVATEANDEAT) return;
      }
      if (!GetWindow(target->hwnd)) return;
    }
  }

  // Double clicks, for classes that want them.
  if (IsButtonDown(msg)) {
    DWORD now = GetTickCount();
    bool dbl = g_lastClick.hwnd == target->hwnd && g_lastClick.msg == msg &&
               now - g_lastClick.time <= g_doubleClickTime &&
               abs(screen.x - g_lastClick.pt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
               abs(screen.y - g_lastClick.pt.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
    bool wantsDbl = hit != HTCLIENT || (target->cls && (target->cls->wc.style & CS_DBLCLKS));
    if (dbl && wantsDbl) {
      msg += WM_LBUTTONDBLCLK - WM_LBUTTONDOWN;
      g_lastClick.hwnd = NULL;
    } else {
      g_lastClick = LastClick{target->hwnd, msg, now, screen};
    }
  }

  if (hit == HTCLIENT || cap) {
    RECT c = ScreenClient(target);
    WPARAM wp = MouseKeys(q);
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK) wp |= wParam & 0xFFFF0000;
    QueueInput(target, msg, wp, MAKELPARAM(screen.x - c.left, screen.y - c.top));
  } else {
    // The non-client messages lie WM_NCMOUSEMOVE - WM_MOUSEMOVE below theirs.
    QueueInput(target, msg - (WM_MOUSEMOVE - WM_NCMOUSEMOVE), (WPARAM)hit, MAKELPARAM(screen.x, screen.y));
  }
}

// Keys: msg is WM_KEYDOWN or WM_KEYUP; Alt makes them WM_SYSKEY*.
void KeyInput(Window* top, UINT msg, UINT vk, LPARAM lParam) {
  if (!GetWindow(top->hwnd)) return;
  Queue* q = QueueOf(top);
  bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
  if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
  if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
  if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
  bool wasDown = (q->keys[vk & 0xFF] & 0x80) != 0;
  SetKey(q, vk, down);
  if (down && wasDown) lParam |= 0x40000000;  // a repeat
  if (!down) lParam |= 0xC0000000;
  bool alt = (q->keys[VK_MENU] & 0x80) != 0 && !(q->keys[VK_CONTROL] & 0x80);
  if (alt) lParam |= 0x20000000;
  Window* target = GetWindow(q->focus);
  bool sys = alt || vk == VK_F10 || vk == VK_MENU;
  if (!target) {
    // With no focus, keys go to the active window as system keys.
    target = GetWindow(q->active);
    sys = true;
  }
  if (!target) target = top;
  if (sys) msg = down ? WM_SYSKEYDOWN : WM_SYSKEYUP;
  else msg = down ? WM_KEYDOWN : WM_KEYUP;
  QueueInput(target, msg, vk, lParam);
}

// Text the backend has already made from keys.
void CharInput(Window* top, wchar_t ch) {
  if (!GetWindow(top->hwnd)) return;
  Queue* q = QueueOf(top);
  Window* target = GetWindow(q->focus);
  bool alt = (q->keys[VK_MENU] & 0x80) != 0 && !(q->keys[VK_CONTROL] & 0x80);
  if (!target) {
    target = GetWindow(q->active);
    alt = true;
  }
  if (!target) return;
  QueueInput(target, alt ? WM_SYSCHAR : WM_CHAR, (WPARAM)ch, alt ? 0x20000001 : 1);
}

void CloseRequested(Window* top) {
  if (GetWindow(top->hwnd)) PostMessageW(top->hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
}

void ResizeRequested(Window* top, int width, int height) {
  if (!GetWindow(top->hwnd)) return;
  SetWindowPos(top->hwnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace winport

extern "C" {

// Focus and activation

HWND WINAPI GetFocus(void) { return CurrentQueue()->focus; }
HWND WINAPI GetActiveWindow(void) { return CurrentQueue()->active; }
HWND WINAPI GetForegroundWindow(void) { return GetWindow(g_foreground) ? g_foreground : NULL; }

HWND WINAPI SetFocus(HWND h) {
  Queue* q = CurrentQueue();
  HWND old = q->focus;
  Window* w = NULL;
  if (h) {
    w = GetWindowOrFail(h);
    if (!w) return NULL;
    if (w->tid != q->tid) q = QueueOf(w);
    old = q->focus;
    // A window inside a hidden or disabled parent cannot take the focus.
    for (Window* p = w; p; p = p->parent) {
      if (p->style & WS_DISABLED && p != w) return NULL;
      if (!p->Visible() && p != w) return NULL;
    }
  }
  if (HaveHooks(WH_CBT) && CallHooks(WH_CBT, HCBT_SETFOCUS, (WPARAM)h, (LPARAM)old)) return NULL;
  if (w) {
    Window* top = w->TopLevel();
    if (q->active != top->hwnd) {
      ActivateWindow(top, false);
      if (!GetWindow(h)) return NULL;
    }
  }
  old = q->focus;
  if (old == h) return old;
  if (Window* o = GetWindow(old)) SendTo(o, WM_KILLFOCUS, (WPARAM)h, 0);
  if (h && !GetWindow(h)) return NULL;
  q->focus = h;
  if (w) SendTo(w, WM_SETFOCUS, (WPARAM)old, 0);
  return GetWindow(old) ? old : NULL;
}

HWND WINAPI SetActiveWindow(HWND h) {
  Queue* q = CurrentQueue();
  HWND old = q->active;
  if (!h) {
    if (Window* o = GetWindow(old)) {
      SendTo(o, WM_NCACTIVATE, FALSE, 0);
      SendTo(o, WM_ACTIVATE, WA_INACTIVE, 0);
    }
    q->active = NULL;
    if (q->focus) {
      if (Window* f = GetWindow(q->focus)) SendTo(f, WM_KILLFOCUS, 0, 0);
      q->focus = NULL;
    }
    if (g_foreground == old) g_foreground = NULL;
    return old;
  }
  Window* w = GetWindowOrFail(h);
  if (!w) return NULL;
  ActivateWindow(w, false);
  return old;
}

BOOL WINAPI SetForegroundWindow(HWND h) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  ActivateWindow(w, false);
  return TRUE;
}
BOOL WINAPI AllowSetForegroundWindow(DWORD) { return TRUE; }
BOOL WINAPI LockSetForegroundWindow(UINT) { return TRUE; }

// Capture

HWND WINAPI SetCapture(HWND h) {
  Queue* q = CurrentQueue();
  HWND old = q->capture;
  Window* w = h ? GetWindowOrFail(h) : NULL;
  if (h && !w) return NULL;
  q->capture = h;
  g_capture = h;
  SendCaptureChanged(old, h);
  return old;
}

HWND WINAPI GetCapture(void) { return CurrentQueue()->capture; }

BOOL WINAPI ReleaseCapture(void) {
  Queue* q = CurrentQueue();
  HWND old = q->capture;
  q->capture = NULL;
  if (g_capture == old) g_capture = NULL;
  SendCaptureChanged(old, NULL);
  return TRUE;
}

// The keyboard

SHORT WINAPI GetKeyState(int vk) {
  if (vk < 0 || vk > 255) return 0;
  BYTE k = CurrentQueue()->keys[vk];
  return (SHORT)(((k & 0x80) ? 0x8000 : 0) | (k & 0x01));
}

SHORT WINAPI GetAsyncKeyState(int vk) {
  if (vk < 0 || vk > 255) return 0;
  UserGuard g(UserLock());
  BYTE k = g_asyncKeys[vk];
  g_asyncKeys[vk] &= ~0x01;
  return (SHORT)(((k & 0x80) ? 0x8000 : 0) | (k & 0x01));
}

BOOL WINAPI GetKeyboardState(PBYTE keys) {
  if (!keys) return FALSE;
  memcpy(keys, CurrentQueue()->keys, 256);
  return TRUE;
}

BOOL WINAPI SetKeyboardState(LPBYTE keys) {
  if (!keys) return FALSE;
  memcpy(CurrentQueue()->keys, keys, 256);
  return TRUE;
}

// The keyboard layout: US English, PC scan codes. ext marks the keys that
// share a scan code with the keypad and arrive with the extended bit.
namespace {
struct KeyInfo {
  BYTE vk, scan, ext;
  const char* name;
};
const KeyInfo kKeys[] = {
    {VK_ESCAPE, 0x01, 0, "Esc"},      {'1', 0x02, 0, "1"},
    {'2', 0x03, 0, "2"},              {'3', 0x04, 0, "3"},
    {'4', 0x05, 0, "4"},              {'5', 0x06, 0, "5"},
    {'6', 0x07, 0, "6"},              {'7', 0x08, 0, "7"},
    {'8', 0x09, 0, "8"},              {'9', 0x0A, 0, "9"},
    {'0', 0x0B, 0, "0"},              {VK_OEM_MINUS, 0x0C, 0, "-"},
    {VK_OEM_PLUS, 0x0D, 0, "="},      {VK_BACK, 0x0E, 0, "Backspace"},
    {VK_TAB, 0x0F, 0, "Tab"},         {'Q', 0x10, 0, "Q"},
    {'W', 0x11, 0, "W"},              {'E', 0x12, 0, "E"},
    {'R', 0x13, 0, "R"},              {'T', 0x14, 0, "T"},
    {'Y', 0x15, 0, "Y"},              {'U', 0x16, 0, "U"},
    {'I', 0x17, 0, "I"},              {'O', 0x18, 0, "O"},
    {'P', 0x19, 0, "P"},              {VK_OEM_4, 0x1A, 0, "["},
    {VK_OEM_6, 0x1B, 0, "]"},         {VK_RETURN, 0x1C, 0, "Enter"},
    {VK_CONTROL, 0x1D, 0, "Ctrl"},    {'A', 0x1E, 0, "A"},
    {'S', 0x1F, 0, "S"},              {'D', 0x20, 0, "D"},
    {'F', 0x21, 0, "F"},              {'G', 0x22, 0, "G"},
    {'H', 0x23, 0, "H"},              {'J', 0x24, 0, "J"},
    {'K', 0x25, 0, "K"},              {'L', 0x26, 0, "L"},
    {VK_OEM_1, 0x27, 0, ";"},         {VK_OEM_7, 0x28, 0, "'"},
    {VK_OEM_3, 0x29, 0, "`"},         {VK_SHIFT, 0x2A, 0, "Shift"},
    {VK_OEM_5, 0x2B, 0, "\\"},        {'Z', 0x2C, 0, "Z"},
    {'X', 0x2D, 0, "X"},              {'C', 0x2E, 0, "C"},
    {'V', 0x2F, 0, "V"},              {'B', 0x30, 0, "B"},
    {'N', 0x31, 0, "N"},              {'M', 0x32, 0, "M"},
    {VK_OEM_COMMA, 0x33, 0, ","},     {VK_OEM_PERIOD, 0x34, 0, "."},
    {VK_OEM_2, 0x35, 0, "/"},         {VK_RSHIFT, 0x36, 0, "Right Shift"},
    {VK_MULTIPLY, 0x37, 0, "Num *"},  {VK_MENU, 0x38, 0, "Alt"},
    {VK_SPACE, 0x39, 0, "Space"},     {VK_CAPITAL, 0x3A, 0, "Caps Lock"},
    {VK_F1, 0x3B, 0, "F1"},           {VK_F2, 0x3C, 0, "F2"},
    {VK_F3, 0x3D, 0, "F3"},           {VK_F4, 0x3E, 0, "F4"},
    {VK_F5, 0x3F, 0, "F5"},           {VK_F6, 0x40, 0, "F6"},
    {VK_F7, 0x41, 0, "F7"},           {VK_F8, 0x42, 0, "F8"},
    {VK_F9, 0x43, 0, "F9"},           {VK_F10, 0x44, 0, "F10"},
    {VK_PAUSE, 0x45, 0, "Pause"},     {VK_SCROLL, 0x46, 0, "Scroll Lock"},
    {VK_NUMPAD7, 0x47, 0, "Num 7"},   {VK_NUMPAD8, 0x48, 0, "Num 8"},
    {VK_NUMPAD9, 0x49, 0, "Num 9"},   {VK_SUBTRACT, 0x4A, 0, "Num -"},
    {VK_NUMPAD4, 0x4B, 0, "Num 4"},   {VK_NUMPAD5, 0x4C, 0, "Num 5"},
    {VK_NUMPAD6, 0x4D, 0, "Num 6"},   {VK_ADD, 0x4E, 0, "Num +"},
    {VK_NUMPAD1, 0x4F, 0, "Num 1"},   {VK_NUMPAD2, 0x50, 0, "Num 2"},
    {VK_NUMPAD3, 0x51, 0, "Num 3"},   {VK_NUMPAD0, 0x52, 0, "Num 0"},
    {VK_DECIMAL, 0x53, 0, "Num Del"}, {VK_F11, 0x57, 0, "F11"},
    {VK_F12, 0x58, 0, "F12"},         {VK_HOME, 0x47, 1, "Home"},
    {VK_UP, 0x48, 1, "Up"},           {VK_PRIOR, 0x49, 1, "Page Up"},
    {VK_LEFT, 0x4B, 1, "Left"},       {VK_RIGHT, 0x4D, 1, "Right"},
    {VK_END, 0x4F, 1, "End"},         {VK_DOWN, 0x50, 1, "Down"},
    {VK_NEXT, 0x51, 1, "Page Down"},  {VK_INSERT, 0x52, 1, "Insert"},
    {VK_DELETE, 0x53, 1, "Delete"},   {VK_DIVIDE, 0x35, 1, "Num /"},
    {VK_NUMLOCK, 0x45, 1, "Num Lock"}, {VK_LWIN, 0x5B, 1, "Left Windows"},
    {VK_RWIN, 0x5C, 1, "Right Windows"}, {VK_APPS, 0x5D, 1, "Application"},
    {VK_LSHIFT, 0x2A, 0, "Shift"},    {VK_LCONTROL, 0x1D, 0, "Ctrl"},
    {VK_RCONTROL, 0x1D, 1, "Right Ctrl"}, {VK_LMENU, 0x38, 0, "Alt"},
    {VK_RMENU, 0x38, 1, "Right Alt"},
};

const KeyInfo* KeyByVk(UINT vk) {
  for (const KeyInfo& k : kKeys)
    if (k.vk == vk) return &k;
  return NULL;
}

// The first entry for a scan code is the plain key; ext picks the
// extended twin when there is one.
const KeyInfo* KeyByScan(UINT scan, bool ext) {
  const KeyInfo* plain = NULL;
  for (const KeyInfo& k : kKeys) {
    if (k.scan != scan) continue;
    if ((k.ext != 0) == ext) return &k;
    if (!plain) plain = &k;
  }
  return ext ? NULL : plain;
}
}  // namespace

UINT WINAPI MapVirtualKeyW(UINT code, UINT type) {
  const KeyInfo* k;
  switch (type) {
    case MAPVK_VK_TO_VSC:
    case MAPVK_VK_TO_VSC_EX:
      k = KeyByVk(code);
      if (!k) return 0;
      return type == MAPVK_VK_TO_VSC_EX && k->ext ? 0xE000 | k->scan : k->scan;
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX: {
      bool ext = (code & 0xFF00) == 0xE000;
      k = KeyByScan(code & 0xFF, ext);
      if (!k) return 0;
      UINT vk = k->vk;
      if (type == MAPVK_VSC_TO_VK) {
        if (vk == VK_RSHIFT) vk = VK_SHIFT;
      } else if (vk == VK_SHIFT) {
        vk = VK_LSHIFT;
      } else if (vk == VK_CONTROL) {
        vk = ext ? VK_RCONTROL : VK_LCONTROL;
      } else if (vk == VK_MENU) {
        vk = ext ? VK_RMENU : VK_LMENU;
      }
      return vk;
    }
    case MAPVK_VK_TO_CHAR:
      if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) return code;
      if (code >= VK_NUMPAD0 && code <= VK_NUMPAD9) return '0' + (code - VK_NUMPAD0);
      k = KeyByVk(code);
      if (k && k->name[0] && !k->name[1]) return (BYTE)k->name[0];
      if (code == VK_SPACE) return ' ';
      if (code == VK_RETURN) return '\r';
      if (code == VK_TAB) return '\t';
      if (code == VK_BACK) return '\b';
      if (code == VK_ESCAPE) return 0x1B;
      return 0;
  }
  return 0;
}

UINT WINAPI MapVirtualKeyA(UINT code, UINT type) { return MapVirtualKeyW(code, type); }

int WINAPI GetKeyNameTextW(LONG lp, LPWSTR buf, int max) {
  if (!buf || max <= 0) return 0;
  const KeyInfo* k = KeyByScan((lp >> 16) & 0xFF, (lp & (1 << 24)) != 0);
  if (!k) {
    buf[0] = 0;
    return 0;
  }
  int n = 0;
  for (; k->name[n] && n < max - 1; n++) buf[n] = (wchar_t)(BYTE)k->name[n];
  buf[n] = 0;
  return n;
}

int WINAPI GetKeyNameTextA(LONG lp, LPSTR buf, int max) {
  if (!buf || max <= 0) return 0;
  wchar_t w[64];
  int n = GetKeyNameTextW(lp, w, 64);
  if (n >= max) n = max - 1;
  for (int i = 0; i < n; i++) buf[i] = (char)w[i];
  buf[n] = 0;
  return n;
}

// The mouse and the cursor

BOOL WINAPI GetCursorPos(LPPOINT p) {
  if (!p) return FALSE;
  *p = CursorPos();
  return TRUE;
}

BOOL WINAPI SetCursorPos(int x, int y) {
  CursorPos().x = x;
  CursorPos().y = y;
  return TRUE;
}

HCURSOR WINAPI SetCursor(HCURSOR c) {
  HCURSOR old = g_cursor;
  g_cursor = c;
  return old;
}
HCURSOR WINAPI GetCursor(void) { return g_cursor; }
int WINAPI ShowCursor(BOOL show) { return show ? ++g_cursorShown : --g_cursorShown; }

BOOL WINAPI ClipCursor(const RECT* r) {
  g_clipCursor = r ? *r : MakeRect(0, 0, 0, 0);
  return TRUE;
}
BOOL WINAPI GetClipCursor(LPRECT r) {
  if (!r) return FALSE;
  if (RectEmpty(g_clipCursor)) {
    SIZE s = GetBackend()->ScreenSize();
    *r = MakeRect(0, 0, s.cx, s.cy);
  } else {
    *r = g_clipCursor;
  }
  return TRUE;
}

UINT WINAPI GetDoubleClickTime(void) { return g_doubleClickTime; }
BOOL WINAPI SetDoubleClickTime(UINT ms) {
  g_doubleClickTime = ms ? ms : 500;
  return TRUE;
}
BOOL WINAPI SwapMouseButton(BOOL) { return FALSE; }

BOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT t) {
  if (!t || t->cbSize < sizeof(TRACKMOUSEEVENT)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (t->dwFlags & TME_QUERY) {
    t->hwndTrack = g_tracking.hwnd;
    t->dwFlags = g_tracking.hwnd ? g_tracking.flags : 0;
    t->dwHoverTime = g_tracking.hoverTime;
    return TRUE;
  }
  if (t->dwFlags & TME_CANCEL) {
    if (g_tracking.hwnd == t->hwndTrack) g_tracking.hwnd = NULL;
    return TRUE;
  }
  if (!GetWindowOrFail(t->hwndTrack)) return FALSE;
  // A window the mouse has already left hears so at once.
  if ((t->dwFlags & TME_LEAVE) && g_mouseOver != t->hwndTrack) {
    PostMessageW(t->hwndTrack, WM_MOUSELEAVE, 0, 0);
    return TRUE;
  }
  g_tracking = Tracking{t->hwndTrack, t->dwFlags, t->dwHoverTime, GetTickCount(), false};
  return TRUE;
}

BOOL WINAPI DragDetect(HWND, POINT) { return FALSE; }

}  // extern "C"
