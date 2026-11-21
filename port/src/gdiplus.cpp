// gdiplus.cpp - GDI+'s bitmaps and image codecs, for CImage: bitmaps in
// any of the GDI pixel formats, their pixels locked and converted, and
// BMP, PNG, JPEG and GIF read (stb_image) and written (PNG and JPEG with
// stb_image_write, BMP and GIF by hand).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <gdiplus.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <vector>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "third_party/stb_image.h"
#include "third_party/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

using namespace Gdiplus;

namespace {

// A bitmap: its pixels top-down in its own format, rows of a whole
// number of DWORDs, and for an indexed one its palette.
struct Bitmap : GpBitmap {
  UINT width = 0, height = 0;
  PixelFormat format = PixelFormat32bppARGB;
  INT stride = 0;
  std::vector<BYTE> bits;
  std::vector<ARGB> palette;
  UINT paletteFlags = 0;
  GUID raw = ImageFormatMemoryBMP;
  // A lock in progress.
  bool locked = false;
  Rect lockRect;
  UINT lockFlags = 0;
  std::vector<BYTE> lockBuffer;

  BYTE* Row(UINT y) { return bits.data() + (size_t)y * stride; }
};

Bitmap* AsBitmap(GpImage* image) { return static_cast<Bitmap*>(image); }

bool KnownFormat(PixelFormat f) {
  switch (f) {
    case PixelFormat1bppIndexed:
    case PixelFormat4bppIndexed:
    case PixelFormat8bppIndexed:
    case PixelFormat16bppRGB555:
    case PixelFormat16bppRGB565:
    case PixelFormat16bppARGB1555:
    case PixelFormat24bppRGB:
    case PixelFormat32bppRGB:
    case PixelFormat32bppARGB:
    case PixelFormat32bppPARGB:
      return true;
    default:
      return false;
  }
}

INT StrideFor(UINT width, PixelFormat f) { return (INT)((((size_t)width * GetPixelFormatSize(f)) + 31) / 32 * 4); }

Bitmap* NewBitmap(UINT w, UINT h, PixelFormat f) {
  Bitmap* b = new Bitmap;
  b->width = w;
  b->height = h;
  b->format = f;
  b->stride = StrideFor(w, f);
  b->bits.assign((size_t)b->stride * h, 0);
  if (IsIndexedPixelFormat(f)) b->palette.assign((size_t)1 << GetPixelFormatSize(f), 0xFF000000u);
  return b;
}

// One pixel of a row as ARGB, not premultiplied.
ARGB ReadPixel(const Bitmap* b, const BYTE* row, UINT x) {
  switch (b->format) {
    case PixelFormat1bppIndexed:
    case PixelFormat4bppIndexed:
    case PixelFormat8bppIndexed: {
      UINT bpp = GetPixelFormatSize(b->format);
      UINT bit = x * bpp;
      UINT idx = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
      return idx < b->palette.size() ? b->palette[idx] : 0xFF000000u;
    }
    case PixelFormat16bppRGB555:
    case PixelFormat16bppARGB1555: {
      UINT v = row[x * 2] | (row[x * 2 + 1] << 8);
      UINT r = (v >> 10) & 31, g = (v >> 5) & 31, bl = v & 31;
      ARGB a = b->format == PixelFormat16bppARGB1555 ? ((v & 0x8000) ? 0xFFu : 0u) : 0xFFu;
      return a << 24 | (r * 255 / 31) << 16 | (g * 255 / 31) << 8 | (bl * 255 / 31);
    }
    case PixelFormat16bppRGB565: {
      UINT v = row[x * 2] | (row[x * 2 + 1] << 8);
      UINT r = (v >> 11) & 31, g = (v >> 5) & 63, bl = v & 31;
      return 0xFF000000u | (r * 255 / 31) << 16 | (g * 255 / 63) << 8 | (bl * 255 / 31);
    }
    case PixelFormat24bppRGB: {
      const BYTE* p = row + x * 3;
      return 0xFF000000u | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    }
    case PixelFormat32bppRGB: {
      const BYTE* p = row + x * 4;
      return 0xFF000000u | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    }
    case PixelFormat32bppPARGB: {
      const BYTE* p = row + x * 4;
      UINT a = p[3];
      if (a == 0) return 0;
      auto un = [a](UINT c) { return (ARGB)std::min<UINT>(255, (c * 255 + a / 2) / a); };
      return (ARGB)a << 24 | un(p[2]) << 16 | un(p[1]) << 8 | un(p[0]);
    }
    default: {  // 32bppARGB
      const BYTE* p = row + x * 4;
      return (ARGB)p[3] << 24 | (ARGB)p[2] << 16 | (ARGB)p[1] << 8 | p[0];
    }
  }
}

// One pixel written into a row of a format that is not indexed.
void WritePixel(PixelFormat f, BYTE* row, UINT x, ARGB c) {
  UINT a = c >> 24, r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, bl = c & 0xFF;
  switch (f) {
    case PixelFormat16bppRGB555:
    case PixelFormat16bppARGB1555: {
      UINT v = (r >> 3) << 10 | (g >> 3) << 5 | (bl >> 3);
      if (f == PixelFormat16bppARGB1555 && a >= 128) v |= 0x8000;
      row[x * 2] = (BYTE)v;
      row[x * 2 + 1] = (BYTE)(v >> 8);
      break;
    }
    case PixelFormat16bppRGB565: {
      UINT v = (r >> 3) << 11 | (g >> 2) << 5 | (bl >> 3);
      row[x * 2] = (BYTE)v;
      row[x * 2 + 1] = (BYTE)(v >> 8);
      break;
    }
    case PixelFormat24bppRGB:
      row[x * 3] = (BYTE)bl, row[x * 3 + 1] = (BYTE)g, row[x * 3 + 2] = (BYTE)r;
      break;
    case PixelFormat32bppRGB:
      row[x * 4] = (BYTE)bl, row[x * 4 + 1] = (BYTE)g, row[x * 4 + 2] = (BYTE)r, row[x * 4 + 3] = 0xFF;
      break;
    case PixelFormat32bppPARGB:
      row[x * 4] = (BYTE)((bl * a + 127) / 255), row[x * 4 + 1] = (BYTE)((g * a + 127) / 255);
      row[x * 4 + 2] = (BYTE)((r * a + 127) / 255), row[x * 4 + 3] = (BYTE)a;
      break;
    default:
      row[x * 4] = (BYTE)bl, row[x * 4 + 1] = (BYTE)g, row[x * 4 + 2] = (BYTE)r, row[x * 4 + 3] = (BYTE)a;
      break;
  }
}

// Copies part of a row between formats; indexed formats only as they are.
bool ConvertRow(const Bitmap* from, const BYTE* src, UINT x0, PixelFormat to, BYTE* dst, UINT n) {
  if (IsIndexedPixelFormat(to) || IsIndexedPixelFormat(from->format)) {
    if (to == from->format) {
      UINT bpp = GetPixelFormatSize(to);
      if ((x0 * bpp) % 8 == 0) {
        memcpy(dst, src + x0 * bpp / 8, ((size_t)n * bpp + 7) / 8);
      } else {
        memset(dst, 0, ((size_t)n * bpp + 7) / 8);
        for (UINT i = 0; i < n; i++) {
          UINT sb = (x0 + i) * bpp, db = i * bpp;
          UINT v = (src[sb / 8] >> (8 - bpp - sb % 8)) & ((1u << bpp) - 1);
          dst[db / 8] |= (BYTE)(v << (8 - bpp - db % 8));
        }
      }
      return true;
    }
    if (IsIndexedPixelFormat(to)) return false;
  }
  if (to == from->format) {
    memcpy(dst, src + (size_t)x0 * GetPixelFormatSize(to) / 8, (size_t)n * GetPixelFormatSize(to) / 8);
    return true;
  }
  for (UINT i = 0; i < n; i++) WritePixel(to, dst, i, ReadPixel(from, src, x0 + i));
  return true;
}

// ---- reading files

std::vector<BYTE> ReadAll(IStream* s) {
  std::vector<BYTE> out;
  BYTE buf[65536];
  for (;;) {
    ULONG got = 0;
    HRESULT hr = s->Read(buf, sizeof buf, &got);
    if (got) out.insert(out.end(), buf, buf + got);
    if (FAILED(hr) || got == 0) break;
  }
  return out;
}

bool ReadFile(const WCHAR* name, std::vector<BYTE>* out) {
  HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  BYTE buf[65536];
  DWORD got;
  while (::ReadFile(h, buf, sizeof buf, &got, NULL) && got) out->insert(out->end(), buf, buf + got);
  CloseHandle(h);
  return true;
}

GUID Sniff(const std::vector<BYTE>& d) {
  if (d.size() >= 8 && memcmp(d.data(), "\x89PNG\r\n\x1a\n", 8) == 0) return ImageFormatPNG;
  if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ImageFormatJPEG;
  if (d.size() >= 2 && d[0] == 'B' && d[1] == 'M') return ImageFormatBMP;
  if (d.size() >= 6 && (memcmp(d.data(), "GIF87a", 6) == 0 || memcmp(d.data(), "GIF89a", 6) == 0))
    return ImageFormatGIF;
  return ImageFormatUndefined;
}

// An uncompressed BMP of 1, 4 or 8 bits, which keeps its palette; NULL
// for any other.
Bitmap* DecodeIndexedBmp(const std::vector<BYTE>& d) {
  auto u16 = [&](size_t o) { return (UINT)(d[o] | d[o + 1] << 8); };
  auto u32 = [&](size_t o) { return (UINT)(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (UINT)d[o + 3] << 24); };
  if (d.size() < 54) return NULL;
  UINT offBits = u32(10), hdr = u32(14);
  if (hdr < 40 || 14 + (size_t)hdr > d.size()) return NULL;
  int w = (int)u32(18), h = (int)u32(22);
  UINT bpp = u16(28), compression = u32(30), used = u32(46);
  if ((bpp != 1 && bpp != 4 && bpp != 8) || compression != 0 || w <= 0 || h == 0 || w > 65535) return NULL;
  bool topDown = h < 0;
  UINT height = (UINT)(topDown ? -h : h);
  if (height > 65535) return NULL;
  PixelFormat f = bpp == 1 ? PixelFormat1bppIndexed : bpp == 4 ? PixelFormat4bppIndexed : PixelFormat8bppIndexed;
  UINT colors = used && used <= (1u << bpp) ? used : 1u << bpp;
  size_t pal = 14 + hdr;
  INT stride = StrideFor((UINT)w, f);
  if (pal + (size_t)colors * 4 > d.size() || offBits + (size_t)stride * height > d.size()) return NULL;
  Bitmap* b = NewBitmap((UINT)w, height, f);
  b->palette.assign(colors, 0xFF000000u);
  for (UINT i = 0; i < colors; i++) {
    const BYTE* q = &d[pal + i * 4];
    b->palette[i] = 0xFF000000u | (ARGB)q[2] << 16 | (ARGB)q[1] << 8 | q[0];
  }
  for (UINT y = 0; y < height; y++) {
    UINT src = topDown ? y : height - 1 - y;
    memcpy(b->Row(y), &d[offBits + (size_t)src * stride], (size_t)stride);
  }
  return b;
}

// A decoded image: a BMP with a palette as it is, else 24 bits when it
// has no alpha and 32 bits with it.
Status Decode(const std::vector<BYTE>& data, GpBitmap** out) {
  if (!out) return InvalidParameter;
  *out = NULL;
  GUID kind = Sniff(data);
  if (kind == ImageFormatUndefined) return UnknownImageFormat;
  if (IsEqualGUID(kind, ImageFormatBMP)) {
    if (Bitmap* b = DecodeIndexedBmp(data)) {
      b->raw = kind;
      *out = b;
      return Ok;
    }
  }
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &comp, 4);
  if (!px) return UnknownImageFormat;
  bool alpha = comp == 2 || comp == 4;
  Bitmap* b = NewBitmap((UINT)w, (UINT)h, alpha ? PixelFormat32bppARGB : PixelFormat24bppRGB);
  b->raw = kind;
  for (int y = 0; y < h; y++) {
    const stbi_uc* s = px + (size_t)y * w * 4;
    for (int x = 0; x < w; x++)
      WritePixel(b->format, b->Row(y), x,
                 (ARGB)s[x * 4 + 3] << 24 | (ARGB)s[x * 4] << 16 | (ARGB)s[x * 4 + 1] << 8 | s[x * 4 + 2]);
  }
  stbi_image_free(px);
  *out = b;
  return Ok;
}

