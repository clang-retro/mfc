// oledlg.h - for 16-bit Windows: the OLE common dialogs of the portable
// layer (port/include/oledlg.h), with the ANSI types of the 16-bit SDK.
// 16-bit OLE 2 has no OLE dialog DLL; src/win16oleui.cpp gives the
// functions (see docs/win16.md).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <commdlg.h>

// The SDK has the ANSI OPENFILENAME only. The wide structure is not
// defined: only the wide OLE dialog structures point to it.
typedef OPENFILENAME OPENFILENAMEA;
struct tagOFNW;
typedef struct tagOFNW OPENFILENAMEW;

#include_next <oledlg.h>
