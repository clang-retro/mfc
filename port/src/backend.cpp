// backend.cpp - choosing the backend that shows top-level windows, and
// the headless one, which shows nothing.
//
// WINPORT_BACKEND names the backend: "wayland" shows windows through
// the Wayland compositor, "x11" on the X display, and "headless" keeps
// every window in memory, which suits tests and programs run without a
// display. Without it the compositor is used when WAYLAND_DISPLAY names
// one, then the X display when DISPLAY names one, and headless otherwise. WINPORT_SCREEN=WIDTHxHEIGHT sets the headless screen's size.
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

  HeadlessBackend() {
    const char* s = getenv("WINPORT_SCREEN");
    int w, h;
    if (s && sscanf(s, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
      size.cx = w;
      size.cy = h;
    }
  }
  const char* Name() override { return "headless"; }
  SIZE ScreenSize() override { return size; }
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

Backend* GetBackend() {
  static Backend* b = [] {
    const char* name = getenv("WINPORT_BACKEND");
    Backend* b = NULL;
    if (name && *name) {
      if (strcmp(name, "x11") == 0) {
        b = NewX11Backend();
        if (!b) fprintf(stderr, "winport: cannot open the X display; windows stay in memory\n");
      } else if (strcmp(name, "wayland") == 0) {
        b = NewWaylandBackend();
        if (!b) fprintf(stderr, "winport: cannot reach the Wayland compositor; windows stay in memory\n");
      } else if (strcmp(name, "headless") != 0) {
        fprintf(stderr, "winport: no backend named %s; windows stay in memory\n", name);
      }
    } else {
      const char* wl = getenv("WAYLAND_DISPLAY");
      const char* x = getenv("DISPLAY");
      if (wl && *wl) b = NewWaylandBackend();
      if (!b && x && *x) b = NewX11Backend();
    }
    return b ? b : NewHeadlessBackend();
  }();
  return b;
}

// The standard control classes register here as they are written.
void RegisterSystemClasses() {
  RegisterMenuClass();
  RegisterDialogClass();
  RegisterButtonClasses();
  RegisterEditClass();
  RegisterListClasses();
  RegisterMdiClass();
  RegisterCommonControls();
}

}  // namespace winport
