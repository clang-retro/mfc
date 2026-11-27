// opengl_backend.h - the system OpenGL under the wgl functions of
// opengl.cpp. A backend makes rendering contexts that draw into a buffer
// in memory: 32-bit pixels, 0xAARRGGBB in the machine's byte order, top
// row first. opengl.cpp copies the buffer to the window.
//
// Backends, as the installable client drivers (ICDs) of Windows: CGL on
// Mac OS X (in opengl.cpp, WINPORT_CGL) and AGL on the classic Mac OS for
// PowerPC (opengl_agl.cpp, WINPORT_AGL). On the other systems (Linux, the
// classic Mac OS on 68k) the backend is the generic implementation, a
// software renderer, as GDI Generic is on Windows (opengl_soft.cpp over
// port/softgl, WINPORT_SOFTGL). Define WINPORT_NO_SOFTGL to leave it out:
// then WINPORT_GL is not defined there and the wgl functions fail.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_OPENGL_BACKEND_H
#define WINPORT_OPENGL_BACKEND_H

#if defined(__APPLE__) && defined(__MACH__)
#define WINPORT_CGL 1
#elif defined(macintosh) && (defined(__POWERPC__) || defined(__powerpc__))
#define WINPORT_AGL 1
#elif !defined(WINPORT_NO_SOFTGL)
#define WINPORT_SOFTGL 1
#endif
#if defined(WINPORT_CGL) || defined(WINPORT_AGL) || defined(WINPORT_SOFTGL)
#define WINPORT_GL 1
#endif
// On Linux, the system's OpenGL through EGL comes before the generic
// implementation when its renderer is hardware (opengl_egl.cpp). Define
// WINPORT_NO_EGL to leave it out.
#if defined(WINPORT_SOFTGL) && defined(__linux__) && !defined(WINPORT_NO_EGL)
#define WINPORT_EGL 1
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

// Makes the buffer of ctx, the current context, hold what it drew. A
// backend that draws into the buffer itself does nothing.
void Fetch(Native ctx);

// Whether the renderer is the system's own (an ICD), not the generic
// implementation: the pixel formats then have no PFD_GENERIC_FORMAT.
bool Accelerated();

// The bits of the depth and stencil buffers of a context.
int DepthBits();
int StencilBits();

#ifdef WINPORT_EGL
// The system's OpenGL through EGL (opengl_egl.cpp), with the functions
// above. Use is whether contexts are made there; Hardware is whether its
// renderer is hardware.
namespace egl {
bool Use();
bool Hardware();
int DepthBits();
int StencilBits();
void* Make(void* share);
void Destroy(void* ctx);
bool SetCurrent(void* ctx);
bool SetBuffer(void* ctx, int w, int h, void* pixels);
void Flush(bool finish);
void Fetch(void* ctx);
void* FindProc(const char* name);
}  // namespace egl
#endif

}  // namespace gl
}  // namespace winport

#endif
