/* saver_common.c - shared code of the screen saver host of the classic
 * Mac OS. See saver_common.h. */
#include "saver_common.h"

#include <AppleEvents.h>
#include <Folders.h>
#include <Memory.h>

/* The name of the folder of the screen savers, in the System Folder. */
static const unsigned char kFolderName[] = "\x0D" "Screen Savers";
/* The name of the settings file, in the Preferences folder. */
static const unsigned char kPrefsName[] = "\x18" "Screen Saver Preferences";

/* Copies the Pascal string src into dst. */
void SaverCopyPString(StringPtr dst, ConstStr255Param src) {
  BlockMoveData(src, dst, src[0] + 1);
}

/* Gives true when the Pascal strings a and b are equal. */
Boolean SaverEqualPString(ConstStr255Param a, ConstStr255Param b) {
  short i;
  if (a[0] != b[0]) return false;
  for (i = 1; i <= a[0]; i++)
    if (a[i] != b[i]) return false;
  return true;
}

/* Puts the default settings into prefs: on, 5 minutes, no screen saver. */
void SaverPrefsDefault(SaverPrefs *prefs) {
  prefs->magic = SAVER_PREFS_MAGIC;
  prefs->version = SAVER_PREFS_VERSION;
  prefs->enabled = 1;
  prefs->delayMinutes = 5;
  prefs->saverName[0] = 0;
}

/* Makes the FSSpec of the settings file. The file does not need to
 * exist. */
static OSErr PrefsSpec(FSSpec *spec) {
  short vRefNum;
  long dirID;
  OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, true,
                         &vRefNum, &dirID);
  if (err != noErr) return err;
  err = FSMakeFSSpec(vRefNum, dirID, kPrefsName, spec);
  return err == fnfErr ? noErr : err;
}

/* Gives the modification date of the settings file, or 0. */
unsigned long SaverPrefsDate(void) {
  FSSpec spec;
  CInfoPBRec pb;
  if (PrefsSpec(&spec) != noErr) return 0;
  BlockZero(&pb, sizeof pb);
  pb.hFileInfo.ioNamePtr = spec.name;
  pb.hFileInfo.ioVRefNum = spec.vRefNum;
  pb.hFileInfo.ioDirID = spec.parID;
  if (PBGetCatInfoSync(&pb) != noErr) return 0;
  return pb.hFileInfo.ioFlMdDat;
}

/* Reads the settings file into prefs. */
OSErr SaverPrefsRead(SaverPrefs *prefs, unsigned long *modDate) {
  FSSpec spec;
  short ref;
  long count = sizeof *prefs;
  OSErr err;
  if (modDate) *modDate = SaverPrefsDate();
  err = PrefsSpec(&spec);
  if (err == noErr) err = FSpOpenDF(&spec, fsRdPerm, &ref);
  if (err == noErr) {
    err = FSRead(ref, &count, prefs);
    FSClose(ref);
  }
  if (err == noErr && (count != sizeof *prefs ||
                       prefs->magic != SAVER_PREFS_MAGIC ||
                       prefs->version != SAVER_PREFS_VERSION ||
                       prefs->saverName[0] > 63))
    err = eofErr;
  if (err != noErr) SaverPrefsDefault(prefs);
  return err;
}

/* Writes prefs into the settings file. */
OSErr SaverPrefsWrite(const SaverPrefs *prefs) {
  FSSpec spec;
  short ref;
  long count = sizeof *prefs;
  OSErr err = PrefsSpec(&spec);
  if (err != noErr) return err;
  err = FSpCreate(&spec, SAVER_CREATOR, SAVER_PREFS_TYPE, smSystemScript);
  if (err != noErr && err != dupFNErr) return err;
  err = FSpOpenDF(&spec, fsRdWrPerm, &ref);
  if (err != noErr) return err;
  err = SetEOF(ref, 0);
  if (err == noErr) err = FSWrite(ref, &count, prefs);
  FSClose(ref);
  return err;
}

/* Finds the "Screen Savers" folder in the System Folder. */
OSErr SaverFolder(short *vRefNum, long *dirID, Boolean create) {
  long sysDir;
  CInfoPBRec pb;
  Str63 name;
  OSErr err = FindFolder(kOnSystemDisk, kSystemFolderType, false, vRefNum,
                         &sysDir);
  if (err != noErr) return err;
  SaverCopyPString(name, kFolderName);
  BlockZero(&pb, sizeof pb);
  pb.dirInfo.ioNamePtr = name;
  pb.dirInfo.ioVRefNum = *vRefNum;
  pb.dirInfo.ioDrDirID = sysDir;
  err = PBGetCatInfoSync(&pb);
  if (err == noErr) {
    if (!(pb.dirInfo.ioFlAttrib & ioDirMask)) return dirNFErr;
    *dirID = pb.dirInfo.ioDrDirID;
    return noErr;
  }
  if (err != fnfErr || !create) return err;
  return DirCreate(*vRefNum, sysDir, kFolderName, dirID);
}

