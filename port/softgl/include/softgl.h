/* softgl.h - the driver interface of the software OpenGL (softgl).
 *
 * softgl is a generic implementation of OpenGL 1.1, as GDI Generic is on
 * Windows. It has the gl functions of OpenGL 1.1 and the extensions of
 * the Windows generic implementation (GL_WIN_swap_hint, GL_EXT_bgra,
 * GL_EXT_paletted_texture and GL_EXT_vertex_array). It does not know a
 * window system: the wgl layer of the program (port/src/opengl.cpp)
 * makes the contexts, gives each one a buffer in memory and copies the
 * buffer to the window.
 *
 * The functions here are for that layer. A program calls the gl
 * functions and the wgl functions only.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#ifndef SOFTGL_H
#define SOFTGL_H

#ifdef __cplusplus
extern "C" {
#endif

/* A rendering context. */
typedef struct sgl_context sgl_context;

/* Makes a rendering context. It shares its display lists and texture
 * objects with share, which can be NULL. Gives NULL when there is not
 * sufficient memory. */
sgl_context *sgl_create_context(sgl_context *share);

/* Destroys a rendering context. If it is current, no context is current
 * after the call. */
void sgl_destroy_context(sgl_context *ctx);

/* Makes ctx the current context of the thread, or no context when ctx
 * is NULL. Gives 1 on success and 0 on failure. */
int sgl_make_current(sgl_context *ctx);

/* Gives the current context of the thread, or NULL. */
sgl_context *sgl_get_current(void);

/* Points ctx at a color buffer of width x height pixels. Each pixel is a
 * 32-bit value 0xAARRGGBB in the byte order of the machine. The top row
 * is first. rowpixels is the number of pixels from one row to the next.
 * The context makes its depth, stencil and accumulation buffers in the
 * same size. The width, the height and rowpixels must not be more than
 * 32767, and rowpixels must not be less than the width. Gives 1 on
 * success, and 0 when a size is not correct or when there is not
 * sufficient memory. */
int sgl_set_buffer(sgl_context *ctx, int width, int height, void *pixels, long rowpixels);

/* Copies the state groups in mask (the GL_..._BIT values of
 * glPushAttrib) from one context to another. Gives 1 on success. */
int sgl_copy_context(sgl_context *from, sgl_context *to, unsigned long mask);

/* Lets ctx use the display lists and texture objects of from. ctx must
 * not have lists or textures of its own yet. Gives 1 on success. */
int sgl_share_lists(sgl_context *from, sgl_context *ctx);

/* Gives the address of an extension function by its name, as
 * wglGetProcAddress does, or NULL. */
void *sgl_get_proc(const char *name);

#ifdef __cplusplus
}
#endif

#endif
