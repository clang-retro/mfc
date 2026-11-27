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
`dir/include/mfc`, the libraries to `dir/lib` and the DLLs to `dir/bin`
(for Linux and macOS, see below).

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

A regular DLL can use `mfc.dll` instead (see below): build it with
`-D_AFXDLL -D_USRDLL`, as Visual C++ does. It then shares the library
with the program but keeps a module state of its own (its CWinApp,
instance and resource handles, window classes, window objects and OLE
factories), and each function it exports starts with
`AFX_MANAGE_STATE(AfxGetStaticModuleState())`, which makes that state
the current one until the function returns. The DLL's windows and COM
objects switch to it themselves.

### Linux and macOS

A portable layer in `port/` implements the part of the Win32 API that MFC
uses over POSIX, so the libraries also build for Linux and macOS with
the system's own clang:

```sh
make TARGET=x86_64-linux-gnu STDLIB=          # Linux (glibc, libstdc++)
make TARGET=arm64-apple-macos11 STDLIB=        # macOS
```

Without `BIN`, the build uses `llvm-ar` when it is on the path and the
system's `ar` otherwise.

This adds `libwinport.a`, which programs link after the MFC library:

```sh
clang++ -std=c++17 -Ipath/to/mfc/port/include -Ipath/to/mfc/include app.cpp \
  -Lpath/to/build/x86_64-linux-gnu -lmfc -lwinport -ldl -lpthread -o app
```

On macOS the system libraries are `-framework Cocoa -framework OpenGL -lpthread` instead
of `-ldl -lpthread`.

`make install` (to `/usr/local`, or `PREFIX=dir`; `DESTDIR` works too)
puts the headers in `include/libmfc` (the portable layer's in
`include/libmfc/winport`), the static libraries in `lib`, the shared
ones in `lib/libmfc-shared`, and `res2cpp.py` in `share/libmfc`. It also
installs pkg-config files, `libmfc` and `libmfcu` (Unicode) for the
static libraries and `libmfc-shared` and `libmfcu-shared` for the shared
ones, and a CMake package, `libmfc`. Both find the install from where
they are, so the tree can be moved:

```sh
python3 "$(pkg-config --variable=res2cpp libmfc)" app.rc -o app-res.cpp
clang++ app.cpp app-res.cpp $(pkg-config --cflags --libs libmfc) -o app
```

```cmake
find_package(libmfc REQUIRED)
add_executable(app WIN32 app.cpp)
target_link_libraries(app PRIVATE libmfc::mfc)  # or mfcu, mfc_shared, mfcu_shared
libmfc_add_resources(app app.rc)
```

A program may define its own `WinMain`, as on Windows (declared with C
names in `winbase.h`); otherwise the library's runs `AfxWinMain`.

A program's resources go in as C++: `port/tools/res2cpp.py` compiles the
`.rc` file with `llvm-rc` and writes the result as a source file that
registers it before the program's constructors run (it needs Python 3,
`llvm-rc` and clang):

```sh
python3 path/to/mfc/port/tools/res2cpp.py app.rc -o app-res.cpp
```

MFC's include directory is searched after any given with `-I`.

Screen savers link `libscrnsave.a` (`libscrnsavw.a` for Unicode), which
has the `WinMain` of the SDK's `scrnsave.h`. See
[docs/screensavers.md](docs/screensavers.md).

It is a work in progress. So far the non-window classes, automation, the
registry (kept in `~/.config/libmfc/registry.reg`), resources, sockets
(with `WSAAsyncSelect`), ODBC through the system's driver manager
(unixODBC or iODBC, loaded when first used; `WINPORT_ODBC` names its
library when it is elsewhere),
WinInet's HTTP, HTTPS, FTP and gopher, and COM, both within a process
(in-process servers in a shared library included) and between
processes (local servers COM starts, with IDispatch, OLE documents'
interfaces, type information and monikers crossing), work, and so does the window manager: window
classes and windows, messages and the message loop, timers, hooks,
focus, painting, and drawing in software with device contexts, bitmaps,
DIBs, regions, fills and blits, lines, curves, shapes and paths, and
text. Windows show on Wayland, on X11 or with Cocoa on macOS (Mac OS X
10.4 and later, PowerPC and Intel); `WINPORT_BACKEND` picks one
(`wayland`, `x11`, `cocoa`, or `headless` to keep windows in memory, as
the tests do). A window draws its own frame, the caption's icon
included, so on Wayland it asks the compositor for client-side
decorations (xdg-decoration); its icon goes to the taskbar and the
window switcher (`_NET_WM_ICON` on X11, xdg-toplevel-icon on Wayland,
the Dock on macOS). Wayland, X11 and xkbcommon are loaded when a program
starts, so it runs without them. Text copied and pasted through the
clipboard goes to and from other programs.

Printing and print preview work too. The printers are the system's CUPS
queues and "Save as PDF"; a printer DC draws each page as a picture at
the printer's resolution (300 dots an inch unless the `DEVMODE` asks for
another), and the job becomes a PDF file that goes to CUPS's `lp`, to the
file `DOCINFO` names, or (for "Save as PDF" with no file named) to the
document's name in `~/Documents`. PrintDlg and PageSetupDlg show their
dialogs, with hooks and the usual control IDs from `dlgs.h`, or return
the default printer. `WINPORT_PRINTERS=pdf` leaves CUPS out.

Anonymous pipes (`CreatePipe`, `PeekNamedPipe`) give a child started
with `STARTF_USESTDHANDLES` its standard input and output. Serial ports
work over termios: `COM1` is the device `WINPORT_COM1` names, or else
`/dev/ttyS0` on Linux and the first `/dev/cu.*` device on macOS, and a
device's own path (`/dev/ttyUSB0`) opens as a port too. Its `DCB`,
`COMMTIMEOUTS`, modem lines and `WaitCommEvent` work as on Windows, and
reads, writes and event waits on a port opened with
`FILE_FLAG_OVERLAPPED` run in the background until `GetOverlappedResult`
or the event says they are done.

Sound goes through ALSA on Linux (libasound, loaded when first used;
`WINPORT_AUDIO_DEVICE` names the device) and through audio queues on
macOS. `waveOut` and `waveIn` take any format the built-in ACM driver
reads (PCM of 8 to 32 bits and float, A-law, mu-law and IMA ADPCM, at
any rate and channels) and convert it for the device, with callbacks,
loops, pause, positions and volume. The ACM itself enumerates, suggests
and converts those formats. `mmio` reads and writes RIFF files on disk
and in memory; `PlaySound` plays WAVE files, resources and images; and
`timeSetEvent` timers run on their own threads. `WINPORT_AUDIO=null`
chooses a loopback device that keeps time like a sound card and records
what was played. `Beep` plays its tone the same way.

`GetEnvironmentStrings`, `GetUserName` (from the password database) and
`SHGetFolderPath` work: `CSIDL_APPDATA` and the other folders are the
XDG folders on Linux (`XDG_CONFIG_HOME`, `XDG_DATA_HOME` and the
`user-dirs.dirs` names such as Documents) and the usual folders under
`~/Library` on macOS, and `CSIDL_PROFILE` is the home folder.
`SendInput`, `mouse_event` and `keybd_event` feed the window manager's
input as a real mouse and keyboard would, and mouse messages go to the
window under the pointer, passing through controls that answer
`WM_NCHITTEST` with `HTTRANSPARENT` (such as group boxes).

Text comes from the desktop's input methods as well as from the keys:
an IBus or Fcitx engine through Wayland's text-input protocol or X's
XIM, or macOS's input sources. What one is composing reaches the window
with the focus as `WM_IME_STARTCOMPOSITION`, `WM_IME_COMPOSITION` and
`WM_IME_ENDCOMPOSITION`, with the text from `ImmGetCompositionString`;
`DefWindowProc` shows it in a small window at the caret (or where
`ImmSetCompositionWindow` puts it) and turns the finished text into
`WM_IME_CHAR` and then `WM_CHAR`, so a program that knows nothing of
input methods still gets the characters. The input method's candidates
appear by the caret, or where `ImmSetCandidateWindow` says. Dead keys and
the Compose key work as in the desktop's other programs (xkbcommon's
compose tables on Wayland, Xlib's on X11). A window given no input
context (`ImmAssociateContext` with `NULL`) gets the finished text as
plain characters.

