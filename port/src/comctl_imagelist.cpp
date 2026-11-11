// comctl_imagelist.cpp - image lists: same-sized images with masks or
// alpha, drawn plain, transparent, blended or with an overlay; dragging
// an image across windows; and saving a list to a stream.
//
// Each image is kept as 32-bit pixels whose alpha is its mask (0 where it
// is transparent), and is drawn through AlphaBlend.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <commctrl.h>
#include <objidl.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

using namespace winport;

struct ImageList : HIMAGELIST__ {
  int cx = 0, cy = 0;
  UINT flags = 0;
  int grow = 4;
  COLORREF bk = CLR_NONE;
  std::vector<std::vector<DWORD>> images;  // ARGB, not premultiplied
  int overlays[16];                        // overlay 1-15: an image index, or -1
  HBITMAP infoImage = NULL, infoMask = NULL;  // made for ImageList_GetImageInfo
  ImageList() {
    for (int& o : overlays) o = -1;
  }
  ~ImageList() {
    if (infoImage) DeleteObject(infoImage);
    if (infoMask) DeleteObject(infoMask);
  }
  bool Masked() const { return (flags & ILC_MASK) != 0; }
  int Count() const { return (int)images.size(); }
  bool Valid(int i) const { return i >= 0 && i < Count(); }
};

namespace {

std::mutex& ListLock() {
  static auto* m = new std::mutex;
  return *m;
}

std::set<HIMAGELIST>& Lists() {
  static auto* s = new std::set<HIMAGELIST>;
  return *s;
}

ImageList* Valid(HIMAGELIST h) {
  std::lock_guard<std::mutex> g(ListLock());
  return Lists().count(h) ? static_cast<ImageList*>(h) : NULL;
}

ImageList* NewList(int cx, int cy, UINT flags, int grow) {
  ImageList* h = new ImageList;
  h->cx = cx;
  h->cy = cy;
  h->flags = flags;
  h->grow = std::max(grow, 1);
  std::lock_guard<std::mutex> g(ListLock());
  Lists().insert(h);
  return h;
}

DWORD Argb(BYTE a, COLORREF c) { return ((DWORD)a << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c); }
COLORREF Rgb(DWORD p) { return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF); }

// A bitmap's pixels as 32-bit top-down rows; alpha only if it has any.
bool ReadBitmap(HBITMAP b, int* w, int* h, std::vector<DWORD>* px, bool* alpha) {
  BITMAP bm;
  if (!b || !GetObjectW(b, sizeof bm, &bm)) return false;
  *w = bm.bmWidth;
  *h = bm.bmHeight;
  px->assign((size_t)bm.bmWidth * bm.bmHeight, 0);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = bm.bmWidth;
  bi.bmiHeader.biHeight = -bm.bmHeight;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC dc = GetDC(NULL);
  int got = GetDIBits(dc, b, 0, bm.bmHeight, px->data(), &bi, DIB_RGB_COLORS);
  ReleaseDC(NULL, dc);
  if (!got) return false;
  *alpha = false;
  if (bm.bmBitsPixel == 32)
    for (DWORD p : *px)
      if (p >> 24) {
        *alpha = true;
        break;
      }
  return true;
}

// Cuts a strip of images out of a bitmap and its mask (white is transparent).
std::vector<std::vector<DWORD>> Cut(ImageList* il, HBITMAP image, HBITMAP mask, bool useColorKey, COLORREF key) {
  std::vector<std::vector<DWORD>> out;
  int w, h;
  std::vector<DWORD> px;
  bool alpha;
  if (!ReadBitmap(image, &w, &h, &px, &alpha) || il->cx <= 0) return out;
  std::vector<DWORD> mpx;
  int mw = 0, mh = 0;
  bool ma;
  bool hasMask = mask && ReadBitmap(mask, &mw, &mh, &mpx, &ma);
  int n = w / il->cx;
  for (int k = 0; k < n; k++) {
    std::vector<DWORD> img((size_t)il->cx * il->cy, 0);
    for (int y = 0; y < il->cy && y < h; y++)
      for (int x = 0; x < il->cx; x++) {
        int sx = k * il->cx + x;
        DWORD p = px[(size_t)y * w + sx];
        BYTE a = alpha ? (BYTE)(p >> 24) : 255;
        if (useColorKey && (p & 0xFFFFFF) == (Argb(0, key) & 0xFFFFFF)) {
          a = 0;
          p = 0;  // the masked part of the image turns black
        }
        if (hasMask && y < mh && sx < mw && (mpx[(size_t)y * mw + sx] & 0xFFFFFF)) a = 0;
        img[(size_t)y * il->cx + x] = (p & 0xFFFFFF) | ((DWORD)a << 24);
      }
    out.push_back(std::move(img));
  }
  return out;
}

