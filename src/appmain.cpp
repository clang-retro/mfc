// appmain.cpp - the program entry point. MFC programs have no WinMain of
// their own; the linker takes this one from the library.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

#ifndef _AFX_BUILDING_DLL  // not in mfc.dll: each program has its own WinMain, from libmfcs.a

#ifdef _UNICODE
extern "C" int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine,
                               int nCmdShow) {
  return AfxWinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}

// Without -municode the C runtime calls WinMain even in a Unicode program,
// so this one finds the wide command line itself.
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow) {
  LPWSTR p = ::GetCommandLineW();
  if (*p == L'"') {
    for (p++; *p != L'\0' && *p != L'"'; p++) {}
    if (*p == L'"') p++;
  } else {
    while (*p > L' ') p++;
  }
  while (*p != L'\0' && *p <= L' ') p++;
  return AfxWinMain(hInstance, hPrevInstance, p, nCmdShow);
}
#else
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
                              int nCmdShow) {
  return AfxWinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
#endif

#endif  // _AFX_BUILDING_DLL
