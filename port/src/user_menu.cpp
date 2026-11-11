// user_menu.cpp - menus: building and changing them, loading them from
// resources, the menu bar, the system menu, and tracking a menu with the
// mouse and the keyboard.
//
// A menu is a list of items; an item that opens a submenu holds the
// submenu's handle. A popup menu shows in a window of the "#32768" class
// while it is tracked. Tracking runs a message loop of its own on the
// caller's thread, as on Windows: mouse and key messages drive the menu,
// everything else is dispatched as usual.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>
#include <wctype.h>

#include <string>
#include <vector>

using namespace winport;


namespace {

// The type bits an item keeps; MF_POPUP comes from having a submenu.
const UINT kTypeBits = MFT_BITMAP | MFT_OWNERDRAW | MFT_SEPARATOR | MFT_MENUBARBREAK | MFT_MENUBREAK |
                       MFT_RADIOCHECK | MFT_RIGHTJUSTIFY | MFT_RIGHTORDER;
const UINT kStateBits = MFS_GRAYED | MFS_CHECKED | MFS_HILITE | MFS_DEFAULT;

struct MenuItem {
  UINT type = MFT_STRING;
  UINT state = 0;
  UINT id = 0;
  HMENU sub = NULL;
  HBITMAP checked = NULL, unchecked = NULL;
  ULONG_PTR data = 0;
  std::wstring text;
  HBITMAP bmp = NULL;   // an MFT_BITMAP item's bitmap, or hbmpItem
  RECT rect = {0, 0, 0, 0};  // a popup's client coordinates, or the menu bar's window coordinates
  int xTab = 0;         // where an accelerator's text starts, in a popup

  bool Separator() const { return (type & MFT_SEPARATOR) != 0; }
  bool Disabled() const { return (state & MFS_GRAYED) != 0; }
  bool Owner() const { return (type & MFT_OWNERDRAW) != 0; }
  bool Bitmap() const { return (type & MFT_BITMAP) != 0; }
};

struct MenuObj : GdiObject {
  MenuObj() : GdiObject(kObjMenu) {}
  std::vector<MenuItem> items;
  bool popup = false;
  DWORD style = 0;
  UINT maxHeight = 0;
  HBRUSH back = NULL;
  DWORD helpId = 0;
  ULONG_PTR menuData = 0;
  int hot = -1;          // the selected item
  HWND wnd = NULL;       // the popup window showing it, while tracked
  HWND owner = NULL;     // the window that gets owner draw messages
  SIZE size = {0, 0};    // a popup's laid out client size
  bool system = false;   // a window's system menu
};

MenuObj* MenuOf(HMENU h) { return h ? GdiAs<MenuObj>((HGDIOBJ)h, kObjMenu) : NULL; }

MenuObj* MenuOrFail(HMENU h) {
  MenuObj* m = MenuOf(h);
  if (!m) SetLastError(ERROR_INVALID_MENU_HANDLE);
  return m;
}

HMENU NewMenu(bool popup) {
  MenuObj* m = new MenuObj;
  m->popup = popup;
  return (HMENU)NewGdiHandle(m);
}

// Finds an item by position or by command. A search by command looks
// into submenus first; a submenu item whose ID matches is found when no
// command item is.
struct ItemRef {
  HMENU menu = NULL;
  MenuObj* m = NULL;
  int pos = -1;
  MenuItem* item() const { return &m->items[pos]; }
};

bool FindItem(HMENU h, UINT item, UINT flags, ItemRef* out, int depth = 0) {
  MenuObj* m = MenuOf(h);
  if (!m || depth > 32) return false;
  if (flags & MF_BYPOSITION) {
    if (item >= m->items.size()) return false;
    *out = {h, m, (int)item};
    return true;
  }
  ItemRef fallback;
  for (size_t i = 0; i < m->items.size(); i++) {
    MenuItem& it = m->items[i];
    if (it.sub) {
      if (FindItem(it.sub, item, flags, out, depth + 1)) return true;
      if (it.id == item && !fallback.m) fallback = {h, m, (int)i};
    } else if (it.id == item) {
      *out = {h, m, (int)i};
      return true;
    }
  }
  if (fallback.m) {
    *out = fallback;
    return true;
  }
  return false;
}

void DestroyTree(HMENU h, int depth = 0) {
  MenuObj* m = MenuOf(h);
  if (!m || depth > 32) return;
  if (m->wnd && IsWindow(m->wnd)) DestroyWindow(m->wnd);
  std::vector<HMENU> subs;
  for (MenuItem& it : m->items)
    if (it.sub) subs.push_back(it.sub);
  FreeGdiHandle((HGDIOBJ)h);
  for (HMENU s : subs) DestroyTree(s, depth + 1);
}

// The flags GetMenuState and WM_MENUSELECT give for an item.
UINT FlagsOf(const MenuItem& it) {
  UINT f = (it.type & kTypeBits) | (it.state & kStateBits);
  if (it.sub) f |= MF_POPUP;
  return f;
}

// Fills an item from the flags of AppendMenu, InsertMenu and ModifyMenu.
// MF_USECHECKBITMAPS has the bit MFT_RADIOCHECK has, so it is dropped.
void SetFromFlags(MenuItem* it, UINT flags, UINT_PTR id, LPCWSTR data) {
  it->type = flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR | MF_MENUBARBREAK | MF_MENUBREAK | MF_RIGHTJUSTIFY);
  it->state = flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT);
  if (flags & MF_POPUP) {
    it->sub = (HMENU)id;
    it->id = (UINT)id;
    if (MenuObj* s = MenuOf(it->sub)) s->popup = true;
  } else {
    it->sub = NULL;
    it->id = (UINT)id;
  }
  it->text.clear();
  it->bmp = NULL;
  it->data = 0;
  if (flags & MF_BITMAP) {
    it->bmp = (HBITMAP)data;
  } else if (flags & MF_OWNERDRAW) {
    it->data = (ULONG_PTR)data;
  } else if (!(flags & MF_SEPARATOR)) {
    if (data)
      it->text = data;
    else
      it->type |= MF_SEPARATOR;
  }
}

// The text form of an item for the A functions: a string item's text is
// converted; a bitmap or owner draw item's value passes through.
LPCWSTR WideItem(UINT flags, LPCSTR s, std::wstring& buf) {
  if (flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) return (LPCWSTR)s;
  if (!s) return NULL;
  buf = Widen(s);
  return buf.c_str();
}

BOOL InsertCore(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCWSTR data, bool append) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  MenuItem it;
  SetFromFlags(&it, flags, id, data);
  if (append) {
    m->items.push_back(it);
    return TRUE;
  }
  if (flags & MF_BYPOSITION) {
    if (pos >= m->items.size())
      m->items.push_back(it);
    else
      m->items.insert(m->items.begin() + pos, it);
    return TRUE;
  }
  ItemRef r;
  if (!FindItem(h, pos, MF_BYCOMMAND, &r)) {
    // As on Windows, an unknown command appends to this menu.
    m->items.push_back(it);
    return TRUE;
  }
  r.m->items.insert(r.m->items.begin() + r.pos, it);
  return TRUE;
}

BOOL ModifyCore(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCWSTR data) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, pos, flags, &r)) return FALSE;
  MenuItem* it = r.item();
  HMENU oldSub = it->sub;
  HBITMAP checked = it->checked, unchecked = it->unchecked;
  SetFromFlags(it, flags, id, data);
  it->checked = checked;
  it->unchecked = unchecked;
  if (oldSub && oldSub != it->sub) DestroyTree(oldSub);
  return TRUE;
}

BOOL RemoveCore(HMENU h, UINT pos, UINT flags, bool destroy) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, pos, flags, &r)) return FALSE;
  HMENU sub = r.item()->sub;
  if (r.m->hot == r.pos) r.m->hot = -1;
  r.m->items.erase(r.m->items.begin() + r.pos);
  if (destroy && sub) DestroyTree(sub);
  return TRUE;
}

// MENUITEMINFO in both character sets: the W form, with the A form's
// string converted on the way in.
bool SizeOk(UINT cb) { return cb == sizeof(MENUITEMINFOW) || cb == sizeof(MENUITEMINFOW) - sizeof(HBITMAP); }

void ApplyInfo(MenuItem* it, const MENUITEMINFOW* mi) {
  UINT mask = mi->fMask;
  bool hasBitmap = mi->cbSize == sizeof(MENUITEMINFOW);
  if (mask & MIIM_TYPE) {
    it->type = mi->fType & kTypeBits;
    if (it->type & MFT_BITMAP) {
      it->bmp = (HBITMAP)mi->dwTypeData;
      it->text.clear();
    } else if (!(it->type & (MFT_SEPARATOR | MFT_OWNERDRAW))) {
      it->text = mi->dwTypeData ? mi->dwTypeData : L"";
    }
  }
  if (mask & MIIM_FTYPE) it->type = (it->type & MFT_BITMAP) | (mi->fType & kTypeBits & ~MFT_BITMAP);
  if (mask & MIIM_STRING) it->text = mi->dwTypeData ? mi->dwTypeData : L"";
  if ((mask & MIIM_BITMAP) && hasBitmap) it->bmp = mi->hbmpItem;
  if (mask & MIIM_STATE) it->state = mi->fState & kStateBits;
  if (mask & MIIM_ID) it->id = mi->wID;
  if (mask & MIIM_SUBMENU) {
    it->sub = mi->hSubMenu;
    if (MenuObj* s = MenuOf(it->sub)) s->popup = true;
  }
  if (mask & MIIM_CHECKMARKS) {
    it->checked = mi->hbmpChecked;
    it->unchecked = mi->hbmpUnchecked;
  }
  if (mask & MIIM_DATA) it->data = mi->dwItemData;
}