// ---- codecs

struct Codec {
  CLSID clsid;
  const GUID* format;
  const WCHAR* name;
  const WCHAR* description;
  const WCHAR* extensions;
  const WCHAR* mime;
  bool encoder;
};

#define CODEC_CLSID(n) {0x557cf400 + (n), 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}}
const Codec kCodecs[] = {
    {CODEC_CLSID(0), &ImageFormatBMP, L"Built-in BMP Codec", L"BMP", L"*.BMP;*.DIB;*.RLE", L"image/bmp", true},
    {CODEC_CLSID(1), &ImageFormatJPEG, L"Built-in JPEG Codec", L"JPEG", L"*.JPG;*.JPEG;*.JPE;*.JFIF",
     L"image/jpeg", true},
    {CODEC_CLSID(2), &ImageFormatGIF, L"Built-in GIF Codec", L"GIF", L"*.GIF", L"image/gif", true},
    {CODEC_CLSID(6), &ImageFormatPNG, L"Built-in PNG Codec", L"PNG", L"*.PNG", L"image/png", true},
};

size_t WideBytes(const WCHAR* s) { return (wcslen(s) + 1) * sizeof(WCHAR); }

void CodecSizes(bool encoders, UINT* n, UINT* size) {
  *n = 0;
  *size = 0;
  for (const Codec& c : kCodecs) {
    if (encoders && !c.encoder) continue;
    ++*n;
    *size += (UINT)(sizeof(ImageCodecInfo) + WideBytes(c.name) + WideBytes(L"") + WideBytes(c.description) +
                    WideBytes(c.extensions) + WideBytes(c.mime));
  }
}

