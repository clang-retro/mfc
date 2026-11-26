/* vertex.c - the vertices of the software OpenGL: glBegin and glEnd,
 * the current values, lighting, texture coordinate generation, fog,
 * primitive assembly, clipping and culling.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <math.h>
#include <stdlib.h>

#include "sgl.h"

/* The bits of the clip mask: the six planes of the view volume, then
 * the user planes. */
#define CLIP_LEFT 0x01
#define CLIP_RIGHT 0x02
#define CLIP_BOTTOM 0x04
#define CLIP_TOP 0x08
#define CLIP_NEAR 0x10
#define CLIP_FAR 0x20
#define CLIP_USER0 0x40

/* Makes the vertex buffer of a new context. */
void sgl_vertex_init(sgl_context *c) {
  c->maxverts = 64;
  c->verts = (sgl_vertex *)malloc((size_t)c->maxverts * sizeof(sgl_vertex));
  if (!c->verts) c->maxverts = 0;
  c->light_dirty = 1;
}

/* Computes the values of a light that do not change from vertex to
 * vertex. */
void sgl_light_changed(sgl_light *l) {
  GLfloat len;
  if (l->spot_cutoff >= 180.0f)
    l->cos_cutoff = -1.0f;
  else
    l->cos_cutoff = cosf(l->spot_cutoff * 3.14159265358979f / 180.0f);
  len = sqrtf(l->position[0] * l->position[0] + l->position[1] * l->position[1] +
              l->position[2] * l->position[2]);
  if (len > 0.0f) {
    l->vp_unit[0] = l->position[0] / len;
    l->vp_unit[1] = l->position[1] / len;
    l->vp_unit[2] = l->position[2] / len;
  } else {
    l->vp_unit[0] = l->vp_unit[1] = 0.0f;
    l->vp_unit[2] = 1.0f;
  }
  l->half_inf[0] = l->vp_unit[0];
  l->half_inf[1] = l->vp_unit[1];
  l->half_inf[2] = l->vp_unit[2] + 1.0f;
  len = sqrtf(l->half_inf[0] * l->half_inf[0] + l->half_inf[1] * l->half_inf[1] +
              l->half_inf[2] * l->half_inf[2]);
  if (len > 0.0f) {
    l->half_inf[0] /= len;
    l->half_inf[1] /= len;
    l->half_inf[2] /= len;
  }
}

/* Gives x to the power n for a whole number n >= 0, by repeated squaring:
 * much faster than powf without an FPU. */
static GLfloat ipow(GLfloat x, unsigned n) {
  GLfloat r = 1.0f;
  while (n) {
    if (n & 1) r *= x;
    x *= x;
    n >>= 1;
  }
  return r;
}

/* Selects the table of x to the power of the shininess, and computes it
 * when no table of the cache has that shininess. */
void sgl_material_changed(sgl_material *m) {
  int i, k;
  GLfloat n = m->shininess;
  GLfloat *t;
  if (m->shine_of[m->shine_cur] == n) return;
  for (k = 0; k < SGL_SHINE_CACHE; k++) {
    if (m->shine_of[k] == n) {
      m->shine_cur = k;
      return;
    }
  }
  k = m->shine_next;
  m->shine_next = (k + 1) % SGL_SHINE_CACHE;
  m->shine_cur = k;
  m->shine_of[k] = n;
  t = m->shine_table[k];
  for (i = 0; i <= SGL_SHINE_TABLE; i++) {
    GLfloat x = (GLfloat)i / SGL_SHINE_TABLE;
    if (n == 0.0f)
      t[i] = 1.0f;
    else if (n == (GLfloat)(unsigned)n)
      t[i] = ipow(x, (unsigned)n);
    else
      t[i] = powf(x, n);
  }
}

/* Gives x to the power of the shininess of m, for x in [0, 1], from the
 * table. */
static GLfloat shine(const sgl_material *m, GLfloat x) {
  const GLfloat *t = m->shine_table[m->shine_cur];
  GLfloat f = x * SGL_SHINE_TABLE;
  int i = (int)f;
  if (i >= SGL_SHINE_TABLE) return t[SGL_SHINE_TABLE];
  f -= (GLfloat)i;
  return t[i] + (t[i + 1] - t[i]) * f;
}

/* Computes the products of the lights and the materials, when a light
 * or a material changed. */
static void update_lighting(sgl_context *c) {
  int side, i, k;
  c->nlights = 0;
  for (i = 0; i < SGL_MAX_LIGHTS; i++)
    if (c->a.enabled[SGL_LIGHT0 + i]) c->light_index[c->nlights++] = i;
  for (side = 0; side < 2; side++) {
    const sgl_material *m = &c->a.material[side];
    for (k = 0; k < 3; k++)
      c->base_color[side][k] = m->emission[k] + c->a.model_ambient[k] * m->ambient[k];
    c->base_color[side][3] = m->diffuse[3];
    for (i = 0; i < c->nlights; i++) {
      const sgl_light *l = &c->a.light[c->light_index[i]];
      for (k = 0; k < 3; k++) {
        c->light_amb[side][i][k] = l->ambient[k] * m->ambient[k];
        c->light_diff[side][i][k] = l->diffuse[k] * m->diffuse[k];
        c->light_spec[side][i][k] = l->specular[k] * m->specular[k];
      }
    }
  }
  c->light_dirty = 0;
}

/* Computes the color of one side of a lit vertex at eye position eye
 * with unit eye normal n. */
