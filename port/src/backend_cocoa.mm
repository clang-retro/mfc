// backend_cocoa.mm - the AppKit half of the Cocoa backend (see cocoa.h).
//
// Each top-level window becomes a borderless NSWindow, since the window
// draws its own caption and borders, whose view draws the window's
// surface. Menus, list drop-downs and tooltips float above the other
// windows and never take the keyboard. Input is taken from the event
// queue as it is waited for, ahead of AppKit's own handling, so keys
// reach the program as they would on Windows.
//
// It uses only what AppKit had in Mac OS X 10.4, so the same code runs
// from 10.4 on PowerPC and Intel to current versions, and it must run on
// the program's main thread. It is built without ARC.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#import <Cocoa/Cocoa.h>

#include <math.h>

#include <vector>

#include "cocoa.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// Mac OS X 10.4's headers have neither of these.
#if !defined(NSINTEGER_DEFINED) && !defined(GNUSTEP)
typedef int NSInteger;
typedef unsigned int NSUInteger;
#endif
#if !defined(CGFLOAT_DEFINED) && !defined(GNUSTEP)
typedef float CGFloat;
#endif

using namespace winport::cocoa;

// A borderless window that can still take the keyboard, unless it is a
// popup.
@interface WinportWindow : NSWindow {
 @public
  void* owner;
  BOOL popup;
}
@end

// Draws the window's pixels.
@interface WinportView : NSView {
 @public
  void* owner;
}
@end

// Activation, and quitting from the application menu or the Dock.
@interface WinportDelegate : NSObject
@end

struct winport::cocoa::Win {
  WinportWindow* window;
  WinportView* view;
};

