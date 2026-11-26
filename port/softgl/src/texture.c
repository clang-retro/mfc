/* texture.c - the textures of the software OpenGL: texture objects,
 * images, parameters, the environment, coordinate generation and the
 * paletted textures of GL_EXT_paletted_texture.
 *
 * Texels are kept as 0xAARRGGBB. A luminance texel has the luminance in
 * red, green and blue; an intensity texel has it in all four.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* Sets the parameters of a texture object to the values of the
 * specification. */
static void init_texture(sgl_texture *t, GLuint name, GLenum target) {
  memset(t, 0, sizeof *t);
  t->name = name;
  t->target = target;
  t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  t->mag_filter = GL_LINEAR;
  t->wrap_s = t->wrap_t = GL_REPEAT;
  t->priority = 1.0f;
  t->palette_format = GL_RGBA;
  t->refs = 1;
}

/* Frees the images of a texture object. */
static void free_levels(sgl_texture *t) {
  int i;
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) {
    free(t->level[i].texels);
    free(t->level[i].indices);
    t->level[i].texels = NULL;
    t->level[i].indices = NULL;
  }
}

/* Sets up the default textures of a new context. */
void sgl_texture_init(sgl_context *c) {
  init_texture(&c->default_1d, 0, GL_TEXTURE_1D);
  init_texture(&c->default_2d, 0, GL_TEXTURE_2D);
  c->tex_1d = &c->default_1d;
  c->tex_2d = &c->default_2d;
}

/* Lets go of the textures that a context holds: its default textures
 * and its references to bound objects. */
void sgl_texture_release(sgl_context *c) {
  free_levels(&c->default_1d);
  free_levels(&c->default_2d);
  c->tex_1d = &c->default_1d;
  c->tex_2d = &c->default_2d;
}

/* Frees all the texture objects of a set of shared objects. */
void sgl_texture_free_all(sgl_shared *s) {
  int i;
  for (i = 0; i < 64; i++) {
    sgl_texture *t = s->textures[i];
    while (t) {
      sgl_texture *next = t->next;
      free_levels(t);
      free(t);
      t = next;
    }
    s->textures[i] = NULL;
  }
}

/* Finds a texture object by name, or gives NULL. */
static sgl_texture *find_texture(sgl_shared *s, GLuint name) {
  sgl_texture *t = s->textures[name & 63];
  while (t && t->name != name) t = t->next;
  return t;
}

/* Gives the texture of the primitives: the 2D texture when it is on,
 * else the 1D texture, or NULL. */
sgl_texture *sgl_current_texture(sgl_context *c) {
  if (c->a.enabled[SGL_TEXTURE_2D]) return c->tex_2d;
  if (c->a.enabled[SGL_TEXTURE_1D]) return c->tex_1d;
  return NULL;
}

/* Gives the bound texture of a target, or NULL for an invalid target. */
static sgl_texture *bound(sgl_context *c, GLenum target) {
  if (target == GL_TEXTURE_2D) return c->tex_2d;
  if (target == GL_TEXTURE_1D) return c->tex_1d;
  return NULL;
}

/* Gives the base format of an internal format, or 0 for an invalid
 * one. */
static GLenum base_format(GLint internal) {
  switch (internal) {
    case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12:
    case GL_LUMINANCE16:
      return GL_LUMINANCE;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2:
    case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12:
    case GL_LUMINANCE16_ALPHA16:
      return GL_LUMINANCE_ALPHA;
    case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5: case GL_RGB8:
    case GL_RGB10: case GL_RGB12: case GL_RGB16:
      return GL_RGB;
    case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA8:
    case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:
      return GL_RGBA;
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16:
      return GL_ALPHA;
    case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12:
    case GL_INTENSITY16:
      return GL_INTENSITY;
    case GL_COLOR_INDEX1_EXT: case GL_COLOR_INDEX2_EXT: case GL_COLOR_INDEX4_EXT:
    case GL_COLOR_INDEX8_EXT: case GL_COLOR_INDEX12_EXT: case GL_COLOR_INDEX16_EXT:
      return GL_COLOR_INDEX;
    default:
      return 0;
  }
}

