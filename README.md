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

The libraries go to `build/<triple>` (or `build/<config name>`): the
static libraries `libmfc.a` and `libmfcu.a` (Unicode), and the shared
library, `mfc.dll` and `mfcu.dll` (see below). `make static` builds only
the static ones. `make install PREFIX=dir` copies the headers to
`dir/include/mfc`, the libraries to `dir/lib` and the DLLs to `dir/bin`.

A program then needs only the include path and the library path:

```sh
clang++ --config=winxp.cfg -Ipath/to/mfc/include app.cpp app-res.o \
  -Lpath/to/build/i586-pc-windows5.1-gnu -mwindows -o app.exe
```

As with Visual C++, `afx.h` names the libraries to link, so no `-l`
options are needed: `mfc` or `mfcu` by `_UNICODE`, and `user32`, `gdi32`,
`comdlg32`, `comctl32`, `shell32`, `advapi32`, `winspool`, `ole32`,
`oleaut32` and `uuid` (`afxsock.h` adds `ws2_32`, `afxinet.h` adds
`wininet`, `afxole.h` adds `oledlg` and `urlmon`, and `afxdb.h` adds
`odbc32`).
Define `_AFX_NOFORCE_LIBS` to turn that off. The library provides
`WinMain` (and `wWinMain`), so Unicode programs link with or without
`-municode`.

A regular DLL (an MFC DLL with a CWinApp of its own, such as a
control's `.ocx`) builds the same way with `-shared` and a `.def` file
for its exports:

```sh
clang++ --config=winxp.cfg -Ipath/to/mfc/include -shared ctl.cpp ctl.def ctl-res.o \
  -Lpath/to/build/i586-pc-windows5.1-gnu -o ctl.ocx
```

The library provides its `DllMain`, which starts the DLL's CWinApp
(InitInstance) when it loads and ends it (ExitInstance) when it unloads,
so such a DLL does not write one; `_AFX_NO_DLLMAIN` turns that off. The
DLL has its own copy of the library, with its own instance handle,
resources, window classes and window objects, so `AFX_MANAGE_STATE` has
nothing to do and the program and the DLL need not use the same
library build. `AfxDllGetClassObject`, `AfxDllCanUnloadNow` and
`COleObjectFactory::UpdateRegistryAll` implement the usual exports.

### Linux and macOS

A portable layer in `port/` implements the part of the Win32 API that MFC
uses over POSIX, so the libraries also build for Linux and macOS with
the system's own clang:

```sh
make TARGET=x86_64-linux-gnu STDLIB=          # Linux (glibc, libstdc++)
make TARGET=arm64-apple-macos11 STDLIB= AR=ar # macOS
```

This adds `libwinport.a`, which programs link after the MFC library:

```sh
clang++ -std=c++17 -Ipath/to/mfc/port/include -Ipath/to/mfc/include app.cpp \
  -Lpath/to/build/x86_64-linux-gnu -lmfc -lwinport -ldl -lpthread -o app
```

It is a work in progress. So far the non-window classes, automation, the
registry (kept in `~/.config/winport/registry.reg`), sockets and COM
within one process work; windows, drawing and resources compiled from
`.rc` files come next, with X11, Wayland and Cocoa backends (the Cocoa
one for Mac OS X 10.4 and later, PowerPC and Intel). The functions not
written yet are listed in `port/src/stubs.txt` and fail with
`ERROR_CALL_NOT_IMPLEMENTED`; `WINPORT_DEBUG=1` names each one the first
time a program calls it.

The ANSI code page there is UTF-8, `WCHAR` is the system's 32-bit
`wchar_t`, and `long` is 64 bits on 64-bit systems while `LONG` and
`DWORD` stay 32 bits. Code that passes a `long*` where the API takes a
`LONG*` needs `LONG`; in dispatch maps `long` remains the type of
`VT_I4`, as MFC documents.

### The shared library

A program built with `_AFXDLL` uses the library in `mfc.dll` (`mfcu.dll`
for Unicode), which goes next to the program. `afx.h` then names
`mfcdll` (the import library) and `mfcs` (`mfcudll` and `mfcus`), the
latter with the few pieces each module keeps for itself, such as
`WinMain`:

```sh
clang++ --config=winxp.cfg -D_AFXDLL -Ipath/to/mfc/include app.cpp app-res.o \
  -Lpath/to/build/i586-pc-windows5.1-gnu -mwindows -o app.exe
```

The DLL uses no C runtime DLL of its own choosing: it is built with the
same C library as the program (`msvcrt.dll`, or a static picolibc in
each module, whose `malloc` takes its blocks from the process heap), so
memory from `new` or `malloc` in one module can be freed in another.
With MinGW and libstdc++, link without `-static`, so that libstdc++ and
libgcc are DLLs too: only then does an exception thrown in one module
reach a `catch` in another.

An extension DLL is built with `_AFXDLL` and `_AFXEXT` and shares the
program's `mfc.dll`: its classes, marked `AFX_EXT_CLASS` (functions
`AFX_EXT_API`), are part of the program, and its resources are found
after the program's own. Its `DllMain` registers it:

