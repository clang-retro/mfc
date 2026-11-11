// crt0.cpp - the entry point of a Windows program: main calls WinMain
// with the module, the command line after the program's name and the
// show state. A program that has its own main does not link this.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

// Skips the program's name by the Windows rules: quoted or up to a
// space, then the spaces after it.
static LPSTR SkipProgramName(LPSTR p) {
  if (*p == '"') {
    p++;
    while (*p && *p != '"') p++;
    if (*p) p++;
  } else {
    while (*p && *p != ' ' && *p != '\t') p++;
  }
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

int main(int, char**) {
  STARTUPINFOA si;
  GetStartupInfoA(&si);
  int show = (si.dwFlags & STARTF_USESHOWWINDOW) ? si.wShowWindow : 10;  // SW_SHOWDEFAULT
  return WinMain(GetModuleHandleA(NULL), NULL, SkipProgramName(GetCommandLineA()), show);
}
