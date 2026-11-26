/* saver_common.h - shared code of the screen saver host of the classic
 * Mac OS: the settings file, the "Screen Savers" folder, and the start
 * and stop of a screen saver program.
 *
 * The control panel ("Screen Saver"), the background engine ("Screen
 * Saver Engine") and the After Dark modules use this code. A screen saver
 * is an application in the folder "Screen Savers" of the System Folder.
 */
#ifndef SAVER_COMMON_H
#define SAVER_COMMON_H

#include <Files.h>
#include <Processes.h>

#define SAVER_CREATOR 'WPss' /* creator of the host files */
#define SAVER_PREFS_TYPE 'pref'
#define SAVER_PREFS_MAGIC 'WPsp'
#define SAVER_PREFS_VERSION 1
#define SAVER_MAX 32 /* most screen savers in the menu */

/* The settings. The control panel writes them; the engine reads them. The
 * layout is the same on 68k and PowerPC (2-byte alignment), so that the
 * two can share the file. */
#pragma pack(push, 2)
typedef struct SaverPrefs {
  OSType magic;        /* SAVER_PREFS_MAGIC */
  short version;       /* SAVER_PREFS_VERSION */
  short enabled;       /* 1: the engine starts the screen saver */
  short delayMinutes;  /* idle time before the start */
  Str63 saverName;     /* file name of the screen saver program */
} SaverPrefs;
#pragma pack(pop)

/* Puts the default settings into prefs. */
void SaverPrefsDefault(SaverPrefs *prefs);

/* Reads the settings file into prefs. Gives the modification date of the
 * file in modDate (when modDate is not NULL). When there is no valid
 * file, puts the defaults into prefs and gives an error. */
OSErr SaverPrefsRead(SaverPrefs *prefs, unsigned long *modDate);

/* Writes prefs into the settings file. Makes the file when necessary. */
OSErr SaverPrefsWrite(const SaverPrefs *prefs);

/* Gives the modification date of the settings file, or 0. */
unsigned long SaverPrefsDate(void);

/* Finds the "Screen Savers" folder in the System Folder. Makes the folder
 * when create is true and the folder does not exist. */
OSErr SaverFolder(short *vRefNum, long *dirID, Boolean create);

/* Finds the screen saver program with the file name name. */
OSErr SaverFind(ConstStr255Param name, FSSpec *spec);

/* Puts the names of the applications in the "Screen Savers" folder into
 * names, sorted. Gives the number of names (at most max). */
short SaverList(Str63 names[], short max);

/* Starts the program spec in the background. Puts its process into psn. */
OSErr SaverLaunch(FSSpec *spec, ProcessSerialNumber *psn);

/* Gives true when the process psn is running. */
Boolean SaverRunning(ProcessSerialNumber *psn);

/* Finds the running process of the program spec. Gives true and puts the
 * process into psn when it runs. */
Boolean SaverFindProcess(const FSSpec *spec, ProcessSerialNumber *psn);

/* Sends the Apple event "quit" to the process psn. */
OSErr SaverQuit(ProcessSerialNumber *psn);

/* Copies the Pascal string src into dst. */
void SaverCopyPString(StringPtr dst, ConstStr255Param src);

/* Gives true when the Pascal strings a and b are equal (the case of the
 * letters is significant). */
Boolean SaverEqualPString(ConstStr255Param a, ConstStr255Param b);

#endif
