// font.cpp - finding fonts, measuring them and drawing their glyphs.
//
// TrueType and OpenType files are read with stb_truetype (public domain
// or MIT, in third_party/). The table directory, names and metrics that
// matching needs are read here, from just those tables, so that scanning
// the font folders stays cheap. Without any font file, text uses a 6x13
// bitmap font built in (font_builtin.inc), scaled by whole pixels.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only

// stb_truetype goes first, before the Win32 headers' macros.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#endif
#include "third_party/stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "font.h"

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wctype.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#if defined(__MSDOS__)
#include <dos.h>
#endif

namespace winport {

#if defined(macintosh)
namespace mactb {
bool FontsFolder(std::string* path);  // macos_toolbox.cpp
}
#endif

namespace {

#include "font_builtin.inc"
#include "font_symbol.inc"

// ---------------------------------------------------------------------
// Reading font files

UINT16 U16(const BYTE* p) { return (UINT16)((p[0] << 8) | p[1]); }
INT16 S16(const BYTE* p) { return (INT16)U16(p); }
UINT32 U32(const BYTE* p) { return ((UINT32)p[0] << 24) | ((UINT32)p[1] << 16) | ((UINT32)p[2] << 8) | p[3]; }

bool ReadAt(int fd, off_t off, void* buf, size_t n) {
  BYTE* b = (BYTE*)buf;
  while (n > 0) {
    ssize_t r = pread(fd, b, n, off);
    if (r <= 0) return false;
    b += r;
    off += r;
    n -= (size_t)r;
  }
  return true;
}

// One font in a file, as the index knows it.
struct FontFile {
  std::string path;
  int index = 0;        // within a collection
  // For a font in a Mac resource ('sfnt' in a .dfont or a suitcase):
  // where the TrueType data starts in the file and how long it is.
  // A length of 0 means the whole file.
  off_t base = 0;
  size_t length = 0;
  std::wstring family;  // name ID 1
  std::wstring style;   // name ID 2
  int weight = FW_NORMAL;
  bool italic = false;
  bool fixedPitch = false;
  bool added = false;  // by AddFontResource
};

std::wstring NameString(const BYTE* p, int len, bool utf16) {
  std::wstring s;
  if (utf16) {
    for (int i = 0; i + 1 < len; i += 2) s += (wchar_t)U16(p + i);
  } else {
    for (int i = 0; i < len; i++) s += (wchar_t)p[i];
  }
  return s;
}

// Reads the name, OS/2, post and head tables of the font at offset off.
// Reads the names and style of the font whose table directory is at off.
// The table offsets in the directory count from base: 0 for a font file,
// the start of the resource for a font in a Mac resource.
bool ReadFontInfo(int fd, off_t off, FontFile* ff, off_t base = 0) {
  BYTE hdr[12];
  if (!ReadAt(fd, off, hdr, 12)) return false;
  UINT32 ver = U32(hdr);
  if (ver != 0x00010000 && ver != 0x4F54544F /* OTTO */ && ver != 0x74727565 /* true */) return false;
  int n = U16(hdr + 4);
  if (n <= 0 || n > 200) return false;
  std::vector<BYTE> dir((size_t)n * 16);
  if (!ReadAt(fd, off + 12, dir.data(), dir.size())) return false;
  std::vector<BYTE> name, os2, post, head;
  for (int i = 0; i < n; i++) {
    const BYTE* r = &dir[(size_t)i * 16];
    UINT32 tag = U32(r), toff = U32(r + 8), tlen = U32(r + 12);
    std::vector<BYTE>* dst = NULL;
    if (tag == 0x6E616D65) dst = &name;
    else if (tag == 0x4F532F32) dst = &os2;
    else if (tag == 0x706F7374) dst = &post;
    else if (tag == 0x68656164) dst = &head;
    if (!dst || tlen > (1u << 20)) continue;
    if (dst == &post && tlen > 32) tlen = 32;
    dst->resize(tlen);
    if (!ReadAt(fd, base + (off_t)toff, dst->data(), tlen)) dst->clear();
  }
  if (name.size() < 6) return false;
  // Names: Windows Unicode English first, then any Windows Unicode, then Mac Roman.
  int count = U16(&name[2]), strOff = U16(&name[4]);
  int bestScore[3] = {0, 0, 0};
  std::wstring best[3];
  for (int i = 0; i < count; i++) {
    size_t at = 6 + (size_t)i * 12;
    if (at + 12 > name.size()) break;
    const BYTE* r = &name[at];
    int plat = U16(r), enc = U16(r + 2), lang = U16(r + 4), id = U16(r + 6), len = U16(r + 8), o = U16(r + 10);
    if (id != 1 && id != 2) continue;
    size_t so = (size_t)strOff + o;
    if (so + len > name.size()) continue;
    int score = 0;
    bool utf16 = false;
    if (plat == 3 && (enc == 1 || enc == 0)) {
      score = lang == 0x409 ? 3 : 2;
      utf16 = true;
    } else if (plat == 0) {
      score = 2;
      utf16 = true;
    } else if (plat == 1 && enc == 0) {
      score = 1;
    }
    if (score > bestScore[id]) {
      bestScore[id] = score;
      best[id] = NameString(&name[so], len, utf16);
    }
  }
  if (best[1].empty()) return false;
  ff->family = best[1];
  ff->style = best[2];
  bool bold = false, italic = false;
  if (head.size() >= 46) {
    int mac = U16(&head[44]);
    bold = (mac & 1) != 0;
    italic = (mac & 2) != 0;
  }
  ff->weight = bold ? FW_BOLD : FW_NORMAL;
  if (os2.size() >= 64) {
    int w = U16(&os2[4]);
    if (w >= 1 && w <= 1000) ff->weight = w < 10 ? w * 100 : w;
    int sel = U16(&os2[62]);
    if (sel & 1) italic = true;
  }
  ff->italic = italic;
  if (post.size() >= 16) ff->fixedPitch = U32(&post[12]) != 0;
  return true;
}

// Adds the TrueType fonts ('sfnt' resources) in a Mac resource fork to
// out. fd is open on the fork: the data fork of a .dfont file, or the
// resource fork of a font suitcase. path is the name that opens that fork.
int ReadResourceFonts(int fd, const std::string& path, std::vector<FontFile>* out) {
  BYTE hdr[16];
  if (!ReadAt(fd, 0, hdr, 16)) return 0;
  UINT32 dataOff = U32(hdr), mapOff = U32(hdr + 4), mapLen = U32(hdr + 12);
  if (mapLen < 30 || mapLen > (1u << 24)) return 0;
  std::vector<BYTE> map(mapLen);
  if (!ReadAt(fd, (off_t)mapOff, map.data(), mapLen)) return 0;
  size_t types = U16(&map[24]);
  if (types + 2 > mapLen) return 0;
  int nTypes = (int)U16(&map[types]) + 1;
  int added = 0;
  for (int t = 0; t < nTypes && t < 0x10000; t++) {
    size_t te = types + 2 + (size_t)t * 8;
    if (te + 8 > mapLen) break;
    if (U32(&map[te]) != 0x73666E74 /* sfnt */) continue;
    int count = (int)U16(&map[te + 4]) + 1;
    size_t refs = types + U16(&map[te + 6]);
    for (int i = 0; i < count; i++) {
      size_t r = refs + (size_t)i * 12;
      if (r + 12 > mapLen) break;
      UINT32 at = dataOff + (U32(&map[r + 4]) & 0xFFFFFF);
      BYTE len[4];
      if (!ReadAt(fd, (off_t)at, len, 4)) continue;
      FontFile ff;
      ff.path = path;
      ff.base = (off_t)at + 4;
      ff.length = U32(len);
      if (ReadFontInfo(fd, ff.base, &ff, ff.base)) {
        out->push_back(ff);
        added++;
      }
    }
  }
  return added;
}

// Adds the fonts in a file to out; a collection holds several.
int ReadFontFile(const std::string& path, std::vector<FontFile>* out) {
  const char* dot = strrchr(path.c_str(), '.');
  const char* slash = strrchr(path.c_str(), '/');
  bool dfont = dot && (!slash || dot > slash) && !strcasecmp(dot, ".dfont");
#if defined(__APPLE__) || defined(macintosh)
  // A font suitcase has no extension; its fonts are in the resource fork.
  if (!dfont && (!dot || (slash && dot < slash))) {
    std::string fork = path + "/..namedfork/rsrc";
    int rfd = open(fork.c_str(), O_RDONLY);
    if (rfd < 0) return 0;
    int n = ReadResourceFonts(rfd, fork, out);
    close(rfd);
    return n;
  }
#endif
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return 0;
  int added = 0;
  BYTE hdr[12];
  if (dfont) {
    added = ReadResourceFonts(fd, path, out);
  } else if (ReadAt(fd, 0, hdr, 12)) {
    if (U32(hdr) == 0x74746366 /* ttcf */) {
      int n = (int)U32(hdr + 8);
      if (n > 0 && n < 256) {
        std::vector<BYTE> offs((size_t)n * 4);
        if (ReadAt(fd, 12, offs.data(), offs.size()))
          for (int i = 0; i < n; i++) {
            FontFile ff;
            ff.path = path;
            ff.index = i;
            if (ReadFontInfo(fd, (off_t)U32(&offs[(size_t)i * 4]), &ff)) {
              out->push_back(ff);
              added++;
            }
          }
      }
    } else {
      FontFile ff;
      ff.path = path;
      if (ReadFontInfo(fd, 0, &ff)) {
        out->push_back(ff);
        added++;
      }
    }
  }
  close(fd);
  return added;
}

// True for a file that can hold fonts: TrueType and OpenType files, Mac
// .dfont files, and (on Mac OS X and the classic Mac OS) files without an
// extension, which can be font suitcases.
bool FontExtension(const char* name) {
  const char* dot = strrchr(name, '.');
#if defined(__APPLE__) || defined(macintosh)
  if (!dot) return true;
#else
  if (!dot) return false;
#endif
  return !strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf") || !strcasecmp(dot, ".ttc") ||
         !strcasecmp(dot, ".dfont");
}

void ScanDir(const std::string& dir, int depth, std::set<std::pair<dev_t, ino_t>>* seen,
             std::vector<FontFile>* out) {
  if (depth > 8) return;
  struct stat st;
  if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return;
  if (!seen->insert(std::make_pair(st.st_dev, st.st_ino)).second) return;
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  std::vector<std::string> names;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    names.push_back(e->d_name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  for (const std::string& n : names) {
    std::string p = dir + "/" + n;
    if (stat(p.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode))
      ScanDir(p, depth + 1, seen, out);
    else if (S_ISREG(st.st_mode) && FontExtension(n.c_str()))
      ReadFontFile(p, out);
  }
}

// The folders to search for font files. WINPORT_FONTS replaces the list
// ("builtin" gives no folders); otherwise the folders of the platform.
std::vector<std::string> FontDirs() {
  std::vector<std::string> dirs;
  const char* env = getenv("WINPORT_FONTS");
  if (env) {
    std::string s = env;
    if (s == "builtin") return dirs;
    size_t start = 0;
#if defined(__MSDOS__)
    // A DOS path has a drive letter and a colon, so ';' divides the list.
    const char sep = ';';
#else
    const char sep = ':';
#endif
    while (start <= s.size()) {
      size_t c = s.find(sep, start);
      if (c == std::string::npos) c = s.size();
      if (c > start) dirs.push_back(s.substr(start, c - start));
      start = c + 1;
    }
    return dirs;
  }
  const char* home = getenv("HOME");
#if defined(macintosh)
  // The Fonts folder in the System Folder (System 7.1 and later).
  (void)home;
  std::string fonts;
  if (mactb::FontsFolder(&fonts)) dirs.push_back(fonts);
#elif defined(__MSDOS__)
  // The FONTS folder next to the program, then the Fonts folder of a
  // Windows installation on the same machine.
  (void)home;
  if (__argc > 0 && __argv[0]) {
    std::string exe = __argv[0];
    size_t slash = exe.find_last_of("\\/:");
    dirs.push_back((slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/FONTS");
  }
  if (const char* windir = getenv("WINDIR")) dirs.push_back(std::string(windir) + "/FONTS");
  dirs.push_back("C:/WINDOWS/FONTS");
#elif defined(__APPLE__)
  dirs.push_back("/System/Library/Fonts");
  dirs.push_back("/Library/Fonts");
  if (home) dirs.push_back(std::string(home) + "/Library/Fonts");
#else
  if (const char* xdg = getenv("XDG_DATA_HOME"))
    dirs.push_back(std::string(xdg) + "/fonts");
  else if (home)
    dirs.push_back(std::string(home) + "/.local/share/fonts");
  if (home) dirs.push_back(std::string(home) + "/.fonts");
  dirs.push_back("/usr/local/share/fonts");
  dirs.push_back("/usr/share/fonts");
#endif
  return dirs;
}

// The index of every font found, made the first time a font is used.
struct FontIndex {
  std::mutex lock;
  bool scanned = false;
  std::vector<FontFile> files;
  void Scan() {
    if (scanned) return;
    scanned = true;
    std::set<std::pair<dev_t, ino_t>> seen;
    for (const std::string& d : FontDirs()) ScanDir(d, 0, &seen, &files);
  }
};

FontIndex& Index() {
  static FontIndex* idx = new FontIndex;
  return *idx;
}

bool SameName(const std::wstring& a, const wchar_t* b) {
  size_t n = wcslen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; i++)
    if (towlower(a[i]) != towlower(b[i])) return false;
  return true;
}

// The families to try for a Windows face name, best first.
const wchar_t* const kSans[] = {L"Arial", L"Liberation Sans", L"Helvetica", L"Arimo", L"DejaVu Sans",
                                L"FreeSans", L"Geneva", L"Lucida Grande", L"Verdana", L"Noto Sans",
                                L"Inter", NULL};
const wchar_t* const kSerif[] = {L"Times New Roman", L"Liberation Serif", L"Times", L"Tinos", L"DejaVu Serif",
                                 L"FreeSerif", L"Noto Serif", NULL};
const wchar_t* const kMono[] = {L"Courier New", L"Liberation Mono", L"Courier", L"Cousine", L"DejaVu Sans Mono",
                                L"FreeMono", L"Menlo", L"Monaco", L"Noto Sans Mono", NULL};
const wchar_t* const kUi[] = {L"Tahoma", L"Microsoft Sans Serif", L"Arial", L"Liberation Sans", L"Helvetica",
                              L"Arimo", L"DejaVu Sans", L"FreeSans", L"Geneva", L"Lucida Grande", NULL};

enum Kind { kKindSans, kKindSerif, kKindMono, kKindUi };

// Windows names and what they stand for.
struct Alias {
  const wchar_t* name;
  Kind kind;
};
const Alias kAliases[] = {
    {L"MS Sans Serif", kKindUi}, {L"MS Shell Dlg", kKindUi},     {L"MS Shell Dlg 2", kKindUi},
    {L"System", kKindUi},        {L"Microsoft Sans Serif", kKindUi}, {L"Tahoma", kKindUi},
    {L"Segoe UI", kKindUi},      {L"Small Fonts", kKindUi},      {L"Arial", kKindSans},
    {L"Helvetica", kKindSans},   {L"Verdana", kKindSans},        {L"MS Serif", kKindSerif},
    {L"Times New Roman", kKindSerif}, {L"Times", kKindSerif},    {L"Georgia", kKindSerif},
    {L"Courier New", kKindMono}, {L"Courier", kKindMono},        {L"Fixedsys", kKindMono},
    {L"Terminal", kKindMono},    {L"Lucida Console", kKindMono}, {L"Consolas", kKindMono},
};

const wchar_t* const* KindList(Kind k) {
  switch (k) {
    case kKindSerif: return kSerif;
    case kKindMono: return kMono;
    case kKindUi: return kUi;
    default: return kSans;
  }
}

// Picks the best font of a family for a weight and slant.
const FontFile* PickStyle(const std::vector<FontFile>& files, const std::wstring& family, int weight,
                          bool italic) {
  const FontFile* best = NULL;
  int bestScore = 0;
  for (const FontFile& f : files) {
    if (!SameName(f.family, family.c_str())) continue;
    int score = abs(f.weight - weight) + (f.italic != italic ? 2000 : 0);
    // Prefer the plain style over, say, "Condensed" at the same weight.
    if (!f.style.empty() && !SameName(f.style, L"Regular") && !SameName(f.style, L"Bold") &&
        !SameName(f.style, L"Italic") && !SameName(f.style, L"Bold Italic") && !SameName(f.style, L"Book") &&
        !SameName(f.style, L"Oblique") && !SameName(f.style, L"Bold Oblique") && !SameName(f.style, L"Roman"))
      score += 50;
    if (!best || score < bestScore) {
      best = &f;
      bestScore = score;
    }
  }
  return best;
}

const FontFile* Match(const LOGFONTW& lf) {
  FontIndex& idx = Index();
  idx.Scan();
  if (idx.files.empty()) return NULL;
  int weight = lf.lfWeight == FW_DONTCARE ? FW_NORMAL : (int)lf.lfWeight;
  bool italic = lf.lfItalic != 0;
  std::wstring face = lf.lfFaceName;
  std::vector<std::wstring> tries;
  if (!face.empty()) tries.push_back(face);
  Kind kind = kKindSans;
  bool known = false;
  for (const Alias& a : kAliases)
    if (!face.empty() && SameName(face, a.name)) {
      kind = a.kind;
      known = true;
    }
  if (!known) {
    int fam = lf.lfPitchAndFamily & 0xF0;
    if ((lf.lfPitchAndFamily & 3) == FIXED_PITCH || fam == FF_MODERN)
      kind = kKindMono;
    else if (fam == FF_ROMAN)
      kind = kKindSerif;
    else if (face.empty() && fam == FF_DONTCARE)
      kind = kKindUi;
  }
  for (const wchar_t* const* l = KindList(kind); *l; l++) tries.push_back(*l);
  for (const wchar_t* const* l = kSans; *l; l++) tries.push_back(*l);
  for (const std::wstring& t : tries)
    if (const FontFile* f = PickStyle(idx.files, t, weight, italic)) return f;
  // Anything at all, preferring a plain style.
  return PickStyle(idx.files, idx.files[0].family, weight, italic);
}

// ---------------------------------------------------------------------
// Loaded font files

}  // namespace

struct FontSource {
  std::vector<BYTE> data;
  stbtt_fontinfo info = {};
  int unitsPerEm = 2048;
  int winAscent = 0, winDescent = 0;
  int hheaAscent = 0, hheaDescent = 0, lineGap = 0;
  int avgWidth = 0;
  int xMin = 0, xMax = 0;
  int underlinePos = 0, underlineSize = 0;
  int strikePos = 0, strikeSize = 0;
  int weight = FW_NORMAL;
  bool italic = false, fixedPitch = false;
  bool macRoman = false;  // the cmap has Mac Roman codes, not Unicode
};

namespace {

const BYTE* Table(const FontSource* s, const char* tag, UINT32* len) {
  UINT32 off = (UINT32)s->info.fontstart;
  const BYTE* d = s->data.data();
  if (off + 12 > s->data.size()) return NULL;
  int n = U16(d + off + 4);
  UINT32 want = ((UINT32)(BYTE)tag[0] << 24) | ((UINT32)(BYTE)tag[1] << 16) | ((UINT32)(BYTE)tag[2] << 8) | (BYTE)tag[3];
  for (int i = 0; i < n; i++) {
    size_t r = off + 12 + (size_t)i * 16;
    if (r + 16 > s->data.size()) return NULL;
    if (U32(d + r) != want) continue;
    UINT32 to = U32(d + r + 8), tl = U32(d + r + 12);
    if ((size_t)to + tl > s->data.size()) return NULL;
    *len = tl;
    return d + to;
  }
  return NULL;
}

// Tells if the cmap table of a font has no Unicode subtable (platform 0,
// or platform 3 encoding 1 or 10). The TrueType fonts of System 7 have
// only a Mac Roman subtable.
bool OnlyMacRoman(const FontSource* s) {
  UINT32 len = 0;
  const BYTE* cmap = Table(s, "cmap", &len);
  if (!cmap || len < 4) return false;
  int n = U16(cmap + 2);
  for (int i = 0; i < n && 4 + 8 * (UINT32)(i + 1) <= len; i++) {
    int platform = U16(cmap + 4 + 8 * i), encoding = U16(cmap + 6 + 8 * i);
    if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) return false;
  }
  return true;
}

// Gives the Mac Roman code of a Unicode character, or 0 if Mac Roman has
// no such character.
int ToMacRomanCode(UINT32 ch) {
  static const UINT16 kHigh[128] = {
      0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5,
      0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4,
      0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6,
      0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265,
      0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF,
      0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5,
      0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044,
      0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
      0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9,
      0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
  };
  if (ch < 0x80) return (int)ch;
  for (int i = 0; i < 128; i++)
    if (kHigh[i] == ch) return 0x80 + i;
  return 0;
}

// Gives the glyph of a Unicode character in a font, or 0. For a font
// with only a Mac Roman cmap, the character is first changed to its Mac
// Roman code. A code of a symbol font (U+F020 to U+F0FF) is used as a
// byte.
int GlyphIndex(const FontSource* s, UINT32 ch) {
  if (!s) return 0;
  if (s->macRoman) {
    int code = ch >= 0xF020 && ch <= 0xF0FF ? (int)(ch - 0xF000) : ToMacRomanCode(ch);
    return code ? stbtt_FindGlyphIndex(&s->info, code) : 0;
  }
  return stbtt_FindGlyphIndex(&s->info, (int)ch);
}

FontSource* LoadSource(const FontFile& ff) {
  static std::map<std::pair<std::string, int>, FontSource*>* loaded =
      new std::map<std::pair<std::string, int>, FontSource*>;
  auto key = std::make_pair(ff.path, ff.index + (int)ff.base);
  auto it = loaded->find(key);
  if (it != loaded->end()) return it->second;
  FontSource* s = NULL;
  FILE* f = fopen(ff.path.c_str(), "rb");
  if (f) {
    std::unique_ptr<FontSource> src(new FontSource);
    long size = (long)ff.length;
    if (!ff.length) {
      fseek(f, 0, SEEK_END);
      size = ftell(f);
    }
    fseek(f, (long)ff.base, SEEK_SET);
    if (size > 0 && size < (64L << 20)) {
      src->data.resize((size_t)size);
      if (fread(src->data.data(), 1, (size_t)size, f) == (size_t)size) {
        int off = stbtt_GetFontOffsetForIndex(src->data.data(), ff.index);
        if (off >= 0 && stbtt_InitFont(&src->info, src->data.data(), off)) s = src.release();
      }
    }
    fclose(f);
  }
  if (s) {
    UINT32 len = 0;
    if (const BYTE* head = Table(s, "head", &len))
      if (len >= 54) {
        s->unitsPerEm = U16(head + 18);
        s->xMin = S16(head + 36);
        s->xMax = S16(head + 40);
      }
    if (s->unitsPerEm <= 0) s->unitsPerEm = 2048;
    s->macRoman = OnlyMacRoman(s);
    stbtt_GetFontVMetrics(&s->info, &s->hheaAscent, &s->hheaDescent, &s->lineGap);
    s->winAscent = s->hheaAscent;
    s->winDescent = -s->hheaDescent;
    if (const BYTE* os2 = Table(s, "OS/2", &len)) {
      if (len >= 78) {
        s->avgWidth = S16(os2 + 2);
        s->strikeSize = S16(os2 + 26);
        s->strikePos = S16(os2 + 28);
        s->winAscent = U16(os2 + 74);
        s->winDescent = U16(os2 + 76);
      }
    }
    if (const BYTE* post = Table(s, "post", &len))
      if (len >= 16) {
        s->underlinePos = S16(post + 8);
        s->underlineSize = S16(post + 10);
      }
    if (s->winAscent + s->winDescent <= 0) {
      s->winAscent = s->unitsPerEm * 4 / 5;
      s->winDescent = s->unitsPerEm / 5;
    }
    s->weight = ff.weight;
    s->italic = ff.italic;
    s->fixedPitch = ff.fixedPitch;
  }
  (*loaded)[key] = s;
  return s;
}

int Round(double v) { return (int)floor(v + 0.5); }

// The built-in font's image of a character.
const unsigned char* BuiltinRows(UINT32 ch, bool bold) {
  for (int i = 0; i < kBuiltinCount; i++)
    if (kBuiltinChars[i] == ch) return bold ? kBuiltinBold[i] : kBuiltinRegular[i];
  return NULL;
}

struct FontKey {
  LOGFONTW lf;
  bool operator<(const FontKey& o) const { return memcmp(&lf, &o.lf, sizeof lf) < 0; }
};

}  // namespace

// ---------------------------------------------------------------------
// Glyphs

namespace {

// The families to draw a symbol from when the font asked for has none.
// Wingdings' pictures (U+1F4xx to U+1F5xx) are mostly in the emoji block,
// so the outline emoji fonts follow, and last GNU Unifont's plane 1, which
// has them all. Colour emoji fonts are bitmaps, which cannot be drawn
// as outlines, and are left out.
const wchar_t* const kSymbolFallbacks[] = {L"Symbola", L"Noto Sans Symbols 2", L"Noto Sans Symbols2",
                                           L"Noto Sans Symbols", L"DejaVu Sans", L"Segoe UI Symbol",
                                           L"Apple Symbols", L"Arial Unicode MS", L"FreeSerif", L"FreeSans",
                                           L"OpenSymbol", L"Noto Sans Math", L"STIX Two Math", L"Noto Emoji",
                                           L"OpenMoji", L"Twemoji Mozilla", L"Unifont Upper", NULL};

// An installed font that has the character, or NULL.
FontSource* FallbackFor(UINT32 ch) {
  static std::mutex* lock = new std::mutex;
  static std::map<UINT32, FontSource*>* found = new std::map<UINT32, FontSource*>;
  {
    std::lock_guard<std::mutex> g(*lock);
    auto it = found->find(ch);
    if (it != found->end()) return it->second;
  }
  FontSource* out = NULL;
  {
    std::lock_guard<std::mutex> g(Index().lock);
    Index().Scan();
    for (const wchar_t* const* f = kSymbolFallbacks; *f && !out; f++)
      if (const FontFile* ff = PickStyle(Index().files, *f, FW_NORMAL, false))
        if (FontSource* s = LoadSource(*ff))
          if (int g = GlyphIndex(s, ch))
            if (!stbtt_IsGlyphEmpty(&s->info, g)) out = s;
  }
  std::lock_guard<std::mutex> g(*lock);
  (*found)[ch] = out;
  return out;
}

}  // namespace

// Where a character's outline comes from: the font itself, or for a symbol
// font that is not installed, the Unicode character from it or another
// font. False for the built-in bitmap font.
bool RealFont::Source(UINT32 ch, FontSource** s, int* glyph, float* sx, float* sy) {
  *s = src;
  *sx = scaleX;
  *sy = scaleY;
  if (symbolFont) {
    UINT32 b = ch >= 0xF020 && ch <= 0xF0FF ? ch - 0xF000 : ch;
    if (b < 0x100) {
      if (!symbolMap) {
        // The font itself: its characters sit at U+F020 to U+F0FF.
        *glyph = GlyphIndex(src, 0xF000 + b);
        if (!*glyph) *glyph = GlyphIndex(src, b);
        return true;
      }
      if (symbolMap[b]) ch = symbolMap[b];
    }
  }
  if (src) *glyph = GlyphIndex(src, ch);
  if (symbolMap && (!src || !*glyph)) {
    if (FontSource* fb = FallbackFor(ch)) {
      // The same em size as the font asked for.
      double em = src ? scaleY * src->unitsPerEm : (double)kBuiltinAscent * bitmapScale;
      *s = fb;
      *sy = (float)(em / fb->unitsPerEm);
      *sx = *sy * (src ? scaleX / scaleY : 1.0f);
      *glyph = GlyphIndex(fb, ch);
      return true;
    }
  }
  return src != NULL;
}

bool RealFont::HasGlyph(UINT32 ch) {
  FontSource* s;
  int glyph = 0;
  float sx, sy;
  if (!Source(ch, &s, &glyph, &sx, &sy)) return BuiltinRows(ch, false) != NULL;
  return glyph != 0;
}

// Gives the outline of character ch in font units, as closed contours of
// points; curves are given by their on-curve and control points.
bool RealFont::Outline(UINT32 ch, std::vector<std::vector<OutlinePoint>>* contours) {
  contours->clear();
  FontSource* from = NULL;
  int glyph = 0;
  float sx = scaleX, sy = scaleY;
  if (!Source(ch, &from, &glyph, &sx, &sy)) return false;
  std::lock_guard<std::mutex> g(lock_);
  stbtt_vertex* v = NULL;
  int n = stbtt_GetGlyphShape(&from->info, glyph, &v);
  // Synthetic italic slants by a fifth of the height, as Render does.
  float slant = synthItalic ? 0.2f : 0.0f;
  auto add = [&](float x, float y, bool on) {
    OutlinePoint p;
    p.x = x * sx + y * sy * slant;
    p.y = y * sy;
    p.onCurve = on;
    contours->back().push_back(p);
  };
  float cx = 0, cy = 0;  // the pen, in font units
  for (int i = 0; i < n; i++) {
    const stbtt_vertex& e = v[i];
    switch (e.type) {
      case STBTT_vmove:
        contours->emplace_back();
        add(e.x, e.y, true);
        break;
      case STBTT_vline:
        if (!contours->empty()) add(e.x, e.y, true);
        break;
      case STBTT_vcurve:
        if (!contours->empty()) {
          add(e.cx, e.cy, false);
          add(e.x, e.y, true);
        }
        break;
      case STBTT_vcubic:
        // CFF outlines: the cubic curve as eight straight segments.
        if (!contours->empty())
          for (int k = 1; k <= 8; k++) {
            float t = k / 8.0f, u = 1 - t;
            float x = u * u * u * cx + 3 * u * u * t * e.cx + 3 * u * t * t * e.cx1 + t * t * t * e.x;
            float y = u * u * u * cy + 3 * u * u * t * e.cy + 3 * u * t * t * e.cy1 + t * t * t * e.y;
            add(x, y, true);
          }
        break;
    }
    cx = e.x;
    cy = e.y;
  }
  stbtt_FreeShape(&from->info, v);
  // Drop the closing point where it repeats the first.
  for (auto& c : *contours)
    if (c.size() > 1 && c.back().onCurve && c.back().x == c.front().x && c.back().y == c.front().y) c.pop_back();
  contours->erase(std::remove_if(contours->begin(), contours->end(),
                                 [](const std::vector<OutlinePoint>& c) { return c.size() < 2; }),
                  contours->end());
  return true;
}

// Gives the size of the em square of the font, in font units.
int RealFont::EmSquare() { return src ? src->unitsPerEm : 0; }

const GlyphImage& RealFont::Glyph(UINT32 ch) {
  std::lock_guard<std::mutex> g(lock_);
  auto it = cache_.find(ch);
  if (it != cache_.end()) return it->second;
  GlyphImage& img = cache_[ch];
  Render(ch, &img);
  return img;
}

void RealFont::Render(UINT32 ch, GlyphImage* g) {
  FontSource* from = NULL;
  int glyph = 0;
  float sx = scaleX, sy = scaleY;
  if (!Source(ch, &from, &glyph, &sx, &sy)) {
    if (symbolFont && ch >= 0xF020 && ch <= 0xF0FF) ch -= 0xF000;
    if (symbolMap && ch < 0x100 && symbolMap[ch]) ch = symbolMap[ch];
    const unsigned char* rows = BuiltinRows(ch, boldBitmap);
    if (!rows) rows = BuiltinRows(0xFFFD, boldBitmap);
    int s = bitmapScale;
    int cellH = kBuiltinAscent + kBuiltinDescent;
    g->advance = kBuiltinWidth * s;
    g->left = 0;
    g->top = -kBuiltinAscent * s;
    g->w = kBuiltinWidth * s;
    g->h = cellH * s;
    g->cov.assign((size_t)g->w * g->h, 0);
    if (ch != ' ' && ch != 0xA0)
      for (int y = 0; y < cellH; y++)
        for (int x = 0; x < kBuiltinWidth; x++)
          if (rows[y] & (0x80 >> x))
            for (int yy = 0; yy < s; yy++)
              for (int xx = 0; xx < s; xx++) g->cov[(size_t)(y * s + yy) * g->w + x * s + xx] = 255;
  } else {
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&from->info, glyph, &adv, &lsb);
    g->advance = Round(adv * sx);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&from->info, glyph, sx, sy, &x0, &y0, &x1, &y1);
    g->left = x0;
    g->top = y0;
    g->w = x1 - x0;
    g->h = y1 - y0;
    if (g->w > 0 && g->h > 0 && g->w < 4096 && g->h < 4096) {
      g->cov.assign((size_t)g->w * g->h, 0);
      stbtt_MakeGlyphBitmap(&from->info, g->cov.data(), g->w, g->h, g->w, sx, sy, glyph);
    } else {
      g->w = g->h = 0;
    }
  }
  if (synthItalic && g->w > 0) {
    // Slant by a fifth of the height above the baseline.
    int top = g->top, bottom = g->top + g->h;
    int shiftTop = Round(-top * 0.2), shiftBottom = Round(-bottom * 0.2);
    int minShift = std::min(shiftTop, shiftBottom), maxShift = std::max(shiftTop, shiftBottom);
    int nw = g->w + maxShift - minShift;
    std::vector<BYTE> out((size_t)nw * g->h, 0);
    for (int y = 0; y < g->h; y++) {
      int sh = Round(-(top + y) * 0.2) - minShift;
      memcpy(&out[(size_t)y * nw + sh], &g->cov[(size_t)y * g->w], (size_t)g->w);
    }
    g->cov.swap(out);
    g->w = nw;
    g->left += minShift;
  }
  if (synthBold && g->w > 0) {
    int nw = g->w + 1;
    std::vector<BYTE> out((size_t)nw * g->h, 0);
    for (int y = 0; y < g->h; y++)
      for (int x = 0; x < nw; x++) {
        int a = x < g->w ? g->cov[(size_t)y * g->w + x] : 0;
        int b = x > 0 ? g->cov[(size_t)y * g->w + x - 1] : 0;
        out[(size_t)y * nw + x] = (BYTE)std::max(a, b);
      }
    g->cov.swap(out);
    g->w = nw;
  }
  if (synthBold) g->advance += 1;
}

