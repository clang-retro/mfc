// user_button.cpp - the Button, Static and ScrollBar controls.
//
// Each control keeps its state in a structure whose address is in the
// window's extra bytes, and asks its parent for colors with the
// WM_CTLCOLOR messages, as on Windows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <commctrl.h>
#include <string.h>

#include <algorithm>
#include <string>

using namespace winport;

namespace {

template <class T>
T* StateOf(HWND h) {
  return (T*)GetWindowLongPtrW(h, 0);
}

template <class T>
T* NewState(HWND h) {
  T* s = new T;
  SetWindowLongPtrW(h, 0, (LONG_PTR)s);
  return s;
}

template <class T>
void FreeState(HWND h) {
  delete StateOf<T>(h);
  SetWindowLongPtrW(h, 0, 0);
}

void Notify(HWND h, UINT code) {
  HWND parent = GetParent(h);
  if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), code), (LPARAM)h);
}

// The brush the parent gives for a control's background; the parent may
// also set the DC's colors.
HBRUSH CtlColor(HWND h, HDC dc, UINT msg) {
  HWND parent = GetParent(h);
  HBRUSH br = NULL;
  if (parent) br = (HBRUSH)SendMessageW(parent, msg, (WPARAM)dc, (LPARAM)h);
  if (!br) br = (HBRUSH)DefWindowProcW(parent ? parent : h, msg, (WPARAM)dc, (LPARAM)h);
  return br;
}

std::wstring TextOf(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s(n + 1, L'\0');
  n = GetWindowTextW(h, &s[0], n + 1);
  s.resize(n > 0 ? n : 0);
  return s;
}

// Text in a control's font; a disabled control's looks etched.
void DrawLabel(HDC dc, const std::wstring& text, RECT r, UINT fmt, bool enabled) {
  int mode = SetBkMode(dc, TRANSPARENT);
  if (enabled) {
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, fmt);
  } else {
    COLORREF old = SetTextColor(dc, GetSysColor(COLOR_BTNHIGHLIGHT));
    RECT s = r;
    OffsetRect(&s, 1, 1);
    DrawTextW(dc, text.c_str(), (int)text.size(), &s, fmt);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, fmt);
    SetTextColor(dc, old);
  }
  SetBkMode(dc, mode);
}

SIZE ImageSize(HANDLE image, UINT type) {
  SIZE s = {0, 0};
  if (!image) return s;
  if (type == IMAGE_BITMAP) {
    BITMAP bm;
    if (GetObjectW(image, sizeof bm, &bm)) s = SIZE{bm.bmWidth, bm.bmHeight};
  } else {
    ICONINFO ii;
    if (GetIconInfo((HICON)image, &ii)) {
      BITMAP bm;
      if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof bm, &bm)) {
        s = SIZE{bm.bmWidth, bm.bmHeight};
      } else if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof bm, &bm)) {
        s = SIZE{bm.bmWidth, bm.bmHeight / 2};
      }
      if (ii.hbmColor) DeleteObject(ii.hbmColor);
      if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
  }
  return s;
}

void DrawImage(HDC dc, HANDLE image, UINT type, int x, int y, bool enabled) {
  SIZE s = ImageSize(image, type);
  if (type == IMAGE_BITMAP) {
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, image);
    BitBlt(dc, x, y, s.cx, s.cy, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
  } else {
    DrawIconEx(dc, x, y, (HICON)image, s.cx, s.cy, 0, NULL, DI_NORMAL);
  }
  (void)enabled;
}

// ---------------------------------------------------------------------
// Buttons

struct ButtonState {
  UINT check = BST_UNCHECKED;
  bool pushed = false, focus = false, tracking = false, spacePressed = false;
  HFONT font = NULL;
  HANDLE image = NULL;
  UINT imageType = IMAGE_BITMAP;
  // The Vista-era parts: a split button's drop-down part, a command
  // link's note, the shield, an image list and the text's margins.
  bool dropDown = false, shield = false, hot = false;
  UINT splitStyle = 0;
  SIZE splitSize = {0, 0};  // 0 wide: the default
  HIMAGELIST splitImages = NULL;
  WCHAR glyph = 0;  // a Marlett character, or 0 for the arrow
  std::wstring note;
  BUTTON_IMAGELIST images = {NULL, {0, 0, 0, 0}, 0};
  RECT margin = {1, 1, 1, 1};
};

UINT TypeOf(HWND h) { return GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK; }

bool IsCheck(UINT t) { return t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE; }
bool IsRadio(UINT t) { return t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON; }
bool IsPush(UINT t) { return t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON || t == BS_PUSHBOX || t == BS_USERBUTTON; }
bool IsSplit(UINT t) { return t == BS_SPLITBUTTON || t == BS_DEFSPLITBUTTON; }
bool IsLink(UINT t) { return t == BS_COMMANDLINK || t == BS_DEFCOMMANDLINK; }
bool IsDefault(UINT t) { return t == BS_DEFPUSHBUTTON || t == BS_DEFSPLITBUTTON || t == BS_DEFCOMMANDLINK; }

// The split part's width: as set, or the arrow's room.
int SplitWidth(ButtonState* s) { return s->splitSize.cx > 0 ? s->splitSize.cx : 16; }

// Where a split button's drop-down part is, at its right (or left).
RECT SplitPart(HWND h, ButtonState* s) {
  RECT rc;
  GetClientRect(h, &rc);
  int w = SplitWidth(s);
  if (s->splitStyle & BCSS_ALIGNLEFT)
    rc.right = rc.left + w;
  else
    rc.left = rc.right - w;
  return rc;
}

// A small triangle pointing down, in the text color.
void DrawArrow(HDC dc, RECT r, bool enabled) {
  int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
  HBRUSH br = GetSysColorBrush(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
  for (int i = 0; i < 4; i++) {
    RECT line = {cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i};
    FillRect(dc, &line, br);
  }
}

// The command link's arrow: a green circle with a white arrow in it.
void DrawLinkArrow(HDC dc, int x, int y, bool enabled) {
  HBRUSH br = CreateSolidBrush(enabled ? RGB(0, 140, 60) : GetSysColor(COLOR_GRAYTEXT));
  HGDIOBJ oldBrush = SelectObject(dc, br), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, x, y, x + 17, y + 17);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(br);
  HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
  RECT shaft = {x + 4, y + 7, x + 10, y + 10};
  FillRect(dc, &shaft, white);
  for (int i = 0; i < 4; i++) {
    RECT head = {x + 10 + i, y + 4 + i, x + 11 + i, y + 13 - i};
    FillRect(dc, &head, white);
  }
}

