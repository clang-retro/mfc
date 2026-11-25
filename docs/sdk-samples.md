# Samples of the Win32 SDK

`tools/build-sdk-samples.py` builds samples of a Win32 SDK over the
portable layer. The samples are C and C++ programs for Windows NT and
Windows 95. They build without changes to their files.

The SDK is not part of this project. Use the samples of your own copy of
the SDK.

## The samples

| Name | Sample | Program |
| --- | --- | --- |
| `multipad` | MULTIPAD, an MDI text editor like Notepad | `multipad` |
| `3dfo` | The OpenGL screen saver 3D Flying Objects | `ss3dfo.scr` |
| `flwbox` | The OpenGL screen saver Flower Box | `ssflwbox.scr` |
| `maze` | The OpenGL screen saver Maze | `ssmaze.scr` |
| `pipes` | The OpenGL screen saver Pipes | `sspipes.scr` |
| `text3d` | The OpenGL screen saver 3D Text | `sstext3d.scr` |

The screen savers use the code of the `SCRSAVE/COMMON` folder of the SDK.
That code has its own `WinMain` and does not use `libscrnsave.a`.

## Build the samples

1. Build the library for the target (`make ... static`).
2. Set the compiler and the flags in the environment:

   | Variable | Value |
   | --- | --- |
   | `CC`, `CXX` | The C and C++ compilers. The default is `clang` and `clang++` in `BIN`, or on `PATH`. |
   | `CFLAGS` | The flags for C and C++: the target, the SDK of the system and the optimization. |
   | `CXXFLAGS` | More flags for C++: the headers of the C++ library. |
   | `LDFLAGS` | The flags for the link: the target, the linker and the C++ library. |
   | `MFC_OUT` | The directory with `libwinport.a` and `libglaux.a`. |
   | `SYSLIBS` | The system libraries. The default is `-framework Cocoa -framework OpenGL -framework GLUT`. |
   | `HOST_CXX` | A C++ compiler for the host. `res2cpp.py` uses it to read the macros of the layer. The default is `CXX`. |

3. Run the script:

   ```sh
   python3 tools/build-sdk-samples.py --sdk path/to/SDK --out out
   ```

   `--sdk` is the SDK or its `SAMPLES` folder. `--only pipes,maze`
   builds only these samples.

The programs go into `out/bin`. The log of the commands goes into
`out/build.log`.

For Mac OS X 10.4 on PowerPC, see [macosx-10.4.md](macosx-10.4.md) for
the values of the variables.

## What the script changes

The script copies the files of a sample into `out/src`. It changes the
copies, not the files of the SDK. Where the layer or a compiler flag can
give a sample what it expects, the script does not change its code. It
changes the code only where clang has no flag for an old idiom:

- It makes the file names lower case, and the `#include` paths use `/`.
- It adds the commas that old resource scripts do not have after a
  string. `llvm-rc` needs them.
- It changes the cast-as-lvalue idiom `*((TYPE *)p)++` to standard C.
- It gives the type `int` to the old declarations `static name;` in C++.
- For a 64-bit target only, it gives the type `LRESULT` to window
  procedures that the samples declare as `LONG`. On a 32-bit target the
  layer makes `LRESULT` the same type as `LONG`, as on Win32.
- When the compiler finds a variable that is not declared, it moves the
  declaration of a `for` variable out of its loop. The old C++ compilers
  kept such a variable after the loop.
- For a big-endian target, it writes the numbers of the texture files
  (`.bmp` and `.a8`) of the screen savers in the order of the processor.
  The savers keep these files as resources of their own types (100 and
  101), and the layer does not change such resources. The code of the
  savers reads the numbers of these files in the order of the processor.
- It declares a structure before a prototype that names it first
  (`maze.h`). The list is `PATCHES` in the script.

The script compiles C with `-std=gnu89` and `-fcommon`, and with
`NO_STRICT`, as the compilers of the time did. It makes the release build
(`NDEBUG`). `rand` gives numbers up to `0x7FFF`, as in the C library of
Visual C++: the samples compute `RAND_MAX + 1` in an `int`.

The layer and the flags give the samples what the headers and compilers
of their time gave:

| What | How |
| --- | --- |
| `HIMC` is a `DWORD` | `imm.h` declares it so when `WINVER` is less than `0x040A`. The script sets `WINVER` to `0x0400`. |
| `index` and `rindex` are not functions | `string.h` and `strings.h` of the layer hide them. C compiles with `-fno-builtin-index -fno-builtin-rindex`. |
| A `main` that returns `void` | The screen savers compile with `-Dmain=ss_old_main`. The layer has the `main` of the program. |

The resource compiler reads the headers of a sample. The script gives it
copies of the headers that keep only the preprocessor lines, as the
resource compiler of Windows does.

## Run the programs

Run a screen saver with `/s`. Run it without an argument to see the
configuration dialog.
