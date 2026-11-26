/* list.c - the display lists of the software OpenGL.
 *
 * While a list is compiled, the current dispatch table is the save
 * table: each canonical command adds a node to the list, as opengl32.dll
 * does on Windows. A node has an operation code and its arguments. A
 * hand-written node (SGL_OP_HAND) has its play function in its first
 * argument. Memory that a node needs (copies of the program's arrays)
 * is a chain of blocks that the node owns.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdlib.h>

#include "sgl.h"

/* A block of memory that a node owns. */
typedef struct blob {
  struct blob *next;
  double align; /* the payload follows, aligned for any type */
} blob;

/* Gives the list with name, or NULL. */
static sgl_list *find_list(sgl_shared *s, GLuint name) {
  sgl_list *l = s->lists[name & 63];
  while (l && l->name != name) l = l->next;
  return l;
}

/* Frees the nodes of a list, and keeps the list empty. */
static void clear_list(sgl_list *l) {
  sgl_node *n = l->first;
  while (n) {
    sgl_node *next = n->next;
    blob *b = (blob *)n->data;
    while (b) {
      blob *bn = b->next;
      free(b);
      b = bn;
    }
    free(n);
    n = next;
  }
  l->first = l->last = NULL;
}

/* Frees all the lists of a set of shared objects. */
void sgl_list_free_all(sgl_shared *s) {
  int i;
  for (i = 0; i < 64; i++) {
    sgl_list *l = s->lists[i];
    while (l) {
      sgl_list *next = l->next;
      clear_list(l);
      free(l);
      l = next;
    }
    s->lists[i] = NULL;
  }
}

/* Adds a node with operation op and count arguments to the list that is
 * compiled. Gives its arguments, or NULL when there is not sufficient
 * memory. */
sgl_cell *sgl_list_add(int op, int count) {
  sgl_context *c = sgl_ctx;
  sgl_node *n;
  if (!c->compiling) return NULL;
  n = (sgl_node *)malloc(sizeof(sgl_node) + (size_t)(count ? count : 1) * sizeof(sgl_cell));
  if (!n) {
    sgl_error(GL_OUT_OF_MEMORY);
    return NULL;
  }
  n->next = NULL;
  n->op = op;
  n->count = count;
  n->args = (sgl_cell *)(n + 1);
  n->data = NULL;
  if (c->compiling->last)
    c->compiling->last->next = n;
  else
    c->compiling->first = n;
  c->compiling->last = n;
  return n->args;
}

/* Copies bytes bytes from p into a block of the last node of the list
 * that is compiled. Gives the copy, or NULL. */
void *sgl_list_copy(const void *p, size_t bytes) {
  sgl_context *c = sgl_ctx;
  sgl_node *n = c->compiling ? c->compiling->last : NULL;
  blob *b;
  if (!n || !p) return NULL;
  b = (blob *)malloc(sizeof(blob) + bytes);
  if (!b) {
    sgl_error(GL_OUT_OF_MEMORY);
    return NULL;
  }
  memcpy(&b->align, p, bytes);
  b->next = (blob *)n->data;
  n->data = b;
  return &b->align;
}

/* Gives 1 when the list that is compiled also executes its commands
 * (GL_COMPILE_AND_EXECUTE). */
int sgl_list_executes(void) { return sgl_ctx->list_mode == GL_COMPILE_AND_EXECUTE; }

/* Adds a hand-written node: play is called with the node's arguments,
 * of which the first is play itself. Gives the node, or NULL. */
sgl_node *sgl_list_add_hand(void (*play)(const sgl_cell *), int count, void *data) {
  sgl_cell *a = sgl_list_add(SGL_OP_HAND, count + 1);
  sgl_node *n;
  (void)data;
  if (!a) return NULL;
  a[0].fn = play;
  n = sgl_ctx->compiling->last;
  return n;
}

/* Plays the commands of a list. */
static void play_list(sgl_context *c, sgl_list *l) {
  sgl_node *n;
  if (c->call_depth >= SGL_LIST_DEPTH) return;
  c->call_depth++;
  for (n = l->first; n; n = n->next) {
    if (n->op == SGL_OP_HAND)
      n->args[0].fn(n->args);
    else
      sgl_play_generated(n->op, n->args);
  }
  c->call_depth--;
}

