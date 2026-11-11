// gdi_blit.cpp - filling and copying pixels: brushes and raster
// operations, rectangle and region fills, BitBlt and its relatives, and
// DIBs drawn on a device.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <stdlib.h>
#include <string.h>

using namespace winport;

namespace winport {

// ---------------------------------------------------------------------
// Raster operations

DWORD Rop3(DWORD rop, DWORD d, DWORD s, DWORD p) {
  switch ((rop >> 16) & 0xFF) {
    case 0x00: return 0;
    case 0xFF: return 0xFFFFFF;
    case 0xCC: return s;
    case 0xF0: return p;
    case 0xAA: return d;
    case 0x55: return ~d & 0xFFFFFF;
    case 0x33: return ~s & 0xFFFFFF;
    case 0x88: return s & d;
    case 0xEE: return s | d;
    case 0x66: return s ^ d;
    case 0x5A: return p ^ d;
    case 0xC0: return p & s;
    case 0xBB: return (~s | d) & 0xFFFFFF;
    case 0x44: return s & ~d & 0xFFFFFF;
    case 0x22: return ~s & d & 0xFFFFFF;
    case 0x11: return ~(s | d) & 0xFFFFFF;
    case 0xFB: return p | ~s | d;
  }
  // The general case: each bit of the code is the result for one
  // combination of pattern, source and destination bits.
  BYTE code = (BYTE)(rop >> 16);
  DWORD r = 0;
  for (int i = 0; i < 8; i++) {
    if (!(code & (1 << i))) continue;
    DWORD m = ((i & 4) ? p : ~p) & ((i & 2) ? s : ~s) & ((i & 1) ? d : ~d);
    r |= m;
  }
  return r & 0xFFFFFF;
}

DWORD Rop2(int rop, DWORD d, DWORD p) {
  DWORD r;
  switch (rop) {
    case R2_BLACK: r = 0; break;
    case R2_NOTMERGEPEN: r = ~(d | p); break;
    case R2_MASKNOTPEN: r = d & ~p; break;
    case R2_NOTCOPYPEN: r = ~p; break;
    case R2_MASKPENNOT: r = p & ~d; break;
    case R2_NOT: r = ~d; break;
    case R2_XORPEN: r = d ^ p; break;
    case R2_NOTMASKPEN: r = ~(d & p); break;
    case R2_MASKPEN: r = d & p; break;
    case R2_NOTXORPEN: r = ~(d ^ p); break;
    case R2_NOP: r = d; break;
    case R2_MERGENOTPEN: r = d | ~p; break;
    case R2_MERGEPENNOT: r = p | ~d; break;
    case R2_MERGEPEN: r = d | p; break;
    case R2_WHITE: r = 0xFFFFFF; break;
    default: r = p; break;
  }
  return r & 0xFFFFFF;
}

namespace {

bool UsesSource(DWORD rop) {
  BYTE c = (BYTE)(rop >> 16);
  return ((c >> 2) & 0x33) != (c & 0x33);
}

bool IsMonoDC(DCObj* dc) {
  if (dc->kind != kMemoryDC) return false;
  BitmapObj* b = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP);
  return b && b->mono;
}

// A color on a monochrome bitmap: white when light enough.
DWORD ToMono(DWORD p) {
  int lum = (int)(((p >> 16) & 0xFF) * 30 + ((p >> 8) & 0xFF) * 59 + (p & 0xFF) * 11) / 100;
  return lum >= 128 ? 0xFFFFFF : 0;
}

bool HatchBit(int hatch, int x, int y) {
  x &= 7;
  y &= 7;
  switch (hatch) {
    case HS_HORIZONTAL: return y == 4;
    case HS_VERTICAL: return x == 4;
    case HS_FDIAGONAL: return x == y;
    case HS_BDIAGONAL: return x == 7 - y;
    case HS_CROSS: return x == 4 || y == 4;
    case HS_DIAGCROSS: return x == y || x == 7 - y;
  }
  return false;
}

}  // namespace

DWORD BrushPaint::At(int x, int y) const {
  if (solid) return color;
  const BrushObj* b = brush;
  if (b->lb.lbStyle == BS_HATCHED) return HatchBit((int)b->lb.lbHatch, x - ox, y - oy) ? color : back;
  if (b->patWidth <= 0 || b->patHeight <= 0) return color;
  int px = (x - ox) % b->patWidth, py = (y - oy) % b->patHeight;
  if (px < 0) px += b->patWidth;
  if (py < 0) py += b->patHeight;
  DWORD p = b->pattern[(size_t)py * b->patWidth + px];
  if (b->monoPattern) return p ? back : fore;
  return p;
}