```cpp
static AFX_EXTENSION_MODULE extDLL = {NULL, NULL};
extern "C" int APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
  if (dwReason == DLL_PROCESS_ATTACH) {
    if (!AfxInitExtensionModule(extDLL, hInstance)) return 0;
    new CDynLinkLibrary(extDLL);
  } else if (dwReason == DLL_PROCESS_DETACH) {
    AfxTermExtensionModule(extDLL);
  }
  return 1;
}
```

```sh
clang++ --config=winxp.cfg -D_AFXDLL -Ipath/to/mfc/include -shared ext.cpp ext-res.o \
  -Lpath/to/build/i586-pc-windows5.1-gnu -Wl,--out-implib,libext.a -o ext.dll
```

## Testing

`make check` (with the same variables as the build) builds the programs
in `tests/` in ANSI and Unicode and runs them under Wine, starting Xvfb
when there is no display:

* `core`: CString, collections, CFile, CArchive and serialization,
  CTime, exceptions and run-time classes, on the console.
* `memtest`: the memory diagnostics in a debug build: DEBUG_NEW and
  object blocks, CMemoryState (Checkpoint, Difference, DumpStatistics,
  DumpAllObjectsSince) caught through afxDump, guard bytes and
  AfxCheckMemory, AfxIsMemoryBlock, AfxDoForAllObjects, tracking turned
  off, the allocation hook and delayed frees, on the console.
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
* `conntest`: connection points and type libraries, on the console: a
  connection map through IConnectionPointContainer and
  IConnectionPoint (enumeration, limits, cookies), AfxConnectionAdvise
  with and without a reference, events fired to dispatch sinks, and the
  type information of IMPLEMENT_OLETYPELIB from a type library the test
  writes and registers.
* `socktest`: `afxsock.h` over the loopback interface: CAsyncSocket
  events, blocking CSocket calls against a server thread, CArchive
  through a CSocketFile, datagrams, errors and cancelling a blocking
  call, and IPv6 datagrams and connections over `::1` with the Ex
  functions (the IPv6 checks run only where the system has IPv6).
* `inettest`: `afxinet.h` against an HTTP server and an FTP server the
  test runs itself: AfxParseURL, OpenURL for http, ftp and file URLs,
  GET, POST and PUT requests (SendRequest, SendRequestEx and
  EndRequest, buffered writes), request headers, status codes and
  QueryInfo, ReadString, cookies, the status callback,
  CInternetException, and FTP directories, files, listings
  (CFtpFileFind), PutFile, GetFile, Rename and Remove, in passive and
  active mode.
* `isapitest`: `afxisapi.h`, with an extension and a filter called
  through the functions an ISAPI DLL exports by a pretend server:
  CHtmlStream (growing, attached buffers), a parse map's commands
  (the default command, a base class's map, arguments by position and
  by name with defaults, numbers of each ITS_ type, posted form data
  read in parts, ITS_RAW data, parse errors), the reply's status and
  headers, server variables, and CHttpFilter notifications.
* `htmltest`: `afxhtml.h` with a WebBrowser control of the test's own
  (Wine's needs Gecko to show a page), registered for CLSID_WebBrowser
  in the process. For CHtmlView: the browser filling the view,
  Navigate, the Navigate2 forms and LoadFromResource with their
  arguments, the browser's events reaching the handlers (and a cancel
  going back), the properties, PutProperty and GetProperty, ExecWB,
  File Print, the forms commands, GetSource, the IDocHostUIHandler
  questions and the browser's keys. For CDHtmlDialog, over a page of
  fake elements: the page from the resources and its title, the
  DDX_DHtml functions both ways, the element and control functions, the
  DHTML event map by id, class and tag and the document's events,
  window.external only for a local page, and a new page. For
  CMultiPageDHtmlDialog: each page's embedded map from the URL map, by
  resource id or name, with the dialog's own map on every page, and an
  ActiveX control's events (DHTML_EVENT_AXCONTROL) with by-value and
  by-reference arguments. For HTML editing: CHtmlEditView's start page
  in design mode, the CHtmlEditCtrlBase commands with their arguments
  and values, the page's text both ways, the editing command map
  (enabling, checks, handlers), CHtmlEditDoc saving, a new document and
  opening a URL, and CHtmlEditCtrl.
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
* `richsrvtest`: CRichEditDoc as a server document, with its container
  in the same program: a new rich text object embedded, its text read
  back through the object's data, a copy saved and loaded again with
  its formatting, and a link to a saved file.
