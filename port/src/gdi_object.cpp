// gdi_object.cpp - GDI objects: handles, stock objects, pens, brushes,
// fonts, palettes, bitmaps and DIBs, and regions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <mutex>

using namespace winport;

namespace winport {

// ---------------------------------------------------------------------
// Handles: an index into the table, with a generation count in the high
// bits so that a stale handle does not find the object that reused its
// slot.

namespace {

struct Slot {
  GdiObject* obj = NULL;
  UINT gen = 0;
};
std::recursive_mutex& GdiLock() {
  static auto* m = new std::recursive_mutex;
  return *m;
}
std::vector<Slot>& Table() {
  static auto* t = new std::vector<Slot>(1);  // slot 0 is never used
  return *t;
}
std::vector<UINT>& FreeSlots() {
  static auto* v = new std::vector<UINT>;
  return *v;
}
const UINT kIndexBits = 20;
const UINT_PTR kGdiTag = 0x80000000u;  // keeps GDI handles apart from window handles

UINT_PTR Encode(UINT index, UINT gen) {
  return kGdiTag | ((UINT_PTR)(gen & 0x7FF) << kIndexBits) | index;
}

}  // namespace

HGDIOBJ NewGdiHandle(GdiObject* o) {
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  std::vector<Slot>& t = Table();
  UINT i;
  if (!FreeSlots().empty()) {
    i = FreeSlots().back();
    FreeSlots().pop_back();
  } else {
    i = (UINT)t.size();
    if (i >= (1u << kIndexBits)) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      delete o;
      return NULL;
    }
    t.push_back(Slot());
  }
  t[i].obj = o;
  t[i].gen++;
  return (HGDIOBJ)Encode(i, t[i].gen);
}

GdiObject* GdiObj(HGDIOBJ h, DWORD type) {
  UINT_PTR v = (UINT_PTR)h;
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  UINT i = (UINT)(v & ((1u << kIndexBits) - 1));
  std::vector<Slot>& t = Table();
  if (!(v & kGdiTag) || i == 0 || i >= t.size() || !t[i].obj || Encode(i, t[i].gen) != v) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  GdiObject* o = t[i].obj;
  if (type && o->type != type) {
    // A memory DC is a DC too.
    bool dc = type == OBJ_DC && (o->type == OBJ_MEMDC || o->type == OBJ_METADC || o->type == OBJ_ENHMETADC);
    if (!dc) {
      SetLastError(ERROR_INVALID_HANDLE);
      return NULL;
    }
  }
  return o;
}

HGDIOBJ HandleOf(const GdiObject* o) {
  if (!o) return NULL;
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  std::vector<Slot>& t = Table();
  for (UINT i = 1; i < t.size(); i++)
    if (t[i].obj == o) return (HGDIOBJ)Encode(i, t[i].gen);
  return NULL;
}

void FreeGdiHandle(HGDIOBJ h) {
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  GdiObject* o = GdiObj(h);
  if (!o) return;
  UINT i = (UINT)((UINT_PTR)h & ((1u << kIndexBits) - 1));
  Table()[i].obj = NULL;
  FreeSlots().push_back(i);
  delete o;
}

void Ref(HGDIOBJ h) {
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  if (GdiObject* o = GdiObj(h)) o->refs++;
}

void Unref(HGDIOBJ h) {
  std::lock_guard<std::recursive_mutex> g(GdiLock());
  GdiObject* o = GdiObj(h);
  if (!o) return;
  if (--o->refs <= 0 && o->deletePending) FreeGdiHandle(h);
}

// ---------------------------------------------------------------------
// Stock objects

namespace {

HGDIOBJ g_stock[STOCK_LAST + 1];
HGDIOBJ g_sysBrush[COLOR_MENUBAR + 1];
HBITMAP g_stockBitmap;

const PALETTEENTRY kDefaultPalette[20] = {
    {0, 0, 0, 0},       {128, 0, 0, 0},     {0, 128, 0, 0},     {128, 128, 0, 0},   {0, 0, 128, 0},
    {128, 0, 128, 0},   {0, 128, 128, 0},   {192, 192, 192, 0}, {192, 220, 192, 0}, {166, 202, 240, 0},
    {255, 251, 240, 0}, {160, 160, 164, 0}, {128, 128, 128, 0}, {255, 0, 0, 0},     {0, 255, 0, 0},
    {255, 255, 0, 0},   {0, 0, 255, 0},     {255, 0, 255, 0},   {0, 255, 255, 0},   {255, 255, 255, 0},
};

HGDIOBJ Stock(GdiObject* o) {
  o->stock = true;
  return NewGdiHandle(o);
}

HGDIOBJ StockBrush(UINT style, COLORREF c) {
  BrushObj* b = new BrushObj;
  b->lb.lbStyle = style;
  b->lb.lbColor = c;
  return Stock(b);
}

HGDIOBJ StockPen(UINT style, COLORREF c) {
  PenObj* p = new PenObj;
  p->style = style;
  p->color = c;
  return Stock(p);
}

HGDIOBJ StockFont(const wchar_t* face, LONG height, LONG weight, BYTE pitch, BYTE charset) {
  FontObj* f = new FontObj;
  f->lf.lfHeight = height;
  f->lf.lfWeight = weight;
  f->lf.lfCharSet = charset;
  f->lf.lfPitchAndFamily = pitch;
  wcsncpy(f->lf.lfFaceName, face, LF_FACESIZE - 1);
  return Stock(f);
}

void InitStock() {
  static std::once_flag once;
  std::call_once(once, [] {
    g_stock[WHITE_BRUSH] = StockBrush(BS_SOLID, RGB(255, 255, 255));
    g_stock[LTGRAY_BRUSH] = StockBrush(BS_SOLID, RGB(192, 192, 192));
    g_stock[GRAY_BRUSH] = StockBrush(BS_SOLID, RGB(128, 128, 128));
    g_stock[DKGRAY_BRUSH] = StockBrush(BS_SOLID, RGB(64, 64, 64));
    g_stock[BLACK_BRUSH] = StockBrush(BS_SOLID, RGB(0, 0, 0));
    g_stock[NULL_BRUSH] = StockBrush(BS_NULL, 0);
    g_stock[WHITE_PEN] = StockPen(PS_SOLID, RGB(255, 255, 255));
    g_stock[BLACK_PEN] = StockPen(PS_SOLID, RGB(0, 0, 0));
    g_stock[NULL_PEN] = StockPen(PS_NULL, 0);
    g_stock[OEM_FIXED_FONT] = StockFont(L"Terminal", 12, FW_NORMAL, FIXED_PITCH | FF_MODERN, OEM_CHARSET);
    g_stock[ANSI_FIXED_FONT] = StockFont(L"Courier", 13, FW_NORMAL, FIXED_PITCH | FF_MODERN, ANSI_CHARSET);
    g_stock[ANSI_VAR_FONT] = StockFont(L"MS Sans Serif", 13, FW_NORMAL, VARIABLE_PITCH | FF_SWISS, ANSI_CHARSET);
    g_stock[SYSTEM_FONT] = StockFont(L"System", 16, FW_BOLD, VARIABLE_PITCH | FF_SWISS, ANSI_CHARSET);
    g_stock[DEVICE_DEFAULT_FONT] = StockFont(L"System", 16, FW_BOLD, VARIABLE_PITCH | FF_SWISS, ANSI_CHARSET);
    g_stock[SYSTEM_FIXED_FONT] = StockFont(L"Fixedsys", 15, FW_NORMAL, FIXED_PITCH | FF_MODERN, ANSI_CHARSET);
    g_stock[DEFAULT_GUI_FONT] = StockFont(L"MS Shell Dlg", -11, FW_NORMAL, VARIABLE_PITCH | FF_SWISS, DEFAULT_CHARSET);
    PaletteObj* pal = new PaletteObj;
    pal->entries.assign(kDefaultPalette, kDefaultPalette + 20);
    g_stock[DEFAULT_PALETTE] = Stock(pal);
    // The DC brush and pen take their colors from the DC.
    g_stock[DC_BRUSH] = StockBrush(BS_SOLID, RGB(255, 255, 255));
    g_stock[DC_PEN] = StockPen(PS_SOLID, RGB(0, 0, 0));
    for (int i = 0; i <= COLOR_MENUBAR; i++) {
      BrushObj* b = new BrushObj;
      b->sysColor = i;
      b->lb.lbColor = GetSysColor(i);
      g_sysBrush[i] = Stock(b);
    }
    BitmapObj* bm = new BitmapObj;
    bm->width = bm->height = 1;
    bm->bpp = 1;
    bm->mono = true;
    bm->pixels.assign(1, 0);
    g_stockBitmap = (HBITMAP)Stock(bm);
  });
}

}  // namespace

