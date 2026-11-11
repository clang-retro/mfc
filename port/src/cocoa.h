// cocoa.h - between the Cocoa backend's two halves: backend_cocoa.mm,
// which uses AppKit and sees none of the Windows headers, and
// backend_cocoa.cpp, which uses the window manager and sees none of the
// Mac headers. Their types clash (BOOL, for one), so neither includes
// the other's.
//
// Coordinates are Windows' screen coordinates: from the top left of the
// screen with the menu bar, growing down.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_COCOA_H
#define _WINPORT_COCOA_H

#include <stddef.h>
#include <stdint.h>

namespace winport {
namespace cocoa {

// Called on the backend's side (backend_cocoa.mm). A window is known to
// the other side by its owner, an opaque pointer.
struct Win;
bool Start();
void ScreenSize(int* width, int* height);
Win* Create(void* owner, int x, int y, int width, int height, bool popup);
void Destroy(Win* w);
enum { kHide, kShow, kShowKey };
void Show(Win* w, int how);
void SetFrame(Win* w, int x, int y, int width, int height);
void SetTitle(Win* w, const uint16_t* text, size_t length);
void Invalidate(Win* w, int x, int y, int width, int height);  // in the window
// The pointer's shape: a system cursor by its IDC_ number, or else the
// picture (ARGB, not premultiplied, top row first); hidden hides it.
void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY);
// Takes and handles events for up to timeout ms (-1 for no limit), or
// until Wake.
void Wait(long timeout);
void Wake();

// Called on the window manager's side (backend_cocoa.cpp), with the
// owner given to Create.
enum { kShift = 1, kControl = 2, kOption = 4, kCommand = 8 };
enum { kMove, kDown, kUp };
// button: 0 left, 1 right, 2 middle.
void OnMouse(void* owner, int kind, int button, int x, int y, unsigned mods);
// In lines: up and right are positive.
void OnWheel(void* owner, int x, int y, double up, double right, unsigned mods);
enum { kKeyDown, kKeyUp, kModifiers };
// owner: the key window's, or NULL when it is not ours. text and plain
// are the characters the key types with and without its modifiers.
// Returns whether the key was taken.
bool OnKey(void* owner, int type, unsigned code, unsigned mods, const uint16_t* text, size_t textLength,
           const uint16_t* plain, size_t plainLength);
void OnBecameKey(void* owner);
void OnQuit();
// Drawing: the window's pixels (0x00RRGGBB in the machine's byte order,
// top row first), held still until EndDraw. False when there are none.
bool BeginDraw(void* owner, const uint32_t** pixels, int* width, int* height);
void EndDraw();

}  // namespace cocoa
}  // namespace winport

#endif