* `dbtest`: `afxdb.h` against an in-memory ODBC driver written in the
  test (`tests/odbcmock.h`), since Wine has no 32-bit ODBC driver to
  test with: opening, moving, filters, sorts and parameters, nulls,
  adding, editing and deleting records, stored procedures (a result
  set, a return value, output and input/output parameters), transactions,
  field info and errors, on the console.
* `rvtest`: a CRecordView over the same driver; the form showing the
  current record, edits written back when moving, nulls shown as empty
  fields and indeterminate check boxes, and the record commands enabled
  at each end.
* `daotest`: `afxdao.h` against an in-memory DAO engine written in the
  test (`tests/daomock.h`, with the interfaces' stubs generated by
  `tests/gen-daostubs.py`), since Wine has no DAO: the engine made
  through its licence key, workspaces, creating a database with tables,
  fields and indexes and reading their info back, recordsets of each
  type (moves, positions, bookmarks, Find, Seek on an index, filters,
  sorts and Requery), DFX members and nulls, AddNew, Edit, Update,
  CancelUpdate and Delete, parameters of recordsets and querydefs,
  saved and temporary querydefs, transactions and CDaoException, on the
  console.
* `dvtest`: a CDaoRecordView over the same engine: the form showing
  the current record, edits written back when moving, nulls shown as
  empty fields and indeterminate check boxes, and the record commands
  enabled at each end (known from the start, through bookmarks).
* `dragtest`: `afxole.h` in a scroll view: data put on the clipboard
  and read back (cached, rendered when asked for, and as a file),
  a COleDropTarget fed enter, over, leave and drop by hand, scrolling
  near the edges, and a real DoDragDrop from the view onto itself.
* `oletest`: an OLE container: objects of a class the test registers
  itself, embedded in a COleDocument, drawn by its view, opened and
  changed through their verbs and the verb menu, saved to flat and
  compound files and loaded again (Save As included), deleted, copied
  through the clipboard and cloned.
* `srvtest`: an OLE server and its container in one program, which COM
  starts again as the server: registration, an embedded object made,
  shown, changed, updated and closed through COleServerDoc and
  COleServerItem, the server's embedded menu and data, the object
  loaded again by a second server process, and its clipboard copy.
* `linktest`: links to a server's files and their items, bound as a
  container's link binds them: to an open document through the running
  object table, and to a closed file whose class the shell registration
  gives, which COM opens hidden and closes when the link lets go. It
  checks the item's moniker, data and notifications, the item container,
  the Link Source data Paste Link uses, links to an object embedded
  in a container's file, the object information the Object Properties
  and Change Source dialogs show, and COleMessageFilter's answers.
* `iptest`: in-place activation, with the container and the server in
  one program: the object activated in the container's view, the
  shared menu, both sides' accelerators, the server's toolbar in the
  container's frame, resizing through the resize bar, UI deactivation
  and full deactivation, and CRectTracker.
* `docobtest`: Active Documents, with the container and the server in
  one program: a COleDocObjectItem's document site makes the server's
  document a DocObject, which shows over the whole of the container's
  view; the test checks the view's rectangle, the shared menu with
  both Help popups, commands through IOleCommandTarget (standard ones
  and an OLE command map's own, with an update handler's text and
  check), the
  view state, printing through IPrint (the page count, page sets, a
  target printer and the continue callback), closing the view, and the
  DocObject registration.
* `occtest`: ActiveX control containment, with a control of the test's
  own: CWnd::CreateControl in a frame window (properties, methods,
  events and property notifications through event sink maps, ambient
  properties, frozen events, moving, enabling, focus and destroying),
  and a dialog template with controls among ordinary items (made on
  WM_INITDIALOG in the template's tab order, state from DLGINIT after
  its license key, DDX_Control and the DDX_OC functions).
* `exttest`: an extension DLL (`exttest-dll`), with the program and the
  DLL using `mfc.dll`: the DLL's document and view in the program's
  document template, a command routed to the DLL's message map, the
  DLL's serializable class through FromName and CArchive, strings and a
  dialog template from the DLL's resources and the program's from the
  program, AfxGetApp in the DLL, exceptions from one module caught in
  the other, objects and memory freed in the other module, and a
  CStringArray grown in both. It runs only against the shared library,
  as do `core`, `sditest` and `dlgtest` a second time (`SHARED_TESTS`;
  logs in `tests/sa` and `tests/su`).
