// user_icon.cpp - icons, cursors, bitmaps loaded from resources and
// files, LoadImage and CopyImage, and accelerator tables.
//
// An icon or cursor keeps the two bitmaps Windows gives it: an AND mask
// (monochrome; a monochrome icon's mask is twice as tall and holds the
// XOR image in its lower half) and a color image that is XORed on, or,
// for a 32-bit image with alpha, blended. The system's own icons and
// cursors are drawn here; each remembers its IDI_ or IDC_ number so that
// a backend can show the platform's cursor instead.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace winport {

IconObj::~IconObj() {
  if (mask) DeleteObject(mask);
  if (color) DeleteObject(color);
}

IconObj* IconOf(HICON h) {
  IconObj* o = GdiAs<IconObj>((HGDIOBJ)h, kObjIcon);
  return o;
}

}  // namespace winport

namespace {

// ---------------------------------------------------------------------
// Making icons

HBITMAP CopyBitmap(HBITMAP src) {
  if (!src) return NULL;
  BITMAP bm;
  if (!GetObjectW(src, sizeof bm, &bm)) return NULL;
  HDC sdc = CreateCompatibleDC(NULL), ddc = CreateCompatibleDC(NULL);
  HBITMAP dst;
  if (bm.bmBitsPixel == 1) {
    dst = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
  } else {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits;
    dst = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    // A 32-bit source keeps its alpha, which BitBlt would drop.
    if (dst && bm.bmBitsPixel == 32 && GetDIBits(sdc, src, 0, bm.bmHeight, bits, &bi, DIB_RGB_COLORS)) {
      DeleteDC(sdc);
      DeleteDC(ddc);
      return dst;
    }
  }
  HGDIOBJ o1 = SelectObject(sdc, src), o2 = SelectObject(ddc, dst);
  BitBlt(ddc, 0, 0, bm.bmWidth, bm.bmHeight, sdc, 0, 0, SRCCOPY);
  SelectObject(sdc, o1);
  SelectObject(ddc, o2);
  DeleteDC(sdc);
  DeleteDC(ddc);
  return dst;
}

HICON NewIcon(bool isIcon, POINT hot, HBITMAP mask, HBITMAP color, bool alpha) {
  BITMAP bm;
  if (!mask || !GetObjectW(mask, sizeof bm, &bm)) {
    if (mask) DeleteObject(mask);
    if (color) DeleteObject(color);
    return NULL;
  }
  IconObj* o = new IconObj;
  o->isIcon = isIcon;
  o->hotspot = hot;
  o->mask = mask;
  o->color = color;
  o->alpha = alpha;
  o->width = bm.bmWidth;
  o->height = color ? bm.bmHeight : bm.bmHeight / 2;
  return (HICON)NewGdiHandle(o);
}

// A 32-bit top-down DIB section and its pixels.
HBITMAP NewDib32(int w, int h, DWORD** bits) {
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* p = NULL;
  HBITMAP b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &p, NULL, 0);
  *bits = (DWORD*)p;
  return b;
}

// Makes an icon from a DIB as icon resources hold it: the header's height
// covers the image and the mask below it.
HICON IconFromDib(const BYTE* data, DWORD size, bool isIcon, POINT hot, int cx, int cy) {
  if (size < sizeof(BITMAPINFOHEADER)) return NULL;
  BITMAPINFOHEADER bih;
  memcpy(&bih, data, sizeof bih);
  if (bih.biSize < sizeof(BITMAPINFOHEADER) || bih.biWidth <= 0 || bih.biHeight == 0) return NULL;
  int w = bih.biWidth, h = abs(bih.biHeight) / 2;
  if (h <= 0) return NULL;
  int bpp = bih.biBitCount;
  int colors = bih.biClrUsed ? (int)bih.biClrUsed : (bpp <= 8 ? 1 << bpp : 0);
  size_t colorBytes = (size_t)colors * 4 + (bih.biCompression == BI_BITFIELDS ? 12 : 0);
  size_t stride = (size_t)((w * bpp + 31) / 32) * 4;
  size_t maskStride = (size_t)((w + 31) / 32) * 4;
  size_t imageAt = bih.biSize + colorBytes;
  size_t maskAt = imageAt + stride * h;
  if (maskAt > size) return NULL;
  bool haveMask = maskAt + maskStride * h <= size;

  // The image, as Windows reads it: the header with half the height.
  std::vector<BYTE> info(bih.biSize + colorBytes);
  memcpy(info.data(), data, info.size());
  BITMAPINFOHEADER* ih = (BITMAPINFOHEADER*)info.data();
  ih->biHeight = h;
  ih->biSizeImage = 0;
  const BYTE* image = data + imageAt;

  // The mask, 1 where the screen shows through; bottom-up rows.
  std::vector<BYTE> mask(maskStride * h * (bpp == 1 ? 2 : 1), 0);
  if (haveMask) memcpy(mask.data(), data + maskAt, maskStride * h);

  bool alpha = false;
  if (bpp == 32)
    for (int i = 0; i < w * h && !alpha; i++)
      if (image[(size_t)i * 4 + 3]) alpha = true;

  int outW = cx > 0 ? cx : w, outH = cy > 0 ? cy : h;
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP hmask, hcolor = NULL;
  if (bpp == 1) {
    // Monochrome: the mask on top, the image below, in one bitmap.
    hmask = CreateBitmap(outW, outH * 2, 1, 1, NULL);
    HGDIOBJ old = SelectObject(dc, hmask);
    struct {
      BITMAPINFOHEADER h;
      RGBQUAD c[2];
    } mono = {};
    mono.h = *ih;
    mono.h.biSize = sizeof(BITMAPINFOHEADER);
    mono.h.biClrUsed = 2;
    mono.c[1] = {255, 255, 255, 0};
    mono.h.biCompression = BI_RGB;
    StretchDIBits(dc, 0, 0, outW, outH, 0, 0, w, h, mask.data(), (BITMAPINFO*)&mono, DIB_RGB_COLORS, SRCCOPY);
    StretchDIBits(dc, 0, outH, outW, outH, 0, 0, w, h, image, (BITMAPINFO*)&mono, DIB_RGB_COLORS, SRCCOPY);
    SelectObject(dc, old);
  } else {
    hmask = CreateBitmap(outW, outH, 1, 1, NULL);
    struct {
      BITMAPINFOHEADER h;
      RGBQUAD c[2];
    } mono = {};
    mono.h = *ih;
    mono.h.biSize = sizeof(BITMAPINFOHEADER);
    mono.h.biBitCount = 1;
    mono.h.biClrUsed = 2;
    mono.h.biCompression = BI_RGB;
    mono.c[1] = {255, 255, 255, 0};
    HGDIOBJ old = SelectObject(dc, hmask);
    StretchDIBits(dc, 0, 0, outW, outH, 0, 0, w, h, mask.data(), (BITMAPINFO*)&mono, DIB_RGB_COLORS, SRCCOPY);
    DWORD* bits;
    hcolor = NewDib32(outW, outH, &bits);
    SelectObject(dc, hcolor);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, outW, outH, 0, 0, w, h, image, (BITMAPINFO*)info.data(), DIB_RGB_COLORS, SRCCOPY);
    SelectObject(dc, old);
    GdiFlush();
    if (alpha) {
      // Keep the alpha (StretchDIBits copied the 32-bit pixels whole when
      // the size is the same), premultiplied for AlphaBlend.
      if (outW == w && outH == h) {
        for (int y = 0; y < h; y++) {
          const DWORD* src = (const DWORD*)(image + stride * (size_t)(h - 1 - y));
          for (int x = 0; x < w; x++) bits[(size_t)y * w + x] = src[x];
        }
      }
      for (int i = 0; i < outW * outH; i++) {
        DWORD p = bits[i];
        DWORD a = p >> 24;
        DWORD r = ((p >> 16) & 0xFF) * a / 255, g = ((p >> 8) & 0xFF) * a / 255, b = (p & 0xFF) * a / 255;
        bits[i] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
  }
  DeleteDC(dc);
  hot.x = hot.x * outW / w;
  hot.y = hot.y * outH / h;
  return NewIcon(isIcon, hot, hmask, hcolor, alpha);
}

// ---------------------------------------------------------------------
// The system's icons and cursors

// Pictures: '.' shows the screen through, 'X' black, 'O' white, '~'
// inverts the screen.
HCURSOR CursorFromArt(const char* const* art, int hx, int hy, int id) {
  const int size = 32;
  HBITMAP mask = CreateBitmap(size, size * 2, 1, 1, NULL);
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, mask);
  RECT top = {0, 0, size, size}, bottom = {0, size, size, size * 2};
  FillRect(dc, &top, (HBRUSH)GetStockObject(WHITE_BRUSH));
  FillRect(dc, &bottom, (HBRUSH)GetStockObject(BLACK_BRUSH));
  for (int y = 0; art[y]; y++)
    for (int x = 0; art[y][x]; x++) {
      char c = art[y][x];
      if (c == 'X') {
        SetPixelV(dc, x, y, RGB(0, 0, 0));
      } else if (c == 'O') {
        SetPixelV(dc, x, y, RGB(0, 0, 0));
        SetPixelV(dc, x, y + size, RGB(255, 255, 255));
      } else if (c == '~') {
        SetPixelV(dc, x, y + size, RGB(255, 255, 255));
      }
    }
  SelectObject(dc, old);
  DeleteDC(dc);
  HCURSOR h = NewIcon(false, POINT{hx, hy}, mask, NULL, false);
  if (IconObj* o = IconOf(h)) {
    o->systemId = id;
    o->stock = true;
  }
  return h;
}

