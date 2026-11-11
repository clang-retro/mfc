// backend_wayland.cpp - showing top-level windows on a Wayland compositor.
//
// Each top-level window becomes an xdg_toplevel, which the window draws
// its own frame in, and its surface is copied to a shared-memory buffer
// as it changes. Menus, list drop-downs and tooltips become xdg_popups of
// the window they belong to. Wayland does not tell a program where its
// windows are on the screen, so a window keeps the position the program
// gave it, and a popup goes where it lies relative to its window.
//
// libwayland-client and libxkbcommon are loaded when the backend starts,
// so programs run without them when there is no compositor. The xdg-shell
// protocol's message tables are written out below from the protocol's
// published description.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#if defined(__unix__) && !defined(__APPLE__) && __has_include(<wayland-client.h>) && \
    __has_include(<xkbcommon/xkbcommon.h>)

// libwayland-client's protocol helpers call these functions and name
// these interfaces; here they go through pointers filled in when the
// library is loaded.
#define wl_proxy_marshal_flags (*winport_wl_proxy_marshal_flags)
#define wl_proxy_add_listener (*winport_wl_proxy_add_listener)
#define wl_proxy_destroy (*winport_wl_proxy_destroy)
#define wl_proxy_get_version (*winport_wl_proxy_get_version)
#define wl_proxy_set_user_data (*winport_wl_proxy_set_user_data)
#define wl_proxy_get_user_data (*winport_wl_proxy_get_user_data)
#define wl_registry_interface (*winport_wl_registry_interface)
#define wl_compositor_interface (*winport_wl_compositor_interface)
#define wl_surface_interface (*winport_wl_surface_interface)
#define wl_shm_interface (*winport_wl_shm_interface)
#define wl_shm_pool_interface (*winport_wl_shm_pool_interface)
#define wl_buffer_interface (*winport_wl_buffer_interface)
#define wl_callback_interface (*winport_wl_callback_interface)
#define wl_seat_interface (*winport_wl_seat_interface)
#define wl_pointer_interface (*winport_wl_pointer_interface)
#define wl_keyboard_interface (*winport_wl_keyboard_interface)
#define wl_output_interface (*winport_wl_output_interface)
#pragma push_macro("interface")
#undef interface  // winport.h makes it mean struct, as on Windows
#include <wayland-client.h>
#include <wayland-cursor.h>
#pragma pop_macro("interface")
#undef wl_proxy_marshal_flags
#undef wl_proxy_add_listener
#undef wl_proxy_destroy
#undef wl_proxy_get_version
#undef wl_proxy_set_user_data
#undef wl_proxy_get_user_data

#include <xkbcommon/xkbcommon.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <set>

// The pointers the helpers above go through.
extern "C" {
const struct wl_interface* winport_wl_registry_interface;
const struct wl_interface* winport_wl_compositor_interface;
const struct wl_interface* winport_wl_surface_interface;
const struct wl_interface* winport_wl_shm_interface;
const struct wl_interface* winport_wl_shm_pool_interface;
const struct wl_interface* winport_wl_buffer_interface;
const struct wl_interface* winport_wl_callback_interface;
const struct wl_interface* winport_wl_seat_interface;
const struct wl_interface* winport_wl_pointer_interface;
const struct wl_interface* winport_wl_keyboard_interface;
const struct wl_interface* winport_wl_output_interface;
}

