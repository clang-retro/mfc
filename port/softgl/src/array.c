/* array.c - the vertex arrays of the software OpenGL (OpenGL 1.1 and
 * GL_EXT_vertex_array).
 *
 * glArrayElement, glDrawArrays and glDrawElements call glBegin, the
 * current value functions and glVertex through the current dispatch
 * table. Thus in a display list they are recorded as the vertices that
 * they give, as the specification tells.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "sgl.h"

/* Gives the size in bytes of one element of an array type, or 0. */
static int elem_size(GLenum type) {
  switch (type) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
    case GL_DOUBLE: return 8;
    default: return 0;
  }
}

/* Sets the values of an array. */
static void set_array(sgl_array *a, GLint size, GLenum type, GLsizei stride, const void *p) {
  if (stride < 0 || elem_size(type) == 0) {
    sgl_error(stride < 0 ? GL_INVALID_VALUE : GL_INVALID_ENUM);
    return;
  }
  a->size = size;
  a->type = type;
  a->stride = stride;
  a->pointer = p;
}

/* glVertexPointer: sets the vertex array. */
void APIENTRY sgl_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
  if (size < 2 || size > 4) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  set_array(&sgl_ctx->c.vertex, size, type, stride, p);
}

/* glNormalPointer: sets the normal array. */
void APIENTRY sgl_NormalPointer(GLenum type, GLsizei stride, const void *p) {
  set_array(&sgl_ctx->c.normal, 3, type, stride, p);
}

/* glColorPointer: sets the color array. */
void APIENTRY sgl_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
  if (size < 3 || size > 4) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  set_array(&sgl_ctx->c.color, size, type, stride, p);
}

/* glIndexPointer: sets the color index array (not used in RGBA mode). */
void APIENTRY sgl_IndexPointer(GLenum type, GLsizei stride, const void *p) {
  set_array(&sgl_ctx->c.index, 1, type, stride, p);
}

/* glTexCoordPointer: sets the texture coordinate array. */
void APIENTRY sgl_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p) {
  if (size < 1 || size > 4) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  set_array(&sgl_ctx->c.tex, size, type, stride, p);
}

/* glEdgeFlagPointer: sets the edge flag array. */
void APIENTRY sgl_EdgeFlagPointer(GLsizei stride, const void *p) {
  set_array(&sgl_ctx->c.edge, 1, GL_UNSIGNED_BYTE, stride, p);
}

/* glVertexPointerEXT: as glVertexPointer; count is not used. */
void APIENTRY sgl_VertexPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *p) {
  (void)count;
  sgl_VertexPointer(size, type, stride, p);
}

/* glNormalPointerEXT: as glNormalPointer. */
void APIENTRY sgl_NormalPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *p) {
  (void)count;
  sgl_NormalPointer(type, stride, p);
}

/* glColorPointerEXT: as glColorPointer. */
void APIENTRY sgl_ColorPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *p) {
  (void)count;
  sgl_ColorPointer(size, type, stride, p);
}

/* glIndexPointerEXT: as glIndexPointer. */
void APIENTRY sgl_IndexPointerEXT(GLenum type, GLsizei stride, GLsizei count, const void *p) {
  (void)count;
  sgl_IndexPointer(type, stride, p);
}

/* glTexCoordPointerEXT: as glTexCoordPointer. */
void APIENTRY sgl_TexCoordPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count, const void *p) {
  (void)count;
  sgl_TexCoordPointer(size, type, stride, p);
}

/* glEdgeFlagPointerEXT: as glEdgeFlagPointer. */
void APIENTRY sgl_EdgeFlagPointerEXT(GLsizei stride, GLsizei count, const GLboolean *p) {
  (void)count;
  sgl_EdgeFlagPointer(stride, p);
}

/* Reads n values of element i of array a into out, as floats. When
 * normalize is true, integer types map to [-1, 1] or [0, 1]. */
static void fetch(const sgl_array *a, GLint i, int n, int normalize, GLfloat *out) {
  int size = elem_size(a->type), k;
  long stride = a->stride ? a->stride : (long)size * a->size;
  const unsigned char *p = (const unsigned char *)a->pointer + stride * i;
  for (k = 0; k < n; k++, p += size) {
    switch (a->type) {
      case GL_BYTE: {
        GLbyte v = *(const GLbyte *)p;
        out[k] = normalize ? (GLfloat)v / 127.0f : (GLfloat)v;
        break;
      }
      case GL_UNSIGNED_BYTE: {
        GLubyte v = *p;
        out[k] = normalize ? (GLfloat)v / 255.0f : (GLfloat)v;
        break;
      }
      case GL_SHORT: {
        GLshort v;
        memcpy(&v, p, 2);
        out[k] = normalize ? (GLfloat)v / 32767.0f : (GLfloat)v;
        break;
      }
      case GL_UNSIGNED_SHORT: {
        GLushort v;
        memcpy(&v, p, 2);
        out[k] = normalize ? (GLfloat)v / 65535.0f : (GLfloat)v;
        break;
      }
      case GL_INT: {
        GLint v;
        memcpy(&v, p, 4);
        out[k] = normalize ? (GLfloat)v / 2147483647.0f : (GLfloat)v;
        break;
      }
      case GL_UNSIGNED_INT: {
        GLuint v;
        memcpy(&v, p, 4);
        out[k] = normalize ? (GLfloat)v / 4294967295.0f : (GLfloat)v;
        break;
      }
      case GL_DOUBLE: {
        GLdouble v;
        memcpy(&v, p, 8);
        out[k] = (GLfloat)v;
        break;
      }
      default: {
        GLfloat v;
        memcpy(&v, p, 4);
        out[k] = v;
        break;
      }
    }
  }
}

