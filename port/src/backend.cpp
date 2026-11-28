// backend.cpp - choosing the backend that shows top-level windows, and
// the headless one, which shows nothing.
//
// WINPORT_BACKEND names the backend: "cocoa" shows windows on macOS,
// "wayland" through the Wayland compositor, "x11" on the X display, and
// "headless" keeps every window in memory, which suits tests and
// programs run without a display. Without it macOS uses Cocoa; elsewhere
// the compositor is used when WAYLAND_DISPLAY names one, then the X
// display when DISPLAY names one, and headless otherwise. On Haiku the
// Haiku backend (backend_haiku.cpp) shows the windows. On the classic
// Mac OS the Toolbox shows the windows, and on 32-bit DOS the DOS backend
// (backend_dos.cpp). WINPORT_SCREEN=WIDTHxHEIGHT sets the headless
// screen's size, and on DOS the screen mode's.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

namespace winport {

namespace {

struct HeadlessBackend : Backend {
  std::mutex lock;
  std::condition_variable cv;
  bool woken = false;
  SIZE size = {1024, 768};
  double scale = 1;

  // WINPORT_SCREEN gives the screen's size ("1920x1080"), and
  // WINPORT_SCALE the desktop's scale ("2", "1.5"), for tests.
  HeadlessBackend() {
    const char* s = getenv("WINPORT_SCREEN");
    int w, h;
    if (s && sscanf(s, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
      size.cx = w;
      size.cy = h;
    }
    const char* k = getenv("WINPORT_SCALE");
    double v;
    if (k && sscanf(k, "%lf", &v) == 1 && v >= 1 && v <= 10) scale = v;
  }
  const char* Name() override { return "headless"; }
  SIZE ScreenSize() override { return size; }
  // WINPORT_MONITORS gives more monitors than the one screen, for tests:
  // "1024x768+0+0,800x600+1024+0", each one's size and place, the
  // primary one first. It is read each time.
  std::vector<Monitor> Monitors() override {
    std::vector<Monitor> m;
    const char* s = getenv("WINPORT_MONITORS");
    while (s && *s) {
      int w, h, x, y, n = 0;
      if (sscanf(s, "%dx%d%d%d%n", &w, &h, &x, &y, &n) != 4 || w <= 0 || h <= 0) break;
      RECT r = {x, y, x + w, y + h};
      m.push_back(Monitor{r, r, scale});
      s += n;
      if (*s != ',') break;
      s++;
    }
    if (m.empty()) return Backend::Monitors();
    return m;
  }
  double DesktopScale() override { return scale; }
  void Wait(DWORD timeout) override {
    std::unique_lock<std::mutex> g(lock);
    if (timeout == INFINITE)
      cv.wait(g, [this] { return woken; });
    else
      cv.wait_for(g, std::chrono::milliseconds(timeout), [this] { return woken; });
    woken = false;
  }
  void Wake() override {
    std::lock_guard<std::mutex> g(lock);
    woken = true;
    cv.notify_all();
  }
};

}  // namespace

Backend* NewHeadlessBackend() { return new HeadlessBackend; }

#if defined(__MSDOS__) && defined(__i386__)
// 32-bit DOS: the DOS backend shows the windows on a VESA or VGA screen,
// unless WINPORT_BACKEND is "headless".
Backend* GetBackend() {
  static Backend* b = [] {
    const char* name = getenv("WINPORT_BACKEND");
    Backend* b = NULL;
    if (!name || strcmp(name, "headless") != 0) b = NewDosBackend();
    return b ? b : NewHeadlessBackend();
  }();
  return b;
}
#elif defined(__HAIKU__)
// Haiku: app_server shows the windows (backend_haiku.cpp), unless
// WINPORT_BACKEND is "headless" or there is no app_server.
Backend* GetBackend() {
  static Backend* b = [] {
    const char* name = getenv("WINPORT_BACKEND");
    Backend* b = NULL;
    if (!name || strcmp(name, "headless") != 0) b = NewHaikuBackend();
    return b ? b : NewHeadlessBackend();
  }();
  return b;
}
#elif defined(macintosh)
// The classic Mac OS: the Toolbox shows the windows, unless
// WINPORT_BACKEND is "headless" or the Toolbox cannot.
Backend* GetBackend() {
  static Backend* b = [] {
    const char* name = getenv("WINPORT_BACKEND");
    Backend* b = NULL;
    if (!name || strcmp(name, "headless") != 0) b = NewMacBackend();
    return b ? b : NewHeadlessBackend();
  }();
  return b;
}
#else
Backend* GetBackend() {
  static Backend* b = [] {
    const char* name = getenv("WINPORT_BACKEND");
    Backend* b = NULL;
    if (name && *name) {
      if (strcmp(name, "x11") == 0) {
        b = NewX11Backend();
        if (!b) fprintf(stderr, "winport: cannot open the X display; windows stay in memory\n");
      } else if (strcmp(name, "cocoa") == 0) {
        b = NewCocoaBackend();
        if (!b) fprintf(stderr, "winport: Cocoa is only on macOS; windows stay in memory\n");
      } else if (strcmp(name, "wayland") == 0) {
        b = NewWaylandBackend();
        if (!b) fprintf(stderr, "winport: cannot reach the Wayland compositor; windows stay in memory\n");
      } else if (strcmp(name, "headless") != 0) {
        fprintf(stderr, "winport: no backend named %s; windows stay in memory\n", name);
      }
    } else {
#ifdef __APPLE__
      b = NewCocoaBackend();
#endif
      const char* wl = getenv("WAYLAND_DISPLAY");
      const char* x = getenv("DISPLAY");
      if (!b && wl && *wl) b = NewWaylandBackend();
      if (!b && x && *x) b = NewX11Backend();
    }
    return b ? b : NewHeadlessBackend();
  }();
  return b;
}
#endif

// The standard control classes register here as they are written.
void RegisterSystemClasses() {
  RegisterMenuClass();
  RegisterDialogClass();
  RegisterButtonClasses();
  RegisterEditClass();
  RegisterRichEditClasses();
  RegisterListClasses();
  RegisterMdiClass();
  RegisterCommonControls();
  RegisterImeClass();
}

}  // namespace winport
