// rich_rtf.cpp - rich text (RTF) for the rich edit control: writing a span
// of formatted text, with its fonts, colours, paragraph formats and
// embedded objects, and reading it back.
//
// An object goes in an \object group whose \objdata is an OLE 1.0
// embedded object stream (MS-OLEDS 2.2): the class name, then the
// object's storage as a compound file for native data. That is the form
// rich text readers know for OLE 2 objects.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "rich.h"

#include <stdio.h>
#include <string.h>

#include <map>

namespace winport {
namespace rich {

namespace {

// ---------------------------------------------------------------------
// Writing

struct Tables {
  std::vector<std::wstring> fonts;
  std::vector<BYTE> charsets;
  std::vector<COLORREF> colors;  // index 0 is "auto"

  int Font(const CharFmt& f) {
    for (size_t i = 0; i < fonts.size(); i++)
      if (fonts[i] == f.face) return (int)i;
    fonts.push_back(f.face);
    charsets.push_back(f.charset == DEFAULT_CHARSET ? ANSI_CHARSET : f.charset);
    return (int)fonts.size() - 1;
  }
  int Color(COLORREF c) {
    for (size_t i = 1; i < colors.size(); i++)
      if (colors[i] == c) return (int)i;
    colors.push_back(c);
    return (int)colors.size() - 1;
  }
};

void Append(std::string* out, const char* fmt, long v) {
  char buf[48];
  snprintf(buf, sizeof buf, fmt, v);
  *out += buf;
}

// Text, escaped; non-ASCII as \u with a '?' for readers that skip it.
void Text(std::string* out, const std::wstring& s) {
  for (wchar_t wc : s) {
    unsigned long c = (unsigned long)wc;
    if (c == '\\' || c == '{' || c == '}') {
      *out += '\\';
      *out += (char)c;
    } else if (c == '\t') {
      *out += "\\tab ";
    } else if (c < 0x20) {
      // other controls are left out
    } else if (c < 0x80) {
      *out += (char)c;
    } else if (c > 0xFFFF) {
      c -= 0x10000;
      Append(out, "\\u%ld?", (long)(short)(0xD800 + (c >> 10)));
      Append(out, "\\u%ld?", (long)(short)(0xDC00 + (c & 0x3FF)));
    } else {
      Append(out, "\\u%ld?", (long)(short)c);
    }
  }
}

void CharProps(std::string* out, Tables& t, const CharFmt& f) {
  *out += "\\plain";
  Append(out, "\\f%ld", t.Font(f));
  Append(out, "\\fs%ld", (f.height + 5) / 10);
  if (f.effects & CFE_BOLD) *out += "\\b";
  if (f.effects & CFE_ITALIC) *out += "\\i";
  if (f.effects & CFE_UNDERLINE) {
    switch (f.underlineType) {
      case CFU_UNDERLINEWORD: *out += "\\ulw"; break;
      case CFU_UNDERLINEDOUBLE: *out += "\\uldb"; break;
      case CFU_UNDERLINEDOTTED: *out += "\\uld"; break;
      default: *out += "\\ul"; break;
    }
  }
  if (f.effects & CFE_STRIKEOUT) *out += "\\strike";
  if (f.effects & CFE_SMALLCAPS) *out += "\\scaps";
  if (f.effects & CFE_ALLCAPS) *out += "\\caps";
  if (f.effects & CFE_HIDDEN) *out += "\\v";
  if (f.effects & CFE_PROTECTED) *out += "\\protect";
  if (f.effects & CFE_SUBSCRIPT) *out += "\\sub";
  if (f.effects & CFE_SUPERSCRIPT) *out += "\\super";
  if (f.offset > 0) Append(out, "\\up%ld", f.offset / 10);
  if (f.offset < 0) Append(out, "\\dn%ld", -f.offset / 10);
  if (!(f.effects & CFE_AUTOCOLOR)) Append(out, "\\cf%ld", t.Color(f.color));
  if (!(f.effects & CFE_AUTOBACKCOLOR)) Append(out, "\\highlight%ld", t.Color(f.back));
  *out += ' ';
}

void ParaProps(std::string* out, const ParaFmt& p) {
  *out += "\\pard";
  switch (p.align) {
    case PFA_RIGHT: *out += "\\qr"; break;
    case PFA_CENTER: *out += "\\qc"; break;
    case PFA_JUSTIFY: *out += "\\qj"; break;
  }
  if (p.startIndent + p.offset) Append(out, "\\li%ld", p.startIndent + p.offset);
  if (p.offset) Append(out, "\\fi%ld", -p.offset);
  if (p.rightIndent) Append(out, "\\ri%ld", p.rightIndent);
  if (p.spaceBefore) Append(out, "\\sb%ld", p.spaceBefore);
  if (p.spaceAfter) Append(out, "\\sa%ld", p.spaceAfter);
  if (p.lineSpacing) {
    Append(out, "\\sl%ld", p.lineSpacing);
    if (p.lineRule == 5) *out += "\\slmult1";
  }
  for (LONG tab : p.tabs) Append(out, "\\tx%ld", tab);
  *out += ' ';
}

// The OLE 1.0 form of an embedded object.
void Put32(std::vector<BYTE>* v, DWORD d) {
  for (int i = 0; i < 4; i++) v->push_back((BYTE)(d >> (8 * i)));
}
void PutString(std::vector<BYTE>* v, const std::string& s) {
  if (s.empty()) {
    Put32(v, 0);
    return;
  }
  Put32(v, (DWORD)s.size() + 1);
  v->insert(v->end(), s.begin(), s.end());
  v->push_back(0);
}

std::vector<BYTE> Ole1Stream(const std::string& cls, const std::vector<BYTE>& native) {
  std::vector<BYTE> v;
  Put32(&v, 0x00000501);  // OLEVersion
  Put32(&v, 2);           // embedded
  PutString(&v, cls);
  PutString(&v, "");  // topic
  PutString(&v, "");  // item
  Put32(&v, (DWORD)native.size());
  v.insert(v.end(), native.begin(), native.end());
  Put32(&v, 0x00000501);  // no presentation follows
  Put32(&v, 0);
  return v;
}

bool ParseOle1(const std::vector<BYTE>& v, std::string* cls, std::vector<BYTE>* native) {
  size_t p = 0;
  auto get32 = [&](DWORD* d) {
    if (p + 4 > v.size()) return false;
    *d = v[p] | (v[p + 1] << 8) | (v[p + 2] << 16) | ((DWORD)v[p + 3] << 24);
    p += 4;
    return true;
  };
  auto getString = [&](std::string* s) {
    DWORD n;
    if (!get32(&n)) return false;
    if (n > v.size() - p) return false;
    s->assign((const char*)v.data() + p, n ? n - 1 : 0);
    p += n;
    return true;
  };
  DWORD version, format, size;
  std::string topic, item;
  if (!get32(&version) || !get32(&format) || format != 2) return false;
  if (!getString(cls) || !getString(&topic) || !getString(&item) || !get32(&size)) return false;
  if (size > v.size() - p) return false;
  native->assign(v.begin() + p, v.begin() + p + size);
  return true;
}

// ---------------------------------------------------------------------
// Reading

// Windows-1252's characters from 0x80 to 0x9F; the rest of the code page
// is Latin-1.
const unsigned short kCp1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                    0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

enum Dest { kText, kFontTable, kColorTable, kSkip, kObjClass, kObjData, kObject };

struct State {
  CharFmt cf;
  ParaFmt pf;
  Dest dest = kText;
  int uc = 1;
  bool objectGroup = false;  // the group an \object started in
};

struct PendingObject {
  std::string cls;
  std::string hex;
  LONG w = 0, h = 0;
};

class Reader {
 public:
  Reader(const std::string& in, const CharFmt& base, const ObjectLoader& loader, RtfText* out)
      : in_(in), base_(base), loader_(loader), out_(out) {}

