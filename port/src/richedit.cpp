// richedit.cpp - the rich edit control (RichEdit20W, RichEdit20A and the
// 1.0 class RICHEDIT): text with character and paragraph formats, word
// wrap to the window or to a target device, printing a page at a time
// with EM_FORMATRANGE, rich text and plain text in and out, finding,
// undo, the clipboard, and OLE objects in the text through IRichEditOle
// and the container's IRichEditOleCallback.
//
// Paragraphs end with a lone CR, as in rich edit 2.0: WM_GETTEXT gives
// the text that way and character positions count it once. Each
// character has a character format; a paragraph's format is kept on its
// mark, and the last paragraph's, which has none, on the control. An
// object stands on one character, U+FFFC.
//
// The ANSI classes take ANSI text where the Unicode ones take Unicode:
// in EM_GETSELTEXT, EM_GETTEXTRANGE and the CHARFORMAT face name (the
// structure's size tells which form it is).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "ole.h"
#include "rich.h"
#include "user.h"

#include <commdlg.h>
#include <string.h>
#include <wctype.h>

#include <algorithm>
#include <climits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace winport;
using namespace winport::rich;
using winport::ole::Assign;
using winport::ole::SafeRelease;

namespace winport {
namespace rich {

Obj::~Obj() {
  if (ole) {
    if (viewAdvise) {
      IViewObject* v = NULL;
      if (SUCCEEDED(ole->QueryInterface(IID_IViewObject, (void**)&v))) {
        v->SetAdvise(aspect, 0, NULL);
        v->Release();
      }
    }
    ole->Close(OLECLOSE_NOSAVE);
    SafeRelease(ole);
  }
  SafeRelease(stg);
  SafeRelease(site);
}

}  // namespace rich
}  // namespace winport

namespace {

struct Attr {
  int cf = 0;
  int pf = 0;  // for a paragraph mark
  std::shared_ptr<Obj> obj;
};

struct Line {
  int start = 0, len = 0;  // the characters shown
  int end = 0;             // where the next line starts
  int y = 0, height = 0, ascent = 0;
  int x = 0;  // the line's left, from the left of the layout
  int width = 0;
  std::vector<int> xs;  // len + 1 boundaries, from the line's left
};

struct FontInfo {
  HFONT font = NULL;
  int ascent = 0, descent = 0, avg = 0;
};

// Fonts for the formats at one resolution, by format number.
class Fonts {
 public:
  explicit Fonts(int dpiY) : dpi_(dpiY) {}
  ~Fonts() { Clear(); }
  int Dpi() const { return dpi_; }
  const FontInfo& Get(const CharFmt& f, int key) {
    auto it = map_.find(key);
    if (it != map_.end()) return it->second;
    LOGFONTW lf = {};
    int h = MulDiv(f.height, dpi_, 1440);
    if (f.effects & (CFE_SUBSCRIPT | CFE_SUPERSCRIPT)) h = h * 2 / 3;
    lf.lfHeight = -std::max(1, h);
    lf.lfWeight = f.weight ? f.weight : ((f.effects & CFE_BOLD) ? FW_BOLD : FW_NORMAL);
    lf.lfItalic = (f.effects & CFE_ITALIC) ? TRUE : FALSE;
    lf.lfUnderline = (f.effects & CFE_UNDERLINE) ? TRUE : FALSE;
    lf.lfStrikeOut = (f.effects & CFE_STRIKEOUT) ? TRUE : FALSE;
    lf.lfCharSet = f.charset;
    lf.lfPitchAndFamily = f.pitch;
    wcsncpy(lf.lfFaceName, f.face.c_str(), LF_FACESIZE - 1);
    FontInfo fi;
    fi.font = CreateFontIndirectW(&lf);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, fi.font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    DeleteDC(dc);
    // The metrics at this resolution, though the DC is the screen's.
    fi.ascent = tm.tmAscent;
    fi.descent = tm.tmDescent;
    fi.avg = tm.tmAveCharWidth ? tm.tmAveCharWidth : 1;
    return map_[key] = fi;
  }
  void Clear() {
    for (auto& p : map_) DeleteObject(p.second.font);
    map_.clear();
  }

 private:
  int dpi_;
  std::map<int, FontInfo> map_;
};

enum Wrap { kWrapWindow, kWrapNone, kWrapWidth };

class ReOle;
class Site;

struct Snapshot {
  std::wstring text;
  std::vector<Attr> attr;
  int tailPf = 0;
  int anchor = 0, caret = 0;
};

struct RichEdit {
  HWND hwnd = NULL;
  bool ansi = false;
  std::wstring text;
  std::vector<Attr> attr;
  int tailPf = 0;
  std::vector<CharFmt> cfs;
  std::vector<ParaFmt> pfs;
  int defCf = 0;
  int insCf = -1;  // the format for typing at an empty selection
  int anchor = 0, caret = 0;
  DWORD eventMask = 0, options = 0;
  LONG limit = cchTextLimitDefault;
  bool modified = false, focus = false, tracking = false, hideSel = false;
  wchar_t password = 0;
  bool sysBk = true;
  COLORREF bk = 0;
  RECT fmt = {0, 0, 0, 0};
  bool fmtSet = false;
  int leftMargin = 1, rightMargin = 1;
  Wrap wrap = kWrapWindow;
  LONG wrapTwips = 0;
  std::vector<Line> lines;
  int contentHeight = 0, maxWidth = 0;
  int yOffset = 0, xOffset = 0;
  int dpiX = 96, dpiY = 96;
  std::unique_ptr<Fonts> fonts;
  std::unique_ptr<Snapshot> undo;
  bool typing = false;
  IRichEditOleCallback* callback = NULL;
  ReOle* reole = NULL;
  std::vector<Site*> sites;
  std::string hostApp, hostObj;
  bool caretMade = false;
  int caretHeight = 0;
  int goalX = -1;
  CHARRANGE lastSel = {-1, -1};
  bool swallowChar = false;
  // The last EM_FORMATRANGE, for EM_DISPLAYBAND.
  FORMATRANGE lastRange = {};
  bool haveRange = false;

