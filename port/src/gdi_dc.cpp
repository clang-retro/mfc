// gdi_dc.cpp - device contexts: creation, the selected objects and
// attributes, mapping modes, clipping, SaveDC and RestoreDC, and the
// device's capabilities.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>

using namespace winport;

namespace winport {

SIZE ScreenSize() { return GetBackend()->ScreenSize(); }

DCObj* DCOf(HDC h) { return GdiAs<DCObj>(h, OBJ_DC); }

void InitDC(DCObj* dc) {
  dc->s = DCState();
  dc->s.pen = GetStockObject(BLACK_PEN);
  dc->s.brush = GetStockObject(WHITE_BRUSH);
  dc->s.font = GetStockObject(SYSTEM_FONT);
  dc->s.palette = GetStockObject(DEFAULT_PALETTE);
  if (dc->kind == kMemoryDC) {
    dc->s.bitmap = StockBitmap();
    Ref(dc->s.bitmap);
  }
  Ref(dc->s.pen);
  Ref(dc->s.brush);
  Ref(dc->s.font);
  Ref(dc->s.palette);
}

namespace {

void UnrefState(DCState& st) {
  Unref(st.pen);
  Unref(st.brush);
  Unref(st.font);
  Unref(st.palette);
  if (st.bitmap) Unref(st.bitmap);
}

void RefState(DCState& st) {
  Ref(st.pen);
  Ref(st.brush);
  Ref(st.font);
  Ref(st.palette);
  if (st.bitmap) Ref(st.bitmap);
}

LONG Scale(LONG v, LONG num, LONG den) {
  if (num == den) return v;
  if (den == 0) return 0;
  long long r = (long long)v * num;
  // Round half away from zero, as MulDiv does.
  long long h = den / 2 * (den < 0 ? -1 : 1);
  r = r >= 0 ? (r + (den > 0 ? h : -h)) / den : (r - (den > 0 ? h : -h)) / den;
  return (LONG)r;
}

// Isotropic mode keeps a logical unit the same size both ways by
// shrinking the larger viewport extent.
void FixIsotropic(DCState& s) {
  if (s.mapMode != MM_ISOTROPIC || s.windowExt.cx == 0 || s.windowExt.cy == 0) return;
  long long xs = (long long)s.viewportExt.cx * 1000 / s.windowExt.cx;
  long long ys = (long long)s.viewportExt.cy * 1000 / s.windowExt.cy;
  long long ax = xs < 0 ? -xs : xs, ay = ys < 0 ? -ys : ys;
  if (ax > ay)
    s.viewportExt.cx = (LONG)(((long long)s.viewportExt.cx * ay / (ax ? ax : 1)));
  else if (ay > ax)
    s.viewportExt.cy = (LONG)(((long long)s.viewportExt.cy * ax / (ay ? ay : 1)));
}

}  // namespace

void FreeDC(DCObj* dc) {
  UnrefState(dc->s);
  for (DCState& st : dc->saved) UnrefState(st);
  dc->saved.clear();
  if (dc->kind == kMemoryDC)
    if (BitmapObj* b = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP))
      if (b->selectedIn == dc) b->selectedIn = NULL;
  FreeGdiHandle(HandleOf(dc));
}

POINT DCObj::ToDevice(LONG x, LONG y) const {
  if (s.graphicsMode == GM_ADVANCED) {
    const XFORM& m = s.xform;
    double fx = x * m.eM11 + y * m.eM21 + m.eDx, fy = x * m.eM12 + y * m.eM22 + m.eDy;
    x = (LONG)(fx < 0 ? fx - 0.5 : fx + 0.5);
    y = (LONG)(fy < 0 ? fy - 0.5 : fy + 0.5);
  }
  POINT p = {Scale(x - s.windowOrg.x, s.viewportExt.cx, s.windowExt.cx) + s.viewportOrg.x,
             Scale(y - s.windowOrg.y, s.viewportExt.cy, s.windowExt.cy) + s.viewportOrg.y};
  return p;
}

POINT DCObj::ToLogical(LONG x, LONG y) const {
  POINT p = {Scale(x - s.viewportOrg.x, s.windowExt.cx, s.viewportExt.cx) + s.windowOrg.x,
             Scale(y - s.viewportOrg.y, s.windowExt.cy, s.viewportExt.cy) + s.windowOrg.y};
  if (s.graphicsMode == GM_ADVANCED) {
    const XFORM& m = s.xform;
    double det = m.eM11 * m.eM22 - m.eM12 * m.eM21;
    if (det != 0) {
      double dx = p.x - m.eDx, dy = p.y - m.eDy;
      double fx = (dx * m.eM22 - dy * m.eM21) / det, fy = (dy * m.eM11 - dx * m.eM12) / det;
      p.x = (LONG)(fx < 0 ? fx - 0.5 : fx + 0.5);
      p.y = (LONG)(fy < 0 ? fy - 0.5 : fy + 0.5);
    }
  }
  return p;
}

