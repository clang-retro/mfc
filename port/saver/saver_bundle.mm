// saver_bundle.mm - the principal class of a Mac OS X .saver bundle: a
// ScreenSaverView that runs the screen saver of a Windows program in it
// (see port/src/saver_host.h). tools/build-sdk-samples.py compiles it
// once for each screen saver, with:
//
//   WINPORT_SAVER_CLASS  the name of the class, which must be different
//                        for each screen saver (System Preferences loads
//                        all of them in one process)
//
// One screen saver runs in a process. With more than one screen, the
// screen saver engine makes one view for each screen: the screen saver
// draws in the first view that starts, and the other views stay black.
// When that view stops, the screen saver moves to another view that runs.
//
// It uses only the API of Mac OS X 10.0, and it is built without ARC.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#import <AppKit/AppKit.h>
#import <ScreenSaver/ScreenSaver.h>

#include "../src/saver_host.h"

#ifndef WINPORT_SAVER_CLASS
#error "define WINPORT_SAVER_CLASS"
#endif

@interface WINPORT_SAVER_CLASS : ScreenSaverView
@end

// The views that animate, in the order they started, and the one that
// has the screen saver.
static const int kMaxViews = 16;
static WINPORT_SAVER_CLASS* g_views[kMaxViews];
static int g_count;
static WINPORT_SAVER_CLASS* g_attached;

// Removes a view from the views that animate.
static void Forget(WINPORT_SAVER_CLASS* v) {
  for (int i = 0; i < g_count; i++)
    if (g_views[i] == v) {
      for (int j = i + 1; j < g_count; j++) g_views[j - 1] = g_views[j];
      g_count--;
      return;
    }
}

@implementation WINPORT_SAVER_CLASS

// initWithFrame:isPreview: sets a frame rate of 60 frames a second.
- (id)initWithFrame:(NSRect)frame isPreview:(BOOL)isPreview {
  self = [super initWithFrame:frame isPreview:isPreview];
  if (self) [self setAnimationTimeInterval:1.0 / 60.0];
  return self;
}

// attach puts the screen saver in this view, and starts it the first time.
- (void)attach {
  NSSize size = [self bounds].size;
  g_attached = self;
  if (WinportSaverRunning()) {
    WinportSaverMove(self, (int)size.width, (int)size.height);
    return;
  }
  NSString* exe = [[[NSBundle bundleForClass:[self class]] executablePath] lastPathComponent];
  NSString* program = [exe stringByAppendingString:@".scr"];
  WinportSaverStart(self, (int)size.width, (int)size.height, [program cString]);
}

// detach takes the screen saver out of this view, and gives it to the
// next view that animates.
- (void)detach {
  if (g_attached != self) return;
  WinportSaverDetach();
  g_attached = nil;
  if (g_count > 0) [g_views[0] attach];
}

// startAnimation starts the animation, and gives the screen saver to this
// view when no other view has it.
- (void)startAnimation {
  [super startAnimation];
  Forget(self);
  if (g_count < kMaxViews) g_views[g_count++] = self;
  if (!g_attached) [self attach];
}

// stopAnimation stops the animation, and takes the screen saver out of
// this view.
- (void)stopAnimation {
  [super stopAnimation];
  Forget(self);
  [self detach];
}

// animateOneFrame runs the screen saver for one frame.
- (void)animateOneFrame {
  if (g_attached == self) WinportSaverFrame();
}

// setFrameSize: gives the screen saver the new size of the view.
- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  if (g_attached == self) WinportSaverMove(self, (int)size.width, (int)size.height);
}

// drawRect: paints the view black. The screen saver draws in a subview.
- (void)drawRect:(NSRect)rect {
  [[NSColor blackColor] set];
  NSRectFill(rect);
}

// isOpaque tells AppKit that the view paints all of its area.
- (BOOL)isOpaque {
  return YES;
}

// hasConfigureSheet: the settings dialog of the screen saver is not
// available in System Preferences.
- (BOOL)hasConfigureSheet {
  return NO;
}

// dealloc takes the screen saver out of the view before it goes.
- (void)dealloc {
  Forget(self);
  [self detach];
  [super dealloc];
}

@end
