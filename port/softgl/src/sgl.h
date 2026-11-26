/* sgl.h - the internal state of the software OpenGL (softgl).
 *
 * The renderer is the generic implementation of OpenGL 1.1, as GDI
 * Generic is on Windows. It draws into a buffer in memory that the
 * window system gives (see softgl.h). All the arithmetic of the geometry
 * is in float. The rasterizer steps in integers (16.16 fixed point).
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SGL_H
#define SGL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include "softgl.h"

#ifndef APIENTRY
#define APIENTRY
#endif

/* The names of GL_EXT_paletted_texture, when the headers do not have
 * them. */
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
#ifndef GL_COLOR_INDEX1_EXT
#define GL_COLOR_INDEX1_EXT 0x80E2
#define GL_COLOR_INDEX2_EXT 0x80E3
#define GL_COLOR_INDEX4_EXT 0x80E4
#define GL_COLOR_INDEX8_EXT 0x80E5
#define GL_COLOR_INDEX12_EXT 0x80E6
#define GL_COLOR_INDEX16_EXT 0x80E7
#endif

/* A thread-local variable on systems with threads. The classic Mac OS
 * has one thread for the renderer. */
#if defined(macintosh) || defined(__MSDOS__) || defined(SGL_NO_TLS)
#define SGL_THREAD
#else
#define SGL_THREAD _Thread_local
#endif

/* The size of the depth buffer elements: 16 bits on the 68k and other
 * small machines, 32 bits elsewhere. */
#ifndef SGL_DEPTH_BITS
#if defined(__m68k__) || defined(__MSDOS__) || defined(__i86__)
#define SGL_DEPTH_BITS 16
#else
#define SGL_DEPTH_BITS 32
#endif
#endif
#if SGL_DEPTH_BITS == 16
typedef uint16_t sgl_depth_t;
#define SGL_DEPTH_SHIFT 15 /* from the 31-bit z of the rasterizer */
#else
typedef uint32_t sgl_depth_t;
#define SGL_DEPTH_SHIFT 0
#endif
/* Multiplies two values that fit in 16 bits with sign. The 68000 has
 * no 32-bit multiply: this is one muls instruction there, not a call to
 * the runtime library. */
#define SGL_MUL16(a, b) ((int32_t)(int16_t)(a) * (int32_t)(int16_t)(b))

/* The z of the rasterizer: [0, 1] is [0, SGL_Z_MAX]. */
#define SGL_Z_MAX 0x7FFFFFFF

/* The limits. */
#define SGL_MAX_SIZE 32767 /* the width and height of a buffer */
#define SGL_MAX_LIGHTS 8
#define SGL_MAX_CLIP_PLANES 6
#define SGL_MODELVIEW_DEPTH 32
#define SGL_PROJECTION_DEPTH 4
#define SGL_TEXTURE_DEPTH 4
#define SGL_ATTRIB_DEPTH 16
#define SGL_NAME_DEPTH 64
#define SGL_LIST_DEPTH 64
#define SGL_MAX_TEXTURE_LEVELS 11
#define SGL_MAX_TEXTURE_SIZE 1024
#define SGL_MAX_EVAL_ORDER 16
#define SGL_SHINE_TABLE 256
#define SGL_SHINE_CACHE 4

/* One argument of a command in a display list. */
typedef union sgl_cell {
  GLfloat f;
  GLint i;
  GLuint u;
  GLdouble d;
  void *p;
  void (*fn)(const union sgl_cell *);
} sgl_cell;

/* An extension function by name. */
struct sgl_proc {
  const char *name;
  void (*fn)(void);
};

#include "dispatch.h"

/* A 4 x 4 matrix, in column order as OpenGL keeps it. */
typedef struct sgl_matrix {
  GLfloat m[16];
} sgl_matrix;

/* A stack of matrices. */
typedef struct sgl_stack {
  sgl_matrix *top;
  sgl_matrix *base;
  int depth, max;
} sgl_stack;

