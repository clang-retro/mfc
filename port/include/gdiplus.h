// gdiplus.h - the part of GDI+ that reads and writes images: bitmaps,
// their pixels and palettes, and the BMP, PNG, JPEG and GIF codecs, as
// the flat API (Gdiplus::DllExports) that CImage uses. The drawing
// classes (Graphics, Pen, Brush) are not here.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_GDIPLUS_H
#define _WINPORT_GDIPLUS_H

#include <windows.h>
#include <objidl.h>

namespace Gdiplus {

typedef float REAL;
typedef DWORD ARGB;
typedef INT PixelFormat;

enum Status {
  Ok = 0,
  GenericError = 1,
  InvalidParameter = 2,
  OutOfMemory = 3,
  ObjectBusy = 4,
  InsufficientBuffer = 5,
  NotImplemented = 6,
  Win32Error = 7,
  WrongState = 8,
  Aborted = 9,
  FileNotFound = 10,
  ValueOverflow = 11,
  AccessDenied = 12,
  UnknownImageFormat = 13,
  FontFamilyNotFound = 14,
  FontStyleNotFound = 15,
  NotTrueTypeFont = 16,
  UnsupportedGdiplusVersion = 17,
  GdiplusNotInitialized = 18,
  PropertyNotFound = 19,
  PropertyNotSupported = 20,
  ProfileNotFound = 21
};
typedef Status GpStatus;

// Pixel formats: the index, the bits per pixel, and the kind.
#define PixelFormatIndexed 0x00010000
#define PixelFormatGDI 0x00020000
#define PixelFormatAlpha 0x00040000
#define PixelFormatPAlpha 0x00080000
#define PixelFormatExtended 0x00100000
#define PixelFormatCanonical 0x00200000
#define PixelFormatUndefined 0
#define PixelFormatDontCare 0
#define PixelFormat1bppIndexed (1 | (1 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat4bppIndexed (2 | (4 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat8bppIndexed (3 | (8 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat16bppGrayScale (4 | (16 << 8) | PixelFormatExtended)
#define PixelFormat16bppRGB555 (5 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppRGB565 (6 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppARGB1555 (7 | (16 << 8) | PixelFormatAlpha | PixelFormatGDI)
#define PixelFormat24bppRGB (8 | (24 << 8) | PixelFormatGDI)
#define PixelFormat32bppRGB (9 | (32 << 8) | PixelFormatGDI)
#define PixelFormat32bppARGB (10 | (32 << 8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical)
#define PixelFormat32bppPARGB (11 | (32 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI)
#define PixelFormat48bppRGB (12 | (48 << 8) | PixelFormatExtended)
#define PixelFormat64bppARGB (13 | (64 << 8) | PixelFormatAlpha | PixelFormatCanonical | PixelFormatExtended)
#define PixelFormat64bppPARGB (14 | (64 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatExtended)
#define PixelFormatMax 15

inline UINT GetPixelFormatSize(PixelFormat f) { return (f >> 8) & 0xFF; }
inline BOOL IsIndexedPixelFormat(PixelFormat f) { return (f & PixelFormatIndexed) != 0; }
inline BOOL IsAlphaPixelFormat(PixelFormat f) { return (f & PixelFormatAlpha) != 0; }

enum PaletteFlags { PaletteFlagsHasAlpha = 1, PaletteFlagsGrayScale = 2, PaletteFlagsHalftone = 4 };

struct ColorPalette {
  UINT Flags;
  UINT Count;
  ARGB Entries[1];
};

enum ImageLockMode { ImageLockModeRead = 1, ImageLockModeWrite = 2, ImageLockModeUserInputBuf = 4 };

class BitmapData {
 public:
  UINT Width;
  UINT Height;
  INT Stride;
  Gdiplus::PixelFormat PixelFormat;
  VOID* Scan0;
  UINT_PTR Reserved;
};

class Rect {
 public:
  Rect() : X(0), Y(0), Width(0), Height(0) {}
  Rect(INT x, INT y, INT w, INT h) : X(x), Y(y), Width(w), Height(h) {}
  INT X, Y, Width, Height;
};

enum ImageCodecFlags {
  ImageCodecFlagsEncoder = 0x00000001,
  ImageCodecFlagsDecoder = 0x00000002,
  ImageCodecFlagsSupportBitmap = 0x00000004,
  ImageCodecFlagsSupportVector = 0x00000008,
  ImageCodecFlagsSeekableEncode = 0x00000010,
  ImageCodecFlagsBlockingDecode = 0x00000020,
  ImageCodecFlagsBuiltin = 0x00010000,
  ImageCodecFlagsSystem = 0x00020000,
  ImageCodecFlagsUser = 0x00040000
};

class ImageCodecInfo {
 public:
  CLSID Clsid;
  GUID FormatID;
  const WCHAR* CodecName;
  const WCHAR* DllName;
  const WCHAR* FormatDescription;
  const WCHAR* FilenameExtension;
  const WCHAR* MimeType;
  DWORD Flags;
  DWORD Version;
  DWORD SigCount;
  DWORD SigSize;
  const BYTE* SigPattern;
  const BYTE* SigMask;
};

enum EncoderParameterValueType {
  EncoderParameterValueTypeByte = 1,
  EncoderParameterValueTypeASCII = 2,
  EncoderParameterValueTypeShort = 3,
  EncoderParameterValueTypeLong = 4,
  EncoderParameterValueTypeRational = 5,
  EncoderParameterValueTypeLongRange = 6,
  EncoderParameterValueTypeUndefined = 7,
  EncoderParameterValueTypeRationalRange = 8
};

class EncoderParameter {
 public:
  GUID Guid;
  ULONG NumberOfValues;
  ULONG Type;
  VOID* Value;
};

class EncoderParameters {
 public:
  UINT Count;
  EncoderParameter Parameter[1];
};

// The formats, as ImageCodecInfo::FormatID names them.
inline const GUID ImageFormatUndefined = {0xb96b3ca9, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatMemoryBMP = {0xb96b3caa, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatBMP = {0xb96b3cab, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatEMF = {0xb96b3cac, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatWMF = {0xb96b3cad, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatJPEG = {0xb96b3cae, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatPNG = {0xb96b3caf, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatGIF = {0xb96b3cb0, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatTIFF = {0xb96b3cb1, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatEXIF = {0xb96b3cb2, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
inline const GUID ImageFormatIcon = {0xb96b3cb5, 0x0728, 0x11d3, {0x9d, 0x7b, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
// The JPEG encoder's one parameter: the quality, a LONG from 0 to 100.
inline const GUID EncoderQuality = {0x1d5be4b5, 0xfa4a, 0x452d, {0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb}};

enum DebugEventLevel { DebugEventLevelFatal, DebugEventLevelWarning };
typedef VOID(WINAPI* DebugEventProc)(DebugEventLevel level, CHAR* message);
typedef Status(WINAPI* NotificationHookProc)(ULONG_PTR* token);
typedef VOID(WINAPI* NotificationUnhookProc)(ULONG_PTR token);

struct GdiplusStartupInput {
  UINT32 GdiplusVersion;
  DebugEventProc DebugEventCallback;
  BOOL SuppressBackgroundThread;
  BOOL SuppressExternalCodecs;
  GdiplusStartupInput(DebugEventProc debugEventCallback = NULL, BOOL suppressBackgroundThread = FALSE,
                      BOOL suppressExternalCodecs = FALSE)
      : GdiplusVersion(1),
        DebugEventCallback(debugEventCallback),
        SuppressBackgroundThread(suppressBackgroundThread),
        SuppressExternalCodecs(suppressExternalCodecs) {}
};

struct GdiplusStartupOutput {
  NotificationHookProc NotificationHook;
  NotificationUnhookProc NotificationUnhook;
};

extern "C" {
Status WINAPI GdiplusStartup(ULONG_PTR* token, const GdiplusStartupInput* input, GdiplusStartupOutput* output);
VOID WINAPI GdiplusShutdown(ULONG_PTR token);
}

// The objects behind the flat API's pointers.
class GpImage {};
class GpBitmap : public GpImage {};

namespace DllExports {
extern "C" {
GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR* filename, GpBitmap** bitmap);
GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR* filename, GpBitmap** bitmap);
GpStatus WINAPI GdipCreateBitmapFromStream(IStream* stream, GpBitmap** bitmap);
GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream* stream, GpBitmap** bitmap);
GpStatus WINAPI GdipLoadImageFromFile(const WCHAR* filename, GpImage** image);
GpStatus WINAPI GdipLoadImageFromStream(IStream* stream, GpImage** image);
GpStatus WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format, BYTE* scan0,
                                          GpBitmap** bitmap);
GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE hpal, GpBitmap** bitmap);
GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap* bitmap, HBITMAP* hbmReturn, ARGB background);
GpStatus WINAPI GdipCloneImage(GpImage* image, GpImage** cloneImage);
GpStatus WINAPI GdipDisposeImage(GpImage* image);
GpStatus WINAPI GdipGetImageWidth(GpImage* image, UINT* width);
GpStatus WINAPI GdipGetImageHeight(GpImage* image, UINT* height);
GpStatus WINAPI GdipGetImagePixelFormat(GpImage* image, PixelFormat* format);
GpStatus WINAPI GdipGetImageRawFormat(GpImage* image, GUID* format);
GpStatus WINAPI GdipGetImagePaletteSize(GpImage* image, INT* size);
GpStatus WINAPI GdipGetImagePalette(GpImage* image, ColorPalette* palette, INT size);
GpStatus WINAPI GdipSetImagePalette(GpImage* image, const ColorPalette* palette);
GpStatus WINAPI GdipBitmapGetPixel(GpBitmap* bitmap, INT x, INT y, ARGB* color);
GpStatus WINAPI GdipBitmapSetPixel(GpBitmap* bitmap, INT x, INT y, ARGB color);
GpStatus WINAPI GdipBitmapLockBits(GpBitmap* bitmap, const Rect* rect, UINT flags, PixelFormat format,
                                   BitmapData* lockedBitmapData);
GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap* bitmap, BitmapData* lockedBitmapData);
GpStatus WINAPI GdipSaveImageToFile(GpImage* image, const WCHAR* filename, const CLSID* clsidEncoder,
                                    const EncoderParameters* encoderParams);
GpStatus WINAPI GdipSaveImageToStream(GpImage* image, IStream* stream, const CLSID* clsidEncoder,
                                      const EncoderParameters* encoderParams);
GpStatus WINAPI GdipGetImageDecodersSize(UINT* numDecoders, UINT* size);
GpStatus WINAPI GdipGetImageDecoders(UINT numDecoders, UINT size, ImageCodecInfo* decoders);
GpStatus WINAPI GdipGetImageEncodersSize(UINT* numEncoders, UINT* size);
GpStatus WINAPI GdipGetImageEncoders(UINT numEncoders, UINT size, ImageCodecInfo* encoders);
}
}  // namespace DllExports

}  // namespace Gdiplus

#endif  // _WINPORT_GDIPLUS_H