/* glNewList: starts the compilation of a display list. */
void APIENTRY sgl_NewList(GLuint name, GLenum mode) {
  sgl_context *c = sgl_ctx;
  sgl_list *l;
  if (name == 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (c->compiling || c->prim != GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  l = (sgl_list *)calloc(1, sizeof *l);
  if (!l) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  l->name = name;
  c->compiling = l;
  c->list_mode = mode;
  c->table = &sgl_save_table;
  sgl_cur = c->table;
}

/* glEndList: ends the compilation, and replaces any list of that name. */
void APIENTRY sgl_EndList(void) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  sgl_list *l = c->compiling, *old, **pp;
  if (!l) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  pp = &s->lists[l->name & 63];
  while (*pp && (*pp)->name != l->name) pp = &(*pp)->next;
  old = *pp;
  if (old) {
    l->next = old->next;
    clear_list(old);
    free(old);
  } else {
    l->next = NULL;
  }
  *pp = l;
  c->compiling = NULL;
  c->table = &sgl_exec_table;
  sgl_cur = c->table;
}

/* glCallList: plays a display list. */
void APIENTRY sgl_CallList(GLuint name) {
  sgl_context *c = sgl_ctx;
  sgl_list *l = find_list(c->shared, name);
  if (l) play_list(c, l);
}

/* Gives element i of a list of names of type type. */
static GLuint list_name(GLenum type, const void *lists, int i) {
  switch (type) {
    case GL_BYTE: return (GLuint)(GLint)((const GLbyte *)lists)[i];
    case GL_UNSIGNED_BYTE: return ((const GLubyte *)lists)[i];
    case GL_SHORT: return (GLuint)(GLint)((const GLshort *)lists)[i];
    case GL_UNSIGNED_SHORT: return ((const GLushort *)lists)[i];
    case GL_INT: return (GLuint)((const GLint *)lists)[i];
    case GL_UNSIGNED_INT: return ((const GLuint *)lists)[i];
    case GL_FLOAT: return (GLuint)((const GLfloat *)lists)[i];
    case GL_2_BYTES: {
      const GLubyte *p = (const GLubyte *)lists + i * 2;
      return ((GLuint)p[0] << 8) | p[1];
    }
    case GL_3_BYTES: {
      const GLubyte *p = (const GLubyte *)lists + i * 3;
      return ((GLuint)p[0] << 16) | ((GLuint)p[1] << 8) | p[2];
    }
    case GL_4_BYTES: {
      const GLubyte *p = (const GLubyte *)lists + i * 4;
      return ((GLuint)p[0] << 24) | ((GLuint)p[1] << 16) | ((GLuint)p[2] << 8) | p[3];
    }
    default: return 0;
  }
}

/* glCallLists: plays n display lists, each name plus the list base. */
void APIENTRY sgl_CallLists(GLsizei n, GLenum type, const void *lists) {
  sgl_context *c = sgl_ctx;
  int i;
  for (i = 0; i < n; i++) sgl_CallList(c->a.list_base + list_name(type, lists, i));
}

/* glListBase: sets the base of glCallLists. */
void APIENTRY sgl_ListBase(GLuint base) { sgl_ctx->a.list_base = base; }

/* glGenLists: gives the first of range names that are not in use, or
 * 0. */
GLuint APIENTRY sgl_GenLists(GLsizei range) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  GLuint first;
  int i, ok;
  if (range < 0) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (range == 0) return 0;
  for (;;) {
    first = s->next_list;
    ok = 1;
    for (i = 0; i < range; i++)
      if (find_list(s, first + (GLuint)i)) {
        ok = 0;
        s->next_list = first + (GLuint)i + 1;
        break;
      }
    if (ok) break;
  }
  /* Each name is in use from now on: an empty list. */
  for (i = 0; i < range; i++) {
    sgl_list *l = (sgl_list *)calloc(1, sizeof *l);
    if (!l) {
      sgl_error(GL_OUT_OF_MEMORY);
      return 0;
    }
    l->name = first + (GLuint)i;
    l->next = s->lists[l->name & 63];
    s->lists[l->name & 63] = l;
  }
  s->next_list = first + (GLuint)range;
  return first;
}

/* glDeleteLists: deletes range lists from list. */
void APIENTRY sgl_DeleteLists(GLuint list, GLsizei range) {
  sgl_shared *s = sgl_ctx->shared;
  int i;
  if (range < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < range; i++) {
    GLuint name = list + (GLuint)i;
    sgl_list **pp = &s->lists[name & 63];
    while (*pp && (*pp)->name != name) pp = &(*pp)->next;
    if (*pp) {
      sgl_list *l = *pp;
      *pp = l->next;
      clear_list(l);
      free(l);
    }
  }
}

/* glIsList: tells if name is a display list. */
GLboolean APIENTRY sgl_IsList(GLuint name) {
  return find_list(sgl_ctx->shared, name) ? GL_TRUE : GL_FALSE;
}

