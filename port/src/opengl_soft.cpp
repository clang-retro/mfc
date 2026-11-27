// opengl_soft.cpp - the OpenGL backend of opengl.cpp on the systems that
// have no OpenGL of their own: the generic implementation of port/softgl,
// a software renderer, as GDI Generic is on Windows. Each context draws
// into the buffer that opengl.cpp gives it; opengl.cpp copies the buffer
// to the window with GDI, as for the other backends.
//
// On Linux the system's OpenGL comes first when its renderer is hardware
// (opengl_egl.cpp, WINPORT_EGL): then every context is made there, and
// the generic implementation draws none.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "opengl_backend.h"

#ifdef WINPORT_SOFTGL

#include "softgl.h"

namespace winport {
namespace gl {

#ifdef WINPORT_EGL
#define WINPORT_ICD(call) \
  if (egl::Use()) return egl::call
#else
#define WINPORT_ICD(call)
#endif

// Makes a context of the generic implementation (see opengl_backend.h).
Native Make(Native share) {
  WINPORT_ICD(Make(share));
  return sgl_create_context((sgl_context*)share);
}

// Destroys a context (see opengl_backend.h).
void Destroy(Native ctx) {
  WINPORT_ICD(Destroy(ctx));
  sgl_destroy_context((sgl_context*)ctx);
}

// Makes ctx current (see opengl_backend.h).
bool SetCurrent(Native ctx) {
  WINPORT_ICD(SetCurrent(ctx));
  return sgl_make_current((sgl_context*)ctx) != 0;
}

// Points ctx at a buffer (see opengl_backend.h). The generic
// implementation draws 0xAARRGGBB pixels in the machine's byte order, top
// row first, as opengl.cpp wants.
bool SetBuffer(Native ctx, int w, int h, void* pixels) {
  WINPORT_ICD(SetBuffer(ctx, w, h, pixels));
  return sgl_set_buffer((sgl_context*)ctx, w, h, pixels, w) != 0;
}

// Copies context state (see opengl_backend.h). EGL has no way to copy
// between contexts.
bool Copy(Native from, Native to, unsigned mask) {
#ifdef WINPORT_EGL
  if (egl::Use()) return false;
#endif
  return sgl_copy_context((sgl_context*)from, (sgl_context*)to, mask) != 0;
}

// The generic implementation draws at once: there is nothing to wait
// for (see opengl_backend.h).
void RealFlush(bool finish) {
  WINPORT_ICD(Flush(finish));
  (void)finish;
}

// The generic implementation draws into the buffer (see
// opengl_backend.h).
void Fetch(Native ctx) {
  WINPORT_ICD(Fetch(ctx));
  (void)ctx;
}

// Gives an extension function of the generic implementation by its name
// (see opengl_backend.h).
void* FindProc(const char* name) {
  WINPORT_ICD(FindProc(name));
  return sgl_get_proc(name);
}

// Whether the system's OpenGL draws (see opengl_backend.h).
bool Accelerated() {
#ifdef WINPORT_EGL
  return egl::Use();
#else
  return false;
#endif
}

// The generic implementation has a 32-bit depth buffer and an 8-bit
// stencil buffer (see opengl_backend.h).
int DepthBits() {
  WINPORT_ICD(DepthBits());
  return 32;
}

int StencilBits() {
  WINPORT_ICD(StencilBits());
  return 8;
}

}  // namespace gl
}  // namespace winport

#endif  // WINPORT_SOFTGL
