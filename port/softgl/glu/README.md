# GLU

This folder holds the OpenGL Utility Library (GLU 1.3) of the SGI
reference implementation, from Mesa GLU 9.0.3. The files are not
changed. The makefile compiles `libtess/*.c` (but not
`libtess/priorityq-heap.c`, which `priorityq.c` includes) and
`libutil/*.c`. The NURBS part (`libnurbs`, C++) is not here.

The files are under the SGI Free Software License B, version 2.0
(`SGI-B-2.0`). The text of the license is at the start of each file.
The Free Software Foundation lists this license as free and compatible
with the GNU GPL. The GNU AGPL permits combination with it, so it can
be linked with clang-retro MFC (`AGPL-3.0-only`).