/* Gives log2(n) when n is a power of two, or -1. */
static int log2_exact(int n) {
  int i = 0;
  if (n <= 0 || (n & (n - 1))) return -1;
  while ((1 << i) < n) i++;
  return i;
}

/* Gives a texel of base format base from RGBA bytes p. */
static uint32_t texel(GLenum base, const uint8_t *p) {
  switch (base) {
    case GL_LUMINANCE:
      return 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_LUMINANCE_ALPHA:
      return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_INTENSITY:
      return ((uint32_t)p[0] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
    case GL_ALPHA:
      return ((uint32_t)p[3] << 24) | 0x00FFFFFFu;
    case GL_RGB:
      return 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    default:
      return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
  }
}

/* Makes the texels of a paletted level from its indices and the
 * palette of the texture. */
static void expand_palette(sgl_texture *t, sgl_level *lv) {
  size_t i, n = (size_t)lv->width * lv->height;
  if (!lv->indices || !lv->texels) return;
  for (i = 0; i < n; i++) {
    int k = lv->indices[i];
    lv->texels[i] = k < t->palette_size ? t->palette[k] : 0xFF000000u;
  }
}

/* Computes if the texture has all the levels of a mipmap. */
static void check_complete(sgl_texture *t) {
  int i, w = t->level[0].width, h = t->level[0].height;
  t->complete = 0;
  if (!t->level[0].texels) return;
  for (i = 1; i < SGL_MAX_TEXTURE_LEVELS && (w > 1 || h > 1); i++) {
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
    if (!t->level[i].texels || t->level[i].width != w || t->level[i].height != h) return;
  }
  t->complete = 1;
}

/* Sets level lv of the bound texture of target to an image of w x h
 * pixels. Gives 1 on success. */
static int set_image(sgl_context *c, GLenum target, GLint level, GLint internal, int w, int h,
                     GLint border, GLenum format, GLenum type, const void *pixels,
                     const sgl_pixelstore *ps) {
  sgl_texture *t;
  sgl_level *lv;
  GLenum base = base_format(internal);
  int wl = log2_exact(w - 2 * border), hl = log2_exact(h - 2 * border);
  size_t n;
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) return 1;
  t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS || border < 0 || border > 1 || wl < 0 ||
      hl < 0 || w > SGL_MAX_TEXTURE_SIZE + 2 || h > SGL_MAX_TEXTURE_SIZE + 2) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (!base) {
    sgl_error(GL_INVALID_VALUE);
    return 0;
  }
  if (sgl_format_components(format) == 0) {
    sgl_error(GL_INVALID_ENUM);
    return 0;
  }
  /* The border texels are dropped: the texture is the inside. */
  w -= 2 * border;
  h -= 2 * border;
  lv = &t->level[level];
  n = (size_t)w * h;
  free(lv->texels);
  free(lv->indices);
  lv->indices = NULL;
  lv->texels = (uint32_t *)malloc(n * 4);
  if (!lv->texels) {
    sgl_error(GL_OUT_OF_MEMORY);
    return 0;
  }
  lv->width = w;
  lv->height = h;
  lv->wlog = wl;
  lv->hlog = hl;
  lv->internal = (GLenum)internal;
  lv->border = border;
  if (base == GL_COLOR_INDEX) {
    lv->base = base_format((GLint)t->palette_format);
    if (lv->base == 0 || lv->base == GL_COLOR_INDEX) lv->base = GL_RGBA;
    lv->indices = (uint8_t *)malloc(n ? n : 1);
    if (!lv->indices) {
      sgl_error(GL_OUT_OF_MEMORY);
      return 0;
    }
    if (pixels) {
      uint8_t *all = (uint8_t *)malloc((size_t)(w + 2 * border) * (h + 2 * border));
      int y;
      if (!all) {
        sgl_error(GL_OUT_OF_MEMORY);
        return 0;
      }
      sgl_unpack_index(ps, w + 2 * border, h + 2 * border, type, pixels, all);
      for (y = 0; y < h; y++)
        memcpy(lv->indices + (size_t)y * w, all + (size_t)(y + border) * (w + 2 * border) + border,
               (size_t)w);
      free(all);
    } else {
      memset(lv->indices, 0, n);
    }
    expand_palette(t, lv);
  } else {
    lv->base = base;
    if (pixels) {
      int W = w + 2 * border, H = h + 2 * border, x, y;
      uint8_t *rgba = (uint8_t *)malloc((size_t)W * H * 4);
      if (!rgba) {
        sgl_error(GL_OUT_OF_MEMORY);
        return 0;
      }
      if (!sgl_unpack_rgba8(ps, W, H, format, type, pixels, rgba)) {
        free(rgba);
        sgl_error(GL_INVALID_ENUM);
        return 0;
      }
      for (y = 0; y < h; y++) {
        const uint8_t *src = rgba + ((size_t)(y + border) * W + border) * 4;
        uint32_t *dst = lv->texels + (size_t)y * w;
        for (x = 0; x < w; x++, src += 4) dst[x] = texel(base, src);
      }
      free(rgba);
    } else {
      memset(lv->texels, 0, n * 4);
    }
  }
  check_complete(t);
  return 1;
}

