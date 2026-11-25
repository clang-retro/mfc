// GL/glu.h - the OpenGL utility library for Win32 programs. On Mac OS X
// this is the OpenGL framework's <OpenGL/glu.h>; elsewhere it is the
// system's <GL/glu.h>.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_GL_GLU_H
#define WINPORT_GL_GLU_H
#include <GL/gl.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/glu.h>
#else
#include_next <GL/glu.h>
#endif
#endif
