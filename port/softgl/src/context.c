/* context.c - the contexts of the software OpenGL, the enables, the
 * simple state, the queries and the attribute stacks.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

SGL_THREAD sgl_context *sgl_ctx = NULL;
SGL_THREAD const struct sgl_dispatch *sgl_cur = &sgl_nop_table;

/* The strings of glGetString. The version must have "1.1": the
 * programs of the Windows SDK look for it. */
static const char kVendor[] = "clang-retro";
static const char kRenderer[] = "clang-retro Generic";
static const char kVersion[] = "1.1.0";
static const char kExtensions[] =
    "GL_WIN_swap_hint GL_EXT_bgra GL_EXT_paletted_texture GL_EXT_vertex_array";

/* Records error e, if no error is recorded yet. */
void sgl_error(GLenum e) {
  if (sgl_ctx && sgl_ctx->error == GL_NO_ERROR) sgl_ctx->error = e;
}

/* Gives the number of values of the parameter pname of glLightfv,
 * glMaterialfv, glLightModelfv, glTexParameterfv, glTexEnvfv, glTexGenfv
 * and glFogfv. */
int sgl_pname_count(GLenum pname) {
  switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_POSITION:
    case GL_AMBIENT_AND_DIFFUSE:
    case GL_LIGHT_MODEL_AMBIENT:
    case GL_TEXTURE_BORDER_COLOR:
    case GL_TEXTURE_ENV_COLOR:
    case GL_OBJECT_PLANE:
    case GL_EYE_PLANE:
    case GL_FOG_COLOR:
      return 4;
    case GL_SPOT_DIRECTION:
    case GL_COLOR_INDEXES:
      return 3;
    default:
      return 1;
  }
}

/* Gives 1 when the parameter pname is a color, which an integer form
 * gives in the range of the integers. */
int sgl_pname_is_color(GLenum pname) {
  switch (pname) {
    case GL_AMBIENT:
    case GL_DIFFUSE:
    case GL_SPECULAR:
    case GL_EMISSION:
    case GL_AMBIENT_AND_DIFFUSE:
    case GL_LIGHT_MODEL_AMBIENT:
    case GL_TEXTURE_BORDER_COLOR:
    case GL_TEXTURE_ENV_COLOR:
    case GL_FOG_COLOR:
      return 1;
    default:
      return 0;
  }
}

/* Gives the index in sgl_cap of a capability of glEnable, or -1. */
int sgl_cap_index(GLenum cap) {
  if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + SGL_MAX_LIGHTS) return SGL_LIGHT0 + (int)(cap - GL_LIGHT0);
  if (cap >= GL_CLIP_PLANE0 && cap < GL_CLIP_PLANE0 + SGL_MAX_CLIP_PLANES)
    return SGL_CLIP_PLANE0 + (int)(cap - GL_CLIP_PLANE0);
  switch (cap) {
    case GL_ALPHA_TEST: return SGL_ALPHA_TEST;
    case GL_AUTO_NORMAL: return SGL_AUTO_NORMAL;
    case GL_BLEND: return SGL_BLEND;
    case GL_COLOR_MATERIAL: return SGL_COLOR_MATERIAL;
    case GL_CULL_FACE: return SGL_CULL_FACE;
    case GL_DEPTH_TEST: return SGL_DEPTH_TEST;
    case GL_DITHER: return SGL_DITHER;
    case GL_FOG: return SGL_FOG;
    case GL_LIGHTING: return SGL_LIGHTING;
    case GL_LINE_SMOOTH: return SGL_LINE_SMOOTH;
    case GL_LINE_STIPPLE: return SGL_LINE_STIPPLE;
    case GL_COLOR_LOGIC_OP: return SGL_COLOR_LOGIC_OP;
    case GL_NORMALIZE: return SGL_NORMALIZE;
    case GL_POINT_SMOOTH: return SGL_POINT_SMOOTH;
    case GL_POLYGON_SMOOTH: return SGL_POLYGON_SMOOTH;
    case GL_POLYGON_STIPPLE: return SGL_POLYGON_STIPPLE;
    case GL_SCISSOR_TEST: return SGL_SCISSOR_TEST;
    case GL_STENCIL_TEST: return SGL_STENCIL_TEST;
    case GL_TEXTURE_1D: return SGL_TEXTURE_1D;
    case GL_TEXTURE_2D: return SGL_TEXTURE_2D;
    case GL_TEXTURE_GEN_S: return SGL_TEXTURE_GEN_S;
    case GL_TEXTURE_GEN_T: return SGL_TEXTURE_GEN_T;
    case GL_TEXTURE_GEN_R: return SGL_TEXTURE_GEN_R;
    case GL_TEXTURE_GEN_Q: return SGL_TEXTURE_GEN_Q;
    case GL_POLYGON_OFFSET_FILL: return SGL_POLYGON_OFFSET_FILL;
    case GL_POLYGON_OFFSET_LINE: return SGL_POLYGON_OFFSET_LINE;
    case GL_POLYGON_OFFSET_POINT: return SGL_POLYGON_OFFSET_POINT;
    case GL_MAP1_VERTEX_3: return SGL_MAP1_VERTEX_3;
    case GL_MAP1_VERTEX_4: return SGL_MAP1_VERTEX_4;
    case GL_MAP1_NORMAL: return SGL_MAP1_NORMAL;
    case GL_MAP1_COLOR_4: return SGL_MAP1_COLOR_4;
    case GL_MAP1_TEXTURE_COORD_1: return SGL_MAP1_TEXTURE_COORD_1;
    case GL_MAP1_TEXTURE_COORD_2: return SGL_MAP1_TEXTURE_COORD_2;
    case GL_MAP1_TEXTURE_COORD_3: return SGL_MAP1_TEXTURE_COORD_3;
    case GL_MAP1_TEXTURE_COORD_4: return SGL_MAP1_TEXTURE_COORD_4;
    case GL_MAP1_INDEX: return SGL_MAP1_INDEX;
    case GL_MAP2_VERTEX_3: return SGL_MAP2_VERTEX_3;
    case GL_MAP2_VERTEX_4: return SGL_MAP2_VERTEX_4;
    case GL_MAP2_NORMAL: return SGL_MAP2_NORMAL;
    case GL_MAP2_COLOR_4: return SGL_MAP2_COLOR_4;
    case GL_MAP2_TEXTURE_COORD_1: return SGL_MAP2_TEXTURE_COORD_1;
    case GL_MAP2_TEXTURE_COORD_2: return SGL_MAP2_TEXTURE_COORD_2;
    case GL_MAP2_TEXTURE_COORD_3: return SGL_MAP2_TEXTURE_COORD_3;
    case GL_MAP2_TEXTURE_COORD_4: return SGL_MAP2_TEXTURE_COORD_4;
    case GL_MAP2_INDEX: return SGL_MAP2_INDEX;
    default: return -1;
  }
}

/* Sets the attribute state of a new context to the values of the
 * specification. */