static void light_vertex(sgl_context *c, int side, const GLfloat eye[4], const GLfloat n[3],
                         GLfloat out[4]) {
  const sgl_material *m = &c->a.material[side];
  GLfloat r = c->base_color[side][0], g = c->base_color[side][1], b = c->base_color[side][2];
  GLfloat vx = 0.0f, vy = 0.0f, vz = 1.0f;
  int i;
  if (c->a.local_viewer) {
    GLfloat len = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    if (len > 0.0f) {
      vx = -eye[0] / len;
      vy = -eye[1] / len;
      vz = -eye[2] / len;
    }
  }
  for (i = 0; i < c->nlights; i++) {
    const sgl_light *l = &c->a.light[c->light_index[i]];
    GLfloat lx, ly, lz, att = 1.0f, ndotl, hx, hy, hz;
    if (l->position[3] != 0.0f) {
      GLfloat d2, d, inv;
      lx = l->position[0] - eye[0];
      ly = l->position[1] - eye[1];
      lz = l->position[2] - eye[2];
      d2 = lx * lx + ly * ly + lz * lz;
      d = sqrtf(d2);
      inv = d > 0.0f ? 1.0f / d : 0.0f;
      lx *= inv;
      ly *= inv;
      lz *= inv;
      if (l->attenuation[1] != 0.0f || l->attenuation[2] != 0.0f || l->attenuation[0] != 1.0f) {
        GLfloat den = l->attenuation[0] + l->attenuation[1] * d + l->attenuation[2] * d2;
        att = den > 0.0f ? 1.0f / den : 1.0f;
      }
      if (l->cos_cutoff > -1.0f) {
        GLfloat sp = -(lx * l->spot_dir[0] + ly * l->spot_dir[1] + lz * l->spot_dir[2]);
        if (sp < l->cos_cutoff) continue;
        if (l->spot_exponent != 0.0f) att *= powf(sp, l->spot_exponent);
      }
    } else {
      lx = l->vp_unit[0];
      ly = l->vp_unit[1];
      lz = l->vp_unit[2];
    }
    r += att * c->light_amb[side][i][0];
    g += att * c->light_amb[side][i][1];
    b += att * c->light_amb[side][i][2];
    ndotl = n[0] * lx + n[1] * ly + n[2] * lz;
    if (ndotl <= 0.0f) continue;
    r += att * ndotl * c->light_diff[side][i][0];
    g += att * ndotl * c->light_diff[side][i][1];
    b += att * ndotl * c->light_diff[side][i][2];
    if (l->position[3] == 0.0f && !c->a.local_viewer) {
      hx = l->half_inf[0];
      hy = l->half_inf[1];
      hz = l->half_inf[2];
    } else {
      GLfloat len;
      hx = lx + vx;
      hy = ly + vy;
      hz = lz + vz;
      len = sqrtf(hx * hx + hy * hy + hz * hz);
      if (len > 0.0f) {
        hx /= len;
        hy /= len;
        hz /= len;
      }
    }
    {
      GLfloat ndoth = n[0] * hx + n[1] * hy + n[2] * hz;
      if (ndoth > 0.0f) {
        GLfloat s = att * shine(m, ndoth > 1.0f ? 1.0f : ndoth);
        r += s * c->light_spec[side][i][0];
        g += s * c->light_spec[side][i][1];
        b += s * c->light_spec[side][i][2];
      }
    }
  }
  out[0] = sgl_clamp01(r);
  out[1] = sgl_clamp01(g);
  out[2] = sgl_clamp01(b);
  out[3] = sgl_clamp01(c->base_color[side][3]);
}

/* Gives the eye normal of the current normal, made a unit vector when
 * GL_NORMALIZE is on or lighting needs it. */
static void eye_normal(sgl_context *c, GLfloat out[3]) {
  const GLfloat *m = c->inv_modelview.m;
  const GLfloat *n = c->a.normal;
  out[0] = m[0] * n[0] + m[1] * n[1] + m[2] * n[2];
  out[1] = m[4] * n[0] + m[5] * n[1] + m[6] * n[2];
  out[2] = m[8] * n[0] + m[9] * n[1] + m[10] * n[2];
  if (c->a.enabled[SGL_NORMALIZE]) {
    GLfloat len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 0.0f) {
      len = 1.0f / len;
      out[0] *= len;
      out[1] *= len;
      out[2] *= len;
    }
  }
}

/* Computes the texture coordinates of a vertex: generation, then the
 * texture matrix. */