const char* const kArrow[] = {
    "X", "XX", "XOX", "XOOX", "XOOOX", "XOOOOX", "XOOOOOX", "XOOOOOOX", "XOOOOOOOX", "XOOOOOOOOX",
    "XOOOOOOOOOX", "XOOOOOOXXXXX", "XOOOXOOX", "XOOXXOOX", "XOX..XOOX", "XX...XOOX", "X.....XOOX",
    "......XOOX", ".......XX", NULL};
const char* const kIBeam[] = {"XXX.XXX", "...X...", "...X...", "...X...", "...X...", "...X...", "...X...",
                              "...X...", "...X...", "...X...", "...X...", "...X...", "...X...", "...X...",
                              "...X...", "XXX.XXX", NULL};
const char* const kWait[] = {"XXXXXXXXXXX", "XOOOOOOOOOX", ".XOOOOOOOX.", ".XOOOOOOOX.", "..XOOOOOX..",
                             "...XOOOX...", "....XOX....", "...XOOOX...", "..XOOOOOX..", ".XOOOOOOOX.",
                             ".XOOOOOOOX.", "XOOOOOOOOOX", "XXXXXXXXXXX", NULL};
const char* const kCross[] = {".........X.........", ".........X.........", ".........X.........",
                              ".........X.........", ".........X.........", ".........X.........",
                              ".........X.........", ".........X.........", ".........X.........",
                              "XXXXXXXXXXXXXXXXXXX", ".........X.........", ".........X.........",
                              ".........X.........", ".........X.........", ".........X.........",
                              ".........X.........", ".........X.........", ".........X.........",
                              ".........X.........", NULL};
const char* const kSizeWE[] = {"...X.......X...", "..XX.......XX..", ".XOXXXXXXXXXOX.", "XOOOOOOOOOOOOOX",
                               ".XOXXXXXXXXXOX.", "..XX.......XX..", "...X.......X...", NULL};
const char* const kSizeNS[] = {"...X...", "..XOX..", ".XOOOX.", "XXXOXXX", "...X...", "...X...", "...X...",
                               "...X...", "...X...", "...X...", "...X...", "XXXOXXX", ".XOOOX.", "..XOX..",
                               "...X...", NULL};
const char* const kSizeNWSE[] = {"XXXXX......", "XOOX.......", "XOX........", "XX.X.......", "X...X......",
                                 ".....X.....", "......X...X", ".......X.XX", "........XOX", ".......XOOX",
                                 "......XXXXX", NULL};
const char* const kSizeNESW[] = {"......XXXXX", ".......XOOX", "........XOX", ".......X.XX", "......X...X",
                                 ".....X.....", "X...X......", "XX.X.......", "XOX........", "XOOX.......",
                                 "XXXXX......", NULL};
const char* const kSizeAll[] = {"....X....", "...XOX...", "..XOOOX..", "....X....", ".X..X..X.", "XO..X..OX",
                                "XOXXXXXOX", "XO..X..OX", ".X..X..X.", "....X....", "..XOOOX..", "...XOX...",
                                "....X....", NULL};
const char* const kUpArrow[] = {"....X....", "...XOX...", "..XOOOX..", ".XOOOOOX.", "XXXXOXXXX", "...XOX...",
                                "...XOX...", "...XOX...", "...XOX...", "...XOX...", "...XOX...", "...XXX...",
                                NULL};
const char* const kNo[] = {"....XXXXX....", "..XXOOOOOXX..", ".XOOXXXXXOOX.", ".XOX.....XOOX", "XOX.....XOXOX",
                           "XOX....XOX.OX", "XOX...XOX..OX", "XOX..XOX...OX", "XOX.XOX...XOX", "XOXOX.....XOX",
                           ".XOOX....XOX.", ".XOOXXXXXOOX.", "..XXOOOOOXX..", "....XXXXX....", NULL};