static void init_attrib(sgl_attrib *a) {
  int i;
  memset(a, 0, sizeof *a);
  a->enabled[SGL_DITHER] = 1;
  a->color[0] = a->color[1] = a->color[2] = a->color[3] = 1.0f;
  a->normal[2] = 1.0f;
  a->tex[3] = 1.0f;
  a->index = 1.0f;
  a->raster_pos[3] = 1.0f;
  a->raster_color[0] = a->raster_color[1] = a->raster_color[2] = a->raster_color[3] = 1.0f;
  a->raster_tex[3] = 1.0f;
  a->raster_valid = GL_TRUE;
  a->edge = GL_TRUE;
  a->point_size = 1.0f;
  a->line_width = 1.0f;
  a->line_repeat = 1;
  a->line_pattern = 0xFFFF;
  a->cull_face = GL_BACK;
  a->front_face = GL_CCW;
  a->polygon_mode[0] = a->polygon_mode[1] = GL_FILL;
  for (i = 0; i < 32; i++) a->stipple[i] = 0xFFFFFFFFu;
  a->zoom_x = a->zoom_y = 1.0f;
  a->red_scale = a->green_scale = a->blue_scale = a->alpha_scale = a->depth_scale = 1.0f;
  a->read_buffer = GL_BACK;
  for (i = 0; i < SGL_MAX_LIGHTS; i++) {
    sgl_light *l = &a->light[i];
    l->ambient[3] = 1.0f;
    if (i == 0) {
      l->diffuse[0] = l->diffuse[1] = l->diffuse[2] = l->diffuse[3] = 1.0f;
      l->specular[0] = l->specular[1] = l->specular[2] = l->specular[3] = 1.0f;
    } else {
      l->diffuse[3] = l->specular[3] = 1.0f;
    }
    l->position[2] = 1.0f;
    l->spot_dir[2] = -1.0f;
    l->spot_cutoff = 180.0f;
    l->attenuation[0] = 1.0f;
    sgl_light_changed(l);
  }
  for (i = 0; i < 2; i++) {
    sgl_material *m = &a->material[i];
    m->ambient[0] = m->ambient[1] = m->ambient[2] = 0.2f;
    m->ambient[3] = 1.0f;
    m->diffuse[0] = m->diffuse[1] = m->diffuse[2] = 0.8f;
    m->diffuse[3] = 1.0f;
    m->specular[3] = 1.0f;
    m->emission[3] = 1.0f;
    {
      int k;
      for (k = 0; k < SGL_SHINE_CACHE; k++) m->shine_of[k] = -1.0f;
    }
    sgl_material_changed(m);
  }
  a->model_ambient[0] = a->model_ambient[1] = a->model_ambient[2] = 0.2f;
  a->model_ambient[3] = 1.0f;
  a->shade_model = GL_SMOOTH;
  a->color_material_face = GL_FRONT_AND_BACK;
  a->color_material_mode = GL_AMBIENT_AND_DIFFUSE;
  a->fog_mode = GL_EXP;
  a->fog_density = 1.0f;
  a->fog_end = 1.0f;
  a->depth_func = GL_LESS;
  a->depth_mask = GL_TRUE;
  a->clear_depth = 1.0;
  a->alpha_func = GL_ALWAYS;
  a->blend_src = GL_ONE;
  a->blend_dst = GL_ZERO;
  a->logic_op = GL_COPY;
  a->draw_buffer = GL_BACK;
  a->color_mask[0] = a->color_mask[1] = a->color_mask[2] = a->color_mask[3] = GL_TRUE;
  a->stencil_func = GL_ALWAYS;
  a->stencil_fail = a->stencil_zfail = a->stencil_zpass = GL_KEEP;
  a->stencil_mask = a->stencil_writemask = 0xFFFFFFFFu;
  a->depth_far = 1.0;
  a->matrix_mode = GL_MODELVIEW;
  a->tex_env_mode = GL_MODULATE;
  for (i = 0; i < 4; i++) {
    a->gen_mode[i] = GL_EYE_LINEAR;
  }
  a->gen_object[0][0] = a->gen_eye[0][0] = 1.0f;
  a->gen_object[1][1] = a->gen_eye[1][1] = 1.0f;
  a->hint_perspective = a->hint_point = a->hint_line = a->hint_polygon = a->hint_fog = GL_DONT_CARE;
  a->grid1_n = a->grid2_un = a->grid2_vn = 1;
  a->grid1_u2 = a->grid2_u2 = a->grid2_v2 = 1.0f;
}

/* Sets the client state of a new context to the values of the
 * specification. */
static void init_client(sgl_client *c) {
  memset(c, 0, sizeof *c);
  c->vertex.size = c->color.size = c->tex.size = 4;
  c->vertex.type = c->normal.type = c->color.type = c->tex.type = GL_FLOAT;
  c->normal.size = 3;
  c->index.type = GL_FLOAT;
  c->index.size = 1;
  c->pack.alignment = c->unpack.alignment = 4;
}

/* Makes a new set of shared objects. */
static sgl_shared *new_shared(void) {
  sgl_shared *s = (sgl_shared *)calloc(1, sizeof *s);
  if (!s) return NULL;
  s->refs = 1;
  s->next_list = 1;
  s->next_texture = 1;
  return s;
}

/* Lets go of a set of shared objects, and frees it after its last user. */
static void release_shared(sgl_shared *s) {
  if (!s || --s->refs > 0) return;
  sgl_list_free_all(s);
  sgl_texture_free_all(s);
  free(s);
}

/* Makes a rendering context (see softgl.h). */
sgl_context *sgl_create_context(sgl_context *share) {
  sgl_context *c = (sgl_context *)calloc(1, sizeof *c);
  if (!c) return NULL;
  if (share) {
    c->shared = share->shared;
    c->shared->refs++;
  } else {
    c->shared = new_shared();
    if (!c->shared) {
      free(c);
      return NULL;
    }
  }
  c->table = &sgl_exec_table;
  c->error = GL_NO_ERROR;
  init_attrib(&c->a);
  init_client(&c->c);
  sgl_stack_init(&c->modelview, SGL_MODELVIEW_DEPTH);
  sgl_stack_init(&c->projection, SGL_PROJECTION_DEPTH);
  sgl_stack_init(&c->texture, SGL_TEXTURE_DEPTH);
  if (!c->modelview.base || !c->projection.base || !c->texture.base) {
    sgl_destroy_context(c);
    return NULL;
  }
  c->stack = &c->modelview;
  c->mvp_dirty = c->inv_dirty = 1;
  c->texture_identity = 1;
  c->prim = GL_NONE;
  c->render_mode = GL_RENDER;
  sgl_vertex_init(c);
  sgl_texture_init(c);
  sgl_eval_init(c);
  return c;
}

/* Destroys a rendering context (see softgl.h). */
void sgl_destroy_context(sgl_context *c) {
  int i;
  if (!c) return;
  if (sgl_ctx == c) sgl_make_current(NULL);
  sgl_texture_release(c);
  release_shared(c->shared);
  sgl_stack_free(&c->modelview);
  sgl_stack_free(&c->projection);
  sgl_stack_free(&c->texture);
  sgl_eval_free(c);
  for (i = 0; i < 10; i++) free(c->pixel_map[i]);
  free(c->verts);
  free(c->depth);
  free(c->stencil);
  free(c->accum);
  free(c);
}

/* Makes ctx current (see softgl.h). */
int sgl_make_current(sgl_context *c) {
  sgl_ctx = c;
  sgl_cur = c ? c->table : &sgl_nop_table;
  return 1;
}

/* Gives the current context (see softgl.h). */
sgl_context *sgl_get_current(void) { return sgl_ctx; }

/* Points a context at a color buffer and sizes its other buffers
 * (see softgl.h). */
int sgl_set_buffer(sgl_context *c, int w, int h, void *pixels, long rowpixels) {
  size_t n;
  /* The rasterizer multiplies rows and columns in 16 bits. */
  if (!c || w < 0 || h < 0 || w > SGL_MAX_SIZE || h > SGL_MAX_SIZE || rowpixels < w ||
      rowpixels > SGL_MAX_SIZE)
    return 0;
  n = (size_t)w * (size_t)h;
  if (n != c->depth_size || !c->depth) {
    sgl_depth_t *d = (sgl_depth_t *)malloc((n ? n : 1) * sizeof *d);
    if (!d) return 0;
    free(c->depth);
    free(c->stencil);
    c->depth = d;
    c->stencil = NULL; /* made on first use */
    free(c->accum);
    c->accum = NULL;
    c->depth_size = n;
    memset(d, 0xFF, n * sizeof *d);
  }
  c->color = (uint32_t *)pixels;
  c->width = w;
  c->height = h;
  c->rowpixels = rowpixels;
  /* The first context of a window gets a viewport and a scissor box
   * that cover the window. */
  if (c->a.viewport[2] == 0 && c->a.viewport[3] == 0) {
    c->a.viewport[2] = c->a.scissor[2] = w;
    c->a.viewport[3] = c->a.scissor[3] = h;
  }
  return 1;
}

