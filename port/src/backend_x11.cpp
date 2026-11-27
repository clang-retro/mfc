// backend_x11.cpp - showing top-level windows on an X11 display.
//
// Each top-level window becomes an X window without the window
// manager's frame, since the window draws its own caption and borders,
// and its surface is copied to the X window as it changes. Menus, list
// drop-downs and tooltips go around the window manager. libX11 is loaded
// when the backend starts, so programs run without it when there is no
// display.
//
// A translucent window (layered, or with a window region) gets a 32-bit
// visual with premultiplied alpha when a compositing manager runs, and
// otherwise a bounding shape of its visible pixels; either way its input
// shape lets the mouse through where it is fully transparent. Shapes take
// libXext's SHAPE extension, loaded when there is one.
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
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace winport {

namespace {

#define X11_FUNCTIONS(F)                                                                                        \
  F(XInitThreads) F(XOpenDisplay) F(XCloseDisplay) F(XInternAtom) F(XCreateWindow) F(XDestroyWindow)           \
  F(XMapRaised) F(XUnmapWindow) F(XMoveResizeWindow) F(XChangeProperty) F(XSetWMProtocols) F(XCreateGC)        \
  F(XFreeGC) F(XPutImage) F(XInitImage) F(XFlush) F(XPending) F(XNextEvent) F(XLookupString) F(XLookupKeysym) \
  F(XSetTransientForHint) F(XSetWMNormalHints) F(XSetWMHints) F(XStoreName) F(XDefineCursor)                  \
  F(XCreateFontCursor) F(XRaiseWindow) F(XSetInputFocus) F(XWarpPointer) F(XGetWindowAttributes)               \
  F(XTranslateCoordinates) F(XSync) F(XFreeCursor) F(XSetSelectionOwner) F(XGetSelectionOwner)          \
  F(XConvertSelection) F(XGetWindowProperty) F(XFree) F(XDeleteProperty) F(XSendEvent) F(XCheckTypedWindowEvent) F(XCheckTypedEvent) \
  F(XMatchVisualInfo) F(XCreateColormap) F(XResourceManagerString) F(XQueryPointer) F(XChangeActivePointerGrab)

// The input method (XIM), when libX11 has it.
#define XIM_FUNCTIONS(F)                                                                                 \
  F(XOpenIM) F(XCloseIM) F(XGetIMValues) F(XSetIMValues) F(XCreateIC) F(XDestroyIC) F(XSetICFocus) F(XUnsetICFocus)    \
  F(XFilterEvent) F(Xutf8LookupString) F(Xutf8ResetIC) F(XSetICValues) F(XGetICValues)               \
  F(XVaCreateNestedList) F(XSetLocaleModifiers) F(XSupportsLocale)

struct XLib {
#define F(name) decltype(&::name) name = nullptr;
  X11_FUNCTIONS(F)
  XIM_FUNCTIONS(F)
#undef F
  bool xim = false;
  bool Load() {
    void* lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
#define F(name)                                         \
  name = (decltype(&::name))dlsym(lib, #name);          \
  if (!name) return false;
    X11_FUNCTIONS(F)
#undef F
    xim = true;
#define F(name)                                \
  name = (decltype(&::name))dlsym(lib, #name); \
  if (!name) xim = false;
    XIM_FUNCTIONS(F)
#undef F
    return true;
  }
};

// libXcursor, for cursors in color; without it they are black and white
// font cursors. Its header may be missing, so the image is declared here
// as the library defines it.
struct XcursorImageRec {
  unsigned version, size, width, height, xhot, yhot, delay;
  unsigned* pixels;  // ARGB, not premultiplied
};
struct XcursorLib {
  XcursorImageRec* (*XcursorImageCreate)(int width, int height) = nullptr;
  void (*XcursorImageDestroy)(XcursorImageRec* image) = nullptr;
  Cursor (*XcursorImageLoadCursor)(Display* dpy, const XcursorImageRec* image) = nullptr;
  bool Load() {
    void* lib = dlopen("libXcursor.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    XcursorImageCreate = (decltype(XcursorImageCreate))dlsym(lib, "XcursorImageCreate");
    XcursorImageDestroy = (decltype(XcursorImageDestroy))dlsym(lib, "XcursorImageDestroy");
    XcursorImageLoadCursor = (decltype(XcursorImageLoadCursor))dlsym(lib, "XcursorImageLoadCursor");
    return XcursorImageCreate && XcursorImageDestroy && XcursorImageLoadCursor;
  }
};

// libXfixes, which says when another program takes the clipboard.
struct XfixesLib {
  Bool (*XFixesQueryExtension)(Display* dpy, int* eventBase, int* errorBase) = nullptr;
  void (*XFixesSelectSelectionInput)(Display* dpy, ::Window w, Atom selection, unsigned long mask) = nullptr;
  bool Load() {
    void* lib = dlopen("libXfixes.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    XFixesQueryExtension = (decltype(XFixesQueryExtension))dlsym(lib, "XFixesQueryExtension");
    XFixesSelectSelectionInput = (decltype(XFixesSelectSelectionInput))dlsym(lib, "XFixesSelectSelectionInput");
    return XFixesQueryExtension && XFixesSelectSelectionInput;
  }
};

// libXext's SHAPE extension, for windows that are not rectangles. Its
// header may be missing, so the functions are declared here as the
// library defines them.
struct XextLib {
  enum { kBounding = 0, kInput = 2, kSet = 0, kUnsorted = 0 };
  Bool (*XShapeQueryExtension)(Display* dpy, int* eventBase, int* errorBase) = nullptr;
  void (*XShapeCombineRectangles)(Display* dpy, ::Window w, int kind, int x, int y, XRectangle* rects, int n,
                                  int op, int ordering) = nullptr;
  void (*XShapeCombineMask)(Display* dpy, ::Window w, int kind, int x, int y, Pixmap mask, int op) = nullptr;
  bool Load() {
    void* lib = dlopen("libXext.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    XShapeQueryExtension = (decltype(XShapeQueryExtension))dlsym(lib, "XShapeQueryExtension");
    XShapeCombineRectangles = (decltype(XShapeCombineRectangles))dlsym(lib, "XShapeCombineRectangles");
    XShapeCombineMask = (decltype(XShapeCombineMask))dlsym(lib, "XShapeCombineMask");
    return XShapeQueryExtension && XShapeCombineRectangles && XShapeCombineMask;
  }
};

// The system cursors' nearest X font cursors.
unsigned FontCursorFor(int id) {
  switch (id) {
    case 32513: return XC_xterm;               // IDC_IBEAM
    case 32514: return XC_watch;               // IDC_WAIT
    case 32515: return XC_crosshair;           // IDC_CROSS
    case 32516: return XC_sb_up_arrow;         // IDC_UPARROW
    case 32640: case 32646: return XC_fleur;   // IDC_SIZE, IDC_SIZEALL
    case 32642: return XC_bottom_right_corner; // IDC_SIZENWSE
    case 32643: return XC_bottom_left_corner;  // IDC_SIZENESW
    case 32644: return XC_sb_h_double_arrow;   // IDC_SIZEWE
    case 32645: return XC_sb_v_double_arrow;   // IDC_SIZENS
    case 32648: return XC_circle;              // IDC_NO
    case 32649: return XC_hand2;               // IDC_HAND
    case 32650: return XC_watch;               // IDC_APPSTARTING
    case 32651: return XC_question_arrow;      // IDC_HELP
  }
  return XC_left_ptr;
}

// The keys that are not text, by keysym.
struct KeyMap {
  KeySym sym;
  BYTE vk;
  bool ext;
};
const KeyMap kKeys[] = {
#define KEY(name, vk, ext) {XK_##name, vk, ext},
#include "keysyms.inc"
#undef KEY
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
  bool argb = false;  // a 32-bit visual with alpha
  // The shapes last given the server, and whether any were.
  bool shaped = false;
  std::vector<RECT> input, bounding;
  // The input method's context for the window, and the text it is
  // composing there when the program shows it (XIMPreeditCallbacks).
  XIC ic = NULL;
  std::wstring preedit;
  int caret = 0;
};

struct X11Backend : Backend {
  XLib x;
  Display* dpy = NULL;
  int screen = 0;
  ::Window root = 0;
  Visual* visual = NULL;
  int depth = 24;
  GC gc = NULL;
  // Translucent windows: the 32-bit visual, its colormap and a GC for it,
  // the compositing manager's selection, and the SHAPE extension.
  Visual* argbVisual = NULL;
  Colormap argbColormap = 0;
  GC argbGc = NULL;
  Atom compositor = 0;
  XextLib xext;
  bool haveShape = false;
  int wakePipe[2] = {-1, -1};
  Atom netIcon = 0;
  Atom wmDelete = 0, wmProtocols = 0, motifHints = 0, netName = 0, utf8 = 0, netType = 0, typeNormal = 0,
       typeDialog = 0, typeMenu = 0, typeTooltip = 0, typeCombo = 0;
  std::map<::Window, Native*> byXid;
  std::recursive_mutex lock;  // the X connection and the map
  XIM im = NULL;
  XIMStyle imStyle = 0;
  Cursor arrow = 0;
  XcursorLib xcursor;
  bool haveXcursor = false;
  Cursor cursor = 0;          // the one the windows show
  bool cursorOwned = false;   // made for a program's cursor, freed when replaced
  std::map<unsigned, Cursor> fontCursors;
  // The clipboard: an unmapped window owns it and is sent its requests.
  ::Window clipWin = 0;
  Atom clipboard = 0, targets = 0, textPlain = 0, clipProp = 0, incr = 0;
  // Drags from other programs (the XDND protocol): the source, what it
  // offers, the window of ours it is over and where.
  Atom xdndAware = 0, xdndEnter = 0, xdndPosition = 0, xdndStatus = 0, xdndDrop = 0, xdndFinished = 0,
       xdndLeave = 0, xdndSelection = 0, xdndTypeList = 0, xdndActionCopy = 0, xdndActionMove = 0,
       xdndActionLink = 0, uriList = 0, textPlainBare = 0, dropProp = 0;
  ::Window dndSource = 0;
  DesktopOfferPtr dndOffer;
  Native* dndOver = NULL;
  POINT dndAt = {0, 0};
  DWORD dndAllowed = 0;
  // Drags to other programs: the window under the drag, its XDND
  // version, and its last answer; what the drag carries, in the types
  // offered.
  ::Window outTarget = 0;
  int outVersion = 0;
  bool outActive = false, outWaiting = false, outAccept = false, outFinished = false;
  Atom outAction = 0;
  DWORD outSent = 0;
  std::vector<Atom> outTypes;
  std::string outUris, outText;  // UTF-8
  std::string clipText;  // UTF-8, what this program offers
  int fixesEvent = -1;   // XFixesSelectionNotify, when the server has XFixes
  DWORD clipGeneration = 0;
  bool notified = false;  // a SelectionNotify came through the event loop
  XSelectionEvent notifyEvent = {};
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
    XVisualInfo vi;
    if (x.XMatchVisualInfo(dpy, screen, 32, TrueColor, &vi)) {
      argbVisual = vi.visual;
      argbColormap = x.XCreateColormap(dpy, root, argbVisual, AllocNone);
    }
    char cm[32];
    snprintf(cm, sizeof cm, "_NET_WM_CM_S%d", screen);
    compositor = x.XInternAtom(dpy, cm, False);
    int shapeEvent, shapeError;
    haveShape = xext.Load() && xext.XShapeQueryExtension(dpy, &shapeEvent, &shapeError);
    if (pipe(wakePipe) == 0) {
      fcntl(wakePipe[0], F_SETFL, O_NONBLOCK);
      fcntl(wakePipe[1], F_SETFL, O_NONBLOCK);
    }
    wmDelete = x.XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmProtocols = x.XInternAtom(dpy, "WM_PROTOCOLS", False);
    motifHints = x.XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    netName = x.XInternAtom(dpy, "_NET_WM_NAME", False);
    netIcon = x.XInternAtom(dpy, "_NET_WM_ICON", False);
    utf8 = x.XInternAtom(dpy, "UTF8_STRING", False);
    netType = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    typeNormal = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    typeDialog = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    typeMenu = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    typeTooltip = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    typeCombo = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
    arrow = x.XCreateFontCursor(dpy, XC_left_ptr);
    cursor = arrow;
    haveXcursor = xcursor.Load();
    clipboard = x.XInternAtom(dpy, "CLIPBOARD", False);
    targets = x.XInternAtom(dpy, "TARGETS", False);
    textPlain = x.XInternAtom(dpy, "text/plain;charset=utf-8", False);
    clipProp = x.XInternAtom(dpy, "WINPORT_CLIPBOARD", False);
    incr = x.XInternAtom(dpy, "INCR", False);
    xdndAware = x.XInternAtom(dpy, "XdndAware", False);
    xdndEnter = x.XInternAtom(dpy, "XdndEnter", False);
    xdndPosition = x.XInternAtom(dpy, "XdndPosition", False);
    xdndStatus = x.XInternAtom(dpy, "XdndStatus", False);
    xdndDrop = x.XInternAtom(dpy, "XdndDrop", False);
    xdndFinished = x.XInternAtom(dpy, "XdndFinished", False);
    xdndLeave = x.XInternAtom(dpy, "XdndLeave", False);
    xdndSelection = x.XInternAtom(dpy, "XdndSelection", False);
    xdndTypeList = x.XInternAtom(dpy, "XdndTypeList", False);
    xdndActionCopy = x.XInternAtom(dpy, "XdndActionCopy", False);
    xdndActionMove = x.XInternAtom(dpy, "XdndActionMove", False);
    xdndActionLink = x.XInternAtom(dpy, "XdndActionLink", False);
    textPlainBare = x.XInternAtom(dpy, "text/plain", False);
    uriList = x.XInternAtom(dpy, "text/uri-list", False);
    dropProp = x.XInternAtom(dpy, "WINPORT_DROP", False);
    OpenInputMethod();
    XSetWindowAttributes ca = {};
    clipWin = x.XCreateWindow(dpy, root, -10, -10, 1, 1, 0, 0, InputOnly, CopyFromParent, 0, &ca);
    XfixesLib fixes;
    int errorBase;
    if (fixes.Load() && fixes.XFixesQueryExtension(dpy, &fixesEvent, &errorBase))
      fixes.XFixesSelectSelectionInput(dpy, clipWin, clipboard, 1 /* XFixesSetSelectionOwnerNotifyMask */);
    else
      fixesEvent = -1;
    return true;
  }

  const char* Name() override { return "x11"; }

  // The desktop's scale: its Xft.dpi resource over 96, which desktops set
  // for their scaling, or else GDK_SCALE.
  double DesktopScale() override {
    if (dpy) {
      const char* res = x.XResourceManagerString(dpy);
      for (const char* p = res; p && *p;) {
        const char* end = strchr(p, '\n');
        if (!strncmp(p, "Xft.dpi:", 8)) {
          double dpi = atof(p + 8);
          if (dpi >= 96 && dpi <= 960) return dpi / 96;
          break;
        }
        p = end ? end + 1 : NULL;
      }
    }
    const char* gdk = getenv("GDK_SCALE");
    int k = gdk ? atoi(gdk) : 0;
    return k >= 1 && k <= 10 ? k : 1;
  }

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

  // The window's icons for the taskbar and the window switcher: each one's
  // width, height and ARGB pixels, as CARDINALs (longs to Xlib).
  void SetIcon(Native* n) {
    if (!n->managed) return;
    std::vector<long> data;
    for (const CursorPicture& p : DesktopIcons(n->w)) {
      data.push_back(p.width);
      data.push_back(p.height);
      for (DWORD px : p.argb) data.push_back((long)(unsigned long)px);
    }
    if (data.empty())
      x.XDeleteProperty(dpy, n->xid, netIcon);
    else
      x.XChangeProperty(dpy, n->xid, netIcon, XA_CARDINAL, 32, PropModeReplace, (const unsigned char*)data.data(),
                        (int)data.size());
  }

  void IconChanged(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy) return;
    SetIcon(n);
    x.XFlush(dpy);
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
    a.cursor = cursor;
    unsigned long mask = CWBackPixel | CWOverrideRedirect | CWEventMask | CWCursor;
    n->argb = WantArgb(w);
    if (n->argb) {
      a.colormap = argbColormap;
      a.border_pixel = 0;
      mask |= CWColormap | CWBorderPixel;
      n->xid = x.XCreateWindow(dpy, root, w->rect.left, w->rect.top, n->width, n->height, 0, 32, InputOutput,
                               argbVisual, mask, &a);
      if (!argbGc) argbGc = x.XCreateGC(dpy, n->xid, 0, NULL);
    } else {
      n->xid = x.XCreateWindow(dpy, root, w->rect.left, w->rect.top, n->width, n->height, 0, CopyFromParent,
                               InputOutput, CopyFromParent, mask, &a);
    }
    if (n->managed) {
      // No frame from the window manager: the window draws its own.
      long motif[5] = {2 /* decorations */, 0, 0, 0, 0};
      x.XChangeProperty(dpy, n->xid, motifHints, motifHints, 32, PropModeReplace, (const unsigned char*)motif, 5);
      x.XSetWMProtocols(dpy, n->xid, &wmDelete, 1);
      Atom version = 5;
      x.XChangeProperty(dpy, n->xid, xdndAware, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&version, 1);
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
    SetIcon(n);
    byXid[n->xid] = n;
    w->native = n;
    SetShapes(n);
    if (n->managed) CreateIC(n);
    return n;
  }

  // Whether a window shows with alpha: translucent, with a compositing
  // manager to blend it.
  bool WantArgb(Window* w) {
    return argbVisual && argbColormap && Translucent(w) && x.XGetSelectionOwner(dpy, compositor) != None;
  }

  // The window's shapes: where it takes the mouse, and without alpha,
  // where it shows; the defaults (all of it) for an opaque window.
  void SetShapes(Native* n) {
    if (!haveShape) return;
    Window* w = n->w;
    bool translucent = Translucent(w);
    if (!translucent && !n->shaped) return;
    std::vector<RECT> input, bounding;
    if (translucent) {
      input = MouseArea(w);
      if (!n->argb) bounding = ShownArea(w);
    }
    auto same = [](const std::vector<RECT>& a, const std::vector<RECT>& b) {
      if (a.size() != b.size()) return false;
      for (size_t i = 0; i < a.size(); i++)
        if (!EqualRect(&a[i], &b[i])) return false;
      return true;
    };
    auto apply = [&](int kind, const std::vector<RECT>& area, bool whole) {
      if (whole) {
        xext.XShapeCombineMask(dpy, n->xid, kind, 0, 0, None, XextLib::kSet);
        return;
      }
      std::vector<XRectangle> xr;
      for (const RECT& r : area)
        xr.push_back(XRectangle{(short)r.left, (short)r.top, (unsigned short)(r.right - r.left),
                                (unsigned short)(r.bottom - r.top)});
      xext.XShapeCombineRectangles(dpy, n->xid, kind, 0, 0, xr.data(), (int)xr.size(), XextLib::kSet,
                                   XextLib::kUnsorted);
    };
    if (!n->shaped || !translucent || !same(input, n->input)) apply(XextLib::kInput, input, !translucent);
    bool boundingWhole = !translucent || n->argb;
    if (!n->shaped || boundingWhole != n->bounding.empty() || !same(bounding, n->bounding))
      apply(XextLib::kBounding, bounding, boundingWhole);
    n->input = input;
    n->bounding = bounding;
    n->shaped = translucent;
  }

  void TranslucencyChanged(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy) return;
    if (WantArgb(w) != n->argb) {
      // Another visual takes another X window.
      bool mapped = n->mapped;
      Destroy(w);
      Create(w);
      if (mapped) Show(w, true);
      return;
    }
    SetShapes(n);
    x.XFlush(dpy);
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
    if (n->ic && im) x.XDestroyIC(n->ic);
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
    if (Translucent(n->w)) SetShapes(n);
    if (n->argb) {
      // Premultiplied ARGB, as the compositing manager blends it.
      int w = r.right - r.left, h = r.bottom - r.top;
      std::vector<DWORD> px((size_t)w * h);
      Composite(n->w, r, px.data(), w);
      XImage img = {};
      img.width = w;
      img.height = h;
      img.format = ZPixmap;
      img.data = (char*)px.data();
      img.byte_order = LSBFirst;
      img.bitmap_unit = 32;
      img.bitmap_bit_order = LSBFirst;
      img.bitmap_pad = 32;
      img.depth = 32;
      img.bytes_per_line = w * 4;
      img.bits_per_pixel = 32;
      img.red_mask = 0xFF0000;
      img.green_mask = 0x00FF00;
      img.blue_mask = 0x0000FF;
      if (!x.XInitImage(&img)) return;
      x.XPutImage(dpy, n->xid, argbGc, &img, 0, 0, r.left, r.top, w, h);
      return;
    }
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

  // A cursor in color, or with no font cursor like it, from its picture.
  Cursor MakeCursor(const CursorPicture& p) {
    if (!haveXcursor || p.width <= 0 || p.height <= 0) return 0;
    XcursorImageRec* img = xcursor.XcursorImageCreate(p.width, p.height);
    if (!img) return 0;
    img->xhot = std::min<int>(std::max<int>(p.hotspot.x, 0), p.width - 1);
    img->yhot = std::min<int>(std::max<int>(p.hotspot.y, 0), p.height - 1);
    memcpy(img->pixels, p.argb.data(), p.argb.size() * 4);
    Cursor c = xcursor.XcursorImageLoadCursor(dpy, img);
    xcursor.XcursorImageDestroy(img);
    return c;
  }

  void SetCursor(HCURSOR h) override {
    CursorPicture p;
    bool have = h && CursorImage(h, &p);
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    Cursor c = 0;
    bool owned = false;
    if (!h) {
      // Hidden: a cursor with nothing in it.
      CursorPicture blank;
      blank.width = blank.height = 1;
      blank.argb.assign(1, 0);
      c = MakeCursor(blank);
      owned = c != 0;
    } else if (have && p.systemId == 0) {
      c = MakeCursor(p);
      owned = c != 0;
    }
    if (!c) {
      unsigned shape = FontCursorFor(have ? p.systemId : 0);
      auto it = fontCursors.find(shape);
      if (it == fontCursors.end()) it = fontCursors.emplace(shape, x.XCreateFontCursor(dpy, shape)).first;
      c = it->second;
    }
    for (auto& e : byXid) x.XDefineCursor(dpy, e.first, c);
    // Over another program's window the drag's grab shows it.
    if (outActive)
      x.XChangeActivePointerGrab(dpy, ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask |
                                          LeaveWindowMask, c, CurrentTime);
    if (cursorOwned && cursor != c) x.XFreeCursor(dpy, cursor);
    cursor = c;
    cursorOwned = owned;
    x.XFlush(dpy);
  }

  // ---------------------------------------------------------------
  // The clipboard

  bool ClipboardForeign(DWORD* generation) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return false;
    ::Window owner = x.XGetSelectionOwner(dpy, clipboard);
    if (owner == None || owner == clipWin) return false;
    if (fixesEvent >= 0) {
      XEvent e;
      while (x.XCheckTypedEvent(dpy, fixesEvent, &e)) clipGeneration++;
      *generation = clipGeneration ^ (DWORD)owner;
    } else {
      // Without XFixes there is no telling: look again every two seconds.
      *generation = (DWORD)owner ^ (GetTickCount() / 2000);
    }
    return true;
  }

  // Waits for the owner's answer to a request for the clipboard.
  bool AwaitNotify(XSelectionEvent* out) {
    DWORD start = GetTickCount();
    for (;;) {
      XEvent e;
      if (x.XCheckTypedWindowEvent(dpy, clipWin, SelectionNotify, &e)) {
        *out = e.xselection;
        return true;
      }
      if (notified) {
        notified = false;
        *out = notifyEvent;
        return true;
      }
      if (GetTickCount() - start > 1000) return false;
      pollfd fd = {ConnectionNumber(dpy), POLLIN, 0};
      poll(&fd, 1, 20);
    }
  }

  bool ClipboardFetch(std::wstring* text) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return false;
    for (Atom type : {utf8, (Atom)XA_STRING}) {
      notified = false;
      x.XConvertSelection(dpy, clipboard, type, clipProp, clipWin, CurrentTime);
      x.XFlush(dpy);
      XSelectionEvent ev;
      if (!AwaitNotify(&ev)) return false;
      if (ev.property == None) continue;
      Atom got;
      int format;
      unsigned long n, after;
      unsigned char* data = NULL;
      if (x.XGetWindowProperty(dpy, clipWin, clipProp, 0, 0x1000000, True, AnyPropertyType, &got, &format, &n, &after,
                               &data) != Success)
        return false;
      bool ok = false;
      if (data && got != incr && format == 8) {
        std::string bytes((const char*)data, n);
        if (got == XA_STRING) {
          text->clear();
          for (unsigned char c : bytes) *text += (wchar_t)c;
        } else {
          *text = Widen(bytes.c_str(), (int)bytes.size());
        }
        ok = true;
      }
      if (data) x.XFree(data);
      return ok;
    }
    return false;
  }

  void ClipboardOwn(const std::wstring& text) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    clipText = Narrow(text.c_str(), (int)text.size());
    x.XSetSelectionOwner(dpy, clipboard, clipWin, CurrentTime);
    x.XFlush(dpy);
  }

  // Another program asks for the clipboard.
  void Request(const XSelectionRequestEvent& r) {
    XEvent reply = {};
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = r.display;
    reply.xselection.requestor = r.requestor;
    reply.xselection.selection = r.selection;
    reply.xselection.target = r.target;
    reply.xselection.time = r.time;
    Atom prop = r.property != None ? r.property : r.target;
    reply.xselection.property = prop;
    std::lock_guard<std::recursive_mutex> g(lock);
    // The clipboard's text, or what a drag to another program carries.
    bool drag = r.selection == xdndSelection;
    const std::string& text = drag ? outText : clipText;
    bool hasText = !drag || !outText.empty();
    if (r.target == targets) {
      std::vector<Atom> list = {targets};
      if (drag)
        list.insert(list.end(), outTypes.begin(), outTypes.end());
      else
        list.insert(list.end(), {utf8, textPlain, XA_STRING});
      x.XChangeProperty(dpy, r.requestor, prop, XA_ATOM, 32, PropModeReplace, (const unsigned char*)list.data(),
                        (int)list.size());
    } else if (drag && r.target == uriList && !outUris.empty()) {
      x.XChangeProperty(dpy, r.requestor, prop, r.target, 8, PropModeReplace, (const unsigned char*)outUris.data(),
                        (int)outUris.size());
    } else if (hasText && (r.target == utf8 || r.target == textPlain || (drag && r.target == textPlainBare))) {
      x.XChangeProperty(dpy, r.requestor, prop, r.target, 8, PropModeReplace, (const unsigned char*)text.data(),
                        (int)text.size());
    } else if (hasText && r.target == XA_STRING) {
      std::wstring w = Widen(text.c_str(), (int)text.size());
      std::string latin1;
      for (wchar_t c : w) latin1 += c < 0x100 ? (char)c : '?';
      x.XChangeProperty(dpy, r.requestor, prop, XA_STRING, 8, PropModeReplace, (const unsigned char*)latin1.data(),
                        (int)latin1.size());
    } else {
      reply.xselection.property = None;
    }
    x.XSendEvent(dpy, r.requestor, False, 0, &reply);
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

  // ---------------------------------------------------------------
  // Drags from other programs

  // What a drag from another program offers: its types, fetched from
  // the XdndSelection; the answers go to its source.
  struct Offer : DesktopOffer {
    X11Backend* b = NULL;
    ::Window self = 0, source = 0;
    int version = 0;
    std::vector<Atom> types;
    Time dropTime = CurrentTime;
    bool Has(Atom a) const { return std::find(types.begin(), types.end(), a) != types.end(); }
    bool Fetch(bool wantFiles, std::string* bytes) override {
      Atom type = 0;
      if (wantFiles) {
        type = b->uriList;
      } else {
        for (Atom a : {b->utf8, b->textPlain, b->textPlainBare, (Atom)XA_STRING})
          if (Has(a)) {
            type = a;
            break;
          }
      }
      if (!type || !b->ConvertDrop(type, dropTime, bytes)) return false;
      if (type == XA_STRING) {
        // Latin-1
        std::wstring w;
        for (unsigned char c : *bytes) w += (wchar_t)c;
        *bytes = Narrow(w.c_str(), (int)w.size());
      }
      return true;
    }
    void Answer(DWORD effect) override {
      // Bit 1: send the positions inside the window too.
      b->SendDnd(source, b->xdndStatus, (long)self, (effect ? 1 : 0) | 2, 0, 0,
                 effect ? (long)b->ActionOf(effect) : 0);
    }
    void Finished(DWORD effect) override {
      b->SendDnd(source, b->xdndFinished, (long)self, effect ? 1 : 0, effect ? (long)b->ActionOf(effect) : 0, 0, 0);
    }
  };

  void SendDnd(::Window to, Atom type, long l0, long l1, long l2, long l3, long l4) {
    XEvent r = {};
    r.xclient.type = ClientMessage;
    r.xclient.display = dpy;
    r.xclient.window = to;
    r.xclient.message_type = type;
    r.xclient.format = 32;
    r.xclient.data.l[0] = l0;
    r.xclient.data.l[1] = l1;
    r.xclient.data.l[2] = l2;
    r.xclient.data.l[3] = l3;
    r.xclient.data.l[4] = l4;
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    x.XSendEvent(dpy, to, False, NoEventMask, &r);
    x.XFlush(dpy);
  }

  // The XDND action of a drop effect, and the effect of an action.
  Atom ActionOf(DWORD effect) {
    if (effect & DROPEFFECT_MOVE) return xdndActionMove;
    if (effect & DROPEFFECT_COPY) return xdndActionCopy;
    if (effect & DROPEFFECT_LINK) return xdndActionLink;
    return 0;
  }
  DWORD EffectOf(Atom action) {
    if (action == xdndActionMove) return DROPEFFECT_MOVE;
    if (action == xdndActionLink) return DROPEFFECT_LINK;
    return DROPEFFECT_COPY;  // copy, ask, or one of the source's own
  }

  // The buttons and keys held now, as MK_ flags.
  DWORD PointerKeys() {
    ::Window r, c;
    int rx, ry, wx, wy;
    unsigned mask = 0;
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!x.XQueryPointer(dpy, root, &r, &c, &rx, &ry, &wx, &wy, &mask)) return 0;
    return (DWORD)Keys(mask) | ((mask & Mod1Mask) ? MK_ALT : 0);
  }

  // Fetches the XdndSelection in a type, as the clipboard is fetched.
  bool ConvertDrop(Atom type, Time time, std::string* bytes) {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return false;
    notified = false;
    x.XConvertSelection(dpy, xdndSelection, type, dropProp, clipWin, time);
    x.XFlush(dpy);
    XSelectionEvent ev;
    if (!AwaitNotify(&ev) || ev.property == None) return false;
    Atom got;
    int format;
    unsigned long n, after;
    unsigned char* data = NULL;
    if (x.XGetWindowProperty(dpy, clipWin, dropProp, 0, 0x1000000, True, AnyPropertyType, &got, &format, &n, &after,
                             &data) != Success)
      return false;
    bool ok = data && got != incr && format == 8;
    if (ok) bytes->assign((const char*)data, n);
    if (data) x.XFree(data);
    return ok;
  }

  void DndMessage(Native* n, const XClientMessageEvent& m) {
    if (m.message_type == xdndEnter) {
      if (dndOffer && dndOver) DesktopDragLeave(dndOver->w, dndOffer);
      std::shared_ptr<Offer> o = std::make_shared<Offer>();
      o->b = this;
      o->self = n->xid;
      o->source = dndSource = (::Window)m.data.l[0];
      o->version = (int)((unsigned long)m.data.l[1] >> 24);
      if (m.data.l[1] & 1) {
        // More than three types: the source lists them.
        Atom got;
        int format;
        unsigned long count = 0, after;
        unsigned char* data = NULL;
        std::lock_guard<std::recursive_mutex> g(lock);
        if (x.XGetWindowProperty(dpy, dndSource, xdndTypeList, 0, 1024, False, XA_ATOM, &got, &format, &count,
                                 &after, &data) == Success && data) {
          for (unsigned long i = 0; i < count; i++) o->types.push_back(((Atom*)data)[i]);
        }
        if (data) x.XFree(data);
      } else {
        for (int i = 2; i < 5; i++)
          if (m.data.l[i]) o->types.push_back((Atom)m.data.l[i]);
      }
      o->files = o->Has(uriList);
      o->text = o->Has(utf8) || o->Has(textPlain) || o->Has(textPlainBare) || o->Has(XA_STRING);
      dndOffer = o;
      dndOver = NULL;
    } else if (m.message_type == xdndPosition) {
      if ((::Window)m.data.l[0] != dndSource || !dndOffer) return;
      dndAt = POINT{(LONG)((unsigned long)m.data.l[2] >> 16), (LONG)(m.data.l[2] & 0xFFFF)};
      // The source asks for one action; another program's drop may still
      // be a copy. A source that asks the user offers all three.
      Atom asked = (Atom)m.data.l[4];
      dndAllowed = asked == xdndActionMove   ? DROPEFFECT_MOVE | DROPEFFECT_COPY
                   : asked == xdndActionLink ? DROPEFFECT_LINK | DROPEFFECT_COPY
                   : asked == xdndActionCopy || !asked ? DROPEFFECT_COPY
                                                       : DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
      if (dndOver && dndOver != n) DesktopDragLeave(dndOver->w, dndOffer);
      dndOver = n;
      DesktopDragOver(n->w, dndOffer, dndAt, PointerKeys(), dndAllowed);
    } else if (m.message_type == xdndLeave) {
      if ((::Window)m.data.l[0] != dndSource) return;
      if (dndOffer && dndOver) DesktopDragLeave(dndOver->w, dndOffer);
      dndOffer.reset();
      dndOver = NULL;
      dndSource = 0;
    } else if (m.message_type == xdndDrop) {
      if ((::Window)m.data.l[0] != dndSource || !dndOffer) return;
      ((Offer*)dndOffer.get())->dropTime = (Time)m.data.l[2];
      DesktopDrop(n->w, dndOffer, dndAt, PointerKeys() & ~(DWORD)(MK_LBUTTON | MK_MBUTTON | MK_RBUTTON), dndAllowed);
      dndOffer.reset();
      dndOver = NULL;
      dndSource = 0;
    }
  }

  // ---------------------------------------------------------------
  // Drags to other programs

  // The window at a root point that takes drops, and its XDND version;
  // ours is set when the program's own window is there.
  ::Window DropWindowAt(POINT pt, int* version, bool* ours) {
    *ours = false;
    *version = 0;
    ::Window w = root;
    for (int depth = 0; depth < 32; depth++) {
      ::Window child = 0;
      int cx, cy;
      if (!x.XTranslateCoordinates(dpy, root, w, pt.x, pt.y, &cx, &cy, &child) || !child) return 0;
      w = child;
      if (byXid.count(w)) {
        *ours = true;
        return 0;
      }
      Atom got;
      int format;
      unsigned long count = 0, after;
      unsigned char* data = NULL;
      if (x.XGetWindowProperty(dpy, w, xdndAware, 0, 1, False, XA_ATOM, &got, &format, &count, &after, &data) ==
              Success && data && count == 1) {
        *version = (int)*(Atom*)data;
        x.XFree(data);
        if (*version >= 3) return w;
        *version = 0;
        return 0;
      }
      if (data) x.XFree(data);
    }
    return 0;
  }

  // What the drag carries, as the other programs' types.
  void OfferDrag(IDataObject* data) {
    outTypes.clear();
    outUris.clear();
    outText.clear();
    std::vector<std::wstring> paths;
    std::wstring text;
    if (DataFiles(data, &paths)) {
      outUris = UrisFromPaths(paths);
      outTypes.push_back(uriList);
    }
    if (DataText(data, &text)) {
      outText = Narrow(text.c_str(), (int)text.size());
      for (Atom a : {utf8, textPlain, textPlainBare, (Atom)XA_STRING}) outTypes.push_back(a);
    }
    x.XChangeProperty(dpy, clipWin, xdndTypeList, XA_ATOM, 32, PropModeReplace, (const unsigned char*)outTypes.data(),
                      (int)outTypes.size());
    x.XSetSelectionOwner(dpy, xdndSelection, clipWin, CurrentTime);
  }

  // The action the keys ask for, as Windows takes them: Ctrl copies,
  // Shift moves, both link; else a copy.
  Atom AskedAction(DWORD keys, DWORD ok) {
    DWORD want = (keys & MK_CONTROL) && (keys & MK_SHIFT) ? DROPEFFECT_LINK
                 : (keys & MK_CONTROL)                    ? DROPEFFECT_COPY
                 : (keys & MK_SHIFT)                      ? DROPEFFECT_MOVE
                                                          : DROPEFFECT_COPY;
    if (!(want & ok)) want = ok;
    return ActionOf(want & ok);
  }

  void LeaveTarget() {
    if (outTarget) SendDnd(outTarget, xdndLeave, (long)clipWin, 0, 0, 0, 0);
    outTarget = 0;
    outAccept = outWaiting = outFinished = false;
  }

  int DragOut(IDataObject* data, IDropSource*, DWORD ok, DWORD keys, POINT pt, DWORD* effect) override {
    std::unique_lock<std::recursive_mutex> g(lock);
    if (!dpy) return kDragOurs;
    int version;
    bool ours;
    ::Window target = DropWindowAt(pt, &version, &ours);
    if (ours) return kDragOurs;
    if (!outActive) {
      outActive = true;
      OfferDrag(data);
    }
    if (outTypes.empty()) target = 0;  // nothing another program could take
    if (target != outTarget) {
      LeaveTarget();
      outTarget = target;
      outVersion = std::min(version, 5);
      if (target) {
        SendDnd(target, xdndEnter, (long)clipWin, ((long)outVersion << 24) | (outTypes.size() > 3 ? 1 : 0),
                outTypes.size() > 0 ? (long)outTypes[0] : 0, outTypes.size() > 1 ? (long)outTypes[1] : 0,
                outTypes.size() > 2 ? (long)outTypes[2] : 0);
      }
    }
    if (target && (!outWaiting || GetTickCount() - outSent > 500)) {
      // One position at a time: the next waits for the answer.
      SendDnd(target, xdndPosition, (long)clipWin, 0, ((long)pt.x << 16) | (pt.y & 0xFFFF), CurrentTime,
              (long)AskedAction(keys, ok));
      outWaiting = true;
      outSent = GetTickCount();
    }
    *effect = target && outAccept ? EffectOf(outAction) & ok : DROPEFFECT_NONE;
    if (target && outAccept && !*effect) *effect = ok & DROPEFFECT_COPY ? DROPEFFECT_COPY : ok;
    return kDragOther;
  }

  // Waits for the target's answer, handling what comes meanwhile.
  bool AwaitTarget(bool* flag, DWORD ms) {
    DWORD start = GetTickCount();
    while (!*flag && outTarget) {
      if (GetTickCount() - start > ms) return false;
      Drain();
      if (*flag) break;
      pollfd fd = {ConnectionNumber(dpy), POLLIN, 0};
      poll(&fd, 1, 20);
    }
    return *flag;
  }

  DWORD DragOutDrop() override {
    if (!dpy || !outTarget) {
      DragOutLeave();
      return DROPEFFECT_NONE;
    }
    // The answer to the last position decides.
    DWORD start = GetTickCount();
    while (outWaiting && GetTickCount() - start < 1000) {
      Drain();
      if (!outWaiting) break;
      pollfd fd = {ConnectionNumber(dpy), POLLIN, 0};
      poll(&fd, 1, 20);
    }
    if (!outAccept) {
      DragOutLeave();
      return DROPEFFECT_NONE;
    }
    DWORD effect = EffectOf(outAction);
    outFinished = false;
    SendDnd(outTarget, xdndDrop, (long)clipWin, 0, CurrentTime, 0, 0);
    // The target fetches the data, then says it has finished. Before
    // version 5 it does not say how.
    AwaitTarget(&outFinished, 10000);
    if (outFinished && outVersion >= 5) effect = outAccept ? EffectOf(outAction) : DROPEFFECT_NONE;
    outTarget = 0;
    outActive = false;
    return effect;
  }

  void DragOutLeave() override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    LeaveTarget();
    outActive = false;
  }

  // The target of a drag to another program answers.
  void TargetMessage(const XClientMessageEvent& m) {
    if (!outTarget || (::Window)m.data.l[0] != outTarget) return;
    if (m.message_type == xdndStatus) {
      outWaiting = false;
      outAccept = (m.data.l[1] & 1) != 0;
      outAction = outVersion >= 2 ? (Atom)m.data.l[4] : xdndActionCopy;
    } else if (m.message_type == xdndFinished) {
      outFinished = true;
      if (outVersion >= 5) {
        outAccept = (m.data.l[1] & 1) != 0;
        outAction = (Atom)m.data.l[2];
      }
    }
  }

  void Handle(XEvent& e) {
    if (e.xany.window == clipWin && clipWin) {
      if (e.type == SelectionRequest) {
        Request(e.xselectionrequest);
      } else if (e.type == SelectionNotify) {
        std::lock_guard<std::recursive_mutex> g(lock);
        notifyEvent = e.xselection;
        notified = true;
      } else if (e.type == ClientMessage) {
        TargetMessage(e.xclient);
      } else if (fixesEvent >= 0 && e.type == fixesEvent) {
        std::lock_guard<std::recursive_mutex> g(lock);
        clipGeneration++;
      }
      return;
    }
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
        if (e.xclient.message_type == wmProtocols && (Atom)e.xclient.data.l[0] == wmDelete)
          CloseRequested(w);
        else
          DndMessage(n, e.xclient);
        break;
      case FocusIn:
        if (n->ic && im) {
          std::lock_guard<std::recursive_mutex> g(lock);
          x.XSetICFocus(n->ic);
        }
        if (n->managed && e.xfocus.mode == NotifyNormal && GetActiveWindow() != w->hwnd && IsWindowEnabled(w->hwnd))
          SetActiveWindow(w->hwnd);
        if (n->ic) ImeCursorMoved(w);
        break;
      case FocusOut:
        if (n->ic && im) {
          std::lock_guard<std::recursive_mutex> g(lock);
          x.XUnsetICFocus(n->ic);
        }
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
        std::string text;  // what the input method gives, in UTF-8
        bool fromIm = false;
        {
          std::lock_guard<std::recursive_mutex> g(lock);
          len = x.XLookupString(&e.xkey, buf, sizeof buf, &sym, NULL);
          if (e.type == KeyPress && n->ic && im) {
            char big[256];
            Status st = 0;
            KeySym s2 = 0;
            int n8 = x.Xutf8LookupString(n->ic, &e.xkey, big, sizeof big - 1, &s2, &st);
            std::vector<char> more;
            if (st == XBufferOverflow) {
              more.resize(n8 + 1);
              n8 = x.Xutf8LookupString(n->ic, &e.xkey, more.data(), n8, &s2, &st);
            }
            if ((st == XLookupChars || st == XLookupBoth) && n8 > 0)
              text.assign(more.empty() ? big : more.data(), n8);
            // Text with no key of its own is the input method's: what
            // it has finished composing, or a compose sequence's.
            fromIm = e.xkey.keycode == 0 || !n->preedit.empty();
          }
        }
        if (fromIm) {
          std::wstring done = Widen(text.c_str(), (int)text.size());
          if (!n->preedit.empty())
            ImeCommit(w, done);
          else
            for (wchar_t c : done) CharInput(w, c);
          n->preedit.clear();
          if (e.xkey.keycode == 0) break;
          text.clear();
        }
        KeySym base = x.XLookupKeysym(&e.xkey, 0);
        bool ext;
        UINT vk = VkOf(base, &ext);
        if (!vk) vk = VkOf(sym, &ext);
        UINT scan = e.xkey.keycode >= 8 ? e.xkey.keycode - 8 : 0;
        LPARAM lp = 1 | ((LPARAM)(scan & 0xFF) << 16) | (ext ? (1 << 24) : 0);
        bool down = e.type == KeyPress;
        wchar_t ch = 0;
        std::wstring typed = Widen(text.c_str(), (int)text.size());
        if (down) {
          ch = typed.size() == 1 ? typed[0] : CharOf(sym, buf, len);
          if (sym == XK_Return || sym == XK_KP_Enter) ch = L'\r';
          if (sym == XK_BackSpace) ch = L'\b';
          if (sym == XK_Tab || sym == XK_ISO_Left_Tab) ch = L'\t';
          if (sym == XK_Escape) ch = 0x1B;
          if (ch == 0x7F) ch = 0;  // Delete is a key, not text
        }
        // The key's text comes from X, with its layout, rather than from
        // TranslateMessage (bit 25 tells it so).
        if (ch) lp |= 0x02000000;
        if (vk) KeyInput(w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
        if (ch) CharInput(w, ch);
        // A compose sequence's text, of more than one character.
        if (down && typed.size() > 1)
          for (wchar_t c : typed) CharInput(w, c);
        break;
      }
    }
  }

  // ---------------------------------------------------------------
  // The input method

  // Opens the user's input method (XMODIFIERS names it) in the locale's
  // character set, or a UTF-8 one, and chooses how its composition shows:
  // drawn by the program, at the caret by the input method, or in the
  // input method's own window.
  void OpenInputMethod() {
    if (!x.xim) return;
    std::string before = setlocale(LC_CTYPE, NULL) ? setlocale(LC_CTYPE, NULL) : "C";
    if (!setlocale(LC_CTYPE, "") || !x.XSupportsLocale() || strcmp(setlocale(LC_CTYPE, NULL), "C") == 0 ||
        strcmp(setlocale(LC_CTYPE, NULL), "POSIX") == 0)
      setlocale(LC_CTYPE, "C.UTF-8");
    if (x.XSupportsLocale()) {
      x.XSetLocaleModifiers("");
      im = x.XOpenIM(dpy, NULL, NULL, NULL);
      if (!im) {
        x.XSetLocaleModifiers("@im=none");
        im = x.XOpenIM(dpy, NULL, NULL, NULL);
      }
    }
    setlocale(LC_CTYPE, before.c_str());
    if (!im) return;
    // The input method's server may go; its contexts go with it.
    XIMCallback destroyed = {(XPointer)this, (XIMProc)ImDestroyed};
    x.XSetIMValues(im, XNDestroyCallback, &destroyed, NULL);
    XIMStyles* styles = NULL;
    if (x.XGetIMValues(im, XNQueryInputStyle, &styles, NULL) || !styles) {
      x.XCloseIM(im);
      im = NULL;
      return;
    }
    const XIMStyle prefer[] = {XIMPreeditCallbacks | XIMStatusNothing, XIMPreeditCallbacks | XIMStatusNone,
                               XIMPreeditPosition | XIMStatusNothing, XIMPreeditPosition | XIMStatusNone,
                               XIMPreeditNothing | XIMStatusNothing,  XIMPreeditNothing | XIMStatusNone,
                               XIMPreeditNone | XIMStatusNone};
    for (XIMStyle want : prefer) {
      for (unsigned short i = 0; i < styles->count_styles && !imStyle; i++)
        if (styles->supported_styles[i] == want) imStyle = want;
      if (imStyle) break;
    }
    x.XFree(styles);
    if (!imStyle) {
      x.XCloseIM(im);
      im = NULL;
    }
  }
  static void ImDestroyed(XIM, XPointer data, XPointer) { ((X11Backend*)data)->im = NULL; }

  void CreateIC(Native* n) {
    if (!im) return;
    if (imStyle & XIMPreeditCallbacks) {
      XIMCallback start = {(XPointer)n, (XIMProc)PreeditStart};
      XIMCallback done = {(XPointer)n, (XIMProc)PreeditDone};
      XIMCallback draw = {(XPointer)n, (XIMProc)PreeditDraw};
      XIMCallback caret = {(XPointer)n, (XIMProc)PreeditCaret};
      XPoint spot = {0, 0};
      XVaNestedList attrs = x.XVaCreateNestedList(0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done,
                                                  XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret,
                                                  XNSpotLocation, &spot, NULL);
      n->ic = x.XCreateIC(im, XNInputStyle, imStyle, XNClientWindow, n->xid, XNFocusWindow, n->xid,
                          XNPreeditAttributes, attrs, NULL);
      x.XFree(attrs);
    } else if (imStyle & XIMPreeditPosition) {
      XPoint spot = {0, 0};
      XVaNestedList attrs = x.XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
      n->ic = x.XCreateIC(im, XNInputStyle, imStyle, XNClientWindow, n->xid, XNFocusWindow, n->xid,
                          XNPreeditAttributes, attrs, NULL);
      x.XFree(attrs);
    } else {
      n->ic = x.XCreateIC(im, XNInputStyle, imStyle, XNClientWindow, n->xid, XNFocusWindow, n->xid, NULL);
    }
  }

  static int PreeditStart(XIC, XPointer data, XPointer) {
    ((Native*)data)->preedit.clear();
    return -1;  // no limit on its length
  }
  static void PreeditDone(XIC, XPointer data, XPointer) {
    Native* n = (Native*)data;
    if (!n->preedit.empty()) ImePreedit(n->w, L"", 0);
    n->preedit.clear();
  }
  static void PreeditDraw(XIC, XPointer data, XIMPreeditDrawCallbackStruct* d) {
    Native* n = (Native*)data;
    if (!d) return;
    std::wstring text;
    if (d->text) {
      if (d->text->encoding_is_wchar) {
        if (d->text->string.wide_char) text.assign(d->text->string.wide_char, d->text->length);
      } else if (d->text->string.multi_byte) {
        // In the locale the input method was opened in, UTF-8.
        text = Widen(d->text->string.multi_byte);
      }
    }
    int first = std::max(0, std::min(d->chg_first, (int)n->preedit.size()));
    int count = std::max(0, std::min(d->chg_length, (int)n->preedit.size() - first));
    n->preedit.replace(first, count, text);
    n->caret = std::max(0, std::min(d->caret, (int)n->preedit.size()));
    ImePreedit(n->w, n->preedit, n->caret);
  }
  static void PreeditCaret(XIC, XPointer data, XIMPreeditCaretCallbackStruct* c) {
    Native* n = (Native*)data;
    if (!c) return;
    switch (c->direction) {
      case XIMForwardChar: n->caret++; break;
      case XIMBackwardChar: n->caret--; break;
      case XIMLineStart: n->caret = 0; break;
      case XIMLineEnd: n->caret = (int)n->preedit.size(); break;
      case XIMAbsolutePosition: n->caret = c->position; break;
      default: break;
    }
    n->caret = std::max(0, std::min(n->caret, (int)n->preedit.size()));
    c->position = n->caret;
    ImePreedit(n->w, n->preedit, n->caret);
  }

  // Where the input method's candidates (or its composition, at the
  // caret) go: XNSpotLocation is the text's baseline.
  void ImeCursorMoved(Window* top) override {
    RECT r;
    if (!im || !ImeCursorRect(top, &r)) return;
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(top);
    if (!n || !n->ic || !(imStyle & (XIMPreeditPosition | XIMPreeditCallbacks))) return;
    XPoint spot = {(short)r.left, (short)r.bottom};
    XVaNestedList attrs = x.XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
    x.XSetICValues(n->ic, XNPreeditAttributes, attrs, NULL);
    x.XFree(attrs);
    x.XFlush(dpy);
  }
  void ImeReset(Window* top) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(top);
    if (!n || !n->ic || !im) return;
    if (char* left = x.Xutf8ResetIC(n->ic)) x.XFree(left);
    n->preedit.clear();
    x.XFlush(dpy);
  }

  void Drain() {
    for (;;) {
      XEvent e;
      {
        std::lock_guard<std::recursive_mutex> g(lock);
        if (!dpy || !x.XPending(dpy)) return;
        x.XNextEvent(dpy, &e);
        // The input method takes the keys it composes with.
        if (im && x.XFilterEvent(&e, None)) continue;
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
