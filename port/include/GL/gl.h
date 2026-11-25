// GL/gl.h - OpenGL for Win32 programs. On Mac OS X this is the OpenGL
// framework's <OpenGL/gl.h>; elsewhere it is the system's <GL/gl.h>.
// Link with -framework OpenGL on Mac OS X.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_GL_GL_H
#define WINPORT_GL_GL_H
#include <windef.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#include_next <GL/gl.h>
#endif

// The extensions that the Windows gl.h declares, with the names of their
// function pointer types. A program finds the functions with
// wglGetProcAddress.
#ifndef GL_EXT_vertex_array
#define GL_EXT_vertex_array 1
#endif
#ifndef GL_EXT_paletted_texture
#define GL_EXT_paletted_texture 1
#endif
#ifndef GL_WIN_swap_hint
#define GL_WIN_swap_hint 1
#endif
#ifndef GL_COLOR_TABLE_FORMAT_EXT
#define GL_COLOR_TABLE_FORMAT_EXT 0x80D8
#define GL_COLOR_TABLE_WIDTH_EXT 0x80D9
#define GL_COLOR_TABLE_RED_SIZE_EXT 0x80DA
#define GL_COLOR_TABLE_GREEN_SIZE_EXT 0x80DB
#define GL_COLOR_TABLE_BLUE_SIZE_EXT 0x80DC
#define GL_COLOR_TABLE_ALPHA_SIZE_EXT 0x80DD
#define GL_COLOR_TABLE_LUMINANCE_SIZE_EXT 0x80DE
#define GL_COLOR_TABLE_INTENSITY_SIZE_EXT 0x80DF
#endif
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX1_EXT 0x80E2
#define GL_COLOR_INDEX2_EXT 0x80E3
#define GL_COLOR_INDEX4_EXT 0x80E4
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_COLOR_INDEX12_EXT 0x80E6
#define GL_COLOR_INDEX16_EXT 0x80E7
#endif
#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef WINGDIAPI
#define WINGDIAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
typedef void(APIENTRY* PFNGLARRAYELEMENTEXTPROC)(GLint i);
typedef void(APIENTRY* PFNGLDRAWARRAYSEXTPROC)(GLenum mode, GLint first, GLsizei count);
typedef void(APIENTRY* PFNGLVERTEXPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count,
                                                 const GLvoid* pointer);
typedef void(APIENTRY* PFNGLNORMALPOINTEREXTPROC)(GLenum type, GLsizei stride, GLsizei count, const GLvoid* pointer);
typedef void(APIENTRY* PFNGLCOLORPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count,
                                                const GLvoid* pointer);
typedef void(APIENTRY* PFNGLINDEXPOINTEREXTPROC)(GLenum type, GLsizei stride, GLsizei count, const GLvoid* pointer);
typedef void(APIENTRY* PFNGLTEXCOORDPOINTEREXTPROC)(GLint size, GLenum type, GLsizei stride, GLsizei count,
                                                   const GLvoid* pointer);
typedef void(APIENTRY* PFNGLEDGEFLAGPOINTEREXTPROC)(GLsizei stride, GLsizei count, const GLboolean* pointer);
typedef void(APIENTRY* PFNGLGETPOINTERVEXTPROC)(GLenum pname, GLvoid** params);
typedef void(APIENTRY* PFNGLADDSWAPHINTRECTWINPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void(APIENTRY* PFNGLCOLORTABLEEXTPROC)(GLenum target, GLenum internalFormat, GLsizei width, GLenum format,
                                              GLenum type, const GLvoid* data);
typedef void(APIENTRY* PFNGLCOLORSUBTABLEEXTPROC)(GLenum target, GLsizei start, GLsizei count, GLenum format,
                                                 GLenum type, const GLvoid* data);
typedef void(APIENTRY* PFNGLGETCOLORTABLEEXTPROC)(GLenum target, GLenum format, GLenum type, GLvoid* data);
typedef void(APIENTRY* PFNGLGETCOLORTABLEPARAMETERIVEXTPROC)(GLenum target, GLenum pname, GLint* params);
typedef void(APIENTRY* PFNGLGETCOLORTABLEPARAMETERFVEXTPROC)(GLenum target, GLenum pname, GLfloat* params);
#endif
