// gdi_text.cpp - text: drawing and measuring strings, font metrics and
// enumeration, and DrawText and its relatives.
//
// Glyphs come from font.cpp as coverage bitmaps. Text is blended onto the
// device in the text color (or, on a monochrome bitmap and for
// non-antialiased fonts, drawn where coverage passes half). The
// background box and ETO_OPAQUE fill go first, then the glyphs, then
// underline and strikeout. A font's escapement turns the whole line.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "font.h"
#include "gdi.h"
#include "user.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

const double kPi = 3.14159265358979323846;

int RoundI(double v) { return (int)floor(v + 0.5); }

int ScaleSignedX(DCObj* dc, int v) { return v < 0 ? -dc->ScaleX(-v) : dc->ScaleX(v); }

RealFont* FontOf(DCObj* dc) {
  FontObj* fo = GdiAs<FontObj>(dc->s.font, OBJ_FONT);
  if (!fo) fo = GdiAs<FontObj>(GetStockObject(SYSTEM_FONT), OBJ_FONT);
  LOGFONTW lf = fo ? fo->lf : LOGFONTW();
  if (lf.lfHeight) lf.lfHeight = lf.lfHeight < 0 ? -dc->ScaleY(-lf.lfHeight) : dc->ScaleY(lf.lfHeight);
  if (lf.lfHeight == 0 && fo && fo->lf.lfHeight) lf.lfHeight = fo->lf.lfHeight < 0 ? -1 : 1;
  if (lf.lfWidth) lf.lfWidth = dc->ScaleX(abs(lf.lfWidth));
  return RealizeFont(lf);
}

// ANSI strings are UTF-8; each character remembers the byte it starts at.
struct AnsiText {
  std::wstring w;
  std::vector<int> byteAt;  // one more entry than characters: the length
};

// With a symbol font each byte is its own character, as Windows has it.
AnsiText FromAnsi(const char* s, int n, bool raw = false) {
  AnsiText t;
  if (!s) n = 0;
  if (n < 0) n = (int)strlen(s);
  const BYTE* p = (const BYTE*)s;
  int i = 0;
  while (i < n) {
    t.byteAt.push_back(i);
    BYTE c = p[i];
    if (raw) {
      t.w += (wchar_t)c;
      i++;
      continue;
    }
    int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    UINT32 cp = 0xFFFD;
    if (len > 0 && i + len <= n) {
      cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
      for (int k = 1; k < len; k++) {
        if ((p[i + k] & 0xC0) != 0x80) {
          len = 0;
          break;
        }
        cp = (cp << 6) | (p[i + k] & 0x3F);
      }
    }
    if (len <= 0) {
      cp = 0xFFFD;
      len = 1;
    }
    t.w += (wchar_t)cp;
    i += len;
  }
  t.byteAt.push_back(n);
  return t;
}

// The text of an A function, read for the font selected into the DC.
AnsiText FromAnsi(HDC hdc, const char* s, int n) {
  DCObj* dc = DCOf(hdc);
  return FromAnsi(s, n, dc && FontOf(dc)->charset == SYMBOL_CHARSET);
}

std::string ToAnsi(const std::wstring& w) { return Narrow(w.c_str(), (int)w.size()); }

// Advances in device pixels, with the DC's extra spacing, or from dx.
std::vector<int> Advances(DCObj* dc, RealFont* f, const wchar_t* s, int n, const INT* dx, bool pdy) {
  std::vector<int> adv((size_t)n);
  if (dx) {
    for (int i = 0; i < n; i++) adv[i] = ScaleSignedX(dc, dx[pdy ? i * 2 : i]);
    return adv;
  }
  int extra = ScaleSignedX(dc, dc->s.charExtra);
  int breaks = dc->s.breakCount, breakExtra = ScaleSignedX(dc, dc->s.breakExtra);
  int seen = 0;
  for (int i = 0; i < n; i++) {
    adv[i] = f->Advance((UINT32)s[i]) + extra;
    if (s[i] == L' ' && breaks > 0 && seen < breaks) {
      adv[i] += breakExtra / breaks + (seen < abs(breakExtra % breaks) ? (breakExtra < 0 ? -1 : 1) : 0);
      seen++;
    }
  }
  return adv;
}

int Sum(const std::vector<int>& v) {
  int t = 0;
  for (int a : v) t += a;
  return t;
}