RECT DCObj::ToDevice(const RECT& r) const {
  POINT a = ToDevice(r.left, r.top), b = ToDevice(r.right, r.bottom);
  RECT d = {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.x < b.x ? b.x : a.x, a.y < b.y ? b.y : a.y};
  return d;
}

int DCObj::ScaleX(int dx) const {
  int v = Scale(dx, s.viewportExt.cx, s.windowExt.cx);
  return v < 0 ? -v : v;
}
int DCObj::ScaleY(int dy) const {
  int v = Scale(dy, s.viewportExt.cy, s.windowExt.cy);
  return v < 0 ? -v : v;
}

RECT DCObj::DeviceBounds() {
  switch (kind) {
    case kWindowDC: {
      UserGuard g(UserLock());
      Window* w = ::winport::GetWindow(hwnd);
      if (!w) return MakeRect(0, 0, 0, 0);
      if (client) return MakeRect(0, 0, w->client.right - w->client.left, w->client.bottom - w->client.top);
      return MakeRect(0, 0, w->rect.right - w->rect.left, w->rect.bottom - w->rect.top);
    }
    case kMemoryDC: {
      BitmapObj* b = GdiAs<BitmapObj>(s.bitmap, OBJ_BITMAP);
      return b ? MakeRect(0, 0, b->width, b->height) : MakeRect(0, 0, 0, 0);
    }
    default:
      return MakeRect(0, 0, size.cx, size.cy);
  }
}

Region DCObj::Visible() {
  if (kind != kWindowDC) return Region(DeviceBounds());
  UserGuard g(UserLock());
  Window* w = ::winport::GetWindow(hwnd);
  if (!w) return Region();
  if (!w->parent && !w->surface) {
    // The desktop: the whole screen, for reading.
    SIZE sz = ScreenSize();
    return Region(MakeRect(0, 0, sz.cx, sz.cy));
  }
  Region r = VisibleRegion(w, client);
  POINT o = SurfaceOrigin(w);
  if (client) {
    o.x += w->client.left;
    o.y += w->client.top;
  }
  r.Offset(-o.x, -o.y);
  return r;
}

// ---------------------------------------------------------------------
// Drawing scopes

DrawScope::DrawScope(DCObj* dc, bool forWriting) : dc_(dc), writing_(forWriting) {
  if (!dc) return;
  switch (dc->kind) {
    case kWindowDC: {
      UserGuard* g = new UserGuard(UserLock());
      guard_ = g;
      Window* w = ::winport::GetWindow(dc->hwnd);
      if (!w) return;
      Window* top = w->TopLevel();
      if (!top->surface) {
        // The desktop's DC reads the windows as the screen shows them.
        if (forWriting) return;
        SIZE sz = ScreenSize();
        screen_.assign((size_t)sz.cx * sz.cy, GetSysColor(COLOR_BACKGROUND) == 0 ? 0 : ToPixel(GetSysColor(COLOR_BACKGROUND)));
        std::vector<Window*>& tops = TopLevels();
        for (size_t i = tops.size(); i-- > 0;) {
          Window* t = tops[i];
          if (!t->Visible() || !t->surface) continue;
          for (int y = 0; y < t->surface->height; y++) {
            int sy = t->rect.top + y;
            if (sy < 0 || sy >= sz.cy) continue;
            for (int x = 0; x < t->surface->width; x++) {
              int sx = t->rect.left + x;
              if (sx >= 0 && sx < sz.cx) screen_[(size_t)sy * sz.cx + sx] = t->surface->Row(y)[x];
            }
          }
        }
        px.row0 = screen_.data();
        px.stride = sz.cx;
        px.width = sz.cx;
        px.height = sz.cy;
        clip.Set(MakeRect(0, 0, sz.cx, sz.cy));
        ok = true;
        return;
      }
      Surface* sf = top->surface;
      px.row0 = sf->pixels.data();
      px.stride = sf->width;
      px.width = sf->width;
      px.height = sf->height;
      origin = SurfaceOrigin(w);
      if (dc->client) {
        origin.x += w->client.left;
        origin.y += w->client.top;
      }
      clip = dc->Visible();
      break;
    }
    case kMemoryDC: {
      bitmap_ = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP);
      if (!bitmap_) return;
      px = bitmap_->Lock();
      clip.Set(MakeRect(0, 0, px.width, px.height));
      break;
    }
    default:
      return;
  }
  if (dc->hasExtraClip) clip.Intersect(dc->extraClip);
  if (dc->s.hasClip) clip.Intersect(dc->s.clip);
  ok = px.Valid();
}

