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

AnsiText FromAnsi(const char* s, int n) {
  AnsiText t;
  if (!s) n = 0;
  if (n < 0) n = (int)strlen(s);
  const BYTE* p = (const BYTE*)s;
  int i = 0;
  while (i < n) {
    t.byteAt.push_back(i);
    BYTE c = p[i];
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
  AnsiText t = FromAnsi(s, (int)n);
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
  AnsiText t = FromAnsi(s, n);
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
  tm->tmDigitizedAspectX = kLogPixels;
  tm->tmDigitizedAspectY = kLogPixels;
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

// Outlines, kerning and raw font tables are not offered.
DWORD WINAPI GetKerningPairsW(HDC hdc, DWORD, LPKERNINGPAIR) { return DCOf(hdc) ? 0 : 0; }
DWORD WINAPI GetKerningPairsA(HDC hdc, DWORD n, LPKERNINGPAIR p) { return GetKerningPairsW(hdc, n, p); }
UINT WINAPI GetOutlineTextMetricsW(HDC, UINT, LPOUTLINETEXTMETRICW) { return 0; }
UINT WINAPI GetOutlineTextMetricsA(HDC, UINT, LPOUTLINETEXTMETRICA) { return 0; }
DWORD WINAPI GetGlyphOutlineW(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*) { return GDI_ERROR; }
DWORD WINAPI GetGlyphOutlineA(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*) { return GDI_ERROR; }
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
  AnsiText t = FromAnsi(s, n);
  return TabbedCore(hdc, x, y, t.w.c_str(), (int)t.w.size(), nTabs, tabs, origin, true);
}
DWORD WINAPI GetTabbedTextExtentW(HDC hdc, LPCWSTR s, int n, int nTabs, const INT* tabs) {
  return (DWORD)TabbedCore(hdc, 0, 0, s, n, nTabs, tabs, 0, false);
}
DWORD WINAPI GetTabbedTextExtentA(HDC hdc, LPCSTR s, int n, int nTabs, const INT* tabs) {
  AnsiText t = FromAnsi(s, n);
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
  AnsiText t = FromAnsi(s, n);
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
