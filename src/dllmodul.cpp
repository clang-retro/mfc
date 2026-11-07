// dllmodul.cpp - the entry point of a DLL built with the library (an MFC
// "regular DLL", such as a control's .ocx). DLLs have no DllMain of their
// own: afx.h gives each module one that calls _AfxDllMain.
//
// The library is linked statically, so a DLL has its own copy of every
// global, handle map and thread state, apart from those of the program
// and of other DLLs. That is why AFX_MANAGE_STATE has nothing to do.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

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