// A copy of an A structure in W form; the string is converted.
MENUITEMINFOW InfoToW(const MENUITEMINFOA* a, std::wstring& buf) {
  MENUITEMINFOW w;
  memset(&w, 0, sizeof w);
  memcpy(&w, a, a->cbSize < sizeof w ? a->cbSize : sizeof w);
  w.cbSize = a->cbSize;
  bool text = (a->fMask & MIIM_STRING) ||
              ((a->fMask & MIIM_TYPE) && !(a->fType & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW)));
  if (text && a->dwTypeData) {
    buf = Widen(a->dwTypeData);
    w.dwTypeData = (LPWSTR)buf.c_str();
  }
  return w;
}

BOOL GetInfoCore(HMENU h, UINT item, BOOL byPos, MENUITEMINFOW* mi, bool unicode) {
  if (!mi || !SizeOk(mi->cbSize)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, byPos ? MF_BYPOSITION : MF_BYCOMMAND, &r)) {
    SetLastError(ERROR_MENU_ITEM_NOT_FOUND);
    return FALSE;
  }
  const MenuItem& it = *r.item();
  UINT mask = mi->fMask;
  if (mask & (MIIM_TYPE | MIIM_FTYPE)) mi->fType = it.type & kTypeBits;
  bool wantText = (mask & MIIM_STRING) || ((mask & MIIM_TYPE) && !(it.type & (MFT_BITMAP | MFT_SEPARATOR | MFT_OWNERDRAW)));
  if ((mask & MIIM_TYPE) && it.Bitmap()) mi->dwTypeData = (LPWSTR)it.bmp;
  if (wantText) {
    if (unicode) {
      UINT len = (UINT)it.text.size();
      if (mi->dwTypeData && mi->cch) {
        UINT n = len < mi->cch - 1 ? len : mi->cch - 1;
        memcpy(mi->dwTypeData, it.text.c_str(), n * sizeof(WCHAR));
        mi->dwTypeData[n] = 0;
        mi->cch = n;
      } else {
        mi->cch = len;
      }
    } else {
      std::string s = Narrow(it.text.c_str());
      LPSTR out = (LPSTR)mi->dwTypeData;
      if (out && mi->cch) {
        size_t n = s.size() < mi->cch - 1 ? s.size() : mi->cch - 1;
        while (n > 0 && n < s.size() && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
        memcpy(out, s.c_str(), n);
        out[n] = 0;
        mi->cch = (UINT)n;
      } else {
        mi->cch = (UINT)s.size();
      }
    }
  }
  if ((mask & MIIM_BITMAP) && mi->cbSize == sizeof(MENUITEMINFOW)) mi->hbmpItem = it.Bitmap() ? NULL : it.bmp;
  if (mask & MIIM_STATE) mi->fState = it.state & kStateBits;
  if (mask & MIIM_ID) mi->wID = it.id;
  if (mask & MIIM_SUBMENU) mi->hSubMenu = it.sub;
  if (mask & MIIM_CHECKMARKS) {
    mi->hbmpChecked = it.checked;
    mi->hbmpUnchecked = it.unchecked;
  }
  if (mask & MIIM_DATA) mi->dwItemData = it.data;
  return TRUE;
}

// ---------------------------------------------------------------------
// Menu templates. Strings are UTF-16, as the resource compiler writes them.

const WORD* ReadString(const WORD* p, std::wstring* out) {
  out->clear();
  while (*p) out->push_back((wchar_t)*p++);
  return p + 1;
}

const WORD* ParseItems(const WORD* p, HMENU menu, int depth) {
  for (;;) {
    WORD flags = *p++;
    WORD id = 0;
    if (!(flags & MF_POPUP)) id = *p++;
    std::wstring text;
    p = ReadString(p, &text);
    HMENU sub = NULL;
    if (flags & MF_POPUP) {
      sub = CreatePopupMenu();
      if (depth < 32) p = ParseItems(p, sub, depth + 1);
    }
    UINT f = flags & ~(MF_END | MF_POPUP);
    if (sub) f |= MF_POPUP;
    // An empty string item with no ID and no flags is a separator.
    if (!sub && id == 0 && text.empty() && !(flags & ~MF_END)) f |= MF_SEPARATOR;
    InsertCore(menu, 0, f, sub ? (UINT_PTR)sub : id, (f & MF_SEPARATOR) ? NULL : text.c_str(), true);
    if (flags & MF_END) break;
  }
  return p;
}

const BYTE* ParseItemsEx(const BYTE* base, const BYTE* p, HMENU menu, int depth) {
  for (;;) {
    DWORD type, state, id;
    memcpy(&type, p, 4);
    memcpy(&state, p + 4, 4);
    memcpy(&id, p + 8, 4);
    WORD resInfo;
    memcpy(&resInfo, p + 12, 2);
    std::wstring text;
    const WORD* s = (const WORD*)(p + 14);
    s = ReadString(s, &text);
    p = (const BYTE*)s;
    p = base + (((p - base) + 3) & ~(ptrdiff_t)3);
    MENUITEMINFOW mi = {sizeof mi};
    mi.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID;
    mi.fType = type;
    mi.fState = state;
    mi.wID = id;
    if (!(type & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW))) {
      mi.fMask |= MIIM_STRING;
      mi.dwTypeData = (LPWSTR)text.c_str();
    }
    if (resInfo & 0x01) {
      p += 4;  // the submenu's help ID
      HMENU sub = CreatePopupMenu();
      if (depth < 32) p = ParseItemsEx(base, p, sub, depth + 1);
      mi.fMask |= MIIM_SUBMENU;
      mi.hSubMenu = sub;
    }
    InsertMenuItemW(menu, (UINT)-1, TRUE, &mi);
    if (resInfo & 0x80) break;
  }
  return p;
}

// ---------------------------------------------------------------------
// Layout and drawing

HFONT MenuFont(bool bold) {
  static HFONT normal, heavy;
  if (!normal) {
    NONCLIENTMETRICSW ncm = {sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    normal = CreateFontIndirectW(&ncm.lfMenuFont);
    ncm.lfMenuFont.lfWeight = FW_BOLD;
    heavy = CreateFontIndirectW(&ncm.lfMenuFont);
  }
  return bold ? heavy : normal;
}

int CheckWidth() { return GetSystemMetrics(SM_CXMENUCHECK); }
int Gutter() { return CheckWidth() + 6; }
const int kSeparatorHeight = 9;
const int kBarPad = 6;

// A special bitmap's size: the HBMMENU_ values draw the MDI buttons.
bool SpecialBitmap(HBITMAP b) { return (UINT_PTR)b >= 1 && (UINT_PTR)b <= 11; }

SIZE BitmapSize(HBITMAP b) {
  SIZE s = {0, 0};
  if (!b || b == HBMMENU_CALLBACK) return s;
  if (SpecialBitmap(b)) return SIZE{GetSystemMetrics(SM_CXMENUSIZE), GetSystemMetrics(SM_CYMENUSIZE)};
  BITMAP bm;
  if (GetObjectW(b, sizeof bm, &bm)) {
    s.cx = bm.bmWidth;
    s.cy = bm.bmHeight;
  }
  return s;
}

// The parts of an item's text: what shows, and the accelerator after a tab.
void SplitText(const std::wstring& t, std::wstring* label, std::wstring* accel) {
  size_t tab = t.find_first_of(L"\t\b");
  *label = t.substr(0, tab);
  *accel = tab == std::wstring::npos ? L"" : t.substr(tab + 1);
}

SIZE TextSize(HDC dc, const std::wstring& s) {
  RECT r = {0, 0, 0, 0};
  if (s.empty()) {
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    return SIZE{0, tm.tmHeight};
  }
  DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_SINGLELINE | DT_CALCRECT);
  return SIZE{r.right - r.left, r.bottom - r.top};
}

SIZE Measure(HDC dc, MenuObj* m, MenuItem& it, bool bar) {
  if (it.Owner()) {
    MEASUREITEMSTRUCT mis = {ODT_MENU, 0, it.id, 0, 0, it.data};
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    mis.itemHeight = tm.tmHeight + 4;
    if (m->owner) SendMessageW(m->owner, WM_MEASUREITEM, 0, (LPARAM)&mis);
    return SIZE{(LONG)mis.itemWidth + (bar ? 0 : Gutter()), (LONG)mis.itemHeight};
  }
  if (it.Separator()) return SIZE{0, bar ? 0 : kSeparatorHeight};
  if (it.Bitmap()) {
    SIZE s = BitmapSize(it.bmp);
    if (!bar) s.cx += Gutter();
    return s;
  }
  HGDIOBJ old = SelectObject(dc, MenuFont((it.state & MFS_DEFAULT) != 0));
  std::wstring label, accel;
  SplitText(it.text, &label, &accel);
  SIZE s = TextSize(dc, label);
  SelectObject(dc, old);
  if (bar) return SIZE{s.cx + 2 * kBarPad, GetSystemMetrics(SM_CYMENU)};
  int h = s.cy + 4;
  if (h < CheckWidth() + 4) h = CheckWidth() + 4;
  return SIZE{s.cx + Gutter(), h};
}

