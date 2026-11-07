// windows.h - the portable Win32 layer's master header, for building
// Windows programs (and MFC) on Linux and macOS.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINDOWS_H
#define _WINPORT_WINDOWS_H

#include <stdarg.h>
#include <string.h>
#include <windef.h>
#include <winnt.h>
#include <winerror.h>
#include <winbase.h>
#include <winnls.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <winver.h>
#ifndef WIN32_LEAN_AND_MEAN
#include <commdlg.h>
#include <shellapi.h>
#include <winspool.h>
#include <ole2.h>
#endif

#endif
