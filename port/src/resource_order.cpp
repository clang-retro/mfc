// resource_order.cpp - puts the numbers of compiled resources in the byte
// order of the processor.
//
// A .res file holds its numbers little-endian, as Windows does. Programs
// and the layer read the data of a menu, a dialog, a string table and the
// other standard resources as structures of WORD and DWORD. On a
// big-endian processor (PowerPC Mac OS X, 68k and PowerPC Mac OS), the
// layer therefore turns these numbers around once, when the resources are
// registered. Text stays text: the characters of a string are UTF-16
// numbers, so they turn around as well. Bytes (pixels, the creation data
// of a control) stay as they are.
//
// Message tables, version resources and the types that the program
// defines keep the order of the file.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <string.h>
#include <vector>

namespace winport {

namespace {

// Walks a resource in the order of the file and writes each number in
// the order of the processor into a copy of the data.
struct Walker {
  const BYTE* src;
  BYTE* dst;
  size_t size;
  size_t pos = 0;
  bool bad = false;

  // Reads a 16-bit number at the position, writes it in the order of the
  // processor, and steps over it. Past the end, it marks the walk bad.
  WORD W16() {
    if (pos + 2 > size) {
      bad = true;
      pos = size;
      return 0;
    }
    WORD v = (WORD)(src[pos] | (src[pos + 1] << 8));
    memcpy(dst + pos, &v, 2);
    pos += 2;
    return v;
  }

  // Reads a 32-bit number at the position, writes it in the order of the
  // processor, and steps over it.
  DWORD W32() {
    if (pos + 4 > size) {
      bad = true;
      pos = size;
      return 0;
    }
    DWORD v = (DWORD)src[pos] | ((DWORD)src[pos + 1] << 8) | ((DWORD)src[pos + 2] << 16) |
              ((DWORD)src[pos + 3] << 24);
    memcpy(dst + pos, &v, 4);
    pos += 4;
    return v;
  }

  // Steps over n bytes, which keep their order.
  void Bytes(size_t n) {
    if (pos + n > size) {
      bad = true;
      pos = size;
      return;
    }
    pos += n;
  }

  // Steps to the next multiple of four from the start of the resource.
  void Align4() { pos = (pos + 3) & ~(size_t)3; if (pos > size) pos = size; }

  // Converts a string of 16-bit characters up to and with its zero.
  void Text() {
    while (!bad && W16() != 0) {
    }
  }

  // Converts a name or an ordinal (0xFFFF and a number), as dialogs and
  // menus hold them.
  void SzOrOrd() {
    if (bad) return;
    if (W16() == 0xFFFF) {
      W16();
      return;
    }
    pos -= 2;
    Text();
  }

