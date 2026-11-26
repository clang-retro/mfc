/* gl12.c - the functions of OpenGL 1.2 that GLU calls. The renderer is
 * OpenGL 1.1: they record GL_INVALID_OPERATION.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "sgl.h"

/* glTexImage3D: there are no 3D textures (gluBuild3DMipmaps calls it). */
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                           GLsizei d, GLint border, GLenum format, GLenum type, const void *pixels) {
  (void)target, (void)level, (void)internal, (void)w, (void)h, (void)d, (void)border;
  (void)format, (void)type, (void)pixels;
  if (sgl_ctx) sgl_error(GL_INVALID_OPERATION);
}