DWORD Blend(DWORD d, DWORD c, int a) {
  if (a >= 255) return c;
  int r = (((c >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255;
  int g = (((c >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255;
  int b = ((c & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255;
  return (DWORD)((r << 16) | (g << 8) | b);
}

// Draws coverage cov (w x h), whose (0, 0) lies at (gx, gy) in glyph space
// (x along the line, y down from it), from origin o along unit vector u.
struct GlyphPainter {
  DrawScope* ds;
  DWORD color;
  bool aa;
  double ux, uy;  // along the line
  bool rotated;
  RECT dirty = {0, 0, 0, 0};

  void Plot(int x, int y, int a) {
    if (a <= 0) return;
    if (!aa) {
      if (a < 128) return;
      a = 255;
    }
    DWORD& d = ds->At(x, y);
    d = Blend(d & 0xFFFFFF, color, a);
  }
  void Grow(const RECT& r) {
    if (RectEmpty(dirty)) {
      dirty = r;
      return;
    }
    dirty.left = std::min(dirty.left, r.left);
    dirty.top = std::min(dirty.top, r.top);
    dirty.right = std::max(dirty.right, r.right);
    dirty.bottom = std::max(dirty.bottom, r.bottom);
  }
  // cov NULL means full coverage.
  void Paint(double ox, double oy, int gx, int gy, int w, int h, const BYTE* cov) {
    if (w <= 0 || h <= 0) return;
    if (!rotated) {
      int x0 = RoundI(ox) + gx, y0 = RoundI(oy) + gy;
      RECT box = MakeRect(x0, y0, x0 + w, y0 + h);
      for (const RECT& c : ds->clip.Rects()) {
        RECT r = Intersect(c, box);
        if (RectEmpty(r)) continue;
        for (int y = r.top; y < r.bottom; y++)
          for (int x = r.left; x < r.right; x++)
            Plot(x, y, cov ? cov[(size_t)(y - y0) * w + (x - x0)] : 255);
        Grow(r);
      }
      return;
    }
    // v is u turned a quarter clockwise on the screen: down from the line.
    double vx = -uy, vy = ux;
    double cx[4], cy[4];
    double gxs[4] = {(double)gx, (double)gx + w, (double)gx, (double)gx + w};
    double gys[4] = {(double)gy, (double)gy, (double)gy + h, (double)gy + h};
    double l = 1e300, t = 1e300, r = -1e300, b = -1e300;
    for (int i = 0; i < 4; i++) {
      cx[i] = ox + gxs[i] * ux + gys[i] * vx;
      cy[i] = oy + gxs[i] * uy + gys[i] * vy;
      l = std::min(l, cx[i]);
      r = std::max(r, cx[i]);
      t = std::min(t, cy[i]);
      b = std::max(b, cy[i]);
    }
    RECT box = MakeRect((int)floor(l), (int)floor(t), (int)ceil(r) + 1, (int)ceil(b) + 1);
    for (const RECT& c : ds->clip.Rects()) {
      RECT rr = Intersect(c, box);
      if (RectEmpty(rr)) continue;
      for (int y = rr.top; y < rr.bottom; y++)
        for (int x = rr.left; x < rr.right; x++) {
          double dx = x + 0.5 - ox, dy = y + 0.5 - oy;
          int px = (int)floor(dx * ux + dy * uy) - gx, py = (int)floor(dx * vx + dy * vy) - gy;
          if (px < 0 || py < 0 || px >= w || py >= h) continue;
          Plot(x, y, cov ? cov[(size_t)py * w + px] : 255);
        }
      Grow(rr);
    }
  }
};

BrushPaint SolidPaint(DCObj* dc, COLORREF c) {
  BrushPaint p;
  p.null = false;
  p.solid = true;
  p.color = ToPixel(c);
  if (IsMonoDC(dc)) p.color = ToMono(p.color);
  return p;
}

BOOL TextOutCore(HDC hdc, int x, int y, UINT opt, const RECT* rc, const wchar_t* s, int n, const INT* dx) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (n < 0 || (n > 0 && !s)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (IsMetaDC(dc)) {
    meta::Text(dc, x, y, opt, rc, s, n, dx);
    return TRUE;
  }
  RealFont* f = FontOf(dc);
  std::vector<int> adv = Advances(dc, f, s, n, dx, (opt & ETO_PDY) != 0);
  int width = Sum(adv);
  double angle = f->escapement * kPi / 1800;
  double ux = cos(angle), uy = -sin(angle);
  double vx = -uy, vy = ux;
  bool rotated = f->escapement % 3600 != 0;

  UINT align = dc->s.textAlign;
  POINT ref = (align & TA_UPDATECP) ? dc->s.current : POINT{x, y};
  PtF p = dc->ToDeviceF(ref.x, ref.y);
  double shift = 0;
  if ((align & TA_CENTER) == TA_CENTER)
    shift = -width / 2.0;
  else if (align & TA_RIGHT)
    shift = -width;
  double down = 0;
  if ((align & TA_BASELINE) == TA_BASELINE)
    down = 0;
  else if (align & TA_BOTTOM)
    down = -f->descent;
  else
    down = f->ascent;
  double ox = p.x + ux * shift + vx * down, oy = p.y + uy * shift + vy * down;
  if (align & TA_UPDATECP) {
    if ((align & TA_CENTER) != TA_CENTER) {
      double dir = (align & TA_RIGHT) ? -1 : 1;
      double ex = p.x + ux * width * dir, ey = p.y + uy * width * dir;
      dc->s.current = dc->ToLogical(RoundI(ex), RoundI(ey));
    }
  }
  if (dc->pathState == DCObj::kPathOpen) return TRUE;  // glyph outlines are not offered

  RECT clipRect = {0, 0, 0, 0};
  bool clipped = (opt & ETO_CLIPPED) && rc;
  if (rc) clipRect = dc->ToDevice(*rc);
  if ((opt & ETO_OPAQUE) && rc) FillDevice(dc, clipRect, SolidPaint(dc, dc->s.bkColor), PATCOPY);
  if (dc->s.bkMode == OPAQUE && n > 0) {
    // The background box of the text.
    double ax = ox - vx * f->ascent, ay = oy - vy * f->ascent;
    if (!rotated) {
      RECT box = MakeRect(RoundI(ax), RoundI(ay), RoundI(ax) + width, RoundI(ay) + f->height);
      if (width < 0) box = MakeRect(RoundI(ax) + width, RoundI(ay), RoundI(ax), RoundI(ay) + f->height);
      if (clipped) box = Intersect(box, clipRect);
      FillDevice(dc, box, SolidPaint(dc, dc->s.bkColor), PATCOPY);
    } else {
      std::vector<Figure> fig(1);
      double h = f->height;
      fig[0].pts = {{ax - 0.5, ay - 0.5},
                    {ax + ux * width - 0.5, ay + uy * width - 0.5},
                    {ax + ux * width + vx * h - 0.5, ay + uy * width + vy * h - 0.5},
                    {ax + vx * h - 0.5, ay + vy * h - 0.5}};
      fig[0].closed = true;
      DWORD c = ToPixel(dc->s.bkColor);
      if (IsMonoDC(dc)) c = ToMono(c);
      if (clipped) {
        int saved = SaveDC(hdc);
        IntersectClipRect(hdc, rc->left, rc->top, rc->right, rc->bottom);
        FillFiguresDevice(dc, fig, c);
        RestoreDC(hdc, saved);
      } else {
        FillFiguresDevice(dc, fig, c);
      }
    }
  }
  if (n == 0) return TRUE;

  DrawScope ds(dc);
  if (!ds.ok) return TRUE;
  if (clipped) ds.clip.Intersect(clipRect);
  GlyphPainter gp;
  gp.ds = &ds;
  gp.color = ToPixel(dc->s.textColor);
  bool mono = IsMonoDC(dc);
  if (mono) gp.color = ToMono(gp.color);
  gp.aa = f->antialias && !mono;
  gp.ux = ux;
  gp.uy = uy;
  gp.rotated = rotated;
  // A pen position that is not a whole pixel would make each glyph round
  // differently; keep the origin on the grid when the line is level.
  if (!rotated) {
    ox = RoundI(ox);
    oy = RoundI(oy);
  }
  double pos = 0;
  for (int i = 0; i < n; i++) {
    const GlyphImage& g = f->Glyph((UINT32)s[i]);
    if (!g.cov.empty())
      gp.Paint(ox + ux * pos, oy + uy * pos, g.left, g.top, g.w, g.h, g.cov.data());
    pos += adv[i];
  }
  if (f->underline) gp.Paint(ox, oy, 0, f->underlinePos, width, f->underlineSize, NULL);
  if (f->strikeOut) gp.Paint(ox, oy, 0, -f->strikePos, width, f->strikeSize, NULL);
  ds.Dirty(gp.dirty);
  return TRUE;
}

}  // namespace

// ---------------------------------------------------------------------
// Drawing text

BOOL WINAPI ExtTextOutW(HDC hdc, int x, int y, UINT opt, const RECT* rc, LPCWSTR s, UINT n, const INT* dx) {
  return TextOutCore(hdc, x, y, opt, rc, s, (int)n, dx);
}

BOOL WINAPI ExtTextOutA(HDC hdc, int x, int y, UINT opt, const RECT* rc, LPCSTR s, UINT n, const INT* dx) {
  AnsiText t = FromAnsi(hdc, s, (int)n);
  std::vector<INT> wdx;
  bool pdy = (opt & ETO_PDY) != 0;
  if (dx) {
    // One entry per byte (two with ETO_PDY); a character takes the sum
    // of its bytes'.
    size_t chars = t.w.size();
    wdx.assign(chars * (pdy ? 2 : 1), 0);
    for (size_t i = 0; i < chars; i++)
      for (int b = t.byteAt[i]; b < t.byteAt[i + 1]; b++) {
        if (pdy) {
          wdx[i * 2] += dx[b * 2];
          wdx[i * 2 + 1] += dx[b * 2 + 1];
        } else {
          wdx[i] += dx[b];
        }
      }
  }
  return TextOutCore(hdc, x, y, opt, rc, t.w.c_str(), (int)t.w.size(), dx ? wdx.data() : NULL);
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR s, int n) {
  return TextOutCore(hdc, x, y, 0, NULL, s, n, NULL);
}

BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR s, int n) {
  if (n < 0) return FALSE;
  return ExtTextOutA(hdc, x, y, 0, NULL, s, (UINT)n, NULL);
}

// ---------------------------------------------------------------------
// Measuring text

BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR s, int n, int maxExt, LPINT fit, LPINT dx, LPSIZE size) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !size || n < 0 || (n > 0 && !s)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  RealFont* f = FontOf(dc);
  std::vector<int> adv = Advances(dc, f, s, n, NULL, false);
  int total = 0, fitted = 0;
  for (int i = 0; i < n; i++) {
    total += adv[i];
    int logical = dc->LogicalX(total);
    if (dx) dx[i] = logical;
    if (fit && logical <= maxExt && fitted == i) fitted = i + 1;
  }
  if (fit) *fit = fitted;
  size->cx = dc->LogicalX(total);
  size->cy = dc->LogicalY(f->height);
  return TRUE;
}

BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR s, int n, int maxExt, LPINT fit, LPINT dx, LPSIZE size) {
  if (n < 0) return FALSE;
  AnsiText t = FromAnsi(hdc, s, n);
  std::vector<INT> wdx(t.w.size() + 1);
  int wfit = 0;
  if (!GetTextExtentExPointW(hdc, t.w.c_str(), (int)t.w.size(), maxExt, fit ? &wfit : NULL, wdx.data(), size))
    return FALSE;
  if (fit) *fit = t.byteAt[wfit];
  if (dx)
    for (size_t i = 0; i < t.w.size(); i++)
      for (int b = t.byteAt[i]; b < t.byteAt[i + 1]; b++) dx[b] = wdx[i];
  return TRUE;
}

BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR s, int n, LPSIZE size) {
  return GetTextExtentExPointW(hdc, s, n, 0, NULL, NULL, size);
}
BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR s, int n, LPSIZE size) {
  return GetTextExtentExPointA(hdc, s, n, 0, NULL, NULL, size);
}
BOOL WINAPI GetTextExtentPointW(HDC hdc, LPCWSTR s, int n, LPSIZE size) {
  return GetTextExtentPoint32W(hdc, s, n, size);
}
BOOL WINAPI GetTextExtentPointA(HDC hdc, LPCSTR s, int n, LPSIZE size) {
  return GetTextExtentPoint32A(hdc, s, n, size);
}

namespace {

void FillMetrics(DCObj* dc, RealFont* f, TEXTMETRICW* tm) {
  memset(tm, 0, sizeof *tm);
  tm->tmHeight = dc->LogicalY(f->height);
  tm->tmAscent = dc->LogicalY(f->ascent);
  tm->tmDescent = tm->tmHeight - tm->tmAscent;
  tm->tmInternalLeading = dc->LogicalY(f->internalLeading);
  tm->tmExternalLeading = dc->LogicalY(f->externalLeading);
  tm->tmAveCharWidth = dc->LogicalX(f->avgWidth);
  tm->tmMaxCharWidth = dc->LogicalX(f->maxWidth);
  tm->tmWeight = f->weight;
  tm->tmOverhang = 0;
  tm->tmDigitizedAspectX = SystemDpi();
  tm->tmDigitizedAspectY = SystemDpi();
  tm->tmFirstChar = 0x20;
  tm->tmLastChar = 0xFFFC;
  tm->tmDefaultChar = 0x1F;
  tm->tmBreakChar = 0x20;
  tm->tmItalic = f->italic ? 255 : 0;
  tm->tmUnderlined = f->underline ? 255 : 0;
  tm->tmStruckOut = f->strikeOut ? 255 : 0;
  tm->tmPitchAndFamily = f->pitchAndFamily;
  tm->tmCharSet = f->charset;
}

void MetricsToA(const TEXTMETRICW& w, TEXTMETRICA* a) {
  memcpy(a, &w, offsetof(TEXTMETRICA, tmFirstChar));
  a->tmFirstChar = 0x1E;
  a->tmLastChar = 0xFF;
  a->tmDefaultChar = 0x1F;
  a->tmBreakChar = 0x20;
  a->tmItalic = w.tmItalic;
  a->tmUnderlined = w.tmUnderlined;
  a->tmStruckOut = w.tmStruckOut;
  a->tmPitchAndFamily = w.tmPitchAndFamily;
  a->tmCharSet = w.tmCharSet;
}

void LogFontToA(const LOGFONTW& w, LOGFONTA* a) {
  memcpy(a, &w, offsetof(LOGFONTA, lfFaceName));
  std::string face = Narrow(w.lfFaceName);
  memset(a->lfFaceName, 0, sizeof a->lfFaceName);
  strncpy(a->lfFaceName, face.c_str(), LF_FACESIZE - 1);
}

}  // namespace

BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW tm) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !tm) return FALSE;
  FillMetrics(dc, FontOf(dc), tm);
  return TRUE;
}

BOOL WINAPI GetTextMetricsA(HDC hdc, LPTEXTMETRICA tm) {
  TEXTMETRICW w;
  if (!tm || !GetTextMetricsW(hdc, &w)) return FALSE;
  MetricsToA(w, tm);
  return TRUE;
}

int WINAPI GetTextFaceW(HDC hdc, int n, LPWSTR out) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  const std::wstring& face = FontOf(dc)->face;
  if (!out) return (int)face.size() + 1;
  if (n <= 0) return 0;
  int len = std::min((int)face.size(), n - 1);
  wmemcpy(out, face.c_str(), (size_t)len);
  out[len] = 0;
  return len;
}

int WINAPI GetTextFaceA(HDC hdc, int n, LPSTR out) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  std::string face = ToAnsi(FontOf(dc)->face);
  if (!out) return (int)face.size() + 1;
  if (n <= 0) return 0;
  int len = std::min((int)face.size(), n - 1);
  memcpy(out, face.c_str(), (size_t)len);
  out[len] = 0;
  return len;
}

