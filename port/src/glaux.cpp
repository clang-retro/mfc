// glaux.cpp - the part of the Win32 SDK's OpenGL auxiliary library that
// samples and screen savers use (GL/glaux.h): image loading for textures
// and simple solids. Windows .bmp files are read with LoadImage, SGI .rgb
// files here. The solids come from GLU and GLUT on Mac OS X, and from
// glaux_solids.h on the classic Mac OS and with the generic (software)
// OpenGL, where there is no GLUT.
//
// The library is libglaux.a, made for Mac OS X, the classic Mac OS and
// the systems with the generic OpenGL (Linux). On Mac OS X, link it with
// -framework OpenGL -framework GLUT.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <GL/glaux.h>

#include "opengl_backend.h"

#if (defined(macintosh) && !defined(__MACH__)) || defined(WINPORT_SOFTGL)
#include "glaux_solids.h"
#elif defined(__APPLE__) && defined(__MACH__)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
// GLUT's header can undefine APIENTRY, which windef.h defines empty.
#ifndef APIENTRY
#define APIENTRY
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace {

// Makes an image record of w by h pixels; NULL if out of memory.
AUX_RGBImageRec* NewImage(int w, int h) {
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return NULL;
  AUX_RGBImageRec* r = (AUX_RGBImageRec*)malloc(sizeof *r);
  if (!r) return NULL;
  r->sizeX = w;
  r->sizeY = h;
  r->data = (unsigned char*)malloc((size_t)w * h * 3);
  if (!r->data) {
    free(r);
    return NULL;
  }
  return r;
}

// Reads a Windows bitmap file through LoadImage and GetDIBits.
AUX_RGBImageRec* LoadDIB(const char* path) {
  HBITMAP bmp = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
  if (!bmp) return NULL;
  BITMAP b;
  AUX_RGBImageRec* r = NULL;
  if (GetObjectA(bmp, sizeof b, &b) && (r = NewImage(b.bmWidth, b.bmHeight)) != NULL) {
    // 24 bits a pixel, bottom row first, rows padded to 4 bytes.
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = b.bmWidth;
    bi.bmiHeader.biHeight = b.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    size_t stride = ((size_t)b.bmWidth * 3 + 3) & ~(size_t)3;
    std::vector<unsigned char> rows(stride * b.bmHeight);
    HDC dc = GetDC(NULL);
    int got = GetDIBits(dc, bmp, 0, (UINT)b.bmHeight, rows.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    if (got == b.bmHeight) {
      // Blue, green, red to red, green, blue, without the padding.
      for (int y = 0; y < b.bmHeight; y++)
        for (int x = 0; x < b.bmWidth; x++) {
          const unsigned char* s = &rows[y * stride + (size_t)x * 3];
          unsigned char* d = &r->data[((size_t)y * b.bmWidth + x) * 3];
          d[0] = s[2];
          d[1] = s[1];
          d[2] = s[0];
        }
    } else {
      free(r->data);
      free(r);
      r = NULL;
    }
  }
  DeleteObject(bmp);
  return r;
}

// A big-endian 16-bit value.
unsigned BE16(const unsigned char* p) { return (unsigned)p[0] << 8 | p[1]; }
// A big-endian 32-bit value.
unsigned long BE32(const unsigned char* p) {
  return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

// Expands one run-length encoded row of an SGI image into out (w bytes).
void UnpackRow(const unsigned char* p, const unsigned char* end, unsigned char* out, int w) {
  int x = 0;
  while (p < end && x < w) {
    unsigned char c = *p++;
    int n = c & 0x7F;
    if (!n) break;
    if (c & 0x80) {
      while (n-- && p < end && x < w) out[x++] = *p++;
    } else {
      if (p >= end) break;
      unsigned char v = *p++;
      while (n-- && x < w) out[x++] = v;
    }
  }
}

// Reads an SGI image file (.rgb, .bw): one byte a channel, verbatim or
// run-length encoded. A grey image gives grey pixels; alpha is dropped.
AUX_RGBImageRec* LoadSGI(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  std::vector<unsigned char> file;
  unsigned char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) file.insert(file.end(), buf, buf + n);
  fclose(f);
  if (file.size() < 512 || BE16(&file[0]) != 474 || file[3] != 1) return NULL;
  bool rle = file[2] == 1;
  int w = (int)BE16(&file[6]), h = (int)BE16(&file[8]), zs = (int)BE16(&file[10]);
  if (zs < 1) zs = 1;
  AUX_RGBImageRec* r = NewImage(w, h);
  if (!r) return NULL;
  memset(r->data, 0, (size_t)w * h * 3);
  std::vector<unsigned char> row(w);
  const unsigned char* end = file.data() + file.size();
  for (int z = 0; z < 3; z++) {
    int ch = z < zs ? z : 0;
    for (int y = 0; y < h; y++) {
      if (rle) {
        size_t t = 512 + ((size_t)ch * h + y) * 4;
        size_t lt = 512 + ((size_t)zs * h + (size_t)ch * h + y) * 4;
        if (lt + 4 > file.size()) continue;
        unsigned long off = BE32(&file[t]), len = BE32(&file[lt]);
        if (off >= file.size()) continue;
        const unsigned char* e = file.data() + off + len;
        UnpackRow(file.data() + off, e < end ? e : end, row.data(), w);
      } else {
        size_t off = 512 + ((size_t)ch * h + y) * w;
        if (off + w > file.size()) continue;
        memcpy(row.data(), &file[off], (size_t)w);
      }
      // SGI images are stored bottom row first, as GL wants them.
      for (int x = 0; x < w; x++) r->data[((size_t)y * w + x) * 3 + z] = row[x];
    }
  }
  return r;
}

// A wide path in the ANSI code page.
std::string Narrow(LPCWSTR w) {
  if (!w) return std::string();
  int n = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
  std::string s(n > 0 ? (size_t)n : 1, '\0');
  if (n > 0) WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], n, NULL, NULL);
  s.resize(strlen(s.c_str()));
  return s;
}