struct SystemCursor {
  int id;
  const char* const* art;
  int hx, hy;
};
const SystemCursor kCursors[] = {
    {32512, kArrow, 0, 0},     {32513, kIBeam, 3, 8},     {32514, kWait, 5, 6},     {32515, kCross, 9, 9},
    {32516, kUpArrow, 4, 0},   {32642, kSizeNWSE, 5, 5},  {32643, kSizeNESW, 5, 5}, {32644, kSizeWE, 7, 3},
    {32645, kSizeNS, 3, 7},    {32646, kSizeAll, 4, 6},   {32648, kNo, 6, 6},       {32649, kArrow, 0, 0},
    {32650, kWait, 5, 6},      {32651, kArrow, 0, 0},     {32640, kSizeAll, 4, 6},  {32641, kSizeAll, 4, 6},
};

// The system icons, drawn with GDI. In the mask pass everything that is
// drawn is black; in the color pass, in its colors.
void DrawSystemIcon(HDC dc, int id, bool maskPass) {
  auto color = [&](COLORREF c) { return maskPass ? RGB(0, 0, 0) : c; };
  HPEN pen = CreatePen(PS_SOLID, 1, color(RGB(0, 0, 0)));
  HGDIOBJ op = SelectObject(dc, pen);
  auto brushed = [&](COLORREF c, auto draw) {
    HBRUSH b = CreateSolidBrush(color(c));
    HGDIOBJ ob = SelectObject(dc, b);
    draw();
    SelectObject(dc, ob);
    DeleteObject(b);
  };
  auto glyph = [&](const wchar_t* s, COLORREF c, int size) {
    if (maskPass) return;
    HFONT f = CreateFontW(-size, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0,
                          L"Times New Roman");
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT r = {0, 0, 32, 32};
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
    DeleteObject(f);
  };
  switch (id) {
    case 32513:  // IDI_HAND: a red disc with a white cross
      brushed(RGB(255, 0, 0), [&] { Ellipse(dc, 2, 2, 30, 30); });
      if (!maskPass) {
        HPEN w = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
        HGDIOBJ o = SelectObject(dc, w);
        MoveToEx(dc, 10, 10, NULL);
        LineTo(dc, 21, 21);
        MoveToEx(dc, 21, 10, NULL);
        LineTo(dc, 10, 21);
        SelectObject(dc, o);
        DeleteObject(w);
      }
      break;
    case 32514:  // IDI_QUESTION
      brushed(RGB(255, 255, 255), [&] { Ellipse(dc, 2, 2, 30, 30); });
      glyph(L"?", RGB(0, 0, 255), 24);
      break;
    case 32515: {  // IDI_EXCLAMATION: a yellow triangle
      POINT tri[] = {{15, 1}, {30, 29}, {1, 29}};
      brushed(RGB(255, 255, 0), [&] { Polygon(dc, tri, 3); });
      glyph(L"!", RGB(0, 0, 0), 20);
      break;
    }
    case 32516:  // IDI_ASTERISK: a white balloon with a blue "i"
      brushed(RGB(255, 255, 255), [&] { Ellipse(dc, 2, 2, 30, 30); });
      glyph(L"i", RGB(0, 0, 255), 24);
      break;
    default: {  // IDI_APPLICATION, IDI_WINLOGO: a little window
      brushed(RGB(255, 255, 255), [&] { Rectangle(dc, 2, 4, 30, 28); });
      brushed(RGB(0, 0, 128), [&] { Rectangle(dc, 2, 4, 30, 9); });
      break;
    }
  }
  SelectObject(dc, op);
  DeleteObject(pen);
}

HICON SystemIcon(int id) {
  const int size = 32;
  HDC dc = CreateCompatibleDC(NULL);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
  DWORD* bits;
  HBITMAP color = NewDib32(size, size, &bits);
  RECT all = {0, 0, size, size};
  HGDIOBJ old = SelectObject(dc, mask);
  FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
  DrawSystemIcon(dc, id, true);
  SelectObject(dc, color);
  FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
  DrawSystemIcon(dc, id, false);
  // Outside the shape the image must be black, so that XOR leaves the screen.
  SelectObject(dc, mask);
  HDC cdc = CreateCompatibleDC(NULL);
  HGDIOBJ oc = SelectObject(cdc, color);
  SetTextColor(cdc, RGB(0, 0, 0));
  SetBkColor(cdc, RGB(255, 255, 255));
  BitBlt(cdc, 0, 0, size, size, dc, 0, 0, 0x00220326 /* DSna */);
  SelectObject(cdc, oc);
  DeleteDC(cdc);
  SelectObject(dc, old);
  DeleteDC(dc);
  HICON h = NewIcon(true, POINT{size / 2, size / 2}, mask, color, false);
  if (IconObj* o = IconOf(h)) {
    o->systemId = id;
    o->stock = true;
  }
  return h;
}

std::mutex& SystemLock() {
  static std::mutex* m = new std::mutex;
  return *m;
}

HCURSOR SystemCursorFor(int id) {
  static std::map<int, HCURSOR>* made = new std::map<int, HCURSOR>;
  std::lock_guard<std::mutex> g(SystemLock());
  auto it = made->find(id);
  if (it != made->end()) return it->second;
  for (const SystemCursor& c : kCursors)
    if (c.id == id) return (*made)[id] = CursorFromArt(c.art, c.hx, c.hy, id);
  return NULL;
}

HICON SystemIconFor(int id) {
  if (id == 32517) id = 32512;  // IDI_WINLOGO
  if (id < 32512 || id > 32516) return NULL;
  static std::map<int, HICON>* made = new std::map<int, HICON>;
  std::lock_guard<std::mutex> g(SystemLock());
  auto it = made->find(id);
  if (it != made->end()) return it->second;
  return (*made)[id] = SystemIcon(id);
}

// ---------------------------------------------------------------------
// Icon and cursor directories

#pragma pack(push, 2)
struct GroupHeader {
  WORD reserved, type, count;
};
struct GroupEntry {
  BYTE width, height, colors, reserved;  // a cursor's: WORD width, WORD height (doubled)
  WORD planes, bitCount;
  DWORD bytes;
  WORD id;
};
#pragma pack(pop)

int Pick(const BYTE* dir, bool icon, int cx, int cy, UINT flags) {
  const GroupHeader* h = (const GroupHeader*)dir;
  if (!h || h->count == 0) return 0;
  const GroupEntry* e = (const GroupEntry*)(dir + sizeof(GroupHeader));
  if (cx == 0) cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
  if (cy == 0) cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
  if (flags & LR_DEFAULTSIZE) {
    cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
  }
  int best = -1;
  long bestScore = 0;
  for (int i = 0; i < h->count; i++) {
    int w, ht, bits;
    if (icon) {
      w = e[i].width ? e[i].width : 256;
      ht = e[i].height ? e[i].height : 256;
      bits = e[i].bitCount ? e[i].bitCount : 8;
    } else {
      const WORD* c = (const WORD*)&e[i];
      w = c[0];
      ht = c[1] / 2;
      bits = e[i].bitCount;
    }
    if (flags & LR_MONOCHROME) bits = bits == 1 ? 32 : 1;
    long score = (long)abs(w - cx) * 100000L + (long)abs(ht - cy) * 100000L + (32 - std::min(bits, 32));
    if (best < 0 || score < bestScore) {
      best = i;
      bestScore = score;
    }
  }
  return e[best].id;
}