namespace winport {

namespace {

#define WL_FUNCTIONS(F)                                                                                          \
  F(wl_display_connect) F(wl_display_disconnect) F(wl_display_get_fd) F(wl_display_roundtrip)                   \
  F(wl_display_dispatch_pending) F(wl_display_prepare_read) F(wl_display_read_events) F(wl_display_cancel_read) \
  F(wl_display_flush) F(wl_display_get_error)

#define XKB_FUNCTIONS(F)                                                                                      \
  F(xkb_context_new) F(xkb_keymap_new_from_string) F(xkb_keymap_unref) F(xkb_state_new) F(xkb_state_unref) \
  F(xkb_state_update_mask) F(xkb_state_key_get_one_sym) F(xkb_state_key_get_utf32)                         \
  F(xkb_state_mod_name_is_active) F(xkb_keymap_key_get_syms_by_level) F(xkb_keymap_key_repeats)

#define CURSOR_FUNCTIONS(F) F(wl_cursor_theme_load) F(wl_cursor_theme_get_cursor) F(wl_cursor_image_get_buffer)

void* Open(const char* a, const char* b) {
  void* lib = dlopen(a, RTLD_NOW | RTLD_LOCAL);
  return lib ? lib : dlopen(b, RTLD_NOW | RTLD_LOCAL);
}

struct WlLib {
#define F(name) decltype(&::name) name = nullptr;
  WL_FUNCTIONS(F)
  XKB_FUNCTIONS(F)
  CURSOR_FUNCTIONS(F)
#undef F
  bool cursors = false;
  bool Load() {
    void* lib = Open("libwayland-client.so.0", "libwayland-client.so");
    void* xkb = Open("libxkbcommon.so.0", "libxkbcommon.so");
    if (!lib || !xkb) return false;
#define F(name)                                \
  name = (decltype(&::name))dlsym(lib, #name); \
  if (!name) return false;
    WL_FUNCTIONS(F)
#undef F
#define P(name)                                                                       \
  winport_##name = (decltype(winport_##name))dlsym(lib, #name);                       \
  if (!winport_##name) return false;
    P(wl_proxy_marshal_flags) P(wl_proxy_add_listener) P(wl_proxy_destroy) P(wl_proxy_get_version)
    P(wl_proxy_set_user_data) P(wl_proxy_get_user_data) P(wl_registry_interface) P(wl_compositor_interface)
    P(wl_surface_interface) P(wl_shm_interface) P(wl_shm_pool_interface) P(wl_buffer_interface)
    P(wl_callback_interface) P(wl_seat_interface) P(wl_pointer_interface) P(wl_keyboard_interface)
    P(wl_output_interface)
#undef P
#define F(name)                                \
  name = (decltype(&::name))dlsym(xkb, #name); \
  if (!name) return false;
    XKB_FUNCTIONS(F)
#undef F
    // Without libwayland-cursor the compositor's own pointer shows.
    if (void* cur = Open("libwayland-cursor.so.0", "libwayland-cursor.so")) {
      cursors = true;
#define F(name)                                \
  name = (decltype(&::name))dlsym(cur, #name); \
  if (!name) cursors = false;
      CURSOR_FUNCTIONS(F)
#undef F
    }
    return true;
  }
};

// ---------------------------------------------------------------
// xdg-shell, version 1: the messages each interface sends and takes.

enum {
  XDG_WM_BASE_DESTROY, XDG_WM_BASE_CREATE_POSITIONER, XDG_WM_BASE_GET_XDG_SURFACE, XDG_WM_BASE_PONG
};
enum {
  XDG_POSITIONER_DESTROY, XDG_POSITIONER_SET_SIZE, XDG_POSITIONER_SET_ANCHOR_RECT, XDG_POSITIONER_SET_ANCHOR,
  XDG_POSITIONER_SET_GRAVITY, XDG_POSITIONER_SET_CONSTRAINT_ADJUSTMENT, XDG_POSITIONER_SET_OFFSET
};
enum {
  XDG_SURFACE_DESTROY, XDG_SURFACE_GET_TOPLEVEL, XDG_SURFACE_GET_POPUP, XDG_SURFACE_SET_WINDOW_GEOMETRY,
  XDG_SURFACE_ACK_CONFIGURE
};
enum {
  XDG_TOPLEVEL_DESTROY, XDG_TOPLEVEL_SET_PARENT, XDG_TOPLEVEL_SET_TITLE, XDG_TOPLEVEL_SET_APP_ID,
  XDG_TOPLEVEL_SHOW_WINDOW_MENU, XDG_TOPLEVEL_MOVE, XDG_TOPLEVEL_RESIZE, XDG_TOPLEVEL_SET_MAX_SIZE,
  XDG_TOPLEVEL_SET_MIN_SIZE, XDG_TOPLEVEL_SET_MAXIMIZED, XDG_TOPLEVEL_UNSET_MAXIMIZED,
  XDG_TOPLEVEL_SET_FULLSCREEN, XDG_TOPLEVEL_UNSET_FULLSCREEN, XDG_TOPLEVEL_SET_MINIMIZED
};
enum { XDG_POPUP_DESTROY, XDG_POPUP_GRAB };

extern wl_interface xdg_wm_base_iface, xdg_positioner_iface, xdg_surface_iface, xdg_toplevel_iface,
    xdg_popup_iface;

// The interface each object argument names, by message; filled in when
// the core interfaces are known.
const wl_interface* xdgTypes[] = {
    NULL, NULL, NULL, NULL,                   // 0: untyped arguments
    &xdg_positioner_iface,                    // 4: create_positioner
    &xdg_surface_iface, NULL,                 // 5: get_xdg_surface (wl_surface)
    &xdg_toplevel_iface,                      // 7: get_toplevel
    &xdg_popup_iface, &xdg_surface_iface, &xdg_positioner_iface,  // 8: get_popup
    &xdg_toplevel_iface,                      // 11: set_parent
    NULL, NULL, NULL, NULL,                   // 12: show_window_menu (wl_seat)
    NULL, NULL,                               // 16: move (wl_seat)
    NULL, NULL, NULL,                         // 18: resize (wl_seat)
    NULL,                                     // 21: set_fullscreen (wl_output)
    NULL, NULL,                               // 22: grab (wl_seat)
};

const wl_message kWmBaseRequests[] = {
    {"destroy", "", xdgTypes}, {"create_positioner", "n", xdgTypes + 4},
    {"get_xdg_surface", "no", xdgTypes + 5}, {"pong", "u", xdgTypes}};
const wl_message kWmBaseEvents[] = {{"ping", "u", xdgTypes}};
const wl_message kPositionerRequests[] = {
    {"destroy", "", xdgTypes},       {"set_size", "ii", xdgTypes},
    {"set_anchor_rect", "iiii", xdgTypes}, {"set_anchor", "u", xdgTypes},
    {"set_gravity", "u", xdgTypes},  {"set_constraint_adjustment", "u", xdgTypes},
    {"set_offset", "ii", xdgTypes}};
const wl_message kSurfaceRequests[] = {
    {"destroy", "", xdgTypes}, {"get_toplevel", "n", xdgTypes + 7}, {"get_popup", "n?oo", xdgTypes + 8},
    {"set_window_geometry", "iiii", xdgTypes}, {"ack_configure", "u", xdgTypes}};
const wl_message kSurfaceEvents[] = {{"configure", "u", xdgTypes}};
const wl_message kToplevelRequests[] = {
    {"destroy", "", xdgTypes},           {"set_parent", "?o", xdgTypes + 11},
    {"set_title", "s", xdgTypes},        {"set_app_id", "s", xdgTypes},
    {"show_window_menu", "ouii", xdgTypes + 12}, {"move", "ou", xdgTypes + 16},
    {"resize", "ouu", xdgTypes + 18},    {"set_max_size", "ii", xdgTypes},
    {"set_min_size", "ii", xdgTypes},    {"set_maximized", "", xdgTypes},
    {"unset_maximized", "", xdgTypes},   {"set_fullscreen", "?o", xdgTypes + 21},
    {"unset_fullscreen", "", xdgTypes},  {"set_minimized", "", xdgTypes}};
const wl_message kToplevelEvents[] = {{"configure", "iia", xdgTypes}, {"close", "", xdgTypes}};
const wl_message kPopupRequests[] = {{"destroy", "", xdgTypes}, {"grab", "ou", xdgTypes + 22}};
const wl_message kPopupEvents[] = {{"configure", "iiii", xdgTypes}, {"popup_done", "", xdgTypes}};

wl_interface xdg_wm_base_iface = {"xdg_wm_base", 1, 4, kWmBaseRequests, 1, kWmBaseEvents};
wl_interface xdg_positioner_iface = {"xdg_positioner", 1, 7, kPositionerRequests, 0, NULL};
wl_interface xdg_surface_iface = {"xdg_surface", 1, 5, kSurfaceRequests, 1, kSurfaceEvents};
wl_interface xdg_toplevel_iface = {"xdg_toplevel", 1, 14, kToplevelRequests, 2, kToplevelEvents};
wl_interface xdg_popup_iface = {"xdg_popup", 1, 2, kPopupRequests, 2, kPopupEvents};

void FillXdgTypes() {
  xdgTypes[6] = &wl_surface_interface;
  xdgTypes[12] = &wl_seat_interface;
  xdgTypes[16] = &wl_seat_interface;
  xdgTypes[18] = &wl_seat_interface;
  xdgTypes[21] = &wl_output_interface;
  xdgTypes[22] = &wl_seat_interface;
}

// A request on an xdg object; iface is the new object's, if it makes one.
template <typename... A>
wl_proxy* Req(void* obj, uint32_t opcode, const wl_interface* iface, A... args) {
  wl_proxy* p = (wl_proxy*)obj;
  return winport_wl_proxy_marshal_flags(p, opcode, iface, winport_wl_proxy_get_version(p), 0, args...);
}
void Done(void* obj, uint32_t opcode) {
  wl_proxy* p = (wl_proxy*)obj;
  winport_wl_proxy_marshal_flags(p, opcode, NULL, winport_wl_proxy_get_version(p), WL_MARSHAL_FLAG_DESTROY);
}

enum { ANCHOR_TOP_LEFT = 5, GRAVITY_BOTTOM_RIGHT = 8 };
enum { ADJUST_SLIDE_X = 1, ADJUST_SLIDE_Y = 2 };
enum {
  EDGE_TOP = 1, EDGE_BOTTOM = 2, EDGE_LEFT = 4, EDGE_TOP_LEFT = 5, EDGE_BOTTOM_LEFT = 6, EDGE_RIGHT = 8,
  EDGE_TOP_RIGHT = 9, EDGE_BOTTOM_RIGHT = 10
};

// ---------------------------------------------------------------
// Keys

struct KeyMap {
  xkb_keysym_t sym;
  BYTE vk;
  bool ext;
};
const KeyMap kKeys[] = {
#define KEY(name, vk, ext) {XKB_KEY_##name, vk, ext},
#include "keysyms.inc"
#undef KEY
};

UINT VkOf(xkb_keysym_t sym, bool* ext) {
  *ext = false;
  if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) return 'A' + (UINT)(sym - XKB_KEY_a);
  if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) return 'A' + (UINT)(sym - XKB_KEY_A);
  if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) return '0' + (UINT)(sym - XKB_KEY_0);
  if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F24) return VK_F1 + (UINT)(sym - XKB_KEY_F1);
  for (const KeyMap& k : kKeys)
    if (k.sym == sym) {
      *ext = k.ext;
      return k.vk;
    }
  return 0;
}

std::string Utf8(const std::wstring& s) {
  std::string u;
  for (wchar_t c : s) {
    unsigned v = (unsigned)c;
    if (v < 0x80) {
      u += (char)v;
    } else if (v < 0x800) {
      u += (char)(0xC0 | (v >> 6));
      u += (char)(0x80 | (v & 0x3F));
    } else if (v < 0x10000) {
      u += (char)(0xE0 | (v >> 12));
      u += (char)(0x80 | ((v >> 6) & 0x3F));
      u += (char)(0x80 | (v & 0x3F));
    } else {
      u += (char)(0xF0 | (v >> 18));
      u += (char)(0x80 | ((v >> 12) & 0x3F));
      u += (char)(0x80 | ((v >> 6) & 0x3F));
      u += (char)(0x80 | (v & 0x3F));
    }
  }
  return u;
}

ULONGLONG Millis() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ULONGLONG)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---------------------------------------------------------------
// Windows

// A shared-memory buffer the compositor shows.
struct Buffer {
  wl_buffer* buffer = NULL;
  void* data = NULL;
  size_t size = 0;
  int width = 0, height = 0;
  bool busy = false;
};

struct Native {
  Window* w = NULL;
  wl_surface* surface = NULL;
  void* xdgSurface = NULL;
  void* toplevel = NULL;
  void* popup = NULL;
  Native* parent = NULL;  // a popup's window
  bool configured = false;
  bool shown = false;
  int pendingWidth = 0, pendingHeight = 0;  // from the toplevel's configure
  Buffer buffers[2];
  wl_callback* frame = NULL;
  RECT damage = {0, 0, 0, 0};  // still to be shown
};

struct WaylandBackend;
WaylandBackend* g_wl;

struct WaylandBackend : Backend {
  WlLib wl;
  wl_display* dpy = NULL;
  wl_registry* registry = NULL;
  wl_compositor* compositor = NULL;
  wl_shm* shm = NULL;
  wl_seat* seat = NULL;
  wl_pointer* pointer = NULL;
  wl_keyboard* keyboard = NULL;
  wl_output* output = NULL;
  void* wmBase = NULL;
  SIZE screen = {1024, 768};
  int wakePipe[2] = {-1, -1};
  std::recursive_mutex lock;  // the windows' buffers and the sets below
  std::set<Native*> natives;
  std::string appId;