// An icon as an image: drawn over black and over white, which gives its
// alpha whether it has a mask or an alpha channel.
std::vector<DWORD> FromIcon(ImageList* il, HICON icon) {
  int cx = il->cx, cy = il->cy;
  std::vector<DWORD> out((size_t)cx * cy, 0);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = cx;
  bi.bmiHeader.biHeight = -cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits[2];
  HDC mem = CreateCompatibleDC(NULL);
  HBITMAP bm[2];
  for (int i = 0; i < 2; i++) {
    bm[i] = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits[i], NULL, 0);
    if (!bm[i]) {
      DeleteDC(mem);
      return out;
    }
    HGDIOBJ old = SelectObject(mem, bm[i]);
    RECT r = {0, 0, cx, cy};
    FillRect(mem, &r, (HBRUSH)GetStockObject(i ? WHITE_BRUSH : BLACK_BRUSH));
    DrawIconEx(mem, 0, 0, icon, cx, cy, 0, NULL, DI_NORMAL);
    GdiFlush();
    SelectObject(mem, old);
  }
  const DWORD* onBlack = (const DWORD*)bits[0];
  const DWORD* onWhite = (const DWORD*)bits[1];
  for (int i = 0; i < cx * cy; i++) {
    DWORD b = onBlack[i], w = onWhite[i];
    int diff = (int)((w >> 8) & 0xFF) - (int)((b >> 8) & 0xFF);
    int a = 255 - std::max(0, diff);
    DWORD c = 0;
    if (a > 0) {
      auto un = [&](int shift) { return (DWORD)std::min(255, (int)((b >> shift) & 0xFF) * 255 / a) << shift; };
      c = un(16) | un(8) | un(0);
    }
    out[i] = c | ((DWORD)a << 24);
  }
  DeleteObject(bm[0]);
  DeleteObject(bm[1]);
  DeleteDC(mem);
  return out;
}

DWORD Mix(DWORD p, COLORREF with, int percent) {
  auto ch = [&](int shift, int w) {
    int a = (p >> shift) & 0xFF;
    return (DWORD)((a * (100 - percent) + w * percent) / 100) << shift;
  };
  return (p & 0xFF000000) | ch(16, GetRValue(with)) | ch(8, GetGValue(with)) | ch(0, GetBValue(with));
}

// Composes one image into premultiplied BGRA and blends it onto dc.
void Blit(ImageList* il, int i, HDC dc, int x, int y, int cx, int cy, int sx, int sy, COLORREF bk, COLORREF fg,
          UINT style, DWORD rop) {
  if (!il->Valid(i)) return;
  if (cx <= 0) cx = il->cx;
  if (cy <= 0) cy = il->cy;
  cx = std::min(cx, il->cx - sx);
  cy = std::min(cy, il->cy - sy);
  if (cx <= 0 || cy <= 0) return;
  if (bk == CLR_DEFAULT) bk = il->bk;
  if (fg == CLR_DEFAULT) fg = GetSysColor(COLOR_HIGHLIGHT);
  int blend = (style & ILD_BLEND50) ? 50 : (style & ILD_BLEND25) ? 25 : 0;
  if (fg == CLR_NONE) blend = 0;
  bool mask = (style & ILD_MASK) != 0;
  bool opaque = mask || (style & (ILD_IMAGE | ILD_ROP)) || (!(style & ILD_TRANSPARENT) && bk != CLR_NONE);
  const std::vector<DWORD>& img = il->images[i];
  bool useAlpha = il->Masked() || (il->flags & 0xFE) == ILC_COLOR32;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = cx;
  bi.bmiHeader.biHeight = -cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits;
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bm = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bm) {
    DeleteDC(mem);
    return;
  }
  DWORD* out = (DWORD*)bits;
  for (int yy = 0; yy < cy; yy++)
    for (int xx = 0; xx < cx; xx++) {
      DWORD p = img[(size_t)(yy + sy) * il->cx + xx + sx];
      int a = useAlpha ? (int)(p >> 24) : 255;
      if (style & (ILD_IMAGE | ILD_ROP)) a = 255;
      DWORD c;
      if (mask) {
        c = a ? 0 : 0xFFFFFF;
        a = 255;
      } else {
        if (blend && a) p = Mix(p, fg, blend);
        c = p & 0xFFFFFF;
        if (opaque && a < 255) {
          // Over the background color.
          DWORD back = Argb(0, bk);
          auto over = [&](int shift) {
            return (DWORD)((((c >> shift) & 0xFF) * a + ((back >> shift) & 0xFF) * (255 - a)) / 255) << shift;
          };
          c = over(16) | over(8) | over(0);
          a = 255;
        }
      }
      // Premultiplied for AlphaBlend.
      auto pm = [&](int shift) { return (DWORD)(((c >> shift) & 0xFF) * a / 255) << shift; };
      out[(size_t)yy * cx + xx] = ((DWORD)a << 24) | pm(16) | pm(8) | pm(0);
    }
  HGDIOBJ old = SelectObject(mem, bm);
  if (style & ILD_ROP) {
    BitBlt(dc, x, y, cx, cy, mem, 0, 0, rop ? rop : SRCCOPY);
  } else if (opaque) {
    BitBlt(dc, x, y, cx, cy, mem, 0, 0, SRCCOPY);
  } else {
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, cx, cy, mem, 0, 0, cx, cy, bf);
  }
  SelectObject(mem, old);
  DeleteObject(bm);
  DeleteDC(mem);
}