  bool Run() {
    size_t i = 0;
    while (i < in_.size() && in_[i] != '{') i++;
    if (i >= in_.size()) return false;
    State top;
    top.cf = base_;
    stack_.push_back(top);
    pos_ = i;
    bool started = false;
    while (pos_ < in_.size()) {
      char c = in_[pos_];
      if (c == '{') {
        pos_++;
        State s = stack_.back();
        s.objectGroup = false;
        stack_.push_back(s);
        started = true;
        pendingStar_ = false;
      } else if (c == '}') {
        pos_++;
        EndGroup();
        if (stack_.size() <= 1) break;  // the document's group is done
        stack_.pop_back();
        pendingStar_ = false;
      } else if (c == '\\') {
        pos_++;
        ControlWord();
      } else if (c == '\r' || c == '\n') {
        pos_++;
      } else {
        pos_++;
        Byte((unsigned char)c);
      }
    }
    if (!started) return false;
    out_->last = stack_.back().pf;
    return true;
  }

 private:
  State& Cur() { return stack_.back(); }

  void EndGroup() {
    State& s = Cur();
    if (s.dest == kFontTable && !fontName_.empty()) CommitFont();
    if (s.objectGroup && object_) {
      std::unique_ptr<PendingObject> p = std::move(object_);
      MakeObject(*p);
    }
  }

