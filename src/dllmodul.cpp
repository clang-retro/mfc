// dllmodul.cpp - the entry point of a DLL built with the library (an MFC
// "regular DLL", such as a control's .ocx). DLLs have no DllMain of their
// own: afx.h gives each module one that calls _AfxDllMain.
//
// With the static library, a DLL has its own copy of every global, handle
// map and thread state, apart from those of the program and of other
// DLLs, so AFX_MANAGE_STATE has nothing to do.
//
// With mfc.dll (_AFXDLL), this file is in libmfcs.a, so each regular DLL
// has its own module state here: its CWinApp, instance and resource
// handles, window classes and OLE factories. The DLL's exports switch to
// it with AFX_MANAGE_STATE(AfxGetStaticModuleState()); its windows and
// COM objects remember it (CCmdTarget::m_pModuleState). Window objects
// and threads stay shared with the program.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

#ifndef _AFXDLL  // the static library; mfc.dll has dllinit.cpp

static void UnregisterClasses(HINSTANCE hInstance) {
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  CStringArray* pList = pModuleState->m_pstrUnregisterList;
  pModuleState->m_pstrUnregisterList = NULL;
  if (pList == NULL) return;
  for (INT_PTR i = 0; i < pList->GetSize(); i++) ::UnregisterClass(pList->GetAt(i), hInstance);
  delete pList;
}

extern "C" BOOL WINAPI _AfxDllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
      pModuleState->m_bDLL = TRUE;
      static TCHAR szNoCommandLine[] = _T("");
      if (!AfxWinInit(hInstance, NULL, szNoCommandLine, SW_SHOWNORMAL)) return FALSE;
      CWinApp* pApp = AfxGetApp();
      if (pApp != NULL && !pApp->InitInstance()) {
        pApp->ExitInstance();
        AfxWinTerm();
        UnregisterClasses(hInstance);
        return FALSE;
      }
      return TRUE;
    }
    case DLL_PROCESS_DETACH: {
      CWinApp* pApp = AfxGetApp();
      if (pApp != NULL) pApp->ExitInstance();
      AfxWinTerm();
      // When the process ends (lpReserved set) Windows takes everything
      // away itself, and the other DLLs may be gone already.
      if (lpReserved == NULL) {
        UnregisterClasses(hInstance);
        AfxTermThreadState();
      }
      return TRUE;
    }
    case DLL_THREAD_DETACH:
      // A thread of the program that used the DLL: its state in this copy
      // of the library.
      AfxTermThreadState();
      return TRUE;
  }
  return TRUE;
}

#endif  // _AFXDLL

#if defined(_AFXDLL) && !defined(_AFX_BUILDING_DLL)  // libmfcs.a

extern "C" IMAGE_DOS_HEADER __ImageBase;

static AFX_MODULE_STATE g_dllModuleState;
static AFX_MODULE_STATE* g_pStateBefore;  // the state before the DLL's set-up

// Whether this copy of the file is in a DLL rather than the program
// (which links it when it calls AfxGetStaticModuleState).
static BOOL IsInDll() { return (HMODULE)&__ImageBase != ::GetModuleHandle(NULL); }

AFX_MODULE_STATE* AFXAPI AfxGetStaticModuleState() {
  return IsInDll() ? &g_dllModuleState : AfxGetAppModuleState();
}

// The window procedure of the DLL's windows.
static LRESULT CALLBACK DllWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  AFX_MAINTAIN_STATE state(&g_dllModuleState);
  return AfxWndProc(hWnd, nMsg, wParam, lParam);
}

// Runs before the DLL's own constructors, so that its CWinApp and OLE
// factories are made in its state. DllMain gives the previous state back.
__attribute__((constructor(101))) static void EnterDllState() {
  if (!IsInDll() || g_pStateBefore != NULL) return;
  g_dllModuleState.m_pfnAfxWndProc = &DllWndProc;
  g_pStateBefore = AfxSetModuleState(&g_dllModuleState);
}

static void UnregisterClasses(HINSTANCE hInstance) {
  CStringArray* pList = g_dllModuleState.m_pstrUnregisterList;
  g_dllModuleState.m_pstrUnregisterList = NULL;
  if (pList == NULL) return;
  for (INT_PTR i = 0; i < pList->GetSize(); i++) ::UnregisterClass(pList->GetAt(i), hInstance);
  delete pList;
}

extern "C" BOOL WINAPI _AfxDllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
  switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
      EnterDllState();
      AFX_MODULE_STATE* pStateBefore = g_pStateBefore;
      g_dllModuleState.m_bDLL = TRUE;
      static TCHAR szNoCommandLine[] = _T("");
      BOOL bResult = AfxWinInit(hInstance, NULL, szNoCommandLine, SW_SHOWNORMAL);
      CWinApp* pApp = AfxGetApp();
      if (bResult && pApp != NULL && !pApp->InitInstance()) {
        pApp->ExitInstance();
        UnregisterClasses(hInstance);
        bResult = FALSE;
      }
      AfxSetModuleState(pStateBefore);
      g_pStateBefore = NULL;
      return bResult;
    }
    case DLL_PROCESS_DETACH: {
      AFX_MAINTAIN_STATE state(&g_dllModuleState);
      CWinApp* pApp = AfxGetApp();
      if (pApp != NULL) pApp->ExitInstance();
      // When the process ends (lpReserved set) Windows takes everything
      // away itself.
      if (lpReserved == NULL) {
        UnregisterClasses(hInstance);
        _AfxTermModuleMaps(&g_dllModuleState);
      }
      return TRUE;
    }
  }
  return TRUE;
}

#endif