void DrawScope::Dirty(const RECT& device) {
  RECT r = Intersect(device, clip.Bounds());
  if (RectEmpty(r)) return;
  if (RectEmpty(dirty_)) {
    dirty_ = r;
  } else {
    if (r.left < dirty_.left) dirty_.left = r.left;
    if (r.top < dirty_.top) dirty_.top = r.top;
    if (r.right > dirty_.right) dirty_.right = r.right;
    if (r.bottom > dirty_.bottom) dirty_.bottom = r.bottom;
  }
}

DrawScope::~DrawScope() {
  if (bitmap_) {
    if (writing_) bitmap_->Unlock();
  }
  if (guard_) {
    if (ok && writing_ && !RectEmpty(dirty_) && screen_.empty()) {
      Window* w = ::winport::GetWindow(dc_->hwnd);
      if (w) SurfaceChanged(w->TopLevel(), Offset(dirty_, origin.x, origin.y));
    }
    delete (UserGuard*)guard_;
  }
}

}  // namespace winport

extern "C" {

// ---------------------------------------------------------------------
// Creation

HDC WINAPI CreateCompatibleDC(HDC hdc) {
  if (hdc && !DCOf(hdc)) return NULL;
  DCObj* dc = new DCObj(kMemoryDC);
  HDC h = (HDC)NewGdiHandle(dc);
  InitDC(dc);
  return h;
}

static HDC CreateDCCore(const std::wstring& driver, const std::wstring& device) {
  bool display = driver.empty() || _wcsicmp(driver.c_str(), L"DISPLAY") == 0 ||
                 _wcsicmp(device.c_str(), L"DISPLAY") == 0;
  DCObj* dc = new DCObj(display ? kInfoDC : kPrinterDC);
  if (display) {
    dc->size = ScreenSize();
  } else {
    // A letter page at 300 dots an inch until printing has a backend.
    dc->size.cx = 2550;
    dc->size.cy = 3300;
  }
  HDC h = (HDC)NewGdiHandle(dc);
  InitDC(dc);
  return h;
}

HDC WINAPI CreateDCW(LPCWSTR driver, LPCWSTR device, LPCWSTR, const DEVMODEW*) {
  return CreateDCCore(driver ? driver : L"", device ? device : L"");
}
HDC WINAPI CreateDCA(LPCSTR driver, LPCSTR device, LPCSTR, const DEVMODEA*) {
  return CreateDCCore(driver ? Widen(driver) : std::wstring(), device ? Widen(device) : std::wstring());
}
HDC WINAPI CreateICW(LPCWSTR driver, LPCWSTR device, LPCWSTR output, const DEVMODEW* dm) {
  return CreateDCW(driver, device, output, dm);
}
HDC WINAPI CreateICA(LPCSTR driver, LPCSTR device, LPCSTR output, const DEVMODEA* dm) {
  return CreateDCA(driver, device, output, dm);
}
HDC WINAPI ResetDCW(HDC hdc, const DEVMODEW*) { return DCOf(hdc) ? hdc : NULL; }
HDC WINAPI ResetDCA(HDC hdc, const DEVMODEA*) { return DCOf(hdc) ? hdc : NULL; }

BOOL WINAPI DeleteDC(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (dc->kind == kWindowDC) return ReleaseDC(dc->hwnd, hdc) != 0;
  FreeDC(dc);
  return TRUE;
}

HWND WINAPI WindowFromDC(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  return dc && dc->kind == kWindowDC ? dc->hwnd : NULL;
}

// ---------------------------------------------------------------------
// Objects

HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h) {
  DCObj* dc = DCOf(hdc);
  GdiObject* o = GdiObj(h);
  if (!dc || !o) return NULL;
  HGDIOBJ* slot = NULL;
  switch (o->type) {
    case OBJ_PEN: slot = &dc->s.pen; break;
    case OBJ_BRUSH: slot = &dc->s.brush; break;
    case OBJ_FONT: slot = &dc->s.font; break;
    case OBJ_REGION: return (HGDIOBJ)(INT_PTR)SelectClipRgn(hdc, (HRGN)h);
    case OBJ_BITMAP: {
      if (dc->kind != kMemoryDC) return NULL;
      BitmapObj* b = (BitmapObj*)o;
      if (b->selectedIn && b->selectedIn != dc && !b->stock) return NULL;
      BitmapObj* old = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP);
      if (old && old->selectedIn == dc) old->selectedIn = NULL;
      if (!b->stock) b->selectedIn = dc;
      slot = &dc->s.bitmap;
      break;
    }
    default:
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
  }
  HGDIOBJ old = *slot;
  if (old == h) return old;
  Ref(h);
  *slot = h;
  // The old object stays valid for the caller even if it was deleted
  // while selected: it goes when the caller lets it go here.
  GdiObject* oo = GdiObj(old);
  if (oo) {
    oo->refs--;
    if (oo->refs <= 0 && oo->deletePending) {
      FreeGdiHandle(old);
      return old;
    }
  }
  return old;
}

HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return NULL;
  switch (type) {
    case OBJ_PEN: case OBJ_EXTPEN: return dc->s.pen;
    case OBJ_BRUSH: return dc->s.brush;
    case OBJ_FONT: return dc->s.font;
    case OBJ_PAL: return dc->s.palette;
    case OBJ_BITMAP: return dc->kind == kMemoryDC ? dc->s.bitmap : NULL;
  }
  return NULL;
}

HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE pal, BOOL) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !GdiAs<PaletteObj>(pal, OBJ_PAL)) return NULL;
  HGDIOBJ old = dc->s.palette;
  Ref(pal);
  dc->s.palette = pal;
  Unref(old);
  return (HPALETTE)old;
}

UINT WINAPI RealizePalette(HDC hdc) { return DCOf(hdc) ? 0 : GDI_ERROR; }
BOOL WINAPI UpdateColors(HDC hdc) { return DCOf(hdc) != NULL; }

// ---------------------------------------------------------------------
// Attributes

#define DC_ATTR(type, Get, Set, field, bad)    \
  type WINAPI Get(HDC hdc) {                   \
    DCObj* dc = DCOf(hdc);                     \
    return dc ? (type)dc->s.field : (type)bad; \
  }                                            \
  type WINAPI Set(HDC hdc, type v) {           \
    DCObj* dc = DCOf(hdc);                     \
    if (!dc) return (type)bad;                 \
    type old = (type)dc->s.field;              \
    dc->s.field = v;                           \
    return old;                                \
  }

DC_ATTR(COLORREF, GetBkColor, SetBkColor, bkColor, CLR_INVALID)
DC_ATTR(COLORREF, GetTextColor, SetTextColor, textColor, CLR_INVALID)
DC_ATTR(COLORREF, GetDCBrushColor, SetDCBrushColor, dcBrushColor, CLR_INVALID)
DC_ATTR(COLORREF, GetDCPenColor, SetDCPenColor, dcPenColor, CLR_INVALID)
DC_ATTR(int, GetBkMode, SetBkMode, bkMode, 0)
DC_ATTR(int, GetROP2, SetROP2, rop2, 0)
DC_ATTR(int, GetPolyFillMode, SetPolyFillMode, polyFill, 0)
DC_ATTR(int, GetStretchBltMode, SetStretchBltMode, stretchMode, 0)
DC_ATTR(UINT, GetTextAlign, SetTextAlign, textAlign, GDI_ERROR)
DC_ATTR(int, GetTextCharacterExtra, SetTextCharacterExtra, charExtra, 0x80000000)
DC_ATTR(int, GetArcDirection, SetArcDirection, arcDirection, 0)
DC_ATTR(DWORD, GetLayout, SetLayout, layout, GDI_ERROR)

#undef DC_ATTR

BOOL WINAPI SetTextJustification(HDC hdc, int extra, int count) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  dc->s.breakExtra = extra;
  dc->s.breakCount = count;
  return TRUE;
}

int WINAPI SetGraphicsMode(HDC hdc, int mode) {
  DCObj* dc = DCOf(hdc);
  if (!dc || (mode != GM_COMPATIBLE && mode != GM_ADVANCED)) return 0;
  int old = dc->s.graphicsMode;
  dc->s.graphicsMode = mode;
  return old;
}
int WINAPI GetGraphicsMode(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  return dc ? dc->s.graphicsMode : 0;
}