// Runs draw with the model scaled by s, keeping normals unit length.
template <class F>
void Scaled(GLdouble s, F draw) {
  glPushAttrib(GL_ENABLE_BIT);
  glEnable(GL_NORMALIZE);
  glPushMatrix();
  glScaled(s, s, s);
  draw();
  glPopMatrix();
  glPopAttrib();
}

// Draws a GLU quadric shape, filled or as lines.
template <class F>
void Quadric(bool solid, F draw) {
  GLUquadricObj* q = gluNewQuadric();
  if (!q) return;
  gluQuadricDrawStyle(q, solid ? GLU_FILL : GLU_LINE);
  gluQuadricNormals(q, GLU_SMOOTH);
  draw(q);
  gluDeleteQuadric(q);
}

}  // namespace

extern "C" {

// Loads a .bmp file as 24-bit RGB rows, bottom row first.
AUX_RGBImageRec* APIENTRY auxDIBImageLoadA(LPCSTR path) { return path ? LoadDIB(path) : NULL; }
// The Unicode form of auxDIBImageLoadA.
AUX_RGBImageRec* APIENTRY auxDIBImageLoadW(LPCWSTR path) { return path ? LoadDIB(Narrow(path).c_str()) : NULL; }
// Loads an SGI .rgb file as 24-bit RGB rows.
AUX_RGBImageRec* APIENTRY auxRGBImageLoadA(LPCSTR path) { return path ? LoadSGI(path) : NULL; }
// The Unicode form of auxRGBImageLoadA.
AUX_RGBImageRec* APIENTRY auxRGBImageLoadW(LPCWSTR path) { return path ? LoadSGI(Narrow(path).c_str()) : NULL; }

// Draws a sphere of radius r as lines.
void APIENTRY auxWireSphere(GLdouble r) {
  Quadric(false, [&](GLUquadricObj* q) { gluSphere(q, r, 16, 16); });
}
// Draws a solid sphere of radius r.
void APIENTRY auxSolidSphere(GLdouble r) {
  Quadric(true, [&](GLUquadricObj* q) { gluSphere(q, r, 16, 16); });
}
// Draws a cube with sides of length s as lines.
void APIENTRY auxWireCube(GLdouble s) { glutWireCube(s); }
// Draws a solid cube with sides of length s.
void APIENTRY auxSolidCube(GLdouble s) { glutSolidCube(s); }

// A box is a unit cube scaled to each side.
void APIENTRY auxWireBox(GLdouble w, GLdouble h, GLdouble d) {
  glPushMatrix();
  glScaled(w, h, d);
  glutWireCube(1.0);
  glPopMatrix();
}
// Draws a solid box of width w, height h and depth d.
void APIENTRY auxSolidBox(GLdouble w, GLdouble h, GLdouble d) {
  glPushAttrib(GL_ENABLE_BIT);
  glEnable(GL_NORMALIZE);
  glPushMatrix();
  glScaled(w, h, d);
  glutSolidCube(1.0);
  glPopMatrix();
  glPopAttrib();
}

// Draws a torus as lines.
void APIENTRY auxWireTorus(GLdouble inner, GLdouble outer) { glutWireTorus(inner, outer, 10, 20); }
// Draws a solid torus.
void APIENTRY auxSolidTorus(GLdouble inner, GLdouble outer) { glutSolidTorus(inner, outer, 10, 20); }

// The cylinder stands on the origin and goes up along y, as in glaux.
void APIENTRY auxWireCylinder(GLdouble r, GLdouble h) {
  glPushMatrix();
  glRotated(-90, 1, 0, 0);
  Quadric(false, [&](GLUquadricObj* q) { gluCylinder(q, r, r, h, 16, 1); });
  glPopMatrix();
}
// Draws a solid cylinder of radius r and height h.
void APIENTRY auxSolidCylinder(GLdouble r, GLdouble h) {
  glPushMatrix();
  glRotated(-90, 1, 0, 0);
  Quadric(true, [&](GLUquadricObj* q) { gluCylinder(q, r, r, h, 16, 1); });
  glPopMatrix();
}

// GLUT's icosahedron and octahedron have radius 1; its tetrahedron and
// dodecahedron have radius sqrt(3). glaux takes the radius.
void APIENTRY auxWireIcosahedron(GLdouble r) { Scaled(r, [] { glutWireIcosahedron(); }); }
// Draws a solid icosahedron of radius r.
void APIENTRY auxSolidIcosahedron(GLdouble r) { Scaled(r, [] { glutSolidIcosahedron(); }); }
// Draws an octahedron of radius r as lines.
void APIENTRY auxWireOctahedron(GLdouble r) { Scaled(r, [] { glutWireOctahedron(); }); }
// Draws a solid octahedron of radius r.
void APIENTRY auxSolidOctahedron(GLdouble r) { Scaled(r, [] { glutSolidOctahedron(); }); }
// Draws a tetrahedron of radius r as lines.
void APIENTRY auxWireTetrahedron(GLdouble r) { Scaled(r / sqrt(3.0), [] { glutWireTetrahedron(); }); }
// Draws a solid tetrahedron of radius r.
void APIENTRY auxSolidTetrahedron(GLdouble r) { Scaled(r / sqrt(3.0), [] { glutSolidTetrahedron(); }); }
// Draws a dodecahedron of radius r as lines.
void APIENTRY auxWireDodecahedron(GLdouble r) { Scaled(r / sqrt(3.0), [] { glutWireDodecahedron(); }); }
// Draws a solid dodecahedron of radius r.
void APIENTRY auxSolidDodecahedron(GLdouble r) { Scaled(r / sqrt(3.0), [] { glutSolidDodecahedron(); }); }

// Draws a cone as lines.
void APIENTRY auxWireCone(GLdouble base, GLdouble h) { glutWireCone(base, h, 15, 15); }
// Draws a solid cone.
void APIENTRY auxSolidCone(GLdouble base, GLdouble h) { glutSolidCone(base, h, 15, 15); }
// Draws a teapot of size s as lines.
void APIENTRY auxWireTeapot(GLdouble s) { glutWireTeapot(s); }
// Draws a solid teapot of size s.
void APIENTRY auxSolidTeapot(GLdouble s) { glutSolidTeapot(s); }

}  // extern "C"