  // The pointer
  Native* ptrIn = NULL;
  POINT ptrAt = {0, 0};
  uint32_t buttons = 0;  // MK_ flags
  uint32_t lastSerial = 0;  // of the last button press or key
  uint32_t enterSerial = 0;
  bool releaseAfterGrab = false;  // the compositor took a button press
  bool discreteAxis = false;
  int axisAcc[2] = {0, 0};
  wl_cursor_theme* cursorTheme = NULL;
  wl_surface* cursorSurface = NULL;
  int cursorHotX = 0, cursorHotY = 0;
  bool cursorHidden = false;
  Buffer cursorBuffer;  // a program's own cursor

  // The keyboard
  Native* kbdIn = NULL;
  xkb_context* xkbCtx = NULL;
  xkb_keymap* keymap = NULL;
  xkb_state* xkbState = NULL;
  int repeatRate = 25, repeatDelay = 400;
  uint32_t repeatKey = 0;
  ULONGLONG repeatNext = 0;

  bool Start() {
    if (!wl.Load()) return false;
    FillXdgTypes();
    dpy = wl.wl_display_connect(NULL);
    if (!dpy) return false;
    g_wl = this;
    registry = wl_display_get_registry(dpy);
    static const wl_registry_listener rl = {Global, GlobalRemove};
    wl_registry_add_listener(registry, &rl, this);
    wl.wl_display_roundtrip(dpy);
    if (!compositor || !shm || !wmBase) {
      fprintf(stderr, "winport: the Wayland compositor lacks xdg-shell\n");
      wl.wl_display_disconnect(dpy);
      dpy = NULL;
      return false;
    }
    wl.wl_display_roundtrip(dpy);  // the seat's devices, the output's mode
    if (pipe(wakePipe) == 0) {
      fcntl(wakePipe[0], F_SETFL, O_NONBLOCK);
      fcntl(wakePipe[1], F_SETFL, O_NONBLOCK);
    }
    xkbCtx = wl.xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (wl.cursors) {
      const char* size = getenv("XCURSOR_SIZE");
      cursorTheme = wl.wl_cursor_theme_load(getenv("XCURSOR_THEME"), size ? atoi(size) : 24, shm);
    }
    cursorSurface = wl_compositor_create_surface(compositor);
    ThemeCursor("left_ptr");
    char comm[64] = "";
    FILE* f = fopen("/proc/self/comm", "r");
    if (f) {
      if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
      fclose(f);
    }
    appId = *comm ? comm : "winport";
    return true;
  }