// Lays out a popup's items in columns; returns its client size.
SIZE LayoutPopup(MenuObj* m) {
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, MenuFont(false));
  int x = 0, y = 0, colWidth = 0, height = 0;
  size_t colStart = 0;
  auto endColumn = [&](size_t end) {
    // Every item of a column is as wide as its widest, with room for
    // accelerators and submenu arrows.
    int accelWidth = 0;
    for (size_t j = colStart; j < end; j++) {
      MenuItem& it = m->items[j];
      if (it.Owner() || it.Separator() || it.Bitmap()) continue;
      std::wstring label, accel;
      SplitText(it.text, &label, &accel);
      if (!accel.empty()) {
        int w = TextSize(dc, accel).cx;
        if (w > accelWidth) accelWidth = w;
      }
    }
    int width = colWidth + (accelWidth ? accelWidth + 16 : 0) + CheckWidth() + 2;
    for (size_t j = colStart; j < end; j++) {
      m->items[j].rect.left = x;
      m->items[j].rect.right = x + width;
      m->items[j].xTab = x + width - CheckWidth() - 2 - accelWidth;
    }
    x += width;
    colWidth = 0;
    colStart = end;
  };
  for (size_t i = 0; i < m->items.size(); i++) {
    MenuItem& it = m->items[i];
    if (i > 0 && (it.type & (MFT_MENUBREAK | MFT_MENUBARBREAK))) {
      endColumn(i);
      if (it.type & MFT_MENUBARBREAK) x += 2;
      y = 0;
    }
    SIZE s = Measure(dc, m, it, false);
    it.rect.top = y;
    it.rect.bottom = y + s.cy;
    y += s.cy;
    if (y > height) height = y;
    if (s.cx > colWidth) colWidth = s.cx;
  }
  endColumn(m->items.size());
  SelectObject(dc, old);
  DeleteDC(dc);
  if (m->items.empty()) return SIZE{Gutter() + CheckWidth(), 4};
  return SIZE{x, height};
}

// Lays out a menu bar in the window's bar rectangle, one line of items.
void LayoutBar(MenuObj* m, const RECT& bar) {
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, MenuFont(false));
  int x = bar.left;
  size_t right = m->items.size();
  for (size_t i = 0; i < m->items.size(); i++)
    if (m->items[i].type & MFT_RIGHTJUSTIFY) {
      right = i;
      break;
    }
  std::vector<SIZE> sizes;
  for (MenuItem& it : m->items) sizes.push_back(Measure(dc, m, it, true));
  for (size_t i = 0; i < right; i++) {
    m->items[i].rect = MakeRect(x, bar.top, x + sizes[i].cx, bar.bottom);
    x += sizes[i].cx;
  }
  // Items from a right-justified one on line up at the right.
  int rx = bar.right;
  for (size_t i = m->items.size(); i-- > right;) {
    m->items[i].rect = MakeRect(rx - sizes[i].cx, bar.top, rx, bar.bottom);
    rx -= sizes[i].cx;
  }
  SelectObject(dc, old);
  DeleteDC(dc);
}

// Small glyphs as rows of pixels: a check mark, a bullet and an arrow.
const char* const kCheck[] = {"......X", ".....XX", "X...XXX", "XX.XXX.", "XXXXX..", ".XXX...", "..X...."};
const char* const kBullet[] = {".XXXX.", "XXXXXX", "XXXXXX", "XXXXXX", "XXXXXX", ".XXXX."};
const char* const kArrow[] = {"X...", "XX..", "XXX.", "XXXX", "XXX.", "XX..", "X..."};

template <size_t N>
void Glyph(HDC dc, const RECT& box, const char* const (&rows)[N], COLORREF color) {
  int w = (int)strlen(rows[0]), h = (int)N;
  int x0 = (box.left + box.right - w) / 2, y0 = (box.top + box.bottom - h) / 2;
  HBRUSH br = CreateSolidBrush(color);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w;) {
      if (rows[y][x] != 'X') {
        x++;
        continue;
      }
      int e = x;
      while (e < w && rows[y][e] == 'X') e++;
      RECT r = MakeRect(x0 + x, y0 + y, x0 + e, y0 + y + 1);
      FillRect(dc, &r, br);
      x = e;
    }
  DeleteObject(br);
}

void DrawSpecialBitmap(HDC dc, HBITMAP b, const RECT& r) {
  UINT_PTR v = (UINT_PTR)b;
  UINT state = 0;
  if (v == (UINT_PTR)HBMMENU_MBAR_CLOSE || v == (UINT_PTR)HBMMENU_MBAR_CLOSE_D || v == (UINT_PTR)HBMMENU_POPUP_CLOSE)
    state = DFCS_CAPTIONCLOSE;
  else if (v == (UINT_PTR)HBMMENU_MBAR_RESTORE || v == (UINT_PTR)HBMMENU_POPUP_RESTORE)
    state = DFCS_CAPTIONRESTORE;
  else if (v == (UINT_PTR)HBMMENU_MBAR_MINIMIZE || v == (UINT_PTR)HBMMENU_MBAR_MINIMIZE_D ||
           v == (UINT_PTR)HBMMENU_POPUP_MINIMIZE)
    state = DFCS_CAPTIONMIN;
  else if (v == (UINT_PTR)HBMMENU_POPUP_MAXIMIZE)
    state = DFCS_CAPTIONMAX;
  else
    return;  // HBMMENU_SYSTEM: the window's icon, drawn by the MDI code
  if (v == (UINT_PTR)HBMMENU_MBAR_CLOSE_D || v == (UINT_PTR)HBMMENU_MBAR_MINIMIZE_D) state |= DFCS_INACTIVE;
  RECT box = r;
  InflateRect(&box, -1, -2);
  DrawFrameControl(dc, &box, DFC_CAPTION, state);
}