* `dlltest`: MFC in a DLL: a control in a regular DLL (`dlltest-dll`,
  built like an `.ocx`) registered through DllRegisterServer, made by
  COM from the registry and hosted by the program (properties, methods
  and events across the two copies of the library), the DLL's own
  instance, resources and CWinApp, a dialog from the DLL's template, and
  unloading through DllCanUnloadNow, with the DLL's ExitInstance and
  window classes.
* `ctltest`: writing a control with COleControl, the test being both
  its server and its container: stock and custom properties, a method,
  stock and custom events, verbs from its message map (ON_OLEVERB,
  ON_STDOLEVERB), the font object, ambient properties, frozen
  events, activation and focus, the container's extended control
  (its extender properties, moving and hiding the control through it,
  and the control's own properties and methods), drawing in its window
  and through IViewObject, its state through IPersistStreamInit,
  IPersistPropertyBag and IPersistStorage into new instances, the
  registry keys of AfxOleRegisterControlClass, and the control's
  COlePropertyPage, through IPropertyPage (its size and title, DDP_ and
  DDX values from and to one or two controls, dirty controls, Apply)
  and in the property frame of the Properties verb, which also shows
  the stock color page; the stock color, font and picture pages
  setting the control's colors, font and picture; CPictureHolder (its
  kinds of picture, drawing one, PX_Picture); IPerPropertyBrowsing
  and IQuickActivate; and licensing, through IClassFactory2 with and
  without a license file (AfxVerifyLicFile) and a run-time key, also
  from CreateControl; and downloads of a local file through
  CMonikerFile, CAsyncMonikerFile and a CCachedDataPathProperty loaded
  from a property bag (PX_DataPath).
* `cmntest`: a CReBar with a toolbar, a combo box and a second row in
  its bands (the frame's layout as rows come and go, idle updates of
  the bands' controls), CReBarCtrl, CCheckListBox (item data, clicks
  on the box, the space bar, disabled items, three states), a
  CDragListBox drag, CLinkCtrl (in comctl32 6, from the test's
  manifest) and CPagerCtrl.
* `wltest`: windowless controls, with a COleControl of the test's own
  in a frame window: drawing in the container's window (checked by its
  pixels) and InvalidateControl, mouse messages with capture, focus and
  keys, the control's own GetDC, drag and drop through the drop target
  the container made for the frame (entering, leaving for the frame,
  coming back, the drop and its data), moving, hiding and destroying,
  and the same control made windowed when it does not ask for windowless
  activation.
* `bindtest`: data binding, with a data source control and bound
  controls of the test's own: properties bound to fields follow the
  current row (and null fields and no row), changes to the fields, and
  set the fields, and the other controls bound to them; a property
  given the data source; the data source control's rowset; unbinding;
  destroying the bound controls or the data source control first; a
  data source without a row position; bindings from a dialog
  template's DLGINIT records; an older data source control that makes
  an ICursor, with the same bindings; and ON_DSCNOTIFY, which hears of
  each step of a change and can refuse it.

The GUI tests drive themselves with timers, check what they see, and
save screenshots (PNG when ImageMagick is installed) next to their logs
in `build/<triple>/tests/a` and `tests/u`. CI runs them on every pull
request with the stock MinGW-w64 toolchain.

## What is there

* Core (`afx.h`, `afxcoll.h`, `afxtempl.h`, `afxstr.h`): CObject and
  CRuntimeClass with dynamic creation and serialization, CString
  (CStringA/CStringW), the collection classes and templates, CFile,
  CStdioFile, CMemFile, CArchive, CTime, CTimeSpan, exceptions, and
  the memory diagnostics (CMemoryState, AfxCheckMemory, afxMemDF,
  AfxSetAllocHook, AfxDoForAllObjects and the leak report at exit).
* Windows (`afxwin.h`): CWinApp, CWinThread, CWnd and message maps,
  command routing and idle updates, GDI objects and device contexts,
  standard controls (and CCheckListBox), CDialog with DDX/DDV, frames,
  documents and views (SDI and MDI), CScrollView, printing.
* `afxext.h`: CToolBar, CStatusBar, CDialogBar, docking and floating
  (dock bars, CMiniFrameWnd, drag and drop, CDockState), CSplitterWnd (static and dynamic, with split boxes and shared
  scroll bars), CReBar (toolbars, dialog bars and other windows in
  its bands), CFormView, CEditView, CBitmapButton, CRectTracker.
* `afxrich.h`: CRichEditView and CRichEditDoc (rich text or plain text
  files, formatting the selection, find and replace, printing), with
  OLE objects in the text through CRichEditCntrItem (Insert Object,
  Paste Special, the selected item). CRichEditDoc is a COleServerDoc,
  so a program serves its documents by overriding OnGetEmbeddedItem.