/* Copies the unpack store of the context into a block of the last
 * node. Gives the copy. */
static sgl_pixelstore *save_store(void) {
  return (sgl_pixelstore *)sgl_list_copy(&sgl_ctx->c.unpack, sizeof(sgl_pixelstore));
}

/* Runs fn with the unpack store set to ps, then restores the store. */
#define WITH_STORE(ps, call)              \
  do {                                    \
    sgl_pixelstore keep_ = sgl_ctx->c.unpack; \
    if (ps) sgl_ctx->c.unpack = *(ps);    \
    call;                                 \
    sgl_ctx->c.unpack = keep_;            \
  } while (0)

/* Plays a recorded glTexImage2D. */
static void play_TexImage2D(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[10].p,
             sgl_TexImage2D(a[1].u, a[2].i, a[3].i, a[4].i, a[5].i, a[6].i, a[7].u, a[8].u, a[9].p));
}

/* Records glTexImage2D, with a copy of the image. */
void APIENTRY save_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                              GLint border, GLenum format, GLenum type, const void *pixels) {
  sgl_node *n = sgl_list_add_hand(play_TexImage2D, 10, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].i = level;
    a[3].i = internal;
    a[4].i = w;
    a[5].i = h;
    a[6].i = border;
    a[7].u = format;
    a[8].u = type;
    a[9].p = pixels ? sgl_list_copy(pixels, sgl_image_bytes(&sgl_ctx->c.unpack, w, h, format, type)) : NULL;
    a[10].p = save_store();
  }
  if (sgl_list_executes()) sgl_TexImage2D(target, level, internal, w, h, border, format, type, pixels);
}

/* Plays a recorded glTexImage1D. */
static void play_TexImage1D(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[9].p,
             sgl_TexImage1D(a[1].u, a[2].i, a[3].i, a[4].i, a[5].i, a[6].u, a[7].u, a[8].p));
}

/* Records glTexImage1D, with a copy of the image. */
void APIENTRY save_TexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border,
                              GLenum format, GLenum type, const void *pixels) {
  sgl_node *n = sgl_list_add_hand(play_TexImage1D, 9, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].i = level;
    a[3].i = internal;
    a[4].i = w;
    a[5].i = border;
    a[6].u = format;
    a[7].u = type;
    a[8].p = pixels ? sgl_list_copy(pixels, sgl_image_bytes(&sgl_ctx->c.unpack, w, 1, format, type)) : NULL;
    a[9].p = save_store();
  }
  if (sgl_list_executes()) sgl_TexImage1D(target, level, internal, w, border, format, type, pixels);
}

/* Plays a recorded glTexSubImage2D. */
static void play_TexSubImage2D(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[10].p,
             sgl_TexSubImage2D(a[1].u, a[2].i, a[3].i, a[4].i, a[5].i, a[6].i, a[7].u, a[8].u, a[9].p));
}

/* Records glTexSubImage2D, with a copy of the image. */
void APIENTRY save_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                 GLenum format, GLenum type, const void *pixels) {
  sgl_node *n = sgl_list_add_hand(play_TexSubImage2D, 10, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].i = level;
    a[3].i = x;
    a[4].i = y;
    a[5].i = w;
    a[6].i = h;
    a[7].u = format;
    a[8].u = type;
    a[9].p = sgl_list_copy(pixels, sgl_image_bytes(&sgl_ctx->c.unpack, w, h, format, type));
    a[10].p = save_store();
  }
  if (sgl_list_executes()) sgl_TexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}

/* Plays a recorded glTexSubImage1D. */
static void play_TexSubImage1D(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[8].p,
             sgl_TexSubImage1D(a[1].u, a[2].i, a[3].i, a[4].i, a[5].u, a[6].u, a[7].p));
}

/* Records glTexSubImage1D, with a copy of the image. */
void APIENTRY save_TexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format,
                                 GLenum type, const void *pixels) {
  sgl_node *n = sgl_list_add_hand(play_TexSubImage1D, 8, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].i = level;
    a[3].i = x;
    a[4].i = w;
    a[5].u = format;
    a[6].u = type;
    a[7].p = sgl_list_copy(pixels, sgl_image_bytes(&sgl_ctx->c.unpack, w, 1, format, type));
    a[8].p = save_store();
  }
  if (sgl_list_executes()) sgl_TexSubImage1D(target, level, x, w, format, type, pixels);
}

/* Plays a recorded glDrawPixels. */
static void play_DrawPixels(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[6].p, sgl_DrawPixels(a[1].i, a[2].i, a[3].u, a[4].u, a[5].p));
}