  // Converts all the rest as 16-bit numbers.
  void AllWords() {
    while (!bad && pos + 2 <= size) W16();
  }
};

// The items of an extended menu, and their submenus.
void MenuExItems(Walker& w, int depth) {
  while (!w.bad && w.pos < w.size) {
    w.W32();  // type
    w.W32();  // state
    w.W32();  // ID
    WORD info = w.W16();
    w.Text();
    w.Align4();
    if (info & 0x01) {
      w.W32();  // the help ID of the submenu
      if (depth < 32) MenuExItems(w, depth + 1);
    }
    if (info & 0x80) return;
  }
}

// A menu: the standard one is only 16-bit numbers.
void Menu(Walker& w) {
  if (w.size < 4) return;
  WORD version = (WORD)(w.src[0] | (w.src[1] << 8));
  if (version != 1) {
    w.AllWords();
    return;
  }
  w.W16();
  WORD offset = w.W16();
  // The header (with its help ID) up to the first item.
  while (!w.bad && w.pos < 4 + (size_t)offset) w.W32();
  MenuExItems(w, 0);
}

// A dialog, the standard one or the extended one, with its controls.
void Dialog(Walker& w) {
  if (w.size < 4) return;
  bool ex = (w.src[0] | (w.src[1] << 8)) == 1 && (w.src[2] | (w.src[3] << 8)) == 0xFFFF;
  DWORD style;
  if (ex) {
    w.W16();  // version
    w.W16();  // signature
    w.W32();  // help ID
    w.W32();  // extended style
    style = w.W32();
  } else {
    style = w.W32();
    w.W32();  // extended style
  }
  WORD count = w.W16();
  for (int i = 0; i < 4; i++) w.W16();  // x, y, cx, cy
  w.SzOrOrd();  // menu
  w.SzOrOrd();  // class
  w.Text();     // title
  if (style & (DS_SETFONT | DS_SHELLFONT)) {
    w.W16();  // point size
    if (ex) {
      w.W16();     // weight
      w.Bytes(2);  // italic, character set
    }
    w.Text();  // typeface
  }
  for (WORD i = 0; i < count && !w.bad; i++) {
    w.Align4();
    if (ex) {
      w.W32();  // help ID
      w.W32();  // extended style
      w.W32();  // style
    } else {
      w.W32();  // style
      w.W32();  // extended style
    }
    for (int k = 0; k < 4; k++) w.W16();  // x, y, cx, cy
    if (ex)
      w.W32();  // ID
    else
      w.W16();
    w.SzOrOrd();  // class
    w.SzOrOrd();  // text
    WORD extra = w.W16();
    w.Bytes(extra);  // creation data
  }
}

// A BITMAPINFOHEADER (or a BITMAPCOREHEADER, or a later header) and the
// color masks of BI_BITFIELDS. The color table and the pixels are bytes.
void BitmapHeader(Walker& w) {
  if (w.pos + 4 > w.size) return;
  size_t start = w.pos;
  DWORD headerSize = w.W32();
  if (headerSize == 12) {
    for (int i = 0; i < 4; i++) w.W16();  // width, height, planes, bit count
    return;
  }
  if (headerSize < 16) return;
  w.W32();  // width
  w.W32();  // height
  w.W16();  // planes
  w.W16();  // bit count
  DWORD compression = 0;
  if (headerSize >= 20) compression = w.W32();
  while (!w.bad && w.pos + 4 <= start + headerSize) w.W32();
  // The three masks after a 40-byte header with BI_BITFIELDS.
  if (headerSize == 40 && compression == BI_BITFIELDS)
    for (int i = 0; i < 3; i++) w.W32();
}

// An icon or a cursor image: a PNG stays as it is.
void IconImage(Walker& w, bool cursor) {
  if (cursor) {
    w.W16();  // hot spot x
    w.W16();  // hot spot y
  }
  if (w.pos + 4 <= w.size && memcmp(w.src + w.pos, "\x89PNG", 4) == 0) return;
  BitmapHeader(w);
}

// The directory of an icon or a cursor group.
void Group(Walker& w, bool cursor) {
  w.W16();  // reserved
  w.W16();  // type
  WORD count = w.W16();
  for (WORD i = 0; i < count && !w.bad; i++) {
    if (cursor) {
      w.W16();  // width
      w.W16();  // height
    } else {
      w.Bytes(4);  // width, height, colors, reserved
    }
    w.W16();  // planes
    w.W16();  // bit count
    w.W32();  // size
    w.W16();  // ID
  }
}

// The initial data of the controls of a dialog (RT_DLGINIT): for each,
// the control's ID, the message and the length, and the data as bytes.
void DlgInit(Walker& w) {
  while (!w.bad && w.pos + 2 <= w.size) {
    if (w.W16() == 0) return;  // the end
    w.W16();                   // message
    DWORD length = w.W32();
    w.Bytes(length);
  }
}

}  // namespace

// Gives the data of a resource with its numbers in the order of the
// processor: on a big-endian processor a converted copy, else the data.
const BYTE* NativeResourceData(WORD type, const BYTE* data, DWORD size) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  const bool convert = true;
#else
  const bool convert = false;
#endif
  return convert ? ConvertResourceData(type, data, size) : data;
}

// Gives a copy of the data of a resource of a standard type, with its
// numbers read little-endian and written in the order of the processor.
// The data of other types comes back as it is.
const BYTE* ConvertResourceData(WORD type, const BYTE* data, DWORD size) {
  switch (type) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 9: case 12: case 14: case 240: case 241:
      break;
    default:
      return data;
  }
  // The copy lives as long as the program, as the registered data does.
  BYTE* copy = new BYTE[size ? size : 1];
  memcpy(copy, data, size);
  Walker w{data, copy, size};
  switch (type) {
    case 1:  // RT_CURSOR
      IconImage(w, true);
      break;
    case 2:  // RT_BITMAP
      BitmapHeader(w);
      break;
    case 3:  // RT_ICON
      IconImage(w, false);
      break;
    case 4:  // RT_MENU
      Menu(w);
      break;
    case 5:  // RT_DIALOG
      Dialog(w);
      break;
    case 6:  // RT_STRING
    case 9:  // RT_ACCELERATOR
    case 241:  // RT_TOOLBAR
      w.AllWords();
      break;
    case 12:  // RT_GROUP_CURSOR
      Group(w, true);
      break;
    case 14:  // RT_GROUP_ICON
      Group(w, false);
      break;
    case 240:  // RT_DLGINIT
      DlgInit(w);
      break;
  }
  return copy;
}

}  // namespace winport
