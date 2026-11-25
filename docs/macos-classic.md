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
| Window classes | Build. The windows stay in memory (the headless backend) until the Toolbox backend is there. |
| Sockets (`CAsyncSocket`, `CSocket`) and WinInet (`CInternetSession`) | Not available |
| COM between programs | Not available. COM in one program works. |

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
`libwinport.a` (the portable layer). There are no shared libraries.

The Makefile does these steps for the classic Mac OS:

1. It compiles with `-Os`, `-ffunction-sections` and `-fdata-sections`,
   so that the linker can remove the code that the program does not
   use. On 68k, it uses 32-bit addresses (`-mcmodel=large`).
2. It uses the libstdc++ headers of mac-sdk, then `port/compat/macos`,
   then the C library headers.
3. It leaves out the files for sockets, WinInet, services, COM between
   programs, and the X11, Wayland and Cocoa backends.

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
runs `tests/run-macos-tests.sh`. The script builds `tests/core.cpp` and
`tests/memtest.cpp` into applications and runs them in mac-sdk's
simulators: `tools/sim68k.py` as a 68000 and a 68020, or
`tools/simppc.py`. The simulators need the Python module `unicorn`.
Without it, the script only builds the tests.

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