namespace {

WinportDelegate* g_delegate;
std::vector<Win*> g_wins;
WinportWindow* g_grab;  // where the pressed buttons went down
unsigned g_buttons;

// The screen with the menu bar, whose top left is the origin.
NSRect MainScreen() {
  NSArray* screens = [NSScreen screens];
  if ([screens count] == 0) return NSMakeRect(0, 0, 1024, 768);
  return [(NSScreen*)[screens objectAtIndex:0] frame];
}

NSRect ToCocoa(int x, int y, int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  return NSMakeRect(x, MainScreen().size.height - y - height, width, height);
}

unsigned Mods(NSUInteger flags) {
  unsigned m = 0;
  if (flags & NSShiftKeyMask) m |= kShift;
  if (flags & NSControlKeyMask) m |= kControl;
  if (flags & NSAlternateKeyMask) m |= kOption;
  if (flags & NSCommandKeyMask) m |= kCommand;
  return m;
}

WinportWindow* Ours(NSWindow* w) {
  if (!w || ![w isKindOfClass:[WinportWindow class]]) return nil;
  return ((WinportWindow*)w)->owner ? (WinportWindow*)w : nil;
}

// Our topmost window under the pointer.
WinportWindow* WindowAt(NSPoint p) {
  NSArray* order = [NSApp orderedWindows];
  for (NSUInteger i = 0; i < [order count]; i++) {
    WinportWindow* w = Ours((NSWindow*)[order objectAtIndex:i]);
    if (w && [w isVisible] && NSPointInRect(p, [w frame])) return w;
  }
  return nil;
}

// Hands a mouse event on; returns whether AppKit should not see it.
void Mouse(NSEvent* e) {
  NSPoint at = [NSEvent mouseLocation];
  int x = (int)floor(at.x), y = (int)floor(MainScreen().size.height - at.y);
  unsigned mods = Mods([e modifierFlags]);
  int kind = kMove, button = 0;
  switch ([e type]) {
    case NSLeftMouseDown: kind = kDown; button = 0; break;
    case NSLeftMouseUp: kind = kUp; button = 0; break;
    case NSRightMouseDown: kind = kDown; button = 1; break;
    case NSRightMouseUp: kind = kUp; button = 1; break;
    case NSOtherMouseDown:
    case NSOtherMouseUp:
      if ([e buttonNumber] != 2) return;
      kind = [e type] == NSOtherMouseDown ? kDown : kUp;
      button = 2;
      break;
    case NSScrollWheel: {
      WinportWindow* w = g_grab ? g_grab : WindowAt(at);
      // Cocoa's horizontal delta grows leftward.
      if (w) OnWheel(w->owner, x, y, [e deltaY], -[e deltaX], mods);
      return;
    }
    default: break;  // moves and drags
  }
  WinportWindow* w;
  if (kind == kDown) {
    if (!g_buttons) {
      g_grab = Ours([e window]);
      if (!g_grab) g_grab = WindowAt(at);
    }
    g_buttons |= 1u << button;
    w = g_grab;
  } else if (kind == kUp) {
    g_buttons &= ~(1u << button);
    w = g_grab ? g_grab : WindowAt(at);
    if (!g_buttons) g_grab = nil;
  } else {
    w = g_grab ? g_grab : WindowAt(at);
    if (!w) w = Ours([NSApp keyWindow]);
  }
  if (w) OnMouse(w->owner, kind, button, x, y, mods);
}

std::vector<uint16_t> Chars(NSString* s) {
  std::vector<uint16_t> v([s length]);
  if (!v.empty()) [s getCharacters:(unichar*)&v[0]];
  return v;
}

// Takes the event when it is for our windows; false leaves it to AppKit.
bool TakeEvent(NSEvent* e) {
  switch ([e type]) {
    case NSKeyDown:
    case NSKeyUp:
    case NSFlagsChanged: {
      int type = [e type] == NSKeyDown ? kKeyDown : [e type] == NSKeyUp ? kKeyUp : kModifiers;
      // Command+Q and Command+H belong to the application menu.
      if (type == kKeyDown && ([e modifierFlags] & NSCommandKeyMask) && ([e keyCode] == 0x0C || [e keyCode] == 0x04))
        return false;
      std::vector<uint16_t> text, plain;
      if (type != kModifiers) {
        text = Chars([e characters]);
        plain = Chars([e charactersIgnoringModifiers]);
      }
      WinportWindow* key = Ours([NSApp keyWindow]);
      return OnKey(key ? key->owner : NULL, type, [e keyCode], Mods([e modifierFlags]), text.empty() ? NULL : &text[0],
                   text.size(), plain.empty() ? NULL : &plain[0], plain.size());
    }
    case NSLeftMouseDown: case NSLeftMouseUp: case NSRightMouseDown: case NSRightMouseUp:
    case NSOtherMouseDown: case NSOtherMouseUp:
      // Clicks elsewhere, such as in the menu bar, are AppKit's alone.
      if (Ours([e window]) || g_grab) Mouse(e);
      return false;  // AppKit still brings the window forward
    case NSMouseMoved: case NSLeftMouseDragged: case NSRightMouseDragged: case NSOtherMouseDragged:
    case NSScrollWheel:
      Mouse(e);
      return false;
    default:
      return false;
  }
}

void MakeMenu() {
  NSString* name = [[NSProcessInfo processInfo] processName];
  NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
  NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:name action:NULL keyEquivalent:@""];
  NSMenu* app = [[NSMenu alloc] initWithTitle:name];
  [app addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
  [app addItem:[NSMenuItem separatorItem]];
  NSMenuItem* quit = (NSMenuItem*)[app addItemWithTitle:[@"Quit " stringByAppendingString:name]
                                    action:@selector(quit:)
                             keyEquivalent:@"q"];
  [quit setTarget:g_delegate];
  [appItem setSubmenu:app];
  [bar addItem:appItem];
  [NSApp setMainMenu:bar];
  // Mac OS X 10.4 finds the application menu this way.
  if ([NSApp respondsToSelector:@selector(setAppleMenu:)])
    [NSApp performSelector:@selector(setAppleMenu:) withObject:app];
  [app release];
  [appItem release];
  [bar release];
}

}  // namespace