/* A vertex after transformation and lighting. */
typedef struct sgl_vertex {
  GLfloat clip[4];     /* clip coordinates */
  GLfloat win[4];      /* window x, y and z (0 to 1), and 1 / w */
  GLfloat color[2][4]; /* front and back color, 0 to 1 */
  GLfloat tex[4];      /* texture coordinates, after the texture matrix */
  GLfloat fog;         /* fog factor: 1 is no fog */
  GLfloat eye[4];      /* eye coordinates (for clipping by the user planes) */
  unsigned clipmask;   /* the planes that it is outside of */
  GLboolean edge;      /* the edge flag */
} sgl_vertex;

/* A light. */
typedef struct sgl_light {
  GLfloat ambient[4], diffuse[4], specular[4];
  GLfloat position[4];     /* in eye coordinates */
  GLfloat spot_dir[3];     /* in eye coordinates */
  GLfloat spot_exponent, spot_cutoff;
  GLfloat attenuation[3];  /* constant, linear, quadratic */
  /* Computed when the light is changed: */
  GLfloat cos_cutoff;
  GLfloat vp_unit[3];      /* the unit direction of a directional light */
  GLfloat half_inf[3];     /* the half vector for an infinite viewer */
} sgl_light;

/* A material (one side). */
typedef struct sgl_material {
  GLfloat ambient[4], diffuse[4], specular[4], emission[4];
  GLfloat shininess;
  /* Tables of x^shininess for x in [0, 1], for the last shininess
   * values: a program that changes between a few materials does not
   * compute a table again. */
  GLfloat shine_table[SGL_SHINE_CACHE][SGL_SHINE_TABLE + 1];
  GLfloat shine_of[SGL_SHINE_CACHE]; /* the shininess of each table */
  int shine_cur;           /* the table of the current shininess */
  int shine_next;          /* the table to replace next */
} sgl_material;

/* One level of a texture image. The texels are 0xAARRGGBB. */
typedef struct sgl_level {
  uint32_t *texels;
  uint8_t *indices;        /* for a paletted texture: the indices */
  int width, height;
  int wlog, hlog;          /* log2 of the size, or -1 */
  GLenum internal;         /* the internal format that was asked for */
  GLenum base;             /* GL_RGBA, GL_RGB, GL_ALPHA, GL_LUMINANCE, ... */
  GLint border;
} sgl_level;

/* A texture object. */
typedef struct sgl_texture {
  GLuint name;
  int refs;
  GLenum target;
  sgl_level level[SGL_MAX_TEXTURE_LEVELS];
  GLenum min_filter, mag_filter, wrap_s, wrap_t;
  GLfloat border_color[4];
  GLfloat priority;
  uint32_t palette[256];   /* GL_EXT_paletted_texture */
  int palette_size;
  GLenum palette_format;
  int complete;            /* all the mipmap levels are there */
  struct sgl_texture *next;
} sgl_texture;

/* A command of a display list. */
typedef struct sgl_node {
  struct sgl_node *next;
  int op;
  int count;
  sgl_cell *args;          /* count cells */
  void *data;              /* memory that the node owns, or NULL */
} sgl_node;

/* A display list. */
typedef struct sgl_list {
  GLuint name;
  sgl_node *first, *last;
  struct sgl_list *next;
} sgl_list;

/* The objects that contexts can share (wglShareLists): display lists
 * and texture objects. */
typedef struct sgl_shared {
  int refs;
  sgl_list *lists[64];       /* a hash table by name */
  sgl_texture *textures[64]; /* a hash table by name */
  GLuint next_list, next_texture;
} sgl_shared;

/* A map of an evaluator. */
typedef struct sgl_map {
  int order_u, order_v;
  GLfloat u1, u2, v1, v2;
  int k;                   /* the number of values in each point */
  GLfloat *points;         /* order_u * order_v * k values */
} sgl_map;