BOOL WINAPI GetCharWidth32W(HDC hdc, UINT first, UINT last, LPINT out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out || last < first) return FALSE;
  RealFont* f = FontOf(dc);
  for (UINT c = first; c <= last; c++) out[c - first] = dc->LogicalX(f->Advance(c));
  return TRUE;
}
BOOL WINAPI GetCharWidth32A(HDC hdc, UINT first, UINT last, LPINT out) {
  // Single bytes stand for the Latin-1 characters of the same number.
  return GetCharWidth32W(hdc, first & 0xFF, last & 0xFF, out);
}
BOOL WINAPI GetCharWidthW(HDC hdc, UINT first, UINT last, LPINT out) { return GetCharWidth32W(hdc, first, last, out); }
BOOL WINAPI GetCharWidthA(HDC hdc, UINT first, UINT last, LPINT out) { return GetCharWidth32A(hdc, first, last, out); }

BOOL WINAPI GetCharABCWidthsW(HDC hdc, UINT first, UINT last, LPABC out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out || last < first) return FALSE;
  RealFont* f = FontOf(dc);
  for (UINT c = first; c <= last; c++) {
    const GlyphImage& g = f->Glyph(c);
    ABC& abc = out[c - first];
    if (g.w > 0) {
      abc.abcA = g.left;
      abc.abcB = (UINT)g.w;
    } else {
      abc.abcA = 0;
      abc.abcB = 0;
    }
    abc.abcC = g.advance - abc.abcA - (int)abc.abcB;
    abc.abcA = abc.abcA < 0 ? -dc->LogicalX(-abc.abcA) : dc->LogicalX(abc.abcA);
    abc.abcB = (UINT)dc->LogicalX((int)abc.abcB);
    abc.abcC = abc.abcC < 0 ? -dc->LogicalX(-abc.abcC) : dc->LogicalX(abc.abcC);
  }
  return TRUE;
}
BOOL WINAPI GetCharABCWidthsA(HDC hdc, UINT first, UINT last, LPABC out) {
  return GetCharABCWidthsW(hdc, first & 0xFF, last & 0xFF, out);
}

// Kerning and raw font tables are not offered.
DWORD WINAPI GetKerningPairsW(HDC hdc, DWORD, LPKERNINGPAIR) { return DCOf(hdc) ? 0 : 0; }
DWORD WINAPI GetKerningPairsA(HDC hdc, DWORD n, LPKERNINGPAIR p) { return GetKerningPairsW(hdc, n, p); }

namespace {

// The outline metrics of the DC's font without the name strings, in
// logical units. False for the built-in bitmap font, which has no outline.
bool OutlineMetrics(HDC hdc, OUTLINETEXTMETRICW* m, RealFont** font) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return false;
  RealFont* f = FontOf(dc);
  if (!f->EmSquare()) return false;
  memset(m, 0, sizeof *m);
  FillMetrics(dc, f, &m->otmTextMetrics);
  const TEXTMETRICW& tm = m->otmTextMetrics;
  m->otmfsSelection = (f->italic ? 1u : 0u) | (f->weight >= FW_BOLD ? 0x20u : 0u);
  m->otmsCharSlopeRise = 1;
  m->otmItalicAngle = f->italic ? -120 : 0;  // tenths of a degree
  m->otmEMSquare = (UINT)f->EmSquare();
  m->otmAscent = tm.tmAscent;
  m->otmDescent = -tm.tmDescent;
  m->otmLineGap = (UINT)tm.tmExternalLeading;
  m->otmsCapEmHeight = (UINT)(tm.tmAscent - tm.tmInternalLeading);
  m->otmsXHeight = m->otmsCapEmHeight / 2;
  m->otmrcFontBox.left = 0;
  m->otmrcFontBox.top = tm.tmAscent;
  m->otmrcFontBox.right = tm.tmMaxCharWidth;
  m->otmrcFontBox.bottom = -tm.tmDescent;
  m->otmMacAscent = tm.tmAscent;
  m->otmMacDescent = -tm.tmDescent;
  m->otmMacLineGap = (UINT)tm.tmExternalLeading;
  m->otmusMinimumPPEM = 9;
  m->otmptSubscriptSize.x = m->otmptSuperscriptSize.x = tm.tmAveCharWidth * 2 / 3;
  m->otmptSubscriptSize.y = m->otmptSuperscriptSize.y = tm.tmHeight * 2 / 3;
  m->otmptSubscriptOffset.y = tm.tmDescent / 2;
  m->otmptSuperscriptOffset.y = tm.tmAscent / 2;
  m->otmsStrikeoutSize = (UINT)dc->LogicalY(f->strikeSize);
  m->otmsStrikeoutPosition = dc->LogicalY(f->strikePos);
  m->otmsUnderscoreSize = dc->LogicalY(f->underlineSize);
  m->otmsUnderscorePosition = -dc->LogicalY(f->underlinePos);
  *font = f;
  return true;
}

// 16.16 fixed point from a float.
FIXED ToFixed(double v) {
  LONG l = (LONG)floor(v * 65536.0 + 0.5);
  FIXED f;
  f.value = (short)(l >> 16);
  f.fract = (WORD)(l & 0xFFFF);
  return f;
}

// A float from 16.16 fixed point.
double FromFixed(FIXED f) { return f.value + f.fract / 65536.0; }

// Appends n bytes to a growing outline buffer.
void Put(std::vector<BYTE>* out, const void* p, size_t n) {
  out->insert(out->end(), (const BYTE*)p, (const BYTE*)p + n);
}

// The contours as GGO_NATIVE records: straight runs as TT_PRIM_LINE and
// curves as TT_PRIM_QSPLINE (control points, then the end point).
void NativeOutline(const std::vector<std::vector<OutlinePoint>>& contours, std::vector<BYTE>* out) {
  for (const auto& c : contours) {
    size_t start = out->size();
    TTPOLYGONHEADER h = {};
    h.dwType = TT_POLYGON_TYPE;
    h.pfxStart.x = ToFixed(c[0].x);
    h.pfxStart.y = ToFixed(c[0].y);
    Put(out, &h, sizeof h);
    // Visit the points after the start and back to the start, which
    // closes the contour.
    size_t n = c.size();
    size_t i = 1;
    while (i <= n) {
      bool line = c[i % n].onCurve;
      std::vector<POINTFX> pts;
      if (line) {
        while (i <= n && c[i % n].onCurve) {
          POINTFX p = {ToFixed(c[i % n].x), ToFixed(c[i % n].y)};
          pts.push_back(p);
          i++;
        }
      } else {
        while (i <= n) {
          const OutlinePoint& q = c[i % n];
          POINTFX p = {ToFixed(q.x), ToFixed(q.y)};
          pts.push_back(p);
          i++;
          if (q.onCurve) break;
        }
      }
      WORD type = line ? TT_PRIM_LINE : TT_PRIM_QSPLINE, count = (WORD)pts.size();
      Put(out, &type, sizeof type);
      Put(out, &count, sizeof count);
      Put(out, pts.data(), pts.size() * sizeof(POINTFX));
    }
    DWORD cb = (DWORD)(out->size() - start);
    memcpy(out->data() + start, &cb, sizeof cb);
  }
}


// The contours as GGO_BEZIER records: straight runs as TT_PRIM_LINE and
// each quadratic curve raised to a cubic one as TT_PRIM_CSPLINE (two
// control points, then the end point, for each curve).
void BezierOutline(const std::vector<std::vector<OutlinePoint>>& contours, std::vector<BYTE>* out) {
  for (const auto& c : contours) {
    size_t start = out->size();
    TTPOLYGONHEADER h = {};
    h.dwType = TT_POLYGON_TYPE;
    h.pfxStart.x = ToFixed(c[0].x);
    h.pfxStart.y = ToFixed(c[0].y);
    Put(out, &h, sizeof h);
    size_t n = c.size();
    size_t i = 1;
    double px = c[0].x, py = c[0].y;  // the last point on the curve
    while (i <= n) {
      bool line = c[i % n].onCurve;
      std::vector<POINTFX> pts;
      if (line) {
        while (i <= n && c[i % n].onCurve) {
          POINTFX p = {ToFixed(c[i % n].x), ToFixed(c[i % n].y)};
          pts.push_back(p);
          px = c[i % n].x;
          py = c[i % n].y;
          i++;
        }
      } else {
        // Each off-curve point is a quadratic curve, which ends on the
        // next point or halfway to the next off-curve point.
        while (i <= n && !c[i % n].onCurve) {
          const OutlinePoint& q = c[i % n];
          const OutlinePoint& next = c[(i + 1) % n];
          double ex = next.onCurve ? next.x : (q.x + next.x) / 2;
          double ey = next.onCurve ? next.y : (q.y + next.y) / 2;
          POINTFX c1 = {ToFixed(px + (q.x - px) * 2 / 3), ToFixed(py + (q.y - py) * 2 / 3)};
          POINTFX c2 = {ToFixed(ex + (q.x - ex) * 2 / 3), ToFixed(ey + (q.y - ey) * 2 / 3)};
          POINTFX e = {ToFixed(ex), ToFixed(ey)};
          pts.push_back(c1);
          pts.push_back(c2);
          pts.push_back(e);
          px = ex;
          py = ey;
          i++;
          if (next.onCurve) i++;
        }
      }
      WORD type = line ? TT_PRIM_LINE : TT_PRIM_CSPLINE, count = (WORD)pts.size();
      Put(out, &type, sizeof type);
      Put(out, &count, sizeof count);
      Put(out, pts.data(), pts.size() * sizeof(POINTFX));
    }
    DWORD cb = (DWORD)(out->size() - start);
    memcpy(out->data() + start, &cb, sizeof cb);
  }
}

// A glyph drawn for GetGlyphOutline: coverage 0 to 255 for each pixel,
// top row first, with its place: left and top from the pen position on
// the baseline, y up.
struct GlyphBits {
  int left = 0, top = 0, w = 0, h = 0;
  std::vector<BYTE> cov;
};