/* glTexImage2D: sets a level of the 2D texture. */
void APIENTRY sgl_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                             GLint border, GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_2D && target != GL_PROXY_TEXTURE_2D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_image(c, target, level, internal, w, h, border, format, type, pixels, &c->c.unpack);
}

/* glTexImage1D: sets a level of the 1D texture (a 2D texture one texel
 * high). */
void APIENTRY sgl_TexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border,
                             GLenum format, GLenum type, const void *pixels) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_1D && target != GL_PROXY_TEXTURE_1D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_image(c, target, level, internal, w, 1 + 2 * border, border, format, type, pixels,
            &c->c.unpack);
}

/* Changes texels [x, x + w) x [y, y + h) of level lv of the bound
 * texture of target. */
static void sub_image(sgl_context *c, GLenum target, GLint level, int x, int y, int w, int h,
                      GLenum format, GLenum type, const void *pixels, const sgl_pixelstore *ps) {
  sgl_texture *t = bound(c, target);
  sgl_level *lv;
  int i, j;
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &t->level[level];
  if (!lv->texels || x < 0 || y < 0 || x + w > lv->width || y + h > lv->height) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (w <= 0 || h <= 0) return;
  if (lv->indices && format == GL_COLOR_INDEX) {
    uint8_t *idx = (uint8_t *)malloc((size_t)w * h);
    if (!idx) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    sgl_unpack_index(ps, w, h, type, pixels, idx);
    for (j = 0; j < h; j++) memcpy(lv->indices + (size_t)(y + j) * lv->width + x, idx + (size_t)j * w, (size_t)w);
    free(idx);
    expand_palette(t, lv);
  } else {
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!rgba) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    if (!sgl_unpack_rgba8(ps, w, h, format, type, pixels, rgba)) {
      free(rgba);
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
        lv->texels[(size_t)(y + j) * lv->width + x + i] = texel(lv->base, rgba + ((size_t)j * w + i) * 4);
    free(rgba);
  }
}

/* glTexSubImage2D: changes a rectangle of a level of the 2D texture. */
void APIENTRY sgl_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                GLenum format, GLenum type, const void *pixels) {
  sub_image(sgl_ctx, target, level, x, y, w, h, format, type, pixels, &sgl_ctx->c.unpack);
}

/* glTexSubImage1D: changes a span of a level of the 1D texture. */
void APIENTRY sgl_TexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format,
                                GLenum type, const void *pixels) {
  sub_image(sgl_ctx, target, level, x, 0, w, 1, format, type, pixels, &sgl_ctx->c.unpack);
}

/* Reads w x h pixels of the color buffer at x, y as RGBA bytes, rows
 * bottom first. */