/* The bits of the enables, by capability. */
enum sgl_cap {
  SGL_ALPHA_TEST, SGL_AUTO_NORMAL, SGL_BLEND, SGL_COLOR_MATERIAL,
  SGL_CULL_FACE, SGL_DEPTH_TEST, SGL_DITHER, SGL_FOG, SGL_LIGHTING,
  SGL_LINE_SMOOTH, SGL_LINE_STIPPLE, SGL_COLOR_LOGIC_OP, SGL_NORMALIZE,
  SGL_POINT_SMOOTH, SGL_POLYGON_SMOOTH, SGL_POLYGON_STIPPLE,
  SGL_SCISSOR_TEST, SGL_STENCIL_TEST, SGL_TEXTURE_1D, SGL_TEXTURE_2D,
  SGL_TEXTURE_GEN_S, SGL_TEXTURE_GEN_T, SGL_TEXTURE_GEN_R,
  SGL_TEXTURE_GEN_Q, SGL_POLYGON_OFFSET_FILL, SGL_POLYGON_OFFSET_LINE,
  SGL_POLYGON_OFFSET_POINT, SGL_MAP1_VERTEX_3, SGL_MAP1_VERTEX_4,
  SGL_MAP1_NORMAL, SGL_MAP1_COLOR_4, SGL_MAP1_TEXTURE_COORD_1,
  SGL_MAP1_TEXTURE_COORD_2, SGL_MAP1_TEXTURE_COORD_3,
  SGL_MAP1_TEXTURE_COORD_4, SGL_MAP1_INDEX, SGL_MAP2_VERTEX_3,
  SGL_MAP2_VERTEX_4, SGL_MAP2_NORMAL, SGL_MAP2_COLOR_4,
  SGL_MAP2_TEXTURE_COORD_1, SGL_MAP2_TEXTURE_COORD_2,
  SGL_MAP2_TEXTURE_COORD_3, SGL_MAP2_TEXTURE_COORD_4, SGL_MAP2_INDEX,
  SGL_LIGHT0, /* to SGL_LIGHT0 + 7 */
  SGL_CLIP_PLANE0 = SGL_LIGHT0 + SGL_MAX_LIGHTS, /* to + 5 */
  SGL_CAP_COUNT = SGL_CLIP_PLANE0 + SGL_MAX_CLIP_PLANES
};

/* The evaluator maps, in the order of the SGL_MAP1_ and SGL_MAP2_ caps. */
#define SGL_MAP_TYPES 9

/* The state that glPushAttrib saves, in groups. */
typedef struct sgl_attrib {
  GLbitfield mask;         /* the groups that are saved */
  uint8_t enabled[SGL_CAP_COUNT];
  /* GL_CURRENT_BIT */
  GLfloat color[4], normal[3], tex[4], index;
  GLfloat raster_pos[4], raster_color[4], raster_tex[4];
  GLfloat raster_distance;
  GLboolean raster_valid, edge;
  /* GL_POINT_BIT, GL_LINE_BIT */
  GLfloat point_size, line_width;
  GLint line_repeat;
  GLushort line_pattern;
  /* GL_POLYGON_BIT */
  GLenum cull_face, front_face, polygon_mode[2];
  GLfloat offset_factor, offset_units;
  /* GL_POLYGON_STIPPLE_BIT */
  uint32_t stipple[32];
  /* GL_PIXEL_MODE_BIT */
  GLfloat zoom_x, zoom_y;
  GLfloat red_scale, red_bias, green_scale, green_bias;
  GLfloat blue_scale, blue_bias, alpha_scale, alpha_bias;
  GLfloat depth_scale, depth_bias;
  GLenum read_buffer;
  /* GL_LIGHTING_BIT */
  sgl_light light[SGL_MAX_LIGHTS];
  sgl_material material[2];
  GLfloat model_ambient[4];
  GLboolean local_viewer, two_side;
  GLenum shade_model, color_material_face, color_material_mode;
  /* GL_FOG_BIT */
  GLenum fog_mode;
  GLfloat fog_color[4], fog_density, fog_start, fog_end;
  /* GL_DEPTH_BUFFER_BIT */
  GLenum depth_func;
  GLboolean depth_mask;
  GLclampd clear_depth;
  /* GL_COLOR_BUFFER_BIT */
  GLenum alpha_func, blend_src, blend_dst, logic_op, draw_buffer;
  GLclampf alpha_ref;
  GLfloat clear_color[4];
  GLboolean color_mask[4];
  /* GL_STENCIL_BUFFER_BIT */
  GLenum stencil_func, stencil_fail, stencil_zfail, stencil_zpass;
  GLint stencil_ref, stencil_clear;
  GLuint stencil_mask, stencil_writemask;
  /* GL_VIEWPORT_BIT */
  GLint viewport[4];
  GLclampd depth_near, depth_far;
  /* GL_TRANSFORM_BIT */
  GLenum matrix_mode;
  GLdouble clip_plane[SGL_MAX_CLIP_PLANES][4]; /* in eye coordinates */
  /* GL_TEXTURE_BIT */
  GLenum tex_env_mode;
  GLfloat tex_env_color[4];
  GLenum gen_mode[4];
  GLfloat gen_object[4][4], gen_eye[4][4];
  GLuint bound_1d, bound_2d;
  /* GL_HINT_BIT */
  GLenum hint_perspective, hint_point, hint_line, hint_polygon, hint_fog;
  /* GL_SCISSOR_BIT */
  GLint scissor[4];
  /* GL_EVAL_BIT */
  GLint grid1_n, grid2_un, grid2_vn;
  GLfloat grid1_u1, grid1_u2, grid2_u1, grid2_u2, grid2_v1, grid2_v2;
  /* GL_LIST_BIT */
  GLuint list_base;
  /* GL_ACCUM_BUFFER_BIT */
  GLfloat clear_accum[4];
} sgl_attrib;