static void tex_coords(sgl_context *c, const GLfloat obj[4], const GLfloat eye[4],
                       const GLfloat n[3], GLfloat out[4]) {
  const sgl_attrib *a = &c->a;
  int i;
  out[0] = a->tex[0];
  out[1] = a->tex[1];
  out[2] = a->tex[2];
  out[3] = a->tex[3];
  for (i = 0; i < 4; i++) {
    if (!a->enabled[SGL_TEXTURE_GEN_S + i]) continue;
    switch (a->gen_mode[i]) {
      case GL_OBJECT_LINEAR: {
        const GLfloat *p = a->gen_object[i];
        out[i] = p[0] * obj[0] + p[1] * obj[1] + p[2] * obj[2] + p[3] * obj[3];
        break;
      }
      case GL_EYE_LINEAR: {
        const GLfloat *p = a->gen_eye[i];
        out[i] = p[0] * eye[0] + p[1] * eye[1] + p[2] * eye[2] + p[3] * eye[3];
        break;
      }
      case GL_SPHERE_MAP: {
        GLfloat ux = eye[0], uy = eye[1], uz = eye[2], len, d, rx, ry, rz, mm;
        len = sqrtf(ux * ux + uy * uy + uz * uz);
        if (len > 0.0f) {
          ux /= len;
          uy /= len;
          uz /= len;
        }
        d = 2.0f * (n[0] * ux + n[1] * uy + n[2] * uz);
        rx = ux - n[0] * d;
        ry = uy - n[1] * d;
        rz = uz - n[2] * d + 1.0f;
        mm = 2.0f * sqrtf(rx * rx + ry * ry + rz * rz);
        if (mm == 0.0f) mm = 1.0f;
        if (i == 0) out[0] = rx / mm + 0.5f;
        if (i == 1) out[1] = ry / mm + 0.5f;
        break;
      }
      default:
        break;
    }
  }
  if (!c->texture_identity) {
    GLfloat t[4];
    sgl_matrix_apply(c->texture.top, out, t);
    out[0] = t[0];
    out[1] = t[1];
    out[2] = t[2];
    out[3] = t[3];
  }
}

/* Gives the fog factor at eye position eye: 1 is no fog. */
static GLfloat fog_factor(const sgl_context *c, const GLfloat eye[4]) {
  GLfloat z = eye[2] < 0.0f ? -eye[2] : eye[2], f;
  switch (c->a.fog_mode) {
    case GL_LINEAR:
      if (c->a.fog_end == c->a.fog_start) return 1.0f;
      f = (c->a.fog_end - z) / (c->a.fog_end - c->a.fog_start);
      break;
    case GL_EXP:
      f = expf(-c->a.fog_density * z);
      break;
    default: {
      GLfloat dz = c->a.fog_density * z;
      f = expf(-dz * dz);
      break;
    }
  }
  return sgl_clamp01(f);
}

/* Computes the clip mask of a vertex. */
static unsigned clip_mask(const sgl_context *c, const sgl_vertex *v) {
  GLfloat x = v->clip[0], y = v->clip[1], z = v->clip[2], w = v->clip[3];
  unsigned m = 0;
  if (x < -w) m |= CLIP_LEFT;
  if (x > w) m |= CLIP_RIGHT;
  if (y < -w) m |= CLIP_BOTTOM;
  if (y > w) m |= CLIP_TOP;
  if (z < -w) m |= CLIP_NEAR;
  if (z > w) m |= CLIP_FAR;
  if (c->clip_user) {
    int i;
    for (i = 0; i < SGL_MAX_CLIP_PLANES; i++) {
      if (!(c->clip_user & (1u << i))) continue;
      {
        const GLdouble *p = c->a.clip_plane[i];
        if (p[0] * v->eye[0] + p[1] * v->eye[1] + p[2] * v->eye[2] + p[3] * v->eye[3] < 0.0)
          m |= CLIP_USER0 << i;
      }
    }
  }
  return m;
}

/* Transforms and lights a vertex at object position obj, with the
 * current normal, color and texture coordinates (see sgl.h). */
void sgl_transform_vertex(sgl_context *c, const GLfloat obj[4], sgl_vertex *v) {
  GLfloat n[3];
  sgl_matrix_apply(&c->mvp, obj, v->clip);
  if (c->need_eye) sgl_matrix_apply(c->modelview.top, obj, v->eye);
  if (c->lit || c->need_normal) eye_normal(c, n);
  if (c->lit) {
    if (c->light_dirty) update_lighting(c);
    light_vertex(c, 0, v->eye, n, v->color[0]);
    if (c->a.two_side) {
      GLfloat back[3];
      back[0] = -n[0];
      back[1] = -n[1];
      back[2] = -n[2];
      light_vertex(c, 1, v->eye, back, v->color[1]);
    }
  } else {
    memcpy(v->color[0], c->a.color, sizeof v->color[0]);
  }
  if (c->a.enabled[SGL_TEXTURE_2D] || c->a.enabled[SGL_TEXTURE_1D])
    tex_coords(c, obj, v->eye, n, v->tex);
  v->fog = c->a.enabled[SGL_FOG] ? fog_factor(c, v->eye) : 1.0f;
  v->edge = c->a.edge;
  v->clipmask = clip_mask(c, v);
}

/* Computes the window coordinates of a vertex that is in the view
 * volume. */
void sgl_compute_window(sgl_context *c, sgl_vertex *v) {
  GLfloat iw = v->clip[3] != 0.0f ? 1.0f / v->clip[3] : 1.0f;
  const GLint *vp = c->a.viewport;
  GLfloat zs = (GLfloat)(c->a.depth_far - c->a.depth_near) * 0.5f;
  GLfloat zb = (GLfloat)(c->a.depth_far + c->a.depth_near) * 0.5f;
  v->win[0] = (v->clip[0] * iw + 1.0f) * (GLfloat)vp[2] * 0.5f + (GLfloat)vp[0];
  v->win[1] = (v->clip[1] * iw + 1.0f) * (GLfloat)vp[3] * 0.5f + (GLfloat)vp[1];
  v->win[2] = v->clip[2] * iw * zs + zb;
  v->win[3] = iw;
}

