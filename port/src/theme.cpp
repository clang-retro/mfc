// theme.cpp - the built-in visual style (see theme.h): whether a program
// asked for it, and how each part of each class looks in each state.
//
// The look is drawn with GDI from a few colors: the face and window
// colors, grays for borders and glyphs, and the highlight color as the
// accent of hot, pressed, checked and focused parts. Sizes follow the
// screen's DPI.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "theme.h"

#include <vssym32.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <string>

#include "user.h"
#include "winport.h"

namespace winport {
namespace theme {

namespace {

// ---------------------------------------------------------------------
// Whether the program asked for visual styles

// Whether a manifest asks for version 6 of the common controls: a
// dependency on Microsoft.Windows.Common-Controls with a version from 6.
bool ManifestAsks(const std::string& xml) {
  std::string lower = xml;
  for (char& ch : lower)
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
  size_t at = lower.find("microsoft.windows.common-controls");
  if (at == std::string::npos) return false;
  // The version attribute of the same element.
  size_t open = lower.rfind('<', at);
  size_t close = lower.find('>', at);
  if (open == std::string::npos || close == std::string::npos) return false;
  std::string tag = lower.substr(open, close - open);
  size_t v = tag.find("version");
  if (v == std::string::npos) return false;
  v = tag.find_first_of("\"'", v);
  if (v == std::string::npos) return false;
  return atoi(tag.c_str() + v + 1) >= 6;
}

bool AskedFor() {
  const char* env = getenv("WINPORT_VISUAL_STYLES");
  if (env && *env) return strcmp(env, "0") != 0;
  HMODULE exe = GetModuleHandleW(NULL);
  for (int id = 1; id <= 3; id++) {
    HRSRC r = FindResourceW(exe, MAKEINTRESOURCEW(id), (LPCWSTR)RT_MANIFEST);
    if (!r) continue;
    HGLOBAL g = LoadResource(exe, r);
    const char* data = g ? (const char*)LockResource(g) : NULL;
    if (data && ManifestAsks(std::string(data, SizeofResource(exe, r)))) return true;
  }
  return false;
}

// ---------------------------------------------------------------------
// Colors and shapes

COLORREF Mix(COLORREF a, COLORREF b, int percentB) {
  int k = 100 - percentB;
  return RGB((GetRValue(a) * k + GetRValue(b) * percentB) / 100, (GetGValue(a) * k + GetGValue(b) * percentB) / 100,
             (GetBValue(a) * k + GetBValue(b) * percentB) / 100);
}

COLORREF Accent() { return GetSysColor(COLOR_HIGHLIGHT); }
const COLORREF kWhite = RGB(255, 255, 255);
const COLORREF kBorder = RGB(173, 173, 173);    // a control's border
const COLORREF kDisabledBorder = RGB(204, 204, 204);
const COLORREF kDisabledFill = RGB(244, 244, 244);
const COLORREF kGlyph = RGB(96, 96, 96);         // arrows and grippers
const COLORREF kDisabledGlyph = RGB(191, 191, 191);
const COLORREF kLine = RGB(217, 217, 217);       // panes and separators

// A light wash of the accent, for hot (12), pressed (25) and checked
// (18) parts.
COLORREF Wash(int percent) { return Mix(kWhite, Accent(), percent); }

void Fill(HDC dc, const RECT& r, COLORREF c) {
  if (r.right <= r.left || r.bottom <= r.top) return;
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

void Frame(HDC dc, const RECT& r, COLORREF c) {
  if (r.right <= r.left || r.bottom <= r.top) return;
  HBRUSH b = CreateSolidBrush(c);
  FrameRect(dc, &r, b);
  DeleteObject(b);
}

// A box with a one-pixel border; rounded leaves its corner pixels out.
void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, bool rounded = true) {
  if (r.right - r.left < 2 || r.bottom - r.top < 2) {
    Fill(dc, r, border);
    return;
  }
  Fill(dc, RECT{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, fill);
  int c = rounded ? 1 : 0;
  Fill(dc, RECT{r.left + c, r.top, r.right - c, r.top + 1}, border);
  Fill(dc, RECT{r.left + c, r.bottom - 1, r.right - c, r.bottom}, border);
  Fill(dc, RECT{r.left, r.top + 1, r.left + 1, r.bottom - 1}, border);
  Fill(dc, RECT{r.right - 1, r.top + 1, r.right, r.bottom - 1}, border);
}

// A chevron pointing up (0), down (1), left (2) or right (3), centered
// in r, two pixels thick.
void Chevron(HDC dc, const RECT& r, int dir, COLORREF c, int size = 0) {
  int n = size ? size : std::max(2, std::min(r.right - r.left, r.bottom - r.top) / 4);
  int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  HBRUSH b = CreateSolidBrush(c);
  for (int i = -n; i <= n; i++) {
    int d = n - abs(i);  // distance back from the point
    RECT p;
    switch (dir) {
      case 0: p = RECT{cx + i, cy - n / 2 + (n - d), cx + i + 1, cy - n / 2 + (n - d) + 2}; break;
      case 1: p = RECT{cx + i, cy + n / 2 - (n - d) - 1, cx + i + 1, cy + n / 2 - (n - d) + 1}; break;
      case 2: p = RECT{cx - n / 2 + (n - d), cy + i, cx - n / 2 + (n - d) + 2, cy + i + 1}; break;
      default: p = RECT{cx + n / 2 - (n - d) - 1, cy + i, cx + n / 2 - (n - d) + 1, cy + i + 1}; break;
    }
    FillRect(dc, &p, b);
  }
  DeleteObject(b);
}

// A check mark in r, two pixels thick.
void Check(HDC dc, const RECT& r, COLORREF c) {
  int w = r.right - r.left, h = r.bottom - r.top;
  int s = std::min(w, h);
  int x0 = r.left + (w - s) / 2, y0 = r.top + (h - s) / 2;
  // From a quarter of the way down the left, down to the bottom third,
  // then up to the top right.
  int ax = x0 + s / 5, ay = y0 + s / 2;
  int bx = x0 + s * 2 / 5, by = y0 + s * 7 / 10;
  int cx = x0 + s * 4 / 5, cy = y0 + s * 3 / 10;
  HBRUSH b = CreateSolidBrush(c);
  for (int x = ax; x <= bx; x++) {
    int y = ay + (by - ay) * (x - ax) / std::max(1, bx - ax);
    RECT p = {x, y, x + 1, y + 2};
    FillRect(dc, &p, b);
  }
  for (int x = bx; x <= cx; x++) {
    int y = by + (cy - by) * (x - bx) / std::max(1, cx - bx);
    RECT p = {x, y, x + 1, y + 2};
    FillRect(dc, &p, b);
  }
  DeleteObject(b);
}

// A circle filling r, its inside and its ring.
void Circle(HDC dc, const RECT& r, COLORREF fill, COLORREF ring) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, ring);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  Ellipse(dc, r.left, r.top, r.right, r.bottom);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(p);
}

// Dots in a grid, for grippers and size boxes.
void Dots(HDC dc, const RECT& r, bool vertical) {
  int step = Scale96(4);
  if (vertical) {
    int x = (r.left + r.right) / 2 - 1;
    for (int y = r.top + 2; y + 2 <= r.bottom - 2; y += step) Fill(dc, RECT{x, y, x + 2, y + 2}, kGlyph);
  } else {
    int y = (r.top + r.bottom) / 2 - 1;
    for (int x = r.left + 2; x + 2 <= r.right - 2; x += step) Fill(dc, RECT{x, y, x + 2, y + 2}, kGlyph);
  }
}

// A size box's dots: a triangle of them in the bottom right corner.
void SizeDots(HDC dc, const RECT& r) {
  int step = Scale96(4);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j <= i; j++) {
      int x = r.right - step * (j + 1), y = r.bottom - step * (3 - i);
      Fill(dc, RECT{x, y, x + 2, y + 2}, kGlyph);
    }
}

// ---------------------------------------------------------------------
// The states, folded: normal, hot, pressed, disabled.

enum Look { kNormal, kHot, kPressed, kDisabled, kChecked, kFocused };

// The look of a part whose states are numbered normal, hot, pressed,
// disabled (and more after).
Look Common(int state) {
  switch (state) {
    case 2: return kHot;
    case 3: return kPressed;
    case 4: return kDisabled;
    default: return kNormal;
  }
}

// A push button's face and border.
void PushFace(HDC dc, const RECT& r, Look look, bool defaulted) {
  switch (look) {
    case kHot: Box(dc, r, Wash(12), Accent()); break;
    case kPressed: Box(dc, r, Wash(25), Mix(Accent(), RGB(0, 0, 0), 20)); break;
    case kDisabled: Box(dc, r, kDisabledFill, kDisabledBorder); break;
    case kChecked: Box(dc, r, Wash(18), Accent()); break;
    default:
      Box(dc, r, RGB(251, 251, 251), defaulted ? Accent() : kBorder);
      if (defaulted) Frame(dc, RECT{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, Mix(Accent(), kWhite, 40));
      break;
  }
}

// A flat button that shows only when hot or pressed (toolbars, spins,
// combo boxes' drop-down buttons).
void FlatFace(HDC dc, const RECT& r, Look look) {
  switch (look) {
    case kHot: Box(dc, r, Wash(12), Wash(45)); break;
    case kPressed: Box(dc, r, Wash(25), Wash(60)); break;
    case kChecked: Box(dc, r, Wash(18), Wash(60)); break;
    default: break;
  }
}

COLORREF GlyphColor(Look look) { return look == kDisabled ? kDisabledGlyph : kGlyph; }

// ---------------------------------------------------------------------
// The classes

int BoxSize() { return Scale96(13); }

void DrawCheckBox(HDC dc, const RECT& r, int state) {
  int kind = (state - 1) / 4;  // unchecked, checked, mixed, implicit, excluded
  Look look = Common((state - 1) % 4 + 1);
  RECT b = r;
  bool on = kind == 1 || kind == 3;
  COLORREF accent = look == kDisabled ? kDisabledBorder : Accent();
  if (on) {
    Box(dc, b, look == kHot ? Mix(accent, kWhite, 15) : look == kPressed ? Mix(accent, RGB(0, 0, 0), 20) : accent,
        look == kPressed ? Mix(accent, RGB(0, 0, 0), 20) : accent);
    RECT m = b;
    InflateRect(&m, -Scale96(2), -Scale96(2));
    Check(dc, m, kWhite);
    return;
  }
  COLORREF fill = look == kDisabled ? kDisabledFill : look == kHot ? Wash(8) : look == kPressed ? Wash(20) : kWhite;
  COLORREF border = look == kDisabled ? kDisabledBorder : look == kNormal ? RGB(118, 118, 118) : Accent();
  Box(dc, b, fill, border);
  if (kind == 2) {
    RECT m = b;
    InflateRect(&m, -Scale96(3), -Scale96(3));
    Fill(dc, m, accent);
  } else if (kind == 4) {
    // Excluded: a dash.
    int y = (b.top + b.bottom) / 2 - 1;
    Fill(dc, RECT{b.left + Scale96(3), y, b.right - Scale96(3), y + 2}, look == kDisabled ? kDisabledGlyph : kGlyph);
  }
}

void DrawRadio(HDC dc, const RECT& r, int state) {
  bool on = state >= 5;
  Look look = Common((state - 1) % 4 + 1);
  COLORREF accent = look == kDisabled ? kDisabledBorder : Accent();
  COLORREF fill = look == kDisabled ? kDisabledFill : look == kHot ? Wash(8) : look == kPressed ? Wash(20) : kWhite;
  COLORREF ring = on ? accent : look == kDisabled ? kDisabledBorder : look == kNormal ? RGB(118, 118, 118) : Accent();
  Circle(dc, r, fill, ring);
  if (on) {
    RECT d = r;
    int in = (r.right - r.left) * 3 / 10;
    InflateRect(&d, -in, -in);
    Circle(dc, d, accent, accent);
  }
}

bool DrawButton(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case BP_PUSHBUTTON:
    case BP_USERBUTTON:
      PushFace(dc, r, state == PBS_DEFAULTED || state == PBS_DEFAULTED_ANIMATING ? kNormal : Common(state),
               state == PBS_DEFAULTED || state == PBS_DEFAULTED_ANIMATING);
      return true;
    case BP_COMMANDLINK: {
      Look look = state == CMDLS_DEFAULTED ? kNormal : Common(state);
      if (look == kNormal && state != CMDLS_DEFAULTED) return true;  // flat until hot
      if (state == CMDLS_DEFAULTED) {
        Box(dc, r, kWhite, Wash(60));
        return true;
      }
      FlatFace(dc, r, look);
      return true;
    }
    case BP_COMMANDLINKGLYPH: {
      Look look = Common(state);
      Chevron(dc, r, 3, look == kDisabled ? kDisabledGlyph : Accent());
      return true;
    }
    case BP_CHECKBOX: DrawCheckBox(dc, r, state); return true;
    case BP_RADIOBUTTON: DrawRadio(dc, r, state); return true;
    case BP_GROUPBOX:
      Box(dc, r, kWhite, kLine, false);
      return true;
  }
  return false;
}

bool DrawEdit(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case EP_EDITTEXT:
    case EP_BACKGROUND:
    case EP_BACKGROUNDWITHBORDER: {
      bool disabled = (part == EP_EDITTEXT && (state == ETS_DISABLED || state == ETS_READONLY)) ||
                      (part != EP_EDITTEXT && (state == EBS_DISABLED || state == EBS_READONLY));
      bool focused = (part == EP_EDITTEXT && state == ETS_FOCUSED) || (part != EP_EDITTEXT && state == EBS_FOCUSED);
      bool hot = (part == EP_EDITTEXT && state == ETS_HOT) || (part != EP_EDITTEXT && state == EBS_HOT);
      COLORREF fill = disabled ? kDisabledFill : kWhite;
      if (part == EP_BACKGROUND) {
        Fill(dc, r, fill);
        return true;
      }
      Box(dc, r, fill, disabled ? kDisabledBorder : focused ? Accent() : hot ? RGB(23, 23, 23) : RGB(122, 122, 122),
          false);
      return true;
    }
    case EP_EDITBORDER_NOSCROLL:
    case EP_EDITBORDER_HSCROLL:
    case EP_EDITBORDER_VSCROLL:
    case EP_EDITBORDER_HVSCROLL: {
      // The border only: the inside stays as the control drew it.
      COLORREF c = state == 4 ? kDisabledBorder : state == 3 ? Accent() : state == 2 ? RGB(23, 23, 23) : RGB(122, 122, 122);
      Frame(dc, r, c);
      return true;
    }
    case EP_CARET:
    case EP_PASSWORD: return false;
  }
  return false;
}

