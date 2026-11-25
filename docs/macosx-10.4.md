# Mac OS X 10.4 and 10.5 on PowerPC

The portable layer and the library build for Mac OS X 10.4 (Tiger) and
10.5 (Leopard) on 32-bit PowerPC Macs. The windows show with the Cocoa
backend. OpenGL programs draw through `OpenGL.framework`.

## Requirements

- clang from clang-retro/llvm. This clang uses emulated TLS for
  PowerPC Mac OS X before 10.7, and it lets `std::atomic` call the
  atomic library functions on these versions.
- The cctools of dos-sdk (`scripts/build-cctools-ppc.sh`): `ld64`,
  `ar`, `ranlib` and `libtool` for PowerPC. `ld64` cannot read the symbol
  table that `llvm-ar` writes for PowerPC.
- The compiler-rt builtins of dos-sdk
  (`scripts/build-builtins-macosx.sh`). Set `SDK` when you build them:
  then the library also has emulated TLS (`emutls.c`) and the atomic
  library functions (`atomic.c`).
- libc++ of dos-sdk (`scripts/build-libcxx-macosx.sh`). For 10.4 and
  later, it has threads, locales, wide characters and the file system
  library.
- The Mac OS X 10.4 SDK (`MacOSX10.4u.sdk` or `MacOSX10.4.0.sdk`).

## Build the library

```sh
export PATH=path/to/cctools/bin:$PATH
make TARGET=powerpc-apple-macosx10.4 BIN=path/to/clang/bin \
  SDK=path/to/MacOSX10.4u.sdk LIBCXX=path/to/libcxx-macosx static
```

The archives go into `build/powerpc-apple-macosx10.4`. Use `static`: the
shared library is not available for these versions.

## Build a program

```sh
clang++ --target=powerpc-apple-macosx10.4 -isysroot $SDK -std=c++17 \
  -nostdinc++ -isystem $LIBCXX/include/c++/v1 \
  -isystem path/to/mfc/port/compat/macosx -DWINPORT_MACOSX_COMPAT \
  -Ipath/to/mfc/port/include app.cpp app-res.cpp \
  -fuse-ld=path/to/cctools/bin/powerpc-apple-darwin8-ld \
  -nostdlib++ -L$LIBCXX/lib -lc++ -lc++abi -lunwind \
  path/to/mfc/build/powerpc-apple-macosx10.4/libwinport.a \
  -framework Cocoa -framework OpenGL -o app
```

Compile the resources with `port/tools/res2cpp.py`.

## What the port adds for these versions

| Part | What it does |
| --- | --- |
| `port/compat/macosx` | Declares the C library functions that 10.4 does not have (for example `strnlen`, `wcsdup`, `clock_gettime` and `posix_spawn`), and fixes headers of the 10.4 SDK. |
| `port/src/compat_macosx.cpp` | The code of these functions. |
| `port/src/backend_cocoa.mm` | Uses only the Cocoa API of 10.4 when the SDK is older than 10.5. |
| `port/src/audio_coreaudio.cpp` | Has no sound device when the SDK has no audio queues (before 10.5). The loopback device is available. |
| `port/src/font.cpp` | Reads the fonts of Mac OS X: `.ttf`, `.otf`, `.ttc`, `.dfont` and font suitcases (the `sfnt` resources of the resource fork). |

## OpenGL

`port/src/opengl.cpp` gives the `wgl` functions of Windows over CGL:

- `ChoosePixelFormat` and `DescribePixelFormat` give two formats:
  1. Double-buffered, for a window.
  2. Single-buffered, for a window or a bitmap.
  Both have 32-bit color and a 24-bit depth buffer.
- `wglCreateContext` makes an offscreen CGL context. The context draws
  into memory with the size of the window.
- `SwapBuffers` copies the image into the window with
  `SetDIBitsToDevice`. With a single-buffered format, `glFinish` and
  `glFlush` copy it. `glFlush` copies it at most every 20 ms.
- `wglUseFontBitmaps` and `wglUseFontOutlines` make display lists from
  the glyphs of the font of the device context.
- `wglShareLists` makes the second context again, with the lists of the
  first.

`GL/glaux.h` and `libglaux.a` give the `aux` functions of the Windows SDK
(images and shapes) over GLU and GLUT.

On other systems, the `wgl` functions are stubs that fail.

## Build the samples of the Win32 SDK

Set these variables for `tools/build-sdk-samples.py` (see
[sdk-samples.md](sdk-samples.md)):

```sh
export PATH=path/to/cctools/bin:$PATH
export CC=path/to/clang CXX=path/to/clang++
export CFLAGS="--target=powerpc-apple-macosx10.4 -isysroot $SDK \
  -isystem path/to/mfc/port/compat/macosx -DWINPORT_MACOSX_COMPAT -O2"
export CXXFLAGS="-nostdinc++ -isystem $LIBCXX/include/c++/v1 -std=c++17"
export LDFLAGS="--target=powerpc-apple-macosx10.4 -isysroot $SDK \
  -fuse-ld=path/to/cctools/bin/powerpc-apple-darwin8-ld \
  -nostdlib++ -L$LIBCXX/lib -lc++ -lc++abi -lunwind"
export MFC_OUT=path/to/mfc/build/powerpc-apple-macosx10.4
python3 tools/build-sdk-samples.py --sdk path/to/SDK --out out
```

## Test on Tiger

The programs run on Mac OS X 10.4 in QEMU (`qemu-system-ppc -M mac99`).