  void CommitFont() {
    std::wstring name = fontName_;
    while (!name.empty() && (name.back() == L';' || name.back() == L' ')) name.pop_back();
    fonts_[curFont_] = name;
    fontName_.clear();
  }

  void Emit(wchar_t ch, std::shared_ptr<Obj> obj = nullptr) {
    State& s = Cur();
    switch (s.dest) {
      case kText:
        if (skip_ > 0) {
          skip_--;
          return;
        }
        out_->text += ch;
        out_->cf.push_back(s.cf);
        out_->pf.push_back(s.pf);
        out_->obj.push_back(obj);
        return;
      case kFontTable:
        if (ch == L';') {
          CommitFont();
          return;
        }
        fontName_ += ch;
        return;
      case kColorTable:
        if (ch == L';') {
          colors_.push_back(haveColor_ ? RGB(r_, g_, b_) : (COLORREF)-1);
          r_ = g_ = b_ = 0;
          haveColor_ = false;
        }
        return;
      case kObjClass:
        if (object_) object_->cls += (char)ch;
        return;
      case kObjData:
        if (object_ && isxdigit((int)ch)) object_->hex += (char)ch;
        return;
      default:
        return;
    }
  }

  void Byte(unsigned char b) {
    if (skip_ > 0 && Cur().dest == kText) {
      skip_--;
      return;
    }
    if (b < 0x80) {
      Emit((wchar_t)b);
      return;
    }
    if (codepage_ == 65001) {
      // UTF-8 bytes as they come.
      utf8_ += (char)b;
      std::wstring w = Widen(utf8_.c_str());
      if (!w.empty() && w[0] != 0xFFFD) {
        utf8_.clear();
        for (wchar_t c : w) Emit(c);
      } else if (utf8_.size() >= 4) {
        utf8_.clear();
      }
      return;
    }
    Emit(b >= 0x80 && b < 0xA0 ? (wchar_t)kCp1252[b - 0x80] : (wchar_t)b);
  }

  void Unicode(long n) {
    unsigned long u = (unsigned long)(n < 0 ? n + 65536 : n);
    if (sizeof(wchar_t) == 4 && u >= 0xD800 && u < 0xDC00) {
      high_ = u;
      return;
    }
    if (sizeof(wchar_t) == 4 && u >= 0xDC00 && u < 0xE000 && high_) {
      u = 0x10000 + ((high_ - 0xD800) << 10) + (u - 0xDC00);
    }
    high_ = 0;
    int skip = Cur().uc;
    skip_ = 0;
    Emit((wchar_t)u);
    skip_ = skip;
  }