HBITMAP StockBitmap() {
  InitStock();
  return g_stockBitmap;
}

// ---------------------------------------------------------------------
// DIBs

namespace {

bool LittleEndian() {
  const WORD one = 1;
  return *(const BYTE*)&one == 1;
}

int MaskShift(DWORD m) {
  int s = 0;
  if (!m) return 0;
  while (!(m & 1)) {
    m >>= 1;
    s++;
  }
  return s;
}
int MaskBits(DWORD m) {
  int n = 0;
  for (; m; m >>= 1) n += m & 1;
  return n;
}
// A masked field scaled to 8 bits.
BYTE Field(DWORD v, DWORD mask) {
  if (!mask) return 0;
  int bits = MaskBits(mask);
  DWORD x = (v & mask) >> MaskShift(mask);
  if (bits >= 8) return (BYTE)(x >> (bits - 8));
  // Repeat the high bits into the low ones, so that full scale is 255.
  DWORD r = x << (8 - bits);
  for (int b = bits; b < 8; b += bits) r |= x << (8 - bits) >> b;
  return (BYTE)r;
}
DWORD Unfield(BYTE c, DWORD mask) {
  if (!mask) return 0;
  int bits = MaskBits(mask);
  DWORD x = bits >= 8 ? (DWORD)c << (bits - 8) : (DWORD)c >> (8 - bits);
  return (x << MaskShift(mask)) & mask;
}

DWORD Quad(const RGBQUAD& q) { return ((DWORD)q.rgbRed << 16) | ((DWORD)q.rgbGreen << 8) | q.rgbBlue; }

int Nearest(const std::vector<DWORD>& colors, DWORD p) {
  int best = 0;
  long bestD = 0x7FFFFFFF;
  int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
  for (size_t i = 0; i < colors.size(); i++) {
    DWORD c = colors[i];
    int dr = r - (int)((c >> 16) & 0xFF), dg = g - (int)((c >> 8) & 0xFF), db = b - (int)(c & 0xFF);
    long d = dr * dr + dg * dg + db * db;
    if (d < bestD) {
      bestD = d;
      best = (int)i;
      if (d == 0) break;
    }
  }
  return best;
}

// The standard color tables GetDIBits gives for indexed formats.
std::vector<DWORD> DefaultColors(int bpp) {
  std::vector<DWORD> c;
  if (bpp == 1) {
    c = {0x000000, 0xFFFFFF};
  } else if (bpp == 4) {
    static const DWORD vga[16] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0x808080,
                                  0xC0C0C0, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    c.assign(vga, vga + 16);
  } else if (bpp == 8) {
    // The 20 static colors at the ends and a 6x6x6 cube with grays between.
    for (int i = 0; i < 10; i++) c.push_back(Quad({kDefaultPalette[i].peBlue, kDefaultPalette[i].peGreen,
                                                   kDefaultPalette[i].peRed, 0}));
    for (int r = 0; r < 6; r++)
      for (int g = 0; g < 6; g++)
        for (int b = 0; b < 6; b++) c.push_back(((DWORD)(r * 51) << 16) | ((DWORD)(g * 51) << 8) | (DWORD)(b * 51));
    while (c.size() < 246) {
      DWORD v = (DWORD)((c.size() - 226) * 255 / 20);
      c.push_back((v << 16) | (v << 8) | v);
    }
    for (int i = 10; i < 20; i++) c.push_back(Quad({kDefaultPalette[i].peBlue, kDefaultPalette[i].peGreen,
                                                    kDefaultPalette[i].peRed, 0}));
  }
  return c;
}

}  // namespace

bool ParseDib(const BITMAPINFO* bmi, UINT usage, DibFormat* f) {
  if (!bmi) return false;
  const BITMAPINFOHEADER& h = bmi->bmiHeader;
  if (h.biSize == sizeof(BITMAPCOREHEADER)) {
    const BITMAPCOREHEADER& c = *(const BITMAPCOREHEADER*)bmi;
    f->width = c.bcWidth;
    f->height = c.bcHeight;
    f->topDown = false;
    f->bpp = c.bcBitCount;
    f->compression = BI_RGB;
    const RGBTRIPLE* t = (const RGBTRIPLE*)((const BYTE*)bmi + c.bcSize);
    f->colors.clear();
    if (f->bpp <= 8)
      for (int i = 0; i < (1 << f->bpp); i++)
        f->colors.push_back(((DWORD)t[i].rgbtRed << 16) | ((DWORD)t[i].rgbtGreen << 8) | t[i].rgbtBlue);
  } else if (h.biSize >= sizeof(BITMAPINFOHEADER)) {
    f->width = h.biWidth;
    f->height = h.biHeight < 0 ? -h.biHeight : h.biHeight;
    f->topDown = h.biHeight < 0;
    f->bpp = h.biBitCount;
    f->compression = h.biCompression;
    const BYTE* after = (const BYTE*)bmi + h.biSize;
    if (h.biCompression == BI_BITFIELDS) {
      // In a version 4 or 5 header the masks are part of it.
      const DWORD* m = h.biSize >= 52 ? (const DWORD*)((const BYTE*)bmi + 40) : (const DWORD*)after;
      f->masks[0] = m[0];
      f->masks[1] = m[1];
      f->masks[2] = m[2];
      if (h.biSize < 52) after += 12;
    } else if (f->bpp == 16) {
      f->masks[0] = 0x7C00;
      f->masks[1] = 0x03E0;
      f->masks[2] = 0x001F;
    } else if (f->bpp == 32) {
      f->masks[0] = 0xFF0000;
      f->masks[1] = 0x00FF00;
      f->masks[2] = 0x0000FF;
    }
    f->colors.clear();
    if (f->bpp <= 8) {
      int n = h.biClrUsed ? (int)h.biClrUsed : 1 << f->bpp;
      if (n > 256) n = 256;
      if (usage == DIB_PAL_COLORS) {
        // Indices into the selected palette; the default one stands in.
        const WORD* idx = (const WORD*)after;
        for (int i = 0; i < n; i++) {
          const PALETTEENTRY& e = kDefaultPalette[idx[i] % 20];
          f->colors.push_back(((DWORD)e.peRed << 16) | ((DWORD)e.peGreen << 8) | e.peBlue);
        }
      } else {
        const RGBQUAD* q = (const RGBQUAD*)after;
        for (int i = 0; i < n; i++) f->colors.push_back(Quad(q[i]));
      }
    }
  } else {
    return false;
  }
  if (f->compression != BI_RGB && f->compression != BI_BITFIELDS) return false;
  if (f->bpp != 1 && f->bpp != 4 && f->bpp != 8 && f->bpp != 16 && f->bpp != 24 && f->bpp != 32) return false;
  if (f->bpp <= 8 && f->colors.empty()) f->colors = DefaultColors(f->bpp);
  f->stride = ((f->width * f->bpp + 31) / 32) * 4;
  return f->width > 0 && f->height > 0;
}

