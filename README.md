# clang-retro MFC

An independent implementation of the Microsoft Foundation Classes API,
for building existing MFC programs with clang-retro (and other clang or
MinGW-w64 toolchains) without Visual C++. The aim is source
compatibility: a program written for MFC compiles against these headers
and links against `libmfc.a` (ANSI) or `libmfcu.a` (Unicode) unchanged,
as far as standard C++ allows.

It is written from MFC's public documentation and the behaviour of
programs that use it. No Microsoft source code is used or copied.

## Building

```sh
# Windows 2000/XP sysroot (msvcrt.dll)
make BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroots/win2k-xp

# Any of win32-sdk's clang config files, such as picolibc (no C runtime DLL)
make BIN=/path/to/llvm/bin CFG=/path/to/cfg/win32-picolibc.cfg

# Stock clang with Ubuntu's MinGW-w64 and libstdc++
make TARGET=i686-w64-mingw32 STDLIB=
```

The libraries go to `build/<triple>` (or `build/<config name>`).
`make install PREFIX=dir` copies the headers to `dir/include/mfc` and the
libraries to `dir/lib`.

A program then needs only the include path and the library path:

```sh
clang++ --config=winxp.cfg -Ipath/to/mfc/include app.cpp app-res.o \
  -Lpath/to/build/i586-pc-windows5.1-gnu -mwindows -o app.exe
```

As with Visual C++, `afx.h` names the libraries to link, so no `-l`
options are needed: `mfc` or `mfcu` by `_UNICODE`, and `user32`, `gdi32`,
`comdlg32`, `comctl32`, `shell32`, `advapi32`, `winspool` and `ole32`.
Define `_AFX_NOFORCE_LIBS` to turn that off. The library provides
`WinMain` (and `wWinMain`), so Unicode programs link with or without
`-municode`.

## Testing

`make check` (with the same variables as the build) builds the programs
in `tests/` in ANSI and Unicode and runs them under Wine, starting Xvfb
when there is no display:

* `core`: CString, collections, CFile, CArchive and serialization,
  CTime, exceptions and run-time classes, on the console.
* `dlgtest`: a dialog with data exchange and validation, common
  controls, and a property sheet.
* `sditest`: a single-document program with a toolbar, a status bar and
  an edit view; idle command updates, tooltips, saving and loading.
* `mditest`: a multiple-document program whose child frames are split
  into a form view and a scroll view; several documents, the Window menu
  commands, closing.

The GUI tests drive themselves with timers, check what they see, and
save screenshots (PNG when ImageMagick is installed) next to their logs
in `build/<triple>/tests/a` and `tests/u`. CI runs them on every pull
request with the stock MinGW-w64 toolchain.

## What is there

* Core (`afx.h`, `afxcoll.h`, `afxtempl.h`, `afxstr.h`): CObject and
  CRuntimeClass with dynamic creation and serialization, CString
  (CStringA/CStringW), the collection classes and templates, CFile,
  CStdioFile, CMemFile, CArchive, CTime, CTimeSpan, exceptions.
* Windows (`afxwin.h`): CWinApp, CWinThread, CWnd and message maps,
  command routing and idle updates, GDI objects and device contexts,
  standard controls, CDialog with DDX/DDV, frames, documents and views
  (SDI and MDI), CScrollView, printing.
* `afxext.h`: CToolBar, CStatusBar, CDialogBar, docking at the frame
  edges, CSplitterWnd (static and dynamic), CFormView, CEditView,
  CBitmapButton.
* `afxdlgs.h`: the common dialogs (file, color, font, print, page setup,
  find/replace) and CPropertySheet/CPropertyPage, wizards included.
* `afxcmn.h`, `afxcview.h`: the common controls, CImageList, CListView,
  CTreeView. `afxmt.h`: synchronization objects.

## Differences from Visual C++ MFC

* Message map handler names: Visual C++ 6 code writes plain names
  (`ON_COMMAND(ID_X, OnX)`), later code writes member pointers
  (`ON_COMMAND(ID_X, &CMyView::OnX)`). Standard C++ has no expression
  that accepts both, so the second form needs
  `_AFX_MSGMAP_MEMBER_POINTERS` defined before `afxwin.h`.
* `min` and `max` are function templates rather than macros (MinGW-w64
  defines the macros for C only). `NOMINMAX` turns them off.
* Toolbars dock at the frame's edges but do not float. Print preview,
  split boxes on scroll bars, OLE/COM automation, ActiveX control
  containment, database classes and sockets are not implemented yet.
* `llvm-rc` (and `llvm-windres`, which uses it) has no `TOOLBAR`
  statement yet; write toolbar resources as raw resource type 241, as
  `tests/sditest.rc` does.

## Plans

Win16 (with win16-sdk), and later a portable layer that implements the
part of the Win32 API MFC uses, with controls drawn in the classic
style, so that MFC programs build and look the same on macOS and Linux.

## Layout

`include/` has the public headers, `src/` the library, `tests/` the
test programs and `tools/` the generator for the `ON_WM_*` macros and
CWnd's default handlers (`tools/gen-msgmap.py`) and a development helper
that lists declared functions the library does not define yet.