Kernel objects go further than events, mutexes and semaphores: named
ones, shared with the user's other processes through POSIX shared memory
(a mutex whose owner's process ends is abandoned to the next waiter;
waits in other processes look again every few milliseconds), waitable
timers (named ones within the process), APCs and alertable waits
(`QueueUserAPC`, `SleepEx`), I/O completion ports (packets posted, and
the results of overlapped reads and writes on a file associated with
one), and file mappings over `mmap` (a named mapping without a file is
POSIX shared memory, which other processes open by name). Threads can be
held and ended from outside as on Windows: `SuspendThread` stops a
running thread where it is (with Mach on macOS, a signal elsewhere) and
`ResumeThread` lets it go when the count is back to zero, and
`TerminateThread` stops one for good, leaving what it holds held, as
Windows does. On DOS and the classic Mac OS, whose threads take turns,
a thread can be held only before it starts. Services work
too: the service control manager keeps them under
`HKLM\SYSTEM\CurrentControlSet\Services`, `StartService` runs the
program, and its dispatcher takes `ControlService`'s controls over a
local socket; `ReportEvent` writes to the system log. The registry's
file is shared: each change is written at once, under a lock, and a
program reads the file again when another has changed it. The file is
plain text, much as regedit exports it (`"name"="text"`,
`"name"=dword:1`), so a person can edit it. The configuration directory
(`$XDG_CONFIG_HOME/libmfc`, or `~/.config/libmfc`) holds it and the
`.ini` files.

The look can be changed there. The system colors come from
`HKEY_CURRENT_USER\Control Panel\Colors` when a program starts, by
Windows' names for them, and the caption shades from each title color to
its gradient color when `GradientCaptions` is set:

```
[HKEY_CURRENT_USER\Control Panel\Colors]
"ActiveTitle"="0 84 147"
"GradientActiveTitle"="58 166 240"
"ButtonFace"="212 208 200"
[HKEY_CURRENT_USER\Control Panel\Desktop]
"GradientCaptions"=dword:00000001
```

The other names are `Scrollbar`, `Background`, `InactiveTitle`,
`GradientInactiveTitle`, `Menu`, `MenuBar`, `MenuText`, `MenuHilight`,
`Window`, `WindowFrame`, `WindowText`, `TitleText`, `InactiveTitleText`,
`ActiveBorder`, `InactiveBorder`, `AppWorkspace`, `Hilight`,
`HilightText`, `ButtonShadow`, `ButtonHilight`, `ButtonDkShadow`,
`ButtonLight`, `ButtonText`, `ButtonAlternateFace`, `GrayText`,
`InfoText`, `InfoWindow` and `HotTrackingColor`. `SetSysColors` and
`SPI_SETGRADIENTCAPTIONS` change them while the program runs (bit 0x10
of `UserPreferencesMask`, as on Windows, also turns the gradient on).

Sizes and fonts come from `HKEY_CURRENT_USER\Control Panel\Desktop\WindowMetrics`:
`BorderWidth`, `CaptionWidth`, `CaptionHeight`, `SmCaptionWidth`,
`SmCaptionHeight`, `MenuWidth`, `MenuHeight`, `ScrollWidth` and
`ScrollHeight` in pixels at 96 dots per inch (or, as Windows keeps them,
negative twips: `"-270"` is 18 pixels at 96 dots per inch), and `CaptionFont`, `SmCaptionFont`, `MenuFont`,
`StatusFont`, `MessageFont` and `IconFont` as `"face,points"` with
`,bold` or `,italic` after it (or a LOGFONTW as Windows writes it):

```
[HKEY_CURRENT_USER\Control Panel\Desktop\WindowMetrics]
"CaptionHeight"="24"
"CaptionFont"="DejaVu Sans,9,bold"
"MenuFont"="DejaVu Sans,9"
```

`SPI_SETNONCLIENTMETRICS` changes them while the program runs, and
windows that are open take the new frame; with `SPIF_UPDATEINIFILE` it
saves them there too, as `SPI_SETGRADIENTCAPTIONS` does the gradient.

On a HiDPI display a program draws at the display's resolution, as a
DPI-aware program does on Windows: a screen DC's `LOGPIXELSX` is 96 times
the desktop's scale (192 on a Retina display or a Wayland output of scale
2, 144 at 150%), and the system's sizes, fonts, stock fonts, icons, check
boxes and the glyphs of caption buttons, scroll bars and menus grow with
it, so dialogs, laid out in dialog units, keep their proportions. The
scale is the output's on Wayland (fractional ones through xdg-output and
the viewporter), `Xft.dpi` (or `GDK_SCALE`) on X11 and
`backingScaleFactor` on macOS; `LogPixels` (a DWORD) in
`HKEY_CURRENT_USER\Control Panel\Desktop` sets the DPI instead. A
program that is not DPI aware sees 96 dots per inch, and Wayland and
macOS show its windows scaled up as pictures (X11 shows them as they
are). The awareness comes from the compatibility setting for the
program's file (in `HKEY_CURRENT_USER\Software\Microsoft\Windows
NT\CurrentVersion\AppCompatFlags\Layers`, `"~ HIGHDPIAWARE"` or
`"~ DPIUNAWARE"` under the file's path), else its manifest (`dpiAware`,
`dpiAwareness`), else `SetProcessDPIAware`, `SetProcessDpiAwareness` or
`SetProcessDpiAwarenessContext` called before the first window; a
program that says nothing is aware. Per-monitor awareness is taken as
system awareness: the DPI is the same on every screen and does not
change while the program runs.

Each program watches the registry file (with inotify on Linux, kqueue on
macOS), so saving it in an editor, or another program changing these
keys, restyles the programs already running: what changed is applied
as it would be on Windows, colors with `WM_SYSCOLORCHANGE` and sizes and
fonts with `WM_SETTINGCHANGE` and a new frame. A color a program set
itself with `SetSysColors` stays until the file changes that color.

GDI+ is there as far as images go (`CImage` uses it): bitmaps in the
usual pixel formats, locking their bits, and BMP, PNG, JPEG and GIF
files and streams, read with [stb_image](https://github.com/nothings/stb)
and PNG and JPEG written with stb_image_write (public domain or MIT, in
`port/src/third_party/`), BMP and GIF by hand. A GIF is written as one
frame: an indexed bitmap with its own palette, and a true-color one with
its own colors when it has 256 or fewer, else through a color cube
(GDI+ takes every true-color image through its halftone palette);
pixels under half alpha become the transparent color. CryptoAPI has the base and
enhanced providers' hashes (MD4, MD5, SHA-1, SHA-256, SHA-384, SHA-512
and HMAC), RC4 and AES keys (derived, imported or random),
`CryptGenRandom`, and RSA: key pairs generated or imported (1024 bits
unless asked, 384 to 16384), PKCS #1 v1.5 and OAEP encryption, PKCS #1
v1.5 signatures, and `PUBLICKEYBLOB`, `PRIVATEKEYBLOB` and `SIMPLEBLOB`
blobs, in CryptoAPI's byte order. Key containers are directories under
`crypto/user/` in the configuration directory (`crypto/machine/` for
`CRYPT_MACHINE_KEYSET`), one file for each pair, readable by the owner
only and not encrypted; opening one that is not there fails with
`NTE_BAD_KEYSET` until `CRYPT_NEWKEYSET` makes it. A private key blob
exports only in plain, not wrapped in a session key. `shlwapi`'s path
and string functions, access control lists and SDDL work as well.

The shell namespace is the file system: the desktop is the root `/`,
an item ID list holds one item for each component of a path, and shell
folders (`SHGetDesktopFolder`, `IShellFolder`) list, parse, compare and
rename what is in them. There are no shortcuts (`SFGAO_LINK`), since
symbolic links need none. `SHBrowseForFolder` shows a tree of the
folders below its root and answers the `BFFM_` messages. There are no
visual styles: the controls draw the classic look, as Windows does with
styles turned off (scroll bar troughs in a checker, trackbar thumbs with
a point, tabs with cut corners), so `OpenThemeData` opens nothing and
`IsThemeActive` is false. There is one monitor, the screen. OpenGL programs draw through
the OpenGL framework on macOS, and elsewhere through the generic
implementation, a software renderer for OpenGL 1.1 in `libwinport.a`
(see [docs/software-opengl.md](docs/software-opengl.md)).

Layered windows show with their transparency: `SetLayeredWindowAttributes`
with a color key and a constant alpha, and `UpdateLayeredWindow` (and
`UpdateLayeredWindowIndirect`) with a picture's own premultiplied alpha
from a 32-bit DIB section, the constant alpha over it, or a color key.
As on Windows, the mouse goes through a layered window where it is fully
transparent, and everywhere with `WS_EX_TRANSPARENT`; a layered window
shows nothing until it has attributes or a picture; and once
`SetLayeredWindowAttributes` is called, `UpdateLayeredWindow` fails until
`WS_EX_LAYERED` is cleared and set again. A window region from
`SetWindowRgn` cuts a top-level window's shape on the screen the same
way. On X11 a translucent window gets a 32-bit visual when a compositing
manager runs; without one, its color key and region become a shape
(SHAPE extension) and its alpha is not shown. Its input shape follows
the transparent pixels either way. On Wayland it is an ARGB buffer with
an input region, and on macOS a window that is not opaque. The desktop's
DC (`GetDC(NULL)`) reads the screen with translucent windows blended.

A regular DLL builds as a shared library (`-shared`, exporting
`DllMain` and the `.def` file's names; the program links with
`-rdynamic` on Linux, and the library with `-undefined dynamic_lookup`
on macOS), and `LoadLibrary` calls its `DllMain`.

The shared libraries build too (`make all`; `make static` leaves them
out). They go in `build/<target>/shared` under the static libraries'
names, `libmfc.so`, `libmfcu.so` and `libwinport.so` (`.dylib` on
macOS), so a program built with `_AFXDLL` picks them with that
directory first, and links `libmfcs.a` (`libmfcus.a`) for its own part:

```sh
clang++ -std=c++17 -D_AFXDLL -Ipath/to/mfc/port/include -Ipath/to/mfc/include app.cpp \
  -Lpath/to/build/x86_64-linux-gnu/shared -Lpath/to/build/x86_64-linux-gnu \
  -lmfcs -lmfc -lwinport -ldl -lpthread -Wl,-rpath,path/to/build/x86_64-linux-gnu/shared -o app