* `afxdb.h`: CDatabase, CRecordset (snapshot, dynaset and forward-only,
  with RFX for the usual types, bookmarks and parameters, output
  parameters of procedures included),
  CRecordView and the DDX_Field functions, CDBException, CDBVariant.
* `afxdao.h`: the DAO classes over the DAO 3.6 engine (or 3.5):
  CDaoWorkspace, CDaoDatabase, CDaoTableDef, CDaoQueryDef,
  CDaoRecordset with the DFX_ functions, CDaoRecordView and the
  DDX_Field functions for DAO, CDaoException and the info structures.
* `afxdisp.h`: automation clients (COleDispatchDriver and the variant,
  date, currency and safe array classes) and servers: dispatch maps on
  any CCmdTarget, interface maps, aggregation, COleObjectFactory with
  registration and licensing, AfxOleLockApp and the /Automation start,
  connection points (connection maps, CConnectionPoint,
  AfxConnectionAdvise) and type libraries (DECLARE_OLETYPELIB,
  IMPLEMENT_OLETYPELIB, AfxOleRegisterTypeLib).
* `afxole.h`: data transfer: COleDataSource (cached and delayed data,
  the clipboard, DoDragDrop), COleDataObject, COleDropSource and
  COleDropTarget, with the CView drag and drop handlers. Containers:
  COleDocument (flat or compound files), COleClientItem (new, pasted,
  dropped, linked, static and cloned objects, verbs, drawing, the
  clipboard), COleStreamFile, CMonikerFile and CAsyncMonikerFile
  (downloads through URL monikers, which need urlmon.dll from Internet
  Explorer 3 or later), AfxOleSetEditMenu. Programs can include
  `afxolecl.rc` for the container messages. Servers: COleServerDoc,
  COleServerItem (drawing, extents, verbs, data, the clipboard),
  COleLinkingDoc (links to a file and to its items through monikers,
  the running object table and IOleItemContainer, Link Source data on
  the clipboard, and links to the objects a container embeds),
  COleTemplateServer (registration, one object per process for SDI),
  CWinApp::RegisterShellFileTypes, and the embedded menus of SetServerInfo, with
  `afxolesv.rc` for the Update and Exit menu text. In-place activation
  on both sides: the container's frame takes the object's toolbars and
  shared menu through COleFrameHook (IOleInPlaceFrame), and the server
  shows its view in a COleIPFrameWnd with a COleResizeBar, from the
  in-place resources of SetServerInfo and CDocTemplate::CreateOleFrame.
* `afxdocob.h`: Active Documents: CDocObjectServer (IOleDocument,
  IOleDocumentView, IOleCommandTarget, IPrint) made by
  COleServerDoc::GetDocObjectServer, CDocObjectServerItem, OLE command
  maps (BEGIN_OLECMD_MAP, ON_OLECMD and the ON_OLECMD_* entries) with
  COleCmdUI, which gives update handlers' state and text to QueryStatus,
  COleDocIPFrameWnd, whose Help popup goes into the container's Help
  menu, and the container's COleDocObjectItem; COleTemplateServer
  registers OAT_DOC_OBJECT_SERVER classes as DocObjects.
* `afxocc.h`: ActiveX control containment after AfxEnableControlContainer:
  COleControlContainer, COleControlSite and COccManager;
  CWnd::CreateControl, InvokeHelper, GetProperty and SetProperty on a
  control's window object, whose window functions (text, styles,
  position, enabling, focus) go to the control; controls in dialog
  templates with their DLGINIT state; event sink maps (ON_EVENT,
  ON_EVENT_RANGE, ON_EVENT_REFLECT, ON_PROPNOTIFY) and the DDX_OC
  functions. Windowless controls (IOleInPlaceSiteWindowless): the
  container draws them in its window's WM_PAINT and passes them mouse,
  keyboard and focus messages. Data binding to OLE DB data source
  controls (DataSource) and to the older ones that make an ICursor
  (IVBDSC; `ocdb.h` declares the cursor interfaces):
  CWnd::BindDefaultProperty binds a control's property to a field of
  the current row, both ways, BindProperty gives a property the data
  source (or the cursor), GetDSCCursor gives its rowset or cursor, the
  dialog editor's bindings in DLGINIT records are made with the dialog,
  and ON_DSCNOTIFY (and ON_DSCNOTIFY_RANGE and _REFLECT) hears of the
  data source control's changes.
  Extended controls (IOleControlSite::GetExtendedControl): the
  extender properties, and the control's own through the same
  IDispatch.