// ---------------------------------------------------------------------
// Realizing fonts

RealFont* RealizeFont(const LOGFONTW& in) {
  static std::mutex* lock = new std::mutex;
  static std::map<FontKey, RealFont*>* fonts = new std::map<FontKey, RealFont*>;
  FontKey key;
  memset(&key.lf, 0, sizeof key.lf);
  key.lf = in;
  // Only the name up to its terminator counts.
  size_t n = 0;
  while (n < LF_FACESIZE && in.lfFaceName[n]) n++;
  memset(key.lf.lfFaceName, 0, sizeof key.lf.lfFaceName);
  wmemcpy(key.lf.lfFaceName, in.lfFaceName, n < LF_FACESIZE ? n : LF_FACESIZE - 1);
  std::lock_guard<std::mutex> g(*lock);
  auto it = fonts->find(key);
  if (it != fonts->end()) return it->second;

  const LOGFONTW& lf = key.lf;
  RealFont* f = new RealFont;
  f->lf = lf;
  f->italic = lf.lfItalic != 0;
  f->underline = lf.lfUnderline != 0;
  f->strikeOut = lf.lfStrikeOut != 0;
  f->weight = lf.lfWeight == FW_DONTCARE ? FW_NORMAL : (int)lf.lfWeight;
  f->charset = lf.lfCharSet == DEFAULT_CHARSET ? ANSI_CHARSET : lf.lfCharSet;
  f->escapement = lf.lfEscapement;
  f->antialias = lf.lfQuality != NONANTIALIASED_QUALITY && lf.lfQuality != DRAFT_QUALITY;
  bool wantBold = f->weight >= 600;

  const FontFile* ff;
  {
    std::lock_guard<std::mutex> ig(Index().lock);
    ff = Match(lf);
  }
  FontSource* src = ff ? LoadSource(*ff) : NULL;
  if (src) {
    f->src = src;
    f->scalable = true;
    f->family = ff->family;
    f->fixedPitch = src->fixedPitch;
    double cell = src->winAscent + src->winDescent;
    double scale;
    if (lf.lfHeight < 0)
      scale = (double)-lf.lfHeight / src->unitsPerEm;
    else if (lf.lfHeight > 0)
      scale = lf.lfHeight / cell;
    else
      scale = 12.0 / src->unitsPerEm;
    f->scaleX = f->scaleY = (float)scale;
    if (lf.lfWidth > 0 && src->avgWidth > 0) f->scaleX = (float)(lf.lfWidth / (double)src->avgWidth);
    f->ascent = Round(src->winAscent * scale);
    f->descent = Round(src->winDescent * scale);
    f->height = f->ascent + f->descent;
    int em = Round(src->unitsPerEm * scale);
    f->internalLeading = std::max(0, f->height - em);
    int hheaHeight = src->hheaAscent - src->hheaDescent;
    int ext = src->lineGap - ((src->winAscent + src->winDescent) - hheaHeight);
    f->externalLeading = ext > 0 ? Round(ext * scale) : 0;
    int avg = src->avgWidth;
    if (avg <= 0) {
      int adv, lsb;
      stbtt_GetGlyphHMetrics(&src->info, GlyphIndex(src, 'x'), &adv, &lsb);
      avg = adv;
    }
    f->avgWidth = std::max(1, Round(avg * f->scaleX));
    f->maxWidth = std::max(f->avgWidth, Round((src->xMax - src->xMin) * f->scaleX));
    if (src->underlineSize > 0) {
      f->underlineSize = std::max(1, Round(src->underlineSize * scale));
      f->underlinePos = std::max(1, Round(-src->underlinePos * scale));
    } else {
      f->underlineSize = std::max(1, f->height / 14);
      f->underlinePos = std::max(1, f->descent / 2);
    }
    if (src->strikeSize > 0) {
      f->strikeSize = std::max(1, Round(src->strikeSize * scale));
      f->strikePos = Round(src->strikePos * scale);
    } else {
      f->strikeSize = f->underlineSize;
      f->strikePos = f->ascent / 3;
    }
    f->synthBold = wantBold && src->weight < 600;
    f->synthItalic = f->italic && !src->italic;
    if (!f->synthBold) f->weight = wantBold ? std::max(src->weight, 600) : src->weight;
  } else {
    // The built-in font, scaled by whole pixels to come nearest the height.
    int cell = kBuiltinAscent + kBuiltinDescent;
    int em = kBuiltinAscent;
    int s = 1;
    if (lf.lfHeight < 0) s = std::max(1, Round((double)-lf.lfHeight / em));
    if (lf.lfHeight > 0) s = std::max(1, Round((double)lf.lfHeight / cell));
    f->bitmapScale = s;
    f->boldBitmap = wantBold;
    f->family = L"Fixed";
    f->fixedPitch = true;
    f->ascent = kBuiltinAscent * s;
    f->descent = kBuiltinDescent * s;
    f->height = cell * s;
    f->internalLeading = 2 * s;
    f->avgWidth = f->maxWidth = kBuiltinWidth * s;
    f->underlinePos = s;
    f->underlineSize = s;
    f->strikePos = 3 * s;
    f->strikeSize = s;
    f->synthItalic = f->italic;
    f->antialias = false;
  }
  if (f->synthBold) {
    f->avgWidth += 1;
    f->maxWidth += 1;
  }
  f->face = lf.lfFaceName[0] ? std::wstring(lf.lfFaceName) : f->family;
  // Symbol and Wingdings are symbol fonts whatever the LOGFONT says; when
  // another family stands in, their characters are drawn as Unicode.
  const uint32_t* map = SameName(f->face, L"Symbol") ? kSymbolToUnicode
                        : SameName(f->face, L"Wingdings") ? kWingdingsToUnicode
                                                          : NULL;
  if (map) {
    f->symbolFont = true;
    f->charset = SYMBOL_CHARSET;
    if (!f->src || !SameName(f->family, f->face.c_str())) f->symbolMap = map;
  }
  BYTE family = lf.lfPitchAndFamily & 0xF0;
  if (family == FF_DONTCARE) family = f->fixedPitch ? FF_MODERN : FF_SWISS;
  // TMPF_FIXED_PITCH set means a variable pitch font.
  f->pitchAndFamily = (BYTE)(family | (f->fixedPitch ? 0 : TMPF_FIXED_PITCH) |
                             (f->scalable ? TMPF_VECTOR | TMPF_TRUETYPE : 0));
  (*fonts)[key] = f;
  return f;
}