// Draws contours (y up) with 8 x 8 samples in each pixel, filled by the
// nonzero rule, as TrueType fills.
GlyphBits Rasterize(const std::vector<std::vector<OutlinePoint>>& contours) {
  struct Edge {
    double x0, y0, x1, y1;
  };
  std::vector<Edge> edges;
  double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  auto add = [&](double x0, double y0, double x1, double y1) {
    if (y0 != y1) edges.push_back({x0, y0, x1, y1});
    minX = std::min(minX, std::min(x0, x1));
    maxX = std::max(maxX, std::max(x0, x1));
    minY = std::min(minY, std::min(y0, y1));
    maxY = std::max(maxY, std::max(y0, y1));
  };
  for (const auto& c : contours) {
    size_t n = c.size();
    if (n < 2) continue;
    double px = c[0].x, py = c[0].y;
    for (size_t i = 1; i <= n; i++) {
      const OutlinePoint& q = c[i % n];
      if (q.onCurve) {
        add(px, py, q.x, q.y);
        px = q.x;
        py = q.y;
        continue;
      }
      const OutlinePoint& next = c[(i + 1) % n];
      double ex = next.onCurve ? next.x : (q.x + next.x) / 2;
      double ey = next.onCurve ? next.y : (q.y + next.y) / 2;
      const int kSteps = 16;
      for (int k = 1; k <= kSteps; k++) {
        double t = (double)k / kSteps, u = 1 - t;
        double x = u * u * px + 2 * u * t * q.x + t * t * ex;
        double y = u * u * py + 2 * u * t * q.y + t * t * ey;
        add(px, py, x, y);
        px = x;
        py = y;
      }
      if (next.onCurve) i++;
    }
  }
  GlyphBits b;
  if (edges.empty()) return b;
  b.left = (int)floor(minX);
  b.top = (int)ceil(maxY);
  b.w = std::max(1, (int)ceil(maxX) - b.left);
  b.h = std::max(1, b.top - (int)floor(minY));
  if ((size_t)b.w * b.h > 4096u * 4096u) return GlyphBits();
  const int kSub = 8;
  std::vector<int> counts((size_t)b.w * b.h, 0);
  std::vector<std::pair<double, int>> cross;
  for (int row = 0; row < b.h * kSub; row++) {
    // The sample's height, at the middle of its row of samples.
    double y = b.top - (row + 0.5) / kSub;
    cross.clear();
    for (const Edge& e : edges) {
      bool up = e.y1 > e.y0;
      double lo = up ? e.y0 : e.y1, hi = up ? e.y1 : e.y0;
      if (y < lo || y >= hi) continue;
      double x = e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
      cross.push_back({x, up ? 1 : -1});
    }
    std::sort(cross.begin(), cross.end());
    int wind = 0;
    for (size_t k = 0; k + 1 < cross.size(); k++) {
      wind += cross[k].second;
      if (!wind) continue;
      // The samples whose middles fall between the two crossings.
      int s0 = (int)ceil((cross[k].first - b.left) * kSub - 0.5);
      int s1 = (int)ceil((cross[k + 1].first - b.left) * kSub - 0.5);
      s0 = std::max(s0, 0);
      s1 = std::min(s1, b.w * kSub);
      int* line = counts.data() + (size_t)(row / kSub) * b.w;
      for (int sx = s0; sx < s1; sx++) line[sx / kSub]++;
    }
  }
  b.cov.resize(counts.size());
  for (size_t i = 0; i < counts.size(); i++) b.cov[i] = (BYTE)std::min(255, counts[i] * 255 / (kSub * kSub));
  return b;
}

// Writes a glyph as a GGO_BITMAP (one bit for each pixel) or a GGO_GRAY
// bitmap (a byte for each pixel, 0 to levels), rows on 4-byte bounds.
void GlyphBitmap(const GlyphBits& b, UINT kind, std::vector<BYTE>* out) {
  bool mono = kind == GGO_BITMAP;
  int levels = kind == GGO_GRAY2_BITMAP ? 4 : kind == GGO_GRAY4_BITMAP ? 16 : 64;
  size_t stride = mono ? (size_t)((b.w + 31) / 32) * 4 : (size_t)((b.w + 3) / 4) * 4;
  out->assign(stride * b.h, 0);
  for (int y = 0; y < b.h; y++) {
    BYTE* row = out->data() + stride * y;
    for (int x = 0; x < b.w; x++) {
      int c = b.cov[(size_t)y * b.w + x];
      if (mono) {
        if (c >= 128) row[x / 8] |= (BYTE)(0x80 >> (x % 8));
      } else {
        row[x] = (BYTE)((c * levels + 127) / 255);
      }
    }
  }
}

}  // namespace

// Fills the outline metrics of the font of hdc, with its names.
UINT WINAPI GetOutlineTextMetricsW(HDC hdc, UINT size, LPOUTLINETEXTMETRICW out) {
  OUTLINETEXTMETRICW m;
  RealFont* f;
  if (!OutlineMetrics(hdc, &m, &f)) return 0;
  // The names follow the structure, as offsets from its start.
  std::wstring style = f->weight >= FW_BOLD ? (f->italic ? L"Bold Italic" : L"Bold")
                                            : (f->italic ? L"Italic" : L"Regular");
  std::wstring full = f->family + L" " + style;
  const std::wstring* names[4] = {&f->family, &f->face, &style, &full};
  size_t total = sizeof m;
  PSTR* slots[4] = {&m.otmpFamilyName, &m.otmpFaceName, &m.otmpStyleName, &m.otmpFullName};
  for (int i = 0; i < 4; i++) {
    *slots[i] = (PSTR)(uintptr_t)total;
    total += (names[i]->size() + 1) * sizeof(WCHAR);
  }
  m.otmSize = (UINT)total;
  if (!out) return (UINT)total;
  if (size < sizeof m) return 0;
  memcpy(out, &m, sizeof m);
  for (int i = 0; i < 4; i++) {
    size_t at = (size_t)(uintptr_t)*slots[i];
    size_t bytes = (names[i]->size() + 1) * sizeof(WCHAR);
    if (at + bytes <= size) memcpy((BYTE*)out + at, names[i]->c_str(), bytes);
  }
  return (UINT)std::min<size_t>(total, size);
}

// The ANSI form of GetOutlineTextMetricsW.
UINT WINAPI GetOutlineTextMetricsA(HDC hdc, UINT size, LPOUTLINETEXTMETRICA out) {
  OUTLINETEXTMETRICW m;
  RealFont* f;
  if (!OutlineMetrics(hdc, &m, &f)) return 0;
  std::string family = ToAnsi(f->family), face = ToAnsi(f->face);
  std::string style = f->weight >= FW_BOLD ? (f->italic ? "Bold Italic" : "Bold") : (f->italic ? "Italic" : "Regular");
  std::string full = family + " " + style;
  const std::string* names[4] = {&family, &face, &style, &full};
  OUTLINETEXTMETRICA a;
  memset(&a, 0, sizeof a);
  // The fields after the text metrics are the same in both forms.
  MetricsToA(m.otmTextMetrics, &a.otmTextMetrics);
  memcpy(&a.otmFiller, &m.otmFiller, offsetof(OUTLINETEXTMETRICW, otmpFamilyName) - offsetof(OUTLINETEXTMETRICW, otmFiller));
  size_t total = sizeof a;
  PSTR* slots[4] = {&a.otmpFamilyName, &a.otmpFaceName, &a.otmpStyleName, &a.otmpFullName};
  for (int i = 0; i < 4; i++) {
    *slots[i] = (PSTR)(uintptr_t)total;
    total += names[i]->size() + 1;
  }
  a.otmSize = (UINT)total;
  if (!out) return (UINT)total;
  if (size < sizeof a) return 0;
  memcpy(out, &a, sizeof a);
  for (int i = 0; i < 4; i++) {
    size_t at = (size_t)(uintptr_t)*slots[i];
    if (at + names[i]->size() + 1 <= size) memcpy((BYTE*)out + at, names[i]->c_str(), names[i]->size() + 1);
  }
  return (UINT)std::min<size_t>(total, size);
}

DWORD WINAPI GetGlyphOutlineW(HDC hdc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buf,
                              const MAT2* mat) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !gm) return GDI_ERROR;
  RealFont* f = FontOf(dc);
  UINT kind = format & ~(UINT)(GGO_UNHINTED | GGO_GLYPH_INDEX);
  // Glyph indexes are not kept: the character code stands for itself.
  const GlyphImage& g = f->Glyph(ch);
  gm->gmBlackBoxX = (UINT)std::max(1, dc->LogicalX(g.w));
  gm->gmBlackBoxY = (UINT)std::max(1, dc->LogicalY(g.h));
  gm->gmptGlyphOrigin.x = g.left < 0 ? -dc->LogicalX(-g.left) : dc->LogicalX(g.left);
  gm->gmptGlyphOrigin.y = g.top < 0 ? dc->LogicalY(-g.top) : -dc->LogicalY(g.top);
  gm->gmCellIncX = (short)dc->LogicalX(g.advance);
  gm->gmCellIncY = 0;
  bool bitmap = kind == GGO_BITMAP || kind == GGO_GRAY2_BITMAP || kind == GGO_GRAY4_BITMAP ||
                kind == GGO_GRAY8_BITMAP;
  if (kind != GGO_METRICS && kind != GGO_NATIVE && kind != GGO_BEZIER && !bitmap) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return GDI_ERROR;
  }
  double m11 = 1, m12 = 0, m21 = 0, m22 = 1;
  if (mat) {
    m11 = FromFixed(mat->eM11);
    m12 = FromFixed(mat->eM12);
    m21 = FromFixed(mat->eM21);
    m22 = FromFixed(mat->eM22);
  }
  bool identity = m11 == 1 && m12 == 0 && m21 == 0 && m22 == 1;
  std::vector<std::vector<OutlinePoint>> contours;
  bool outline = f->Outline(ch, &contours);
  // Device pixels to logical units, then the caller's transformation.
  double ux = g.advance ? (double)dc->LogicalX(g.advance) / g.advance : 1.0;
  double uy = dc->LogicalY(1000) / 1000.0;
  if (outline)
    for (auto& c : contours)
      for (OutlinePoint& p : c) {
        double x = p.x * ux, y = p.y * uy;
        p.x = (float)(m11 * x + m21 * y);
        p.y = (float)(m12 * x + m22 * y);
      }

  // A transformed glyph: the box and the advance follow the
  // transformation, and the bitmap is drawn from the outline.
  GlyphBits bits;
  bool drawn = false;
  if (!identity && outline) {
    bits = Rasterize(contours);
    drawn = true;
    if (bits.w > 0) {
      gm->gmBlackBoxX = (UINT)bits.w;
      gm->gmBlackBoxY = (UINT)bits.h;
      gm->gmptGlyphOrigin.x = bits.left;
      gm->gmptGlyphOrigin.y = bits.top;
    }
    double adv = gm->gmCellIncX;
    gm->gmCellIncX = (short)floor(m11 * adv + 0.5);
    gm->gmCellIncY = (short)floor(m12 * adv + 0.5);
  }
  if (kind == GGO_METRICS) return 0;

  std::vector<BYTE> data;
  if (bitmap) {
    if (!drawn) {
      // The glyph as text draws it.
      bits.left = gm->gmptGlyphOrigin.x;
      bits.top = gm->gmptGlyphOrigin.y;
      bits.w = g.w;
      bits.h = g.h;
      bits.cov = g.cov;
      if (bits.cov.size() != (size_t)bits.w * bits.h) bits.w = bits.h = 0;
    }
    // A glyph with no ink (a space) has no bitmap.
    if (bits.w > 0 && bits.h > 0) GlyphBitmap(bits, kind, &data);
  } else {
    if (!outline) return GDI_ERROR;
    if (kind == GGO_BEZIER)
      BezierOutline(contours, &data);
    else
      NativeOutline(contours, &data);
  }
  if (!buf || !size) return (DWORD)data.size();
  if (size < data.size()) return GDI_ERROR;
  if (!data.empty()) memcpy(buf, data.data(), data.size());
  return (DWORD)data.size();
}

