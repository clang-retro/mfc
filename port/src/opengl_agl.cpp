// opengl_agl.cpp - the OpenGL backend of opengl.cpp on the classic Mac
// OS for PowerPC: Apple OpenGL through AGL. Each context draws off screen
// (aglSetOffScreen) into the buffer that opengl.cpp gives it; opengl.cpp
// copies the buffer to the window with GDI, as on Mac OS X.
//
// Apple OpenGL is a weak import (mac-sdk's AGL/agl.h and GL/gl.h): on a
// Mac without it, the functions are NULL and Make gives NULL, so
// wglCreateContext fails. The system's glFlush and glFinish, and the
// functions that wglGetProcAddress gives, come from OpenGLLibrary
// through the Code Fragment Manager, as opengl.cpp has glFlush and
// glFinish of its own.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "opengl_backend.h"

#ifdef WINPORT_AGL

#include <AGL/agl.h>
#include <string.h>

// The Toolbox headers have no C++ linkage of their own: on PowerPC the
// functions are C symbols of InterfaceLib.
extern "C" {
#include <CodeFragments.h>
}

namespace winport {
namespace gl {

namespace {

// Gives the connection to OpenGLLibrary, made once, or NULL.
ConnectionID Library() {
  static ConnectionID conn = NULL;
  static bool tried = false;
  if (!tried) {
    tried = true;
    Ptr entry = NULL;
    Str255 err;
    if (GetSharedLibrary("\pOpenGLLibrary", kPowerPCArch, kReferenceCFrag, &conn, &entry, err) !=
        noErr)
      conn = NULL;
  }
  return conn;
}

// Chooses an off-screen pixel format with the attributes in attr.
AGLPixelFormat Choose(const GLint* attr) { return aglChoosePixelFormat(NULL, 0, attr); }

}  // namespace

// Makes an off-screen AGL context (see opengl_backend.h).
Native Make(Native share) {
  if ((void*)aglChoosePixelFormat == NULL) return NULL;
  GLint full[] = {AGL_RGBA,       AGL_OFFSCREEN, AGL_PIXEL_SIZE,   32, AGL_ALPHA_SIZE, 8,
                  AGL_DEPTH_SIZE, 24,            AGL_STENCIL_SIZE, 8,  AGL_NONE};
  AGLPixelFormat pf = Choose(full);
  if (!pf) {
    // The software renderer of Apple OpenGL 1.1 has no stencil buffer
    // and a 16-bit depth buffer.
    GLint small[] = {AGL_RGBA, AGL_OFFSCREEN, AGL_PIXEL_SIZE, 32, AGL_DEPTH_SIZE, 16, AGL_NONE};
    pf = Choose(small);
  }
  if (!pf) return NULL;
  AGLContext ctx = aglCreateContext(pf, (AGLContext)share);
  aglDestroyPixelFormat(pf);
  return ctx;
}

// Destroys a context (see opengl_backend.h).
void Destroy(Native ctx) {
  if (!ctx) return;
  if (aglGetCurrentContext() == (AGLContext)ctx) aglSetCurrentContext(NULL);
  aglDestroyContext((AGLContext)ctx);
}

// Makes ctx current (see opengl_backend.h).
bool SetCurrent(Native ctx) {
  if ((void*)aglSetCurrentContext == NULL) return ctx == NULL;
  return aglSetCurrentContext((AGLContext)ctx) != GL_FALSE;
}

// Points ctx at a buffer (see opengl_backend.h). AGL's 32-bit off-screen
// pixels are 0xAARRGGBB in big-endian order, top row first.
bool SetBuffer(Native ctx, int w, int h, void* pixels) {
  return aglSetOffScreen((AGLContext)ctx, w, h, w * 4, pixels) != GL_FALSE;
}

// Copies context state (see opengl_backend.h).
bool Copy(Native from, Native to, unsigned mask) {
  return aglCopyContext((AGLContext)from, (AGLContext)to, mask) != GL_FALSE;
}

// Calls OpenGLLibrary's glFinish or glFlush (see opengl_backend.h).
void RealFlush(bool finish) {
  typedef void (*Fn)(void);
  static Fn flush = (Fn)FindProc("glFlush");
  static Fn fin = (Fn)FindProc("glFinish");
  if (!aglGetCurrentContext()) return;
  Fn f = finish ? fin : flush;
  if (f) f();
}

// Finds a function of OpenGLLibrary by name (see opengl_backend.h).
void* FindProc(const char* name) {
  ConnectionID conn = Library();
  size_t n = name ? strlen(name) : 0;
  if (!conn || n == 0 || n > 255) return NULL;
  Str255 pname;
  pname[0] = (unsigned char)n;
  memcpy(pname + 1, name, n);
  Ptr addr = NULL;
  SymClass cls = 0;
  if (FindSymbol(conn, pname, &addr, &cls) != noErr) return NULL;
  return addr;
}

}  // namespace gl
}  // namespace winport

#endif  // WINPORT_AGL
