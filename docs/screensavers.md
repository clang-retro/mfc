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

## System screen savers on Mac OS X

On Mac OS X, `tools/build-sdk-samples.py` also makes a `.saver` bundle
for each OpenGL screen saver of the SDK samples (for example
`3D Pipes.saver`). The bundle is a module of `ScreenSaver.framework`. The
screen saver engine and the Screen Saver (Screen Effects) panel of System
Preferences load it as they load the screen savers of Apple.

To install a screen saver, copy its bundle to one of these folders:

- `/Library/Screen Savers`, for all users.
- `~/Library/Screen Savers`, for one user.

Then select the screen saver in System Preferences.

The bundles run on Mac OS X 10.0 to 10.5 on PowerPC and on 10.4 and 10.5
on Intel. See [macosx-10.4.md](macosx-10.4.md) for the build of a fat
bundle.

### How the bundle works

The principal class of the bundle (`port/saver/saver_bundle.mm`) is a
subclass of `ScreenSaverView`. The engine owns the process and its event
loop. Thus the program cannot have its own loop. `port/src/saver_host.cpp`
does these steps:

1. It runs `WinMain` of the program on a stack of its own (a coroutine),
   on the main thread.
2. It gives the program the command line `name.scr /p HWND`. `HWND` is a
   window that fills the view. The program runs as a preview in it.
3. The Cocoa backend puts the views of the windows into the
   `ScreenSaverView`, in place of windows of their own.
4. For each frame of the engine (`animateOneFrame`), the program runs
   until it waits for its next message. Then control goes back to the
   engine.
5. When the view stops, the program stops at its next wait. When another
   view starts, the program moves into that view and keeps its state.

`ExitProcess` does not end the process: it stops the program. The
resources come from the bundle, not from the engine.

### Limits of the bundle

- One screen saver runs in a process. With more than one screen, the
  screen saver draws on the first screen. The other screens stay black.
- The settings dialog of the program is not available in System
  Preferences (`hasConfigureSheet` gives `NO`). Open the `.scr` program
  with the Option key held down to change the settings.
- Each bundle has a different name for its Objective-C classes, because
  System Preferences loads all bundles in one process.

## System screen savers on the classic Mac OS

The classic Mac OS (System 7 to Mac OS 9) has no screen saver host of
its own. The layer gives one in `port/saver/macos`:

| File | Type | Folder | What it does |
| --- | --- | --- | --- |
| Screen Saver Engine | `appe` (faceless background application) | Extensions | Monitors the mouse and the keyboard. After the idle time, starts the selected screen saver. |
| Screen Saver | `APPC` (control panel) | Control Panels | Selects the screen saver and the idle time, and has a "Test" button. Writes "Screen Saver Preferences" in the Preferences folder. |
| The screen savers | `APPL` | Screen Savers (in the System Folder) | The `.scr` programs, built for the classic Mac OS. |
| The After Dark modules | `ADgm` | After Dark Files | Optional. One module for each screen saver, for After Dark. |

`tools/build-sdk-samples.py` makes all of them for the classic Mac OS
(fat programs and 68k modules). It gives each screen saver the name
that the control panel shows (for example `3D Pipes`).

### Install

1. Copy "Screen Saver Engine" to the Extensions folder.
2. Copy "Screen Saver" to the Control Panels folder.
3. Copy the screen savers to the folder "Screen Savers" in the System
   Folder. The control panel makes this folder when it does not exist.
4. Restart the Mac. Open the control panel, then select a screen saver
   and the idle time.

### How it works

- The engine reads the position of the mouse, the keys and the mouse
  button every half second. When they do not change for the idle time,
  it starts the screen saver with `LaunchApplication`. The engine reads
  the settings again when the control panel changes them.
- The program gets no arguments. Thus the layer gives it `/S`, and it
  runs over the whole screen. It stops when the mouse moves, or when the
  user clicks or presses a key. Then the idle time starts again.
- An After Dark module is a 68k code resource (`ad_module.c`). At
  `Initialize`, it starts the screen saver of the same name from the
  "Screen Savers" folder. At `Close`, it sends the Apple event "quit" to
  the program if the program did not stop itself. The module does not
  draw. mac-sdk's `macapp.py --code-resource` links it.

### Tests

`tests/macos-saver` has a small screen saver (`testsaver.c`) and a small
After Dark host (`adhost.c`). The host calls a module as After Dark
does, moves the code of the module in memory between two calls, and
checks that the module starts and stops the screen saver. On Mac OS 9.2.1
in QEMU:

- The engine and the control panel start with the Mac. The engine
  starts the screen saver after one minute without input, and the
  screen saver stops when the mouse moves.
- `adhost` gives "ADHOST PASSED".

### Limits

- The After Dark modules are not tested with After Dark itself.
- The control panel cannot open the settings dialog of a screen saver.
  To change the settings, open the screen saver with the Option key held
  down.
- On a locked startup disk (a CD), the control panel cannot save the
  settings. It shows the error.

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
