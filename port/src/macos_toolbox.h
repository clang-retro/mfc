// macos_toolbox.h - what the two halves of the classic Mac OS backend
// share. backend_macos.cpp is the window manager's half, with the Win32
// headers; macos_toolbox.cpp is the Toolbox half, with the Mac headers.
// The two sets of headers cannot be in one file: both name ShowWindow,
// MoveWindow, DrawText, Rect, Point and more.
//
// Coordinates are those of the program's screen: the Mac's global
// coordinates below the menu bar.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <stdint.h>

#include <string>

namespace winport {
namespace mactb {

// Modifier keys, as the Event Manager gives them.
enum : unsigned {
  kCommand = 0x0100,
  kShift = 0x0200,
  kCapsLock = 0x0400,
  kOption = 0x0800,
  kControl = 0x1000,
};

// Kinds of mouse input.
enum { kMove, kDown, kUp };

// Starts the Toolbox if the program has not, and tells whether the
// backend can show windows here (Color QuickDraw or not).
bool Start();
// The whole screen, with the menu bar.
void ScreenSize(int* width, int* height);
// The part of the screen that windows can use: the screen below the menu
// bar.
void WorkArea(int* x, int* y, int* width, int* height);
// Gives the POSIX path of the Fonts folder in the System Folder, or
// false when the system has none (before System 7.1).
bool FontsFolder(std::string* path);
// Where the pointer is now, from the top-left corner of the screen.
void MousePosition(int* x, int* y);
// A window with no frame of its own (the layer draws the frame), hidden.
// owner comes back in the On functions.
void* Create(void* owner, int x, int y, int width, int height);
// Shows or hides a window; a shown window comes to the front unless
// behind is set.
void Show(void* win, bool show, bool behind);
void Destroy(void* win);
void SetFrame(void* win, int x, int y, int width, int height);
// The title, in Mac Roman, for the Application menu's window list.
void SetTitle(void* win, const char* macRoman);
// Copies the rectangle x, y, w, h of 0x00RRGGBB pixels (width by height,
// top row first) to the same place in the window.
void Draw(void* win, const uint32_t* pixels, int width, int height, int x, int y, int w, int h);
// Handles events for up to ms milliseconds (-1: until there is one),
// and returns early when *woken becomes true. Other threads run while it
// waits.
void Wait(long ms, volatile bool* woken);
// Gives a character typed in Mac Roman as Unicode.
uint16_t FromMacRoman(unsigned char c);

// Implemented by backend_macos.cpp. Positions are in the window.
void OnMouse(void* owner, int kind, int x, int y, unsigned mods);
void OnKey(void* owner, bool down, unsigned keyCode, unsigned charCode, unsigned mods);
void OnModifiers(void* owner, unsigned was, unsigned now);
void OnUpdate(void* owner);
void OnActivate(void* owner, bool active);
void OnQuit();
// The window that keys go to: the program's active one.
void* KeyOwner();

}  // namespace mactb
}  // namespace winport