Status FillCodecs(bool encoders, UINT n, UINT size, ImageCodecInfo* out) {
  UINT wantN, wantSize;
  CodecSizes(encoders, &wantN, &wantSize);
  if (!out || n != wantN || size < wantSize) return InvalidParameter;
  BYTE* strings = (BYTE*)(out + n);
  auto put = [&](const WCHAR* s) {
    WCHAR* p = (WCHAR*)strings;
    memcpy(p, s, WideBytes(s));
    strings += WideBytes(s);
    return (const WCHAR*)p;
  };
  UINT i = 0;
  for (const Codec& c : kCodecs) {
    if (encoders && !c.encoder) continue;
    ImageCodecInfo& info = out[i++];
    memset(&info, 0, sizeof info);
    info.Clsid = c.clsid;
    info.FormatID = *c.format;
    info.CodecName = put(c.name);
    info.DllName = put(L"");
    info.FormatDescription = put(c.description);
    info.FilenameExtension = put(c.extensions);
    info.MimeType = put(c.mime);
    info.Flags = ImageCodecFlagsBuiltin | ImageCodecFlagsSupportBitmap |
                 (c.encoder && encoders ? ImageCodecFlagsEncoder : ImageCodecFlagsDecoder);
    info.Version = 1;
  }
  return Ok;
}

