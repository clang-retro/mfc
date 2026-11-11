// backend_x11.cpp - showing top-level windows on an X11 display.
//
// Each top-level window becomes an X window without the window
// manager's frame, since the window draws its own caption and borders,
// and its surface is copied to the X window as it changes. Menus, list
// drop-downs and tooltips go around the window manager. libX11 is loaded
// when the backend starts, so programs run without it when there is no
// display.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(__unix__) && !defined(__APPLE__) && __has_include(<X11/Xlib.h>)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <mutex>

namespace winport {

namespace {

#define X11_FUNCTIONS(F)                                                                                        \
  F(XInitThreads) F(XOpenDisplay) F(XCloseDisplay) F(XInternAtom) F(XCreateWindow) F(XDestroyWindow)           \
  F(XMapRaised) F(XUnmapWindow) F(XMoveResizeWindow) F(XChangeProperty) F(XSetWMProtocols) F(XCreateGC)        \
  F(XFreeGC) F(XPutImage) F(XInitImage) F(XFlush) F(XPending) F(XNextEvent) F(XLookupString) F(XLookupKeysym) \
  F(XSetTransientForHint) F(XSetWMNormalHints) F(XSetWMHints) F(XStoreName) F(XDefineCursor)                  \
  F(XCreateFontCursor) F(XRaiseWindow) F(XSetInputFocus) F(XWarpPointer) F(XGetWindowAttributes)               \
  F(XTranslateCoordinates) F(XSync)

struct XLib {
#define F(name) decltype(&::name) name = nullptr;
  X11_FUNCTIONS(F)
#undef F
  bool Load() {
    void* lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
#define F(name)                                         \
  name = (decltype(&::name))dlsym(lib, #name);          \
  if (!name) return false;
    X11_FUNCTIONS(F)
#undef F
    return true;
  }
};

// The keys that are not text, by keysym.
struct KeyMap {
  KeySym sym;
  BYTE vk;
  bool ext;
};
const KeyMap kKeys[] = {
    {XK_BackSpace, VK_BACK, false},     {XK_Tab, VK_TAB, false},          {XK_ISO_Left_Tab, VK_TAB, false},
    {XK_Return, VK_RETURN, false},      {XK_Escape, VK_ESCAPE, false},    {XK_space, VK_SPACE, false},
    {XK_Pause, VK_PAUSE, false},        {XK_Print, VK_SNAPSHOT, true},    {XK_Insert, VK_INSERT, true},
    {XK_Delete, VK_DELETE, true},       {XK_Home, VK_HOME, true},         {XK_End, VK_END, true},
    {XK_Prior, VK_PRIOR, true},         {XK_Next, VK_NEXT, true},         {XK_Left, VK_LEFT, true},
    {XK_Right, VK_RIGHT, true},         {XK_Up, VK_UP, true},             {XK_Down, VK_DOWN, true},
    {XK_Shift_L, VK_LSHIFT, false},     {XK_Shift_R, VK_RSHIFT, false},   {XK_Control_L, VK_LCONTROL, false},
    {XK_Control_R, VK_RCONTROL, true},  {XK_Alt_L, VK_LMENU, false},      {XK_Alt_R, VK_RMENU, true},
    {XK_Meta_L, VK_LMENU, false},       {XK_Meta_R, VK_RMENU, true},      {XK_ISO_Level3_Shift, VK_RMENU, true},
    {XK_Super_L, VK_LWIN, true},        {XK_Super_R, VK_RWIN, true},      {XK_Menu, VK_APPS, true},
    {XK_Caps_Lock, VK_CAPITAL, false},  {XK_Num_Lock, VK_NUMLOCK, true},  {XK_Scroll_Lock, VK_SCROLL, false},
    {XK_KP_0, VK_NUMPAD0, false},       {XK_KP_1, VK_NUMPAD1, false},     {XK_KP_2, VK_NUMPAD2, false},
    {XK_KP_3, VK_NUMPAD3, false},       {XK_KP_4, VK_NUMPAD4, false},     {XK_KP_5, VK_NUMPAD5, false},
    {XK_KP_6, VK_NUMPAD6, false},       {XK_KP_7, VK_NUMPAD7, false},     {XK_KP_8, VK_NUMPAD8, false},
    {XK_KP_9, VK_NUMPAD9, false},       {XK_KP_Insert, VK_INSERT, false}, {XK_KP_End, VK_END, false},
    {XK_KP_Down, VK_DOWN, false},       {XK_KP_Next, VK_NEXT, false},     {XK_KP_Left, VK_LEFT, false},
    {XK_KP_Begin, VK_CLEAR, false},     {XK_KP_Right, VK_RIGHT, false},   {XK_KP_Home, VK_HOME, false},
    {XK_KP_Up, VK_UP, false},           {XK_KP_Prior, VK_PRIOR, false},   {XK_KP_Delete, VK_DELETE, false},
    {XK_KP_Decimal, VK_DECIMAL, false}, {XK_KP_Add, VK_ADD, false},       {XK_KP_Subtract, VK_SUBTRACT, false},
    {XK_KP_Multiply, VK_MULTIPLY, false}, {XK_KP_Divide, VK_DIVIDE, true}, {XK_KP_Enter, VK_RETURN, true},
    {XK_minus, VK_OEM_MINUS, false},    {XK_equal, VK_OEM_PLUS, false},   {XK_bracketleft, VK_OEM_4, false},
    {XK_bracketright, VK_OEM_6, false}, {XK_semicolon, VK_OEM_1, false},  {XK_apostrophe, VK_OEM_7, false},
    {XK_grave, VK_OEM_3, false},        {XK_backslash, VK_OEM_5, false},  {XK_comma, VK_OEM_COMMA, false},
    {XK_period, VK_OEM_PERIOD, false},  {XK_slash, VK_OEM_2, false},      {XK_less, VK_OEM_102, false},
};

UINT VkOf(KeySym sym, bool* ext) {
  *ext = false;
  if (sym >= XK_a && sym <= XK_z) return 'A' + (UINT)(sym - XK_a);
  if (sym >= XK_A && sym <= XK_Z) return 'A' + (UINT)(sym - XK_A);
  if (sym >= XK_0 && sym <= XK_9) return '0' + (UINT)(sym - XK_0);
  if (sym >= XK_F1 && sym <= XK_F24) return VK_F1 + (UINT)(sym - XK_F1);
  for (const KeyMap& k : kKeys)
    if (k.sym == sym) {
      *ext = k.ext;
      return k.vk;
    }
  return 0;
}

// The text a keysym types, for the keysyms that carry one.
wchar_t CharOf(KeySym sym, const char* latin1, int n) {
  if ((sym & 0xFF000000) == 0x01000000) return (wchar_t)(sym & 0x00FFFFFF);
  if (n == 1) return (wchar_t)(BYTE)latin1[0];
  if (sym >= 0x20 && sym < 0x100) return (wchar_t)sym;
  return 0;
}

struct Native {
  ::Window xid = 0;
  Window* w = NULL;
  bool mapped = false;
  bool managed = true;  // by the window manager, rather than override-redirect
  int width = 0, height = 0;
};

struct X11Backend : Backend {
  XLib x;
  Display* dpy = NULL;
  int screen = 0;
  ::Window root = 0;
  Visual* visual = NULL;
  int depth = 24;
  GC gc = NULL;
  int wakePipe[2] = {-1, -1};
  Atom wmDelete = 0, wmProtocols = 0, motifHints = 0, netName = 0, utf8 = 0, netType = 0, typeNormal = 0,
       typeDialog = 0, typeMenu = 0, typeTooltip = 0, typeCombo = 0;
  std::map<::Window, Native*> byXid;
  std::recursive_mutex lock;  // the X connection and the map
  Cursor arrow = 0;
  bool moving = false;  // a ConfigureNotify is moving one of ours
  DWORD lastPress = 0;

