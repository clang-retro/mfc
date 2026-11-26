/* matrix.c - the matrices, the matrix stacks, the viewport and the clip
 * planes of the software OpenGL.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* Sets m to the identity matrix. */
void sgl_matrix_identity(sgl_matrix *m) {
  memset(m->m, 0, sizeof m->m);
  m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

/* Sets out to a * b. out can be a or b. */
void sgl_matrix_mul(sgl_matrix *out, const sgl_matrix *a, const sgl_matrix *b) {
  sgl_matrix r;
  int i, j;
  for (j = 0; j < 4; j++)
    for (i = 0; i < 4; i++)
      r.m[j * 4 + i] = a->m[i] * b->m[j * 4] + a->m[4 + i] * b->m[j * 4 + 1] +
                       a->m[8 + i] * b->m[j * 4 + 2] + a->m[12 + i] * b->m[j * 4 + 3];
  *out = r;
}

/* Sets out to the inverse of m. Gives 0 when m has no inverse (out is
 * then the identity). */
int sgl_matrix_invert(sgl_matrix *out, const sgl_matrix *m) {
  const GLfloat *a = m->m;
  GLfloat inv[16], det;
  int i;
  inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
           a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
  inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
           a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
  inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
           a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
  inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
            a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
  inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
           a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
  inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
           a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
  inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
           a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
  inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
            a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
  inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
           a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
  inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
           a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
  inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
            a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
  inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
            a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
  inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
           a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
  inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
           a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
  inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
            a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
  inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
            a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
  det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
  if (det == 0.0f) {
    sgl_matrix_identity(out);
    return 0;
  }
  det = 1.0f / det;
  for (i = 0; i < 16; i++) out->m[i] = inv[i] * det;
  return 1;
}

/* Sets out to m * in, for a column vector in of 4 values. */
void sgl_matrix_apply(const sgl_matrix *m, const GLfloat in[4], GLfloat out[4]) {
  const GLfloat *a = m->m;
  GLfloat x = in[0], y = in[1], z = in[2], w = in[3];
  out[0] = a[0] * x + a[4] * y + a[8] * z + a[12] * w;
  out[1] = a[1] * x + a[5] * y + a[9] * z + a[13] * w;
  out[2] = a[2] * x + a[6] * y + a[10] * z + a[14] * w;
  out[3] = a[3] * x + a[7] * y + a[11] * z + a[15] * w;
}

/* Makes a stack of max matrices, with the identity on top. */
void sgl_stack_init(sgl_stack *s, int max) {
  s->base = (sgl_matrix *)malloc((size_t)max * sizeof(sgl_matrix));
  s->top = s->base;
  s->depth = 0;
  s->max = max;
  if (s->base) sgl_matrix_identity(s->top);
}

/* Frees the memory of a stack. */
void sgl_stack_free(sgl_stack *s) {
  free(s->base);
  s->base = s->top = NULL;
}

/* Computes the matrix from object to clip coordinates when a matrix
 * changed. */
void sgl_update_matrices(sgl_context *c) {
  if (!c->mvp_dirty) return;
  sgl_matrix_mul(&c->mvp, c->projection.top, c->modelview.top);
  c->mvp_dirty = 0;
}

/* Computes the inverse of the modelview matrix when it changed. */
void sgl_update_inverse(sgl_context *c) {
  if (!c->inv_dirty) return;
  sgl_matrix_invert(&c->inv_modelview, c->modelview.top);
  c->inv_dirty = 0;
}

/* Marks the values that depend on the top of the current stack. */
static void changed(sgl_context *c) {
  if (c->stack == &c->texture) {
    const GLfloat *m = c->texture.top->m;
    c->texture_identity = m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
                          m[5] == 1 && m[6] == 0 && m[7] == 0 && m[8] == 0 && m[9] == 0 &&
                          m[10] == 1 && m[11] == 0 && m[12] == 0 && m[13] == 0 &&
                          m[14] == 0 && m[15] == 1;
  } else {
    c->mvp_dirty = 1;
    if (c->stack == &c->modelview) c->inv_dirty = 1;
  }
}

/* Gives the current context, or NULL (with GL_INVALID_OPERATION) between
 * glBegin and glEnd. */
static sgl_context *outside(void) {
  sgl_context *c = sgl_ctx;
  if (c->prim != GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return c;
}

/* Multiplies the top of the current stack by m on the right. */
static void mult(const sgl_matrix *m) {
  sgl_context *c = outside();
  if (!c) return;
  sgl_matrix_mul(c->stack->top, c->stack->top, m);
  changed(c);
}

/* glMatrixMode: sets the stack that the matrix functions change. */
void APIENTRY sgl_MatrixMode(GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  switch (mode) {
    case GL_MODELVIEW: c->stack = &c->modelview; break;
    case GL_PROJECTION: c->stack = &c->projection; break;
    case GL_TEXTURE: c->stack = &c->texture; break;
    default: sgl_error(GL_INVALID_ENUM); return;
  }
  c->a.matrix_mode = mode;
}

/* glLoadIdentity: sets the top of the stack to the identity. */
void APIENTRY sgl_LoadIdentity(void) {
  sgl_context *c = outside();
  if (!c) return;
  sgl_matrix_identity(c->stack->top);
  changed(c);
}

/* glLoadMatrixf: sets the top of the stack to m. */
void APIENTRY sgl_LoadMatrixf(const GLfloat *m) {
  sgl_context *c = outside();
  if (!c) return;
  memcpy(c->stack->top->m, m, sizeof c->stack->top->m);
  changed(c);
}

/* glMultMatrixf: multiplies the top of the stack by m. */
void APIENTRY sgl_MultMatrixf(const GLfloat *m) {
  sgl_matrix t;
  memcpy(t.m, m, sizeof t.m);
  mult(&t);
}

/* glTranslatef: multiplies by a translation. */
void APIENTRY sgl_Translatef(GLfloat x, GLfloat y, GLfloat z) {
  sgl_context *c = outside();
  GLfloat *m;
  if (!c) return;
  m = c->stack->top->m;
  m[12] += m[0] * x + m[4] * y + m[8] * z;
  m[13] += m[1] * x + m[5] * y + m[9] * z;
  m[14] += m[2] * x + m[6] * y + m[10] * z;
  m[15] += m[3] * x + m[7] * y + m[11] * z;
  changed(c);
}

/* glScalef: multiplies by a scale. */
void APIENTRY sgl_Scalef(GLfloat x, GLfloat y, GLfloat z) {
  sgl_context *c = outside();
  GLfloat *m;
  int i;
  if (!c) return;
  m = c->stack->top->m;
  for (i = 0; i < 4; i++) {
    m[i] *= x;
    m[4 + i] *= y;
    m[8 + i] *= z;
  }
  changed(c);
}

/* glRotatef: multiplies by a rotation of angle degrees about (x, y, z). */
void APIENTRY sgl_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
  sgl_matrix r;
  GLfloat len = sqrtf(x * x + y * y + z * z), s, co, t;
  if (len == 0.0f) return;
  x /= len;
  y /= len;
  z /= len;
  angle *= 3.14159265358979f / 180.0f;
  s = sinf(angle);
  co = cosf(angle);
  t = 1.0f - co;
  sgl_matrix_identity(&r);
  r.m[0] = x * x * t + co;
  r.m[1] = y * x * t + z * s;
  r.m[2] = x * z * t - y * s;
  r.m[4] = x * y * t - z * s;
  r.m[5] = y * y * t + co;
  r.m[6] = y * z * t + x * s;
  r.m[8] = x * z * t + y * s;
  r.m[9] = y * z * t - x * s;
  r.m[10] = z * z * t + co;
  mult(&r);
}

/* glOrtho: multiplies by a parallel projection. */
void APIENTRY sgl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
  sgl_matrix m;
  if (l == r || b == t || n == f) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  sgl_matrix_identity(&m);
  m.m[0] = (GLfloat)(2.0 / (r - l));
  m.m[5] = (GLfloat)(2.0 / (t - b));
  m.m[10] = (GLfloat)(-2.0 / (f - n));
  m.m[12] = (GLfloat)(-(r + l) / (r - l));
  m.m[13] = (GLfloat)(-(t + b) / (t - b));
  m.m[14] = (GLfloat)(-(f + n) / (f - n));
  mult(&m);
}