static uint8_t *read_color(sgl_context *c, int x, int y, int w, int h) {
  uint8_t *p = (uint8_t *)malloc((size_t)w * h * 4 + 4);
  int i, j;
  if (!p) return NULL;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) {
      int px = x + i, py = y + j;
      uint32_t v = 0;
      uint8_t *o = p + ((size_t)j * w + i) * 4;
      if (c->color && px >= 0 && py >= 0 && px < c->width && py < c->height)
        v = c->color[(long)(c->height - 1 - py) * c->rowpixels + px];
      o[0] = (uint8_t)(v >> 16);
      o[1] = (uint8_t)(v >> 8);
      o[2] = (uint8_t)v;
      o[3] = (uint8_t)(v >> 24);
    }
  return p;
}

/* glCopyTexImage2D: sets a level of the 2D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y,
                                 GLsizei w, GLsizei h, GLint border) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  set_image(c, target, level, (GLint)internal, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE, p,
            &sgl_tight_store);
  free(p);
}

/* glCopyTexImage1D: sets a level of the 1D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexImage1D(GLenum target, GLint level, GLenum internal, GLint x, GLint y,
                                 GLsizei w, GLint border) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, 1);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  set_image(c, target, level, (GLint)internal, w, 1, border, GL_RGBA, GL_UNSIGNED_BYTE, p,
            &sgl_tight_store);
  free(p);
}

/* glCopyTexSubImage2D: changes a rectangle of the 2D texture from the
 * color buffer. */
void APIENTRY sgl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff, GLint x,
                                    GLint y, GLsizei w, GLsizei h) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, h);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, yoff, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p, &sgl_tight_store);
  free(p);
}

/* glCopyTexSubImage1D: changes a span of the 1D texture from the color
 * buffer. */
void APIENTRY sgl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoff, GLint x, GLint y,
                                    GLsizei w) {
  sgl_context *c = sgl_ctx;
  uint8_t *p = read_color(c, x, y, w, 1);
  if (!p) {
    sgl_error(GL_OUT_OF_MEMORY);
    return;
  }
  sub_image(c, target, level, xoff, 0, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, p, &sgl_tight_store);
  free(p);
}

/* glGetTexImage: gives a level of a texture. */
void APIENTRY sgl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t = bound(c, target);
  if (!t || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (!t->level[level].texels) return;
  sgl_pack_texels(c, t->level[level].width, t->level[level].height, t->level[level].texels, format,
                  type, pixels);
}

/* glTexParameterfv: sets a parameter of the bound texture of target. */
void APIENTRY sgl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_texture *t = bound(sgl_ctx, target);
  GLenum v = (GLenum)params[0];
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER: t->min_filter = v; break;
    case GL_TEXTURE_MAG_FILTER: t->mag_filter = v; break;
    case GL_TEXTURE_WRAP_S: t->wrap_s = v; break;
    case GL_TEXTURE_WRAP_T: t->wrap_t = v; break;
    case GL_TEXTURE_BORDER_COLOR: memcpy(t->border_color, params, sizeof t->border_color); break;
    case GL_TEXTURE_PRIORITY: t->priority = sgl_clamp01(params[0]); break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexParameterfv: gives a parameter of the bound texture. */
void APIENTRY sgl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_texture *t = bound(sgl_ctx, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_MIN_FILTER: params[0] = (GLfloat)t->min_filter; break;
    case GL_TEXTURE_MAG_FILTER: params[0] = (GLfloat)t->mag_filter; break;
    case GL_TEXTURE_WRAP_S: params[0] = (GLfloat)t->wrap_s; break;
    case GL_TEXTURE_WRAP_T: params[0] = (GLfloat)t->wrap_t; break;
    case GL_TEXTURE_BORDER_COLOR: memcpy(params, t->border_color, sizeof t->border_color); break;
    case GL_TEXTURE_PRIORITY: params[0] = t->priority; break;
    case GL_TEXTURE_RESIDENT: params[0] = 1.0f; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexParameteriv: as glGetTexParameterfv, as integers. */
void APIENTRY sgl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_BORDER_COLOR ? 4 : 1;
  sgl_GetTexParameterfv(target, pname, f);
  for (i = 0; i < n; i++)
    params[i] = pname == GL_TEXTURE_BORDER_COLOR ? (GLint)(f[i] * 2147483647.0f) : (GLint)f[i];
}