  const char* Name() override { return "wayland"; }
  SIZE ScreenSize() override { return screen; }

  // ---------------------------------------------------------------
  // Globals

  static void Global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
    WaylandBackend* b = (WaylandBackend*)data;
    if (strcmp(iface, "wl_compositor") == 0 && version >= 4) {
      b->compositor = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, "wl_shm") == 0) {
      b->shm = (wl_shm*)wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, "xdg_wm_base") == 0) {
      b->wmBase = wl_registry_bind(r, name, &xdg_wm_base_iface, 1);
      static void (*const listener[])(void) = {(void (*)(void))Ping};
      winport_wl_proxy_add_listener((wl_proxy*)b->wmBase, (void (**)(void))listener, b);
    } else if (strcmp(iface, "wl_seat") == 0 && !b->seat) {
      b->seat = (wl_seat*)wl_registry_bind(r, name, &wl_seat_interface, std::min<uint32_t>(version, 5));
      static const wl_seat_listener sl = {SeatCaps, SeatName};
      wl_seat_add_listener(b->seat, &sl, b);
    } else if (strcmp(iface, "wl_output") == 0 && !b->output && version >= 2) {
      b->output = (wl_output*)wl_registry_bind(r, name, &wl_output_interface, 2);
      static const wl_output_listener ol = {OutGeometry, OutMode, OutDone, OutScale, NULL, NULL};
      wl_output_add_listener(b->output, &ol, b);
    }
  }
  static void GlobalRemove(void*, wl_registry*, uint32_t) {}
  static void Ping(void* data, void* base, uint32_t serial) {
    (void)data;
    Req(base, XDG_WM_BASE_PONG, NULL, serial);
  }
  static void OutGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*,
                          int32_t) {}
  static void OutMode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t) {
    if ((flags & WL_OUTPUT_MODE_CURRENT) && width > 0 && height > 0) ((WaylandBackend*)data)->screen = {width, height};
  }
  static void OutDone(void*, wl_output*) {}
  static void OutScale(void*, wl_output*, int32_t) {}

  static void SeatCaps(void* data, wl_seat* seat, uint32_t caps) {
    WaylandBackend* b = (WaylandBackend*)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !b->pointer) {
      b->pointer = wl_seat_get_pointer(seat);
      static const wl_pointer_listener pl = {PtrEnter, PtrLeave, PtrMotion, PtrButton, PtrAxis, PtrFrame,
                                             PtrAxisSource, PtrAxisStop, PtrAxisDiscrete, NULL, NULL};
      wl_pointer_add_listener(b->pointer, &pl, b);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !b->keyboard) {
      b->keyboard = wl_seat_get_keyboard(seat);
      static const wl_keyboard_listener kl = {KbdKeymap, KbdEnter, KbdLeave, KbdKey, KbdModifiers, KbdRepeat};
      wl_keyboard_add_listener(b->keyboard, &kl, b);
    }
  }
  static void SeatName(void*, wl_seat*, const char*) {}

  // ---------------------------------------------------------------
  // Showing windows

  Native* NativeOf(Window* w) { return (Native*)w->native; }

  // Menus, drop-downs and tooltips are popups of a window; the rest are
  // toplevels.
  bool IsPopup(Window* w) {
    if ((w->style & WS_CAPTION) == WS_CAPTION) return false;
    if (w->exStyle & WS_EX_APPWINDOW) return false;
    return (w->style & WS_POPUP) != 0;
  }

  // The toplevel a popup belongs to: its owner's, or else the window with
  // the keyboard, or any shown toplevel.
  Native* PopupParent(Window* w) {
    for (Window* o = w->owner; o; o = o->owner) {
      Native* n = NativeOf(o->TopLevel());
      while (n && n->popup) n = n->parent;
      if (n && n->toplevel) return n;
    }
    if (kbdIn && kbdIn->toplevel) return kbdIn;
    for (Native* n : natives)
      if (n->toplevel) return n;
    return NULL;
  }

  Native* Create(Window* w) {
    Native* n = new Native;
    n->w = w;
    n->surface = wl_compositor_create_surface(compositor);
    wl_surface_set_user_data(n->surface, n);
    natives.insert(n);
    w->native = n;
    return n;
  }

  static void XdgConfigure(void* data, void* xdgSurface, uint32_t serial) {
    Native* n = (Native*)data;
    Req(xdgSurface, XDG_SURFACE_ACK_CONFIGURE, NULL, serial);
    int cw = n->pendingWidth, ch = n->pendingHeight;
    n->pendingWidth = n->pendingHeight = 0;
    Window* w = n->w;
    if (cw > 0 && ch > 0 && n->toplevel) {
      RECT r = w->rect;
      if (cw != r.right - r.left || ch != r.bottom - r.top) ResizeRequested(w, cw, ch);
    }
    UserGuard ug(UserLock());
    std::lock_guard<std::recursive_mutex> g(g_wl->lock);
    if (!g_wl->natives.count(n)) return;
    if (!n->configured) {
      n->configured = true;
      Surface* s = w->surface;
      if (s) n->damage = RECT{0, 0, s->width, s->height};
    }
    g_wl->Flush(n);
  }
  static void ToplevelConfigure(void* data, void*, int32_t width, int32_t height, wl_array*) {
    Native* n = (Native*)data;
    n->pendingWidth = width;
    n->pendingHeight = height;
  }
  static void ToplevelClose(void* data, void*) { CloseRequested(((Native*)data)->w); }
  static void PopupConfigure(void* data, void*, int32_t x, int32_t y, int32_t, int32_t) {
    // Where the compositor put the popup, after keeping it on the screen.
    Native* n = (Native*)data;
    if (!n->parent) return;
    Window* w = n->w;
    POINT at = {n->parent->w->rect.left + x, n->parent->w->rect.top + y};
    if (at.x != w->rect.left || at.y != w->rect.top) {
      g_wl->placing = true;
      SetWindowPos(w->hwnd, NULL, at.x, at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      g_wl->placing = false;
    }
  }
  static void PopupDone(void* data, void*) {
    // The compositor closed the popup: the menu or list it shows is over.
    Native* n = (Native*)data;
    HWND h = n->w->hwnd;
    HWND cap = GetCapture();
    if (cap) PostMessageW(cap, WM_CANCELMODE, 0, 0);
    else PostMessageW(h, WM_CANCELMODE, 0, 0);
  }
  bool placing = false;  // a popup's configure is moving it

  void SizeLimits(Native* n) {
    Window* w = n->w;
    int cw = std::max(1, (int)(w->rect.right - w->rect.left)), ch = std::max(1, (int)(w->rect.bottom - w->rect.top));
    if (w->style & WS_THICKFRAME) {
      Req(n->toplevel, XDG_TOPLEVEL_SET_MIN_SIZE, NULL, 1, 1);
      Req(n->toplevel, XDG_TOPLEVEL_SET_MAX_SIZE, NULL, 0, 0);
    } else {
      Req(n->toplevel, XDG_TOPLEVEL_SET_MIN_SIZE, NULL, cw, ch);
      Req(n->toplevel, XDG_TOPLEVEL_SET_MAX_SIZE, NULL, cw, ch);
    }
  }

  void Map(Native* n) {
    Window* w = n->w;
    n->configured = false;
    n->xdgSurface = Req(wmBase, XDG_WM_BASE_GET_XDG_SURFACE, &xdg_surface_iface, NULL, n->surface);
    static void (*const sl[])(void) = {(void (*)(void))XdgConfigure};
    winport_wl_proxy_add_listener((wl_proxy*)n->xdgSurface, (void (**)(void))sl, n);
    Native* parent = IsPopup(w) ? PopupParent(w) : NULL;
    if (parent) {
      n->parent = parent;
      int cw = std::max(1, (int)(w->rect.right - w->rect.left)), ch = std::max(1, (int)(w->rect.bottom - w->rect.top));
      int pw = std::max(1, (int)(parent->w->rect.right - parent->w->rect.left));
      int ph = std::max(1, (int)(parent->w->rect.bottom - parent->w->rect.top));
      int dx = w->rect.left - parent->w->rect.left, dy = w->rect.top - parent->w->rect.top;
      // The anchor must lie in the parent; the offset takes it the rest of
      // the way.
      int ax = std::max(0, std::min(dx, pw - 1)), ay = std::max(0, std::min(dy, ph - 1));
      wl_proxy* pos = Req(wmBase, XDG_WM_BASE_CREATE_POSITIONER, &xdg_positioner_iface, NULL);
      Req(pos, XDG_POSITIONER_SET_SIZE, NULL, cw, ch);
      Req(pos, XDG_POSITIONER_SET_ANCHOR_RECT, NULL, ax, ay, 1, 1);
      Req(pos, XDG_POSITIONER_SET_ANCHOR, NULL, (uint32_t)ANCHOR_TOP_LEFT);
      Req(pos, XDG_POSITIONER_SET_GRAVITY, NULL, (uint32_t)GRAVITY_BOTTOM_RIGHT);
      Req(pos, XDG_POSITIONER_SET_CONSTRAINT_ADJUSTMENT, NULL, (uint32_t)(ADJUST_SLIDE_X | ADJUST_SLIDE_Y));
      Req(pos, XDG_POSITIONER_SET_OFFSET, NULL, dx - ax, dy - ay);
      n->popup = Req(n->xdgSurface, XDG_SURFACE_GET_POPUP, &xdg_popup_iface, NULL, parent->xdgSurface, pos);
      Done(pos, XDG_POSITIONER_DESTROY);
      static void (*const pl[])(void) = {(void (*)(void))PopupConfigure, (void (*)(void))PopupDone};
      winport_wl_proxy_add_listener((wl_proxy*)n->popup, (void (**)(void))pl, n);
    } else {
      n->parent = NULL;
      n->toplevel = Req(n->xdgSurface, XDG_SURFACE_GET_TOPLEVEL, &xdg_toplevel_iface, NULL);
      static void (*const tl[])(void) = {(void (*)(void))ToplevelConfigure, (void (*)(void))ToplevelClose};
      winport_wl_proxy_add_listener((wl_proxy*)n->toplevel, (void (**)(void))tl, n);
      Req(n->toplevel, XDG_TOPLEVEL_SET_TITLE, NULL, Utf8(w->text).c_str());
      Req(n->toplevel, XDG_TOPLEVEL_SET_APP_ID, NULL, appId.c_str());
      if (w->owner) {
        Native* o = NativeOf(w->owner->TopLevel());
        if (o && o->toplevel) Req(n->toplevel, XDG_TOPLEVEL_SET_PARENT, NULL, o->toplevel);
      }
      SizeLimits(n);
    }
    wl_surface_commit(n->surface);
    n->shown = true;
  }

  void Unmap(Native* n) {
    // A popup goes before the window it belongs to.
    for (Native* c : std::set<Native*>(natives))
      if (c->parent == n && c->shown) Unmap(c);
    if (n->popup) Done(n->popup, XDG_POPUP_DESTROY);
    if (n->toplevel) Done(n->toplevel, XDG_TOPLEVEL_DESTROY);
    if (n->xdgSurface) Done(n->xdgSurface, XDG_SURFACE_DESTROY);
    n->popup = n->toplevel = n->xdgSurface = NULL;
    n->parent = NULL;
    n->configured = false;
    n->shown = false;
    if (n->frame) {
      wl_callback_destroy(n->frame);
      n->frame = NULL;
    }
    wl_surface_attach(n->surface, NULL, 0, 0);
    wl_surface_commit(n->surface);
    if (ptrIn == n) ptrIn = NULL;
  }

  void Show(Window* w, bool show) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    Native* n = NativeOf(w);
    if (!n) {
      if (!show) return;
      n = Create(w);
    }
    if (show && !n->shown) Map(n);
    else if (!show && n->shown) Unmap(n);
    wl.wl_display_flush(dpy);
  }

  void FreeBuffer(Buffer& b) {
    if (b.buffer) wl_buffer_destroy(b.buffer);
    if (b.data) munmap(b.data, b.size);
    b = Buffer();
  }

  void Destroy(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n) return;
    if (n->shown) Unmap(n);
    for (Buffer& b : n->buffers) FreeBuffer(b);
    wl_surface_destroy(n->surface);
    natives.erase(n);
    if (kbdIn == n) kbdIn = NULL;
    if (ptrIn == n) ptrIn = NULL;
    w->native = NULL;
    delete n;
    if (dpy) wl.wl_display_flush(dpy);
  }

  void Moved(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !n->shown || placing) return;
    if (n->popup) {
      // A popup cannot move once shown: show it again where it now is.
      Unmap(n);
      Map(n);
    } else if (n->toplevel) {
      SizeLimits(n);
      wl_surface_commit(n->surface);
    }
    wl.wl_display_flush(dpy);
  }

  void TitleChanged(Window* w) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !n->toplevel) return;
    Req(n->toplevel, XDG_TOPLEVEL_SET_TITLE, NULL, Utf8(w->text).c_str());
    wl.wl_display_flush(dpy);
  }

  static void BufferRelease(void* data, wl_buffer*) {
    Buffer* b = (Buffer*)data;
    std::lock_guard<std::recursive_mutex> g(g_wl->lock);
    b->busy = false;
  }

  bool NewBuffer(Buffer& b, int width, int height) {
    FreeBuffer(b);
    size_t size = (size_t)width * height * 4;
    int fd = memfd_create("winport-surface", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)size) != 0) {
      close(fd);
      return false;
    }
    void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
      close(fd);
      return false;
    }
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    static const wl_buffer_listener bl = {BufferRelease};
    wl_buffer_add_listener(b.buffer, &bl, &b);
    b.data = data;
    b.size = size;
    b.width = width;
    b.height = height;
    return true;
  }

  static void FrameDone(void* data, wl_callback* cb, uint32_t) {
    Native* n = (Native*)data;
    UserGuard ug(UserLock());
    std::lock_guard<std::recursive_mutex> g(g_wl->lock);
    wl_callback_destroy(cb);
    if (!g_wl->natives.count(n) || n->frame != cb) return;
    n->frame = NULL;
    g_wl->Flush(n);
  }

  // Shows what has changed of the window, once the compositor is ready
  // for another frame. Called with the window manager's lock.
  void Flush(Native* n) {
    Surface* s = n->w->surface;
    if (!n->shown || !n->configured || n->frame || !s || s->width <= 0 || s->height <= 0) return;
    if (IsRectEmpty(&n->damage)) return;
    Buffer* b = NULL;
    for (Buffer& c : n->buffers)
      if (!c.busy) {
        b = &c;
        break;
      }
    if (!b) return;  // shown when a buffer comes back
    if (b->width != s->width || b->height != s->height) {
      if (!NewBuffer(*b, s->width, s->height)) return;
    }
    memcpy(b->data, s->pixels.data(), b->size);
    wl_surface_attach(n->surface, b->buffer, 0, 0);
    RECT d = n->damage;
    wl_surface_damage_buffer(n->surface, d.left, d.top, d.right - d.left, d.bottom - d.top);
    n->damage = RECT{0, 0, 0, 0};
    b->busy = true;
    n->frame = wl_surface_frame(n->surface);
    static const wl_callback_listener fl = {FrameDone};
    wl_callback_add_listener(n->frame, &fl, n);
    wl_surface_commit(n->surface);
  }

  void Present(Window* w, const RECT& r) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !dpy) return;
    Surface* s = w->surface;
    if (!s) return;
    RECT all = {0, 0, s->width, s->height}, d;
    if (!IntersectRect(&d, &r, &all)) return;
    // A buffer the size of an older surface is shown whole.
    for (Buffer& b : n->buffers)
      if (b.buffer && (b.width != s->width || b.height != s->height)) d = all;
    UnionRect(&n->damage, &n->damage, &d);
    // The other buffer is behind by what the last frame showed; copying
    // the whole surface keeps it simple.
    Flush(n);
    wl.wl_display_flush(dpy);
  }

  bool BeginMoveSize(Window* w, LRESULT hit) override {
    std::lock_guard<std::recursive_mutex> g(lock);
    Native* n = NativeOf(w);
    if (!n || !n->toplevel || !seat) return false;
    if (hit == HTCAPTION) {
      Req(n->toplevel, XDG_TOPLEVEL_MOVE, NULL, seat, lastSerial);
    } else {
      uint32_t edge;
      switch (hit) {
        case HTLEFT: edge = EDGE_LEFT; break;
        case HTRIGHT: edge = EDGE_RIGHT; break;
        case HTTOP: edge = EDGE_TOP; break;
        case HTBOTTOM: edge = EDGE_BOTTOM; break;
        case HTTOPLEFT: edge = EDGE_TOP_LEFT; break;
        case HTTOPRIGHT: edge = EDGE_TOP_RIGHT; break;
        case HTBOTTOMLEFT: edge = EDGE_BOTTOM_LEFT; break;
        case HTBOTTOMRIGHT: edge = EDGE_BOTTOM_RIGHT; break;
        default: return false;
      }
      Req(n->toplevel, XDG_TOPLEVEL_RESIZE, NULL, seat, lastSerial, edge);
    }
    // The compositor keeps the button's release.
    releaseAfterGrab = true;
    wl.wl_display_flush(dpy);
    return true;
  }

  // ---------------------------------------------------------------
  // The pointer's shape

  // Shows a cursor from the theme; false when the theme lacks it.
  bool ThemeCursor(const char* name) {
    if (!cursorTheme) return false;
    wl_cursor* c = wl.wl_cursor_theme_get_cursor(cursorTheme, name);
    if (!c && strcmp(name, "left_ptr") == 0) c = wl.wl_cursor_theme_get_cursor(cursorTheme, "default");
    if (!c || c->image_count == 0) return false;
    wl_cursor_image* img = c->images[0];
    wl_surface_attach(cursorSurface, wl.wl_cursor_image_get_buffer(img), 0, 0);
    wl_surface_damage(cursorSurface, 0, 0, img->width, img->height);
    wl_surface_commit(cursorSurface);
    cursorHotX = img->hotspot_x;
    cursorHotY = img->hotspot_y;
    return true;
  }

  // Shows a cursor from its picture.
  bool PictureCursor(const CursorPicture& p) {
    if (p.width <= 0 || p.height <= 0) return false;
    Buffer& b = cursorBuffer;
    FreeBuffer(b);
    size_t size = (size_t)p.width * p.height * 4;
    int fd = memfd_create("winport-cursor", MFD_CLOEXEC);
    if (fd < 0) return false;
    void* data = ftruncate(fd, (off_t)size) == 0 ? mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (data == MAP_FAILED) {
      close(fd);
      return false;
    }
    // The compositor wants alpha premultiplied.
    DWORD* out = (DWORD*)data;
    for (size_t i = 0; i < p.argb.size(); i++) {
      DWORD c = p.argb[i], a = c >> 24;
      out[i] = (a << 24) | ((((c >> 16) & 0xFF) * a / 255) << 16) | ((((c >> 8) & 0xFF) * a / 255) << 8) |
               ((c & 0xFF) * a / 255);
    }
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b.buffer = wl_shm_pool_create_buffer(pool, 0, p.width, p.height, p.width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    b.data = data;
    b.size = size;
    b.width = p.width;
    b.height = p.height;
    wl_surface_attach(cursorSurface, b.buffer, 0, 0);
    wl_surface_damage(cursorSurface, 0, 0, p.width, p.height);
    wl_surface_commit(cursorSurface);
    cursorHotX = p.hotspot.x;
    cursorHotY = p.hotspot.y;
    return true;
  }

  void ApplyCursor() {
    if (!pointer || !enterSerial) return;
    if (cursorHidden) wl_pointer_set_cursor(pointer, enterSerial, NULL, 0, 0);
    else wl_pointer_set_cursor(pointer, enterSerial, cursorSurface, cursorHotX, cursorHotY);
  }

  void SetCursor(HCURSOR h) override {
    CursorPicture p;
    bool have = h && CursorImage(h, &p);
    std::lock_guard<std::recursive_mutex> g(lock);
    if (!dpy) return;
    cursorHidden = !h;
    if (h) {
      const char* name = NULL;
      switch (have ? p.systemId : 32512) {
        case 32512: name = "left_ptr"; break;
        case 32513: name = "xterm"; break;
        case 32514: name = "watch"; break;
        case 32515: name = "crosshair"; break;
        case 32516: name = "sb_up_arrow"; break;
        case 32640: case 32646: name = "fleur"; break;
        case 32642: name = "bottom_right_corner"; break;
        case 32643: name = "bottom_left_corner"; break;
        case 32644: name = "sb_h_double_arrow"; break;
        case 32645: name = "sb_v_double_arrow"; break;
        case 32648: name = "crossed_circle"; break;
        case 32649: name = "hand2"; break;
        case 32650: name = "left_ptr_watch"; break;
        case 32651: name = "question_arrow"; break;
      }
      if (!(name && ThemeCursor(name)) && !(have && PictureCursor(p))) ThemeCursor("left_ptr");
    }
    ApplyCursor();
    wl.wl_display_flush(dpy);
  }

  // ---------------------------------------------------------------
  // The pointer

  static Native* Of(wl_surface* s) {
    if (!s) return NULL;
    Native* n = (Native*)wl_surface_get_user_data(s);
    std::lock_guard<std::recursive_mutex> g(g_wl->lock);
    return g_wl->natives.count(n) ? n : NULL;
  }

  WPARAM Keys() {
    WPARAM k = buttons;
    if (xkbState) {
      if (wl.xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) k |= MK_SHIFT;
      if (wl.xkb_state_mod_name_is_active(xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) k |= MK_CONTROL;
    }
    return k;
  }

  void EndGrab() {
    if (!releaseAfterGrab) return;
    releaseAfterGrab = false;
    buttons &= ~MK_LBUTTON;
    if (ptrIn) MouseInput(ptrIn->w, WM_LBUTTONUP, ptrAt.x, ptrAt.y, Keys());
  }

  static void PtrEnter(void* data, wl_pointer* p, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->enterSerial = serial;
    b->ptrIn = Of(s);
    b->ptrAt = {wl_fixed_to_int(x), wl_fixed_to_int(y)};
    b->ApplyCursor();
    b->EndGrab();
    if (b->ptrIn) MouseInput(b->ptrIn->w, WM_MOUSEMOVE, b->ptrAt.x, b->ptrAt.y, b->Keys());
  }
  static void PtrLeave(void* data, wl_pointer*, uint32_t, wl_surface*) { ((WaylandBackend*)data)->ptrIn = NULL; }
  static void PtrMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->ptrAt = {wl_fixed_to_int(x), wl_fixed_to_int(y)};
    b->EndGrab();
    if (b->ptrIn) MouseInput(b->ptrIn->w, WM_MOUSEMOVE, b->ptrAt.x, b->ptrAt.y, b->Keys());
  }
  static void PtrButton(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->EndGrab();
    bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down) b->lastSerial = serial;
    UINT msg;
    WPARAM flag;
    switch (button) {
      case 0x110: msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; flag = MK_LBUTTON; break;  // BTN_LEFT
      case 0x111: msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; flag = MK_RBUTTON; break;  // BTN_RIGHT
      case 0x112: msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; flag = MK_MBUTTON; break;  // BTN_MIDDLE
      default: return;
    }
    if (down) b->buttons |= flag;
    else b->buttons &= ~flag;
    if (b->ptrIn) MouseInput(b->ptrIn->w, msg, b->ptrAt.x, b->ptrAt.y, b->Keys());
  }
  void Wheel(uint32_t axis, int notches) {
    if (!ptrIn || !notches) return;
    // Wayland's vertical axis grows downward; the wheel's grows away from
    // the user.
    UINT msg = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? WM_MOUSEWHEEL : WM_MOUSEHWHEEL;
    int delta = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? -notches * WHEEL_DELTA : notches * WHEEL_DELTA;
    MouseInput(ptrIn->w, msg, ptrAt.x, ptrAt.y, MAKEWPARAM(Keys(), (WORD)(short)delta));
  }
  static void PtrAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    WaylandBackend* b = (WaylandBackend*)data;
    if (b->discreteAxis || axis > 1) return;
    // Without notches, ten units of scrolling make one.
    b->axisAcc[axis] += wl_fixed_to_int(value);
    int notches = b->axisAcc[axis] / 10;
    b->axisAcc[axis] -= notches * 10;
    b->Wheel(axis, notches);
  }
  static void PtrFrame(void* data, wl_pointer*) { ((WaylandBackend*)data)->discreteAxis = false; }
  static void PtrAxisSource(void*, wl_pointer*, uint32_t) {}
  static void PtrAxisStop(void* data, wl_pointer*, uint32_t, uint32_t axis) {
    if (axis <= 1) ((WaylandBackend*)data)->axisAcc[axis] = 0;
  }
  static void PtrAxisDiscrete(void* data, wl_pointer*, uint32_t axis, int32_t discrete) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->discreteAxis = true;
    b->Wheel(axis, discrete);
  }

  // ---------------------------------------------------------------
  // The keyboard

  static void KbdKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    WaylandBackend* b = (WaylandBackend*)data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !b->xkbCtx) {
      close(fd);
      return;
    }
    char* text = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (text == MAP_FAILED) return;
    xkb_keymap* km =
        b->wl.xkb_keymap_new_from_string(b->xkbCtx, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    if (!km) return;
    if (b->xkbState) b->wl.xkb_state_unref(b->xkbState);
    if (b->keymap) b->wl.xkb_keymap_unref(b->keymap);
    b->keymap = km;
    b->xkbState = b->wl.xkb_state_new(km);
  }
  static void KbdEnter(void* data, wl_keyboard*, uint32_t serial, wl_surface* s, wl_array*) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->lastSerial = serial;
    Native* n = Of(s);
    b->kbdIn = n;
    if (n && n->toplevel && GetActiveWindow() != n->w->hwnd && IsWindowEnabled(n->w->hwnd))
      SetActiveWindow(n->w->hwnd);
  }
  static void KbdLeave(void* data, wl_keyboard*, uint32_t, wl_surface*) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->kbdIn = NULL;
    b->repeatKey = 0;
  }
  void Key(uint32_t key, bool down) {
    if (!xkbState || !keymap) return;
    // Keys go to the window with the focus, whichever surface has the
    // keyboard; a popup without a grab leaves it with its toplevel.
    Native* n = kbdIn;
    if (!n) return;
    xkb_keycode_t code = key + 8;
    const xkb_keysym_t* syms = NULL;
    xkb_keysym_t base = 0;
    if (wl.xkb_keymap_key_get_syms_by_level(keymap, code, 0, 0, &syms) > 0) base = syms[0];
    xkb_keysym_t sym = wl.xkb_state_key_get_one_sym(xkbState, code);
    bool ext;
    UINT vk = VkOf(base, &ext);
    if (!vk) vk = VkOf(sym, &ext);
    LPARAM lp = 1 | ((LPARAM)(key & 0xFF) << 16) | (ext ? (1 << 24) : 0);
    wchar_t ch = 0;
    if (down) {
      uint32_t u = wl.xkb_state_key_get_utf32(xkbState, code);
      if (sym == XKB_KEY_KP_Enter) u = '\r';
      if (u && u != 0x7F && u < 0x110000) ch = (wchar_t)u;
    }
    // The key's text comes from xkbcommon, with its layout, rather than
    // from TranslateMessage (bit 25 tells it so).
    if (ch) lp |= 0x02000000;
    if (vk) KeyInput(n->w, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
    if (ch) CharInput(n->w, ch);
  }
  static void KbdKey(void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
    WaylandBackend* b = (WaylandBackend*)data;
    bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (down) b->lastSerial = serial;
    b->Key(key, down);
    if (down && b->keymap && b->repeatRate > 0 && b->wl.xkb_keymap_key_repeats(b->keymap, key + 8)) {
      b->repeatKey = key;
      b->repeatNext = Millis() + b->repeatDelay;
    } else if (!down && key == b->repeatKey) {
      b->repeatKey = 0;
    }
  }
  static void KbdModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked,
                           uint32_t group) {
    WaylandBackend* b = (WaylandBackend*)data;
    if (b->xkbState) b->wl.xkb_state_update_mask(b->xkbState, depressed, latched, locked, 0, 0, group);
  }
  static void KbdRepeat(void* data, wl_keyboard*, int32_t rate, int32_t delay) {
    WaylandBackend* b = (WaylandBackend*)data;
    b->repeatRate = rate;
    b->repeatDelay = delay;
  }

  void Repeat() {
    if (!repeatKey) return;
    ULONGLONG now = Millis();
    if (now < repeatNext) return;
    Key(repeatKey, true);
    repeatNext = now + std::max(1, 1000 / std::max(1, repeatRate));
  }

  // ---------------------------------------------------------------
  // Waiting

  void Check() {
    int err = wl.wl_display_get_error(dpy);
    if (err) {
      fprintf(stderr, "winport: lost the Wayland compositor (error %d)\n", err);
      _exit(1);
    }
  }

  void Wait(DWORD timeout) override {
    if (!dpy) return;
    wl.wl_display_dispatch_pending(dpy);
    while (wl.wl_display_prepare_read(dpy) != 0) wl.wl_display_dispatch_pending(dpy);
    wl.wl_display_flush(dpy);
    pollfd fds[2];
    fds[0].fd = wl.wl_display_get_fd(dpy);
    fds[0].events = POLLIN;
    fds[1].fd = wakePipe[0];
    fds[1].events = POLLIN;
    long long ms = timeout == INFINITE ? -1 : (long long)timeout;
    if (repeatKey) {
      ULONGLONG now = Millis();
      long long left = repeatNext > now ? (long long)(repeatNext - now) : 0;
      if (ms < 0 || left < ms) ms = left;
    }
    int r = poll(fds, 2, (int)std::min<long long>(ms, 0x7FFFFFFF));
    if (r > 0 && (fds[0].revents & POLLIN)) wl.wl_display_read_events(dpy);
    else wl.wl_display_cancel_read(dpy);
    if (r > 0 && (fds[1].revents & POLLIN)) {
      char b[64];
      while (read(wakePipe[0], b, sizeof b) > 0) {
      }
    }
    wl.wl_display_dispatch_pending(dpy);
    Repeat();
    wl.wl_display_flush(dpy);
    Check();
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

Backend* NewWaylandBackend() {
  WaylandBackend* b = new WaylandBackend;
  if (b->Start()) return b;
  delete b;
  return NULL;
}

}  // namespace winport

#else

namespace winport {
Backend* NewWaylandBackend() { return NULL; }
}  // namespace winport

#endif
