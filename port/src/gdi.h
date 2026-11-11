// gdi.h - the drawing layer's internals: GDI objects and handles, device
// contexts, and the 32-bit pixels everything draws on.
//
// Everything is drawn in software. A window's device context draws into
// its top-level window's surface (see user.h); a memory device context
// draws into the bitmap selected into it. Pixels are 0x00RRGGBB.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_GDI_H
#define _WINPORT_GDI_H

#include "winport.h"
#include "region.h"

#include <vector>

namespace winport {

struct Window;

inline DWORD ToPixel(COLORREF c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }
inline COLORREF ToColor(DWORD p) { return ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF); }

// A block of 32-bit pixels to draw on.
struct Pixels {
  DWORD* row0 = NULL;
  ptrdiff_t stride = 0;  // in pixels; negative for a bottom-up image
  int width = 0, height = 0;
  DWORD* Row(int y) const { return row0 + y * stride; }
  bool Valid() const { return row0 != NULL && width > 0 && height > 0; }
};

// Objects. A handle is an index into a table, so that a stale or wrong
// handle is caught rather than followed.
struct GdiObject {
  DWORD type;  // OBJ_PEN, OBJ_BRUSH, ...
  bool stock = false;
  // Selections in DCs and their saved states; an object deleted while
  // selected goes when the last selection ends.
  int refs = 0;
  bool deletePending = false;
  explicit GdiObject(DWORD t) : type(t) {}
  virtual ~GdiObject() {}
};
HGDIOBJ NewGdiHandle(GdiObject* o);
// The object for a handle, or NULL with ERROR_INVALID_HANDLE; type 0
// takes any kind.
GdiObject* GdiObj(HGDIOBJ h, DWORD type = 0);
template <class T>
T* GdiAs(HGDIOBJ h, DWORD type) {
  return static_cast<T*>(GdiObj(h, type));
}
HGDIOBJ HandleOf(const GdiObject* o);
void FreeGdiHandle(HGDIOBJ h);
// Selection counting: Ref when an object goes into a DC or a saved
// state, Unref when it leaves.
void Ref(HGDIOBJ h);
void Unref(HGDIOBJ h);
// The stock 1x1 monochrome bitmap a new memory DC holds.
HBITMAP StockBitmap();

// DIBs: the format a BITMAPINFO describes, and conversion of a row to
// and from 32-bit pixels.
struct DibFormat {
  int width = 0, height = 0;  // height positive
  bool topDown = false;
  int bpp = 0;
  DWORD compression = BI_RGB;
  DWORD masks[3] = {0, 0, 0};
  std::vector<DWORD> colors;  // the color table as pixels
  int stride = 0;             // bytes per row
  // The row of memory holding image row y (0 at the top).
  int MemRow(int y) const { return topDown ? y : height - 1 - y; }
};
bool ParseDib(const BITMAPINFO* bmi, UINT usage, DibFormat* f);
void DibRowToPixels(const DibFormat& f, const BYTE* row, DWORD* out, int count);
void PixelsToDibRow(const DibFormat& f, const DWORD* in, BYTE* row, int count);

struct PenObj : GdiObject {
  PenObj() : GdiObject(OBJ_PEN) {}
  UINT style = PS_SOLID;  // with the PS_GEOMETRIC, end cap and join bits of an extended pen
  int width = 0;
  COLORREF color = 0;
  bool extended = false;
};

struct BitmapObj;

struct BrushObj : GdiObject {
  BrushObj() : GdiObject(OBJ_BRUSH) {}
  LOGBRUSH lb = {BS_SOLID, 0, 0};
  int sysColor = -1;  // a GetSysColorBrush brush follows the system color
  // A pattern brush's pixels; a monochrome pattern draws its 1 bits in
  // the background color and its 0 bits in the text color.
  std::vector<DWORD> pattern;
  int patWidth = 0, patHeight = 0;
  bool monoPattern = false;
};

struct FontObj : GdiObject {
  FontObj() : GdiObject(OBJ_FONT) {}
  LOGFONTW lf = {};
};

struct RegionObj : GdiObject {
  RegionObj() : GdiObject(OBJ_REGION) {}
  Region rgn;
};

struct PaletteObj : GdiObject {
  PaletteObj() : GdiObject(OBJ_PAL) {}
  std::vector<PALETTEENTRY> entries;
};

struct DCObj;

// A bitmap. A device-dependent bitmap keeps 32-bit pixels, top row
// first, and remembers whether it is monochrome. A DIB section's bits
// belong to the application, in its format; drawing works on a 32-bit
// copy that is filled from the bits before and written back after.
struct BitmapObj : GdiObject {
  BitmapObj() : GdiObject(OBJ_BITMAP) {}
  ~BitmapObj();
  int width = 0, height = 0;
  int bpp = 32;
  bool mono = false;  // a monochrome device-dependent bitmap
  std::vector<DWORD> pixels;  // 32-bit pixels, or the copy of a DIB section's
  // DIB sections
  bool dib = false;
  BYTE* bits = NULL;
  bool ownBits = false;
  HANDLE section = NULL;
  DWORD sectionOffset = 0;
  BITMAPINFOHEADER bih = {};
  std::vector<RGBQUAD> colors;  // the color table, up to 256 entries
  DWORD masks[3] = {0, 0, 0};   // BI_BITFIELDS
  int dibStride = 0;            // bytes per row
  bool topDown = false;
  DCObj* selectedIn = NULL;
  SIZE dimension = {0, 0};  // SetBitmapDimensionEx

