# Screen savers

The portable layer has the screen saver library of the Windows SDK:
`scrnsave.h`, with `libscrnsave.a` for ANSI programs and
`libscrnsavw.a` for Unicode programs. A screen saver written for
Windows builds without changes on Linux, macOS and the classic Mac OS.

## What the library does

The library has `WinMain`. It reads the command line, as Windows does:

| Command line | Action |
| --- | --- |
| `/s` or `-s` | Runs the screen saver in a window over the whole screen. |
| `/p hwnd` or `/l hwnd` | Runs the screen saver in a child window of `hwnd` (a preview). `fChildPreview` is `TRUE`. |
| `/c`, `/c:hwnd` or no argument | Shows the configuration dialog (`DLG_SCRNSAVECONFIGURE`). |
| `/a` | Changes the password. There are no passwords: it does nothing. |

On the classic Mac OS, there is no command line. No argument runs the
screen saver, because the user opens the application to see it.

A screen saver with its own command-line code (as the SDK samples have)
reads `GetCommandLine`. When the Finder opens a program that has the
dialog `DLG_SCRNSAVECONFIGURE` (2003), the layer gives it the command
line `/S`, as the Windows shell does when it opens a `.scr` file. With
the Option key held down, it gives `/C`, which shows the settings
dialog.

Mac OS X does the same: when the program has no arguments (as when you
open it in the Finder), the layer gives it `/S`, or `/C` with the Option
key held down. The `-psn_...` argument that the Finder of Mac OS X 10.4
to 10.8 adds is not part of the command line. To see the settings from
Terminal, give `/c`.

The library loads the strings of the program (`IDS_DESCRIPTION` into
`szName`, and the `ids...` strings) and registers the window class
`WindowsScreenSaverClass`, with `ScreenSaverProc` as its window
procedure.

`DefScreenSaverProc` closes the screen saver when the mouse moves more
than 4 pixels, when the user clicks or presses a key, and when the
application becomes inactive. It hides the cursor. Call it for all
messages that `ScreenSaverProc` does not handle.

## What the program gives

- `ScreenSaverProc`, the window procedure of the screen saver.
- `ScreenSaverConfigureDialog`, the dialog procedure of the configuration
  dialog.
- `RegisterDialogClasses`. Give `TRUE` when the dialog has no classes of
  its own.
- The resources: the string `IDS_DESCRIPTION`, the icon `ID_APP` and
  the dialog `DLG_SCRNSAVECONFIGURE`. Compile them with
  `port/tools/res2cpp.py`.

## Build a screen saver

Link the screen saver library before `libwinport.a`:

```sh
python3 path/to/mfc/port/tools/res2cpp.py saver.rc -o saver-res.cpp
clang++ -std=c++17 -Ipath/to/mfc/port/include saver.cpp saver-res.cpp \
  -Lpath/to/mfc/build/x86_64-linux-gnu -lscrnsave -lwinport -ldl -lpthread -o saver
./saver /s
```

For a Unicode program, use `-DUNICODE -D_UNICODE` and `-lscrnsavw`.

## Limits

- OpenGL screen savers draw through the OpenGL of the system (Mac OS X,
  and the classic Mac OS on PowerPC) or through the generic
  implementation (Linux, and the classic Mac OS on 68k). See
  [software-opengl.md](software-opengl.md).
- There are no passwords (`ScreenSaverChangePassword` does nothing, and
  `SCRM_VERIFYPW` is not sent).

## Tests

`tests/sstest.cpp` is a screen saver that draws for three timer ticks
and then presses a key. `tests/run-tests.sh` runs it with `/s` on Linux
and macOS, and `tests/run-macos-tests.sh` runs it on the classic Mac OS
simulators.