bool DrawCombo(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case CP_DROPDOWNBUTTON:
    case CP_DROPDOWNBUTTONRIGHT:
    case CP_DROPDOWNBUTTONLEFT: {
      Look look = Common(state);
      FlatFace(dc, r, look);
      Chevron(dc, r, 1, GlyphColor(look), std::max(2, Scale96(4)));
      return true;
    }
    case CP_BORDER:
      Box(dc, r, state == CBB_DISABLED ? kDisabledFill : kWhite,
          state == CBB_DISABLED ? kDisabledBorder : state == CBB_FOCUSED ? Accent() : state == CBB_HOT ? RGB(23, 23, 23)
                                                                                                    : RGB(122, 122, 122),
          false);
      return true;
    case CP_READONLY: PushFace(dc, r, Common(state), false); return true;
    case CP_BACKGROUND: Fill(dc, r, kWhite); return true;
    case CP_TRANSPARENTBACKGROUND: return true;
  }
  return false;
}

bool DrawScroll(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case SBP_ARROWBTN: {
      int dir = (state - 1) / 4;  // up, down, left, right
      Look look = Common((state - 1) % 4 + 1);
      if (state >= ABS_UPHOVER) {
        dir = state - ABS_UPHOVER;
        look = kNormal;
      }
      COLORREF back = RGB(240, 240, 240), glyph = GlyphColor(look);
      if (look == kHot) back = RGB(218, 218, 218);
      if (look == kPressed) {
        back = kGlyph;
        glyph = kWhite;
      }
      Fill(dc, r, back);
      Chevron(dc, r, dir, glyph, std::max(2, std::min(r.right - r.left, r.bottom - r.top) / 5));
      return true;
    }
    case SBP_THUMBBTNHORZ:
    case SBP_THUMBBTNVERT: {
      Look look = Common(state);
      COLORREF c = look == kHot ? RGB(166, 166, 166) : look == kPressed ? kGlyph : RGB(194, 194, 194);
      Fill(dc, r, RGB(240, 240, 240));
      RECT t = r;
      if (part == SBP_THUMBBTNVERT)
        InflateRect(&t, -std::max(1, (r.right - r.left) / 6), 0);
      else
        InflateRect(&t, 0, -std::max(1, (r.bottom - r.top) / 6));
      Fill(dc, t, c);
      return true;
    }
    case SBP_LOWERTRACKHORZ:
    case SBP_UPPERTRACKHORZ:
    case SBP_LOWERTRACKVERT:
    case SBP_UPPERTRACKVERT:
      Fill(dc, r, state == SCRBS_PRESSED ? RGB(218, 218, 218) : RGB(240, 240, 240));
      return true;
    case SBP_GRIPPERHORZ:
    case SBP_GRIPPERVERT: return true;  // flat thumbs have no gripper
    case SBP_SIZEBOX:
      Fill(dc, r, RGB(240, 240, 240));
      SizeDots(dc, r);
      return true;
  }
  return false;
}