/* Records glDrawPixels, with a copy of the image. */
void APIENTRY save_DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels) {
  sgl_node *n = sgl_list_add_hand(play_DrawPixels, 6, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].i = w;
    a[2].i = h;
    a[3].u = format;
    a[4].u = type;
    a[5].p = sgl_list_copy(pixels, sgl_image_bytes(&sgl_ctx->c.unpack, w, h, format, type));
    a[6].p = save_store();
  }
  if (sgl_list_executes()) sgl_DrawPixels(w, h, format, type, pixels);
}

/* Plays a recorded glBitmap. */
static void play_Bitmap(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[8].p,
             sgl_Bitmap(a[1].i, a[2].i, a[3].f, a[4].f, a[5].f, a[6].f, (const GLubyte *)a[7].p));
}

/* Records glBitmap, with a copy of the bitmap. */
void APIENTRY save_Bitmap(GLsizei w, GLsizei h, GLfloat xorig, GLfloat yorig, GLfloat xmove,
                          GLfloat ymove, const GLubyte *bitmap) {
  sgl_node *n = sgl_list_add_hand(play_Bitmap, 8, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].i = w;
    a[2].i = h;
    a[3].f = xorig;
    a[4].f = yorig;
    a[5].f = xmove;
    a[6].f = ymove;
    a[7].p = bitmap ? sgl_list_copy(bitmap, sgl_image_bytes(&sgl_ctx->c.unpack, w, h, GL_COLOR_INDEX, GL_BITMAP)) : NULL;
    a[8].p = save_store();
  }
  if (sgl_list_executes()) sgl_Bitmap(w, h, xorig, yorig, xmove, ymove, bitmap);
}

/* Plays a recorded glPolygonStipple. */
static void play_PolygonStipple(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[2].p, sgl_PolygonStipple((const GLubyte *)a[1].p));
}

/* Records glPolygonStipple, with a copy of the pattern. */
void APIENTRY save_PolygonStipple(const GLubyte *mask) {
  sgl_node *n = sgl_list_add_hand(play_PolygonStipple, 2, NULL);
  if (n) {
    n->args[1].p = sgl_list_copy(mask, sgl_image_bytes(&sgl_ctx->c.unpack, 32, 32, GL_COLOR_INDEX, GL_BITMAP));
    n->args[2].p = save_store();
  }
  if (sgl_list_executes()) sgl_PolygonStipple(mask);
}

/* Plays one name of a recorded glCallLists: the list name + base. */
static void play_call_base(const sgl_cell *a) { sgl_CallList(sgl_ctx->a.list_base + a[1].u); }

/* Records glCallLists: the names, plus the list base when it is played. */
void APIENTRY save_CallLists(GLsizei n, GLenum type, const void *lists) {
  int i;
  for (i = 0; i < n; i++) {
    /* Each name becomes a glCallList of name + base at play time, as
     * the base is read when the list is played. Keep the name and the
     * type as one node. */
    sgl_cell *a;
    GLuint name = list_name(type, lists, i);
    a = sgl_list_add(SGL_OP_HAND, 2);
    if (a) {
      a[0].fn = play_call_base;
      a[1].u = name;
    }
  }
  if (sgl_list_executes()) sgl_CallLists(n, type, lists);
}


/* Plays a recorded glMap1f. */
static void play_Map1f(const sgl_cell *a) {
  sgl_Map1f(a[1].u, a[2].f, a[3].f, a[4].i, a[5].i, (const GLfloat *)a[6].p);
}

/* Records glMap1f, with the control points copied tightly. */
void APIENTRY save_Map1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order,
                         const GLfloat *points) {
  int k = sgl_map_k(target), i;
  sgl_node *n = sgl_list_add_hand(play_Map1f, 6, NULL);
  if (n && k > 0 && order > 0 && order <= SGL_MAX_EVAL_ORDER) {
    GLfloat tight[SGL_MAX_EVAL_ORDER * 4];
    for (i = 0; i < order; i++) memcpy(tight + i * k, points + (size_t)i * stride, (size_t)k * sizeof(GLfloat));
    n->args[1].u = target;
    n->args[2].f = u1;
    n->args[3].f = u2;
    n->args[4].i = k;
    n->args[5].i = order;
    n->args[6].p = sgl_list_copy(tight, (size_t)order * k * sizeof(GLfloat));
  } else if (n) {
    n->args[0].fn = NULL;
    n->op = SGL_OP_NONE;
  }
  if (sgl_list_executes()) sgl_Map1f(target, u1, u2, stride, order, points);
}