DWORD WINAPI GetGlyphOutlineA(HDC hdc, UINT ch, UINT format, LPGLYPHMETRICS gm, DWORD size, LPVOID buf,
                              const MAT2* mat) {
  // A single byte stands for the Latin-1 character of the same number.
  if (!(format & GGO_GLYPH_INDEX)) ch &= 0xFF;
  return GetGlyphOutlineW(hdc, ch, format, gm, size, buf, mat);
}
DWORD WINAPI GetFontData(HDC, DWORD, DWORD, LPVOID, DWORD) { return GDI_ERROR; }
DWORD WINAPI GetFontLanguageInfo(HDC hdc) { return 0; }

int WINAPI AddFontResourceA(LPCSTR path) { return path ? AddFontFile(path) : 0; }
int WINAPI AddFontResourceW(LPCWSTR path) { return path ? AddFontFile(Narrow(path).c_str()) : 0; }

// ---------------------------------------------------------------------
// Enumerating fonts

namespace {

// Calls back for each family (or, given a name, for that family alone).
template <class Fn>
int EnumCore(HDC hdc, const wchar_t* face, BYTE charset, Fn fn) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  if (charset != DEFAULT_CHARSET && charset != ANSI_CHARSET) return 1;
  int ret = 1;
  for (const FamilyInfo& fam : FontFamilies()) {
    if (face && *face && lstrcmpiW(face, fam.name.c_str()) != 0) continue;
    LOGFONTW lf = {};
    lf.lfHeight = 16;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfPitchAndFamily = (BYTE)((fam.fixedPitch ? FIXED_PITCH : VARIABLE_PITCH) | fam.family);
    lf.lfOutPrecision = OUT_STROKE_PRECIS;
    lf.lfClipPrecision = CLIP_STROKE_PRECIS;
    lf.lfQuality = DRAFT_QUALITY;
    wcsncpy(lf.lfFaceName, fam.name.c_str(), LF_FACESIZE - 1);
    RealFont* f = RealizeFont(lf);
    ENUMLOGFONTEXW elf = {};
    elf.elfLogFont = lf;
    elf.elfLogFont.lfHeight = f->height;
    elf.elfLogFont.lfWidth = f->avgWidth;
    wcsncpy(elf.elfFullName, fam.name.c_str(), LF_FULLFACESIZE - 1);
    wcscpy(elf.elfStyle, L"Regular");
    wcscpy(elf.elfScript, L"Western");
    NEWTEXTMETRICEXW ntm = {};
    TEXTMETRICW tm;
    // Metrics at this size, with no DC scaling.
    DCObj plain(kInfoDC);
    FillMetrics(&plain, f, &tm);
    memcpy(&ntm.ntmTm, &tm, sizeof tm);
    ntm.ntmTm.ntmFlags = NTM_REGULAR;
    ntm.ntmTm.ntmSizeEM = f->scalable ? 2048 : (UINT)(f->height - f->internalLeading);
    ntm.ntmTm.ntmCellHeight = (UINT)f->height;
    ntm.ntmTm.ntmAvgWidth = (UINT)f->avgWidth;
    ntm.ntmFontSig.fsCsb[0] = 1;  // Latin 1
    DWORD type = f->scalable ? TRUETYPE_FONTTYPE : RASTER_FONTTYPE;
    ret = fn(&elf, &ntm, type);
    if (!ret) break;
  }
  return ret;
}

}  // namespace

int WINAPI EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lf, FONTENUMPROCW proc, LPARAM data, DWORD) {
  if (!proc) return 0;
  return EnumCore(hdc, lf ? lf->lfFaceName : NULL, lf ? lf->lfCharSet : (BYTE)DEFAULT_CHARSET,
                  [&](ENUMLOGFONTEXW* elf, NEWTEXTMETRICEXW* ntm, DWORD type) {
                    return proc((const LOGFONTW*)elf, (const TEXTMETRICW*)ntm, type, data);
                  });
}

int WINAPI EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lf, FONTENUMPROCA proc, LPARAM data, DWORD) {
  if (!proc) return 0;
  std::wstring face = lf ? Widen(lf->lfFaceName) : std::wstring();
  return EnumCore(hdc, face.c_str(), lf ? lf->lfCharSet : (BYTE)DEFAULT_CHARSET,
                  [&](ENUMLOGFONTEXW* elf, NEWTEXTMETRICEXW* ntm, DWORD type) {
                    ENUMLOGFONTEXA a = {};
                    LogFontToA(elf->elfLogFont, &a.elfLogFont);
                    strncpy((char*)a.elfFullName, ToAnsi(elf->elfFullName).c_str(), LF_FULLFACESIZE - 1);
                    strcpy((char*)a.elfStyle, "Regular");
                    strcpy((char*)a.elfScript, "Western");
                    NEWTEXTMETRICEXA n = {};
                    MetricsToA(*(TEXTMETRICW*)&ntm->ntmTm, (TEXTMETRICA*)&n.ntmTm);
                    n.ntmTm.ntmFlags = ntm->ntmTm.ntmFlags;
                    n.ntmTm.ntmSizeEM = ntm->ntmTm.ntmSizeEM;
                    n.ntmTm.ntmCellHeight = ntm->ntmTm.ntmCellHeight;
                    n.ntmTm.ntmAvgWidth = ntm->ntmTm.ntmAvgWidth;
                    n.ntmFontSig = ntm->ntmFontSig;
                    return proc((const LOGFONTA*)&a, (const TEXTMETRICA*)&n, type, data);
                  });
}

int WINAPI EnumFontFamiliesW(HDC hdc, LPCWSTR face, FONTENUMPROCW proc, LPARAM data) {
  LOGFONTW lf = {};
  lf.lfCharSet = DEFAULT_CHARSET;
  if (face) wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
  return EnumFontFamiliesExW(hdc, &lf, proc, data, 0);
}

int WINAPI EnumFontFamiliesA(HDC hdc, LPCSTR face, FONTENUMPROCA proc, LPARAM data) {
  LOGFONTA lf = {};
  lf.lfCharSet = DEFAULT_CHARSET;
  if (face) strncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
  return EnumFontFamiliesExA(hdc, &lf, proc, data, 0);
}

int WINAPI EnumFontsW(HDC hdc, LPCWSTR face, FONTENUMPROCW proc, LPARAM data) {
  return EnumFontFamiliesW(hdc, face, proc, data);
}
int WINAPI EnumFontsA(HDC hdc, LPCSTR face, FONTENUMPROCA proc, LPARAM data) {
  return EnumFontFamiliesA(hdc, face, proc, data);
}

// ---------------------------------------------------------------------
// Tabbed text

namespace {

int AvgCharWidth(HDC hdc) {
  TEXTMETRICW tm;
  if (!GetTextMetricsW(hdc, &tm)) return 8;
  return std::max(1, (int)tm.tmAveCharWidth);
}

// The next tab stop after position pos (relative to the origin).
int NextTab(int pos, int nTabs, const INT* tabs, int defaultStep) {
  if (nTabs <= 0 || !tabs) return (pos / defaultStep + 1) * defaultStep;
  if (nTabs == 1) {
    int step = abs(tabs[0]) > 0 ? abs(tabs[0]) : defaultStep;
    return (pos / step + 1) * step;
  }
  for (int i = 0; i < nTabs; i++)
    if (abs(tabs[i]) > pos) return abs(tabs[i]);
  return (pos / defaultStep + 1) * defaultStep;
}

LONG TabbedCore(HDC hdc, int x, int y, const wchar_t* s, int n, int nTabs, const INT* tabs, int origin, bool draw) {
  if (!s || n < 0) return 0;
  SIZE sz = {0, 0};
  int defaultStep = 8 * AvgCharWidth(hdc);
  int pos = x;
  int height = 0;
  {
    SIZE h;
    if (GetTextExtentPoint32W(hdc, L"", 0, &h)) height = h.cy;
  }
  DCObj* dc = DCOf(hdc);
  bool opaque = dc && dc->s.bkMode == OPAQUE;
  int start = 0;
  for (int i = 0; i <= n; i++) {
    if (i < n && s[i] != L'\t') continue;
    int len = i - start;
    if (len > 0) {
      GetTextExtentPoint32W(hdc, s + start, len, &sz);
      if (draw) ExtTextOutW(hdc, pos, y, 0, NULL, s + start, (UINT)len, NULL);
      pos += sz.cx;
    }
    if (i < n) {
      int next = origin + NextTab(pos - origin, nTabs, tabs, defaultStep);
      if (draw && opaque) {
        RECT gap = {pos, y, next, y + height};
        ExtTextOutW(hdc, pos, y, ETO_OPAQUE, &gap, L"", 0, NULL);
      }
      pos = next;
    }
    start = i + 1;
  }
  return MAKELONG(pos - x, height);
}

}  // namespace