void DibRowToPixels(const DibFormat& f, const BYTE* row, DWORD* out, int count) {
  size_t nc = f.colors.size();
  for (int x = 0; x < count; x++) {
    DWORD p;
    switch (f.bpp) {
      case 1: {
        size_t i = (row[x >> 3] >> (7 - (x & 7))) & 1;
        p = i < nc ? f.colors[i] : 0;
        break;
      }
      case 4: {
        size_t i = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
        p = i < nc ? f.colors[i] : 0;
        break;
      }
      case 8: {
        size_t i = row[x];
        p = i < nc ? f.colors[i] : 0;
        break;
      }
      case 16: {
        DWORD v = row[2 * x] | ((DWORD)row[2 * x + 1] << 8);
        p = ((DWORD)Field(v, f.masks[0]) << 16) | ((DWORD)Field(v, f.masks[1]) << 8) | Field(v, f.masks[2]);
        break;
      }
      case 24:
        p = ((DWORD)row[3 * x + 2] << 16) | ((DWORD)row[3 * x + 1] << 8) | row[3 * x];
        break;
      default: {
        const BYTE* b = row + 4 * x;
        DWORD v = b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16) | ((DWORD)b[3] << 24);
        p = ((DWORD)Field(v, f.masks[0]) << 16) | ((DWORD)Field(v, f.masks[1]) << 8) | Field(v, f.masks[2]);
        break;
      }
    }
    out[x] = p;
  }
}

void PixelsToDibRow(const DibFormat& f, const DWORD* in, BYTE* row, int count) {
  DWORD lastPixel = 0xFFFFFFFF;
  int lastIndex = 0;
  for (int x = 0; x < count; x++) {
    DWORD p = in[x] & 0xFFFFFF;
    if (f.bpp <= 8) {
      if (p != lastPixel) {
        lastIndex = Nearest(f.colors, p);
        lastPixel = p;
      }
      int i = lastIndex;
      if (f.bpp == 1) {
        BYTE bit = (BYTE)(0x80 >> (x & 7));
        row[x >> 3] = i ? row[x >> 3] | bit : row[x >> 3] & ~bit;
      } else if (f.bpp == 4) {
        BYTE& b = row[x >> 1];
        b = (x & 1) ? (BYTE)((b & 0xF0) | i) : (BYTE)((b & 0x0F) | (i << 4));
      } else {
        row[x] = (BYTE)i;
      }
      continue;
    }
    BYTE r = (BYTE)(p >> 16), g = (BYTE)(p >> 8), b = (BYTE)p;
    if (f.bpp == 24) {
      row[3 * x] = b;
      row[3 * x + 1] = g;
      row[3 * x + 2] = r;
    } else {
      DWORD v = Unfield(r, f.masks[0]) | Unfield(g, f.masks[1]) | Unfield(b, f.masks[2]);
      if (f.bpp == 16) {
        row[2 * x] = (BYTE)v;
        row[2 * x + 1] = (BYTE)(v >> 8);
      } else {
        BYTE* o = row + 4 * x;
        o[0] = (BYTE)v;
        o[1] = (BYTE)(v >> 8);
        o[2] = (BYTE)(v >> 16);
        o[3] = (BYTE)(v >> 24);
      }
    }
  }
}

// ---------------------------------------------------------------------
// Bitmaps

namespace {

DibFormat FormatOf(const BitmapObj* b) {
  DibFormat f;
  f.width = b->width;
  f.height = b->height;
  f.topDown = b->topDown;
  f.bpp = b->bpp;
  f.compression = b->bih.biCompression;
  memcpy(f.masks, b->masks, sizeof f.masks);
  for (const RGBQUAD& q : b->colors) f.colors.push_back(Quad(q));
  if (f.bpp <= 8 && f.colors.empty()) f.colors = DefaultColors(f.bpp);
  f.stride = b->dibStride;
  return f;
}

}  // namespace

BitmapObj::~BitmapObj() {
  if (ownBits) free(bits);
}

bool BitmapObj::Direct() const {
  return dib && bpp == 32 && LittleEndian() && masks[0] == 0xFF0000 && masks[1] == 0xFF00 && masks[2] == 0xFF;
}

Pixels BitmapObj::Lock() {
  Pixels p;
  p.width = width;
  p.height = height;
  if (Direct()) {
    DWORD* base = (DWORD*)bits;
    p.row0 = topDown ? base : base + (size_t)(height - 1) * width;
    p.stride = topDown ? width : -(ptrdiff_t)width;
    return p;
  }
  if (dib) Unpack();
  p.row0 = pixels.data();
  p.stride = width;
  return p;
}

void BitmapObj::Unlock() {
  if (dib && !Direct()) Pack();
}

void BitmapObj::Unpack() {
  DibFormat f = FormatOf(this);
  pixels.resize((size_t)width * height);
  for (int y = 0; y < height; y++) DibRowToPixels(f, bits + (size_t)f.MemRow(y) * dibStride, &pixels[(size_t)y * width], width);
}

void BitmapObj::Pack() {
  DibFormat f = FormatOf(this);
  for (int y = 0; y < height; y++) PixelsToDibRow(f, &pixels[(size_t)y * width], bits + (size_t)f.MemRow(y) * dibStride, width);
}

}  // namespace winport

