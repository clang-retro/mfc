/* saver_panel.c - "Screen Saver", the control panel of the screen saver
 * host of the classic Mac OS.
 *
 * The control panel is an application (file type 'APPC') in the Control
 * Panels folder. It shows the screen saver programs of the "Screen
 * Savers" folder, and writes the selected screen saver and the idle time
 * into the settings file. The engine ("Screen Saver Engine") reads the
 * settings file. The button "Test" starts the screen saver at once.
 */
#include "saver_common.h"

#include <AppleEvents.h>
#include <Controls.h>
#include <Dialogs.h>
#include <Events.h>
#include <Folders.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>

QDGlobals qd;

enum { kAppleMenu = 128, kFileMenu = 129, kSaverMenu = 130, kDelayMenu = 131 };
enum { kPopupProc = 1008 }; /* popupMenuProc */
enum { kEngineCreator = 'WPse' };

static const short kDelays[] = {1, 2, 3, 5, 10, 15, 20, 30, 45, 60};
#define DELAY_COUNT (short)(sizeof kDelays / sizeof kDelays[0])

static WindowPtr gWindow;
static ControlHandle gEnabled, gSaverPopup, gDelayPopup, gTest;
static MenuHandle gSaverMenu, gDelayMenu;
static Str63 gSavers[SAVER_MAX];
static short gSaverCount;
static SaverPrefs gPrefs;
static Boolean gQuit;
static OSErr gSaveError; /* the result of the last write of the settings */

/* Copies the C string s into the Pascal string p. */
static void CToP(StringPtr p, const char *s) {
  short n = 0;
  while (s[n] && n < 255) { p[n + 1] = (unsigned char)s[n]; n++; }
  p[0] = (unsigned char)n;
}

/* Gives true when the engine runs. */
static Boolean EngineRunning(void) {
  ProcessSerialNumber p = {0, kNoProcess};
  ProcessInfoRec info;
  while (GetNextProcess(&p) == noErr) {
    BlockZero(&info, sizeof info);
    info.processInfoLength = sizeof info;
    if (GetProcessInformation(&p, &info) == noErr &&
        info.processSignature == kEngineCreator)
      return true;
  }
  return false;
}

/* Starts the engine from the Extensions folder when it does not run (for
 * example just after the user installed it). */
static void StartEngine(void) {
  short vRefNum;
  long dirID;
  FSSpec spec;
  Str63 name;
  if (EngineRunning()) return;
  if (FindFolder(kOnSystemDisk, kExtensionFolderType, false, &vRefNum,
                 &dirID) != noErr)
    return;
  CToP(name, "Screen Saver Engine");
  if (FSMakeFSSpec(vRefNum, dirID, name, &spec) == noErr)
    SaverLaunch(&spec, NULL);
}

/* Saves the settings that the controls show. */
static void SaveSettings(void) {
  short item = GetControlValue(gSaverPopup);
  gPrefs.enabled = GetControlValue(gEnabled);
  gPrefs.delayMinutes = kDelays[GetControlValue(gDelayPopup) - 1];
  if (gSaverCount > 0 && item >= 1 && item <= gSaverCount)
    SaverCopyPString(gPrefs.saverName, gSavers[item - 1]);
  gSaveError = SaverPrefsWrite(&gPrefs);
}

/* Starts the selected screen saver at once. */
static void TestSaver(void) {
  FSSpec spec;
  if (SaverFind(gPrefs.saverName, &spec) == noErr)
    SaverLaunch(&spec, NULL);
}

/* Makes the menu bar: the Apple menu and the File menu. */
static void MakeMenus(void) {
  Str255 s;
  MenuHandle m;
  s[0] = 1; s[1] = 0x14; /* the apple character */
  m = NewMenu(kAppleMenu, s);
  CToP(s, "About Screen Saver...");
  AppendMenu(m, s);
  AppendMenu(m, (ConstStringPtr)"\x02(-");
  AppendResMenu(m, 'DRVR');
  InsertMenu(m, 0);
  CToP(s, "File");
  m = NewMenu(kFileMenu, s);
  CToP(s, "Close/W");
  AppendMenu(m, s);
  CToP(s, "Quit/Q");
  AppendMenu(m, s);
  InsertMenu(m, 0);
  DrawMenuBar();
}

