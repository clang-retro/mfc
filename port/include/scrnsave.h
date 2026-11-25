// scrnsave.h - the screen saver library (libscrnsave.a, and
// libscrnsavw.a for UNICODE programs). The library has WinMain: it reads
// the command line, and shows the configuration dialog (/c, or no
// argument), the screen saver over the whole screen (/s) or a preview in
// a window (/p hwnd). The program gives ScreenSaverProc,
// ScreenSaverConfigureDialog and RegisterDialogClasses.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SCRNSAVE_H
#define _WINPORT_SCRNSAVE_H

#include <windows.h>

// The resources that the program gives.
#define IDS_DESCRIPTION 1      // the name of the screen saver
#define ID_APP 100             // its icon
#define DLG_SCRNSAVECONFIGURE 2003  // its configuration dialog

// The strings of the library (the program can give them too).
#define idsIsPassword 1000
#define idsIniFile 1001
#define idsScreenSaver 1002
#define idsPassword 1003
#define idsDifferentPW 1004
#define idsChangePW 1005
#define idsBadOldPW 1006
#define idsAppName 1007
#define idsNoHelpMemory 1008
#define idsHelpFile 1009
#define idsDefKeyword 1010

// The sizes of the library's strings.
#define MAXFILELEN 13
#define TITLEBARNAMELEN 40
#define APPNAMEBUFFERLEN 40
#define BUFFLEN 255

// The message that asks the screen saver to check the password.
#define SCRM_VERIFYPW WM_APP

#ifdef __cplusplus
#ifdef __cplusplus
extern "C" {
#endif
#endif

#ifdef UNICODE
LRESULT WINAPI ScreenSaverProcW(HWND, UINT, WPARAM, LPARAM);
#define ScreenSaverProc ScreenSaverProcW
#else
LRESULT WINAPI ScreenSaverProc(HWND, UINT, WPARAM, LPARAM);
#endif

// The default window procedure of a screen saver: it closes the screen
// saver when the user moves the mouse, clicks or types, and hides the
// cursor.
LRESULT WINAPI DefScreenSaverProc(HWND, UINT, WPARAM, LPARAM);

// The dialog procedure of the configuration dialog (the program's).
BOOL WINAPI ScreenSaverConfigureDialog(HWND, UINT, WPARAM, LPARAM);

// Registers the window classes of the configuration dialog (the
// program's). FALSE stops the dialog.
BOOL WINAPI RegisterDialogClasses(HANDLE);

// Changes the password. There are no passwords here: it does nothing.
void WINAPI ScreenSaverChangePassword(HWND);

// The library's state, for the program to read.
extern HINSTANCE hMainInstance;  // the program
extern HWND hMainWindow;         // the screen saver's window
extern BOOL fChildPreview;       // TRUE in a preview window (/p)
extern TCHAR szName[TITLEBARNAMELEN];      // IDS_DESCRIPTION
extern TCHAR szAppName[APPNAMEBUFFERLEN];  // idsAppName, or szName
extern TCHAR szIniFile[MAXFILELEN];        // idsIniFile
extern TCHAR szScreenSaver[22];            // idsScreenSaver
extern TCHAR szHelpFile[MAXFILELEN];       // idsHelpFile
extern TCHAR szNoHelpMemory[BUFFLEN];      // idsNoHelpMemory
extern UINT MyHelpMessage;                 // the help message

#ifdef __cplusplus
#ifdef __cplusplus
}  // extern "C"
#endif
#endif

#endif