// Writes and reads a list's images as one bitmap in BMP form, four across.
const int kAcross = 4;

}  // namespace

extern "C" {

HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int initial, int grow) {
  if (cx <= 0 || cy <= 0) return NULL;
  ImageList* h = NewList(cx, cy, flags, grow);
  h->images.reserve(std::max(initial, 0));
  return h;
}

BOOL WINAPI ImageList_Destroy(HIMAGELIST h_){
  ImageList* h = static_cast<ImageList*>(h_);
  {
    std::lock_guard<std::mutex> g(ListLock());
    if (!Lists().erase(h_)) return FALSE;
  }
  delete h;
  return TRUE;
}

int WINAPI ImageList_GetImageCount(HIMAGELIST h) {
  ImageList* il = Valid(h);
  return il ? il->Count() : 0;
}

BOOL WINAPI ImageList_SetImageCount(HIMAGELIST h_, UINT n){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return FALSE;
  h->images.resize(n, std::vector<DWORD>((size_t)h->cx * h->cy, 0));
  return TRUE;
}

int WINAPI ImageList_Add(HIMAGELIST h_, HBITMAP image, HBITMAP mask){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return -1;
  auto add = Cut(h, image, h->Masked() ? mask : NULL, false, 0);
  if (add.empty()) return -1;
  int first = h->Count();
  for (auto& a : add) h->images.push_back(std::move(a));
  return first;
}

int WINAPI ImageList_AddMasked(HIMAGELIST h_, HBITMAP image, COLORREF key){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return -1;
  if (key == CLR_DEFAULT) {
    int w, hh;
    std::vector<DWORD> px;
    bool a;
    if (ReadBitmap(image, &w, &hh, &px, &a) && !px.empty()) key = Rgb(px[0]);
  }
  auto add = Cut(h, image, NULL, h->Masked() && key != CLR_NONE, key);
  if (add.empty()) return -1;
  int first = h->Count();
  for (auto& a : add) h->images.push_back(std::move(a));
  return first;
}

BOOL WINAPI ImageList_Replace(HIMAGELIST h_, int i, HBITMAP image, HBITMAP mask){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || !h->Valid(i)) return FALSE;
  auto add = Cut(h, image, h->Masked() ? mask : NULL, false, 0);
  if (add.empty()) return FALSE;
  h->images[i] = std::move(add[0]);
  return TRUE;
}

int WINAPI ImageList_ReplaceIcon(HIMAGELIST h_, int i, HICON icon){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || !icon) return -1;
  std::vector<DWORD> img = FromIcon(h, icon);
  if (i < 0) {
    h->images.push_back(std::move(img));
    return h->Count() - 1;
  }
  if (!h->Valid(i)) return -1;
  h->images[i] = std::move(img);
  return i;
}

BOOL WINAPI ImageList_Remove(HIMAGELIST h_, int i){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return FALSE;
  if (i == -1) {
    h->images.clear();
    return TRUE;
  }
  if (!h->Valid(i)) return FALSE;
  h->images.erase(h->images.begin() + i);
  for (int& o : h->overlays)
    if (o == i)
      o = -1;
    else if (o > i)
      o--;
  return TRUE;
}

COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST h_, COLORREF bk){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return CLR_NONE;
  COLORREF old = h->bk;
  h->bk = bk;
  return old;
}

COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST h) {
  ImageList* il = Valid(h);
  return il ? il->bk : CLR_NONE;
}

BOOL WINAPI ImageList_SetOverlayImage(HIMAGELIST h_, int image, int overlay){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || overlay < 1 || overlay > 15) return FALSE;
  h->overlays[overlay] = image;
  return TRUE;
}

BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS* p) {
  ImageList* h = p ? Valid(p->himl) : NULL;
  if (!h || !h->Valid(p->i)) return FALSE;
  Blit(h, p->i, p->hdcDst, p->x, p->y, p->cx, p->cy, p->xBitmap, p->yBitmap, p->rgbBk, p->rgbFg, p->fStyle,
       p->dwRop);
  int ov = (p->fStyle & ILD_OVERLAYMASK) >> 8;
  if (ov && !(p->fStyle & ILD_MASK) && h->Valid(h->overlays[ov]))
    Blit(h, h->overlays[ov], p->hdcDst, p->x, p->y, p->cx, p->cy, p->xBitmap, p->yBitmap, CLR_NONE, p->rgbFg,
         (p->fStyle & ~(ILD_OVERLAYMASK | ILD_IMAGE)) | ILD_TRANSPARENT, 0);
  return TRUE;
}

BOOL WINAPI ImageList_DrawEx(HIMAGELIST h_, int i, HDC dc, int x, int y, int dx, int dy, COLORREF bk, COLORREF fg,
                             UINT style){
  ImageList* h = static_cast<ImageList*>(h_);
  IMAGELISTDRAWPARAMS p = {};
  p.cbSize = sizeof p;
  p.himl = h;
  p.i = i;
  p.hdcDst = dc;
  p.x = x;
  p.y = y;
  p.cx = dx;
  p.cy = dy;
  p.rgbBk = bk;
  p.rgbFg = fg;
  p.fStyle = style;
  return ImageList_DrawIndirect(&p);
}

BOOL WINAPI ImageList_Draw(HIMAGELIST h_, int i, HDC dc, int x, int y, UINT style){
  ImageList* h = static_cast<ImageList*>(h_);
  return ImageList_DrawEx(h, i, dc, x, y, 0, 0, CLR_DEFAULT, CLR_DEFAULT, style);
}

HICON WINAPI ImageList_GetIcon(HIMAGELIST h_, int i, UINT style){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || !h->Valid(i)) return NULL;
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = h->cx;
  bi.bmiHeader.biHeight = -h->cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits;
  HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!color) return NULL;
  DWORD* px = (DWORD*)bits;
  std::vector<BYTE> mask((size_t)((h->cx + 15) / 16 * 2) * h->cy, 0);
  int stride = (h->cx + 15) / 16 * 2;
  int blend = (style & ILD_BLEND50) ? 50 : (style & ILD_BLEND25) ? 25 : 0;
  const std::vector<DWORD>& img = h->images[i];
  bool useAlpha = h->Masked() || (h->flags & 0xFE) == ILC_COLOR32;
  for (int y = 0; y < h->cy; y++)
    for (int x = 0; x < h->cx; x++) {
      DWORD p = img[(size_t)y * h->cx + x];
      int a = useAlpha ? (int)(p >> 24) : 255;
      if (blend && a) p = Mix(p, GetSysColor(COLOR_HIGHLIGHT), blend);
      if (a < 128) mask[(size_t)y * stride + x / 8] |= (BYTE)(0x80 >> (x % 8));
      px[(size_t)y * h->cx + x] = a ? ((p & 0xFFFFFF) | ((DWORD)a << 24)) : 0;
    }
  HBITMAP mono = CreateBitmap(h->cx, h->cy, 1, 1, mask.data());
  ICONINFO ii = {TRUE, 0, 0, mono, color};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mono);
  DeleteObject(color);
  return icon;
}

HIMAGELIST WINAPI ImageList_LoadImageW(HINSTANCE inst, LPCWSTR name, int cx, int grow, COLORREF key, UINT type,
                                       UINT flags) {
  if (type == IMAGE_BITMAP) {
    HBITMAP b = (HBITMAP)LoadImageW(inst, name, IMAGE_BITMAP, 0, 0, flags);
    if (!b) return NULL;
    BITMAP bm;
    GetObjectW(b, sizeof bm, &bm);
    if (cx <= 0) cx = bm.bmHeight;
    UINT ilc = (key != CLR_NONE ? ILC_MASK : 0) | (bm.bmBitsPixel >= 32 ? ILC_COLOR32 : ILC_COLOR24);
    ImageList* h = (ImageList*)ImageList_Create(cx, bm.bmHeight, ilc, bm.bmWidth / std::max(cx, 1), grow);
    if (key != CLR_NONE)
      ImageList_AddMasked(h, b, key);
    else
      ImageList_Add(h, b, NULL);
    DeleteObject(b);
    return h;
  }
  if (type == IMAGE_ICON || type == IMAGE_CURSOR) {
    HICON icon = (HICON)LoadImageW(inst, name, type, cx, cx, flags);
    if (!icon) return NULL;
    ICONINFO ii;
    int w = cx, hgt = cx;
    if (GetIconInfo(icon, &ii)) {
      BITMAP bm;
      if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof bm, &bm)) {
        w = bm.bmWidth;
        hgt = bm.bmHeight;
      } else if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof bm, &bm)) {
        w = bm.bmWidth;
        hgt = bm.bmHeight / 2;
      }
      if (ii.hbmColor) DeleteObject(ii.hbmColor);
      if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
    if (cx > 0) w = hgt = cx;
    ImageList* h = (ImageList*)ImageList_Create(w, hgt, ILC_MASK | ILC_COLOR32, 1, grow);
    ImageList_ReplaceIcon(h, -1, icon);
    DestroyIcon(icon);
    return h;
  }
  return NULL;
}