/* Copies the groups of state in mask (see softgl.h). */
int sgl_copy_context(sgl_context *from, sgl_context *to, unsigned long mask) {
  sgl_context *keep = sgl_ctx;
  if (!from || !to) return 0;
  /* The copy is a push in one context and a pop in the other. */
  sgl_make_current(from);
  sgl_PushAttrib((GLbitfield)mask);
  if (from->attrib_depth == 0) {
    sgl_make_current(keep);
    return 0;
  }
  sgl_make_current(to);
  if (to->attrib_depth < SGL_ATTRIB_DEPTH) {
    to->attrib_stack[to->attrib_depth++] = from->attrib_stack[--from->attrib_depth];
    sgl_PopAttrib();
  } else {
    from->attrib_depth--;
  }
  sgl_make_current(keep);
  return 1;
}

/* Shares the lists and textures of from with ctx (see softgl.h). */
int sgl_share_lists(sgl_context *from, sgl_context *c) {
  if (!from || !c) return 0;
  if (from->shared == c->shared) return 1;
  release_shared(c->shared);
  c->shared = from->shared;
  c->shared->refs++;
  return 1;
}

/* Gives an extension function by name (see softgl.h). */
void *sgl_get_proc(const char *name) {
  const struct sgl_proc *p;
  if (!name) return NULL;
  for (p = sgl_extension_procs; p->name; p++)
    if (strcmp(p->name, name) == 0) return (void *)p->fn;
  return NULL;
}

/* Gives the current context, and records GL_INVALID_OPERATION when the
 * call is between glBegin and glEnd. Gives NULL then. */
static sgl_context *outside(void) {
  sgl_context *c = sgl_ctx;
  if (c->prim != GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return NULL;
  }
  return c;
}

/* Turns a capability on or off. */
static void set_cap(GLenum cap, int on) {
  sgl_context *c = outside();
  int i;
  if (!c) return;
  i = sgl_cap_index(cap);
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.enabled[i] = (uint8_t)(on ? 1 : 0);
}

/* glEnable: turns a capability on. */
void APIENTRY sgl_Enable(GLenum cap) { set_cap(cap, 1); }

/* glDisable: turns a capability off. */
void APIENTRY sgl_Disable(GLenum cap) { set_cap(cap, 0); }

/* Gives the array of the client state for a cap of glEnableClientState. */
static sgl_array *client_array(GLenum cap) {
  sgl_context *c = sgl_ctx;
  switch (cap) {
    case GL_VERTEX_ARRAY: return &c->c.vertex;
    case GL_NORMAL_ARRAY: return &c->c.normal;
    case GL_COLOR_ARRAY: return &c->c.color;
    case GL_INDEX_ARRAY: return &c->c.index;
    case GL_TEXTURE_COORD_ARRAY: return &c->c.tex;
    case GL_EDGE_FLAG_ARRAY: return &c->c.edge;
    default: return NULL;
  }
}

/* glIsEnabled: tells if a capability or an array is on. */
GLboolean APIENTRY sgl_IsEnabled(GLenum cap) {
  sgl_array *arr = client_array(cap);
  int i;
  if (arr) return arr->enabled;
  i = sgl_cap_index(cap);
  if (i < 0) {
    sgl_error(GL_INVALID_ENUM);
    return GL_FALSE;
  }
  return sgl_ctx->a.enabled[i] ? GL_TRUE : GL_FALSE;
}

/* glEnableClientState: turns an array on. */
void APIENTRY sgl_EnableClientState(GLenum cap) {
  sgl_array *arr = client_array(cap);
  if (!arr) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  arr->enabled = GL_TRUE;
}

/* glDisableClientState: turns an array off. */
void APIENTRY sgl_DisableClientState(GLenum cap) {
  sgl_array *arr = client_array(cap);
  if (!arr) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  arr->enabled = GL_FALSE;
}

/* glGetError: gives the recorded error and clears it. */
GLenum APIENTRY sgl_GetError(void) {
  GLenum e = sgl_ctx->error;
  sgl_ctx->error = GL_NO_ERROR;
  return e;
}

/* glGetString: gives the vendor, renderer, version or extensions. */
const GLubyte *APIENTRY sgl_GetString(GLenum name) {
  switch (name) {
    case GL_VENDOR: return (const GLubyte *)kVendor;
    case GL_RENDERER: return (const GLubyte *)kRenderer;
    case GL_VERSION: return (const GLubyte *)kVersion;
    case GL_EXTENSIONS: return (const GLubyte *)kExtensions;
    default:
      sgl_error(GL_INVALID_ENUM);
      return NULL;
  }
}

/* glHint: sets a hint. The perspective hint GL_FASTEST turns the
 * perspective correction of textures off. */
void APIENTRY sgl_Hint(GLenum target, GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (target) {
    case GL_PERSPECTIVE_CORRECTION_HINT: c->a.hint_perspective = mode; break;
    case GL_POINT_SMOOTH_HINT: c->a.hint_point = mode; break;
    case GL_LINE_SMOOTH_HINT: c->a.hint_line = mode; break;
    case GL_POLYGON_SMOOTH_HINT: c->a.hint_polygon = mode; break;
    case GL_FOG_HINT: c->a.hint_fog = mode; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glCullFace: sets the faces that culling removes. */
void APIENTRY sgl_CullFace(GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.cull_face = mode;
}

/* glFrontFace: sets the winding of the front faces. */
void APIENTRY sgl_FrontFace(GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  if (mode != GL_CW && mode != GL_CCW) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.front_face = mode;
}

/* glLineWidth: sets the width of the lines. */
void APIENTRY sgl_LineWidth(GLfloat width) {
  sgl_context *c = outside();
  if (!c) return;
  if (width <= 0.0f) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.line_width = width;
}

/* glPointSize: sets the size of the points. */
void APIENTRY sgl_PointSize(GLfloat size) {
  sgl_context *c = outside();
  if (!c) return;
  if (size <= 0.0f) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.point_size = size;
}

/* glLineStipple: sets the stipple pattern of the lines. */
void APIENTRY sgl_LineStipple(GLint factor, GLushort pattern) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.line_repeat = factor < 1 ? 1 : (factor > 256 ? 256 : factor);
  c->a.line_pattern = pattern;
}

/* glPolygonMode: sets how the polygons of the faces are drawn. */
void APIENTRY sgl_PolygonMode(GLenum face, GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  if (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (face == GL_FRONT || face == GL_FRONT_AND_BACK) c->a.polygon_mode[0] = mode;
  if (face == GL_BACK || face == GL_FRONT_AND_BACK) c->a.polygon_mode[1] = mode;
  if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) sgl_error(GL_INVALID_ENUM);
}

/* glPolygonOffset: sets the depth offset of the polygons. */
void APIENTRY sgl_PolygonOffset(GLfloat factor, GLfloat units) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.offset_factor = factor;
  c->a.offset_units = units;
}

/* glShadeModel: sets flat or smooth shading. */
void APIENTRY sgl_ShadeModel(GLenum mode) {
  sgl_context *c = outside();
  if (!c) return;
  if (mode != GL_FLAT && mode != GL_SMOOTH) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.shade_model = mode;
}

/* glScissor: sets the scissor box. */
void APIENTRY sgl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
  sgl_context *c = outside();
  if (!c) return;
  if (w < 0 || h < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  c->a.scissor[0] = x;
  c->a.scissor[1] = y;
  c->a.scissor[2] = w;
  c->a.scissor[3] = h;
}

