// appmainc.cpp - a main() for C libraries whose start-up code only calls
// WinMain when the program defines it itself. picolibc's GUI entry point
// refers to WinMain weakly, which does not pull the library's WinMain in,
// and falls back to main; this main then starts the program the way
// WinMain would. It is in its own file so that a program with its own
// main never links it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

#ifndef _AFX_BUILDING_DLL  // not in mfc.dll: each program has its own main, from libmfcs.a

int main() {
  LPTSTR p = ::GetCommandLine();
  if (*p == _T('"')) {
    for (p++; *p != _T('\0') && *p != _T('"'); p++) {}
    if (*p == _T('"')) p++;
  } else {
    while (*p > _T(' ')) p++;
  }
  while (*p != _T('\0') && *p <= _T(' ')) p++;
  STARTUPINFO si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  ::GetStartupInfo(&si);
  int nCmdShow = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : SW_SHOWDEFAULT;
  return AfxWinMain(::GetModuleHandle(NULL), NULL, p, nCmdShow);
}

#endif  // _AFX_BUILDING_DLL