/* glFrustum: multiplies by a perspective projection. */
void APIENTRY sgl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
  sgl_matrix m;
  if (n <= 0.0 || f <= 0.0 || l == r || b == t || n == f) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  memset(m.m, 0, sizeof m.m);
  m.m[0] = (GLfloat)(2.0 * n / (r - l));
  m.m[5] = (GLfloat)(2.0 * n / (t - b));
  m.m[8] = (GLfloat)((r + l) / (r - l));
  m.m[9] = (GLfloat)((t + b) / (t - b));
  m.m[10] = (GLfloat)(-(f + n) / (f - n));
  m.m[11] = -1.0f;
  m.m[14] = (GLfloat)(-2.0 * f * n / (f - n));
  mult(&m);
}

/* glPushMatrix: pushes a copy of the top of the stack. */
void APIENTRY sgl_PushMatrix(void) {
  sgl_context *c = outside();
  sgl_stack *s;
  if (!c) return;
  s = c->stack;
  if (s->depth + 1 >= s->max) {
    sgl_error(GL_STACK_OVERFLOW);
    return;
  }
  s->top[1] = s->top[0];
  s->top++;
  s->depth++;
}

/* glPopMatrix: pops the top of the stack. */
void APIENTRY sgl_PopMatrix(void) {
  sgl_context *c = outside();
  sgl_stack *s;
  if (!c) return;
  s = c->stack;
  if (s->depth == 0) {
    sgl_error(GL_STACK_UNDERFLOW);
    return;
  }
  s->top--;
  s->depth--;
  changed(c);
}

