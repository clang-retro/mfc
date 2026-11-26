# softgl

softgl is a software renderer for OpenGL 1.1. It is the "generic
implementation" under the `wgl` functions on the systems that have no
OpenGL of their own: Linux and the classic Mac OS on 68k. It operates
on a 68000 without a floating-point unit. The OpenGL screen savers of
the Win32 SDK operate on it without changes.

For the design, the build, the tests and the limits, see
[docs/software-opengl.md](../../docs/software-opengl.md).

Part of clang-retro MFC. The renderer is under `AGPL-3.0-only`. GLU
(`glu/`) is under the SGI Free Software License B 2.0; see
[glu/README.md](glu/README.md).
