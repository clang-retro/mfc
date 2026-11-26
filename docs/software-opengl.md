# Software OpenGL (the generic implementation)

Some systems have no OpenGL of their own. On these systems, the `wgl`
functions use a software renderer in `port/softgl`. Windows has a
renderer of the same type: the "generic implementation" of
`opengl32.dll`, which GDI uses when there is no graphics driver for
OpenGL. The renderer in `port/softgl` copies the design of that
implementation, so that OpenGL programs for Windows NT and Windows 95
operate without changes.

## Where the renderer is used

| System | OpenGL under the `wgl` functions |
| --- | --- |
| Linux | The generic implementation (`port/softgl`). |
| Classic Mac OS, 68k | The generic implementation (`port/softgl`). |
| Classic Mac OS, PowerPC | Apple OpenGL through AGL (`port/src/opengl_agl.cpp`). |
| Mac OS X and macOS | The OpenGL framework through CGL (`port/src/opengl.cpp`). |

The Apple renderers have the function of the installable client driver
(ICD) of Windows. The generic implementation is used on all other
portable targets.

To remove the generic implementation, compile with
`-DWINPORT_NO_SOFTGL`. Then `ChoosePixelFormat` fails with
`ERROR_NOT_SUPPORTED`, as before.

## What the renderer does

The renderer gives the functions of OpenGL 1.1, as `opengl32.dll` of
Windows NT 4.0 gives them:

- `glGetString(GL_VERSION)` gives `1.1.0`.
  `glGetString(GL_RENDERER)` gives `clang-retro Generic`.
- These extensions are available. `wglGetProcAddress` gives their
  functions:
  - `GL_WIN_swap_hint` (`glAddSwapHintRectWIN`)
  - `GL_EXT_bgra`
  - `GL_EXT_paletted_texture` (`glColorTableEXT` and the related
    functions)
  - `GL_EXT_vertex_array` (`glVertexPointerEXT` and the related
    functions)
- Vertices: immediate mode, vertex arrays, display lists, evaluators
  (`glMap1`, `glMap2`, `glEvalMesh`) and rectangles.
- Transformation: the three matrix stacks, the viewport, the depth
  range and six user clip planes.
- Lighting: eight lights, two-sided lighting, local viewer, spot
  lights, attenuation and `glColorMaterial`.
- Texture coordinate generation: object linear, eye linear and sphere
  map.
- Rasterization: points, lines (width and stipple), polygons (fill,
  line and point modes, stipple, culling, polygon offset), flat and
  smooth shading.
- Textures: 1D and 2D, mipmaps, nearest and linear filters, repeat and
  clamp, texture objects, the four texture environments and
  perspective correction.
- Per-fragment operations: fog, scissor, alpha test, stencil, depth
  test, blending and color mask.
- Pixels: `glDrawPixels`, `glReadPixels`, `glCopyPixels`, `glBitmap`,
  the pixel store, transfer and zoom, and the pixel maps.
- The accumulation buffer, `glPushAttrib` and `glPushClientAttrib`.

GLU 1.3 is also available: the SGI reference GLU (matrices, mipmaps,
image scale, quadrics and tessellation). Its license is the SGI Free
Software License B 2.0. See `port/softgl/glu/README.md`.

## How the renderer is built

The renderer is in C (C99). The makefile compiles it with the portable
layer and puts the objects in `libwinport.a`:

- `port/softgl/src`: the renderer.
- `port/softgl/glu`: GLU (`libtess` and `libutil`).
- `port/softgl/include/GL`: `gl.h`, `glext.h` and `glu.h`.

A program links only with `libwinport.a`. It does not need other
libraries for OpenGL. Add `-Iport/softgl/include` after
`-Iport/include`, so that `<GL/gl.h>` finds the headers.

`libglaux.a` gives the `aux` functions of the Windows SDK
(`GL/glaux.h`) on these systems too.

### The dispatch table

Each `gl` function calls the entry of the current dispatch table, as
`opengl32.dll` does. There are three tables:

- The table of no context. Its functions do nothing.
- The table that executes.
- The table that compiles a display list.

`glNewList` and `glEndList` change the table.
`port/softgl/tools/gen-dispatch.py` makes `src/dispatch.c` and
`src/dispatch.h` from `include/GL/gl.h`. Do not edit the generated
files. Change the script and run it again:

```sh
python3 port/softgl/tools/gen-dispatch.py
```

