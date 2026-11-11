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