  void ControlWord() {
    if (pos_ >= in_.size()) return;
    char c = in_[pos_];
    if (!isalpha((unsigned char)c)) {
      pos_++;
      switch (c) {
        case '\\':
        case '{':
        case '}':
          Byte((unsigned char)c);
          return;
        case '\'': {
          unsigned v = 0;
          int k = 0;
          while (k < 2 && pos_ < in_.size() && isxdigit((unsigned char)in_[pos_])) {
            char d = in_[pos_++];
            v = v * 16 + (isdigit((unsigned char)d) ? d - '0' : (tolower(d) - 'a' + 10));
            k++;
          }
          Byte((unsigned char)v);
          return;
        }
        case '~': Emit(0xA0); return;
        case '_': Emit(0x2011); return;
        case '-': return;
        case '*': pendingStar_ = true; return;
        case '\r':
        case '\n': Emit(L'\r'); return;
        case '\t': Emit(L'\t'); return;
        default: return;
      }
    }
    std::string word;
    while (pos_ < in_.size() && isalpha((unsigned char)in_[pos_])) word += in_[pos_++];
    bool hasNum = false;
    long num = 0;
    bool neg = false;
    if (pos_ < in_.size() && in_[pos_] == '-' && pos_ + 1 < in_.size() && isdigit((unsigned char)in_[pos_ + 1])) {
      neg = true;
      pos_++;
    }
    while (pos_ < in_.size() && isdigit((unsigned char)in_[pos_])) {
      hasNum = true;
      num = num * 10 + (in_[pos_++] - '0');
    }
    if (neg) num = -num;
    if (pos_ < in_.size() && in_[pos_] == ' ') pos_++;
    bool star = pendingStar_;
    pendingStar_ = false;
    Word(word, hasNum, num, star);
  }