LONG WINAPI TabbedTextOutW(HDC hdc, int x, int y, LPCWSTR s, int n, int nTabs, const INT* tabs, int origin) {
  return TabbedCore(hdc, x, y, s, n, nTabs, tabs, origin, true);
}
LONG WINAPI TabbedTextOutA(HDC hdc, int x, int y, LPCSTR s, int n, int nTabs, const INT* tabs, int origin) {
  AnsiText t = FromAnsi(hdc, s, n);
  return TabbedCore(hdc, x, y, t.w.c_str(), (int)t.w.size(), nTabs, tabs, origin, true);
}
DWORD WINAPI GetTabbedTextExtentW(HDC hdc, LPCWSTR s, int n, int nTabs, const INT* tabs) {
  return (DWORD)TabbedCore(hdc, 0, 0, s, n, nTabs, tabs, 0, false);
}
DWORD WINAPI GetTabbedTextExtentA(HDC hdc, LPCSTR s, int n, int nTabs, const INT* tabs) {
  AnsiText t = FromAnsi(hdc, s, n);
  return (DWORD)TabbedCore(hdc, 0, 0, t.w.c_str(), (int)t.w.size(), nTabs, tabs, 0, false);
}

// ---------------------------------------------------------------------
// DrawText

namespace {

struct DrawTextJob {
  HDC hdc;
  UINT fmt;
  int tabWidth;
  int lineHeight;
  // The text with its prefixes taken out, and which characters to underline.
  std::wstring text;
  std::vector<bool> underline;

  int Width(const std::wstring& s) const {
    if (s.empty()) return 0;
    if (fmt & DT_EXPANDTABS) {
      int pos = 0;
      size_t start = 0;
      for (size_t i = 0; i <= s.size(); i++) {
        if (i < s.size() && s[i] != L'\t') continue;
        SIZE sz = {0, 0};
        if (i > start) GetTextExtentPoint32W(hdc, s.c_str() + start, (int)(i - start), &sz);
        pos += sz.cx;
        if (i < s.size()) pos = (pos / tabWidth + 1) * tabWidth;
        start = i + 1;
      }
      return pos;
    }
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz);
    return sz.cx;
  }
};

struct Line {
  size_t start;      // in the job's text
  std::wstring text;  // as drawn, possibly with an ellipsis
};

// Shortens a line that does not fit, by the ellipsis flags.
void Ellipsize(const DrawTextJob& job, std::wstring* s, int width) {
  if (job.Width(*s) <= width) return;
  const std::wstring dots = L"...";
  if (job.fmt & DT_PATH_ELLIPSIS) {
    size_t slash = s->find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0) {
      std::wstring head = s->substr(0, slash), tail = s->substr(slash);
      while (!head.empty()) {
        head.pop_back();
        std::wstring t = head + dots + tail;
        if (job.Width(t) <= width) {
          *s = t;
          return;
        }
      }
      *s = dots + tail;
      if (job.Width(*s) <= width) return;
    }
  }
  if (job.fmt & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS)) {
    std::wstring t = *s;
    while (!t.empty()) {
      t.pop_back();
      if (job.Width(t + dots) <= width) break;
    }
    *s = t + dots;
  }
}

int DrawTextCore(HDC hdc, std::wstring str, RECT* rc, UINT fmt, DRAWTEXTPARAMS* params, std::wstring* modified) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !rc) return 0;
  TEXTMETRICW tm;
  GetTextMetricsW(hdc, &tm);
  DrawTextJob job;
  job.hdc = hdc;
  job.fmt = fmt;
  int tabChars = 8;
  if (fmt & DT_TABSTOP) {
    tabChars = (fmt >> 8) & 0xFF;
    job.fmt &= ~0xFF00u;
    fmt = job.fmt;
  }
  if (params && params->iTabLength > 0) tabChars = params->iTabLength;
  job.tabWidth = std::max(1, tabChars * (int)tm.tmAveCharWidth);
  job.lineHeight = tm.tmHeight + ((fmt & DT_EXTERNALLEADING) ? tm.tmExternalLeading : 0);
  int lmargin = params ? params->iLeftMargin : 0, rmargin = params ? params->iRightMargin : 0;
  if (params) params->uiLengthDrawn = 0;

  RECT area = *rc;
  area.left += lmargin;
  area.right -= rmargin;
  int width = area.right - area.left;

  if (str.empty()) {
    if (fmt & DT_CALCRECT) {
      rc->right = rc->left;
      rc->bottom = rc->top + ((fmt & DT_SINGLELINE) ? job.lineHeight : 0);
    }
    return job.lineHeight;
  }

  // Prefixes
  bool prefixes = !(fmt & DT_NOPREFIX);
  for (size_t i = 0; i < str.size(); i++) {
    if (prefixes && str[i] == L'&') {
      if (i + 1 < str.size() && str[i + 1] == L'&') {
        job.text += L'&';
        job.underline.push_back(false);
        i++;
        continue;
      }
      if (i + 1 < str.size()) {
        job.text += str[i + 1];
        job.underline.push_back(!(fmt & DT_HIDEPREFIX));
        i++;
      }
      continue;
    }
    job.text += str[i];
    job.underline.push_back(false);
  }

  // Lines
  std::vector<Line> lines;
  const std::wstring& t = job.text;
  size_t i = 0;
  bool single = (fmt & DT_SINGLELINE) != 0;
  while (i <= t.size()) {
    size_t end = i;
    if (single) {
      end = t.size();
    } else {
      while (end < t.size() && t[end] != L'\r' && t[end] != L'\n') end++;
    }
    std::wstring seg = t.substr(i, end - i);
    if (single) {
      // Line breaks show as nothing on one line.
      for (wchar_t& c : seg)
        if (c == L'\r' || c == L'\n') c = L' ';
    }
    if ((fmt & DT_WORDBREAK) && !single) {
      // Break at spaces so that each piece fits.
      size_t pos = 0;
      do {
        size_t fit = seg.size() - pos;
        if (job.Width(seg.substr(pos)) > width) {
          size_t lastBreak = std::wstring::npos;
          size_t k = pos;
          for (; k < seg.size(); k++) {
            if (seg[k] == L' ' || seg[k] == L'\t') {
              if (job.Width(seg.substr(pos, k - pos)) > width) break;
              lastBreak = k;
            }
          }
          if (lastBreak != std::wstring::npos && lastBreak > pos) {
            fit = lastBreak - pos;
          } else if (fmt & DT_EDITCONTROL) {
            // Break inside the word, at least one character a line.
            size_t n = 1;
            while (pos + n < seg.size() && job.Width(seg.substr(pos, n + 1)) <= width) n++;
            fit = n;
          } else {
            // A word longer than the line overflows it.
            size_t k2 = pos;
            while (k2 < seg.size() && seg[k2] != L' ' && seg[k2] != L'\t') k2++;
            fit = k2 - pos;
          }
        }
        Line ln;
        ln.start = i + pos;
        ln.text = seg.substr(pos, fit);
        lines.push_back(ln);
        pos += fit;
        while (pos < seg.size() && seg[pos] == L' ') pos++;  // spaces at a break go
      } while (pos < seg.size());
      if (seg.empty()) lines.push_back(Line{i, std::wstring()});
    } else {
      lines.push_back(Line{i, seg});
    }
    if (end >= t.size()) break;
    // \r\n, \n\r, \r or \n ends a line.
    size_t next = end + 1;
    if (next < t.size() && ((t[end] == L'\r' && t[next] == L'\n') || (t[end] == L'\n' && t[next] == L'\r'))) next++;
    i = next;
    if (i == t.size()) break;
  }

  if (fmt & (DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS)) {
    for (Line& ln : lines) Ellipsize(job, &ln.text, width);
    if (modified && !lines.empty() && lines[0].text != t.substr(lines[0].start, lines[0].text.size()))
      *modified = lines[0].text;
  }

  int maxWidth = 0;
  for (const Line& ln : lines) maxWidth = std::max(maxWidth, job.Width(ln.text));
  int total = job.lineHeight * (int)lines.size();
  int y = area.top;
  if (single) {
    if (fmt & DT_VCENTER)
      y = area.top + (area.bottom - area.top - job.lineHeight) / 2;
    else if (fmt & DT_BOTTOM)
      y = area.bottom - job.lineHeight;
  }
  if (fmt & DT_CALCRECT) {
    rc->right = rc->left + lmargin + maxWidth + rmargin;
    rc->bottom = rc->top + total;
    if (params) params->uiLengthDrawn = (UINT)str.size();
    return total;
  }

  UINT oldAlign = SetTextAlign(hdc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
  UINT clipOpt = (fmt & DT_NOCLIP) ? 0 : ETO_CLIPPED;
  int startY = y;
  for (const Line& ln : lines) {
    int w = job.Width(ln.text);
    int x = area.left;
    if (fmt & DT_CENTER)
      x = area.left + (width - w) / 2;
    else if (fmt & DT_RIGHT)
      x = area.right - w;
    if (!(fmt & DT_PREFIXONLY)) {
      if (fmt & DT_EXPANDTABS) {
        int pos = 0;
        size_t start = 0;
        for (size_t k = 0; k <= ln.text.size(); k++) {
          if (k < ln.text.size() && ln.text[k] != L'\t') continue;
          if (k > start) {
            ExtTextOutW(hdc, x + pos, y, clipOpt, rc, ln.text.c_str() + start, (UINT)(k - start), NULL);
            pos += job.Width(ln.text.substr(start, k - start));
          }
          if (k < ln.text.size()) pos = (pos / job.tabWidth + 1) * job.tabWidth;
          start = k + 1;
        }
      } else {
        ExtTextOutW(hdc, x, y, clipOpt, rc, ln.text.c_str(), (UINT)ln.text.size(), NULL);
      }
    }
    // Underlines for prefixed characters.
    for (size_t k = 0; k < ln.text.size(); k++) {
      size_t at = ln.start + k;
      if (at >= job.underline.size() || !job.underline[at]) continue;
      if (ln.text[k] != t[at]) continue;  // cut off by an ellipsis
      int before = job.Width(ln.text.substr(0, k));
      int cw = job.Width(ln.text.substr(k, 1));
      int uy = y + tm.tmAscent + 1;
      HBRUSH b = CreateSolidBrush(GetTextColor(hdc));
      RECT u = {x + before, uy, x + before + cw, uy + 1};
      if (!(fmt & DT_NOCLIP)) {
        RECT clipped;
        if (!IntersectRect(&clipped, &u, rc)) clipped = RECT{0, 0, 0, 0};
        u = clipped;
      }
      FillRect(hdc, &u, b);
      DeleteObject(b);
    }
    y += job.lineHeight;
  }
  SetTextAlign(hdc, oldAlign);
  if (params) params->uiLengthDrawn = (UINT)str.size();
  if (single && (fmt & (DT_VCENTER | DT_BOTTOM))) return startY - area.top + total;
  return total;
}

}  // namespace