namespace {

BitmapObj* NewDDB(int w, int h, bool mono) {
  BitmapObj* b = new BitmapObj;
  b->width = w;
  b->height = h;
  b->bpp = mono ? 1 : 32;
  b->mono = mono;
  b->pixels.assign((size_t)w * h, mono ? 0 : 0);
  return b;
}

// Reads device-dependent bitmap bits: rows padded to 16 bits.
void ReadDDBBits(BitmapObj* b, const void* data, int bpp) {
  const BYTE* src = (const BYTE*)data;
  int stride = ((b->width * bpp + 15) / 16) * 2;
  DibFormat f;
  f.width = b->width;
  f.bpp = bpp;
  if (bpp == 16) {
    f.masks[0] = 0xF800;
    f.masks[1] = 0x07E0;
    f.masks[2] = 0x001F;
  } else if (bpp == 32) {
    f.masks[0] = 0xFF0000;
    f.masks[1] = 0xFF00;
    f.masks[2] = 0xFF;
  }
  if (bpp <= 8) f.colors = DefaultColors(bpp);
  for (int y = 0; y < b->height; y++) DibRowToPixels(f, src + (size_t)y * stride, &b->pixels[(size_t)y * b->width], b->width);
}

LONG WriteDDBBits(BitmapObj* b, LONG count, void* data) {
  int bpp = b->mono ? 1 : 32;
  int stride = ((b->width * bpp + 15) / 16) * 2;
  LONG total = stride * b->height;
  if (!data) return total;
  if (count > total) count = total;
  std::vector<BYTE> buf(total);
  DibFormat f;
  f.width = b->width;
  f.bpp = bpp;
  f.masks[0] = 0xFF0000;
  f.masks[1] = 0xFF00;
  f.masks[2] = 0xFF;
  if (bpp == 1) f.colors = DefaultColors(1);
  Pixels px = b->Lock();
  for (int y = 0; y < b->height; y++) PixelsToDibRow(f, px.Row(y), &buf[(size_t)y * stride], b->width);
  memcpy(data, buf.data(), count);
  return count;
}

// A monochrome bitmap holds only black and white.
DWORD MonoOf(DWORD p) { return (p & 0xFFFFFF) == 0xFFFFFF ? 0xFFFFFF : 0; }

}  // namespace

extern "C" {

// ---------------------------------------------------------------------
// Stock objects

HGDIOBJ WINAPI GetStockObject(int i) {
  InitStock();
  if (i < 0 || i > STOCK_LAST) return NULL;
  return g_stock[i];
}

HBRUSH WINAPI GetSysColorBrush(int i) {
  InitStock();
  if (i < 0 || i > COLOR_MENUBAR) return NULL;
  return (HBRUSH)g_sysBrush[i];
}

// ---------------------------------------------------------------------
// Pens

HPEN WINAPI CreatePen(int style, int width, COLORREF color) {
  PenObj* p = new PenObj;
  p->style = (UINT)style;
  p->width = width < 0 ? 0 : width;
  p->color = color;
  if ((style & PS_STYLE_MASK) > PS_INSIDEFRAME) p->style = PS_SOLID;
  return (HPEN)NewGdiHandle(p);
}

HPEN WINAPI CreatePenIndirect(const LOGPEN* lp) {
  if (!lp) return NULL;
  return CreatePen((int)lp->lopnStyle, lp->lopnWidth.x, lp->lopnColor);
}

HPEN WINAPI ExtCreatePen(DWORD style, DWORD width, const LOGBRUSH* lb, DWORD, const DWORD*) {
  if (!lb) return NULL;
  PenObj* p = new PenObj;
  p->style = style;
  p->width = (int)width;
  p->color = lb->lbStyle == BS_NULL ? 0 : lb->lbColor;
  if (lb->lbStyle == BS_NULL) p->style = (style & ~PS_STYLE_MASK) | PS_NULL;
  p->extended = true;
  return (HPEN)NewGdiHandle(p);
}

// ---------------------------------------------------------------------
// Brushes

HBRUSH WINAPI CreateSolidBrush(COLORREF c) {
  BrushObj* b = new BrushObj;
  b->lb.lbColor = c;
  return (HBRUSH)NewGdiHandle(b);
}

HBRUSH WINAPI CreateHatchBrush(int hatch, COLORREF c) {
  if (hatch < HS_HORIZONTAL || hatch > HS_DIAGCROSS) return CreateSolidBrush(c);
  BrushObj* b = new BrushObj;
  b->lb.lbStyle = BS_HATCHED;
  b->lb.lbColor = c;
  b->lb.lbHatch = (ULONG_PTR)hatch;
  return (HBRUSH)NewGdiHandle(b);
}

HBRUSH WINAPI CreatePatternBrush(HBITMAP hbm) {
  BitmapObj* bm = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!bm) return NULL;
  BrushObj* b = new BrushObj;
  b->lb.lbStyle = BS_PATTERN;
  b->lb.lbHatch = (ULONG_PTR)hbm;
  Pixels px = bm->Lock();
  b->patWidth = px.width;
  b->patHeight = px.height;
  for (int y = 0; y < px.height; y++) b->pattern.insert(b->pattern.end(), px.Row(y), px.Row(y) + px.width);
  bm->Unlock();
  b->monoPattern = bm->mono;
  return (HBRUSH)NewGdiHandle(b);
}

HBRUSH WINAPI CreateDIBPatternBrushPt(const void* packed, UINT usage) {
  const BITMAPINFO* bmi = (const BITMAPINFO*)packed;
  DibFormat f;
  if (!ParseDib(bmi, usage, &f)) return NULL;
  const BYTE* bits = (const BYTE*)bmi + bmi->bmiHeader.biSize;
  if (f.bpp <= 8) bits += (f.colors.size()) * (usage == DIB_PAL_COLORS ? sizeof(WORD) : sizeof(RGBQUAD));
  if (bmi->bmiHeader.biCompression == BI_BITFIELDS && bmi->bmiHeader.biSize < 52) bits += 12;
  BrushObj* b = new BrushObj;
  b->lb.lbStyle = BS_DIBPATTERNPT;
  b->patWidth = f.width;
  b->patHeight = f.height;
  b->pattern.resize((size_t)f.width * f.height);
  for (int y = 0; y < f.height; y++)
    DibRowToPixels(f, bits + (size_t)f.MemRow(y) * f.stride, &b->pattern[(size_t)y * f.width], f.width);
  return (HBRUSH)NewGdiHandle(b);
}

HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL h, UINT usage) {
  void* p = GlobalLock(h);
  HBRUSH b = p ? CreateDIBPatternBrushPt(p, usage) : NULL;
  if (p) GlobalUnlock(h);
  return b;
}

HBRUSH WINAPI CreateBrushIndirect(const LOGBRUSH* lb) {
  if (!lb) return NULL;
  switch (lb->lbStyle) {
    case BS_SOLID: return CreateSolidBrush(lb->lbColor);
    case BS_HATCHED: return CreateHatchBrush((int)lb->lbHatch, lb->lbColor);
    case BS_PATTERN:
    case BS_PATTERN8X8: return CreatePatternBrush((HBITMAP)lb->lbHatch);
    case BS_DIBPATTERN:
    case BS_DIBPATTERN8X8: return CreateDIBPatternBrush((HGLOBAL)lb->lbHatch, LOWORD(lb->lbColor));
    case BS_DIBPATTERNPT: return CreateDIBPatternBrushPt((const void*)lb->lbHatch, LOWORD(lb->lbColor));
    default: {
      BrushObj* b = new BrushObj;
      b->lb.lbStyle = BS_NULL;
      return (HBRUSH)NewGdiHandle(b);
    }
  }
}

// ---------------------------------------------------------------------
// Fonts. The rasterizer picks a face when the font is drawn with.

HFONT WINAPI CreateFontIndirectW(const LOGFONTW* lf) {
  if (!lf) return NULL;
  FontObj* f = new FontObj;
  f->lf = *lf;
  f->lf.lfFaceName[LF_FACESIZE - 1] = 0;
  return (HFONT)NewGdiHandle(f);
}