/* glClearColor: sets the color that glClear gives. */
void APIENTRY sgl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.clear_color[0] = sgl_clamp01(r);
  c->a.clear_color[1] = sgl_clamp01(g);
  c->a.clear_color[2] = sgl_clamp01(b);
  c->a.clear_color[3] = sgl_clamp01(a);
}

/* glClearDepth: sets the depth that glClear gives. */
void APIENTRY sgl_ClearDepth(GLclampd depth) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.clear_depth = depth < 0.0 ? 0.0 : (depth > 1.0 ? 1.0 : depth);
}

/* glClearStencil: sets the stencil value that glClear gives. */
void APIENTRY sgl_ClearStencil(GLint s) {
  sgl_context *c = outside();
  if (c) c->a.stencil_clear = s;
}

/* glClearIndex: color index mode is not there; the value is kept. */
void APIENTRY sgl_ClearIndex(GLfloat i) { (void)i; }

/* glClearAccum: sets the color that glClear gives to the accumulation
 * buffer. */
void APIENTRY sgl_ClearAccum(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.clear_accum[0] = r;
  c->a.clear_accum[1] = g;
  c->a.clear_accum[2] = b;
  c->a.clear_accum[3] = a;
}

/* glColorMask: sets which color components are written. */
void APIENTRY sgl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.color_mask[0] = r;
  c->a.color_mask[1] = g;
  c->a.color_mask[2] = b;
  c->a.color_mask[3] = a;
}

/* glDepthMask: sets if the depth buffer is written. */
void APIENTRY sgl_DepthMask(GLboolean flag) {
  sgl_context *c = outside();
  if (c) c->a.depth_mask = flag;
}

/* glStencilMask: sets which stencil bits are written. */
void APIENTRY sgl_StencilMask(GLuint mask) {
  sgl_context *c = outside();
  if (c) c->a.stencil_writemask = mask;
}

/* glIndexMask: color index mode is not there. */
void APIENTRY sgl_IndexMask(GLuint mask) { (void)mask; }

/* glIndexf: color index mode is not there. */
void APIENTRY sgl_Indexf(GLfloat c) { sgl_ctx->a.index = c; }

/* glBlendFunc: sets the blend factors. */
void APIENTRY sgl_BlendFunc(GLenum sfactor, GLenum dfactor) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.blend_src = sfactor;
  c->a.blend_dst = dfactor;
}

/* glLogicOp: sets the logical operation of the color buffer. */
void APIENTRY sgl_LogicOp(GLenum opcode) {
  sgl_context *c = outside();
  if (c) c->a.logic_op = opcode;
}

/* glStencilFunc: sets the stencil test. */
void APIENTRY sgl_StencilFunc(GLenum func, GLint ref, GLuint mask) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.stencil_func = func;
  c->a.stencil_ref = ref;
  c->a.stencil_mask = mask;
}

/* glStencilOp: sets the stencil operations. */
void APIENTRY sgl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  sgl_context *c = outside();
  if (!c) return;
  c->a.stencil_fail = fail;
  c->a.stencil_zfail = zfail;
  c->a.stencil_zpass = zpass;
}

