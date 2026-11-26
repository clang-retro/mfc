// glaux_solids.h - the GLUT solids that glaux.cpp uses, for systems that
// have no GLUT (the classic Mac OS). The functions have GLUT's names,
// sizes and orientations, and draw with OpenGL 1.1 and GLU only.
//
// The teapot is drawn from the Bezier patches of Martin Newell's teapot.
// The control points and patches are from freeglut's fg_teapot_data.h:
//
//   Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.
//   Permission is hereby granted, free of charge, to any person obtaining
//   a copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions: The above copyright notice and this
//   permission notice shall be included in all copies or substantial
//   portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
//   WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//   NONINFRINGEMENT. IN NO EVENT SHALL PAWEL W. OLSZTA BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_GLAUX_SOLIDS_H
#define WINPORT_GLAUX_SOLIDS_H

#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>

namespace glaux_solids {

// A point or a direction in space.
struct Vec {
  double x, y, z;
};

// Gives the dot product of a and b.
inline double Dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Gives the cross product of a and b.
inline Vec Cross(const Vec& a, const Vec& b) {
  Vec r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  return r;
}

// Gives v with length 1 (or v when its length is 0).
inline Vec Unit(Vec v) {
  double l = sqrt(Dot(v, v));
  if (l > 0) {
    v.x /= l;
    v.y /= l;
    v.z /= l;
  }
  return v;
}

// Draws a convex solid that is centred on the origin. verts are its
// corners and dirs the outward directions of its faces (the corners of
// its dual solid). Each face is the set of corners farthest along its
// direction, drawn counterclockwise from outside. mode is GL_POLYGON for
// a solid and GL_LINE_LOOP for a wire frame.
inline void Convex(const Vec* verts, int nverts, const Vec* dirs, int nfaces, GLenum mode) {
  for (int f = 0; f < nfaces; f++) {
    Vec n = Unit(dirs[f]);
    double best = -1e30;
    for (int i = 0; i < nverts; i++) best = Dot(verts[i], n) > best ? Dot(verts[i], n) : best;
    // The corners of the face, and their angles around its centre.
    int idx[8];
    double ang[8];
    int k = 0;
    Vec c = {0, 0, 0};
    for (int i = 0; i < nverts && k < 8; i++)
      if (Dot(verts[i], n) > best - 1e-6) {
        idx[k++] = i;
        c.x += verts[i].x;
        c.y += verts[i].y;
        c.z += verts[i].z;
      }
    if (k < 3) continue;
    c.x /= k;
    c.y /= k;
    c.z /= k;
    Vec u = {verts[idx[0]].x - c.x, verts[idx[0]].y - c.y, verts[idx[0]].z - c.z};
    u = Unit(u);
    Vec w = Cross(n, u);
    for (int i = 0; i < k; i++) {
      Vec d = {verts[idx[i]].x - c.x, verts[idx[i]].y - c.y, verts[idx[i]].z - c.z};
      ang[i] = atan2(Dot(d, w), Dot(d, u));
    }
    // A sort by angle gives the counterclockwise order around n.
    for (int i = 1; i < k; i++)
      for (int j = i; j > 0 && ang[j] < ang[j - 1]; j--) {
        double ta = ang[j];
        ang[j] = ang[j - 1];
        ang[j - 1] = ta;
        int ti = idx[j];
        idx[j] = idx[j - 1];
        idx[j - 1] = ti;
      }
    glBegin(mode);
    glNormal3d(n.x, n.y, n.z);
    for (int i = 0; i < k; i++) glVertex3d(verts[idx[i]].x, verts[idx[i]].y, verts[idx[i]].z);
    glEnd();
  }
}

// The golden ratio.
const double kPhi = 1.6180339887498949;

// Fills v with the 12 corners of an icosahedron of circumradius r.
inline void IcosaCorners(Vec* v, double r) {
  double s = r / sqrt(1 + kPhi * kPhi);
  int k = 0;
  for (int a = -1; a <= 1; a += 2)
    for (int b = -1; b <= 1; b += 2) {
      Vec p0 = {0, a * s, b * kPhi * s}, p1 = {a * s, b * kPhi * s, 0}, p2 = {b * kPhi * s, 0, a * s};
      v[k++] = p0;
      v[k++] = p1;
      v[k++] = p2;
    }
}

// Fills v with the 20 corners of a dodecahedron of circumradius r.
inline void DodecaCorners(Vec* v, double r) {
  double s = r / sqrt(3.0);
  int k = 0;
  for (int a = -1; a <= 1; a += 2)
    for (int b = -1; b <= 1; b += 2) {
      for (int c = -1; c <= 1; c += 2) {
        Vec p = {a * s, b * s, c * s};
        v[k++] = p;
      }
      Vec p0 = {0, a * s / kPhi, b * s * kPhi}, p1 = {a * s / kPhi, b * s * kPhi, 0},
          p2 = {b * s * kPhi, 0, a * s / kPhi};
      v[k++] = p0;
      v[k++] = p1;
      v[k++] = p2;
    }
}

// Draws GLUT's tetrahedron: circumradius sqrt(3).
inline void Tetrahedron(GLenum mode) {
  Vec v[4] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
  Vec d[4] = {{-1, -1, -1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, -1}};
  Convex(v, 4, d, 4, mode);
}

// Draws GLUT's octahedron: circumradius 1.
inline void Octahedron(GLenum mode) {
  Vec v[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  Vec d[8];
  int k = 0;
  for (int a = -1; a <= 1; a += 2)
    for (int b = -1; b <= 1; b += 2)
      for (int c = -1; c <= 1; c += 2) {
        Vec p = {(double)a, (double)b, (double)c};
        d[k++] = p;
      }
  Convex(v, 6, d, 8, mode);
}

// Draws GLUT's icosahedron: circumradius 1.
inline void Icosahedron(GLenum mode) {
  Vec v[12], d[20];
  IcosaCorners(v, 1);
  DodecaCorners(d, 1);
  Convex(v, 12, d, 20, mode);
}

// Draws GLUT's dodecahedron: circumradius sqrt(3).
inline void Dodecahedron(GLenum mode) {
  Vec v[20], d[12];
  DodecaCorners(v, sqrt(3.0));
  IcosaCorners(d, 1);
  Convex(v, 20, d, 12, mode);
}

// Draws GLUT's cube of edge s, centred on the origin.
inline void Cube(double s, GLenum mode) {
  double h = s / 2;
  Vec v[8], d[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  int k = 0;
  for (int a = -1; a <= 1; a += 2)
    for (int b = -1; b <= 1; b += 2)
      for (int c = -1; c <= 1; c += 2) {
        Vec p = {a * h, b * h, c * h};
        v[k++] = p;
      }
  Convex(v, 8, d, 6, mode);
}

// Draws GLUT's torus around the z axis: tube radius inner, ring radius
// outer, with sides segments around the tube and rings around the axis.
inline void Torus(double inner, double outer, int sides, int rings, bool wire) {
  const double kTwoPi = 6.2831853071795862;
  for (int i = 0; i < rings; i++) {
    double t0 = kTwoPi * i / rings, t1 = kTwoPi * (i + 1) / rings;
    glBegin(wire ? GL_LINE_STRIP : GL_QUAD_STRIP);
    for (int j = 0; j <= sides; j++) {
      double p = kTwoPi * j / sides;
      for (int e = 0; e < 2; e++) {
        double t = e ? t1 : t0;
        double cx = cos(t), cy = sin(t), cp = cos(p), sp = sin(p);
        glNormal3d(cx * cp, cy * cp, sp);
        glVertex3d(cx * (outer + inner * cp), cy * (outer + inner * cp), inner * sp);
      }
    }
    glEnd();
  }
}

// Draws GLUT's cone: its base of radius base in the plane z = 0, its tip
// at z = height.
inline void Cone(double base, double height, int slices, int stacks, bool wire) {
  GLUquadricObj* q = gluNewQuadric();
  if (!q) return;
  gluQuadricDrawStyle(q, wire ? GLU_LINE : GLU_FILL);
  gluQuadricNormals(q, GLU_SMOOTH);
  gluCylinder(q, base, 0.0, height, slices, stacks);
  // The base faces down.
  glPushMatrix();
  glRotated(180, 1, 0, 0);
  gluDisk(q, 0.0, base, slices, 1);
  glPopMatrix();
  gluDeleteQuadric(q);
}

// The teapot's patches: 16 indexes into kTeapotPoints each. The first six
// (rim, body, lid and bottom) are the quarter with x >= 0 and y <= 0, and
// are drawn in all four quarters. The last four (handle and spout) are
// the half with y <= 0, and are drawn mirrored too.
const int kTeapotPatches[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    {24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39},
    {40, 41, 42, 40, 43, 44, 45, 46, 47, 47, 47, 47, 48, 49, 50, 51},
    {48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63},
    {64, 64, 64, 64, 65, 66, 67, 68, 69, 70, 71, 72, 39, 38, 37, 36},
    {73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88},
    {85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100},
    {101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116},
    {113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128}};

// The teapot's control points, with z up.
const GLfloat kTeapotPoints[129][3] = {
    {1.40000f, 0.00000f, 2.40000f},   {1.40000f, -0.78400f, 2.40000f},  {0.78400f, -1.40000f, 2.40000f},
    {0.00000f, -1.40000f, 2.40000f},  {1.33750f, 0.00000f, 2.53125f},   {1.33750f, -0.74900f, 2.53125f},
    {0.74900f, -1.33750f, 2.53125f},  {0.00000f, -1.33750f, 2.53125f},  {1.43750f, 0.00000f, 2.53125f},
    {1.43750f, -0.80500f, 2.53125f},  {0.80500f, -1.43750f, 2.53125f},  {0.00000f, -1.43750f, 2.53125f},
    {1.50000f, 0.00000f, 2.40000f},   {1.50000f, -0.84000f, 2.40000f},  {0.84000f, -1.50000f, 2.40000f},
    {0.00000f, -1.50000f, 2.40000f},  {1.75000f, 0.00000f, 1.87500f},   {1.75000f, -0.98000f, 1.87500f},
    {0.98000f, -1.75000f, 1.87500f},  {0.00000f, -1.75000f, 1.87500f},  {2.00000f, 0.00000f, 1.35000f},
    {2.00000f, -1.12000f, 1.35000f},  {1.12000f, -2.00000f, 1.35000f},  {0.00000f, -2.00000f, 1.35000f},
    {2.00000f, 0.00000f, 0.90000f},   {2.00000f, -1.12000f, 0.90000f},  {1.12000f, -2.00000f, 0.90000f},
    {0.00000f, -2.00000f, 0.90000f},  {2.00000f, 0.00000f, 0.45000f},   {2.00000f, -1.12000f, 0.45000f},
    {1.12000f, -2.00000f, 0.45000f},  {0.00000f, -2.00000f, 0.45000f},  {1.50000f, 0.00000f, 0.22500f},
    {1.50000f, -0.84000f, 0.22500f},  {0.84000f, -1.50000f, 0.22500f},  {0.00000f, -1.50000f, 0.22500f},
    {1.50000f, 0.00000f, 0.15000f},   {1.50000f, -0.84000f, 0.15000f},  {0.84000f, -1.50000f, 0.15000f},
    {0.00000f, -1.50000f, 0.15000f},  {0.00000f, 0.00000f, 3.15000f},   {0.00000f, -0.00200f, 3.15000f},
    {0.00200f, 0.00000f, 3.15000f},   {0.80000f, 0.00000f, 3.15000f},   {0.80000f, -0.45000f, 3.15000f},
    {0.45000f, -0.80000f, 3.15000f},  {0.00000f, -0.80000f, 3.15000f},  {0.00000f, 0.00000f, 2.85000f},
    {0.20000f, 0.00000f, 2.70000f},   {0.20000f, -0.11200f, 2.70000f},  {0.11200f, -0.20000f, 2.70000f},
    {0.00000f, -0.20000f, 2.70000f},  {0.40000f, 0.00000f, 2.55000f},   {0.40000f, -0.22400f, 2.55000f},
    {0.22400f, -0.40000f, 2.55000f},  {0.00000f, -0.40000f, 2.55000f},  {1.30000f, 0.00000f, 2.55000f},
    {1.30000f, -0.72800f, 2.55000f},  {0.72800f, -1.30000f, 2.55000f},  {0.00000f, -1.30000f, 2.55000f},
    {1.30000f, 0.00000f, 2.40000f},   {1.30000f, -0.72800f, 2.40000f},  {0.72800f, -1.30000f, 2.40000f},
    {0.00000f, -1.30000f, 2.40000f},  {0.00000f, 0.00000f, 0.00000f},   {0.00000f, -1.42500f, 0.00000f},
    {0.79800f, -1.42500f, 0.00000f},  {1.42500f, -0.79800f, 0.00000f},  {1.42500f, 0.00000f, 0.00000f},
    {0.00000f, -1.50000f, 0.07500f},  {0.84000f, -1.50000f, 0.07500f},  {1.50000f, -0.84000f, 0.07500f},
    {1.50000f, 0.00000f, 0.07500f},   {-1.60000f, 0.00000f, 2.02500f},  {-1.60000f, -0.30000f, 2.02500f},
    {-1.50000f, -0.30000f, 2.25000f}, {-1.50000f, 0.00000f, 2.25000f},  {-2.30000f, 0.00000f, 2.02500f},
    {-2.30000f, -0.30000f, 2.02500f}, {-2.50000f, -0.30000f, 2.25000f}, {-2.50000f, 0.00000f, 2.25000f},
    {-2.70000f, 0.00000f, 2.02500f},  {-2.70000f, -0.30000f, 2.02500f}, {-3.00000f, -0.30000f, 2.25000f},
    {-3.00000f, 0.00000f, 2.25000f},  {-2.70000f, 0.00000f, 1.80000f},  {-2.70000f, -0.30000f, 1.80000f},
    {-3.00000f, -0.30000f, 1.80000f}, {-3.00000f, 0.00000f, 1.80000f},  {-2.70000f, 0.00000f, 1.57500f},
    {-2.70000f, -0.30000f, 1.57500f}, {-3.00000f, -0.30000f, 1.35000f}, {-3.00000f, 0.00000f, 1.35000f},
    {-2.50000f, 0.00000f, 1.12500f},  {-2.50000f, -0.30000f, 1.12500f}, {-2.65000f, -0.30000f, 0.93750f},
    {-2.65000f, 0.00000f, 0.93750f},  {-2.00000f, 0.00000f, 0.90000f},  {-2.00000f, -0.30000f, 0.90000f},
    {-1.90000f, -0.30000f, 0.60000f}, {-1.90000f, 0.00000f, 0.60000f},  {1.70000f, 0.00000f, 1.42500f},
    {1.70000f, -0.66000f, 1.42500f},  {1.70000f, -0.66000f, 0.60000f},  {1.70000f, 0.00000f, 0.60000f},
    {2.60000f, 0.00000f, 1.42500f},   {2.60000f, -0.66000f, 1.42500f},  {3.10000f, -0.66000f, 0.82500f},
    {3.10000f, 0.00000f, 0.82500f},   {2.30000f, 0.00000f, 2.10000f},   {2.30000f, -0.25000f, 2.10000f},
    {2.40000f, -0.25000f, 2.02500f},  {2.40000f, 0.00000f, 2.02500f},   {2.70000f, 0.00000f, 2.40000f},
    {2.70000f, -0.25000f, 2.40000f},  {3.30000f, -0.25000f, 2.40000f},  {3.30000f, 0.00000f, 2.40000f},
    {2.80000f, 0.00000f, 2.47500f},   {2.80000f, -0.25000f, 2.47500f},  {3.52500f, -0.25000f, 2.49375f},
    {3.52500f, 0.00000f, 2.49375f},   {2.90000f, 0.00000f, 2.47500f},   {2.90000f, -0.15000f, 2.47500f},
    {3.45000f, -0.15000f, 2.51250f},  {3.45000f, 0.00000f, 2.51250f},   {2.80000f, 0.00000f, 2.40000f},
    {2.80000f, -0.15000f, 2.40000f},  {3.20000f, -0.15000f, 2.40000f},  {3.20000f, 0.00000f, 2.40000f}};

// Draws one patch with the evaluator, its x and y multiplied by sx and
// sy. When one of them is -1, the patch is mirrored, so its rows are
// taken in the other order to keep the faces pointing out.
inline void TeapotPatch(const int* patch, float sx, float sy, GLenum mode, int grid) {
  GLfloat p[4][4][3];
  bool flip = (sx * sy) < 0;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      const GLfloat* c = kTeapotPoints[patch[(flip ? 3 - i : i) * 4 + j]];
      p[i][j][0] = c[0] * sx;
      p[i][j][1] = c[1] * sy;
      p[i][j][2] = c[2];
    }
  glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &p[0][0][0]);
  glEvalMesh2(mode, 0, grid, 0, grid);
}

// Draws GLUT's teapot of size s: y is up, and the teapot is centred on
// the origin.
inline void Teapot(double s, bool wire) {
  const int grid = 10;
  glPushAttrib(GL_ENABLE_BIT | GL_EVAL_BIT);
  glEnable(GL_AUTO_NORMAL);
  glEnable(GL_NORMALIZE);
  glEnable(GL_MAP2_VERTEX_3);
  glMapGrid2f(grid, 0, 1, grid, 0, 1);
  glPushMatrix();
  glRotated(270, 1, 0, 0);
  glScaled(0.5 * s, 0.5 * s, 0.5 * s);
  glTranslated(0, 0, -1.5);
  GLenum mode = wire ? GL_LINE : GL_FILL;
  for (int k = 0; k < 10; k++) {
    const int* patch = kTeapotPatches[k];
    TeapotPatch(patch, 1, 1, mode, grid);
    TeapotPatch(patch, 1, -1, mode, grid);
    if (k < 6) {
      TeapotPatch(patch, -1, 1, mode, grid);
      TeapotPatch(patch, -1, -1, mode, grid);
    }
  }
  glPopMatrix();
  glPopAttrib();
}

}  // namespace glaux_solids