The portable layer defines `glFlush` and `glFinish` itself: for a
single-buffered pixel format, they copy the picture to the window. Thus
the makefile compiles the renderer with `-DSGL_LAYER_FLUSH`, which
removes these two functions from `dispatch.c`.

### The interface to the window layer

`port/softgl/include/softgl.h` gives the functions that
`port/src/opengl_soft.cpp` uses:

| Function | Purpose |
| --- | --- |
| `sgl_create_context` | Makes a rendering context. It can share display lists and textures with another context. |
| `sgl_destroy_context` | Destroys a context. |
| `sgl_make_current` | Makes a context the current context of the thread. |
| `sgl_set_buffer` | Gives the color buffer to the context. The context makes its depth, stencil and accumulation buffers in the same size. |
| `sgl_copy_context` | Copies attribute groups (`wglCopyContext`). |
| `sgl_share_lists` | Shares display lists (`wglShareLists`). |
| `sgl_get_proc` | Gives an extension function by its name (`wglGetProcAddress`). |

The color buffer has one 32-bit value for each pixel, `0xAARRGGBB` in
the byte order of the machine. The top row is first. The width and the
height must not be more than 32767 pixels.

To use the renderer on a different system, write a file that has the
same function as `opengl_soft.cpp`: give a buffer to the context, and
copy the buffer to the screen after `SwapBuffers` or `glFlush`.

## Speed on the 68000

The 68000 has no floating-point unit and no 32-bit multiply
instruction. The renderer is designed for these limits:

- Geometry (transformation, lighting and clipping) is in `float`. This
  work is done once for each vertex.
- The rasterizer converts each attribute plane of a triangle to fixed
  point. Each span starts and steps with integer operations.
- Color, blend and texture operations multiply 16-bit values only
  (`SGL_MUL16`). The compiler makes one `muls` or `mulu` instruction
  for each multiply.
- The depth buffer has 16 bits for each pixel on the 68k and on 16-bit
  x86. It has 32 bits on other systems. Set `SGL_DEPTH_BITS` to change
  it.
- Perspective correction divides once for each 16 pixels.
  `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST)` removes the
  division.
- The table of the specular power keeps the four last shininess
  values. A program that changes between some materials does not
  calculate the table again.

Most of the remaining time is in the floating-point functions of the
runtime library (`__mulsf3`, `__addsf3`). A faster runtime library
makes the renderer faster without changes to the renderer.

## Tests

- `port/softgl/tests/render.c` draws scenes into a 64 x 64 buffer and
  examines pixels: flat and smooth shading, depth, a lit sphere in a
  display list, a texture with perspective, vertex arrays, an evaluator
  mesh, blending and the scissor.

  ```sh
  cd port/softgl
  cc -std=c99 -Iinclude -Isrc src/*.c tests/render.c \
    glu/libtess/dict.c glu/libtess/geom.c glu/libtess/memalloc.c \
    glu/libtess/mesh.c glu/libtess/normal.c glu/libtess/priorityq.c \
    glu/libtess/render.c glu/libtess/sweep.c glu/libtess/tess.c \
    glu/libtess/tessmono.c glu/libutil/*.c -Iglu/include -lm -o render
  ./render
  ```

  The output is `PASS softgl render`.
- `tests/gltest.cpp` uses the `wgl` functions. With the generic
  implementation, it also draws a triangle and examines its pixels.
  `make check` runs it on Linux. `make check TARGET=m68k-apple-macos`
  runs it in the 68000 and 68020 simulators of mac-sdk.
- `tools/build-sdk-samples.py` builds the OpenGL screen savers of the
  Win32 SDK for Linux and for the 68k Mac. See
  [sdk-samples.md](sdk-samples.md).

## Limits

- Color index mode is not available. All pixel formats are RGBA.
- Selection and feedback (`GL_SELECT`, `GL_FEEDBACK`) do not record
  hits or vertices. `glRenderMode` gives 0.
- `glLogicOp` has no effect.
- The width and the height of a texture must be powers of two.
- The OpenGL 1.2 functions are not available. `glTexImage3D` is a stub
  for GLU only.
- NURBS of GLU (`gluNewNurbsRenderer`) are not available.
- The pixel format tells 32 depth bits. On the 68k, the depth buffer
  has 16 bits (`glGetIntegerv(GL_DEPTH_BITS)` gives 16).

## Other systems

The renderer has no dependency on the window layer. Its C code
compiles for DOS, 16-bit Windows, AmigaOS, Atari TOS and OS/2 too. A
front end for each of these systems must copy the color buffer to the
screen, as `opengl_soft.cpp` does through GDI.