  DWORD Style() const { return (DWORD)GetWindowLongW(hwnd, GWL_STYLE); }
  bool Multi() const { return (Style() & ES_MULTILINE) != 0; }
  bool ReadOnly() const { return (Style() & ES_READONLY) != 0; }
  int N() const { return (int)text.size(); }
  int SelMin() const { return std::min(anchor, caret); }
  int SelMax() const { return std::max(anchor, caret); }
};

RichEdit* RichOf(HWND h) { return (RichEdit*)GetWindowLongPtrW(h, 0); }

int InternCf(RichEdit* e, const CharFmt& f) {
  for (size_t i = 0; i < e->cfs.size(); i++)
    if (e->cfs[i] == f) return (int)i;
  e->cfs.push_back(f);
  return (int)e->cfs.size() - 1;
}

int InternPf(RichEdit* e, const ParaFmt& f) {
  for (size_t i = 0; i < e->pfs.size(); i++)
    if (e->pfs[i] == f) return (int)i;
  e->pfs.push_back(f);
  return (int)e->pfs.size() - 1;
}

int Px(LONG twips, int dpi) { return MulDiv(twips, dpi, 1440); }

// ---------------------------------------------------------------------
// Paragraphs and formats at a position

int ParaStart(RichEdit* e, int cp) {
  while (cp > 0 && e->text[cp - 1] != L'\r') cp--;
  return cp;
}

int ParaEnd(RichEdit* e, int cp) {
  int n = e->N();
  while (cp < n && e->text[cp] != L'\r') cp++;
  return cp;
}

int PfAt(RichEdit* e, int cp) {
  int pe = ParaEnd(e, cp);
  return pe < e->N() ? e->attr[pe].pf : e->tailPf;
}

// The format typing at the selection would have.
int CurCf(RichEdit* e) {
  if (e->insCf >= 0) return e->insCf;
  int c = e->SelMin(), n = e->N();
  if (e->anchor != e->caret && c < n) return e->attr[c].cf;
  if (c > 0 && e->text[c - 1] != L'\r') return e->attr[c - 1].cf;
  if (c < n) return e->attr[c].cf;
  if (c > 0) return e->attr[c - 1].cf;
  return e->defCf;
}

// The paragraphs a range touches, as the positions of their marks (N for
// the last paragraph).
std::vector<int> ParaMarks(RichEdit* e, int a, int b) {
  std::vector<int> marks;
  int p = ParaEnd(e, a);
  for (;;) {
    marks.push_back(p);
    if (p >= e->N() || p >= b) break;
    p = ParaEnd(e, p + 1);
  }
  return marks;
}

// ---------------------------------------------------------------------
// CHARFORMAT and PARAFORMAT in their four and two forms

enum CfForm { kCfA, kCfW, kCf2A, kCf2W, kCfBad };

CfForm FormOf(UINT cb) {
  if (cb == sizeof(CHARFORMATA)) return kCfA;
  if (cb == sizeof(CHARFORMATW)) return kCfW;
  if (cb == sizeof(CHARFORMAT2A)) return kCf2A;
  if (cb == sizeof(CHARFORMAT2W)) return kCf2W;
  return kCfBad;
}

template <class CF>
void ReadBase(const CF* p, CharFmt* f, DWORD* mask) {
  *mask = p->dwMask;
  f->effects = p->dwEffects;
  f->height = p->yHeight;
  f->offset = p->yOffset;
  f->color = p->crTextColor;
  f->charset = p->bCharSet;
  f->pitch = p->bPitchAndFamily;
}

template <class CF2>
void Read2(const CF2* p, CharFmt* f) {
  f->weight = p->wWeight;
  f->spacing = p->sSpacing;
  f->back = p->crBackColor;
  f->lcid = p->lcid;
  f->style = p->sStyle;
  f->kerning = p->wKerning;
  f->underlineType = p->bUnderlineType;
  f->animation = p->bAnimation;
  f->revAuthor = p->bRevAuthor;
}

// The fields of a CHARFORMAT and which of them it sets.
bool ReadCharFormat(const void* p, CharFmt* f, DWORD* mask) {
  if (!p) return false;
  CfForm form = FormOf(*(const UINT*)p);
  switch (form) {
    case kCfA:
    case kCf2A: {
      const CHARFORMATA* a = (const CHARFORMATA*)p;
      ReadBase(a, f, mask);
      f->face = Widen(std::string(a->szFaceName, strnlen(a->szFaceName, LF_FACESIZE)).c_str());
      if (form == kCf2A) Read2((const CHARFORMAT2A*)p, f);
      else *mask &= CFM_ALL;
      return true;
    }
    case kCfW:
    case kCf2W: {
      const CHARFORMATW* w = (const CHARFORMATW*)p;
      ReadBase(w, f, mask);
      f->face.assign(w->szFaceName, wcsnlen(w->szFaceName, LF_FACESIZE));
      if (form == kCf2W) Read2((const CHARFORMAT2W*)p, f);
      else *mask &= CFM_ALL;
      return true;
    }
    default:
      return false;
  }
}

template <class CF>
void WriteBase(CF* p, const CharFmt& f, DWORD mask) {
  p->dwMask = mask;
  p->dwEffects = f.effects;
  p->yHeight = f.height;
  p->yOffset = f.offset;
  p->crTextColor = f.color;
  p->bCharSet = f.charset;
  p->bPitchAndFamily = f.pitch;
}

template <class CF2>
void Write2(CF2* p, const CharFmt& f) {
  p->wWeight = f.weight;
  p->sSpacing = f.spacing;
  p->crBackColor = f.back;
  p->lcid = f.lcid;
  p->sStyle = f.style;
  p->wKerning = f.kerning;
  p->bUnderlineType = f.underlineType;
  p->bAnimation = f.animation;
  p->bRevAuthor = f.revAuthor;
}

DWORD WriteCharFormat(void* p, const CharFmt& f, DWORD mask) {
  if (!p) return 0;
  CfForm form = FormOf(*(const UINT*)p);
  switch (form) {
    case kCfA:
    case kCf2A: {
      CHARFORMATA* a = (CHARFORMATA*)p;
      if (form == kCfA) mask &= CFM_ALL;
      WriteBase(a, f, mask);
      std::string face = Narrow(f.face.c_str());
      memset(a->szFaceName, 0, LF_FACESIZE);
      memcpy(a->szFaceName, face.c_str(), std::min(face.size(), (size_t)LF_FACESIZE - 1));
      if (form == kCf2A) Write2((CHARFORMAT2A*)p, f);
      return mask;
    }
    case kCfW:
    case kCf2W: {
      CHARFORMATW* w = (CHARFORMATW*)p;
      if (form == kCfW) mask &= CFM_ALL;
      WriteBase(w, f, mask);
      memset(w->szFaceName, 0, sizeof w->szFaceName);
      wcsncpy(w->szFaceName, f.face.c_str(), LF_FACESIZE - 1);
      if (form == kCf2W) Write2((CHARFORMAT2W*)p, f);
      return mask;
    }
    default:
      return 0;
  }
}

// A format with the parts mask names taken from in.
CharFmt ApplyCf(const CharFmt& base, DWORD mask, const CharFmt& in) {
  CharFmt r = base;
  DWORD bits = mask & kEffectBits;
  r.effects = (r.effects & ~bits) | (in.effects & bits);
  if (mask & CFM_WEIGHT) {
    r.weight = in.weight ? in.weight : FW_NORMAL;
    if (!(mask & CFM_BOLD)) r.effects = r.weight >= FW_SEMIBOLD ? (r.effects | CFE_BOLD) : (r.effects & ~CFE_BOLD);
  } else if (mask & CFM_BOLD) {
    r.weight = (r.effects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
  }
  if (mask & CFM_UNDERLINETYPE) {
    r.underlineType = in.underlineType;
    if (!(mask & CFM_UNDERLINE))
      r.effects = in.underlineType ? (r.effects | CFE_UNDERLINE) : (r.effects & ~CFE_UNDERLINE);
  } else if (mask & CFM_UNDERLINE) {
    r.underlineType = (r.effects & CFE_UNDERLINE) ? CFU_UNDERLINE : 0;
  }
  if (mask & CFM_SIZE) r.height = std::max(1L, (long)in.height);
  if (mask & CFM_OFFSET) r.offset = in.offset;
  if (mask & CFM_COLOR) r.color = in.color;
  if (mask & CFM_BACKCOLOR) r.back = in.back;
  if (mask & CFM_FACE) {
    r.face = in.face;
    if (!(mask & CFM_CHARSET) && in.pitch) r.pitch = in.pitch;
  }
  if (mask & CFM_CHARSET) r.charset = in.charset;
  if (mask & CFM_SPACING) r.spacing = in.spacing;
  if (mask & CFM_LCID) r.lcid = in.lcid;
  if (mask & CFM_STYLE) r.style = in.style;
  if (mask & CFM_KERNING) r.kerning = in.kerning;
  if (mask & CFM_ANIMATION) r.animation = in.animation;
  if (mask & CFM_REVAUTHOR) r.revAuthor = in.revAuthor;
  return r;
}

// The mask bits of the parts in which two formats differ.
DWORD DiffCf(const CharFmt& a, const CharFmt& b) {
  DWORD d = (a.effects ^ b.effects) & kEffectBits;
  if (d & (CFE_SUBSCRIPT | CFE_SUPERSCRIPT)) d |= CFM_SUBSCRIPT;
  if (a.height != b.height) d |= CFM_SIZE;
  if (a.offset != b.offset) d |= CFM_OFFSET;
  if (a.color != b.color) d |= CFM_COLOR;
  if (a.back != b.back) d |= CFM_BACKCOLOR;
  if (a.face != b.face) d |= CFM_FACE;
  if (a.charset != b.charset) d |= CFM_CHARSET;
  if (a.weight != b.weight) d |= CFM_WEIGHT;
  if (a.spacing != b.spacing) d |= CFM_SPACING;
  if (a.lcid != b.lcid) d |= CFM_LCID;
  if (a.style != b.style) d |= CFM_STYLE;
  if (a.kerning != b.kerning) d |= CFM_KERNING;
  if (a.underlineType != b.underlineType) d |= CFM_UNDERLINETYPE;
  if (a.animation != b.animation) d |= CFM_ANIMATION;
  if (a.revAuthor != b.revAuthor) d |= CFM_REVAUTHOR;
  return d;
}

bool ReadParaFormat(const void* p, ParaFmt* f, DWORD* mask) {
  if (!p) return false;
  UINT cb = *(const UINT*)p;
  if (cb != sizeof(PARAFORMAT) && cb != sizeof(PARAFORMAT2)) return false;
  const PARAFORMAT* a = (const PARAFORMAT*)p;
  *mask = a->dwMask;
  f->numbering = a->wNumbering;
  f->effects = a->wEffects;
  f->startIndent = a->dxStartIndent;
  f->rightIndent = a->dxRightIndent;
  f->offset = a->dxOffset;
  f->align = a->wAlignment;
  int n = std::max(0, std::min((int)a->cTabCount, MAX_TAB_STOPS));
  f->tabs.assign(a->rgxTabs, a->rgxTabs + n);
  if (cb == sizeof(PARAFORMAT2)) {
    const PARAFORMAT2* b = (const PARAFORMAT2*)p;
    f->spaceBefore = b->dySpaceBefore;
    f->spaceAfter = b->dySpaceAfter;
    f->lineSpacing = b->dyLineSpacing;
    f->style = b->sStyle;
    f->lineRule = b->bLineSpacingRule;
    f->outline = b->bOutlineLevel;
    f->shadingWeight = b->wShadingWeight;
    f->shadingStyle = b->wShadingStyle;
    f->numberingStart = b->wNumberingStart;
    f->numberingStyle = b->wNumberingStyle;
    f->numberingTab = b->wNumberingTab;
    f->borderSpace = b->wBorderSpace;
    f->borderWidth = b->wBorderWidth;
    f->borders = b->wBorders;
  } else {
    *mask &= PFM_ALL;
  }
  return true;
}

DWORD WriteParaFormat(void* p, const ParaFmt& f, DWORD mask) {
  if (!p) return 0;
  UINT cb = *(const UINT*)p;
  if (cb != sizeof(PARAFORMAT) && cb != sizeof(PARAFORMAT2)) return 0;
  if (cb == sizeof(PARAFORMAT)) mask &= PFM_ALL;
  PARAFORMAT* a = (PARAFORMAT*)p;
  a->dwMask = mask;
  a->wNumbering = f.numbering;
  a->wEffects = f.effects;
  a->dxStartIndent = f.startIndent;
  a->dxRightIndent = f.rightIndent;
  a->dxOffset = f.offset;
  a->wAlignment = f.align;
  a->cTabCount = (SHORT)f.tabs.size();
  memset(a->rgxTabs, 0, sizeof a->rgxTabs);
  for (size_t i = 0; i < f.tabs.size() && i < MAX_TAB_STOPS; i++) a->rgxTabs[i] = f.tabs[i];
  if (cb == sizeof(PARAFORMAT2)) {
    PARAFORMAT2* b = (PARAFORMAT2*)p;
    b->dySpaceBefore = f.spaceBefore;
    b->dySpaceAfter = f.spaceAfter;
    b->dyLineSpacing = f.lineSpacing;
    b->sStyle = f.style;
    b->bLineSpacingRule = f.lineRule;
    b->bOutlineLevel = f.outline;
    b->wShadingWeight = f.shadingWeight;
    b->wShadingStyle = f.shadingStyle;
    b->wNumberingStart = f.numberingStart;
    b->wNumberingStyle = f.numberingStyle;
    b->wNumberingTab = f.numberingTab;
    b->wBorderSpace = f.borderSpace;
    b->wBorderWidth = f.borderWidth;
    b->wBorders = f.borders;
  }
  return mask;
}

ParaFmt ApplyPf(const ParaFmt& base, DWORD mask, const ParaFmt& in) {
  ParaFmt r = base;
  if (mask & PFM_STARTINDENT) r.startIndent = in.startIndent;
  if (mask & PFM_OFFSETINDENT) r.startIndent += in.startIndent;
  if (mask & PFM_RIGHTINDENT) r.rightIndent = in.rightIndent;
  if (mask & PFM_OFFSET) r.offset = in.offset;
  if ((mask & PFM_ALIGNMENT) && in.align >= PFA_LEFT && in.align <= PFA_JUSTIFY) r.align = in.align;
  if (mask & PFM_TABSTOPS) r.tabs = in.tabs;
  if (mask & PFM_NUMBERING) r.numbering = in.numbering;
  WORD eff = (WORD)((mask >> 16) & 0xFFFF);
  r.effects = (WORD)((r.effects & ~eff) | (in.effects & eff));
  if (mask & PFM_SPACEBEFORE) r.spaceBefore = in.spaceBefore;
  if (mask & PFM_SPACEAFTER) r.spaceAfter = in.spaceAfter;
  if (mask & PFM_LINESPACING) {
    r.lineSpacing = in.lineSpacing;
    r.lineRule = in.lineRule;
  }
  if (mask & PFM_STYLE) r.style = in.style;
  if (mask & PFM_SHADING) {
    r.shadingWeight = in.shadingWeight;
    r.shadingStyle = in.shadingStyle;
  }
  if (mask & PFM_NUMBERINGSTART) r.numberingStart = in.numberingStart;
  if (mask & PFM_NUMBERINGSTYLE) r.numberingStyle = in.numberingStyle;
  if (mask & PFM_NUMBERINGTAB) r.numberingTab = in.numberingTab;
  if (mask & PFM_BORDER) {
    r.borderSpace = in.borderSpace;
    r.borderWidth = in.borderWidth;
    r.borders = in.borders;
  }
  return r;
}

DWORD DiffPf(const ParaFmt& a, const ParaFmt& b) {
  DWORD d = (DWORD)(a.effects ^ b.effects) << 16;
  if (a.startIndent != b.startIndent) d |= PFM_STARTINDENT;
  if (a.rightIndent != b.rightIndent) d |= PFM_RIGHTINDENT;
  if (a.offset != b.offset) d |= PFM_OFFSET;
  if (a.align != b.align) d |= PFM_ALIGNMENT;
  if (a.tabs != b.tabs) d |= PFM_TABSTOPS;
  if (a.numbering != b.numbering) d |= PFM_NUMBERING;
  if (a.spaceBefore != b.spaceBefore) d |= PFM_SPACEBEFORE;
  if (a.spaceAfter != b.spaceAfter) d |= PFM_SPACEAFTER;
  if (a.lineSpacing != b.lineSpacing || a.lineRule != b.lineRule) d |= PFM_LINESPACING;
  if (a.style != b.style) d |= PFM_STYLE;
  if (a.shadingWeight != b.shadingWeight || a.shadingStyle != b.shadingStyle) d |= PFM_SHADING;
  if (a.numberingStart != b.numberingStart) d |= PFM_NUMBERINGSTART;
  if (a.numberingStyle != b.numberingStyle) d |= PFM_NUMBERINGSTYLE;
  if (a.numberingTab != b.numberingTab) d |= PFM_NUMBERINGTAB;
  if (a.borderSpace != b.borderSpace || a.borderWidth != b.borderWidth || a.borders != b.borders) d |= PFM_BORDER;
  return d;
}

// ---------------------------------------------------------------------
// Layout

// Where measuring happens: a DC and the fonts at its resolution.
struct Ctx {
  HDC dc;
  Fonts* fonts;
  int dpiX, dpiY;
};

struct Layout {
  std::vector<Line> lines;
  int bottom = 0;
  int next = 0;
  int maxWidth = 0;
};

wchar_t Shown(RichEdit* e, wchar_t c) { return e->password && c != L'\r' ? e->password : c; }

// The advance of each character of [from, to) of one paragraph (-1 for
// a tab, whose advance depends on where it is), and the heights above
// and below the baseline.
void Measure(RichEdit* e, Ctx& c, int from, int to, std::vector<int>* adv, std::vector<int>* asc,
             std::vector<int>* desc) {
  int n = to - from;
  adv->assign(n, 0);
  asc->assign(n, 0);
  desc->assign(n, 0);
  std::vector<int> dx;
  std::wstring s;
  int k = from;
  while (k < to) {
    const Attr& a = e->attr[k];
    wchar_t ch = e->text[k];
    const CharFmt& f = e->cfs[a.cf];
    const FontInfo& fi = c.fonts->Get(f, a.cf);
    int up = f.offset > 0 ? Px(f.offset, c.dpiY) : 0, down = f.offset < 0 ? Px(-f.offset, c.dpiY) : 0;
    if (ch == kObjectChar && a.obj && !e->password) {
      (*adv)[k - from] = std::max(1, MulDiv(a.obj->sizel.cx, c.dpiX, 2540));
      (*asc)[k - from] = std::max(1, MulDiv(a.obj->sizel.cy, c.dpiY, 2540));
      (*desc)[k - from] = fi.descent;
      k++;
      continue;
    }
    if (f.effects & CFE_HIDDEN) {
      k++;
      continue;
    }
    if (ch == L'\t' && !e->password) {
      (*adv)[k - from] = -1;
      (*asc)[k - from] = fi.ascent + up;
      (*desc)[k - from] = fi.descent + down;
      k++;
      continue;
    }
    int j = k;
    s.clear();
    while (j < to && e->attr[j].cf == a.cf && e->text[j] != L'\t' && e->text[j] != kObjectChar) {
      s += Shown(e, e->text[j]);
      j++;
    }
    if (j == k) {  // an object character without an object
      s += L' ';
      j++;
    }
    dx.assign(s.size(), 0);
    SIZE sz;
    HGDIOBJ old = SelectObject(c.dc, fi.font);
    GetTextExtentExPointW(c.dc, s.c_str(), (int)s.size(), 0, NULL, dx.data(), &sz);
    SelectObject(c.dc, old);
    for (int i = 0; i < j - k; i++) {
      (*adv)[k - from + i] = dx[i] - (i ? dx[i - 1] : 0);
      (*asc)[k - from + i] = fi.ascent + up;
      (*desc)[k - from + i] = fi.descent + down;
    }
    k = j;
  }
}

int TabAdvance(const ParaFmt& pf, int x, int dpi) {
  for (LONG t : pf.tabs) {
    int tx = Px(t, dpi);
    if (tx > x) return tx - x;
  }
  int d = std::max(1, Px(lDefaultTab, dpi));
  return (x / d + 1) * d - x;
}

// Lays out the text from `from` (the start of a line) up to `to`, in
// lines of at most `width` pixels (0: no wrapping), stopping before a
// line that would end below maxBottom (when it is not negative). With
// tail, an empty last paragraph gets its line too.
void LayoutText(RichEdit* e, Ctx& c, int from, int to, int width, int alignWidth, int maxBottom, bool tail,
                Layout* out) {
  int n = e->N();
  int y = 0;
  out->next = from;
  int ps = from;
  std::vector<int> adv, asc, desc;
  bool stop = false;
  while (!stop) {
    bool emptyTail = ps == n && (n == 0 || e->text[n - 1] == L'\r');
    if (ps > n || (ps >= to && !(tail && emptyTail))) break;
    int realStart = ParaStart(e, ps);
    int pe = ParaEnd(e, ps);
    const ParaFmt& pf = e->pfs[pe < n ? e->attr[pe].pf : e->tailPf];
    int indent0 = Px(pf.startIndent, c.dpiX), indent1 = Px(pf.startIndent + pf.offset, c.dpiX);
    int right = Px(pf.rightIndent, c.dpiX);
    if (ps == realStart) y += Px(pf.spaceBefore, c.dpiY);
    Measure(e, c, ps, pe, &adv, &asc, &desc);
    int i = ps;
    for (;;) {
      if (i >= to && !(tail && emptyTail) && i > from) {
        stop = true;
        break;
      }
      int left = std::max(0, i == realStart ? indent0 : indent1);
      int avail = width > 0 ? std::max(1, width - left - right) : INT_MAX / 4;
      int x = 0, j = i, lastBreak = -1;
      std::vector<int> xs(1, 0);
      while (j < pe) {
        int a = adv[j - ps];
        if (a < 0) a = TabAdvance(pf, x + left, c.dpiX);
        if (width > 0 && x + a > avail && j > i) break;
        x += a;
        xs.push_back(x);
        if (e->text[j] == L' ' || e->text[j] == L'\t') lastBreak = j + 1;
        j++;
      }
      int cut = j;
      if (j < pe) {
        if (e->text[j] == L' ') {
          // Spaces at a break hang past the edge.
          while (cut < pe && e->text[cut] == L' ') {
            xs.push_back(x);
            cut++;
          }
        } else if (lastBreak > i) {
          cut = lastBreak;
          xs.resize(cut - i + 1);
        }
      }
      Line l;
      l.start = i;
      l.len = cut - i;
      l.end = cut == pe ? (pe < n ? pe + 1 : pe) : cut;
      l.xs = xs;
      int vis = l.len;
      while (vis > 0 && (e->text[i + vis - 1] == L' ')) vis--;
      l.width = l.xs[vis];
      int A = 0, D = 0;
      for (int k = i; k < cut; k++) {
        A = std::max(A, asc[k - ps]);
        D = std::max(D, desc[k - ps]);
      }
      if (A == 0 && D == 0) {
        // An empty line has the height of its mark's font.
        int cf = pe < n ? e->attr[pe].cf : (n > 0 ? e->attr[n - 1].cf : e->defCf);
        if (pe >= n && e->insCf >= 0 && e->SelMin() == n) cf = e->insCf;
        const FontInfo& fi = c.fonts->Get(e->cfs[cf], cf);
        A = fi.ascent;
        D = fi.descent;
      }
      int h = A + D;
      switch (pf.lineRule) {
        case 1: h = h * 3 / 2; break;
        case 2: h = h * 2; break;
        case 3: h = std::max(h, Px(pf.lineSpacing, c.dpiY)); break;
        case 4: h = std::max(1, Px(std::abs(pf.lineSpacing), c.dpiY)); break;
        case 5: h = std::max(1, h * pf.lineSpacing / 20); break;
      }
      int base = width > 0 ? avail : std::max(0, alignWidth - left - right);
      int shift = 0;
      if (base > l.width) {
        if (pf.align == PFA_RIGHT) shift = base - l.width;
        if (pf.align == PFA_CENTER) shift = (base - l.width) / 2;
      }
      l.x = left + shift;
      l.y = y;
      l.height = h;
      l.ascent = A;
      if (maxBottom >= 0 && y + h > maxBottom && !out->lines.empty()) {
        out->next = l.start;
        out->bottom = y;
        return;
      }
      y += h;
      out->maxWidth = std::max(out->maxWidth, l.x + l.width);
      out->next = l.end;
      out->lines.push_back(std::move(l));
      i = cut;
      if (cut >= pe) break;
    }
    if (stop) break;
    y += Px(pf.spaceAfter, c.dpiY);
    if (pe >= n) break;
    ps = pe + 1;
  }
  out->bottom = y;
}

// ---------------------------------------------------------------------
// The window's layout, scrolling and caret

struct ScreenDC {
  HDC dc;
  explicit ScreenDC() { dc = CreateCompatibleDC(NULL); }
  ~ScreenDC() { DeleteDC(dc); }
};

void SetFormatRect(RichEdit* e) {
  if (e->fmtSet) return;
  RECT rc;
  GetClientRect(e->hwnd, &rc);
  e->fmt = rc;
  e->fmt.left += e->leftMargin;
  e->fmt.right -= e->rightMargin;
  if (e->fmt.right < e->fmt.left) e->fmt.right = e->fmt.left;
}

int ViewWidth(RichEdit* e) { return std::max(1, (int)(e->fmt.right - e->fmt.left)); }
int ViewHeight(RichEdit* e) { return std::max(1, (int)(e->fmt.bottom - e->fmt.top)); }

int WrapWidth(RichEdit* e) {
  if (!e->Multi()) return 0;
  switch (e->wrap) {
    case kWrapNone: return 0;
    case kWrapWidth: return std::max(1, Px(e->wrapTwips, e->dpiX));
    default: return ViewWidth(e);
  }
}

void UpdateScrollBars(RichEdit* e) {
  DWORD s = e->Style();
  if (s & WS_VSCROLL) {
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = std::max(0, e->contentHeight - 1);
    si.nPage = ViewHeight(e);
    si.nPos = e->yOffset;
    SetScrollInfo(e->hwnd, SB_VERT, &si, TRUE);
  }
  if (s & WS_HSCROLL) {
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = std::max(0, e->maxWidth + 8);
    si.nPage = ViewWidth(e);
    si.nPos = e->xOffset;
    SetScrollInfo(e->hwnd, SB_HORZ, &si, TRUE);
  }
}

void Relayout(RichEdit* e) {
  SetFormatRect(e);
  ScreenDC sdc;
  Ctx c = {sdc.dc, e->fonts.get(), e->dpiX, e->dpiY};
  Layout out;
  LayoutText(e, c, 0, e->N(), WrapWidth(e), ViewWidth(e), -1, true, &out);
  e->lines = std::move(out.lines);
  if (e->lines.empty()) {
    Line l;
    l.xs.assign(1, 0);
    l.height = 16;
    l.ascent = 13;
    e->lines.push_back(l);
  }
  e->contentHeight = out.bottom;
  e->maxWidth = out.maxWidth;
  int maxY = std::max(0, e->contentHeight - ViewHeight(e));
  if (e->yOffset > maxY) e->yOffset = maxY;
  if (WrapWidth(e) > 0) e->xOffset = 0;
  UpdateScrollBars(e);
  InvalidateRect(e->hwnd, NULL, FALSE);
}

int LineOf(RichEdit* e, int cp) {
  int lo = 0, hi = (int)e->lines.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (e->lines[mid].start <= cp)
      lo = mid;
    else
      hi = mid - 1;
  }
  return lo;
}

POINT PosOf(RichEdit* e, int cp) {
  const Line& l = e->lines[LineOf(e, cp)];
  int k = std::max(0, std::min(cp - l.start, l.len));
  POINT p;
  p.x = e->fmt.left + l.x + l.xs[k] - e->xOffset;
  p.y = e->fmt.top + l.y - e->yOffset;
  return p;
}

int LineAtY(RichEdit* e, int y) {
  int rel = y - e->fmt.top + e->yOffset;
  int li = 0;
  for (size_t i = 0; i < e->lines.size(); i++) {
    if (e->lines[i].y <= rel) li = (int)i;
  }
  return li;
}

int CpAt(RichEdit* e, int x, int y, bool* onObject = NULL) {
  int li = LineAtY(e, y);
  const Line& l = e->lines[li];
  int rel = x - e->fmt.left + e->xOffset - l.x;
  int k = 0;
  while (k < l.len && rel >= (l.xs[k] + l.xs[k + 1]) / 2) k++;
  if (onObject) {
    *onObject = false;
    for (int i = 0; i < l.len; i++)
      if (rel >= l.xs[i] && rel < l.xs[i + 1] && e->text[l.start + i] == kObjectChar && e->attr[l.start + i].obj) {
        *onObject = true;
        return l.start + i;
      }
  }
  return l.start + k;
}

void PlaceCaret(RichEdit* e) {
  if (!e->focus) return;
  const Line& l = e->lines[LineOf(e, e->caret)];
  if (e->caretMade && e->caretHeight != l.height) {
    DestroyCaret();
    e->caretMade = false;
  }
  if (!e->caretMade) {
    CreateCaret(e->hwnd, NULL, 1, l.height);
    e->caretHeight = l.height;
    e->caretMade = true;
    if (e->anchor == e->caret || e->hideSel) ShowCaret(e->hwnd);
  }
  POINT p = PosOf(e, e->caret);
  SetCaretPos(p.x, p.y);
}

void NotifyScroll(RichEdit* e, UINT code) {
  if (!(e->eventMask & ENM_SCROLL)) return;
  HWND parent = GetParent(e->hwnd);
  if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(e->hwnd), code), (LPARAM)e->hwnd);
}