  void Word(const std::string& w, bool hasNum, long n, bool star) {
    State& s = Cur();
    CharFmt& cf = s.cf;
    ParaFmt& pf = s.pf;
    bool on = !hasNum || n != 0;
    auto effect = [&](DWORD e) {
      if (on)
        cf.effects |= e;
      else
        cf.effects &= ~e;
    };
    // Destinations
    if (w == "fonttbl") {
      s.dest = kFontTable;
      return;
    }
    if (w == "colortbl") {
      s.dest = kColorTable;
      return;
    }
    if (w == "object") {
      object_.reset(new PendingObject);
      s.objectGroup = true;
      s.dest = kObject;
      return;
    }
    if (w == "objclass") {
      s.dest = kObjClass;
      return;
    }
    if (w == "objdata") {
      s.dest = kObjData;
      return;
    }
    if (w == "objw") {
      if (object_) object_->w = n;
      return;
    }
    if (w == "objh") {
      if (object_) object_->h = n;
      return;
    }
    if (w == "result") {
      // The object's picture or text, for readers without the object.
      s.dest = kSkip;
      return;
    }
    static const char* const kSkipped[] = {"info", "stylesheet", "pict", "header", "footer", "headerl",
                                           "headerr", "footerl", "footerr", "footnote", "listtable",
                                           "listoverridetable", "rsidtbl", "generator", "xmlnstbl",
                                           "themedata", "colorschememapping", "latentstyles", "datastore",
                                           "fldinst", "filetbl", "revtbl", "pgdsctbl", "mmathPr", "nonshppict"};
    for (const char* k : kSkipped)
      if (w == k) {
        s.dest = kSkip;
        return;
      }
    if (star && s.dest != kText) {
      s.dest = kSkip;
      return;
    }
    if (s.dest == kColorTable) {
      if (w == "red") r_ = (BYTE)n, haveColor_ = true;
      else if (w == "green") g_ = (BYTE)n, haveColor_ = true;
      else if (w == "blue") b_ = (BYTE)n, haveColor_ = true;
      return;
    }
    if (s.dest == kFontTable) {
      if (w == "f") {
        if (!fontName_.empty()) CommitFont();
        curFont_ = n;
      } else if (w == "fcharset") {
        fontCharset_[curFont_] = (BYTE)n;
      }
      return;
    }
    if (s.dest != kText && s.dest != kObject) return;
    if (star) {
      // An unknown destination of the text: left out.
      s.dest = kSkip;
      return;
    }
    // Characters
    if (w == "par" || w == "line" || w == "sect" || w == "page") {
      Emit(L'\r');
    } else if (w == "tab") {
      Emit(L'\t');
    } else if (w == "u") {
      Unicode(n);
    } else if (w == "uc") {
      s.uc = (int)n;
    } else if (w == "emdash") {
      Emit(0x2014);
    } else if (w == "endash") {
      Emit(0x2013);
    } else if (w == "bullet") {
      Emit(0x2022);
    } else if (w == "lquote") {
      Emit(0x2018);
    } else if (w == "rquote") {
      Emit(0x2019);
    } else if (w == "ldblquote") {
      Emit(0x201C);
    } else if (w == "rdblquote") {
      Emit(0x201D);
    } else if (w == "emspace" || w == "enspace" || w == "qmspace") {
      Emit(L' ');
    } else if (w == "ansicpg") {
      codepage_ = (int)n;
    } else if (w == "deff") {
      deff_ = n;
    }
    // Character formats
    else if (w == "plain") {
      CharFmt f = base_;
      auto it = fonts_.find(deff_);
      if (it != fonts_.end()) f.face = it->second;
      cf = f;
    } else if (w == "b") {
      effect(CFE_BOLD);
      cf.weight = on ? FW_BOLD : FW_NORMAL;
    } else if (w == "i") {
      effect(CFE_ITALIC);
    } else if (w == "ul" || w == "uld" || w == "uldb" || w == "ulw") {
      effect(CFE_UNDERLINE);
      cf.underlineType = !on ? 0
                         : w == "uld"  ? CFU_UNDERLINEDOTTED
                         : w == "uldb" ? CFU_UNDERLINEDOUBLE
                         : w == "ulw"  ? CFU_UNDERLINEWORD
                                       : CFU_UNDERLINE;
    } else if (w == "ulnone") {
      cf.effects &= ~CFE_UNDERLINE;
      cf.underlineType = 0;
    } else if (w == "strike") {
      effect(CFE_STRIKEOUT);
    } else if (w == "scaps") {
      effect(CFE_SMALLCAPS);
    } else if (w == "caps") {
      effect(CFE_ALLCAPS);
    } else if (w == "v") {
      effect(CFE_HIDDEN);
    } else if (w == "protect") {
      effect(CFE_PROTECTED);
    } else if (w == "sub") {
      cf.effects = (cf.effects & ~CFE_SUPERSCRIPT) | CFE_SUBSCRIPT;
    } else if (w == "super") {
      cf.effects = (cf.effects & ~CFE_SUBSCRIPT) | CFE_SUPERSCRIPT;
    } else if (w == "nosupersub") {
      cf.effects &= ~(CFE_SUBSCRIPT | CFE_SUPERSCRIPT);
    } else if (w == "up") {
      cf.offset = (hasNum ? n : 6) * 10;
    } else if (w == "dn") {
      cf.offset = -(hasNum ? n : 6) * 10;
    } else if (w == "f") {
      auto it = fonts_.find(n);
      if (it != fonts_.end()) cf.face = it->second;
      auto cs = fontCharset_.find(n);
      if (cs != fontCharset_.end()) cf.charset = cs->second;
    } else if (w == "fs") {
      if (hasNum && n > 0) cf.height = n * 10;
    } else if (w == "cf") {
      if (n <= 0 || n >= (long)colors_.size() || colors_[n] == (COLORREF)-1) {
        cf.effects |= CFE_AUTOCOLOR;
      } else {
        cf.effects &= ~CFE_AUTOCOLOR;
        cf.color = colors_[n];
      }
    } else if (w == "highlight" || w == "cb" || w == "chcbpat") {
      if (n <= 0 || n >= (long)colors_.size() || colors_[n] == (COLORREF)-1) {
        cf.effects |= CFE_AUTOBACKCOLOR;
      } else {
        cf.effects &= ~CFE_AUTOBACKCOLOR;
        cf.back = colors_[n];
      }
    } else if (w == "lang") {
      cf.lcid = (LCID)n;
    }
    // Paragraph formats
    else if (w == "pard") {
      pf = ParaFmt();
      li_ = fi_ = 0;
    } else if (w == "ql") {
      pf.align = PFA_LEFT;
    } else if (w == "qr") {
      pf.align = PFA_RIGHT;
    } else if (w == "qc") {
      pf.align = PFA_CENTER;
    } else if (w == "qj") {
      pf.align = PFA_JUSTIFY;
    } else if (w == "li") {
      li_ = n;
      Indents(pf);
    } else if (w == "fi") {
      fi_ = n;
      Indents(pf);
    } else if (w == "ri") {
      pf.rightIndent = n;
    } else if (w == "sb") {
      pf.spaceBefore = n;
    } else if (w == "sa") {
      pf.spaceAfter = n;
    } else if (w == "sl") {
      pf.lineSpacing = n;
      pf.lineRule = n == 0 ? 0 : (n < 0 ? 4 : 3);
    } else if (w == "slmult") {
      if (n && pf.lineSpacing > 0) pf.lineRule = 5;
    } else if (w == "tx") {
      if (pf.tabs.size() < MAX_TAB_STOPS) pf.tabs.push_back(n);
    }
  }

