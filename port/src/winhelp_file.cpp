// winhelp_file.cpp - reads Windows help files (.hlp) and contents
// files (.cnt) for the layer's WinHelp viewer.
//
// A help file is a small file system: a directory (a B+ tree) names the
// internal files, such as |SYSTEM (the title and options), |TOPIC (the
// text, in blocks that may be LZ77 compressed), |Phrases or |PhrIndex
// and |PhrImage (the phrases that compress the text), |FONT, |CONTEXT
// (the hashes of the context names), |CTXOMAP (the [MAP] numbers),
// |KWBTREE and |KWDATA (the index) and |bmN (the pictures). The layout
// follows the description of the format that comes with helpdeco
// (Manfred Winterhoff's helpfile.txt). No code of WinHelp, helpdeco or
// Wine is used.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winhelp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include <algorithm>

namespace winport {
namespace help {

namespace {

// Reads a little-endian 16-bit value.
inline uint16_t U16(const BYTE* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
// Reads a little-endian signed 16-bit value.
inline int16_t S16(const BYTE* p) { return (int16_t)U16(p); }
// Reads a little-endian 32-bit value.
inline uint32_t U32(const BYTE* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// A reader of the packed values of |TOPIC and the pictures. Reading past
// the end gives zeros and sets bad.
struct Reader {
  const BYTE* p;
  const BYTE* end;
  bool bad = false;
  // Makes a reader of the n bytes at p.
  Reader(const BYTE* b, size_t n) : p(b), end(b + n) {}
  // Whether n more bytes are there; sets bad if not.
  bool Has(size_t n) {
    if ((size_t)(end - p) >= n) return true;
    bad = true;
    p = end;
    return false;
  }
  // Reads one byte.
  BYTE Byte() { return Has(1) ? *p++ : 0; }
  // Reads a raw 16-bit value.
  uint16_t Word() {
    if (!Has(2)) return 0;
    uint16_t v = U16(p);
    p += 2;
    return v;
  }
  // Reads a raw 32-bit value.
  uint32_t Dword() {
    if (!Has(4)) return 0;
    uint32_t v = U32(p);
    p += 4;
    return v;
  }
  // A compressed unsigned short: one byte (even) or two (odd).
  unsigned CUShort() {
    if (!Has(1)) return 0;
    if (!(*p & 1)) return *p++ >> 1;
    return Word() >> 1;
  }
  // A compressed signed short.
  int CShort() {
    if (!Has(1)) return 0;
    if (!(*p & 1)) return (*p++ >> 1) - 0x40;
    return (Word() >> 1) - 0x4000;
  }
  // A compressed unsigned long: two bytes (even) or four (odd).
  uint32_t CULong() {
    if (!Has(2)) return 0;
    if (!(*p & 1)) return Word() >> 1;
    return Dword() >> 1;
  }
  // A compressed signed long.
  int32_t CLong() {
    if (!Has(2)) return 0;
    if (!(*p & 1)) return (int32_t)(Word() >> 1) - 0x4000;
    return (int32_t)(Dword() >> 1) - 0x4000000;
  }
  // Reads a NUL-terminated string.
  std::string String() {
    const BYTE* s = p;
    while (p < end && *p) p++;
    std::string r((const char*)s, p - s);
    if (p < end) p++;
    return r;
  }
};

// The values WinHelp adds up for each character of a context name.
const signed char kHashTable[256] = {
    (signed char)0x00, (signed char)0xD1, (signed char)0xD2, (signed char)0xD3, (signed char)0xD4,
    (signed char)0xD5, (signed char)0xD6, (signed char)0xD7, (signed char)0xD8, (signed char)0xD9,
    (signed char)0xDA, (signed char)0xDB, (signed char)0xDC, (signed char)0xDD, (signed char)0xDE,
    (signed char)0xDF, (signed char)0xE0, (signed char)0xE1, (signed char)0xE2, (signed char)0xE3,
    (signed char)0xE4, (signed char)0xE5, (signed char)0xE6, (signed char)0xE7, (signed char)0xE8,
    (signed char)0xE9, (signed char)0xEA, (signed char)0xEB, (signed char)0xEC, (signed char)0xED,
    (signed char)0xEE, (signed char)0xEF, (signed char)0xF0, 0x0B, (signed char)0xF2, (signed char)0xF3,
    (signed char)0xF4, (signed char)0xF5, (signed char)0xF6, (signed char)0xF7, (signed char)0xF8,
    (signed char)0xF9, (signed char)0xFA, (signed char)0xFB, (signed char)0xFC, (signed char)0xFD, 0x0C,
    (signed char)0xFF, 0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0D,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x50, 0x51,
    0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, (signed char)0x80,
    (signed char)0x81, (signed char)0x82, (signed char)0x83, 0x0B, (signed char)0x85, (signed char)0x86,
    (signed char)0x87, (signed char)0x88, (signed char)0x89, (signed char)0x8A, (signed char)0x8B,
    (signed char)0x8C, (signed char)0x8D, (signed char)0x8E, (signed char)0x8F, (signed char)0x90,
    (signed char)0x91, (signed char)0x92, (signed char)0x93, (signed char)0x94, (signed char)0x95,
    (signed char)0x96, (signed char)0x97, (signed char)0x98, (signed char)0x99, (signed char)0x9A,
    (signed char)0x9B, (signed char)0x9C, (signed char)0x9D, (signed char)0x9E, (signed char)0x9F,
    (signed char)0xA0, (signed char)0xA1, (signed char)0xA2, (signed char)0xA3, (signed char)0xA4,
    (signed char)0xA5, (signed char)0xA6, (signed char)0xA7, (signed char)0xA8, (signed char)0xA9,
    (signed char)0xAA, (signed char)0xAB, (signed char)0xAC, (signed char)0xAD, (signed char)0xAE,
    (signed char)0xAF, (signed char)0xB0, (signed char)0xB1, (signed char)0xB2, (signed char)0xB3,
    (signed char)0xB4, (signed char)0xB5, (signed char)0xB6, (signed char)0xB7, (signed char)0xB8,
    (signed char)0xB9, (signed char)0xBA, (signed char)0xBB, (signed char)0xBC, (signed char)0xBD,
    (signed char)0xBE, (signed char)0xBF, (signed char)0xC0, (signed char)0xC1, (signed char)0xC2,
    (signed char)0xC3, (signed char)0xC4, (signed char)0xC5, (signed char)0xC6, (signed char)0xC7,
    (signed char)0xC8, (signed char)0xC9, (signed char)0xCA, (signed char)0xCB, (signed char)0xCC,
    (signed char)0xCD, (signed char)0xCE, (signed char)0xCF,
};

// The code page of a Windows character set.
UINT CodePageOfCharset(int charset) {
  switch (charset) {
    case EASTEUROPE_CHARSET: return 1250;
    case RUSSIAN_CHARSET: return 1251;
    case GREEK_CHARSET: return 1253;
    case TURKISH_CHARSET: return 1254;
    case HEBREW_CHARSET: return 1255;
    case ARABIC_CHARSET: return 1256;
    case BALTIC_CHARSET: return 1257;
    case SHIFTJIS_CHARSET: return 932;
    case GB2312_CHARSET: return 936;
    case HANGUL_CHARSET: return 949;
    case CHINESEBIG5_CHARSET: return 950;
    default: return 1252;
  }
}

// The code page of a language (its primary language).
UINT CodePageOfLcid(unsigned lcid) {
  switch (lcid & 0x3FF) {
    case 0x05: case 0x0E: case 0x15: case 0x18: case 0x1A: case 0x1B: case 0x24: return 1250;
    case 0x19: case 0x22: case 0x23: case 0x02: return 1251;
    case 0x08: return 1253;
    case 0x1F: return 1254;
    case 0x0D: return 1255;
    case 0x01: return 1256;
    case 0x25: case 0x26: case 0x27: return 1257;
    case 0x11: return 932;
    case 0x04: return (lcid & 0xFC00) == 0x0400 ? 950 : 936;
    case 0x12: return 949;
    default: return 1252;
  }
}

// A LOGFONT family of a WinHelp 3.x font family.
BYTE FamilyOf(BYTE fam) {
  switch (fam) {
    case 1: return FF_MODERN | FIXED_PITCH;
    case 2: return FF_ROMAN | VARIABLE_PITCH;
    case 3: return FF_SWISS | VARIABLE_PITCH;
    case 4: return FF_SCRIPT;
    case 5: return FF_DECORATIVE;
    default: return FF_DONTCARE;
  }
}

// Undoes the run-length packing of pictures.
std::vector<BYTE> UnRunLength(const BYTE* p, size_t n) {
  std::vector<BYTE> out;
  const BYTE* end = p + n;
  while (p < end) {
    BYTE c = *p++;
    if (c & 0x80) {
      size_t k = std::min((size_t)(c & 0x7F), (size_t)(end - p));
      out.insert(out.end(), p, p + k);
      p += k;
    } else if (p < end) {
      out.insert(out.end(), c, *p++);
    }
  }
  return out;
}

// Unpacks picture data by its packing method (0 none, 1 run length,
// 2 LZ77, 3 both).
std::vector<BYTE> Unpack(int method, const BYTE* p, size_t n) {
  switch (method) {
    case 1: return UnRunLength(p, n);
    case 2: return Expand(p, n);
    case 3: {
      std::vector<BYTE> lz = Expand(p, n);
      return UnRunLength(lz.data(), lz.size());
    }
    default: return std::vector<BYTE>(p, p + n);
  }
}

// Whether a face name is that of a symbol font, whose bytes are glyphs.
bool IsSymbolFace(const std::wstring& face) {
  std::wstring f;
  for (wchar_t c : face) f += (wchar_t)towlower(c);
  return f == L"symbol" || f.compare(0, 9, L"wingdings") == 0 || f == L"webdings" || f == L"marlett" ||
         f == L"ms outlook" || f == L"zapf dingbats";
}

// Reads a link's target "context>window@file" of a picture hotspot.
void SplitTarget(const std::wstring& s, Link* link) {
  std::wstring ctx = s;
  size_t at = ctx.find(L'@');
  if (at != std::wstring::npos) {
    link->file = ctx.substr(at + 1);
    ctx.resize(at);
  }
  size_t gt = ctx.find(L'>');
  if (gt != std::wstring::npos) {
    link->window = ctx.substr(gt + 1);
    ctx.resize(gt);
  }
  link->context = ctx;
}

}  // namespace

// Expands LZ77 data: each flag byte tells, from its low bit up, whether
// the next item is a byte to copy (0) or a 16-bit back reference (1) of
// 12 bits of distance (less one) and 4 of length (less three).
std::vector<BYTE> Expand(const BYTE* p, size_t n, size_t limit) {
  std::vector<BYTE> out;
  out.reserve(std::min(limit, n * 3));
  const BYTE* end = p + n;
  while (p < end && out.size() < limit) {
    BYTE flags = *p++;
    for (int bit = 0; bit < 8 && p < end && out.size() < limit; bit++) {
      if (flags & (1 << bit)) {
        if (end - p < 2) {
          p = end;
          break;
        }
        unsigned w = U16(p);
        p += 2;
        size_t dist = (w & 0xFFF) + 1, len = (w >> 12) + 3;
        if (dist > out.size()) {
          // A reference before the start: the window's initial spaces.
          for (size_t i = 0; i < len && out.size() < limit; i++) out.push_back(' ');
          continue;
        }
        size_t from = out.size() - dist;
        for (size_t i = 0; i < len && out.size() < limit; i++) out.push_back(out[from + i]);
      } else {
        out.push_back(*p++);
      }
    }
  }
  return out;
}

// Computes the hash WinHelp keeps of a context name.
uint32_t File::Hash(const std::wstring& name) {
  if (name.empty()) return 1;
  uint32_t h = 0;
  for (wchar_t c : name) h = h * 43 + (uint32_t)(int32_t)kHashTable[(BYTE)(c < 256 ? c : '?')];
  return h;
}

// Opens and reads a help file.
std::shared_ptr<File> File::Open(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  std::shared_ptr<File> h(new File());
  h->path_ = path;
  BYTE buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) h->data_.insert(h->data_.end(), buf, buf + n);
  fclose(f);
  if (!h->Parse()) return nullptr;
  return h;
}

// Finds an internal file by name, with the '|' or without it (3.0).
bool File::FindInternal(const char* name, Internal* f) const {
  auto it = dir_.find(name);
  if (it == dir_.end() && name[0] == '|') it = dir_.find(name + 1);
  if (it == dir_.end()) return false;
  *f = it->second;
  return true;
}

// Walks the leaf pages of a B+ tree, from the first, in order.
template <class F>
bool File::ForEachLeaf(const Internal& tree, F each) const {
  if (tree.size < 38 || U16(tree.data) != 0x293B) return false;
  unsigned pageSize = U16(tree.data + 4);
  int root = S16(tree.data + 26);
  int pages = S16(tree.data + 30);
  int levels = S16(tree.data + 32);
  const BYTE* base = tree.data + 38;
  if (pageSize < 8) return false;
  // The page's start, or NULL if it is outside the tree.
  auto page = [&](int i) -> const BYTE* {
    if (i < 0 || i >= pages || 38 + (size_t)(i + 1) * pageSize > tree.size) return nullptr;
    return base + (size_t)i * pageSize;
  };
  int cur = root;
  for (int l = 1; l < levels; l++) {
    const BYTE* p = page(cur);
    if (!p) return false;
    cur = S16(p + 4);
  }
  for (int guard = 0; cur != -1 && guard <= pages; guard++) {
    const BYTE* p = page(cur);
    if (!p) return false;
    int n = S16(p + 2);
    const BYTE* e = p + 8;
    const BYTE* end = p + pageSize;
    for (int i = 0; i < n && e < end; i++) {
      size_t used = each(e, end);
      if (!used) return true;
      e += used;
    }
    cur = S16(p + 6);
  }
  return true;
}

// Reads the header, the directory and the internal files.
bool File::Parse() {
  if (data_.size() < 16 || U32(data_.data()) != 0x00035F3F) return false;
  uint32_t dirStart = U32(data_.data() + 4);
  // An internal file at an offset: its FILEHEADER, then its bytes.
  auto internal = [&](uint32_t off, Internal* f) {
    if ((uint64_t)off + 9 > data_.size()) return false;
    uint32_t used = U32(data_.data() + off + 4);
    if ((uint64_t)off + 9 + used > data_.size()) used = (uint32_t)(data_.size() - off - 9);
    f->data = data_.data() + off + 9;
    f->size = used;
    return true;
  };
  Internal dir;
  if (!internal(dirStart, &dir)) return false;
  ForEachLeaf(dir, [&](const BYTE* e, const BYTE* end) -> size_t {
    const BYTE* s = e;
    while (s < end && *s) s++;
    if (end - s < 5) return 0;
    std::string name((const char*)e, s - e);
    Internal f;
    if (internal(U32(s + 1), &f)) dir_[name] = f;
    return (s - e) + 5;
  });
  Internal sys;
  if (!FindInternal("|SYSTEM", &sys) || sys.size < 12 || U16(sys.data) != 0x036C) return false;
  ReadSystem();
  ReadFonts();
  ReadPhrases();
  ReadContextMaps();
  ReadTopics();
  return true;
}

// Reads |SYSTEM: the version, compression, title and options.
void File::ReadSystem() {
  Internal sys;
  FindInternal("|SYSTEM", &sys);
  minor_ = U16(sys.data + 2);
  flags_ = U16(sys.data + 10);
  if (minor_ <= 16) {
    blockSize_ = 2048;
    lz_ = false;
    Reader r(sys.data + 12, sys.size - 12);
    std::string t = r.String();
    title_ = Decode(t.data(), t.size());
  } else {
    lz_ = (flags_ & 0x0C) != 0;
    blockSize_ = (flags_ & 0x08) ? 2048 : 4096;
    // The records: first the code page, which the strings need.
    for (int pass = 0; pass < 2; pass++) {
      const BYTE* p = sys.data + 12;
      const BYTE* end = sys.data + sys.size;
      while (end - p >= 4) {
        unsigned type = U16(p), size = U16(p + 2);
        const BYTE* d = p + 4;
        if ((size_t)(end - d) < size) break;
        p = d + size;
        // A record's string, without the zero.
        auto str = [&]() {
          size_t n = 0;
          while (n < size && d[n]) n++;
          return Decode((const char*)d, n);
        };
        if (pass == 0) {
          if (type == 9 && size >= 2) codePage_ = CodePageOfLcid(U16(d));
          if (type == 11 && size >= 1) codePage_ = CodePageOfCharset(d[0]);
          continue;
        }
        switch (type) {
          case 1: title_ = str(); break;
          case 2: copyright_ = str(); break;
          case 3:
            if (size >= 4) contents_ = U32(d);
            break;
          case 4: config_.push_back(str()); break;
          case 6:
            if (size >= 90) {
              WindowDef w;
              unsigned fl = U16(d);
              // A fixed-size string of the record.
              auto field = [&](int off, int n) {
                size_t k = 0;
                while ((int)k < n && d[off + k]) k++;
                return Decode((const char*)d + off, k);
              };
              if (fl & 0x01) w.type = field(2, 10);
              if (fl & 0x02) w.name = field(12, 9);
              if (fl & 0x04) w.caption = field(21, 51);
              w.hasPos = (fl & 0x08) && (fl & 0x10) && (fl & 0x20) && (fl & 0x40);
              w.x = S16(d + 72);
              w.y = S16(d + 74);
              w.cx = S16(d + 76);
              w.cy = S16(d + 78);
              w.maximize = (fl & 0x80) && (U16(d + 80) & 1);
              w.onTop = (fl & 0x400) != 0;
              if (fl & 0x100) {
                w.hasColor = true;
                w.color = U32(d + 82) & 0xFFFFFF;
              }
              if (fl & 0x200) {
                w.hasNsrColor = true;
                w.nsrColor = U32(d + 86) & 0xFFFFFF;
              }
              windows_.push_back(w);
            }
            break;
          case 10: cnt_ = str(); break;
        }
      }
    }
  }
  // A TOPICPOS counts 16K for each block (3.1 and later), compressed or
  // not; WinHelp 3.0 counts the bytes of |TOPIC itself.
  decompressSize_ = minor_ <= 16 ? blockSize_ - 12 : 0x4000;
}

// Reads the phrase table: |Phrases (3.1 and 4.0) or |PhrIndex and
// |PhrImage (4.0, Hall compression).
void File::ReadPhrases() {
  Internal f, img;
  if (FindInternal("|PhrIndex", &f) && FindInternal("|PhrImage", &img) && f.size >= 28) {
    uint32_t count = U32(f.data + 4);
    uint32_t imageSize = U32(f.data + 12), imageCompressed = U32(f.data + 16);
    int bitCount = U16(f.data + 24) & 0x0F;
    std::vector<BYTE> image;
    if (imageCompressed != imageSize && imageCompressed <= img.size)
      image = Expand(img.data, imageCompressed, imageSize);
    else
      image.assign(img.data, img.data + std::min(imageSize, img.size));
    // The phrases' lengths, in bits from the low bit of each 32-bit word.
    const BYTE* bits = f.data + 28;
    size_t nbits = (size_t)(f.size - 28) * 8, bit = 0;
    // The next bit of the lengths.
    auto next = [&]() -> int {
      if (bit >= nbits) return 0;
      size_t word = bit / 32, b = bit % 32;
      bit++;
      return (U32(bits + word * 4) >> b) & 1;
    };
    size_t at = 0;
    hall_ = true;
    for (uint32_t i = 0; i < count && bit < nbits; i++) {
      size_t n = 1;
      while (next()) n += (size_t)1 << bitCount;
      if (next()) n += 1;
      if (bitCount > 1 && next()) n += 2;
      if (bitCount > 2 && next()) n += 4;
      if (bitCount > 3 && next()) n += 8;
      if (bitCount > 4 && next()) n += 16;
      if (at + n > image.size()) break;
      phrases_.push_back(std::string((const char*)image.data() + at, n));
      at += n;
    }
    return;
  }
  if (!FindInternal("|Phrases", &f) || f.size < 4) return;
  const BYTE* p = f.data;
  size_t size = f.size;
  // Some multimedia files have a longer header.
  if (U16(p) == 0x0800 && size > 40 && U16(p + 4) == 0x0100) {
    p += 2;
    size -= 2;
  }
  unsigned count = U16(p);
  if (U16(p + 2) != 0x0100) return;
  std::vector<BYTE> text;
  const BYTE* offs;
  if (minor_ <= 16) {
    offs = p + 4;
    if (4 + (size_t)(count + 1) * 2 > size) return;
    unsigned first = U16(offs);
    unsigned last = U16(offs + count * 2);
    if (4 + (size_t)last > size + 0 || first > last) return;
    text.assign(offs + first, offs + std::min<size_t>(last, size - 4));
  } else {
    if (size < 8) return;
    uint32_t expanded = U32(p + 4);
    bool longHeader = p != f.data;
    offs = p + 8 + (longHeader ? 30 : 0);
    size_t head = (offs - f.data) + (size_t)(count + 1) * 2;
    if (head > f.size) return;
    text = Expand(f.data + head, f.size - head, expanded);
  }
  unsigned base = U16(offs);
  for (unsigned i = 0; i < count; i++) {
    unsigned a = U16(offs + i * 2) - base, b = U16(offs + (i + 1) * 2) - base;
    if (b < a || b > text.size()) break;
    phrases_.push_back(std::string((const char*)text.data() + a, b - a));
  }
}

// Reads |FONT: the face names and the font descriptors.
void File::ReadFonts() {
  Internal f;
  if (!FindInternal("|FONT", &f) || f.size < 8) return;
  unsigned faces = U16(f.data), descs = U16(f.data + 2);
  unsigned faceOff = U16(f.data + 4), descOff = U16(f.data + 6);
  if (!faces || descOff < faceOff || descOff > f.size) return;
  unsigned faceLen = (descOff - faceOff) / faces;
  std::vector<std::wstring> names;
  for (unsigned i = 0; i < faces; i++) {
    const char* s = (const char*)f.data + faceOff + i * faceLen;
    size_t n = 0;
    while (n < faceLen && s[n]) n++;
    std::wstring name = Decode(s, n);
    // A multimedia file's face names end with ",n": a character table.
    size_t comma = name.find(L',');
    if (comma != std::wstring::npos) name.resize(comma);
    names.push_back(name);
  }
  twips_ = faceOff >= 12;
  size_t descSize = faceOff >= 16 ? 42 : faceOff >= 12 ? 42 : 11;
  for (unsigned i = 0; i < descs; i++) {
    size_t off = descOff + i * descSize;
    if (off + descSize > f.size) break;
    const BYTE* d = f.data + off;
    Font font;
    unsigned face;
    if (descSize == 11) {
      BYTE attr = d[0];
      font.weight = (attr & 1) ? FW_BOLD : FW_NORMAL;
      font.italic = (attr & 2) != 0;
      font.underline = (attr & 4) != 0;
      font.strike = (attr & 8) != 0;
      font.doubleUnderline = (attr & 0x10) != 0;
      font.smallCaps = (attr & 0x20) != 0;
      font.height = d[1] * 10;
      font.family = FamilyOf(d[2]);
      face = U16(d + 3);
      font.color = RGB(d[5], d[6], d[7]);
    } else if (faceOff >= 16) {
      face = U16(d);
      font.color = RGB(d[6], d[7], d[8]);
      font.height = std::abs((int32_t)U32(d + 12));
      font.weight = S16(d + 28);
      font.italic = d[32] != 0;
      font.underline = d[33] != 0;
      font.strike = d[34] != 0;
      font.doubleUnderline = d[35] != 0;
      font.smallCaps = d[36] != 0;
      font.family = d[39];
    } else {
      face = U16(d + 1);
      font.color = RGB(d[3], d[4], d[5]);
      font.height = std::abs((int32_t)U32(d + 14));
      font.weight = S16(d + 30);
      font.italic = d[34] != 0;
      font.underline = d[35] != 0;
      font.strike = d[36] != 0;
      font.doubleUnderline = d[37] != 0;
      font.smallCaps = d[38] != 0;
      font.family = d[41];
    }
    if (font.height <= 0) font.height = 200;
    if (font.weight <= 0) font.weight = FW_NORMAL;
    if (face < names.size()) font.face = names[face];
    if (IsSymbolFace(font.face)) font.charset = SYMBOL_CHARSET;
    fonts_.push_back(font);
  }
}

// Reads |CONTEXT, |CTXOMAP and |TOMAP.
void File::ReadContextMaps() {
  Internal f;
  if (FindInternal("|CONTEXT", &f))
    ForEachLeaf(f, [&](const BYTE* e, const BYTE* end) -> size_t {
      if (end - e < 8) return 0;
      hashes_[U32(e)] = U32(e + 4);
      return 8;
    });
  if (FindInternal("|CTXOMAP", &f) && f.size >= 2) {
    unsigned n = U16(f.data);
    for (unsigned i = 0; i < n && 2 + (size_t)(i + 1) * 8 <= f.size; i++)
      map_[U32(f.data + 2 + i * 8)] = U32(f.data + 6 + i * 8);
  }
  if (FindInternal("|TOMAP", &f))
    for (size_t i = 0; i + 4 <= f.size; i += 4) tomap_.push_back(U32(f.data + i));
}

// Expands a link's text (LinkData2) with the phrase table: n bytes at p
// that become outSize bytes.
void File::ExpandText(const BYTE* p, size_t n, size_t outSize, std::vector<BYTE>* out) const {
  out->clear();
  if (outSize <= n || phrases_.empty()) {
    out->assign(p, p + std::min(n, outSize));
    return;
  }
  out->reserve(outSize);
  const BYTE* end = p + n;
  // Adds a phrase's text.
  auto phrase = [&](size_t i) {
    if (i < phrases_.size()) out->insert(out->end(), phrases_[i].begin(), phrases_[i].end());
  };
  while (p < end && out->size() < outSize) {
    BYTE c = *p++;
    if (hall_) {
      if (!(c & 1)) {
        phrase(c / 2);
      } else if ((c & 3) == 1) {
        if (p < end) phrase((size_t)c * 64 + 64 + *p++);
      } else if ((c & 7) == 3) {
        size_t k = std::min((size_t)(c / 8 + 1), (size_t)(end - p));
        out->insert(out->end(), p, p + k);
        p += k;
      } else if ((c & 0xF) == 7) {
        out->insert(out->end(), c / 16 + 1, ' ');
      } else {
        out->insert(out->end(), c / 16 + 1, 0);
      }
    } else {
      if (c == 0 || c > 15) {
        out->push_back(c);
      } else if (p < end) {
        unsigned code = (c * 256 - 256) + *p++;
        phrase(code / 2);
        if (code & 1) out->push_back(' ');
      }
    }
  }
  if (out->size() > outSize) out->resize(outSize);
}

// Reads the TOPICLINK at a TOPICPOS: its bytes, and the next one's place.
bool File::ReadLink(uint32_t pos, std::vector<BYTE>* link, uint32_t* next) const {
  size_t idx;
  if (minor_ <= 16) {
    // WinHelp 3.0: the place in |TOPIC itself, block headers included.
    size_t b = pos / blockSize_, off = pos % blockSize_;
    if (off < 12 || b >= blockStart_.size()) return false;
    idx = blockStart_[b] + off - 12;
  } else {
    if (pos < 12) return false;
    size_t b = (pos - 12) / decompressSize_, off = (pos - 12) % decompressSize_;
    if (b >= blockStart_.size()) return false;
    idx = blockStart_[b] + off;
  }
  if (idx + 21 > topic_.size()) return false;
  const BYTE* p = topic_.data() + idx;
  uint32_t size = U32(p);
  if (size < 21 || idx + size > topic_.size()) return false;
  link->assign(p, p + size);
  uint32_t n = U32(p + 12);
  if (minor_ <= 16)
    *next = n ? pos + n : 0xFFFFFFFF;
  else
    *next = n;
  return true;
}

// Reads |TOPIC: expands its blocks and walks its TOPICLINKs to find the
// topics, their titles and their records.
void File::ReadTopics() {
  Internal f;
  if (!FindInternal("|TOPIC", &f)) return;
  for (size_t at = 0; at + 12 <= f.size; at += blockSize_) {
    size_t n = std::min((size_t)blockSize_, (size_t)f.size - at) - 12;
    blockStart_.push_back((uint32_t)topic_.size());
    if (lz_) {
      std::vector<BYTE> b = Expand(f.data + at + 12, n, decompressSize_);
      topic_.insert(topic_.end(), b.begin(), b.end());
    } else {
      topic_.insert(topic_.end(), f.data + at + 12, f.data + at + 12 + n);
    }
  }
  if (f.size < 12) return;
  uint32_t pos = U32(f.data + 4);  // the first block's FirstTopicLink
  if (pos == 0xFFFFFFFF || pos < 12) pos = 12;
  std::vector<BYTE> link, text;
  uint32_t curBlock = 0xFFFFFFFF, count = 0;
  Topic* topic = nullptr;
  bool nonScroll = false;
  uint32_t scrollPos = 0;
  for (size_t guard = 0; guard < topic_.size() / 21 + 1; guard++) {
    uint32_t next;
    if (!ReadLink(pos, &link, &next)) break;
    uint32_t block = minor_ <= 16 ? pos / blockSize_ : (pos - 12) / decompressSize_;
    if (block != curBlock) {
      curBlock = block;
      count = 0;
    }
    uint32_t offset = curBlock * 0x8000 + count;
    BYTE type = link[20];
    uint32_t dataLen1 = U32(link.data() + 16), dataLen2 = U32(link.data() + 4);
    if (dataLen1 < 21 || dataLen1 > link.size()) dataLen1 = (uint32_t)link.size();
    if (type == 2) {
      topics_.emplace_back();
      topic = &topics_.back();
      topic->pos = pos;
      topic->offset = offset;
      Reader r(link.data() + 21, dataLen1 - 21);
      if (minor_ <= 16) {
        r.Dword();
        topic->browsePrev = r.Dword();
        topic->browseNext = r.Dword();
        topic->number = (uint32_t)topics_.size() - 1 + 16;
        nonScroll = false;
      } else {
        r.Dword();
        topic->browsePrev = r.Dword();
        topic->browseNext = r.Dword();
        topic->number = r.Dword();
        uint32_t ns = r.Dword();
        scrollPos = r.Dword();
        nonScroll = ns != 0xFFFFFFFF && ns != scrollPos;
      }
      ExpandText(link.data() + dataLen1, link.size() - dataLen1, dataLen2, &text);
      size_t i = 0;
      for (int s = 0; i < text.size(); s++) {
        size_t j = i;
        while (j < text.size() && text[j]) j++;
        std::wstring str = Decode((const char*)text.data() + i, j - i);
        if (s == 0)
          topic->title = str;
        else if (!str.empty())
          topic->macros.push_back(str);
        i = j + 1;
      }
    } else if ((type == 0x20 || type == 0x23 || type == 1) && topic) {
      if (nonScroll && pos == scrollPos) nonScroll = false;
      topic->refs.push_back({pos, offset, nonScroll});
      if (type != 1) {
        Reader r(link.data() + 21, dataLen1 - 21);
        r.CLong();
        count += r.CUShort();
      }
    }
    if (next == 0xFFFFFFFF || next == 0 || next == pos) break;
    pos = next;
  }
  // The last TOPICLINK may be an empty topic that only ends the file.
  if (!topics_.empty() && topics_.back().refs.empty() && topics_.back().title.empty()) topics_.pop_back();
  for (auto& t : topics_) offsets_.push_back(t.offset);
}

// Reads a display or table record into its cells.
void File::ReadRecord(const std::vector<BYTE>& link, Record* rec) const {
  BYTE type = link[20];
  uint32_t dataLen1 = U32(link.data() + 16), dataLen2 = U32(link.data() + 4);
  if (dataLen1 < 21 || dataLen1 > link.size()) return;
  std::vector<BYTE> text;
  ExpandText(link.data() + dataLen1, link.size() - dataLen1, dataLen2, &text);
  size_t ti = 0;
  int scale = twips_ ? 1 : 10;
  Reader r(link.data() + 21, dataLen1 - 21);
  r.CLong();
  if (type == 0x20 || type == 0x23) r.CUShort();
  if (type == 0x23) {
    rec->table = true;
    int ncols = r.Byte();
    int ttype = r.Byte();
    rec->relativeWidths = ttype == 0 || ttype == 2;
    if (rec->relativeWidths) rec->minTableWidth = (int16_t)r.Word() * scale;
    for (int i = 0; i < ncols; i++) {
      Record::Column c;
      c.width = (int16_t)r.Word();
      c.gap = (int16_t)r.Word();
      if (!rec->relativeWidths) {
        c.gap *= scale;
        c.width *= scale;
      }
      rec->columns.push_back(c);
    }
  }
  int font = 0;
  for (int cellGuard = 0; cellGuard < 256 && !r.bad; cellGuard++) {
    Cell cell;
    if (type == 0x23) {
      int col = (int16_t)r.Word();
      if (col == -1 || r.bad) break;
      cell.column = col;
      r.Word();
      r.Byte();
    }
    r.Byte();
    r.Byte();
    r.Word();
    unsigned bits = r.Word();
    ParaFormat& pf = cell.format;
    if (bits & 0x0001) r.CLong();
    if (bits & 0x0002) pf.spaceAbove = r.CShort() * scale;
    if (bits & 0x0004) pf.spaceBelow = r.CShort() * scale;
    if (bits & 0x0008) pf.lineSpacing = r.CShort() * scale;
    if (bits & 0x0010) pf.leftIndent = r.CShort() * scale;
    if (bits & 0x0020) pf.rightIndent = r.CShort() * scale;
    if (bits & 0x0040) pf.firstIndent = r.CShort() * scale;
    if (bits & 0x0100) {
      pf.border = r.Byte();
      pf.borderWidth = (int16_t)r.Word() * scale;
    }
    if (bits & 0x0200) {
      int n = r.CShort();
      for (int i = 0; i < n && i < 64 && !r.bad; i++) {
        unsigned t = r.CUShort();
        ParaFormat::Tab tab = {(int)(t & 0x3FFF) * scale, 0};
        if (t & 0x4000) tab.type = r.CUShort();
        pf.tabs.push_back(tab);
      }
    }
    if (bits & 0x0400) pf.align = 1;
    if (bits & 0x0800) pf.align = 2;
    // The text: a string, then a command, until the end.
    for (int guard = 0; guard < 100000; guard++) {
      size_t j = ti;
      while (j < text.size() && text[j]) j++;
      if (j > ti) {
        Item it;
        it.type = Item::kText;
        if (font >= 0 && font < (int)fonts_.size() && fonts_[font].charset == SYMBOL_CHARSET) {
          for (size_t k = ti; k < j; k++) it.text += (wchar_t)(BYTE)text[k];
        } else {
          it.text = Decode((const char*)text.data() + ti, j - ti);
        }
        cell.items.push_back(it);
      }
      ti = j < text.size() ? j + 1 : j;
      if (!r.Has(1)) break;
      BYTE cmd = r.Byte();
      if (cmd == 0xFF) break;
      Item it;
      switch (cmd) {
        case 0x20:
          r.Dword();
          continue;
        case 0x21:
          r.Word();
          continue;
        case 0x80:
          font = r.Word();
          it.type = Item::kFont;
          it.font = font;
          break;
        case 0x81: it.type = Item::kLineBreak; break;
        case 0x82: it.type = Item::kParaEnd; break;
        case 0x83: it.type = Item::kTab; break;
        case 0x86:
        case 0x87:
        case 0x88: {
          it.align = cmd - 0x86;
          int ptype = r.Byte();
          int32_t size = r.CLong();
          if (ptype == 0x22) r.CUShort();
          const BYTE* start = r.p;
          if (size < 0 || !r.Has(size)) break;
          if (ptype == 3 || ptype == 0x22) {
            it.type = Item::kPicture;
            if (size >= 4 && S16(start) == 0)
              it.pictureNumber = S16(start + 2);
            else if (size > 2)
              it.picture = ReadPicture(start + 2, size - 2);
          } else if (ptype == 5 && size > 6) {
            it.type = Item::kWindow;
            size_t k = 6;
            while (k < size && start[k]) k++;
            it.text = Decode((const char*)start + 6, k - 6);
          } else {
            r.p = start + size;
            continue;
          }
          r.p = start + size;
          break;
        }
        case 0x89: it.type = Item::kLinkEnd; break;
        case 0x8B:
          it.type = Item::kText;
          it.text = L" ";
          break;
        case 0x8C: continue;
        case 0xC8:
        case 0xCC: {
          unsigned len = r.Word();
          const BYTE* s = r.p;
          if (!r.Has(len)) break;
          size_t k = 0;
          while (k < len && s[k]) k++;
          it.type = Item::kLinkStart;
          it.link.kind = Link::kMacro;
          it.link.visible = cmd == 0xC8;
          it.link.macro = Decode((const char*)s, k);
          r.p = s + len;
          break;
        }
        case 0xE0:
        case 0xE1:
          it.type = Item::kLinkStart;
          it.link.kind = cmd == 0xE1 ? Link::kJump : Link::kPopup;
          it.link.target = r.Dword();
          break;
        case 0xE2:
        case 0xE3:
        case 0xE6:
        case 0xE7:
          it.type = Item::kLinkStart;
          it.link.kind = (cmd & 1) ? Link::kJump : Link::kPopup;
          it.link.visible = !(cmd & 4);
          it.link.hasHash = true;
          it.link.target = r.Dword();
          break;
        case 0xEA:
        case 0xEB:
        case 0xEE:
        case 0xEF: {
          unsigned size = r.Word();
          const BYTE* s = r.p;
          if (!r.Has(size)) break;
          Reader q(s, size);
          int t = q.Byte();
          it.type = Item::kLinkStart;
          it.link.kind = (cmd & 1) ? Link::kJump : Link::kPopup;
          it.link.visible = !(cmd & 4);
          it.link.hasHash = true;
          it.link.target = q.Dword();
          if (t == 1) {
            unsigned w = q.Byte();
            if (w < windows_.size()) it.link.window = windows_[w].name;
          } else if (t == 4 || t == 6) {
            std::string file = q.String();
            it.link.file = Decode(file.data(), file.size());
            if (t == 6) {
              std::string win = q.String();
              it.link.window = Decode(win.data(), win.size());
            }
          }
          r.p = s + size;
          break;
        }
        default:
          // Not known: the rest of the record cannot be read.
          r.bad = true;
          break;
      }
      if (r.bad) break;
      cell.items.push_back(it);
    }
    rec->cells.push_back(std::move(cell));
    if (type != 0x23) break;
  }
}

// Reads a picture (SHG or MRB format): the one of the resolutions
// nearest the screen's, with its hotspots.
std::shared_ptr<Picture> File::ReadPicture(const BYTE* data, size_t n) const {
  if (n < 8) return nullptr;
  unsigned magic = U16(data), count = U16(data + 2);
  if ((magic != 0x506C && magic != 0x706C) || !count || 4 + (size_t)count * 4 > n) return nullptr;
  // Picks the resolution nearest 96 dpi.
  size_t best = 0;
  int bestDiff = 1 << 30;
  for (unsigned i = 0; i < count; i++) {
    uint32_t off = U32(data + 4 + i * 4);
    if (off + 2 >= n) continue;
    int dpi = 96;
    if (data[off] == 5 || data[off] == 6) {
      Reader r(data + off + 2, n - off - 2);
      dpi = (int)r.CULong();
    }
    int diff = std::abs(dpi - 96);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = off;
    }
  }
  if (best + 2 >= n) return nullptr;
  const BYTE* q = data + best;
  size_t qn = n - best;
  auto pic = std::make_shared<Picture>();
  int ptype = q[0], packing = q[1];
  Reader r(q + 2, qn - 2);
  uint32_t csize, hsize, coff, hoff;
  if (ptype == 5 || ptype == 6) {
    r.CULong();
    r.CULong();
    r.CUShort();
    int bitCount = (int)r.CUShort();
    int width = (int)r.CULong(), height = (int)r.CULong();
    uint32_t used = r.CULong(), important = r.CULong();
    csize = r.CULong();
    hsize = r.CULong();
    coff = r.Dword();
    hoff = r.Dword();
    if (r.bad || width <= 0 || height <= 0 || width > 16384 || height > 16384) return nullptr;
    if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 24 && bitCount != 16 && bitCount != 32)
      return nullptr;
    pic->type = Picture::kBitmap;
    pic->width = width;
    pic->height = height;
    pic->transparent = important == 1 && bitCount <= 8;
    unsigned ncolors = used ? used : (bitCount <= 8 ? 1u << bitCount : 0);
    if (ptype == 6) {
      for (unsigned i = 0; i < ncolors && r.Has(4); i++) {
        RGBQUAD c;
        c.rgbBlue = r.Byte();
        c.rgbGreen = r.Byte();
        c.rgbRed = r.Byte();
        c.rgbReserved = 0;
        r.Byte();
        pic->palette.push_back(c);
      }
    } else if (bitCount == 1) {
      pic->palette.push_back({0, 0, 0, 0});
      pic->palette.push_back({255, 255, 255, 0});
    }
    if (coff >= qn) return nullptr;
    std::vector<BYTE> bits = Unpack(packing, q + coff, std::min((size_t)csize, qn - coff));
    size_t stride = ((size_t)width * bitCount + 31) / 32 * 4;
    if (ptype == 6) {
      bits.resize(stride * height);
      pic->bits = std::move(bits);
      pic->bitCount = bitCount;
    } else {
      // A device-dependent bitmap: rows top-down, of a multiple of two
      // bytes; a DIB has them bottom-up.
      size_t ddb = ((size_t)width * bitCount + 15) / 16 * 2;
      pic->bits.assign(stride * height, 0);
      for (int y = 0; y < height; y++) {
        size_t from = (size_t)y * ddb;
        if (from >= bits.size()) break;
        size_t k = std::min(ddb, bits.size() - from);
        memcpy(&pic->bits[(size_t)(height - 1 - y) * stride], &bits[from], std::min(k, stride));
      }
      pic->bitCount = bitCount;
      if (bitCount > 1 && bitCount <= 8 && pic->palette.empty()) {
        // The standard VGA colors.
        static const BYTE vga[16][3] = {{0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
                                        {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
                                        {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                        {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
        for (unsigned i = 0; i < (1u << bitCount); i++) {
          const BYTE* c = vga[i % 16];
          pic->palette.push_back({c[2], c[1], c[0], 0});
        }
      }
    }
  } else if (ptype == 8) {
    pic->mapMode = (int)r.CUShort();
    int w = r.Word(), h = r.Word();
    uint32_t expanded = r.CULong();
    csize = r.CULong();
    hsize = r.CULong();
    coff = r.Dword();
    hoff = r.Dword();
    if (r.bad || coff >= qn) return nullptr;
    pic->type = Picture::kMetafile;
    pic->metafile = Unpack(packing, q + coff, std::min((size_t)csize, qn - coff));
    if (expanded && pic->metafile.size() > expanded) pic->metafile.resize(expanded);
    // The size is in hundredths of a millimeter (as a METAFILEPICT's).
    pic->width = (int)((int64_t)w * 96 / 2540);
    pic->height = (int)((int64_t)h * 96 / 2540);
    if (pic->width <= 0 || pic->height <= 0) {
      pic->width = w;
      pic->height = h;
    }
  } else {
    return nullptr;
  }
  // The hotspots: a table of their kinds and places, macro data, then
  // a name and a target for each.
  if (hsize && hoff && hoff + 7 <= qn) {
    Reader h(q + hoff, std::min((size_t)hsize, qn - hoff));
    h.Byte();
    unsigned count = h.Word();
    uint32_t macroSize = h.Dword();
    struct Raw {
      BYTE id0, id1;
      RECT rc;
    };
    std::vector<Raw> raw;
    for (unsigned i = 0; i < count && h.Has(15); i++) {
      Raw x;
      x.id0 = h.Byte();
      x.id1 = h.Byte();
      h.Byte();
      int x0 = h.Word(), y0 = h.Word(), w = h.Word(), hh = h.Word();
      x.rc = {x0, y0, x0 + w, y0 + hh};
      h.Dword();
      raw.push_back(x);
    }
    if (h.Has(macroSize)) h.p += macroSize;
    for (auto& x : raw) {
      std::string name = h.String();
      std::string target = h.String();
      if (h.bad) break;
      Picture::Hotspot hs;
      hs.rect = x.rc;
      std::wstring t = Decode(target.data(), target.size());
      BYTE id = x.id0;
      hs.link.visible = !(id & 4);
      if ((id & 0xF0) == 0xC0) {
        hs.link.kind = Link::kMacro;
        hs.link.macro = t;
      } else {
        hs.link.kind = (id & 1) ? Link::kJump : Link::kPopup;
        SplitTarget(t, &hs.link);
      }
      pic->hotspots.push_back(hs);
    }
  }
  return pic;
}

// Reads a |bmN picture, once.
std::shared_ptr<Picture> File::GetPicture(int number) {
  auto it = pictures_.find(number);
  if (it != pictures_.end()) return it->second;
  char name[32];
  snprintf(name, sizeof(name), "|bm%d", number);
  Internal f;
  std::shared_ptr<Picture> pic;
  if (FindInternal(name, &f)) pic = ReadPicture(f.data, f.size);
  pictures_[number] = pic;
  return pic;
}

// Reads a topic's records, the first time it is shown.
Topic& File::GetTopic(size_t i) {
  Topic& t = topics_[i];
  if (t.parsed) return t;
  t.parsed = true;
  std::vector<BYTE> link;
  for (const Topic::Ref& ref : t.refs) {
    uint32_t next;
    if (!ReadLink(ref.pos, &link, &next)) continue;
    Record rec;
    rec.offset = ref.offset;
    ReadRecord(link, &rec);
    (ref.nonScroll ? t.nonScroll : t.scroll).push_back(std::move(rec));
  }
  return t;
}

// Finds the topic that holds a TOPICOFFSET.
int File::TopicAt(uint32_t offset, int* record) {
  if (record) *record = -1;
  if (offsets_.empty()) return -1;
  auto it = std::lower_bound(offsets_.begin(), offsets_.end(), offset);
  if (it != offsets_.end() && *it == offset) return (int)(it - offsets_.begin());
  if (it == offsets_.begin()) return -1;
  int i = (int)(it - offsets_.begin()) - 1;
  if (record) {
    // Within the topic: the record that holds it, counted over the
    // records that scroll.
    const Topic& t = topics_[i];
    int k = -1;
    for (const auto& ref : t.refs) {
      if (ref.offset > offset) break;
      if (!ref.nonScroll) k++;
    }
    *record = k > 0 ? k : -1;
  }
  return i;
}

// Finds the contents topic.
int File::ContentsTopic() {
  if (contents_ != 0xFFFFFFFF) {
    int i = TopicAt(contents_);
    if (i >= 0) return i;
  }
  if (minor_ <= 16 && !tomap_.empty()) {
    for (size_t i = 0; i < topics_.size(); i++)
      if (topics_[i].pos == tomap_[0]) return (int)i;
  }
  return topics_.empty() ? -1 : 0;
}

// Finds a topic by its context name.
int File::TopicByName(const std::wstring& name) { return TopicByHash(Hash(name)); }

// Finds a topic by the hash of its context name.
int File::TopicByHash(uint32_t hash, int* record) {
  auto it = hashes_.find(hash);
  if (it == hashes_.end()) return -1;
  return TopicAt(it->second, record);
}

// Finds a topic by its [MAP] number.
int File::TopicByMap(uint32_t id, int* record) {
  auto it = map_.find(id);
  if (it != map_.end()) {
    if (minor_ <= 16) {
      for (size_t i = 0; i < topics_.size(); i++)
        if (topics_[i].number == it->second) return (int)i;
      return -1;
    }
    return TopicAt(it->second, record);
  }
  // WinHelp 3.0 files without a map: the number is the topic's.
  if (minor_ <= 16 && map_.empty()) {
    for (size_t i = 0; i < topics_.size(); i++)
      if (topics_[i].number == id) return (int)i;
  }
  return -1;
}

// Finds the topic a hotspot leads to.
int File::TopicOfLink(const Link& link, int* record) {
  if (record) *record = -1;
  if (link.hasHash) return TopicByHash(link.target, record);
  if (!link.context.empty()) return TopicByHash(Hash(link.context), record);
  // WinHelp 3.0: a topic number, through |TOMAP.
  if (link.target < tomap_.size()) {
    uint32_t pos = tomap_[link.target];
    for (size_t i = 0; i < topics_.size(); i++)
      if (topics_[i].pos == pos) return (int)i;
  }
  for (size_t i = 0; i < topics_.size(); i++)
    if (topics_[i].number == link.target) return (int)i;
  return -1;
}

// Reads the keywords of an index: |KWBTREE and |KWDATA for K.
const std::vector<std::pair<std::wstring, std::vector<uint32_t>>>& File::Keywords(wchar_t index) {
  auto found = keywords_.find(index);
  if (found != keywords_.end()) return found->second;
  auto& list = keywords_[index];
  char tree[16], data[16];
  snprintf(tree, sizeof(tree), "|%cWBTREE", (char)index);
  snprintf(data, sizeof(data), "|%cWDATA", (char)index);
  Internal t, d;
  if (!FindInternal(tree, &t) || !FindInternal(data, &d)) return list;
  ForEachLeaf(t, [&](const BYTE* e, const BYTE* end) -> size_t {
    const BYTE* s = e;
    while (s < end && *s) s++;
    if (end - s < 7) return 0;
    std::wstring kw = Decode((const char*)e, s - e);
    unsigned count = U16(s + 1);
    uint32_t off = U32(s + 3);
    std::vector<uint32_t> offs;
    for (unsigned i = 0; i < count && (size_t)off + (i + 1) * 4 <= d.size; i++) offs.push_back(U32(d.data + off + i * 4));
    list.emplace_back(kw, offs);
    return (s - e) + 7;
  });
  return list;
}

// Gives the text of a topic without its formatting: a line for each
// paragraph, tabs between a table's cells.
std::wstring File::PlainText(size_t i) {
  Topic& t = GetTopic(i);
  std::wstring out;
  for (int region = 0; region < 2; region++) {
    for (const Record& rec : region ? t.scroll : t.nonScroll) {
      bool first = true;
      for (size_t c = 0; c < rec.cells.size(); c++) {
        if (rec.table && rec.cells[c].items.empty()) continue;
        if (rec.table && !first) {
          while (!out.empty() && out.back() == L'\n') out.pop_back();
          out += L'\t';
        }
        first = false;
        for (const Item& it : rec.cells[c].items) {
          switch (it.type) {
            case Item::kText: out += it.text; break;
            case Item::kTab: out += L'\t'; break;
            case Item::kLineBreak:
            case Item::kParaEnd: out += L'\n'; break;
            default: break;
          }
        }
      }
      if (!out.empty() && out.back() != L'\n') out += L'\n';
    }
  }
  return out;
}

// Converts text of the file's code page to Unicode.
std::wstring File::Decode(const char* s, size_t n) const {
  std::wstring out;
  if (!n) return out;
  int k = MultiByteToWideChar(codePage_, 0, s, (int)n, NULL, 0);
  if (k > 0) {
    out.resize(k);
    MultiByteToWideChar(codePage_, 0, s, (int)n, &out[0], k);
    return out;
  }
  for (size_t i = 0; i < n; i++) out += (wchar_t)(BYTE)s[i];
  return out;
}

// Reads a contents file: the lines ":Title", ":Base", ":Index",
// ":Link", and "level text" (a heading) or "level text=topic".
bool ReadContents(const std::string& path, Contents* out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char line[2048];
  // Contents files are of the system's ANSI code page; Western here.
  auto decode = [](const std::string& s) {
    std::wstring w;
    int k = MultiByteToWideChar(1252, 0, s.data(), (int)s.size(), NULL, 0);
    if (k > 0) {
      w.resize(k);
      MultiByteToWideChar(1252, 0, s.data(), (int)s.size(), &w[0], k);
    }
    return w;
  };
  while (fgets(line, sizeof(line), f)) {
    std::string s = line;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.empty()) continue;
    if (s[0] == ':') {
      size_t sp = s.find(' ');
      std::string cmd = s.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
      std::string arg = sp == std::string::npos ? std::string() : s.substr(sp + 1);
      for (char& c : cmd) c = (char)tolower((unsigned char)c);
      if (cmd == "title") out->title = decode(arg);
      if (cmd == "base") out->base = decode(arg);
      if (cmd == "link" || cmd == "index") {
        size_t eq = arg.find('=');
        out->links.push_back(decode(eq == std::string::npos ? arg : arg.substr(eq + 1)));
      }
      continue;
    }
    if (s[0] < '1' || s[0] > '9') continue;
    CntEntry e;
    e.level = s[0] - '0';
    size_t i = 1;
    while (i < s.size() && s[i] == ' ') i++;
    std::string rest = s.substr(i);
    // The '=' that is not escaped with a backslash.
    size_t eq = std::string::npos;
    std::string text;
    for (size_t k = 0; k < rest.size(); k++) {
      if (rest[k] == '\\' && k + 1 < rest.size()) {
        text += rest[++k];
        continue;
      }
      if (rest[k] == '=') {
        eq = k;
        break;
      }
      text += rest[k];
    }
    e.text = decode(text);
    if (eq == std::string::npos) {
      e.book = true;
    } else {
      std::wstring target = decode(rest.substr(eq + 1));
      if (!target.empty() && target[0] == L'!') {
        e.context = target;
      } else {
        size_t gt = target.find(L'>');
        if (gt != std::wstring::npos) {
          e.window = target.substr(gt + 1);
          target.resize(gt);
        }
        size_t at = target.find(L'@');
        if (at != std::wstring::npos) {
          e.file = target.substr(at + 1);
          target.resize(at);
        }
        e.context = target;
      }
    }
    out->entries.push_back(e);
  }
  fclose(f);
  return true;
}

}  // namespace help
}  // namespace winport
