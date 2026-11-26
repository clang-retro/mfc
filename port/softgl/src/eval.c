/* eval.c - the evaluators of the software OpenGL: one- and
 * two-dimensional Bezier maps, glEvalCoord, glEvalPoint and glEvalMesh.
 *
 * The functions here call the execution functions (sgl_Begin and the
 * others) directly, not through the dispatch table: glEvalMesh in a
 * display list is played back as itself.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* The order of the map types, as in sgl_context's map1 and map2. */
enum { M_VERTEX3, M_VERTEX4, M_INDEX, M_COLOR4, M_NORMAL, M_TEX1, M_TEX2, M_TEX3, M_TEX4 };

/* Gives the map type of a target of glMap1 or glMap2, or -1, and tells
 * in *two if the target is a two-dimensional map. */
static int map_type(GLenum target, int *two) {
  *two = 0;
  if (target >= GL_MAP2_COLOR_4 && target <= GL_MAP2_VERTEX_4) {
    *two = 1;
    target = target - GL_MAP2_COLOR_4 + GL_MAP1_COLOR_4;
  }
  switch (target) {
    case GL_MAP1_VERTEX_3: return M_VERTEX3;
    case GL_MAP1_VERTEX_4: return M_VERTEX4;
    case GL_MAP1_INDEX: return M_INDEX;
    case GL_MAP1_COLOR_4: return M_COLOR4;
    case GL_MAP1_NORMAL: return M_NORMAL;
    case GL_MAP1_TEXTURE_COORD_1: return M_TEX1;
    case GL_MAP1_TEXTURE_COORD_2: return M_TEX2;
    case GL_MAP1_TEXTURE_COORD_3: return M_TEX3;
    case GL_MAP1_TEXTURE_COORD_4: return M_TEX4;
    default: return -1;
  }
}

/* The number of values in each control point of a map type. */
static const int kValues[9] = {3, 4, 1, 4, 3, 1, 2, 3, 4};

/* The enable of each one-dimensional map type. */
static const int kCap1[9] = {SGL_MAP1_VERTEX_3, SGL_MAP1_VERTEX_4, SGL_MAP1_INDEX,
                             SGL_MAP1_COLOR_4, SGL_MAP1_NORMAL, SGL_MAP1_TEXTURE_COORD_1,
                             SGL_MAP1_TEXTURE_COORD_2, SGL_MAP1_TEXTURE_COORD_3,
                             SGL_MAP1_TEXTURE_COORD_4};

/* The enable of each two-dimensional map type. */
static const int kCap2[9] = {SGL_MAP2_VERTEX_3, SGL_MAP2_VERTEX_4, SGL_MAP2_INDEX,
                             SGL_MAP2_COLOR_4, SGL_MAP2_NORMAL, SGL_MAP2_TEXTURE_COORD_1,
                             SGL_MAP2_TEXTURE_COORD_2, SGL_MAP2_TEXTURE_COORD_3,
                             SGL_MAP2_TEXTURE_COORD_4};

/* Gives the number of values in each control point of a map target, or
 * 0 for an invalid target. */
int sgl_map_k(GLenum target) {
  int two, t = map_type(target, &two);
  return t < 0 ? 0 : kValues[t];
}

/* Sets the maps of a new context to their initial values: one point,
 * with the initial current value. */
void sgl_eval_init(sgl_context *c) {
  static const GLfloat init[9][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 0}, {1, 1, 1, 1},
                                     {0, 0, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0},
                                     {0, 0, 0, 1}};
  int i;
  for (i = 0; i < SGL_MAP_TYPES; i++) {
    sgl_map *m1 = &c->map1[i], *m2 = &c->map2[i];
    m1->order_u = 1;
    m1->order_v = 1;
    m1->u1 = 0;
    m1->u2 = 1;
    m1->k = kValues[i];
    m1->points = (GLfloat *)malloc(4 * sizeof(GLfloat));
    if (m1->points) memcpy(m1->points, init[i], 4 * sizeof(GLfloat));
    *m2 = *m1;
    m2->v1 = 0;
    m2->v2 = 1;
    m2->points = (GLfloat *)malloc(4 * sizeof(GLfloat));
    if (m2->points) memcpy(m2->points, init[i], 4 * sizeof(GLfloat));
  }
}

/* Frees the control points of the maps of a context. */
void sgl_eval_free(sgl_context *c) {
  int i;
  for (i = 0; i < SGL_MAP_TYPES; i++) {
    free(c->map1[i].points);
    free(c->map2[i].points);
    c->map1[i].points = c->map2[i].points = NULL;
  }
}

