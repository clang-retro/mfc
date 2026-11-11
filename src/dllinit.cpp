// dllinit.cpp - the entry point of mfc.dll, the shared library.
//
// The program's CWinApp starts from the program's WinMain (appmain.cpp,
// in libmfcs.a), as with the static library, so the DLL has little to do:
// it frees the library's state of each thread that ends.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

#ifdef _AFX_BUILDING_DLL

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD dwReason, LPVOID lpReserved) {
  switch (dwReason) {
    case DLL_THREAD_DETACH:
      AfxTermThreadState();
      break;
    case DLL_PROCESS_DETACH:
      // When the process ends (lpReserved set) Windows takes everything
      // away itself.
      if (lpReserved == NULL) AfxTermThreadState();
      break;
  }
  return TRUE;
}

#endif
