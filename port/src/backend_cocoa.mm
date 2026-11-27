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

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1020 && !defined(GNUSTEP)
// The Mac OS X 10.0 and 10.1 headers do not declare this function, but
// CoreGraphics has it.
enum { kCGInterpolationNone = 1 };
extern "C" void CGContextSetInterpolationQuality(CGContextRef c, int quality);
#endif
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1010 && !defined(GNUSTEP)
// The events of the other mouse buttons arrived in 10.1, with these
// numbers. Mac OS X 10.0 sends none of them.
enum { NSOtherMouseDown = 25, NSOtherMouseUp = 26, NSOtherMouseDragged = 27 };
#endif

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// Mac OS X 10.4's headers have neither of these.
#if !defined(NSINTEGER_DEFINED) && !defined(GNUSTEP)
typedef int NSInteger;
typedef unsigned int NSUInteger;
#endif
#if !defined(CGFLOAT_DEFINED) && !defined(GNUSTEP)
typedef float CGFloat;
#endif
// The 10.4.0 SDK (Xcode 2.0) has no byte order flags: Quartz then reads
// 32-bit pixels big-endian, which is the PowerPC's own order.
#if !defined(kCGBitmapByteOrder32Host) && !defined(GNUSTEP)
#if defined(__BIG_ENDIAN__)
#define kCGBitmapByteOrder32Host 0
#else
#define kCGBitmapByteOrder32Host (2 << 12)
#endif
#endif

using namespace winport::cocoa;

// A screen saver bundle compiles this file again with its own
// WINPORT_OBJC_PREFIX. Objective-C classes share one name space in a
// process, and System Preferences loads more than one screen saver.
#ifdef WINPORT_OBJC_PREFIX
#define WINPORT_JOIN2(a, b) a##b
#define WINPORT_JOIN(a, b) WINPORT_JOIN2(a, b)
#define WinportWindow WINPORT_JOIN(WINPORT_OBJC_PREFIX, Window)
#define WinportView WINPORT_JOIN(WINPORT_OBJC_PREFIX, View)
#define WinportDelegate WINPORT_JOIN(WINPORT_OBJC_PREFIX, Delegate)
#define WinportDragSource WINPORT_JOIN(WINPORT_OBJC_PREFIX, DragSource)
#endif

// A borderless window that can still take the keyboard, unless it is a
// popup.
@interface WinportWindow : NSWindow {
 @public
  void* owner;
  BOOL popup;
}
@end