/* Gives the signed distance of v from clip plane bit (positive inside). */
static GLfloat plane_dist(const sgl_context *c, const sgl_vertex *v, unsigned bit) {
  switch (bit) {
    case CLIP_LEFT: return v->clip[3] + v->clip[0];
    case CLIP_RIGHT: return v->clip[3] - v->clip[0];
    case CLIP_BOTTOM: return v->clip[3] + v->clip[1];
    case CLIP_TOP: return v->clip[3] - v->clip[1];
    case CLIP_NEAR: return v->clip[3] + v->clip[2];
    case CLIP_FAR: return v->clip[3] - v->clip[2];
    default: {
      int i = 0;
      const GLdouble *p;
      while ((unsigned)(CLIP_USER0 << i) != bit) i++;
      p = c->a.clip_plane[i];
      return (GLfloat)(p[0] * v->eye[0] + p[1] * v->eye[1] + p[2] * v->eye[2] + p[3] * v->eye[3]);
    }
  }
}

/* Gives a new vertex of the clipping buffer, growing it, or NULL. */
static sgl_vertex *new_clip_vertex(sgl_context *c) {
  if (c->nclip >= c->maxclip) return NULL;
  return &c->clipverts[c->nclip++];
}

/* Makes sure the clipping buffer has space for n vertices. Pointers
 * into the buffer stay good, as the buffer is only grown between
 * primitives. */
static int reserve_clip(sgl_context *c, int n) {
  if (n <= c->maxclip) return 1;
  {
    sgl_vertex *p = (sgl_vertex *)realloc(c->clipverts, (size_t)n * sizeof(sgl_vertex));
    if (!p) return 0;
    c->clipverts = p;
    c->maxclip = n;
  }
  return 1;
}

/* Sets out to the vertex at t from a to b. */
static void lerp_vertex(const sgl_context *c, const sgl_vertex *a, const sgl_vertex *b, GLfloat t,
                        sgl_vertex *out) {
  int i;
  for (i = 0; i < 4; i++) {
    out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    out->color[0][i] = a->color[0][i] + (b->color[0][i] - a->color[0][i]) * t;
    out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i]) * t;
    out->eye[i] = a->eye[i] + (b->eye[i] - a->eye[i]) * t;
  }
  if (c->lit && c->a.two_side)
    for (i = 0; i < 4; i++) out->color[1][i] = a->color[1][i] + (b->color[1][i] - a->color[1][i]) * t;
  out->fog = a->fog + (b->fog - a->fog) * t;
  out->edge = a->edge;
  out->clipmask = clip_mask(c, out);
}

/* Clips the polygon in[0..n-1] by each plane in mask. Gives the number
 * of vertices in out, which holds pointers to in's vertices or to new
 * vertices of the clipping buffer. */
static int clip_polygon(sgl_context *c, sgl_vertex **in, int n, unsigned mask, sgl_vertex **out) {
  sgl_vertex **src = in, **dst;
  sgl_vertex *bufa[64 + 16], *bufb[64 + 16];
  sgl_vertex **tmp_a = n + 16 <= 80 ? bufa : (sgl_vertex **)malloc((size_t)(n + 16) * sizeof(void *));
  sgl_vertex **tmp_b = n + 16 <= 80 ? bufb : (sgl_vertex **)malloc((size_t)(n + 16) * sizeof(void *));
  unsigned bit;
  int count = n, i;
  if (!tmp_a || !tmp_b) {
    if (tmp_a != bufa) free(tmp_a);
    if (tmp_b != bufb) free(tmp_b);
    return 0;
  }
  dst = tmp_a;
  for (bit = 1; bit <= (CLIP_USER0 << (SGL_MAX_CLIP_PLANES - 1)) && count >= 3; bit <<= 1) {
    int k = 0;
    if (!(mask & bit)) continue;
    for (i = 0; i < count; i++) {
      sgl_vertex *a = src[i], *b = src[(i + 1) % count];
      GLfloat da = plane_dist(c, a, bit), db = plane_dist(c, b, bit);
      if (da >= 0.0f) dst[k++] = a;
      if ((da >= 0.0f) != (db >= 0.0f)) {
        sgl_vertex *nv = new_clip_vertex(c);
        if (!nv) continue;
        lerp_vertex(c, a, b, da / (da - db), nv);
        /* A new edge along the clip plane is drawn in line mode only when
         * the edge it cuts is drawn. */
        nv->edge = da >= 0.0f ? a->edge : GL_TRUE;
        dst[k++] = nv;
      }
    }
    count = k;
    src = dst;
    dst = dst == tmp_a ? tmp_b : tmp_a;
  }
  for (i = 0; i < count; i++) out[i] = src[i];
  if (tmp_a != bufa) free(tmp_a);
  if (tmp_b != bufb) free(tmp_b);
  return count < 3 ? 0 : count;
}

/* Draws the outline of a polygon (GL_LINE mode), side gives the color. */
static void outline(sgl_context *c, sgl_vertex **v, int n, int side, const GLfloat *flat) {
  int i;
  int offset = c->a.enabled[SGL_POLYGON_OFFSET_LINE];
  for (i = 0; i < n; i++)
    if (v[i]->edge) sgl_raster_line(c, v[i], v[(i + 1) % n], side, flat, offset ? 1.0f : 0.0f);
}

/* Draws a polygon of n vertices (in the order of the primitive). The
 * vertex provoking gives the color of flat shading. See sgl.h. */