BOOL WINAPI SetMiterLimit(HDC hdc, FLOAT limit, PFLOAT old) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (old) *old = dc->s.miterLimit;
  dc->s.miterLimit = limit;
  return TRUE;
}
BOOL WINAPI GetMiterLimit(HDC hdc, PFLOAT out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return FALSE;
  *out = dc->s.miterLimit;
  return TRUE;
}

DWORD WINAPI SetMapperFlags(HDC hdc, DWORD flags) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return GDI_ERROR;
  DWORD old = dc->s.mapperFlags;
  dc->s.mapperFlags = flags;
  return old;
}

BOOL WINAPI SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT old) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (old) *old = dc->s.brushOrg;
  dc->s.brushOrg.x = x;
  dc->s.brushOrg.y = y;
  return TRUE;
}
BOOL WINAPI GetBrushOrgEx(HDC hdc, LPPOINT out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return FALSE;
  *out = dc->s.brushOrg;
  return TRUE;
}

BOOL WINAPI SetColorAdjustment(HDC hdc, const COLORADJUSTMENT*) { return DCOf(hdc) != NULL; }
BOOL WINAPI GetColorAdjustment(HDC hdc, LPCOLORADJUSTMENT ca) {
  if (!DCOf(hdc) || !ca) return FALSE;
  memset(ca, 0, sizeof *ca);
  ca->caSize = sizeof *ca;
  ca->caRedGamma = ca->caGreenGamma = ca->caBlueGamma = 10000;
  ca->caReferenceWhite = 10000;
  return TRUE;
}

UINT WINAPI SetBoundsRect(HDC hdc, const RECT*, UINT) { return DCOf(hdc) ? DCB_RESET : 0; }
UINT WINAPI GetBoundsRect(HDC hdc, LPRECT r, UINT) {
  if (!DCOf(hdc)) return 0;
  if (r) *r = MakeRect(0, 0, 0, 0);
  return DCB_RESET;
}

BOOL WINAPI GetDCOrgEx(HDC hdc, LPPOINT p) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !p) return FALSE;
  p->x = p->y = 0;
  if (dc->kind == kWindowDC) {
    UserGuard g(UserLock());
    if (Window* w = ::winport::GetWindow(dc->hwnd)) {
      RECT r = dc->client ? ScreenClient(w) : ScreenRect(w);
      p->x = r.left;
      p->y = r.top;
    }
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// Mapping

int WINAPI SetMapMode(HDC hdc, int mode) {
  DCObj* dc = DCOf(hdc);
  if (!dc || mode < MM_TEXT || mode > MM_ANISOTROPIC) return 0;
  DCState& s = dc->s;
  int old = s.mapMode;
  s.mapMode = mode;
  // Units per inch of each fixed mode, against pixels per inch.
  LONG units = 0;
  switch (mode) {
    case MM_LOMETRIC: units = 254; break;
    case MM_HIMETRIC: units = 2540; break;
    case MM_LOENGLISH: units = 100; break;
    case MM_HIENGLISH: units = 1000; break;
    case MM_TWIPS: units = 1440; break;
  }
  if (mode == MM_TEXT) {
    s.windowExt.cx = s.windowExt.cy = 1;
    s.viewportExt.cx = s.viewportExt.cy = 1;
  } else if (units) {
    s.windowExt.cx = s.windowExt.cy = units;
    s.viewportExt.cx = kLogPixels;
    s.viewportExt.cy = -kLogPixels;
  } else if (old != MM_ISOTROPIC && old != MM_ANISOTROPIC && mode == MM_ISOTROPIC) {
    FixIsotropic(s);
  }
  return old;
}

int WINAPI GetMapMode(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  return dc ? dc->s.mapMode : 0;
}

static BOOL SetPoint(HDC hdc, POINT DCState::*field, int x, int y, LPPOINT old, bool add) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  POINT& p = dc->s.*field;
  if (old) *old = p;
  if (add) {
    p.x += x;
    p.y += y;
  } else {
    p.x = x;
    p.y = y;
  }
  return TRUE;
}

static BOOL GetPoint(HDC hdc, POINT DCState::*field, LPPOINT out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return FALSE;
  *out = dc->s.*field;
  return TRUE;
}