COLORREF ProgressColor(int state) {
  switch (state) {
    case PBFS_ERROR: return RGB(218, 38, 38);
    case PBFS_PAUSED: return RGB(218, 165, 32);
    default: return RGB(6, 176, 37);
  }
}

bool DrawProgress(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case PP_BAR:
    case PP_BARVERT:
    case PP_TRANSPARENTBAR:
    case PP_TRANSPARENTBARVERT: Box(dc, r, RGB(230, 230, 230), RGB(188, 188, 188), false); return true;
    case PP_FILL:
    case PP_FILLVERT: Fill(dc, r, ProgressColor(state)); return true;
    case PP_CHUNK:
    case PP_CHUNKVERT: Fill(dc, r, ProgressColor(PBFS_NORMAL)); return true;
    case PP_PULSEOVERLAY:
    case PP_MOVEOVERLAY:
    case PP_PULSEOVERLAYVERT:
    case PP_MOVEOVERLAYVERT: Fill(dc, r, Mix(ProgressColor(PBFS_NORMAL), kWhite, 40)); return true;
  }
  return false;
}

bool DrawTab(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case TABP_TABITEM:
    case TABP_TABITEMLEFTEDGE:
    case TABP_TABITEMRIGHTEDGE:
    case TABP_TABITEMBOTHEDGE:
    case TABP_TOPTABITEM:
    case TABP_TOPTABITEMLEFTEDGE:
    case TABP_TOPTABITEMRIGHTEDGE:
    case TABP_TOPTABITEMBOTHEDGE: {
      COLORREF fill = state == TIS_SELECTED   ? kWhite
                      : state == TIS_HOT      ? Wash(12)
                      : state == TIS_DISABLED ? kDisabledFill
                                              : RGB(240, 240, 240);
      // The bottom stays open: the item joins the pane under it.
      RECT box = r;
      box.bottom += 1;
      Box(dc, box, fill, state == TIS_HOT ? Wash(45) : kLine, false);
      if (state == TIS_SELECTED || state == TIS_FOCUSED)
        Fill(dc, RECT{r.left + 1, r.bottom - 1, r.right - 1, r.bottom + 1}, kWhite);
      return true;
    }
    case TABP_PANE: Box(dc, r, kWhite, kLine, false); return true;
    case TABP_BODY:
    case TABP_AEROWIZARDBODY: Fill(dc, r, kWhite); return true;
  }
  return false;
}

