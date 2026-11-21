// user_layered.cpp - layered windows (SetLayeredWindowAttributes,
// UpdateLayeredWindow) and how a top-level window that is not a plain
// opaque rectangle shows: its pixels as premultiplied ARGB, with a
// layered window's color key and alpha and its window region applied,
// for the backends to show with transparency, and the parts of it that
// take the mouse.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>

#include <algorithm>

using namespace winport;

namespace {

DWORD RgbOf(COLORREF c) { return (DWORD)GetRValue(c) << 16 | (DWORD)GetGValue(c) << 8 | GetBValue(c); }

DWORD Scale(DWORD argb, UINT a) {
  auto m = [a](DWORD c) { return (c * a + 127) / 255; };
  return m(argb >> 24) << 24 | m((argb >> 16) & 0xFF) << 16 | m((argb >> 8) & 0xFF) << 8 | m(argb & 0xFF);
}

bool IsLayered(const Window* w) { return (w->exStyle & WS_EX_LAYERED) != 0; }

// One pixel of the top-level window's look, x and y within its surface.
DWORD Pixel(Window* t, int x, int y) {
  if (t->shaped && !t->shape.Contains(x, y)) return 0;
  if (!IsLayered(t)) return 0xFF000000u | (t->surface->Row(y)[x] & 0xFFFFFF);
  switch (t->layer) {
    case 1: {
      DWORD c = t->surface->Row(y)[x] & 0xFFFFFF;
      if ((t->layerFlags & LWA_COLORKEY) && c == RgbOf(t->layerKey)) return 0;
      DWORD p = 0xFF000000u | c;
      return (t->layerFlags & LWA_ALPHA) ? Scale(p, t->layerAlpha) : p;
    }
    case 2: {
      if (x >= t->layerWidth || y >= t->layerHeight) return 0;
      DWORD p = t->layerPixels[(size_t)y * t->layerWidth + x];
      if (t->layerFlags & ULW_OPAQUE) return 0xFF000000u | (p & 0xFFFFFF);
      if ((t->layerFlags & ULW_COLORKEY) && (p & 0xFFFFFF) == RgbOf(t->layerKey)) return 0;
      return (t->layerFlags & ULW_ALPHA) ? Scale(p, t->layerAlpha) : p;
    }
    default: return 0;  // not shown until it has its attributes or a picture
  }
}

// The rectangles of the surface where keep(x, y): spans in each row,
// rows with the same spans made into one.
template <typename Keep>
std::vector<RECT> Area(Window* t, Keep keep) {
  std::vector<RECT> out;
  Surface* s = t->surface;
  if (!s) return out;
  std::vector<std::pair<int, int>> prev, cur;
  size_t prevStart = 0;  // where prev's rectangles begin in out
  for (int y = 0; y < s->height; y++) {
    cur.clear();
    for (int x = 0; x < s->width;) {
      if (!keep(x, y)) {
        x++;
        continue;
      }
      int x0 = x;
      while (x < s->width && keep(x, y)) x++;
      cur.push_back({x0, x});
    }
    if (y > 0 && cur == prev) {
      for (size_t i = prevStart; i < out.size(); i++) out[i].bottom = y + 1;
    } else {
      prevStart = out.size();
      for (auto& sp : cur) out.push_back(RECT{sp.first, y, sp.second, y + 1});
      prev = cur;
    }
  }
  return out;
}

bool Layered(HWND h) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return false;
  }
  if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  return true;
}

// The source DC's pixels at p, w by h, as premultiplied ARGB: a 32-bit
// DIB section selected into it is read as it is, with its alpha when
// perPixel; anything else is copied out opaque.
std::vector<DWORD> SourcePixels(HDC src, POINT p, int w, int h, bool perPixel) {
  std::vector<DWORD> out((size_t)w * h, 0);
  DIBSECTION ds;
  HGDIOBJ bmp = GetCurrentObject(src, OBJ_BITMAP);
  if (bmp && GetObjectW(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmBits &&
      ds.dsBmih.biCompression == BI_RGB) {
    int bw = ds.dsBm.bmWidth, bh = ds.dsBm.bmHeight;
    bool topDown = ds.dsBmih.biHeight < 0;
    for (int y = 0; y < h; y++) {
      int sy = p.y + y;
      if (sy < 0 || sy >= bh) continue;
      const DWORD* row = (const DWORD*)((const BYTE*)ds.dsBm.bmBits +
                                        (size_t)(topDown ? sy : bh - 1 - sy) * ds.dsBm.bmWidthBytes);
      for (int x = 0; x < w; x++) {
        int sx = p.x + x;
        if (sx < 0 || sx >= bw) continue;
        out[(size_t)y * w + x] = perPixel ? row[sx] : 0xFF000000u | (row[sx] & 0xFFFFFF);
      }
    }
    return out;
  }
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = NULL;
  HBITMAP dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!dib) return out;
  HDC mem = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(mem, dib);
  BitBlt(mem, 0, 0, w, h, src, p.x, p.y, SRCCOPY);
  GdiFlush();
  const DWORD* px = (const DWORD*)bits;
  for (size_t i = 0; i < out.size(); i++) out[i] = 0xFF000000u | (px[i] & 0xFFFFFF);
  SelectObject(mem, old);
  DeleteDC(mem);
  DeleteObject(dib);
  return out;
}

}  // namespace