// The shield that marks a step needing an administrator.
void DrawShield(HDC dc, int x, int y) {
  POINT pts[] = {{x + 1, y + 1}, {x + 8, y}, {x + 15, y + 1}, {x + 15, y + 7}, {x + 8, y + 15}, {x + 1, y + 7}};
  HBRUSH br = CreateSolidBrush(RGB(40, 90, 200));
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(20, 50, 130));
  HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
  Polygon(dc, pts, 6);
  SelectObject(dc, op);
  SelectObject(dc, ob);
  DeleteObject(pen);
  DeleteObject(br);
  RECT gold = {x + 8, y + 2, x + 14, y + 7};
  HBRUSH g = CreateSolidBrush(RGB(250, 200, 40));
  FillRect(dc, &gold, g);
  RECT gold2 = {x + 3, y + 7, x + 8, y + 11};
  FillRect(dc, &gold2, g);
  DeleteObject(g);
}

// The image list's picture for the button's state, and its size.
int ImageIndex(HWND h, ButtonState* s) {
  int n = s->images.himl ? ImageList_GetImageCount(s->images.himl) : 0;
  if (n <= 1) return 0;
  // The order of the pictures: normal, hot, pressed, disabled, default.
  int state = !IsWindowEnabled(h) ? 4 : s->pushed ? 3 : s->hot ? 2 : s->focus ? 5 : 1;
  return std::min(n, state) - 1;
}
SIZE ImagesSize(ButtonState* s) {
  SIZE z = {0, 0};
  int cx, cy;
  if (s->images.himl && ImageList_GetIconSize(s->images.himl, &cx, &cy)) z = SIZE{cx, cy};
  return z;
}

// The image list's picture, put where its alignment says; r loses the
// room it takes.
void DrawImages(HWND h, HDC dc, ButtonState* s, RECT* r) {
  if (!s->images.himl || ImageList_GetImageCount(s->images.himl) <= 0) return;
  SIZE is = ImagesSize(s);
  const RECT& m = s->images.margin;
  int x, y;
  switch (s->images.uAlign) {
    case BUTTON_IMAGELIST_ALIGN_RIGHT:
      x = r->right - m.right - is.cx;
      y = (r->top + r->bottom - is.cy) / 2;
      r->right = x - m.left;
      break;
    case BUTTON_IMAGELIST_ALIGN_TOP:
      x = (r->left + r->right - is.cx) / 2;
      y = r->top + m.top;
      r->top = y + is.cy + m.bottom;
      break;
    case BUTTON_IMAGELIST_ALIGN_BOTTOM:
      x = (r->left + r->right - is.cx) / 2;
      y = r->bottom - m.bottom - is.cy;
      r->bottom = y - m.top;
      break;
    case BUTTON_IMAGELIST_ALIGN_CENTER:
      x = (r->left + r->right - is.cx) / 2;
      y = (r->top + r->bottom - is.cy) / 2;
      break;
    default:
      x = r->left + m.left;
      y = (r->top + r->bottom - is.cy) / 2;
      r->left = x + is.cx + m.right;
      break;
  }
  ImageList_Draw(s->images.himl, ImageIndex(h, s), dc, x, y, ILD_TRANSPARENT);
}

UINT TextFormat(DWORD style, bool push) {
  UINT fmt = (style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
  switch (style & BS_CENTER) {
    case BS_LEFT: fmt |= DT_LEFT; break;
    case BS_RIGHT: fmt |= DT_RIGHT; break;
    case BS_CENTER: fmt |= DT_CENTER; break;
    default: fmt |= push ? DT_CENTER : DT_LEFT; break;
  }
  if (!(style & BS_MULTILINE)) {
    switch (style & BS_VCENTER) {
      case BS_TOP: fmt |= DT_TOP; break;
      case BS_BOTTOM: fmt |= DT_BOTTOM; break;
      default: fmt |= DT_VCENTER; break;
    }
  }
  return fmt;
}

// Draws the text or image of a button within r; returns where the text went.
RECT DrawContent(HWND h, HDC dc, ButtonState* s, RECT r, DWORD style, bool push) {
  bool enabled = IsWindowEnabled(h) != FALSE;
  if ((style & (BS_ICON | BS_BITMAP)) && s->image) {
    SIZE is = ImageSize(s->image, s->imageType);
    int x = (r.left + r.right - is.cx) / 2, y = (r.top + r.bottom - is.cy) / 2;
    DrawImage(dc, s->image, s->imageType, x, y, enabled);
    return MakeRect(x, y, x + is.cx, y + is.cy);
  }
  std::wstring text = TextOf(h);
  UINT fmt = TextFormat(style, push);
  RECT calc = r;
  DrawTextW(dc, text.c_str(), (int)text.size(), &calc, fmt | DT_CALCRECT);
  // Place the calculated box as the format would.
  int w = calc.right - calc.left, ht = calc.bottom - calc.top;
  RECT box = r;
  if (fmt & DT_CENTER) box.left = (r.left + r.right - w) / 2;
  else if (fmt & DT_RIGHT) box.left = r.right - w;
  box.right = box.left + w;
  if (fmt & DT_VCENTER) box.top = (r.top + r.bottom - ht) / 2;
  else if (fmt & DT_BOTTOM) box.top = r.bottom - ht;
  box.bottom = box.top + ht;
  DrawLabel(dc, text, r, fmt, enabled);
  return box;
}

// A font like the button's, bigger: a command link's first line.
HFONT LinkFont(HDC dc, ButtonState* s) {
  LOGFONTW lf = {};
  HGDIOBJ f = s->font ? (HGDIOBJ)s->font : GetStockObject(DEFAULT_GUI_FONT);
  GetObjectW(f, sizeof lf, &lf);
  lf.lfHeight = lf.lfHeight ? lf.lfHeight * 4 / 3 : -15;
  (void)dc;
  return CreateFontIndirectW(&lf);
}

// The command link's layout: the text and the note in width w, with the
// heights they take.
void LinkLayout(HWND h, HDC dc, ButtonState* s, int w, int* textHeight, int* noteHeight) {
  std::wstring text = TextOf(h);
  HFONT big = LinkFont(dc, s);
  HGDIOBJ old = SelectObject(dc, big);
  RECT r = {0, 0, std::max(w, 1), 0};
  DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT);
  *textHeight = r.bottom;
  SelectObject(dc, s->font ? (HGDIOBJ)s->font : GetStockObject(DEFAULT_GUI_FONT));
  *noteHeight = 0;
  if (!s->note.empty()) {
    RECT n = {0, 0, std::max(w, 1), 0};
    DrawTextW(dc, s->note.c_str(), -1, &n, DT_WORDBREAK | DT_CALCRECT);
    *noteHeight = n.bottom;
  }
  SelectObject(dc, old);
  DeleteObject(big);
}

const int kLinkIndent = 10 + 17 + 8;  // left margin, arrow, gap