/* Plays a recorded glMap2f. */
static void play_Map2f(const sgl_cell *a) {
  sgl_Map2f(a[1].u, a[2].f, a[3].f, a[4].i, a[5].i, a[6].f, a[7].f, a[8].i, a[9].i,
            (const GLfloat *)a[10].p);
}

/* Records glMap2f, with the control points copied tightly. */
void APIENTRY save_Map2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
                         GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points) {
  int k = sgl_map_k(target), i, j;
  sgl_node *n = sgl_list_add_hand(play_Map2f, 10, NULL);
  if (n && k > 0 && uorder > 0 && vorder > 0 && uorder <= SGL_MAX_EVAL_ORDER &&
      vorder <= SGL_MAX_EVAL_ORDER) {
    GLfloat *tight = (GLfloat *)malloc((size_t)uorder * vorder * k * sizeof(GLfloat));
    if (tight) {
      for (i = 0; i < uorder; i++)
        for (j = 0; j < vorder; j++)
          memcpy(tight + ((size_t)i * vorder + j) * k, points + (size_t)i * ustride + (size_t)j * vstride,
                 (size_t)k * sizeof(GLfloat));
      n->args[1].u = target;
      n->args[2].f = u1;
      n->args[3].f = u2;
      n->args[4].i = vorder * k;
      n->args[5].i = uorder;
      n->args[6].f = v1;
      n->args[7].f = v2;
      n->args[8].i = k;
      n->args[9].i = vorder;
      n->args[10].p = sgl_list_copy(tight, (size_t)uorder * vorder * k * sizeof(GLfloat));
      free(tight);
    }
  } else if (n) {
    n->op = SGL_OP_NONE;
  }
  if (sgl_list_executes()) sgl_Map2f(target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points);
}

/* Plays a recorded glPixelMapfv. */
static void play_PixelMapfv(const sgl_cell *a) {
  sgl_PixelMapfv(a[1].u, a[2].i, (const GLfloat *)a[3].p);
}

/* Records glPixelMapfv, with a copy of the values. */
void APIENTRY save_PixelMapfv(GLenum map, GLsizei n, const GLfloat *values) {
  sgl_node *node = sgl_list_add_hand(play_PixelMapfv, 3, NULL);
  if (node) {
    node->args[1].u = map;
    node->args[2].i = n;
    node->args[3].p = n > 0 ? sgl_list_copy(values, (size_t)n * sizeof(GLfloat)) : NULL;
  }
  if (sgl_list_executes()) sgl_PixelMapfv(map, n, values);
}

/* Plays a recorded glColorTableEXT. */
static void play_ColorTableEXT(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[7].p,
             sgl_ColorTableEXT(a[1].u, a[2].u, a[3].i, a[4].u, a[5].u, a[6].p));
}

/* Records glColorTableEXT, with a copy of the palette. */
void APIENTRY save_ColorTableEXT(GLenum target, GLenum internal, GLsizei width, GLenum format,
                                 GLenum type, const void *table) {
  sgl_node *n = sgl_list_add_hand(play_ColorTableEXT, 7, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].u = internal;
    a[3].i = width;
    a[4].u = format;
    a[5].u = type;
    a[6].p = table ? sgl_list_copy(table, sgl_image_bytes(&sgl_ctx->c.unpack, width, 1, format, type)) : NULL;
    a[7].p = save_store();
  }
  if (sgl_list_executes()) sgl_ColorTableEXT(target, internal, width, format, type, table);
}

/* Plays a recorded glColorSubTableEXT. */
static void play_ColorSubTableEXT(const sgl_cell *a) {
  WITH_STORE((const sgl_pixelstore *)a[7].p,
             sgl_ColorSubTableEXT(a[1].u, a[2].i, a[3].i, a[4].u, a[5].u, a[6].p));
}

/* Records glColorSubTableEXT, with a copy of the entries. */
void APIENTRY save_ColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format,
                                    GLenum type, const void *data) {
  sgl_node *n = sgl_list_add_hand(play_ColorSubTableEXT, 7, NULL);
  if (n) {
    sgl_cell *a = n->args;
    a[1].u = target;
    a[2].i = start;
    a[3].i = count;
    a[4].u = format;
    a[5].u = type;
    a[6].p = sgl_list_copy(data, sgl_image_bytes(&sgl_ctx->c.unpack, count, 1, format, type));
    a[7].p = save_store();
  }
  if (sgl_list_executes()) sgl_ColorSubTableEXT(target, start, count, format, type, data);
}