// Draws the window's pixels, and is the client of the input method:
// NSTextInputClient from 10.5, NSTextInput before.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1050 && !defined(GNUSTEP)
#define WINPORT_TEXT_INPUT_CLIENT 1
@interface WinportView : NSView <NSTextInputClient> {
#else
@interface WinportView : NSView <NSTextInput> {
#endif
 @public
  void* owner;
}
@end

// Activation, and Exit in the application menu or Quit in the Dock.
@interface WinportDelegate : NSObject
@end

// The source of a drag of the program's own (RunDrag): the operations it
// allows, and the one it ended with.
@interface WinportDragSource : NSObject {
 @public
  NSDragOperation allowed;
  NSDragOperation done;
}
@end

// A hosted window (SetHost) has no NSWindow: window is nil, and the
// view is a subview of the host view.
struct winport::cocoa::Win {
  WinportWindow* window;
  WinportView* view;
  bool hosted;
};

namespace {

WinportDelegate* g_delegate;
std::vector<Win*> g_wins;
WinportWindow* g_grab;  // where the pressed buttons went down
unsigned g_buttons;
// A screen saver bundle (SetHost): g_yield gives control back to the
// host, and g_host is the view the windows are in (nil while the screen
// saver has no view).
NSView* g_host;
void (*g_yield)(long);
// The program's pixels to a point (SetUnits).
double g_units = 1;
// The input method: the text it is composing (marked text) and the
// selection in it, and what it did with the key being handed to it.
std::vector<uint16_t> g_marked;
NSUInteger g_markedSelection;
void* g_markedOwner;
struct KeyRecord {
  bool active;    // interpretKeyEvents is running
  bool inserted;  // it typed text, in text
  bool composed;  // it finished a composition, or typed more than one character
  std::vector<uint16_t> text;
} g_key;

int Px(CGFloat points) { return (int)floor(points * g_units + 0.5); }
CGFloat Pt(int pixels) { return (CGFloat)(pixels / g_units); }

// The screen with the menu bar, whose top left is the origin.
NSRect MainScreen() {
  NSArray* screens = [NSScreen screens];
  if ([screens count] == 0) return NSMakeRect(0, 0, 1024, 768);
  return [(NSScreen*)[screens objectAtIndex:0] frame];
}

NSRect ToCocoa(int x, int y, int width, int height) {
  if (width < 1) width = 1;
  if (height < 1) height = 1;
  return NSMakeRect(Pt(x), MainScreen().size.height - Pt(y + height), Pt(width), Pt(height));
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
  int x = (int)floor(at.x * g_units), y = (int)floor((MainScreen().size.height - at.y) * g_units);
  unsigned mods = Mods([e modifierFlags]);
  int kind = kMove, button = 0;
  switch ([e type]) {
    case NSLeftMouseDown: kind = kDown; button = 0; break;
    case NSLeftMouseUp: kind = kUp; button = 0; break;
    case NSRightMouseDown: kind = kDown; button = 1; break;
    case NSRightMouseUp: kind = kUp; button = 1; break;
    case NSOtherMouseDown:
    case NSOtherMouseUp:
      if ((long)[e buttonNumber] != 2) return;  // (long: the 10.0 headers do not declare it)
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

// Drags: the operations of our kinds, and the other way.
int KindsOf(NSDragOperation ops) {
  int k = 0;
  if (ops & NSDragOperationCopy) k |= kDragCopy;
  if (ops & (NSDragOperationMove | NSDragOperationGeneric)) k |= kDragMove;
  if (ops & NSDragOperationLink) k |= kDragLink;
  return k;
}
NSDragOperation OperationOf(int kind, NSDragOperation allowed) {
  if (kind & kDragMove) return (allowed & NSDragOperationMove) ? NSDragOperationMove : NSDragOperationGeneric;
  if (kind & kDragCopy) return NSDragOperationCopy;
  if (kind & kDragLink) return NSDragOperationLink;
  return NSDragOperationNone;
}
// What the drag over our windows carries, read as it enters.
DragData g_drag;
DragData ReadDrag(id<NSDraggingInfo> info) {
  DragData d;
  NSPasteboard* pb = [info draggingPasteboard];
  NSArray* types = [pb types];
  if ([types containsObject:NSFilenamesPboardType]) {
    NSArray* names = [pb propertyListForType:NSFilenamesPboardType];
    if ([names isKindOfClass:[NSArray class]])
      for (NSUInteger i = 0; i < [names count]; i++) {
        id name = [names objectAtIndex:i];
        if ([name isKindOfClass:[NSString class]]) d.paths.push_back([(NSString*)name fileSystemRepresentation]);
      }
  }
  if ([types containsObject:NSStringPboardType]) {
    NSString* s = [pb stringForType:NSStringPboardType];
    if (s) {
      d.hasText = true;
      d.text.resize([s length]);
      if (!d.text.empty()) [s getCharacters:(unichar*)&d.text[0]];
    }
  }
  d.local = [info draggingSource] != nil;
  return d;
}
unsigned DragMods() {
  NSEvent* e = [NSApp currentEvent];
  return e ? Mods([e modifierFlags]) : 0;
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
      // A key without Command, Control or Option (which is Alt here)
      // goes to the input method first, and every key but Command's while
      // it is composing. What it types comes back to the view.
      unsigned mods = Mods([e modifierFlags]);
      WinportView* view = key ? (WinportView*)[key contentView] : nil;
      if (type == kKeyDown && view && [view isKindOfClass:[WinportView class]] &&
          (!(mods & (kCommand | kControl | kOption)) || (!g_marked.empty() && !(mods & kCommand)))) {
        bool hadMarked = !g_marked.empty();
        g_key.active = true;
        g_key.inserted = g_key.composed = false;
        g_key.text.clear();
        [view interpretKeyEvents:[NSArray arrayWithObject:e]];
        g_key.active = false;
        if (hadMarked || !g_marked.empty() || g_key.composed) {
          OnProcessKey(key->owner, [e keyCode]);
          return true;
        }
        // What it typed is the key's text: with a dead key's accent, say.
        if (g_key.inserted) text = g_key.text;
      }
      return OnKey(key ? key->owner : NULL, type, [e keyCode], mods, text.empty() ? NULL : &text[0], text.size(),
                   plain.empty() ? NULL : &plain[0], plain.size());
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
  // Exit, as Windows names it; it closes the active window as Alt+F4 does.
  NSMenuItem* quit = (NSMenuItem*)[app addItemWithTitle:@"Exit"
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

// Start: makes the program a foreground application with a menu bar.
// In a host (SetHost), the application is the host's: it does nothing.
bool Start() {
  if (g_yield) return true;
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
#if !defined(GNUSTEP) && (!defined(MAC_OS_X_VERSION_10_6) || MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_6) && \
    MAC_OS_X_VERSION_MAX_ALLOWED >= 1030
  // (TransformProcessType arrived in 10.3. Before it, such a program stays
  // in the background.)
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

// The screen's backingScaleFactor (Mac OS X 10.7), else its
// userSpaceScaleFactor (10.4), else 1.
// A screen's pixels to a point.
double ScaleOf(NSScreen* screen) {
  double scale = 1;
  SEL sel = NULL;
  if ([screen respondsToSelector:@selector(backingScaleFactor)]) sel = @selector(backingScaleFactor);
  else if ([screen respondsToSelector:@selector(userSpaceScaleFactor)]) sel = @selector(userSpaceScaleFactor);
  if (sel) {
    NSInvocation* call = [NSInvocation invocationWithMethodSignature:[screen methodSignatureForSelector:sel]];
    [call setSelector:sel];
    [call invokeWithTarget:screen];
    CGFloat v = 1;
    [call getReturnValue:&v];
    if (v >= 1 && v <= 8) scale = v;
  }
  return scale;
}

double DesktopScale() {
  if (g_yield) return 1;
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSArray* screens = [NSScreen screens];
  double scale = [screens count] ? ScaleOf((NSScreen*)[screens objectAtIndex:0]) : 1;
  [pool release];
  return scale;
}

std::vector<Screen> Screens() {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  std::vector<Screen> out;
  NSArray* screens = [NSScreen screens];
  double top = MainScreen().size.height;
  for (NSUInteger i = 0; i < [screens count]; i++) {
    NSScreen* screen = (NSScreen*)[screens objectAtIndex:i];
    NSRect f = [screen frame], v = [screen visibleFrame];
    Screen s;
    s.x = Px(f.origin.x);
    s.y = Px(top - f.origin.y - f.size.height);
    s.width = Px(f.size.width);
    s.height = Px(f.size.height);
    s.workX = Px(v.origin.x);
    s.workY = Px(top - v.origin.y - v.size.height);
    s.workWidth = Px(v.size.width);
    s.workHeight = Px(v.size.height);
    s.scale = g_yield ? 1 : ScaleOf(screen);
    out.push_back(s);
  }
  [pool release];
  return out;
}

void SetUnits(double unitsPerPoint) {
  if (unitsPerPoint >= 1) g_units = unitsPerPoint;
}

void ScreenSize(int* width, int* height) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSRect f = MainScreen();
  *width = Px(f.size.width);
  *height = Px(f.size.height);
  [pool release];
}

void WorkArea(int* x, int* y, int* width, int* height) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSArray* screens = [NSScreen screens];
  NSRect full = MainScreen();
  NSRect f = [screens count] ? [(NSScreen*)[screens objectAtIndex:0] visibleFrame] : full;
  *x = Px(f.origin.x);
  *y = Px(full.size.height - f.origin.y - f.size.height);
  *width = Px(f.size.width);
  *height = Px(f.size.height);
  [pool release];
}

// Create: makes the NSWindow and the view of a top-level window, or
// only a view in the host view (SetHost).
Win* Create(void* owner, int x, int y, int width, int height, bool popup) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  Win* w = new Win;
  if (g_yield) {
    NSRect r = g_host ? [g_host bounds] : NSMakeRect(0, 0, Pt(width), Pt(height));
    WinportView* view = [[WinportView alloc] initWithFrame:r];
    view->owner = owner;
    [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    w->window = nil;
    w->view = view;
    w->hosted = true;
    g_wins.push_back(w);
    [pool release];
    return w;
  }
  w->hosted = false;
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
  if (!popup) [view registerForDraggedTypes:[NSArray arrayWithObjects:NSFilenamesPboardType, NSStringPboardType, nil]];
  [win setContentView:view];
  [win makeFirstResponder:view];
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
  if (w->window) w->window->owner = NULL;
  w->view->owner = NULL;
  if (w->hosted) [w->view removeFromSuperview];
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
  if (w->hosted) {
    // NSView has setHidden: only from 10.3, so a hidden view leaves the host.
    if (how == kHide) [w->view removeFromSuperview];
    else if (g_host && [w->view superview] != g_host) {
      [w->view setFrame:[g_host bounds]];
      [g_host addSubview:w->view];
    }
    [pool release];
    return;
  }
  if (how == kHide) [w->window orderOut:nil];
  else if (how == kShow || w->window->popup) [w->window orderFront:nil];
  else [w->window makeKeyAndOrderFront:nil];
  [pool release];
}

// SetFrame: moves an NSWindow. A hosted view keeps the size of the host.
void SetFrame(Win* w, int x, int y, int width, int height) {
  if (w->hosted) return;
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

void SetTranslucent(Win* w, bool translucent, bool clickThrough) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  [w->window setOpaque:translucent ? NO : YES];
  [w->window setBackgroundColor:translucent ? [NSColor clearColor] : [NSColor windowBackgroundColor]];
  [w->window setHasShadow:translucent ? NO : YES];
  [w->window setIgnoresMouseEvents:clickThrough ? YES : NO];
  [w->view setNeedsDisplay:YES];
  [pool release];
}

void Invalidate(Win* w, int x, int y, int width, int height) {
  CGFloat h = [w->view bounds].size.height;
  [w->view setNeedsDisplayInRect:NSMakeRect(Pt(x), h - Pt(y + height), Pt(width), Pt(height))];
}

// An image of a picture: ARGB, not premultiplied, top row first.
static NSImage* ImageOf(const uint32_t* argb, int width, int height) {
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
  // In points: a picture in the program's pixels shows at its size there.
  NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(Pt(width), Pt(height))];
  [img addRepresentation:rep];
  [rep release];
  return img;
}

// SetCursor: sets the pointer. In a host, the pointer is the host's.
void SetCursor(bool hidden, int systemId, const uint32_t* argb, int width, int height, int hotX, int hotY) {
  if (g_yield) return;
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
    NSImage* img = ImageOf(argb, width, height);
    [g_own release];
    g_own = [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(Pt(hotX), Pt(hotY))];
    [img release];
    c = g_own;
  }
  if (!c) c = [NSCursor arrowCursor];
  [c set];
  [pool release];
}

// SetAppIcon: sets the icon of the application, unless it is a host's.
void SetAppIcon(const uint32_t* argb, int width, int height) {
  if (g_yield) return;
  if (!argb || width <= 0 || height <= 0) return;
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSImage* img = ImageOf(argb, width, height);
  [NSApp setApplicationIconImage:img];
  [img release];
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

int RunDrag(const std::vector<std::string>& paths, const std::u16string* text, int allowed) {
  WinportWindow* win = g_grab ? g_grab : Ours([NSApp keyWindow]);
  if (!win || (paths.empty() && !text)) return -1;
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSView* view = [win contentView];
  NSPasteboard* pb = [NSPasteboard pasteboardWithName:NSDragPboard];
  NSMutableArray* types = [NSMutableArray array];
  if (!paths.empty()) [types addObject:NSFilenamesPboardType];
  if (text) [types addObject:NSStringPboardType];
  [pb declareTypes:types owner:nil];
  if (!paths.empty()) {
    NSMutableArray* names = [NSMutableArray array];
    for (size_t i = 0; i < paths.size(); i++) {
      NSString* name = [NSString stringWithUTF8String:paths[i].c_str()];
      if (name) [names addObject:name];
    }
    [pb setPropertyList:names forType:NSFilenamesPboardType];
  }
  if (text) {
    NSString* s = [[[NSString alloc] initWithCharacters:(const unichar*)text->data() length:text->size()] autorelease];
    [pb setString:s forType:NSStringPboardType];
  }
  // A plain square goes with the pointer.
  NSImage* image = [[[NSImage alloc] initWithSize:NSMakeSize(32, 32)] autorelease];
  [image lockFocus];
  [[NSColor colorWithCalibratedWhite:0.3 alpha:0.4] set];
  NSRectFill(NSMakeRect(0, 0, 32, 32));
  [image unlockFocus];
  NSPoint mouse = [NSEvent mouseLocation];
  NSPoint inWindow = [win convertScreenToBase:mouse];
  NSPoint inView = [view convertPoint:inWindow fromView:nil];
  NSEvent* now = [NSApp currentEvent];
  NSEvent* e = [NSEvent mouseEventWithType:NSLeftMouseDragged
                                  location:inWindow
                             modifierFlags:now ? [now modifierFlags] : 0
                                 timestamp:now ? [now timestamp] : 0
                              windowNumber:[win windowNumber]
                                   context:nil
                               eventNumber:0
                                clickCount:1
                                  pressure:1];
  WinportDragSource* source = [[WinportDragSource alloc] init];
  source->allowed = NSDragOperationNone;
  if (allowed & kDragCopy) source->allowed |= NSDragOperationCopy;
  if (allowed & kDragMove) source->allowed |= NSDragOperationMove | NSDragOperationGeneric;
  if (allowed & kDragLink) source->allowed |= NSDragOperationLink;
  source->done = NSDragOperationNone;
  [view dragImage:image
               at:NSMakePoint(inView.x - 16, inView.y - 16)
           offset:NSMakeSize(0, 0)
            event:e
       pasteboard:pb
           source:source
        slideBack:YES];
  int kinds = KindsOf(source->done);
  [source release];
  // AppKit took the button's release.
  if (g_buttons & 1) {
    g_buttons &= ~1u;
    if (!g_buttons) g_grab = nil;
    NSPoint at = [NSEvent mouseLocation];
    OnMouse(win->owner, kUp, 0, (int)floor(at.x * g_units), (int)floor((MainScreen().size.height - at.y) * g_units),
            0);
  }
  [pool release];
  return (kinds & kDragMove) ? kDragMove : (kinds & kDragCopy) ? kDragCopy : kinds & kDragLink;
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

// Wait: takes the events for up to timeout ms. In a host, the events are
// the host's: a wait gives control back to it.
void Wait(long timeout) {
  if (g_yield) {
    g_yield(timeout);
    return;
  }
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

// Wake: ends a Wait. A host calls the program again each frame.
void ImeReset(Win* w) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if (g_markedOwner == w->view->owner) g_marked.clear();
  // Through the selectors, as the headers before 10.6 lack them.
  if ([w->view respondsToSelector:@selector(inputContext)]) {
    id context = [w->view performSelector:@selector(inputContext)];
    if ([context respondsToSelector:@selector(discardMarkedText)]) [context performSelector:@selector(discardMarkedText)];
  } else if (Class manager = NSClassFromString(@"NSInputManager")) {
    id current = [manager performSelector:@selector(currentInputManager)];
    if ([current respondsToSelector:@selector(markedTextAbandoned:)])
      [current performSelector:@selector(markedTextAbandoned:) withObject:w->view];
  }
  [pool release];
}

void ImeMoved(Win* w) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  if ([w->view respondsToSelector:@selector(inputContext)]) {
    id context = [w->view performSelector:@selector(inputContext)];
    if ([context respondsToSelector:@selector(invalidateCharacterCoordinates)])
      [context performSelector:@selector(invalidateCharacterCoordinates)];
  }
  [pool release];
}

void Wake() {
  if (g_yield) return;
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

// SetHost: puts the top-level windows made from now on in the view host.
void SetHost(void* host, void (*yield)(long)) {
  g_host = (NSView*)host;
  g_yield = yield;
}

// MoveHosted: moves the hosted views into the view host, or out of all
// views when host is NULL.
void MoveHosted(void* host) {
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  for (size_t i = 0; i < g_wins.size(); i++) {
    Win* w = g_wins[i];
    if (!w->hosted) continue;
    [w->view removeFromSuperview];
    if (host) {
      [w->view setFrame:[(NSView*)host bounds]];
      [(NSView*)host addSubview:w->view];
    }
  }
  g_host = (NSView*)host;
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
// The input method's side.
static NSString* PlainString(id s) {
  return [s isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)s string] : (NSString*)s;
}
- (void)insertText:(id)s {
  std::vector<uint16_t> text = Chars(PlainString(s));
  bool wasMarked = !g_marked.empty();
  g_marked.clear();
  if (g_key.active && !wasMarked && text.size() == 1) {
    g_key.inserted = true;
    g_key.text = text;
    return;
  }
  // Text finished: by the composition, or more than a key types, or
  // from outside a key (the character viewer).
  if (g_key.active) g_key.composed = true;
  OnImeText(owner, true, text.empty() ? NULL : &text[0], text.size(), 0);
}
- (void)setMarkedText:(id)s selectedRange:(NSRange)selection {
  std::vector<uint16_t> text = Chars(PlainString(s));
  if (text.empty() && g_marked.empty()) return;
  g_marked = text;
  g_markedSelection = selection.location <= text.size() ? selection.location : text.size();
  g_markedOwner = owner;
  OnImeText(owner, false, text.empty() ? NULL : &text[0], text.size(), g_markedSelection);
}
- (void)unmarkText {
  // The composition is finished as it stands.
  if (g_marked.empty()) return;
  std::vector<uint16_t> text;
  text.swap(g_marked);
  OnImeText(owner, true, &text[0], text.size(), 0);
}
- (BOOL)hasMarkedText {
  return g_marked.empty() ? NO : YES;
}
- (NSRange)markedRange {
  return g_marked.empty() ? NSMakeRange(NSNotFound, 0) : NSMakeRange(0, g_marked.size());
}
- (NSRange)selectedRange {
  return NSMakeRange(g_marked.empty() ? 0 : g_markedSelection, 0);
}
- (NSArray*)validAttributesForMarkedText {
  return [NSArray array];
}
// The program's text is its own: the input method gets none of it.
- (NSAttributedString*)attributedSubstringFromRange:(NSRange)range {
  return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)p {
  return 0;
}
// Where the candidates go: by the caret of the window with the focus.
- (NSRect)firstRectForCharacterRange:(NSRange)range {
  int x, y, width, height;
  if (!OnImeRect(owner, &x, &y, &width, &height)) {
    NSRect f = [self window] ? [[self window] frame] : NSMakeRect(0, 0, 0, 0);
    return NSMakeRect(f.origin.x, f.origin.y, 0, 0);
  }
  return ToCocoa(x, y, width, height);
}
// Keys the input method does not type with (arrows, Return) are the
// window's, as OnKey gives them.
- (void)doCommandBySelector:(SEL)selector {
}
#ifdef WINPORT_TEXT_INPUT_CLIENT
- (void)insertText:(id)s replacementRange:(NSRange)range {
  [self insertText:s];
}
- (void)setMarkedText:(id)s selectedRange:(NSRange)selection replacementRange:(NSRange)range {
  [self setMarkedText:s selectedRange:selection];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
  return nil;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
  return [self firstRectForCharacterRange:range];
}
#else
- (long)conversationIdentifier {
  return (long)self;
}
#endif
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}
// Drags from other programs, and ours, go to the window under them.
- (BOOL)dropPointOf:(id<NSDraggingInfo>)info x:(int*)x y:(int*)y {
  NSWindow* win = [self window];
  if (!win) return NO;
  NSPoint at = [win convertBaseToScreen:[info draggingLocation]];
  *x = (int)floor(at.x * g_units);
  *y = (int)floor((MainScreen().size.height - at.y) * g_units);
  return YES;
}
- (NSDragOperation)drag:(id<NSDraggingInfo>)info enter:(BOOL)enter {
  int x, y;
  if (![self dropPointOf:info x:&x y:&y]) return NSDragOperationNone;
  if (enter) g_drag = ReadDrag(info);
  NSDragOperation allowed = [info draggingSourceOperationMask];
  return OperationOf(OnDrag(x, y, enter, g_drag, KindsOf(allowed), DragMods()), allowed);
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
  return [self drag:info enter:YES];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info {
  return [self drag:info enter:NO];
}
- (void)draggingExited:(id<NSDraggingInfo>)info {
  OnDragLeft();
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
  int x, y;
  if (![self dropPointOf:info x:&x y:&y]) return NO;
  return OnDrop(x, y, g_drag, KindsOf([info draggingSourceOperationMask]), DragMods()) ? YES : NO;
}
- (void)drawRect:(NSRect)dirty {
  const uint32_t* pixels;
  int width, height;
  bool alpha = false;
  if (!owner || !BeginDraw(owner, &pixels, &width, &height, &alpha)) return;
  CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
  if (ctx) {
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef data = CGDataProviderCreateWithData(NULL, pixels, (size_t)width * height * 4, NULL);
    // 0x00RRGGBB in the machine's byte order, or premultiplied ARGB.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1040 && !defined(GNUSTEP)
    // Before 10.4 there is no byte order flag: the order is the machine's
    // on PowerPC, which these headers are for.
    CGImageAlphaInfo info = alpha ? kCGImageAlphaPremultipliedFirst : kCGImageAlphaNoneSkipFirst;
#else
    CGBitmapInfo info = (CGBitmapInfo)(alpha ? kCGImageAlphaPremultipliedFirst : kCGImageAlphaNoneSkipFirst) |
                        (CGBitmapInfo)kCGBitmapByteOrder32Host;
#endif
    CGImageRef img = CGImageCreate(width, height, 8, 32, width * 4, rgb, info, data, NULL, false,
                                   kCGRenderingIntentDefault);
    CGFloat h = [self bounds].size.height;
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, CGRectMake(dirty.origin.x, dirty.origin.y, dirty.size.width, dirty.size.height));
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    // With alpha the picture replaces what was there, rather than going over it.
#if (defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1050) || defined(GNUSTEP)
    if (alpha) CGContextSetBlendMode(ctx, kCGBlendModeCopy);
#else
    // Before 10.5 there is no copy blend mode: clearing the area first
    // gives the same result for a premultiplied picture.
    if (alpha) CGContextClearRect(ctx, CGRectMake(0, h - Pt(height), Pt(width), Pt(height)));
#endif
    // The pixels fill their size in points: on a Retina display, a
    // DPI-aware program's pixels are the display's own.
    CGContextDrawImage(ctx, CGRectMake(0, h - Pt(height), Pt(width), Pt(height)), img);
    CGContextRestoreGState(ctx);
    CGImageRelease(img);
    CGDataProviderRelease(data);
    CGColorSpaceRelease(rgb);
  }
  EndDraw();
}
@end

@implementation WinportDragSource
- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)local {
  return allowed;
}
- (void)draggedImage:(NSImage*)image endedAt:(NSPoint)at operation:(NSDragOperation)op {
  done = op;
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
