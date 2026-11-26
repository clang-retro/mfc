# Software OpenGL (the generic implementation)

Some systems have no OpenGL of their own. On these systems, the `wgl`
functions use softgl, a software renderer. Windows has a renderer of
the same type: the "generic implementation" of `opengl32.dll`, which
GDI uses when there is no graphics driver for OpenGL.

softgl is a separate project:
[clang-retro/softgl](https://github.com/clang-retro/softgl). Its
documentation tells the design, the functions, the speed on the 68000
and the limits:
[software-opengl.md](https://github.com/clang-retro/softgl/blob/main/docs/software-opengl.md).

## The submodule

MFC has softgl as a git submodule in `port/softgl`. Clone MFC with
the submodule:

```sh
git clone --recurse-submodules https://github.com/clang-retro/mfc.git
```

For a clone that you have already, get the submodule:

```sh
git submodule update --init
```

If `port/softgl` is empty, the makefile stops and tells you to run
this command.

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
`ERROR_NOT_SUPPORTED`.

## How MFC builds the renderer

The makefile compiles the files of `port/softgl/src` and
`port/softgl/glu` (the NURBS of GLU, `glu/libnurbs`, are C++) with the
portable layer and puts the objects in
`libwinport.a`. A program links only with `libwinport.a`. Add
`-Iport/softgl/include` after `-Iport/include`, so that `<GL/gl.h>`
finds the headers. `libglaux.a` gives the `aux` functions of the
Windows SDK (`GL/glaux.h`) on these systems too.

The portable layer defines `glFlush` and `glFinish` itself: for a
single-buffered pixel format, they copy the picture to the window.
Thus the makefile compiles the renderer with `-DSGL_LAYER_FLUSH`,
which removes these two functions from softgl.

`port/src/opengl_soft.cpp` connects the `wgl` functions to the
interface of softgl (`port/softgl/include/softgl.h`). It gives a
color buffer to the context, and copies the buffer to the window after
`SwapBuffers` or `glFlush`.

## Tests

- `tests/gltest.cpp` uses the `wgl` functions. With the generic
  implementation, it also draws a triangle and examines its pixels.
  `make check` runs it on Linux. `make check TARGET=m68k-apple-macos`
  runs it in the 68000 and 68020 simulators of mac-sdk.
- `tools/build-sdk-samples.py` builds the OpenGL screen savers of the
  Win32 SDK for Linux and for the 68k Mac. See
  [sdk-samples.md](sdk-samples.md).
- softgl has its own tests (`make check` in `port/softgl`).