bool BrushPaint::Draws(int x, int y) const {
  if (null) return false;
  if (!transparentHatch) return true;
  return HatchBit((int)brush->lb.lbHatch, x - ox, y - oy);
}

BrushPaint PaintFor(DCObj* dc, HBRUSH h) {
  BrushPaint p;
  // A system color index plus one stands for that color's brush.
  if ((UINT_PTR)h > 0 && (UINT_PTR)h <= COLOR_MENUBAR + 1) h = GetSysColorBrush((int)(UINT_PTR)h - 1);
  BrushObj* b = GdiAs<BrushObj>(h, OBJ_BRUSH);
  if (!b || b->lb.lbStyle == BS_NULL) return p;
  p.null = false;
  p.brush = b;
  p.fore = ToPixel(dc->s.textColor);
  p.back = ToPixel(dc->s.bkColor);
  POINT org = dc->s.brushOrg;
  p.ox = org.x;
  p.oy = org.y;
  COLORREF c = b->lb.lbColor;
  if (b->sysColor >= 0) c = GetSysColor(b->sysColor);
  if (h == GetStockObject(DC_BRUSH)) c = dc->s.dcBrushColor;
  p.color = ToPixel(c);
  p.solid = b->lb.lbStyle == BS_SOLID;
  if (b->lb.lbStyle == BS_HATCHED && dc->s.bkMode == TRANSPARENT) p.transparentHatch = true;
  if (IsMonoDC(dc)) {
    p.color = ToMono(p.color);
    p.fore = ToMono(p.fore);
    p.back = ToMono(p.back);
  }
  return p;
}

void FillDevice(DCObj* dc, const RECT& device, const BrushPaint& paint, DWORD rop) {
  bool usesPat = (((rop >> 16) & 0xFF) >> 4) != (((rop >> 16) & 0xFF) & 0x0F);
  if (usesPat && paint.null) return;
  DrawScope ds(dc);
  if (!ds.ok) return;
  bool mono = IsMonoDC(dc);
  for (const RECT& c : ds.clip.Rects()) {
    RECT r = Intersect(c, device);
    if (RectEmpty(r)) continue;
    for (int y = r.top; y < r.bottom; y++) {
      DWORD* row = ds.px.Row(y + ds.origin.y) + ds.origin.x;
      if (paint.solid && !paint.transparentHatch && ((rop >> 16) & 0xFF) == 0xF0) {
        for (int x = r.left; x < r.right; x++) row[x] = paint.color;
        continue;
      }
      for (int x = r.left; x < r.right; x++) {
        if (usesPat && !paint.Draws(x, y)) continue;
        DWORD v = Rop3(rop, row[x] & 0xFFFFFF, 0, usesPat ? paint.At(x, y) : 0);
        row[x] = mono ? ToMono(v) : v;
      }
    }
    ds.Dirty(r);
  }
}

}  // namespace winport

