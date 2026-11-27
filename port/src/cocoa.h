// cocoa.h - between the Cocoa backend's two halves: backend_cocoa.mm,
// which uses AppKit and sees none of the Windows headers, and
// backend_cocoa.cpp, which uses the window manager and sees none of the
// Mac headers. Their types clash (BOOL, for one), so neither includes
// the other's.
//
// Coordinates are Windows' screen coordinates: from the top left of the
// screen with the menu bar, growing down, in the program's pixels, of
// which there are SetUnits to a point (2 for a DPI-aware program on a
// Retina display, 1 otherwise).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_COCOA_H
#define _WINPORT_COCOA_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace winport {
namespace cocoa {

// Called on the backend's side (backend_cocoa.mm). A window is known to
// the other side by its owner, an opaque pointer.
struct Win;
bool Start();
// The main screen's pixels to a point (backingScaleFactor), 1 in a host.
double DesktopScale();
void SetUnits(double unitsPerPoint);
void ScreenSize(int* width, int* height);
// The screen without the menu bar and the Dock.
void WorkArea(int* x, int* y, int* width, int* height);
Win* Create(void* owner, int x, int y, int width, int height, bool popup);
void Destroy(Win* w);
enum { kHide, kShow, kShowKey };
void Show(Win* w, int how);
void SetFrame(Win* w, int x, int y, int width, int height);
void SetTitle(Win* w, const uint16_t* text, size_t length);
void Invalidate(Win* w, int x, int y, int width, int height);  // in the window
// A window drawn with alpha, which lets the mouse through where it is
// fully transparent; clickThrough lets it through everywhere.
void SetTranslucent(Win* w, bool translucent, bool clickThrough);
// The pointer's shape: a system cursor by its IDC_ number, or else the
// picture (ARGB, not premultiplied, top row first); hidden hides it.
void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY);
// The application's icon in the Dock and the application switcher, from
// a picture as SetCursor takes it.
void SetAppIcon(const uint32_t* argb, int width, int height);
// The general pasteboard: its change count, its text (false when it has
// none), and putting text there, which returns the new change count.
long PasteboardChangeCount();
bool PasteboardText(std::u16string* text);
long SetPasteboardText(const std::u16string& text);
// Takes and handles events for up to timeout ms (-1 for no limit), or
// until Wake.
void Wait(long timeout);
void Wake();
// The input method: the program has ended the composition itself; and
// where the candidates go has changed.
void ImeReset(Win* w);
void ImeMoved(Win* w);
// A screen saver bundle (saver_host.cpp): the views of the top-level
// windows go into host (an NSView) in place of windows of their own, and
// Wait calls yield, which gives control back to the program that owns
// the process. The application and its events are the host's. Call it
// before the first window is made.
void SetHost(void* host, void (*yield)(long timeout));
// Moves the views of the top-level windows into another host view, at
// its size, or out of all views when host is NULL.
void MoveHosted(void* host);

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
// The input method has taken a key (it shows as VK_PROCESSKEY), and the
// text it is composing (commit false, with its cursor) or has finished,
// in UTF-16. OnImeRect gives where its candidates go, in screen
// coordinates; false when there is no such place.
void OnProcessKey(void* owner, unsigned code);
void OnImeText(void* owner, bool commit, const uint16_t* text, size_t length, size_t cursor);
bool OnImeRect(void* owner, int* x, int* y, int* width, int* height);
// Files dragged from the Finder, at a screen point: whether a window of
// ours there takes them, and the drop (UTF-8 paths), which says whether
// one did.
bool OnDragFiles(int x, int y);
bool OnDropFiles(int x, int y, const std::vector<std::string>& paths);
void OnQuit();
// Drawing: the window's pixels (0x00RRGGBB in the machine's byte order,
// top row first; premultiplied 0xAARRGGBB when alpha), held still until
// EndDraw. False when there are none.
bool BeginDraw(void* owner, const uint32_t** pixels, int* width, int* height, bool* alpha);
void EndDraw();

}  // namespace cocoa
}  // namespace winport

#endif
