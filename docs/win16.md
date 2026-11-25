# 16-bit Windows

The library builds for 16-bit Windows (Windows 3.0 and later) with
clang-retro's `i8086-pc-win16` target and the win16-sdk sysroot. It gives
the core classes and the window classes of MFC 2.5, in the large memory
model, ANSI only.

## What is there

The core classes:

- `CObject`, `CRuntimeClass`, `DECLARE_DYNAMIC`, `DECLARE_DYNCREATE` and
  `DECLARE_SERIAL`
- `CString` (`CStringA`), and `CStringW` for text that you convert
- the collections of `afxcoll.h` and `afxtempl.h`
- `CFile`, `CStdioFile`, `CMemFile`, `CFileFind` and `CFileStatus`
- `CArchive` and serialization
- `CTime` and `CTimeSpan`
- the exceptions, with C++ `try` and `catch`, and with `TRY` and `CATCH`
- the memory diagnostics: `DEBUG_NEW`, `CMemoryState`,
  `AfxCheckMemory` and `AfxDumpMemoryLeaks`

The window classes:

- `CWinApp`, `CWinThread` (the main thread only) and `CCmdTarget`
- message maps, `ON_COMMAND`, `ON_UPDATE_COMMAND_UI`, the `ON_WM_`
  macros, the control notifications and message reflection
- `CWnd`, `CFrameWnd`, `CMDIFrameWnd`, `CMDIChildWnd`, `CMiniFrameWnd`
  and `CSplitterWnd`
- `CDialog`, dialog data exchange (`DDX_` and `DDV_`), and the common
  dialogs of `commdlg.dll` (`CFileDialog`, `CColorDialog`,
  `CFontDialog`, `CPrintDialog` and `CFindReplaceDialog`)
- the controls: `CButton`, `CEdit`, `CStatic`, `CListBox`,
  `CCheckListBox`, `CComboBox` and `CScrollBar`
- `CDC`, `CPaintDC`, `CClientDC`, `CWindowDC`, `CMetaFileDC`, the GDI
  objects and `CMenu`
- document/view: `CDocument`, `CView`, `CScrollView`, `CFormView`, `CEditView`,
  `CSingleDocTemplate`, `CMultiDocTemplate`, printing and print preview
- the profile functions of `CWinApp`, with the INI file of the program
- `CToolBar`, `CStatusBar` and `CDialogBar`, with docking, floating and
  the idle update of the buttons and panes

## What is not there

- OLE: automation, OLE controls, containers, servers and drag and drop.
  The OLE functions of `CCmdTarget`, `CWnd`, `CView` and `CDocTemplate`
  are there, but they do nothing (`src/win16ole.cpp`).