/* glMap1f: sets a one-dimensional map. */
void APIENTRY sgl_Map1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order,
                        const GLfloat *points) {
  sgl_context *c = sgl_ctx;
  int two, t = map_type(target, &two), k, i;
  sgl_map *m;
  GLfloat *p;
  if (t < 0 || two) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  k = kValues[t];
  if (u1 == u2 || order < 1 || order > SGL_MAX_EVAL_ORDER || stride < k) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  p = (GLfloat *)malloc((size_t)order * k * sizeof(GLfloat));
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  for (i = 0; i < order; i++) memcpy(p + i * k, points + (size_t)i * stride, (size_t)k * sizeof(GLfloat));
  m = &c->map1[t];
  free(m->points);
  m->points = p;
  m->order_u = order;
  m->order_v = 1;
  m->u1 = u1;
  m->u2 = u2;
  m->k = k;
}

/* glMap2f: sets a two-dimensional map. */
void APIENTRY sgl_Map2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
                        GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points) {
  sgl_context *c = sgl_ctx;
  int two, t = map_type(target, &two), k, i, j;
  sgl_map *m;
  GLfloat *p;
  if (t < 0 || !two) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  k = kValues[t];
  if (u1 == u2 || v1 == v2 || uorder < 1 || uorder > SGL_MAX_EVAL_ORDER || vorder < 1 ||
      vorder > SGL_MAX_EVAL_ORDER || ustride < k || vstride < k) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  p = (GLfloat *)malloc((size_t)uorder * vorder * k * sizeof(GLfloat));
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  for (i = 0; i < uorder; i++)
    for (j = 0; j < vorder; j++)
      memcpy(p + ((size_t)i * vorder + j) * k, points + (size_t)i * ustride + (size_t)j * vstride,
             (size_t)k * sizeof(GLfloat));
  m = &c->map2[t];
  free(m->points);
  m->points = p;
  m->order_u = uorder;
  m->order_v = vorder;
  m->u1 = u1;
  m->u2 = u2;
  m->v1 = v1;
  m->v2 = v2;
  m->k = k;
}

/* glMap1d: converts the points to float and calls glMap1f. */
void APIENTRY sgl_Map1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order,
                        const GLdouble *points) {
  int k = sgl_map_k(target), i, j;
  GLfloat p[SGL_MAX_EVAL_ORDER * 4];
  if (k == 0 || order < 1 || order > SGL_MAX_EVAL_ORDER || stride < k) {
    sgl_error(k == 0 ? GL_INVALID_ENUM : GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < order; i++)
    for (j = 0; j < k; j++) p[i * k + j] = (GLfloat)points[(size_t)i * stride + j];
  SGL_DISPATCH->Map1f(target, (GLfloat)u1, (GLfloat)u2, k, order, p);
}

/* glMap2d: converts the points to float and calls glMap2f. */
void APIENTRY sgl_Map2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder,
                        GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points) {
  int k = sgl_map_k(target), i, j, n;
  GLfloat *p;
  if (k == 0 || uorder < 1 || uorder > SGL_MAX_EVAL_ORDER || vorder < 1 ||
      vorder > SGL_MAX_EVAL_ORDER || ustride < k || vstride < k) {
    sgl_error(k == 0 ? GL_INVALID_ENUM : GL_INVALID_VALUE);
    return;
  }
  p = (GLfloat *)malloc((size_t)uorder * vorder * k * sizeof(GLfloat));
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  for (i = 0; i < uorder; i++)
    for (j = 0; j < vorder; j++)
      for (n = 0; n < k; n++)
        p[((size_t)i * vorder + j) * k + n] = (GLfloat)points[(size_t)i * ustride + (size_t)j * vstride + n];
  SGL_DISPATCH->Map2f(target, (GLfloat)u1, (GLfloat)u2, vorder * k, uorder, (GLfloat)v1, (GLfloat)v2,
                      k, vorder, p);
  free(p);
}

/* Computes the Bernstein basis of order n (degree n - 1) at t into b,
 * and its derivative into db when db is not NULL. */
