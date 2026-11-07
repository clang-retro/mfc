// ole2.h - OLE: COM, automation and OLE documents.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OLE2_H
#define _WINPORT_OLE2_H

#include <objbase.h>
#include <oleauto.h>
#include <oleidl.h>

extern "C" {
HRESULT WINAPI OleInitialize(LPVOID);
void WINAPI OleUninitialize(void);
DWORD WINAPI OleBuildVersion(void);
}

#endif
