// rich.h - what the rich edit control's parts share: character and
// paragraph formats, the objects in the text, and the rich text (RTF)
// reader and writer (rich_rtf.cpp) that richedit.cpp uses for
// EM_STREAMIN and EM_STREAMOUT.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <ole2.h>
#include <richedit.h>
#include <richole.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace winport {
namespace rich {

// The effects that are bits of dwEffects whose mask bit is the same bit
// (CFE_SUBSCRIPT and CFE_SUPERSCRIPT share the mask CFM_SUBSCRIPT).
const DWORD kEffectBits = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT | CFE_PROTECTED | CFE_LINK |
                          CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR | CFE_SUBSCRIPT | CFE_SUPERSCRIPT | CFE_SMALLCAPS |
                          CFE_ALLCAPS | CFE_HIDDEN | CFE_OUTLINE | CFE_SHADOW | CFE_EMBOSS | CFE_IMPRINT |
                          CFE_DISABLED | CFE_REVISED;

// The character an embedded object stands on in the text.
const wchar_t kObjectChar = 0xFFFC;

struct CharFmt {
  DWORD effects = CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR;
  LONG height = 200;  // twips
  LONG offset = 0;    // twips above the baseline
  COLORREF color = 0, back = 0;
  BYTE charset = DEFAULT_CHARSET, pitch = DEFAULT_PITCH | FF_DONTCARE;
  std::wstring face = L"Arial";
  WORD weight = FW_NORMAL;
  SHORT spacing = 0, style = 0;
  WORD kerning = 0;
  LCID lcid = 0;
  BYTE underlineType = 0, animation = 0, revAuthor = 0;

  bool operator==(const CharFmt& o) const {
    return effects == o.effects && height == o.height && offset == o.offset && color == o.color &&
           back == o.back && charset == o.charset && pitch == o.pitch && face == o.face && weight == o.weight &&
           spacing == o.spacing && style == o.style && kerning == o.kerning && lcid == o.lcid &&
           underlineType == o.underlineType && animation == o.animation && revAuthor == o.revAuthor;
  }
  bool operator!=(const CharFmt& o) const { return !(*this == o); }
};

struct ParaFmt {
  WORD numbering = 0, effects = 0;  // effects: PFE_ bits
  LONG startIndent = 0, rightIndent = 0, offset = 0;
  WORD align = PFA_LEFT;
  std::vector<LONG> tabs;
  LONG spaceBefore = 0, spaceAfter = 0, lineSpacing = 0;
  SHORT style = 0;
  BYTE lineRule = 0, outline = 0;
  WORD shadingWeight = 0, shadingStyle = 0, numberingStart = 0, numberingStyle = 0, numberingTab = 0;
  WORD borderSpace = 0, borderWidth = 0, borders = 0;

  bool operator==(const ParaFmt& o) const {
    return numbering == o.numbering && effects == o.effects && startIndent == o.startIndent &&
           rightIndent == o.rightIndent && offset == o.offset && align == o.align && tabs == o.tabs &&
           spaceBefore == o.spaceBefore && spaceAfter == o.spaceAfter && lineSpacing == o.lineSpacing &&
           style == o.style && lineRule == o.lineRule && outline == o.outline &&
           shadingWeight == o.shadingWeight && shadingStyle == o.shadingStyle &&
           numberingStart == o.numberingStart && numberingStyle == o.numberingStyle &&
           numberingTab == o.numberingTab && borderSpace == o.borderSpace && borderWidth == o.borderWidth &&
           borders == o.borders;
  }
  bool operator!=(const ParaFmt& o) const { return !(*this == o); }
};

// An object in the text: what REOBJECT describes, with the references
// the control holds.
struct Obj {
  IOleObject* ole = NULL;
  IStorage* stg = NULL;
  IOleClientSite* site = NULL;
  CLSID clsid = {};
  SIZEL sizel = {0, 0};  // HIMETRIC
  DWORD aspect = DVASPECT_CONTENT;
  DWORD flags = 0;
  DWORD user = 0;
  DWORD viewAdvise = 0;
  ~Obj();
};

// One character of a span of text being written as rich text.
struct RichChar {
  wchar_t ch;
  const CharFmt* cf;
  const ParaFmt* pf;  // for a paragraph mark ('\r')
  Obj* obj;           // for kObjectChar
};

// What the writer is given: the characters, and the format of the last
// paragraph when the span is the whole text (whose last paragraph has no
// mark of its own).
struct RtfSpan {
  std::vector<RichChar> chars;
  const ParaFmt* tail = NULL;
};

// The native data of an object: its ProgID (or CLSID string) and its
// storage as a compound file.
typedef std::function<bool(Obj*, std::string* className, std::vector<BYTE>* native)> ObjectSaver;
// Makes an object from that again; NULL when it cannot be.
typedef std::function<std::shared_ptr<Obj>(const std::string& className, const std::vector<BYTE>& native,
                                           LONG cxTwips, LONG cyTwips)>
    ObjectLoader;

std::string WriteRtf(const RtfSpan& span, bool withObjects, const ObjectSaver& saver);

// What the reader makes: characters with their formats.
struct RtfText {
  std::wstring text;
  std::vector<CharFmt> cf;  // one per character
  std::vector<ParaFmt> pf;  // one per character; meaningful at marks
  std::vector<std::shared_ptr<Obj>> obj;
  ParaFmt last;  // the paragraph state at the end
};

bool ReadRtf(const std::string& in, const CharFmt& base, const ObjectLoader& loader, RtfText* out);

// Whether bytes look like rich text.
bool IsRtf(const char* p, size_t n);

}  // namespace rich
}  // namespace winport