BOOL WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT old) {
  return SetPoint(hdc, &DCState::viewportOrg, x, y, old, false);
}
BOOL WINAPI OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT old) {
  return SetPoint(hdc, &DCState::viewportOrg, x, y, old, true);
}
BOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT out) { return GetPoint(hdc, &DCState::viewportOrg, out); }
BOOL WINAPI SetWindowOrgEx(HDC hdc, int x, int y, LPPOINT old) {
  return SetPoint(hdc, &DCState::windowOrg, x, y, old, false);
}
BOOL WINAPI OffsetWindowOrgEx(HDC hdc, int x, int y, LPPOINT old) {
  return SetPoint(hdc, &DCState::windowOrg, x, y, old, true);
}
BOOL WINAPI GetWindowOrgEx(HDC hdc, LPPOINT out) { return GetPoint(hdc, &DCState::windowOrg, out); }

static BOOL SetExt(HDC hdc, bool viewport, int x, int y, LPSIZE old) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  SIZE& e = viewport ? dc->s.viewportExt : dc->s.windowExt;
  if (old) *old = e;
  // Only the scalable modes have extents to set.
  if (dc->s.mapMode != MM_ISOTROPIC && dc->s.mapMode != MM_ANISOTROPIC) return TRUE;
  if (x == 0 || y == 0) return FALSE;
  e.cx = x;
  e.cy = y;
  FixIsotropic(dc->s);
  return TRUE;
}

BOOL WINAPI SetViewportExtEx(HDC hdc, int x, int y, LPSIZE old) { return SetExt(hdc, true, x, y, old); }
BOOL WINAPI SetWindowExtEx(HDC hdc, int x, int y, LPSIZE old) { return SetExt(hdc, false, x, y, old); }

BOOL WINAPI GetViewportExtEx(HDC hdc, LPSIZE out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return FALSE;
  *out = dc->s.viewportExt;
  return TRUE;
}
BOOL WINAPI GetWindowExtEx(HDC hdc, LPSIZE out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return FALSE;
  *out = dc->s.windowExt;
  return TRUE;
}

static BOOL ScaleExt(HDC hdc, bool viewport, int xn, int xd, int yn, int yd, LPSIZE old) {
  DCObj* dc = DCOf(hdc);
  if (!dc || xd == 0 || yd == 0) return FALSE;
  SIZE& e = viewport ? dc->s.viewportExt : dc->s.windowExt;
  if (old) *old = e;
  if (dc->s.mapMode != MM_ISOTROPIC && dc->s.mapMode != MM_ANISOTROPIC) return TRUE;
  LONG cx = (LONG)((long long)e.cx * xn / xd), cy = (LONG)((long long)e.cy * yn / yd);
  if (cx == 0 || cy == 0) return FALSE;
  e.cx = cx;
  e.cy = cy;
  FixIsotropic(dc->s);
  return TRUE;
}

BOOL WINAPI ScaleViewportExtEx(HDC hdc, int xn, int xd, int yn, int yd, LPSIZE old) {
  return ScaleExt(hdc, true, xn, xd, yn, yd, old);
}
BOOL WINAPI ScaleWindowExtEx(HDC hdc, int xn, int xd, int yn, int yd, LPSIZE old) {
  return ScaleExt(hdc, false, xn, xd, yn, yd, old);
}

BOOL WINAPI LPtoDP(HDC hdc, LPPOINT pts, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || (!pts && n)) return FALSE;
  for (int i = 0; i < n; i++) pts[i] = dc->ToDevice(pts[i].x, pts[i].y);
  return TRUE;
}

BOOL WINAPI DPtoLP(HDC hdc, LPPOINT pts, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || (!pts && n)) return FALSE;
  for (int i = 0; i < n; i++) pts[i] = dc->ToLogical(pts[i].x, pts[i].y);
  return TRUE;
}

BOOL WINAPI SetWorldTransform(HDC hdc, const XFORM* x) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !x || dc->s.graphicsMode != GM_ADVANCED) return FALSE;
  dc->s.xform = *x;
  return TRUE;
}

BOOL WINAPI GetWorldTransform(HDC hdc, LPXFORM x) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !x) return FALSE;
  *x = dc->s.xform;
  return TRUE;
}

BOOL WINAPI CombineTransform(LPXFORM out, const XFORM* a, const XFORM* b) {
  if (!out || !a || !b) return FALSE;
  XFORM r;
  r.eM11 = a->eM11 * b->eM11 + a->eM12 * b->eM21;
  r.eM12 = a->eM11 * b->eM12 + a->eM12 * b->eM22;
  r.eM21 = a->eM21 * b->eM11 + a->eM22 * b->eM21;
  r.eM22 = a->eM21 * b->eM12 + a->eM22 * b->eM22;
  r.eDx = a->eDx * b->eM11 + a->eDy * b->eM21 + b->eDx;
  r.eDy = a->eDx * b->eM12 + a->eDy * b->eM22 + b->eDy;
  *out = r;
  return TRUE;
}