bool ScrollToCaret(RichEdit* e) {
  bool moved = false;
  const Line& l = e->lines[LineOf(e, e->caret)];
  int vh = ViewHeight(e);
  int oldY = e->yOffset, oldX = e->xOffset;
  if (l.y < e->yOffset) {
    e->yOffset = l.y;
  } else if (l.y + l.height > e->yOffset + vh) {
    e->yOffset = std::max(0, l.y + l.height - vh);
  }
  if (WrapWidth(e) == 0) {
    int k = std::max(0, std::min(e->caret - l.start, l.len));
    int cx = l.x + l.xs[k];
    int vw = ViewWidth(e);
    if (cx < e->xOffset)
      e->xOffset = std::max(0, cx - vw / 3);
    else if (cx >= e->xOffset + vw)
      e->xOffset = cx - vw + 1 + vw / 3;
  }
  if (e->yOffset != oldY || e->xOffset != oldX) {
    moved = true;
    UpdateScrollBars(e);
    InvalidateRect(e->hwnd, NULL, FALSE);
    if (e->yOffset != oldY) NotifyScroll(e, EN_VSCROLL);
    if (e->xOffset != oldX) NotifyScroll(e, EN_HSCROLL);
  }
  PlaceCaret(e);
  return moved;
}

void ScrollTo(RichEdit* e, int y, int x) {
  int maxY = std::max(0, e->contentHeight - ViewHeight(e));
  y = std::max(0, std::min(y, maxY));
  x = std::max(0, x);
  if (y == e->yOffset && x == e->xOffset) return;
  e->yOffset = y;
  e->xOffset = x;
  UpdateScrollBars(e);
  InvalidateRect(e->hwnd, NULL, FALSE);
  PlaceCaret(e);
}

// ---------------------------------------------------------------------
// Notifications

void NotifyCommand(RichEdit* e, UINT code, DWORD mask) {
  if (!(e->eventMask & mask)) return;
  HWND parent = GetParent(e->hwnd);
  if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(e->hwnd), code), (LPARAM)e->hwnd);
}

WORD SelType(RichEdit* e) {
  int a = e->SelMin(), b = e->SelMax();
  if (a == b) return SEL_EMPTY;
  int objs = 0, chars = 0;
  for (int i = a; i < b; i++) {
    if (e->text[i] == kObjectChar && e->attr[i].obj)
      objs++;
    else
      chars++;
  }
  WORD t = 0;
  if (chars) t |= SEL_TEXT;
  if (chars > 1) t |= SEL_MULTICHAR;
  if (objs) t |= SEL_OBJECT;
  if (objs > 1) t |= SEL_MULTIOBJECT;
  return t;
}

void NotifySel(RichEdit* e) {
  CHARRANGE cr = {e->SelMin(), e->SelMax()};
  if (cr.cpMin == e->lastSel.cpMin && cr.cpMax == e->lastSel.cpMax) return;
  e->lastSel = cr;
  if (!(e->eventMask & ENM_SELCHANGE)) return;
  HWND parent = GetParent(e->hwnd);
  if (!parent) return;
  SELCHANGE sc = {};
  sc.nmhdr.hwndFrom = e->hwnd;
  sc.nmhdr.idFrom = GetDlgCtrlID(e->hwnd);
  sc.nmhdr.code = EN_SELCHANGE;
  sc.chrg = cr;
  sc.seltyp = SelType(e);
  SendMessageW(parent, WM_NOTIFY, sc.nmhdr.idFrom, (LPARAM)&sc);
}

void NotifyChange(RichEdit* e) {
  NotifyCommand(e, EN_UPDATE, ENM_UPDATE);
  NotifyCommand(e, EN_CHANGE, ENM_CHANGE);
}

// ---------------------------------------------------------------------
// Changing the text

void TellRemoved(RichEdit* e, const std::vector<std::shared_ptr<Obj>>& removed) {
  if (!e->callback) return;
  IRichEditOleCallback* cb = e->callback;
  cb->AddRef();
  for (auto& o : removed)
    if (o->ole) cb->DeleteObject(o->ole);
  cb->Release();
}

// The objects in `before` that `after` does not have.
std::vector<std::shared_ptr<Obj>> Gone(const std::vector<Attr>& before, const std::vector<Attr>& after) {
  std::set<Obj*> kept;
  for (const Attr& a : after)
    if (a.obj) kept.insert(a.obj.get());
  std::vector<std::shared_ptr<Obj>> gone;
  for (const Attr& a : before)
    if (a.obj && !kept.count(a.obj.get())) gone.push_back(a.obj);
  return gone;
}

void SaveUndo(RichEdit* e) {
  std::unique_ptr<Snapshot> s(new Snapshot);
  s->text = e->text;
  s->attr = e->attr;
  s->tailPf = e->tailPf;
  s->anchor = e->anchor;
  s->caret = e->caret;
  e->undo = std::move(s);
}

// Plain text as the control keeps it: CR LF and LF become CR.
std::wstring Normalize(const std::wstring& s) {
  std::wstring r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    wchar_t c = s[i];
    if (c == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') {
      r += L'\r';
      i++;
    } else if (c == L'\n') {
      r += L'\r';
    } else if (c != 0) {
      r += c;
    }
  }
  return r;
}

std::vector<Attr> Attrs(size_t n, int cf, int pf) {
  Attr a;
  a.cf = cf;
  a.pf = pf;
  return std::vector<Attr>(n, a);
}

// Puts ins, with its formats, in place of [a, b).
bool Replace(RichEdit* e, int a, int b, std::wstring ins, std::vector<Attr> insAttr, bool undo, bool typed) {
  if (!e->Multi()) {
    // One line: up to the first paragraph mark.
    size_t nl = ins.find(L'\r');
    if (nl != std::wstring::npos) {
      ins.resize(nl);
      insAttr.resize(nl);
    }
  }
  int n = e->N();
  a = std::max(0, std::min(a, n));
  b = std::max(a, std::min(b, n));
  size_t after = n - (b - a);
  bool truncated = false;
  if (after + ins.size() > (size_t)e->limit) {
    size_t room = (size_t)e->limit > after ? (size_t)e->limit - after : 0;
    ins.resize(room);
    insAttr.resize(room);
    truncated = true;
  }
  if (ins.empty() && a == b) {
    if (truncated) NotifyCommand(e, EN_MAXTEXT, ENM_CHANGE);
    return false;
  }
  if (undo && !(typed && e->typing)) SaveUndo(e);
  if (!undo) e->undo.reset();
  std::vector<std::shared_ptr<Obj>> removed;
  for (int i = a; i < b; i++)
    if (e->attr[i].obj) removed.push_back(e->attr[i].obj);
  e->text.replace(a, b - a, ins);
  e->attr.erase(e->attr.begin() + a, e->attr.begin() + b);
  e->attr.insert(e->attr.begin() + a, insAttr.begin(), insAttr.end());
  e->anchor = e->caret = a + (int)ins.size();
  e->insCf = -1;
  e->modified = true;
  e->typing = typed;
  e->goalX = -1;
  Relayout(e);
  ScrollToCaret(e);
  NotifyChange(e);
  NotifySel(e);
  if (truncated) NotifyCommand(e, EN_MAXTEXT, ENM_CHANGE);
  // An object kept by the undo buffer still leaves the text.
  TellRemoved(e, removed);
  return true;
}

// Plain text in place of the selection, in the format typing would have.
bool ReplaceSel(RichEdit* e, const std::wstring& s, bool undo, bool typed) {
  std::wstring ins = Normalize(s);
  int cf = CurCf(e), pf = PfAt(e, e->SelMin());
  return Replace(e, e->SelMin(), e->SelMax(), ins, Attrs(ins.size(), cf, pf), undo, typed);
}

// All of the text anew, without undo.
void SetAll(RichEdit* e, const std::wstring& t, std::vector<Attr> attrs, int tailPf) {
  std::vector<Attr> old;
  old.swap(e->attr);
  std::unique_ptr<Snapshot> undo = std::move(e->undo);
  e->text = t;
  e->attr = std::move(attrs);
  e->tailPf = tailPf;
  e->anchor = e->caret = 0;
  e->insCf = -1;
  e->yOffset = e->xOffset = 0;
  e->typing = false;
  e->goalX = -1;
  Relayout(e);
  PlaceCaret(e);
  std::vector<Attr> none;
  std::vector<std::shared_ptr<Obj>> removed = Gone(old, e->attr);
  if (undo) {
    std::vector<std::shared_ptr<Obj>> more = Gone(undo->attr, e->attr);
    for (auto& o : more)
      if (std::find(removed.begin(), removed.end(), o) == removed.end()) removed.push_back(o);
  }
  TellRemoved(e, removed);
  NotifySel(e);
}

void SetPlainText(RichEdit* e, const std::wstring& t) {
  std::wstring s = Normalize(t);
  if (!e->Multi()) {
    size_t nl = s.find(L'\r');
    if (nl != std::wstring::npos) s.resize(nl);
  }
  if ((LONG)s.size() > e->limit) s.resize(e->limit);
  int pf = InternPf(e, ParaFmt());
  SetAll(e, s, Attrs(s.size(), e->defCf, pf), pf);
}

void Select(RichEdit* e, int anchor, int caret, bool scroll = true) {
  int n = e->N();
  anchor = std::max(0, std::min(anchor, n));
  caret = std::max(0, std::min(caret, n));
  if (anchor != e->anchor || caret != e->caret) {
    bool wasEmpty = e->anchor == e->caret;
    e->anchor = anchor;
    e->caret = caret;
    e->insCf = -1;
    e->typing = false;
    InvalidateRect(e->hwnd, NULL, FALSE);
    if (e->caretMade && e->focus && !e->hideSel) {
      if (wasEmpty && anchor != caret) HideCaret(e->hwnd);
      if (!wasEmpty && anchor == caret) ShowCaret(e->hwnd);
    }
  }
  if (scroll)
    ScrollToCaret(e);
  else
    PlaceCaret(e);
  NotifySel(e);
}