void Put16(std::vector<BYTE>* v, UINT x) {
  v->push_back((BYTE)x);
  v->push_back((BYTE)(x >> 8));
}
void Put32(std::vector<BYTE>* v, UINT x) {
  Put16(v, x & 0xFFFF);
  Put16(v, x >> 16);
}

// A BMP file: indexed formats with their palette, 32 bits as they are,
// the rest as 24 bits.
std::vector<BYTE> EncodeBmp(Bitmap* b) {
  bool indexed = IsIndexedPixelFormat(b->format);
  PixelFormat f = indexed ? b->format
                  : (b->format == PixelFormat32bppARGB || b->format == PixelFormat32bppPARGB ||
                     b->format == PixelFormat32bppRGB)
                      ? PixelFormat32bppARGB
                      : PixelFormat24bppRGB;
  UINT bpp = GetPixelFormatSize(f);
  INT stride = StrideFor(b->width, f);
  UINT colors = indexed ? (UINT)b->palette.size() : 0;
  UINT offset = 14 + 40 + colors * 4;
  UINT imageSize = (UINT)stride * b->height;
  std::vector<BYTE> out;
  out.push_back('B');
  out.push_back('M');
  Put32(&out, offset + imageSize);
  Put32(&out, 0);
  Put32(&out, offset);
  Put32(&out, 40);
  Put32(&out, b->width);
  Put32(&out, b->height);  // bottom-up
  Put16(&out, 1);
  Put16(&out, bpp);
  Put32(&out, 0);  // BI_RGB
  Put32(&out, imageSize);
  Put32(&out, 2835);
  Put32(&out, 2835);
  Put32(&out, colors);
  Put32(&out, 0);
  for (UINT i = 0; i < colors; i++) Put32(&out, b->palette[i] & 0xFFFFFF);
  std::vector<BYTE> row((size_t)stride, 0);
  for (UINT y = b->height; y-- > 0;) {
    std::fill(row.begin(), row.end(), 0);
    ConvertRow(b, b->Row(y), 0, f, row.data(), b->width);
    out.insert(out.end(), row.begin(), row.end());
  }
  return out;
}

// ---- GIF

// GIF's LZW: variable-length codes from minCode + 1 bits up to 12, least
// significant bit first, in sub-blocks of up to 255 bytes.
void LzwEncode(const std::vector<BYTE>& idx, int minCode, std::vector<BYTE>* out) {
  std::vector<BYTE> packed;
  uint32_t acc = 0;
  int nbits = 0;
  auto emit = [&](UINT code, int size) {
    acc |= (uint32_t)code << nbits;
    nbits += size;
    while (nbits >= 8) {
      packed.push_back((BYTE)acc);
      acc >>= 8;
      nbits -= 8;
    }
  };
  const UINT clear = 1u << minCode, eoi = clear + 1;
  UINT next = eoi + 1;
  int size = minCode + 1;
  // The table: (prefix code, byte) to code, by open addressing.
  const size_t kSlots = 8192;
  std::vector<uint32_t> keys(kSlots, 0xFFFFFFFFu);
  std::vector<uint16_t> codes(kSlots);
  auto slot = [&](uint32_t key) {
    size_t h = (key * 2654435761u) & (kSlots - 1);
    while (keys[h] != 0xFFFFFFFFu && keys[h] != key) h = (h + 1) & (kSlots - 1);
    return h;
  };
  emit(clear, size);
  if (!idx.empty()) {
    UINT prefix = idx[0];
    for (size_t i = 1; i < idx.size(); i++) {
      uint32_t key = (uint32_t)prefix << 8 | idx[i];
      size_t h = slot(key);
      if (keys[h] == key) {
        prefix = codes[h];
        continue;
      }
      emit(prefix, size);
      // A decoder adds its entry a code later, so the width grows once
      // the next code would not fit; a full table starts over.
      if (next < 4095) {
        keys[h] = key;
        codes[h] = (uint16_t)next++;
        if (next > (1u << size) && size < 12) size++;
      } else {
        emit(clear, size);
        std::fill(keys.begin(), keys.end(), 0xFFFFFFFFu);
        next = eoi + 1;
        size = minCode + 1;
      }
      prefix = idx[i];
    }
    emit(prefix, size);
  }
  emit(eoi, size);
  if (nbits) packed.push_back((BYTE)acc);
  out->push_back((BYTE)minCode);
  for (size_t off = 0; off < packed.size(); off += 255) {
    size_t n = std::min<size_t>(255, packed.size() - off);
    out->push_back((BYTE)n);
    out->insert(out->end(), packed.begin() + off, packed.begin() + off + n);
  }
  out->push_back(0);
}