bool DrawTrack(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case TKP_TRACK:
    case TKP_TRACKVERT: Box(dc, r, RGB(231, 234, 234), RGB(214, 214, 214), false); return true;
    case TKP_THUMB:
    case TKP_THUMBVERT:
    case TKP_THUMBBOTTOM:
    case TKP_THUMBTOP:
    case TKP_THUMBLEFT:
    case TKP_THUMBRIGHT: {
      COLORREF c = state == TUS_DISABLED ? kDisabledBorder
                   : state == TUS_HOT    ? Mix(Accent(), RGB(0, 0, 0), 15)
                   : state == TUS_PRESSED ? Mix(Accent(), RGB(0, 0, 0), 35)
                                          : Accent();
      if (part == TKP_THUMB || part == TKP_THUMBVERT) {
        Box(dc, r, c, c);
        return true;
      }
      // A pointed thumb: a box with its point toward the tics.
      int w = r.right - r.left, h = r.bottom - r.top;
      if (part == TKP_THUMBBOTTOM || part == TKP_THUMBTOP) {
        int tip = std::min(w / 2, h / 3);
        bool down = part == TKP_THUMBBOTTOM;
        RECT body = down ? RECT{r.left, r.top, r.right, r.bottom - tip} : RECT{r.left, r.top + tip, r.right, r.bottom};
        Box(dc, body, c, c);
        for (int i = 0; i < tip; i++) {
          int y = down ? body.bottom + i : body.top - 1 - i;
          Fill(dc, RECT{r.left + i * w / 2 / tip, y, r.right - i * w / 2 / tip, y + 1}, c);
        }
      } else {
        int tip = std::min(h / 2, w / 3);
        bool right = part == TKP_THUMBRIGHT;
        RECT body = right ? RECT{r.left, r.top, r.right - tip, r.bottom} : RECT{r.left + tip, r.top, r.right, r.bottom};
        Box(dc, body, c, c);
        for (int i = 0; i < tip; i++) {
          int x = right ? body.right + i : body.left - 1 - i;
          Fill(dc, RECT{x, r.top + i * h / 2 / tip, x + 1, r.bottom - i * h / 2 / tip}, c);
        }
      }
      return true;
    }
    case TKP_TICS:
    case TKP_TICSVERT: Fill(dc, r, RGB(196, 196, 196)); return true;
  }
  return false;
}

