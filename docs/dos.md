# 32-bit DOS

The library builds for 32-bit DOS with clang-retro's `i386-pc-msdos`
target and the `i386-msdos` sysroot of clang-retro/dos-sdk. A program is
one flat LE executable. The DOS/32A extender is its stub, so the program
needs no other file.

## Build the library

```sh
make static TARGET=i386-pc-msdos BIN=path/to/clang-dos/bin
make check TARGET=i386-pc-msdos BIN=path/to/clang-dos/bin
```

The `i486-pc-msdos`, `i586-pc-msdos` and `i686-pc-msdos` targets also
work. The libraries are in `build/i386-pc-msdos`: `libmfc.a`,
`libmfcu.a`, `libwinport.a`, `libscrnsave.a` and `libglaux.a`.

## Build a program

```sh
clang++ --target=i386-pc-msdos -O2 -D_GNU_SOURCE \
  -isystem port/compat/dos -Iinclude -Iport/include -Iport/softgl/include \
  app.cpp build/i386-pc-msdos/libmfc.a build/i386-pc-msdos/libwinport.a \
  -o app.exe
```

Use `-isystem port/compat/dos`. It has the POSIX headers that the DOS C
library does not have.

## What the port does

The port uses the portable layer (`PORTABLE=1`), as on Linux. These parts
are special to DOS:

- **The screen.** `port/src/backend_dos.cpp` shows the windows on a VESA
  2.0 screen with a linear frame buffer. It selects the mode with the
  most colors (15, 16, 24 or 32 bits) for the size in `WINPORT_SCREEN`.
  The default size is 640x480. Without VESA 2.0, it uses VGA mode 13h
  (320x200, 256 colors). At exit, it sets the text mode again.
- **The mouse and the keyboard.** The mouse uses the mouse driver
  (INT 33h). The keyboard uses the BIOS (INT 16h). DOS gives no key-up
  event, so a key-up comes immediately after each key-down.
- **Files.** `open` and `fopen` use the binary mode by default, as on
  Unix. Temporary files go to `TMP` or `TEMP`, else to the current folder.
- **Fonts.** The port looks for TrueType fonts in the `FONTS` folder
  next to the program, then in `%WINDIR%\FONTS` and `C:\WINDOWS\FONTS`.
  Without a font, it uses the bitmap font that is built in. That font has
  no outlines, so `wglUseFontOutlines` and `GetGlyphOutline` fail.
  `WINPORT_FONTS` replaces the list. Use `;` between the folders.
- **Threads.** The threads are cooperative (see the dos-sdk
  documentation). The message loop gives time to the other threads.
- **OpenGL.** The generic implementation (`port/softgl`) draws. There is
  no hardware acceleration yet.

`port/src/compat_dos.cpp` has the POSIX functions that DOS does not have.
`pread`, `pwrite`, `mmap` over the heap, shared memory over files in the
temporary folder, `poll` on files, `uname` and `ftime` work. Sockets, the
dynamic loader and `posix_spawn` fail with the usual error.

## The settings

| Variable | Use |
| --- | --- |
| `WINPORT_SCREEN` | The screen size, `WxH` or `WxHxBPP`. Example: `800x600` or `1024x768x16`. |
| `WINPORT_FONTS` | The font folders, divided by `;`. `builtin` uses only the built-in font. |
| `TMP`, `TEMP` | The folder for temporary files. |

## Limits

- An FPU is necessary.
- 16 MB of memory is the minimum for an OpenGL program. 8 MB is not
  sufficient for the screen savers.
- There is no network, no clipboard of other programs, no printer and no
  sound.
- A screen saver with no arguments starts as with `/s`.
- The tests ran in DOSBox only. A test on a real machine is necessary.

## Tests

`make check TARGET=i386-pc-msdos` runs `tests/run-dos-tests.sh`. The
script builds `core`, `memtest`, `sstest` and `gltest` and runs each in
DOSBox. A test passes when its output has its `PASS` line. Without
DOSBox, the script only builds the tests. `DOSBOX` names DOSBox, and
`DOS_TESTS` selects the tests.