// The GLUT functions of glaux.cpp, over the functions above.
// Draws a solid cone (see glaux_solids::Cone).
inline void glutSolidCone(GLdouble b, GLdouble h, GLint sl, GLint st) { glaux_solids::Cone(b, h, sl, st, false); }
// Draws a wire cone (see glaux_solids::Cone).
inline void glutWireCone(GLdouble b, GLdouble h, GLint sl, GLint st) { glaux_solids::Cone(b, h, sl, st, true); }
// Draws a solid cube of edge s.
inline void glutSolidCube(GLdouble s) { glaux_solids::Cube(s, GL_POLYGON); }
// Draws a wire cube of edge s.
inline void glutWireCube(GLdouble s) { glaux_solids::Cube(s, GL_LINE_LOOP); }
// Draws a solid torus (see glaux_solids::Torus).
inline void glutSolidTorus(GLdouble i, GLdouble o, GLint s, GLint r) { glaux_solids::Torus(i, o, s, r, false); }
// Draws a wire torus (see glaux_solids::Torus).
inline void glutWireTorus(GLdouble i, GLdouble o, GLint s, GLint r) { glaux_solids::Torus(i, o, s, r, true); }
// Draws a solid icosahedron of circumradius 1.
inline void glutSolidIcosahedron() { glaux_solids::Icosahedron(GL_POLYGON); }
// Draws a wire icosahedron of circumradius 1.
inline void glutWireIcosahedron() { glaux_solids::Icosahedron(GL_LINE_LOOP); }
// Draws a solid octahedron of circumradius 1.
inline void glutSolidOctahedron() { glaux_solids::Octahedron(GL_POLYGON); }
// Draws a wire octahedron of circumradius 1.
inline void glutWireOctahedron() { glaux_solids::Octahedron(GL_LINE_LOOP); }
// Draws a solid tetrahedron of circumradius sqrt(3).
inline void glutSolidTetrahedron() { glaux_solids::Tetrahedron(GL_POLYGON); }
// Draws a wire tetrahedron of circumradius sqrt(3).
inline void glutWireTetrahedron() { glaux_solids::Tetrahedron(GL_LINE_LOOP); }
// Draws a solid dodecahedron of circumradius sqrt(3).
inline void glutSolidDodecahedron() { glaux_solids::Dodecahedron(GL_POLYGON); }
// Draws a wire dodecahedron of circumradius sqrt(3).
inline void glutWireDodecahedron() { glaux_solids::Dodecahedron(GL_LINE_LOOP); }
// Draws a solid teapot of size s.
inline void glutSolidTeapot(GLdouble s) { glaux_solids::Teapot(s, false); }
// Draws a wire teapot of size s.
inline void glutWireTeapot(GLdouble s) { glaux_solids::Teapot(s, true); }

#endif