bool DrawHeader(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case HP_HEADERITEM:
    case HP_HEADERITEMLEFT:
    case HP_HEADERITEMRIGHT: {
      int s = (state - 1) % 3;  // normal, hot, pressed
      Fill(dc, r, s == 1 ? Wash(12) : s == 2 ? Wash(25) : kWhite);
      Fill(dc, RECT{r.right - 1, r.top, r.right, r.bottom}, RGB(229, 229, 229));
      Fill(dc, RECT{r.left, r.bottom - 1, r.right, r.bottom}, RGB(229, 229, 229));
      return true;
    }
    case HP_HEADERSORTARROW: Chevron(dc, r, state == HSAS_SORTEDUP ? 0 : 1, kGlyph, std::max(2, Scale96(3))); return true;
    case HP_HEADERDROPDOWN:
    case HP_HEADERDROPDOWNFILTER:
    case HP_HEADEROVERFLOW: Chevron(dc, r, 1, kGlyph, std::max(2, Scale96(3))); return true;
  }
  return false;
}

bool DrawToolBar(HDC dc, int part, int state, const RECT& r) {
  Look look = state == TS_CHECKED ? kChecked : state == TS_HOTCHECKED ? kHot : Common(state);
  switch (part) {
    case TP_BUTTON:
    case TP_DROPDOWNBUTTON:
    case TP_SPLITBUTTON:
    case TP_SPLITBUTTONDROPDOWN:
      FlatFace(dc, r, look == kDisabled ? kNormal : look);
      if (part == TP_SPLITBUTTONDROPDOWN) Chevron(dc, r, 1, GlyphColor(look), std::max(2, Scale96(3)));
      return true;
    case TP_DROPDOWNBUTTONGLYPH: Chevron(dc, r, 1, GlyphColor(look), std::max(2, Scale96(3))); return true;
    case TP_SEPARATOR: {
      int x = (r.left + r.right) / 2;
      Fill(dc, RECT{x, r.top + 2, x + 1, r.bottom - 2}, kLine);
      return true;
    }
    case TP_SEPARATORVERT: {
      int y = (r.top + r.bottom) / 2;
      Fill(dc, RECT{r.left + 2, y, r.right - 2, y + 1}, kLine);
      return true;
    }
  }
  return false;
}

bool DrawSpin(HDC dc, int part, int state, const RECT& r) {
  if (part < SPNP_UP || part > SPNP_DOWNHORZ) return false;
  Look look = Common(state);
  if (look == kNormal || look == kDisabled)
    Box(dc, r, look == kDisabled ? kDisabledFill : RGB(251, 251, 251), look == kDisabled ? kDisabledBorder : kBorder);
  else
    PushFace(dc, r, look, false);
  int dir = part == SPNP_UP ? 0 : part == SPNP_DOWN ? 1 : part == SPNP_UPHORZ ? 3 : 2;
  Chevron(dc, r, dir, GlyphColor(look), std::max(2, std::min(r.right - r.left, r.bottom - r.top) / 4));
  return true;
}

bool DrawStatus(HDC dc, int part, int, const RECT& r) {
  switch (part) {
    case SP_PANE:
    case SP_GRIPPERPANE: Fill(dc, RECT{r.right - 1, r.top + 2, r.right, r.bottom - 2}, kLine); return true;
    case SP_GRIPPER: SizeDots(dc, r); return true;
  }
  return false;
}

bool DrawRebar(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case RP_GRIPPER: Dots(dc, r, true); return true;
    case RP_GRIPPERVERT: Dots(dc, r, false); return true;
    case RP_BAND: return true;
    case RP_BACKGROUND: Fill(dc, r, GetSysColor(COLOR_BTNFACE)); return true;
    case RP_CHEVRON:
    case RP_CHEVRONVERT: {
      Look look = state == CHEVS_HOT ? kHot : state == CHEVS_PRESSED ? kPressed : kNormal;
      FlatFace(dc, r, look);
      Chevron(dc, r, part == RP_CHEVRON ? 3 : 1, kGlyph, std::max(2, Scale96(3)));
      return true;
    }
    case RP_SPLITTER:
    case RP_SPLITTERVERT: Fill(dc, r, kLine); return true;
  }
  return false;
}

// A list or tree item's highlight.
void ItemFace(HDC dc, const RECT& r, int state) {
  switch (state) {
    case LISS_HOT: Box(dc, r, Wash(10), Wash(30), false); break;
    case LISS_SELECTED: Box(dc, r, Wash(22), Wash(55), false); break;
    case LISS_HOTSELECTED: Box(dc, r, Wash(28), Accent(), false); break;
    case LISS_SELECTEDNOTFOCUS: Box(dc, r, RGB(217, 217, 217), RGB(217, 217, 217), false); break;
    default: break;
  }
}

bool DrawTree(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case TVP_TREEITEM: ItemFace(dc, r, state); return true;
    case TVP_GLYPH:
    case TVP_HOTGLYPH:
      Chevron(dc, r, state == GLPS_OPENED ? 1 : 3, part == TVP_HOTGLYPH ? Accent() : kGlyph,
              std::max(2, Scale96(3)));
      return true;
    case TVP_BRANCH: return true;
  }
  return false;
}