  bool Start() {
    if (!x.Load()) return false;
    x.XInitThreads();
    dpy = x.XOpenDisplay(NULL);
    if (!dpy) return false;
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    depth = DefaultDepth(dpy, screen);
    if (depth < 24) {
      fprintf(stderr, "winport: the X display needs 24-bit color\n");
      x.XCloseDisplay(dpy);
      dpy = NULL;
      return false;
    }
    gc = x.XCreateGC(dpy, root, 0, NULL);
    if (pipe(wakePipe) == 0) {
      fcntl(wakePipe[0], F_SETFL, O_NONBLOCK);
      fcntl(wakePipe[1], F_SETFL, O_NONBLOCK);
    }
    wmDelete = x.XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmProtocols = x.XInternAtom(dpy, "WM_PROTOCOLS", False);
    motifHints = x.XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    netName = x.XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8 = x.XInternAtom(dpy, "UTF8_STRING", False);
    netType = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    typeNormal = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    typeDialog = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    typeMenu = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    typeTooltip = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    typeCombo = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
    arrow = x.XCreateFontCursor(dpy, XC_left_ptr);
    return true;
  }

  const char* Name() override { return "x11"; }

  SIZE ScreenSize() override {
    if (!dpy) return SIZE{1024, 768};
    return SIZE{DisplayWidth(dpy, screen), DisplayHeight(dpy, screen)};
  }

