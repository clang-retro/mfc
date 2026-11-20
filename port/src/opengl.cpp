// opengl.cpp - pixel formats and the wgl functions, without OpenGL: a DC
// here has no pixel formats to choose from, so ChoosePixelFormat finds
// none and no rendering context can be made. Each call fails with
// ERROR_NOT_SUPPORTED.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>

namespace {
template <class T>
T Unsupported(T r) {
  SetLastError(ERROR_NOT_SUPPORTED);
  return r;
}
}  // namespace

extern "C" {

int WINAPI ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR*) { return Unsupported(0); }
// No formats: DescribePixelFormat counts them, and there are none.
int WINAPI DescribePixelFormat(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR) { return Unsupported(0); }
int WINAPI GetPixelFormat(HDC) { return Unsupported(0); }
BOOL WINAPI SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR*) { return Unsupported(FALSE); }
BOOL WINAPI SwapBuffers(HDC) { return Unsupported(FALSE); }

HGLRC WINAPI wglCreateContext(HDC) { return Unsupported((HGLRC)NULL); }
HGLRC WINAPI wglCreateLayerContext(HDC, int) { return Unsupported((HGLRC)NULL); }
BOOL WINAPI wglDeleteContext(HGLRC) { return Unsupported(FALSE); }
BOOL WINAPI wglCopyContext(HGLRC, HGLRC, UINT) { return Unsupported(FALSE); }
HGLRC WINAPI wglGetCurrentContext(void) { return NULL; }
HDC WINAPI wglGetCurrentDC(void) { return NULL; }
PROC WINAPI wglGetProcAddress(LPCSTR) { return Unsupported((PROC)NULL); }
// Making no context current succeeds; there is none to begin with.
BOOL WINAPI wglMakeCurrent(HDC, HGLRC rc) { return rc ? Unsupported(FALSE) : TRUE; }
BOOL WINAPI wglShareLists(HGLRC, HGLRC) { return Unsupported(FALSE); }
BOOL WINAPI wglUseFontBitmapsA(HDC, DWORD, DWORD, DWORD) { return Unsupported(FALSE); }
BOOL WINAPI wglUseFontBitmapsW(HDC, DWORD, DWORD, DWORD) { return Unsupported(FALSE); }
BOOL WINAPI wglUseFontOutlinesA(HDC, DWORD, DWORD, DWORD, FLOAT, FLOAT, int, LPGLYPHMETRICSFLOAT) {
  return Unsupported(FALSE);
}
BOOL WINAPI wglUseFontOutlinesW(HDC, DWORD, DWORD, DWORD, FLOAT, FLOAT, int, LPGLYPHMETRICSFLOAT) {
  return Unsupported(FALSE);
}
BOOL WINAPI wglDescribeLayerPlane(HDC, int, int, UINT, LPLAYERPLANEDESCRIPTOR) { return Unsupported(FALSE); }
int WINAPI wglSetLayerPaletteEntries(HDC, int, int, int, const COLORREF*) { return Unsupported(0); }
int WINAPI wglGetLayerPaletteEntries(HDC, int, int, int, COLORREF*) { return Unsupported(0); }
BOOL WINAPI wglRealizeLayerPalette(HDC, int, BOOL) { return Unsupported(FALSE); }
BOOL WINAPI wglSwapLayerBuffers(HDC, UINT) { return Unsupported(FALSE); }

}  // extern "C"
