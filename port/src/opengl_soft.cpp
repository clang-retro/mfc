// opengl_soft.cpp - the OpenGL backend of opengl.cpp on the systems that
// have no OpenGL of their own: the generic implementation of port/softgl,
// a software renderer, as GDI Generic is on Windows. Each context draws
// into the buffer that opengl.cpp gives it; opengl.cpp copies the buffer
// to the window with GDI, as for the other backends.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "opengl_backend.h"

#ifdef WINPORT_SOFTGL

#include "softgl.h"

namespace winport {
namespace gl {

// Makes a context of the generic implementation (see opengl_backend.h).
Native Make(Native share) { return sgl_create_context((sgl_context*)share); }

// Destroys a context (see opengl_backend.h).
void Destroy(Native ctx) { sgl_destroy_context((sgl_context*)ctx); }

// Makes ctx current (see opengl_backend.h).
bool SetCurrent(Native ctx) { return sgl_make_current((sgl_context*)ctx) != 0; }

// Points ctx at a buffer (see opengl_backend.h). The generic
// implementation draws 0xAARRGGBB pixels in the machine's byte order, top
// row first, as opengl.cpp wants.
bool SetBuffer(Native ctx, int w, int h, void* pixels) {
  return sgl_set_buffer((sgl_context*)ctx, w, h, pixels, w) != 0;
}

// Copies context state (see opengl_backend.h).
bool Copy(Native from, Native to, unsigned mask) {
  return sgl_copy_context((sgl_context*)from, (sgl_context*)to, mask) != 0;
}

// The generic implementation draws at once: there is nothing to wait
// for (see opengl_backend.h).
void RealFlush(bool) {}

// Gives an extension function of the generic implementation by its name
// (see opengl_backend.h).
void* FindProc(const char* name) { return sgl_get_proc(name); }

}  // namespace gl
}  // namespace winport

#endif  // WINPORT_SOFTGL