/* A vertex array. */
typedef struct sgl_array {
  GLboolean enabled;
  GLint size;
  GLenum type;
  GLsizei stride;
  const void *pointer;
} sgl_array;

/* The pixel store of packing or unpacking. */
typedef struct sgl_pixelstore {
  GLboolean swap_bytes, lsb_first;
  GLint row_length, skip_rows, skip_pixels, alignment;
} sgl_pixelstore;

/* The state of the client: arrays and pixel store, which
 * glPushClientAttrib saves. */
typedef struct sgl_client {
  GLbitfield mask;
  sgl_array vertex, normal, color, index, tex, edge;
  sgl_pixelstore pack, unpack;
} sgl_client;

/* A rendering context. */
struct sgl_context {
  /* The buffer that it draws into (sgl_set_buffer). */
  uint32_t *color;
  long rowpixels;          /* pixels from one row to the next */
  int width, height;
  sgl_depth_t *depth;
  uint8_t *stencil;
  GLfloat *accum;
  size_t depth_size;

  sgl_shared *shared;
  const struct sgl_dispatch *table; /* the table for execution or saving */
  GLenum error;

  sgl_attrib a;            /* the attribute state */
  sgl_attrib attrib_stack[SGL_ATTRIB_DEPTH];
  int attrib_depth;
  sgl_client c;
  sgl_client client_stack[SGL_ATTRIB_DEPTH];
  int client_depth;

  /* The matrices. */
  sgl_stack modelview, projection, texture;
  sgl_stack *stack;        /* the stack of the matrix mode */
  sgl_matrix mvp;          /* projection * modelview */
  sgl_matrix inv_modelview;/* for the normals and the eye planes */
  int mvp_dirty, inv_dirty;
  int texture_identity;    /* the texture matrix is the identity */

  /* glBegin and glEnd. */
  GLenum prim;             /* the primitive, or GL_NONE outside glBegin */
  sgl_vertex *verts;       /* the vertices of the primitive */
  int nverts, maxverts;
  int lit;                 /* lighting is on for this primitive */
  int need_eye;            /* the vertices need eye coordinates */
  int need_normal;         /* the vertices need eye normals (texgen) */
  int clip_user;           /* the user clip planes that are on (bits) */
  sgl_vertex *clipverts;   /* the vertices that clipping makes */
  int nclip, maxclip;

  /* The lighting, computed again when a light or material changes. */
  int light_dirty;
  int nlights;
  int light_index[SGL_MAX_LIGHTS];
  GLfloat light_amb[2][SGL_MAX_LIGHTS][3];  /* light * material, by side */
  GLfloat light_diff[2][SGL_MAX_LIGHTS][3];
  GLfloat light_spec[2][SGL_MAX_LIGHTS][3];
  GLfloat base_color[2][4];                 /* emission + model ambient */

  /* Display lists. */
  sgl_list *compiling;     /* the list that glNewList started, or NULL */
  GLenum list_mode;
  int call_depth;

  /* Textures. */
  sgl_texture default_1d, default_2d;
  sgl_texture *tex_1d, *tex_2d;

  /* Evaluators. */
  sgl_map map1[SGL_MAP_TYPES], map2[SGL_MAP_TYPES];

