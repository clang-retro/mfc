# The classic Mac OS

The library builds for the classic Mac OS (System 7 to Mac OS 9) on 68k
and PowerPC Macs. It uses the portable layer in `port/` and the C and
C++ libraries of [clang-retro/mac-sdk](https://github.com/clang-retro/mac-sdk).

## Status

| Part | Status |
| --- | --- |
| Core classes (`CObject`, `CString`, the collections, `CFile`, `CArchive`, `CTime`, the exceptions) | Builds and passes `tests/core.cpp` |
| Memory diagnostics | Passes `tests/memtest.cpp` |
| Threads (`CWinThread`, the synchronization objects) | Builds, over mac-sdk's cooperative threads |
| Window classes | Build. The Toolbox backend shows the top-level windows (see [Windows](#windows)). |
| Sockets (`CAsyncSocket`, `CSocket`) and WinInet (`CInternetSession`) | Build, over the sockets of mac-sdk's C library (MacTCP). `tests/socktest.cpp` passes on a 68020. `tests/inettest.cpp` passes its HTTP and FTP checks, but it does not finish in the simulator's step limit. HTTPS is not available (no OpenSSL). |
| OpenGL (`wgl`, `GL/gl.h`, `GL/glu.h`) | PowerPC only, over Apple OpenGL and AGL. Passes `tests/gltest.cpp` with the simulator's fake OpenGL. See [OpenGL](#opengl). |
| Screen savers (`scrnsave.h`) | Pass `tests/sstest.cpp`. See [screensavers.md](screensavers.md). |
| COM between programs | Not available. COM in one program works. |

## Windows

The Toolbox backend shows each top-level window in a Toolbox window
with no frame (`plainDBox`). The portable layer draws the whole window,
also the caption and the frame, into 32-bit pixels. The backend copies
the changed pixels to the screen with `CopyBits`:

- With 32-bit Color QuickDraw, it copies them from a direct PixMap.
- Without it (a Mac Plus, a Mac SE or a Classic), it dithers them to
  black and white.

The code is in two files, because the Win32 headers and the Toolbox
headers use the same names (`ShowWindow`, `Rect`, `Point` and more):

| File | Contents |
| --- | --- |
| `port/src/backend_macos.cpp` | The backend, with the Win32 headers. It gives the input to the portable layer. |
| `port/src/macos_toolbox.cpp` | The Toolbox calls: windows, `CopyBits` and the event loop. |
| `port/src/macos_toolbox.h` | The interface between the two files. |

The backend gives the input to the program as Windows does:

- The keyboard. Command works as Control, so Command+C copies. Option
  works as Alt. Typed characters change from Mac Roman to Unicode.
- The mouse. A click with Control held is a right-button click.
- The Finder's Quit Application command asks the active window to close.

The event loop calls `WaitNextEvent` with a sleep of one tick, and then
lets the other threads run.

The backend starts the Toolbox (`InitGraf` to `InitCursor`) before it
shows the first window. A program that starts the Toolbox itself sets
`winport_mac_toolbox_started` (an `extern "C" int`) to 1 first. The
backend cannot find this from low memory: the Finder sets `MBarHeight`
and `CurrentA5` before the program runs.

When the backend starts the Toolbox, it also puts two menus in the Mac
menu bar. The Apple menu holds the desk accessories (the Apple Menu
Items). The File menu holds Quit, which does the same as the Finder's
Quit Application command. The program's own menus stay in its windows.
A program that starts the Toolbox itself makes its own menu bar.

To keep the windows in memory (for tests), set `WINPORT_BACKEND` to
`headless`. The backend also uses the headless backend when the
Toolbox cannot start.

## Requirements

- clang from clang-retro/llvm. A clang that does not use emulated TLS
  for the classic Mac OS can compile only for 68k, with
  `-femulated-tls` (the Makefile gives it).
- A mac-sdk checkout with its libraries built: the runtime library, the
  C library and the C++ library (`runtime/build.sh`,
  `runtime/build-libc.sh` and `runtime/build-libstdcxx.sh`). The
  libraries must be in `lib/` of the checkout.

## Build the libraries

```sh
make TARGET=m68k-apple-macos BIN=path/to/llvm/bin MACSDK=path/to/mac-sdk static
make TARGET=powerpc-apple-macos9 BIN=path/to/llvm/bin MACSDK=path/to/mac-sdk static
```

`MACSDK` is `../mac-sdk` if you do not give it. The output goes to
`build/<triple>`: `libmfc.a` (ANSI), `libmfcu.a` (Unicode) and
`libwinport.a` (the portable layer), and `libscrnsave.a` and
`libscrnsavw.a` (the screen saver library). There are no shared
libraries.

The Makefile does these steps for the classic Mac OS:

1. It compiles with `-Os`, `-ffunction-sections` and `-fdata-sections`,
   so that the linker can remove the code that the program does not
   use. On 68k, it uses 32-bit addresses (`-mcmodel=large`).
2. It uses the libstdc++ headers of mac-sdk, then `port/compat/macos`,
   then the C library headers.
3. It leaves out the files for services, COM between programs, and the
   X11, Wayland and Cocoa backends.

`port/compat/macos` has the declarations that picolibc's headers do not
have (`shm_open`, `cfmakeraw`, `environ`, some `ioctl` requests), and an
empty `link.h`. mac-sdk's C library has the functions (see its
`docs/posix.md`).

## Build a program

Use `tools/macapp.py` of mac-sdk. Give the flags after `--` in their
joined form (`-isystemDIR`), because `macapp.py` takes each word that
does not start with `-` as a source file:

```sh
CLANG=path/to/llvm/bin/clang python3 path/to/mac-sdk/tools/macapp.py \
  --cpu 68k --size 8192 -o App.bin -- -Os -D_GNU_SOURCE \
  -fno-delete-null-pointer-checks -isystem$MFC/port/compat/macos \
  -I$MFC/include -I$MFC/port/include app.cpp \
  $MFC/build/m68k-apple-macos/libmfc.a $MFC/build/m68k-apple-macos/libwinport.a
```

`--size 8192` gives the application a partition of 8 MB. The test
programs have about 2 MB of code.

## Tests

`make check TARGET=m68k-apple-macos MACSDK=...` (or the PowerPC target)
runs `tests/run-macos-tests.sh`. The script builds `tests/core.cpp`,
`tests/memtest.cpp` and `tests/sstest.cpp`, and `tests/gltest.cpp` on
PowerPC (set `MACOS_TESTS` for others, for example `wintest`) into applications and runs
them in mac-sdk's simulators: `tools/sim68k.py` as a 68000 and a 68020,
or `tools/simppc.py`. The simulators need the Python module `unicorn`.
Without it, the script only builds the tests.

The simulators have a 640 by 480 screen. They make the windows, check
each `CopyBits` call, and give null events. A 68000 has no Color
QuickDraw, so `wintest` tests the black and white path there and the
color path on a 68020 and on PowerPC.

## OpenGL

On PowerPC, the `wgl` functions use Apple OpenGL through AGL
(`port/src/opengl_agl.cpp`), in the same way that they use CGL on
Mac OS X. Each rendering context draws off screen, into a buffer the
size of the window. `SwapBuffers`, and `glFlush` or `glFinish` for a
single-buffered format, copy the buffer into the window with GDI.

- The headers and the imports are those of mac-sdk (`GL/gl.h`,
  `GL/glu.h`, `AGL/agl.h`). OpenGL 1.2 is available, with the
  extensions that Apple OpenGL exports.
- `wglGetProcAddress` finds a function of `OpenGLLibrary` by its name
  with the Code Fragment Manager (`FindSymbol`).
- Apple OpenGL is a weak import. On a Mac without it, the program
  starts and `wglCreateContext` fails with `ERROR_NOT_SUPPORTED`.
- There is no OpenGL on 68k. The `wgl` functions fail there, and a
  program that calls `gl` functions does not link.
- The test runs in the simulator over a fake OpenGL that clears the
  buffer but does not draw. On a real Mac, the program draws with
  Apple's software renderer or the graphics card.

## Limits

- The threads are cooperative. A thread runs until it waits, sleeps,
  locks a locked mutex or yields.
- A `thread_local` access is a function call (emulated TLS).
- Memory mappings (`CreateFileMapping`, `VirtualAlloc`) are blocks in
  the application heap. Give the application a sufficiently large
  partition.
- The m68k backend of LLVM cannot assemble a function of more than
  32 KB at `-O2` (no long branches on the 68000). The Makefile uses
  `-Os`.
