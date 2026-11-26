/* adhost.c - a small After Dark host, to test the After Dark modules of
 * port/saver/macos/ad_module.c without After Dark.
 *
 * It opens the module "System Folder:Screen Savers:Test Saver AD", calls
 * it as After Dark does (Initialize, Blank, DrawFrame, Close), and checks
 * that the module starts and stops the program "Test Saver". Between two
 * calls, it moves the code of the module in memory, to test the
 * relocation of the code resource. It shows the results in its window and
 * ends with "ADHOST PASSED" or "ADHOST FAILED".
 */
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Windows.h>

#include "../../port/saver/macos/saver_common.h"

QDGlobals qd;

short CallModule(Ptr code, Handle *storage, RgnHandle blankRgn,
                 short message, void *params);

#define MAX_LINES 32

static WindowPtr gWindow;
static Str255 gLines[MAX_LINES]; /* the log, to draw it again */
static short gLineCount;
static short gFails;

/* The parameter block of After Dark (68k alignment). */
#pragma pack(push, 2)
typedef struct GMParamBlock {
  short controlValues[4];
  void *monitors;
  Boolean colorQDAvail;
  short systemConfig;
  void *qdGlobalsCopy;
  short brightness;
  Rect demoRect;
  StringPtr errorMessage;
  void *sndChannel;
  short adVersion;
  void *extensions;
} GMParamBlock;
#pragma pack(pop)

/* Copies the C string s into the Pascal string p. */
static void CToP(StringPtr p, const char *s) {
  short n = 0;
  while (s[n] && n < 255) { p[n + 1] = (unsigned char)s[n]; n++; }
  p[0] = (unsigned char)n;
}

/* Draws the log into the window. */
static void DrawLog(void) {
  short i;
  SetPort(gWindow);
  for (i = 0; i < gLineCount; i++) {
    MoveTo(10, 20 + 15 * i);
    DrawString(gLines[i]);
  }
}

/* Adds a line of text, and the number n in hexadecimal when n is not -1,
 * to the log, and draws it. */
static void Log(const char *text, long n) {
  StringPtr s;
  if (gLineCount == MAX_LINES) return;
  s = gLines[gLineCount];
  CToP(s, text);
  if (n != -1) {
    short sh;
    s[++s[0]] = ' ';
    for (sh = 28; sh >= 0; sh -= 4)
      s[++s[0]] = "0123456789ABCDEF"[(n >> sh) & 15];
  }
  SetPort(gWindow);
  MoveTo(10, 20 + 15 * gLineCount);
  DrawString(s);
  gLineCount++;
}

/* Writes the result of a check, and counts the failures. */
static void Check(const char *text, Boolean ok) {
  Log(text, -1);
  Log(ok ? "    ok" : "    FAIL", -1);
  if (!ok) gFails++;
}

/* Lets the other programs run for the time ticks. Handles the update
 * events: the Process Manager brings another program to the front only
 * when this program has no events left. */
static void Wait(long ticks) {
  unsigned long end = TickCount() + ticks;
  EventRecord event;
  while (TickCount() < end) {
    if (WaitNextEvent(everyEvent, &event, 6, NULL) &&
        event.what == updateEvt) {
      BeginUpdate((WindowPtr)event.message);
      DrawLog();
      EndUpdate((WindowPtr)event.message);
    }
  }
}

/* Gives true when the program "Test Saver" runs. */
static Boolean SaverIsRunning(void) {
  Str63 name;
  FSSpec spec;
  ProcessSerialNumber psn;
  CToP(name, "Test Saver");
  return SaverFind(name, &spec) == noErr && SaverFindProcess(&spec, &psn);
}

/* Moves the locked handle h to another address, and locks it again. */
static void MoveCode(Handle h) {
  Handle block = NewHandle(4096);
  HUnlock(h);
  MoveHHi(h);
  HLock(h);
  if (block) DisposeHandle(block);
}

/* The host: calls the module and shows the results. */
int main(void) {
  Rect r;
  Str255 s, error;
  short vRefNum, ref, result, i;
  long dirID;
  FSSpec spec;
  Handle code, storage = NULL;
  RgnHandle rgn;
  GMParamBlock params;
  Ptr first;

  MaxApplZone();
  InitGraf(&qd.thePort);
  InitFonts();
  InitWindows();
  InitMenus();
  TEInit();
  InitDialogs(NULL);
  InitCursor();
  SetRect(&r, 40, 60, 600, 440);
  CToP(s, "After Dark host test");
  gWindow = NewWindow(NULL, &r, s, true, noGrowDocProc, (WindowPtr)-1,
                      false, 0);
  SetPort(gWindow);
  TextFont(4); /* Monaco */
  TextSize(9);

  if (SaverFolder(&vRefNum, &dirID, false) != noErr) {
    Check("Screen Savers folder", false);
    goto done;
  }
  CToP(s, "Test Saver AD");
  FSMakeFSSpec(vRefNum, dirID, s, &spec);
  ref = FSpOpenResFile(&spec, fsRdPerm);
  Check("open the module file", ref != -1);
  if (ref == -1) goto done;
  code = Get1IndResource('ADgm', 1);
  Check("code resource ADgm", code != NULL);
  if (!code) goto done;
  HNoPurge(code);
  HLock(code);
  first = *code;
  Log("module at", (long)first);

  rgn = NewRgn();
  RectRgn(rgn, &qd.screenBits.bounds);
  BlockZero(&params, sizeof params);
  error[0] = 0;
  params.errorMessage = error;
  params.adVersion = 0x0300;
  params.colorQDAvail = true;

  result = CallModule(*code, &storage, rgn, 0 /* Initialize */, &params);
  Log("Initialize gives", result);
  if (error[0] && gLineCount < MAX_LINES)
    SaverCopyPString(gLines[gLineCount++], error);
  Check("Initialize gives noErr", result == 0 && storage != NULL);
  Wait(240);
  Check("Test Saver runs", SaverIsRunning());

  result = CallModule(*code, &storage, rgn, 2 /* Blank */, &params);
  Check("Blank gives noErr", result == 0);
  MoveCode(code);
  Log("module now at", (long)*code);
  for (i = 0; i < 10; i++) {
    result = CallModule(*code, &storage, rgn, 3 /* DrawFrame */, &params);
    if (result != 0) break;
    Wait(6);
  }
  Check("DrawFrame gives noErr", result == 0);
  result = CallModule(*code, &storage, rgn, 1 /* Close */, &params);
  Check("Close gives noErr", result == 0);
  Wait(300);
  Check("Test Saver stopped", !SaverIsRunning());
  CloseResFile(ref);

done:
  SelectWindow(gWindow);
  Log(gFails ? "ADHOST FAILED" : "ADHOST PASSED", -1);
  while (!Button()) Wait(30);
  return 0;
}