namespace {

// Copies source pixels to a device rectangle of the destination, scaled
// to fit; a negative width or height mirrors. The source is first copied
// aside, so that it may overlap the destination.
struct Source {
  std::vector<DWORD> pixels;  // sw x sh, read from the source
  std::vector<BYTE> valid;    // whether each pixel lay on the source
  int w = 0, h = 0;
  bool mono = false;
  DWORD bk = 0xFFFFFF;  // the source DC's background color, for color to monochrome
  std::vector<BYTE> alpha;
};

bool ReadSource(DCObj* src, int sx, int sy, int sw, int sh, Source* out) {
  out->w = sw;
  out->h = sh;
  out->pixels.assign((size_t)sw * sh, 0);
  out->valid.assign((size_t)sw * sh, 0);
  out->mono = IsMonoDC(src);
  out->bk = ToPixel(src->s.bkColor);
  DrawScope ds(src, false);
  if (!ds.ok) return false;
  // Reading is limited to the source's own pixels, not its clipping.
  RECT bounds = MakeRect(-ds.origin.x, -ds.origin.y, ds.px.width - ds.origin.x, ds.px.height - ds.origin.y);
  for (int y = 0; y < sh; y++) {
    int yy = sy + y;
    if (yy < bounds.top || yy >= bounds.bottom) continue;
    const DWORD* row = ds.px.Row(yy + ds.origin.y) + ds.origin.x;
    for (int x = 0; x < sw; x++) {
      int xx = sx + x;
      if (xx < bounds.left || xx >= bounds.right) continue;
      out->pixels[(size_t)y * sw + x] = row[xx];
      out->valid[(size_t)y * sw + x] = 1;
    }
  }
  return true;
}

enum BlendKind { kRop, kTransparent, kAlpha };

struct BlendInfo {
  BlendKind kind = kRop;
  DWORD rop = SRCCOPY;
  DWORD transparent = 0;
  BLENDFUNCTION bf = {};
};

void PutPixels(DCObj* dst, int dx, int dy, int dw, int dh, const Source& s, const BlendInfo& how) {
  if (dw == 0 || dh == 0 || s.w <= 0 || s.h <= 0) return;
  bool mirrorX = dw < 0, mirrorY = dh < 0;
  if (mirrorX) {
    dx += dw;
    dw = -dw;
  }
  if (mirrorY) {
    dy += dh;
    dh = -dh;
  }
  RECT drect = MakeRect(dx, dy, dx + dw, dy + dh);
  bool dstMono = IsMonoDC(dst);
  DWORD fore = ToPixel(dst->s.textColor), back = ToPixel(dst->s.bkColor);
  BrushPaint pat;
  bool usesPat = false;
  if (how.kind == kRop) {
    BYTE c = (BYTE)(how.rop >> 16);
    usesPat = (c >> 4) != (c & 0x0F);
    if (usesPat) pat = PaintFor(dst, (HBRUSH)dst->s.brush);
  }
  DrawScope ds(dst);
  if (!ds.ok) return;
  for (const RECT& c : ds.clip.Rects()) {
    RECT r = Intersect(c, drect);
    if (RectEmpty(r)) continue;
    for (int y = r.top; y < r.bottom; y++) {
      int ty = (int)((long long)(y - dy) * s.h / dh);
      if (mirrorY) ty = s.h - 1 - ty;
      DWORD* row = ds.px.Row(y + ds.origin.y) + ds.origin.x;
      for (int x = r.left; x < r.right; x++) {
        int tx = (int)((long long)(x - dx) * s.w / dw);
        if (mirrorX) tx = s.w - 1 - tx;
        size_t i = (size_t)ty * s.w + tx;
        if (!s.valid[i]) continue;
        DWORD sp = s.pixels[i];
        DWORD d = row[x];
        DWORD out;
        if (how.kind == kTransparent) {
          if ((sp & 0xFFFFFF) == how.transparent) continue;
          out = sp & 0xFFFFFF;
        } else if (how.kind == kAlpha) {
          DWORD sa = how.bf.AlphaFormat & AC_SRC_ALPHA ? sp >> 24 : 255;
          DWORD ca = how.bf.SourceConstantAlpha;
          // With per-pixel alpha the source is premultiplied.
          DWORD a = sa * ca / 255;
          DWORD res = 0;
          for (int sh = 0; sh <= 16; sh += 8) {
            DWORD sc = (sp >> sh) & 0xFF, dc = (d >> sh) & 0xFF;
            DWORD v = (how.bf.AlphaFormat & AC_SRC_ALPHA) ? (sc * ca / 255 + dc * (255 - a) / 255)
                                                          : (sc * a + dc * (255 - a)) / 255;
            if (v > 255) v = 255;
            res |= v << sh;
          }
          out = res;
        } else {
          sp &= 0xFFFFFF;
          if (s.mono && !dstMono) sp = sp ? back : fore;
          else if (!s.mono && dstMono) sp = sp == (s.bk & 0xFFFFFF) ? 0xFFFFFF : 0;
          out = Rop3(how.rop, d & 0xFFFFFF, sp, usesPat ? pat.At(x, y) : 0);
        }
        row[x] = dstMono ? ToMono(out) : out;
      }
    }
    ds.Dirty(r);
  }
}

// The device rectangle for a logical one given by origin and size; the
// size keeps its sign so that mirroring survives.
void DeviceBox(DCObj* dc, int x, int y, int w, int h, int* dx, int* dy, int* dw, int* dh) {
  POINT a = dc->ToDevice(x, y), b = dc->ToDevice(x + w, y + h);
  *dx = a.x;
  *dy = a.y;
  *dw = b.x - a.x;
  *dh = b.y - a.y;
}

BOOL StretchCore(HDC hdst, int x, int y, int w, int h, HDC hsrc, int sx, int sy, int sw, int sh, const BlendInfo& how) {
  DCObj* dst = DCOf(hdst);
  if (!dst) return FALSE;
  if (how.kind == kRop && !UsesSource(how.rop)) {
    int dx, dy, dw, dh;
    DeviceBox(dst, x, y, w, h, &dx, &dy, &dw, &dh);
    RECT r = {dw < 0 ? dx + dw : dx, dh < 0 ? dy + dh : dy, dw < 0 ? dx : dx + dw, dh < 0 ? dy : dy + dh};
    FillDevice(dst, r, PaintFor(dst, (HBRUSH)dst->s.brush), how.rop);
    return TRUE;
  }
  DCObj* src = DCOf(hsrc);
  if (!src) return FALSE;
  int dx, dy, dw, dh, ssx, ssy, ssw, ssh;
  DeviceBox(dst, x, y, w, h, &dx, &dy, &dw, &dh);
  DeviceBox(src, sx, sy, sw, sh, &ssx, &ssy, &ssw, &ssh);
  // A mirrored source reads the same pixels the other way round.
  if (ssw < 0) {
    ssx += ssw;
    ssw = -ssw;
    dx += dw;
    dw = -dw;
  }
  if (ssh < 0) {
    ssy += ssh;
    ssh = -ssh;
    dy += dh;
    dh = -dh;
  }
  Source s;
  if (!ReadSource(src, ssx, ssy, ssw, ssh, &s)) return FALSE;
  PutPixels(dst, dx, dy, dw, dh, s, how);
  return TRUE;
}

// A DIB's pixels, top row first, and the source rectangle within them in
// top-down terms.
bool ReadDib(const BITMAPINFO* bmi, const void* bits, UINT usage, int sx, int syFromBottom, int sw, int sh,
             int startScan, int numScans, Source* out) {
  DibFormat f;
  if (!bits || !ParseDib(bmi, usage, &f)) return false;
  std::vector<DWORD> all((size_t)f.width * f.height, 0);
  std::vector<BYTE> have(f.height, 0);
  if (numScans < 0) {
    startScan = 0;
    numScans = f.height;
  }
  for (int i = 0; i < numScans; i++) {
    int memRow = startScan + i;
    if (memRow < 0 || memRow >= f.height) continue;
    int y = f.topDown ? memRow : f.height - 1 - memRow;
    DibRowToPixels(f, (const BYTE*)bits + (size_t)i * f.stride, &all[(size_t)y * f.width], f.width);
    have[y] = 1;
  }
  // A bottom-up DIB counts its source y from the bottom.
  int sy = f.topDown ? syFromBottom : f.height - syFromBottom - sh;
  out->w = sw;
  out->h = sh;
  out->pixels.assign((size_t)sw * sh, 0);
  out->valid.assign((size_t)sw * sh, 0);
  for (int y = 0; y < sh; y++) {
    int yy = sy + y;
    if (yy < 0 || yy >= f.height || !have[yy]) continue;
    for (int x = 0; x < sw; x++) {
      int xx = sx + x;
      if (xx < 0 || xx >= f.width) continue;
      out->pixels[(size_t)y * sw + x] = all[(size_t)yy * f.width + xx];
      out->valid[(size_t)y * sw + x] = 1;
    }
  }
  return true;
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------------
// Rectangles and regions

BOOL WINAPI PatBlt(HDC hdc, int x, int y, int w, int h, DWORD rop) {
  BlendInfo how;
  how.rop = rop;
  if (UsesSource(rop)) return FALSE;
  return StretchCore(hdc, x, y, w, h, NULL, 0, 0, 0, 0, how);
}

int WINAPI FillRect(HDC hdc, const RECT* r, HBRUSH brush) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !r) return FALSE;
  FillDevice(dc, dc->ToDevice(*r), PaintFor(dc, brush), PATCOPY);
  return TRUE;
}