void Undo(RichEdit* e) {
  if (!e->undo || e->ReadOnly()) return;
  Snapshot cur;
  cur.text = e->text;
  cur.attr = e->attr;
  cur.tailPf = e->tailPf;
  cur.anchor = e->anchor;
  cur.caret = e->caret;
  Snapshot& s = *e->undo;
  std::vector<std::shared_ptr<Obj>> removed = Gone(cur.attr, s.attr);
  e->text = s.text;
  e->attr = s.attr;
  e->tailPf = s.tailPf;
  e->anchor = s.anchor;
  e->caret = s.caret;
  // Undo again redoes.
  *e->undo = std::move(cur);
  e->modified = true;
  e->typing = false;
  e->insCf = -1;
  Relayout(e);
  ScrollToCaret(e);
  NotifyChange(e);
  NotifySel(e);
  TellRemoved(e, removed);
}

// ---------------------------------------------------------------------
// Formats of the selection

bool SetCharFormat(RichEdit* e, WPARAM how, const void* p) {
  CharFmt in;
  DWORD mask;
  if (!ReadCharFormat(p, &in, &mask)) return false;
  if (how & SCF_ALL) {
    for (Attr& a : e->attr) a.cf = InternCf(e, ApplyCf(e->cfs[a.cf], mask, in));
    e->defCf = InternCf(e, ApplyCf(e->cfs[e->defCf], mask, in));
    e->insCf = -1;
  } else if (how & SCF_SELECTION) {
    int a = e->SelMin(), b = e->SelMax();
    if (a == b && (how & SCF_WORD)) {
      auto word = [](wchar_t c) { return iswalnum(c) || c == L'_'; };
      while (a > 0 && word(e->text[a - 1])) a--;
      while (b < e->N() && word(e->text[b])) b++;
    }
    if (a == b) {
      e->insCf = InternCf(e, ApplyCf(e->cfs[CurCf(e)], mask, in));
      Relayout(e);
      PlaceCaret(e);
      return true;
    }
    SaveUndo(e);
    for (int i = a; i < b; i++) e->attr[i].cf = InternCf(e, ApplyCf(e->cfs[e->attr[i].cf], mask, in));
    e->insCf = -1;
    e->modified = true;
  } else {
    // The default format, and the text that has it.
    int old = e->defCf;
    e->defCf = InternCf(e, ApplyCf(e->cfs[old], mask, in));
    for (Attr& a : e->attr)
      if (a.cf == old) a.cf = e->defCf;
  }
  Relayout(e);
  PlaceCaret(e);
  return true;
}

DWORD GetCharFormat(RichEdit* e, WPARAM how, void* p) {
  if (!p || FormOf(*(const UINT*)p) == kCfBad) return 0;
  if (!(how & SCF_SELECTION)) return WriteCharFormat(p, e->cfs[e->defCf], CFM_ALL2);
  int a = e->SelMin(), b = e->SelMax();
  if (a == b) return WriteCharFormat(p, e->cfs[CurCf(e)], CFM_ALL2);
  const CharFmt& first = e->cfs[e->attr[a].cf];
  DWORD diff = 0;
  int last = -1;
  for (int i = a + 1; i < b; i++) {
    int cf = e->attr[i].cf;
    if (cf == last || cf == e->attr[a].cf) continue;
    last = cf;
    diff |= DiffCf(first, e->cfs[cf]);
  }
  return WriteCharFormat(p, first, CFM_ALL2 & ~diff);
}

bool SetParaFormat(RichEdit* e, const void* p) {
  ParaFmt in;
  DWORD mask;
  if (!ReadParaFormat(p, &in, &mask)) return false;
  SaveUndo(e);
  for (int m : ParaMarks(e, e->SelMin(), e->SelMax())) {
    int& pf = m < e->N() ? e->attr[m].pf : e->tailPf;
    pf = InternPf(e, ApplyPf(e->pfs[pf], mask, in));
  }
  e->modified = true;
  Relayout(e);
  PlaceCaret(e);
  return true;
}

DWORD GetParaFormat(RichEdit* e, void* p) {
  std::vector<int> marks = ParaMarks(e, e->SelMin(), e->SelMax());
  auto pfOf = [&](int m) { return m < e->N() ? e->attr[m].pf : e->tailPf; };
  const ParaFmt& first = e->pfs[pfOf(marks[0])];
  DWORD diff = 0;
  for (size_t i = 1; i < marks.size(); i++) diff |= DiffPf(first, e->pfs[pfOf(marks[i])]);
  return WriteParaFormat(p, first, PFM_ALL2 & ~diff);
}

// ---------------------------------------------------------------------
// Objects: the control's client site and IRichEditOle

void InvalidateObjects(RichEdit* e) {
  if (e && e->hwnd) InvalidateRect(e->hwnd, NULL, FALSE);
}

class Site final : public IOleClientSite, public IAdviseSink {
 public:
  explicit Site(RichEdit* e) : e_(e) {
    if (e_) e_->sites.push_back(this);
  }
  ~Site() { Detach(); }
  void Detach() {
    if (!e_) return;
    auto& v = e_->sites;
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
    e_ = NULL;
  }
  RichEdit* Control() const { return e_; }

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleClientSite)) {
      *out = static_cast<IOleClientSite*>(this);
    } else if (IsEqualIID(riid, IID_IAdviseSink)) {
      *out = static_cast<IAdviseSink*>(this);
    } else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  REFCOUNTED(IOleClientSite)

  // IOleClientSite
  STDMETHOD(SaveObject)() override {
    Obj* o = Find();
    if (!o || !o->ole || !o->stg) return E_FAIL;
    IPersistStorage* ps = NULL;
    HRESULT hr = o->ole->QueryInterface(IID_IPersistStorage, (void**)&ps);
    if (FAILED(hr)) return hr;
    hr = OleSave(ps, o->stg, TRUE);
    ps->SaveCompleted(NULL);
    ps->Release();
    return hr;
  }
  STDMETHOD(GetMoniker)(DWORD, DWORD, IMoniker** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetContainer)(IOleContainer** out) override {
    if (out) *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD(ShowObject)() override { return S_OK; }
  STDMETHOD(OnShowWindow)(BOOL) override {
    InvalidateObjects(e_);
    return S_OK;
  }
  STDMETHOD(RequestNewObjectLayout)() override { return E_NOTIMPL; }

  // IAdviseSink
  STDMETHOD_(void, OnDataChange)(FORMATETC*, STGMEDIUM*) override {}
  STDMETHOD_(void, OnViewChange)(DWORD, LONG) override { InvalidateObjects(e_); }
  STDMETHOD_(void, OnRename)(IMoniker*) override {}
  STDMETHOD_(void, OnSave)() override {}
  STDMETHOD_(void, OnClose)() override { InvalidateObjects(e_); }

 private:
  Obj* Find() {
    if (!e_) return NULL;
    for (Attr& a : e_->attr)
      if (a.obj && a.obj->site == static_cast<IOleClientSite*>(this)) return a.obj.get();
    return NULL;
  }
  RichEdit* e_;
};

// Connects an object to the control: its site, the view advise, and the
// container's names.
void SetUpObject(RichEdit* e, Obj* o) {
  if (!o->site) o->site = new Site(e);
  o->ole->SetClientSite(o->site);
  OleSetContainedObject(o->ole, TRUE);
  if (!e->hostApp.empty()) {
    std::wstring app = Widen(e->hostApp.c_str()), obj = Widen(e->hostObj.c_str());
    o->ole->SetHostNames(app.c_str(), obj.c_str());
  }
  IViewObject* v = NULL;
  if (SUCCEEDED(o->ole->QueryInterface(IID_IViewObject, (void**)&v))) {
    IAdviseSink* sink = NULL;
    if (SUCCEEDED(o->site->QueryInterface(IID_IAdviseSink, (void**)&sink))) {
      if (SUCCEEDED(v->SetAdvise(o->aspect, 0, sink))) o->viewAdvise = 1;
      sink->Release();
    }
    v->Release();
  }
  if (o->sizel.cx <= 0 || o->sizel.cy <= 0) {
    SIZEL s = {0, 0};
    if (SUCCEEDED(o->ole->GetExtent(o->aspect, &s)) && s.cx > 0 && s.cy > 0)
      o->sizel = s;
    else
      o->sizel.cx = o->sizel.cy = 2540;
  }
}

// The objects in text order, with their positions.
std::vector<int> ObjectPositions(RichEdit* e) {
  std::vector<int> v;
  for (int i = 0; i < e->N(); i++)
    if (e->attr[i].obj) v.push_back(i);
  return v;
}

HRESULT InsertObject(RichEdit* e, REOBJECT* reo) {
  if (!reo || !reo->poleobj) return E_INVALIDARG;
  if (e->ReadOnly()) return E_FAIL;
  std::shared_ptr<Obj> o = std::make_shared<Obj>();
  Assign(o->ole, reo->poleobj);
  Assign(o->stg, reo->pstg);
  Assign(o->site, reo->polesite);
  o->clsid = reo->clsid;
  if (IsEqualCLSID(o->clsid, CLSID_NULL)) o->ole->GetUserClassID(&o->clsid);
  o->aspect = reo->dvaspect ? reo->dvaspect : DVASPECT_CONTENT;
  o->flags = reo->dwFlags;
  o->user = reo->dwUser;
  o->sizel = reo->sizel;
  int a, b;
  if (reo->cp == (LONG)REO_CP_SELECTION) {
    a = e->SelMin();
    b = e->SelMax();
  } else {
    a = b = std::max(0, std::min((int)reo->cp, e->N()));
  }
  if (e->callback && e->callback->QueryInsertObject(&o->clsid, o->stg, a) != S_OK) return E_FAIL;
  SetUpObject(e, o.get());
  Attr at;
  e->anchor = a;
  e->caret = b;
  at.cf = CurCf(e);
  at.pf = PfAt(e, a);
  at.obj = o;
  Replace(e, a, b, std::wstring(1, kObjectChar), std::vector<Attr>(1, at), true, false);
  return S_OK;
}

class ReOle final : public IRichEditOle {
 public:
  explicit ReOle(RichEdit* e) : e_(e) {}
  void Detach() { e_ = NULL; }

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IRichEditOle)) {
      *out = static_cast<IRichEditOle*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IRichEditOle)

  STDMETHOD(GetClientSite)(LPOLECLIENTSITE* out) override {
    if (!out) return E_INVALIDARG;
    if (!e_) return E_UNEXPECTED;
    *out = new Site(e_);
    return S_OK;
  }
  STDMETHOD_(LONG, GetObjectCount)() override { return e_ ? (LONG)ObjectPositions(e_).size() : 0; }
  STDMETHOD_(LONG, GetLinkCount)() override {
    if (!e_) return 0;
    LONG n = 0;
    for (int cp : ObjectPositions(e_))
      if (e_->attr[cp].obj->flags & REO_LINK) n++;
    return n;
  }
  STDMETHOD(GetObject)(LONG iob, REOBJECT* reo, DWORD flags) override {
    if (!e_) return E_UNEXPECTED;
    if (!reo || reo->cbStruct < sizeof(REOBJECT)) return E_INVALIDARG;
    int cp = Locate(iob, reo);
    if (cp < 0) return E_INVALIDARG;
    Obj* o = e_->attr[cp].obj.get();
    reo->cp = cp;
    reo->clsid = o->clsid;
    reo->poleobj = (flags & REO_GETOBJ_POLEOBJ) ? o->ole : NULL;
    reo->pstg = (flags & REO_GETOBJ_PSTG) ? o->stg : NULL;
    reo->polesite = (flags & REO_GETOBJ_POLESITE) ? o->site : NULL;
    if (reo->poleobj) reo->poleobj->AddRef();
    if (reo->pstg) reo->pstg->AddRef();
    if (reo->polesite) reo->polesite->AddRef();
    reo->sizel = o->sizel;
    reo->dvaspect = o->aspect;
    reo->dwFlags = o->flags;
    if (cp >= e_->SelMin() && cp < e_->SelMax()) reo->dwFlags |= REO_SELECTED;
    reo->dwUser = o->user;
    return S_OK;
  }
  STDMETHOD(InsertObject)(REOBJECT* reo) override {
    if (!e_) return E_UNEXPECTED;
    return ::InsertObject(e_, reo);
  }
  STDMETHOD(ConvertObject)(LONG, REFCLSID, LPCSTR) override { return E_NOTIMPL; }
  STDMETHOD(ActivateAs)(REFCLSID, REFCLSID) override { return E_NOTIMPL; }
  STDMETHOD(SetHostNames)(LPCSTR app, LPCSTR obj) override {
    if (!e_) return E_UNEXPECTED;
    e_->hostApp = app ? app : "";
    e_->hostObj = obj ? obj : "";
    std::wstring wa = Widen(e_->hostApp.c_str()), wo = Widen(e_->hostObj.c_str());
    for (int cp : ObjectPositions(e_)) e_->attr[cp].obj->ole->SetHostNames(wa.c_str(), wo.c_str());
    return S_OK;
  }
  STDMETHOD(SetLinkAvailable)(LONG iob, BOOL avail) override {
    Obj* o = At(iob);
    if (!o) return E_INVALIDARG;
    o->flags = avail ? (o->flags | REO_LINKAVAILABLE) : (o->flags & ~REO_LINKAVAILABLE);
    return S_OK;
  }
  STDMETHOD(SetDvaspect)(LONG iob, DWORD aspect) override {
    Obj* o = At(iob);
    if (!o) return E_INVALIDARG;
    o->aspect = aspect;
    Relayout(e_);
    return S_OK;
  }
  STDMETHOD(HandsOffStorage)(LONG iob) override {
    Obj* o = At(iob);
    if (!o) return E_INVALIDARG;
    SafeRelease(o->stg);
    return S_OK;
  }
  STDMETHOD(SaveCompleted)(LONG iob, LPSTORAGE stg) override {
    Obj* o = At(iob);
    if (!o) return E_INVALIDARG;
    if (stg) Assign(o->stg, stg);
    return S_OK;
  }
  STDMETHOD(InPlaceDeactivate)() override { return S_OK; }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return S_OK; }
  STDMETHOD(GetClipboardData)(CHARRANGE*, DWORD, LPDATAOBJECT* out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(ImportDataObject)(LPDATAOBJECT, CLIPFORMAT, HGLOBAL) override { return E_NOTIMPL; }

 private:
  int Locate(LONG iob, REOBJECT* reo) {
    std::vector<int> pos = ObjectPositions(e_);
    if (iob == (LONG)REO_IOB_SELECTION) {
      for (int cp : pos)
        if (cp >= e_->SelMin() && cp < e_->SelMax()) return cp;
      return -1;
    }
    if (iob == (LONG)REO_IOB_USE_CP) {
      int cp = reo ? (int)reo->cp : -1;
      return cp >= 0 && cp < e_->N() && e_->attr[cp].obj ? cp : -1;
    }
    return iob >= 0 && iob < (LONG)pos.size() ? pos[iob] : -1;
  }
  Obj* At(LONG iob) {
    if (!e_) return NULL;
    int cp = Locate(iob, NULL);
    return cp >= 0 ? e_->attr[cp].obj.get() : NULL;
  }
  RichEdit* e_;
};