  Native* NativeOf(Window* w) { return (Native*)w->native; }

  // Windows with a caption, or owned by nothing, are the window manager's;
  // captionless popups such as menus and drop-downs are placed directly.
  bool Managed(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return true;
    if (w->exStyle & WS_EX_APPWINDOW) return true;
    if (!(w->style & WS_POPUP)) return true;
    return false;
  }

  Atom TypeOf(Window* w) {
    if (w->cls && w->cls->name == L"#32768") return typeMenu;
    if (w->cls && (w->cls->name == L"ComboLBox")) return typeCombo;
    if (w->cls && w->cls->name == L"tooltips_class32") return typeTooltip;
    if (w->owner) return typeDialog;
    return typeNormal;
  }

  void SetTitle(Native* n) {
    std::string t = Narrow(n->w->text.c_str());
    std::wstring wt = n->w->text;
    std::string u8;
    for (wchar_t c : wt) {
      unsigned v = (unsigned)c;
      if (v < 0x80) {
        u8 += (char)v;
      } else if (v < 0x800) {
        u8 += (char)(0xC0 | (v >> 6));
        u8 += (char)(0x80 | (v & 0x3F));
      } else if (v < 0x10000) {
        u8 += (char)(0xE0 | (v >> 12));
        u8 += (char)(0x80 | ((v >> 6) & 0x3F));
        u8 += (char)(0x80 | (v & 0x3F));
      } else {
        u8 += (char)(0xF0 | (v >> 18));
        u8 += (char)(0x80 | ((v >> 12) & 0x3F));
        u8 += (char)(0x80 | ((v >> 6) & 0x3F));
        u8 += (char)(0x80 | (v & 0x3F));
      }
    }
    x.XStoreName(dpy, n->xid, t.c_str());
    x.XChangeProperty(dpy, n->xid, netName, utf8, 8, PropModeReplace, (const unsigned char*)u8.data(),
                      (int)u8.size());
  }

  void SetSizeHints(Native* n) {
    XSizeHints hints = {};
    Window* w = n->w;
    hints.flags = PPosition | USPosition | PSize;
    hints.x = w->rect.left;
    hints.y = w->rect.top;
    hints.width = n->width;
    hints.height = n->height;
    if (!(w->style & WS_THICKFRAME)) {
      hints.flags |= PMinSize | PMaxSize;
      hints.min_width = hints.max_width = n->width;
      hints.min_height = hints.max_height = n->height;
    }
    x.XSetWMNormalHints(dpy, n->xid, &hints);
  }

  Native* Create(Window* w) {
    Native* n = new Native;
    n->w = w;
    n->managed = Managed(w);
    n->width = std::max(1, (int)(w->rect.right - w->rect.left));
    n->height = std::max(1, (int)(w->rect.bottom - w->rect.top));
    XSetWindowAttributes a = {};
    a.background_pixel = 0;
    a.override_redirect = n->managed ? False : True;
    a.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | StructureNotifyMask | FocusChangeMask | EnterWindowMask | LeaveWindowMask;
    a.cursor = arrow;
    n->xid = x.XCreateWindow(dpy, root, w->rect.left, w->rect.top, n->width, n->height, 0, CopyFromParent,
                             InputOutput, CopyFromParent, CWBackPixel | CWOverrideRedirect | CWEventMask | CWCursor,
                             &a);
    if (n->managed) {
      // No frame from the window manager: the window draws its own.
      long motif[5] = {2 /* decorations */, 0, 0, 0, 0};
      x.XChangeProperty(dpy, n->xid, motifHints, motifHints, 32, PropModeReplace, (const unsigned char*)motif, 5);
      x.XSetWMProtocols(dpy, n->xid, &wmDelete, 1);
      XWMHints wmh = {};
      wmh.flags = InputHint | StateHint;
      wmh.input = True;
      wmh.initial_state = NormalState;
      x.XSetWMHints(dpy, n->xid, &wmh);
      SetSizeHints(n);
      if (w->owner) {
        Window* o = w->owner->TopLevel();
        if (o->native) x.XSetTransientForHint(dpy, n->xid, NativeOf(o)->xid);
      }
    }
    Atom type = TypeOf(w);
    x.XChangeProperty(dpy, n->xid, netType, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&type, 1);
    SetTitle(n);
    byXid[n->xid] = n;
    w->native = n;
    return n;
  }