HIMAGELIST WINAPI ImageList_LoadImageA(HINSTANCE inst, LPCSTR name, int cx, int grow, COLORREF key, UINT type,
                                       UINT flags) {
  if (IS_INTRESOURCE(name)) return ImageList_LoadImageW(inst, (LPCWSTR)name, cx, grow, key, type, flags);
  std::wstring w = Widen(name);
  return ImageList_LoadImageW(inst, w.c_str(), cx, grow, key, type, flags);
}

BOOL WINAPI ImageList_Copy(HIMAGELIST dst_, int iDst, HIMAGELIST src_, int iSrc, UINT flags){
  ImageList* dst = static_cast<ImageList*>(dst_);
  ImageList* src = static_cast<ImageList*>(src_);
  if (!Valid(dst) || !Valid(src) || !dst->Valid(iDst) || !src->Valid(iSrc)) return FALSE;
  if (dst->cx != src->cx || dst->cy != src->cy) return FALSE;
  if ((flags & ILCF_SWAP) && dst == src)
    std::swap(dst->images[iDst], dst->images[iSrc]);
  else
    dst->images[iDst] = src->images[iSrc];
  return TRUE;
}

BOOL WINAPI ImageList_GetIconSize(HIMAGELIST h_, int* cx, int* cy){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h)) return FALSE;
  if (cx) *cx = h->cx;
  if (cy) *cy = h->cy;
  return TRUE;
}

BOOL WINAPI ImageList_SetIconSize(HIMAGELIST h_, int cx, int cy){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || cx <= 0 || cy <= 0) return FALSE;
  h->cx = cx;
  h->cy = cy;
  h->images.clear();
  return TRUE;
}

BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST h_, int i, IMAGEINFO* info){
  ImageList* h = static_cast<ImageList*>(h_);
  if (!Valid(h) || !h->Valid(i) || !info) return FALSE;
  // One strip of all the images, and its mask, made fresh.
  if (h->infoImage) DeleteObject(h->infoImage);
  if (h->infoMask) DeleteObject(h->infoMask);
  int n = h->Count(), w = h->cx * n;
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h->cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits;
  h->infoImage = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  int stride = (w + 15) / 16 * 2;
  std::vector<BYTE> mask((size_t)stride * h->cy, 0);
  if (h->infoImage)
    for (int k = 0; k < n; k++)
      for (int y = 0; y < h->cy; y++)
        for (int x = 0; x < h->cx; x++) {
          DWORD p = h->images[k][(size_t)y * h->cx + x];
          int sx = k * h->cx + x;
          ((DWORD*)bits)[(size_t)y * w + sx] = p & 0xFFFFFF;
          if (h->Masked() && (p >> 24) < 128) mask[(size_t)y * stride + sx / 8] |= (BYTE)(0x80 >> (sx % 8));
        }
  h->infoMask = h->Masked() ? CreateBitmap(w, h->cy, 1, 1, mask.data()) : NULL;
  info->hbmImage = h->infoImage;
  info->hbmMask = h->infoMask;
  info->Unused1 = info->Unused2 = 0;
  info->rcImage = {i * h->cx, 0, (i + 1) * h->cx, h->cy};
  return TRUE;
}