namespace winport {
namespace cocoa {

bool Start() {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  [NSApplication sharedApplication];
  // A program run outside an application bundle becomes a foreground
  // application, with a Dock icon and a menu bar.
  if ([NSApp respondsToSelector:@selector(setActivationPolicy:)]) {
    NSMethodSignature* sig = [NSApp methodSignatureForSelector:@selector(setActivationPolicy:)];
    NSInvocation* call = [NSInvocation invocationWithMethodSignature:sig];
    NSInteger regular = 0;
    [call setSelector:@selector(setActivationPolicy:)];
    [call setArgument:&regular atIndex:2];
    [call invokeWithTarget:NSApp];
  }
#if !defined(GNUSTEP) && (!defined(MAC_OS_X_VERSION_10_6) || MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6)
  else {
    ProcessSerialNumber psn = {0, kCurrentProcess};
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
  }
#endif
  g_delegate = [[WinportDelegate alloc] init];
  [NSApp setDelegate:(id)g_delegate];
  MakeMenu();
  [NSApp finishLaunching];
  [NSApp activateIgnoringOtherApps:YES];
  [pool release];
  return true;
}

void ScreenSize(int* width, int* height) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSRect f = MainScreen();
  *width = (int)f.size.width;
  *height = (int)f.size.height;
  [pool release];
}

Win* Create(void* owner, int x, int y, int width, int height, bool popup) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  Win* w = new Win;
  WinportWindow* win = [[WinportWindow alloc] initWithContentRect:ToCocoa(x, y, width, height)
                                                        styleMask:0 /* borderless */
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
  win->owner = owner;
  win->popup = popup;
  [win setReleasedWhenClosed:NO];
  [win setAcceptsMouseMovedEvents:YES];
  [win setOpaque:YES];
  [win setHasShadow:YES];
  if (popup) [win setLevel:NSPopUpMenuWindowLevel];
  NSRect frame = [win frame];
  WinportView* view = [[WinportView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
  view->owner = owner;
  [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [win setContentView:view];
  [win setDelegate:(id)g_delegate];
  w->window = win;
  w->view = view;
  g_wins.push_back(w);
  [pool release];
  return w;
}

void Destroy(Win* w) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (g_grab == w->window) {
    g_grab = nil;
    g_buttons = 0;
  }
  w->window->owner = NULL;
  w->view->owner = NULL;
  [w->window setDelegate:nil];
  [w->window orderOut:nil];
  [w->window close];
  [w->view release];
  [w->window release];
  for (size_t i = 0; i < g_wins.size(); i++)
    if (g_wins[i] == w) {
      g_wins.erase(g_wins.begin() + i);
      break;
    }
  delete w;
  [pool release];
}

void Show(Win* w, int how) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (how == kHide) [w->window orderOut:nil];
  else if (how == kShow || w->window->popup) [w->window orderFront:nil];
  else [w->window makeKeyAndOrderFront:nil];
  [pool release];
}

void SetFrame(Win* w, int x, int y, int width, int height) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  [w->window setFrame:ToCocoa(x, y, width, height) display:NO];
  [pool release];
}

void SetTitle(Win* w, const uint16_t* text, size_t length) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSString* s = [[NSString alloc] initWithCharacters:(const unichar*)text length:length];
  [w->window setTitle:s];
  [s release];
  [pool release];
}

void Invalidate(Win* w, int x, int y, int width, int height) {
  CGFloat h = [w->view bounds].size.height;
  [w->view setNeedsDisplayInRect:NSMakeRect(x, h - y - height, width, height)];
}

void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY) {
  static bool g_hidden;
  static NSCursor* g_own;  // the program's own cursor, kept while shown
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (hidden != g_hidden) {
    if (hidden) [NSCursor hide];
    else [NSCursor unhide];
    g_hidden = hidden;
  }
  NSCursor* c = nil;
  switch (systemId) {
    case 32512: case 32514: case 32650: c = [NSCursor arrowCursor]; break;
    case 32513: c = [NSCursor IBeamCursor]; break;
    case 32515: c = [NSCursor crosshairCursor]; break;
    case 32516: c = [NSCursor resizeUpCursor]; break;
    case 32640: case 32646: c = [NSCursor openHandCursor]; break;
    case 32644: c = [NSCursor resizeLeftRightCursor]; break;
    case 32645: c = [NSCursor resizeUpDownCursor]; break;
    case 32649: c = [NSCursor pointingHandCursor]; break;
    case 32648:
      if ([NSCursor respondsToSelector:@selector(operationNotAllowedCursor)])
        c = [NSCursor performSelector:@selector(operationNotAllowedCursor)];
      break;
  }
  if (!c && argb && width > 0 && height > 0) {
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:width * 4
                                                                  bitsPerPixel:32];
    // Premultiplied RGBA, as the representation keeps it.
    unsigned char* out = [rep bitmapData];
    for (int i = 0; i < width * height; i++) {
      uint32_t px = argb[i], a = px >> 24;
      out[i * 4 + 0] = (unsigned char)(((px >> 16) & 0xFF) * a / 255);
      out[i * 4 + 1] = (unsigned char)(((px >> 8) & 0xFF) * a / 255);
      out[i * 4 + 2] = (unsigned char)((px & 0xFF) * a / 255);
      out[i * 4 + 3] = (unsigned char)a;
    }
    NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
    [img addRepresentation:rep];
    [rep release];
    [g_own release];
    g_own = [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(hotX, hotY)];
    [img release];
    c = g_own;
  }
  if (!c) c = [NSCursor arrowCursor];
  [c set];
  [pool release];
}

