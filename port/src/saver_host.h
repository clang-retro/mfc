// saver_host.h - runs the screen saver of a Windows program inside a
// host that owns the process and its event loop: the ScreenSaverView of
// a Mac OS X .saver bundle (port/saver/saver_bundle.mm).
//
// The functions have only C types, so that Objective-C code includes this
// file without the Windows headers. Call them on the main thread.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SAVER_HOST_H
#define _WINPORT_SAVER_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

// Starts the screen saver in view (an NSView) of width by height pixels.
// program is the name of the program in its command line, such as
// "sspipes.scr". The program's WinMain gets "/p HWND", the command line
// of a preview in a window HWND that fills the view. Returns 0 when the
// screen saver could not start.
int WinportSaverStart(void* view, int width, int height, const char* program);

// Runs the screen saver until it waits for its next message: once for
// each frame of the host's animation.
void WinportSaverFrame(void);

// Moves the screen saver into another view (or the same view with a new
// size). The screen saver keeps its state.
void WinportSaverMove(void* view, int width, int height);

// Takes the screen saver out of its view. It stops until
// WinportSaverMove gives it a view again.
void WinportSaverDetach(void);

// Gives 1 while the screen saver has started and its WinMain has not
// returned.
int WinportSaverRunning(void);

#ifdef __cplusplus
}
#endif

#endif