void PaintLink(HWND h, HDC dc, ButtonState* s, RECT rc, DWORD style, bool enabled) {
  HBRUSH br = CtlColor(h, dc, WM_CTLCOLORBTN);
  FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_BTNFACE));
  if (s->pushed) {
    DrawEdge(dc, &rc, BDR_SUNKENOUTER, BF_RECT);
  } else if (s->hot || IsDefault(style & BS_TYPEMASK) || s->focus) {
    FrameRect(dc, &rc, GetSysColorBrush(s->hot ? COLOR_HOTLIGHT : COLOR_BTNSHADOW));
  }
  int off = s->pushed ? 1 : 0;
  int x = rc.left + 10 + off, y = rc.top + 10 + off;
  if (s->images.himl && ImageList_GetImageCount(s->images.himl) > 0)
    ImageList_Draw(s->images.himl, ImageIndex(h, s), dc, x, y, ILD_TRANSPARENT);
  else if (s->shield)
    DrawShield(dc, x, y);
  else
    DrawLinkArrow(dc, x, y, enabled);
  RECT text = {rc.left + kLinkIndent + off, rc.top + 8 + off, rc.right - 8 + off, rc.bottom - 4 + off};
  int th, nh;
  LinkLayout(h, dc, s, text.right - text.left, &th, &nh);
  int mode = SetBkMode(dc, TRANSPARENT);
  HFONT big = LinkFont(dc, s);
  HGDIOBJ old = SelectObject(dc, big);
  SetTextColor(dc, enabled ? RGB(0, 51, 153) : GetSysColor(COLOR_GRAYTEXT));
  std::wstring label = TextOf(h);
  DrawTextW(dc, label.c_str(), -1, &text, DT_WORDBREAK);
  SelectObject(dc, s->font ? (HGDIOBJ)s->font : GetStockObject(DEFAULT_GUI_FONT));
  if (!s->note.empty()) {
    RECT n = text;
    n.top += th + 2;
    SetTextColor(dc, enabled ? GetSysColor(COLOR_BTNTEXT) : GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, s->note.c_str(), -1, &n, DT_WORDBREAK);
  }
  SelectObject(dc, old);
  DeleteObject(big);
  SetBkMode(dc, mode);
  if (s->focus) {
    RECT f = rc;
    InflateRect(&f, -2, -2);
    DrawFocusRect(dc, &f);
  }
}

// BCM_GETIDEALSIZE: the size that shows all of the button. A command link
// keeps the width it is given (or has) and answers its height.
void IdealSize(HWND h, ButtonState* s, SIZE* out) {
  HDC dc = GetDC(h);
  HGDIOBJ old = SelectObject(dc, s->font ? (HGDIOBJ)s->font : GetStockObject(SYSTEM_FONT));
  UINT type = TypeOf(h);
  if (IsLink(type)) {
    RECT rc;
    GetClientRect(h, &rc);
    int w = out->cx > 0 ? out->cx : rc.right;
    int th, nh;
    LinkLayout(h, dc, s, w - kLinkIndent - 8, &th, &nh);
    out->cx = w;
    out->cy = std::max(8 + th + (nh ? nh + 2 : 0) + 10, 41);
  } else {
    std::wstring text = TextOf(h);
    RECT r = {0, 0, 0, 0};
    DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &r, DT_SINGLELINE | DT_CALCRECT);
    SIZE is = ImagesSize(s);
    int w = r.right + 8 + s->margin.left + s->margin.right, ht = r.bottom + 8 + s->margin.top + s->margin.bottom;
    if (s->images.himl) {
      if (s->images.uAlign == BUTTON_IMAGELIST_ALIGN_TOP || s->images.uAlign == BUTTON_IMAGELIST_ALIGN_BOTTOM)
        ht += is.cy + s->images.margin.top + s->images.margin.bottom;
      else
        w += is.cx + s->images.margin.left + s->images.margin.right;
      ht = std::max<int>(ht, is.cy + 8);
    }
    if (s->shield) w += 18;
    if (IsSplit(type)) w += SplitWidth(s);
    if (IsCheck(type) || IsRadio(type)) w += 17;
    out->cx = w;
    out->cy = ht;
  }
  SelectObject(dc, old);
  ReleaseDC(h, dc);
}

void Redraw(HWND h);

