# Mac OS X 10.0 to 10.5 on PowerPC and Intel

The portable layer and the library build for Mac OS X 10.4 (Tiger) and
10.5 (Leopard) on 32-bit PowerPC Macs and on 32-bit Intel Macs (i386).
A fat program has both and runs on both. The windows show with the Cocoa
backend. OpenGL programs draw through `OpenGL.framework`.

The port also builds for Mac OS X 10.0 to 10.3 on PowerPC. A program
built for 10.0 runs on all versions from 10.0 to 10.5. See
[Mac OS X 10.0 to 10.3](#mac-os-x-100-to-103).

## Requirements

- clang from clang-retro/llvm. This clang uses emulated TLS for 32-bit
  Mac OS X before 10.7 (PowerPC and Intel), and it lets `std::atomic`
  call the atomic library functions on these versions.
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
- The Mac OS X 10.4 SDK. For Intel and for fat programs, use
  `MacOSX10.4u.sdk`: `MacOSX10.4.0.sdk` has only PowerPC.

dos-sdk's `scripts/build-runtime-macosx.sh` builds libc++ and the builtins
for both processors as fat libraries. CI publishes them as the
`macosx10.4` sysroot. `packaging/get-toolchain.sh -s macosx10.4 DIR`
downloads it with `MacOSX10.4u.sdk`.

## Build the library

```sh
export PATH=path/to/cctools/bin:$PATH
make TARGET=powerpc-apple-macosx10.4 BIN=path/to/clang/bin \
  SDK=path/to/MacOSX10.4u.sdk LIBCXX=path/to/libcxx-macosx static
```

The archives go into `build/powerpc-apple-macosx10.4`. Use `static`: the
shared library is not available for these versions. For Intel, use
`TARGET=i386-apple-macosx10.4`.

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

## Make a fat program

Build the program once for each processor, then join the two with
`lipo`:

```sh
powerpc-apple-darwin8-lipo -create app-ppc app-i386 -output app
```

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

For Intel, use `--target=i386-apple-macosx10.4` and the Intel build of
the library. Then join the programs of `out-ppc/bin` and `out-i386/bin`
with `lipo`.

For OpenGL screen savers, the script also makes `.saver` bundles (see
[screensavers.md](screensavers.md)). To make a fat bundle, copy the
PowerPC bundle and join the two programs in `Contents/MacOS`:

```sh
cp -R "out-ppc/bin/3D Pipes.saver" .
powerpc-apple-darwin8-lipo -create "out-ppc/bin/3D Pipes.saver/Contents/MacOS/sspipes" \
  "out-i386/bin/3D Pipes.saver/Contents/MacOS/sspipes" \
  -output "3D Pipes.saver/Contents/MacOS/sspipes"
```

For a bundle that also runs on 10.0 to 10.3, build the PowerPC part for
10.0 (see below).

## Test on Tiger

The programs run on Mac OS X 10.4 in QEMU (`qemu-system-ppc -M mac99`).

In QEMU, the WindowServer of Mac OS X 10.0 does not start, because 10.0
has no display driver for the QEMU display. Command-line programs run.
A program that uses AppKit stops when AppKit cannot connect to the
WindowServer. Thus the windows and the `.saver` bundles of 10.0 are not
tested in QEMU.

## Mac OS X 10.0 to 10.3

Build for 10.0 to make one PowerPC program for 10.0 to 10.5. Intel Macs
have 10.4 or later, so the Intel part of a fat program stays a 10.4
build.

### Requirements for 10.0

- A 10.0 sysroot: the headers of the Mac OS X 10.0 developer tools and
  the libraries and frameworks of a 10.0 system. The sysroot must have
  the private frameworks and all of `/usr/lib`, because the linker reads
  the libraries that the frameworks use.
- dos-sdk's `scripts/fix-macosx-10.0-headers.sh` on the sysroot. It
  fixes headers that clang cannot compile, and it makes the Foundation
  and AppKit functions `extern "C"`.
- dos-sdk's `scripts/build-c99-macosx.sh` on the sysroot. It installs
  `libc99.a` and its headers: the C99 and POSIX functions that 10.0 to
  10.2 do not have (for example the wide character functions, `dlopen`,
  recursive mutexes, `localtime_r` and `inet_pton`).
- libc++ of dos-sdk for 10.0 (`OSX_VERSION=10.0` and `SDK` set to the
  sysroot). For 10.0 to 10.3, it has threads, wide characters and the
  monotonic clock. It does not have locales, streams or the file system
  library.

### Build the library for 10.0

```sh
make TARGET=powerpc-apple-macosx10.0 BIN=path/to/clang/bin \
  SDK=path/to/sysroot-10.0 LIBCXX=path/to/libcxx-10.0 static
```

### Link a program for 10.0

Link as for 10.4, with these changes:

- Use `--target=powerpc-apple-macosx10.0` and the 10.0 sysroot.
- Add `-lc99` after the libc++ libraries.
- Add `-lobjc` and `-Wl,-flat_namespace`. Mac OS X 10.0 has no two-level
  namespace: the frameworks do not export the symbols of the frameworks
  and libraries that they use. With a flat namespace, the linker finds
  each symbol in all of the libraries.
- Add `-Wl,-no_uuid -Wl,-no_function_starts -Wl,-no_data_in_code_info
  -Wl,-no_version_load_command`. The dyld of 10.0 does not load a bundle
  that has these load commands ("unknown load command"). Programs start
  with them, but bundles (for example the `.saver` bundles) do not.
- Use a clang that writes a symbol table for each Objective-C module
  (clang-retro/llvm after PR 65). The Objective-C runtime of 10.0 stops
  the program with a bus error before `main` when an Objective-C file has
  no class or category.

### Differences on 10.0 to 10.3

| Part | Difference |
| --- | --- |
| Theme file | Before 10.3 there is no `kqueue`. Changes to the theme file show at the next start of the program. |
| Background programs | Before 10.3 there is no `TransformProcessType`. A program started outside an application bundle stays in the background. |
| Mouse | 10.0 sends no events for the middle mouse button. |
| Pictures | Before 10.4, the window pictures use the byte order of PowerPC. |
