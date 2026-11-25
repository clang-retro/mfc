// imm.cpp - the input method manager (imm.h). No input methods exist
// here, so no window has an input context and each call does nothing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <imm.h>

extern "C" {

// No window has an input context.
HIMC WINAPI ImmGetContext(HWND) { return NULL; }
// Releasing no context succeeds.
BOOL WINAPI ImmReleaseContext(HWND, HIMC) { return TRUE; }
// Returns the window's previous context, which is always none.
HIMC WINAPI ImmAssociateContext(HWND, HIMC) { return NULL; }
// No context can be made.
HIMC WINAPI ImmCreateContext(void) { return NULL; }
// There is no context to destroy.
BOOL WINAPI ImmDestroyContext(HIMC) { return FALSE; }
// The input method is never open.
BOOL WINAPI ImmGetOpenStatus(HIMC) { return FALSE; }
// Opening or closing no input method fails.
BOOL WINAPI ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
// There is no composition window to place.
BOOL WINAPI ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return FALSE; }
// There is never a composition string.
LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD) { return 0; }
// There is never a composition string.
LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD) { return 0; }
// No keyboard layout is an input method.
BOOL WINAPI ImmIsIME(HKL) { return FALSE; }

}  // extern "C"