static void bernstein(int n, GLfloat t, GLfloat *b, GLfloat *db) {
  GLfloat lower[SGL_MAX_EVAL_ORDER];
  int i, j, deg = n - 1;
  GLfloat s = 1.0f - t;
  /* de Casteljau's triangle of the basis: after degree d, b holds the
   * basis of degree d. */
  b[0] = 1.0f;
  for (j = 1; j <= deg; j++) {
    if (j == deg && db) memcpy(lower, b, (size_t)j * sizeof(GLfloat));
    b[j] = 0.0f;
    for (i = j; i > 0; i--) b[i] = b[i] * s + b[i - 1] * t;
    b[0] *= s;
  }
  if (db) {
    if (deg == 0) {
      db[0] = 0.0f;
      return;
    }
    if (deg == 1) {
      lower[0] = 1.0f;
    }
    for (i = 0; i <= deg; i++) {
      GLfloat a = i > 0 ? lower[i - 1] : 0.0f, c = i < deg ? lower[i] : 0.0f;
      db[i] = (GLfloat)deg * (a - c);
    }
  }
}

/* Evaluates a one-dimensional map at u into out (m->k values). */
static void eval1(const sgl_map *m, GLfloat u, GLfloat *out) {
  GLfloat b[SGL_MAX_EVAL_ORDER], t = (u - m->u1) / (m->u2 - m->u1);
  int i, k;
  bernstein(m->order_u, t, b, NULL);
  for (k = 0; k < m->k; k++) {
    GLfloat s = 0.0f;
    for (i = 0; i < m->order_u; i++) s += b[i] * m->points[i * m->k + k];
    out[k] = s;
  }
}

/* Evaluates a two-dimensional map at u, v into out (m->k values), and
 * the partial derivatives into du and dv when they are not NULL. */
static void eval2(const sgl_map *m, GLfloat u, GLfloat v, GLfloat *out, GLfloat *du, GLfloat *dv) {
  GLfloat bu[SGL_MAX_EVAL_ORDER], bv[SGL_MAX_EVAL_ORDER];
  GLfloat dbu[SGL_MAX_EVAL_ORDER], dbv[SGL_MAX_EVAL_ORDER];
  GLfloat su = (u - m->u1) / (m->u2 - m->u1), sv = (v - m->v1) / (m->v2 - m->v1);
  int i, j, k, deriv = du != NULL;
  bernstein(m->order_u, su, bu, deriv ? dbu : NULL);
  bernstein(m->order_v, sv, bv, deriv ? dbv : NULL);
  for (k = 0; k < m->k; k++) {
    GLfloat s = 0.0f, sdu = 0.0f, sdv = 0.0f;
    for (i = 0; i < m->order_u; i++)
      for (j = 0; j < m->order_v; j++) {
        GLfloat p = m->points[(i * m->order_v + j) * m->k + k];
        s += bu[i] * bv[j] * p;
        if (deriv) {
          sdu += dbu[i] * bv[j] * p;
          sdv += bu[i] * dbv[j] * p;
        }
      }
    out[k] = s;
    if (deriv) {
      du[k] = sdu;
      dv[k] = sdv;
    }
  }
}

/* Keeps the current values that the evaluators change. */
typedef struct saved {
  GLfloat color[4], normal[3], tex[4];
} saved;

/* Saves the current color, normal and texture coordinates. */
static void save_current(const sgl_context *c, saved *s) {
  memcpy(s->color, c->a.color, sizeof s->color);
  memcpy(s->normal, c->a.normal, sizeof s->normal);
  memcpy(s->tex, c->a.tex, sizeof s->tex);
}

/* Restores the current color, normal and texture coordinates. */
static void restore_current(sgl_context *c, const saved *s) {
  memcpy(c->a.color, s->color, sizeof s->color);
  memcpy(c->a.normal, s->normal, sizeof s->normal);
  memcpy(c->a.tex, s->tex, sizeof s->tex);
}

/* glEvalCoord1f: evaluates the one-dimensional maps that are on at u. */
void APIENTRY sgl_EvalCoord1f(GLfloat u) {
  sgl_context *c = sgl_ctx;
  GLfloat v[4];
  saved s;
  int t;
  save_current(c, &s);
  if (c->a.enabled[SGL_MAP1_COLOR_4]) {
    eval1(&c->map1[M_COLOR4], u, v);
    sgl_Color4f(v[0], v[1], v[2], v[3]);
  }
  for (t = M_TEX4; t >= M_TEX1; t--)
    if (c->a.enabled[kCap1[t]]) {
      GLfloat r[4] = {0, 0, 0, 1};
      eval1(&c->map1[t], u, r);
      sgl_TexCoord4f(r[0], r[1], r[2], r[3]);
      break;
    }
  if (c->a.enabled[SGL_MAP1_NORMAL]) {
    eval1(&c->map1[M_NORMAL], u, v);
    sgl_Normal3f(v[0], v[1], v[2]);
  }
  if (c->a.enabled[SGL_MAP1_VERTEX_4]) {
    eval1(&c->map1[M_VERTEX4], u, v);
    sgl_Vertex4f(v[0], v[1], v[2], v[3]);
  } else if (c->a.enabled[SGL_MAP1_VERTEX_3]) {
    eval1(&c->map1[M_VERTEX3], u, v);
    sgl_Vertex4f(v[0], v[1], v[2], 1.0f);
  }
  restore_current(c, &s);
}

