// imm.h - the input method manager. There are no input methods here: a
// window has no input context, and the functions do nothing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_IMM_H
#define _WINPORT_IMM_H

#include <windows.h>

DECLARE_HANDLE(HIMC);
DECLARE_HANDLE(HIMCC);

typedef struct tagCOMPOSITIONFORM {
  DWORD dwStyle;
  POINT ptCurrentPos;
  RECT rcArea;
} COMPOSITIONFORM, *PCOMPOSITIONFORM, *LPCOMPOSITIONFORM;
#define CFS_DEFAULT 0x0000
#define CFS_RECT 0x0001
#define CFS_POINT 0x0002
#define CFS_FORCE_POSITION 0x0020
#define GCS_COMPSTR 0x0008
#define GCS_RESULTSTR 0x0800

#ifdef __cplusplus
extern "C" {
#endif
HIMC WINAPI ImmGetContext(HWND);
BOOL WINAPI ImmReleaseContext(HWND, HIMC);
HIMC WINAPI ImmAssociateContext(HWND, HIMC);
HIMC WINAPI ImmCreateContext(void);
BOOL WINAPI ImmDestroyContext(HIMC);
BOOL WINAPI ImmGetOpenStatus(HIMC);
BOOL WINAPI ImmSetOpenStatus(HIMC, BOOL);
BOOL WINAPI ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM);
LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD);
LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD);
BOOL WINAPI ImmIsIME(HKL);
#ifdef __cplusplus
}
#endif
#define ImmGetCompositionString __WINPORT_AW(ImmGetCompositionString)

#endif