long PasteboardChangeCount() {
  return (long)[[NSPasteboard generalPasteboard] changeCount];
}

bool PasteboardText(std::u16string* text) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSStringPboardType];
  bool ok = s != nil;
  if (ok) {
    text->resize([s length]);
    if (!text->empty()) [s getCharacters:(unichar*)&(*text)[0]];
  }
  [pool release];
  return ok;
}

long SetPasteboardText(const std::u16string& text) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  if (text.empty()) {
    [pb declareTypes:[NSArray array] owner:nil];
  } else {
    [pb declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
    NSString* s = [[NSString alloc] initWithCharacters:(const unichar*)text.data() length:text.size()];
    [pb setString:s forType:NSStringPboardType];
    [s release];
  }
  long count = (long)[pb changeCount];
  [pool release];
  return count;
}

void Wait(long timeout) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  for (size_t i = 0; i < g_wins.size(); i++) [g_wins[i]->window displayIfNeeded];
  NSDate* until = timeout < 0 ? [NSDate distantFuture] : [NSDate dateWithTimeIntervalSinceNow:timeout / 1000.0];
  NSEvent* e = [NSApp nextEventMatchingMask:NSAnyEventMask untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
  while (e) {
    if ([e type] != NSApplicationDefined && !TakeEvent(e)) [NSApp sendEvent:e];
    e = [NSApp nextEventMatchingMask:NSAnyEventMask
                           untilDate:[NSDate distantPast]
                              inMode:NSDefaultRunLoopMode
                             dequeue:YES];
  }
  [pool release];
}

void Wake() {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSEvent* e = [NSEvent otherEventWithType:NSApplicationDefined
                                  location:NSMakePoint(0, 0)
                             modifierFlags:0
                                 timestamp:0
                              windowNumber:0
                                   context:nil
                                   subtype:0
                                     data1:0
                                     data2:0];
  [NSApp postEvent:e atStart:NO];
  [pool release];
}

}  // namespace cocoa
}  // namespace winport

@implementation WinportWindow
- (BOOL)canBecomeKeyWindow {
  return !popup;
}
- (BOOL)canBecomeMainWindow {
  return !popup;
}
@end

@implementation WinportView
- (BOOL)isOpaque {
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (void)drawRect:(NSRect)dirty {
  const uint32_t* pixels;
  int width, height;
  if (!owner || !BeginDraw(owner, &pixels, &width, &height)) return;
  CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
  if (ctx) {
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef data = CGDataProviderCreateWithData(NULL, pixels, (size_t)width * height * 4, NULL);
    // 0x00RRGGBB in the machine's byte order.
    CGImageRef img = CGImageCreate(width, height, 8, 32, width * 4, rgb,
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | (CGBitmapInfo)kCGBitmapByteOrder32Host, data, NULL,
                                   false, kCGRenderingIntentDefault);
    CGFloat h = [self bounds].size.height;
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, CGRectMake(dirty.origin.x, dirty.origin.y, dirty.size.width, dirty.size.height));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGContextDrawImage(ctx, CGRectMake(0, h - height, width, height), img);
    CGContextRestoreGState(ctx);
    CGImageRelease(img);
    CGDataProviderRelease(data);
    CGColorSpaceRelease(rgb);
  }
  EndDraw();
}
@end

@implementation WinportDelegate
- (void)windowDidBecomeKey:(NSNotification*)note {
  WinportWindow* w = Ours((NSWindow*)[note object]);
  if (w && !w->popup) OnBecameKey(w->owner);
}
- (void)quit:(id)sender {
  OnQuit();
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
  OnQuit();
  return NSTerminateCancel;
}
@end