HIMAGELIST WINAPI ImageList_Merge(HIMAGELIST ha, int ia, HIMAGELIST hb, int ib, int dx, int dy) {
  ImageList *a = Valid(ha), *b = Valid(hb);
  if (!a || !b || !a->Valid(ia) || !b->Valid(ib)) return NULL;
  int left = std::min(0, dx), top = std::min(0, dy);
  int right = std::max(a->cx, dx + b->cx), bottom = std::max(a->cy, dy + b->cy);
  int cx = right - left, cy = bottom - top;
  ImageList* h = (ImageList*)ImageList_Create(cx, cy, ILC_MASK | ILC_COLOR32, 1, 1);
  std::vector<DWORD> img((size_t)cx * cy, 0);
  auto put = [&](ImageList* src, int i, int ox, int oy) {
    bool useAlpha = src->Masked() || (src->flags & 0xFE) == ILC_COLOR32;
    for (int y = 0; y < src->cy; y++)
      for (int x = 0; x < src->cx; x++) {
        DWORD p = src->images[i][(size_t)y * src->cx + x];
        if (!useAlpha) p |= 0xFF000000;
        int a = p >> 24;
        if (!a) continue;
        DWORD& d = img[(size_t)(y + oy) * cx + x + ox];
        int da = d >> 24;
        int oa = a + da * (255 - a) / 255;
        auto ch = [&](int s) {
          int v = oa ? (((p >> s) & 0xFF) * a + ((d >> s) & 0xFF) * da * (255 - a) / 255) / oa : 0;
          return (DWORD)std::min(v, 255) << s;
        };
        d = ((DWORD)oa << 24) | ch(16) | ch(8) | ch(0);
      }
  };
  put(a, ia, -left, -top);
  put(b, ib, dx - left, dy - top);
  h->images.push_back(std::move(img));
  return h;
}

HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST hsrc) {
  ImageList* src = Valid(hsrc);
  if (!src) return NULL;
  ImageList* h = NewList(src->cx, src->cy, src->flags, src->grow);
  h->bk = src->bk;
  h->images = src->images;
  memcpy(h->overlays, src->overlays, sizeof h->overlays);
  return h;
}

// ---------------------------------------------------------------------
// Streams: a small header, then the images (and the mask) as BMP files.

#pragma pack(push, 2)
struct ListHeader {
  WORD magic;  // "IL"
  WORD version;
  WORD count, max, grow;
  WORD cx, cy;
  DWORD bk;
  WORD flags;
  short overlays[4];
};
#pragma pack(pop)

static bool WriteAll(LPSTREAM s, const void* p, ULONG n) {
  ULONG done = 0;
  return SUCCEEDED(s->Write(p, n, &done)) && done == n;
}

static bool ReadAll(LPSTREAM s, void* p, ULONG n) {
  ULONG done = 0;
  return SUCCEEDED(s->Read(p, n, &done)) && done == n;
}

static bool WriteBmp(LPSTREAM s, int w, int h, int bpp, const void* bits, size_t size) {
  BITMAPFILEHEADER fh = {};
  BITMAPINFOHEADER ih = {};
  RGBQUAD mono[2] = {{0, 0, 0, 0}, {255, 255, 255, 0}};
  DWORD palette = bpp == 1 ? sizeof mono : 0;
  fh.bfType = 0x4D42;
  fh.bfOffBits = sizeof fh + sizeof ih + palette;
  fh.bfSize = (DWORD)(fh.bfOffBits + size);
  ih.biSize = sizeof ih;
  ih.biWidth = w;
  ih.biHeight = h;  // bottom-up, as BMP files are
  ih.biPlanes = 1;
  ih.biBitCount = (WORD)bpp;
  ih.biSizeImage = (DWORD)size;
  return WriteAll(s, &fh, sizeof fh) && WriteAll(s, &ih, sizeof ih) && (!palette || WriteAll(s, mono, palette)) &&
         WriteAll(s, bits, (ULONG)size);
}

// Reads a BMP into 32-bit top-down pixels.
static bool ReadBmp(LPSTREAM s, int* w, int* h, std::vector<DWORD>* px) {
  BITMAPFILEHEADER fh;
  if (!ReadAll(s, &fh, sizeof fh) || fh.bfType != 0x4D42 || fh.bfOffBits < sizeof fh) return false;
  std::vector<BYTE> rest(fh.bfSize > sizeof fh ? fh.bfSize - sizeof fh : 0);
  if (rest.size() < sizeof(BITMAPINFOHEADER) || !ReadAll(s, rest.data(), (ULONG)rest.size())) return false;
  BITMAPINFO* bi = (BITMAPINFO*)rest.data();
  *w = bi->bmiHeader.biWidth;
  *h = std::abs(bi->bmiHeader.biHeight);
  const BYTE* bits = rest.data() + (fh.bfOffBits - sizeof fh);
  if (bits > rest.data() + rest.size()) return false;
  HDC dc = GetDC(NULL);
  HBITMAP b = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT, bits, bi, DIB_RGB_COLORS);
  ReleaseDC(NULL, dc);
  if (!b) return false;
  bool alpha;
  int rw, rh;
  bool ok = ReadBitmap(b, &rw, &rh, px, &alpha);
  DeleteObject(b);
  return ok;
}