// One frame, its colors in a global table: an indexed bitmap's palette
// as it is; else the image's own colors when there are 256 or fewer,
// and a 6-7-6 color cube when there are more. Pixels with less than half
// alpha, or an indexed bitmap's colors of alpha 0, are transparent.
std::vector<BYTE> EncodeGif(Bitmap* b) {
  std::vector<ARGB> table;
  std::vector<BYTE> idx((size_t)b->width * b->height);
  int transparent = -1;
  if (IsIndexedPixelFormat(b->format)) {
    table = b->palette;
    if (table.empty()) table.push_back(0xFF000000u);
    for (size_t i = 0; i < table.size(); i++)
      if ((table[i] >> 24) == 0) {
        transparent = (int)i;
        break;
      }
    UINT bpp = GetPixelFormatSize(b->format);
    for (UINT y = 0; y < b->height; y++) {
      const BYTE* row = b->Row(y);
      for (UINT x = 0; x < b->width; x++) {
        UINT bit = x * bpp;
        UINT v = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
        idx[(size_t)y * b->width + x] = (BYTE)(v < table.size() ? v : 0);
      }
    }
  } else {
    std::vector<ARGB> px(idx.size());
    bool clear = false;
    for (UINT y = 0; y < b->height; y++)
      for (UINT x = 0; x < b->width; x++) {
        ARGB c = ReadPixel(b, b->Row(y), x);
        if ((c >> 24) < 0x80) {
          clear = true;
          c = 0;
        } else {
          c &= 0xFFFFFF;
          c |= 0xFF000000u;
        }
        px[(size_t)y * b->width + x] = c;
      }
    std::vector<ARGB> colors;
    for (ARGB c : px)
      if (c) colors.push_back(c);
    std::sort(colors.begin(), colors.end());
    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
    size_t room = clear ? 255 : 256;
    if (clear) {
      transparent = 0;
      table.push_back(0);
    }
    if (colors.size() <= room) {
      table.insert(table.end(), colors.begin(), colors.end());
      for (size_t i = 0; i < px.size(); i++)
        idx[i] = px[i] ? (BYTE)(std::lower_bound(colors.begin(), colors.end(), px[i]) - colors.begin() +
                                (clear ? 1 : 0))
                       : 0;
    } else {
      size_t base = table.size();
      for (UINT r = 0; r < 6; r++)
        for (UINT g = 0; g < 7; g++)
          for (UINT bl = 0; bl < 6; bl++)
            table.push_back(0xFF000000u | (r * 51) << 16 | (g * 255 / 6) << 8 | bl * 51);
      for (size_t i = 0; i < px.size(); i++) {
        ARGB c = px[i];
        if (!c) {
          idx[i] = 0;
          continue;
        }
        UINT r = (((c >> 16) & 0xFF) * 5 + 127) / 255, g = (((c >> 8) & 0xFF) * 6 + 127) / 255,
             bl = ((c & 0xFF) * 5 + 127) / 255;
        idx[i] = (BYTE)(base + (r * 7 + g) * 6 + bl);
      }
    }
  }
  int tableBits = 1;
  while ((1u << tableBits) < table.size()) tableBits++;
  std::vector<BYTE> out;
  auto put16 = [&](UINT v) {
    out.push_back((BYTE)v);
    out.push_back((BYTE)(v >> 8));
  };
  const char* sig = "GIF89a";
  out.insert(out.end(), sig, sig + 6);
  put16(b->width);
  put16(b->height);
  out.push_back((BYTE)(0x80 | (tableBits - 1) << 4 | (tableBits - 1)));
  out.push_back(0);  // background
  out.push_back(0);  // aspect
  for (UINT i = 0; i < (1u << tableBits); i++) {
    ARGB c = i < table.size() ? table[i] : 0;
    out.push_back((BYTE)(c >> 16));
    out.push_back((BYTE)(c >> 8));
    out.push_back((BYTE)c);
  }
  if (transparent >= 0) {
    const BYTE gce[] = {0x21, 0xF9, 4, 1, 0, 0, (BYTE)transparent, 0};
    out.insert(out.end(), gce, gce + sizeof gce);
  }
  out.push_back(0x2C);
  put16(0);
  put16(0);
  put16(b->width);
  put16(b->height);
  out.push_back(0);
  LzwEncode(idx, std::max(2, tableBits), &out);
  out.push_back(0x3B);
  return out;
}

void Append(void* context, void* data, int size) {
  std::vector<BYTE>* v = (std::vector<BYTE>*)context;
  v->insert(v->end(), (BYTE*)data, (BYTE*)data + size);
}

Status Encode(Bitmap* b, const CLSID* clsid, const EncoderParameters* params, std::vector<BYTE>* out) {
  const Codec* codec = NULL;
  for (const Codec& c : kCodecs)
    if (c.encoder && clsid && c.clsid == *clsid) codec = &c;
  if (!codec) return InvalidParameter;
  if (*codec->format == ImageFormatBMP) {
    *out = EncodeBmp(b);
    return Ok;
  }
  if (*codec->format == ImageFormatGIF) {
    *out = EncodeGif(b);
    return Ok;
  }
  bool alpha = IsAlphaPixelFormat(b->format);
  if (IsIndexedPixelFormat(b->format))
    for (ARGB c : b->palette)
      if ((c >> 24) != 0xFF) alpha = true;
  bool jpeg = *codec->format == ImageFormatJPEG;
  if (jpeg) alpha = false;
  int comp = alpha ? 4 : 3;
  std::vector<BYTE> rgba((size_t)b->width * b->height * comp);
  for (UINT y = 0; y < b->height; y++)
    for (UINT x = 0; x < b->width; x++) {
      ARGB c = ReadPixel(b, b->Row(y), x);
      BYTE* p = &rgba[((size_t)y * b->width + x) * comp];
      p[0] = (BYTE)(c >> 16), p[1] = (BYTE)(c >> 8), p[2] = (BYTE)c;
      if (alpha) p[3] = (BYTE)(c >> 24);
    }
  int ok;
  if (jpeg) {
    int quality = 90;
    for (UINT i = 0; params && i < params->Count; i++)
      if (params->Parameter[i].Guid == EncoderQuality && params->Parameter[i].NumberOfValues > 0 &&
          params->Parameter[i].Value)
        quality = (int)*(const ULONG*)params->Parameter[i].Value;
    quality = std::max(1, std::min(100, quality));
    ok = stbi_write_jpg_to_func(Append, out, (int)b->width, (int)b->height, comp, rgba.data(), quality);
  } else {
    ok = stbi_write_png_to_func(Append, out, (int)b->width, (int)b->height, comp, rgba.data(),
                                (int)b->width * comp);
  }
  return ok ? Ok : GenericError;
}

}  // namespace

