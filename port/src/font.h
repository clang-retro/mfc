// font.h - fonts as the drawing layer sees them: a LOGFONT matched to a
// font file (or to the built-in bitmap font) at a size in device pixels,
// with its metrics and its glyphs as coverage bitmaps.
//
// Font files are found by scanning the system's font folders the first
// time a font is used; WINPORT_FONTS=dir:dir... replaces those folders,
// and an empty WINPORT_FONTS (or "builtin") uses only the built-in font.
// Windows face names such as "MS Sans Serif" and "Courier New" map to
// whichever similar family is installed.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_FONT_H
#define _WINPORT_FONT_H

#include "winport.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace winport {

// One character's image: coverage from 0 to 255, placed relative to the
// pen position on the baseline (y grows downward).
struct GlyphImage {
  int left = 0, top = 0, w = 0, h = 0;
  int advance = 0;
  std::vector<BYTE> cov;
};

struct FontSource;

// A font at one size. Sizes are in device pixels.
struct RealFont {
  LOGFONTW lf = {};      // as asked for, with the height in device pixels
  std::wstring face;     // the face name GetTextFace reports
  std::wstring family;   // the family that was found
  int height = 0, ascent = 0, descent = 0;
  int internalLeading = 0, externalLeading = 0;
  int avgWidth = 0, maxWidth = 0;
  int weight = FW_NORMAL;
  bool italic = false, underline = false, strikeOut = false;
  bool fixedPitch = false, scalable = false;
  BYTE charset = ANSI_CHARSET, pitchAndFamily = FF_SWISS;
  bool antialias = true;
  int underlinePos = 1, underlineSize = 1;  // below the baseline
  int strikePos = 3, strikeSize = 1;        // above the baseline
  int escapement = 0;                       // tenths of a degree

  // A character's image and advance; thread safe.
  const GlyphImage& Glyph(UINT32 ch);
  int Advance(UINT32 ch) { return Glyph(ch).advance; }
  bool HasGlyph(UINT32 ch);

  // How the font is made
  FontSource* src = NULL;  // NULL: the built-in bitmap font
  float scaleX = 1, scaleY = 1;
  int bitmapScale = 1;
  bool boldBitmap = false;
  bool synthBold = false, synthItalic = false;

 private:
  std::mutex lock_;
  std::unordered_map<UINT32, GlyphImage> cache_;
  void Render(UINT32 ch, GlyphImage* g);
};

// The font for a LOGFONT whose height and width are already in device
// pixels. Fonts are kept for the life of the process.
RealFont* RealizeFont(const LOGFONTW& lf);

// The families that can be asked for by name: the installed ones and the
// usual Windows names. Each has whether it is fixed pitch.
struct FamilyInfo {
  std::wstring name;
  bool fixedPitch = false;
  BYTE family = FF_SWISS;
};
std::vector<FamilyInfo> FontFamilies();

// Adds a font file, as AddFontResource does; returns how many fonts it held.
int AddFontFile(const char* path);
bool RemoveFontFile(const char* path);

}  // namespace winport

#endif