/* glGetTexLevelParameterfv: gives a value of a level of a texture. */
void APIENTRY sgl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params) {
  GLint i;
  sgl_GetTexLevelParameteriv(target, level, pname, &i);
  params[0] = (GLfloat)i;
}

/* glGetTexLevelParameteriv: gives a value of a level of a texture. For
 * a proxy target the values are those of a texture that would fit. */
void APIENTRY sgl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  const sgl_level *lv;
  sgl_texture *t;
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    /* Every proxy image of a valid size fits. */
    params[0] = pname == GL_TEXTURE_WIDTH || pname == GL_TEXTURE_HEIGHT ? SGL_MAX_TEXTURE_SIZE : 0;
    return;
  }
  t = bound(c, target);
  if (!t || level < 0 || level >= SGL_MAX_TEXTURE_LEVELS) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  lv = &t->level[level];
  switch (pname) {
    case GL_TEXTURE_WIDTH: params[0] = lv->texels ? lv->width + 2 * lv->border : 0; break;
    case GL_TEXTURE_HEIGHT: params[0] = lv->texels ? lv->height + 2 * lv->border : 0; break;
    case GL_TEXTURE_BORDER: params[0] = lv->border; break;
    case GL_TEXTURE_INTERNAL_FORMAT: params[0] = lv->texels ? (GLint)lv->internal : 1; break;
    case GL_TEXTURE_RED_SIZE: case GL_TEXTURE_GREEN_SIZE: case GL_TEXTURE_BLUE_SIZE:
    case GL_TEXTURE_ALPHA_SIZE: case GL_TEXTURE_LUMINANCE_SIZE: case GL_TEXTURE_INTENSITY_SIZE:
      params[0] = lv->texels ? 8 : 0;
      break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glTexEnvfv: sets a parameter of the texture environment. */
void APIENTRY sgl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  if (target != GL_TEXTURE_ENV) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (pname == GL_TEXTURE_ENV_MODE)
    c->a.tex_env_mode = (GLenum)params[0];
  else if (pname == GL_TEXTURE_ENV_COLOR) {
    int i;
    for (i = 0; i < 4; i++) c->a.tex_env_color[i] = sgl_clamp01(params[i]);
  } else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnvfv: gives a parameter of the texture environment. */
void APIENTRY sgl_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *params) {
  sgl_context *c = sgl_ctx;
  (void)target;
  if (pname == GL_TEXTURE_ENV_MODE)
    params[0] = (GLfloat)c->a.tex_env_mode;
  else if (pname == GL_TEXTURE_ENV_COLOR)
    memcpy(params, c->a.tex_env_color, sizeof c->a.tex_env_color);
  else
    sgl_error(GL_INVALID_ENUM);
}

/* glGetTexEnviv: as glGetTexEnvfv, as integers. */
void APIENTRY sgl_GetTexEnviv(GLenum target, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  (void)target;
  if (pname == GL_TEXTURE_ENV_MODE)
    params[0] = (GLint)c->a.tex_env_mode;
  else if (pname == GL_TEXTURE_ENV_COLOR) {
    int i;
    for (i = 0; i < 4; i++) params[i] = (GLint)(c->a.tex_env_color[i] * 2147483647.0f);
  } else
    sgl_error(GL_INVALID_ENUM);
}

/* glTexGenfv: sets a parameter of the generation of a texture
 * coordinate. An eye plane is kept in eye coordinates. */
