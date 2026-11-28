// haiku.h - between the two halves of the Haiku backend:
// backend_haiku_be.cpp, which uses the Haiku API (BApplication, BWindow,
// BView, BBitmap) and sees none of the Windows headers, and
// backend_haiku.cpp, which uses the window manager and sees none of the
// Haiku headers. Their types and macros clash, so neither includes the
// other's headers.
//
// Coordinates are screen pixels from the top left of the screen, as in
// Windows. A window has no border from app_server: the window manager
// draws the frame and the caption into the window's pixels.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_HAIKU_H
#define _WINPORT_HAIKU_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace winport {
namespace haiku {

// A window, known to the other half only through this pointer and the
// owner pointer given to Create.
struct Win;

// The modifier keys that are held, as one set of flags. Haiku's Command
// key (the key marked Alt on a PC keyboard) is Alt, and its Option key
// (the Windows key) is kWin, as their places on the keyboard say.
enum { kShift = 1, kControl = 2, kAlt = 4, kWin = 8, kCapsLock = 16 };

// Input that the windows receive. The window threads put it in a queue,
// and Take gives it to the thread that waits for input.
struct Event {
  enum Kind { kMouseDown, kMouseUp, kMouseMove, kWheel, kKeyDown, kKeyUp, kActivated, kClose, kQuit } kind;
  void* owner = NULL;  // the window's owner, or NULL for kQuit
  int x = 0, y = 0;    // the mouse, in the window
  int button = 0;      // 0 left, 1 right, 2 middle
  float wheelX = 0, wheelY = 0;  // in notches: down and right are positive
  unsigned mods = 0;   // the k flags above
  unsigned key = 0;    // Haiku's key code (the key's place on the keyboard)
  uint32_t ch = 0;     // the character the key types with its modifiers, or 0
  uint32_t raw = 0;    // the character the key types without modifiers, or 0
  bool active = false; // kActivated: the window became active (true) or not
};

// The path of the program's image (the executable), from the kernel;
// false when there is none. kernel_module.cpp uses it.
bool ImagePath(std::string* path);

// Called on the window manager's side (backend_haiku.cpp).
// Start makes the BApplication and runs it on a thread of its own. It
// returns false when there is no app_server to connect to.
bool Start();
void ScreenSize(int* width, int* height);
// Makes a window. A popup (a menu, a tooltip, a drop-down list) floats
// over the other windows and does not take the focus.
Win* Create(void* owner, int x, int y, int width, int height, bool popup);
void Destroy(Win* w);
void Show(Win* w, bool show, bool activate);
void SetFrame(Win* w, int x, int y, int width, int height);
void SetTitle(Win* w, const std::string& utf8);
// Copies a rectangle of the window's pixels (0x00RRGGBB, top row first,
// stride pixels to a row) and shows it. The rectangle is in the window.
void Update(Win* w, const uint32_t* pixels, int width, int height, int stride, int x, int y, int cx, int cy);
// The pointer's shape: a system cursor by its IDC_ number, or the
// picture (ARGB, not premultiplied, top row first); hidden hides it.
void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY);
// The system clipboard: a number that changes when its contents change,
// its text, and putting text there (which gives the new number).
long ClipboardCount();
bool ClipboardText(std::string* utf8);
long SetClipboardText(const std::string& utf8);
// Waits up to timeout ms (-1 for no limit) for input or Wake, and gives
// the input that came.
std::vector<Event> Take(long timeout);
void Wake();

}  // namespace haiku
}  // namespace winport

#endif