BOOL WINAPI ModifyWorldTransform(HDC hdc, const XFORM* x, DWORD mode) {
  DCObj* dc = DCOf(hdc);
  if (!dc || dc->s.graphicsMode != GM_ADVANCED) return FALSE;
  switch (mode) {
    case MWT_IDENTITY: dc->s.xform = XFORM{1, 0, 0, 1, 0, 0}; return TRUE;
    case MWT_LEFTMULTIPLY: return x && CombineTransform(&dc->s.xform, x, &dc->s.xform);
    case MWT_RIGHTMULTIPLY: return x && CombineTransform(&dc->s.xform, &dc->s.xform, x);
  }
  return FALSE;
}

// ---------------------------------------------------------------------
// Clipping. The clip region is kept in device coordinates.

int WINAPI ExtSelectClipRgn(HDC hdc, HRGN h, int mode) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return ERROR;
  DCState& s = dc->s;
  if (!h) {
    if (mode != RGN_COPY) return ERROR;
    s.clip.Clear();
    s.hasClip = false;
    Region vis = dc->Visible();
    return vis.Complexity();
  }
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r) return ERROR;
  Region cur = s.hasClip ? s.clip : Region(dc->DeviceBounds());
  switch (mode) {
    case RGN_COPY: cur = r->rgn; break;
    case RGN_AND: cur.Intersect(r->rgn); break;
    case RGN_OR: cur.Union(r->rgn); break;
    case RGN_XOR: cur.Xor(r->rgn); break;
    case RGN_DIFF: cur.Subtract(r->rgn); break;
    default: return ERROR;
  }
  s.clip = cur;
  s.hasClip = true;
  Region eff = dc->Visible();
  eff.Intersect(cur);
  return eff.Complexity();
}

int WINAPI SelectClipRgn(HDC hdc, HRGN h) { return ExtSelectClipRgn(hdc, h, RGN_COPY); }

static int ClipRect(HDC hdc, int l, int t, int r, int b, bool exclude) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return ERROR;
  RECT d = dc->ToDevice(MakeRect(l, t, r, b));
  DCState& s = dc->s;
  if (!s.hasClip) {
    s.clip.Set(dc->DeviceBounds());
    s.hasClip = true;
  }
  if (exclude)
    s.clip.Subtract(d);
  else
    s.clip.Intersect(d);
  Region eff = dc->Visible();
  eff.Intersect(s.clip);
  return eff.Complexity();
}

int WINAPI IntersectClipRect(HDC hdc, int l, int t, int r, int b) { return ClipRect(hdc, l, t, r, b, false); }
int WINAPI ExcludeClipRect(HDC hdc, int l, int t, int r, int b) { return ClipRect(hdc, l, t, r, b, true); }

int WINAPI OffsetClipRgn(HDC hdc, int x, int y) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return ERROR;
  if (!dc->s.hasClip) return dc->Visible().Complexity();
  dc->s.clip.Offset(dc->ScaleX(x) * (x < 0 ? -1 : 1), dc->ScaleY(y) * (y < 0 ? -1 : 1));
  return dc->s.clip.Complexity();
}

int WINAPI GetClipRgn(HDC hdc, HRGN h) {
  DCObj* dc = DCOf(hdc);
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!dc || !r) return -1;
  if (!dc->s.hasClip) return 0;
  r->rgn = dc->s.clip;
  return 1;
}

// The effective clipping area in device coordinates.
static Region Effective(DCObj* dc) {
  Region r = dc->Visible();
  if (dc->hasExtraClip) r.Intersect(dc->extraClip);
  if (dc->s.hasClip) r.Intersect(dc->s.clip);
  return r;
}

int WINAPI GetClipBox(HDC hdc, LPRECT out) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !out) return ERROR;
  Region r = Effective(dc);
  RECT b = r.Bounds();
  POINT a = dc->ToLogical(b.left, b.top), c = dc->ToLogical(b.right, b.bottom);
  *out = MakeRect(a.x < c.x ? a.x : c.x, a.y < c.y ? a.y : c.y, a.x < c.x ? c.x : a.x, a.y < c.y ? c.y : a.y);
  return r.Complexity();
}

BOOL WINAPI PtVisible(HDC hdc, int x, int y) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return -1;
  POINT p = dc->ToDevice(x, y);
  return Effective(dc).Contains(p.x, p.y);
}