int WINAPI FrameRect(HDC hdc, const RECT* r, HBRUSH brush) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !r) return FALSE;
  if (r->right <= r->left || r->bottom <= r->top) return TRUE;
  BrushPaint p = PaintFor(dc, brush);
  RECT d = dc->ToDevice(*r);
  int bx = dc->ScaleX(1), by = dc->ScaleY(1);
  if (bx < 1) bx = 1;
  if (by < 1) by = 1;
  FillDevice(dc, MakeRect(d.left, d.top, d.right, d.top + by), p, PATCOPY);
  FillDevice(dc, MakeRect(d.left, d.bottom - by, d.right, d.bottom), p, PATCOPY);
  FillDevice(dc, MakeRect(d.left, d.top + by, d.left + bx, d.bottom - by), p, PATCOPY);
  FillDevice(dc, MakeRect(d.right - bx, d.top + by, d.right, d.bottom - by), p, PATCOPY);
  return TRUE;
}

BOOL WINAPI InvertRect(HDC hdc, const RECT* r) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !r) return FALSE;
  FillDevice(dc, dc->ToDevice(*r), BrushPaint(), DSTINVERT);
  return TRUE;
}

static BOOL RegionOp(HDC hdc, HRGN h, HBRUSH brush, DWORD rop) {
  DCObj* dc = DCOf(hdc);
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!dc || !r) return FALSE;
  BrushPaint p = brush ? PaintFor(dc, brush) : BrushPaint();
  for (const RECT& rc : r->rgn.Rects()) FillDevice(dc, dc->ToDevice(rc), p, rop);
  return TRUE;
}