HFONT WINAPI CreateFontIndirectA(const LOGFONTA* lf) {
  if (!lf) return NULL;
  LOGFONTW w;
  memcpy(&w, lf, offsetof(LOGFONTA, lfFaceName));
  char face[LF_FACESIZE + 1] = {};
  memcpy(face, lf->lfFaceName, LF_FACESIZE);
  std::wstring wf = Widen(face);
  memset(w.lfFaceName, 0, sizeof w.lfFaceName);
  wcsncpy(w.lfFaceName, wf.c_str(), LF_FACESIZE - 1);
  return CreateFontIndirectW(&w);
}

HFONT WINAPI CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                         DWORD underline, DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision,
                         DWORD quality, DWORD pitchAndFamily, LPCWSTR face) {
  LOGFONTW lf = {height, width, escapement, orientation, weight, (BYTE)italic, (BYTE)underline, (BYTE)strikeOut,
                 (BYTE)charSet, (BYTE)outPrecision, (BYTE)clipPrecision, (BYTE)quality, (BYTE)pitchAndFamily, {0}};
  if (face) wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
  return CreateFontIndirectW(&lf);
}

HFONT WINAPI CreateFontA(int height, int width, int escapement, int orientation, int weight, DWORD italic,
                         DWORD underline, DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision,
                         DWORD quality, DWORD pitchAndFamily, LPCSTR face) {
  std::wstring f = face ? Widen(face) : std::wstring();
  return CreateFontW(height, width, escapement, orientation, weight, italic, underline, strikeOut, charSet,
                     outPrecision, clipPrecision, quality, pitchAndFamily, face ? f.c_str() : NULL);
}

// ---------------------------------------------------------------------
// Palettes. Every display is true color, so palettes only hold colors.

HPALETTE WINAPI CreatePalette(const LOGPALETTE* lp) {
  if (!lp) return NULL;
  PaletteObj* p = new PaletteObj;
  p->entries.assign(lp->palPalEntry, lp->palPalEntry + lp->palNumEntries);
  return (HPALETTE)NewGdiHandle(p);
}

HPALETTE WINAPI CreateHalftonePalette(HDC) {
  PaletteObj* p = new PaletteObj;
  for (DWORD c : DefaultColors(8)) p->entries.push_back({(BYTE)(c >> 16), (BYTE)(c >> 8), (BYTE)c, 0});
  return (HPALETTE)NewGdiHandle(p);
}

UINT WINAPI GetPaletteEntries(HPALETTE h, UINT start, UINT n, LPPALETTEENTRY out) {
  PaletteObj* p = GdiAs<PaletteObj>(h, OBJ_PAL);
  if (!p) return 0;
  if (!out) return (UINT)p->entries.size();
  UINT i = 0;
  for (; i < n && start + i < p->entries.size(); i++) out[i] = p->entries[start + i];
  return i;
}

UINT WINAPI SetPaletteEntries(HPALETTE h, UINT start, UINT n, const PALETTEENTRY* in) {
  PaletteObj* p = GdiAs<PaletteObj>(h, OBJ_PAL);
  if (!p || !in) return 0;
  UINT i = 0;
  for (; i < n && start + i < p->entries.size(); i++) p->entries[start + i] = in[i];
  return i;
}

BOOL WINAPI ResizePalette(HPALETTE h, UINT n) {
  PaletteObj* p = GdiAs<PaletteObj>(h, OBJ_PAL);
  if (!p) return FALSE;
  p->entries.resize(n);
  return TRUE;
}

BOOL WINAPI AnimatePalette(HPALETTE h, UINT start, UINT n, const PALETTEENTRY* in) {
  return SetPaletteEntries(h, start, n, in) != 0;
}

UINT WINAPI GetNearestPaletteIndex(HPALETTE h, COLORREF c) {
  PaletteObj* p = GdiAs<PaletteObj>(h, OBJ_PAL);
  if (!p || p->entries.empty()) return CLR_INVALID;
  std::vector<DWORD> colors;
  for (const PALETTEENTRY& e : p->entries) colors.push_back(((DWORD)e.peRed << 16) | ((DWORD)e.peGreen << 8) | e.peBlue);
  return (UINT)Nearest(colors, ToPixel(c));
}

UINT WINAPI GetSystemPaletteEntries(HDC, UINT start, UINT n, LPPALETTEENTRY out) {
  std::vector<DWORD> c = DefaultColors(8);
  if (!out) return 256;
  UINT i = 0;
  for (; i < n && start + i < c.size(); i++) out[i] = {(BYTE)(c[start + i] >> 16), (BYTE)(c[start + i] >> 8), (BYTE)c[start + i], 0};
  return i;
}

UINT WINAPI GetSystemPaletteUse(HDC) { return SYSPAL_STATIC; }

// ---------------------------------------------------------------------
// Bitmaps

HBITMAP WINAPI CreateBitmap(int w, int h, UINT planes, UINT bpp, const void* data) {
  if (w < 0 || h < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (w == 0 || h == 0) return StockBitmap();
  bool mono = planes * bpp == 1;
  BitmapObj* b = NewDDB(w, h, mono);
  if (data) {
    ReadDDBBits(b, data, (int)(planes * bpp));
    if (mono)
      for (DWORD& p : b->pixels) p = MonoOf(p);
  }
  return (HBITMAP)NewGdiHandle(b);
}

HBITMAP WINAPI CreateBitmapIndirect(const BITMAP* bm) {
  if (!bm) return NULL;
  return CreateBitmap(bm->bmWidth, bm->bmHeight, bm->bmPlanes, bm->bmBitsPixel, bm->bmBits);
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int w, int h) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return NULL;
  if (w == 0 || h == 0) return StockBitmap();
  bool mono = false;
  if (dc->kind == kMemoryDC) {
    BitmapObj* sel = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP);
    mono = sel && sel->mono;
  }
  return (HBITMAP)NewGdiHandle(NewDDB(w, h, mono));
}

HBITMAP WINAPI CreateDiscardableBitmap(HDC hdc, int w, int h) { return CreateCompatibleBitmap(hdc, w, h); }

HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO* bmi, UINT usage, void** bitsOut, HANDLE section,
                                DWORD offset) {
  DibFormat f;
  if (bitsOut) *bitsOut = NULL;
  if (!ParseDib(bmi, usage, &f)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  BitmapObj* b = new BitmapObj;
  b->dib = true;
  b->width = f.width;
  b->height = f.height;
  b->bpp = f.bpp;
  b->topDown = f.topDown;
  b->dibStride = f.stride;
  b->bih = bmi->bmiHeader;
  if (b->bih.biSize != sizeof(BITMAPINFOHEADER)) {
    b->bih.biSize = sizeof(BITMAPINFOHEADER);
    b->bih.biWidth = f.width;
    b->bih.biHeight = f.topDown ? -f.height : f.height;
    b->bih.biPlanes = 1;
    b->bih.biBitCount = (WORD)f.bpp;
  }
  b->bih.biSizeImage = (DWORD)f.stride * f.height;
  memcpy(b->masks, f.masks, sizeof b->masks);
  for (DWORD c : f.colors) b->colors.push_back({(BYTE)c, (BYTE)(c >> 8), (BYTE)(c >> 16), 0});
  // No file mappings here: a section handle is noted, the memory is ours.
  b->section = section;
  b->sectionOffset = offset;
  b->bits = (BYTE*)calloc(1, (size_t)f.stride * f.height);
  if (!b->bits) {
    delete b;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  b->ownBits = true;
  if (bitsOut) *bitsOut = b->bits;
  return (HBITMAP)NewGdiHandle(b);
}

int WINAPI SetDIBits(HDC, HBITMAP hbm, UINT start, UINT lines, const void* bits, const BITMAPINFO* bmi, UINT usage) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  DibFormat f;
  if (!b || !bits || !ParseDib(bmi, usage, &f)) return 0;
  Pixels px = b->Lock();
  std::vector<DWORD> row(f.width);
  int done = 0;
  // Scan line start counts from the bottom of the DIB.
  for (UINT i = 0; i < lines; i++) {
    int dibRow = (int)(start + i);  // in memory order
    if (dibRow >= f.height) break;
    int y = f.topDown ? dibRow : f.height - 1 - dibRow;
    if (y >= px.height) continue;
    DibRowToPixels(f, (const BYTE*)bits + (size_t)i * f.stride, row.data(), f.width);
    int n = f.width < px.width ? f.width : px.width;
    DWORD* out = px.Row(y);
    for (int x = 0; x < n; x++) out[x] = b->mono ? MonoOf(row[x]) : row[x];
    done++;
  }
  b->Unlock();
  return done;
}

int WINAPI GetDIBits(HDC, HBITMAP hbm, UINT start, UINT lines, LPVOID bits, LPBITMAPINFO bmi, UINT usage) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!b || !bmi) return 0;
  BITMAPINFOHEADER& h = bmi->bmiHeader;
  if (h.biSize < sizeof(BITMAPINFOHEADER) && h.biSize != sizeof(BITMAPCOREHEADER)) return 0;
  if (h.biBitCount == 0 || !bits) {
    if (h.biBitCount == 0) {
      // Describe the bitmap.
      h.biWidth = b->width;
      h.biHeight = b->dib && b->topDown ? -b->height : b->height;
      h.biPlanes = 1;
      h.biBitCount = (WORD)(b->dib ? b->bpp : b->mono ? 1 : 32);
      h.biCompression = b->dib ? b->bih.biCompression : BI_RGB;
      h.biSizeImage = (DWORD)(((b->width * h.biBitCount + 31) / 32) * 4) * b->height;
      h.biXPelsPerMeter = h.biYPelsPerMeter = 0;
      h.biClrUsed = h.biClrImportant = 0;
      if (h.biCompression == BI_BITFIELDS) memcpy(bmi->bmiColors, b->masks, sizeof b->masks);
      return b->height;
    }
    // Fill in the size and the color table.
    if (h.biWidth == 0) h.biWidth = b->width;
    if (h.biHeight == 0) h.biHeight = b->height;
    int bpp = h.biBitCount;
    h.biSizeImage = (DWORD)(((h.biWidth * bpp + 31) / 32) * 4) * (h.biHeight < 0 ? -h.biHeight : h.biHeight);
    if (bpp <= 8 && usage == DIB_RGB_COLORS) {
      std::vector<DWORD> c = b->dib && b->bpp == bpp && !b->colors.empty() ? std::vector<DWORD>() : DefaultColors(bpp);
      if (c.empty())
        for (const RGBQUAD& q : b->colors) c.push_back(Quad(q));
      for (size_t i = 0; i < c.size(); i++) bmi->bmiColors[i] = {(BYTE)c[i], (BYTE)(c[i] >> 8), (BYTE)(c[i] >> 16), 0};
    }
    return b->height;
  }
  if (h.biBitCount <= 8 && usage == DIB_RGB_COLORS) {
    // The color table: a DIB section's own, or a standard one.
    std::vector<DWORD> c;
    if (b->dib && b->bpp == h.biBitCount && !b->colors.empty())
      for (const RGBQUAD& q : b->colors) c.push_back(Quad(q));
    else
      c = DefaultColors(h.biBitCount);
    for (size_t i = 0; i < c.size(); i++) bmi->bmiColors[i] = {(BYTE)c[i], (BYTE)(c[i] >> 8), (BYTE)(c[i] >> 16), 0};
    h.biClrUsed = 0;
  }
  DibFormat f;
  if (!ParseDib(bmi, usage, &f)) return 0;
  Pixels px = b->Lock();
  std::vector<DWORD> row(f.width, 0);
  int done = 0;
  for (UINT i = 0; i < lines; i++) {
    int dibRow = (int)(start + i);
    if (dibRow >= f.height) break;
    int y = f.topDown ? dibRow : f.height - 1 - dibRow;
    if (y < px.height) {
      int n = f.width < px.width ? f.width : px.width;
      memcpy(row.data(), px.Row(y), n * sizeof(DWORD));
    }
    PixelsToDibRow(f, row.data(), (BYTE*)bits + (size_t)i * f.stride, f.width);
    done++;
  }
  b->Unlock();
  return done;
}

HBITMAP WINAPI CreateDIBitmap(HDC hdc, const BITMAPINFOHEADER* bih, DWORD init, const void* bits,
                              const BITMAPINFO* bmi, UINT usage) {
  if (!bih) return NULL;
  int w, h;
  if (bih->biSize == sizeof(BITMAPCOREHEADER)) {
    w = ((const BITMAPCOREHEADER*)bih)->bcWidth;
    h = ((const BITMAPCOREHEADER*)bih)->bcHeight;
  } else {
    w = bih->biWidth;
    h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
  }
  bool mono = false;
  if (DCObj* dc = hdc ? DCOf(hdc) : NULL) {
    if (dc->kind == kMemoryDC) {
      BitmapObj* sel = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP);
      mono = sel && sel->mono;
    }
  }
  HBITMAP hbm = (HBITMAP)NewGdiHandle(NewDDB(w > 0 ? w : 1, h > 0 ? h : 1, mono));
  if ((init & CBM_INIT) && bits && bmi) SetDIBits(hdc, hbm, 0, h, bits, bmi, usage);
  return hbm;
}

LONG WINAPI GetBitmapBits(HBITMAP hbm, LONG count, LPVOID data) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!b) return 0;
  if (b->dib) {
    LONG total = b->dibStride * b->height;
    if (!data) return total;
    if (count > total) count = total;
    memcpy(data, b->bits, count);
    return count;
  }
  LONG n = WriteDDBBits(b, count, data);
  return n;
}

LONG WINAPI SetBitmapBits(HBITMAP hbm, DWORD count, const void* data) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!b || !data) return 0;
  if (b->dib) {
    DWORD total = (DWORD)b->dibStride * b->height;
    if (count > total) count = total;
    memcpy(b->bits, data, count);
    return (LONG)count;
  }
  int bpp = b->mono ? 1 : 32;
  DWORD total = (DWORD)(((b->width * bpp + 15) / 16) * 2) * b->height;
  std::vector<BYTE> buf(total, 0);
  memcpy(buf.data(), data, count < total ? count : total);
  ReadDDBBits(b, buf.data(), bpp);
  if (b->mono)
    for (DWORD& p : b->pixels) p = MonoOf(p);
  return (LONG)(count < total ? count : total);
}