/* Finds the screen saver program with the file name name. */
OSErr SaverFind(ConstStr255Param name, FSSpec *spec) {
  short vRefNum;
  long dirID;
  OSErr err;
  if (name[0] == 0) return fnfErr;
  err = SaverFolder(&vRefNum, &dirID, false);
  if (err != noErr) return err;
  return FSMakeFSSpec(vRefNum, dirID, name, spec);
}

/* Compares the Pascal strings a and b by their characters. Gives a
 * negative number, 0 or a positive number. */
static short ComparePString(ConstStr255Param a, ConstStr255Param b) {
  short i, n = a[0] < b[0] ? a[0] : b[0];
  for (i = 1; i <= n; i++)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return a[0] - b[0];
}

/* Puts the names of the applications in the "Screen Savers" folder into
 * names, sorted. */
short SaverList(Str63 names[], short max) {
  short vRefNum, count = 0, index, i;
  long dirID;
  CInfoPBRec pb;
  Str63 name;
  if (SaverFolder(&vRefNum, &dirID, false) != noErr) return 0;
  for (index = 1; count < max; index++) {
    BlockZero(&pb, sizeof pb);
    pb.hFileInfo.ioNamePtr = name;
    pb.hFileInfo.ioVRefNum = vRefNum;
    pb.hFileInfo.ioDirID = dirID;
    pb.hFileInfo.ioFDirIndex = index;
    if (PBGetCatInfoSync(&pb) != noErr) break;
    if (pb.hFileInfo.ioFlAttrib & ioDirMask) continue;
    if (pb.hFileInfo.ioFlFndrInfo.fdType != 'APPL') continue;
    /* Insertion sort: the folder is small. */
    for (i = count; i > 0 && ComparePString(names[i - 1], name) > 0; i--)
      SaverCopyPString(names[i], names[i - 1]);
    SaverCopyPString(names[i], name);
    count++;
  }
  return count;
}

/* Starts the program spec in the background. */
OSErr SaverLaunch(FSSpec *spec, ProcessSerialNumber *psn) {
  LaunchParamBlockRec pb;
  OSErr err;
  BlockZero(&pb, sizeof pb);
  pb.launchBlockID = extendedBlock;
  pb.launchEPBLength = extendedBlockLen;
  /* launchContinue: this program keeps running. launchNoFileFlags: do
   * not read the flags of the file (it is an application). */
  pb.launchControlFlags = launchContinue | 0x0800;
  pb.launchAppSpec = spec;
  err = LaunchApplication(&pb);
  if (err != noErr) return err;
  /* When the caller is the front process (for example After Dark or the
   * control panel), the new process can stay in the background, behind
   * the windows of the caller. A screen saver must be in front. */
  SetFrontProcess(&pb.launchProcessSN);
  if (psn) *psn = pb.launchProcessSN;
  return noErr;
}

/* Gives true when the process psn is running. */
Boolean SaverRunning(ProcessSerialNumber *psn) {
  ProcessInfoRec info;
  BlockZero(&info, sizeof info);
  info.processInfoLength = sizeof info;
  return GetProcessInformation(psn, &info) == noErr;
}

/* Finds the running process of the program spec. */
Boolean SaverFindProcess(const FSSpec *spec, ProcessSerialNumber *psn) {
  ProcessSerialNumber p;
  ProcessInfoRec info;
  FSSpec app;
  p.highLongOfPSN = 0;
  p.lowLongOfPSN = kNoProcess;
  while (GetNextProcess(&p) == noErr) {
    BlockZero(&info, sizeof info);
    info.processInfoLength = sizeof info;
    info.processAppSpec = &app;
    if (GetProcessInformation(&p, &info) != noErr) continue;
    if (app.vRefNum == spec->vRefNum && app.parID == spec->parID &&
        SaverEqualPString(app.name, spec->name)) {
      *psn = p;
      return true;
    }
  }
  return false;
}

/* Sends the Apple event "quit" to the process psn. */
OSErr SaverQuit(ProcessSerialNumber *psn) {
  AEAddressDesc target;
  AppleEvent event, reply;
  OSErr err = AECreateDesc(typeProcessSerialNumber, psn, sizeof *psn,
                           &target);
  if (err != noErr) return err;
  err = AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &target,
                           kAutoGenerateReturnID, kAnyTransactionID, &event);
  AEDisposeDesc(&target);
  if (err != noErr) return err;
  err = AESend(&event, &reply, kAENoReply, kAENormalPriority,
               kAEDefaultTimeout, NULL, NULL);
  AEDisposeDesc(&event);
  return err;
}