  void Show(Window* w, bool show) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    Native* n = NativeOf(w);
    if (!n) {
      if (!show) return;
      n = Create(w);
    }
    if (show) {
      x.XMapRaised(dpy, n->xid);
      n->mapped = true;
      if (n->managed && !(w->style & WS_DISABLED)) x.XSetInputFocus(dpy, n->xid, RevertToParent, CurrentTime);
    } else {
      x.XUnmapWindow(dpy, n->xid);
      n->mapped = false;
    }
    x.XFlush(dpy);
  }

  void Destroy(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n) return;
    byXid.erase(n->xid);
    if (dpy) {
      x.XDestroyWindow(dpy, n->xid);
      x.XFlush(dpy);
    }
    w->native = NULL;
    delete n;
  }

  void Moved(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy || moving) return;
    n->width = std::max(1, (int)(w->rect.right - w->rect.left));
    n->height = std::max(1, (int)(w->rect.bottom - w->rect.top));
    if (n->managed) SetSizeHints(n);
    x.XMoveResizeWindow(dpy, n->xid, w->rect.left, w->rect.top, n->width, n->height);
    x.XFlush(dpy);
  }

  void TitleChanged(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy) return;
    SetTitle(n);
    x.XFlush(dpy);
  }

  void Put(Native* n, RECT r) {
    Surface* s = n->w->surface;
    if (!s || s->width <= 0 || s->height <= 0) return;
    RECT all = {0, 0, s->width, s->height};
    if (!IntersectRect(&r, &r, &all)) return;
    XImage img = {};
    img.width = s->width;
    img.height = s->height;
    img.format = ZPixmap;
    img.data = (char*)s->pixels.data();
    img.byte_order = LSBFirst;
    img.bitmap_unit = 32;
    img.bitmap_bit_order = LSBFirst;
    img.bitmap_pad = 32;
    img.depth = depth;
    img.bytes_per_line = s->width * 4;
    img.bits_per_pixel = 32;
    img.red_mask = 0xFF0000;
    img.green_mask = 0x00FF00;
    img.blue_mask = 0x0000FF;
    if (!x.XInitImage(&img)) return;
    x.XPutImage(dpy, n->xid, gc, &img, r.left, r.top, r.left, r.top, r.right - r.left, r.bottom - r.top);
  }

  void Present(Window* w, const RECT& r) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy || !n->mapped) return;
    Put(n, r);
    x.XFlush(dpy);
  }

  // ---------------------------------------------------------------
  // Input

  WPARAM Keys(unsigned state) {
    WPARAM k = 0;
    if (state & Button1Mask) k |= MK_LBUTTON;
    if (state & Button2Mask) k |= MK_MBUTTON;
    if (state & Button3Mask) k |= MK_RBUTTON;
    if (state & ShiftMask) k |= MK_SHIFT;
    if (state & ControlMask) k |= MK_CONTROL;
    return k;
  }

  // Where the pointer is in the window's surface. The window may not be
  // where we put it, so this goes by the root position.
  POINT At(Native* n, int rootX, int rootY) {
    return POINT{rootX - n->w->rect.left, rootY - n->w->rect.top};
  }

  void Handle(XEvent& e) {
    Native* n;
    {
      std::lock_guard<std::recursive_mutex> g(lock);
      auto it = byXid.find(e.xany.window);
      if (it == byXid.end()) return;
      n = it->second;
    }
    Window* w = n->w;
    switch (e.type) {
      case Expose: {
        std::lock_guard<std::recursive_mutex> g(lock);
        RECT r = {e.xexpose.x, e.xexpose.y, e.xexpose.x + e.xexpose.width, e.xexpose.y + e.xexpose.height};
        UserGuard ug(UserLock());
        if (GetWindow(w->hwnd)) Put(n, r);
        if (e.xexpose.count == 0) x.XFlush(dpy);
        break;
      }
      case MapNotify: n->mapped = true; break;
      case UnmapNotify: n->mapped = false; break;
      case ConfigureNotify: {
        int cw = e.xconfigure.width, ch = e.xconfigure.height;
        // The window manager may have placed or sized the window itself.
        int rx = e.xconfigure.x, ry = e.xconfigure.y;
        if (!e.xconfigure.send_event && n->managed) {
          ::Window child;
          {
            std::lock_guard<std::recursive_mutex> g(lock);
            x.XTranslateCoordinates(dpy, n->xid, root, 0, 0, &rx, &ry, &child);
          }
        }
        RECT r = w->rect;
        if (cw != r.right - r.left || ch != r.bottom - r.top) {
          n->width = cw;
          n->height = ch;
          ResizeRequested(w, cw, ch);
        }
        if (n->managed && (rx != w->rect.left || ry != w->rect.top)) {
          moving = true;
          SetWindowPos(w->hwnd, NULL, rx, ry, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
          moving = false;
        }
        break;
      }
      case ClientMessage:
        if (e.xclient.message_type == wmProtocols && (Atom)e.xclient.data.l[0] == wmDelete) CloseRequested(w);
        break;
      case FocusIn:
        if (n->managed && e.xfocus.mode == NotifyNormal && GetActiveWindow() != w->hwnd && IsWindowEnabled(w->hwnd))
          SetActiveWindow(w->hwnd);
        break;
      case ButtonPress:
      case ButtonRelease: {
        POINT p = At(n, e.xbutton.x_root, e.xbutton.y_root);
        WPARAM keys = Keys(e.xbutton.state);
        bool down = e.type == ButtonPress;
        UINT msg = 0;
        switch (e.xbutton.button) {
          case Button1: msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; break;
          case Button2: msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; break;
          case Button3: msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; break;
          case Button4:
          case Button5:
            if (down) MouseInput(w, WM_MOUSEWHEEL, p.x, p.y, MAKEWPARAM(keys, e.xbutton.button == Button4 ? WHEEL_DELTA : -WHEEL_DELTA));
            return;
          case 6:
          case 7:
            if (down) MouseInput(w, WM_MOUSEHWHEEL, p.x, p.y, MAKEWPARAM(keys, e.xbutton.button == 7 ? WHEEL_DELTA : -WHEEL_DELTA));
            return;
          default: return;
        }
        MouseInput(w, msg, p.x, p.y, keys);
        break;
      }
      case MotionNotify: {
        POINT p = At(n, e.xmotion.x_root, e.xmotion.y_root);
        MouseInput(w, WM_MOUSEMOVE, p.x, p.y, Keys(e.xmotion.state));
        break;
      }
      case KeyPress:
      case KeyRelease: {
        char buf[16];
        KeySym sym = 0;
        int len;
        {
          std::lock_guard<std::recursive_mutex> g(lock);
          len = x.XLookupString(&e.xkey, buf, sizeof buf, &sym, NULL);
        }
        KeySym base = x.XLookupKeysym(&e.xkey, 0);
        bool ext;
        UINT vk = VkOf(base, &ext);
        if (!vk) vk = VkOf(sym, &ext);
        UINT scan = e.xkey.keycode >= 8 ? e.xkey.keycode - 8 : 0;
        LPARAM lp = 1 | ((LPARAM)(scan & 0xFF) << 16) | (ext ? (1 << 24) : 0);
        bool down = e.type == KeyPress;
        wchar_t ch = 0;
        if (down) {
          ch = CharOf(sym, buf, len);
          if (sym == XK_Return || sym == XK_KP_Enter) ch = L'\r';
          if (sym == XK_BackSpace) ch = L'\b';
          if (sym == XK_Tab || sym == XK_ISO_Left_Tab) ch = L'\t';
          if (sym == XK_Escape) ch = 0x1B;
        }
        // The key's text comes from X, with its layout, rather than from
        // TranslateMessage (bit 25 tells it so).
        if (ch) lp |= 0x02000000;
        if (vk) KeyInput(w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
        if (ch) CharInput(w, ch);
        break;
      }
    }
  }

  void Drain() {
    for (;;) {
      XEvent e;
      {
        std::lock_guard<std::recursive_mutex> g(lock);
        if (!dpy || !x.XPending(dpy)) return;
        x.XNextEvent(dpy, &e);
      }
      Handle(e);
    }
  }

  void Wait(DWORD timeout) override {
    if (!dpy) return;
    Drain();
    pollfd fds[2];
    fds[0].fd = ConnectionNumber(dpy);
    fds[0].events = POLLIN;
    fds[1].fd = wakePipe[0];
    fds[1].events = POLLIN;
    int ms = timeout == INFINITE ? -1 : (int)std::min<DWORD>(timeout, 0x7FFFFFFF);
    {
      std::lock_guard<std::recursive_mutex> g(lock);
      x.XFlush(dpy);
    }
    int r = poll(fds, 2, ms);
    if (r > 0 && (fds[1].revents & POLLIN)) {
      char b[64];
      while (read(wakePipe[0], b, sizeof b) > 0) {
      }
    }
    Drain();
  }

  void Wake() override {
    if (wakePipe[1] >= 0) {
      char c = 1;
      ssize_t ignored = write(wakePipe[1], &c, 1);
      (void)ignored;
    }
  }
};

}  // namespace

Backend* NewX11Backend() {
  X11Backend* b = new X11Backend;
  if (b->Start()) return b;
  delete b;
  return NULL;
}

}  // namespace winport

#else

namespace winport {
Backend* NewX11Backend() { return NULL; }
}  // namespace winport

#endif