  /* Selection and feedback: only GL_RENDER is drawn. */
  GLenum render_mode;
  GLuint *select_buffer;
  GLsizei select_size;
  GLuint names[SGL_NAME_DEPTH];
  int name_depth;

  /* Pixel maps (only stored). */
  GLfloat *pixel_map[10];
  GLint pixel_map_size[10];

  /* The paletted texture of GL_PROXY_TEXTURE_2D. */
  int proxy_palette_size;
};

/* The current context and the current table of this thread. */
extern SGL_THREAD sgl_context *sgl_ctx;
extern SGL_THREAD const struct sgl_dispatch *sgl_cur;
#define SGL_DISPATCH sgl_cur

/* context.c */
void sgl_error(GLenum e);
int sgl_pname_count(GLenum pname);
int sgl_pname_is_color(GLenum pname);
int sgl_cap_index(GLenum cap);
#define SGL_ON(c, cap) ((c)->a.enabled[cap])

/* matrix.c */
void sgl_matrix_identity(sgl_matrix *m);
void sgl_matrix_mul(sgl_matrix *out, const sgl_matrix *a, const sgl_matrix *b);
int sgl_matrix_invert(sgl_matrix *out, const sgl_matrix *m);
void sgl_matrix_apply(const sgl_matrix *m, const GLfloat in[4], GLfloat out[4]);
void sgl_stack_init(sgl_stack *s, int max);
void sgl_stack_free(sgl_stack *s);
void sgl_update_matrices(sgl_context *c);
void sgl_update_inverse(sgl_context *c);

/* vertex.c */
void sgl_vertex_init(sgl_context *c);
void sgl_light_changed(sgl_light *l);
void sgl_material_changed(sgl_material *m);
void sgl_transform_vertex(sgl_context *c, const GLfloat obj[4], sgl_vertex *v);
void sgl_draw_polygon(sgl_context *c, sgl_vertex **v, int n, int provoking);
void sgl_draw_line(sgl_context *c, sgl_vertex *a, sgl_vertex *b, int provoking);
void sgl_draw_point(sgl_context *c, sgl_vertex *a);
void sgl_compute_window(sgl_context *c, sgl_vertex *v);

/* raster.c */
void sgl_raster_triangle(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b,
                         const sgl_vertex *d, int side, const GLfloat *flat, GLfloat zoffset);
void sgl_raster_line(sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, int side,
                     const GLfloat *flat, GLfloat zoffset);
void sgl_raster_point(sgl_context *c, const sgl_vertex *a, int side, GLfloat zoffset);
void sgl_write_fragment(sgl_context *c, int x, int y, uint32_t z, const GLfloat rgba[4],
                        const GLfloat *tex);
void sgl_clip_rect(sgl_context *c, int *x0, int *y0, int *x1, int *y1);

/* texture.c */
void sgl_texture_init(sgl_context *c);
void sgl_texture_free_all(sgl_shared *s);
void sgl_texture_release(sgl_context *c);
sgl_texture *sgl_current_texture(sgl_context *c);

/* pixels.c */
size_t sgl_image_bytes(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type);
int sgl_unpack_rgba8(const sgl_pixelstore *ps, int w, int h, GLenum format, GLenum type,
                     const void *pixels, uint8_t *out);
void sgl_pack_texels(sgl_context *c, int w, int h, const uint32_t *texels, GLenum format,
                     GLenum type, void *dst);
void sgl_unpack_index(const sgl_pixelstore *ps, int w, int h, GLenum type,
                      const void *pixels, uint8_t *out);
int sgl_format_components(GLenum format);
extern const sgl_pixelstore sgl_tight_store;

/* list.c */
sgl_cell *sgl_list_add(int op, int count);
void *sgl_list_copy(const void *p, size_t bytes);
int sgl_list_executes(void);
void sgl_list_free_all(sgl_shared *s);
sgl_node *sgl_list_add_hand(void (*play)(const sgl_cell *), int count, void *data);

/* eval.c */
void sgl_eval_init(sgl_context *c);
void sgl_eval_free(sgl_context *c);
int sgl_map_k(GLenum target);

/* extension.c: the extension procedures by name. */
extern const struct sgl_proc sgl_extension_procs[];

/* Clamps a float to [0, 1]. */
static inline GLfloat sgl_clamp01(GLfloat x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

#endif