std::wstring KeyOf(HINSTANCE inst, LPCWSTR name, UINT type, int cx, int cy) {
  wchar_t buf[96];
  swprintf(buf, 96, L"%p:%u:%d:%d:", (void*)inst, type, cx, cy);
  std::wstring k = buf;
  if (IS_INTRESOURCE(name)) {
    swprintf(buf, 96, L"#%u", (unsigned)LOWORD((UINT_PTR)name));
    k += buf;
  } else {
    k += name;
  }
  return k;
}

std::map<std::wstring, HANDLE>& Shared() {
  static std::map<std::wstring, HANDLE>* m = new std::map<std::wstring, HANDLE>;
  return *m;
}

std::vector<BYTE> ReadFile(LPCWSTR path) {
  std::vector<BYTE> data;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) return data;
  DWORD size = GetFileSize(f, NULL);
  if (size != INVALID_FILE_SIZE && size < (64u << 20)) {
    data.resize(size);
    DWORD got = 0;
    if (!::ReadFile(f, data.data(), size, &got, NULL) || got != size) data.clear();
  }
  CloseHandle(f);
  return data;
}

// An icon or cursor from a .ico or .cur file.
HICON IconFromFile(LPCWSTR path, bool icon, int cx, int cy, UINT flags) {
  std::vector<BYTE> d = ReadFile(path);
  if (d.size() < 6) return NULL;
  const GroupHeader* h = (const GroupHeader*)d.data();
  if (h->reserved != 0 || (h->type != 1 && h->type != 2) || h->count == 0) return NULL;
  // File entries are 16 bytes: the size and offset of each image follow
  // the same first fields as a resource directory's.
  struct FileEntry {
    BYTE width, height, colors, reserved;
    WORD a, b;  // planes and bits, or a cursor's hotspot
    DWORD bytes, offset;
  };
  if (d.size() < 6 + (size_t)h->count * 16) return NULL;
  const FileEntry* fe = (const FileEntry*)(d.data() + 6);
  if (cx == 0 || (flags & LR_DEFAULTSIZE)) cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
  if (cy == 0 || (flags & LR_DEFAULTSIZE)) cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
  int best = 0;
  long bestScore = -1;
  for (int i = 0; i < h->count; i++) {
    int w = fe[i].width ? fe[i].width : 256, ht = fe[i].height ? fe[i].height : 256;
    int bits = h->type == 1 ? fe[i].b : 0;
    long score = (long)abs(w - cx) * 100000L + (long)abs(ht - cy) * 100000L + (32 - std::min(bits, 32));
    if (bestScore < 0 || score < bestScore) {
      best = i;
      bestScore = score;
    }
  }
  const FileEntry& e = fe[best];
  if ((size_t)e.offset + e.bytes > d.size()) return NULL;
  POINT hot = {0, 0};
  if (h->type == 2) hot = {e.a, e.b};
  return IconFromDib(d.data() + e.offset, e.bytes, icon, hot, (flags & LR_DEFAULTSIZE) || cx ? cx : 0,
                     (flags & LR_DEFAULTSIZE) || cy ? cy : 0);
}

HICON IconFromResource(HINSTANCE inst, LPCWSTR name, bool icon, int cx, int cy, UINT flags) {
  HRSRC g = FindResourceW(inst, name, icon ? (LPCWSTR)RT_GROUP_ICON : (LPCWSTR)RT_GROUP_CURSOR);
  if (!g) return NULL;
  BYTE* dir = (BYTE*)LockResource(LoadResource(inst, g));
  int id = Pick(dir, icon, cx, cy, flags);
  if (!id) return NULL;
  HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(id), icon ? (LPCWSTR)RT_ICON : (LPCWSTR)RT_CURSOR);
  if (!r) return NULL;
  BYTE* bits = (BYTE*)LockResource(LoadResource(inst, r));
  DWORD size = SizeofResource(inst, r);
  int w = cx, h = cy;
  if (flags & LR_DEFAULTSIZE) {
    w = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    h = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
  }
  return CreateIconFromResourceEx(bits, size, icon, 0x00030000, w, h, flags);
}

// Maps the 3D grays of a color table or pixels to the system's colors,
// and the corner pixel's color to the window color.
void MapColors(RGBQUAD* table, int n, UINT flags, int cornerIndex) {
  auto map = [&](RGBQUAD& q, int index) {
    COLORREF c = RGB(q.rgbRed, q.rgbGreen, q.rgbBlue), to = CLR_INVALID;
    if ((flags & LR_LOADTRANSPARENT) && index == cornerIndex) {
      to = GetSysColor((flags & LR_LOADMAP3DCOLORS) ? COLOR_3DFACE : COLOR_WINDOW);
    } else if (flags & LR_LOADMAP3DCOLORS) {
      if (c == RGB(128, 128, 128)) to = GetSysColor(COLOR_3DSHADOW);
      else if (c == RGB(192, 192, 192)) to = GetSysColor(COLOR_3DFACE);
      else if (c == RGB(223, 223, 223)) to = GetSysColor(COLOR_3DLIGHT);
    }
    if (to != CLR_INVALID) {
      q.rgbRed = GetRValue(to);
      q.rgbGreen = GetGValue(to);
      q.rgbBlue = GetBValue(to);
    }
  };
  for (int i = 0; i < n; i++) map(table[i], i);
}

