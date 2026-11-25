// GL/glaux.h - the part of the Win32 SDK's OpenGL auxiliary library that
// screen savers and samples use: loading images for textures and drawing
// simple solids. The library is libglaux.a (port/src/glaux.cpp); on Mac
// OS X it needs -framework OpenGL -framework GLUT.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_GL_GLAUX_H
#define WINPORT_GL_GLAUX_H
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

#ifdef __cplusplus
extern "C" {
#endif

// An image: sizeX by sizeY pixels of red, green and blue bytes, the bottom
// row first, as glTexImage2D wants them. Free data, then the record.
typedef struct _AUX_RGBImageRec {
  GLint sizeX, sizeY;
  unsigned char* data;
} AUX_RGBImageRec;

AUX_RGBImageRec* APIENTRY auxRGBImageLoadA(LPCSTR);
AUX_RGBImageRec* APIENTRY auxRGBImageLoadW(LPCWSTR);
AUX_RGBImageRec* APIENTRY auxDIBImageLoadA(LPCSTR);
AUX_RGBImageRec* APIENTRY auxDIBImageLoadW(LPCWSTR);
#ifdef UNICODE
#define auxRGBImageLoad auxRGBImageLoadW
#define auxDIBImageLoad auxDIBImageLoadW
#else
#define auxRGBImageLoad auxRGBImageLoadA
#define auxDIBImageLoad auxDIBImageLoadA
#endif

void APIENTRY auxWireSphere(GLdouble);
void APIENTRY auxSolidSphere(GLdouble);
void APIENTRY auxWireCube(GLdouble);
void APIENTRY auxSolidCube(GLdouble);
void APIENTRY auxWireBox(GLdouble, GLdouble, GLdouble);
void APIENTRY auxSolidBox(GLdouble, GLdouble, GLdouble);
void APIENTRY auxWireTorus(GLdouble, GLdouble);
void APIENTRY auxSolidTorus(GLdouble, GLdouble);
void APIENTRY auxWireCylinder(GLdouble, GLdouble);
void APIENTRY auxSolidCylinder(GLdouble, GLdouble);
void APIENTRY auxWireIcosahedron(GLdouble);
void APIENTRY auxSolidIcosahedron(GLdouble);
void APIENTRY auxWireOctahedron(GLdouble);
void APIENTRY auxSolidOctahedron(GLdouble);
void APIENTRY auxWireTetrahedron(GLdouble);
void APIENTRY auxSolidTetrahedron(GLdouble);
void APIENTRY auxWireDodecahedron(GLdouble);
void APIENTRY auxSolidDodecahedron(GLdouble);
void APIENTRY auxWireCone(GLdouble, GLdouble);
void APIENTRY auxSolidCone(GLdouble, GLdouble);
void APIENTRY auxWireTeapot(GLdouble);
void APIENTRY auxSolidTeapot(GLdouble);

#ifdef __cplusplus
}
#endif
#endif
