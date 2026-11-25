// opengl_backend.h - the system OpenGL under the wgl functions of
// opengl.cpp. A backend makes rendering contexts that draw into a buffer
// in memory: 32-bit pixels, 0xAARRGGBB in the machine's byte order, top
// row first. opengl.cpp copies the buffer to the window.
//
// Backends: CGL on Mac OS X (in opengl.cpp, WINPORT_CGL) and AGL on the
// classic Mac OS for PowerPC (opengl_agl.cpp, WINPORT_AGL). Without one,
// WINPORT_GL is not defined and the wgl functions fail.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_OPENGL_BACKEND_H
#define WINPORT_OPENGL_BACKEND_H

#if defined(__APPLE__) && defined(__MACH__)
#define WINPORT_CGL 1
#elif defined(macintosh) && (defined(__POWERPC__) || defined(__powerpc__))
#define WINPORT_AGL 1
#endif
#if defined(WINPORT_CGL) || defined(WINPORT_AGL)
#define WINPORT_GL 1
#endif

namespace winport {
namespace gl {

// A rendering context of the system's OpenGL.
typedef void* Native;

// Makes a context that draws in memory, with 8 bits for each of red,
// green, blue and alpha, a depth buffer and (if the renderer has one) a
// stencil buffer. It shares display lists and textures with share, which
// can be NULL. Gives NULL when the system has no OpenGL.
Native Make(Native share);

// Destroys a context. If it is current, no context is current after.
void Destroy(Native ctx);

// Makes ctx the current context, or no context when ctx is NULL. Gives
// false on failure.
bool SetCurrent(Native ctx);

// Points ctx at a buffer of w x h pixels (rowbytes w * 4). Gives false
// on failure.
bool SetBuffer(Native ctx, int w, int h, void* pixels);

// Copies the state in mask from one context to another.
bool Copy(Native from, Native to, unsigned mask);

// Calls the system's glFinish (finish) or glFlush for the current context,
// not the ones of opengl.cpp.
void RealFlush(bool finish);

// Gives the address of a function of the system's OpenGL by its name, or
// NULL.
void* FindProc(const char* name);

}  // namespace gl
}  // namespace winport

#endif