void sgl_draw_polygon(sgl_context *c, sgl_vertex **v, int n, int provoking) {
  sgl_vertex *small[80];
  sgl_vertex **p = v;
  unsigned all_and = ~0u, all_or = 0;
  GLfloat area = 0.0f;
  int i, side, front, mode;
  const GLfloat *flat = NULL;
  sgl_vertex *prov = v[provoking];
  for (i = 0; i < n; i++) {
    all_and &= v[i]->clipmask;
    all_or |= v[i]->clipmask;
  }
  if (all_and) return; /* all outside one plane */
  c->nclip = 0;
  if (all_or) {
    if (!reserve_clip(c, n + 32)) return;
    p = n + 16 <= 80 ? small : (sgl_vertex **)malloc((size_t)(n + 16) * sizeof(void *));
    if (!p) return;
    n = clip_polygon(c, v, n, all_or, p);
    if (n < 3) {
      if (p != small) free(p);
      return;
    }
  }
  for (i = 0; i < n; i++) sgl_compute_window(c, p[i]);
  for (i = 0; i < n; i++) {
    const sgl_vertex *a = p[i], *b = p[(i + 1) % n];
    area += a->win[0] * b->win[1] - b->win[0] * a->win[1];
  }
  front = (area > 0.0f) == (c->a.front_face == GL_CCW);
  if (area == 0.0f) goto done;
  if (c->a.enabled[SGL_CULL_FACE]) {
    if (c->a.cull_face == GL_FRONT_AND_BACK) goto done;
    if (front && c->a.cull_face == GL_FRONT) goto done;
    if (!front && c->a.cull_face == GL_BACK) goto done;
  }
  side = (!front && c->lit && c->a.two_side) ? 1 : 0;
  if (c->a.shade_model == GL_FLAT) flat = prov->color[side];
  mode = c->a.polygon_mode[front ? 0 : 1];
  if (mode == GL_FILL) {
    GLfloat off = c->a.enabled[SGL_POLYGON_OFFSET_FILL] ? 1.0f : 0.0f;
    for (i = 1; i + 1 < n; i++) sgl_raster_triangle(c, p[0], p[i], p[i + 1], side, flat, off);
  } else if (mode == GL_LINE) {
    outline(c, p, n, side, flat);
  } else {
    for (i = 0; i < n; i++)
      if (p[i]->edge) sgl_raster_point(c, p[i], side, c->a.enabled[SGL_POLYGON_OFFSET_POINT] ? 1.0f : 0.0f);
  }
done:
  if (p != v && p != small) free(p);
}

/* Draws a line from a to b; the vertex provoking (0 for a, 1 for b)
 * gives the color of flat shading. */
void sgl_draw_line(sgl_context *c, sgl_vertex *a, sgl_vertex *b, int provoking) {
  sgl_vertex ca, cb;
  const GLfloat *flat = NULL;
  GLfloat t0 = 0.0f, t1 = 1.0f;
  unsigned m = a->clipmask | b->clipmask, bit;
  if (a->clipmask & b->clipmask) return;
  if (c->a.shade_model == GL_FLAT) flat = (provoking ? b : a)->color[0];
  if (m) {
    for (bit = 1; bit <= (CLIP_USER0 << (SGL_MAX_CLIP_PLANES - 1)); bit <<= 1) {
      GLfloat da, db, t;
      if (!(m & bit)) continue;
      da = plane_dist(c, a, bit);
      db = plane_dist(c, b, bit);
      if (da < 0.0f && db < 0.0f) return;
      if (da < 0.0f) {
        t = da / (da - db);
        if (t > t0) t0 = t;
      } else if (db < 0.0f) {
        t = da / (da - db);
        if (t < t1) t1 = t;
      }
      if (t0 > t1) return;
    }
    lerp_vertex(c, a, b, t0, &ca);
    lerp_vertex(c, a, b, t1, &cb);
    a = &ca;
    b = &cb;
  }
  sgl_compute_window(c, a);
  sgl_compute_window(c, b);
  sgl_raster_line(c, a, b, 0, flat, 0.0f);
}

/* Draws a point. */
void sgl_draw_point(sgl_context *c, sgl_vertex *a) {
  if (a->clipmask) return;
  sgl_compute_window(c, a);
  sgl_raster_point(c, a, 0, 0.0f);
}

