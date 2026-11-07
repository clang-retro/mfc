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
`comdlg32`, `comctl32`, `shell32`, `advapi32`, `winspool`, `ole32`,
`oleaut32` and `uuid` (`afxsock.h` adds `ws2_32` and `afxdb.h` adds
`odbc32`).
Define `_AFX_NOFORCE_LIBS` to turn that off. The library provides
`WinMain` (and `wWinMain`), so Unicode programs link with or without
`-municode`.

## Testing

`make check` (with the same variables as the build) builds the programs
in `tests/` in ANSI and Unicode and runs them under Wine, starting Xvfb
when there is no display:

* `core`: CString, collections, CFile, CArchive and serialization,
  CTime, exceptions and run-time classes, on the console.
* `disptest`: the automation helpers in `afxdisp.h` (COleDateTime and
  its span, COleCurrency, COleVariant, COleSafeArray) and a
  COleDispatchDriver calling an IDispatch written in the test, on the
  console.
* `autotest`: automation servers: a dispatch map's methods and
  properties called through IDispatch and COleDispatchDriver (argument
  conversion, by-reference arguments, errors, exceptions, the default
  member, derived classes), interface maps, aggregation, a
  COleObjectFactory in the same process and its registry entries, and
  the program started again by COM as a local server, on the console.
* `socktest`: `afxsock.h` over the loopback interface: CAsyncSocket
  events, blocking CSocket calls against a server thread, CArchive
  through a CSocketFile, datagrams, errors and cancelling a blocking
  call.
* `dlgtest`: a dialog with data exchange and validation, common
  controls, and a property sheet.
* `sditest`: a single-document program with a toolbar, a status bar and
  an edit view; idle command updates, tooltips, saving and loading.
* `mditest`: a multiple-document program whose child frames are split
  into a form view and a scroll view; several documents, the Window menu
  commands, closing.
* `docktest`: toolbars docked side by side and on each edge, floating,
  dragged with the mouse, double-clicked, resized while floating, and
  the layout saved and loaded with SaveBarState and LoadBarState.
* `splittest`: a dynamic splitter split by dragging its split boxes and
  with Window > Split from the keyboard, panes scrolling together on
  shared scroll bars, next and previous pane, and unsplitting by
  dragging a bar to the edge.
* `richtest`: a rich edit document and view; bold, italic and paragraph
  alignment on the selection, finding and replacing, saving and loading
  rich text and plain text, page layout for printing, word wrap, an
  object embedded in the text (selected, saved and loaded with it), and
  the Insert Object, Paste Special and Links dialogs.
* `dbtest`: `afxdb.h` against an in-memory ODBC driver written in the
  test (`tests/odbcmock.h`), since Wine has no 32-bit ODBC driver to
  test with: opening, moving, filters, sorts and parameters, nulls,
  adding, editing and deleting records, transactions, field info and
  errors, on the console.
* `rvtest`: a CRecordView over the same driver; the form showing the
  current record, edits written back when moving, nulls shown as empty
  fields and indeterminate check boxes, and the record commands enabled
  at each end.
* `dragtest`: `afxole.h` in a scroll view: data put on the clipboard
  and read back (cached, rendered when asked for, and as a file),
  a COleDropTarget fed enter, over, leave and drop by hand, scrolling
  near the edges, and a real DoDragDrop from the view onto itself.
* `oletest`: an OLE container: objects of a class the test registers
  itself, embedded in a COleDocument, drawn by its view, opened and
  changed through their verbs and the verb menu, saved to flat and
  compound files and loaded again (Save As included), deleted, copied
  through the clipboard and cloned.

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
* `afxext.h`: CToolBar, CStatusBar, CDialogBar, docking and floating
  (dock bars, CMiniFrameWnd, drag and drop, CDockState), CSplitterWnd (static and dynamic, with split boxes and shared
  scroll bars), CFormView, CEditView,
  CBitmapButton.
* `afxrich.h`: CRichEditView and CRichEditDoc (rich text or plain text
  files, formatting the selection, find and replace, printing), with
  OLE objects in the text through CRichEditCntrItem (Insert Object,
  Paste Special, the selected item).
* `afxdb.h`: CDatabase, CRecordset (snapshot, dynaset and forward-only,
  with RFX for the usual types, bookmarks and parameters),
  CRecordView and the DDX_Field functions, CDBException, CDBVariant.
* `afxdisp.h`: automation clients (COleDispatchDriver and the variant,
  date, currency and safe array classes) and servers: dispatch maps on
  any CCmdTarget, interface maps, aggregation, COleObjectFactory with
  registration and licensing, AfxOleLockApp and the /Automation start.
* `afxole.h`: data transfer: COleDataSource (cached and delayed data,
  the clipboard, DoDragDrop), COleDataObject, COleDropSource and
  COleDropTarget, with the CView drag and drop handlers. Containers:
  COleDocument (flat or compound files), COleClientItem (new, pasted,
  dropped, linked, static and cloned objects, verbs, drawing, the
  clipboard), COleStreamFile, AfxOleSetEditMenu. Programs can include
  `afxolecl.rc` for the container messages.
* `afxodlgs.h`: the OLE dialogs over oledlg: COleInsertDialog,
  COlePasteSpecialDialog, COleLinksDialog, COleUpdateDialog,
  COleChangeIconDialog, COleConvertDialog and COleBusyDialog, and the
  COleDocument Links, Convert and Change Icon commands.
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
* Floating bars are tool windows, so the system draws their captions,
  and they resize with the system's sizing border.
* CRichEditDoc derives from COleDocument rather than COleServerDoc
  (OLE servers are not there yet). Wine's rich edit control draws an
  embedded object from its data rather than through IViewObject, and
  writes only the object's picture into rich text, so there a saved
  object comes back as a picture.
* Wine's oledlg ends a cancelled Insert Object dialog with OLEUI_FALSE,
  which DoModal reports as IDABORT, and its Links dialog does nothing
  yet.
* Wine's rich edit control does not implement EM_FORMATRANGE yet, so
  under Wine a CRichEditView prints blank pages; Windows prints them.
* A dispatch map entry calls its member function through a template
  made from the function's own type, not through assembly that pushes
  the arguments, so the function's parameter types must be the ones its
  VTS_ string describes (as they must in MFC) and it must not be
  overloaded. Drag scrolling starts as soon as the pointer is in the
  inset, without waiting nScrollDelay. Type libraries (EnableTypeLib), connection points and
  events, OLE servers and ActiveX control containment are not
  implemented yet.
* Container items are not activated in place yet: objects open in
  their own windows. COleDocument saves its items in its own format,
  which is not the one Visual C++ MFC writes. Wine's
  OleCreateStaticFromData makes objects without IOleObject, so static
  items cannot be created under Wine.
* Sockets are IPv4, as in MFC 6. `CAsyncSocket::FromHandle` returns
  only sockets attached in the calling thread, and NULL for others,
  rather than making a temporary object.
* CRecordset keeps the rows it has fetched and writes changes with
  UPDATE, INSERT and DELETE statements that match the record by the
  values it read, rather than with positioned cursors, so it works with
  any driver. A dynaset therefore behaves like a snapshot: it does not
  see other users' changes until Requery. Output parameters of stored
  procedures are not supported, and the DAO classes (`afxdao.h`) are
  not there.
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
