// regstr.h - the names of registry keys and values that Windows 95
// defines; here those that screen savers and the control panel use.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_REGSTR_H
#define _WINPORT_REGSTR_H

#include <windows.h>

#define REGSTR_PATH_SCREENSAVE TEXT("Control Panel\\Desktop")
#define REGSTR_VALUE_USESCRPASSWORD TEXT("ScreenSaveUsePassword")
#define REGSTR_VALUE_SCRPASSWORD TEXT("ScreenSave_Data")
#define REGSTR_VALUE_LOWPOWERACTIVE TEXT("ScreenSaveLowPowerActive")
#define REGSTR_VALUE_POWEROFFACTIVE TEXT("ScreenSavePowerOffActive")
#define REGSTR_PATH_SETUP TEXT("Software\\Microsoft\\Windows\\CurrentVersion")
#define REGSTR_PATH_RUN REGSTR_PATH_SETUP TEXT("\\Run")
#define REGSTR_PATH_EXPLORER REGSTR_PATH_SETUP TEXT("\\Explorer")

#endif