// A split button's drop-down: its part shows pushed while the parent
// handles BCN_DROPDOWN.
void DropDown(HWND h, ButtonState* s) {
  NMBCDROPDOWN nm;
  memset(&nm, 0, sizeof nm);
  nm.hdr.hwndFrom = h;
  nm.hdr.idFrom = (UINT_PTR)GetDlgCtrlID(h);
  nm.hdr.code = BCN_DROPDOWN;
  GetClientRect(h, &nm.rcButton);
  s->dropDown = true;
  Redraw(h);
  SendMessageW(GetParent(h), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
  if (IsWindow(h) && StateOf<ButtonState>(h) == s) {
    s->dropDown = false;
    Redraw(h);
  }
}

void PaintButton(HWND h, HDC dc) {
  ButtonState* s = StateOf<ButtonState>(h);
  if (!s) return;
  DWORD style = GetWindowLongW(h, GWL_STYLE);
  UINT type = style & BS_TYPEMASK;
  RECT rc;
  GetClientRect(h, &rc);
  HGDIOBJ oldFont = SelectObject(dc, s->font ? (HGDIOBJ)s->font : GetStockObject(SYSTEM_FONT));
  bool enabled = IsWindowEnabled(h) != FALSE;

  if (type == BS_OWNERDRAW) {
    DRAWITEMSTRUCT dis = {ODT_BUTTON, (UINT)GetDlgCtrlID(h), 0, ODA_DRAWENTIRE, 0, h, dc, rc, 0};
    if (s->pushed) dis.itemState |= ODS_SELECTED;
    if (s->focus) dis.itemState |= ODS_FOCUS;
    if (!enabled) dis.itemState |= ODS_DISABLED;
    CtlColor(h, dc, WM_CTLCOLORBTN);
    SendMessageW(GetParent(h), WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
    SelectObject(dc, oldFont);
    return;
  }

  if (type == BS_GROUPBOX) {
    HBRUSH br = CtlColor(h, dc, WM_CTLCOLORSTATIC);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    RECT frame = rc;
    frame.top += tm.tmHeight / 2;
    DrawEdge(dc, &frame, EDGE_ETCHED, BF_RECT | ((style & BS_FLAT) ? BF_FLAT | BF_MONO : 0));
    std::wstring text = TextOf(h);
    if (!text.empty()) {
      RECT t = {rc.left + 8, rc.top, rc.right - 8, rc.top + tm.tmHeight};
      RECT calc = t;
      DrawTextW(dc, text.c_str(), (int)text.size(), &calc, DT_SINGLELINE | DT_CALCRECT);
      if ((style & BS_CENTER) == BS_CENTER) OffsetRect(&calc, (t.right - calc.right) / 2, 0);
      else if ((style & BS_CENTER) == BS_RIGHT) OffsetRect(&calc, t.right - calc.right, 0);
      RECT bg = calc;
      InflateRect(&bg, 2, 0);
      FillRect(dc, &bg, br);
      DrawLabel(dc, text, calc, DT_SINGLELINE | DT_LEFT | DT_TOP, enabled);
    }
    SelectObject(dc, oldFont);
    return;
  }

  if (IsLink(type)) {
    PaintLink(h, dc, s, rc, style, enabled);
    SelectObject(dc, oldFont);
    return;
  }

  bool pushLike = (style & BS_PUSHLIKE) && (IsCheck(type) || IsRadio(type));
  if (IsSplit(type)) {
    HBRUSH br = CtlColor(h, dc, WM_CTLCOLORBTN);
    FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_BTNFACE));
    RECT r = rc;
    if (IsDefault(type)) {
      FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
      InflateRect(&r, -1, -1);
    }
    bool split = !(s->splitStyle & BCSS_NOSPLIT);
    RECT part = SplitPart(h, s);
    if (IsDefault(type)) InflateRect(&part, 0, -1);
    if (s->splitStyle & BCSS_ALIGNLEFT) part.left = r.left; else part.right = r.right;
    RECT main = r;
    if (s->splitStyle & BCSS_ALIGNLEFT) main.left = part.right; else main.right = part.left;
    UINT state = DFCS_BUTTONPUSH | (s->pushed ? DFCS_PUSHED : 0) | ((style & BS_FLAT) ? DFCS_FLAT : 0);
    if (split) {
      DrawFrameControl(dc, &main, DFC_BUTTON, state);
      DrawFrameControl(dc, &part, DFC_BUTTON,
                       DFCS_BUTTONPUSH | (s->dropDown ? DFCS_PUSHED : 0) | ((style & BS_FLAT) ? DFCS_FLAT : 0));
    } else {
      DrawFrameControl(dc, &r, DFC_BUTTON, state | (s->dropDown ? DFCS_PUSHED : 0));
    }
    RECT glyph = part;
    if (s->dropDown) OffsetRect(&glyph, 1, 1);
    if ((s->splitStyle & BCSS_IMAGE) && s->splitImages) {
      int cx = 0, cy = 0;
      ImageList_GetIconSize(s->splitImages, &cx, &cy);
      ImageList_Draw(s->splitImages, 0, dc, (glyph.left + glyph.right - cx) / 2, (glyph.top + glyph.bottom - cy) / 2,
                     ILD_TRANSPARENT);
    } else {
      DrawArrow(dc, glyph, enabled);
    }
    RECT content = main;
    InflateRect(&content, -3, -3);
    content.left += s->margin.left - 1;
    content.right -= s->margin.right - 1;
    if (s->pushed) OffsetRect(&content, 1, 1);
    DrawImages(h, dc, s, &content);
    if (s->shield) {
      DrawShield(dc, content.left, (content.top + content.bottom - 16) / 2);
      content.left += 18;
    }
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    DrawContent(h, dc, s, content, style, true);
    if (s->focus) {
      RECT f = main;
      InflateRect(&f, -3, -3);
      DrawFocusRect(dc, &f);
    }
    SelectObject(dc, oldFont);
    return;
  }
  if (IsPush(type) || pushLike) {
    HBRUSH br = CtlColor(h, dc, WM_CTLCOLORBTN);
    FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_BTNFACE));
    RECT r = rc;
    bool def = type == BS_DEFPUSHBUTTON;
    bool down = s->pushed || (pushLike && s->check == BST_CHECKED);
    if (def) {
      FrameRect(dc, &r, GetSysColorBrush(COLOR_WINDOWFRAME));
      InflateRect(&r, -1, -1);
    }
    UINT state = DFCS_BUTTONPUSH;
    if (down) state |= pushLike && !s->pushed ? DFCS_CHECKED : DFCS_PUSHED;
    if (style & BS_FLAT) state |= DFCS_FLAT;
    DrawFrameControl(dc, &r, DFC_BUTTON, state);
    RECT content = r;
    InflateRect(&content, -3, -3);
    content.left += s->margin.left - 1;
    content.top += s->margin.top - 1;
    content.right -= s->margin.right - 1;
    content.bottom -= s->margin.bottom - 1;
    if (down) OffsetRect(&content, 1, 1);
    DrawImages(h, dc, s, &content);
    if (s->shield) {
      DrawShield(dc, content.left, (content.top + content.bottom - 16) / 2);
      content.left += 18;
    }
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    DrawContent(h, dc, s, content, style, true);
    if (s->focus) {
      RECT f = r;
      InflateRect(&f, -3, -3);
      DrawFocusRect(dc, &f);
    }
    SelectObject(dc, oldFont);
    return;
  }

  // Check boxes and radio buttons: a box beside the text.
  HBRUSH br = CtlColor(h, dc, WM_CTLCOLORSTATIC);
  FillRect(dc, &rc, br ? br : GetSysColorBrush(COLOR_BTNFACE));
  const int box = GetSystemMetrics(SM_CXMENUCHECK);  // 13 at 96 DPI
  RECT b, text = rc;
  bool right = (style & BS_LEFTTEXT) != 0;
  int top;
  switch (style & BS_VCENTER) {
    case BS_TOP: top = rc.top; break;
    case BS_BOTTOM: top = rc.bottom - box; break;
    default: top = (rc.top + rc.bottom - box) / 2; break;
  }
  if (right) {
    b = MakeRect(rc.right - box, top, rc.right, top + box);
    text.right = b.left - Scale96(4);
  } else {
    b = MakeRect(rc.left, top, rc.left + box, top + box);
    text.left = b.right + Scale96(4);
  }
  UINT state = IsRadio(type) ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK;
  if (s->check == BST_CHECKED) state |= DFCS_CHECKED;
  if (s->check == BST_INDETERMINATE) state |= DFCS_CHECKED | DFCS_BUTTON3STATE;
  if (s->pushed) state |= DFCS_PUSHED;
  if (!enabled) state |= DFCS_INACTIVE;
  if (style & BS_FLAT) state |= DFCS_FLAT;
  DrawFrameControl(dc, &b, DFC_BUTTON, state);
  RECT where = DrawContent(h, dc, s, text, style, false);
  if (s->focus) {
    InflateRect(&where, 1, 1);
    IntersectRect(&where, &where, &rc);
    DrawFocusRect(dc, &where);
  }
  SelectObject(dc, oldFont);
}

void Redraw(HWND h) {
  InvalidateRect(h, NULL, FALSE);
  UpdateWindow(h);
}