/* glEvalCoord2f: evaluates the two-dimensional maps that are on at u,
 * v. With GL_AUTO_NORMAL the normal is the cross product of the
 * derivatives of the vertex map. */
void APIENTRY sgl_EvalCoord2f(GLfloat u, GLfloat v) {
  sgl_context *c = sgl_ctx;
  GLfloat p[4], du[4], dv[4];
  saved s;
  int t, v4 = c->a.enabled[SGL_MAP2_VERTEX_4], v3 = c->a.enabled[SGL_MAP2_VERTEX_3];
  int autonorm = c->a.enabled[SGL_AUTO_NORMAL] && (v3 || v4);
  save_current(c, &s);
  if (c->a.enabled[SGL_MAP2_COLOR_4]) {
    eval2(&c->map2[M_COLOR4], u, v, p, NULL, NULL);
    sgl_Color4f(p[0], p[1], p[2], p[3]);
  }
  for (t = M_TEX4; t >= M_TEX1; t--)
    if (c->a.enabled[kCap2[t]]) {
      GLfloat r[4] = {0, 0, 0, 1};
      eval2(&c->map2[t], u, v, r, NULL, NULL);
      sgl_TexCoord4f(r[0], r[1], r[2], r[3]);
      break;
    }
  if (!autonorm && c->a.enabled[SGL_MAP2_NORMAL]) {
    eval2(&c->map2[M_NORMAL], u, v, p, NULL, NULL);
    sgl_Normal3f(p[0], p[1], p[2]);
  }
  if (v4 || v3) {
    const sgl_map *m = &c->map2[v4 ? M_VERTEX4 : M_VERTEX3];
    p[3] = 1.0f;
    eval2(m, u, v, p, autonorm ? du : NULL, autonorm ? dv : NULL);
    if (autonorm) {
      GLfloat a[3], b[3];
      int i;
      if (v4 && p[3] != 0.0f) {
        /* The derivatives of x / w. */
        for (i = 0; i < 3; i++) {
          a[i] = (du[i] * p[3] - p[i] * du[3]) / (p[3] * p[3]);
          b[i] = (dv[i] * p[3] - p[i] * dv[3]) / (p[3] * p[3]);
        }
      } else {
        for (i = 0; i < 3; i++) {
          a[i] = du[i];
          b[i] = dv[i];
        }
      }
      sgl_Normal3f(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
    }
    sgl_Vertex4f(p[0], p[1], p[2], v4 ? p[3] : 1.0f);
  }
  restore_current(c, &s);
}

/* glMapGrid1f: sets the grid of glEvalMesh1. */
void APIENTRY sgl_MapGrid1f(GLint n, GLfloat u1, GLfloat u2) {
  sgl_context *c = sgl_ctx;
  if (n <= 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.grid1_n = n;
  c->a.grid1_u1 = u1;
  c->a.grid1_u2 = u2;
}

/* glMapGrid2f: sets the grid of glEvalMesh2. */
void APIENTRY sgl_MapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2) {
  sgl_context *c = sgl_ctx;
  if (un <= 0 || vn <= 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.grid2_un = un;
  c->a.grid2_u1 = u1;
  c->a.grid2_u2 = u2;
  c->a.grid2_vn = vn;
  c->a.grid2_v1 = v1;
  c->a.grid2_v2 = v2;
}

/* Gives u of point i of the one-dimensional grid. */
static GLfloat grid1(const sgl_context *c, GLint i) {
  return c->a.grid1_u1 + (c->a.grid1_u2 - c->a.grid1_u1) * (GLfloat)i / (GLfloat)c->a.grid1_n;
}

/* Gives u of column i of the two-dimensional grid. */
static GLfloat grid2u(const sgl_context *c, GLint i) {
  return c->a.grid2_u1 + (c->a.grid2_u2 - c->a.grid2_u1) * (GLfloat)i / (GLfloat)c->a.grid2_un;
}

/* Gives v of row j of the two-dimensional grid. */
static GLfloat grid2v(const sgl_context *c, GLint j) {
  return c->a.grid2_v1 + (c->a.grid2_v2 - c->a.grid2_v1) * (GLfloat)j / (GLfloat)c->a.grid2_vn;
}

/* glEvalPoint1: evaluates at point i of the grid. */
void APIENTRY sgl_EvalPoint1(GLint i) { sgl_EvalCoord1f(grid1(sgl_ctx, i)); }

/* glEvalPoint2: evaluates at point i, j of the grid. */
void APIENTRY sgl_EvalPoint2(GLint i, GLint j) {
  sgl_context *c = sgl_ctx;
  sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j));
}