BOOL WINAPI FillRgn(HDC hdc, HRGN h, HBRUSH brush) { return RegionOp(hdc, h, brush, PATCOPY); }
BOOL WINAPI InvertRgn(HDC hdc, HRGN h) { return RegionOp(hdc, h, NULL, DSTINVERT); }
BOOL WINAPI PaintRgn(HDC hdc, HRGN h) {
  DCObj* dc = DCOf(hdc);
  return dc && RegionOp(hdc, h, (HBRUSH)dc->s.brush, PATCOPY);
}

BOOL WINAPI FrameRgn(HDC hdc, HRGN h, HBRUSH brush, int w, int ht) {
  DCObj* dc = DCOf(hdc);
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!dc || !r) return FALSE;
  // The frame is the region less itself shrunk by the frame size: what
  // stays inside when moved the frame's width or height any way.
  Region shrunk = r->rgn;
  const int moves[4][2] = {{w, 0}, {-w, 0}, {0, ht}, {0, -ht}};
  for (const auto& m : moves) {
    Region moved = r->rgn;
    moved.Offset(m[0], m[1]);
    shrunk.Intersect(moved);
  }
  Region inner = r->rgn;
  inner.Subtract(shrunk);
  BrushPaint p = PaintFor(dc, brush);
  for (const RECT& rc : inner.Rects()) FillDevice(dc, dc->ToDevice(rc), p, PATCOPY);
  return TRUE;
}

// ---------------------------------------------------------------------
// Pixels

COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF c) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return CLR_INVALID;
  POINT p = dc->ToDevice(x, y);
  DrawScope ds(dc);
  if (!ds.ok || !ds.clip.Contains(p.x, p.y)) return CLR_INVALID;
  DWORD v = ToPixel(c);
  if (IsMonoDC(dc)) v = ToMono(v);
  ds.At(p.x, p.y) = v;
  ds.Dirty(MakeRect(p.x, p.y, p.x + 1, p.y + 1));
  return ToColor(v);
}

BOOL WINAPI SetPixelV(HDC hdc, int x, int y, COLORREF c) { return SetPixel(hdc, x, y, c) != CLR_INVALID; }

COLORREF WINAPI GetPixel(HDC hdc, int x, int y) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return CLR_INVALID;
  POINT p = dc->ToDevice(x, y);
  DrawScope ds(dc, false);
  if (!ds.ok) return CLR_INVALID;
  int px = p.x + ds.origin.x, py = p.y + ds.origin.y;
  if (px < 0 || py < 0 || px >= ds.px.width || py >= ds.px.height) return CLR_INVALID;
  if (dc->kind == kWindowDC && !dc->Visible().Contains(p.x, p.y)) return CLR_INVALID;
  return ToColor(ds.At(p.x, p.y) & 0xFFFFFF);
}

COLORREF WINAPI GetNearestColor(HDC hdc, COLORREF c) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return CLR_INVALID;
  if (IsMonoDC(dc)) return ToColor(ToMono(ToPixel(c)));
  return c & 0xFFFFFF;
}

// ---------------------------------------------------------------------
// Copying

BOOL WINAPI BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
  BlendInfo how;
  how.rop = rop;
  return StretchCore(dst, x, y, w, h, src, sx, sy, w, h, how);
}

BOOL WINAPI StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
  BlendInfo how;
  how.rop = rop;
  return StretchCore(dst, x, y, w, h, src, sx, sy, sw, sh, how);
}