// Unchecks the automatic radio buttons of the group around h.
void UncheckGroup(HWND h) {
  HWND parent = GetParent(h);
  if (!parent) return;
  std::vector<HWND> kids;
  for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) kids.push_back(c);
  int at = -1;
  for (int i = 0; i < (int)kids.size(); i++)
    if (kids[i] == h) at = i;
  if (at < 0) return;
  int start = at;
  while (start > 0 && !(GetWindowLongW(kids[start], GWL_STYLE) & WS_GROUP)) start--;
  int end = at + 1;
  while (end < (int)kids.size() && !(GetWindowLongW(kids[end], GWL_STYLE) & WS_GROUP)) end++;
  for (int i = start; i < end; i++) {
    if (kids[i] == h) continue;
    if ((GetWindowLongW(kids[i], GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON &&
        SendMessageW(kids[i], WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON)
      SendMessageW(kids[i], BM_SETCHECK, BST_UNCHECKED, 0);
  }
}

void Click(HWND h) {
  ButtonState* s = StateOf<ButtonState>(h);
  if (!s) return;
  switch (TypeOf(h)) {
    case BS_AUTOCHECKBOX:
      SendMessageW(h, BM_SETCHECK, s->check == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
      break;
    case BS_AUTO3STATE:
      SendMessageW(h, BM_SETCHECK, (s->check + 1) % 3, 0);
      break;
    case BS_AUTORADIOBUTTON:
      SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
      UncheckGroup(h);
      break;
  }
  Notify(h, BN_CLICKED);
}

LRESULT CALLBACK ButtonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ButtonState* s = StateOf<ButtonState>(h);
  if (!s && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE:
      NewState<ButtonState>(h);
      break;
    case WM_NCDESTROY:
      FreeState<ButtonState>(h);
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintButton(h, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_NCHITTEST:
      if (TypeOf(h) == BS_GROUPBOX) return HTTRANSPARENT;
      break;
    case WM_GETDLGCODE: {
      UINT t = TypeOf(h);
      if (t == BS_GROUPBOX) return DLGC_STATIC;
      if (IsDefault(t)) return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
      if (t == BS_PUSHBUTTON || t == BS_PUSHBOX || t == BS_USERBUTTON || t == BS_OWNERDRAW || IsSplit(t) || IsLink(t))
        return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
      if (IsRadio(t)) return DLGC_BUTTON | DLGC_RADIOBUTTON;
      return DLGC_BUTTON;
    }
    case WM_SETFONT:
      s->font = (HFONT)wp;
      if (lp) Redraw(h);
      return 0;
    case WM_GETFONT:
      return (LRESULT)s->font;
    case WM_SETTEXT: {
      LRESULT r = DefWindowProcW(h, msg, wp, lp);
      Redraw(h);
      return r;
    }
    case WM_ENABLE:
      Redraw(h);
      return 0;
    case WM_SETFOCUS:
      s->focus = true;
      Redraw(h);
      if (GetWindowLongW(h, GWL_STYLE) & BS_NOTIFY) Notify(h, BN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      s->focus = false;
      if (s->tracking && GetCapture() == h) ReleaseCapture();
      s->pushed = false;
      s->spacePressed = false;
      Redraw(h);
      if (GetWindowLongW(h, GWL_STYLE) & BS_NOTIFY) Notify(h, BN_KILLFOCUS);
      return 0;
    case WM_LBUTTONDBLCLK:
      if (GetWindowLongW(h, GWL_STYLE) & BS_NOTIFY) {
        UINT t = TypeOf(h);
        if (IsRadio(t) || t == BS_OWNERDRAW || IsPush(t) || IsSplit(t) || IsLink(t)) {
          Notify(h, BN_DOUBLECLICKED);
          return 0;
        }
      }
      // fall through: a second click
    case WM_LBUTTONDOWN:
      if (TypeOf(h) == BS_GROUPBOX) return 0;
      if (IsSplit(TypeOf(h))) {
        // The drop-down part, or all of a button that has no split.
        RECT part = SplitPart(h, s);
        POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
        if ((s->splitStyle & BCSS_NOSPLIT) || PtInRect(&part, p)) {
          if (GetFocus() != h) SetFocus(h);
          DropDown(h, s);
          return 0;
        }
      }
      if (GetFocus() != h) SetFocus(h);
      SetCapture(h);
      s->tracking = true;
      s->pushed = true;
      Redraw(h);
      return 0;
    case WM_MOUSELEAVE:
      if (s->hot) {
        s->hot = false;
        if (IsLink(TypeOf(h))) Redraw(h);
      }
      return 0;
    case WM_MOUSEMOVE:
      if (!s->hot) {
        s->hot = true;
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
        if (IsLink(TypeOf(h))) Redraw(h);
      }
      if (s->tracking) {
        RECT rc;
        GetClientRect(h, &rc);
        POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
        bool in = PtInRect(&rc, p) != FALSE;
        if (in != s->pushed) {
          s->pushed = in;
          Redraw(h);
        }
      }
      return 0;
    case WM_LBUTTONUP:
      if (s->tracking) {
        bool clicked = s->pushed;
        s->tracking = false;
        s->pushed = false;
        if (GetCapture() == h) ReleaseCapture();
        Redraw(h);
        if (clicked) Click(h);
      }
      return 0;
    case WM_CAPTURECHANGED:
      if (s->tracking) {
        s->tracking = false;
        s->pushed = false;
        Redraw(h);
      }
      return 0;
    case WM_SYSKEYDOWN:
      if (wp == VK_DOWN && IsSplit(TypeOf(h))) {
        DropDown(h, s);
        return 0;
      }
      break;
    case WM_KEYDOWN:
      if (wp == VK_F4 && IsSplit(TypeOf(h))) {
        DropDown(h, s);
        return 0;
      }
      if (wp == VK_SPACE && !(HIWORD(lp) & KF_REPEAT)) {
        s->spacePressed = true;
        s->pushed = true;
        Redraw(h);
        return 0;
      }
      break;
    case WM_KEYUP:
      if (wp == VK_SPACE && s->spacePressed) {
        s->spacePressed = false;
        s->pushed = false;
        Redraw(h);
        Click(h);
        return 0;
      }
      break;
    case WM_CHAR:
      if (IsCheck(TypeOf(h)) && (wp == '+' || wp == '=' || wp == '-')) {
        UINT t = TypeOf(h);
        if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE) {
          SendMessageW(h, BM_SETCHECK, wp == '-' ? BST_UNCHECKED : BST_CHECKED, 0);
          Notify(h, BN_CLICKED);
        }
        return 0;
      }
      break;
    case BM_GETCHECK: {
      UINT t = TypeOf(h);
      return IsCheck(t) || IsRadio(t) ? s->check : 0;
    }
    case BM_SETCHECK: {
      UINT t = TypeOf(h);
      if (!IsCheck(t) && !IsRadio(t)) return 0;
      UINT check = (UINT)wp;
      if (check == BST_INDETERMINATE && t != BS_3STATE && t != BS_AUTO3STATE) check = BST_CHECKED;
      if (check > BST_INDETERMINATE) check = BST_CHECKED;
      if (IsRadio(t)) {
        // The checked radio button of a group is the one Tab reaches.
        LONG style = GetWindowLongW(h, GWL_STYLE);
        LONG want = check ? (style | WS_TABSTOP) : (style & ~WS_TABSTOP);
        if (want != style) SetWindowLongW(h, GWL_STYLE, want);
      }
      if (check != s->check) {
        s->check = check;
        Redraw(h);
      }
      return 0;
    }
    case BM_GETSTATE:
      return s->check | (s->pushed ? BST_PUSHED : 0) | (s->focus ? BST_FOCUS : 0) | (s->hot ? BST_HOT : 0) |
             (s->dropDown ? BST_DROPDOWNPUSHED : 0);
    case BM_SETSTATE:
      if ((wp != 0) != s->pushed) {
        s->pushed = wp != 0;
        Redraw(h);
      }
      return 0;
    case BM_SETSTYLE: {
      LONG style = GetWindowLongW(h, GWL_STYLE);
      UINT type = (UINT)(wp & BS_TYPEMASK), now = style & BS_TYPEMASK;
      // The dialog manager moves the default between push buttons; a
      // split button or command link stays one.
      if ((IsSplit(now) || IsLink(now)) && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON))
        type = (IsSplit(now) ? BS_SPLITBUTTON : BS_COMMANDLINK) | (type == BS_DEFPUSHBUTTON ? 1 : 0);
      SetWindowLongW(h, GWL_STYLE, (style & ~BS_TYPEMASK) | type);
      if (lp) Redraw(h);
      return 0;
    }
    case BM_CLICK:
      if (IsWindowEnabled(h)) {
        s->pushed = true;
        Redraw(h);
        s->pushed = false;
        Redraw(h);
        Click(h);
      }
      return 0;
    case BCM_GETIDEALSIZE: {
      SIZE* sz = (SIZE*)lp;
      if (!sz) return FALSE;
      IdealSize(h, s, sz);
      return TRUE;
    }
    case BCM_SETIMAGELIST: {
      const BUTTON_IMAGELIST* il = (const BUTTON_IMAGELIST*)lp;
      if (!il) return FALSE;
      s->images = *il;
      Redraw(h);
      return TRUE;
    }
    case BCM_GETIMAGELIST:
      if (!lp) return FALSE;
      *(BUTTON_IMAGELIST*)lp = s->images;
      return TRUE;
    case BCM_SETTEXTMARGIN:
      if (!lp) return FALSE;
      s->margin = *(const RECT*)lp;
      Redraw(h);
      return TRUE;
    case BCM_GETTEXTMARGIN:
      if (!lp) return FALSE;
      *(RECT*)lp = s->margin;
      return TRUE;
    case BCM_SETDROPDOWNSTATE:
      if (!IsSplit(TypeOf(h))) return FALSE;
      s->dropDown = wp != 0;
      Redraw(h);
      return TRUE;
    case BCM_SETSPLITINFO: {
      const BUTTON_SPLITINFO* si = (const BUTTON_SPLITINFO*)lp;
      if (!si) return FALSE;
      if (si->mask & BCSIF_GLYPH) s->glyph = (WCHAR)(UINT_PTR)si->himlGlyph;
      if (si->mask & BCSIF_IMAGE) s->splitImages = si->himlGlyph;
      if (si->mask & BCSIF_STYLE) s->splitStyle = si->uSplitStyle;
      if (si->mask & BCSIF_SIZE) s->splitSize = si->size;
      Redraw(h);
      return TRUE;
    }
    case BCM_GETSPLITINFO: {
      BUTTON_SPLITINFO* si = (BUTTON_SPLITINFO*)lp;
      if (!si) return FALSE;
      if (si->mask & BCSIF_GLYPH) si->himlGlyph = (HIMAGELIST)(UINT_PTR)(s->glyph ? s->glyph : 6);
      if (si->mask & BCSIF_IMAGE) si->himlGlyph = s->splitImages;
      if (si->mask & BCSIF_STYLE) si->uSplitStyle = s->splitStyle;
      if (si->mask & BCSIF_SIZE) si->size = SIZE{SplitWidth(s), s->splitSize.cy};
      return TRUE;
    }
    case BCM_SETNOTE:
      if (!IsLink(TypeOf(h))) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
      }
      s->note = lp ? (const WCHAR*)lp : L"";
      Redraw(h);
      return TRUE;
    case BCM_GETNOTE: {
      DWORD* cch = (DWORD*)wp;
      if (!IsLink(TypeOf(h))) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
      }
      if (!cch) return FALSE;
      if (!lp || *cch < s->note.size() + 1) {
        *cch = (DWORD)s->note.size() + 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
      }
      memcpy((WCHAR*)lp, s->note.c_str(), (s->note.size() + 1) * sizeof(WCHAR));
      *cch = (DWORD)s->note.size();
      return TRUE;
    }
    case BCM_GETNOTELENGTH:
      return IsLink(TypeOf(h)) ? (LRESULT)s->note.size() : 0;
    case BCM_SETSHIELD:
      s->shield = lp != 0;
      Redraw(h);
      return TRUE;
    case BM_GETIMAGE:
      return wp == s->imageType ? (LRESULT)s->image : 0;
    case BM_SETIMAGE: {
      HANDLE old = s->image;
      s->image = (HANDLE)lp;
      s->imageType = (UINT)wp;
      Redraw(h);
      return (LRESULT)old;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------
// Statics

struct StaticState {
  HFONT font = NULL;
  HANDLE image = NULL;
  UINT imageType = IMAGE_ICON;
};

UINT StaticType(HWND h) { return GetWindowLongW(h, GWL_STYLE) & SS_TYPEMASK; }

// Sizes an icon or bitmap static to its image.
void FitImage(HWND h, StaticState* s) {
  DWORD style = GetWindowLongW(h, GWL_STYLE);
  if (!s->image || (style & (SS_CENTERIMAGE | SS_REALSIZECONTROL))) return;
  SIZE is = ImageSize(s->image, s->imageType);
  if (is.cx && is.cy)
    SetWindowPos(h, NULL, 0, 0, is.cx, is.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The image a static's text names: an icon or bitmap resource.
void LoadNamedImage(HWND h, StaticState* s, LPCWSTR text) {
  UINT type = StaticType(h);
  if (type != SS_ICON && type != SS_BITMAP) return;
  if (!text || !*text) return;
  HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
  LPCWSTR name = text[0] == 0xFFFF ? MAKEINTRESOURCEW(text[1]) : text;
  HANDLE image;
  if (type == SS_ICON) {
    image = LoadImageW(inst, name, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!image) image = LoadImageW(inst, name, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    s->imageType = IMAGE_ICON;
  } else {
    image = LoadImageW(inst, name, IMAGE_BITMAP, 0, 0, 0);
    s->imageType = IMAGE_BITMAP;
  }
  if (image) {
    s->image = image;
    FitImage(h, s);
  }
}

void PaintStatic(HWND h, HDC dc) {
  StaticState* s = StateOf<StaticState>(h);
  if (!s) return;
  DWORD style = GetWindowLongW(h, GWL_STYLE);
  UINT type = style & SS_TYPEMASK;
  RECT rc;
  GetClientRect(h, &rc);
  HGDIOBJ oldFont = SelectObject(dc, s->font ? (HGDIOBJ)s->font : GetStockObject(SYSTEM_FONT));
  bool enabled = IsWindowEnabled(h) != FALSE;
  switch (type) {
    case SS_BLACKRECT: FillRect(dc, &rc, GetSysColorBrush(COLOR_3DDKSHADOW)); break;
    case SS_GRAYRECT: FillRect(dc, &rc, GetSysColorBrush(COLOR_3DSHADOW)); break;
    case SS_WHITERECT: FillRect(dc, &rc, GetSysColorBrush(COLOR_3DHILIGHT)); break;
    case SS_BLACKFRAME: FrameRect(dc, &rc, GetSysColorBrush(COLOR_3DDKSHADOW)); break;
    case SS_GRAYFRAME: FrameRect(dc, &rc, GetSysColorBrush(COLOR_3DSHADOW)); break;
    case SS_WHITEFRAME: FrameRect(dc, &rc, GetSysColorBrush(COLOR_3DHILIGHT)); break;
    case SS_ETCHEDHORZ: DrawEdge(dc, &rc, EDGE_ETCHED, BF_TOP); break;
    case SS_ETCHEDVERT: DrawEdge(dc, &rc, EDGE_ETCHED, BF_LEFT); break;
    case SS_ETCHEDFRAME: DrawEdge(dc, &rc, EDGE_ETCHED, BF_RECT); break;
    case SS_OWNERDRAW: {
      DRAWITEMSTRUCT dis = {ODT_STATIC, (UINT)GetDlgCtrlID(h), 0, ODA_DRAWENTIRE, 0, h, dc, rc, 0};
      if (!enabled) dis.itemState |= ODS_DISABLED;
      SendMessageW(GetParent(h), WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);
      break;
    }
    case SS_ICON:
    case SS_BITMAP:
    case SS_ENHMETAFILE: {
      HBRUSH br = CtlColor(h, dc, WM_CTLCOLORSTATIC);
      FillRect(dc, &rc, br);
      if (s->image && type != SS_ENHMETAFILE) {
        SIZE is = ImageSize(s->image, s->imageType);
        int x = 0, y = 0;
        if (style & SS_CENTERIMAGE) {
          x = (rc.right - is.cx) / 2;
          y = (rc.bottom - is.cy) / 2;
        }
        DrawImage(dc, s->image, s->imageType, x, y, enabled);
      }
      break;
    }
    default: {
      HBRUSH br = CtlColor(h, dc, WM_CTLCOLORSTATIC);
      FillRect(dc, &rc, br);
      UINT fmt = DT_EXPANDTABS;
      switch (type) {
        case SS_CENTER: fmt |= DT_CENTER | DT_WORDBREAK; break;
        case SS_RIGHT: fmt |= DT_RIGHT | DT_WORDBREAK; break;
        case SS_SIMPLE: fmt |= DT_LEFT | DT_SINGLELINE; break;
        case SS_LEFTNOWORDWRAP: fmt |= DT_LEFT; break;
        default: fmt |= DT_LEFT | DT_WORDBREAK; break;
      }
      if (style & SS_NOPREFIX) fmt |= DT_NOPREFIX;
      if (style & SS_CENTERIMAGE) fmt = (fmt & ~DT_WORDBREAK) | DT_SINGLELINE | DT_VCENTER;
      switch (style & SS_ELLIPSISMASK) {
        case SS_ENDELLIPSIS: fmt |= DT_END_ELLIPSIS | DT_SINGLELINE; break;
        case SS_PATHELLIPSIS: fmt |= DT_PATH_ELLIPSIS | DT_SINGLELINE; break;
        case SS_WORDELLIPSIS: fmt |= DT_WORD_ELLIPSIS | DT_SINGLELINE; break;
      }
      std::wstring text = TextOf(h);
      int mode = SetBkMode(dc, TRANSPARENT);
      COLORREF old = 0;
      if (!enabled) old = SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
      DrawTextW(dc, text.c_str(), (int)text.size(), &rc, fmt);
      if (!enabled) SetTextColor(dc, old);
      SetBkMode(dc, mode);
      break;
    }
  }
  SelectObject(dc, oldFont);
}

LRESULT CALLBACK StaticProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  StaticState* s = StateOf<StaticState>(h);
  if (!s && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE: {
      s = NewState<StaticState>(h);
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs->style & SS_SUNKEN) SetWindowLongW(h, GWL_EXSTYLE, cs->dwExStyle | WS_EX_STATICEDGE);
      UINT type = cs->style & SS_TYPEMASK;
      if (type == SS_ICON || type == SS_BITMAP) {
        // The text names the image; the window keeps no text.
        LRESULT r = DefWindowProcW(h, msg, wp, lp);
        SetWindowTextW(h, L"");
        LoadNamedImage(h, s, cs->lpszName);
        return r;
      }
      break;
    }
    case WM_NCDESTROY:
      FreeState<StaticState>(h);
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintStatic(h, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_NCHITTEST:
      return (GetWindowLongW(h, GWL_STYLE) & SS_NOTIFY) ? HTCLIENT : HTTRANSPARENT;
    case WM_GETDLGCODE:
      return DLGC_STATIC;
    case WM_LBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
      if (GetWindowLongW(h, GWL_STYLE) & SS_NOTIFY) Notify(h, STN_CLICKED);
      return 0;
    case WM_LBUTTONDBLCLK:
    case WM_NCLBUTTONDBLCLK:
      if (GetWindowLongW(h, GWL_STYLE) & SS_NOTIFY) Notify(h, STN_DBLCLK);
      return 0;
    case WM_ENABLE:
      InvalidateRect(h, NULL, FALSE);
      if (GetWindowLongW(h, GWL_STYLE) & SS_NOTIFY) Notify(h, wp ? STN_ENABLE : STN_DISABLE);
      return 0;
    case WM_SETFONT:
      s->font = (HFONT)wp;
      if (lp) InvalidateRect(h, NULL, FALSE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)s->font;
    case WM_SETTEXT: {
      UINT type = StaticType(h);
      if (type == SS_ICON || type == SS_BITMAP) {
        LoadNamedImage(h, s, (LPCWSTR)lp);
        InvalidateRect(h, NULL, FALSE);
        return TRUE;
      }
      LRESULT r = DefWindowProcW(h, msg, wp, lp);
      InvalidateRect(h, NULL, FALSE);
      return r;
    }
    case STM_GETICON:
      return s->imageType == IMAGE_ICON ? (LRESULT)s->image : 0;
    case STM_SETICON: {
      HANDLE old = s->imageType == IMAGE_ICON ? s->image : NULL;
      s->image = (HANDLE)wp;
      s->imageType = IMAGE_ICON;
      FitImage(h, s);
      InvalidateRect(h, NULL, FALSE);
      return (LRESULT)old;
    }
    case STM_GETIMAGE:
      return (UINT)wp == s->imageType || (wp == IMAGE_CURSOR && s->imageType == IMAGE_ICON) ? (LRESULT)s->image : 0;
    case STM_SETIMAGE: {
      HANDLE old = s->image;
      s->image = (HANDLE)lp;
      s->imageType = wp == IMAGE_CURSOR ? IMAGE_ICON : (UINT)wp;
      FitImage(h, s);
      InvalidateRect(h, NULL, FALSE);
      return (LRESULT)old;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------
// Scroll bar controls

struct ScrollState {
  ScrollBar bar;
  UINT arrows = ESB_ENABLE_BOTH;
};

bool Vertical(HWND h) { return (GetWindowLongW(h, GWL_STYLE) & SBS_VERT) != 0; }

int MaxPos(const SCROLLINFO& si) {
  int m = si.nMax - (si.nPage ? (int)si.nPage - 1 : 0);
  return m < si.nMin ? si.nMin : m;
}

int SetInfo(HWND h, ScrollState* s, const SCROLLINFO* si, bool redraw) {
  if (!si) return 0;
  SCROLLINFO& i = s->bar.info;
  if (si->fMask & SIF_RANGE) {
    i.nMin = si->nMin;
    i.nMax = si->nMax;
  }
  if (si->fMask & SIF_PAGE) i.nPage = si->nPage;
  if (si->fMask & SIF_POS) i.nPos = si->nPos;
  int range = i.nMax - i.nMin + 1;
  if (range < 0) range = 0;
  if ((int)i.nPage > range) i.nPage = (UINT)range;
  if (i.nPos < i.nMin) i.nPos = i.nMin;
  if (i.nPos > MaxPos(i)) i.nPos = MaxPos(i);
  if (redraw) {
    InvalidateRect(h, NULL, FALSE);
    UpdateWindow(h);
  }
  return i.nPos;
}

LRESULT CALLBACK ScrollProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ScrollState* s = StateOf<ScrollState>(h);
  if (!s && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE:
      s = NewState<ScrollState>(h);
      s->bar.info.nMax = 0;
      s->bar.info.nPage = 0;
      // A scroll bar made disabled draws as one from the start.
      s->bar.enabled = !(((CREATESTRUCTW*)lp)->style & WS_DISABLED);
      break;
    case WM_NCDESTROY:
      FreeState<ScrollState>(h);
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      DWORD style = GetWindowLongW(h, GWL_STYLE);
      if (style & (SBS_SIZEBOX | SBS_SIZEGRIP))
        DrawFrameControl(dc, &rc, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
      else {
        // The parent gives the trough's brush, as on Windows.
        HWND parent = GetParent(h);
        HBRUSH br = parent ? (HBRUSH)SendMessageW(parent, WM_CTLCOLORSCROLLBAR, (WPARAM)dc, (LPARAM)h) : NULL;
        PaintScrollBar(dc, rc, Vertical(h), &s->bar, br);
      }
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_ENABLE:
      s->bar.enabled = wp != 0;
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      if (GetWindowLongW(h, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP)) return 0;
      if (GetWindowLongW(h, GWL_STYLE) & WS_TABSTOP) SetFocus(h);
      Window* w = winport::GetWindow(h);
      if (!w) return 0;
      POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
      ClientToScreen(h, &p);
      ClickScrollBar(w, &s->bar, w->client, Vertical(h), p, GetParent(h), h);
      if (IsWindow(h)) InvalidateRect(h, NULL, FALSE);
      return 0;
    }
    case WM_KEYDOWN: {
      int code = -1;
      bool v = Vertical(h);
      switch (wp) {
        case VK_UP: if (v) code = SB_LINEUP; break;
        case VK_DOWN: if (v) code = SB_LINEDOWN; break;
        case VK_LEFT: if (!v) code = SB_LINEUP; break;
        case VK_RIGHT: if (!v) code = SB_LINEDOWN; break;
        case VK_PRIOR: code = SB_PAGEUP; break;
        case VK_NEXT: code = SB_PAGEDOWN; break;
        case VK_HOME: code = SB_TOP; break;
        case VK_END: code = SB_BOTTOM; break;
      }
      if (code >= 0) {
        UINT m = v ? WM_VSCROLL : WM_HSCROLL;
        SendMessageW(GetParent(h), m, code, (LPARAM)h);
        SendMessageW(GetParent(h), m, SB_ENDSCROLL, (LPARAM)h);
        return 0;
      }
      break;
    }
    case SBM_SETPOS: {
      int old = s->bar.info.nPos;
      SCROLLINFO si = {sizeof si, SIF_POS};
      si.nPos = (int)wp;
      SetInfo(h, s, &si, lp != 0);
      return old;
    }
    case SBM_GETPOS:
      return s->bar.info.nPos;
    case SBM_SETRANGE:
    case SBM_SETRANGEREDRAW: {
      int old = s->bar.info.nPos;
      SCROLLINFO si = {sizeof si, SIF_RANGE};
      si.nMin = (int)wp;
      si.nMax = (int)lp;
      SetInfo(h, s, &si, msg == SBM_SETRANGEREDRAW);
      return old;
    }
    case SBM_GETRANGE:
      if (wp) *(LPINT)wp = s->bar.info.nMin;
      if (lp) *(LPINT)lp = s->bar.info.nMax;
      return 0;
    case SBM_ENABLE_ARROWS:
      s->arrows = (UINT)wp;
      s->bar.enabled = wp != ESB_DISABLE_BOTH;
      InvalidateRect(h, NULL, FALSE);
      return TRUE;
    case SBM_SETSCROLLINFO:
      return SetInfo(h, s, (const SCROLLINFO*)lp, wp != 0);
    case SBM_GETSCROLLINFO: {
      SCROLLINFO* si = (SCROLLINFO*)lp;
      if (!si) return FALSE;
      const SCROLLINFO& i = s->bar.info;
      if (si->fMask & SIF_RANGE) {
        si->nMin = i.nMin;
        si->nMax = i.nMax;
      }
      if (si->fMask & SIF_PAGE) si->nPage = i.nPage;
      if (si->fMask & SIF_POS) si->nPos = i.nPos;
      if (si->fMask & SIF_TRACKPOS) si->nTrackPos = i.nTrackPos;
      return TRUE;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterButtonClasses() {
  static bool done = false;
  if (done) return;
  done = true;
  LPCWSTR arrow = MAKEINTRESOURCEW(32512);
  RegisterSystemClass(L"Button", ButtonProc, CS_DBLCLKS | CS_PARENTDC | CS_HREDRAW | CS_VREDRAW, sizeof(void*), NULL,
                      arrow);
  RegisterSystemClass(L"Static", StaticProc, CS_DBLCLKS | CS_PARENTDC, sizeof(void*), NULL, arrow);
  RegisterSystemClass(L"ScrollBar", ScrollProc, CS_DBLCLKS | CS_PARENTDC | CS_HREDRAW | CS_VREDRAW, sizeof(void*),
                      NULL, arrow);
}

}  // namespace winport