  // RTF gives the left indent of all lines (\li) and the first line's
  // indent relative to it (\fi); PARAFORMAT gives the first line's
  // (dxStartIndent) and the others' relative to it (dxOffset).
  void Indents(ParaFmt& pf) {
    pf.startIndent = li_ + fi_;
    pf.offset = -fi_;
  }

  void MakeObject(const PendingObject& p) {
    if (!loader_) return;
    std::vector<BYTE> data;
    for (size_t i = 0; i + 1 < p.hex.size(); i += 2) {
      auto nib = [](char c) { return isdigit((unsigned char)c) ? c - '0' : tolower(c) - 'a' + 10; };
      data.push_back((BYTE)(nib(p.hex[i]) * 16 + nib(p.hex[i + 1])));
    }
    std::string cls;
    std::vector<BYTE> native;
    if (!ParseOle1(data, &cls, &native)) return;
    std::shared_ptr<Obj> o = loader_(cls, native, p.w, p.h);
    if (!o) return;
    // The character goes where the object's group is, in the text.
    for (size_t i = stack_.size(); i-- > 0;) {
      if (stack_[i].dest == kText || stack_[i].objectGroup) {
        Dest saved = Cur().dest;
        Cur().dest = kText;
        int skip = skip_;
        skip_ = 0;
        Emit(kObjectChar, o);
        skip_ = skip;
        Cur().dest = saved;
        break;
      }
    }
  }