void DrawBitmapAt(HDC dc, HBITMAP b, int x, int y) {
  SIZE s = BitmapSize(b);
  HDC mem = CreateCompatibleDC(dc);
  HGDIOBJ old = SelectObject(mem, b);
  BitBlt(dc, x, y, s.cx, s.cy, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
}

void DrawItem(HDC dc, HMENU h, MenuObj* m, int index, bool bar) {
  MenuItem& it = m->items[index];
  RECT r = it.rect;
  bool selected = index == m->hot || (it.state & MFS_HILITE);
  if (it.Owner()) {
    DRAWITEMSTRUCT dis = {ODT_MENU, 0, it.id, ODA_DRAWENTIRE, 0, (HWND)h, dc, r, it.data};
    if (selected) dis.itemState |= ODS_SELECTED;
    if (it.state & MFS_CHECKED) dis.itemState |= ODS_CHECKED;
    if (it.state & MF_GRAYED) dis.itemState |= ODS_GRAYED;
    if (it.state & MF_DISABLED) dis.itemState |= ODS_DISABLED;
    if (it.state & MFS_DEFAULT) dis.itemState |= ODS_DEFAULT;
    FillRect(dc, &r, m->back ? m->back : GetSysColorBrush(COLOR_MENU));
    if (m->owner) SendMessageW(m->owner, WM_DRAWITEM, 0, (LPARAM)&dis);
    return;
  }
  FillRect(dc, &r, m->back ? m->back : GetSysColorBrush(COLOR_MENU));
  if (it.Separator()) {
    if (bar) return;
    int y = (r.top + r.bottom) / 2 - 1;
    RECT a = MakeRect(r.left + 1, y, r.right - 1, y + 1), b = MakeRect(r.left + 1, y + 1, r.right - 1, y + 2);
    FillRect(dc, &a, GetSysColorBrush(COLOR_BTNSHADOW));
    FillRect(dc, &b, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
    return;
  }
  if (selected && !(bar && it.Bitmap() && SpecialBitmap(it.bmp))) FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
  bool gray = it.Disabled();
  COLORREF fore = GetSysColor(gray ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
  if (it.Bitmap()) {
    if (SpecialBitmap(it.bmp)) {
      DrawSpecialBitmap(dc, it.bmp, r);
    } else if (it.bmp != HBMMENU_CALLBACK) {
      SIZE s = BitmapSize(it.bmp);
      int x = bar ? r.left : r.left + Gutter();
      DrawBitmapAt(dc, it.bmp, x, (r.top + r.bottom - s.cy) / 2);
    }
    return;
  }
  if (!bar) {
    RECT check = MakeRect(r.left + 2, r.top, r.left + 2 + CheckWidth() + 2, r.bottom);
    HBITMAP mark = (it.state & MFS_CHECKED) ? it.checked : it.unchecked;
    if (mark) {
      SIZE s = BitmapSize(mark);
      DrawBitmapAt(dc, mark, (check.left + check.right - s.cx) / 2, (check.top + check.bottom - s.cy) / 2);
    } else if (it.state & MFS_CHECKED) {
      if (it.type & MFT_RADIOCHECK)
        Glyph(dc, check, kBullet, fore);
      else
        Glyph(dc, check, kCheck, fore);
    } else if (it.bmp && it.bmp != HBMMENU_CALLBACK && !SpecialBitmap(it.bmp)) {
      SIZE s = BitmapSize(it.bmp);
      DrawBitmapAt(dc, it.bmp, (check.left + check.right - s.cx) / 2, (check.top + check.bottom - s.cy) / 2);
    }
    if (it.sub) {
      RECT arrow = MakeRect(r.right - CheckWidth() - 2, r.top, r.right - 2, r.bottom);
      Glyph(dc, arrow, kArrow, fore);
    }
  }
  std::wstring label, accel;
  SplitText(it.text, &label, &accel);
  HGDIOBJ oldFont = SelectObject(dc, MenuFont((it.state & MFS_DEFAULT) != 0));
  int mode = SetBkMode(dc, TRANSPARENT);
  COLORREF oldColor = GetTextColor(dc);
  RECT text = r, keys = r;
  UINT fmt = DT_SINGLELINE | DT_VCENTER;
  if (bar) {
    fmt |= DT_CENTER;
  } else {
    text.left = r.left + Gutter();
    keys.left = it.xTab;
    keys.right = r.right - CheckWidth() - 2;
  }
  auto draw = [&](int dx, COLORREF c) {
    SetTextColor(dc, c);
    RECT t = text, k = keys;
    OffsetRect(&t, dx, dx);
    OffsetRect(&k, dx, dx);
    DrawTextW(dc, label.c_str(), (int)label.size(), &t, fmt);
    if (!accel.empty()) DrawTextW(dc, accel.c_str(), (int)accel.size(), &k, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  };
  // A disabled item looks etched, unless it is selected.
  if (gray && !selected) draw(1, GetSysColor(COLOR_BTNHIGHLIGHT));
  draw(0, gray && !selected ? GetSysColor(COLOR_BTNSHADOW) : fore);
  if (gray && selected) draw(0, GetSysColor(COLOR_GRAYTEXT));
  SetTextColor(dc, oldColor);
  SetBkMode(dc, mode);
  SelectObject(dc, oldFont);
}

void PaintPopup(HMENU h, HDC dc) {
  MenuObj* m = MenuOf(h);
  if (!m) return;
  RECT all = MakeRect(0, 0, 0, 0);
  HWND w = m->wnd;
  if (w) GetClientRect(w, &all);
  FillRect(dc, &all, m->back ? m->back : GetSysColorBrush(COLOR_MENU));
  for (size_t i = 0; i < m->items.size(); i++) {
    MenuItem& it = m->items[i];
    DrawItem(dc, h, m, (int)i, false);
    // A bar break draws a line before its column.
    if (i > 0 && (it.type & MFT_MENUBARBREAK)) {
      RECT a = MakeRect(it.rect.left - 2, 0, it.rect.left - 1, all.bottom);
      RECT b = MakeRect(it.rect.left - 1, 0, it.rect.left, all.bottom);
      FillRect(dc, &a, GetSysColorBrush(COLOR_BTNSHADOW));
      FillRect(dc, &b, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
    }
  }
}

// The menu window: it paints its menu and never takes activation.
LRESULT CALLBACK MenuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCREATE:
      SetWindowLongPtrW(h, 0, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
      return TRUE;
    case MN_GETHMENU: return GetWindowLongPtrW(h, 0);
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      PaintPopup((HMENU)GetWindowLongPtrW(h, 0), dc);
      EndPaint(h, &ps);
      return 0;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

const wchar_t kMenuClass[] = L"#32768";
const DWORD kMenuStyle = WS_POPUP | WS_CLIPSIBLINGS;
const DWORD kMenuExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;

// Shows a popup menu at a screen position, kept on the screen; flags are
// TPM_ alignment flags.
HWND ShowPopup(HMENU h, int x, int y, UINT flags, HWND owner, const RECT* avoid) {
  MenuObj* m = MenuOf(h);
  if (!m) return NULL;
  m->owner = owner;
  m->hot = -1;
  m->size = LayoutPopup(m);
  RECT in = FrameInsets(kMenuStyle, kMenuExStyle, false);
  int cx = m->size.cx + in.left + in.right, cy = m->size.cy + in.top + in.bottom;
  if (m->maxHeight && cy > (int)m->maxHeight) cy = m->maxHeight;
  if (flags & TPM_CENTERALIGN) x -= cx / 2;
  else if (flags & TPM_RIGHTALIGN) x -= cx;
  if (flags & TPM_VCENTERALIGN) y -= cy / 2;
  else if (flags & TPM_BOTTOMALIGN) y -= cy;
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  if (x + cx > sw) x = avoid ? avoid->left - cx : sw - cx;
  if (y + cy > sh) y = avoid && avoid->top - cy >= 0 ? avoid->top - cy : sh - cy;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  RegisterSystemClasses();
  HWND w = CreateWindowExW(kMenuExStyle, kMenuClass, L"", kMenuStyle, x, y, cx, cy, owner, NULL, NULL, (LPVOID)h);
  if (!w) return NULL;
  m->wnd = w;
  SetWindowPos(w, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  UpdateWindow(w);
  return w;
}

void HidePopup(HMENU h) {
  MenuObj* m = MenuOf(h);
  if (!m) return;
  m->hot = -1;
  if (m->wnd) {
    HWND w = m->wnd;
    m->wnd = NULL;
    if (IsWindow(w)) DestroyWindow(w);
  }
}

// ---------------------------------------------------------------------
// Tracking

struct Level {
  HMENU menu;
  bool bar;  // a window's menu bar
};

struct Tracker {
  HWND owner = NULL;  // gets the notifications and the command
  UINT flags = 0;     // TPM_ flags
  bool popup = false;   // TrackPopupMenu, not the menu bar or system menu
  bool system = false;  // the system menu
  bool mouse = false;   // a mouse button opened it
  std::vector<Level> levels;
  bool done = false;
  bool chosen = false;
  UINT cmd = 0;
  HMENU cmdMenu = NULL;
  int cmdPos = -1;
  POINT cmdPt = {0, 0};
  Tracker* outer = NULL;
};

// Starts a menu loop: WM_ENTERMENULOOP and WM_INITMENU, before the first
// menu shows.
void Begin(Tracker* t) {
  SendMessageW(t->owner, WM_ENTERMENULOOP, t->popup, 0);
  if (!(t->flags & TPM_NONOTIFY)) SendMessageW(t->owner, WM_INITMENU, (WPARAM)t->levels[0].menu, 0);
}
Tracker* g_tracker;

bool Notify(Tracker* t) { return !(t->flags & TPM_NONOTIFY); }

Window* BarWindow(Tracker* t) { return GetWindow(t->owner); }

void RedrawItem(Tracker* t, size_t level, int index) {
  if (index < 0 || level >= t->levels.size()) return;
  HMENU h = t->levels[level].menu;
  MenuObj* m = MenuOf(h);
  if (!m || index >= (int)m->items.size()) return;
  if (t->levels[level].bar) {
    DrawMenuBar(t->owner);
  } else if (m->wnd) {
    HDC dc = GetDC(m->wnd);
    DrawItem(dc, h, m, index, false);
    ReleaseDC(m->wnd, dc);
  }
}

void SendSelect(Tracker* t, size_t level) {
  if (!Notify(t)) return;
  HMENU h = t->levels[level].menu;
  MenuObj* m = MenuOf(h);
  if (!m) return;
  UINT sys = t->system ? MF_SYSMENU : 0, mouse = t->mouse ? MF_MOUSESELECT : 0;
  if (m->hot < 0) {
    SendMessageW(t->owner, WM_MENUSELECT, MAKEWPARAM(0, MF_HILITE | sys | mouse), (LPARAM)h);
    return;
  }
  MenuItem& it = m->items[m->hot];
  UINT id = it.sub ? (UINT)m->hot : it.id;
  SendMessageW(t->owner, WM_MENUSELECT, MAKEWPARAM(id, FlagsOf(it) | MF_HILITE | sys | mouse), (LPARAM)h);
}

void Select(Tracker* t, size_t level, int index) {
  MenuObj* m = MenuOf(t->levels[level].menu);
  if (!m || m->hot == index) return;
  int old = m->hot;
  m->hot = index;
  RedrawItem(t, level, old);
  RedrawItem(t, level, index);
  SendSelect(t, level);
}

// Closes the popups deeper than a level.
void CloseFrom(Tracker* t, size_t level) {
  while (t->levels.size() > level + 1) {
    Level l = t->levels.back();
    t->levels.pop_back();
    HidePopup(l.menu);
    if (Notify(t))
      SendMessageW(t->owner, WM_UNINITMENUPOPUP, (WPARAM)l.menu, MAKELPARAM(0, t->system ? MF_SYSMENU : 0));
  }
}

int NextItem(MenuObj* m, int from, int dir) {
  int n = (int)m->items.size();
  if (n == 0) return -1;
  int i = from;
  for (int k = 0; k < n; k++) {
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    if (!m->items[i].Separator()) return i;
  }
  return -1;
}

void InitPopup(Tracker* t, HMENU sub, int pos) {
  if (Notify(t)) SendMessageW(t->owner, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(pos, t->system ? 1 : 0));
}

// Opens the submenu of the selected item at a level; selects its first
// item when asked.
bool OpenSub(Tracker* t, size_t level, bool selectFirst) {
  HMENU h = t->levels[level].menu;
  MenuObj* m = MenuOf(h);
  if (!m || m->hot < 0) return false;
  MenuItem& it = m->items[m->hot];
  if (!it.sub || it.Disabled() || !MenuOf(it.sub)) return false;
  CloseFrom(t, level);
  HMENU sub = it.sub;
  int pos = m->hot;
  InitPopup(t, sub, pos);
  // The handlers may have changed the menu.
  m = MenuOf(h);
  if (!m || pos >= (int)m->items.size() || m->items[pos].sub != sub || !MenuOf(sub)) return false;
  RECT r = m->items[pos].rect;
  int x, y;
  RECT avoid;
  if (t->levels[level].bar) {
    Window* w = BarWindow(t);
    if (!w) return false;
    RECT wr = ScreenRect(w);
    OffsetRect(&r, wr.left, wr.top);
    x = r.left;
    y = r.bottom;
    avoid = r;
  } else {
    POINT p = {r.right, r.top};
    ClientToScreen(m->wnd, &p);
    RECT in = FrameInsets(kMenuStyle, kMenuExStyle, false);
    x = p.x - 3;
    y = p.y - in.top;
    RECT wr;
    GetWindowRect(m->wnd, &wr);
    avoid = wr;
  }
  if (!ShowPopup(sub, x, y, 0, t->owner, &avoid)) return false;
  t->levels.push_back({sub, false});
  if (selectFirst) Select(t, t->levels.size() - 1, NextItem(MenuOf(sub), -1, 1));
  return true;
}

void Choose(Tracker* t, size_t level) {
  HMENU h = t->levels[level].menu;
  MenuObj* m = MenuOf(h);
  if (!m || m->hot < 0) return;
  MenuItem& it = m->items[m->hot];
  if (it.Separator()) return;
  if (it.sub) {
    OpenSub(t, level, true);
    return;
  }
  if (it.Disabled()) {
    // As on Windows, choosing a disabled item ends the menu without a command.
    t->done = true;
    return;
  }
  t->chosen = true;
  t->cmd = it.id;
  t->cmdMenu = h;
  t->cmdPos = m->hot;
  t->done = true;
}

// Where a screen point is: the level and the item under it (-1 for none).
bool HitAt(Tracker* t, POINT p, size_t* level, int* index) {
  for (size_t l = t->levels.size(); l-- > 0;) {
    HMENU h = t->levels[l].menu;
    MenuObj* m = MenuOf(h);
    if (!m) continue;
    if (t->levels[l].bar) {
      Window* w = BarWindow(t);
      if (!w) continue;
      RECT wr = ScreenRect(w);
      POINT q = {p.x - wr.left, p.y - wr.top};
      RECT bar = MenuBarRect(w);
      if (!PtInRect(&bar, q)) continue;
      *level = l;
      *index = -1;
      for (size_t i = 0; i < m->items.size(); i++)
        if (PtInRect(&m->items[i].rect, q)) *index = (int)i;
      return true;
    }
    if (!m->wnd) continue;
    RECT wr;
    GetWindowRect(m->wnd, &wr);
    if (!PtInRect(&wr, p)) continue;
    POINT q = p;
    ScreenToClient(m->wnd, &q);
    *level = l;
    *index = -1;
    for (size_t i = 0; i < m->items.size(); i++)
      if (PtInRect(&m->items[i].rect, q)) *index = (int)i;
    return true;
  }
  return false;
}

void Mouse(Tracker* t, UINT msg, POINT p) {
  size_t level;
  int index;
  bool over = HitAt(t, p, &level, &index);
  bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_NCLBUTTONDOWN || msg == WM_NCRBUTTONDOWN;
  bool up = msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_NCLBUTTONUP || msg == WM_NCRBUTTONUP;
  bool right = msg == WM_RBUTTONUP || msg == WM_NCRBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_NCRBUTTONDOWN;
  if (!over) {
    if (down) t->done = true;  // a click elsewhere ends the menu
    else if (!up) {
      // Off the menus, the deepest popup loses its selection unless it
      // has a submenu open.
      size_t l = t->levels.size() - 1;
      if (!t->levels[l].bar) Select(t, l, -1);
    }
    return;
  }
  t->mouse = true;
  bool bar = t->levels[level].bar;
  MenuObj* m = MenuOf(t->levels[level].menu);
  if (!m) return;
  if (bar) {
    if (down) {
      if (index == m->hot && t->levels.size() > 1) {
        t->done = true;  // a second click on the open title closes it
        return;
      }
      Select(t, level, index);
      OpenSub(t, level, false);
    } else if (!up && index >= 0 && index != m->hot && t->levels.size() > 1) {
      // With a menu open, moving along the bar opens the next one.
      Select(t, level, index);
      CloseFrom(t, level);
      OpenSub(t, level, false);
    }
    return;
  }
  if (index < 0) return;
  if (index != m->hot) {
    CloseFrom(t, level);
    Select(t, level, index);
    if (m->items[index].sub) OpenSub(t, level, false);
    return;
  }
  if (up && (!right || (t->flags & TPM_RIGHTBUTTON) || !t->popup)) {
    if (!m->items[index].sub) Choose(t, level);
  } else if (down && m->items[index].sub && t->levels.size() == level + 1) {
    OpenSub(t, level, false);
  }
}

// An item's mnemonic: the character after a single &.
wchar_t Mnemonic(const MenuItem& it) {
  if (it.Separator() || it.Bitmap() || it.Owner()) return 0;
  const std::wstring& s = it.text;
  for (size_t i = 0; i + 1 < s.size(); i++)
    if (s[i] == L'&') {
      if (s[i + 1] != L'&') return (wchar_t)towupper(s[i + 1]);
      i++;
    }
  return 0;
}

void Char(Tracker* t, wchar_t ch) {
  size_t level = t->levels.size() - 1;
  HMENU h = t->levels[level].menu;
  MenuObj* m = MenuOf(h);
  if (!m) return;
  wchar_t up = (wchar_t)towupper(ch);
  std::vector<int> matches;
  for (size_t i = 0; i < m->items.size(); i++)
    if (Mnemonic(m->items[i]) == up) matches.push_back((int)i);
  int index = -1;
  bool execute = false;
  if (matches.empty()) {
    UINT flags = (t->levels[level].bar ? 0 : MF_POPUP) | (t->system ? MF_SYSMENU : 0);
    LRESULT r = SendMessageW(t->owner, WM_MENUCHAR, MAKEWPARAM(ch, flags), (LPARAM)h);
    switch (HIWORD(r)) {
      case MNC_CLOSE: t->done = true; return;
      case MNC_EXECUTE: index = LOWORD(r); execute = true; break;
      case MNC_SELECT: index = LOWORD(r); break;
      default: MessageBeep(0); return;
    }
    m = MenuOf(h);
    if (!m || index >= (int)m->items.size()) return;
  } else if (matches.size() == 1) {
    index = matches[0];
    execute = true;
  } else {
    // Several items share the letter: step through them.
    index = matches[0];
    for (int i : matches)
      if (i > m->hot) {
        index = i;
        break;
      }
  }
  CloseFrom(t, level);
  Select(t, level, index);
  if (execute) Choose(t, level);
}

void Key(Tracker* t, UINT vk) {
  size_t level = t->levels.size() - 1;
  bool bar = t->levels[level].bar;
  MenuObj* m = MenuOf(t->levels[level].menu);
  if (!m) return;
  bool barBelow = level == 1 && t->levels[0].bar;
  switch (vk) {
    case VK_ESCAPE:
      if (level == 0) {
        t->done = true;
      } else {
        CloseFrom(t, level - 1);
        if (barBelow && t->mouse) t->done = true;
      }
      break;
    case VK_MENU:
    case VK_F10:
      t->done = true;
      break;
    case VK_UP:
    case VK_DOWN:
      if (bar) {
        OpenSub(t, level, true);
      } else {
        Select(t, level, NextItem(m, m->hot, vk == VK_DOWN ? 1 : -1));
      }
      break;
    case VK_LEFT:
    case VK_RIGHT: {
      if (bar) {
        Select(t, level, NextItem(m, m->hot, vk == VK_RIGHT ? 1 : -1));
        break;
      }
      if (vk == VK_RIGHT && m->hot >= 0 && m->items[m->hot].sub) {
        OpenSub(t, level, true);
        break;
      }
      if (vk == VK_LEFT && level > 0 && !t->levels[level - 1].bar) {
        CloseFrom(t, level - 1);
        break;
      }
      if (t->levels[0].bar) {
        // Along the bar to the next menu.
        CloseFrom(t, 0);
        MenuObj* b = MenuOf(t->levels[0].menu);
        if (b) {
          Select(t, 0, NextItem(b, b->hot, vk == VK_RIGHT ? 1 : -1));
          OpenSub(t, 0, true);
        }
      }
      break;
    }
    case VK_RETURN:
      if (m->hot >= 0) Choose(t, level);
      break;
    case VK_HOME:
    case VK_END:
      if (!bar) Select(t, level, NextItem(m, -1, vk == VK_HOME ? 1 : -1));
      break;
  }
}

UINT Run(Tracker* t) {
  t->outer = g_tracker;
  g_tracker = t;
  HWND owner = t->owner;
  HWND oldCapture = GetCapture();
  SetCapture(owner);
  bool idle = false;
  MSG msg;
  while (!t->done) {
    if (!t->levels[0].bar && !MenuOf(t->levels[0].menu)) break;
    if (!idle && !PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE)) {
      idle = true;
      MenuObj* deepest = MenuOf(t->levels.back().menu);
      if (Notify(t)) SendMessageW(owner, WM_ENTERIDLE, MSGF_MENU, (LPARAM)(deepest ? deepest->wnd : NULL));
      continue;
    }
    if (!GetMessageW(&msg, NULL, 0, 0)) {
      PostQuitMessage((int)msg.wParam);
      break;
    }
    idle = false;
    if (!IsWindow(owner)) break;
    if (HaveHooks(WH_MSGFILTER) && CallHooks(WH_MSGFILTER, MSGF_MENU, 0, (LPARAM)&msg)) continue;
    UINT m = msg.message;
    if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL) {
      POINT p = {(SHORT)LOWORD(msg.lParam), (SHORT)HIWORD(msg.lParam)};
      ClientToScreen(msg.hwnd, &p);
      if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MOUSEMOVE ||
          m == WM_LBUTTONDBLCLK || m == WM_RBUTTONDBLCLK) {
        UINT as = m == WM_LBUTTONDBLCLK ? WM_LBUTTONDOWN : m == WM_RBUTTONDBLCLK ? WM_RBUTTONDOWN : m;
        t->cmdPt = p;
        Mouse(t, as, p);
      }
      continue;
    }
    if (m >= WM_NCMOUSEMOVE && m <= WM_NCMBUTTONDBLCLK) {
      POINT p = {(SHORT)LOWORD(msg.lParam), (SHORT)HIWORD(msg.lParam)};
      t->cmdPt = p;
      Mouse(t, m, p);
      continue;
    }
    if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
      TranslateMessage(&msg);
      Key(t, (UINT)msg.wParam);
      continue;
    }
    if (m == WM_CHAR || m == WM_SYSCHAR) {
      if (msg.wParam != VK_RETURN && msg.wParam != VK_ESCAPE) Char(t, (wchar_t)msg.wParam);
      continue;
    }
    if (m == WM_KEYUP || m == WM_SYSKEYUP || m == WM_DEADCHAR || m == WM_SYSDEADCHAR) continue;
    DispatchMessageW(&msg);
  }
  if (GetCapture() == owner) ReleaseCapture();
  if (oldCapture && IsWindow(oldCapture) && oldCapture != owner) SetCapture(oldCapture);
  CloseFrom(t, 0);
  if (!t->levels.empty()) {
    if (t->levels[0].bar) {
      if (MenuObj* b = MenuOf(t->levels[0].menu)) b->hot = -1;
      if (IsWindow(owner)) DrawMenuBar(owner);
    } else {
      HidePopup(t->levels[0].menu);
      if (Notify(t) && IsWindow(owner))
        SendMessageW(owner, WM_UNINITMENUPOPUP, (WPARAM)t->levels[0].menu, MAKELPARAM(0, t->system ? MF_SYSMENU : 0));
    }
  }
  g_tracker = t->outer;
  if (IsWindow(owner)) {
    if (Notify(t)) SendMessageW(owner, WM_MENUSELECT, MAKEWPARAM(0, 0xFFFF), 0);
    SendMessageW(owner, WM_EXITMENULOOP, t->popup, 0);
  }
  if (!t->chosen || !IsWindow(owner)) return 0;
  if (t->flags & TPM_RETURNCMD) return t->cmd;
  MenuObj* cm = MenuOf(t->cmdMenu);
  if (t->system) {
    PostMessageW(owner, WM_SYSCOMMAND, t->cmd, MAKELPARAM(t->cmdPt.x, t->cmdPt.y));
  } else if (cm && (cm->style & MNS_NOTIFYBYPOS)) {
    PostMessageW(owner, WM_MENUCOMMAND, t->cmdPos, (LPARAM)t->cmdMenu);
  } else {
    PostMessageW(owner, WM_COMMAND, MAKEWPARAM(t->cmd, 0), 0);
  }
  return t->cmd;
}

// Brings the system menu's items up to date with the window's state.
void UpdateSystemMenu(Window* w, HMENU h) {
  DWORD s = w->style;
  bool max = (s & WS_MAXIMIZE) != 0, min = (s & WS_MINIMIZE) != 0;
  EnableMenuItem(h, SC_RESTORE, MF_BYCOMMAND | (max || min ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(h, SC_MOVE, MF_BYCOMMAND | (max ? MF_GRAYED : MF_ENABLED));
  EnableMenuItem(h, SC_SIZE, MF_BYCOMMAND | (!max && !min && (s & WS_THICKFRAME) ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(h, SC_MINIMIZE, MF_BYCOMMAND | (!min && (s & WS_MINIMIZEBOX) ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(h, SC_MAXIMIZE, MF_BYCOMMAND | (!max && (s & WS_MAXIMIZEBOX) ? MF_ENABLED : MF_GRAYED));
  if (w->cls && (w->cls->wc.style & CS_NOCLOSE)) EnableMenuItem(h, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
}

void TrackSystem(Window* w, bool mouse) {
  HMENU sys = GetSystemMenu(w->hwnd, FALSE);
  if (!sys) return;
  UpdateSystemMenu(w, sys);
  RECT wr = ScreenRect(w);
  RECT icon = SysMenuRect(w);
  OffsetRect(&icon, wr.left, wr.top);
  if (RectEmpty(icon)) icon = MakeRect(wr.left, wr.top, wr.left, wr.top);
  Tracker t;
  t.owner = w->hwnd;
  t.system = true;
  t.mouse = mouse;
  if (MenuObj* m = MenuOf(sys)) m->system = true;
  t.levels.push_back({sys, false});
  Begin(&t);
  InitPopup(&t, sys, 0);
  if (!MenuOf(sys) || !ShowPopup(sys, icon.left, icon.bottom, 0, t.owner, &icon)) {
    SendMessageW(t.owner, WM_EXITMENULOOP, FALSE, 0);
    return;
  }
  if (!mouse) Select(&t, 0, NextItem(MenuOf(sys), -1, 1));
  Run(&t);
}

void TrackBar(Window* w, int start, bool open, bool mouse) {
  HMENU bar = w->menu;
  MenuObj* m = MenuOf(bar);
  if (!m || m->items.empty()) return;
  m->owner = w->hwnd;
  LayoutBar(m, MenuBarRect(w));
  Tracker t;
  t.owner = w->hwnd;
  t.mouse = mouse;
  t.levels.push_back({bar, true});
  Begin(&t);
  m = MenuOf(bar);
  if (m && start >= 0 && start < (int)m->items.size()) {
    Select(&t, 0, start);
    if (open) OpenSub(&t, 0, !mouse);
  }
  Run(&t);
}

}  // namespace

// ---------------------------------------------------------------------
// The window manager's side

namespace winport {

void RegisterMenuClass() {
  static bool done = false;
  if (done) return;
  done = true;
  RegisterSystemClass(kMenuClass, MenuWndProc, CS_SAVEBITS | CS_DBLCLKS, sizeof(void*), NULL, MAKEINTRESOURCEW(32512));
}

void DrawMenuBarIn(Window* w, HDC dc, const RECT& r) {
  FillRect(dc, &r, GetSysColorBrush(COLOR_MENU));
  MenuObj* m = MenuOf(w->menu);
  if (!m) return;
  m->owner = w->hwnd;
  LayoutBar(m, r);
  HRGN clip = CreateRectRgn(r.left, r.top, r.right, r.bottom);
  int saved = SaveDC(dc);
  ExtSelectClipRgn(dc, clip, RGN_AND);
  for (size_t i = 0; i < m->items.size(); i++) DrawItem(dc, w->menu, m, (int)i, true);
  RestoreDC(dc, saved);
  DeleteObject(clip);
}

void MenuCommand(Window* w, WPARAM wp, LPARAM lp) {
  UINT sc = wp & 0xFFF0;
  if (sc == SC_MOUSEMENU) {
    POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
    if ((wp & 0x0F) == HTSYSMENU) {
      TrackSystem(w, true);
      return;
    }
    if (w->Child() || !MenuOf(w->menu)) return;
    MenuObj* m = MenuOf(w->menu);
    RECT wr = ScreenRect(w);
    LayoutBar(m, MenuBarRect(w));
    POINT q = {p.x - wr.left, p.y - wr.top};
    for (size_t i = 0; i < m->items.size(); i++)
      if (PtInRect(&m->items[i].rect, q)) {
        TrackBar(w, (int)i, true, true);
        return;
      }
    return;
  }
  if (sc == SC_KEYMENU) {
    Window* top = w->TopLevel();
    wchar_t ch = (wchar_t)lp;
    if (ch == L' ' || ch == L'-') {
      if (top->style & WS_SYSMENU) TrackSystem(top, false);
      return;
    }
    MenuObj* m = top->menu ? MenuOf(top->menu) : NULL;
    if (!m) return;
    if (ch == 0) {
      TrackBar(top, NextItem(m, -1, 1), false, false);
      return;
    }
    wchar_t up = (wchar_t)towupper(ch);
    for (size_t i = 0; i < m->items.size(); i++)
      if (Mnemonic(m->items[i]) == up) {
        TrackBar(top, (int)i, true, false);
        return;
      }
    MessageBeep(0);
  }
}

void MenusOnDestroy(Window* w) {
  if (w->sysMenu) {
    DestroyTree(w->sysMenu);
    w->sysMenu = NULL;
  }
  if (!(w->style & WS_CHILD) && w->menu && MenuOf(w->menu)) {
    DestroyTree(w->menu);
    w->menu = NULL;
  }
}

}  // namespace winport

// ---------------------------------------------------------------------
// The API

extern "C" {

HMENU WINAPI CreateMenu(void) { return NewMenu(false); }
HMENU WINAPI CreatePopupMenu(void) { return NewMenu(true); }

BOOL WINAPI DestroyMenu(HMENU h) {
  if (!MenuOrFail(h)) return FALSE;
  DestroyTree(h);
  return TRUE;
}

BOOL WINAPI IsMenu(HMENU h) { return MenuOf(h) != NULL; }

BOOL WINAPI AppendMenuW(HMENU h, UINT flags, UINT_PTR id, LPCWSTR item) {
  return InsertCore(h, 0, flags, id, item, true);
}

BOOL WINAPI AppendMenuA(HMENU h, UINT flags, UINT_PTR id, LPCSTR item) {
  std::wstring buf;
  return InsertCore(h, 0, flags, id, WideItem(flags, item, buf), true);
}

BOOL WINAPI InsertMenuW(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCWSTR item) {
  return InsertCore(h, pos, flags, id, item, pos == (UINT)-1 && (flags & MF_BYPOSITION));
}

BOOL WINAPI InsertMenuA(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCSTR item) {
  std::wstring buf;
  return InsertMenuW(h, pos, flags, id, WideItem(flags, item, buf));
}

BOOL WINAPI ModifyMenuW(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCWSTR item) {
  return ModifyCore(h, pos, flags, id, item);
}

BOOL WINAPI ModifyMenuA(HMENU h, UINT pos, UINT flags, UINT_PTR id, LPCSTR item) {
  std::wstring buf;
  return ModifyCore(h, pos, flags, id, WideItem(flags, item, buf));
}

BOOL WINAPI RemoveMenu(HMENU h, UINT pos, UINT flags) { return RemoveCore(h, pos, flags, false); }
BOOL WINAPI DeleteMenu(HMENU h, UINT pos, UINT flags) { return RemoveCore(h, pos, flags, true); }

BOOL WINAPI InsertMenuItemW(HMENU h, UINT item, BOOL byPos, LPCMENUITEMINFOW mi) {
  if (!mi || !SizeOk(mi->cbSize)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  MenuItem it;
  ApplyInfo(&it, mi);
  if (byPos) {
    if (item >= m->items.size())
      m->items.push_back(it);
    else
      m->items.insert(m->items.begin() + item, it);
    return TRUE;
  }
  ItemRef r;
  if (!FindItem(h, item, MF_BYCOMMAND, &r)) {
    m->items.push_back(it);
    return TRUE;
  }
  r.m->items.insert(r.m->items.begin() + r.pos, it);
  return TRUE;
}

BOOL WINAPI InsertMenuItemA(HMENU h, UINT item, BOOL byPos, LPCMENUITEMINFOA mi) {
  if (!mi || !SizeOk(mi->cbSize)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::wstring buf;
  MENUITEMINFOW w = InfoToW(mi, buf);
  return InsertMenuItemW(h, item, byPos, &w);
}

BOOL WINAPI SetMenuItemInfoW(HMENU h, UINT item, BOOL byPos, LPCMENUITEMINFOW mi) {
  if (!mi || !SizeOk(mi->cbSize)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, byPos ? MF_BYPOSITION : MF_BYCOMMAND, &r)) {
    SetLastError(ERROR_MENU_ITEM_NOT_FOUND);
    return FALSE;
  }
  ApplyInfo(r.item(), mi);
  return TRUE;
}

BOOL WINAPI SetMenuItemInfoA(HMENU h, UINT item, BOOL byPos, LPCMENUITEMINFOA mi) {
  if (!mi || !SizeOk(mi->cbSize)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::wstring buf;
  MENUITEMINFOW w = InfoToW(mi, buf);
  return SetMenuItemInfoW(h, item, byPos, &w);
}

BOOL WINAPI GetMenuItemInfoW(HMENU h, UINT item, BOOL byPos, LPMENUITEMINFOW mi) {
  return GetInfoCore(h, item, byPos, mi, true);
}

BOOL WINAPI GetMenuItemInfoA(HMENU h, UINT item, BOOL byPos, LPMENUITEMINFOA mi) {
  return GetInfoCore(h, item, byPos, (MENUITEMINFOW*)mi, false);
}

BOOL WINAPI GetMenuInfo(HMENU h, LPMENUINFO mi) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  if (!mi || mi->cbSize != sizeof(MENUINFO)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (mi->fMask & MIM_STYLE) mi->dwStyle = m->style;
  if (mi->fMask & MIM_MAXHEIGHT) mi->cyMax = m->maxHeight;
  if (mi->fMask & MIM_BACKGROUND) mi->hbrBack = m->back;
  if (mi->fMask & MIM_HELPID) mi->dwContextHelpID = m->helpId;
  if (mi->fMask & MIM_MENUDATA) mi->dwMenuData = m->menuData;
  return TRUE;
}

static void SetInfo(HMENU h, LPCMENUINFO mi, int depth) {
  MenuObj* m = MenuOf(h);
  if (!m || depth > 32) return;
  if (mi->fMask & MIM_STYLE) m->style = mi->dwStyle;
  if (mi->fMask & MIM_MAXHEIGHT) m->maxHeight = mi->cyMax;
  if (mi->fMask & MIM_BACKGROUND) m->back = mi->hbrBack;
  if (mi->fMask & MIM_HELPID) m->helpId = mi->dwContextHelpID;
  if (mi->fMask & MIM_MENUDATA) m->menuData = mi->dwMenuData;
  if (mi->fMask & MIM_APPLYTOSUBMENUS) {
    std::vector<HMENU> subs;
    for (MenuItem& it : m->items)
      if (it.sub) subs.push_back(it.sub);
    for (HMENU s : subs) SetInfo(s, mi, depth + 1);
  }
}

BOOL WINAPI SetMenuInfo(HMENU h, LPCMENUINFO mi) {
  if (!MenuOrFail(h)) return FALSE;
  if (!mi || mi->cbSize != sizeof(MENUINFO)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  SetInfo(h, mi, 0);
  return TRUE;
}

DWORD WINAPI GetMenuContextHelpId(HMENU h) {
  MenuObj* m = MenuOrFail(h);
  return m ? m->helpId : 0;
}

BOOL WINAPI SetMenuContextHelpId(HMENU h, DWORD id) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  m->helpId = id;
  return TRUE;
}

int WINAPI GetMenuItemCount(HMENU h) {
  MenuObj* m = MenuOrFail(h);
  return m ? (int)m->items.size() : -1;
}

UINT WINAPI GetMenuItemID(HMENU h, int pos) {
  MenuObj* m = MenuOrFail(h);
  if (!m || pos < 0 || pos >= (int)m->items.size()) return (UINT)-1;
  const MenuItem& it = m->items[pos];
  return it.sub ? (UINT)-1 : it.id;
}

HMENU WINAPI GetSubMenu(HMENU h, int pos) {
  MenuObj* m = MenuOrFail(h);
  if (!m || pos < 0 || pos >= (int)m->items.size()) return NULL;
  return m->items[pos].sub;
}

UINT WINAPI GetMenuState(HMENU h, UINT item, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return (UINT)-1;
  const MenuItem& it = *r.item();
  if (it.sub) {
    MenuObj* s = MenuOf(it.sub);
    UINT n = s ? (UINT)s->items.size() : 0;
    return (n << 8) | ((FlagsOf(it) | MF_POPUP) & 0xFF);
  }
  return FlagsOf(it);
}

int WINAPI GetMenuStringW(HMENU h, UINT item, LPWSTR out, int max, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return 0;
  const MenuItem& it = *r.item();
  if (it.Bitmap() || it.Owner() || it.Separator()) {
    if (out && max > 0) out[0] = 0;
    return 0;
  }
  int len = (int)it.text.size();
  if (!out || max <= 0) return len;
  int n = len < max - 1 ? len : max - 1;
  memcpy(out, it.text.c_str(), n * sizeof(WCHAR));
  out[n] = 0;
  return n;
}

int WINAPI GetMenuStringA(HMENU h, UINT item, LPSTR out, int max, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return 0;
  const MenuItem& it = *r.item();
  std::string s = (it.Bitmap() || it.Owner() || it.Separator()) ? std::string() : Narrow(it.text.c_str());
  if (!out || max <= 0) return (int)s.size();
  size_t n = s.size() < (size_t)max - 1 ? s.size() : (size_t)max - 1;
  while (n > 0 && n < s.size() && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  memcpy(out, s.c_str(), n);
  out[n] = 0;
  return (int)n;
}

DWORD WINAPI CheckMenuItem(HMENU h, UINT item, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return (DWORD)-1;
  MenuItem* it = r.item();
  DWORD old = it->state & MF_CHECKED;
  if (flags & MF_CHECKED)
    it->state |= MF_CHECKED;
  else
    it->state &= ~MF_CHECKED;
  return old;
}

BOOL WINAPI CheckMenuRadioItem(HMENU h, UINT first, UINT last, UINT check, UINT flags) {
  ItemRef a, b;
  if (!MenuOrFail(h)) return FALSE;
  if (!FindItem(h, first, flags, &a) || !FindItem(h, last, flags, &b) || a.m != b.m) return FALSE;
  bool found = false;
  for (int i = a.pos; i <= b.pos; i++) {
    MenuItem& it = a.m->items[i];
    bool on = (flags & MF_BYPOSITION) ? (UINT)i == check : (!it.sub && it.id == check);
    if (on) {
      it.state |= MF_CHECKED;
      it.type |= MFT_RADIOCHECK;
      found = true;
    } else {
      it.state &= ~MF_CHECKED;
    }
  }
  return found;
}

BOOL WINAPI EnableMenuItem(HMENU h, UINT item, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return (BOOL)-1;
  MenuItem* it = r.item();
  UINT old = it->state & (MF_GRAYED | MF_DISABLED);
  it->state = (it->state & ~(MF_GRAYED | MF_DISABLED)) | (flags & (MF_GRAYED | MF_DISABLED));
  if (old != (it->state & (MF_GRAYED | MF_DISABLED)) && r.m->wnd) InvalidateRect(r.m->wnd, &it->rect, FALSE);
  return (BOOL)old;
}

BOOL WINAPI HiliteMenuItem(HWND hwnd, HMENU h, UINT item, UINT flags) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return FALSE;
  MenuItem* it = r.item();
  if (flags & MF_HILITE)
    it->state |= MF_HILITE;
  else
    it->state &= ~MF_HILITE;
  Window* w = GetWindow(hwnd);
  if (w && w->menu == h) DrawMenuBar(hwnd);
  return TRUE;
}

static UINT DefaultCore(HMENU h, UINT flags, int depth) {
  MenuObj* m = MenuOf(h);
  if (!m || depth > 32) return (UINT)-1;
  for (size_t i = 0; i < m->items.size(); i++) {
    MenuItem& it = m->items[i];
    if (!(it.state & MFS_DEFAULT)) continue;
    if (it.Disabled() && !(flags & GMDI_USEDISABLED)) return (UINT)-1;
    if (it.sub && (flags & GMDI_GOINTOPOPUPS)) {
      UINT inner = DefaultCore(it.sub, flags, depth + 1);
      if (inner != (UINT)-1) return inner;
    }
    return it.id;
  }
  return (UINT)-1;
}

UINT WINAPI GetMenuDefaultItem(HMENU h, UINT byPos, UINT flags) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return (UINT)-1;
  if (byPos) {
    for (size_t i = 0; i < m->items.size(); i++)
      if (m->items[i].state & MFS_DEFAULT) {
        if (m->items[i].Disabled() && !(flags & GMDI_USEDISABLED)) return (UINT)-1;
        return (UINT)i;
      }
    return (UINT)-1;
  }
  return DefaultCore(h, flags, 0);
}

BOOL WINAPI SetMenuDefaultItem(HMENU h, UINT item, UINT byPos) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  for (MenuItem& it : m->items) it.state &= ~MFS_DEFAULT;
  if (item == (UINT)-1) return TRUE;
  if (byPos) {
    if (item >= m->items.size()) return FALSE;
    m->items[item].state |= MFS_DEFAULT;
    return TRUE;
  }
  for (MenuItem& it : m->items)
    if (it.id == item) {
      it.state |= MFS_DEFAULT;
      return TRUE;
    }
  return FALSE;
}

BOOL WINAPI SetMenuItemBitmaps(HMENU h, UINT item, UINT flags, HBITMAP unchecked, HBITMAP checked) {
  ItemRef r;
  if (!MenuOrFail(h) || !FindItem(h, item, flags, &r)) return FALSE;
  r.item()->checked = checked;
  r.item()->unchecked = unchecked;
  return TRUE;
}

LONG WINAPI GetMenuCheckMarkDimensions(void) {
  return MAKELONG(GetSystemMetrics(SM_CXMENUCHECK), GetSystemMetrics(SM_CYMENUCHECK));
}

HMENU WINAPI LoadMenuIndirectW(const MENUTEMPLATEW* tmpl) {
  if (!tmpl) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  const WORD* p = (const WORD*)tmpl;
  WORD version = p[0], offset = p[1];
  HMENU menu = CreateMenu();
  if (version == 0) {
    ParseItems(p + 2 + offset / 2, menu, 0);
  } else if (version == 1) {
    const BYTE* base = (const BYTE*)tmpl;
    ParseItemsEx(base, base + 4 + offset, menu, 0);
  } else {
    DestroyMenu(menu);
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return menu;
}

HMENU WINAPI LoadMenuIndirectA(const MENUTEMPLATEA* tmpl) { return LoadMenuIndirectW(tmpl); }

HMENU WINAPI LoadMenuW(HINSTANCE inst, LPCWSTR name) {
  HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_MENU);
  if (!r) return NULL;
  const void* data = LockResource(LoadResource(inst, r));
  if (!data) return NULL;
  return LoadMenuIndirectW(data);
}

HMENU WINAPI LoadMenuA(HINSTANCE inst, LPCSTR name) {
  if (IS_INTRESOURCE(name)) return LoadMenuW(inst, (LPCWSTR)name);
  return LoadMenuW(inst, Widen(name).c_str());
}

HMENU WINAPI GetMenu(HWND hwnd) {
  Window* w = GetWindowOrFail(hwnd);
  return w ? w->menu : NULL;
}

BOOL WINAPI SetMenu(HWND hwnd, HMENU h) {
  Window* w = GetWindowOrFail(hwnd);
  if (!w) return FALSE;
  if (w->style & WS_CHILD) {
    SetLastError(ERROR_CHILD_WINDOW_MENU);
    return FALSE;
  }
  if (h && !MenuOrFail(h)) return FALSE;
  if (w->menu == h) return TRUE;
  w->menu = h;
  if (MenuObj* m = MenuOf(h)) m->owner = hwnd;
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  return TRUE;
}

BOOL WINAPI DrawMenuBar(HWND hwnd) {
  Window* w = GetWindowOrFail(hwnd);
  if (!w) return FALSE;
  if (w->Child() || !w->menu) return TRUE;
  RECT bar = MenuBarRect(w);
  if (RectEmpty(bar) || !ReallyVisible(w)) return TRUE;
  HDC dc = GetWindowDC(hwnd);
  DrawMenuBarIn(w, dc, bar);
  ReleaseDC(hwnd, dc);
  return TRUE;
}

HMENU WINAPI GetSystemMenu(HWND hwnd, BOOL revert) {
  Window* w = GetWindowOrFail(hwnd);
  if (!w) return NULL;
  if (revert) {
    if (w->sysMenu) {
      DestroyTree(w->sysMenu);
      w->sysMenu = NULL;
    }
    return NULL;
  }
  if (!(w->style & WS_SYSMENU)) return NULL;
  if (!w->sysMenu || !MenuOf(w->sysMenu)) {
    HMENU m = CreatePopupMenu();
    bool tool = (w->exStyle & WS_EX_TOOLWINDOW) != 0;
    if (!tool) AppendMenuW(m, MF_STRING, SC_RESTORE, L"&Restore");
    AppendMenuW(m, MF_STRING, SC_MOVE, L"&Move");
    AppendMenuW(m, MF_STRING, SC_SIZE, L"&Size");
    if (!tool) {
      AppendMenuW(m, MF_STRING, SC_MINIMIZE, L"Mi&nimize");
      AppendMenuW(m, MF_STRING, SC_MAXIMIZE, L"Ma&ximize");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, SC_CLOSE, L"&Close\tAlt+F4");
    SetMenuDefaultItem(m, SC_CLOSE, FALSE);
    if (MenuObj* o = MenuOf(m)) {
      o->system = true;
      o->owner = hwnd;
    }
    w->sysMenu = m;
    UpdateSystemMenu(w, m);
  }
  return w->sysMenu;
}

BOOL WINAPI GetMenuItemRect(HWND hwnd, HMENU h, UINT pos, LPRECT out) {
  MenuObj* m = MenuOrFail(h);
  if (!m || !out || pos >= m->items.size()) return FALSE;
  RECT r = m->items[pos].rect;
  if (m->wnd) {
    MapWindowPoints(m->wnd, NULL, (POINT*)&r, 2);
  } else {
    Window* w = GetWindow(hwnd);
    if (!w || w->menu != h) return FALSE;
    LayoutBar(m, MenuBarRect(w));
    r = m->items[pos].rect;
    RECT wr = ScreenRect(w);
    OffsetRect(&r, wr.left, wr.top);
  }
  *out = r;
  return TRUE;
}

int WINAPI MenuItemFromPoint(HWND hwnd, HMENU h, POINT p) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return -1;
  for (UINT i = 0; i < m->items.size(); i++) {
    RECT r;
    if (GetMenuItemRect(hwnd, h, i, &r) && PtInRect(&r, p)) return (int)i;
  }
  return -1;
}

BOOL WINAPI GetMenuBarInfo(HWND hwnd, LONG object, LONG item, PMENUBARINFO info) {
  Window* w = GetWindowOrFail(hwnd);
  if (!w || !info || info->cbSize != sizeof(MENUBARINFO)) return FALSE;
  HMENU h = NULL;
  RECT bar = MakeRect(0, 0, 0, 0);
  RECT wr = ScreenRect(w);
  if (object == OBJID_MENU) {
    h = w->Child() ? NULL : w->menu;
    bar = MenuBarRect(w);
    OffsetRect(&bar, wr.left, wr.top);
  } else if (object == OBJID_SYSMENU) {
    h = GetSystemMenu(hwnd, FALSE);
    bar = SysMenuRect(w);
    OffsetRect(&bar, wr.left, wr.top);
  } else if (object == OBJID_CLIENT) {
    h = (HMENU)SendMessageW(hwnd, MN_GETHMENU, 0, 0);
    GetWindowRect(hwnd, &bar);
  } else {
    return FALSE;
  }
  if (!h) return FALSE;
  info->hMenu = h;
  info->hwndMenu = NULL;
  info->fBarFocused = g_tracker && !g_tracker->levels.empty() && g_tracker->levels[0].menu == h;
  info->fFocused = FALSE;
  if (item > 0) {
    if (!GetMenuItemRect(hwnd, h, (UINT)(item - 1), &bar)) return FALSE;
    MenuObj* m = MenuOf(h);
    info->fFocused = m && m->hot == item - 1;
    if (m && m->hot == item - 1 && m->items[item - 1].sub)
      if (MenuObj* s = MenuOf(m->items[item - 1].sub)) info->hwndMenu = s->wnd;
  }
  info->rcBar = bar;
  return TRUE;
}

BOOL WINAPI TrackPopupMenuEx(HMENU h, UINT flags, int x, int y, HWND owner, LPTPMPARAMS params) {
  MenuObj* m = MenuOrFail(h);
  if (!m) return FALSE;
  if (!IsWindow(owner)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  if (g_tracker && !(flags & TPM_RECURSE)) {
    SetLastError(ERROR_POPUP_ALREADY_ACTIVE);
    return FALSE;
  }
  Tracker t;
  t.owner = owner;
  t.flags = flags;
  t.popup = true;
  t.levels.push_back({h, false});
  Begin(&t);
  InitPopup(&t, h, 0);
  if (!MenuOf(h) || !ShowPopup(h, x, y, flags, owner, params ? &params->rcExclude : NULL)) {
    SendMessageW(owner, WM_EXITMENULOOP, TRUE, 0);
    return FALSE;
  }
  UINT r = Run(&t);
  if (flags & TPM_RETURNCMD) return (BOOL)r;
  return TRUE;
}

BOOL WINAPI TrackPopupMenu(HMENU h, UINT flags, int x, int y, int, HWND owner, const RECT*) {
  return TrackPopupMenuEx(h, flags, x, y, owner, NULL);
}

BOOL WINAPI EndMenu(void) {
  if (!g_tracker) return FALSE;
  g_tracker->done = true;
  PostMessageW(g_tracker->owner, WM_NULL, 0, 0);
  return TRUE;
}

}  // extern "C"
