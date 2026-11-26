/* ad_module.c - an After Dark module that starts a screen saver program.
 *
 * After Dark loads a graphics module (file type 'ADgm', creator 'ADrk')
 * and calls its code resource 'ADgm' with a message: Initialize, Blank,
 * DrawFrame (again and again) and Close. This module does not draw. At
 * Initialize, it starts the screen saver program SAVER_NAME from the
 * "Screen Savers" folder of the System Folder. The program shows itself
 * over the whole screen. At Close, the module stops the program (if the
 * program did not stop itself).
 *
 * Build one module for each screen saver, with -DSAVER_NAME="\"name\""
 * (the file name of the program). See ad_entry.S for the entry point.
 */
#include "saver_common.h"

#include <Memory.h>
#include <Quickdraw.h>

#ifndef SAVER_NAME
#error "define SAVER_NAME as the file name of the screen saver program"
#endif

/* The messages of After Dark. */
enum {
  kInitialize = 0,
  kClose = 1,
  kBlank = 2,
  kDrawFrame = 3,
  kModuleSelected = 4,
  kDoAbout = 5
};

/* The result for an error: After Dark shows params->errorMessage. */
enum { kModuleError = -1 };

/* The parameter block of After Dark (68k alignment). Only errorMessage is
 * used. */
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

/* Puts the message text into the error message of After Dark and gives
 * the error result. */
static OSErr Fail(GMParamBlock *params, const char *text) {
  if (params && params->errorMessage) CToP(params->errorMessage, text);
  return kModuleError;
}

/* Starts the screen saver program, and keeps its process in *storage. */
static OSErr StartSaver(Handle *storage, GMParamBlock *params) {
  Str63 name;
  FSSpec spec;
  ProcessSerialNumber psn;
  Handle h;
  CToP(name, SAVER_NAME);
  if (SaverFind(name, &spec) != noErr)
    return Fail(params, "The program \"" SAVER_NAME "\" is not in the "
                        "Screen Savers folder of the System Folder.");
  if (!SaverFindProcess(&spec, &psn) && SaverLaunch(&spec, &psn) != noErr)
    return Fail(params, "The program \"" SAVER_NAME "\" did not start.");
  h = NewHandle(sizeof psn);
  if (!h) return Fail(params, "There is not enough memory.");
  BlockMoveData(&psn, *h, sizeof psn);
  *storage = h;
  return noErr;
}

/* Stops the screen saver program if it still runs, and frees *storage. */
static OSErr StopSaver(Handle storage) {
  ProcessSerialNumber psn;
  if (!storage) return noErr;
  BlockMoveData(*storage, &psn, sizeof psn);
  if (SaverRunning(&psn)) SaverQuit(&psn);
  DisposeHandle(storage);
  return noErr;
}

/* The module: ad_entry.S calls this function for each message. */
OSErr ADModuleMain(Handle *storage, RgnHandle blankRgn, long message,
                   GMParamBlock *params) {
  (void)blankRgn;
  switch (message) {
  case kInitialize:
    return StartSaver(storage, params);
  case kClose:
    return StopSaver(*storage);
  default:
    /* Blank, DrawFrame and the others: the program draws by itself. */
    return noErr;
  }
}