int WINAPI DrawTextExW(HDC hdc, LPWSTR s, int n, LPRECT rc, UINT fmt, LPDRAWTEXTPARAMS params) {
  if (!s && n != 0) return 0;
  if (n < 0) n = s ? (int)wcslen(s) : 0;
  std::wstring mod;
  int r = DrawTextCore(hdc, std::wstring(s ? s : L"", (size_t)n), rc, fmt, params,
                       (fmt & DT_MODIFYSTRING) ? &mod : NULL);
  if (!mod.empty() && s) {
    // The buffer has room for the text and four more characters.
    size_t room = (size_t)n + 4;
    size_t len = std::min(mod.size(), room - 1);
    wmemcpy(s, mod.c_str(), len);
    s[len] = 0;
  }
  return r;
}

int WINAPI DrawTextW(HDC hdc, LPCWSTR s, int n, LPRECT rc, UINT fmt) {
  return DrawTextExW(hdc, (LPWSTR)s, n, rc, fmt & ~DT_MODIFYSTRING, NULL);
}

int WINAPI DrawTextExA(HDC hdc, LPSTR s, int n, LPRECT rc, UINT fmt, LPDRAWTEXTPARAMS params) {
  if (!s && n != 0) return 0;
  if (n < 0) n = s ? (int)strlen(s) : 0;
  AnsiText t = FromAnsi(hdc, s, n);
  std::wstring mod;
  int r = DrawTextCore(hdc, t.w, rc, fmt, params, (fmt & DT_MODIFYSTRING) ? &mod : NULL);
  if (params) params->uiLengthDrawn = (UINT)n;
  if (!mod.empty() && s) {
    std::string a = ToAnsi(mod);
    size_t room = (size_t)n + 4;
    size_t len = std::min(a.size(), room - 1);
    memcpy(s, a.c_str(), len);
    s[len] = 0;
  }
  return r;
}

int WINAPI DrawTextA(HDC hdc, LPCSTR s, int n, LPRECT rc, UINT fmt) {
  return DrawTextExA(hdc, (LPSTR)s, n, rc, fmt & ~DT_MODIFYSTRING, NULL);
}

// ---------------------------------------------------------------------
// GrayString: the text, or what the callback draws, in the gray text
// color (or the brush's color).

namespace {

BOOL GrayCore(HDC hdc, HBRUSH brush, GRAYSTRINGPROC proc, LPARAM data, int n, int x, int y, int w, int h,
              bool unicode) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  COLORREF gray = GetSysColor(COLOR_GRAYTEXT);
  if (brush)
    if (BrushObj* b = GdiAs<BrushObj>(brush, OBJ_BRUSH)) gray = b->lb.lbColor;
  if (!proc) {
    if (unicode) {
      const wchar_t* s = (const wchar_t*)data;
      if (!s) return FALSE;
      if (n <= 0) n = (int)wcslen(s);
      COLORREF old = SetTextColor(hdc, gray);
      int mode = SetBkMode(hdc, TRANSPARENT);
      TextOutW(hdc, x, y, s, n);
      SetBkMode(hdc, mode);
      SetTextColor(hdc, old);
    } else {
      const char* s = (const char*)data;
      if (!s) return FALSE;
      if (n <= 0) n = (int)strlen(s);
      COLORREF old = SetTextColor(hdc, gray);
      int mode = SetBkMode(hdc, TRANSPARENT);
      TextOutA(hdc, x, y, s, n);
      SetBkMode(hdc, mode);
      SetTextColor(hdc, old);
    }
    return TRUE;
  }
  if (w <= 0 || h <= 0) return FALSE;
  // The callback draws black on white into a bitmap; its black pixels
  // become gray on the DC.
  HDC mem = CreateCompatibleDC(hdc);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  void* bits = NULL;
  HBITMAP bm = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bm || !bits) {
    DeleteDC(mem);
    return FALSE;
  }
  HGDIOBJ oldBm = SelectObject(mem, bm);
  HGDIOBJ oldFont = SelectObject(mem, GetCurrentObject(hdc, OBJ_FONT));
  RECT all = {0, 0, w, h};
  FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
  SetTextColor(mem, RGB(0, 0, 0));
  SetBkMode(mem, TRANSPARENT);
  BOOL ok = proc(mem, data, n);
  GdiFlush();
  const DWORD* px = (const DWORD*)bits;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      if ((px[(size_t)yy * w + xx] & 0xFFFFFF) != 0xFFFFFF) SetPixelV(hdc, x + xx, y + yy, gray);
  SelectObject(mem, oldFont);
  SelectObject(mem, oldBm);
  DeleteObject(bm);
  DeleteDC(mem);
  return ok;
}

}  // namespace

BOOL WINAPI GrayStringW(HDC hdc, HBRUSH brush, GRAYSTRINGPROC proc, LPARAM data, int n, int x, int y, int w, int h) {
  return GrayCore(hdc, brush, proc, data, n, x, y, w, h, true);
}
BOOL WINAPI GrayStringA(HDC hdc, HBRUSH brush, GRAYSTRINGPROC proc, LPARAM data, int n, int x, int y, int w, int h) {
  return GrayCore(hdc, brush, proc, data, n, x, y, w, h, false);
}

// ---------------------------------------------------------------------
// DrawState: an icon, bitmap, text or the callback's drawing, as it is
// or embossed (disabled), in one color (mono) or dithered (union).

namespace {

// Draws the image into dc at (0, 0).
BOOL StateDraw(HDC dc, DRAWSTATEPROC proc, LPARAM data, WPARAM wData, int w, int h, UINT kind, UINT flags) {
  switch (kind) {
    case DST_ICON:
      return DrawIconEx(dc, 0, 0, (HICON)data, w, h, 0, NULL, DI_NORMAL);
    case DST_BITMAP: {
      HDC src = CreateCompatibleDC(dc);
      HGDIOBJ old = SelectObject(src, (HBITMAP)data);
      BOOL ok = BitBlt(dc, 0, 0, w, h, src, 0, 0, SRCCOPY);
      SelectObject(src, old);
      DeleteDC(src);
      return ok;
    }
    case DST_TEXT:
    case DST_PREFIXTEXT: {
      const wchar_t* s = (const wchar_t*)data;
      int n = wData ? (int)wData : (int)wcslen(s);
      RECT r = {0, 0, w, h};
      UINT dt = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP;
      if (kind == DST_TEXT) dt |= DT_NOPREFIX;
      if (flags & DSS_HIDEPREFIX) dt |= DT_HIDEPREFIX;
      if (flags & DSS_PREFIXONLY) dt |= DT_PREFIXONLY;
      DrawTextW(dc, s, n, &r, dt);
      return TRUE;
    }
    default:
      return proc ? proc(dc, data, wData, w, h) : FALSE;
  }
}

BOOL StateCore(HDC hdc, HBRUSH brush, DRAWSTATEPROC proc, LPARAM data, WPARAM wData, int x, int y, int w, int h,
               UINT flags) {
  UINT kind = flags & 0x000F;
  if (!DCOf(hdc)) return FALSE;
  // The size, where the caller left it out.
  if (w <= 0 || h <= 0) {
    SIZE size = {0, 0};
    if (kind == DST_ICON) {
      ICONINFO ii;
      if (GetIconInfo((HICON)data, &ii)) {
        BITMAP bm;
        if (ii.hbmColor && GetObject(ii.hbmColor, sizeof bm, &bm)) {
          size.cx = bm.bmWidth;
          size.cy = bm.bmHeight;
        } else if (ii.hbmMask && GetObject(ii.hbmMask, sizeof bm, &bm)) {
          size.cx = bm.bmWidth;
          size.cy = bm.bmHeight / 2;
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
      }
    } else if (kind == DST_BITMAP) {
      BITMAP bm;
      if (GetObject((HBITMAP)data, sizeof bm, &bm)) {
        size.cx = bm.bmWidth;
        size.cy = bm.bmHeight;
      }
    } else if (kind == DST_TEXT || kind == DST_PREFIXTEXT) {
      const wchar_t* s = (const wchar_t*)data;
      if (!s) return FALSE;
      RECT r = {0, 0, 0, 0};
      DrawTextW(hdc, s, wData ? (int)wData : (int)wcslen(s), &r,
                DT_CALCRECT | DT_SINGLELINE | (kind == DST_TEXT ? DT_NOPREFIX : 0));
      size.cx = r.right;
      size.cy = r.bottom;
    }
    if (w <= 0) w = size.cx;
    if (h <= 0) h = size.cy;
  }
  if (w <= 0 || h <= 0) return FALSE;
  // Plain: straight onto the DC.
  if (!(flags & (DSS_DISABLED | DSS_MONO | DSS_UNION))) {
    if (kind == DST_TEXT || kind == DST_PREFIXTEXT || kind == DST_COMPLEX) {
      POINT org;
      SetViewportOrgEx(hdc, 0, 0, &org);
      SetViewportOrgEx(hdc, org.x + x, org.y + y, NULL);
      int mode = SetBkMode(hdc, TRANSPARENT);
      BOOL ok = StateDraw(hdc, proc, data, wData, w, h, kind, flags);
      SetBkMode(hdc, mode);
      SetViewportOrgEx(hdc, org.x, org.y, NULL);
      return ok;
    }
    if (kind == DST_ICON) return DrawIconEx(hdc, x, y, (HICON)data, w, h, 0, NULL, DI_NORMAL);
    HDC src = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(src, (HBITMAP)data);
    BOOL ok = BitBlt(hdc, x, y, w, h, src, 0, 0, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
    return ok;
  }
  // Otherwise the image's shape: what it draws over white and over black.
  HDC mem = CreateCompatibleDC(hdc);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  std::vector<DWORD> onWhite((size_t)w * h), onBlack((size_t)w * h);
  BOOL ok = TRUE;
  for (int pass = 0; pass < 2; pass++) {
    void* bits = NULL;
    HBITMAP bm = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm || !bits) {
      if (bm) DeleteObject(bm);
      DeleteDC(mem);
      return FALSE;
    }
    HGDIOBJ oldBm = SelectObject(mem, bm);
    HGDIOBJ oldFont = SelectObject(mem, GetCurrentObject(hdc, OBJ_FONT));
    RECT all = {0, 0, w, h};
    FillRect(mem, &all, (HBRUSH)GetStockObject(pass == 0 ? WHITE_BRUSH : BLACK_BRUSH));
    SetTextColor(mem, pass == 0 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    ok = StateDraw(mem, proc, data, wData, w, h, kind, flags);
    GdiFlush();
    memcpy(pass == 0 ? onWhite.data() : onBlack.data(), bits, (size_t)w * h * 4);
    SelectObject(mem, oldFont);
    SelectObject(mem, oldBm);
    DeleteObject(bm);
  }
  DeleteDC(mem);
  auto inShape = [&](int xx, int yy) {
    size_t i = (size_t)yy * w + xx;
    return (onWhite[i] & 0xFFFFFF) != 0xFFFFFF || (onBlack[i] & 0xFFFFFF) != 0;
  };
  auto dark = [&](int xx, int yy) {
    // Disabled: the image's darker pixels make the embossed shape (text and
    // line drawings are all of the shape).
    size_t i = (size_t)yy * w + xx;
    DWORD c = onWhite[i];
    int lum = (int)(((c >> 16) & 0xFF) * 30 + ((c >> 8) & 0xFF) * 59 + (c & 0xFF) * 11) / 100;
    return inShape(xx, yy) && lum < 192;
  };
  if (flags & DSS_DISABLED) {
    COLORREF hilite = GetSysColor(COLOR_BTNHIGHLIGHT), shadow = GetSysColor(COLOR_BTNSHADOW);
    for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++)
        if (dark(xx, yy)) SetPixelV(hdc, x + xx + 1, y + yy + 1, hilite);
    for (int yy = 0; yy < h; yy++)
      for (int xx = 0; xx < w; xx++)
        if (dark(xx, yy)) SetPixelV(hdc, x + xx, y + yy, shadow);
    return ok;
  }
  COLORREF mono = GetTextColor(hdc);
  if (brush)
    if (BrushObj* b = GdiAs<BrushObj>(brush, OBJ_BRUSH)) mono = b->lb.lbColor;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++) {
      if (!inShape(xx, yy)) continue;
      if ((flags & DSS_UNION) && ((xx + yy) & 1)) continue;  // every other pixel
      COLORREF c = (flags & DSS_MONO) ? mono : (COLORREF)(onWhite[(size_t)yy * w + xx] & 0xFFFFFF);
      if (!(flags & DSS_MONO)) c = RGB(GetBValue(c), GetGValue(c), GetRValue(c));  // the DIB is BGR
      SetPixelV(hdc, x + xx, y + yy, c);
    }
  return ok;
}

}  // namespace