BOOL WINAPI GdiTransparentBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
                              UINT transparent) {
  BlendInfo how;
  how.kind = kTransparent;
  how.transparent = ToPixel(transparent);
  return StretchCore(dst, x, y, w, h, src, sx, sy, sw, sh, how);
}
BOOL WINAPI TransparentBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
                           UINT transparent) {
  return GdiTransparentBlt(dst, x, y, w, h, src, sx, sy, sw, sh, transparent);
}

BOOL WINAPI GdiAlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
                          BLENDFUNCTION bf) {
  BlendInfo how;
  how.kind = kAlpha;
  how.bf = bf;
  return StretchCore(dst, x, y, w, h, src, sx, sy, sw, sh, how);
}
BOOL WINAPI AlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
                       BLENDFUNCTION bf) {
  return GdiAlphaBlend(dst, x, y, w, h, src, sx, sy, sw, sh, bf);
}

// MaskBlt: the foreground operation where the mask is 1, the background
// one where it is 0.
BOOL WINAPI MaskBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, HBITMAP mask, int mx, int my,
                    DWORD rop) {
  DWORD fore = rop & 0x00FFFFFF, back = ((rop >> 8) & 0x00FF0000) | (rop & 0xFFFF);
  BitmapObj* m = mask ? GdiAs<BitmapObj>(mask, OBJ_BITMAP) : NULL;
  if (!m) return BitBlt(dst, x, y, w, h, src, sx, sy, fore);
  DCObj* d = DCOf(dst);
  if (!d) return FALSE;
  // Two passes through a scratch copy of the destination.
  HDC tmp = CreateCompatibleDC(dst);
  HBITMAP bm = CreateCompatibleBitmap(dst, w, h);
  HGDIOBJ old = SelectObject(tmp, bm);
  SelectObject(tmp, d->s.brush);
  BitBlt(tmp, 0, 0, w, h, dst, x, y, SRCCOPY);
  HDC fg = CreateCompatibleDC(dst);
  HBITMAP fbm = CreateCompatibleBitmap(dst, w, h);
  HGDIOBJ fold = SelectObject(fg, fbm);
  SelectObject(fg, d->s.brush);
  BitBlt(fg, 0, 0, w, h, dst, x, y, SRCCOPY);
  BitBlt(fg, 0, 0, w, h, src, sx, sy, fore);
  BitBlt(tmp, 0, 0, w, h, src, sx, sy, back);
  Pixels mp = m->Lock();
  DCObj* tdc = DCOf(tmp);
  DCObj* fdc = DCOf(fg);
  {
    DrawScope ts(tdc);
    DrawScope fs(fdc, false);
    if (ts.ok && fs.ok) {
      for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
          int ux = mx + xx, uy = my + yy;
          if (ux < 0 || uy < 0 || ux >= mp.width || uy >= mp.height) continue;
          if (mp.Row(uy)[ux] & 0xFFFFFF) ts.At(xx, yy) = fs.At(xx, yy);
        }
    }
  }
  m->Unlock();
  BitBlt(dst, x, y, w, h, tmp, 0, 0, SRCCOPY);
  SelectObject(fg, fold);
  DeleteObject(fbm);
  DeleteDC(fg);
  SelectObject(tmp, old);
  DeleteObject(bm);
  DeleteDC(tmp);
  return TRUE;
}

BOOL WINAPI PlgBlt(HDC, const POINT*, HDC, int, int, int, int, HBITMAP, int, int) {
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

int WINAPI StretchDIBits(HDC hdc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
                         const BITMAPINFO* bmi, UINT usage, DWORD rop) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  Source s;
  if (!ReadDib(bmi, bits, usage, sx, sy, sw, sh, 0, -1, &s)) return 0;
  int dx, dy, dw, dh;
  DeviceBox(dc, x, y, w, h, &dx, &dy, &dw, &dh);
  BlendInfo how;
  how.rop = rop;
  PutPixels(dc, dx, dy, dw, dh, s, how);
  DibFormat f;
  ParseDib(bmi, usage, &f);
  return f.height;
}

int WINAPI SetDIBitsToDevice(HDC hdc, int x, int y, DWORD w, DWORD h, int sx, int sy, UINT start, UINT lines,
                             const void* bits, const BITMAPINFO* bmi, UINT usage) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  Source s;
  if (!ReadDib(bmi, bits, usage, sx, sy, (int)w, (int)h, (int)start, (int)lines, &s)) return 0;
  POINT p = dc->ToDevice(x, y);
  BlendInfo how;
  PutPixels(dc, p.x, p.y, (int)w, (int)h, s, how);
  return (int)lines;
}

}  // extern "C"