void APIENTRY sgl_TexGenfv(GLenum coord, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  int i = (int)coord - GL_S;
  if (i < 0 || i > 3) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_GEN_MODE: c->a.gen_mode[i] = (GLenum)params[0]; break;
    case GL_OBJECT_PLANE: memcpy(c->a.gen_object[i], params, 4 * sizeof(GLfloat)); break;
    case GL_EYE_PLANE: {
      const GLfloat *m;
      GLfloat *p = c->a.gen_eye[i];
      sgl_update_inverse(c);
      m = c->inv_modelview.m;
      p[0] = params[0] * m[0] + params[1] * m[1] + params[2] * m[2] + params[3] * m[3];
      p[1] = params[0] * m[4] + params[1] * m[5] + params[2] * m[6] + params[3] * m[7];
      p[2] = params[0] * m[8] + params[1] * m[9] + params[2] * m[10] + params[3] * m[11];
      p[3] = params[0] * m[12] + params[1] * m[13] + params[2] * m[14] + params[3] * m[15];
      break;
    }
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexGenfv: gives a parameter of the generation of a coordinate. */
void APIENTRY sgl_GetTexGenfv(GLenum coord, GLenum pname, GLfloat *params) {
  sgl_context *c = sgl_ctx;
  int i = (int)coord - GL_S;
  if (i < 0 || i > 3) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  switch (pname) {
    case GL_TEXTURE_GEN_MODE: params[0] = (GLfloat)c->a.gen_mode[i]; break;
    case GL_OBJECT_PLANE: memcpy(params, c->a.gen_object[i], 4 * sizeof(GLfloat)); break;
    case GL_EYE_PLANE: memcpy(params, c->a.gen_eye[i], 4 * sizeof(GLfloat)); break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetTexGendv: as glGetTexGenfv, as doubles. */
void APIENTRY sgl_GetTexGendv(GLenum coord, GLenum pname, GLdouble *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
  sgl_GetTexGenfv(coord, pname, f);
  for (i = 0; i < n; i++) params[i] = f[i];
}

/* glGetTexGeniv: as glGetTexGenfv, as integers. */
void APIENTRY sgl_GetTexGeniv(GLenum coord, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
  sgl_GetTexGenfv(coord, pname, f);
  for (i = 0; i < n; i++) params[i] = (GLint)f[i];
}

/* glGenTextures: gives n names that are not in use. */
void APIENTRY sgl_GenTextures(GLsizei n, GLuint *textures) {
  sgl_shared *s = sgl_ctx->shared;
  int i;
  if (n < 0) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  for (i = 0; i < n; i++) {
    while (find_texture(s, s->next_texture) || s->next_texture == 0) s->next_texture++;
    textures[i] = s->next_texture++;
  }
}

/* glBindTexture: makes a texture object the texture of target. A new
 * name makes a new object. */
void APIENTRY sgl_BindTexture(GLenum target, GLuint name) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  sgl_texture *t;
  if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  if (name == 0) {
    t = target == GL_TEXTURE_2D ? &c->default_2d : &c->default_1d;
  } else {
    t = find_texture(s, name);
    if (!t) {
      t = (sgl_texture *)malloc(sizeof *t);
      if (!t) {
        sgl_error(GL_OUT_OF_MEMORY);
        return;
      }
      init_texture(t, name, target);
      t->next = s->textures[name & 63];
      s->textures[name & 63] = t;
    } else if (t->target != target) {
      sgl_error(GL_INVALID_OPERATION);
      return;
    }
  }
  if (target == GL_TEXTURE_2D)
    c->tex_2d = t;
  else
    c->tex_1d = t;
}

/* glDeleteTextures: deletes texture objects. A bound one is unbound. */
void APIENTRY sgl_DeleteTextures(GLsizei n, const GLuint *textures) {
  sgl_context *c = sgl_ctx;
  sgl_shared *s = c->shared;
  int i;
  for (i = 0; i < n; i++) {
    GLuint name = textures[i];
    sgl_texture **pp, *t;
    if (name == 0) continue;
    pp = &s->textures[name & 63];
    while (*pp && (*pp)->name != name) pp = &(*pp)->next;
    t = *pp;
    if (!t) continue;
    *pp = t->next;
    if (c->tex_2d == t) c->tex_2d = &c->default_2d;
    if (c->tex_1d == t) c->tex_1d = &c->default_1d;
    free_levels(t);
    free(t);
  }
}

/* glIsTexture: tells if name is a texture object. */
GLboolean APIENTRY sgl_IsTexture(GLuint name) {
  return name && find_texture(sgl_ctx->shared, name) ? GL_TRUE : GL_FALSE;
}

/* glAreTexturesResident: all textures are in memory. */
GLboolean APIENTRY sgl_AreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences) {
  int i;
  (void)textures;
  for (i = 0; i < n; i++) residences[i] = GL_TRUE;
  return GL_TRUE;
}

/* glPrioritizeTextures: keeps the priorities. */
void APIENTRY sgl_PrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities) {
  int i;
  for (i = 0; i < n; i++) {
    sgl_texture *t = find_texture(sgl_ctx->shared, textures[i]);
    if (t) t->priority = sgl_clamp01(priorities[i]);
  }
}