```

Extension DLLs (`_AFXEXT`) and regular DLLs that share the library work
as on Windows: the program's startup calls the `DllMain` of each
library it links with, and a regular DLL keeps a module state of its
own.

The common controls are all there: besides the bars, list and tree
views, tabs and the rest, the rebar, pager, SysLink, IP address,
ComboBoxEx, animation (uncompressed and RLE8 AVI clips), month calendar
and date and time picker.

HTTPS uses OpenSSL (libssl 3 or 1.1, loaded when first used;
`WINPORT_LIBSSL` names it when it is elsewhere), and on macOS the
system's Secure Transport. The server's certificate is checked as
WinInet checks it, and `INTERNET_OPTION_SECURITY_FLAGS`
(`SECURITY_FLAG_IGNORE_UNKNOWN_CA` and the others) lets a problem with
it go. `PlgBlt` draws through a parallelogram (and a mask) in software.

`WinHelp` (and so MFC's Help commands) shows `.hlp` files in the
layer's own WinHelp viewer, with the Help Topics dialog box, popups,
pictures and macros. Without a `.hlp` file, it opens a document of the
same name as `.html`, `.pdf` or `.txt`. See
[docs/winhelp.md](docs/winhelp.md). The internet security manager has no zone settings:
local files and `res:` are the local machine, a share or a host without
a dot the intranet, the rest the internet, and `SetZoneMapping` adds
mappings for the process.

The functions not written yet are
listed in `port/src/stubs.txt` and fail with `ERROR_CALL_NOT_IMPLEMENTED`;
`WINPORT_DEBUG=1` names each one the first time a program calls it.

Text uses the TrueType and OpenType fonts installed on the system,
read with [stb_truetype](https://github.com/nothings/stb) (public domain
or MIT, in `port/src/third_party/`); Windows names such as MS Sans Serif,
Times New Roman and Courier New map to similar installed families
(Arial, Liberation, DejaVu, Helvetica and so on). `WINPORT_FONTS` lists
the folders to search instead of the system's, separated by colons; set
to `builtin`, text uses only the built-in fallback, the public domain
X11 6x13 bitmap font (`port/tools/gen-builtin-font.py` makes
`port/src/font_builtin.inc` from it). Symbol and Wingdings are symbol
fonts as on Windows: their `A` text is read a byte at a time, and when
the font is not installed each character is drawn as its Unicode
counterpart (the Adobe Symbol mapping, and for Wingdings the characters
Unicode encodes, such as U+2713 for the check mark), from the stand-in
family or a symbol font such as Symbola, Noto Sans Symbols or DejaVu
Sans. A Wingdings character with no clear counterpart draws as its byte.

The ANSI code page there is UTF-8, `WCHAR` is the system's 32-bit
`wchar_t`, and `long` is 64 bits on 64-bit systems while `LONG` and
`DWORD` stay 32 bits. Code that passes a `long*` where the API takes a
`LONG*` needs `LONG`; in dispatch maps `long` remains the type of
`VT_I4`, as MFC documents.

The compiler COM support of `comdef.h` (`_bstr_t`, `_variant_t`,
`_com_ptr_t` with `_COM_SMARTPTR_TYPEDEF` and the system's `IFooPtr`
types, `_com_error`, and the `_com_dispatch_` helpers that `#import`
wrappers call) is in the portable layer's `comdef.h`, `comutil.h` and
`comip.h`, header only: what Windows has in `comsupp.lib` is inline
there, and `_com_raise_error` is weak, so a program can define its own.
As `LONG` is `int` there, a `_variant_t` made from an `int` is `VT_I4`
(not `VT_INT`) and one from an `unsigned int` is `VT_UI4`; a `long` is
`VT_I4` too. A property put of an object tries `DISPATCH_PROPERTYPUTREF`
and then `DISPATCH_PROPERTYPUT`.

### 16-bit Windows

`make TARGET=i8086-pc-win16` builds the core classes and the window
classes (`CWinApp`, the frames, MDI, dialogs, the controls, the GDI
classes, document/view, a drawn toolbar and status bar, OLE automation,
and OLE containers, servers and drag and drop) for 16-bit Windows, in the
large model, with the Insert Object, Paste Special, Links and Server Busy
dialogs, all the common controls, which the library draws itself, and
OLE controls (`COleControl` and `CWnd::CreateControl`) in a program.
See [docs/win16.md](docs/win16.md).

### 32-bit DOS

`make TARGET=i386-pc-msdos BIN=path/to/clang-dos/bin` builds the library
and the portable layer for 32-bit DOS, with the DOS/32A extender in each
program. Windows show on a VESA screen. See [docs/dos.md](docs/dos.md).

### The classic Mac OS

`make TARGET=m68k-apple-macos MACSDK=path/to/mac-sdk` (or
`TARGET=powerpc-apple-macos9`) builds the core classes and the portable
layer for System 7 to Mac OS 9, with the C and C++ libraries of
clang-retro/mac-sdk. The Toolbox shows the windows. See [docs/macos-classic.md](docs/macos-classic.md).

### Mac OS X 10.0 to 10.5

`make TARGET=powerpc-apple-macosx10.4 SDK=path/to/MacOSX10.4u.sdk
LIBCXX=path/to/libcxx-macosx static` builds the library and the portable
layer for PowerPC Macs with Tiger or Leopard. `TARGET=powerpc-apple-macosx10.0`
with a 10.0 sysroot builds for Mac OS X 10.0 and later. Cocoa shows the windows, and
OpenGL programs draw through `OpenGL.framework`. See
[docs/macosx-10.4.md](docs/macosx-10.4.md).
`tools/build-sdk-samples.py` builds samples of a Win32 SDK, such as
MULTIPAD and the OpenGL screen savers. The screen savers also become
`.saver` bundles for System Preferences. On the classic Mac OS, a
screen saver host and After Dark modules start them. See
[docs/sdk-samples.md](docs/sdk-samples.md) and
[docs/screensavers.md](docs/screensavers.md).

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
after the program's own. Its `DllMain` registers it (AppWizard's
`#include <afxdllx.h>` before it is accepted and does nothing more):

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
when there is no display. For a Linux or macOS target it builds `core`,
`memtest`, `wintest`, `disptest`, `isapitest`, `dlgtest`, `sditest`,
`mditest`, `cmntest`, `splittest`, `docktest`, `dbtest`, `rvtest`,
`socktest`, `occtest`, `dlltest`, `inettest`, `oletest`, `iptest`,
`docobtest`, `linktest`, `bindtest`, `richtest`, `richsrvtest`,
`autotest`, `srvtest`, `atlwintest`, `atltest`, `conntest`, `ctltest`,
`wltest`, `dragtest`, `daotest`, `dvtest`, `htmltest`, `atlctltest`,
`atlutiltest`, `cdlgtest`, `vistatest`, `fptest`, `iotest`, `dpitest`, `imetest`, `audiotest`,
`wtltest`, `sstest` and `hlptest` against
the portable layer, and `exttest`, `core`, `sditest`,
`dlgtest` and `dlltest` again with the shared libraries
(`SHARED_TESTS`), and runs them directly; `NATIVE_TESTS` names others to
try.

