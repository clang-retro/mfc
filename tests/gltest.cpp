// gltest.cpp - OpenGL through wgl: a pixel format for a window, a
// rendering context, a clear to red, SwapBuffers, and the red pixels in
// the window's DC; then wglGetProcAddress and the end of the context.
//
// tests/run-macos-tests.sh runs it on the classic Mac OS for PowerPC,
// over the PowerPC simulator's fake OpenGL (mac-sdk tools/fakegl.py),
// which clears the off-screen buffer. On Linux and the classic Mac OS for
// 68k it runs over the generic implementation (port/softgl), which also
// draws a triangle. Where there is no OpenGL backend,
// ChoosePixelFormat fails with ERROR_NOT_SUPPORTED and the test passes.
//
// On Linux it runs a second time with WINPORT_OPENGL=system, over the
// system's OpenGL through EGL even when its renderer is software
// (llvmpipe), where the system has EGL: the same pictures, a window that
// changes size, and the pixel states of the program kept across the read
// of the picture.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// The size of the test window's client area.
static const int kW = 64, kH = 48;

// Prints the result and gives the exit code.
static int Done() {
  printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
  fflush(stdout);
  return g_failures ? 1 : 0;
}

// Makes a window, draws in it with OpenGL and checks the pixels.
int main(int argc, char** argv) {
  (void)argc;
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "GLTest";
  wc.style = CS_OWNDC;
  CHECK(RegisterClassA(&wc) != 0);
  HWND hwnd = CreateWindowExA(0, "GLTest", "GL test", WS_POPUP | WS_VISIBLE, 0, 0, kW, kH, NULL, NULL,
                              wc.hInstance, NULL);
  CHECK(hwnd != NULL);
  if (!hwnd) return Done();
  HDC dc = GetDC(hwnd);

  PIXELFORMATDESCRIPTOR pfd = {};
  pfd.nSize = sizeof pfd;
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;
  int format = ChoosePixelFormat(dc, &pfd);
  if (!format) {
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    printf("no OpenGL here\n");
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return Done();
  }
  PIXELFORMATDESCRIPTOR got = {};
  CHECK(DescribePixelFormat(dc, format, sizeof got, &got) >= 1);
  CHECK((got.dwFlags & PFD_SUPPORT_OPENGL) && (got.dwFlags & PFD_DOUBLEBUFFER));
  CHECK(SetPixelFormat(dc, format, &got));
  CHECK(GetPixelFormat(dc) == format);

  HGLRC rc = wglCreateContext(dc);
  CHECK(rc != NULL);
  if (!rc) {
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return Done();
  }
  CHECK(wglMakeCurrent(dc, rc));
  CHECK(wglGetCurrentContext() == rc);
  CHECK(wglGetCurrentDC() == dc);
  CHECK(glGetString(GL_VERSION) != NULL);
  const char* renderer = (const char*)glGetString(GL_RENDERER);
  printf("renderer: %s\n", renderer ? renderer : "(none)");
  bool generic = renderer && strstr(renderer, "Generic");
  // The generic implementation's formats are PFD_GENERIC_FORMAT; those of
  // the system's OpenGL on Linux are not.
  if (generic) CHECK(got.dwFlags & PFD_GENERIC_FORMAT);
#ifdef __linux__
  bool icd = renderer && !generic;
  if (icd) CHECK(!(got.dwFlags & PFD_GENERIC_FORMAT));
#else
  bool icd = false;
#endif

  glViewport(0, 0, kW, kH);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  CHECK(SwapBuffers(dc));
  CHECK(GetPixel(dc, 1, 1) == RGB(255, 0, 0));
  CHECK(GetPixel(dc, kW - 2, kH - 2) == RGB(255, 0, 0));

  // The extension functions, found by their names: those of the
  // extensions that GL_EXTENSIONS names.
  const char* ext = (const char*)glGetString(GL_EXTENSIONS);
  CHECK(ext != NULL);
  if (ext && strstr(ext, "GL_ARB_multitexture")) CHECK(wglGetProcAddress("glActiveTextureARB") != NULL);
  if (ext && strstr(ext, "GL_WIN_swap_hint")) CHECK(wglGetProcAddress("glAddSwapHintRectWIN") != NULL);
  if (ext && strstr(ext, "GL_EXT_paletted_texture")) CHECK(wglGetProcAddress("glColorTableEXT") != NULL);
  CHECK(wglGetProcAddress("glNoSuchFunctionXYZ") == NULL);

  // The generic implementation (port/softgl), or the system's OpenGL on
  // Linux, draws: a green triangle on the left half, behind which the red
  // clear stays on the right.
  if (generic || icd) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kW, 0, kH, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2i(0, 0);
    glVertex2i(kW / 2, 0);
    glVertex2i(0, kH);
    glEnd();
    // The program's pixel states do not change how the picture is read,
    // and stay as they are.
    glPixelStorei(GL_PACK_ALIGNMENT, 2);
    glPixelStorei(GL_PACK_ROW_LENGTH, 7);
    // (The generic implementation has no GL_RED_SCALE to read back.)
    if (icd) glPixelTransferf(GL_RED_SCALE, 0.5f);
    CHECK(SwapBuffers(dc));
    CHECK(GetPixel(dc, 2, kH - 3) == RGB(0, 255, 0));
    CHECK(GetPixel(dc, kW - 2, 1) == RGB(255, 0, 0));
    GLint align = 0, rowLength = 0;
    GLfloat scale = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &align);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    CHECK(align == 2 && rowLength == 7);
    if (icd) {
      glGetFloatv(GL_RED_SCALE, &scale);
      CHECK(scale == 0.5f);
      glPixelTransferf(GL_RED_SCALE, 1.0f);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    CHECK(glGetError() == GL_NO_ERROR);

    // A window that grows: SwapBuffers shows the old size once, then the
    // picture has the new size.
    CHECK(SetWindowPos(hwnd, NULL, 0, 0, kW * 2, kH, SWP_NOZORDER | SWP_NOMOVE));
    CHECK(SwapBuffers(dc));
    glViewport(0, 0, kW * 2, kH);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    CHECK(SwapBuffers(dc));
    CHECK(GetPixel(dc, 1, 1) == RGB(0, 0, 255));
    CHECK(GetPixel(dc, kW * 2 - 2, kH - 2) == RGB(0, 0, 255));
    CHECK(glGetError() == GL_NO_ERROR);
  }

  CHECK(wglMakeCurrent(NULL, NULL));
  CHECK(wglGetCurrentContext() == NULL);
  CHECK(wglDeleteContext(rc));
  CHECK(!wglDeleteContext(rc));
  ReleaseDC(hwnd, dc);
  DestroyWindow(hwnd);
#ifdef __linux__
  // Once more over the system's OpenGL.
  if (!getenv("GLTEST_SYSTEM") && argv && argv[0]) {
    setenv("GLTEST_SYSTEM", "1", 1);
    setenv("WINPORT_OPENGL", "system", 1);
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "'%s'", argv[0]);
    printf("again over the system's OpenGL:\n");
    fflush(stdout);
    CHECK(system(cmd) == 0);
  }
#endif
  return Done();
}