/* Makes the window and its controls from the settings. */
static void MakeWindow(void) {
  Rect r;
  Str255 s;
  short i, value;
  SetRect(&r, 0, 0, 360, 170);
  OffsetRect(&r, 60, 80);
  CToP(s, "Screen Saver");
  gWindow = NewWindow(NULL, &r, s, false, noGrowDocProc, (WindowPtr)-1,
                      true, 0);
  SetPort(gWindow);
  TextFont(0);
  TextSize(12);

  SetRect(&r, 20, 16, 340, 34);
  CToP(s, "Start the screen saver when the Mac is idle");
  gEnabled = NewControl(gWindow, &r, s, true, gPrefs.enabled ? 1 : 0, 0, 1,
                        checkBoxProc, 0);

  /* The menus of the popups: not in the menu bar (InsertMenu -1). */
  CToP(s, "Saver");
  gSaverMenu = NewMenu(kSaverMenu, s);
  value = 1;
  gSaverCount = SaverList(gSavers, SAVER_MAX);
  for (i = 0; i < gSaverCount; i++) {
    AppendMenu(gSaverMenu, (ConstStringPtr)"\x01 ");
    SetMenuItemText(gSaverMenu, i + 1, gSavers[i]);
    if (SaverEqualPString(gSavers[i], gPrefs.saverName)) value = i + 1;
  }
  if (gSaverCount == 0) {
    CToP(s, "(no screen savers)");
    AppendMenu(gSaverMenu, (ConstStringPtr)"\x01 ");
    SetMenuItemText(gSaverMenu, 1, s);
  } else if (gPrefs.saverName[0] == 0) {
    SaverCopyPString(gPrefs.saverName, gSavers[0]);
  }
  InsertMenu(gSaverMenu, -1);
  CToP(s, "Delay");
  gDelayMenu = NewMenu(kDelayMenu, s);
  for (i = 0; i < DELAY_COUNT; i++) {
    char text[16];
    short n = kDelays[i], k = 0;
    const char *unit = n == 1 ? " minute" : " minutes";
    if (n >= 10) text[k++] = (char)('0' + n / 10);
    text[k++] = (char)('0' + n % 10);
    while (*unit) text[k++] = *unit++;
    text[k] = 0;
    CToP(s, text);
    AppendMenu(gDelayMenu, (ConstStringPtr)"\x01 ");
    SetMenuItemText(gDelayMenu, i + 1, s);
  }
  InsertMenu(gDelayMenu, -1);

  /* A popup control: min is the menu ID, max is the width of the title. */
  SetRect(&r, 20, 46, 340, 66);
  CToP(s, "Screen saver:");
  gSaverPopup = NewControl(gWindow, &r, s, true, popupTitleLeftJust,
                           kSaverMenu, 110, kPopupProc, 0);
  SetControlValue(gSaverPopup, value);
  SetRect(&r, 20, 76, 340, 96);
  CToP(s, "Start after:");
  gDelayPopup = NewControl(gWindow, &r, s, true, popupTitleLeftJust,
                           kDelayMenu, 110, kPopupProc, 0);
  value = 4; /* 5 minutes */
  for (i = 0; i < DELAY_COUNT; i++)
    if (kDelays[i] == gPrefs.delayMinutes) value = i + 1;
  SetControlValue(gDelayPopup, value);

  SetRect(&r, 250, 106, 340, 126);
  CToP(s, "Test");
  gTest = NewControl(gWindow, &r, s, true, 0, 0, 1, pushButProc, 0);
  ShowWindow(gWindow);
}