/* glArrayElement: gives the values of element i of the arrays that are
 * on, through the current table. */
void APIENTRY sgl_ArrayElement(GLint i) {
  sgl_context *c = sgl_ctx;
  const struct sgl_dispatch *d = SGL_DISPATCH;
  GLfloat v[4];
  if (c->c.edge.enabled) d->EdgeFlag(((const GLboolean *)c->c.edge.pointer)[(c->c.edge.stride ? c->c.edge.stride : 1) * i]);
  if (c->c.tex.enabled) {
    v[1] = v[2] = 0.0f;
    v[3] = 1.0f;
    fetch(&c->c.tex, i, c->c.tex.size, 0, v);
    d->TexCoord4f(v[0], v[1], v[2], v[3]);
  }
  if (c->c.color.enabled) {
    v[3] = 1.0f;
    fetch(&c->c.color, i, c->c.color.size, 1, v);
    d->Color4f(v[0], v[1], v[2], v[3]);
  }
  if (c->c.normal.enabled) {
    fetch(&c->c.normal, i, 3, 1, v);
    d->Normal3f(v[0], v[1], v[2]);
  }
  if (c->c.vertex.enabled) {
    v[2] = 0.0f;
    v[3] = 1.0f;
    fetch(&c->c.vertex, i, c->c.vertex.size, 0, v);
    d->Vertex4f(v[0], v[1], v[2], v[3]);
  }
}

/* glArrayElementEXT: as glArrayElement. */
void APIENTRY sgl_ArrayElementEXT(GLint i) { sgl_ArrayElement(i); }

/* glDrawArrays: draws count elements of the arrays from first. */
void APIENTRY sgl_DrawArrays(GLenum mode, GLint first, GLsizei count) {
  const struct sgl_dispatch *d = SGL_DISPATCH;
  int i;
  if (count < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  d->Begin(mode);
  for (i = 0; i < count; i++) sgl_ArrayElement(first + i);
  d->End();
}

/* glDrawArraysEXT: as glDrawArrays. */
void APIENTRY sgl_DrawArraysEXT(GLenum mode, GLint first, GLsizei count) { sgl_DrawArrays(mode, first, count); }

/* glDrawElements: draws the elements of the arrays that indices name. */
void APIENTRY sgl_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
  const struct sgl_dispatch *d = SGL_DISPATCH;
  int i;
  if (count < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  d->Begin(mode);
  for (i = 0; i < count; i++) {
    GLint k;
    if (type == GL_UNSIGNED_BYTE)
      k = ((const GLubyte *)indices)[i];
    else if (type == GL_UNSIGNED_SHORT)
      k = ((const GLushort *)indices)[i];
    else
      k = (GLint)((const GLuint *)indices)[i];
    sgl_ArrayElement(k);
  }
  d->End();
}

/* glInterleavedArrays: sets and turns on the arrays for one of the
 * interleaved formats. */
void APIENTRY sgl_InterleavedArrays(GLenum format, GLsizei stride, const void *pointer) {
  sgl_context *c = sgl_ctx;
  const unsigned char *p = (const unsigned char *)pointer;
  int tc = 0, cc = 0, nc = 0, vc = 0, cub = 0, size;
  const int f = (int)sizeof(GLfloat);
  switch (format) {
    case GL_V2F: vc = 2; break;
    case GL_V3F: vc = 3; break;
    case GL_C4UB_V2F: cc = 4; cub = 1; vc = 2; break;
    case GL_C4UB_V3F: cc = 4; cub = 1; vc = 3; break;
    case GL_C3F_V3F: cc = 3; vc = 3; break;
    case GL_N3F_V3F: nc = 3; vc = 3; break;
    case GL_C4F_N3F_V3F: cc = 4; nc = 3; vc = 3; break;
    case GL_T2F_V3F: tc = 2; vc = 3; break;
    case GL_T4F_V4F: tc = 4; vc = 4; break;
    case GL_T2F_C4UB_V3F: tc = 2; cc = 4; cub = 1; vc = 3; break;
    case GL_T2F_C3F_V3F: tc = 2; cc = 3; vc = 3; break;
    case GL_T2F_N3F_V3F: tc = 2; nc = 3; vc = 3; break;
    case GL_T2F_C4F_N3F_V3F: tc = 2; cc = 4; nc = 3; vc = 3; break;
    case GL_T4F_C4F_N3F_V4F: tc = 4; cc = 4; nc = 3; vc = 4; break;
    default: sgl_error(GL_INVALID_ENUM); return;
  }
  size = tc * f + (cub ? 4 : cc * f) + nc * f + vc * f;
  if (stride == 0) stride = size;
  c->c.edge.enabled = GL_FALSE;
  c->c.index.enabled = GL_FALSE;
  c->c.tex.enabled = tc != 0;
  if (tc) set_array(&c->c.tex, tc, GL_FLOAT, stride, p);
  p += tc * f;
  c->c.color.enabled = cc != 0;
  if (cc) set_array(&c->c.color, cc, cub ? GL_UNSIGNED_BYTE : GL_FLOAT, stride, p);
  p += cub ? 4 : cc * f;
  c->c.normal.enabled = nc != 0;
  if (nc) set_array(&c->c.normal, 3, GL_FLOAT, stride, p);
  p += nc * f;
  c->c.vertex.enabled = GL_TRUE;
  set_array(&c->c.vertex, vc, GL_FLOAT, stride, p);
}