// An object's storage as a compound file, for rich text.
bool SaveObjectData(Obj* o, std::string* cls, std::vector<BYTE>* native) {
  if (!o->ole) return false;
  ILockBytes* lb = NULL;
  if (FAILED(CreateILockBytesOnHGlobal(NULL, TRUE, &lb))) return false;
  IStorage* stg = NULL;
  HRESULT hr = StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
  IPersistStorage* ps = NULL;
  if (SUCCEEDED(hr)) hr = o->ole->QueryInterface(IID_IPersistStorage, (void**)&ps);
  if (SUCCEEDED(hr)) {
    if (o->stg) {
      hr = OleSave(ps, o->stg, TRUE);
      ps->SaveCompleted(NULL);
      o->stg->Commit(STGC_DEFAULT);
      if (SUCCEEDED(hr)) hr = o->stg->CopyTo(0, NULL, NULL, stg);
    } else {
      hr = OleSave(ps, stg, FALSE);
      ps->SaveCompleted(NULL);
    }
    ps->Release();
  }
  if (stg) {
    if (SUCCEEDED(hr)) hr = stg->Commit(STGC_DEFAULT);
    stg->Release();
  }
  if (SUCCEEDED(hr)) {
    HGLOBAL g = NULL;
    STATSTG st = {};
    hr = GetHGlobalFromILockBytes(lb, &g);
    if (SUCCEEDED(hr)) hr = lb->Stat(&st, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) {
      size_t n = (size_t)st.cbSize.QuadPart;
      const BYTE* p = (const BYTE*)GlobalLock(g);
      if (p) native->assign(p, p + std::min(n, (size_t)GlobalSize(g)));
      GlobalUnlock(g);
    }
  }
  lb->Release();
  if (FAILED(hr) || native->empty()) return false;
  LPOLESTR prog = NULL;
  if (SUCCEEDED(ProgIDFromCLSID(o->clsid, &prog)) && prog) {
    *cls = Narrow(prog);
    CoTaskMemFree(prog);
  } else if (SUCCEEDED(StringFromCLSID(o->clsid, &prog)) && prog) {
    *cls = Narrow(prog);
    CoTaskMemFree(prog);
  }
  return true;
}

std::shared_ptr<Obj> LoadObjectData(RichEdit* e, const std::string& cls, const std::vector<BYTE>& native, LONG cx,
                                    LONG cy) {
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, native.size());
  if (!g) return nullptr;
  memcpy(GlobalLock(g), native.data(), native.size());
  GlobalUnlock(g);
  ILockBytes* lb = NULL;
  if (FAILED(CreateILockBytesOnHGlobal(g, TRUE, &lb))) {
    GlobalFree(g);
    return nullptr;
  }
  IStorage* src = NULL;
  HRESULT hr = StgOpenStorageOnILockBytes(lb, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &src);
  lb->Release();
  if (FAILED(hr)) return nullptr;
  IStorage* dst = NULL;
  if (e->callback) {
    hr = e->callback->GetNewStorage(&dst);
  } else {
    ILockBytes* lb2 = NULL;
    hr = CreateILockBytesOnHGlobal(NULL, TRUE, &lb2);
    if (SUCCEEDED(hr)) {
      hr = StgCreateDocfileOnILockBytes(lb2, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &dst);
      lb2->Release();
    }
  }
  if (SUCCEEDED(hr) && dst) hr = src->CopyTo(0, NULL, NULL, dst);
  src->Release();
  if (FAILED(hr) || !dst) {
    SafeRelease(dst);
    return nullptr;
  }
  std::shared_ptr<Obj> o = std::make_shared<Obj>();
  o->stg = dst;
  if (FAILED(ReadClassStg(dst, &o->clsid)) || IsEqualCLSID(o->clsid, CLSID_NULL)) {
    std::wstring w = Widen(cls.c_str());
    if (FAILED(CLSIDFromProgID(w.c_str(), &o->clsid))) CLSIDFromString(w.c_str(), &o->clsid);
  }
  if (e->callback && e->callback->QueryInsertObject(&o->clsid, dst, (LONG)REO_CP_SELECTION) != S_OK)
    return nullptr;
  o->site = new Site(e);
  hr = OleLoad(dst, IID_IOleObject, o->site, (void**)&o->ole);
  if (FAILED(hr) || !o->ole) return nullptr;
  o->sizel.cx = MulDiv(cx, 2540, 1440);
  o->sizel.cy = MulDiv(cy, 2540, 1440);
  SetUpObject(e, o.get());
  return o;
}

// ---------------------------------------------------------------------
// Rich text and plain text in and out

std::string RtfOf(RichEdit* e, int a, int b, bool whole, bool objects) {
  RtfSpan span;
  span.chars.reserve(b - a);
  for (int i = a; i < b; i++) {
    const Attr& at = e->attr[i];
    span.chars.push_back(RichChar{e->text[i], &e->cfs[at.cf], &e->pfs[at.pf], at.obj.get()});
  }
  if (whole) span.tail = &e->pfs[e->tailPf];
  return WriteRtf(span, objects, [](Obj* o, std::string* cls, std::vector<BYTE>* native) {
    return SaveObjectData(o, cls, native);
  });
}

// Reads rich text into characters with their formats. whole: the text
// is all of the control's, whose last paragraph has no mark.
bool ReadRtfInto(RichEdit* e, const std::string& bytes, bool whole, bool objects, std::wstring* text,
                 std::vector<Attr>* attrs, int* tailPf) {
  RtfText rt;
  ObjectLoader loader;
  if (objects)
    loader = [e](const std::string& cls, const std::vector<BYTE>& native, LONG cx, LONG cy) {
      return LoadObjectData(e, cls, native, cx, cy);
    };
  if (!ReadRtf(bytes, e->cfs[whole ? e->defCf : CurCf(e)], loader, &rt)) return false;
  *tailPf = InternPf(e, rt.last);
  if (whole && !rt.text.empty() && rt.text.back() == L'\r') {
    *tailPf = InternPf(e, rt.pf.back());
    rt.text.pop_back();
    rt.cf.pop_back();
    rt.pf.pop_back();
    rt.obj.pop_back();
  }
  *text = rt.text;
  attrs->resize(rt.text.size());
  int lastCf = -1;
  const CharFmt* lastFmt = NULL;
  for (size_t i = 0; i < rt.text.size(); i++) {
    if (!lastFmt || *lastFmt != rt.cf[i]) {
      lastCf = InternCf(e, rt.cf[i]);
      lastFmt = &rt.cf[i];
    }
    (*attrs)[i].cf = lastCf;
    (*attrs)[i].pf = rt.text[i] == L'\r' ? InternPf(e, rt.pf[i]) : 0;
    (*attrs)[i].obj = rt.obj[i];
  }
  return true;
}

std::vector<char16_t> ToUtf16(const std::wstring& s) {
  std::vector<char16_t> v;
  for (wchar_t wc : s) {
    unsigned long c = (unsigned long)wc;
    if (c > 0xFFFF) {
      c -= 0x10000;
      v.push_back((char16_t)(0xD800 + (c >> 10)));
      v.push_back((char16_t)(0xDC00 + (c & 0x3FF)));
    } else {
      v.push_back((char16_t)c);
    }
  }
  return v;
}

std::wstring FromUtf16(const char16_t* p, size_t n) {
  std::wstring s;
  for (size_t i = 0; i < n; i++) {
    unsigned long c = p[i];
    if (sizeof(wchar_t) == 4 && c >= 0xD800 && c < 0xDC00 && i + 1 < n && p[i + 1] >= 0xDC00 && p[i + 1] < 0xE000) {
      c = 0x10000 + ((c - 0xD800) << 10) + (p[i + 1] - 0xDC00);
      i++;
    }
    s += (wchar_t)c;
  }
  return s;
}

enum ObjectsAs { kOmit, kSpace, kKeep };

// The text of [a, b) as plain text, with CR LF between paragraphs or
// not, and objects left out, as spaces or as their character.
std::wstring PlainOf(RichEdit* e, int a, int b, bool crlf, ObjectsAs objects) {
  std::wstring s;
  for (int i = a; i < b; i++) {
    wchar_t c = e->text[i];
    if (c == L'\r') {
      s += crlf ? L"\r\n" : L"\r";
    } else if (c == kObjectChar && e->attr[i].obj) {
      if (objects == kSpace) s += L' ';
      if (objects == kKeep) s += c;
    } else {
      s += c;
    }
  }
  return s;
}

LRESULT StreamOut(RichEdit* e, DWORD format, EDITSTREAM* es) {
  if (!es || !es->pfnCallback) return 0;
  bool sel = (format & SFF_SELECTION) != 0;
  int a = sel ? e->SelMin() : 0, b = sel ? e->SelMax() : e->N();
  DWORD kind = format & 0x0F;
  std::string bytes;
  LRESULT count;
  if (kind == SF_RTF || kind == SF_RTFNOOBJS) {
    bytes = RtfOf(e, a, b, !sel, kind == SF_RTF);
    count = (LRESULT)bytes.size();
  } else {
    std::wstring s = PlainOf(e, a, b, true, kind == SF_TEXTIZED ? kSpace : kOmit);
    count = (LRESULT)s.size();
    UINT cp = (format & SF_USECODEPAGE) ? HIWORD(format) : 0;
    if ((format & SF_UNICODE) || cp == 1200) {
      std::vector<char16_t> u = ToUtf16(s);
      bytes.assign((const char*)u.data(), u.size() * 2);
    } else {
      bytes = Narrow(s.c_str());
    }
  }
  size_t done = 0;
  es->dwError = 0;
  while (done < bytes.size()) {
    LONG n = (LONG)std::min(bytes.size() - done, (size_t)4096);
    LONG wrote = 0;
    es->dwError = es->pfnCallback(es->dwCookie, (LPBYTE)bytes.data() + done, n, &wrote);
    if (es->dwError != 0 || wrote <= 0) break;
    done += wrote;
  }
  return count;
}

LRESULT StreamIn(RichEdit* e, DWORD format, EDITSTREAM* es) {
  if (!es || !es->pfnCallback) return 0;
  std::string bytes;
  std::vector<BYTE> buf(4096);
  es->dwError = 0;
  for (;;) {
    LONG got = 0;
    es->dwError = es->pfnCallback(es->dwCookie, buf.data(), (LONG)buf.size(), &got);
    if (es->dwError != 0 || got <= 0) break;
    bytes.append((const char*)buf.data(), got);
  }
  bool sel = (format & SFF_SELECTION) != 0;
  DWORD kind = format & 0x0F;
  std::wstring text;
  std::vector<Attr> attrs;
  int tail = InternPf(e, ParaFmt());
  bool rtf = (kind == SF_RTF || kind == SF_RTFNOOBJS) && IsRtf(bytes.data(), bytes.size());
  if (!(rtf && ReadRtfInto(e, bytes, !sel, kind == SF_RTF, &text, &attrs, &tail))) {
    UINT cp = (format & SF_USECODEPAGE) ? HIWORD(format) : 0;
    if ((format & SF_UNICODE) || cp == 1200)
      text = FromUtf16((const char16_t*)bytes.data(), bytes.size() / 2);
    else
      text = Widen(bytes.c_str(), (int)bytes.size());
    text = Normalize(text);
    int cf = sel ? CurCf(e) : e->defCf;
    int pf = sel ? PfAt(e, e->SelMin()) : tail;
    attrs = Attrs(text.size(), cf, pf);
  }
  if (sel) {
    Replace(e, e->SelMin(), e->SelMax(), text, attrs, true, false);
  } else {
    if (!e->Multi()) {
      size_t nl = text.find(L'\r');
      if (nl != std::wstring::npos) {
        text.resize(nl);
        attrs.resize(nl);
      }
    }
    SetAll(e, text, attrs, tail);
    e->modified = false;
  }
  return (LRESULT)text.size();
}

// ---------------------------------------------------------------------
// The clipboard

UINT RtfFormat() {
  static UINT cf = RegisterClipboardFormatW(L"Rich Text Format");
  return cf;
}

HGLOBAL GlobalOf(const void* p, size_t n) {
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
  if (g) {
    memcpy(GlobalLock(g), p, n);
    GlobalUnlock(g);
  }
  return g;
}

void Copy(RichEdit* e) {
  int a = e->SelMin(), b = e->SelMax();
  if (a == b || e->password) return;
  std::wstring s = PlainOf(e, a, b, true, kOmit);
  std::string rtf = RtfOf(e, a, b, false, true);
  if (!OpenClipboard(e->hwnd)) return;
  EmptyClipboard();
  SetClipboardData(CF_UNICODETEXT, GlobalOf(s.c_str(), (s.size() + 1) * sizeof(wchar_t)));
  SetClipboardData(RtfFormat(), GlobalOf(rtf.c_str(), rtf.size() + 1));
  CloseClipboard();
}

bool CanPaste(UINT cf) {
  if (cf == 0)
    return IsClipboardFormatAvailable(RtfFormat()) || IsClipboardFormatAvailable(CF_UNICODETEXT) ||
           IsClipboardFormatAvailable(CF_TEXT);
  if (cf == RtfFormat() || cf == CF_UNICODETEXT || cf == CF_TEXT || cf == CF_OEMTEXT)
    return IsClipboardFormatAvailable(cf) != FALSE;
  return false;
}

void Paste(RichEdit* e, UINT cf) {
  if (e->ReadOnly()) {
    MessageBeep(0);
    return;
  }
  if (!OpenClipboard(e->hwnd)) return;
  std::wstring text;
  std::vector<Attr> attrs;
  bool got = false;
  if ((cf == 0 || cf == RtfFormat()) && IsClipboardFormatAvailable(RtfFormat())) {
    HANDLE g = GetClipboardData(RtfFormat());
    const char* p = g ? (const char*)GlobalLock(g) : NULL;
    if (p) {
      std::string bytes(p, strnlen(p, GlobalSize(g)));
      GlobalUnlock(g);
      int tail;
      got = ReadRtfInto(e, bytes, false, true, &text, &attrs, &tail);
    }
  }
  if (!got && (cf == 0 || cf == CF_UNICODETEXT)) {
    HANDLE g = GetClipboardData(CF_UNICODETEXT);
    const wchar_t* p = g ? (const wchar_t*)GlobalLock(g) : NULL;
    if (p) {
      text = p;
      GlobalUnlock(g);
      got = true;
    }
  }
  if (!got && (cf == 0 || cf == CF_TEXT || cf == CF_OEMTEXT || cf == CF_UNICODETEXT)) {
    HANDLE g = GetClipboardData(CF_TEXT);
    const char* p = g ? (const char*)GlobalLock(g) : NULL;
    if (p) {
      text = Widen(p);
      GlobalUnlock(g);
      got = true;
    }
  }
  CloseClipboard();
  if (!got) return;
  if (attrs.empty()) {
    ReplaceSel(e, text, true, false);
  } else {
    Replace(e, e->SelMin(), e->SelMax(), text, attrs, true, false);
  }
}

void Clear(RichEdit* e) {
  if (e->ReadOnly() || e->SelMin() == e->SelMax()) return;
  ReplaceSel(e, L"", true, false);
}

// ---------------------------------------------------------------------
// Finding

bool IsWordChar(wchar_t c) { return iswalnum(c) || c == L'_'; }

LONG Find(RichEdit* e, WPARAM flags, CHARRANGE range, const std::wstring& what, CHARRANGE* found) {
  int n = e->N(), len = (int)what.size();
  if (len == 0 || len > n) return -1;
  bool matchCase = (flags & FR_MATCHCASE) != 0, whole = (flags & FR_WHOLEWORD) != 0;
  auto match = [&](int p) {
    for (int k = 0; k < len; k++) {
      wchar_t a = e->text[p + k], b = what[k];
      if (matchCase ? a != b : towlower(a) != towlower(b)) return false;
    }
    if (whole) {
      if (p > 0 && IsWordChar(e->text[p - 1])) return false;
      if (p + len < n && IsWordChar(e->text[p + len])) return false;
    }
    return true;
  };
  int hit = -1;
  if (flags & FR_DOWN) {
    int from = std::max(0, std::min((int)range.cpMin, n));
    int to = (range.cpMax < 0 || range.cpMax > n) ? n : (int)range.cpMax;
    for (int p = from; p + len <= to; p++)
      if (match(p)) {
        hit = p;
        break;
      }
  } else {
    int from = (range.cpMin < 0 || range.cpMin > n) ? n : (int)range.cpMin;
    int lowest = range.cpMax < 0 ? 0 : std::min((int)range.cpMax, n);
    for (int p = from - len; p >= lowest; p--)
      if (match(p)) {
        hit = p;
        break;
      }
  }
  if (found) {
    found->cpMin = hit;
    found->cpMax = hit < 0 ? -1 : hit + len;
  }
  return hit;
}