/* glBegin: starts a primitive. */
void APIENTRY sgl_Begin(GLenum mode) {
  sgl_context *c = sgl_ctx;
  int i;
  if (c->prim != GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (mode > GL_POLYGON) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  sgl_update_matrices(c);
  c->lit = c->a.enabled[SGL_LIGHTING];
  c->clip_user = 0;
  for (i = 0; i < SGL_MAX_CLIP_PLANES; i++)
    if (c->a.enabled[SGL_CLIP_PLANE0 + i]) c->clip_user |= 1u << i;
  c->need_normal = 0;
  c->need_eye = c->lit || c->a.enabled[SGL_FOG] || c->clip_user;
  if (c->a.enabled[SGL_TEXTURE_2D] || c->a.enabled[SGL_TEXTURE_1D]) {
    for (i = 0; i < 4; i++) {
      if (!c->a.enabled[SGL_TEXTURE_GEN_S + i]) continue;
      if (c->a.gen_mode[i] != GL_OBJECT_LINEAR) c->need_eye = 1;
      if (c->a.gen_mode[i] == GL_SPHERE_MAP) c->need_normal = 1;
    }
  }
  if (c->lit || c->need_normal) sgl_update_inverse(c);
  if (c->lit) {
    /* The lights that are on can change between primitives. */
    c->light_dirty = 1;
  }
  c->prim = mode;
  c->nverts = 0;
}

/* Draws the vertices of the primitive that glEnd ends. */
static void assemble(sgl_context *c) {
  sgl_vertex *v = c->verts;
  sgl_vertex *p[4];
  int n = c->nverts, i;
  switch (c->prim) {
    case GL_POINTS:
      for (i = 0; i < n; i++) sgl_draw_point(c, &v[i]);
      break;
    case GL_LINES:
      for (i = 0; i + 1 < n; i += 2) sgl_draw_line(c, &v[i], &v[i + 1], 1);
      break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
      for (i = 0; i + 1 < n; i++) sgl_draw_line(c, &v[i], &v[i + 1], 1);
      if (c->prim == GL_LINE_LOOP && n > 2) sgl_draw_line(c, &v[n - 1], &v[0], 1);
      break;
    case GL_TRIANGLES:
      for (i = 0; i + 2 < n; i += 3) {
        p[0] = &v[i];
        p[1] = &v[i + 1];
        p[2] = &v[i + 2];
        sgl_draw_polygon(c, p, 3, 2);
      }
      break;
    case GL_TRIANGLE_STRIP:
      for (i = 0; i + 2 < n; i++) {
        if (i & 1) {
          p[0] = &v[i + 1];
          p[1] = &v[i];
          p[2] = &v[i + 2];
        } else {
          p[0] = &v[i];
          p[1] = &v[i + 1];
          p[2] = &v[i + 2];
        }
        sgl_draw_polygon(c, p, 3, 2);
      }
      break;
    case GL_TRIANGLE_FAN:
      for (i = 1; i + 1 < n; i++) {
        p[0] = &v[0];
        p[1] = &v[i];
        p[2] = &v[i + 1];
        sgl_draw_polygon(c, p, 3, 2);
      }
      break;
    case GL_QUADS:
      for (i = 0; i + 3 < n; i += 4) {
        p[0] = &v[i];
        p[1] = &v[i + 1];
        p[2] = &v[i + 2];
        p[3] = &v[i + 3];
        sgl_draw_polygon(c, p, 4, 3);
      }
      break;
    case GL_QUAD_STRIP:
      for (i = 0; i + 3 < n; i += 2) {
        p[0] = &v[i];
        p[1] = &v[i + 1];
        p[2] = &v[i + 3];
        p[3] = &v[i + 2];
        sgl_draw_polygon(c, p, 4, 2);
      }
      break;
    case GL_POLYGON:
      if (n >= 3) {
        sgl_vertex *small[64];
        sgl_vertex **pp = n <= 64 ? small : (sgl_vertex **)malloc((size_t)n * sizeof(void *));
        if (!pp) break;
        for (i = 0; i < n; i++) pp[i] = &v[i];
        sgl_draw_polygon(c, pp, n, 0);
        if (pp != small) free(pp);
      }
      break;
    default:
      break;
  }
}

/* glEnd: draws the primitive that glBegin started. */
void APIENTRY sgl_End(void) {
  sgl_context *c = sgl_ctx;
  if (c->prim == GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  if (c->render_mode == GL_RENDER && c->color) assemble(c);
  c->prim = GL_NONE;
  c->nverts = 0;
}

/* glVertex4f: adds a vertex to the primitive. */
void APIENTRY sgl_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
  sgl_context *c = sgl_ctx;
  GLfloat obj[4];
  if (c->prim == GL_NONE) return;
  if (c->nverts >= c->maxverts) {
    int n = c->maxverts ? c->maxverts * 2 : 64;
    sgl_vertex *p = (sgl_vertex *)realloc(c->verts, (size_t)n * sizeof(sgl_vertex));
    if (!p) {
      sgl_error(GL_OUT_OF_MEMORY);
      return;
    }
    c->verts = p;
    c->maxverts = n;
  }
  obj[0] = x;
  obj[1] = y;
  obj[2] = z;
  obj[3] = w;
  sgl_transform_vertex(c, obj, &c->verts[c->nverts++]);
}

/* Applies the current color to the material when GL_COLOR_MATERIAL is on. */
static void color_material(sgl_context *c) {
  int side;
  for (side = 0; side < 2; side++) {
    sgl_material *m = &c->a.material[side];
    if (side == 0 && c->a.color_material_face == GL_BACK) continue;
    if (side == 1 && c->a.color_material_face == GL_FRONT) continue;
    switch (c->a.color_material_mode) {
      case GL_AMBIENT: memcpy(m->ambient, c->a.color, sizeof m->ambient); break;
      case GL_DIFFUSE: memcpy(m->diffuse, c->a.color, sizeof m->diffuse); break;
      case GL_SPECULAR: memcpy(m->specular, c->a.color, sizeof m->specular); break;
      case GL_EMISSION: memcpy(m->emission, c->a.color, sizeof m->emission); break;
      default:
        memcpy(m->ambient, c->a.color, sizeof m->ambient);
        memcpy(m->diffuse, c->a.color, sizeof m->diffuse);
        break;
    }
  }
  c->light_dirty = 1;
}

/* glColor4f: sets the current color. */
void APIENTRY sgl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  sgl_context *c = sgl_ctx;
  c->a.color[0] = r;
  c->a.color[1] = g;
  c->a.color[2] = b;
  c->a.color[3] = a;
  if (c->a.enabled[SGL_COLOR_MATERIAL]) color_material(c);
}