/* glViewport: sets the viewport. */
void APIENTRY sgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  sgl_context *c = outside();
  if (!c) return;
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.viewport[0] = x;
  c->a.viewport[1] = y;
  c->a.viewport[2] = w > 4096 ? 4096 : w;
  c->a.viewport[3] = h > 4096 ? 4096 : h;
}

/* glDepthRange: sets the range of the window z. */
void APIENTRY sgl_DepthRange(GLclampd n, GLclampd f) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.depth_near = n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
  c->a.depth_far = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

/* glClipPlane: sets a user clip plane. It is kept in eye coordinates. */
void APIENTRY sgl_ClipPlane(GLenum plane, const GLdouble *eq) {
  sgl_context *c = outside();
  const GLfloat *m;
  int i;
  if (!c) return;
  if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + SGL_MAX_CLIP_PLANES) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  i = (int)(plane - GL_CLIP_PLANE0);
  sgl_update_inverse(c);
  m = c->inv_modelview.m;
  /* The plane in eye coordinates is eq * inverse(modelview). */
  c->a.clip_plane[i][0] = eq[0] * m[0] + eq[1] * m[1] + eq[2] * m[2] + eq[3] * m[3];
  c->a.clip_plane[i][1] = eq[0] * m[4] + eq[1] * m[5] + eq[2] * m[6] + eq[3] * m[7];
  c->a.clip_plane[i][2] = eq[0] * m[8] + eq[1] * m[9] + eq[2] * m[10] + eq[3] * m[11];
  c->a.clip_plane[i][3] = eq[0] * m[12] + eq[1] * m[13] + eq[2] * m[14] + eq[3] * m[15];
}

/* glGetClipPlane: gives a user clip plane in eye coordinates. */
void APIENTRY sgl_GetClipPlane(GLenum plane, GLdouble *eq) {
  sgl_context *c = sgl_ctx;
  int i;
  if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + SGL_MAX_CLIP_PLANES) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < 4; i++) eq[i] = c->a.clip_plane[plane - GL_CLIP_PLANE0][i];
}