UINT WINAPI GetDIBColorTable(HDC hdc, UINT start, UINT n, RGBQUAD* out) {
  DCObj* dc = DCOf(hdc);
  BitmapObj* b = dc ? GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP) : NULL;
  if (!b || !b->dib || !out) return 0;
  UINT i = 0;
  for (; i < n && start + i < b->colors.size(); i++) out[i] = b->colors[start + i];
  return i;
}

UINT WINAPI SetDIBColorTable(HDC hdc, UINT start, UINT n, const RGBQUAD* in) {
  DCObj* dc = DCOf(hdc);
  BitmapObj* b = dc ? GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP) : NULL;
  if (!b || !b->dib || !in) return 0;
  UINT i = 0;
  for (; i < n && start + i < b->colors.size(); i++) b->colors[start + i] = in[i];
  return i;
}

BOOL WINAPI GetBitmapDimensionEx(HBITMAP hbm, LPSIZE s) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!b || !s) return FALSE;
  *s = b->dimension;
  return TRUE;
}

BOOL WINAPI SetBitmapDimensionEx(HBITMAP hbm, int x, int y, LPSIZE old) {
  BitmapObj* b = GdiAs<BitmapObj>(hbm, OBJ_BITMAP);
  if (!b) return FALSE;
  if (old) *old = b->dimension;
  b->dimension.cx = x;
  b->dimension.cy = y;
  return TRUE;
}

// Drawing finishes before each call returns.
BOOL WINAPI GdiFlush(void) { return TRUE; }
DWORD WINAPI GdiSetBatchLimit(DWORD) { return 1; }

// ---------------------------------------------------------------------
// Regions

HRGN WINAPI CreateRectRgn(int l, int t, int r, int b) {
  RegionObj* o = new RegionObj;
  RECT rc = {l < r ? l : r, t < b ? t : b, l < r ? r : l, t < b ? b : t};
  o->rgn.Set(rc);
  return (HRGN)NewGdiHandle(o);
}

HRGN WINAPI CreateRectRgnIndirect(const RECT* r) {
  if (!r) return NULL;
  return CreateRectRgn(r->left, r->top, r->right, r->bottom);
}

// An ellipse or a rounded rectangle, a row at a time: (rw, rh) is the
// size of the corner ellipse.
static HRGN RoundRegion(int l, int t, int r, int b, int rw, int rh) {
  if (l > r) std::swap(l, r);
  if (t > b) std::swap(t, b);
  if (rw > r - l) rw = r - l;
  if (rh > b - t) rh = b - t;
  RegionObj* o = new RegionObj;
  double a = rw / 2.0, c = rh / 2.0;
  for (int y = t; y < b; y++) {
    double inset = 0;
    double dy = 0;
    if (c > 0) {
      if (y < t + c) dy = (t + c) - (y + 0.5);
      else if (y >= b - c) dy = (y + 0.5) - (b - c);
      if (dy > 0) {
        double k = dy / c;
        inset = a - a * sqrt(k < 1 ? 1 - k * k : 0);
      }
    }
    int x0 = l + (int)floor(inset + 0.5), x1 = r - (int)floor(inset + 0.5);
    o->rgn.AppendDisjoint(MakeRect(x0, y, x1, y + 1));
  }
  o->rgn.Normalize();
  return (HRGN)NewGdiHandle(o);
}

HRGN WINAPI CreateEllipticRgn(int l, int t, int r, int b) { return RoundRegion(l, t, r, b, abs(r - l), abs(b - t)); }

HRGN WINAPI CreateEllipticRgnIndirect(const RECT* r) {
  if (!r) return NULL;
  return CreateEllipticRgn(r->left, r->top, r->right, r->bottom);
}

HRGN WINAPI CreateRoundRectRgn(int l, int t, int r, int b, int w, int h) { return RoundRegion(l, t, r, b, w, h); }

HRGN WINAPI CreatePolyPolygonRgn(const POINT* pts, const INT* counts, int polys, int mode) {
  if (!pts || !counts || polys <= 0) return NULL;
  // Edges of every polygon, closed.
  struct Edge {
    double x0, y0, x1, y1;
    int dir;
  };
  std::vector<Edge> edges;
  int top = 0x7FFFFFFF, bottom = -0x7FFFFFFF;
  const POINT* p = pts;
  for (int i = 0; i < polys; i++) {
    int n = counts[i];
    for (int j = 0; j < n; j++) {
      POINT a = p[j], b = p[(j + 1) % n];
      if (a.y < top) top = a.y;
      if (a.y > bottom) bottom = a.y;
      if (a.y == b.y) continue;
      if (a.y < b.y) edges.push_back({(double)a.x, (double)a.y, (double)b.x, (double)b.y, 1});
      else edges.push_back({(double)b.x, (double)b.y, (double)a.x, (double)a.y, -1});
    }
    p += n;
  }
  RegionObj* o = new RegionObj;
  std::vector<std::pair<double, int>> xs;
  for (int y = top; y < bottom; y++) {
    double cy = y + 0.5;
    xs.clear();
    for (const Edge& e : edges)
      if (cy >= e.y0 && cy < e.y1) xs.push_back({e.x0 + (cy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir});
    std::sort(xs.begin(), xs.end());
    int wind = 0;
    for (size_t k = 0; k + 1 < xs.size(); k++) {
      wind += mode == WINDING ? xs[k].second : 1;
      bool inside = mode == WINDING ? wind != 0 : (wind & 1) != 0;
      if (!inside) continue;
      int x0 = (int)ceil(xs[k].first - 0.5), x1 = (int)ceil(xs[k + 1].first - 0.5);
      if (x1 > x0) o->rgn.Union(MakeRect(x0, y, x1, y + 1));
    }
  }
  o->rgn.Normalize();
  return (HRGN)NewGdiHandle(o);
}

HRGN WINAPI CreatePolygonRgn(const POINT* pts, int n, int mode) { return CreatePolyPolygonRgn(pts, &n, 1, mode); }

HRGN WINAPI ExtCreateRegion(const XFORM*, DWORD, const RGNDATA* data) {
  if (!data) return NULL;
  RegionObj* o = new RegionObj;
  const RECT* r = (const RECT*)data->Buffer;
  for (DWORD i = 0; i < data->rdh.nCount; i++) o->rgn.Union(r[i]);
  return (HRGN)NewGdiHandle(o);
}

int WINAPI CombineRgn(HRGN dst, HRGN a, HRGN b, int mode) {
  RegionObj* d = GdiAs<RegionObj>(dst, OBJ_REGION);
  RegionObj* ra = GdiAs<RegionObj>(a, OBJ_REGION);
  RegionObj* rb = mode == RGN_COPY ? NULL : GdiAs<RegionObj>(b, OBJ_REGION);
  if (!d || !ra || (mode != RGN_COPY && !rb)) return ERROR;
  Region r = ra->rgn;
  switch (mode) {
    case RGN_AND: r.Intersect(rb->rgn); break;
    case RGN_OR: r.Union(rb->rgn); break;
    case RGN_XOR: r.Xor(rb->rgn); break;
    case RGN_DIFF: r.Subtract(rb->rgn); break;
    case RGN_COPY: break;
    default: return ERROR;
  }
  d->rgn = r;
  return d->rgn.Complexity();
}

BOOL WINAPI EqualRgn(HRGN a, HRGN b) {
  RegionObj* ra = GdiAs<RegionObj>(a, OBJ_REGION);
  RegionObj* rb = GdiAs<RegionObj>(b, OBJ_REGION);
  if (!ra || !rb) return ERROR;
  return ra->rgn.Equals(rb->rgn);
}

int WINAPI OffsetRgn(HRGN h, int dx, int dy) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r) return ERROR;
  r->rgn.Offset(dx, dy);
  return r->rgn.Complexity();
}