BOOL WINAPI DrawStateW(HDC hdc, HBRUSH brush, DRAWSTATEPROC proc, LPARAM data, WPARAM wData, int x, int y, int w,
                       int h, UINT flags) {
  return StateCore(hdc, brush, proc, data, wData, x, y, w, h, flags);
}
BOOL WINAPI DrawStateA(HDC hdc, HBRUSH brush, DRAWSTATEPROC proc, LPARAM data, WPARAM wData, int x, int y, int w,
                       int h, UINT flags) {
  UINT kind = flags & 0x000F;
  if ((kind == DST_TEXT || kind == DST_PREFIXTEXT) && data) {
    const char* s = (const char*)data;
    AnsiText t = FromAnsi(hdc, s, wData ? (int)wData : (int)strlen(s));
    return StateCore(hdc, brush, proc, (LPARAM)t.w.c_str(), (WPARAM)t.w.size(), x, y, w, h, flags);
  }
  return StateCore(hdc, brush, proc, data, wData, x, y, w, h, flags);
}

// ---------------------------------------------------------------------
// Glyph indices and fractional widths
//
// A glyph index here is the UTF-16 code unit it draws (and ExtTextOut
// takes ETO_GLYPH_INDEX text as characters), so the *I functions measure
// the characters of those numbers.

DWORD WINAPI GetGlyphIndicesW(HDC hdc, LPCWSTR s, int n, LPWORD out, DWORD flags) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out || n < 0 || (n > 0 && !s)) return GDI_ERROR;
  RealFont* f = FontOf(dc);
  for (int i = 0; i < n; i++)
    out[i] = (flags & GGI_MARK_NONEXISTING_GLYPHS) && !f->HasGlyph(s[i]) ? 0xFFFF : (WORD)s[i];
  return (DWORD)n;
}
DWORD WINAPI GetGlyphIndicesA(HDC hdc, LPCSTR s, int n, LPWORD out, DWORD flags) {
  if (n < 0 || (n > 0 && !s)) return GDI_ERROR;
  AnsiText t = FromAnsi(hdc, s, n);
  return GetGlyphIndicesW(hdc, t.w.c_str(), (int)t.w.size(), out, flags);
}

namespace {
std::wstring FromIndices(const WORD* g, int n) {
  std::wstring w;
  for (int i = 0; i < n; i++) w += (wchar_t)g[i];
  return w;
}
}  // namespace

BOOL WINAPI GetTextExtentPointI(HDC hdc, LPWORD g, int n, LPSIZE size) {
  if (n < 0 || (n > 0 && !g)) return FALSE;
  std::wstring w = FromIndices(g, n);
  return GetTextExtentPoint32W(hdc, w.c_str(), n, size);
}
BOOL WINAPI GetTextExtentExPointI(HDC hdc, LPWORD g, int n, int maxExt, LPINT fit, LPINT dx, LPSIZE size) {
  if (n < 0 || (n > 0 && !g)) return FALSE;
  std::wstring w = FromIndices(g, n);
  return GetTextExtentExPointW(hdc, w.c_str(), n, maxExt, fit, dx, size);
}

// From first to last, or with g the n indices in it.
BOOL WINAPI GetCharWidthI(HDC hdc, UINT first, UINT n, LPWORD g, LPINT out) {
  if (!out) return FALSE;
  for (UINT i = 0; i < n; i++) {
    UINT c = g ? g[i] : first + i;
    if (!GetCharWidth32W(hdc, c, c, &out[i])) return FALSE;
  }
  return TRUE;
}
BOOL WINAPI GetCharABCWidthsI(HDC hdc, UINT first, UINT n, LPWORD g, LPABC out) {
  if (!out) return FALSE;
  for (UINT i = 0; i < n; i++) {
    UINT c = g ? g[i] : first + i;
    if (!GetCharABCWidthsW(hdc, c, c, &out[i])) return FALSE;
  }
  return TRUE;
}

// The fonts place glyphs on whole pixels, so the fractions are whole.
BOOL WINAPI GetCharWidthFloatW(HDC hdc, UINT first, UINT last, PFLOAT out) {
  if (!out || last < first) return FALSE;
  for (UINT c = first; c <= last; c++) {
    INT w;
    if (!GetCharWidth32W(hdc, c, c, &w)) return FALSE;
    out[c - first] = (FLOAT)w;
  }
  return TRUE;
}
BOOL WINAPI GetCharWidthFloatA(HDC hdc, UINT first, UINT last, PFLOAT out) {
  return GetCharWidthFloatW(hdc, first & 0xFF, last & 0xFF, out);
}
BOOL WINAPI GetCharABCWidthsFloatW(HDC hdc, UINT first, UINT last, LPABCFLOAT out) {
  if (!out || last < first) return FALSE;
  for (UINT c = first; c <= last; c++) {
    ABC abc;
    if (!GetCharABCWidthsW(hdc, c, c, &abc)) return FALSE;
    out[c - first].abcfA = (FLOAT)abc.abcA;
    out[c - first].abcfB = (FLOAT)abc.abcB;
    out[c - first].abcfC = (FLOAT)abc.abcC;
  }
  return TRUE;
}
BOOL WINAPI GetCharABCWidthsFloatA(HDC hdc, UINT first, UINT last, LPABCFLOAT out) {
  return GetCharABCWidthsFloatW(hdc, first & 0xFF, last & 0xFF, out);
}

// The runs of the Basic Multilingual Plane the font has glyphs for. With
// gs NULL, the size the answer needs.
DWORD WINAPI GetFontUnicodeRanges(HDC hdc, LPGLYPHSET gs) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  RealFont* f = FontOf(dc);
  std::vector<WCRANGE> runs;
  DWORD total = 0;
  for (UINT32 c = 0; c <= 0xFFFF; c++) {
    if (c >= 0xD800 && c <= 0xDFFF) continue;
    if (!f->HasGlyph(c)) continue;
    if (!runs.empty() && runs.back().wcLow + runs.back().cGlyphs == c && runs.back().cGlyphs < 0xFFFF) {
      runs.back().cGlyphs++;
    } else {
      runs.push_back(WCRANGE{(WCHAR)c, 1});
    }
    total++;
  }
  DWORD size = (DWORD)(offsetof(GLYPHSET, ranges) + sizeof(WCRANGE) * (runs.empty() ? 1 : runs.size()));
  if (!gs) return size;
  gs->cbThis = size;
  gs->flAccel = 0;
  gs->cGlyphsSupported = total;
  gs->cRanges = (DWORD)runs.size();
  for (size_t i = 0; i < runs.size(); i++) gs->ranges[i] = runs[i];
  return size;
}