std::vector<FamilyInfo> FontFamilies() {
  std::vector<FamilyInfo> out;
  std::set<std::wstring> have;
  auto add = [&](const std::wstring& name, bool fixed, BYTE family) {
    std::wstring low;
    for (wchar_t c : name) low += (wchar_t)towlower(c);
    if (!have.insert(low).second) return;
    FamilyInfo fi;
    fi.name = name;
    fi.fixedPitch = fixed;
    fi.family = family;
    out.push_back(fi);
  };
  for (const Alias& a : kAliases)
    add(a.name, a.kind == kKindMono, a.kind == kKindMono ? FF_MODERN : a.kind == kKindSerif ? FF_ROMAN : FF_SWISS);
  std::lock_guard<std::mutex> g(Index().lock);
  Index().Scan();
  for (const FontFile& f : Index().files) add(f.family, f.fixedPitch, f.fixedPitch ? FF_MODERN : FF_SWISS);
  return out;
}

int AddFontFile(const char* path) {
  std::lock_guard<std::mutex> g(Index().lock);
  Index().Scan();
  std::vector<FontFile> found;
  int n = ReadFontFile(path, &found);
  for (FontFile& f : found) {
    f.added = true;
    Index().files.push_back(f);
  }
  return n;
}

bool RemoveFontFile(const char* path) {
  std::lock_guard<std::mutex> g(Index().lock);
  std::vector<FontFile>& files = Index().files;
  size_t before = files.size();
  files.erase(std::remove_if(files.begin(), files.end(),
                             [&](const FontFile& f) { return f.added && f.path == path; }),
              files.end());
  return files.size() != before;
}

}  // namespace winport
