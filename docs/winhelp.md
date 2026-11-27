# The WinHelp viewer

The layer has a viewer for WinHelp files (`.hlp`). `WinHelp`, and so
the Help commands of MFC (`CWinApp::WinHelp`, `ID_HELP_FINDER`,
`ID_HELP`, `ID_CONTEXT_HELP`), show the help file in this viewer. The
viewer is in `port/src/winhelp_file.cpp` (the file reader) and
`port/src/winhelp_view.cpp` (the windows).

## How WinHelp finds the file

1. `WinHelp` removes a window name (`app.hlp>proc`) from the file name.
2. `WinHelp` finds the file. A name that is not a full path is relative
   to the current folder. If there is no file with the exact name,
   `WinHelp` finds a file of the same name in a different case
   (`APP.HLP` or `app.hlp`).
3. If the file is a WinHelp file, the viewer shows it.
4. If the file is not a WinHelp file, or if there is no file,
   `WinHelp` opens a document of the same name as `.html`, `.htm`,
   `.pdf` or `.txt` (or a folder of that name with an `index.html`).
   The desktop's opener opens the document. A help context is the
   anchor of the HTML page (`app.html#131072`).
5. If there is no document, `WinHelp` returns `FALSE` and sets
   `ERROR_FILE_NOT_FOUND`.

When `WINPORT_HELP` names a command, `WinHelp` does not use the viewer.
It always opens the document with that command.

## Commands

The viewer does these `WinHelp` commands:

| Command | What the viewer does |
| --- | --- |
| `HELP_CONTENTS`, `HELP_INDEX` | Shows the contents topic. |
| `HELP_FINDER` | Shows the Help Topics dialog box. Without a `.cnt` file, the dialog box opens on the Index tab. |
| `HELP_CONTEXT` | Shows the topic of the `[MAP]` number. |
| `HELP_CONTEXTPOPUP` | Shows the topic of the `[MAP]` number in a popup window. |
| `HELP_WM_HELP`, `HELP_CONTEXTMENU` | Finds the control in the array of ID pairs and shows its topic in a popup window. |
| `HELP_KEY`, `HELP_PARTIALKEY`, `HELP_MULTIKEY` | Shows the topic of the keyword. If more than one topic has the keyword, the viewer shows the Topics Found dialog box. |
| `HELP_COMMAND` | Runs the help macros. |
| `HELP_SETCONTENTS` | Sets the contents topic. |
| `HELP_SETPOPUP_POS` | Sets the position of the next popup window. |
| `HELP_SETWINPOS` | Moves the window. |
| `HELP_FORCEFILE` | Shows the file. |
| `HELP_HELPONHELP` | Shows a message box with a short text about the viewer. |
| `HELP_QUIT` | Closes the viewer windows of the program. |
| `HELP_TCARD` | Does the command. The viewer does not send training card messages. |

## What the viewer reads

The viewer reads WinHelp 3.0, 3.1 and 4.0 files:

- The internal file system and its B+ trees.
- `|SYSTEM`: the title, the copyright, the contents topic, the
  configuration macros, the window definitions and the character set.
- `|TOPIC`: LZ77 compression in blocks of 2 KB or 4 KB, and files
  without compression.
- Text compression: `|Phrases` (WinHelp 3.1) and `|PhrIndex` with
  `|PhrImage` (WinHelp 4.0, Hall compression).
- `|CONTEXT` (the hashes of the context names), `|CTXOMAP` (WinHelp
  3.0) and the `[MAP]` numbers.
- `|FONT`: the font names, sizes, styles and colors.
- The keyword indexes (`|KWBTREE` and `|KWDATA`, and the `A` keywords).
- The pictures: device-dependent bitmaps, device-independent bitmaps
  and metafiles, in `|bm` files or in the topic. The viewer reads the
  run-length and LZ77 packing, and the hotspots in SHG and MRB pictures.
- The contents file (`.cnt`): `:Title`, `:Base`, `:Link`, `:Index`,
  books, topics and macros.

## What the viewer shows

- The topic in a region that scrolls, and the topic title in a region
  that does not scroll.
- Paragraph formats: indents, spaces, line spacing, alignment, tab
  stops (left, right and center), borders and tables.
- Hotspots: jumps (green, with an underline), popups (green, with a
  dotted underline) and macros. Hotspots in pictures and `{button}`
  pictures.
- Popup windows, with the popup color of the help file.
- The button bar, the browse buttons, and the buttons that the help
  file adds with macros.
- The Help Topics dialog box, with the Contents, Index and Find tabs.
  Find finds words in the text of all topics.
- The menu: File, Edit, Options (keep on top, font size) and Help.
- Keyboard: Tab and Shift+Tab move between hotspots, Enter follows a
  hotspot, Esc closes a popup window, Ctrl+C copies the topic text.

## Macros

The viewer does these macros: `About`, `ALink`, `Back`,
`BrowseButtons`, `ChangeButtonBinding`, `ChangeEnable`, `CloseWindow`,
`Contents`, `CopyTopic`, `CreateButton`, `DestroyButton`,
`DisableButton`, `EnableButton`, `ExecFile`, `ExecProgram`, `Exit`,
`FileOpen`, `Find`, `Finder`, `HelpOn`, `IfThen`, `IfThenElse`,
`JumpContents`, `JumpContext`, `JumpHash`, `JumpId`, `JumpKeyword`,
`KLink`, `Next`, `PopupContext`, `PopupHash`, `PopupId`,
`PositionWindow`, `Prev`, `Search`, `SetContents`, `SetPopupColor`,
`ShellExecute`. The short names (`JI`, `PI`, `CB` and the others) are
also correct.

The viewer ignores the other macros.

## Limits

The viewer does not do these items:

- Printing, annotations and bookmarks.
- Macros that call functions in DLLs (`RegisterRoutine`).
- Multimedia Viewer files (`.mvb`) and HTML Help files (`.chm`).
- Full-text search indexes (`.fts`). Find reads the topic text
  instead.
- Embedded windows (`{ewl}`, `{ewc}`, `{ewr}`). The viewer shows an
  empty box.
- Training cards. `HELP_TCARD` does not send `WM_TCARD`.

## Tests

`tests/hlptest.cpp` opens three help files and checks the text, the
pictures, the hotspots, the macros, the keywords and the dialog boxes.
The help files are in `tests/winhelp`:

- `guide.hlp` and `guide.cnt` come from `guide.but` with
  [Halibut](https://www.chiark.greenend.org.uk/~sgtatham/halibut/):
  `halibut --winhelp=guide.hlp guide.but`.
- `feature.hlp` (WinHelp 3.1) and `feature95.hlp` (WinHelp 4.0) come
  from `mkhlp.py`: `python3 mkhlp.py tests/winhelp`. The script writes
  the same bytes each time.