namespace Gdiplus {

extern "C" {

Status WINAPI GdiplusStartup(ULONG_PTR* token, const GdiplusStartupInput* input, GdiplusStartupOutput* output) {
  if (!token || !input) return InvalidParameter;
  if (input->GdiplusVersion < 1 || input->GdiplusVersion > 2) return UnsupportedGdiplusVersion;
  if (input->SuppressBackgroundThread && !output) return InvalidParameter;
  if (output) memset(output, 0, sizeof *output);
  *token = 1;
  return Ok;
}

VOID WINAPI GdiplusShutdown(ULONG_PTR) {}

}  // extern "C"

namespace DllExports {

extern "C" {

GpStatus WINAPI GdipCreateBitmapFromFile(const WCHAR* filename, GpBitmap** bitmap) {
  if (!filename || !bitmap) return InvalidParameter;
  std::vector<BYTE> data;
  if (!ReadFile(filename, &data)) return GetLastError() == ERROR_ACCESS_DENIED ? AccessDenied : OutOfMemory;
  return Decode(data, bitmap);
}
GpStatus WINAPI GdipCreateBitmapFromFileICM(const WCHAR* filename, GpBitmap** bitmap) {
  return GdipCreateBitmapFromFile(filename, bitmap);
}
GpStatus WINAPI GdipCreateBitmapFromStream(IStream* stream, GpBitmap** bitmap) {
  if (!stream || !bitmap) return InvalidParameter;
  return Decode(ReadAll(stream), bitmap);
}
GpStatus WINAPI GdipCreateBitmapFromStreamICM(IStream* stream, GpBitmap** bitmap) {
  return GdipCreateBitmapFromStream(stream, bitmap);
}
GpStatus WINAPI GdipLoadImageFromFile(const WCHAR* filename, GpImage** image) {
  GpBitmap* b = NULL;
  GpStatus st = GdipCreateBitmapFromFile(filename, &b);
  if (image) *image = b;
  return st;
}
GpStatus WINAPI GdipLoadImageFromStream(IStream* stream, GpImage** image) {
  GpBitmap* b = NULL;
  GpStatus st = GdipCreateBitmapFromStream(stream, &b);
  if (image) *image = b;
  return st;
}

GpStatus WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, PixelFormat format, BYTE* scan0,
                                          GpBitmap** bitmap) {
  if (!bitmap || width <= 0 || height <= 0 || !KnownFormat(format)) return InvalidParameter;
  *bitmap = NULL;
  Bitmap* b = NewBitmap((UINT)width, (UINT)height, format);
  if (scan0) {
    size_t n = ((size_t)width * GetPixelFormatSize(format) + 7) / 8;
    for (INT y = 0; y < height; y++) memcpy(b->Row(y), scan0 + (ptrdiff_t)y * stride, n);
  }
  *bitmap = b;
  return Ok;
}

GpStatus WINAPI GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE, GpBitmap** bitmap) {
  if (!hbm || !bitmap) return InvalidParameter;
  *bitmap = NULL;
  BITMAP bm;
  if (!GetObjectW(hbm, sizeof bm, &bm)) return InvalidParameter;
  Bitmap* b = NewBitmap((UINT)bm.bmWidth, (UINT)abs(bm.bmHeight), PixelFormat32bppRGB);
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = bm.bmWidth;
  bi.bmiHeader.biHeight = -(LONG)b->height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  HDC dc = CreateCompatibleDC(NULL);
  int got = GetDIBits(dc, hbm, 0, b->height, b->bits.data(), &bi, DIB_RGB_COLORS);
  DeleteDC(dc);
  if (got <= 0) {
    delete b;
    return GenericError;
  }
  *bitmap = b;
  return Ok;
}

GpStatus WINAPI GdipCreateHBITMAPFromBitmap(GpBitmap* bitmap, HBITMAP* hbmReturn, ARGB background) {
  Bitmap* b = AsBitmap(bitmap);
  if (!b || !hbmReturn) return InvalidParameter;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = (LONG)b->width;
  bi.bmiHeader.biHeight = -(LONG)b->height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = NULL;
  HBITMAP h = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!h) return OutOfMemory;
  // Blended onto the background, as GDI+ does.
  for (UINT y = 0; y < b->height; y++)
    for (UINT x = 0; x < b->width; x++) {
      ARGB c = ReadPixel(b, b->Row(y), x);
      UINT a = c >> 24;
      auto mix = [&](int shift) {
        UINT f = (c >> shift) & 0xFF, k = (background >> shift) & 0xFF;
        return (ARGB)((f * a + k * (255 - a) + 127) / 255) << shift;
      };
      ((ARGB*)bits)[(size_t)y * b->width + x] = 0xFF000000u | mix(16) | mix(8) | mix(0);
    }
  *hbmReturn = h;
  return Ok;
}

