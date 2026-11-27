// theme.h - the built-in visual style: whether a program draws with it,
// and the drawing of its parts (theme.cpp), under uxtheme.cpp's calls and
// the controls of user32 and comctl32.
//
// A program draws with visual styles when it asks for version 6 of the
// common controls in its manifest (as on Windows), or when
// WINPORT_VISUAL_STYLES is 1; WINPORT_VISUAL_STYLES=0 turns them off.
// Without them, the controls draw the classic look and no theme opens.
//
// The look is clang-retro's own: flat parts with one-pixel borders and
// slightly rounded corners, in grays and the highlight color as accent.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_THEME_H
#define WINPORT_THEME_H

#include <windows.h>
#include <uxtheme.h>

namespace winport {
namespace theme {

// The classes of the theme, as OpenThemeData names them.
enum Class {
  kNoClass,
  kButton,
  kEdit,
  kCombo,
  kScrollBar,
  kProgress,
  kTab,
  kTrackBar,
  kHeader,
  kToolBar,
  kSpin,
  kStatus,
  kRebar,
  kTreeView,
  kListView,
  kToolTip,
  kWindow,
};

// Whether the program draws with visual styles.
bool AppThemed();

// Whether control h draws with them: the program does, its controls may
// (SetThemeAppProperties) and SetWindowTheme has not turned them off for
// h.
bool Themed(HWND h);

// The class a name stands for ("Button", "Explorer::TreeView"), or
// kNoClass.
Class ClassOf(const wchar_t* name);

// Draws a part in its state over r (clipped to clip when not NULL).
// False for a part the class does not have.
bool DrawPart(Class c, HDC dc, int part, int state, const RECT& r, const RECT* clip);

// Whether the class draws the part.
bool HasPart(Class c, int part);

// Whether the part leaves some of r undrawn, so that what is behind
// shows.
bool PartTransparent(Class c, int part, int state);

// The room inside the part's borders.
RECT ContentRect(Class c, int part, int state, const RECT& r);

// The part's size: its least (TS_MIN), true (TS_TRUE) or drawn (TS_DRAW)
// size. False when the part has no size of its own.
bool PartSize(Class c, int part, int state, const RECT* r, int which, SIZE* out);

// The part's colors (TMT_TEXTCOLOR, TMT_FILLCOLOR, TMT_BORDERCOLOR and
// the others), integers (TMT_BORDERSIZE and so on) and margins.
bool Color(Class c, int part, int state, int prop, COLORREF* out);
bool Int(Class c, int part, int state, int prop, int* out);
bool Margins(Class c, int part, int state, int prop, MARGINS* out);

// The system colors of the visual style: those of the classic look, with
// the faces and borders of the flat one.
COLORREF SysColor(int index, COLORREF classic);

}  // namespace theme
}  // namespace winport

#endif