HBITMAP BitmapFromDib(const BYTE* data, DWORD size, int cx, int cy, UINT flags) {
  if (size < sizeof(BITMAPINFOHEADER)) return NULL;
  const BITMAPINFOHEADER* bih = (const BITMAPINFOHEADER*)data;
  int bpp = bih->biBitCount;
  int colors = 0;
  if (bih->biSize >= sizeof(BITMAPINFOHEADER)) {
    colors = bih->biClrUsed ? (int)bih->biClrUsed : (bpp <= 8 ? 1 << bpp : 0);
  } else if (bih->biSize == 12) {
    return NULL;  // OS/2 headers are not read
  }
  size_t colorBytes = (size_t)colors * 4 + (bih->biCompression == BI_BITFIELDS ? 12 : 0);
  if (bih->biSize + colorBytes > size) return NULL;
  std::vector<BYTE> info(bih->biSize + colorBytes);
  memcpy(info.data(), data, info.size());
  const BYTE* bits = data + info.size();
  BITMAPINFO* bi = (BITMAPINFO*)info.data();
  int w = bih->biWidth, h = abs(bih->biHeight);
  if (flags & (LR_LOADMAP3DCOLORS | LR_LOADTRANSPARENT)) {
    if (colors > 0) {
      // The corner is the first pixel of the top row.
      int corner = -1;
      size_t stride = (size_t)((w * bpp + 31) / 32) * 4;
      const BYTE* row = bih->biHeight > 0 ? bits + stride * (h - 1) : bits;
      if (bpp == 8) corner = row[0];
      else if (bpp == 4) corner = row[0] >> 4;
      else if (bpp == 1) corner = row[0] >> 7;
      MapColors(bi->bmiColors, colors, flags, corner);
    }
  }
  int outW = cx > 0 ? cx : w, outH = cy > 0 ? cy : h;
  HBITMAP out;
  HDC dc = CreateCompatibleDC(NULL);
  if (flags & LR_MONOCHROME) {
    out = CreateBitmap(outW, outH, 1, 1, NULL);
  } else if (flags & LR_CREATEDIBSECTION) {
    BITMAPINFO* oi = (BITMAPINFO*)info.data();
    std::vector<BYTE> copy = info;
    BITMAPINFOHEADER* ch = (BITMAPINFOHEADER*)copy.data();
    ch->biWidth = outW;
    ch->biHeight = bih->biHeight < 0 ? -outH : outH;
    ch->biSizeImage = 0;
    if (ch->biCompression == BI_RLE8 || ch->biCompression == BI_RLE4) {
      ch->biCompression = BI_RGB;
    }
    (void)oi;
    void* p;
    out = CreateDIBSection(dc, (BITMAPINFO*)copy.data(), DIB_RGB_COLORS, &p, NULL, 0);
  } else {
    HDC screen = GetDC(NULL);
    out = CreateCompatibleBitmap(screen, outW, outH);
    ReleaseDC(NULL, screen);
  }
  if (!out) {
    DeleteDC(dc);
    return NULL;
  }
  HGDIOBJ old = SelectObject(dc, out);
  SetStretchBltMode(dc, COLORONCOLOR);
  StretchDIBits(dc, 0, 0, outW, outH, 0, 0, w, h, bits, bi, DIB_RGB_COLORS, SRCCOPY);
  SelectObject(dc, old);
  DeleteDC(dc);
  return out;
}

// The system's bitmaps (OBM_*): check marks and boxes; others are blank.
HBITMAP SystemBitmap(int id) {
  if (id == OBM_CHECKBOXES) {
    // Four columns of states by three rows, 13x13 each.
    const int s = 13;
    HDC screen = GetDC(NULL);
    HBITMAP b = CreateCompatibleBitmap(screen, s * 4, s * 3);
    ReleaseDC(NULL, screen);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, b);
    RECT all = {0, 0, s * 4, s * 3};
    FillRect(dc, &all, GetSysColorBrush(COLOR_3DFACE));
    for (int row = 0; row < 3; row++)
      for (int col = 0; col < 4; col++) {
        RECT r = {col * s, row * s, col * s + s, row * s + s};
        DrawFrameControl(dc, &r, DFC_BUTTON,
                         (row == 1 ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK) | ((col & 1) ? DFCS_CHECKED : 0) |
                             ((col & 2) ? DFCS_PUSHED : 0));
      }
    SelectObject(dc, old);
    DeleteDC(dc);
    return b;
  }
  int w = 16, h = 16;
  if (id == OBM_CHECK) w = h = 13;
  HBITMAP b = CreateBitmap(w, h, 1, 1, NULL);
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, b);
  RECT all = {0, 0, w, h};
  FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
  if (id == OBM_CHECK) {
    POINT tick[] = {{2, 6}, {5, 9}, {11, 3}};
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HGDIOBJ op = SelectObject(dc, pen);
    Polyline(dc, tick, 3);
    SelectObject(dc, op);
    DeleteObject(pen);
  }
  SelectObject(dc, old);
  DeleteDC(dc);
  return b;
}

LPCWSTR NameW(LPCSTR name, std::wstring& buf) {
  if (IS_INTRESOURCE(name)) return (LPCWSTR)name;
  buf = Widen(name);
  return buf.c_str();
}

}  // namespace

// ---------------------------------------------------------------------
// Icons and cursors

HICON WINAPI CreateIconFromResourceEx(PBYTE bits, DWORD size, BOOL icon, DWORD, int cx, int cy, UINT flags) {
  if (!bits || size < 4) return NULL;
  POINT hot = {0, 0};
  if (!icon) {
    hot.x = ((WORD*)bits)[0];
    hot.y = ((WORD*)bits)[1];
    bits += 4;
    size -= 4;
  }
  if (size >= 8 && !memcmp(bits, "\x89PNG", 4)) {
    SetLastError(ERROR_NOT_SUPPORTED);  // PNG images are not read yet
    return NULL;
  }
  if (flags & LR_DEFAULTSIZE) {
    if (!cx) cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    if (!cy) cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
  }
  return IconFromDib(bits, size, icon != 0, hot, cx, cy);
}

HICON WINAPI CreateIconFromResource(PBYTE bits, DWORD size, BOOL icon, DWORD ver) {
  return CreateIconFromResourceEx(bits, size, icon, ver, 0, 0, LR_DEFAULTSIZE);
}

int WINAPI LookupIconIdFromDirectoryEx(PBYTE dir, BOOL icon, int cx, int cy, UINT flags) {
  return dir ? Pick(dir, icon != 0, cx, cy, flags) : 0;
}

int WINAPI LookupIconIdFromDirectory(PBYTE dir, BOOL icon) {
  return LookupIconIdFromDirectoryEx(dir, icon, 0, 0, LR_DEFAULTSIZE);
}

HICON WINAPI CreateIconIndirect(PICONINFO ii) {
  if (!ii || !ii->hbmMask) return NULL;
  BITMAP bm;
  bool alpha = false;
  if (ii->hbmColor && GetObjectW(ii->hbmColor, sizeof bm, &bm) && bm.bmBitsPixel == 32 && bm.bmBits) {
    const DWORD* p = (const DWORD*)bm.bmBits;
    for (int i = 0; i < bm.bmWidth * bm.bmHeight && !alpha; i++)
      if (p[i] >> 24) alpha = true;
  }
  return NewIcon(ii->fIcon != 0, POINT{(LONG)ii->xHotspot, (LONG)ii->yHotspot}, CopyBitmap(ii->hbmMask),
                 CopyBitmap(ii->hbmColor), alpha);
}

BOOL WINAPI GetIconInfo(HICON h, PICONINFO ii) {
  IconObj* o = IconOf(h);
  if (!o || !ii) return FALSE;
  ii->fIcon = o->isIcon;
  ii->xHotspot = (DWORD)o->hotspot.x;
  ii->yHotspot = (DWORD)o->hotspot.y;
  ii->hbmMask = CopyBitmap(o->mask);
  ii->hbmColor = CopyBitmap(o->color);
  return TRUE;
}