bool DrawList(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case LVP_LISTITEM: ItemFace(dc, r, state); return true;
    case LVP_LISTDETAIL:
    case LVP_LISTSORTEDDETAIL:
    case LVP_LISTGROUP: return true;
    case LVP_GROUPHEADERLINE: Fill(dc, RECT{r.left, (r.top + r.bottom) / 2, r.right, (r.top + r.bottom) / 2 + 1}, kLine); return true;
    case LVP_EXPANDBUTTON:
    case LVP_COLLAPSEBUTTON: Chevron(dc, r, part == LVP_EXPANDBUTTON ? 1 : 0, kGlyph); return true;
  }
  return false;
}

bool DrawToolTip(HDC dc, int part, int state, const RECT& r) {
  switch (part) {
    case TTP_STANDARD:
    case TTP_STANDARDTITLE:
    case TTP_BALLOON:
    case TTP_BALLOONTITLE: Box(dc, r, kWhite, RGB(118, 118, 118), false); return true;
    case TTP_CLOSE: {
      Look look = Common(state);
      FlatFace(dc, r, look);
      int n = std::min(r.right - r.left, r.bottom - r.top) / 4, cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
      for (int i = -n; i <= n; i++) {
        Fill(dc, RECT{cx + i, cy + i, cx + i + 1, cy + i + 1}, kGlyph);
        Fill(dc, RECT{cx + i, cy - i, cx + i + 1, cy - i + 1}, kGlyph);
      }
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------

bool AppThemed() {
  static bool themed = AskedFor();
  return themed;
}

Class ClassOf(const wchar_t* name) {
  if (!name) return kNoClass;
  // "Explorer::TreeView": a subclass of the class after the colons.
  const wchar_t* colons = wcsstr(name, L"::");
  if (colons) name = colons + 2;
  struct Entry {
    const wchar_t* name;
    Class c;
  };
  static const Entry kClasses[] = {
      {L"BUTTON", kButton},     {L"EDIT", kEdit},         {L"COMBOBOX", kCombo},     {L"SCROLLBAR", kScrollBar},
      {L"PROGRESS", kProgress}, {L"TAB", kTab},           {L"TRACKBAR", kTrackBar},  {L"HEADER", kHeader},
      {L"TOOLBAR", kToolBar},   {L"SPIN", kSpin},         {L"STATUS", kStatus},      {L"REBAR", kRebar},
      {L"TREEVIEW", kTreeView}, {L"LISTVIEW", kListView}, {L"TOOLTIP", kToolTip},    {L"WINDOW", kWindow},
  };
  for (const Entry& e : kClasses)
    if (_wcsicmp(name, e.name) == 0) return e.c;
  return kNoClass;
}

bool HasPart(Class c, int part) {
  // Drawing into no DC answers whether the part is there.
  HDC dc = CreateCompatibleDC(NULL);
  RECT r = {0, 0, 1, 1};
  bool ok = DrawPart(c, dc, part, 1, r, NULL);
  DeleteDC(dc);
  return ok;
}

bool DrawPart(Class c, HDC dc, int part, int state, const RECT& r, const RECT* clip) {
  if (!dc) return false;
  int saved = 0;
  if (clip) {
    saved = SaveDC(dc);
    IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
  }
  bool ok = false;
  switch (c) {
    case kButton: ok = DrawButton(dc, part, state, r); break;
    case kEdit: ok = DrawEdit(dc, part, state, r); break;
    case kCombo: ok = DrawCombo(dc, part, state, r); break;
    case kScrollBar: ok = DrawScroll(dc, part, state, r); break;
    case kProgress: ok = DrawProgress(dc, part, state, r); break;
    case kTab: ok = DrawTab(dc, part, state, r); break;
    case kTrackBar: ok = DrawTrack(dc, part, state, r); break;
    case kHeader: ok = DrawHeader(dc, part, state, r); break;
    case kToolBar: ok = DrawToolBar(dc, part, state, r); break;
    case kSpin: ok = DrawSpin(dc, part, state, r); break;
    case kStatus: ok = DrawStatus(dc, part, state, r); break;
    case kRebar: ok = DrawRebar(dc, part, state, r); break;
    case kTreeView: ok = DrawTree(dc, part, state, r); break;
    case kListView: ok = DrawList(dc, part, state, r); break;
    case kToolTip: ok = DrawToolTip(dc, part, state, r); break;
    default: break;
  }
  if (clip) RestoreDC(dc, saved);
  return ok;
}

bool PartTransparent(Class c, int part, int state) {
  switch (c) {
    case kButton: return part == BP_CHECKBOX || part == BP_RADIOBUTTON || part == BP_GROUPBOX ||
                         part == BP_COMMANDLINK || part == BP_COMMANDLINKGLYPH || part == BP_PUSHBUTTON;
    case kToolBar: return true;
    case kSpin:
    case kCombo: return part == CP_DROPDOWNBUTTON || part == CP_DROPDOWNBUTTONRIGHT || part == CP_DROPDOWNBUTTONLEFT ||
                        part == CP_TRANSPARENTBACKGROUND || c == kSpin;
    case kTreeView:
    case kListView:
    case kStatus:
    case kRebar: return true;
    case kTrackBar: return part != TKP_TRACK && part != TKP_TRACKVERT;
    case kEdit: return part >= EP_EDITBORDER_NOSCROLL;
    case kToolTip: return part == TTP_CLOSE;
    default: return false;
  }
  (void)state;
}

RECT ContentRect(Class c, int part, int, const RECT& r) {
  RECT in = r;
  int d = 0;
  switch (c) {
    case kButton: d = part == BP_PUSHBUTTON || part == BP_USERBUTTON ? Scale96(3) : part == BP_GROUPBOX ? 1 : 0; break;
    case kEdit:
    case kProgress:
    case kTab:
    case kToolTip: d = 1; break;
    case kCombo: d = part == CP_BORDER || part == CP_READONLY ? 1 : 0; break;
    case kHeader: d = Scale96(3); break;
    case kToolBar: d = Scale96(2); break;
    default: break;
  }
  InflateRect(&in, -d, -d);
  if (in.right < in.left) in.right = in.left;
  if (in.bottom < in.top) in.bottom = in.top;
  return in;
}

bool PartSize(Class c, int part, int, const RECT* r, int which, SIZE* out) {
  int box = BoxSize();
  int bar = GetSystemMetrics(SM_CXVSCROLL);
  switch (c) {
    case kButton:
      if (part == BP_CHECKBOX || part == BP_RADIOBUTTON) {
        *out = SIZE{box, box};
        return true;
      }
      if (part == BP_COMMANDLINKGLYPH) {
        *out = SIZE{Scale96(20), Scale96(20)};
        return true;
      }
      if (part == BP_PUSHBUTTON) {
        // At least room for the borders and the content margins.
        *out = SIZE{Scale96(6) + 2, Scale96(6) + 2};
        if (which == TS_DRAW && r) *out = SIZE{r->right - r->left, r->bottom - r->top};
        return true;
      }
      return false;
    case kScrollBar:
      if (part == SBP_ARROWBTN || part == SBP_SIZEBOX || part == SBP_GRIPPERHORZ || part == SBP_GRIPPERVERT) {
        *out = SIZE{bar, GetSystemMetrics(SM_CYHSCROLL)};
        return true;
      }
      if (part == SBP_THUMBBTNHORZ || part == SBP_THUMBBTNVERT) {
        *out = SIZE{GetSystemMetrics(SM_CXHTHUMB), GetSystemMetrics(SM_CYVTHUMB)};
        return true;
      }
      return false;
    case kCombo:
      if (part == CP_DROPDOWNBUTTON || part == CP_DROPDOWNBUTTONRIGHT || part == CP_DROPDOWNBUTTONLEFT) {
        *out = SIZE{bar, GetSystemMetrics(SM_CYHSCROLL)};
        return true;
      }
      return false;
    case kTrackBar:
      if (part == TKP_THUMB || part == TKP_THUMBBOTTOM || part == TKP_THUMBTOP) {
        *out = SIZE{Scale96(11), Scale96(21)};
        return true;
      }
      if (part == TKP_THUMBVERT || part == TKP_THUMBLEFT || part == TKP_THUMBRIGHT) {
        *out = SIZE{Scale96(21), Scale96(11)};
        return true;
      }
      if (part == TKP_TRACK || part == TKP_TRACKVERT) {
        *out = part == TKP_TRACK ? SIZE{Scale96(4), Scale96(4)} : SIZE{Scale96(4), Scale96(4)};
        return true;
      }
      if (part == TKP_TICS || part == TKP_TICSVERT) {
        *out = part == TKP_TICS ? SIZE{1, Scale96(4)} : SIZE{Scale96(4), 1};
        return true;
      }
      return false;
    case kHeader:
      if (part == HP_HEADERSORTARROW || part == HP_HEADERDROPDOWN || part == HP_HEADERDROPDOWNFILTER) {
        *out = SIZE{Scale96(9), Scale96(5)};
        return true;
      }
      return false;
    case kTreeView:
      if (part == TVP_GLYPH || part == TVP_HOTGLYPH) {
        *out = SIZE{Scale96(16), Scale96(16)};
        return true;
      }
      return false;
    case kSpin: *out = SIZE{bar, Scale96(9)}; return part >= SPNP_UP && part <= SPNP_DOWNHORZ;
    case kStatus:
      if (part == SP_GRIPPER) {
        *out = SIZE{Scale96(16), Scale96(16)};
        return true;
      }
      return false;
    case kRebar:
      if (part == RP_GRIPPER || part == RP_GRIPPERVERT) {
        *out = SIZE{Scale96(5), Scale96(5)};
        return true;
      }
      if (part == RP_CHEVRON || part == RP_CHEVRONVERT) {
        *out = SIZE{Scale96(14), Scale96(14)};
        return true;
      }
      return false;
    case kToolBar:
      if (part == TP_SPLITBUTTONDROPDOWN || part == TP_DROPDOWNBUTTONGLYPH) {
        *out = SIZE{Scale96(12), Scale96(12)};
        return true;
      }
      if (part == TP_SEPARATOR || part == TP_SEPARATORVERT) {
        *out = SIZE{Scale96(6), Scale96(6)};
        return true;
      }
      return false;
    case kToolTip:
      if (part == TTP_CLOSE) {
        *out = SIZE{Scale96(16), Scale96(16)};
        return true;
      }
      return false;
    default: return false;
  }
}

bool Color(Class c, int part, int state, int prop, COLORREF* out) {
  bool disabled = false;
  switch (c) {
    case kButton:
      disabled = (part == BP_PUSHBUTTON && state == PBS_DISABLED) || (part == BP_GROUPBOX && state == GBS_DISABLED) ||
                 ((part == BP_CHECKBOX || part == BP_RADIOBUTTON) && (state - 1) % 4 == 3);
      break;
    case kEdit: disabled = part == EP_EDITTEXT && state == ETS_DISABLED; break;
    case kTab: disabled = state == TIS_DISABLED; break;
    case kToolBar: disabled = state == TS_DISABLED; break;
    case kTreeView:
    case kListView: disabled = state == LISS_DISABLED; break;
    default: break;
  }
  switch (prop) {
    case TMT_TEXTCOLOR:
      if (c == kButton && part == BP_COMMANDLINK) {
        *out = disabled ? GetSysColor(COLOR_GRAYTEXT) : Mix(Accent(), RGB(0, 0, 0), 20);
        return true;
      }
      if (c == kButton && part == BP_GROUPBOX) {
        *out = disabled ? GetSysColor(COLOR_GRAYTEXT) : Mix(Accent(), RGB(0, 0, 0), 30);
        return true;
      }
      *out = disabled ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(c == kToolTip ? COLOR_INFOTEXT : COLOR_BTNTEXT);
      return true;
    case TMT_FILLCOLOR:
      *out = disabled ? kDisabledFill
                      : c == kProgress && (part == PP_FILL || part == PP_FILLVERT || part == PP_CHUNK)
                            ? ProgressColor(state)
                            : (c == kEdit || c == kTab || c == kHeader || c == kToolTip || c == kListView ||
                               c == kTreeView)
                                  ? kWhite
                                  : RGB(251, 251, 251);
      return true;
    case TMT_BORDERCOLOR:
      *out = disabled ? kDisabledBorder : c == kTab ? kLine : c == kEdit ? RGB(122, 122, 122) : kBorder;
      return true;
    case TMT_EDGELIGHTCOLOR: *out = RGB(227, 227, 227); return true;
    case TMT_EDGEHIGHLIGHTCOLOR: *out = kWhite; return true;
    case TMT_EDGESHADOWCOLOR: *out = RGB(160, 160, 160); return true;
    case TMT_EDGEDKSHADOWCOLOR: *out = RGB(105, 105, 105); return true;
    case TMT_EDGEFILLCOLOR: *out = GetSysColor(COLOR_BTNFACE); return true;
    case TMT_ACCENTCOLORHINT: *out = Accent(); return true;
    case TMT_FILLCOLORHINT: *out = RGB(251, 251, 251); return true;
    case TMT_BORDERCOLORHINT: *out = kBorder; return true;
    case TMT_GLYPHTEXTCOLOR: *out = disabled ? kDisabledGlyph : kGlyph; return true;
  }
  return false;
}

bool Int(Class c, int part, int, int prop, int* out) {
  switch (prop) {
    case TMT_BORDERSIZE: *out = 1; return true;
    case TMT_ROUNDCORNERWIDTH:
    case TMT_ROUNDCORNERHEIGHT: *out = c == kButton && part == BP_PUSHBUTTON ? 2 : 0; return true;
    case TMT_PROGRESSCHUNKSIZE:
      if (c != kProgress) return false;
      *out = Scale96(8);
      return true;
    case TMT_PROGRESSSPACESIZE:
      if (c != kProgress) return false;
      *out = Scale96(2);
      return true;
    case TMT_TEXTBORDERSIZE: *out = 0; return true;
    case TMT_IMAGECOUNT: *out = 1; return true;
    case TMT_ALPHALEVEL: *out = 255; return true;
  }
  return false;
}

bool Margins(Class c, int part, int state, int prop, MARGINS* out) {
  if (prop != TMT_CONTENTMARGINS && prop != TMT_SIZINGMARGINS && prop != TMT_CAPTIONMARGINS) return false;
  RECT big = {0, 0, 1000, 1000};
  RECT in = ContentRect(c, part, state, big);
  if (prop == TMT_SIZINGMARGINS) in = RECT{1, 1, 999, 999};
  *out = MARGINS{in.left, 1000 - in.right, in.top, 1000 - in.bottom};
  return true;
}

COLORREF SysColor(int index, COLORREF classic) {
  if (!AppThemed()) return classic;
  switch (index) {
    case COLOR_SCROLLBAR: return RGB(200, 200, 200);
    case COLOR_MENU:
    case COLOR_MENUBAR:
    case COLOR_BTNFACE: return RGB(240, 240, 240);
    case COLOR_ACTIVEBORDER: return RGB(180, 180, 180);
    case COLOR_INACTIVEBORDER: return RGB(244, 247, 252);
    case COLOR_APPWORKSPACE: return RGB(171, 171, 171);
    case COLOR_HIGHLIGHT: return RGB(30, 110, 200);
    case COLOR_BTNSHADOW: return RGB(160, 160, 160);
    case COLOR_GRAYTEXT: return RGB(110, 110, 110);
    case COLOR_3DDKSHADOW: return RGB(100, 100, 100);
    case COLOR_3DLIGHT: return RGB(227, 227, 227);
    case COLOR_HOTLIGHT: return RGB(20, 95, 180);
    case COLOR_ACTIVECAPTION: return RGB(30, 110, 200);
    case COLOR_INACTIVECAPTION: return RGB(200, 200, 200);
    case COLOR_CAPTIONTEXT: return RGB(255, 255, 255);
    case COLOR_INACTIVECAPTIONTEXT: return RGB(90, 90, 90);
    case COLOR_GRADIENTACTIVECAPTION: return RGB(70, 145, 225);
    case COLOR_GRADIENTINACTIVECAPTION: return RGB(220, 220, 220);
    case COLOR_MENUHILIGHT: return RGB(30, 110, 200);
    case COLOR_INFOBK: return RGB(255, 255, 255);
  }
  return classic;
}

}  // namespace theme
}  // namespace winport