/* glNormal3f: sets the current normal. */
void APIENTRY sgl_Normal3f(GLfloat x, GLfloat y, GLfloat z) {
  GLfloat *n = sgl_ctx->a.normal;
  n[0] = x;
  n[1] = y;
  n[2] = z;
}

/* glTexCoord4f: sets the current texture coordinates. */
void APIENTRY sgl_TexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
  GLfloat *v = sgl_ctx->a.tex;
  v[0] = s;
  v[1] = t;
  v[2] = r;
  v[3] = q;
}

/* glEdgeFlag: sets the current edge flag. */
void APIENTRY sgl_EdgeFlag(GLboolean flag) { sgl_ctx->a.edge = flag; }

/* glRectf: draws a rectangle in the plane z = 0. */
void APIENTRY sgl_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
  sgl_Begin(GL_POLYGON);
  sgl_Vertex4f(x1, y1, 0.0f, 1.0f);
  sgl_Vertex4f(x2, y1, 0.0f, 1.0f);
  sgl_Vertex4f(x2, y2, 0.0f, 1.0f);
  sgl_Vertex4f(x1, y2, 0.0f, 1.0f);
  sgl_End();
}

/* glRasterPos4f: sets the raster position, its color and its texture
 * coordinates. */
void APIENTRY sgl_RasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
  sgl_context *c = sgl_ctx;
  sgl_vertex v;
  GLfloat obj[4];
  int i;
  if (c->prim != GL_NONE) {
    sgl_error(GL_INVALID_OPERATION);
    return;
  }
  sgl_Begin(GL_POINTS);
  c->prim = GL_NONE;
  obj[0] = x;
  obj[1] = y;
  obj[2] = z;
  obj[3] = w;
  c->need_eye = 1;
  sgl_transform_vertex(c, obj, &v);
  if (v.clipmask & 0x3F) {
    c->a.raster_valid = GL_FALSE;
    return;
  }
  sgl_compute_window(c, &v);
  c->a.raster_valid = GL_TRUE;
  for (i = 0; i < 3; i++) c->a.raster_pos[i] = v.win[i];
  c->a.raster_pos[3] = v.clip[3];
  memcpy(c->a.raster_color, v.color[0], sizeof c->a.raster_color);
  memcpy(c->a.raster_tex, c->a.tex, sizeof c->a.raster_tex);
  c->a.raster_distance = sqrtf(v.eye[0] * v.eye[0] + v.eye[1] * v.eye[1] + v.eye[2] * v.eye[2]);
}

/* Gives the sides (bit 0 front, bit 1 back) of a face of glMaterial,
 * or 0 for an invalid face. */
static int faces(GLenum face) {
  switch (face) {
    case GL_FRONT: return 1;
    case GL_BACK: return 2;
    case GL_FRONT_AND_BACK: return 3;
    default: return 0;
  }
}

/* glMaterialfv: sets a parameter of the material of one or both sides. */
void APIENTRY sgl_Materialfv(GLenum face, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  int sides = faces(face), side;
  if (!sides) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  for (side = 0; side < 2; side++) {
    sgl_material *m = &c->a.material[side];
    if (!(sides & (1 << side))) continue;
    switch (pname) {
      case GL_AMBIENT: memcpy(m->ambient, params, sizeof m->ambient); break;
      case GL_DIFFUSE: memcpy(m->diffuse, params, sizeof m->diffuse); break;
      case GL_SPECULAR: memcpy(m->specular, params, sizeof m->specular); break;
      case GL_EMISSION: memcpy(m->emission, params, sizeof m->emission); break;
      case GL_SHININESS:
        m->shininess = params[0] < 0.0f ? 0.0f : (params[0] > 128.0f ? 128.0f : params[0]);
        sgl_material_changed(m);
        break;
      case GL_AMBIENT_AND_DIFFUSE:
        memcpy(m->ambient, params, sizeof m->ambient);
        memcpy(m->diffuse, params, sizeof m->diffuse);
        break;
      case GL_COLOR_INDEXES: break;
      default: sgl_error(GL_INVALID_ENUM); return;
    }
  }
  c->light_dirty = 1;
}