* `afxctl.h`: writing ActiveX controls: COleControl (the OLE control
  interfaces, in-place activation, stock properties, methods and events,
  ambient properties, IViewObject drawing and persistence through
  streams, storages and property bags), COleControlModule, event maps,
  verb maps (ON_OLEVERB, ON_STDOLEVERB), IPerPropertyBrowsing,
  IQuickActivate, GetExtendedControl, windowless activation (the windowlessActivate flag of
  GetControlFlags, IOleInPlaceObjectWindowless and COleControl's
  windowless SetCapture, SetFocus, GetDC and InvalidateControl),
  DECLARE_OLECREATE_EX, DECLARE_OLECTLTYPE,
  DECLARE_PROPPAGEIDS, CFontHolder, CPictureHolder, CPropExchange and
  the PX_ functions (PX_Picture included), AfxOleRegisterControlClass, licensing (IClassFactory2,
  AfxVerifyLicFile), data path properties (CDataPathProperty,
  CCachedDataPathProperty, PX_DataPath), and property pages:
  COlePropertyPage (IPropertyPage2, made from an ordinary dialog
  template), the DDP_ functions, and the stock pages CColorPropPage,
  CFontPropPage and CPicturePropPage.
* `afxodlgs.h`: the OLE dialogs over oledlg: COleInsertDialog,
  COlePasteSpecialDialog, COleLinksDialog, COleUpdateDialog,
  COleChangeIconDialog, COleConvertDialog, COleBusyDialog,
  COleChangeSourceDialog and COlePropertiesDialog (IOleUIObjInfo), and
  the COleDocument Links, Convert and Change Icon commands.
  COleMessageFilter, which AfxOleInit registers for the thread
  (AfxOleGetMessageFilter): BeginBusyState refuses new calls, and a
  refused call of the program's own is retried, then the Server Busy or
  Not Responding dialog lets the user decide.
* `afxdlgs.h`: the common dialogs (file, color, font, print, page setup,
  find/replace) and CPropertySheet/CPropertyPage, wizards included.
* `afxcmn.h`, `afxcview.h`: the common controls (CReBarCtrl,
  CLinkCtrl, CPagerCtrl and CDragListBox among them), CImageList,
  CListView, CTreeView. `afxmt.h`: synchronization objects.
* `afxinet.h`: the internet classes over WinINet: CInternetSession
  (OpenURL, options, cookies, the status callback), CHttpConnection and
  CHttpFile, CFtpConnection, CFtpFileFind, CGopherConnection,
  CGopherFile, CGopherLocator and CGopherFileFind, CInternetFile with
  read and write buffers, CInternetException, AfxParseURL and
  AfxParseURLEx.
* `afxisapi.h`: Internet Server API extensions and filters: CHttpServer
  with parse maps, CHttpServerContext, CHttpFilter,
  CHttpFilterContext, CHtmlStream, and the DLL's exported functions.
* `afxhtml.h`: CHtmlView (the WebBrowser control in a view: navigation,
  the browser's properties and events, IDocHostUIHandler through the
  view's On functions) and CDHtmlDialog (a dialog whose face is an HTML
  page: DHTML event maps, the element functions, the DDX_DHtml
  functions, ActiveX controls in the page and their events,
  window.external) and CMultiPageDHtmlDialog (a page's own event map,
  from BEGIN_EMBED_DHTML_EVENT_MAP and the URL map). HTML editing in
  MSHTML's design mode: CHtmlEditCtrlBase's commands, CHtmlEditCtrl,
  CHtmlEditView with its editing command map
  (BEGIN_DHTMLEDITING_CMDMAP) and CHtmlEditDoc.
  `atlcomcli.h` has the CComPtr, CComQIPtr, CComBSTR and CComVariant
  their members use.

## Differences from Visual C++ MFC

* Message map handler names: Visual C++ 6 code writes plain names
  (`ON_COMMAND(ID_X, OnX)`), later code writes member pointers
  (`ON_COMMAND(ID_X, &CMyView::OnX)`). Standard C++ has no expression
  that accepts both, so the second form needs
  `_AFX_MSGMAP_MEMBER_POINTERS` defined before `afxwin.h`.
* An ISAPI parse map's ITS_ string gives the number of a command's
  arguments; each argument is read as its parameter's own C++ type.
* The memory diagnostics track the blocks DEBUG_NEW makes and, in a
  debug build, CObject-derived objects. They do not track every
  allocation, as Visual C++'s debug heap does. The library replaces the
  global operator delete to free its blocks. When MinGW's libstdc++
  links its own delete first, only objects are tracked
  (`_AfxTracksAllBlocks` says which). A tracked block must be freed by
  the module that made it.