// ---------------------------------------------------------------------
// Painting and printing

int SelectedCp(RichEdit* e, int cp, bool showSel) { return showSel && cp >= e->SelMin() && cp < e->SelMax(); }

// Draws lines laid out with c at (ox, oy) in dc.
void DrawLines(RichEdit* e, HDC dc, Ctx& c, const std::vector<Line>& lines, int ox, int oy, int top, int bottom,
               bool showSel) {
  SetBkMode(dc, TRANSPARENT);
  COLORREF hiText = GetSysColor(COLOR_HIGHLIGHTTEXT);
  HBRUSH hiBrush = GetSysColorBrush(COLOR_HIGHLIGHT);
  bool enabled = IsWindowEnabled(e->hwnd) != FALSE;
  std::wstring s;
  for (const Line& l : lines) {
    int ly = oy + l.y;
    if (ly >= bottom || ly + l.height <= top) continue;
    int base = ly + l.ascent;
    int end = l.start + l.len;
    int k = l.start;
    while (k < end) {
      const Attr& a = e->attr[k];
      bool sel = SelectedCp(e, k, showSel);
      int x0 = ox + l.x + l.xs[k - l.start];
      wchar_t ch = e->text[k];
      if (ch == kObjectChar && a.obj && !e->password) {
        int x1 = ox + l.x + l.xs[k - l.start + 1];
        int h = MulDiv(a.obj->sizel.cy, c.dpiY, 2540);
        RECT rc = {x0, base - h, x1, base};
        OleDraw(a.obj->ole, a.obj->aspect, dc, &rc);
        if (sel) {
          if (e->SelMax() - e->SelMin() == 1) {
            // The object alone: a frame with handles.
            FrameRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            int hs = 5;
            const POINT corners[4] = {{rc.left, rc.top}, {rc.right - hs, rc.top}, {rc.left, rc.bottom - hs},
                                      {rc.right - hs, rc.bottom - hs}};
            for (const POINT& p : corners) {
              RECT r = {p.x, p.y, p.x + hs, p.y + hs};
              FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
            }
          } else {
            InvertRect(dc, &rc);
          }
        }
        k++;
        continue;
      }
      const CharFmt& f = e->cfs[a.cf];
      if (ch == L'\t' && !e->password) {
        if (sel) {
          RECT r = {x0, ly, ox + l.x + l.xs[k - l.start + 1], ly + l.height};
          FillRect(dc, &r, hiBrush);
        }
        k++;
        continue;
      }
      int j = k + 1;
      while (j < end && e->attr[j].cf == a.cf && SelectedCp(e, j, showSel) == sel && e->text[j] != L'\t' &&
             e->text[j] != kObjectChar)
        j++;
      int x1 = ox + l.x + l.xs[j - l.start];
      if (f.effects & CFE_HIDDEN) {
        k = j;
        continue;
      }
      s.clear();
      for (int i = k; i < j; i++) s += Shown(e, e->text[i]);
      if (f.effects & CFE_ALLCAPS)
        for (wchar_t& w : s) w = towupper(w);
      const FontInfo& fi = c.fonts->Get(f, a.cf);
      HGDIOBJ old = SelectObject(dc, fi.font);
      RECT r = {x0, ly, x1, ly + l.height};
      if (sel) {
        FillRect(dc, &r, hiBrush);
        SetTextColor(dc, hiText);
      } else {
        if (!(f.effects & CFE_AUTOBACKCOLOR)) {
          HBRUSH b = CreateSolidBrush(f.back);
          FillRect(dc, &r, b);
          DeleteObject(b);
        }
        COLORREF fg = (f.effects & CFE_AUTOCOLOR) ? GetSysColor(COLOR_WINDOWTEXT) : f.color;
        if (!enabled || (f.effects & CFE_DISABLED)) fg = GetSysColor(COLOR_GRAYTEXT);
        SetTextColor(dc, fg);
      }
      int up = Px(f.offset, c.dpiY);
      ExtTextOutW(dc, x0, base - fi.ascent - up, 0, NULL, s.c_str(), (UINT)s.size(), NULL);
      SelectObject(dc, old);
      k = j;
    }
    // A selected paragraph mark shows as a little block.
    if (showSel && l.end > end && end < e->N() && SelectedCp(e, end, true)) {
      int x0 = ox + l.x + l.xs[l.len];
      const FontInfo& fi = c.fonts->Get(e->cfs[e->attr[end].cf], e->attr[end].cf);
      RECT r = {x0, ly, x0 + std::max(2, fi.avg / 2), ly + l.height};
      FillRect(dc, &r, hiBrush);
    }
  }
}

void Paint(RichEdit* e, HDC dc) {
  RECT rc;
  GetClientRect(e->hwnd, &rc);
  COLORREF bk = e->sysBk ? GetSysColor(COLOR_WINDOW) : e->bk;
  if (!IsWindowEnabled(e->hwnd) && e->sysBk) bk = GetSysColor(COLOR_3DFACE);
  HBRUSH br = CreateSolidBrush(bk);
  FillRect(dc, &rc, br);
  DeleteObject(br);
  int saved = SaveDC(dc);
  IntersectClipRect(dc, e->fmt.left, rc.top, e->fmt.right, rc.bottom);
  bool showSel = (e->focus || !e->hideSel || (e->Style() & ES_NOHIDESEL)) && !e->hideSel &&
                 e->SelMin() != e->SelMax();
  if (!e->focus && !(e->Style() & ES_NOHIDESEL) && !(e->options & ECO_NOHIDESEL)) showSel = false;
  ScreenDC sdc;
  Ctx c = {sdc.dc, e->fonts.get(), e->dpiX, e->dpiY};
  DrawLines(e, dc, c, e->lines, e->fmt.left - e->xOffset, e->fmt.top - e->yOffset, rc.top, rc.bottom, showSel);
  RestoreDC(dc, saved);
}

LRESULT FormatRange(RichEdit* e, bool display, FORMATRANGE* fr) {
  if (!fr) {
    e->haveRange = false;
    return 0;
  }
  HDC target = fr->hdcTarget ? fr->hdcTarget : fr->hdc;
  if (!target) return 0;
  int dpiX = GetDeviceCaps(target, LOGPIXELSX), dpiY = GetDeviceCaps(target, LOGPIXELSY);
  if (dpiX <= 0) dpiX = 96;
  if (dpiY <= 0) dpiY = 96;
  int n = e->N();
  int from = std::max(0, std::min((int)fr->chrg.cpMin, n));
  int to = (fr->chrg.cpMax < 0 || fr->chrg.cpMax > n) ? n : (int)fr->chrg.cpMax;
  if (from >= to) {
    fr->rc.bottom = fr->rc.top;
    return to;
  }
  // A range that starts inside a paragraph lays out from the line there.
  Fonts fonts(dpiY);
  Ctx c = {target, &fonts, dpiX, dpiY};
  int width = std::max(1, MulDiv(fr->rc.right - fr->rc.left, dpiX, 1440));
  int height = std::max(1, MulDiv(fr->rc.bottom - fr->rc.top, dpiY, 1440));
  if (e->wrap == kWrapNone) width = 0;
  Layout out;
  LayoutText(e, c, from, to, width, std::max(1, MulDiv(fr->rc.right - fr->rc.left, dpiX, 1440)), height, false,
             &out);
  if (display && fr->hdc) {
    int ox = MulDiv(fr->rc.left, dpiX, 1440), oy = MulDiv(fr->rc.top, dpiY, 1440);
    int saved = SaveDC(fr->hdc);
    DrawLines(e, fr->hdc, c, out.lines, ox, oy, INT_MIN / 2, INT_MAX / 2, false);
    RestoreDC(fr->hdc, saved);
  }
  fr->rc.bottom = fr->rc.top + MulDiv(out.bottom, 1440, dpiY);
  e->lastRange = *fr;
  e->haveRange = true;
  return out.next;
}

// ---------------------------------------------------------------------
// Words and keys

int WordLeft(RichEdit* e, int i) {
  while (i > 0 && !IsWordChar(e->text[i - 1])) i--;
  while (i > 0 && IsWordChar(e->text[i - 1])) i--;
  return i;
}

int WordRight(RichEdit* e, int i) {
  int n = e->N();
  while (i < n && IsWordChar(e->text[i])) i++;
  while (i < n && !IsWordChar(e->text[i]) && e->text[i] != L'\r') i++;
  return i;
}

int LineEndCp(RichEdit* e, int li) { return e->lines[li].start + e->lines[li].len; }

bool Filter(RichEdit* e, UINT msg, WPARAM wp, LPARAM lp, DWORD mask) {
  if (!(e->eventMask & mask)) return false;
  HWND parent = GetParent(e->hwnd);
  if (!parent) return false;
  MSGFILTER mf = {};
  mf.nmhdr.hwndFrom = e->hwnd;
  mf.nmhdr.idFrom = GetDlgCtrlID(e->hwnd);
  mf.nmhdr.code = EN_MSGFILTER;
  mf.msg = msg;
  mf.wParam = wp;
  mf.lParam = lp;
  return SendMessageW(parent, WM_NOTIFY, mf.nmhdr.idFrom, (LPARAM)&mf) != 0;
}

void Key(RichEdit* e, UINT vk) {
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  int c = e->caret, n = e->N();
  int li = LineOf(e, c);
  int to = -1;
  bool keepGoal = false;
  switch (vk) {
    case VK_LEFT:
      if (!shift && e->anchor != e->caret)
        to = e->SelMin();
      else
        to = ctrl ? WordLeft(e, c) : std::max(0, c - 1);
      break;
    case VK_RIGHT:
      if (!shift && e->anchor != e->caret)
        to = e->SelMax();
      else
        to = ctrl ? WordRight(e, c) : std::min(n, c + 1);
      break;
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT: {
      if (!e->Multi()) return;
      POINT p = PosOf(e, c);
      if (e->goalX < 0) e->goalX = p.x;
      int y;
      if (vk == VK_UP) {
        if (li == 0) {
          to = 0;
          break;
        }
        y = e->fmt.top + e->lines[li - 1].y - e->yOffset;
      } else if (vk == VK_DOWN) {
        if (li + 1 >= (int)e->lines.size()) {
          to = n;
          break;
        }
        y = e->fmt.top + e->lines[li + 1].y - e->yOffset;
      } else {
        int page = std::max(1, ViewHeight(e) - 16);
        int d = vk == VK_PRIOR ? -page : page;
        ScrollTo(e, e->yOffset + d, e->xOffset);
        y = p.y;
      }
      int ti = LineAtY(e, y);
      to = CpAt(e, e->goalX, y);
      if (to > LineEndCp(e, ti)) to = LineEndCp(e, ti);
      keepGoal = true;
      break;
    }
    case VK_HOME:
      to = ctrl ? 0 : e->lines[li].start;
      break;
    case VK_END:
      to = ctrl ? n : LineEndCp(e, li);
      break;
    case VK_DELETE:
      if (e->ReadOnly()) {
        MessageBeep(0);
        return;
      }
      if (shift) {
        Copy(e);
        Clear(e);
        return;
      }
      if (e->anchor == e->caret) {
        if (c >= n) return;
        e->anchor = c;
        e->caret = ctrl ? WordRight(e, c) : c + 1;
      }
      ReplaceSel(e, L"", true, false);
      return;
    case VK_INSERT:
      if (shift)
        Paste(e, 0);
      else if (ctrl)
        Copy(e);
      return;
    case 'Y':
      if (ctrl) {
        Undo(e);
        e->swallowChar = true;
      }
      return;
    default:
      return;
  }
  if (to < 0) return;
  int goal = e->goalX;
  Select(e, shift ? e->anchor : to, to);
  if (keepGoal) e->goalX = goal;
}

void Char(RichEdit* e, wchar_t ch) {
  if (e->swallowChar) {
    e->swallowChar = false;
    return;
  }
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  switch (ch) {
    case 1:  // Ctrl+A
      Select(e, 0, e->N());
      return;
    case 3:
      Copy(e);
      return;
    case 22:
      Paste(e, 0);
      return;
    case 24:
      if (e->ReadOnly()) return;
      Copy(e);
      Clear(e);
      return;
    case 26:
      Undo(e);
      return;
    case 8: {
      if (e->ReadOnly()) {
        MessageBeep(0);
        return;
      }
      if (e->anchor == e->caret) {
        int c = e->caret;
        if (c == 0) return;
        e->anchor = ctrl ? WordLeft(e, c) : c - 1;
      }
      ReplaceSel(e, L"", true, true);
      return;
    }
    case 127:
      return;
    case L'\r':
    case L'\n':
      if (!e->Multi()) return;
      if (e->ReadOnly()) {
        MessageBeep(0);
        return;
      }
      ReplaceSel(e, L"\r", true, true);
      return;
    case L'\t':
      if (e->ReadOnly() || !e->Multi()) return;
      ReplaceSel(e, L"\t", true, true);
      return;
  }
  if (ch < L' ') return;
  if (e->ReadOnly()) {
    MessageBeep(0);
    return;
  }
  ReplaceSel(e, std::wstring(1, ch), true, true);
}

void DefaultFormat(RichEdit* e, HFONT font) {
  LOGFONTW lf;
  if (!font || !GetObjectW(font, sizeof lf, &lf)) return;
  CharFmt in;
  in.face = lf.lfFaceName;
  in.charset = lf.lfCharSet;
  in.pitch = lf.lfPitchAndFamily;
  in.height = lf.lfHeight < 0 ? MulDiv(-lf.lfHeight, 1440, e->dpiY) : MulDiv(lf.lfHeight, 1440 * 4, e->dpiY * 5);
  if (in.height <= 0) in.height = 200;
  in.weight = (WORD)(lf.lfWeight ? lf.lfWeight : FW_NORMAL);
  in.effects = (lf.lfItalic ? CFE_ITALIC : 0) | (lf.lfUnderline ? CFE_UNDERLINE : 0) |
               (lf.lfStrikeOut ? CFE_STRIKEOUT : 0) | (in.weight >= FW_SEMIBOLD ? CFE_BOLD : 0);
  DWORD mask = CFM_FACE | CFM_CHARSET | CFM_SIZE | CFM_WEIGHT | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
  int old = e->defCf;
  e->defCf = InternCf(e, ApplyCf(e->cfs[old], mask, in));
  for (Attr& a : e->attr) a.cf = InternCf(e, ApplyCf(e->cfs[a.cf], mask, in));
  e->insCf = -1;
}