/* glGetMaterialfv: gives a parameter of the material of one side. */
void APIENTRY sgl_GetMaterialfv(GLenum face, GLenum pname, GLfloat *params) {
  const sgl_material *m = &sgl_ctx->a.material[face == GL_BACK ? 1 : 0];
  switch (pname) {
    case GL_AMBIENT: memcpy(params, m->ambient, sizeof m->ambient); break;
    case GL_DIFFUSE: memcpy(params, m->diffuse, sizeof m->diffuse); break;
    case GL_SPECULAR: memcpy(params, m->specular, sizeof m->specular); break;
    case GL_EMISSION: memcpy(params, m->emission, sizeof m->emission); break;
    case GL_SHININESS: params[0] = m->shininess; break;
    case GL_COLOR_INDEXES: params[0] = 0.0f; params[1] = params[2] = 1.0f; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetMaterialiv: gives a parameter of the material of one side as
 * integers. */
void APIENTRY sgl_GetMaterialiv(GLenum face, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = sgl_pname_count(pname);
  sgl_GetMaterialfv(face, pname, f);
  for (i = 0; i < n; i++)
    params[i] = sgl_pname_is_color(pname) ? (GLint)(f[i] * 2147483647.0f) : (GLint)f[i];
}

/* glLightfv: sets a parameter of a light. Positions and directions are
 * changed to eye coordinates by the current modelview matrix. */
void APIENTRY sgl_Lightfv(GLenum light, GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  sgl_light *l;
  if (light < GL_LIGHT0 || light >= GL_LIGHT0 + SGL_MAX_LIGHTS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  l = &c->a.light[light - GL_LIGHT0];
  switch (pname) {
    case GL_AMBIENT: memcpy(l->ambient, params, sizeof l->ambient); break;
    case GL_DIFFUSE: memcpy(l->diffuse, params, sizeof l->diffuse); break;
    case GL_SPECULAR: memcpy(l->specular, params, sizeof l->specular); break;
    case GL_POSITION:
      sgl_matrix_apply(c->modelview.top, params, l->position);
      break;
    case GL_SPOT_DIRECTION: {
      const GLfloat *m = c->modelview.top->m;
      GLfloat d[3];
      d[0] = m[0] * params[0] + m[4] * params[1] + m[8] * params[2];
      d[1] = m[1] * params[0] + m[5] * params[1] + m[9] * params[2];
      d[2] = m[2] * params[0] + m[6] * params[1] + m[10] * params[2];
      {
        GLfloat len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0.0f) {
          d[0] /= len;
          d[1] /= len;
          d[2] /= len;
        }
      }
      memcpy(l->spot_dir, d, sizeof d);
      break;
    }
    case GL_SPOT_EXPONENT: l->spot_exponent = params[0]; break;
    case GL_SPOT_CUTOFF: l->spot_cutoff = params[0]; break;
    case GL_CONSTANT_ATTENUATION: l->attenuation[0] = params[0]; break;
    case GL_LINEAR_ATTENUATION: l->attenuation[1] = params[0]; break;
    case GL_QUADRATIC_ATTENUATION: l->attenuation[2] = params[0]; break;
    default: sgl_error(GL_INVALID_ENUM); return;
  }
  sgl_light_changed(l);
  c->light_dirty = 1;
}

/* glGetLightfv: gives a parameter of a light. */
void APIENTRY sgl_GetLightfv(GLenum light, GLenum pname, GLfloat *params) {
  const sgl_light *l;
  if (light < GL_LIGHT0 || light >= GL_LIGHT0 + SGL_MAX_LIGHTS) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  l = &sgl_ctx->a.light[light - GL_LIGHT0];
  switch (pname) {
    case GL_AMBIENT: memcpy(params, l->ambient, sizeof l->ambient); break;
    case GL_DIFFUSE: memcpy(params, l->diffuse, sizeof l->diffuse); break;
    case GL_SPECULAR: memcpy(params, l->specular, sizeof l->specular); break;
    case GL_POSITION: memcpy(params, l->position, sizeof l->position); break;
    case GL_SPOT_DIRECTION: memcpy(params, l->spot_dir, sizeof l->spot_dir); break;
    case GL_SPOT_EXPONENT: params[0] = l->spot_exponent; break;
    case GL_SPOT_CUTOFF: params[0] = l->spot_cutoff; break;
    case GL_CONSTANT_ATTENUATION: params[0] = l->attenuation[0]; break;
    case GL_LINEAR_ATTENUATION: params[0] = l->attenuation[1]; break;
    case GL_QUADRATIC_ATTENUATION: params[0] = l->attenuation[2]; break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}

/* glGetLightiv: gives a parameter of a light as integers. */
void APIENTRY sgl_GetLightiv(GLenum light, GLenum pname, GLint *params) {
  GLfloat f[4];
  int i, n = sgl_pname_count(pname);
  sgl_GetLightfv(light, pname, f);
  for (i = 0; i < n; i++)
    params[i] = sgl_pname_is_color(pname) ? (GLint)(f[i] * 2147483647.0f) : (GLint)f[i];
}

/* glLightModelfv: sets a parameter of the light model. */
void APIENTRY sgl_LightModelfv(GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT: memcpy(c->a.model_ambient, params, sizeof c->a.model_ambient); break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: c->a.local_viewer = params[0] != 0.0f; break;
    case GL_LIGHT_MODEL_TWO_SIDE: c->a.two_side = params[0] != 0.0f; break;
    default: sgl_error(GL_INVALID_ENUM); return;
  }
  c->light_dirty = 1;
}

/* glColorMaterial: sets the material parameter that follows the color. */
void APIENTRY sgl_ColorMaterial(GLenum face, GLenum mode) {
  sgl_context *c = sgl_ctx;
  if (!faces(face)) {
    sgl_error(GL_INVALID_ENUM);
    return;
  }
  c->a.color_material_face = face;
  c->a.color_material_mode = mode;
  if (c->a.enabled[SGL_COLOR_MATERIAL]) color_material(c);
}

/* glFogfv: sets a parameter of the fog. */
void APIENTRY sgl_Fogfv(GLenum pname, const GLfloat *params) {
  sgl_context *c = sgl_ctx;
  switch (pname) {
    case GL_FOG_MODE: c->a.fog_mode = (GLenum)params[0]; break;
    case GL_FOG_DENSITY: c->a.fog_density = params[0]; break;
    case GL_FOG_START: c->a.fog_start = params[0]; break;
    case GL_FOG_END: c->a.fog_end = params[0]; break;
    case GL_FOG_COLOR: {
      int i;
      for (i = 0; i < 4; i++) c->a.fog_color[i] = sgl_clamp01(params[i]);
      break;
    }
    case GL_FOG_INDEX: break;
    default: sgl_error(GL_INVALID_ENUM); break;
  }
}
