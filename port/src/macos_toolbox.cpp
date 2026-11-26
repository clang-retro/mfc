// macos_toolbox.cpp - the Toolbox half of the classic Mac OS backend
// (see macos_toolbox.h and backend_macos.cpp): the windows, the drawing
// with CopyBits and the event loop.
//
// A window has no frame of the Mac's own (plainDBox): the layer draws
// the Windows frame, caption and menu bar into the window's surface, and
// moves and sizes the window itself. The surface's 0x00RRGGBB pixels
// are a 32-bit direct PixMap for Color QuickDraw. Without 32-bit Color
// QuickDraw, they become a 1-bit BitMap first.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(macintosh)
#include "macos_toolbox.h"

#include <pthread.h>  // sched_yield
#include <stdlib.h>
#include <string.h>

// The Toolbox headers have no C++ linkage of their own: on PowerPC the
// functions are C symbols of InterfaceLib.
extern "C" {
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <MacWindows.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <SegLoad.h>
#include <TextEdit.h>
}

namespace winport {
namespace mactb {

namespace {

QDGlobals g_qd;           // used only when this backend starts the Toolbox
bool g_color;             // 32-bit Color QuickDraw is there
Rect g_screen;            // the whole screen, in global coordinates
int g_menuBar;            // the menu bar's height
WindowPtr g_tracking;     // the window the mouse went down in
Point g_mouse = {-1, -1}; // the mouse's last global position
unsigned g_mods;          // the modifiers of the last event
CTabHandle g_directTable; // the colour table a direct PixMap needs
MenuHandle g_appleMenu;    // the Apple menu, when this backend made it

// Gives the backend's owner of a window, or NULL for another one (a
// desk accessory's).
void* OwnerOf(WindowPtr w) {
  if (!w) return NULL;
  return (void*)GetWRefCon(w);
}

// The Mac rectangle for a window of the program's screen.
Rect MacRect(int x, int y, int width, int height) {
  Rect r;
  r.left = (short)x;
  r.top = (short)(y + g_menuBar);
  r.right = (short)(x + width);
  r.bottom = (short)(y + g_menuBar + height);
  return r;
}

// Converts a run of 0x00RRGGBB pixels to 1-bit black and white (1 is
// black), with an ordered dither, into a BitMap row.
void ToMono(const uint32_t* in, int n, int y, int x0, unsigned char* out) {
  static const unsigned char kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  memset(out, 0, (size_t)(n + 7) / 8);
  for (int i = 0; i < n; i++) {
    uint32_t p = in[i];
    unsigned lum = (((p >> 16) & 0xFF) * 77 + ((p >> 8) & 0xFF) * 151 + (p & 0xFF) * 28) >> 8;
    unsigned level = (lum * 17) >> 8;  // 0 to 16
    if (level <= kBayer[y & 3][(x0 + i) & 3]) out[i >> 3] |= (unsigned char)(0x80 >> (i & 7));
  }
}

// Hands a mouse position (global) to the window it is over, or to the
// window the mouse went down in.
void MouseAt(Point global, int kind) {
  WindowPtr w = g_tracking;
  if (!w) FindWindow(global, &w);
  void* owner = OwnerOf(w);
  if (!owner) return;
  GrafPtr was;
  GetPort(&was);
  SetPort((GrafPtr)w);
  Point local = global;
  GlobalToLocal(&local);
  SetPort(was);
  OnMouse(owner, kind, local.h, local.v, g_mods);
}

// Tells backend_macos.cpp about modifier keys that went down or up.
void Modifiers(unsigned now) {
  now &= kCommand | kShift | kCapsLock | kOption | kControl;
  if (now != g_mods) {
    unsigned was = g_mods;
    g_mods = now;
    OnModifiers(KeyOwner(), was, now);
  }
}

enum { kAppleMenu = 128, kFileMenu = 129, kExitItem = 1 };

// Puts the Apple menu (with the desk accessories) and a menu with the
// program's name and Exit in the Mac menu bar. The program's own menus
// stay in its windows.
void MakeMenuBar() {
  static const unsigned char apple[] = {1, 0x14};  // the Apple symbol
  static const unsigned char file[] = {4, 'F', 'i', 'l', 'e'};
  static const unsigned char exitItem[] = {4, 'E', 'x', 'i', 't'};
  // The program's name (CurApName), or "File" if it is not there.
  const unsigned char* name = LMGetCurApName();
  if (name[0] == 0 || name[0] > 31) name = file;
  g_appleMenu = NewMenu(kAppleMenu, apple);
  if (g_appleMenu) {
    AppendResMenu(g_appleMenu, 'DRVR');
    InsertMenu(g_appleMenu, 0);
  }
  if (MenuHandle m = NewMenu(kFileMenu, name)) {
    AppendMenu(m, exitItem);
    InsertMenu(m, 0);
  }
  DrawMenuBar();
}

// Does what the user chose in the Mac menu bar (MenuSelect's result).
void Choose(long choice) {
  short menu = (short)(choice >> 16);
  short item = (short)(choice & 0xFFFF);
  if (menu == kAppleMenu && g_appleMenu) {
    Str255 name;
    GetMenuItemText(g_appleMenu, item, name);
    OpenDeskAcc(name);
  } else if (menu == kFileMenu && item == kExitItem) {
    OnQuit();
  }
  HiliteMenu(0);
}

// Handles one event.
void Handle(EventRecord& ev) {
  WindowPtr w = NULL;
  Modifiers(ev.modifiers);
  switch (ev.what) {
    case mouseDown: {
      short part = FindWindow(ev.where, &w);
      if (part == inSysWindow) {
        SystemClick(&ev, w);
      } else if (part == inMenuBar) {
        Choose(MenuSelect(ev.where));
      } else if (OwnerOf(w)) {
        g_tracking = w;
        g_mouse = ev.where;
        MouseAt(ev.where, kDown);
      }
      break;
    }
    case mouseUp:
      if (g_tracking) {
        MouseAt(ev.where, kUp);
        g_tracking = NULL;
      }
      break;
    case keyDown:
    case autoKey:
    case keyUp:
      OnKey(KeyOwner(), ev.what != keyUp, (unsigned)(ev.message & keyCodeMask) >> 8,
            (unsigned)(ev.message & charCodeMask), ev.modifiers);
      break;
    case updateEvt: {
      w = (WindowPtr)ev.message;
      BeginUpdate(w);
      if (void* owner = OwnerOf(w)) OnUpdate(owner);
      EndUpdate(w);
      break;
    }
    case activateEvt:
      if (void* owner = OwnerOf((WindowPtr)ev.message)) OnActivate(owner, (ev.modifiers & 1) != 0);
      break;
    case kHighLevelEvent:
      // The Finder's Quit Application (System 7): ask the program to quit.
      if (ev.message == 'aevt' && *(long*)&ev.where == 'quit') OnQuit();
      break;
  }
}

}  // namespace
}  // namespace mactb
}  // namespace winport