  const std::string& in_;
  CharFmt base_;
  const ObjectLoader& loader_;
  RtfText* out_;
  size_t pos_ = 0;
  std::vector<State> stack_;
  std::map<long, std::wstring> fonts_;
  std::map<long, BYTE> fontCharset_;
  std::wstring fontName_;
  long curFont_ = 0, deff_ = 0;
  std::vector<COLORREF> colors_;
  BYTE r_ = 0, g_ = 0, b_ = 0;
  bool haveColor_ = false;
  bool pendingStar_ = false;
  int skip_ = 0;
  unsigned long high_ = 0;
  int codepage_ = 1252;
  std::string utf8_;
  long li_ = 0, fi_ = 0;
  std::unique_ptr<PendingObject> object_;
};

}  // namespace

std::string WriteRtf(const RtfSpan& span, bool withObjects, const ObjectSaver& saver) {
  Tables t;
  CharFmt first = span.chars.empty() ? CharFmt() : *span.chars[0].cf;
  t.Font(first);
  t.colors.push_back(0);
  for (const RichChar& c : span.chars) {
    t.Font(*c.cf);
    if (!(c.cf->effects & CFE_AUTOCOLOR)) t.Color(c.cf->color);
    if (!(c.cf->effects & CFE_AUTOBACKCOLOR)) t.Color(c.cf->back);
  }
  std::string body;
  const CharFmt* cur = NULL;
  ParaFmt defaultPara;
  bool paraStart = true;
  size_t n = span.chars.size();
  for (size_t i = 0; i < n; i++) {
    const RichChar& c = span.chars[i];
    if (paraStart) {
      // The paragraph's format is its mark's.
      const ParaFmt* pf = span.tail ? span.tail : &defaultPara;
      for (size_t j = i; j < n; j++)
        if (span.chars[j].ch == L'\r') {
          pf = span.chars[j].pf;
          break;
        }
      ParaProps(&body, *pf);
      paraStart = false;
    }
    if (!cur || *cur != *c.cf) {
      CharProps(&body, t, *c.cf);
      cur = c.cf;
    }
    if (c.ch == L'\r') {
      body += "\\par\r\n";
      paraStart = true;
    } else if (c.ch == kObjectChar && c.obj) {
      std::string cls;
      std::vector<BYTE> native;
      if (withObjects && saver && saver(c.obj, &cls, &native)) {
        std::vector<BYTE> ole1 = Ole1Stream(cls, native);
        body += "{\\object\\objemb{\\*\\objclass ";
        Text(&body, Widen(cls.c_str()));
        body += "}";
        Append(&body, "\\objw%ld", MulDiv(c.obj->sizel.cx, 1440, 2540));
        Append(&body, "\\objh%ld", MulDiv(c.obj->sizel.cy, 1440, 2540));
        body += "{\\*\\objdata\r\n";
        static const char hex[] = "0123456789abcdef";
        for (size_t k = 0; k < ole1.size(); k++) {
          body += hex[ole1[k] >> 4];
          body += hex[ole1[k] & 15];
          if (k % 64 == 63) body += "\r\n";
        }
        body += "\r\n}}";
      }
    } else {
      Text(&body, std::wstring(1, c.ch));
    }
  }
  if (span.tail) {
    // The last paragraph, which has no mark in the text.
    if (paraStart) ParaProps(&body, *span.tail);
    body += "\\par\r\n";
  }

  std::string out = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\deflang1033{\\fonttbl";
  for (size_t i = 0; i < t.fonts.size(); i++) {
    Append(&out, "{\\f%ld\\fnil", (long)i);
    Append(&out, "\\fcharset%ld ", t.charsets[i]);
    Text(&out, t.fonts[i]);
    out += ";}";
  }
  out += "}\r\n";
  if (t.colors.size() > 1) {
    out += "{\\colortbl ;";
    for (size_t i = 1; i < t.colors.size(); i++) {
      Append(&out, "\\red%ld", GetRValue(t.colors[i]));
      Append(&out, "\\green%ld", GetGValue(t.colors[i]));
      Append(&out, "\\blue%ld;", GetBValue(t.colors[i]));
    }
    out += "}\r\n";
  }
  out += "\\viewkind4\\uc1";
  out += body;
  out += "}\r\n";
  return out;
}

bool ReadRtf(const std::string& in, const CharFmt& base, const ObjectLoader& loader, RtfText* out) {
  Reader r(in, base, loader, out);
  return r.Run();
}

bool IsRtf(const char* p, size_t n) {
  size_t i = 0;
  while (i < n && (p[i] == ' ' || p[i] == '\r' || p[i] == '\n' || p[i] == '\t')) i++;
  return n - i >= 5 && (memcmp(p + i, "{\\rtf", 5) == 0 || memcmp(p + i, "{urtf", 5) == 0);
}

}  // namespace rich
}  // namespace winport
