/* testsaver.c - "Test Saver", a small screen saver program for the tests
 * of the screen saver host of the classic Mac OS.
 *
 * It covers the screen with a black window and draws colored rectangles.
 * It stops when the mouse moves more than 4 pixels, when the user clicks
 * or presses a key, and at the Apple event "quit", as the screen savers
 * of the portable layer do.
 */
#include <AppleEvents.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <Processes.h>

QDGlobals qd;
static Boolean gQuit;

/* Handles the Apple event "quit". */
static pascal OSErr QuitHandler(const AppleEvent *event, AppleEvent *reply,
                                long refcon) {
  (void)event; (void)reply; (void)refcon;
  gQuit = true;
  return noErr;
}

/* Handles the other required Apple events. */
static pascal OSErr IgnoreHandler(const AppleEvent *event, AppleEvent *reply,
                                  long refcon) {
  (void)event; (void)reply; (void)refcon;
  return noErr;
}

/* Draws one rectangle of a random size and color. */
static void DrawFrame(const Rect *bounds) {
  Rect r;
  RGBColor c;
  short w = bounds->right - bounds->left, h = bounds->bottom - bounds->top;
  short x = (short)((unsigned short)Random() % w);
  short y = (short)((unsigned short)Random() % h);
  SetRect(&r, x, y, x + 20 + (unsigned short)Random() % 80,
          y + 20 + (unsigned short)Random() % 60);
  c.red = (unsigned short)Random();
  c.green = (unsigned short)Random();
  c.blue = (unsigned short)Random();
  RGBForeColor(&c);
  PaintRect(&r);
}

/* The screen saver: one window over the whole screen. */
int main(void) {
  WindowPtr window;
  Rect bounds;
  Point start, now;
  EventRecord event;
  RgnHandle menuBar = NULL;

  MaxApplZone();
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(NULL);
  AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                        NewAEEventHandlerUPP(QuitHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
  qd.randSeed = (long)TickCount();
  bounds = qd.screenBits.bounds;
  window = NewCWindow(NULL, &bounds, (ConstStringPtr)"\x00", true, plainDBox,
                      (WindowPtr)-1, false, 0);
  SetPort(window);
  /* Draw over the menu bar too. */
  (void)menuBar;
  BackColor(blackColor);
  EraseRect(&window->portRect);
  HideCursor();
  GetMouse(&start);
  LocalToGlobal(&start);

  while (!gQuit) {
    if (WaitNextEvent(everyEvent, &event, 2, NULL)) {
      switch (event.what) {
      case mouseDown:
      case keyDown:
        gQuit = true;
        break;
      case kHighLevelEvent:
        AEProcessAppleEvent(&event);
        break;
      case updateEvt:
        BeginUpdate((WindowPtr)event.message);
        EraseRect(&window->portRect);
        EndUpdate((WindowPtr)event.message);
        break;
      }
    }
    SetPort(window);
    DrawFrame(&window->portRect);
    GetMouse(&now);
    LocalToGlobal(&now);
    if (now.h - start.h > 4 || start.h - now.h > 4 ||
        now.v - start.v > 4 || start.v - now.v > 4)
      gQuit = true;
  }
  ShowCursor();
  ExitToShell();
  return 0;
}