// Set to a value other than 0 by a program that starts the Toolbox
// (InitGraf to InitCursor) itself, before the layer shows a window.
extern "C" {
int winport_mac_toolbox_started = 0;
}

namespace winport {
namespace mactb {

bool Start() {
  // The low memory (MBarHeight, CurrentA5) does not tell if this program
  // started the Toolbox: the Finder sets them before the program runs.
  GrafPtr port = NULL;
  if (!winport_mac_toolbox_started) {
    winport_mac_toolbox_started = 1;
    InitGraf(&g_qd.thePort);
    InitFonts();
    FlushEvents(everyEvent, 0);
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MakeMenuBar();
  }
  GetPort(&port);
  long qd = 0;
  g_color = Gestalt(gestaltQuickdrawVersion, &qd) == noErr && qd >= gestalt32BitQD;
  g_screen = (**GetGrayRgn()).rgnBBox;
  g_menuBar = LMGetMBarHeight();
  g_screen.top = 0;
  if (g_color) {
    // A direct PixMap's table is not read, but CopyBits wants one.
    g_directTable = (CTabHandle)NewHandleClear(sizeof(ColorTable));
    if (g_directTable) (**g_directTable).ctSeed = 32;
  }
  return true;
}

// Gives where the pointer is now, in the coordinates of MacRect.
void MousePosition(int* x, int* y) {
  Point p;
  GetMouse(&p);
  LocalToGlobal(&p);
  *x = p.h;
  *y = p.v - g_menuBar;
}

void ScreenSize(int* width, int* height) {
  *width = g_screen.right - g_screen.left;
  *height = g_screen.bottom - g_screen.top - g_menuBar;
}

void* Create(void* owner, int x, int y, int width, int height) {
  Rect r = MacRect(x, y, width, height);
  WindowPtr w = g_color ? NewCWindow(NULL, &r, (ConstStringPtr) "\p", false, plainDBox, (WindowPtr)-1, false, (long)owner)
                        : NewWindow(NULL, &r, (ConstStringPtr) "\p", false, plainDBox, (WindowPtr)-1, false, (long)owner);
  return w;
}

void Show(void* win, bool show, bool behind) {
  WindowPtr w = (WindowPtr)win;
  if (!show) {
    HideWindow(w);
    return;
  }
  ShowWindow(w);
  if (!behind) SelectWindow(w);
}

void Destroy(void* win) {
  if (g_tracking == (WindowPtr)win) g_tracking = NULL;
  DisposeWindow((WindowPtr)win);
}

void SetFrame(void* win, int x, int y, int width, int height) {
  WindowPtr w = (WindowPtr)win;
  Rect r = MacRect(x, y, width, height);
  MoveWindow(w, r.left, r.top, false);
  SizeWindow(w, (short)width, (short)height, true);
}

void SetTitle(void* win, const char* macRoman) {
  Str255 t;
  size_t n = strlen(macRoman);
  if (n > 255) n = 255;
  t[0] = (unsigned char)n;
  memcpy(t + 1, macRoman, n);
  SetWTitle((WindowPtr)win, t);
}

void Draw(void* win, const uint32_t* pixels, int width, int height, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  WindowPtr wp = (WindowPtr)win;
  GrafPtr was;
  GetPort(&was);
  SetPort((GrafPtr)wp);
  ForeColor(blackColor);
  BackColor(whiteColor);
  Rect dst = {(short)y, (short)x, (short)(y + h), (short)(x + w)};
  if (g_color && width * 4 < 0x4000) {
    PixMap pm;
    memset(&pm, 0, sizeof pm);
    pm.baseAddr = (Ptr)pixels;
    pm.rowBytes = (short)(0x8000 | (width * 4));
    pm.bounds.right = (short)width;
    pm.bounds.bottom = (short)height;
    pm.hRes = pm.vRes = 72L << 16;
    pm.pixelType = RGBDirect;
    pm.pixelSize = 32;
    pm.cmpCount = 3;
    pm.cmpSize = 8;
    pm.pmTable = g_directTable;
    CopyBits((BitMap*)&pm, &((GrafPtr)wp)->portBits, &dst, &dst, srcCopy, NULL);
  } else {
    // One row at a time through a 1-bit BitMap.
    int rowBytes = ((w + 15) / 16) * 2;
    unsigned char* row = (unsigned char*)malloc((size_t)rowBytes);
    if (row) {
      for (int j = 0; j < h; j++) {
        ToMono(pixels + (size_t)(y + j) * width + x, w, y + j, x, row);
        BitMap bm;
        bm.baseAddr = (Ptr)row;
        bm.rowBytes = (short)rowBytes;
        bm.bounds.left = (short)x;
        bm.bounds.top = (short)(y + j);
        bm.bounds.right = (short)(x + rowBytes * 8);
        bm.bounds.bottom = (short)(y + j + 1);
        Rect line = {(short)(y + j), (short)x, (short)(y + j + 1), (short)(x + w)};
        CopyBits(&bm, &((GrafPtr)wp)->portBits, &line, &line, srcCopy, NULL);
      }
      free(row);
    }
  }
  SetPort(was);
}

void Wait(long ms, volatile bool* woken) {
  unsigned long end = TickCount() + (ms < 0 ? 0 : (unsigned long)(ms * 60 + 999) / 1000);
  for (;;) {
    EventRecord ev;
    // Let the program's other threads run first: a stream of events
    // (each one returns at once) must not stop them.
    sched_yield();
    if (*woken) return;
    // Sleep a tick at most, so that the program's other threads run.
    if (WaitNextEvent(everyEvent, &ev, 1, NULL)) {
      Handle(ev);
      return;
    }
    // The Event Manager gives no event for a mouse that moves.
    Point now;
    GetMouse(&now);
    LocalToGlobal(&now);
    if (now.h != g_mouse.h || now.v != g_mouse.v) {
      g_mouse = now;
      MouseAt(now, kMove);
      return;
    }
    Modifiers(ev.modifiers);
    if (*woken) return;
    if (ms >= 0 && TickCount() >= end) return;
    sched_yield();
    if (*woken) return;
  }
}

uint16_t FromMacRoman(unsigned char c) {
  static const uint16_t kHigh[128] = {
      0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5,
      0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4,
      0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6,
      0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265,
      0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF,
      0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5,
      0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044,
      0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
      0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9,
      0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
  };
  return c < 0x80 ? c : kHigh[c - 0x80];
}

}  // namespace mactb
}  // namespace winport
#endif