HICON WINAPI CreateIcon(HINSTANCE, int w, int h, BYTE planes, BYTE bpp, const BYTE* andBits, const BYTE* xorBits) {
  if (w <= 0 || h <= 0) return NULL;
  if (planes * bpp == 1) {
    // One monochrome bitmap: the AND bits above the XOR bits, in word-aligned rows.
    size_t stride = (size_t)((w + 15) / 16) * 2;
    std::vector<BYTE> both(stride * h * 2);
    if (andBits) memcpy(both.data(), andBits, stride * h);
    if (xorBits) memcpy(both.data() + stride * h, xorBits, stride * h);
    HBITMAP mask = CreateBitmap(w, h * 2, 1, 1, both.data());
    return NewIcon(true, POINT{w / 2, h / 2}, mask, NULL, false);
  }
  HBITMAP mask = CreateBitmap(w, h, 1, 1, andBits);
  HBITMAP color = CreateBitmap(w, h, planes, bpp, xorBits);
  return NewIcon(true, POINT{w / 2, h / 2}, mask, color, false);
}

HCURSOR WINAPI CreateCursor(HINSTANCE inst, int hx, int hy, int w, int h, const void* andBits, const void* xorBits) {
  HICON i = CreateIcon(inst, w, h, 1, 1, (const BYTE*)andBits, (const BYTE*)xorBits);
  if (IconObj* o = IconOf(i)) {
    o->isIcon = false;
    o->hotspot = {hx, hy};
  }
  return i;
}

HICON WINAPI CopyIcon(HICON h) {
  IconObj* o = IconOf(h);
  if (!o) return NULL;
  HICON c = NewIcon(o->isIcon, o->hotspot, CopyBitmap(o->mask), CopyBitmap(o->color), o->alpha);
  if (IconObj* n = IconOf(c)) n->systemId = o->systemId;
  return c;
}

BOOL WINAPI DestroyIcon(HICON h) {
  IconObj* o = IconOf(h);
  if (!o) return FALSE;
  if (o->stock || o->shared) return TRUE;
  if (GetCursor() == h) return FALSE;
  FreeGdiHandle((HGDIOBJ)h);
  return TRUE;
}

BOOL WINAPI DestroyCursor(HCURSOR h) { return DestroyIcon(h); }

BOOL WINAPI DrawIconEx(HDC hdc, int x, int y, HICON h, int cx, int cy, UINT step, HBRUSH brush, UINT flags) {
  (void)step;
  IconObj* o = IconOf(h);
  if (!o || !DCOf(hdc)) return FALSE;
  if (!(flags & DI_NORMAL)) flags |= DI_NORMAL;
  if (cx == 0) cx = (flags & DI_DEFAULTSIZE) ? GetSystemMetrics(o->isIcon ? SM_CXICON : SM_CXCURSOR) : o->width;
  if (cy == 0) cy = (flags & DI_DEFAULTSIZE) ? GetSystemMetrics(o->isIcon ? SM_CYICON : SM_CYCURSOR) : o->height;
  if (brush) {
    RECT r = {x, y, x + cx, y + cy};
    FillRect(hdc, &r, brush);
  }
  HDC mem = CreateCompatibleDC(hdc);
  COLORREF oldText = SetTextColor(hdc, RGB(0, 0, 0)), oldBk = SetBkColor(hdc, RGB(255, 255, 255));
  int oldMode = SetStretchBltMode(hdc, COLORONCOLOR);
  HGDIOBJ old = SelectObject(mem, o->mask);
  bool both = (flags & DI_NORMAL) == DI_NORMAL;
  if (o->alpha && o->color && (flags & DI_IMAGE)) {
    SelectObject(mem, o->color);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(hdc, x, y, cx, cy, mem, 0, 0, o->width, o->height, bf);
  } else {
    if (flags & DI_MASK) StretchBlt(hdc, x, y, cx, cy, mem, 0, 0, o->width, o->height, both ? SRCAND : SRCCOPY);
    if (flags & DI_IMAGE) {
      DWORD rop = both ? SRCINVERT : SRCCOPY;
      if (o->color) {
        SelectObject(mem, o->color);
        StretchBlt(hdc, x, y, cx, cy, mem, 0, 0, o->width, o->height, rop);
      } else {
        StretchBlt(hdc, x, y, cx, cy, mem, 0, o->height, o->width, o->height, rop);
      }
    }
  }
  SelectObject(mem, old);
  DeleteDC(mem);
  SetStretchBltMode(hdc, oldMode);
  SetTextColor(hdc, oldText);
  SetBkColor(hdc, oldBk);
  return TRUE;
}

BOOL WINAPI DrawIcon(HDC hdc, int x, int y, HICON h) {
  return DrawIconEx(hdc, x, y, h, 0, 0, 0, NULL, DI_NORMAL | DI_COMPAT | DI_DEFAULTSIZE);
}