GpStatus WINAPI GdipCloneImage(GpImage* image, GpImage** cloneImage) {
  Bitmap* b = AsBitmap(image);
  if (!b || !cloneImage) return InvalidParameter;
  Bitmap* c = new Bitmap;
  c->width = b->width;
  c->height = b->height;
  c->format = b->format;
  c->stride = b->stride;
  c->bits = b->bits;
  c->palette = b->palette;
  c->paletteFlags = b->paletteFlags;
  c->raw = b->raw;
  *cloneImage = c;
  return Ok;
}

GpStatus WINAPI GdipDisposeImage(GpImage* image) {
  if (!image) return InvalidParameter;
  delete AsBitmap(image);
  return Ok;
}

GpStatus WINAPI GdipGetImageWidth(GpImage* image, UINT* width) {
  if (!image || !width) return InvalidParameter;
  *width = AsBitmap(image)->width;
  return Ok;
}
GpStatus WINAPI GdipGetImageHeight(GpImage* image, UINT* height) {
  if (!image || !height) return InvalidParameter;
  *height = AsBitmap(image)->height;
  return Ok;
}
GpStatus WINAPI GdipGetImagePixelFormat(GpImage* image, PixelFormat* format) {
  if (!image || !format) return InvalidParameter;
  *format = AsBitmap(image)->format;
  return Ok;
}
GpStatus WINAPI GdipGetImageRawFormat(GpImage* image, GUID* format) {
  if (!image || !format) return InvalidParameter;
  *format = AsBitmap(image)->raw;
  return Ok;
}

GpStatus WINAPI GdipGetImagePaletteSize(GpImage* image, INT* size) {
  if (!image || !size) return InvalidParameter;
  Bitmap* b = AsBitmap(image);
  *size = (INT)(offsetof(ColorPalette, Entries) + std::max<size_t>(1, b->palette.size()) * sizeof(ARGB));
  return Ok;
}
GpStatus WINAPI GdipGetImagePalette(GpImage* image, ColorPalette* palette, INT size) {
  if (!image || !palette) return InvalidParameter;
  Bitmap* b = AsBitmap(image);
  INT need = 0;
  GdipGetImagePaletteSize(image, &need);
  if (size < need) return InvalidParameter;
  palette->Flags = b->paletteFlags;
  palette->Count = (UINT)b->palette.size();
  if (!b->palette.empty()) memcpy(palette->Entries, b->palette.data(), b->palette.size() * sizeof(ARGB));
  return Ok;
}
GpStatus WINAPI GdipSetImagePalette(GpImage* image, const ColorPalette* palette) {
  if (!image || !palette || palette->Count == 0) return InvalidParameter;
  Bitmap* b = AsBitmap(image);
  b->palette.assign(palette->Entries, palette->Entries + palette->Count);
  b->paletteFlags = palette->Flags;
  return Ok;
}

GpStatus WINAPI GdipBitmapGetPixel(GpBitmap* bitmap, INT x, INT y, ARGB* color) {
  Bitmap* b = AsBitmap(bitmap);
  if (!b || !color || x < 0 || y < 0 || (UINT)x >= b->width || (UINT)y >= b->height) return InvalidParameter;
  *color = ReadPixel(b, b->Row(y), (UINT)x);
  return Ok;
}
GpStatus WINAPI GdipBitmapSetPixel(GpBitmap* bitmap, INT x, INT y, ARGB color) {
  Bitmap* b = AsBitmap(bitmap);
  if (!b || x < 0 || y < 0 || (UINT)x >= b->width || (UINT)y >= b->height) return InvalidParameter;
  if (IsIndexedPixelFormat(b->format)) return InvalidParameter;
  WritePixel(b->format, b->Row(y), (UINT)x, color);
  return Ok;
}