/* Draws the text of the window that is not part of a control. */
static void DrawStatus(void) {
  Str255 s;
  Rect r;
  SetRect(&r, 20, 128, 340, 160);
  EraseRect(&r);
  MoveTo(20, 142);
  CToP(s, EngineRunning() ? "The engine is running."
                          : "The engine is not running.");
  DrawString(s);
  MoveTo(20, 158);
  if (gSaveError != noErr) {
    /* For example a locked startup disk. */
    short n = gSaveError < 0 ? -gSaveError : gSaveError, k;
    CToP(s, "The settings were not saved (error -");
    for (k = 1000; k > 0; k /= 10)
      if (n >= k || k == 1) s[++s[0]] = (unsigned char)('0' + n / k % 10);
    s[++s[0]] = ')';
    s[++s[0]] = '.';
  } else {
    CToP(s, "Savers: System Folder:Screen Savers");
  }
  DrawString(s);
}

/* Handles a click in the content of the window. */
static void ContentClick(Point where) {
  ControlHandle control;
  short part;
  SetPort(gWindow);
  GlobalToLocal(&where);
  part = FindControl(where, gWindow, &control);
  if (!part) return;
  if (control == gSaverPopup || control == gDelayPopup) {
    TrackControl(control, where, (ControlActionUPP)-1);
    SaveSettings();
  } else if (TrackControl(control, where, NULL)) {
    if (control == gEnabled) {
      SetControlValue(gEnabled, !GetControlValue(gEnabled));
      SaveSettings();
    } else if (control == gTest) {
      TestSaver();
    }
  }
}

/* Handles a command of the menu bar. */
static void DoMenuCommand(long command) {
  short menu = (short)(command >> 16), item = (short)command;
  if (menu == kAppleMenu) {
    if (item > 2) {
      Str255 name;
      GetMenuItemText(GetMenuHandle(kAppleMenu), item, name);
      OpenDeskAcc(name);
    }
  } else if (menu == kFileMenu) {
    gQuit = true;
  }
  HiliteMenu(0);
}

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

/* The control panel: shows the window and handles the events. */
int main(void) {
  EventRecord event;
  WindowPtr window;
  long lastStatus = 0;

  MaxApplZone();
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(NULL);
  InitCursor();
  AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                        NewAEEventHandlerUPP(QuitHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);

  /* Make the "Screen Savers" folder, so that the user can find it. */
  {
    short vRefNum;
    long dirID;
    SaverFolder(&vRefNum, &dirID, true);
  }
  SaverPrefsRead(&gPrefs, NULL);
  StartEngine();
  MakeMenus();
  MakeWindow();
  SaveSettings(); /* writes the file when it did not exist */

  while (!gQuit) {
    if (!WaitNextEvent(everyEvent, &event, 30, NULL)) {
      /* Show a change of the state of the engine. */
      if (TickCount() - lastStatus > 120) {
        SetPort(gWindow);
        DrawStatus();
        lastStatus = TickCount();
      }
      continue;
    }
    switch (event.what) {
    case mouseDown:
      switch (FindWindow(event.where, &window)) {
      case inMenuBar:
        DoMenuCommand(MenuSelect(event.where));
        break;
      case inSysWindow:
        SystemClick(&event, window);
        break;
      case inDrag:
        DragWindow(window, event.where, &qd.screenBits.bounds);
        break;
      case inGoAway:
        if (TrackGoAway(window, event.where)) gQuit = true;
        break;
      case inContent:
        if (window != FrontWindow()) SelectWindow(window);
        else ContentClick(event.where);
        break;
      }
      break;
    case keyDown:
      if (event.modifiers & cmdKey)
        DoMenuCommand(MenuKey((char)(event.message & charCodeMask)));
      break;
    case updateEvt:
      window = (WindowPtr)event.message;
      BeginUpdate(window);
      SetPort(window);
      DrawControls(window);
      DrawStatus();
      EndUpdate(window);
      break;
    case kHighLevelEvent:
      AEProcessAppleEvent(&event);
      break;
    }
  }
  return 0;
}
