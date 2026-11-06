// appmain.cpp - the program entry point. MFC programs have no WinMain of
// their own; the linker takes this one from the library.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

#ifdef _UNICODE
extern "C" int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine,
                               int nCmdShow) {
  return AfxWinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
#else
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
                              int nCmdShow) {
  return AfxWinMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
#endif