GpStatus WINAPI GdipBitmapLockBits(GpBitmap* bitmap, const Rect* rect, UINT flags, PixelFormat format,
                                   BitmapData* data) {
  Bitmap* b = AsBitmap(bitmap);
  if (!b || !data || !(flags & (ImageLockModeRead | ImageLockModeWrite)) || !KnownFormat(format))
    return InvalidParameter;
  if (b->locked) return WrongState;
  Rect r = rect ? *rect : Rect(0, 0, (INT)b->width, (INT)b->height);
  if (r.X < 0 || r.Y < 0 || r.Width <= 0 || r.Height <= 0 || (UINT)(r.X + r.Width) > b->width ||
      (UINT)(r.Y + r.Height) > b->height)
    return InvalidParameter;
  if ((IsIndexedPixelFormat(format) || IsIndexedPixelFormat(b->format)) && format != b->format &&
      IsIndexedPixelFormat(format))
    return InvalidParameter;
  if (!(flags & ImageLockModeUserInputBuf)) {
    data->Stride = StrideFor((UINT)r.Width, format);
    b->lockBuffer.assign((size_t)data->Stride * r.Height, 0);
    data->Scan0 = b->lockBuffer.data();
  } else if (!data->Scan0) {
    return InvalidParameter;
  }
  data->Width = (UINT)r.Width;
  data->Height = (UINT)r.Height;
  data->PixelFormat = format;
  if (flags & ImageLockModeRead)
    for (INT y = 0; y < r.Height; y++)
      if (!ConvertRow(b, b->Row(r.Y + y), (UINT)r.X, format, (BYTE*)data->Scan0 + (ptrdiff_t)y * data->Stride,
                      (UINT)r.Width))
        return InvalidParameter;
  b->locked = true;
  b->lockRect = r;
  b->lockFlags = flags;
  return Ok;
}

GpStatus WINAPI GdipBitmapUnlockBits(GpBitmap* bitmap, BitmapData* data) {
  Bitmap* b = AsBitmap(bitmap);
  if (!b || !data) return InvalidParameter;
  if (!b->locked) return WrongState;
  b->locked = false;
  if (b->lockFlags & ImageLockModeWrite) {
    // Back into the bitmap: through ARGB, or as it is in the same format.
    Bitmap view;
    view.width = data->Width;
    view.height = data->Height;
    view.format = data->PixelFormat;
    view.palette = b->palette;
    for (UINT y = 0; y < data->Height; y++) {
      const BYTE* src = (const BYTE*)data->Scan0 + (ptrdiff_t)y * data->Stride;
      BYTE* dst = b->Row(b->lockRect.Y + y);
      if (view.format == b->format && !IsIndexedPixelFormat(b->format)) {
        UINT bpp = GetPixelFormatSize(b->format);
        memcpy(dst + (size_t)b->lockRect.X * bpp / 8, src, (size_t)data->Width * bpp / 8);
      } else if (IsIndexedPixelFormat(b->format)) {
        if (view.format != b->format) continue;
        UINT bpp = GetPixelFormatSize(b->format);
        for (UINT x = 0; x < data->Width; x++) {
          UINT sb = x * bpp, db = (b->lockRect.X + x) * bpp;
          UINT v = (src[sb / 8] >> (8 - bpp - sb % 8)) & ((1u << bpp) - 1);
          BYTE mask = (BYTE)(((1u << bpp) - 1) << (8 - bpp - db % 8));
          dst[db / 8] = (BYTE)((dst[db / 8] & ~mask) | (v << (8 - bpp - db % 8)));
        }
      } else {
        for (UINT x = 0; x < data->Width; x++)
          WritePixel(b->format, dst, b->lockRect.X + x, ReadPixel(&view, src, x));
      }
    }
  }
  b->lockBuffer.clear();
  return Ok;
}

GpStatus WINAPI GdipSaveImageToFile(GpImage* image, const WCHAR* filename, const CLSID* clsidEncoder,
                                    const EncoderParameters* encoderParams) {
  if (!image || !filename || !clsidEncoder) return InvalidParameter;
  std::vector<BYTE> data;
  GpStatus st = Encode(AsBitmap(image), clsidEncoder, encoderParams, &data);
  if (st != Ok) return st;
  HANDLE h = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return Win32Error;
  DWORD put = 0;
  BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, NULL) && put == data.size();
  CloseHandle(h);
  return ok ? Ok : Win32Error;
}

GpStatus WINAPI GdipSaveImageToStream(GpImage* image, IStream* stream, const CLSID* clsidEncoder,
                                      const EncoderParameters* encoderParams) {
  if (!image || !stream || !clsidEncoder) return InvalidParameter;
  std::vector<BYTE> data;
  GpStatus st = Encode(AsBitmap(image), clsidEncoder, encoderParams, &data);
  if (st != Ok) return st;
  ULONG put = 0;
  HRESULT hr = stream->Write(data.data(), (ULONG)data.size(), &put);
  return SUCCEEDED(hr) && put == data.size() ? Ok : Win32Error;
}

GpStatus WINAPI GdipGetImageDecodersSize(UINT* numDecoders, UINT* size) {
  if (!numDecoders || !size) return InvalidParameter;
  CodecSizes(false, numDecoders, size);
  return Ok;
}
GpStatus WINAPI GdipGetImageDecoders(UINT numDecoders, UINT size, ImageCodecInfo* decoders) {
  return FillCodecs(false, numDecoders, size, decoders);
}
GpStatus WINAPI GdipGetImageEncodersSize(UINT* numEncoders, UINT* size) {
  if (!numEncoders || !size) return InvalidParameter;
  CodecSizes(true, numEncoders, size);
  return Ok;
}
GpStatus WINAPI GdipGetImageEncoders(UINT numEncoders, UINT size, ImageCodecInfo* encoders) {
  return FillCodecs(true, numEncoders, size, encoders);
}

}  // extern "C"

}  // namespace DllExports

}  // namespace Gdiplus