HIMAGELIST WINAPI ImageList_Read(LPSTREAM s) {
  ListHeader hd;
  if (!s || !ReadAll(s, &hd, sizeof hd) || hd.magic != 0x4C49 || !hd.cx || !hd.cy) return NULL;
  int w, h;
  std::vector<DWORD> px, mpx;
  if (!ReadBmp(s, &w, &h, &px)) return NULL;
  bool masked = (hd.flags & ILC_MASK) != 0;
  int mw = 0, mh = 0;
  if (masked && !ReadBmp(s, &mw, &mh, &mpx)) return NULL;
  ImageList* il = (ImageList*)ImageList_Create(hd.cx, hd.cy, hd.flags, hd.count, hd.grow);
  il->bk = hd.bk;
  for (int k = 0; k < hd.count; k++) {
    std::vector<DWORD> img((size_t)hd.cx * hd.cy);
    int ox = (k % kAcross) * hd.cx, oy = (k / kAcross) * hd.cy;
    for (int y = 0; y < hd.cy; y++)
      for (int x = 0; x < hd.cx; x++) {
        int sx = ox + x, sy = oy + y;
        DWORD p = (sx < w && sy < h) ? px[(size_t)sy * w + sx] : 0;
        bool clear = masked && sx < mw && sy < mh && (mpx[(size_t)sy * mw + sx] & 0xFFFFFF);
        // Alpha survives only in a 32-bit list; otherwise the mask says.
        if ((hd.flags & 0xFE) != ILC_COLOR32 || !(p >> 24)) p = (p & 0xFFFFFF) | 0xFF000000;
        if (clear) p &= 0xFFFFFF;
        img[(size_t)y * hd.cx + x] = p;
      }
    il->images.push_back(std::move(img));
  }
  for (int i = 0; i < 4; i++)
    if (hd.overlays[i] >= 0) il->overlays[i + 1] = hd.overlays[i];
  return il;
}

BOOL WINAPI ImageList_Write(HIMAGELIST il_, LPSTREAM s){
  ImageList* il = static_cast<ImageList*>(il_);
  if (!Valid(il) || !s) return FALSE;
  ListHeader hd = {};
  hd.magic = 0x4C49;
  hd.version = 0x101;
  hd.count = (WORD)il->Count();
  hd.max = (WORD)(il->Count() + il->grow);
  hd.grow = (WORD)il->grow;
  hd.cx = (WORD)il->cx;
  hd.cy = (WORD)il->cy;
  hd.bk = il->bk;
  hd.flags = (WORD)il->flags;
  for (int i = 0; i < 4; i++) hd.overlays[i] = (short)il->overlays[i + 1];
  if (!WriteAll(s, &hd, sizeof hd)) return FALSE;
  int rows = std::max(1, (il->Count() + kAcross - 1) / kAcross);
  int w = il->cx * kAcross, h = il->cy * rows;
  std::vector<DWORD> px((size_t)w * h, 0);
  int stride = (w + 31) / 32 * 4;
  std::vector<BYTE> mask((size_t)stride * h, 0xFF);
  for (int k = 0; k < il->Count(); k++)
    for (int y = 0; y < il->cy; y++)
      for (int x = 0; x < il->cx; x++) {
        DWORD p = il->images[k][(size_t)y * il->cx + x];
        int sx = (k % kAcross) * il->cx + x, sy = (k / kAcross) * il->cy + y;
        int row = h - 1 - sy;  // bottom-up
        px[(size_t)row * w + sx] = p;
        if ((p >> 24) >= 128) mask[(size_t)row * stride + sx / 8] &= (BYTE)~(0x80 >> (sx % 8));
      }
  if (!WriteBmp(s, w, h, 32, px.data(), px.size() * 4)) return FALSE;
  if (il->Masked() && !WriteBmp(s, w, h, 1, mask.data(), mask.size())) return FALSE;
  return TRUE;
}

}  // extern "C"

// ---------------------------------------------------------------------
// Dragging: one image at a time, drawn over a window with what it covers
// kept to put back.