- The common controls. 16-bit Windows does not have `comctl32.dll`.
  The library draws the toolbar and the status bar itself (see "The
  toolbar and the status bar"). `CReBar` and the other classes of
  `afxcmn.h` compile, but their windows are not made.
- Tooltips. A toolbar with `CBRS_TOOLTIPS` shows no tooltip.
  `CBRS_FLYBY` works: the status bar shows the prompt of the button.
- The shell file dialog (`IFileDialog`). `CFileDialog` always shows the
  classic dialog.
- Threads. `CWinThread::CreateThread` fails with `ERROR_NOT_SUPPORTED`.
- The medium and small models, and Unicode.

## Build the library

```sh
make TARGET=i8086-pc-win16 BIN=/path/to/llvm/bin SYSROOT=/path/to/sysroot static
```

The library is `build/i8086-pc-win16/libmfc.a`. `WIN16_FLAGS` gives the
flags of the model. The default is `-mcmodel=large -fexceptions`.

`make install TARGET=i8086-pc-win16` copies the headers to
`include/mfc`, `include/mfc/win16` and `include/mfc/winport` of the
sysroot, and the library to `lib`.

## Build a program

Use these flags:

```sh
clang++ --target=i8086-pc-win16 -mcmodel=large -fexceptions -frtti \
  -fno-threadsafe-statics --sysroot=/path/to/sysroot \
  -Ipath/to/mfc/include -Ipath/to/mfc/include/win16 \
  -idirafter path/to/mfc/port/include \
  app.cpp app.res -Lpath/to/mfc/build/i8086-pc-win16 -lmfc -o app.exe
```

- `include/win16` must come before the other MFC headers. Its OLE
  headers send the OLE names to the OLE 2 headers of the 16-bit SDK.
- `-idirafter port/include` must come last. The SDK has no common
  control headers. The headers of the portable layer give their types.
- `-fno-threadsafe-statics` is necessary. A function-local static
  object otherwise needs `__atomic_load_1`, which the 16-bit libraries
  do not have. A task has one thread, so the guard is not necessary.
- Compile the resources with `llvm-rc -win16`.

A program with `main()` and the core classes only can use `-mquickwin`,
which shows its standard output and input in a window.

## How it works

- `afx.h` sets `_AFX_WIN16` and `STRICT`, and includes `afxwin16.h`.
  That header gives the Win32 types and the KERNEL32 functions that the
  core classes call. `src/win16.cpp` does those functions.
- `afxwin.h` includes `afxwin16u.h`. That header gives the Win32 USER,
  GDI and KERNEL types, values and functions that the window classes
  use. `src/win16u.cpp` does those functions. Each function does its
  work with the nearest 16-bit function (`SetBrushOrg` for
  `SetBrushOrgEx`, `TrackPopupMenu` for `TrackPopupMenuEx`), or fails
  as Win32 fails when the system cannot do the call (paths, world
  transforms, enhanced metafiles).
- `INT_PTR`, `UINT_PTR` and `WPARAM` are 16 bits. `DWORD_PTR`,
  `LONG_PTR` and `LPARAM` are 32 bits and hold a far pointer.
- The functions of the library (`AFXAPI`) use the pascal calling
  convention, as in MFC 2.5.

### Messages

16-bit Windows packs some messages in a different way from Win32,
because `WPARAM` has 16 bits. The library reads each one in the 16-bit
way:

| Message | 16-bit packing |
|---|---|
| `WM_COMMAND` | ID in `wParam`; control in the low word and code in the high word of `lParam` |
| `WM_ACTIVATE` | state in `wParam`; window in the low word and minimized flag in the high word of `lParam` |
| `WM_HSCROLL`, `WM_VSCROLL` | code in `wParam`; position in the low word and scroll bar in the high word of `lParam` |
| `WM_MENUSELECT`, `WM_MENUCHAR` | item or character in `wParam`; flags in the low word and menu in the high word of `lParam` |
| `WM_VKEYTOITEM`, `WM_CHARTOITEM` | key in `wParam`; list box in the low word and caret in the high word of `lParam` |
| `WM_MDIACTIVATE` | activation flag in `wParam`; activated window in the low word and deactivated window in the high word of `lParam` |
| `WM_CTLCOLOR` | one message; control in the low word and `CTLCOLOR_` code in the high word of `lParam` |

- `tools/gen-msgmap.py` writes a 16-bit form of each `ON_WM_` macro for
  these messages (`include/afxmsg_wm_.h`).
- `CWnd::OnCommand` and the notification reflection use
  `GET_WM_COMMAND_ID`, `GET_WM_COMMAND_HWND` and `GET_WM_COMMAND_CMD`.
  To send a notification, use `GET_WM_COMMAND_MPS`. `afxwin.h` defines
  these macros for each target.
- `CWnd::OnWndMsg` changes `WM_CTLCOLOR` to the Win32 message of its
  `CTLCOLOR_` code. Thus `ON_WM_CTLCOLOR` and `ON_WM_CTLCOLOR_REFLECT`
  work as on Win32. `Default()` sends the message that came.
- `CMDIFrameWnd` sends `WM_MDISETMENU` with both menus in `lParam`, and
  reads `WM_MDIGETACTIVE` from the two words of the result. In place of
  `WM_MDIREFRESHMENU`, it sends `WM_MDISETMENU` with `wParam` `TRUE`.

### Dialog templates

`CAfxDlgTemplate` (`src/dlgtmpl.cpp`) writes the 16-bit template form.
The header has a byte for the item count. The strings are ANSI. The
class of each item is one byte. There is no alignment.
`DialogBoxIndirect` needs the template in a global memory block. The
library copies the template into one.

### The registry

16-bit Windows has one registry tree, `HKEY_CLASSES_ROOT`. Each key has
one value, the value without a name.

- `RegOpenKeyEx`, `RegCreateKeyEx`, `RegQueryValueEx` and
  `RegSetValueEx` use the 16-bit calls of `shell.dll` for that tree and
  for string values.
- A call for another tree, or for a value with a name, fails with
  `ERROR_BADKEY`. This value is the same as `ERROR_FILE_NOT_FOUND` of
  Win32.
- Thus a program that calls `SetRegistryKey` keeps its settings in its
  INI file. The document types that `RegisterShellFileTypes` records
  go to the registry.

### The task

A 16-bit task has one thread.

- `GetCurrentThreadId` gives the task handle.
- `PostThreadMessage` uses `PostAppMessage`.
- `GetCommandLine` gives the path of the program and the command tail
  of its program segment prefix.
- Events, mutexes and semaphores are objects of the task. A wait for an
  object that is not signaled cannot end, because no other thread can
  signal it. The wait gives `WAIT_TIMEOUT` when the time is up. An
  `INFINITE` wait fails with `ERROR_POSSIBLE_DEADLOCK`.
  `MsgWaitForMultipleObjects` also ends when a message comes.

### The toolbar and the status bar

`src/win16bar.cpp` registers two window classes, `AfxToolbar16` and
`AfxStatusBar16`. `afxwin16u.h` sets `TOOLBARCLASSNAME` and
`STATUSCLASSNAME` to these names. Wine can also have the 32-bit classes
of `comctl32.dll`, which do not read the 16-bit structures. Thus the
names are not the names of the common controls.

- The windows do the messages that `CToolBar` and `CStatusBar` send
  (`TB_ADDBUTTONS`, `TB_SETSTATE`, `TB_GETITEMRECT`, `SB_SETPARTS`,
  `SB_SETTEXT`, `SB_GETRECT` and others). Other messages go to
  `DefWindowProc`.
- A toolbar button has a raised edge. A pressed or checked button has a
  sunken edge, and a checked button has a dithered face. A disabled
  button shows its image in the highlight and shadow colors. With
  `TBSTYLE_FLAT`, only a pressed or checked button has an edge.
- A click sends `WM_COMMAND` with `BN_CLICKED` to the owner of the
  toolbar, in the 16-bit packing.
- The toolbar keeps the text of a button (`SetButtonText`), but it does
  not draw it.
- A status bar pane has a sunken edge, a raised edge (`SBPS_POPOUT`) or
  no edge (`SBPS_NOBORDERS`). An owner-drawn pane sends `WM_DRAWITEM`
  to the parent.
- A press on the size grip of the status bar sizes the frame from its
  lower-right corner.
- A `UINT` has 16 bits. Thus a button style of `CToolBar` has the
  `TBSTYLE_` bits in its low byte and the `TBSTATE_` bits in its high
  byte, as in MFC 2.5. `SBPS_DISABLED` is `0x4000`, and `SBPS_STRETCH` is
  `0x8000`.

### Handle maps

A 16-bit handle is a near pointer. The handle maps of the library use
the handle as a word key (`CMapWordToPtr`).

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
- `CBitmap::LoadMappedBitmap` and `CToolBar::LoadBitmap` do not change
  the colors of the bitmap to the system colors.
- `LoadImage` loads a bitmap, an icon or a cursor from the resources
  only. It does not load from a file.
- `CPrintDialogEx` and `CPageSetupDialog` are not there.
- A scroll bar has no page size. `GetScrollInfo` gives `nPage` 0.
- `ExtCreatePen` makes a cosmetic pen of one color only.
- `IDC_SIZEALL` is `IDC_SIZE`, and `DEFAULT_GUI_FONT` is
  `ANSI_VAR_FONT`.

## Test

`make check TARGET=i8086-pc-win16 BIN=... SYSROOT=...` runs
`tests/run-win16-tests.sh`. The script builds these tests at `-O0`,
`-Os` and `-O2` and runs them in Wine:

| Test | What it does |
|---|---|
| `core` | the core classes |
| `memtest` | the memory diagnostics |
| `win16wnd` | `CWinApp`, `CFrameWnd`, a menu command, a button notification, `WM_CTLCOLOR`, a modal `CDialog` with DDX, and the INI profile |
| `win16doc` | `CSingleDocTemplate`, File New, `OnDraw`, `OnUpdate`, and the command routing to the view and the document |
| `win16mdi` | `CMultiDocTemplate`, two MDI children, the MDI menus, and `MDIGetActive`, `MDINext` and `MDIActivate` |
| `win16bar` | `CToolBar` and `CStatusBar`: the button layout, a click, the idle update, the message pane, docking at the left edge, floating, and `ShowControlBar` |

The window tests need an X display. Without one, the script starts
itself under `xvfb-run`.

Wine puts a 16-bit MDI child that has the position `CW_USEDEFAULT`
outside the client area. The MDI test thus gives its children a
position.
