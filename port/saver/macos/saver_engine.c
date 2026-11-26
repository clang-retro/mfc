/* saver_engine.c - "Screen Saver Engine", the background part of the
 * screen saver host of the classic Mac OS.
 *
 * The engine is a faceless background application (file type 'appe').
 * The Process Manager starts it from the Extensions folder when the Mac
 * starts. It monitors the mouse and the keyboard. When the user does not
 * use them for the time in the settings, it starts the selected screen
 * saver program from the "Screen Savers" folder. The screen saver stops
 * itself when the user moves the mouse or presses a key.
 */
#include "saver_common.h"

#include <AppleEvents.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>

#define POLL_TICKS 30      /* time between two checks (half a second) */
#define PREFS_TICKS 180    /* time between two checks of the settings */

static Boolean gQuit;

/* Handles the Apple event "quit": stops the engine. */
static pascal OSErr QuitHandler(const AppleEvent *event, AppleEvent *reply,
                                long refcon) {
  (void)event; (void)reply; (void)refcon;
  gQuit = true;
  return noErr;
}

/* Handles the other required Apple events. They have nothing to do. */
static pascal OSErr IgnoreHandler(const AppleEvent *event, AppleEvent *reply,
                                  long refcon) {
  (void)event; (void)reply; (void)refcon;
  return noErr;
}

/* Installs the handlers of the required Apple events. */
static void InstallHandlers(void) {
  AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                        NewAEEventHandlerUPP(QuitHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
  AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                        NewAEEventHandlerUPP(IgnoreHandler), 0, false);
}

/* The state of the mouse and the keyboard at the last check. */
typedef struct InputState {
  Point mouse;
  unsigned char keys[16];
  Boolean button;
} InputState;

/* Reads the state of the mouse and the keyboard into state. */
static void ReadInput(InputState *state) {
  state->mouse = LMGetMouseLocation();
  GetKeys(state->keys);
  state->button = Button();
}

/* Gives true when the states a and b are different (the user did
 * something). */
static Boolean InputChanged(const InputState *a, const InputState *b) {
  short i;
  if (a->mouse.h != b->mouse.h || a->mouse.v != b->mouse.v) return true;
  if (a->button != b->button) return true;
  for (i = 0; i < 16; i++)
    if (a->keys[i] != b->keys[i]) return true;
  return false;
}

/* The engine: monitors the input and starts the screen saver. */
int main(void) {
  SaverPrefs prefs;
  unsigned long prefsDate, lastPrefsCheck;
  unsigned long lastActivity;
  InputState last, now;
  ProcessSerialNumber saver;
  Boolean saverOn = false;
  EventRecord event;

  MaxApplZone();
  InstallHandlers();
  SaverPrefsRead(&prefs, &prefsDate);
  lastPrefsCheck = TickCount();
  ReadInput(&last);
  lastActivity = TickCount();

  while (!gQuit) {
    if (WaitNextEvent(everyEvent, &event, POLL_TICKS, NULL) &&
        event.what == kHighLevelEvent)
      AEProcessAppleEvent(&event);

    ReadInput(&now);
    if (InputChanged(&now, &last)) lastActivity = TickCount();
    last = now;

    /* The control panel changed the settings. */
    if (TickCount() - lastPrefsCheck >= PREFS_TICKS) {
      unsigned long date = SaverPrefsDate();
      if (date != prefsDate) {
        SaverPrefsRead(&prefs, &prefsDate);
        lastActivity = TickCount();
      }
      lastPrefsCheck = TickCount();
    }

    if (saverOn) {
      /* The screen saver stopped (the user moved the mouse). The idle
       * time starts again. */
      if (!SaverRunning(&saver)) {
        saverOn = false;
        lastActivity = TickCount();
      }
      continue;
    }
    if (prefs.enabled && prefs.delayMinutes > 0 &&
        TickCount() - lastActivity >=
            (unsigned long)prefs.delayMinutes * 3600) {
      FSSpec spec;
      if (SaverFind(prefs.saverName, &spec) == noErr) {
        if (SaverFindProcess(&spec, &saver) ||
            SaverLaunch(&spec, &saver) == noErr)
          saverOn = true;
      }
      /* Also when the start failed: try again after the next idle time. */
      lastActivity = TickCount();
    }
  }
  return 0;
}