HICON WINAPI LoadIconW(HINSTANCE inst, LPCWSTR name) {
  if (!inst) return IS_INTRESOURCE(name) ? SystemIconFor(LOWORD((UINT_PTR)name)) : NULL;
  return (HICON)LoadImageW(inst, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

HICON WINAPI LoadIconA(HINSTANCE inst, LPCSTR name) {
  std::wstring buf;
  return LoadIconW(inst, NameW(name, buf));
}

HCURSOR WINAPI LoadCursorW(HINSTANCE inst, LPCWSTR name) {
  if (!inst) return IS_INTRESOURCE(name) ? SystemCursorFor(LOWORD((UINT_PTR)name)) : NULL;
  return (HCURSOR)LoadImageW(inst, name, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

HCURSOR WINAPI LoadCursorA(HINSTANCE inst, LPCSTR name) {
  std::wstring buf;
  return LoadCursorW(inst, NameW(name, buf));
}

HCURSOR WINAPI LoadCursorFromFileW(LPCWSTR path) {
  return (HCURSOR)LoadImageW(NULL, path, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

HCURSOR WINAPI LoadCursorFromFileA(LPCSTR path) {
  if (!path) return NULL;
  std::wstring w = Widen(path);
  return LoadCursorFromFileW(w.c_str());
}

// ---------------------------------------------------------------------
// Bitmaps and LoadImage

HANDLE WINAPI LoadImageW(HINSTANCE inst, LPCWSTR name, UINT type, int cx, int cy, UINT flags) {
  if (type != IMAGE_BITMAP && type != IMAGE_ICON && type != IMAGE_CURSOR) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (!name) return NULL;
  bool fromFile = (flags & LR_LOADFROMFILE) != 0;
  std::wstring key;
  if ((flags & LR_SHARED) && !fromFile) {
    key = KeyOf(inst, name, type, cx, cy);
    std::lock_guard<std::mutex> g(SystemLock());
    auto it = Shared().find(key);
    if (it != Shared().end()) return it->second;
  }
  HANDLE result = NULL;
  if (type == IMAGE_BITMAP) {
    if (fromFile) {
      std::vector<BYTE> d = ReadFile(name);
      if (d.size() > sizeof(BITMAPFILEHEADER) && d[0] == 'B' && d[1] == 'M')
        result = BitmapFromDib(d.data() + sizeof(BITMAPFILEHEADER), (DWORD)(d.size() - sizeof(BITMAPFILEHEADER)),
                               cx, cy, flags);
    } else if (!inst) {
      if (IS_INTRESOURCE(name)) result = SystemBitmap(LOWORD((UINT_PTR)name));
    } else if (HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_BITMAP)) {
      result = BitmapFromDib((const BYTE*)LockResource(LoadResource(inst, r)), SizeofResource(inst, r), cx, cy,
                             flags);
    }
  } else {
    bool icon = type == IMAGE_ICON;
    if (fromFile) {
      result = IconFromFile(name, icon, cx, cy, flags);
    } else if (!inst) {
      if (IS_INTRESOURCE(name)) {
        int id = LOWORD((UINT_PTR)name);
        result = icon ? SystemIconFor(id) : SystemCursorFor(id);
      }
    } else {
      result = IconFromResource(inst, name, icon, cx, cy, flags);
    }
  }
  if (!result) {
    if (GetLastError() == 0) SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return NULL;
  }
  if (!key.empty()) {
    if (IconObj* o = IconOf((HICON)result)) o->shared = true;
    std::lock_guard<std::mutex> g(SystemLock());
    Shared()[key] = result;
  }
  return result;
}

HANDLE WINAPI LoadImageA(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy, UINT flags) {
  std::wstring buf;
  return LoadImageW(inst, NameW(name, buf), type, cx, cy, flags);
}

HBITMAP WINAPI LoadBitmapW(HINSTANCE inst, LPCWSTR name) { return (HBITMAP)LoadImageW(inst, name, IMAGE_BITMAP, 0, 0, 0); }

HBITMAP WINAPI LoadBitmapA(HINSTANCE inst, LPCSTR name) {
  std::wstring buf;
  return LoadBitmapW(inst, NameW(name, buf));
}

HANDLE WINAPI CopyImage(HANDLE h, UINT type, int cx, int cy, UINT flags) {
  if (type == IMAGE_BITMAP) {
    BITMAP bm;
    if (!GetObjectW(h, sizeof bm, &bm)) return NULL;
    int w = cx ? cx : bm.bmWidth, ht = cy ? cy : bm.bmHeight;
    HBITMAP out;
    if (flags & LR_MONOCHROME || bm.bmBitsPixel == 1) {
      out = CreateBitmap(w, ht, 1, 1, NULL);
    } else if (flags & LR_CREATEDIBSECTION || bm.bmBits) {
      DWORD* bits;
      out = NewDib32(w, ht, &bits);
    } else {
      HDC screen = GetDC(NULL);
      out = CreateCompatibleBitmap(screen, w, ht);
      ReleaseDC(NULL, screen);
    }
    HDC s = CreateCompatibleDC(NULL), d = CreateCompatibleDC(NULL);
    HGDIOBJ o1 = SelectObject(s, h), o2 = SelectObject(d, out);
    SetStretchBltMode(d, COLORONCOLOR);
    StretchBlt(d, 0, 0, w, ht, s, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(s, o1);
    SelectObject(d, o2);
    DeleteDC(s);
    DeleteDC(d);
    if (flags & LR_COPYDELETEORG) DeleteObject(h);
    return out;
  }
  if (type == IMAGE_ICON || type == IMAGE_CURSOR) {
    IconObj* o = IconOf((HICON)h);
    if (!o) return NULL;
    HICON c;
    if (!cx && !cy) {
      c = CopyIcon((HICON)h);
    } else {
      int w = cx ? cx : o->width, ht = cy ? cy : o->height;
      HBITMAP mask = CreateBitmap(w, o->color ? ht : ht * 2, 1, 1, NULL);
      HDC s = CreateCompatibleDC(NULL), d = CreateCompatibleDC(NULL);
      HGDIOBJ o1 = SelectObject(s, o->mask), o2 = SelectObject(d, mask);
      StretchBlt(d, 0, 0, w, o->color ? ht : ht * 2, s, 0, 0, o->width, o->color ? o->height : o->height * 2,
                 SRCCOPY);
      HBITMAP color = NULL;
      if (o->color) {
        DWORD* bits;
        color = NewDib32(w, ht, &bits);
        SelectObject(s, o->color);
        SelectObject(d, color);
        SetStretchBltMode(d, COLORONCOLOR);
        StretchBlt(d, 0, 0, w, ht, s, 0, 0, o->width, o->height, SRCCOPY);
      }
      SelectObject(s, o1);
      SelectObject(d, o2);
      DeleteDC(s);
      DeleteDC(d);
      c = NewIcon(o->isIcon, POINT{o->hotspot.x * w / o->width, o->hotspot.y * ht / o->height}, mask, color,
                  o->alpha);
    }
    if (flags & LR_COPYDELETEORG) DestroyIcon((HICON)h);
    return c;
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return NULL;
}

HBITMAP WINAPI CreateMappedBitmap(HINSTANCE inst, INT_PTR id, UINT, LPCOLORMAP map, int n) {
  HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(id), (LPCWSTR)RT_BITMAP);
  if (!r) return NULL;
  const BYTE* data = (const BYTE*)LockResource(LoadResource(inst, r));
  DWORD size = SizeofResource(inst, r);
  if (size < sizeof(BITMAPINFOHEADER)) return NULL;
  std::vector<BYTE> copy(data, data + size);
  BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)copy.data();
  int bpp = bih->biBitCount;
  int colors = bih->biClrUsed ? (int)bih->biClrUsed : (bpp <= 8 ? 1 << bpp : 0);
  static const COLORMAP kDefault[] = {
      {RGB(0, 0, 0), 0}, {RGB(128, 128, 128), 0}, {RGB(192, 192, 192), 0}, {RGB(255, 255, 255), 0}};
  COLORMAP def[4];
  if (!map) {
    memcpy(def, kDefault, sizeof def);
    def[0].to = GetSysColor(COLOR_BTNTEXT);
    def[1].to = GetSysColor(COLOR_BTNSHADOW);
    def[2].to = GetSysColor(COLOR_BTNFACE);
    def[3].to = GetSysColor(COLOR_BTNHIGHLIGHT);
    map = def;
    n = 4;
  }
  auto remap = [&](BYTE* b, BYTE* g, BYTE* rr) {
    COLORREF c = RGB(*rr, *g, *b);
    for (int i = 0; i < n; i++)
      if (map[i].from == c) {
        *rr = GetRValue(map[i].to);
        *g = GetGValue(map[i].to);
        *b = GetBValue(map[i].to);
        return;
      }
  };
  if (colors > 0) {
    RGBQUAD* q = (RGBQUAD*)(copy.data() + bih->biSize);
    for (int i = 0; i < colors; i++) remap(&q[i].rgbBlue, &q[i].rgbGreen, &q[i].rgbRed);
  } else if (bpp == 24 || bpp == 32) {
    int w = bih->biWidth, h = abs(bih->biHeight);
    size_t stride = (size_t)((w * bpp + 31) / 32) * 4;
    BYTE* bits = copy.data() + bih->biSize + (bih->biCompression == BI_BITFIELDS ? 12 : 0);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        BYTE* p = bits + stride * y + (size_t)x * (bpp / 8);
        if (p + 3 <= copy.data() + copy.size()) remap(p, p + 1, p + 2);
      }
  }
  return BitmapFromDib(copy.data(), (DWORD)copy.size(), 0, 0, 0);
}

// ---------------------------------------------------------------------
// Accelerators

namespace {

struct AccelObj : GdiObject {
  AccelObj() : GdiObject(kObjAccel) {}
  std::vector<ACCEL> entries;
};

AccelObj* AccelOf(HACCEL h) { return GdiAs<AccelObj>((HGDIOBJ)h, kObjAccel); }

HACCEL NewAccel(const std::vector<ACCEL>& e) {
  AccelObj* a = new AccelObj;
  a->entries = e;
  return (HACCEL)NewGdiHandle(a);
}

}  // namespace

HACCEL WINAPI CreateAcceleratorTableW(LPACCEL entries, int n) {
  if (!entries || n <= 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return NewAccel(std::vector<ACCEL>(entries, entries + n));
}

HACCEL WINAPI CreateAcceleratorTableA(LPACCEL entries, int n) {
  // Character keys are bytes of the ANSI code page, which is UTF-8;
  // below 0x80 they are the same characters.
  return CreateAcceleratorTableW(entries, n);
}

BOOL WINAPI DestroyAcceleratorTable(HACCEL h) {
  AccelObj* a = AccelOf(h);
  if (!a) return FALSE;
  if (!a->stock) FreeGdiHandle((HGDIOBJ)h);
  return TRUE;
}

int WINAPI CopyAcceleratorTableW(HACCEL h, LPACCEL out, int n) {
  AccelObj* a = AccelOf(h);
  if (!a) return 0;
  if (!out) return (int)a->entries.size();
  int c = std::min(n, (int)a->entries.size());
  for (int i = 0; i < c; i++) out[i] = a->entries[i];
  return c;
}

int WINAPI CopyAcceleratorTableA(HACCEL h, LPACCEL out, int n) { return CopyAcceleratorTableW(h, out, n); }

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE inst, LPCWSTR name) {
  HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_ACCELERATOR);
  if (!r) return NULL;
  const BYTE* p = (const BYTE*)LockResource(LoadResource(inst, r));
  DWORD size = SizeofResource(inst, r);
  std::vector<ACCEL> e;
  // Each entry: WORD flags, WORD key, WORD command, WORD padding; the
  // last has flag 0x80.
  for (DWORD off = 0; off + 8 <= size; off += 8) {
    WORD flags, key, cmd;
    memcpy(&flags, p + off, 2);
    memcpy(&key, p + off + 2, 2);
    memcpy(&cmd, p + off + 4, 2);
    ACCEL a = {(BYTE)(flags & 0x7F), key, cmd};
    e.push_back(a);
    if (flags & 0x80) break;
  }
  if (e.empty()) return NULL;
  // A module's table is loaded once and kept, as Windows does.
  static std::map<std::wstring, HACCEL>* loaded = new std::map<std::wstring, HACCEL>;
  std::wstring key = KeyOf(inst, name, 0, 0, 0);
  std::lock_guard<std::mutex> g(SystemLock());
  auto it = loaded->find(key);
  if (it != loaded->end() && AccelOf(it->second)) return it->second;
  HACCEL h = NewAccel(e);
  if (AccelObj* a = AccelOf(h)) a->stock = true;
  (*loaded)[key] = h;
  return h;
}

HACCEL WINAPI LoadAcceleratorsA(HINSTANCE inst, LPCSTR name) {
  std::wstring buf;
  return LoadAcceleratorsW(inst, NameW(name, buf));
}

namespace {

int TranslateCore(HWND hwnd, HACCEL h, LPMSG msg, bool unicode) {
  AccelObj* a = AccelOf(h);
  if (!a || !msg || !hwnd) return 0;
  UINT m = msg->message;
  if (m != WM_KEYDOWN && m != WM_SYSKEYDOWN && m != WM_CHAR && m != WM_SYSCHAR) return 0;
  bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
  bool alt = (m == WM_SYSKEYDOWN || m == WM_SYSCHAR) ? (msg->lParam & (1 << 29)) != 0 || GetKeyState(VK_MENU) < 0
                                                      : GetKeyState(VK_MENU) < 0;
  for (const ACCEL& e : a->entries) {
    bool match = false;
    if (e.fVirt & FVIRTKEY) {
      if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && e.key == (WORD)msg->wParam)
        match = ((e.fVirt & FSHIFT) != 0) == shift && ((e.fVirt & FCONTROL) != 0) == ctrl &&
                ((e.fVirt & FALT) != 0) == alt;
    } else if (m == WM_CHAR || m == WM_SYSCHAR) {
      WPARAM ch = msg->wParam;
      if (!unicode) ch &= 0xFF;
      if (e.key == (WORD)ch) match = ((e.fVirt & FALT) != 0) == (m == WM_SYSCHAR);
    }
    if (!match) continue;
    // A command whose menu item is disabled or grayed is eaten, not sent.
    HMENU menu = GetMenu(hwnd);
    if (menu && !(GetWindowLongW(hwnd, GWL_STYLE) & WS_CHILD)) {
      if (GetCapture() == NULL && !IsIconic(hwnd)) {
        SendMessageW(hwnd, WM_INITMENU, (WPARAM)menu, 0);
        UINT state = GetMenuState(menu, e.cmd, MF_BYCOMMAND);
        if (state != (UINT)-1 && (state & (MF_DISABLED | MF_GRAYED))) return 1;
      }
    }
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(e.cmd, 1), 0);
    return 1;
  }
  return 0;
}

}  // namespace

int WINAPI TranslateAcceleratorW(HWND hwnd, HACCEL h, LPMSG msg) { return TranslateCore(hwnd, h, msg, true); }
int WINAPI TranslateAcceleratorA(HWND hwnd, HACCEL h, LPMSG msg) { return TranslateCore(hwnd, h, msg, false); }
