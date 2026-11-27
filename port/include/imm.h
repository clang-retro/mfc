// imm.h - the input method manager: input contexts, the composition
// string an input method is building, and where its windows go.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_IMM_H
#define _WINPORT_IMM_H

#include <windows.h>

// Before Windows 98 (WINVER 0x040A), the headers declared the input
// context as a DWORD. Programs of that time store the result of a BOOL
// function in one.
#if defined(WINVER) && WINVER < 0x040A
typedef DWORD HIMC;
typedef DWORD HIMCC;
#else
DECLARE_HANDLE(HIMC);
DECLARE_HANDLE(HIMCC);
#endif

typedef struct tagCOMPOSITIONFORM {
  DWORD dwStyle;
  POINT ptCurrentPos;
  RECT rcArea;
} COMPOSITIONFORM, *PCOMPOSITIONFORM, *LPCOMPOSITIONFORM;
#define CFS_DEFAULT 0x0000
#define CFS_RECT 0x0001
#define CFS_POINT 0x0002
#define CFS_FORCE_POSITION 0x0020
#define CFS_CANDIDATEPOS 0x0040
#define CFS_EXCLUDE 0x0080

typedef struct tagCANDIDATEFORM {
  DWORD dwIndex;
  DWORD dwStyle;
  POINT ptCurrentPos;
  RECT rcArea;
} CANDIDATEFORM, *PCANDIDATEFORM, *LPCANDIDATEFORM;

// What ImmGetCompositionString gives, and what WM_IME_COMPOSITION's
// lParam says has changed.
#define GCS_COMPREADSTR 0x0001
#define GCS_COMPREADATTR 0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR 0x0008
#define GCS_COMPATTR 0x0010
#define GCS_COMPCLAUSE 0x0020
#define GCS_CURSORPOS 0x0080
#define GCS_DELTASTART 0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR 0x0800
#define GCS_RESULTCLAUSE 0x1000
#define CS_INSERTCHAR 0x2000
#define CS_NOMOVECARET 0x4000

// The attributes of the composition's characters.
#define ATTR_INPUT 0x00
#define ATTR_TARGET_CONVERTED 0x01
#define ATTR_CONVERTED 0x02
#define ATTR_TARGET_NOTCONVERTED 0x03
#define ATTR_INPUT_ERROR 0x04
#define ATTR_FIXEDCONVERTED 0x05

// ImmNotifyIME
#define NI_COMPOSITIONSTR 0x0015
#define CPS_COMPLETE 0x0001
#define CPS_CONVERT 0x0002
#define CPS_REVERT 0x0003
#define CPS_CANCEL 0x0004

// WM_IME_SETCONTEXT's lParam: the input method's windows a window shows.
#define ISC_SHOWUICOMPOSITIONWINDOW 0x80000000
#define ISC_SHOWUIGUIDELINE 0x40000000
#define ISC_SHOWUIALLCANDIDATEWINDOW 0x0000000F
#define ISC_SHOWUIALL 0xC000000F

// WM_IME_NOTIFY's wParam
#define IMN_CLOSESTATUSWINDOW 0x0001
#define IMN_OPENSTATUSWINDOW 0x0002
#define IMN_CHANGECANDIDATE 0x0003
#define IMN_CLOSECANDIDATE 0x0004
#define IMN_OPENCANDIDATE 0x0005
#define IMN_SETCONVERSIONMODE 0x0006
#define IMN_SETSENTENCEMODE 0x0007
#define IMN_SETOPENSTATUS 0x0008
#define IMN_SETCANDIDATEPOS 0x0009
#define IMN_SETCOMPOSITIONFONT 0x000A
#define IMN_SETCOMPOSITIONWINDOW 0x000B

// The virtual key of a key an input method has taken.
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

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
BOOL WINAPI ImmGetCompositionWindow(HIMC, LPCOMPOSITIONFORM);
BOOL WINAPI ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM);
BOOL WINAPI ImmGetCandidateWindow(HIMC, DWORD, LPCANDIDATEFORM);
BOOL WINAPI ImmSetCompositionFontA(HIMC, LPLOGFONTA);
BOOL WINAPI ImmSetCompositionFontW(HIMC, LPLOGFONTW);
BOOL WINAPI ImmGetCompositionFontA(HIMC, LPLOGFONTA);
BOOL WINAPI ImmGetCompositionFontW(HIMC, LPLOGFONTW);
LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD);
LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD);
BOOL WINAPI ImmNotifyIME(HIMC, DWORD, DWORD, DWORD);
HWND WINAPI ImmGetDefaultIMEWnd(HWND);
BOOL WINAPI ImmIsIME(HKL);
#ifdef __cplusplus
}
#endif
#define ImmGetCompositionString __WINPORT_AW(ImmGetCompositionString)
#define ImmSetCompositionFont __WINPORT_AW(ImmSetCompositionFont)
#define ImmGetCompositionFont __WINPORT_AW(ImmGetCompositionFont)

#endif