/* glDepthFunc: sets the depth test. */
void APIENTRY sgl_DepthFunc(GLenum func) {
  sgl_context *c = outside();
  if (!c) return;
  if (func < GL_NEVER || func > GL_ALWAYS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.depth_func = func;
}

/* glAlphaFunc: sets the alpha test. */
void APIENTRY sgl_AlphaFunc(GLenum func, GLclampf ref) {
  sgl_context *c = outside();
  if (!c) return;
  if (func < GL_NEVER || func > GL_ALWAYS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.alpha_func = func;
  c->a.alpha_ref = sgl_clamp01(ref);
}

/* glDrawBuffer: sets the buffer to draw in. There is one color buffer:
 * the layer copies it to the window. */
void APIENTRY sgl_DrawBuffer(GLenum mode) {
  sgl_context *c = outside();
  if (c) c->a.draw_buffer = mode;
}

/* glReadBuffer: sets the buffer to read from. */
void APIENTRY sgl_ReadBuffer(GLenum mode) {
  sgl_context *c = outside();
  if (c) c->a.read_buffer = mode;
}

/* glFlush: the renderer draws at once, so there is nothing to wait for.
 * The wgl layer copies the picture of a single-buffered window. */
void APIENTRY sgl_Flush(void) {}

/* glFinish: as glFlush. */
void APIENTRY sgl_Finish(void) {}

/* glAddSwapHintRectWIN: the layer copies the whole buffer; the hint is
 * not used. */
void APIENTRY sgl_AddSwapHintRectWIN(GLint x, GLint y, GLsizei w, GLsizei h) {
  (void)x, (void)y, (void)w, (void)h;
}

/* glRenderMode: sets render, selection or feedback mode. Selection and
 * feedback give no hits and no values. */
GLint APIENTRY sgl_RenderMode(GLenum mode) {
  sgl_context *c = outside();
  if (!c) return 0;
  if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  c->render_mode = mode;
  return 0;
}

/* glSelectBuffer: keeps the selection buffer. */
void APIENTRY sgl_SelectBuffer(GLsizei size, GLuint *buffer) {
  sgl_context *c = outside();
  if (!c) return;
  c->select_buffer = buffer;
  c->select_size = size;
}

/* glFeedbackBuffer: feedback gives no values. */
void APIENTRY sgl_FeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer) {
  (void)size, (void)type, (void)buffer;
}

/* glInitNames: empties the name stack. */
void APIENTRY sgl_InitNames(void) { sgl_ctx->name_depth = 0; }

/* glLoadName: replaces the top of the name stack. */
void APIENTRY sgl_LoadName(GLuint name) {
  sgl_context *c = sgl_ctx;
  if (c->name_depth == 0) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  c->names[c->name_depth - 1] = name;
}

/* glPushName: pushes a name on the name stack. */
void APIENTRY sgl_PushName(GLuint name) {
  sgl_context *c = sgl_ctx;
  if (c->name_depth >= SGL_NAME_DEPTH) {
    sgl_error(GL_STACK_OVERFLOW);
    return;
  }
  c->names[c->name_depth++] = name;
}

/* glPopName: pops a name off the name stack. */
void APIENTRY sgl_PopName(void) {
  sgl_context *c = sgl_ctx;
  if (c->name_depth == 0) {
    sgl_error(GL_STACK_UNDERFLOW);
    return;
  }
  c->name_depth--;
}

/* glPassThrough: feedback gives no values. */
void APIENTRY sgl_PassThrough(GLfloat token) { (void)token; }

/* Makes the accumulation buffer on first use. Gives 0 when there is not
 * sufficient memory. */
static int need_accum(sgl_context *c) {
  if (c->accum) return 1;
  c->accum = (GLfloat *)calloc(c->depth_size ? c->depth_size * 4 : 4, sizeof(GLfloat));
  return c->accum != NULL;
}

/* glAccum: an operation of the accumulation buffer. */
void APIENTRY sgl_Accum(GLenum op, GLfloat value) {
  sgl_context *c = outside();
  int x, y;
  if (!c || !c->color || !need_accum(c)) return;
  for (y = 0; y < c->height; y++) {
    uint32_t *row = c->color + (long)(c->height - 1 - y) * c->rowpixels;
    GLfloat *acc = c->accum + (size_t)y * c->width * 4;
    for (x = 0; x < c->width; x++, acc += 4) {
      uint32_t p = row[x];
      GLfloat rgba[4];
      int i;
      rgba[0] = ((p >> 16) & 0xFF) / 255.0f;
      rgba[1] = ((p >> 8) & 0xFF) / 255.0f;
      rgba[2] = (p & 0xFF) / 255.0f;
      rgba[3] = (p >> 24) / 255.0f;
      switch (op) {
        case GL_ACCUM:
          for (i = 0; i < 4; i++) acc[i] += rgba[i] * value;
          break;
        case GL_LOAD:
          for (i = 0; i < 4; i++) acc[i] = rgba[i] * value;
          break;
        case GL_ADD:
          for (i = 0; i < 4; i++) acc[i] += value;
          break;
        case GL_MULT:
          for (i = 0; i < 4; i++) acc[i] *= value;
          break;
        case GL_RETURN: {
          unsigned v[4];
          for (i = 0; i < 4; i++) v[i] = (unsigned)(sgl_clamp01(acc[i] * value) * 255.0f + 0.5f);
          row[x] = (v[3] << 24) | (v[0] << 16) | (v[1] << 8) | v[2];
          break;
        }
        default:
          sgl_error(GL_INVALID_ENUM);
          return;
      }
    }
  }
}

/* The groups of glPushAttrib that each capability belongs to, other
 * than GL_ENABLE_BIT. */
static GLbitfield cap_group(int cap) {
  if (cap >= SGL_LIGHT0 && cap < SGL_LIGHT0 + SGL_MAX_LIGHTS) return GL_LIGHTING_BIT;
  if (cap >= SGL_CLIP_PLANE0) return GL_TRANSFORM_BIT;
  if (cap >= SGL_MAP1_VERTEX_3 && cap <= SGL_MAP2_INDEX) return GL_EVAL_BIT;
  switch (cap) {
    case SGL_ALPHA_TEST: case SGL_BLEND: case SGL_DITHER: case SGL_COLOR_LOGIC_OP:
      return GL_COLOR_BUFFER_BIT;
    case SGL_AUTO_NORMAL: return GL_EVAL_BIT;
    case SGL_COLOR_MATERIAL: case SGL_LIGHTING: return GL_LIGHTING_BIT;
    case SGL_CULL_FACE: case SGL_POLYGON_SMOOTH: case SGL_POLYGON_STIPPLE:
    case SGL_POLYGON_OFFSET_FILL: case SGL_POLYGON_OFFSET_LINE: case SGL_POLYGON_OFFSET_POINT:
      return GL_POLYGON_BIT;
    case SGL_DEPTH_TEST: return GL_DEPTH_BUFFER_BIT;
    case SGL_FOG: return GL_FOG_BIT;
    case SGL_LINE_SMOOTH: case SGL_LINE_STIPPLE: return GL_LINE_BIT;
    case SGL_NORMALIZE: return GL_TRANSFORM_BIT;
    case SGL_POINT_SMOOTH: return GL_POINT_BIT;
    case SGL_SCISSOR_TEST: return GL_SCISSOR_BIT;
    case SGL_STENCIL_TEST: return GL_STENCIL_BUFFER_BIT;
    default: return GL_TEXTURE_BIT;
  }
}

/* glPushAttrib: saves the groups of state in mask. */
void APIENTRY sgl_PushAttrib(GLbitfield mask) {
  sgl_context *c = outside();
  sgl_attrib *s;
  if (!c) return;
  if (c->attrib_depth >= SGL_ATTRIB_DEPTH) {
    sgl_error(GL_STACK_OVERFLOW);
    return;
  }
  s = &c->attrib_stack[c->attrib_depth++];
  *s = c->a;
  s->mask = mask;
  s->bound_1d = c->tex_1d->name;
  s->bound_2d = c->tex_2d->name;
}

/* Copies a member of the saved state back when the group is in mask. */
#define RESTORE(bit, field) \
  do {                       \
    if (m & (bit)) a->field = s->field; \
  } while (0)
#define RESTORE_ARRAY(bit, field) \
  do {                             \
    if (m & (bit)) memcpy(a->field, s->field, sizeof a->field); \
  } while (0)

/* glPopAttrib: restores the groups that the last glPushAttrib saved. */
void APIENTRY sgl_PopAttrib(void) {
  sgl_context *c = outside();
  sgl_attrib *s, *a;
  GLbitfield m;
  int i;
  if (!c) return;
  if (c->attrib_depth == 0) {
    sgl_error(GL_STACK_UNDERFLOW);
    return;
  }
  s = &c->attrib_stack[--c->attrib_depth];
  a = &c->a;
  m = s->mask;
  for (i = 0; i < SGL_CAP_COUNT; i++)
    if ((m & GL_ENABLE_BIT) || (m & cap_group(i))) a->enabled[i] = s->enabled[i];
  RESTORE_ARRAY(GL_CURRENT_BIT, color);
  RESTORE_ARRAY(GL_CURRENT_BIT, normal);
  RESTORE_ARRAY(GL_CURRENT_BIT, tex);
  RESTORE(GL_CURRENT_BIT, index);
  RESTORE_ARRAY(GL_CURRENT_BIT, raster_pos);
  RESTORE_ARRAY(GL_CURRENT_BIT, raster_color);
  RESTORE_ARRAY(GL_CURRENT_BIT, raster_tex);
  RESTORE(GL_CURRENT_BIT, raster_distance);
  RESTORE(GL_CURRENT_BIT, raster_valid);
  RESTORE(GL_CURRENT_BIT, edge);
  RESTORE(GL_POINT_BIT, point_size);
  RESTORE(GL_LINE_BIT, line_width);
  RESTORE(GL_LINE_BIT, line_repeat);
  RESTORE(GL_LINE_BIT, line_pattern);
  RESTORE(GL_POLYGON_BIT, cull_face);
  RESTORE(GL_POLYGON_BIT, front_face);
  RESTORE_ARRAY(GL_POLYGON_BIT, polygon_mode);
  RESTORE(GL_POLYGON_BIT, offset_factor);
  RESTORE(GL_POLYGON_BIT, offset_units);
  RESTORE_ARRAY(GL_POLYGON_STIPPLE_BIT, stipple);
  RESTORE(GL_PIXEL_MODE_BIT, zoom_x);
  RESTORE(GL_PIXEL_MODE_BIT, zoom_y);
  RESTORE(GL_PIXEL_MODE_BIT, red_scale);
  RESTORE(GL_PIXEL_MODE_BIT, red_bias);
  RESTORE(GL_PIXEL_MODE_BIT, green_scale);
  RESTORE(GL_PIXEL_MODE_BIT, green_bias);
  RESTORE(GL_PIXEL_MODE_BIT, blue_scale);
  RESTORE(GL_PIXEL_MODE_BIT, blue_bias);
  RESTORE(GL_PIXEL_MODE_BIT, alpha_scale);
  RESTORE(GL_PIXEL_MODE_BIT, alpha_bias);
  RESTORE(GL_PIXEL_MODE_BIT, depth_scale);
  RESTORE(GL_PIXEL_MODE_BIT, depth_bias);
  RESTORE(GL_PIXEL_MODE_BIT, read_buffer);
  RESTORE_ARRAY(GL_LIGHTING_BIT, light);
  RESTORE_ARRAY(GL_LIGHTING_BIT, material);
  RESTORE_ARRAY(GL_LIGHTING_BIT, model_ambient);
  RESTORE(GL_LIGHTING_BIT, local_viewer);
  RESTORE(GL_LIGHTING_BIT, two_side);
  RESTORE(GL_LIGHTING_BIT, shade_model);
  RESTORE(GL_LIGHTING_BIT, color_material_face);
  RESTORE(GL_LIGHTING_BIT, color_material_mode);
  RESTORE(GL_FOG_BIT, fog_mode);
  RESTORE_ARRAY(GL_FOG_BIT, fog_color);
  RESTORE(GL_FOG_BIT, fog_density);
  RESTORE(GL_FOG_BIT, fog_start);
  RESTORE(GL_FOG_BIT, fog_end);
  RESTORE(GL_DEPTH_BUFFER_BIT, depth_func);
  RESTORE(GL_DEPTH_BUFFER_BIT, depth_mask);
  RESTORE(GL_DEPTH_BUFFER_BIT, clear_depth);
  RESTORE(GL_COLOR_BUFFER_BIT, alpha_func);
  RESTORE(GL_COLOR_BUFFER_BIT, alpha_ref);
  RESTORE(GL_COLOR_BUFFER_BIT, blend_src);
  RESTORE(GL_COLOR_BUFFER_BIT, blend_dst);
  RESTORE(GL_COLOR_BUFFER_BIT, logic_op);
  RESTORE(GL_COLOR_BUFFER_BIT, draw_buffer);
  RESTORE_ARRAY(GL_COLOR_BUFFER_BIT, clear_color);
  RESTORE_ARRAY(GL_COLOR_BUFFER_BIT, color_mask);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_func);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_fail);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_zfail);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_zpass);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_ref);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_clear);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_mask);
  RESTORE(GL_STENCIL_BUFFER_BIT, stencil_writemask);
  RESTORE_ARRAY(GL_VIEWPORT_BIT, viewport);
  RESTORE(GL_VIEWPORT_BIT, depth_near);
  RESTORE(GL_VIEWPORT_BIT, depth_far);
  RESTORE(GL_TRANSFORM_BIT, matrix_mode);
  RESTORE_ARRAY(GL_TRANSFORM_BIT, clip_plane);
  RESTORE(GL_TEXTURE_BIT, tex_env_mode);
  RESTORE_ARRAY(GL_TEXTURE_BIT, tex_env_color);
  RESTORE_ARRAY(GL_TEXTURE_BIT, gen_mode);
  RESTORE_ARRAY(GL_TEXTURE_BIT, gen_object);
  RESTORE_ARRAY(GL_TEXTURE_BIT, gen_eye);
  RESTORE(GL_HINT_BIT, hint_perspective);
  RESTORE(GL_HINT_BIT, hint_point);
  RESTORE(GL_HINT_BIT, hint_line);
  RESTORE(GL_HINT_BIT, hint_polygon);
  RESTORE(GL_HINT_BIT, hint_fog);
  RESTORE_ARRAY(GL_SCISSOR_BIT, scissor);
  RESTORE(GL_EVAL_BIT, grid1_n);
  RESTORE(GL_EVAL_BIT, grid2_un);
  RESTORE(GL_EVAL_BIT, grid2_vn);
  RESTORE(GL_EVAL_BIT, grid1_u1);
  RESTORE(GL_EVAL_BIT, grid1_u2);
  RESTORE(GL_EVAL_BIT, grid2_u1);
  RESTORE(GL_EVAL_BIT, grid2_u2);
  RESTORE(GL_EVAL_BIT, grid2_v1);
  RESTORE(GL_EVAL_BIT, grid2_v2);
  RESTORE(GL_LIST_BIT, list_base);
  RESTORE_ARRAY(GL_ACCUM_BUFFER_BIT, clear_accum);
  if (m & GL_TRANSFORM_BIT)
    c->stack = a->matrix_mode == GL_PROJECTION ? &c->projection
               : a->matrix_mode == GL_TEXTURE  ? &c->texture
                                               : &c->modelview;
  if (m & GL_TEXTURE_BIT) {
    sgl_BindTexture(GL_TEXTURE_1D, s->bound_1d);
    sgl_BindTexture(GL_TEXTURE_2D, s->bound_2d);
  }
}

