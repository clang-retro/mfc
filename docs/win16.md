# 16-bit Windows

The library builds for 16-bit Windows (Windows 3.0 and later) with
clang-retro's `i8086-pc-win16` target and the win16-sdk sysroot. This is
the first step to MFC 2.5 for 16-bit Windows. This step gives the core
classes only.

## What is there

- `CObject`, `CRuntimeClass`, `DECLARE_DYNAMIC`, `DECLARE_DYNCREATE` and
  `DECLARE_SERIAL`
- `CString` (`CStringA`), and `CStringW` for text that you convert
- the collections: `CArray`, `CList`, `CMap`, `CStringArray`,
  `CObList`, `CMapStringToPtr` and the other classes of `afxcoll.h`
- `CFile`, `CStdioFile`, `CMemFile`, `CFileFind` and `CFileStatus`
- `CArchive` and serialization
- `CTime` and `CTimeSpan`
- the exceptions: `CException`, `CFileException`, `CMemoryException`
  and the others, with C++ `try` and `catch`, and with `TRY` and `CATCH`
- the memory diagnostics: `DEBUG_NEW`, `CMemoryState`,
  `AfxCheckMemory` and `AfxDumpMemoryLeaks`
- `AfxMessageBox`, `AfxGetInstanceHandle`, `AfxGetResourceHandle` and
  string resources

The window classes (`CWnd`, `CWinApp`, `CFrameWnd`, `CDialog`, the
controls, and document/view) are not there yet. `afxwin.h` does not
compile for 16-bit Windows.

## Build the library

```sh
make TARGET=i8086-pc-win16 BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroot \
  CXXFLAGS=-Os static
```

The library is `build/i8086-pc-win16/libmfc.a`. It is for the large
memory model with C++ exceptions. `WIN16_FLAGS` gives the flags of the
model (the default is `-mcmodel=large -fexceptions`).

## Build a program

```sh
clang++ --target=i8086-pc-win16 -mcmodel=large -fexceptions -frtti \
  --sysroot=/path/to/sysroot -Os -Ipath/to/mfc/include app.cpp \
  -Lpath/to/mfc/build/i8086-pc-win16 -lmfc -o app.exe
```

A program with `main()` and the core classes can use `-mquickwin`, which
shows its standard output and input in a window.

## How it works

- `afx.h` sets `_AFX_WIN16` and `STRICT` on 16-bit Windows, and includes
  `afxwin16.h`. That header gives the Win32 types (`INT_PTR`, `LPTSTR`,
  `FILETIME`, `WIN32_FIND_DATA` and others) and declares the KERNEL32
  functions that the core classes call.
- `src/win16.cpp` implements those functions. Files, directories and the
  clock use the C library (picolibc for DOS and Windows). File names are
  converted from the ANSI code page to the OEM code page for DOS.
- The library is ANSI only. `_UNICODE` gives an error. `CStringW` and
  the `W` functions convert between ANSI (as Latin-1) and UTF-16.
- `INT_PTR` and `UINT_PTR` are 16 bits, as the counts were in MFC 2.5.
  `DWORD_PTR` and `LONG_PTR` are 32 bits and hold a far pointer.
- The functions of the library (`AFXAPI`) use the pascal calling
  convention, as in MFC 2.5.
- There is one thread. The critical sections and the interlocked
  functions do nothing more than their plain operations. Thread-local
  storage is a table of 16 values.
- The library defines all the global `new` and `delete` operators,
  because the OMF linker has no weak definitions. `new` throws
  `CMemoryException` when there is no memory.

## Differences from Win32

- `FormatMessage` takes the message from a string only
  (`FORMAT_MESSAGE_FROM_STRING`). 16-bit Windows has no message tables.
- DOS keeps only the time of the last write of a file. `GetFileTime`
  gives that time for all three times.
- `LockFile` and `UnlockFile` do nothing.
- `GetTimeZoneInformation` gives the bias from the `TZ` variable, without
  daylight saving dates.
- `CFile` and `CArchive` count in 16-bit values where MFC 2.5 did. An
  archive holds at most 65,535 objects and classes.

## Test

`tests/core.cpp` and `tests/memtest.cpp` build for 16-bit Windows and pass
in Wine at `-O0`, `-Os` and `-O2`:

```sh
clang++ --target=i8086-pc-win16 -mcmodel=large -fexceptions -frtti \
  --sysroot=/path/to/sysroot -Iinclude tests/core.cpp \
  -Lbuild/i8086-pc-win16 -lmfc -o core.exe
wine core.exe
```