/* glEvalMesh1: evaluates points i1 to i2 of the grid as points or a
 * line strip. */
void APIENTRY sgl_EvalMesh1(GLenum mode, GLint i1, GLint i2) {
  sgl_context *c = sgl_ctx;
  GLint i;
  if (mode != GL_POINT && mode != GL_LINE) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sgl_Begin(mode == GL_POINT ? GL_POINTS : GL_LINE_STRIP);
  for (i = i1; i <= i2; i++) sgl_EvalCoord1f(grid1(c, i));
  sgl_End();
}

/* glEvalMesh2: evaluates the grid from i1, j1 to i2, j2 as points,
 * lines or quad strips. */
void APIENTRY sgl_EvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2) {
  sgl_context *c = sgl_ctx;
  GLint i, j;
  switch (mode) {
    case GL_POINT:
      sgl_Begin(GL_POINTS);
      for (j = j1; j <= j2; j++)
        for (i = i1; i <= i2; i++) sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j));
      sgl_End();
      break;
    case GL_LINE:
      for (j = j1; j <= j2; j++) {
        sgl_Begin(GL_LINE_STRIP);
        for (i = i1; i <= i2; i++) sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j));
        sgl_End();
      }
      for (i = i1; i <= i2; i++) {
        sgl_Begin(GL_LINE_STRIP);
        for (j = j1; j <= j2; j++) sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j));
        sgl_End();
      }
      break;
    case GL_FILL:
      for (j = j1; j < j2; j++) {
        sgl_Begin(GL_QUAD_STRIP);
        for (i = i1; i <= i2; i++) {
          sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j));
          sgl_EvalCoord2f(grid2u(c, i), grid2v(c, j + 1));
        }
        sgl_End();
      }
      break;
    default:
      sgl_error(GL_INVALID_ENUM);
      break;
  }
}

/* Fills v with the values of a map query and gives their number. */
static int map_query(GLenum target, GLenum query, GLdouble *v) {
  sgl_context *c = sgl_ctx;
  int two, t = map_type(target, &two), i, n;
  const sgl_map *m;
  if (t < 0) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  m = two ? &c->map2[t] : &c->map1[t];
  switch (query) {
    case GL_COEFF:
      n = m->order_u * m->order_v * m->k;
      for (i = 0; i < n; i++) v[i] = m->points ? m->points[i] : 0.0;
      return n;
    case GL_ORDER:
      v[0] = m->order_u;
      v[1] = m->order_v;
      return two ? 2 : 1;
    case GL_DOMAIN:
      v[0] = m->u1;
      v[1] = m->u2;
      v[2] = m->v1;
      v[3] = m->v2;
      return two ? 4 : 2;
    default:
      sgl_error(GL_INVALID_ENUM);
      return 0;
  }
}

/* glGetMapdv: gives the control points, order or domain of a map. */
void APIENTRY sgl_GetMapdv(GLenum target, GLenum query, GLdouble *v) { map_query(target, query, v); }

/* glGetMapfv: as glGetMapdv, as floats. */
void APIENTRY sgl_GetMapfv(GLenum target, GLenum query, GLfloat *v) {
  GLdouble d[SGL_MAX_EVAL_ORDER * SGL_MAX_EVAL_ORDER * 4];
  int i, n = map_query(target, query, d);
  for (i = 0; i < n; i++) v[i] = (GLfloat)d[i];
}

/* glGetMapiv: as glGetMapdv, as integers. */
void APIENTRY sgl_GetMapiv(GLenum target, GLenum query, GLint *v) {
  GLdouble d[SGL_MAX_EVAL_ORDER * SGL_MAX_EVAL_ORDER * 4];
  int i, n = map_query(target, query, d);
  for (i = 0; i < n; i++) v[i] = (GLint)d[i];
}