/* glPushClientAttrib: saves the arrays and the pixel store. */
void APIENTRY sgl_PushClientAttrib(GLbitfield mask) {
  sgl_context *c = sgl_ctx;
  if (c->client_depth >= SGL_ATTRIB_DEPTH) {
    sgl_error(GL_STACK_OVERFLOW);
    return;
  }
  c->client_stack[c->client_depth] = c->c;
  c->client_stack[c->client_depth++].mask = mask;
}

/* glPopClientAttrib: restores what the last glPushClientAttrib saved. */
void APIENTRY sgl_PopClientAttrib(void) {
  sgl_context *c = sgl_ctx;
  sgl_client *s;
  if (c->client_depth == 0) {
    sgl_error(GL_STACK_UNDERFLOW);
    return;
  }
  s = &c->client_stack[--c->client_depth];
  if (s->mask & GL_CLIENT_PIXEL_STORE_BIT) {
    c->c.pack = s->pack;
    c->c.unpack = s->unpack;
  }
  if (s->mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
    c->c.vertex = s->vertex;
    c->c.normal = s->normal;
    c->c.color = s->color;
    c->c.index = s->index;
    c->c.tex = s->tex;
    c->c.edge = s->edge;
  }
}

/* glGetPointerv: gives the pointer of an array. */
void APIENTRY sgl_GetPointerv(GLenum pname, void **params) {
  sgl_context *c = sgl_ctx;
  switch (pname) {
    case GL_VERTEX_ARRAY_POINTER: *params = (void *)c->c.vertex.pointer; break;
    case GL_NORMAL_ARRAY_POINTER: *params = (void *)c->c.normal.pointer; break;
    case GL_COLOR_ARRAY_POINTER: *params = (void *)c->c.color.pointer; break;
    case GL_INDEX_ARRAY_POINTER: *params = (void *)c->c.index.pointer; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: *params = (void *)c->c.tex.pointer; break;
    case GL_EDGE_FLAG_ARRAY_POINTER: *params = (void *)c->c.edge.pointer; break;
    case GL_SELECTION_BUFFER_POINTER: *params = (void *)c->select_buffer; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetPointervEXT: as glGetPointerv. */
void APIENTRY sgl_GetPointervEXT(GLenum pname, void **params) { sgl_GetPointerv(pname, params); }

/* The kinds of values of the state queries. */
enum { Q_INT, Q_FLOAT, Q_BOOL, Q_COLOR, Q_ENUM };

/* Fills v with the values of the state pname and gives their number and
 * kind (a Q_ value) in *kind, or gives 0 for an unknown pname. */
static int query(sgl_context *c, GLenum pname, GLdouble *v, int *kind) {
  const sgl_attrib *a = &c->a;
  int i, n = 1;
  *kind = Q_INT;
  switch (pname) {
    case GL_CURRENT_COLOR:
      for (i = 0; i < 4; i++) v[i] = a->color[i];
      *kind = Q_COLOR;
      return 4;
    case GL_CURRENT_NORMAL:
      for (i = 0; i < 3; i++) v[i] = a->normal[i];
      *kind = Q_FLOAT;
      return 3;
    case GL_CURRENT_TEXTURE_COORDS:
      for (i = 0; i < 4; i++) v[i] = a->tex[i];
      *kind = Q_FLOAT;
      return 4;
    case GL_CURRENT_RASTER_POSITION:
      for (i = 0; i < 4; i++) v[i] = a->raster_pos[i];
      *kind = Q_FLOAT;
      return 4;
    case GL_CURRENT_RASTER_COLOR:
      for (i = 0; i < 4; i++) v[i] = a->raster_color[i];
      *kind = Q_COLOR;
      return 4;
    case GL_CURRENT_RASTER_POSITION_VALID: v[0] = a->raster_valid; *kind = Q_BOOL; return 1;
    case GL_CURRENT_RASTER_DISTANCE: v[0] = a->raster_distance; *kind = Q_FLOAT; return 1;
    case GL_COLOR_CLEAR_VALUE:
      for (i = 0; i < 4; i++) v[i] = a->clear_color[i];
      *kind = Q_COLOR;
      return 4;
    case GL_DEPTH_CLEAR_VALUE: v[0] = a->clear_depth; *kind = Q_COLOR; return 1;
    case GL_VIEWPORT:
      for (i = 0; i < 4; i++) v[i] = a->viewport[i];
      return 4;
    case GL_SCISSOR_BOX:
      for (i = 0; i < 4; i++) v[i] = a->scissor[i];
      return 4;
    case GL_DEPTH_RANGE: v[0] = a->depth_near; v[1] = a->depth_far; *kind = Q_COLOR; return 2;
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX: {
      const sgl_stack *s = pname == GL_MODELVIEW_MATRIX ? &c->modelview
                           : pname == GL_PROJECTION_MATRIX ? &c->projection : &c->texture;
      for (i = 0; i < 16; i++) v[i] = s->top->m[i];
      *kind = Q_FLOAT;
      return 16;
    }
    case GL_MODELVIEW_STACK_DEPTH: v[0] = c->modelview.depth + 1; return 1;
    case GL_PROJECTION_STACK_DEPTH: v[0] = c->projection.depth + 1; return 1;
    case GL_TEXTURE_STACK_DEPTH: v[0] = c->texture.depth + 1; return 1;
    case GL_ATTRIB_STACK_DEPTH: v[0] = c->attrib_depth; return 1;
    case GL_CLIENT_ATTRIB_STACK_DEPTH: v[0] = c->client_depth; return 1;
    case GL_NAME_STACK_DEPTH: v[0] = c->name_depth; return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: v[0] = SGL_MODELVIEW_DEPTH; return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = SGL_PROJECTION_DEPTH; return 1;
    case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = SGL_TEXTURE_DEPTH; return 1;
    case GL_MAX_ATTRIB_STACK_DEPTH: v[0] = SGL_ATTRIB_DEPTH; return 1;
    case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: v[0] = SGL_ATTRIB_DEPTH; return 1;
    case GL_MAX_NAME_STACK_DEPTH: v[0] = SGL_NAME_DEPTH; return 1;
    case GL_MAX_LIST_NESTING: v[0] = SGL_LIST_DEPTH; return 1;
    case GL_MAX_LIGHTS: v[0] = SGL_MAX_LIGHTS; return 1;
    case GL_MAX_CLIP_PLANES: v[0] = SGL_MAX_CLIP_PLANES; return 1;
    case GL_MAX_TEXTURE_SIZE: v[0] = SGL_MAX_TEXTURE_SIZE; return 1;
    case GL_MAX_EVAL_ORDER: v[0] = SGL_MAX_EVAL_ORDER; return 1;
    case GL_MAX_PIXEL_MAP_TABLE: v[0] = 256; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = v[1] = 4096; return 2;
    case GL_SUBPIXEL_BITS: v[0] = 4; return 1;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
    case GL_DEPTH_BITS: v[0] = SGL_DEPTH_BITS; return 1;
    case GL_STENCIL_BITS: v[0] = 8; return 1;
    case GL_ACCUM_RED_BITS: case GL_ACCUM_GREEN_BITS: case GL_ACCUM_BLUE_BITS:
    case GL_ACCUM_ALPHA_BITS: v[0] = 16; return 1;
    case GL_INDEX_BITS: v[0] = 0; return 1;
    case GL_RGBA_MODE: v[0] = 1; *kind = Q_BOOL; return 1;
    case GL_INDEX_MODE: v[0] = 0; *kind = Q_BOOL; return 1;
    case GL_DOUBLEBUFFER: v[0] = 1; *kind = Q_BOOL; return 1;
    case GL_STEREO: v[0] = 0; *kind = Q_BOOL; return 1;
    case GL_POINT_SIZE: v[0] = a->point_size; *kind = Q_FLOAT; return 1;
    case GL_POINT_SIZE_RANGE: v[0] = 1; v[1] = 64; *kind = Q_FLOAT; return 2;
    case GL_POINT_SIZE_GRANULARITY: v[0] = 1; *kind = Q_FLOAT; return 1;
    case GL_LINE_WIDTH: v[0] = a->line_width; *kind = Q_FLOAT; return 1;
    case GL_LINE_WIDTH_RANGE: v[0] = 1; v[1] = 64; *kind = Q_FLOAT; return 2;
    case GL_LINE_WIDTH_GRANULARITY: v[0] = 1; *kind = Q_FLOAT; return 1;
    case GL_LINE_STIPPLE_PATTERN: v[0] = a->line_pattern; return 1;
    case GL_LINE_STIPPLE_REPEAT: v[0] = a->line_repeat; return 1;
    case GL_CULL_FACE_MODE: v[0] = a->cull_face; *kind = Q_ENUM; return 1;
    case GL_FRONT_FACE: v[0] = a->front_face; *kind = Q_ENUM; return 1;
    case GL_POLYGON_MODE: v[0] = a->polygon_mode[0]; v[1] = a->polygon_mode[1]; *kind = Q_ENUM; return 2;
    case GL_POLYGON_OFFSET_FACTOR: v[0] = a->offset_factor; *kind = Q_FLOAT; return 1;
    case GL_POLYGON_OFFSET_UNITS: v[0] = a->offset_units; *kind = Q_FLOAT; return 1;
    case GL_SHADE_MODEL: v[0] = a->shade_model; *kind = Q_ENUM; return 1;
    case GL_MATRIX_MODE: v[0] = a->matrix_mode; *kind = Q_ENUM; return 1;
    case GL_DEPTH_FUNC: v[0] = a->depth_func; *kind = Q_ENUM; return 1;
    case GL_DEPTH_WRITEMASK: v[0] = a->depth_mask; *kind = Q_BOOL; return 1;
    case GL_ALPHA_TEST_FUNC: v[0] = a->alpha_func; *kind = Q_ENUM; return 1;
    case GL_ALPHA_TEST_REF: v[0] = a->alpha_ref; *kind = Q_COLOR; return 1;
    case GL_BLEND_SRC: v[0] = a->blend_src; *kind = Q_ENUM; return 1;
    case GL_BLEND_DST: v[0] = a->blend_dst; *kind = Q_ENUM; return 1;
    case GL_LOGIC_OP_MODE: v[0] = a->logic_op; *kind = Q_ENUM; return 1;
    case GL_DRAW_BUFFER: v[0] = a->draw_buffer; *kind = Q_ENUM; return 1;
    case GL_READ_BUFFER: v[0] = a->read_buffer; *kind = Q_ENUM; return 1;
    case GL_COLOR_WRITEMASK:
      for (i = 0; i < 4; i++) v[i] = a->color_mask[i];
      *kind = Q_BOOL;
      return 4;
    case GL_STENCIL_FUNC: v[0] = a->stencil_func; *kind = Q_ENUM; return 1;
    case GL_STENCIL_REF: v[0] = a->stencil_ref; return 1;
    case GL_STENCIL_VALUE_MASK: v[0] = a->stencil_mask; return 1;
    case GL_STENCIL_WRITEMASK: v[0] = a->stencil_writemask; return 1;
    case GL_STENCIL_FAIL: v[0] = a->stencil_fail; *kind = Q_ENUM; return 1;
    case GL_STENCIL_PASS_DEPTH_FAIL: v[0] = a->stencil_zfail; *kind = Q_ENUM; return 1;
    case GL_STENCIL_PASS_DEPTH_PASS: v[0] = a->stencil_zpass; *kind = Q_ENUM; return 1;
    case GL_STENCIL_CLEAR_VALUE: v[0] = a->stencil_clear; return 1;
    case GL_LIGHT_MODEL_AMBIENT:
      for (i = 0; i < 4; i++) v[i] = a->model_ambient[i];
      *kind = Q_COLOR;
      return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = a->local_viewer; *kind = Q_BOOL; return 1;
    case GL_LIGHT_MODEL_TWO_SIDE: v[0] = a->two_side; *kind = Q_BOOL; return 1;
    case GL_COLOR_MATERIAL_FACE: v[0] = a->color_material_face; *kind = Q_ENUM; return 1;
    case GL_COLOR_MATERIAL_PARAMETER: v[0] = a->color_material_mode; *kind = Q_ENUM; return 1;
    case GL_FOG_MODE: v[0] = a->fog_mode; *kind = Q_ENUM; return 1;
    case GL_FOG_DENSITY: v[0] = a->fog_density; *kind = Q_FLOAT; return 1;
    case GL_FOG_START: v[0] = a->fog_start; *kind = Q_FLOAT; return 1;
    case GL_FOG_END: v[0] = a->fog_end; *kind = Q_FLOAT; return 1;
    case GL_FOG_COLOR:
      for (i = 0; i < 4; i++) v[i] = a->fog_color[i];
      *kind = Q_COLOR;
      return 4;
    case GL_TEXTURE_BINDING_1D: v[0] = c->tex_1d->name; return 1;
    case GL_TEXTURE_BINDING_2D: v[0] = c->tex_2d->name; return 1;
    case GL_PERSPECTIVE_CORRECTION_HINT: v[0] = a->hint_perspective; *kind = Q_ENUM; return 1;
    case GL_POINT_SMOOTH_HINT: v[0] = a->hint_point; *kind = Q_ENUM; return 1;
    case GL_LINE_SMOOTH_HINT: v[0] = a->hint_line; *kind = Q_ENUM; return 1;
    case GL_POLYGON_SMOOTH_HINT: v[0] = a->hint_polygon; *kind = Q_ENUM; return 1;
    case GL_FOG_HINT: v[0] = a->hint_fog; *kind = Q_ENUM; return 1;
    case GL_LIST_BASE: v[0] = a->list_base; return 1;
    case GL_LIST_INDEX: v[0] = c->compiling ? c->compiling->name : 0; return 1;
    case GL_LIST_MODE: v[0] = c->compiling ? c->list_mode : 0; *kind = Q_ENUM; return 1;
    case GL_RENDER_MODE: v[0] = c->render_mode; *kind = Q_ENUM; return 1;
    case GL_MAP1_GRID_DOMAIN: v[0] = a->grid1_u1; v[1] = a->grid1_u2; *kind = Q_FLOAT; return 2;
    case GL_MAP1_GRID_SEGMENTS: v[0] = a->grid1_n; return 1;
    case GL_MAP2_GRID_DOMAIN:
      v[0] = a->grid2_u1; v[1] = a->grid2_u2; v[2] = a->grid2_v1; v[3] = a->grid2_v2;
      *kind = Q_FLOAT;
      return 4;
    case GL_MAP2_GRID_SEGMENTS: v[0] = a->grid2_un; v[1] = a->grid2_vn; return 2;
    case GL_ZOOM_X: v[0] = a->zoom_x; *kind = Q_FLOAT; return 1;
    case GL_ZOOM_Y: v[0] = a->zoom_y; *kind = Q_FLOAT; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = c->c.unpack.alignment; return 1;
    case GL_UNPACK_ROW_LENGTH: v[0] = c->c.unpack.row_length; return 1;
    case GL_UNPACK_SKIP_ROWS: v[0] = c->c.unpack.skip_rows; return 1;
    case GL_UNPACK_SKIP_PIXELS: v[0] = c->c.unpack.skip_pixels; return 1;
    case GL_UNPACK_SWAP_BYTES: v[0] = c->c.unpack.swap_bytes; *kind = Q_BOOL; return 1;
    case GL_UNPACK_LSB_FIRST: v[0] = c->c.unpack.lsb_first; *kind = Q_BOOL; return 1;
    case GL_PACK_ALIGNMENT: v[0] = c->c.pack.alignment; return 1;
    case GL_PACK_ROW_LENGTH: v[0] = c->c.pack.row_length; return 1;
    case GL_PACK_SKIP_ROWS: v[0] = c->c.pack.skip_rows; return 1;
    case GL_PACK_SKIP_PIXELS: v[0] = c->c.pack.skip_pixels; return 1;
    case GL_PACK_SWAP_BYTES: v[0] = c->c.pack.swap_bytes; *kind = Q_BOOL; return 1;
    case GL_PACK_LSB_FIRST: v[0] = c->c.pack.lsb_first; *kind = Q_BOOL; return 1;
    case GL_TEXTURE_ENV_MODE: v[0] = a->tex_env_mode; *kind = Q_ENUM; return 1;
    case GL_VERTEX_ARRAY_SIZE: v[0] = c->c.vertex.size; return 1;
    case GL_VERTEX_ARRAY_TYPE: v[0] = c->c.vertex.type; *kind = Q_ENUM; return 1;
    case GL_VERTEX_ARRAY_STRIDE: v[0] = c->c.vertex.stride; return 1;
    case GL_NORMAL_ARRAY_TYPE: v[0] = c->c.normal.type; *kind = Q_ENUM; return 1;
    case GL_NORMAL_ARRAY_STRIDE: v[0] = c->c.normal.stride; return 1;
    case GL_COLOR_ARRAY_SIZE: v[0] = c->c.color.size; return 1;
    case GL_COLOR_ARRAY_TYPE: v[0] = c->c.color.type; *kind = Q_ENUM; return 1;
    case GL_COLOR_ARRAY_STRIDE: v[0] = c->c.color.stride; return 1;
    case GL_TEXTURE_COORD_ARRAY_SIZE: v[0] = c->c.tex.size; return 1;
    case GL_TEXTURE_COORD_ARRAY_TYPE: v[0] = c->c.tex.type; *kind = Q_ENUM; return 1;
    case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = c->c.tex.stride; return 1;
    case GL_EDGE_FLAG: v[0] = a->edge; *kind = Q_BOOL; return 1;
    case GL_CURRENT_INDEX: v[0] = a->index; *kind = Q_FLOAT; return 1;
    case GL_AUX_BUFFERS: v[0] = 0; return 1;
    default: {
      int cap = sgl_cap_index(pname);
      sgl_array *arr = client_array(pname);
      if (arr) {
        v[0] = arr->enabled;
        *kind = Q_BOOL;
        return 1;
      }
      if (cap >= 0) {
        v[0] = a->enabled[cap];
        *kind = Q_BOOL;
        return 1;
      }
      n = 0;
      break;
    }
  }
  return n;
}

/* glGetDoublev: gives state as doubles. */
void APIENTRY sgl_GetDoublev(GLenum pname, GLdouble *params) {
  GLdouble v[16];
  int kind, i, n = query(sgl_ctx, pname, v, &kind);
  if (n == 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < n; i++) params[i] = v[i];
}

/* glGetFloatv: gives state as floats. */
void APIENTRY sgl_GetFloatv(GLenum pname, GLfloat *params) {
  GLdouble v[16];
  int kind, i, n = query(sgl_ctx, pname, v, &kind);
  if (n == 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < n; i++) params[i] = (GLfloat)v[i];
}

/* glGetIntegerv: gives state as integers. Colors map to the range of
 * the integers. */
void APIENTRY sgl_GetIntegerv(GLenum pname, GLint *params) {
  GLdouble v[16];
  int kind, i, n = query(sgl_ctx, pname, v, &kind);
  if (n == 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < n; i++) {
    if (kind == Q_COLOR)
      params[i] = (GLint)(v[i] * 2147483647.0);
    else
      params[i] = (GLint)floor(v[i] + 0.5);
  }
}

/* glGetBooleanv: gives state as booleans. */
void APIENTRY sgl_GetBooleanv(GLenum pname, GLboolean *params) {
  GLdouble v[16];
  int kind, i, n = query(sgl_ctx, pname, v, &kind);
  if (n == 0) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < n; i++) params[i] = v[i] != 0.0 ? GL_TRUE : GL_FALSE;
}