BOOL WINAPI SetRectRgn(HRGN h, int l, int t, int r, int b) {
  RegionObj* o = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!o) return FALSE;
  o->rgn.Set(MakeRect(l < r ? l : r, t < b ? t : b, l < r ? r : l, t < b ? b : t));
  return TRUE;
}

int WINAPI GetRgnBox(HRGN h, LPRECT out) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r || !out) return ERROR;
  *out = r->rgn.Bounds();
  return r->rgn.Complexity();
}

BOOL WINAPI PtInRegion(HRGN h, int x, int y) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  return r && r->rgn.Contains(x, y);
}

BOOL WINAPI RectInRegion(HRGN h, const RECT* rc) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r || !rc) return FALSE;
  RECT n = {rc->left < rc->right ? rc->left : rc->right, rc->top < rc->bottom ? rc->top : rc->bottom,
            rc->left < rc->right ? rc->right : rc->left, rc->top < rc->bottom ? rc->bottom : rc->top};
  return r->rgn.Touches(n);
}

DWORD WINAPI GetRegionData(HRGN h, DWORD size, LPRGNDATA out) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r) return 0;
  const std::vector<RECT>& rects = r->rgn.Rects();
  DWORD need = (DWORD)(sizeof(RGNDATAHEADER) + rects.size() * sizeof(RECT));
  if (!out) return need;
  if (size < need) return 0;
  out->rdh.dwSize = sizeof(RGNDATAHEADER);
  out->rdh.iType = RDH_RECTANGLES;
  out->rdh.nCount = (DWORD)rects.size();
  out->rdh.nRgnSize = (DWORD)(rects.size() * sizeof(RECT));
  out->rdh.rcBound = r->rgn.Bounds();
  memcpy(out->Buffer, rects.data(), rects.size() * sizeof(RECT));
  return need;
}

// ---------------------------------------------------------------------
// Objects in general

BOOL WINAPI DeleteObject(HGDIOBJ h) {
  GdiObject* o = GdiObj(h);
  if (!o) return FALSE;
  if (o->stock) return TRUE;
  if (o->type == OBJ_DC || o->type == OBJ_MEMDC) return FALSE;
  if (o->type == OBJ_BITMAP && ((BitmapObj*)o)->selectedIn) return FALSE;
  if (o->refs > 0) {
    o->deletePending = true;
    return TRUE;
  }
  FreeGdiHandle(h);
  return TRUE;
}

DWORD WINAPI GetObjectType(HGDIOBJ h) {
  GdiObject* o = GdiObj(h);
  return o ? o->type : 0;
}

int WINAPI GetObjectW(HGDIOBJ h, int size, LPVOID out) {
  GdiObject* o = GdiObj(h);
  if (!o) return 0;
  switch (o->type) {
    case OBJ_PEN: {
      PenObj* p = (PenObj*)o;
      if (p->extended) {
        EXTLOGPEN e = {p->style, (DWORD)p->width, BS_SOLID, p->color, 0, 0, {0}};
        int n = (int)offsetof(EXTLOGPEN, elpStyleEntry);
        if (!out) return n;
        memcpy(out, &e, size < n ? size : n);
        return size < n ? size : n;
      }
      LOGPEN lp = {p->style, {p->width, 0}, p->color};
      if (!out) return sizeof lp;
      memcpy(out, &lp, size < (int)sizeof lp ? size : (int)sizeof lp);
      return size < (int)sizeof lp ? size : (int)sizeof lp;
    }
    case OBJ_BRUSH: {
      BrushObj* b = (BrushObj*)o;
      LOGBRUSH lb = b->lb;
      if (b->sysColor >= 0) lb.lbColor = GetSysColor(b->sysColor);
      if (!out) return sizeof lb;
      memcpy(out, &lb, size < (int)sizeof lb ? size : (int)sizeof lb);
      return size < (int)sizeof lb ? size : (int)sizeof lb;
    }
    case OBJ_FONT: {
      LOGFONTW& lf = ((FontObj*)o)->lf;
      if (!out) return sizeof lf;
      memcpy(out, &lf, size < (int)sizeof lf ? size : (int)sizeof lf);
      return size < (int)sizeof lf ? size : (int)sizeof lf;
    }
    case OBJ_PAL: {
      WORD n = (WORD)((PaletteObj*)o)->entries.size();
      if (!out) return sizeof(WORD);
      if (size >= (int)sizeof(WORD)) *(WORD*)out = n;
      return sizeof(WORD);
    }
    case OBJ_BITMAP: {
      BitmapObj* b = (BitmapObj*)o;
      BITMAP bm = {0, b->width, b->height, 0, 1, (WORD)(b->dib ? b->bpp : b->mono ? 1 : 32), NULL};
      bm.bmWidthBytes = b->dib ? b->dibStride : ((b->width * bm.bmBitsPixel + 15) / 16) * 2;
      if (b->dib) bm.bmBits = b->bits;
      if (b->dib && size >= (int)sizeof(DIBSECTION)) {
        if (!out) return sizeof(DIBSECTION);
        DIBSECTION ds = {};
        ds.dsBm = bm;
        ds.dsBmih = b->bih;
        memcpy(ds.dsBitfields, b->masks, sizeof ds.dsBitfields);
        ds.dshSection = b->section;
        ds.dsOffset = b->sectionOffset;
        memcpy(out, &ds, sizeof ds);
        return sizeof ds;
      }
      if (!out) return sizeof bm;
      memcpy(out, &bm, size < (int)sizeof bm ? size : (int)sizeof bm);
      return size < (int)sizeof bm ? size : (int)sizeof bm;
    }
  }
  SetLastError(ERROR_INVALID_HANDLE);
  return 0;
}

int WINAPI GetObjectA(HGDIOBJ h, int size, LPVOID out) {
  GdiObject* o = GdiObj(h);
  if (!o) return 0;
  if (o->type != OBJ_FONT) return GetObjectW(h, size, out);
  if (!out) return sizeof(LOGFONTA);
  const LOGFONTW& w = ((FontObj*)o)->lf;
  LOGFONTA a;
  memcpy(&a, &w, offsetof(LOGFONTA, lfFaceName));
  memset(a.lfFaceName, 0, sizeof a.lfFaceName);
  std::string face = Narrow(w.lfFaceName);
  strncpy(a.lfFaceName, face.c_str(), LF_FACESIZE - 1);
  int n = size < (int)sizeof a ? size : (int)sizeof a;
  memcpy(out, &a, n);
  return n;
}

BOOL WINAPI UnrealizeObject(HGDIOBJ h) { return GdiObj(h) != NULL; }

}  // extern "C"