* `core`: CString, collections, CFile, CArchive and serialization,
  CTime, exceptions and run-time classes, on the console; and threads
  made suspended, held while they run (by themselves too), and
  terminated before they start, while they run and while they are held.
* `memtest`: the memory diagnostics in a debug build: DEBUG_NEW and
  object blocks, CMemoryState (Checkpoint, Difference, DumpStatistics,
  DumpAllObjectsSince) caught through afxDump, guard bytes and
  AfxCheckMemory, AfxIsMemoryBlock, AfxDoForAllObjects, tracking turned
  off, the allocation hook and delayed frees, on the console.
* `wintest`: windows and drawing below the MFC classes: window classes
  and windows, the window tree and coordinates, sending and posting,
  the message loop, timers, messages between threads, hooks, ANSI and
  Unicode window procedures, focus, painting and scrolling, memory DCs,
  monochrome bitmaps, DIB sections, raster operations, regions,
  clipping, SaveDC and mapping modes, lines, shapes, fill modes, flood
  fills, paths, text metrics and extents, TextOut and DrawText; the
  common controls through their messages (status bar, progress,
  trackbar, up-down, header, tabs, hotkey, list and tree views, IP
  address, ComboBoxEx, month calendar, date and time picker, an
  animation from an AVI file) and property sheets; printer
  DCs, a job printed to a file, and the Print and Page Setup dialogs
  driven through their hooks; a window shaped by SetWindowRgn (hit
  testing and clipping) and files dropped on a window (WM_DROPFILES,
  DragQueryFile and DragQueryPoint); a CWnd with a message map; and,
  on Linux and macOS, the monitor, layered windows, PrintWindow, scroll
  bar and cursor information, flat scroll bars, GradientFill, shadowed
  text, glyph indices and fractional widths, a metafile's description,
  themes (absent), the classic look (the checker of a scroll bar's
  trough, a list box's scroll bar shown again, a trackbar's pointed
  thumb) and the OpenGL pixel formats, DECIMAL conversions and comparison,
  and the shell namespace (ID lists, shell folders and their items) with
  SHBrowseForFolder driven through its callback, on the console.
* `disptest`: the automation helpers in `afxdisp.h` (COleDateTime and
  its span, COleCurrency, COleVariant, COleSafeArray) and a
  COleDispatchDriver calling an IDispatch written in the test, on the
  console.