// Copies text out as the window's character set: returns the characters
// (bytes for ANSI) copied, without the terminating zero.
LRESULT CopyOut(RichEdit* e, const std::wstring& s, void* buf, size_t room = (size_t)-1) {
  if (!buf) return 0;
  if (e->ansi) {
    std::string a = Narrow(s.c_str());
    if (room != (size_t)-1 && a.size() + 1 > room) a.resize(room ? room - 1 : 0);
    memcpy(buf, a.c_str(), a.size() + 1);
    return (LRESULT)a.size();
  }
  std::wstring w = s;
  if (room != (size_t)-1 && w.size() + 1 > room) w.resize(room ? room - 1 : 0);
  memcpy(buf, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  return (LRESULT)w.size();
}

void Destroy(RichEdit* e) {
  for (Site* s : std::vector<Site*>(e->sites)) s->Detach();
  if (e->reole) {
    e->reole->Detach();
    e->reole->Release();
    e->reole = NULL;
  }
  SafeRelease(e->callback);
  e->undo.reset();
  e->attr.clear();
  e->text.clear();
  e->fonts.reset();
}

LRESULT CALLBACK RichProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  RichEdit* e = RichOf(h);
  if (!e && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE: {
      e = new RichEdit;
      e->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)e);
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      wchar_t cls[64] = {0};
      GetClassNameW(h, cls, 64);
      e->ansi = _wcsicmp(cls, L"RichEdit20A") == 0 || _wcsicmp(cls, L"RICHEDIT") == 0;
      ScreenDC sdc;
      e->dpiX = GetDeviceCaps(sdc.dc, LOGPIXELSX);
      e->dpiY = GetDeviceCaps(sdc.dc, LOGPIXELSY);
      if (e->dpiX <= 0) e->dpiX = 96;
      if (e->dpiY <= 0) e->dpiY = 96;
      e->fonts.reset(new Fonts(e->dpiY));
      e->cfs.push_back(CharFmt());
      e->pfs.push_back(ParaFmt());
      if (cs->style & ES_PASSWORD) e->password = L'*';
      if (cs->style & ES_NOHIDESEL) e->options |= ECO_NOHIDESEL;
      if (cs->style & ES_READONLY) e->options |= ECO_READONLY;
      if (cs->style & ES_AUTOVSCROLL) e->options |= ECO_AUTOVSCROLL;
      if (cs->style & ES_AUTOHSCROLL) e->options |= ECO_AUTOHSCROLL;
      if (cs->style & ES_WANTRETURN) e->options |= ECO_WANTRETURN;
      if (cs->style & ES_SAVESEL) e->options |= ECO_SAVESEL;
      // A one-line control and one that scrolls sideways do not wrap.
      if (!(cs->style & ES_MULTILINE) || ((cs->style & ES_AUTOHSCROLL) && !(cs->style & WS_HSCROLL)))
        e->wrap = kWrapNone;
      e->reole = new ReOle(e);
      DefWindowProcW(h, msg, wp, lp);
      std::wstring t = cs->lpszName && !IS_INTRESOURCE(cs->lpszName) ? cs->lpszName : L"";
      t = Normalize(t);
      e->text = t;
      e->attr = Attrs(t.size(), 0, 0);
      return TRUE;
    }
    case WM_CREATE:
      Relayout(e);
      return 0;
    case WM_DESTROY:
      Destroy(e);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_NCDESTROY:
      Destroy(e);
      SetWindowLongPtrW(h, 0, 0);
      delete e;
      return DefWindowProcW(h, msg, wp, lp);
    case WM_SIZE:
      if (!e->fmtSet) Relayout(e);
      ScrollToCaret(e);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      if (e->focus && e->caretMade) HideCaret(h);
      Paint(e, dc);
      if (e->focus && e->caretMade) ShowCaret(h);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETDLGCODE: {
      LRESULT r = DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL;
      MSG* m = (MSG*)lp;
      if (e->Multi()) {
        r |= DLGC_WANTALLKEYS;
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000))
          r &= ~DLGC_WANTALLKEYS;
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_ESCAPE) r &= ~DLGC_WANTALLKEYS;
      }
      return r;
    }
    case WM_SETFONT:
      DefaultFormat(e, (HFONT)wp);
      Relayout(e);
      PlaceCaret(e);
      if (!lp) ValidateRect(h, NULL);
      return 0;
    case WM_GETFONT:
      return 0;
    case WM_SETTEXT: {
      std::wstring t = lp ? (LPCWSTR)lp : L"";
      if (t.compare(0, 5, L"{\\rtf") == 0) {
        std::string bytes = Narrow(t.c_str());
        std::wstring text;
        std::vector<Attr> attrs;
        int tail;
        if (ReadRtfInto(e, bytes, true, true, &text, &attrs, &tail)) {
          SetAll(e, text, attrs, tail);
          e->modified = false;
          NotifyChange(e);
          return TRUE;
        }
      }
      SetPlainText(e, t);
      e->modified = false;
      NotifyChange(e);
      return TRUE;
    }
    case WM_GETTEXT: {
      if (!wp || !lp) return 0;
      std::wstring s = PlainOf(e, 0, e->N(), false, kSpace);
      size_t n = std::min(s.size(), (size_t)wp - 1);
      memcpy((void*)lp, s.c_str(), n * sizeof(wchar_t));
      ((wchar_t*)lp)[n] = 0;
      return (LRESULT)n;
    }
    case WM_GETTEXTLENGTH:
      return e->N();
    case WM_ENABLE:
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case WM_SETFOCUS:
      e->focus = true;
      PlaceCaret(e);
      InvalidateRect(h, NULL, FALSE);
      NotifyCommand(e, EN_SETFOCUS, 0xFFFFFFFF);
      return 0;
    case WM_KILLFOCUS:
      e->focus = false;
      if (e->caretMade) {
        DestroyCaret();
        e->caretMade = false;
      }
      e->tracking = false;
      InvalidateRect(h, NULL, FALSE);
      NotifyCommand(e, EN_KILLFOCUS, 0xFFFFFFFF);
      return 0;
    case WM_LBUTTONDOWN: {
      if (Filter(e, msg, wp, lp, ENM_MOUSEEVENTS)) return 0;
      if (GetFocus() != h) SetFocus(h);
      bool onObject;
      int i = CpAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp), &onObject);
      bool shift = (wp & MK_SHIFT) != 0;
      if (onObject && !shift) {
        Select(e, i, i + 1);
        return 0;
      }
      Select(e, shift ? e->anchor : i, i);
      SetCapture(h);
      e->tracking = true;
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      if (Filter(e, msg, wp, lp, ENM_MOUSEEVENTS)) return 0;
      bool onObject;
      int i = CpAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp), &onObject);
      if (onObject) {
        Obj* o = e->attr[i].obj.get();
        RECT rc;
        GetClientRect(h, &rc);
        Select(e, i, i + 1);
        o->ole->DoVerb(OLEIVERB_PRIMARY, NULL, o->site, 0, h, &rc);
        return 0;
      }
      int a = i, b = i;
      while (a > 0 && IsWordChar(e->text[a - 1])) a--;
      while (b < e->N() && IsWordChar(e->text[b])) b++;
      while (b < e->N() && e->text[b] == L' ') b++;
      Select(e, a, b);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (e->tracking) {
        int i = CpAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp));
        Select(e, e->anchor, i);
      }
      return 0;
    case WM_LBUTTONUP:
      if (e->tracking) {
        e->tracking = false;
        if (GetCapture() == h) ReleaseCapture();
      }
      Filter(e, msg, wp, lp, ENM_MOUSEEVENTS);
      return 0;
    case WM_CAPTURECHANGED:
      e->tracking = false;
      return 0;
    case WM_RBUTTONUP: {
      if (Filter(e, msg, wp, lp, ENM_MOUSEEVENTS)) return 0;
      if (!e->callback) break;
      CHARRANGE cr = {e->SelMin(), e->SelMax()};
      IOleObject* obj = NULL;
      for (int cp : ObjectPositions(e))
        if (cp >= cr.cpMin && cp < cr.cpMax) {
          obj = e->attr[cp].obj->ole;
          break;
        }
      HMENU menu = NULL;
      if (e->callback->GetContextMenu(SelType(e), obj, &cr, &menu) == S_OK && menu) {
        POINT p = {(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)};
        ClientToScreen(h, &p);
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, p.x, p.y, 0, GetParent(h), NULL);
        DestroyMenu(menu);
      }
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (e->Multi()) {
        int step = e->lines.empty() ? 16 : e->lines[0].height;
        ScrollTo(e, e->yOffset - 3 * step * (SHORT)HIWORD(wp) / WHEEL_DELTA, e->xOffset);
        NotifyScroll(e, EN_VSCROLL);
      }
      return 0;
    case WM_KEYDOWN:
      if (Filter(e, msg, wp, lp, ENM_KEYEVENTS)) return 0;
      Key(e, (UINT)wp);
      return 0;
    case WM_CHAR:
      if (Filter(e, msg, wp, lp, ENM_KEYEVENTS)) return 0;
      Char(e, (wchar_t)wp);
      return 0;
    case WM_VSCROLL:
    case WM_HSCROLL: {
      bool v = msg == WM_VSCROLL;
      int page = v ? ViewHeight(e) : ViewWidth(e);
      int line = 16;
      int pos = v ? e->yOffset : e->xOffset;
      switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= page; break;
        case SB_PAGEDOWN: pos += page; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = v ? e->contentHeight : e->maxWidth; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = (int)(short)HIWORD(wp); break;
        default: return 0;
      }
      if (v)
        ScrollTo(e, pos, e->xOffset);
      else
        ScrollTo(e, e->yOffset, pos);
      NotifyScroll(e, v ? EN_VSCROLL : EN_HSCROLL);
      return 0;
    }
    case WM_CUT:
      if (!e->ReadOnly()) {
        Copy(e);
        Clear(e);
      }
      return 0;
    case WM_COPY:
      Copy(e);
      return 0;
    case WM_PASTE:
      Paste(e, 0);
      return 0;
    case WM_CLEAR:
      Clear(e);
      return 0;
    case WM_UNDO:
    case EM_UNDO:
    case EM_REDO: {
      bool could = e->undo != nullptr;
      Undo(e);
      return could;
    }
    case EM_CANUNDO:
    case EM_CANREDO:
      return e->undo != nullptr && !e->ReadOnly();
    case EM_EMPTYUNDOBUFFER: {
      std::unique_ptr<Snapshot> undo = std::move(e->undo);
      if (undo) TellRemoved(e, Gone(undo->attr, e->attr));
      e->typing = false;
      return 0;
    }
    case EM_STOPGROUPTYPING:
      e->typing = false;
      return 0;
    case EM_SETUNDOLIMIT:
      if (wp == 0) e->undo.reset();
      return wp ? 1 : 0;

    // Selection
    case EM_GETSEL: {
      int a = e->SelMin(), b = e->SelMax();
      if (wp) *(DWORD*)wp = a;
      if (lp) *(DWORD*)lp = b;
      return (a > 0xFFFF || b > 0xFFFF) ? -1 : MAKELRESULT(a, b);
    }
    case EM_EXGETSEL:
      if (lp) {
        ((CHARRANGE*)lp)->cpMin = e->SelMin();
        ((CHARRANGE*)lp)->cpMax = e->SelMax();
      }
      return 0;
    case EM_SETSEL:
    case EM_EXSETSEL: {
      LONG a, b;
      if (msg == EM_SETSEL) {
        a = (LONG)wp;
        b = (LONG)lp;
      } else {
        if (!lp) return e->SelMax();
        a = ((CHARRANGE*)lp)->cpMin;
        b = ((CHARRANGE*)lp)->cpMax;
      }
      int n = e->N();
      if (a < 0) {
        // No selection: the caret at its end.
        Select(e, e->SelMax(), e->SelMax(), false);
      } else {
        if (b < 0 || b > n) b = n;
        if (a > n) a = n;
        Select(e, a, b, false);
      }
      if (!(e->hideSel)) ScrollToCaret(e);
      return e->SelMax();
    }
    case EM_REPLACESEL:
      ReplaceSel(e, lp ? (LPCWSTR)lp : L"", wp != 0, false);
      return 0;
    case EM_GETSELTEXT: {
      std::wstring s = e->text.substr(e->SelMin(), e->SelMax() - e->SelMin());
      return CopyOut(e, s, (void*)lp);
    }
    case EM_GETTEXTRANGE: {
      if (!lp) return 0;
      TEXTRANGEW* tr = (TEXTRANGEW*)lp;
      int n = e->N();
      int a = std::max(0, std::min((int)tr->chrg.cpMin, n));
      int b = (tr->chrg.cpMax < 0 || tr->chrg.cpMax > n) ? n : (int)tr->chrg.cpMax;
      if (b < a) b = a;
      return CopyOut(e, e->text.substr(a, b - a), tr->lpstrText);
    }
    case EM_SELECTIONTYPE:
      return SelType(e);
    case EM_HIDESELECTION:
      e->hideSel = wp != 0;
      if (lp) {
        e->options = wp ? (e->options & ~ECO_NOHIDESEL) : (e->options | ECO_NOHIDESEL);
      }
      InvalidateRect(h, NULL, FALSE);
      return 0;

    // Text
    case EM_GETTEXTLENGTHEX: {
      const GETTEXTLENGTHEX* g = (const GETTEXTLENGTHEX*)wp;
      DWORD flags = g ? g->flags : 0;
      std::wstring s = PlainOf(e, 0, e->N(), (flags & GTL_USECRLF) != 0, kKeep);
      if (flags & GTL_NUMBYTES) {
        UINT cp = g ? g->codepage : 0;
        if (cp == 1200) return (LRESULT)s.size() * sizeof(WCHAR);
        return (LRESULT)Narrow(s.c_str()).size();
      }
      return (LRESULT)s.size();
    }
    case EM_GETTEXTEX: {
      const GETTEXTEX* g = (const GETTEXTEX*)wp;
      if (!g || !lp || g->cb == 0) return 0;
      bool selOnly = (g->flags & GT_SELECTION) != 0;
      int a = selOnly ? e->SelMin() : 0, b = selOnly ? e->SelMax() : e->N();
      std::wstring s = PlainOf(e, a, b, (g->flags & GT_USECRLF) != 0, kKeep);
      if (g->codepage == 1200) {
        size_t room = g->cb / sizeof(wchar_t);
        if (room == 0) return 0;
        size_t n = std::min(s.size(), room - 1);
        memcpy((void*)lp, s.c_str(), n * sizeof(wchar_t));
        ((wchar_t*)lp)[n] = 0;
        return (LRESULT)n;
      }
      std::string t = Narrow(s.c_str());
      size_t n = std::min(t.size(), (size_t)g->cb - 1);
      memcpy((void*)lp, t.c_str(), n);
      ((char*)lp)[n] = 0;
      return (LRESULT)n;
    }
    case EM_SETTEXTEX: {
      const SETTEXTEX* st = (const SETTEXTEX*)wp;
      bool selOnly = st && (st->flags & ST_SELECTION);
      UINT cp = st ? st->codepage : 0;
      std::wstring t;
      std::string bytes;
      if (lp) {
        if (cp == 1200) {
          t = (LPCWSTR)lp;
          bytes = Narrow(t.c_str());
        } else {
          bytes = (const char*)lp;
          t = Widen(bytes.c_str());
        }
      }
      std::wstring text;
      std::vector<Attr> attrs;
      int tail;
      if (IsRtf(bytes.data(), bytes.size()) && ReadRtfInto(e, bytes, !selOnly, true, &text, &attrs, &tail)) {
        if (selOnly)
          Replace(e, e->SelMin(), e->SelMax(), text, attrs, (st->flags & ST_KEEPUNDO) != 0, false);
        else
          SetAll(e, text, attrs, tail);
      } else if (selOnly) {
        ReplaceSel(e, t, (st->flags & ST_KEEPUNDO) != 0, false);
      } else {
        SetPlainText(e, t);
      }
      if (!selOnly) NotifyChange(e);
      return 1;
    }
    case EM_STREAMIN:
      return StreamIn(e, (DWORD)wp, (EDITSTREAM*)lp);
    case EM_STREAMOUT:
      return StreamOut(e, (DWORD)wp, (EDITSTREAM*)lp);
    case EM_GETMODIFY:
      return e->modified;
    case EM_SETMODIFY:
      e->modified = wp != 0;
      return 0;
    case EM_LIMITTEXT:
    case EM_EXLIMITTEXT: {
      LONG v = msg == EM_LIMITTEXT ? (LONG)wp : (LONG)lp;
      e->limit = v > 0 ? v : (msg == EM_LIMITTEXT ? 0x7FFFFFFE : cchTextLimitDefault);
      return 0;
    }
    case EM_GETLIMITTEXT:
      return e->limit;
    case EM_SETREADONLY: {
      LONG style = GetWindowLongW(h, GWL_STYLE);
      SetWindowLongW(h, GWL_STYLE, wp ? (style | ES_READONLY) : (style & ~ES_READONLY));
      e->options = wp ? (e->options | ECO_READONLY) : (e->options & ~ECO_READONLY);
      InvalidateRect(h, NULL, FALSE);
      return TRUE;
    }
    case EM_SETPASSWORDCHAR:
      e->password = (wchar_t)wp;
      Relayout(e);
      return 0;
    case EM_GETPASSWORDCHAR:
      return e->password;
    case EM_SETTEXTMODE:
      return e->N() ? 1 : 0;
    case EM_GETTEXTMODE:
      return TM_RICHTEXT | TM_SINGLELEVELUNDO | TM_MULTICODEPAGE;

    // Lines and positions
    case EM_GETLINECOUNT:
      return (LRESULT)e->lines.size();
    case EM_LINEINDEX: {
      int li = (int)wp;
      if (li == -1) li = LineOf(e, e->caret);
      if (li < 0 || li >= (int)e->lines.size()) return -1;
      return e->lines[li].start;
    }
    case EM_LINELENGTH: {
      int i = (int)wp;
      if (i == -1) {
        int la = LineOf(e, e->SelMin()), lb = LineOf(e, e->SelMax());
        return (e->SelMin() - e->lines[la].start) + (LineEndCp(e, lb) - e->SelMax());
      }
      if (i < 0 || i > e->N()) return 0;
      return e->lines[LineOf(e, i)].len;
    }
    case EM_LINEFROMCHAR:
    case EM_EXLINEFROMCHAR: {
      int i = msg == EM_LINEFROMCHAR ? (int)wp : (int)lp;
      if (i < 0) i = e->SelMin();
      if (i > e->N()) i = e->N();
      return LineOf(e, i);
    }
    case EM_GETLINE: {
      int li = (int)wp;
      if (li < 0 || li >= (int)e->lines.size() || !lp) return 0;
      WORD max = *(WORD*)lp;
      const Line& l = e->lines[li];
      std::wstring s = e->text.substr(l.start, l.end - l.start);
      if (e->ansi) {
        std::string a = Narrow(s.c_str());
        size_t n = std::min(a.size(), (size_t)max);
        memcpy((void*)lp, a.c_str(), n);
        if (n < max) ((char*)lp)[n] = 0;
        return (LRESULT)n;
      }
      size_t n = std::min(s.size(), (size_t)max);
      memcpy((void*)lp, s.c_str(), n * sizeof(wchar_t));
      if (n < max) ((wchar_t*)lp)[n] = 0;
      return (LRESULT)n;
    }
    case EM_GETFIRSTVISIBLELINE:
      return LineAtY(e, e->fmt.top);
    case EM_LINESCROLL: {
      if (!e->Multi()) return FALSE;
      int first = LineAtY(e, e->fmt.top);
      int target = std::max(0, std::min(first + (int)lp, (int)e->lines.size() - 1));
      ScrollTo(e, e->lines[target].y, e->xOffset + (int)wp * 8);
      return TRUE;
    }
    case EM_SCROLL: {
      if (!e->Multi()) return 0;
      int before = LineAtY(e, e->fmt.top);
      int step = 16, page = ViewHeight(e);
      switch (wp) {
        case SB_LINEUP: ScrollTo(e, e->yOffset - step, e->xOffset); break;
        case SB_LINEDOWN: ScrollTo(e, e->yOffset + step, e->xOffset); break;
        case SB_PAGEUP: ScrollTo(e, e->yOffset - page, e->xOffset); break;
        case SB_PAGEDOWN: ScrollTo(e, e->yOffset + page, e->xOffset); break;
        default: return 0;
      }
      return MAKELRESULT(LineAtY(e, e->fmt.top) - before, TRUE);
    }
    case EM_SCROLLCARET:
      ScrollToCaret(e);
      return 0;
    case EM_GETSCROLLPOS:
      if (lp) {
        ((POINT*)lp)->x = e->xOffset;
        ((POINT*)lp)->y = e->yOffset;
      }
      return 1;
    case EM_SETSCROLLPOS:
      if (lp) ScrollTo(e, ((POINT*)lp)->y, ((POINT*)lp)->x);
      return 1;
    case EM_POSFROMCHAR: {
      // Rich edit 2.0: a POINTL to fill and the position; 1.0 style: the
      // position, with the point as the result.
      if (wp > 0xFFFF) {
        int cp = std::max(0, std::min((int)lp, e->N()));
        POINT p = PosOf(e, cp);
        ((POINTL*)wp)->x = p.x;
        ((POINTL*)wp)->y = p.y;
        return 0;
      }
      int cp = (int)wp;
      if (cp < 0 || cp > e->N()) return -1;
      POINT p = PosOf(e, cp);
      return MAKELRESULT((WORD)(SHORT)p.x, (WORD)(SHORT)p.y);
    }
    case EM_CHARFROMPOS: {
      if (!lp) return 0;
      const POINTL* p = (const POINTL*)lp;
      return CpAt(e, p->x, p->y);
    }
    case EM_FINDWORDBREAK: {
      int cp = std::max(0, std::min((int)lp, e->N()));
      switch (wp) {
        case WB_MOVEWORDLEFT:
        case 0:  // WB_LEFT
        case WB_LEFTBREAK: return WordLeft(e, cp);
        case WB_MOVEWORDRIGHT:
        case 1:  // WB_RIGHT
        case WB_RIGHTBREAK: return WordRight(e, cp);
        case 2:  // WB_ISDELIMITER return cp < e->N() && !IsWordChar(e->text[cp]);
        case WB_CLASSIFY: return cp < e->N() && !IsWordChar(e->text[cp]) ? WBF_WORDBREAK : 0;
      }
      return cp;
    }

    // Formats
    case EM_SETCHARFORMAT:
      return SetCharFormat(e, wp, (const void*)lp);
    case EM_GETCHARFORMAT:
      return GetCharFormat(e, wp, (void*)lp);
    case EM_SETPARAFORMAT:
      return SetParaFormat(e, (const void*)lp);
    case EM_GETPARAFORMAT:
      return GetParaFormat(e, (void*)lp);
    case EM_SETFONTSIZE: {
      CHARFORMAT2W cf = {};
      cf.cbSize = sizeof cf;
      int a = e->SelMin(), b = e->SelMax();
      if (a == b) return FALSE;
      SaveUndo(e);
      for (int i = a; i < b; i++) {
        CharFmt f = e->cfs[e->attr[i].cf];
        f.height = std::max(20L, std::min(1638L * 20, (long)f.height + (long)(int)wp * 20));
        e->attr[i].cf = InternCf(e, f);
      }
      e->modified = true;
      Relayout(e);
      return TRUE;
    }
    case EM_SETBKGNDCOLOR: {
      COLORREF old = e->sysBk ? GetSysColor(COLOR_WINDOW) : e->bk;
      e->sysBk = wp != 0;
      if (!wp) e->bk = (COLORREF)lp;
      InvalidateRect(h, NULL, FALSE);
      return old;
    }

    // Layout and options
    case EM_GETRECT:
      if (lp) *(RECT*)lp = e->fmt;
      return 0;
    case EM_SETRECT:
    case EM_SETRECTNP:
      if (lp) {
        e->fmt = *(const RECT*)lp;
        e->fmtSet = true;
      } else {
        e->fmtSet = false;
      }
      Relayout(e);
      if (msg == EM_SETRECTNP) ValidateRect(h, NULL);
      return 0;
    case EM_SETMARGINS: {
      int l = (SHORT)LOWORD(lp), r = (SHORT)HIWORD(lp);
      if (wp & EC_LEFTMARGIN) e->leftMargin = l == (SHORT)EC_USEFONTINFO ? 3 : l;
      if (wp & EC_RIGHTMARGIN) e->rightMargin = r == (SHORT)EC_USEFONTINFO ? 3 : r;
      Relayout(e);
      return 0;
    }
    case EM_GETMARGINS:
      return MAKELRESULT(e->leftMargin, e->rightMargin);
    case EM_SETTABSTOPS: {
      // In dialog units, for the paragraphs' default tab stops.
      ParaFmt in;
      if (wp && lp) {
        LONG base = LOWORD(GetDialogBaseUnits());
        const INT* t = (const INT*)lp;
        for (WPARAM i = 0; i < wp && i < MAX_TAB_STOPS; i++) in.tabs.push_back(MulDiv(t[i] * base / 4, 1440, e->dpiX));
      }
      for (Attr& a : e->attr) a.pf = InternPf(e, ApplyPf(e->pfs[a.pf], PFM_TABSTOPS, in));
      e->tailPf = InternPf(e, ApplyPf(e->pfs[e->tailPf], PFM_TABSTOPS, in));
      Relayout(e);
      return TRUE;
    }
    case EM_SETTARGETDEVICE:
      if (!wp && lp == 1) {
        e->wrap = kWrapNone;
      } else if (!wp && lp == 0) {
        e->wrap = kWrapWindow;
      } else {
        e->wrap = kWrapWidth;
        e->wrapTwips = (LONG)lp;
      }
      Relayout(e);
      ScrollToCaret(e);
      return TRUE;
    case EM_SETWORDWRAPMODE:
    case EM_GETWORDWRAPMODE:
      return 0;
    case EM_REQUESTRESIZE: {
      if (!(e->eventMask & ENM_REQUESTRESIZE)) return 0;
      HWND parent = GetParent(h);
      if (!parent) return 0;
      REQRESIZE rr = {};
      rr.nmhdr.hwndFrom = h;
      rr.nmhdr.idFrom = GetDlgCtrlID(h);
      rr.nmhdr.code = EN_REQUESTRESIZE;
      GetClientRect(h, &rr.rc);
      rr.rc.bottom = rr.rc.top + e->contentHeight + (e->fmt.top - rr.rc.top) * 2;
      SendMessageW(parent, WM_NOTIFY, rr.nmhdr.idFrom, (LPARAM)&rr);
      return 0;
    }
    case EM_SETEVENTMASK: {
      DWORD old = e->eventMask;
      e->eventMask = (DWORD)lp;
      return old;
    }
    case EM_GETEVENTMASK:
      return e->eventMask;
    case EM_SETOPTIONS: {
      DWORD v = (DWORD)lp;
      switch (wp) {
        case ECOOP_SET: e->options = v; break;
        case ECOOP_OR: e->options |= v; break;
        case ECOOP_AND: e->options &= v; break;
        case ECOOP_XOR: e->options ^= v; break;
      }
      LONG style = GetWindowLongW(h, GWL_STYLE);
      style = (e->options & ECO_READONLY) ? (style | ES_READONLY) : (style & ~ES_READONLY);
      SetWindowLongW(h, GWL_STYLE, style);
      InvalidateRect(h, NULL, FALSE);
      return e->options;
    }
    case EM_GETOPTIONS:
      return e->options;
    case EM_AUTOURLDETECT:
      return 0;
    case EM_GETAUTOURLDETECT:
      return FALSE;
    case EM_SETZOOM:
      return wp == 0 && lp == 0;
    case EM_GETZOOM:
      if (wp) *(int*)wp = 0;
      if (lp) *(int*)lp = 0;
      return TRUE;
    case EM_SHOWSCROLLBAR:
      ShowScrollBar(h, (int)wp, lp != 0);
      return 0;
    case EM_GETEDITSTYLE:
    case EM_SETEDITSTYLE:
    case EM_GETTYPOGRAPHYOPTIONS:
    case EM_SETTYPOGRAPHYOPTIONS:
    case EM_GETLANGOPTIONS:
    case EM_SETLANGOPTIONS:
    case EM_GETIMECOMPMODE:
    case EM_SETPALETTE:
      return 0;
    case EM_FMTLINES:
      return wp;
    case EM_GETHANDLE:
    case EM_SETHANDLE:
      return 0;

    // Finding
    case EM_FINDTEXT:
    case EM_FINDTEXTEX:
    case EM_FINDTEXTW:
    case EM_FINDTEXTEXW: {
      if (!lp) return -1;
      bool wide = msg == EM_FINDTEXTW || msg == EM_FINDTEXTEXW;
      bool ex = msg == EM_FINDTEXTEX || msg == EM_FINDTEXTEXW;
      CHARRANGE range = ((const FINDTEXTW*)lp)->chrg;
      std::wstring what;
      if (wide) {
        LPCWSTR s = ((const FINDTEXTW*)lp)->lpstrText;
        what = s ? s : L"";
      } else {
        LPCSTR s = ((const FINDTEXTA*)lp)->lpstrText;
        what = s ? Widen(s) : L"";
      }
      what = Normalize(what);
      CHARRANGE found;
      LONG r = Find(e, wp, range, what, &found);
      if (ex) {
        if (wide)
          ((FINDTEXTEXW*)lp)->chrgText = found;
        else
          ((FINDTEXTEXA*)lp)->chrgText = found;
      }
      return r;
    }

    // The clipboard
    case EM_CANPASTE:
      return !e->ReadOnly() && CanPaste((UINT)wp);
    case EM_PASTESPECIAL:
      Paste(e, (UINT)wp);
      return 0;

    // Printing
    case EM_FORMATRANGE:
      return FormatRange(e, wp != 0, (FORMATRANGE*)lp);
    case EM_DISPLAYBAND: {
      if (!e->haveRange || !lp) return FALSE;
      FORMATRANGE fr = e->lastRange;
      FormatRange(e, true, &fr);
      return TRUE;
    }

    // Objects
    case EM_SETOLECALLBACK:
      Assign(e->callback, (IRichEditOleCallback*)lp);
      return TRUE;
    case EM_GETOLEINTERFACE:
      if (!lp) return FALSE;
      *(IRichEditOle**)lp = e->reole;
      if (e->reole) e->reole->AddRef();
      return e->reole != NULL;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterRichEditClasses() {
  static bool done = false;
  if (done) return;
  done = true;
  LPCWSTR ibeam = MAKEINTRESOURCEW(32513);
  RegisterSystemClass(L"RichEdit20W", RichProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL, ibeam);
  RegisterSystemClass(L"RichEdit20A", RichProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL, ibeam);
  RegisterSystemClass(L"RICHEDIT", RichProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL, ibeam);
  RegisterSystemClass(L"RICHEDIT50W", RichProc, CS_DBLCLKS | CS_GLOBALCLASS, sizeof(void*), NULL, ibeam);
}

}  // namespace winport