* `min` and `max` are function templates rather than macros (MinGW-w64
  defines the macros for C only). `NOMINMAX` turns them off.
* Floating bars are tool windows, so the system draws their captions,
  and they resize with the system's sizing border.
* Wine's rich edit control draws an
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
  inset, without waiting nScrollDelay. A connection point's cookie is
  the sink's interface pointer.
* Containment does not implement property pages for the container.
  The extended control's own properties are Name, Object, Parent and
  Left, Top, Width, Height, Visible and ID (in the container window's
  pixels, with DISPIDs of this library's, AFX_DISPID_EXT_*); Parent is
  the container window's IDispatch when it has automation. Data binding
  uses an OLE DB data source's default data member (with IRowPosition,
  or else the rowset's first row), and a cursor's current row; a field
  is set as soon as its bound property changes (with a cursor, as an
  update of its own unless one is going on). ON_DSCNOTIFY hears of
  every step of a cursor's changes, but of an OLE DB data source's only
  once something is bound to it, and then only of the steps the rowset
  tells of.
  Windowless controls are drawn after the container's own painting,
  in the order they were made. A drag over one goes to the drop target
  it gives (IOleInPlaceObjectWindowless::GetDropTarget, which
  COleControl answers with GetWindowlessDropTarget); when the
  container's window is no drop target, the container makes it one as
  such a control is activated.
* A regular DLL runs in the program's message loop, so its CWinApp's
  OnIdle and PreTranslateMessage are not called (as with Visual C++
  MFC's static library), and the temporary window objects its copy of
  the library makes are freed when the DLL unloads. A regular DLL
  uses the static library: one built with `_AFXDLL` (sharing
  `mfc.dll`, with `AFX_MANAGE_STATE` switching between its state and
  the program's) is not supported.
* The shared library is one DLL, with the database, OLE and network
  classes that Visual C++ puts in DLLs of their own, so it imports
  `odbc32`, `wininet`, `ws2_32`, `oledlg` and `urlmon` whatever the
  program uses. Each module keeps its own C runtime state where the C
  library is linked statically (picolibc): a `FILE*` or `errno` belongs
  to the module that has it.
* An extension DLL's classes and resources are found while it is
  loaded; AfxTermExtensionModule takes its classes out of the list
  CRuntimeClass::FromName searches. AFX_EXTENSION_MODULE's
  pFirstSharedClass and pFirstSharedFactory stay NULL: the classes and
  OLE factories go into the program's lists as the DLL loads.
* An asynchronous moniker file's bind status callback is not
  aggregated: CreateBindStatusCallback ignores its pUnkControlling.
* The gopher classes call WinINet's gopher functions, which Internet
  Explorer 7 and later removed and Wine does not have, so they are not
  tested; neither is https, which needs a server with a certificate.
  CInternetSession's status callback finds its session through the
  handle's parent (INTERNET_OPTION_PARENT_HANDLE), or is the only
  session with the callback on.
* A DHTML event map holds its own class's entries only; a
  CMultiPageDHtmlDialog uses the class's map and the page's embedded
  map together. A handler gets the element whose event it handles (for
  the document's events, the element where the event happened). A
  DHTML_EVENT_AXCONTROL handler's parameters are read as their C++
  types (as a dispatch map's are), so it must not be overloaded. A URL
  map entry matches the page's whole URL or its last part.
  CHtmlEditView's editing command map is searched before its message
  map, and after the class's own entries CHtmlEditView's (the Edit
  menu's commands) apply. CHtmlEditDoc saves through the page's
  IPersistFile, or else writes its text in UTF-8.
* Wine's OleCreatePropertyFrame (as of Wine 9) never calls
  IPropertyPage::Apply, so under Wine the OK and Apply buttons of a
  control's property frame do not change the control; Windows applies
  the pages.
* COleControl's windowless controls ignore the other GetControlFlags
  flags (flicker-free and unclipped drawing, pointer inactivity). A
  control's binary stream (IPersistStreamInit, IPersistStorage, DLGINIT
  data) has this library's own format, not the one Visual C++ MFC
  writes; property bags hold the same names and values.
* COleDocument saves its items in its own format,
  which is not the one Visual C++ MFC writes. Wine's
  OleCreateStaticFromData makes objects without IOleObject, so static
  items cannot be created under Wine.
* Under Wine, a container's OLERENDER_DRAW cache never takes a picture
  from a local server, so such objects draw blank; OLERENDER_FORMAT
  with CF_METAFILEPICT works. Wine has no REGCLS_SINGLEUSE, so a
  single-instance COleObjectFactory revokes itself once it has made
  its object. Wine's object handler calls InitNew again when it runs
  an object it created, even after a save, so a container should clone
  such an object rather than run it again. Menus and clipboard data do
  not cross processes under Wine, so in-place activation is tested
  with the container and server in one process. There Wine gives the
  container the server's own object rather than OLE's handler, and
  COleClientItem::Draw then draws from the object's metafile.
* The stock property pages are built in memory rather than from
  resources. Each lists its kind of property from the objects' type
  information, or, for an object without any, from the stock names
  (BackColor, ForeColor, BorderColor, FillColor, MaskColor; Font;
  Picture, MouseIcon, Icon) whose values are of the kind. The system
  has its own pages for their CLSIDs; where it has none, as under Wine,
  COleControl::OnProperties registers these for its property frame
  while the frame is up. A container's own property frame gets the
  system's pages only. CPicturePropPage::LoadPictureFile is what Browse
  uses, for programs and tests that pick the file themselves.
* A DocObject container keeps its in-place menu's fourth group (after
  the third separator, usually Help) in the shared menu, and
  COleDocIPFrameWnd puts the object's Help popup, the first popup of its
  in-place menu's third group, at the top of it. The whole Help menu
  then counts as the object's group 5, so its commands, the
  container's included, go to the object's window. Wine's oledlg has no
  Change Source, Object Properties or Server Busy dialogs, so tests call
  the interfaces those dialogs use rather than the dialogs, and Wine
  has no OleCreateLinkToFile.
* A DocObject's IPrint prints its document's first view, as File Print
  does, to the container's printer. GetPageInfo knows the pages only
  when the view's OnPreparePrinting sets the last page. Printing to a
  file takes the file's name from a TYMED_FILE pstgmOptions.
  COleDocObjectItem::DoDefaultPrinting prints the whole object through
  IPrint to the container's printer, or else through its Print command
  (OLECMDID_PRINT).
* The running object table holds a stand-in for a document or a linked
  item, which hands out the object's interfaces, so that the table does
  not keep open a document a link opened; asking it for IUnknown gives
  the stand-in. Wine's composite monikers bind only through the running
  object table, so under Wine a link binds to an item whose document is
  not open only when the item moniker is bound with the file's at its
  left, and Wine's OleCreateLink and OleQueryLinkFromData are stubs
  (CanPasteLink looks for Link Source itself).
* Sockets take IPv6 as later versions of MFC do: make the socket with
  `Socket(nType, lEvent, 0, AF_INET6)`, after which the string forms of
  Bind, Connect and SendTo take IPv6 addresses and host names, and
  GetPeerNameEx, GetSockNameEx, ReceiveFromEx and SendToEx give and
  take either family. Create makes IPv4 sockets. Host names need
  getaddrinfo (Windows XP and later); earlier systems take numeric
  IPv6 addresses only. `CAsyncSocket::FromHandle` returns
  only sockets attached in the calling thread, and NULL for others,
  rather than making a temporary object.
* CRecordset keeps the rows it has fetched and writes changes with
  UPDATE, INSERT and DELETE statements that match the record by the
  values it read, rather than with positioned cursors, so it works with
  any driver. A dynaset therefore behaves like a snapshot: it does not
  see other users' changes until Requery. A stored procedure's output
  and input/output parameters (`outputParam`, `inoutParam`) reach
  their members once FlushResultSet has gone past the last result set,
  or as it opens when the procedure has none.
* The DAO classes drive the engine through its Unicode interfaces in
  ANSI programs too. As with Visual C++, text in their variants is
  VT_BSTRT there (ANSI bytes in the BSTR), so it is converted on the
  way in and out.
* `llvm-rc` (and `llvm-windres`, which uses it) has no `TOOLBAR`
  statement yet; write toolbar resources as raw resource type 241, as
  `tests/sditest.rc` does.

## Plans

Win16 (with win16-sdk), and finishing the portable layer (see Linux and
macOS above), with controls drawn in the classic style, so that MFC
programs build and look the same on macOS and Linux.

## Layout

`include/` has the public headers, `src/` the library, `port/` the
portable Win32 layer (its headers, sources and the generator for its
placeholders), `tests/` the
test programs and `tools/` the generator for the `ON_WM_*` macros and
CWnd's default handlers (`tools/gen-msgmap.py`) and a development helper
that lists declared functions the library does not define yet.

## License

Copyright (C) clang-retro contributors.

clang-retro MFC is free software under the GNU Affero General Public
License, version 3 only (`AGPL-3.0-only`); the full text is in
[LICENSE](LICENSE). If you distribute a program built with it, or let
people use a modified version over a network, you must offer the
program's complete source under the same license.

Each contributor keeps the copyright in their own work and contributes
it under the same license; see [CONTRIBUTING.md](CONTRIBUTING.md).