* `autotest`: automation servers: a dispatch map's methods and
  properties called through IDispatch and COleDispatchDriver (argument
  conversion, by-reference arguments, errors, exceptions, the default
  member, derived classes), interface maps, aggregation, a
  COleObjectFactory in the same process and its registry entries, and
  the program started again by COM as a local server (with type
  information from it, natively), on the console.
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
  active mode. On Linux and macOS (Windows' WinINet has had no gopher
  since IE 7, and Wine's is a stub) a gopher server too: locators,
  menus, submenus and searches (CGopherFileFind), text, binary and
  gopher+ files, gopher+ attributes, and gopher URLs.
  And HTTPS against `openssl s_server` with a certificate of its own:
  the unknown authority and the wrong host refused, each let go with its
  security flag, through the API and through CHttpFile.
* `isapitest`: `afxisapi.h`, with an extension and a filter called
  through the functions an ISAPI DLL exports by a pretend server:
  CHtmlStream (growing, attached buffers), a parse map's commands
  (the default command, a base class's map, arguments by position and
  by name with defaults, numbers of each ITS_ type, posted form data
  read in parts, ITS_RAW data, parse errors), the reply's status and
  headers, server variables, and CHttpFilter notifications.
* `htmltest`: `afxhtml.h` with a WebBrowser control of the test's own
  (Wine's needs Gecko to show a page), registered for CLSID_WebBrowser
  in the process. First the internet security manager that decides
  window.external: zones (on the port, which has no zone settings of
  its own: local files and res: are the local machine, a share or a
  dotless host the intranet, the rest the internet), what each zone
  allows, security ids, zone mappings, and a service provider's own
  manager asked first. For CHtmlView: the browser filling the view,
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
  controls, and a property sheet; a Wizard97 wizard (CPropertySheetEx
  with a watermark and header bitmap, CPropertyPageEx with header
  titles); CPrintDialogEx's defaults; CDialogEx's background color and
  tiled bitmap, read back from the screen; and a CTaskDialog that drives
  itself from its timer (command links, radio buttons, the check box,
  the progress bar, a click refused by OnCommandControlClick).
* `sditest`: a single-document program with a toolbar, a status bar and
  an edit view; idle command updates, tooltips, saving and loading.
* `mditest`: a multiple-document program whose child frames are split
  into a form view and a scroll view; several documents, the Window menu
  commands, closing.
* `docktest`: toolbars docked side by side and on each edge, floating,
  dragged with the mouse, double-clicked, resized while floating, and
  the layout saved and loaded with SaveBarState and LoadBarState. With
  clang-retro's compiler its message map names handlers both ways.
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
  field info and errors, and a dynaset seeing another connection's
  changes and deletions where a snapshot does not, on the console.
* `odbctest` (Linux and macOS): the same classes and the wide ODBC
  functions against a real driver, SQLite 3 through unixODBC, with text
  outside ASCII going in and coming back: connecting, creating, inserting
  with parameters, reading wide text in pieces, a filtered and sorted
  recordset with a parameter, a transaction rolled back, and the
  driver's errors. Without the driver manager or the driver it notes
  that and passes, unless `MFC_TEST_ODBC=1`; `MFC_TEST_ODBC_DRIVER`
  names the driver's library where none is registered.
* `rvtest`: a CRecordView over the same driver; the form showing the
  current record, edits written back when moving, nulls shown as empty
  fields and indeterminate check boxes, and the record commands enabled
  at each end.
* `atldbtest`: an OLE DB provider made with `atldb.h`, read and changed
  through the consumer templates: its properties before and after
  initializing, a table and a command over its column map, nulls, a
  string too long for its column, a bad ordinal, rows changed, inserted
  and deleted, and its TABLES schema rowset with restrictions.
* `oledbtest`: the OLE DB consumer templates against an OLE DB provider
  written in the test: data sources opened by class ID and ProgID (and a
  password refused), tables and commands through column maps, accessor
  maps, parameter maps and the dynamic, string and manual accessors,
  rows changed, inserted and deleted, a transaction aborted and one
  committed, the provider's error records, and every object, row and
  accessor released at the end; then a COleDBRecordView moving through
  the rows and writing the form back. Windows only.
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
* `cdlgtest`: the common dialogs, driven by their hooks: CFileDialog
  opening a file (its hook turning the first OK down), two files at
  once, and none, saving with the default extension, and a buffer too
  small for the answer; CColorDialog with a color typed in its
  fields; CFontDialog starting from a LOGFONT; and the modeless
  CFindReplaceDialog telling its owner about Find Next, Replace, Replace
  All and its closing.
* `vistatest`: the Vista pieces: a CSplitButton dropping down its menu
  (a resource menu, then the program's own) with the chosen command
  reaching the parent, the split style, size and glyph; a command link's
  note, the ideal size, text margin and image list; CNetAddressCtrl
  where shell32 (or the portable layer) has it; and the Vista-style
  CFileDialog driven by a
  timer: opening a file with a check box and edit box added (the first
  OK turned down), two files at once, saving with the default
  extension, cancelling, and CFolderPickerDialog. On XP the test checks
  the classic fallback instead.
* `iotest`: pipes and serial ports, on the console: CreatePipe and
  PeekNamedPipe (peeked data still read, a broken pipe each way), a
  child (the test itself) with pipes for its standard handles, polled
  and read, and BuildCommDCB; on Linux and macOS also a serial port (a
  pseudo-terminal named as COM9): its DCB and properties, time-outs that
  return at once, wait, or end when the line goes quiet, a command out
  and a reply in, and WaitCommEvent overlapped, ended by data, by
  SetCommMask and by CancelIo.
* `imetest` (Linux and macOS only): input methods, with the text handed
  in as a backend would: a composition begun, changed, finished, taken
  back and ended by ImmNotifyIME; ImmGetCompositionString's strings,
  attributes, clauses and cursor (in bytes for ANSI); DefWindowProc's
  composition window and its WM_IME_CHAR and WM_CHAR (UTF-8 bytes for an
  ANSI window); a window with no input context or a closed one getting
  plain characters; a context of the program's own; the composition and
  candidate windows' places and the caret as where candidates go; and
  the composition font.
* `audiotest`: voice audio, on the console: ACM format details,
  enumeration and suggestion, and streams through mu-law, IMA ADPCM and
  a rate change; a WAVE file written and read through mmio, its buffer
  used directly, and a file in memory; periodic and one-shot timers;
  and, where there is a sound device, a mu-law tone played while
  recording (checked against what was recorded on the loopback device),
  positions, pause, reset, a loop, a recording stopped early, and
  PlaySound.
* `oletest`: an OLE container: objects of a class the test registers
  itself, embedded in a COleDocument, drawn by its view, opened and
  changed through their verbs and the verb menu, saved to flat and
  compound files and loaded again (Save As included), deleted, copied
  through the clipboard and cloned.
* `srvtest`: an OLE server and its container in one program, which COM
  starts again as the server: registration, an embedded object made,
  shown, changed, updated and closed through COleServerDoc and
  COleServerItem, the server's embedded menu and data, the object
  loaded again by a second server process, and its clipboard copy;
  natively also a server's object held directly, its advise
  connections enumerated and its moniker asked for across processes.
* `linktest`: links to a server's files and their items, bound as a
  container's link binds them: to an open document through the running
  object table, and to a closed file whose class the shell registration
  gives, which COM opens hidden and closes when the link lets go. It
  checks the item's moniker, data and notifications, the item container,
  the Link Source data Paste Link uses, links to an object embedded
  in a container's file, the object information the Object Properties
  and Change Source dialogs show, and COleMessageFilter's answers. Then
  the objects a container makes from a file: a link, which keeps its
  picture without the file open and is saved, loaded and bound again; a
  copy embedded from the file; a link from Link Source data; and a
  storage converted to the class the registry's AutoConvertTo names.
  And the OLE dialogs themselves, answered by their hooks: Server Busy,
  Convert, Change Icon, Change Source, Object Properties (its View page
  applying a new scale) and the progress of Update Links.
* `iptest`: in-place activation, with the container and the server in
  one program: the object activated in the container's view, the
  shared menu and its commands (each goes to the side whose popup it
  is on, through the menu descriptor), both sides' accelerators
  (IsAccelerator reading the container's), the server's toolbar in the
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
  as do `core`, `sditest`, `dlgtest` and `dlltest` a second time
  (`SHARED_TESTS`; logs in `tests/sa` and `tests/su`).
* `dlltest`: MFC in a DLL: a control in a regular DLL (`dlltest-dll`,
  built like an `.ocx`) registered through DllRegisterServer, made by
  COM from the registry and hosted by the program (properties, methods
  and events across the two modules), the DLL's own instance, resources
  and CWinApp, a dialog from the DLL's template whose messages run in the
  DLL's state, and unloading through DllCanUnloadNow, with the DLL's
  ExitInstance and window classes. It runs with the static library and
  again with `mfc.dll` (`_AFXDLL` and `_USRDLL`), where AFX_MANAGE_STATE
  switches between the two modules' states.
* `atltest`: a COM server written with ATL (`atltest-dll`), an ATL 3
  one (CComModule and an object map) in the ANSI build and an ATL 7 one
  (CAtlDllModuleT and OBJECT_ENTRY_AUTO) in the Unicode build:
  registration from an `.rgs` script with replacements and from the old
  DECLARE_REGISTRY, CoCreateInstance through the class factories, calls
  through the vtable and through IDispatchImpl over a type library the
  test writes and registers, Error and ISupportErrorInfo (also through
  IDispatch, as an exception), events to a sink made with CComObject
  through the connection point (Advise, Unadvise, the enumerators),
  IProvideClassInfo2, a tear-off and a cached tear-off, aggregation, a
  singleton class factory, unloading through DllCanUnloadNow and
  unregistration; the client's own CComObject, CComObjectStack and
  CComEnum; objects made in a CAtlAutoThreadModule's threads by
  CComClassFactoryAutoThread, CComGITPtr from another thread, a service
  map, ICollectionOnSTLImpl, the free-threaded marshaler, and the
  registrar as IRegistrar, per machine and per user.
* `atlwintest`: ATL's windows without MFC: a CWindowImpl frame with its
  own window class and traits, its message map (MESSAGE_, COMMAND_ and
  NOTIFY_ handlers, a range, bHandled set to FALSE, a chained map,
  GetCurrentMessage) and OnFinalMessage; a CContainedWindow edit control
  on an alternate map of its parent; a superclassed button whose
  notifications its parent reflects; SubclassWindow and
  UnsubclassWindow; CWindow's wrappers; CDialogImpl, modal and
  modeless, and CSimpleDialog.
* `atlctltest`: ATL's controls and their host, without MFC: a windowed
  control (CComControl) with stock and custom properties
  (CStockPropImpl), a property map saved to and loaded from a stream, a
  storage and a property bag, events through the connection point and
  IPropertyNotifySink, IViewObjectEx, IOleObject and IQuickActivate; a
  windowless one; a licensed one (CComClassFactory2); a composite
  control (CComCompositeControl) and a property page
  (IPropertyPageImpl). CAxWindow and AtlAxWin making and attaching
  controls by CLSID and ProgID; event sinks (IDispEventImpl over the
  type library, IDispEventSimpleImpl with _ATL_FUNC_INFO, a sink map
  advised with AtlAdviseSinkMap); and CAxDialogImpl over DIALOG and
  DIALOGEX templates that name controls by CLSID, their saved state
  (and a license key) from DLGINIT, as the dialog editor writes them.
* `atlnettest`: atlsocket.h, atlmime.h, atlsmtpconnection.h and
  atlhttp.h against an HTTP and an SMTP server of the test's own on the
  loopback address: messages and their encodings, mail sent with a
  dot-stuffed line and a refused recipient, and requests with each kind
  of body, POST, redirects, Basic authentication and a proxy.
* `atlsnaptest`: an MMC snap-in made with `atlsnap.h` against a console
  of the test's own: its nodes' data object formats and display info,
  notifications, the context menu from a menu resource and its commands,
  a toolbar from a TOOLBAR resource with button states, a property page
  applied with OK, and an extension snap-in's node map.
* `atlperftest`: performance counters made with `atlperf.h`, registered
  (the service's Performance key and the Perflib names, or only their
  indexes where Perflib's text is not in the registry), published from
  instances in shared memory and collected as the performance DLL would:
  all, by index, too small a buffer, instances released, and
  unregistered.
* `comdeftest` (Windows, Linux and macOS): `comdef.h`'s `_bstr_t`,
  `_variant_t`, `_com_ptr_t` and `_com_error`, and the dispatch helpers
  against an IDispatch of the test's own (methods with values,
  `VARIANT`s and references, properties by value and by reference, and
  a dispatch exception raised and left for GetErrorInfo); on Windows
  against MinGW's headers and comsupp library, elsewhere the portable
  layer's.
* `fptest` (Windows, Linux and macOS): the Feature Pack's base:
  CSettingsStore's values written and read back by type, CWinAppEx's
  settings and managers, each visual manager in turn; CMFCButton as a
  push button (sized to its text and image), check box and radio group;
  the link; the menu button's menu and the color button's palette
  driven from the keyboard; the browse button's frame space and click;
  masked input (refused characters, backspace, delete, the whole text or
  the typed characters); the font combo box's fonts; the spin button.
* `atlutiltest`: ATL's utility headers in a program without MFC: CString
  from `atlstr.h` (formatting, string resources, conversions, errors as
  CAtlException); CSimpleArray and CSimpleMap; CAtlArray, CAtlList,
  CAtlMap (rehashing as it grows), CRBMap and CRBMultiMap, and the arrays
  and lists of CAutoPtr and interface pointers; CHeapPtr, CComHeapPtr,
  CTempBuffer, CAutoPtr, CHandle; AtlThrow; the heaps of `atlmem.h`,
  CAtlStringMgr, CFixedStringT, AtlGetStringResourceImage and
  CComCurrency (its arithmetic, rounding and overflow); CComSafeArray of numbers,
  BSTRs and VARIANTs, in one and two dimensions; CComDispatchDriver;
  CAtlFile, CAtlTemporaryFile and CAtlFileMapping; CTime, CTimeSpan,
  CFileTime and CFileTimeSpan without MFC; CImage (a 32-bit image with
  alpha through PNG and back and drawn with AlphaBlend, 24 bits through
  BMP, JPEG and a stream, an 8-bit image's color table, SetPixelIndexed
  and a transparent color, a device-dependent bitmap saved, and the file
  dialogs' filter strings); COleDateTime and COleDateTimeSpan; the
  synchronization objects of `atlsync.h` across a second thread;
  Base64, hex, UUEncode, quoted-printable, the B encoding, EscapeXML and
  UTF-8; MD5 and SHA-1 of a known string, and RC4 through a key derived
  from a hash; CSocketAddr; CAtlRegExp (groups, anchors, alternation,
  lazy repeats, negation, abbreviations and the parse errors); CUrl
  cracked and rebuilt, escaping, AtlCanonicalizeUrl and AtlCombineUrl;
  a CWorkerThread with an event and a timer, and a CThreadPool summing
  its requests; CPath; CSid and the
  well-known SIDs, CDacl, CSecurityDesc to SDDL and back, an event created
  through CSecurityAttributes and its DACL read and replaced, a file's
  owner and DACL, and the process's CAccessToken; and CAtlServiceModuleT,
  installed with /Service, started and stopped by the service control
  manager, and removed with /UnregServer.
* `wtltest`: WTL 10 over this ATL, on Windows and natively, built when
  `tools/fetch-wtl.sh` has fetched WTL's headers (skipped otherwise): a CFrameWindowImpl frame
  with CUpdateUI, a message filter and an idle handler, a command bar,
  a toolbar from a toolbar resource in a rebar, a status bar, and a
  splitter of a tree view and a list view; the message cracker maps
  (`atlcrack.h`); a dialog with DDX and CDialogResize; a dialog from
  CIndirectDialogImpl's in-memory template; a modeless property sheet;
  a CScrollWindowImpl view; and WTL's GDI and USER wrappers, CResource
  and CFindFile.
* `hlptest`: the WinHelp viewer, with three help files in
  `tests/winhelp`: the text, tables, pictures, hotspots, popups,
  macros, keywords, `[MAP]` numbers and the Help Topics dialog box.
  See [docs/winhelp.md](docs/winhelp.md).
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
  and in the property frame of the Properties verb or of
  COleControlSite::ShowPropertyFrame, which also shows the stock color
  page; IViewObjectEx hit testing, drawing clipped to the control's
  bounds or with the DC left as it is (the clipPaintDC and
  canOptimizeDraw flags), and IPointerInactive for a control with the
  pointerInactive flag; the stock color, font and picture pages
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
  CStdioFile, CMemFile, CArchive, CTime and CTimeSpan (from
  `atltime.h`, as in MFC 7, with CFileTime and CFileTimeSpan), exceptions, and
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
* `afxoledb.h`: COleDBRecordView, a form view over a CRowset from the
  OLE DB consumer templates (`atldbcli.h`) that moves with the record
  commands and writes the form back to the row. Windows only.
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
  AfxOleRegisterServerClass, AfxOleUnregisterServerClass and
  AfxOleRegisterHelper (registry entries with %1 to %9 substitutions),
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
  IQuickActivate, GetExtendedControl, the GetControlFlags flags
  (windowlessActivate, clipPaintDC, canOptimizeDraw, noFlickerActivate,
  pointerInactive), IViewObjectEx, IPointerInactive, windowless
  activation (IOleInPlaceObjectWindowless and COleControl's windowless
  SetCapture, SetFocus, GetDC and InvalidateControl),
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
  find/replace), CPrintDialogEx (PrintDlgEx), and
  CPropertySheet/CPropertyPage, wizards included; Wizard97's watermark,
  header bitmap and header titles, through CPropertySheetEx and
  CPropertyPageEx or MFC 7's constructors on the base classes.
  CFileDialog in the Vista style (IFileOpenDialog / IFileSaveDialog,
  with its events going to the same virtuals and the IFileDialogCustomize
  calls as members), and CFolderPickerDialog.
* `afxdialogex.h`: CDialogEx, with a background color or bitmap.
  `afxtaskdialog.h` (from `afxwin.h`): CTaskDialog over comctl32 6's
  TaskDialogIndirect, found when it runs, so the program still starts
  on Windows 2000 and XP; there (CTaskDialog::IsSupported is FALSE)
  DoModal shows the text in a message box with the common buttons.
* `afxcmn.h`, `afxcview.h`: the common controls (CReBarCtrl,
  CLinkCtrl, CPagerCtrl and CDragListBox among them), CImageList,
  CListView, CTreeView. CButton has the comctl32 6 and Vista messages
  (command link notes, split button parts, the shield), CSplitButton
  drops down a menu, and CNetAddressCtrl wraps shell32's network address
  control. `afxmt.h`: synchronization objects.
* The Feature Pack's base (MFC 9), so far: CWinAppEx (its settings
  under `Software\<key>\<application>\Workspace`, the window
  placement, and the context menu, keyboard, mouse, shell and tooltip
  managers), CSettingsStore and CSettingsStoreSP (numbers, strings and
  serialized arrays, lists, maps and objects in the registry),
  `afxGlobalData`, and the visual managers (classic, Office XP, Windows,
  Office 2003, VS 2005, Office 2007 in its four styles, VS 2008 and
  Windows 7) that draw the standalone controls: CMFCButton (flat or 3D,
  images for each state, check boxes and radio groups it keeps itself),
  CMFCLinkCtrl, CMFCMenuButton, CMFCColorButton and its palette,
  CMFCEditBrowseCtrl (a file, folder or custom browse button in the
  edit's frame), CMFCMaskedEdit, CMFCFontComboBox and
  CMFCSpinButtonCtrl. `afxcontrolbars.h` includes them all.
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
* ATL, for COM servers and objects (`atlbase.h`, `atlcom.h`): CComModule
  (ATL 3, with an object map) and CAtlDllModuleT and CAtlExeModuleT
  (ATL 7, with OBJECT_ENTRY_AUTO), CAtlComModule; the registrar that
  runs `.rgs` scripts from REGISTRY resources, files and strings (with
  %MODULE% and replacement maps), also as IRegistrar, and per user with
  AtlSetPerUserRegistration (HKCR as `HKCU\Software\Classes`, type
  libraries with RegisterTypeLibForUser); CRegKey; CComAutoThreadModule
  and CAtlAutoThreadModule, whose threads make the objects of
  DECLARE_CLASSFACTORY_AUTO_THREAD, with CComApartment and
  CComSimpleThreadAllocator; CComGITPtr over the global interface table; CComObjectRootEx and the threading models; CComObject,
  CComObjectNoLock, CComObjectCached, CComObjectGlobal,
  CComObjectStack, CComAggObject, CComPolyObject, CComContainedObject,
  tear-offs and cached tear-offs; BEGIN_COM_MAP with the
  COM_INTERFACE_ENTRY forms (2, IID, IMPL, FUNC, TEAR_OFF,
  CACHED_TEAR_OFF, AGGREGATE, AUTOAGGREGATE, CHAIN, NOINTERFACE, the
  blind ones); the creators and the DECLARE_*AGGREGATABLE macros;
  CComClassFactory, CComClassFactory2 (licensing) and the singleton
  factory; CComCoClass with Error and CreateInstance; the
  DECLARE_REGISTRY forms and the category map; IDispatchImpl over a
  registered type library (or the module's own), ISupportErrorInfoImpl
  and AtlReportError, IProvideClassInfoImpl and
  IProvideClassInfo2Impl; IServiceProviderImpl and the service map
  (SERVICE_ENTRY, SERVICE_ENTRY_CHAIN); _ATL_DEBUG_QI, which writes each
  QueryInterface to the debugger; IConnectionPointImpl,
  IConnectionPointContainerImpl and the connection point map,
  CComDynamicUnkArray and CComUnkArray; CComEnum and CComEnumOnSTL with
  the _Copy policies, and ICollectionOnSTLImpl; IObjectWithSiteImpl and IObjectSafetyImpl.
  `atlimpl.cpp` is there, empty, for the ATL 3 programs that include it.
* ATL's windows (`atlwin.h`): CWindow and its wrappers; message maps
  (BEGIN_MSG_MAP, ALT_MSG_MAP, the MESSAGE_, COMMAND_ and NOTIFY_
  handlers and their ranges, CHAIN_MSG_MAP and its forms, the
  REFLECTED_ handlers, REFLECT_NOTIFICATIONS, FORWARD_NOTIFICATIONS,
  DEFAULT_REFLECTION_HANDLER); CWindowImpl over its own window class
  (DECLARE_WND_CLASS, DECLARE_WND_CLASS_EX) or a superclass
  (DECLARE_WND_SUPERCLASS), with the window procedure thunks of x86 and
  x86-64, SubclassWindow and OnFinalMessage; the CWinTraits styles;
  CContainedWindow; CDialogImpl and CSimpleDialog.
* ATL's utilities: `atlstr.h` (CString, CStringA and CStringW for
  programs without MFC, and CAtlString for any; the same CStringT as
  MFC's, over string traits of ATL's own); `atlsimpcoll.h` (CSimpleArray,
  CSimpleValArray, CSimpleMap) and `atlcoll.h` (CAtlArray, CAtlList,
  CAtlMap, CRBMap, CRBMultiMap, CAutoPtrArray, CAutoPtrList,
  CInterfaceArray, CInterfaceList and the element traits, CElementTraits
  and CStringElementTraits among them); `atlalloc.h` (the allocators,
  CHeapPtr, CComHeapPtr, CTempBuffer, CAutoPtr, CAutoVectorPtr, CHandle,
  AtlAdd and AtlMultiply); `atlmem.h` (IAtlMemMgr, CWin32Heap,
  CComHeap, CCRTHeap, CLocalHeap, CGlobalHeap); CAtlStringMgr and
  CFixedStringT (the strings accept a string manager and keep their
  own buffers); `atlcur.h` (CComCurrency); AtlGetStringResourceImage,
  AtlCreateTargetDC, AtlDevModeW2A, AtlComPtrAssign and
  AtlComQIPtrAssign; `atlexcept.h` (CAtlException and AtlThrow,
  which throws MFC's exceptions when MFC is in use); `atlsafe.h`
  (CComSafeArray, CComSafeArrayBound); `atlfile.h` (CAtlFile,
  CAtlTemporaryFile, CAtlFileMapping); CComPtr<IDispatch>'s helpers and
  CComDispatchDriver; `atltime.h` (CTime, CTimeSpan, CFileTime,
  CFileTimeSpan); `atlcomtime.h` (COleDateTime and COleDateTimeSpan,
  which MFC's `afxdisp.h` takes from here); `atlsync.h`
  (CCriticalSection, CEvent, CMutex, CSemaphore and CMutexLock, in
  namespace ATL only, as MFC has its own of the same names); `atlenc.h`
  (Base64, UUEncode, quoted-printable, hex, BEncode, EscapeXML and
  AtlUnicodeToUTF8); `atlcrypt.h` (CCryptProv, CCryptHash and the MD5,
  SHA and HMAC hashes, CCryptKey and the derived, random, imported and
  user keys); `atlsocket.h` (CSocketAddr over getaddrinfo, and
  ZEvtSyncSocket, a stream socket with timeouts); `atlmime.h`
  (CMimeMessage, its headers, text, attachments from files and memory in
  base64, uuencoding or quoted-printable, and attached messages;
  non-ASCII headers "B" encoded); `atlsmtpconnection.h` (CSMTPConnection:
  SendMessage, SendRaw and SendSimple, "host:port" taken too);
  `atlhttp.h` (CAtlHttpClient and CAtlNavigateData: kept connections,
  bodies with a length, chunked or to the close, POST in blocks or
  chunked, redirects, a proxy, and CBasicAuthObject; no NTLM); `atlrx.h`
  (CAtlRegExp and CAtlREMatchContext, ATL's syntax with {} for match
  groups); `atlutil.h` (CUrl, AtlEscapeUrl, AtlUnescapeUrl,
  AtlCanonicalizeUrl, AtlCombineUrl, AtlGetErrorDescription, the thread
  traits, CWorkerThread and CThreadPool); `atldbcli.h` (the OLE DB
  consumer templates: CDataSource, CSession with transactions, CTable,
  CCommand with parameters and multiple results, CAccessor over
  BEGIN_COLUMN_MAP, BEGIN_ACCESSOR_MAP and BEGIN_PARAM_MAP classes with
  DEFINE_COMMAND, CDynamicAccessor, CDynamicStringAccessor,
  CManualAccessor, CNoAccessor, CRowset and CNoRowset, CDBPropSet,
  CDBPropIDSet and CDBErrorInfo; Windows only); `atldb.h` (the OLE DB
  provider templates: BEGIN_PROVIDER_COLUMN_MAP and PROVIDER_COLUMN_ENTRY,
  BEGIN_PROPSET_MAP and CUtlProps, IDBInitializeImpl, IDBPropertiesImpl,
  IDBCreateSessionImpl, IOpenRowsetImpl, IDBCreateCommandImpl,
  ICommandTextImpl, ICommandPropertiesImpl, IAccessorImpl,
  IColumnsInfoImpl, IConvertTypeImpl, IRowsetImpl, IRowsetChangeImpl,
  IRowsetIdentityImpl, IRowsetInfoImpl, CRowsetImpl, and schema rowsets
  over BEGIN_SCHEMA_MAP with CSchemaRowsetImpl and the TABLES, COLUMNS and
  PROVIDER_TYPES rows; Windows only); `atlsnap.h` (MMC snap-ins:
  CSnapInItemImpl with menus from SNAPINMENUID, toolbars from
  BEGIN_SNAPINTOOLBARID_MAP, commands from BEGIN_SNAPINCOMMAND_MAP,
  IComponentDataImpl, IComponentImpl, IExtendContextMenuImpl,
  IExtendPropertySheetImpl, IExtendControlbarImpl, extension snap-ins
  over BEGIN_EXTENSION_SNAPIN_NODEINFO_MAP and CSnapInPropertyPageImpl;
  Windows only); `atlperf.h` (performance counters: CPerfObject
  categories with DECLARE_PERF_CATEGORY and BEGIN_COUNTER_MAP, CPerfMon
  with BEGIN_PERF_MAP, instances by number or name, CPerfLock,
  Register through Perflib or lodctr, Collect, and PERFREG_ENTRY with
  the OpenPerfMon, CollectPerfMon and ClosePerfMon exports; Windows
  only); `atlimage.h` (CImage: DIB sections made, attached and
  drawn with BitBlt, StretchBlt, AlphaBlend, TransparentBlt, MaskBlt and
  PlgBlt, and BMP, GIF, JPEG, PNG and TIFF read and written through
  GDI+, which starts on first use; an image with an alpha channel keeps
  it unpremultiplied, as Visual C++'s does); `atlpath.h` (CPath, CPathA, CPathW and the
  ATLPath functions over shlwapi); `atlsecurity.h` (CSid and Sids, CAcl,
  CDacl, CSacl, CSecurityDesc, CSecurityAttributes, CAccessToken, and
  AtlGetDacl, AtlSetDacl, AtlGetOwnerSid and AtlSetOwnerSid); and
  CAtlServiceModuleT, a service that is also a COM server.
* ATL's controls (`atlctl.h`): CComControl and CComControlBase
  (windowed and windowless, in-place activation, drawing through
  OnDraw, ambient properties, FireOnChanged and FireOnRequestEdit,
  DoVerbProperties); IOleObjectImpl, IOleInPlaceObjectWindowlessImpl,
  IOleInPlaceActiveObjectImpl, IOleControlImpl, IViewObjectExImpl,
  IQuickActivateImpl, IDataObjectImpl, IRunnableObjectImpl,
  IPointerInactiveImpl, IPerPropertyBrowsingImpl,
  ISpecifyPropertyPagesImpl and IPropertyNotifySinkCP; CStockPropImpl
  (the stock properties the class has members for); IPropertyPageImpl and IPropertyPage2Impl;
  CComCompositeControl. The property map (BEGIN_PROP_MAP, PROP_ENTRY,
  PROP_ENTRY_EX, PROP_DATA_ENTRY, PROP_PAGE) behind IPersistStreamInitImpl,
  IPersistStorageImpl and IPersistPropertyBagImpl, in ATL's stream
  format.
* ATL's event sinks (`atlcom.h`): IDispEventImpl (the handlers' types
  from the type library) and IDispEventSimpleImpl (from _ATL_FUNC_INFO),
  BEGIN_SINK_MAP with SINK_ENTRY, SINK_ENTRY_EX and SINK_ENTRY_INFO,
  AtlAdviseSinkMap. Handlers are named `&CMyDlg::OnClick`; with
  clang-retro's compiler also by plain name, as Visual C++ 6 code does.
* ATL's control host (`atlhost.h`, `atliface.h`): the AtlAxWin window
  classes (and AtlAxWinLic, and the versioned names, AtlAxWin71 to
  AtlAxWin140), which make a control from a CLSID, a ProgID, a URL (the
  WebBrowser control) or MSHTML: text; IAxWinHostWindow and
  IAxWinAmbientDispatch; CAxWindow and CAxWindow2; AtlAxCreateControl
  and its forms, AtlAxAttachControl, AtlAxGetHost and AtlAxGetControl;
  AtlAxDialogBox, AtlAxCreateDialog and CAxDialogImpl, which turn a
  dialog's ActiveX items into host windows and give them their DLGINIT
  state; CWindow::GetDlgControl and GetDlgHost.

### WTL

WTL (the Windows Template Library, under the MS-PL) builds on this ATL.
It is not part of this repository: `tools/fetch-wtl.sh [dir]` downloads
WTL 10.01 from NuGet, checks it against a pinned SHA-256, and puts its
headers in `dir/include` (`build/wtl/include` by default). WTL wants
what Visual C++ gives it:

* `-fms-compatibility` (WTL uses `__if_exists` and Visual C++'s array
  rules). On a MinGW target, stock clang then stops defining `__GNUC__`,
  which MinGW-w64's headers need, so add `-fgnuc-version=4.2.1` too.
  WTL programs are built with both:
  `-fms-compatibility -fgnuc-version=4.2.1`.
* `_MSC_VER` of at least 1400 while `atlapp.h` is read, and only then:
  defined for the whole program, it would change what libc++ and the C
  headers do. `tests/wtltest.cpp` shows the way:
  `#define _MSC_VER 1400`, `#include <atlapp.h>`, `#undef _MSC_VER`.
* `WINVER` and `_WIN32_WINNT` 0x0501 and `_WIN32_IE` 0x0600 or more.
* `atlstr.h` included before WTL's headers, for the CString overloads.

On Linux and macOS, WTL builds with `-fms-compatibility
-fms-compatibility-version=19.29`, and `res2cpp.py` needs WTL's include
directory (`-I build/wtl/include`) for resources that include its
headers. A dialog template WTL builds in memory (CIndirectDialogImpl)
writes its text as `WCHAR`, which is 32 bits there; the dialog functions
read such a template too.

What WTL needs from ATL is here: `_ATL_VER` is 0x0800 (ATL 8, Visual
C++ 2005, unless a program defines another), `ATL::Checked` (the
`_s` string functions, made to throw as ATL's do, in `atlchecked.h`),
ATLVERIFY, ATLENSURE and ATLTRY, the trace categories (CTraceCategory,
`atlTraceUI` and the others), `_AtlWinModule` in `atlbase.h`,
`_DialogSplitHelper`, and `atlhost.h` from `atlwin.h` (define
`_ATL_NO_HOSTING` to leave it out), as in ATL.

## Differences from Visual C++ MFC

* Message map handler names: Visual C++ 6 code writes plain names
  (`ON_COMMAND(ID_X, OnX)`), later code writes member pointers
  (`ON_COMMAND(ID_X, &CMyView::OnX)`). Standard C++ has no expression
  that accepts both. clang-retro's compiler has a preprocessor builtin
  for it (`__starts_with_ampersand`), so with it the maps take either
  form, mixed as code likes, with nothing to set (message maps, dispatch
  and event sink maps, DHTML event maps). With other compilers the
  second form needs `_AFX_MSGMAP_MEMBER_POINTERS` defined before
  `afxwin.h`.
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
* The extended control's own properties are Name, Object, Parent and
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
  MFC), and the temporary window objects it makes are freed when the
  DLL unloads. With `mfc.dll`, the DLL's module state holds its
  CWinApp, instance and resource handles, window classes, handle maps
  and OLE factories and object count; the program's run-time class list
  (CRuntimeClass::FromName) and AfxGetThread are shared with it.
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
* A control's binary stream (IPersistStreamInit, IPersistStorage, DLGINIT
  data) and its property bag entries are the ones Visual C++ MFC
  writes: the tests load a stream that MFC 4.2 saved and save it back
  byte for byte. Fonts, pictures and other objects in the stream carry
  their own data, so a font saved on another system may name the face
  that system used.
* COleDocument's "Contents" stream and its "Embedding N" storages are
  laid out as Visual C++ MFC lays them out, for compound and flat
  files. An item's record holds three fixed values whose meaning is not
  known; they are written as Visual C++ MFC writes them for embedded and
  static items and ignored when loading. Wine's
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
  any driver. A dynaset (or dynamic recordset) asks the driver for a
  keyset-driven cursor and reads each record again as it moves to it,
  so other users' changes and deletions show; records they add show
  after Requery, in a dynamic recordset too. With a driver that has no
  keyset-driven cursors, a dynaset behaves like a snapshot. A stored procedure's output
  and input/output parameters (`outputParam`, `inoutParam`) reach
  their members once FlushResultSet has gone past the last result set,
  or as it opens when the procedure has none.
* The DAO classes drive the engine through its Unicode interfaces in
  ANSI programs too. As with Visual C++, text in their variants is
  VT_BSTRT there (ANSI bytes in the BSTR), so it is converted on the
  way in and out.
* The `llvm-rc` of clang-retro (and its `llvm-windres`) compiles the
  `TOOLBAR` statement. Upstream `llvm-rc` and MinGW's `windres` do not.
  For these tools, write a toolbar as raw resource type 241, as
  `tests/sditest.rc` does, so that the test builds with each toolchain.
* CImage::SetTransparentColor takes an index into the color table for
  an indexed image, as with Visual C++, and an RGB value (a COLORREF)
  for any other image.
* CTaskDialog on a system without the task dialog shows a message box
  rather than asserting.
* CFileDialog asks for the Vista dialog when it runs: where Windows has
  none (XP), or a hook or template is set, it shows the classic one, and
  the customization members return E_NOTIMPL. CFolderPickerDialog falls
  back to SHBrowseForFolder there. The portable layer has the Vista
  dialog too: its own file dialog with the program's added controls in a
  panel below the standard ones.

## Plans

OLE 2 for Win16 (the core and window classes and the control bars
are there, see [docs/win16.md](docs/win16.md)), and finishing the
portable layer (see Linux and macOS above), so that MFC programs build
and look the same on macOS and Linux.

## Layout

`include/` has the public headers, `src/` the library, `port/` the
portable Win32 layer (its headers, sources, the generator for its
placeholders, `res2cpp.py`, and in `port/pkg` the pkg-config and CMake
package files), `tests/` the
test programs and `tools/` the generator for the `ON_WM_*` macros and
CWnd's default handlers (`tools/gen-msgmap.py`) and a development helper
that lists declared functions the library does not define yet, and
`tools/fetch-wtl.sh`, which fetches WTL's headers for `wtltest`.

## License

Copyright (C) clang-retro contributors.

clang-retro MFC is free software under the GNU Affero General Public
License, version 3 only (`AGPL-3.0-only`); the full text is in
[LICENSE](LICENSE). If you distribute a program built with it, or let
people use a modified version over a network, you must offer the
program's complete source under the same license.

Each contributor keeps the copyright in their own work and contributes
it under the same license; see [CONTRIBUTING.md](CONTRIBUTING.md).