BOOL WINAPI RectVisible(HDC hdc, const RECT* r) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !r) return FALSE;
  return Effective(dc).Touches(dc->ToDevice(*r));
}

int WINAPI GetRandomRgn(HDC hdc, HRGN h, INT which) {
  DCObj* dc = DCOf(hdc);
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!dc || !r) return -1;
  if (which == 1) return GetClipRgn(hdc, h);
  Region v = Effective(dc);
  // SYSRGN is in screen coordinates.
  POINT o = {0, 0};
  GetDCOrgEx(hdc, &o);
  v.Offset(o.x, o.y);
  r->rgn = v;
  return 1;
}

// ---------------------------------------------------------------------
// Saving and restoring

int WINAPI SaveDC(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  dc->saved.push_back(dc->s);
  RefState(dc->saved.back());
  return (int)dc->saved.size();
}

BOOL WINAPI RestoreDC(HDC hdc, int level) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  int n = (int)dc->saved.size();
  if (level < 0) level = n + 1 + level;
  if (level <= 0 || level > n) return FALSE;
  // Drop the newer saved states, then make the chosen one current.
  while ((int)dc->saved.size() > level) {
    UnrefState(dc->saved.back());
    dc->saved.pop_back();
  }
  DCState st = dc->saved.back();
  dc->saved.pop_back();
  if (dc->kind == kMemoryDC && st.bitmap != dc->s.bitmap) {
    if (BitmapObj* b = GdiAs<BitmapObj>(dc->s.bitmap, OBJ_BITMAP))
      if (b->selectedIn == dc) b->selectedIn = NULL;
    if (BitmapObj* b = GdiAs<BitmapObj>(st.bitmap, OBJ_BITMAP))
      if (!b->stock) b->selectedIn = dc;
  }
  UnrefState(dc->s);
  dc->s = st;  // the saved state's references pass to the current one
  return TRUE;
}

// ---------------------------------------------------------------------
// Capabilities

int WINAPI GetDeviceCaps(HDC hdc, int index) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  bool printer = dc->kind == kPrinterDC;
  SIZE sz = printer ? dc->size : ScreenSize();
  int dpi = printer ? 300 : kLogPixels;
  switch (index) {
    case DRIVERVERSION: return 0x4000;
    case TECHNOLOGY: return printer ? DT_RASPRINTER : DT_RASDISPLAY;
    case HORZSIZE: return sz.cx * 254 / (dpi * 10);
    case VERTSIZE: return sz.cy * 254 / (dpi * 10);
    case HORZRES: case DESKTOPHORZRES: return sz.cx;
    case VERTRES: case DESKTOPVERTRES: return sz.cy;
    case BITSPIXEL: return 32;
    case PLANES: return 1;
    case NUMBRUSHES: case NUMPENS: return -1;
    case NUMMARKERS: return 0;
    case NUMFONTS: return 0;
    case NUMCOLORS: return -1;
    case PDEVICESIZE: return 0;
    case CURVECAPS: return 0x01FF;
    case LINECAPS: return 0x00FE;
    case POLYGONALCAPS: return 0x00FF;
    case TEXTCAPS: return TC_OP_CHARACTER | TC_CP_STROKE | TC_RA_ABLE | TC_UA_ABLE | TC_SO_ABLE | TC_VA_ABLE;
    case CLIPCAPS: return 1;
    case RASTERCAPS:
      return RC_BITBLT | RC_BITMAP64 | RC_GDI20_OUTPUT | RC_DI_BITMAP | RC_DIBTODEV | RC_STRETCHBLT |
             RC_STRETCHDIB | RC_FLOODFILL;
    case ASPECTX: case ASPECTY: return 36;
    case ASPECTXY: return 51;
    case LOGPIXELSX: case LOGPIXELSY: return dpi;
    case SIZEPALETTE: return 0;
    case NUMRESERVED: return 20;
    case COLORRES: return 24;
    case PHYSICALWIDTH: return printer ? sz.cx : 0;
    case PHYSICALHEIGHT: return printer ? sz.cy : 0;
    case PHYSICALOFFSETX: case PHYSICALOFFSETY: return 0;
    case SCALINGFACTORX: case SCALINGFACTORY: return 0;
    case VREFRESH: return printer ? 0 : 60;
    case BLTALIGNMENT: return 0;
    case SHADEBLENDCAPS: return 0x7F;
    case COLORMGMTCAPS: return 0;
  }
  return 0;
}

}  // extern "C"