  // Whether drawing can work on the bits themselves.
  bool Direct() const;
  // Pixels to draw on; a DIB section's copy is filled from its bits.
  Pixels Lock();
  // Writes a DIB section's copy back to its bits.
  void Unlock();
  // Converts between the DIB format and 32-bit pixels.
  void Unpack();
  void Pack();
  int Stride() const { return ((width * bpp + 31) / 32) * 4; }
};

// A device context's attributes, saved and restored by SaveDC.
struct DCState {
  HGDIOBJ pen = NULL, brush = NULL, font = NULL, bitmap = NULL, palette = NULL;
  COLORREF textColor = RGB(0, 0, 0), bkColor = RGB(255, 255, 255);
  COLORREF dcBrushColor = RGB(255, 255, 255), dcPenColor = RGB(0, 0, 0);
  int bkMode = OPAQUE, rop2 = R2_COPYPEN, polyFill = ALTERNATE, stretchMode = BLACKONWHITE;
  int mapMode = MM_TEXT, graphicsMode = GM_COMPATIBLE, arcDirection = AD_COUNTERCLOCKWISE;
  UINT textAlign = TA_LEFT | TA_TOP | TA_NOUPDATECP;
  int charExtra = 0;
  int breakExtra = 0, breakCount = 0;  // SetTextJustification
  DWORD layout = 0, mapperFlags = 0;
  POINT windowOrg = {0, 0}, viewportOrg = {0, 0};
  SIZE windowExt = {1, 1}, viewportExt = {1, 1};
  POINT current = {0, 0}, brushOrg = {0, 0};
  FLOAT miterLimit = 10.0f;
  XFORM xform = {1, 0, 0, 1, 0, 0};
  Region clip;  // the application's clip region, in device coordinates
  bool hasClip = false;
};

enum DCKind { kWindowDC, kMemoryDC, kInfoDC, kPrinterDC, kMetaDC };

struct DCObj : GdiObject {
  explicit DCObj(DCKind k) : GdiObject(k == kMemoryDC ? OBJ_MEMDC : k == kMetaDC ? OBJ_METADC : OBJ_DC), kind(k) {}
  DCKind kind;
  DCState s;
  std::vector<DCState> saved;
  // A window's DC: the window, whether it covers the client area or the
  // whole window, and any further clipping (BeginPaint's update region,
  // GetDCEx's region), in device coordinates.
  HWND hwnd = NULL;
  bool client = true;
  bool owned = false;  // a CS_OWNDC window's own DC, kept by ReleaseDC
  Region extraClip;
  bool hasExtraClip = false;
  // A DC with nowhere to draw (an information or printer DC) has a size.
  SIZE size = {0, 0};
  // A metafile DC records into this.
  std::vector<BYTE> record;
  // Printing
  ABORTPROC abortProc = NULL;
  int docState = 0;

  // The area visible on the device, before the application's clipping.
  Region Visible();
  RECT DeviceBounds();
  // Logical to device coordinates.
  POINT ToDevice(LONG x, LONG y) const;
  POINT ToLogical(LONG x, LONG y) const;
  RECT ToDevice(const RECT& r) const;  // normalized
  int ScaleX(int dx) const;            // a logical width in device units
  int ScaleY(int dy) const;
  bool Identity() const {
    return s.windowExt.cx == s.viewportExt.cx && s.windowExt.cy == s.viewportExt.cy;
  }
};
DCObj* DCOf(HDC h);  // NULL with ERROR_INVALID_HANDLE
// Sets a new DC's default attributes and objects.
void InitDC(DCObj* dc);
// The DC a CS_OWNDC or CS_CLASSDC window keeps.
void FreeDC(DCObj* dc);

// Drawing on a DC: the pixels, where device (0, 0) lies in them, and the
// device area that may be drawn on. A window's DC holds the window
// manager's lock meanwhile. Dirty records what was drawn, in device
// coordinates, for the backend.
struct DrawScope {
  explicit DrawScope(DCObj* dc, bool forWriting = true);
  ~DrawScope();
  DrawScope(const DrawScope&) = delete;
  DrawScope& operator=(const DrawScope&) = delete;
  bool ok = false;
  Pixels px;
  POINT origin = {0, 0};
  Region clip;
  void Dirty(const RECT& device);
  // Device pixel (x, y), which must be in clip.
  DWORD& At(int x, int y) { return px.Row(y + origin.y)[x + origin.x]; }

 private:
  DCObj* dc_;
  void* guard_ = NULL;
  struct BitmapObj* bitmap_ = NULL;
  RECT dirty_ = {0, 0, 0, 0};
  bool writing_;
  std::vector<DWORD> screen_;  // the desktop's pixels, put together for reading
};

// The color a brush paints a pixel at device position (x, y), with the
// DC's colors for monochrome patterns.
struct BrushPaint {
  bool null = true;
  bool solid = true;
  DWORD color = 0;
  const BrushObj* brush = NULL;
  DWORD fore = 0, back = 0;  // for monochrome patterns and hatches
  int ox = 0, oy = 0;        // the brush origin, in device pixels
  bool transparentHatch = false;
  DWORD At(int x, int y) const;
  bool Draws(int x, int y) const;  // false where a transparent hatch leaves the pixel
};
BrushPaint PaintFor(DCObj* dc, HBRUSH brush);

// Fills the device rectangle, clipped, with a brush and a raster op.
void FillDevice(DCObj* dc, const RECT& device, const BrushPaint& paint, DWORD rop3);

// A ternary raster operation applied to one pixel.
DWORD Rop3(DWORD rop, DWORD dst, DWORD src, DWORD pat);
// A binary raster operation (R2_*).
DWORD Rop2(int rop, DWORD dst, DWORD pen);

// The display's size and depth.
SIZE ScreenSize();
const int kLogPixels = 96;

}  // namespace winport

#endif