namespace winport {

bool Translucent(Window* t) { return t->shaped || IsLayered(t); }

void Composite(Window* t, const RECT& r, DWORD* out, int stride) {
  UserGuard g(UserLock());
  Surface* s = t->surface;
  for (int y = r.top; y < r.bottom; y++)
    for (int x = r.left; x < r.right; x++)
      out[(size_t)(y - r.top) * stride + (x - r.left)] =
          s && x >= 0 && y >= 0 && x < s->width && y < s->height ? Pixel(t, x, y) : 0;
}

bool TakesMouse(Window* t, int x, int y) {
  UserGuard g(UserLock());
  if (IsLayered(t) && (t->exStyle & WS_EX_TRANSPARENT)) return false;
  if (!Translucent(t)) return true;
  Surface* s = t->surface;
  if (!s || x < 0 || y < 0 || x >= s->width || y >= s->height) return !IsLayered(t) && (!t->shaped || t->shape.Contains(x, y));
  return (Pixel(t, x, y) >> 24) != 0;
}

std::vector<RECT> ShownArea(Window* t) {
  UserGuard g(UserLock());
  return Area(t, [t](int x, int y) { return (Pixel(t, x, y) >> 24) != 0; });
}

std::vector<RECT> MouseArea(Window* t) {
  UserGuard g(UserLock());
  if (IsLayered(t) && (t->exStyle & WS_EX_TRANSPARENT)) return {};
  return ShownArea(t);
}

void TranslucencyChanged(Window* t) {
  t = t->TopLevel();
  if (!t->native) return;
  GetBackend()->TranslucencyChanged(t);
  if (t->surface) SurfaceChanged(t, RECT{0, 0, t->surface->width, t->surface->height});
}

}  // namespace winport

extern "C" {

BOOL WINAPI SetLayeredWindowAttributes(HWND h, COLORREF key, BYTE alpha, DWORD flags) {
  if (!Layered(h)) return FALSE;
  Window* w = GetWindow(h);
  {
    UserGuard g(UserLock());
    w->layer = 1;
    w->layerKey = key;
    w->layerAlpha = alpha;
    w->layerFlags = flags & (LWA_COLORKEY | LWA_ALPHA);
    w->layerPixels.clear();
    w->layerWidth = w->layerHeight = 0;
  }
  TranslucencyChanged(w);
  return TRUE;
}

BOOL WINAPI GetLayeredWindowAttributes(HWND h, COLORREF* key, BYTE* alpha, DWORD* flags) {
  if (!Layered(h)) return FALSE;
  Window* w = GetWindow(h);
  UserGuard g(UserLock());
  // Only after SetLayeredWindowAttributes, not UpdateLayeredWindow.
  if (w->layer != 1) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (key) *key = w->layerKey;
  if (alpha) *alpha = w->layerAlpha;
  if (flags) *flags = w->layerFlags;
  return TRUE;
}

// The window takes the place and size given and shows the source DC's
// picture, blended as blend says: with its own alpha (premultiplied, as
// AlphaBlend takes it) for AC_SRC_ALPHA, and the constant alpha over
// that. Without a source DC the last picture stays, blended anew. Once
// SetLayeredWindowAttributes has been called it fails, until
// WS_EX_LAYERED is cleared and set again.
BOOL WINAPI UpdateLayeredWindow(HWND h, HDC dst, POINT* pos, SIZE* size, HDC src, POINT* srcPos, COLORREF key,
                                BLENDFUNCTION* blend, DWORD flags) {
  (void)dst;
  if (!Layered(h)) return FALSE;
  Window* w = GetWindow(h);
  if (w->layer == 1 || (size && (size->cx < 0 || size->cy < 0)) ||
      ((flags & ULW_ALPHA) && !blend)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  RECT r;
  GetWindowRect(h, &r);
  UINT swp = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
  if (!pos) swp |= SWP_NOMOVE;
  if (!size || (flags & ULW_EX_NORESIZE)) swp |= SWP_NOSIZE;
  int cx = size && !(flags & ULW_EX_NORESIZE) ? size->cx : r.right - r.left;
  int cy = size && !(flags & ULW_EX_NORESIZE) ? size->cy : r.bottom - r.top;
  if (!(swp & SWP_NOMOVE) || !(swp & SWP_NOSIZE))
    SetWindowPos(h, NULL, pos ? pos->x : r.left, pos ? pos->y : r.top, cx, cy, swp);
  std::vector<DWORD> pixels;
  if (src) {
    POINT sp = srcPos ? *srcPos : POINT{0, 0};
    bool perPixel = blend && (blend->AlphaFormat & AC_SRC_ALPHA) && (flags & ULW_ALPHA);
    pixels = SourcePixels(src, sp, cx, cy, perPixel);
  }
  {
    UserGuard g(UserLock());
    w->layer = 2;
    w->layerKey = key;
    w->layerAlpha = blend ? blend->SourceConstantAlpha : 255;
    w->layerFlags = flags & (ULW_COLORKEY | ULW_ALPHA | ULW_OPAQUE);
    if (src) {
      w->layerPixels.swap(pixels);
      w->layerWidth = cx;
      w->layerHeight = cy;
    }
  }
  TranslucencyChanged(w);
  return TRUE;
}

BOOL WINAPI UpdateLayeredWindowIndirect(HWND h, const UPDATELAYEREDWINDOWINFO* info) {
  if (!info || info->cbSize != sizeof *info) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // The dirty rectangle only saves work on Windows; the whole picture is
  // taken here.
  return UpdateLayeredWindow(h, info->hdcDst, (POINT*)info->pptDst, (SIZE*)info->psize, info->hdcSrc,
                             (POINT*)info->pptSrc, info->crKey, (BLENDFUNCTION*)info->pblend, info->dwFlags);
}

}  // extern "C"