/* Sets entries [start, start + count) of the palette of a texture from
 * an image of count pixels. */
static void set_palette(sgl_texture *t, int start, int count, GLenum format, GLenum type,
                        const void *data) {
  uint8_t rgba[256 * 4];
  int i;
  if (start < 0 || count < 0 || start + count > 256) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (!data || count == 0) return;
  if (!sgl_unpack_rgba8(&sgl_ctx->c.unpack, count, 1, format, type, data, rgba)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (i = 0; i < count; i++) t->palette[start + i] = texel(base_format((GLint)t->palette_format), rgba + i * 4);
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++) expand_palette(t, &t->level[i]);
}

/* glColorTableEXT: sets the palette of the bound texture. For a proxy
 * target it only records the size, which is good up to 256. */
void APIENTRY sgl_ColorTableEXT(GLenum target, GLenum internal, GLsizei width, GLenum format,
                                GLenum type, const void *table) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t;
  int i;
  if (width < 1 || width > 256 || (width & (width - 1))) {
    sgl_error(GL_INVALID_VALUE);
    return;
  }
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    c->proxy_palette_size = width;
    return;
  }
  t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  t->palette_format = internal;
  t->palette_size = width;
  for (i = 0; i < SGL_MAX_TEXTURE_LEVELS; i++)
    if (t->level[i].indices) {
      t->level[i].base = base_format((GLint)internal);
      if (!t->level[i].base || t->level[i].base == GL_COLOR_INDEX) t->level[i].base = GL_RGBA;
    }
  set_palette(t, 0, width, format, type, table);
}

/* glColorSubTableEXT: changes entries of the palette of the bound
 * texture. */
void APIENTRY sgl_ColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format,
                                   GLenum type, const void *data) {
  sgl_texture *t = bound(sgl_ctx, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  set_palette(t, start, count, format, type, data);
}

/* glGetColorTableEXT: gives the palette of the bound texture. */
void APIENTRY sgl_GetColorTableEXT(GLenum target, GLenum format, GLenum type, void *data) {
  sgl_context *c = sgl_ctx;
  sgl_texture *t = bound(c, target);
  if (!t) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sgl_pack_texels(c, t->palette_size, 1, t->palette, format, type, data);
}

/* glGetColorTableParameterivEXT: gives a value of a palette. */
void APIENTRY sgl_GetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params) {
  sgl_context *c = sgl_ctx;
  int size;
  GLenum fmt = GL_RGBA;
  if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D) {
    size = c->proxy_palette_size;
  } else {
    sgl_texture *t = bound(c, target);
    if (!t) {
      sgl_error(GL_INVALID_ENUM);
      return;
    }
    size = t->palette_size;
    fmt = t->palette_format;
  }
  switch (pname) {
    case GL_COLOR_TABLE_WIDTH_EXT: params[0] = size; break;
    case GL_COLOR_TABLE_FORMAT_EXT: params[0] = (GLint)fmt; break;
    case GL_COLOR_TABLE_RED_SIZE_EXT: case GL_COLOR_TABLE_GREEN_SIZE_EXT:
    case GL_COLOR_TABLE_BLUE_SIZE_EXT: case GL_COLOR_TABLE_ALPHA_SIZE_EXT:
      params[0] = 8;
      break;
    default: params[0] = 0; break;
  }
}

/* glGetColorTableParameterfvEXT: as glGetColorTableParameterivEXT. */
void APIENTRY sgl_GetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params) {
  GLint i;
  sgl_GetColorTableParameterivEXT(target, pname, &i);
  params[0] = (GLfloat)i;
}