namespace {

struct Drag {
  ImageList* image = NULL;  // the dragged image, merged with any cursor image
  POINT hot = {0, 0};
  HWND lock = NULL;
  POINT pos = {0, 0};  // in the lock window's coordinates, from its top left
  bool entered = false, shown = false;
  HBITMAP under = NULL;  // what the image covers
  POINT underAt = {0, 0};
};

Drag& TheDrag() {
  static auto* d = new Drag;
  return *d;
}

HDC DragDC(HWND lock) { return lock ? GetWindowDC(lock) : GetDC(NULL); }

void Hide(Drag& d) {
  if (!d.shown || !d.under) return;
  HDC dc = DragDC(d.lock);
  HDC mem = CreateCompatibleDC(dc);
  HGDIOBJ old = SelectObject(mem, d.under);
  BitBlt(dc, d.underAt.x, d.underAt.y, d.image->cx, d.image->cy, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(d.lock, dc);
  d.shown = false;
}

void Show(Drag& d) {
  if (d.shown || !d.image || !Valid(d.image)) return;
  HDC dc = DragDC(d.lock);
  int x = d.pos.x - d.hot.x, y = d.pos.y - d.hot.y;
  if (!d.under) d.under = CreateCompatibleBitmap(dc, d.image->cx, d.image->cy);
  HDC mem = CreateCompatibleDC(dc);
  HGDIOBJ old = SelectObject(mem, d.under);
  BitBlt(mem, 0, 0, d.image->cx, d.image->cy, dc, x, y, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  d.underAt = {x, y};
  ImageList_Draw(d.image, 0, dc, x, y, ILD_TRANSPARENT);
  ReleaseDC(d.lock, dc);
  d.shown = true;
}

}  // namespace

extern "C" {

BOOL WINAPI ImageList_BeginDrag(HIMAGELIST il_, int i, int dx, int dy){
  ImageList* il = static_cast<ImageList*>(il_);
  if (!Valid(il) || !il->Valid(i)) return FALSE;
  ImageList_EndDrag();
  Drag& d = TheDrag();
  d.image = (ImageList*)ImageList_Create(il->cx, il->cy, il->flags | ILC_MASK, 1, 1);
  d.image->images.push_back(il->images[i]);
  if (!il->Masked())
    for (DWORD& p : d.image->images[0]) p |= 0xFF000000;
  d.hot = {dx, dy};
  return TRUE;
}

void WINAPI ImageList_EndDrag(void) {
  Drag& d = TheDrag();
  Hide(d);
  if (d.image) ImageList_Destroy(d.image);
  if (d.under) DeleteObject(d.under);
  d = Drag();
}

BOOL WINAPI ImageList_DragEnter(HWND lock, int x, int y) {
  Drag& d = TheDrag();
  if (!d.image) return FALSE;
  Hide(d);
  d.lock = lock;
  d.pos = {x, y};
  d.entered = true;
  Show(d);
  return TRUE;
}

BOOL WINAPI ImageList_DragLeave(HWND lock) {
  (void)lock;
  Drag& d = TheDrag();
  Hide(d);
  d.entered = false;
  return TRUE;
}

BOOL WINAPI ImageList_DragMove(int x, int y) {
  Drag& d = TheDrag();
  if (!d.image) return FALSE;
  bool was = d.shown;
  Hide(d);
  d.pos = {x, y};
  if (was) Show(d);
  return TRUE;
}

BOOL WINAPI ImageList_DragShowNolock(BOOL show) {
  Drag& d = TheDrag();
  if (!d.image) return FALSE;
  if (show)
    Show(d);
  else
    Hide(d);
  return TRUE;
}

BOOL WINAPI ImageList_SetDragCursorImage(HIMAGELIST il_, int i, int dx, int dy){
  ImageList* il = static_cast<ImageList*>(il_);
  Drag& d = TheDrag();
  if (!d.image || !Valid(il) || !il->Valid(i)) return FALSE;
  bool was = d.shown;
  Hide(d);
  // The cursor's image goes over the dragged one, its hot spot at the drag's.
  ImageList* merged = (ImageList*)ImageList_Merge(d.image, 0, il, i, d.hot.x - dx, d.hot.y - dy);
  if (!merged) return FALSE;
  POINT shift = {std::max(0, dx - d.hot.x), std::max(0, dy - d.hot.y)};
  ImageList_Destroy(d.image);
  d.image = merged;
  d.hot.x += shift.x;
  d.hot.y += shift.y;
  if (d.under) {
    DeleteObject(d.under);
    d.under = NULL;
  }
  if (was) Show(d);
  return TRUE;
}

HIMAGELIST WINAPI ImageList_GetDragImage(POINT* pt, POINT* hot) {
  Drag& d = TheDrag();
  if (!d.image) return NULL;
  if (pt) *pt = d.pos;
  if (hot) *hot = d.hot;
  return d.image;
}

}  // extern "C"
