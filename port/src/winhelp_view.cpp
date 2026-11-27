// winhelp_view.cpp - the layer's WinHelp viewer: the windows that show
// the topics of a help file, and WinHelp's commands and macros.
//
// A help file opens in a main window of the class MS_WINHELP (as
// WinHelp's own), with a menu, a button bar, the region of the topic
// that does not scroll and the region that does. A secondary window of
// the help project is of the class MS_WINDOC. Popup topics show in a
// popup window that closes with the next click or key. The Help Topics
// dialog box has the Contents (from the .cnt file), Index and Find
// tabs. The viewer runs in the program's own process, on the thread
// that calls WinHelp, with the program's message loop.
//
// The text of a topic pane (WM_GETTEXT) is the plain text of the topic
// that it shows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winhelp.h"

#include "dlgtemplate.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace winport {
namespace help {

namespace {

const wchar_t kFrameClass[] = L"MS_WINHELP";
const wchar_t kDocClass[] = L"MS_WINDOC";
const wchar_t kPaneClass[] = L"WinportHelpTopic";
const wchar_t kPopupClass[] = L"WinportHelpPopup";
const wchar_t kAppName[] = L"Windows Help";

const COLORREF kHotColor = RGB(0, 128, 0);
const int kMargin = 6;      // pixels around the text
const int kBarHeight = 30;  // the button bar

// The frame's menu commands and controls.
enum {
  kIdButton = 1000,  // the buttons: kIdButton + their index
  kIdNsPane = 1900,
  kIdScrollPane = 1901,
  kIdOpen = 2001,
  kIdExit,
  kIdCopy,
  kIdOnTop,
  kIdFontSmall,
  kIdFontNormal,
  kIdFontLarge,
  kIdTopics,
  kIdAbout,
  kIdPrint,
};

// The Help Topics dialog box's controls.
enum {
  kIdTab = 100,
  kIdTree = 101,
  kIdIndexLabel1 = 200,
  kIdIndexEdit = 201,
  kIdIndexLabel2 = 202,
  kIdIndexList = 203,
  kIdFindLabel1 = 300,
  kIdFindEdit = 301,
  kIdFindLabel2 = 302,
  kIdFindList = 303,
  kIdFoundList = 400,
};

struct Viewer;

// A piece of a laid-out topic: a run of text, a picture, a button, an
// embedded window's box or a border's line.
struct Frag {
  enum Kind { kText, kPicture, kButton, kBox, kLine };
  Kind kind = kText;
  RECT rc = {0, 0, 0, 0};  // in the layout's own coordinates
  std::wstring text;
  int font = 0;
  int hot = -1;  // the hotspot, in Layout::hots
  int ascent = 0;
  std::shared_ptr<Picture> picture;
};

// A hotspot of a laid-out topic: what it does and where it is.
struct Hot {
  Link link;
  std::vector<RECT> rects;
};

// A topic laid out for a width.
struct Layout {
  std::vector<Frag> frags;
  std::vector<Hot> hots;
  std::vector<int> recordTop;  // each record's top
  int height = 0;
  int right = 0;  // the right edge of the widest line
};

// The fonts of a help file, created as the viewer's zoom wants them.
class FontCache {
 public:
  ~FontCache() { Clear(); }
  // Deletes the fonts (the zoom changed).
  void Clear() {
    for (auto& f : fonts_) DeleteObject(f.second.font);
    fonts_.clear();
  }
  struct Entry {
    HFONT font;
    TEXTMETRICW tm;
  };
  // The font of a |FONT number; underlined for the hotspots of jumps.
  const Entry& Get(File* file, int index, bool underline, int zoom, HDC dc) {
    int key = index * 2 + (underline ? 1 : 0);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    Font f;
    if (index >= 0 && index < (int)file->Fonts().size()) f = file->Fonts()[index];
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    if (dpi <= 0) dpi = 96;
    lf.lfHeight = -MulDiv(f.height, dpi * zoom, 1440 * 100);
    if (lf.lfHeight == 0) lf.lfHeight = -1;
    lf.lfWeight = f.weight;
    lf.lfItalic = f.italic;
    lf.lfUnderline = f.underline || f.doubleUnderline || underline;
    lf.lfStrikeOut = f.strike;
    lf.lfCharSet = f.charset;
    lf.lfPitchAndFamily = f.family;
    std::wstring face = f.face;
    // The Windows 3.0 names of the screen fonts.
    if (_wcsicmp(face.c_str(), L"Helv") == 0 || _wcsicmp(face.c_str(), L"Helvetica") == 0) face = L"Arial";
    if (_wcsicmp(face.c_str(), L"Tms Rmn") == 0) face = L"Times New Roman";
    if (face.empty()) face = L"MS Sans Serif";
    wcsncpy(lf.lfFaceName, face.c_str(), LF_FACESIZE - 1);
    Entry e;
    e.font = CreateFontIndirectW(&lf);
    HGDIOBJ old = SelectObject(dc, e.font);
    GetTextMetricsW(dc, &e.tm);
    SelectObject(dc, old);
    return fonts_[key] = e;
  }

 private:
  std::map<int, Entry> fonts_;
};

// A pane that shows a topic: the region that does not scroll, the one
// that does, or a popup.
struct Pane {
  Viewer* viewer = nullptr;
  HWND hwnd = NULL;
  bool popup = false;
  bool nonScroll = false;
  std::shared_ptr<File> file;
  int topic = -1;
  const std::vector<Record>* records = nullptr;
  std::vector<Record> popupRecords;  // a popup's: both regions
  Layout layout;
  int width = 0;  // the width laid out for
  int scrollY = 0;
  int focus = -1;    // the hotspot that has the keyboard's focus
  int pressed = -1;  // the hotspot the mouse went down on
  COLORREF background = RGB(255, 255, 255);
};

// A button of the button bar.
struct Button {
  std::wstring id;  // BTN_... , for the macros that change it
  std::wstring label, macro;
  HWND hwnd = NULL;
  bool enabled = true;
};

// A help file's window (main or secondary) and its state.
struct Viewer {
  std::shared_ptr<File> file;
  std::wstring windowName;  // empty for the main window
  WindowDef def;
  bool hasDef = false;
  Contents contents;
  bool hasContents = false;
  HWND frame = NULL;
  Pane* ns = nullptr;
  Pane* scroll = nullptr;
  int topic = -1;
  int contentsOverride = -1;
  struct Place {
    int topic, scrollY;
  };
  std::vector<Place> history;
  std::vector<Button> buttons;
  bool browseButtons = false;
  bool onTop = false;
  bool configRun = false;
  int zoom = 100;
  FontCache fonts;
  HWND topicsDialog = NULL;
  HWND foundDialog = NULL;
  COLORREF popupColor = RGB(255, 255, 255);
  bool hasPopupColor = false;
  std::vector<std::wstring> plain;  // the topics' text, lower case, for Find
};

std::vector<std::unique_ptr<Viewer>> g_viewers;
std::map<std::string, std::weak_ptr<File>> g_files;
HWND g_popup = NULL;
POINT g_popupPos = {-1, -1};
bool g_hasPopupPos = false;

// Lower-cases a string, for comparisons that ignore case.
std::wstring Lower(const std::wstring& s) {
  std::wstring r = s;
  for (wchar_t& c : r) c = (wchar_t)towlower(c);
  return r;
}

// Converts twips to the screen's pixels.
int Px(int twips) { return MulDiv(twips, 96, 1440); }

// The directory of a POSIX path, with its slash.
std::string DirOf(const std::string& path) {
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Finds a file in a directory without regard to case, as on Windows.
std::string FindNoCase(const std::string& dir, const std::string& name) {
  std::string p = dir + name;
  FILE* f = fopen(p.c_str(), "rb");
  if (f) {
    fclose(f);
    return p;
  }
  std::string lower = name;
  for (char& c : lower) c = (char)tolower((unsigned char)c);
  // The usual spellings: all lower case, all upper case.
  std::string upper = name;
  for (char& c : upper) c = (char)toupper((unsigned char)c);
  for (const std::string& n : {lower, upper}) {
    p = dir + n;
    if ((f = fopen(p.c_str(), "rb"))) {
      fclose(f);
      return p;
    }
  }
  return std::string();
}

// Opens a help file, or finds it already open.
std::shared_ptr<File> OpenFile(const std::string& path) {
  auto it = g_files.find(path);
  if (it != g_files.end()) {
    if (auto f = it->second.lock()) return f;
  }
  for (auto& v : g_viewers)
    if (v->file->Path() == path) return v->file;
  std::shared_ptr<File> f = File::Open(path);
  if (f) g_files[path] = f;
  return f;
}

// Resolves a help file that a macro or link names, beside the current.
std::string ResolveFile(const std::string& current, const std::wstring& name) {
  if (name.empty()) return current;
  std::string n = PosixPath(name.c_str());
  if (!n.empty() && n[0] == '/') {
    FILE* f = fopen(n.c_str(), "rb");
    if (f) {
      fclose(f);
      return n;
    }
    size_t slash = n.rfind('/');
    n = n.substr(slash + 1);
  }
  std::string found = FindNoCase(DirOf(current), n);
  return found.empty() ? DirOf(current) + n : found;
}

void ClosePopup();
Viewer* GetViewer(const std::shared_ptr<File>& file, const std::wstring& window, bool create);
void ShowTopic(Viewer* v, int topic, int record, bool remember = true);
void RunMacros(Viewer* v, const std::wstring& macros);
void ShowPopup(Viewer* v, const std::shared_ptr<File>& file, int topic, POINT at);
void Follow(Viewer* v, const Link& link, POINT at);
void ShowFinder(Viewer* v, int tab, const std::wstring& text);
void LayoutFrame(Viewer* v);
LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

// ---------------------------------------------------------------------
// Layout

// Lays out the records of a topic for a width: breaks their text into
// lines, places the pictures and tables, and finds the hotspots.
class Layouter {
 public:
  // Makes a layouter that adds to out, measuring with dc.
  Layouter(Viewer* v, File* file, HDC dc, Layout* out) : v_(v), file_(file), dc_(dc), out_(out) {}

  // Lays out records between x0 and x1, from the top.
  void Records(const std::vector<Record>& recs, int x0, int x1) {
    int y = kMargin / 2;
    for (const Record& r : recs) {
      out_->recordTop.push_back(y);
      if (r.table)
        y = Table(r, x0, x1, y);
      else if (!r.cells.empty())
        y = Cell(r.cells[0], x0, x1, y);
    }
    out_->height = y + kMargin / 2;
  }

 private:
  // The font entry of a |FONT number, plain or underlined.
  const FontCache::Entry& FontOf(int font, bool underline) {
    return v_->fonts.Get(file_, font, underline, v_->zoom, dc_);
  }

  // Lays out a table's row: each column's paragraphs side by side.
  int Table(const Record& r, int x0, int x1, int y) {
    std::vector<int> left, right;
    int total = 0;
    for (const auto& c : r.columns) total += c.width + c.gap;
    double scale = 1;
    int avail = x1 - x0;
    if (r.relativeWidths && total > 0) {
      int w = std::max(avail, Px(r.minTableWidth));
      scale = (double)w / total;
    }
    int x = x0;
    for (const auto& c : r.columns) {
      int gap = r.relativeWidths ? (int)(c.gap * scale) : Px(c.gap);
      int width = r.relativeWidths ? (int)(c.width * scale) : Px(c.width);
      left.push_back(x + gap);
      right.push_back(x + gap + std::max(width, 1));
      x += gap + width;
    }
    int bottom = y;
    std::vector<int> colY(r.columns.size(), y);
    for (const auto& cell : r.cells) {
      // A cell of no items only marks where the next column starts.
      if (cell.column < 0 || cell.column >= (int)r.columns.size() || cell.items.empty()) continue;
      int c = cell.column;
      colY[c] = Cell(cell, left[c], right[c], colY[c]);
      bottom = std::max(bottom, colY[c]);
    }
    return bottom;
  }

  // Lays out the paragraphs of one cell (or text record).
  int Cell(const struct Cell& cell, int x0, int x1, int y) {
    const ParaFormat& pf = cell.format;
    size_t i = 0;
    const std::vector<Item>& items = cell.items;
    while (i < items.size()) {
      size_t j = i;
      while (j < items.size() && items[j].type != Item::kParaEnd) j++;
      y = Para(pf, items, i, j, x0, x1, y);
      i = j < items.size() ? j + 1 : j;
    }
    // A cell that holds nothing still is an empty line of a table.
    if (items.empty()) y = Para(pf, items, 0, 0, x0, x1, y);
    y = std::max(y, floatBottom_);
    floatLeft_ = floatRight_ = 0;
    floatBottom_ = 0;
    return y;
  }

  // Lays out one paragraph: items [b, e) in the format pf.
  int Para(const ParaFormat& pf, const std::vector<Item>& items, size_t b, size_t e, int x0, int x1, int y) {
    pf_ = &pf;
    x0_ = x0;
    left_ = x0 + Px(pf.leftIndent);
    right_ = std::max(x1 - Px(pf.rightIndent), left_ + 8);
    y_ = y + Px(pf.spaceAbove);
    int top = y_;
    StartLine(true);
    for (size_t i = b; i < e; i++) {
      const Item& it = items[i];
      switch (it.type) {
        case Item::kText: Text(it.text); break;
        case Item::kFont: font_ = it.font; break;
        case Item::kLineBreak: FinishLine(); StartLine(false); break;
        case Item::kTab: Tab(); break;
        case Item::kPicture: Picture(it); break;
        case Item::kWindow: Window(it); break;
        case Item::kLinkStart: {
          Hot h;
          h.link = it.link;
          out_->hots.push_back(h);
          hot_ = (int)out_->hots.size() - 1;
          break;
        }
        case Item::kLinkEnd: hot_ = -1; break;
        default: break;
      }
    }
    FinishLine();
    y_ += Px(pf.spaceBelow);
    // The borders.
    if (pf.border) {
      int w = (pf.border & 0x20) ? 2 : 1;
      RECT box = {left_ - 2, top - 1, right_ + 2, y_};
      if (pf.border & 0x01) {
        Line(box.left, box.top, box.right, box.top + w);
        Line(box.left, box.bottom - w, box.right, box.bottom);
        Line(box.left, box.top, box.left + w, box.bottom);
        Line(box.right - w, box.top, box.right, box.bottom);
      }
      if (pf.border & 0x02) Line(box.left, box.top, box.right, box.top + w);
      if (pf.border & 0x08) Line(box.left, box.bottom - w, box.right, box.bottom);
      if (pf.border & 0x04) Line(box.left, box.top, box.left + w, box.bottom);
      if (pf.border & 0x10) Line(box.right - w, box.top, box.right, box.bottom);
    }
    return y_;
  }

  // Adds a border's line.
  void Line(int l, int t, int r, int b) {
    Frag f;
    f.kind = Frag::kLine;
    f.rc = {l, t, r, b};
    out_->frags.push_back(f);
  }

  // Starts a line: the first of a paragraph has the first-line indent.
  void StartLine(bool first) {
    line_.clear();
    lineLeft_ = left_ + (first ? Px(pf_->firstIndent) : 0);
    if (floatBottom_ > y_) lineLeft_ = std::max(lineLeft_, floatLeft_);
    lineRight_ = right_;
    if (floatBottom_ > y_ && floatRight_) lineRight_ = std::min(lineRight_, floatRight_);
    x_ = lineLeft_;
    contentRight_ = x_;
    ascent_ = descent_ = 0;
    pendingTab_ = -1;
  }

  // Ends the line: aligns it, places its pieces on the baseline and
  // moves down by its height.
  void FinishLine() {
    ResolveTab();
    const FontCache::Entry& fe = FontOf(font_, false);
    if (line_.empty()) {
      ascent_ = std::max(ascent_, (int)fe.tm.tmAscent);
      descent_ = std::max(descent_, (int)fe.tm.tmDescent);
    }
    int h = ascent_ + descent_;
    int ls = Px(pf_->lineSpacing);
    if (ls > 0 && ls > h) h = ls;
    if (ls < 0) h = -ls;
    int dx = 0;
    if (pf_->align == 1) dx = lineRight_ - contentRight_;
    if (pf_->align == 2) dx = (lineRight_ - contentRight_) / 2;
    if (dx < 0) dx = 0;
    for (Frag& f : line_) {
      int fh = f.rc.bottom - f.rc.top;
      f.rc.left += dx;
      f.rc.right += dx;
      f.rc.top = y_ + ascent_ - f.ascent;
      f.rc.bottom = f.rc.top + fh;
      out_->right = std::max(out_->right, (int)f.rc.right);
      if (f.hot >= 0) out_->hots[f.hot].rects.push_back(f.rc);
      out_->frags.push_back(f);
    }
    line_.clear();
    y_ += h;
  }

  // Adds a piece to the line; it has its width and height set, its top 0.
  void Place(Frag f, int width, bool content) {
    f.rc.left = x_;
    f.rc.right = x_ + width;
    line_.push_back(f);
    x_ += width;
    if (content) contentRight_ = x_;
  }

  // Adds text: word by word, a new line when one does not fit.
  void Text(const std::wstring& s) {
    bool hot = hot_ >= 0 && out_->hots[hot_].link.visible;
    bool underline = hot && out_->hots[hot_].link.kind != Link::kPopup;
    const FontCache::Entry& fe = FontOf(font_, underline);
    HGDIOBJ old = SelectObject(dc_, fe.font);
    size_t i = 0;
    while (i < s.size()) {
      size_t j = i;
      while (j < s.size() && s[j] != L' ') j++;
      size_t k = j;
      while (k < s.size() && s[k] == L' ') k++;
      std::wstring word = s.substr(i, j - i), spaces = s.substr(j, k - j);
      SIZE ws = {0, 0}, ss = {0, 0};
      if (!word.empty()) GetTextExtentPoint32W(dc_, word.c_str(), (int)word.size(), &ws);
      if (!spaces.empty()) GetTextExtentPoint32W(dc_, spaces.c_str(), (int)spaces.size(), &ss);
      if (!word.empty() && x_ + ws.cx > lineRight_ && !line_.empty() && pendingTab_ < 0) {
        FinishLine();
        StartLine(false);
      }
      ascent_ = std::max(ascent_, (int)fe.tm.tmAscent);
      descent_ = std::max(descent_, (int)fe.tm.tmDescent);
      // Joins the text of the same font and hotspot to the piece before.
      std::wstring piece = word + spaces;
      if (!piece.empty()) {
        Frag* last = line_.empty() ? nullptr : &line_.back();
        int w = ws.cx + ss.cx;
        if (last && last->kind == Frag::kText && last->font == font_ && last->hot == hot_ &&
            last->rc.right == x_) {
          last->text += piece;
          last->rc.right += w;
          x_ += w;
          if (!word.empty()) contentRight_ = x_ - ss.cx;
        } else {
          Frag f;
          f.kind = Frag::kText;
          f.text = piece;
          f.font = font_;
          f.hot = hot_;
          f.ascent = fe.tm.tmAscent;
          f.rc.bottom = fe.tm.tmHeight;
          Place(f, w, false);
          if (!word.empty()) contentRight_ = x_ - ss.cx;
        }
      }
      i = k;
    }
    SelectObject(dc_, old);
  }

  // Moves to the next tab stop: the paragraph's, the hanging indent, or
  // every half inch.
  void Tab() {
    ResolveTab();
    int at = x_ - x0_;
    int stop = -1, type = 0;
    for (const auto& t : pf_->tabs) {
      int p = Px(t.pos);
      if (p > at) {
        stop = p;
        type = t.type;
        break;
      }
    }
    int hang = left_ - x0_;
    if (pf_->firstIndent < 0 && at < hang && (stop < 0 || hang < stop)) {
      stop = hang;
      type = 0;
    }
    if (stop < 0) {
      int step = Px(720);
      stop = (at / step + 1) * step;
    }
    if (x0_ + stop > lineRight_ && !line_.empty()) {
      FinishLine();
      StartLine(false);
      return;
    }
    if (type == 0) {
      x_ = x0_ + stop;
    } else {
      pendingTab_ = (int)line_.size();
      tabType_ = type;
      tabPos_ = x0_ + stop;
      tabStart_ = x_;
    }
  }

  // Moves the text after a right or centered tab into place.
  void ResolveTab() {
    if (pendingTab_ < 0) return;
    int width = x_ - tabStart_;
    int dx = tabType_ == 1 ? tabPos_ - width - tabStart_ : tabPos_ - width / 2 - tabStart_;
    if (dx > 0) {
      for (size_t i = pendingTab_; i < line_.size(); i++) {
        line_[i].rc.left += dx;
        line_[i].rc.right += dx;
      }
      x_ += dx;
      contentRight_ += dx;
    }
    pendingTab_ = -1;
  }

  // Adds a picture: in the line (bmc), or at the left (bml) or right
  // (bmr) with the text beside it.
  void Picture(const Item& it) {
    std::shared_ptr<help::Picture> pic = it.picture;
    if (!pic && it.pictureNumber >= 0) pic = file_->GetPicture(it.pictureNumber);
    if (!pic || pic->width <= 0) return;
    Frag f;
    f.kind = Frag::kPicture;
    f.picture = pic;
    f.hot = hot_;
    int w = pic->width, h = pic->height;
    if (it.align == 0) {
      if (x_ + w > lineRight_ && !line_.empty()) {
        FinishLine();
        StartLine(false);
      }
      f.ascent = h;
      f.rc.bottom = h;
      ascent_ = std::max(ascent_, h);
      Place(f, w, true);
      AddPictureHotspots(line_.size() - 1);
      return;
    }
    if (!line_.empty()) {
      FinishLine();
      StartLine(false);
    }
    if (it.align == 1) {
      f.rc = {lineLeft_, y_, lineLeft_ + w, y_ + h};
      floatLeft_ = f.rc.right + 4;
      lineLeft_ = x_ = contentRight_ = floatLeft_;
    } else {
      f.rc = {lineRight_ - w, y_, lineRight_, y_ + h};
      floatRight_ = f.rc.left - 4;
      lineRight_ = floatRight_;
    }
    floatBottom_ = std::max(floatBottom_, y_ + h);
    out_->right = std::max(out_->right, (int)f.rc.right);
    if (f.hot >= 0) out_->hots[f.hot].rects.push_back(f.rc);
    out_->frags.push_back(f);
    AddHotspotsAt(pic, f.rc);
  }

  // Adds the hotspots of the picture in the line (once it is placed,
  // in FinishLine); here the rectangles are relative.
  void AddPictureHotspots(size_t index) {
    Frag& f = line_[index];
    if (!f.picture || f.picture->hotspots.empty()) return;
    // Each becomes a piece of its own over the picture, so that it moves
    // with the line.
    for (const auto& hs : f.picture->hotspots) {
      Hot h;
      h.link = hs.link;
      out_->hots.push_back(h);
      Frag over;
      over.kind = Frag::kBox;
      over.text.clear();
      over.hot = (int)out_->hots.size() - 1;
      over.rc = {f.rc.left + hs.rect.left, hs.rect.top, f.rc.left + hs.rect.right, hs.rect.bottom};
      over.ascent = f.ascent - hs.rect.top;
      over.font = -1;  // nothing to draw
      line_.push_back(over);
    }
  }

  // Adds the hotspots of a picture placed at rc.
  void AddHotspotsAt(const std::shared_ptr<help::Picture>& pic, const RECT& rc) {
    for (const auto& hs : pic->hotspots) {
      Hot h;
      h.link = hs.link;
      RECT r = {rc.left + hs.rect.left, rc.top + hs.rect.top, rc.left + hs.rect.right, rc.top + hs.rect.bottom};
      h.rects.push_back(r);
      out_->hots.push_back(h);
    }
  }

  // Adds an embedded window: a {button} that runs a macro, or a box
  // that stands for a window the viewer cannot make.
  void Window(const Item& it) {
    Frag f;
    const FontCache::Entry& fe = FontOf(font_, false);
    HGDIOBJ old = SelectObject(dc_, fe.font);
    int w, h;
    if (!it.text.empty() && it.text[0] == L'!') {
      // !label,macro
      size_t comma = it.text.find(L',');
      std::wstring label = it.text.substr(1, comma == std::wstring::npos ? std::wstring::npos : comma - 1);
      std::wstring macro = comma == std::wstring::npos ? std::wstring() : it.text.substr(comma + 1);
      f.kind = Frag::kButton;
      f.text = label;
      SIZE sz = {0, 0};
      if (!label.empty()) GetTextExtentPoint32W(dc_, label.c_str(), (int)label.size(), &sz);
      w = label.empty() ? 14 : sz.cx + 16;
      h = label.empty() ? 14 : fe.tm.tmHeight + 6;
      Hot hot;
      hot.link.kind = Link::kMacro;
      hot.link.macro = macro;
      out_->hots.push_back(hot);
      f.hot = (int)out_->hots.size() - 1;
    } else {
      f.kind = Frag::kBox;
      std::wstring t = it.text;
      if (!t.empty() && t[0] == L'*') t = L"Media";
      size_t comma = t.find(L',');
      if (comma != std::wstring::npos) t = t.substr(comma + 1, t.find(L',', comma + 1) - comma - 1);
      f.text = t;
      SIZE sz = {0, 0};
      if (!t.empty()) GetTextExtentPoint32W(dc_, t.c_str(), (int)t.size(), &sz);
      w = sz.cx + 12;
      h = fe.tm.tmHeight + 8;
      f.font = font_;
    }
    SelectObject(dc_, old);
    if (x_ + w > lineRight_ && !line_.empty()) {
      FinishLine();
      StartLine(false);
    }
    f.ascent = h - fe.tm.tmDescent;
    f.rc.bottom = h;
    ascent_ = std::max(ascent_, f.ascent);
    descent_ = std::max(descent_, (int)fe.tm.tmDescent);
    if (f.kind == Frag::kButton) f.font = font_;
    Place(f, w, true);
  }

  Viewer* v_;
  File* file_;
  HDC dc_;
  Layout* out_;
  const ParaFormat* pf_ = nullptr;
  int font_ = 0, hot_ = -1;
  int x0_ = 0, left_ = 0, right_ = 0, y_ = 0;
  int lineLeft_ = 0, lineRight_ = 0, x_ = 0, contentRight_ = 0, ascent_ = 0, descent_ = 0;
  int floatLeft_ = 0, floatRight_ = 0, floatBottom_ = 0;
  int pendingTab_ = -1, tabType_ = 0, tabPos_ = 0, tabStart_ = 0;
  std::vector<Frag> line_;
};

// Lays out a pane's records for its width.
void LayoutPane(Pane* p, int width) {
  p->layout = Layout();
  p->width = width;
  p->focus = -1;
  if (!p->records || !p->file) return;
  HDC dc = GetDC(p->hwnd);
  Layouter l(p->viewer, p->file.get(), dc, &p->layout);
  l.Records(*p->records, kMargin, std::max(width - kMargin, kMargin + 40));
  ReleaseDC(p->hwnd, dc);
}

// ---------------------------------------------------------------------
// Drawing

// Draws a picture into rc.
void DrawPicture(HDC dc, const Picture& pic, const RECT& rc, COLORREF background) {
  if (pic.type == Picture::kBitmap) {
    std::vector<BYTE> buf(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
    BITMAPINFO* bi = (BITMAPINFO*)buf.data();
    bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi->bmiHeader.biWidth = pic.width;
    bi->bmiHeader.biHeight = pic.height;
    bi->bmiHeader.biPlanes = 1;
    bi->bmiHeader.biBitCount = (WORD)pic.bitCount;
    bi->bmiHeader.biCompression = BI_RGB;
    size_t n = std::min(pic.palette.size(), (size_t)256);
    bi->bmiHeader.biClrUsed = (DWORD)n;
    for (size_t i = 0; i < n; i++) {
      RGBQUAD c = pic.palette[i];
      // A transparent picture: its white is the window's color.
      if (pic.transparent && c.rgbRed == 255 && c.rgbGreen == 255 && c.rgbBlue == 255) {
        c.rgbRed = GetRValue(background);
        c.rgbGreen = GetGValue(background);
        c.rgbBlue = GetBValue(background);
      }
      bi->bmiColors[i] = c;
    }
    StretchDIBits(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0, 0, pic.width, pic.height,
                  pic.bits.data(), bi, DIB_RGB_COLORS, SRCCOPY);
  } else if (pic.type == Picture::kMetafile && !pic.metafile.empty()) {
    HMETAFILE mf = SetMetaFileBitsEx((UINT)pic.metafile.size(), pic.metafile.data());
    if (!mf) return;
    int saved = SaveDC(dc);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetViewportOrgEx(dc, rc.left, rc.top, NULL);
    SetViewportExtEx(dc, rc.right - rc.left, rc.bottom - rc.top, NULL);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);
    PlayMetaFile(dc, mf);
    RestoreDC(dc, saved);
    DeleteMetaFile(mf);
  }
}

// Paints a pane: its layout, scrolled, with the hotspots' colors.
void PaintPane(Pane* p, HDC target, const RECT& client) {
  int w = client.right - client.left, h = client.bottom - client.top;
  if (w <= 0 || h <= 0) return;
  HDC dc = CreateCompatibleDC(target);
  HBITMAP bm = CreateCompatibleBitmap(target, w, h);
  HGDIOBJ oldBm = SelectObject(dc, bm);
  HBRUSH bg = CreateSolidBrush(p->background);
  RECT all = {0, 0, w, h};
  FillRect(dc, &all, bg);
  DeleteObject(bg);
  SetBkMode(dc, TRANSPARENT);
  int dy = -p->scrollY;
  File* file = p->file.get();
  for (const Frag& f : p->layout.frags) {
    RECT rc = f.rc;
    OffsetRect(&rc, 0, dy);
    if (rc.bottom < 0 || rc.top > h) continue;
    const Hot* hot = f.hot >= 0 ? &p->layout.hots[f.hot] : nullptr;
    bool styled = hot && hot->link.visible;
    switch (f.kind) {
      case Frag::kText: {
        bool underline = styled && hot->link.kind != Link::kPopup;
        const FontCache::Entry& fe = p->viewer->fonts.Get(file, f.font, underline, p->viewer->zoom, dc);
        HGDIOBJ old = SelectObject(dc, fe.font);
        COLORREF color = RGB(0, 0, 0);
        if (f.font >= 0 && f.font < (int)file->Fonts().size()) color = file->Fonts()[f.font].color;
        if (styled) color = kHotColor;
        SetTextColor(dc, color);
        TextOutW(dc, rc.left, rc.top, f.text.c_str(), (int)f.text.size());
        SelectObject(dc, old);
        if (styled && hot->link.kind == Link::kPopup) {
          // A popup's hotspot: a dotted line under the text.
          int y = rc.top + fe.tm.tmAscent + 1;
          size_t len = f.text.size();
          while (len && f.text[len - 1] == L' ') len--;
          SIZE sz = {0, 0};
          if (len) {
            HGDIOBJ o = SelectObject(dc, fe.font);
            GetTextExtentPoint32W(dc, f.text.c_str(), (int)len, &sz);
            SelectObject(dc, o);
          }
          for (int x = rc.left; x < rc.left + sz.cx; x += 2) SetPixel(dc, x, y, kHotColor);
        }
        break;
      }
      case Frag::kPicture:
        if (f.picture) DrawPicture(dc, *f.picture, rc, p->background);
        break;
      case Frag::kButton: {
        DrawFrameControl(dc, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (p->pressed == f.hot ? DFCS_PUSHED : 0));
        if (!f.text.empty()) {
          const FontCache::Entry& fe = p->viewer->fonts.Get(file, f.font, false, p->viewer->zoom, dc);
          HGDIOBJ old = SelectObject(dc, fe.font);
          SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
          DrawTextW(dc, f.text.c_str(), (int)f.text.size(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
          SelectObject(dc, old);
        }
        break;
      }
      case Frag::kBox: {
        if (f.font < 0) break;  // a picture's hotspot
        HBRUSH b = CreateSolidBrush(RGB(192, 192, 192));
        FillRect(dc, &rc, b);
        DeleteObject(b);
        FrameRect(dc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
        const FontCache::Entry& fe = p->viewer->fonts.Get(file, f.font, false, p->viewer->zoom, dc);
        HGDIOBJ old = SelectObject(dc, fe.font);
        SetTextColor(dc, RGB(64, 64, 64));
        DrawTextW(dc, f.text.c_str(), (int)f.text.size(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        break;
      }
      case Frag::kLine:
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        break;
    }
  }
  // The line between the region that does not scroll and the one that does.
  if (p->nonScroll) {
    RECT line = {0, h - 1, w, h};
    FillRect(dc, &line, (HBRUSH)GetStockObject(GRAY_BRUSH));
  }
  if (p->focus >= 0 && p->focus < (int)p->layout.hots.size() && GetFocus() == p->hwnd) {
    for (RECT rc : p->layout.hots[p->focus].rects) {
      OffsetRect(&rc, 0, dy);
      InflateRect(&rc, 1, 0);
      DrawFocusRect(dc, &rc);
    }
  }
  BitBlt(target, client.left, client.top, w, h, dc, 0, 0, SRCCOPY);
  SelectObject(dc, oldBm);
  DeleteObject(bm);
  DeleteDC(dc);
}

// The hotspot at a point of a pane's client area, or -1.
int HotAt(Pane* p, POINT pt) {
  pt.y += p->scrollY;
  // The last first: a picture's own hotspots lie over the picture's.
  for (int i = (int)p->layout.hots.size() - 1; i >= 0; i--)
    for (const RECT& rc : p->layout.hots[i].rects)
      if (PtInRect(&rc, pt)) return i;
  return -1;
}

// Updates a pane's scroll bar to its layout.
void UpdateScroll(Pane* p) {
  if (p->nonScroll || p->popup) return;
  RECT rc;
  GetClientRect(p->hwnd, &rc);
  int page = rc.bottom - rc.top;
  int maxY = std::max(0, p->layout.height - page);
  p->scrollY = std::max(0, std::min(p->scrollY, maxY));
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = std::max(p->layout.height - 1, 0);
  si.nPage = (UINT)std::max(page, 0);
  si.nPos = p->scrollY;
  SetScrollInfo(p->hwnd, SB_VERT, &si, TRUE);
}

// Scrolls a pane to a position.
void ScrollTo(Pane* p, int y) {
  RECT rc;
  GetClientRect(p->hwnd, &rc);
  int maxY = std::max(0, p->layout.height - (int)(rc.bottom - rc.top));
  y = std::max(0, std::min(y, maxY));
  if (y == p->scrollY) return;
  p->scrollY = y;
  SetScrollPos(p->hwnd, SB_VERT, y, TRUE);
  InvalidateRect(p->hwnd, NULL, FALSE);
}

// The plain text a pane shows.
std::wstring PaneText(Pane* p) {
  std::wstring out;
  if (!p->records) return out;
  for (const Record& rec : *p->records) {
    bool first = true;
    for (size_t c = 0; c < rec.cells.size(); c++) {
      if (rec.table && rec.cells[c].items.empty()) continue;
      if (rec.table && !first) {
        while (!out.empty() && out.back() == L'\n') out.pop_back();
        out += L'\t';
      }
      first = false;
      for (const Item& it : rec.cells[c].items) {
        if (it.type == Item::kText) out += it.text;
        if (it.type == Item::kTab) out += L'\t';
        if (it.type == Item::kLineBreak || it.type == Item::kParaEnd) out += L'\n';
      }
    }
    if (!out.empty() && out.back() != L'\n') out += L'\n';
  }
  return out;
}

// Copies text to the clipboard.
void CopyText(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) return;
  EmptyClipboard();
  std::wstring crlf;
  for (wchar_t c : text) {
    if (c == L'\n') crlf += L'\r';
    crlf += c;
  }
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (crlf.size() + 1) * sizeof(wchar_t));
  if (g) {
    wchar_t* d = (wchar_t*)GlobalLock(g);
    memcpy(d, crlf.c_str(), (crlf.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(g);
    SetClipboardData(CF_UNICODETEXT, g);
  }
  CloseClipboard();
}

// Runs the hotspot of a pane.
void Activate(Pane* p, int hot) {
  if (hot < 0 || hot >= (int)p->layout.hots.size()) return;
  Link link = p->layout.hots[hot].link;
  POINT at = {0, 0};
  if (!p->layout.hots[hot].rects.empty()) {
    const RECT& rc = p->layout.hots[hot].rects.front();
    at.x = rc.left;
    at.y = rc.bottom - p->scrollY;
  }
  ClientToScreen(p->hwnd, &at);
  Viewer* v = p->viewer;
  bool wasPopup = p->popup;
  std::shared_ptr<File> file = p->file;
  // A jump from a popup closes it; a popup from a popup replaces it.
  if (wasPopup) ClosePopup();
  if (file != v->file) {
    Viewer* other = GetViewer(file, std::wstring(), true);
    if (other) v = other;
  }
  Follow(v, link, at);
}

// The topic pane's window procedure.
LRESULT CALLBACK PaneProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Pane* p = (Pane*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (msg) {
    case WM_NCCREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      p = (Pane*)cs->lpCreateParams;
      p->hwnd = h;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
      break;
    }
    case WM_NCDESTROY:
      if (p) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        p->hwnd = NULL;
        if (p->popup) delete p;
      }
      break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      if (p) PaintPane(p, dc, rc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SIZE:
      if (p && !p->popup) {
        int w = LOWORD(lp);
        if (w != p->width) LayoutPane(p, w);
        UpdateScroll(p);
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    case WM_VSCROLL: {
      if (!p) break;
      RECT rc;
      GetClientRect(h, &rc);
      int page = rc.bottom - rc.top, line = 16, y = p->scrollY;
      switch (LOWORD(wp)) {
        case SB_LINEUP: y -= line; break;
        case SB_LINEDOWN: y += line; break;
        case SB_PAGEUP: y -= page - line; break;
        case SB_PAGEDOWN: y += page - line; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = p->layout.height; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
          SCROLLINFO si;
          memset(&si, 0, sizeof(si));
          si.cbSize = sizeof(si);
          si.fMask = SIF_TRACKPOS;
          GetScrollInfo(h, SB_VERT, &si);
          y = si.nTrackPos;
          break;
        }
      }
      ScrollTo(p, y);
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (p && !p->nonScroll && !p->popup) ScrollTo(p, p->scrollY - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 48);
      return 0;
    case WM_SETCURSOR:
      if (p && LOWORD(lp) == HTCLIENT) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        HCURSOR cur = LoadCursorW(NULL, (LPCWSTR)(HotAt(p, pt) >= 0 ? IDC_HAND : IDC_ARROW));
        if (!cur) cur = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        SetCursor(cur);
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      if (!p) break;
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      RECT rc;
      GetClientRect(h, &rc);
      if (p->popup && !PtInRect(&rc, pt)) {
        // A click outside the popup (it has the mouse captured).
        ClosePopup();
        return 0;
      }
      p->pressed = HotAt(p, pt);
      if (!p->popup) SetFocus(h);
      if (p->pressed >= 0) {
        p->focus = p->pressed;
        InvalidateRect(h, NULL, FALSE);
      } else if (p->popup) {
        ClosePopup();
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (!p) break;
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      int hot = HotAt(p, pt);
      int pressed = p->pressed;
      p->pressed = -1;
      if (hot >= 0 && hot == pressed) {
        InvalidateRect(h, NULL, FALSE);
        Activate(p, hot);
      }
      return 0;
    }
    case WM_RBUTTONUP: {
      if (!p || p->popup || !p->viewer->frame) break;
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, kIdCopy, L"&Copy");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      AppendMenuW(m, MF_STRING | (p->viewer->zoom < 100 ? MF_CHECKED : 0), kIdFontSmall, L"Font &Small");
      AppendMenuW(m, MF_STRING | (p->viewer->zoom == 100 ? MF_CHECKED : 0), kIdFontNormal, L"Font &Normal");
      AppendMenuW(m, MF_STRING | (p->viewer->zoom > 100 ? MF_CHECKED : 0), kIdFontLarge, L"Font &Large");
      AppendMenuW(m, MF_STRING | (p->viewer->onTop ? MF_CHECKED : 0), kIdOnTop, L"Keep Help on &Top");
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      ClientToScreen(h, &pt);
      TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, p->viewer->frame, NULL);
      DestroyMenu(m);
      return 0;
    }
    case WM_KEYDOWN: {
      if (!p) break;
      if (p->popup) {
        if (wp == VK_RETURN && p->focus >= 0) {
          Activate(p, p->focus);
          return 0;
        }
        if (wp != VK_TAB) {
          ClosePopup();
          return 0;
        }
      }
      RECT rc;
      GetClientRect(h, &rc);
      int page = rc.bottom - rc.top;
      switch (wp) {
        case VK_UP: ScrollTo(p, p->scrollY - 16); return 0;
        case VK_DOWN: ScrollTo(p, p->scrollY + 16); return 0;
        case VK_PRIOR: ScrollTo(p, p->scrollY - page + 16); return 0;
        case VK_NEXT: ScrollTo(p, p->scrollY + page - 16); return 0;
        case VK_HOME: ScrollTo(p, 0); return 0;
        case VK_END: ScrollTo(p, p->layout.height); return 0;
        case VK_TAB: {
          int n = (int)p->layout.hots.size();
          if (!n) return 0;
          bool back = GetKeyState(VK_SHIFT) < 0;
          // The next hotspot that is on the screen somewhere.
          for (int k = 0; k < n; k++) {
            p->focus = back ? (p->focus <= 0 ? n - 1 : p->focus - 1) : (p->focus + 1) % n;
            if (!p->layout.hots[p->focus].rects.empty()) break;
          }
          if (!p->layout.hots[p->focus].rects.empty()) {
            const RECT& r = p->layout.hots[p->focus].rects.front();
            if (r.top < p->scrollY || r.bottom > p->scrollY + page) ScrollTo(p, r.top - page / 3);
          }
          InvalidateRect(h, NULL, FALSE);
          return 0;
        }
        case VK_RETURN:
          if (p->focus >= 0) Activate(p, p->focus);
          return 0;
        case VK_ESCAPE:
          if (p->viewer->frame) PostMessageW(p->viewer->frame, WM_CLOSE, 0, 0);
          return 0;
      }
      if (wp == 'C' && GetKeyState(VK_CONTROL) < 0 && p->viewer->frame)
        PostMessageW(p->viewer->frame, WM_COMMAND, kIdCopy, 0);
      break;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTTAB;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      if (p) InvalidateRect(h, NULL, FALSE);
      break;
    case WM_CAPTURECHANGED:
      if (p && p->popup && (HWND)lp != h && g_popup == h) PostMessageW(h, WM_CLOSE, 0, 0);
      break;
    case WM_CLOSE:
      if (p && p->popup) {
        ClosePopup();
        return 0;
      }
      break;
    case WM_GETTEXTLENGTH:
      return p ? (LRESULT)PaneText(p).size() : 0;
    case WM_GETTEXT: {
      if (!p || !lp || !wp) return 0;
      std::wstring t = PaneText(p);
      size_t n = std::min(t.size(), (size_t)wp - 1);
      wmemcpy((wchar_t*)lp, t.c_str(), n);
      ((wchar_t*)lp)[n] = 0;
      return (LRESULT)n;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// Closes the popup, if one is open.
void ClosePopup() {
  HWND h = g_popup;
  if (!h) return;
  g_popup = NULL;
  if (GetCapture() == h) ReleaseCapture();
  DestroyWindow(h);
}

// Registers the viewer's window classes, once.
void RegisterClasses() {
  static bool done = false;
  if (done) return;
  done = true;
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
  wc.lpfnWndProc = PaneProc;
  wc.lpszClassName = kPaneClass;
  RegisterClassW(&wc);
  wc.lpszClassName = kPopupClass;
  wc.style = CS_DROPSHADOW | CS_SAVEBITS;
  RegisterClassW(&wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = FrameProc;
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_QUESTION);
  wc.lpszClassName = kFrameClass;
  RegisterClassW(&wc);
  wc.lpszClassName = kDocClass;
  RegisterClassW(&wc);
}

// Shows a topic in a popup near a point of the screen.
void ShowPopup(Viewer* v, const std::shared_ptr<File>& file, int topic, POINT at) {
  ClosePopup();
  if (topic < 0 || !file) return;
  RegisterClasses();
  if (g_hasPopupPos) {
    at = g_popupPos;
    g_hasPopupPos = false;
  }
  Pane* p = new Pane();
  p->viewer = v;
  p->popup = true;
  p->file = file;
  p->topic = topic;
  Topic& t = file->GetTopic(topic);
  p->popupRecords = t.nonScroll;
  p->popupRecords.insert(p->popupRecords.end(), t.scroll.begin(), t.scroll.end());
  p->records = &p->popupRecords;
  p->background = v->hasPopupColor ? v->popupColor : RGB(255, 255, 255);
  HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kPopupClass, L"", WS_POPUP | WS_BORDER, at.x, at.y,
                           100, 50, v->frame, NULL, GetModuleHandleW(NULL), p);
  if (!h) {
    delete p;
    return;
  }
  RECT work;
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  int maxW = std::min(480, (int)(work.right - work.left) * 2 / 3);
  LayoutPane(p, maxW);
  int w = std::min(maxW, p->layout.right + kMargin) + 2;
  if (w < maxW) LayoutPane(p, w);
  int hgt = p->layout.height + 2;
  int x = std::max((int)work.left, std::min(at.x, (int)work.right - w));
  int y = at.y + 2;
  if (y + hgt > work.bottom) y = std::max((int)work.top, at.y - hgt - 20);
  SetWindowPos(h, HWND_TOPMOST, x, y, w, hgt, SWP_SHOWWINDOW);
  g_popup = h;
  SetFocus(h);
  SetCapture(h);
}

// ---------------------------------------------------------------------
// Macros

// Splits a macro string into calls, and each call into its name and
// arguments. Arguments are quoted with "..." or `...', or bare.
struct Call {
  std::wstring name;
  std::vector<std::wstring> args;
};

// Reads macro calls separated by ':' or ';'.
std::vector<Call> ParseMacros(const std::wstring& s) {
  std::vector<Call> calls;
  size_t i = 0, n = s.size();
  while (i < n) {
    while (i < n && (iswspace(s[i]) || s[i] == L':' || s[i] == L';')) i++;
    if (i >= n) break;
    Call c;
    while (i < n && (iswalnum(s[i]) || s[i] == L'_')) c.name += s[i++];
    while (i < n && iswspace(s[i])) i++;
    if (c.name.empty()) {
      i++;
      continue;
    }
    if (i < n && s[i] == L'(') {
      i++;
      std::wstring arg;
      bool any = false;
      int depth = 0;
      while (i < n) {
        wchar_t ch = s[i];
        if (ch == L'"' || ch == L'`') {
          wchar_t close = ch == L'"' ? L'"' : L'\'';
          i++;
          int nest = 0;
          while (i < n) {
            if (ch == L'`' && s[i] == L'`') nest++;
            if (s[i] == close) {
              if (nest == 0) break;
              nest--;
            }
            if (s[i] == L'\\' && i + 1 < n) i++;
            arg += s[i++];
          }
          i++;
          any = true;
          continue;
        }
        if (ch == L'(') depth++;
        if (ch == L')') {
          if (depth == 0) {
            i++;
            break;
          }
          depth--;
        }
        if (ch == L',' && depth == 0) {
          c.args.push_back(arg);
          arg.clear();
          any = false;
          i++;
          continue;
        }
        if (!iswspace(ch) || depth) {
          arg += ch;
          any = true;
        }
        i++;
      }
      if (any || !c.args.empty()) c.args.push_back(arg);
    }
    calls.push_back(c);
  }
  return calls;
}

// A macro's argument, or empty.
std::wstring Arg(const Call& c, size_t i) { return i < c.args.size() ? c.args[i] : std::wstring(); }

// A number argument: decimal, or hexadecimal with 0x.
unsigned long Num(const std::wstring& s) { return wcstoul(s.c_str(), NULL, 0); }

// Whether a macro's name is one of its spellings.
bool Is(const Call& c, const wchar_t* full, const wchar_t* abbrev = NULL) {
  return _wcsicmp(c.name.c_str(), full) == 0 || (abbrev && _wcsicmp(c.name.c_str(), abbrev) == 0);
}

// The topics of keywords (separated by ';') of an index of the file.
std::vector<int> KeywordTopics(File* file, const std::wstring& keywords, wchar_t index) {
  std::vector<int> out;
  const auto& kws = file->Keywords(index);
  size_t i = 0;
  while (i <= keywords.size()) {
    size_t j = keywords.find(L';', i);
    if (j == std::wstring::npos) j = keywords.size();
    std::wstring k = keywords.substr(i, j - i);
    size_t a = k.find_first_not_of(L' '), b = k.find_last_not_of(L' ');
    k = a == std::wstring::npos ? std::wstring() : k.substr(a, b - a + 1);
    if (!k.empty())
      for (const auto& kw : kws)
        if (_wcsicmp(kw.first.c_str(), k.c_str()) == 0)
          for (uint32_t off : kw.second) {
            int t = file->TopicAt(off);
            if (t >= 0 && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
          }
    i = j + 1;
  }
  return out;
}

void ShowFound(Viewer* v, const std::vector<int>& topics);

// Shows the topics of keywords: the topic if there is one, a list of
// them if there are more, a message if none.
void ShowKeywordTopics(Viewer* v, const std::wstring& keywords, wchar_t index, bool quiet = false) {
  std::vector<int> topics = KeywordTopics(v->file.get(), keywords, index);
  if (topics.size() == 1) {
    ShowTopic(v, topics[0], -1);
  } else if (topics.size() > 1) {
    ShowFound(v, topics);
  } else if (!quiet) {
    ShowFinder(v, 1, keywords);
  }
}

// Evaluates the condition of IfThen and IfThenElse.
bool Condition(Viewer* v, const std::wstring& s) {
  std::vector<Call> calls = ParseMacros(s);
  if (calls.empty()) return false;
  const Call& c = calls[0];
  if (Is(c, L"Not")) return !Condition(v, Arg(c, 0));
  if (Is(c, L"TestKLink", L"KL")) return !KeywordTopics(v->file.get(), Arg(c, 0), L'K').empty();
  if (Is(c, L"TestALink", L"AL")) return !KeywordTopics(v->file.get(), Arg(c, 0), L'A').empty();
  if (Is(c, L"IsNotMark", L"NM")) return true;
  if (Is(c, L"IsBook")) return v->hasContents;
  return false;
}

// Rebuilds the button bar's windows.
void RebuildButtons(Viewer* v) {
  if (!v->frame) return;
  HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HDC dc = GetDC(v->frame);
  HGDIOBJ old = SelectObject(dc, font);
  int x = 4;
  for (size_t i = 0; i < v->buttons.size(); i++) {
    Button& b = v->buttons[i];
    std::wstring shown;
    for (wchar_t c : b.label)
      if (c != L'&') shown += c;
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, shown.c_str(), (int)shown.size(), &sz);
    int w = std::max(60, (int)sz.cx + 16);
    if (!b.hwnd) {
      b.hwnd = CreateWindowExW(0, L"BUTTON", b.label.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 4, w, 22,
                               v->frame, (HMENU)(INT_PTR)(kIdButton + i), GetModuleHandleW(NULL), NULL);
      SendMessageW(b.hwnd, WM_SETFONT, (WPARAM)font, FALSE);
    } else {
      SetWindowLongPtrW(b.hwnd, GWLP_ID, kIdButton + i);
      SetWindowPos(b.hwnd, NULL, x, 4, w, 22, SWP_NOZORDER);
    }
    EnableWindow(b.hwnd, b.enabled);
    x += w + 2;
  }
  SelectObject(dc, old);
  ReleaseDC(v->frame, dc);
}

// Finds a button by its identifier, or -1.
int FindButton(Viewer* v, const std::wstring& id) {
  for (size_t i = 0; i < v->buttons.size(); i++)
    if (_wcsicmp(v->buttons[i].id.c_str(), id.c_str()) == 0) return (int)i;
  return -1;
}

// Adds a button (CreateButton); replaces one of the same identifier.
void AddButton(Viewer* v, const std::wstring& id, const std::wstring& label, const std::wstring& macro) {
  int i = FindButton(v, id);
  if (i >= 0) {
    v->buttons[i].label = label;
    v->buttons[i].macro = macro;
    if (v->buttons[i].hwnd) SetWindowTextW(v->buttons[i].hwnd, label.c_str());
  } else {
    Button b;
    b.id = id;
    b.label = label;
    b.macro = macro;
    v->buttons.push_back(b);
  }
  RebuildButtons(v);
}

// Enables or disables the browse and Back buttons by the state.
void UpdateButtons(Viewer* v) {
  if (!v->frame) return;
  for (Button& b : v->buttons) {
    bool on = b.enabled;
    if (_wcsicmp(b.id.c_str(), L"BTN_BACK") == 0) on = on && !v->history.empty();
    if (v->topic >= 0 && v->topic < (int)v->file->TopicCount()) {
      Topic& t = v->file->GetTopic(v->topic);
      if (_wcsicmp(b.id.c_str(), L"BTN_PREV") == 0) on = on && t.browsePrev != 0xFFFFFFFF && t.browsePrev != 0xFFFF;
      if (_wcsicmp(b.id.c_str(), L"BTN_NEXT") == 0) on = on && t.browseNext != 0xFFFFFFFF && t.browseNext != 0xFFFF;
    }
    if (b.hwnd) EnableWindow(b.hwnd, on);
  }
}

// Jumps to a topic of a help file (the file may be another) and window.
void JumpTo(Viewer* v, const std::wstring& fileArg, int (*find)(File*, const std::wstring&),
            const std::wstring& what, bool popup) {
  std::wstring name = fileArg, window;
  size_t gt = name.find(L'>');
  if (gt != std::wstring::npos) {
    window = name.substr(gt + 1);
    name.resize(gt);
  }
  std::shared_ptr<File> file = v->file;
  if (!name.empty()) {
    std::string path = ResolveFile(v->file->Path(), name);
    if (path != v->file->Path()) file = OpenFile(path);
  }
  if (!file) {
    MessageBoxW(v->frame, L"Cannot find the help file.", kAppName, MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  int t = find(file.get(), what);
  if (t < 0) {
    MessageBoxW(v->frame, L"The topic does not exist.", kAppName, MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  if (popup) {
    POINT at;
    GetCursorPos(&at);
    ShowPopup(v, file, t, at);
    return;
  }
  Viewer* target = (file == v->file && window.empty()) ? v : GetViewer(file, window, true);
  if (target) ShowTopic(target, t, -1);
}

// Finds a topic by context name (for JumpId).
int FindByName(File* f, const std::wstring& s) { return f->TopicByName(s); }
// Finds a topic by [MAP] number (for JumpContext).
int FindByMap(File* f, const std::wstring& s) { return f->TopicByMap((uint32_t)Num(s)); }
// Finds a topic by context hash (for JumpHash).
int FindByHash(File* f, const std::wstring& s) { return f->TopicByHash((uint32_t)Num(s)); }
// The contents topic (for JumpContents).
int FindContents(File* f, const std::wstring&) { return f->ContentsTopic(); }

// Runs one macro call.
void RunCall(Viewer* v, const Call& c) {
  if (Is(c, L"About")) {
    std::wstring text = std::wstring(kAppName) + L"\n\n" + v->file->Title();
    if (!v->file->Copyright().empty()) text += L"\n" + v->file->Copyright();
    MessageBoxW(v->frame, text.c_str(), L"About Windows Help", MB_OK | MB_ICONINFORMATION);
  } else if (Is(c, L"Back", L"BK")) {
    if (!v->history.empty()) {
      Viewer::Place pl = v->history.back();
      v->history.pop_back();
      ShowTopic(v, pl.topic, -1, false);
      if (v->scroll) ScrollTo(v->scroll, pl.scrollY);
    }
  } else if (Is(c, L"BrowseButtons", L"BB")) {
    if (!v->browseButtons) {
      v->browseButtons = true;
      AddButton(v, L"BTN_PREV", L"<<", L"Prev()");
      AddButton(v, L"BTN_NEXT", L">>", L"Next()");
      UpdateButtons(v);
    }
  } else if (Is(c, L"Contents")) {
    ShowTopic(v, v->contentsOverride >= 0 ? v->contentsOverride : v->file->ContentsTopic(), -1);
  } else if (Is(c, L"JumpContents")) {
    JumpTo(v, Arg(c, 0), FindContents, L"", false);
  } else if (Is(c, L"JumpId", L"JI") || Is(c, L"JumpID")) {
    JumpTo(v, Arg(c, 0), FindByName, Arg(c, 1), false);
  } else if (Is(c, L"JumpContext", L"JC")) {
    JumpTo(v, Arg(c, 0), FindByMap, Arg(c, 1), false);
  } else if (Is(c, L"JumpHash", L"JH")) {
    JumpTo(v, Arg(c, 0), FindByHash, Arg(c, 1), false);
  } else if (Is(c, L"PopupId", L"PI")) {
    JumpTo(v, Arg(c, 0), FindByName, Arg(c, 1), true);
  } else if (Is(c, L"PopupContext", L"PC")) {
    JumpTo(v, Arg(c, 0), FindByMap, Arg(c, 1), true);
  } else if (Is(c, L"PopupHash", L"PH")) {
    JumpTo(v, Arg(c, 0), FindByHash, Arg(c, 1), true);
  } else if (Is(c, L"JumpKeyword", L"JK")) {
    ShowKeywordTopics(v, Arg(c, 1), L'K');
  } else if (Is(c, L"KLink", L"KL")) {
    ShowKeywordTopics(v, Arg(c, 0), L'K');
  } else if (Is(c, L"ALink", L"AL")) {
    ShowKeywordTopics(v, Arg(c, 0), L'A');
  } else if (Is(c, L"Next")) {
    if (v->topic >= 0) {
      uint32_t n = v->file->GetTopic(v->topic).browseNext;
      int t = n == 0xFFFFFFFF ? -1 : v->file->TopicAt(n);
      if (t >= 0) ShowTopic(v, t, -1);
    }
  } else if (Is(c, L"Prev")) {
    if (v->topic >= 0) {
      uint32_t n = v->file->GetTopic(v->topic).browsePrev;
      int t = n == 0xFFFFFFFF ? -1 : v->file->TopicAt(n);
      if (t >= 0) ShowTopic(v, t, -1);
    }
  } else if (Is(c, L"Finder", L"FD") || Is(c, L"Search")) {
    ShowFinder(v, Is(c, L"Search") ? 1 : 0, std::wstring());
  } else if (Is(c, L"Find")) {
    ShowFinder(v, 2, std::wstring());
  } else if (Is(c, L"CloseWindow", L"CW") || Is(c, L"Exit")) {
    if (v->frame) PostMessageW(v->frame, WM_CLOSE, 0, 0);
  } else if (Is(c, L"CopyTopic", L"CT")) {
    if (v->frame) PostMessageW(v->frame, WM_COMMAND, kIdCopy, 0);
  } else if (Is(c, L"CreateButton", L"CB")) {
    AddButton(v, Arg(c, 0), Arg(c, 1), Arg(c, 2));
  } else if (Is(c, L"ChangeButtonBinding", L"CBB") || Is(c, L"ChangeEnable", L"CE")) {
    int i = FindButton(v, Arg(c, 0));
    if (i >= 0) {
      v->buttons[i].macro = Arg(c, 1);
      if (Is(c, L"ChangeEnable", L"CE")) v->buttons[i].enabled = true;
      UpdateButtons(v);
    }
  } else if (Is(c, L"DestroyButton")) {
    int i = FindButton(v, Arg(c, 0));
    if (i >= 0) {
      if (v->buttons[i].hwnd) DestroyWindow(v->buttons[i].hwnd);
      v->buttons.erase(v->buttons.begin() + i);
      RebuildButtons(v);
    }
  } else if (Is(c, L"DisableButton", L"DB") || Is(c, L"EnableButton", L"EB")) {
    int i = FindButton(v, Arg(c, 0));
    if (i >= 0) {
      v->buttons[i].enabled = Is(c, L"EnableButton", L"EB");
      UpdateButtons(v);
    }
  } else if (Is(c, L"ExecFile", L"EF") || Is(c, L"ShellExecute", L"SE")) {
    std::wstring prog = Arg(c, 0), args = Arg(c, 1);
    ShellExecuteW(v->frame, NULL, prog.c_str(), args.empty() ? NULL : args.c_str(), NULL, SW_SHOWNORMAL);
  } else if (Is(c, L"ExecProgram", L"EP")) {
    std::wstring cmd = Arg(c, 0);
    size_t sp = cmd.find(L' ');
    std::wstring prog = cmd.substr(0, sp), args = sp == std::wstring::npos ? std::wstring() : cmd.substr(sp + 1);
    ShellExecuteW(v->frame, NULL, prog.c_str(), args.empty() ? NULL : args.c_str(), NULL, SW_SHOWNORMAL);
  } else if (Is(c, L"SetContents", L"SC")) {
    std::shared_ptr<File> f = v->file;
    int t = f->TopicByMap((uint32_t)Num(Arg(c, 1)));
    if (t >= 0) v->contentsOverride = t;
  } else if (Is(c, L"SetPopupColor", L"SPC")) {
    unsigned long col = c.args.size() >= 3 ? RGB(Num(Arg(c, 0)), Num(Arg(c, 1)), Num(Arg(c, 2))) : Num(Arg(c, 0));
    v->popupColor = (COLORREF)(col & 0xFFFFFF);
    v->hasPopupColor = true;
  } else if (Is(c, L"IfThen", L"IT")) {
    if (Condition(v, Arg(c, 0))) RunMacros(v, Arg(c, 1));
  } else if (Is(c, L"IfThenElse", L"IE")) {
    RunMacros(v, Condition(v, Arg(c, 0)) ? Arg(c, 1) : Arg(c, 2));
  } else if (Is(c, L"FileOpen", L"FO")) {
    if (v->frame) PostMessageW(v->frame, WM_COMMAND, kIdOpen, 0);
  } else if (Is(c, L"PositionWindow", L"PW")) {
    if (v->frame && c.args.size() >= 4) {
      int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
      SetWindowPos(v->frame, NULL, (int)Num(Arg(c, 0)) * sw / 1024, (int)Num(Arg(c, 1)) * sh / 1024,
                   (int)Num(Arg(c, 2)) * sw / 1024, (int)Num(Arg(c, 3)) * sh / 1024, SWP_NOZORDER);
    }
  } else if (Is(c, L"HelpOn")) {
    MessageBoxW(v->frame,
                L"Click a green word or picture to go to another topic. Use Back to return, and Help Topics "
                L"(or Contents and Index) to find a topic.",
                kAppName, MB_OK | MB_ICONINFORMATION);
  }
  // The rest (DLL routines, menus, bookmarks, printing, training cards)
  // do nothing here.
}

// Runs macros: a string of calls.
void RunMacros(Viewer* v, const std::wstring& macros) {
  for (const Call& c : ParseMacros(macros)) RunCall(v, c);
}

// Follows a hotspot: a jump, a popup or a macro.
void Follow(Viewer* v, const Link& link, POINT at) {
  if (link.kind == Link::kMacro) {
    RunMacros(v, link.macro);
    return;
  }
  std::shared_ptr<File> file = v->file;
  if (!link.file.empty()) {
    std::string path = ResolveFile(v->file->Path(), link.file);
    file = OpenFile(path);
    if (!file) {
      MessageBoxW(v->frame, L"Cannot find the help file.", kAppName, MB_OK | MB_ICONEXCLAMATION);
      return;
    }
  }
  int record = -1;
  int t = file->TopicOfLink(link, &record);
  if (t < 0) {
    MessageBoxW(v->frame, L"The topic does not exist.", kAppName, MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  if (link.kind == Link::kPopup) {
    ShowPopup(v, file, t, at);
    return;
  }
  Viewer* target = v;
  if (file != v->file || !link.window.empty()) target = GetViewer(file, link.window, true);
  if (target) ShowTopic(target, t, record);
}

// ---------------------------------------------------------------------
// The windows

// Places the frame's button bar and panes.
void LayoutFrame(Viewer* v) {
  if (!v->frame || !v->scroll) return;
  RECT rc;
  GetClientRect(v->frame, &rc);
  int top = v->buttons.empty() ? 0 : kBarHeight;
  int w = rc.right - rc.left;
  int nsH = 0;
  if (v->ns->records && !v->ns->records->empty()) {
    if (v->ns->width != w) LayoutPane(v->ns, w);
    nsH = std::min(v->ns->layout.height + 2, (int)(rc.bottom - top) / 2);
  }
  SetWindowPos(v->ns->hwnd, NULL, 0, top, w, nsH, SWP_NOZORDER | (nsH ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
  SetWindowPos(v->scroll->hwnd, NULL, 0, top + nsH, w, std::max(0, (int)rc.bottom - top - nsH), SWP_NOZORDER);
}

// Sets the panes' colors from the window's definition.
void SetColors(Viewer* v) {
  v->scroll->background = v->hasDef && v->def.hasColor ? v->def.color : RGB(255, 255, 255);
  v->ns->background = v->hasDef && v->def.hasNsrColor ? v->def.nsrColor : v->scroll->background;
}

// Shows a topic in a viewer's window, and runs its macros.
void ShowTopic(Viewer* v, int topic, int record, bool remember) {
  if (!v->frame || topic < 0 || topic >= (int)v->file->TopicCount()) return;
  ClosePopup();
  if (remember && v->topic >= 0 && v->topic != topic) v->history.push_back({v->topic, v->scroll->scrollY});
  if (v->history.size() > 200) v->history.erase(v->history.begin());
  v->topic = topic;
  Topic& t = v->file->GetTopic(topic);
  v->ns->topic = v->scroll->topic = topic;
  v->ns->records = &t.nonScroll;
  v->scroll->records = &t.scroll;
  v->ns->width = v->scroll->width = -1;
  v->scroll->scrollY = 0;
  LayoutFrame(v);
  RECT rc;
  GetClientRect(v->scroll->hwnd, &rc);
  LayoutPane(v->scroll, rc.right - rc.left);
  if (record >= 0 && record < (int)v->scroll->layout.recordTop.size())
    v->scroll->scrollY = v->scroll->layout.recordTop[record];
  UpdateScroll(v->scroll);
  InvalidateRect(v->ns->hwnd, NULL, FALSE);
  InvalidateRect(v->scroll->hwnd, NULL, FALSE);
  UpdateButtons(v);
  if (!IsWindowVisible(v->frame)) ShowWindow(v->frame, v->hasDef && v->def.maximize ? SW_SHOWMAXIMIZED : SW_SHOW);
  SetForegroundWindow(v->frame);
  SetFocus(v->scroll->hwnd);
  for (const std::wstring& m : t.macros) RunMacros(v, m);
}

// Frees a viewer whose window is gone.
void DropViewer(Viewer* v) {
  for (auto it = g_viewers.begin(); it != g_viewers.end(); ++it)
    if (it->get() == v) {
      g_viewers.erase(it);
      return;
    }
}

// The main and secondary windows' procedure.
LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Viewer* v = (Viewer*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (msg) {
    case WM_NCCREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      v = (Viewer*)cs->lpCreateParams;
      v->frame = h;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)v);
      break;
    }
    case WM_CREATE: {
      v->ns = new Pane();
      v->scroll = new Pane();
      v->ns->viewer = v->scroll->viewer = v;
      v->ns->file = v->scroll->file = v->file;
      v->ns->nonScroll = true;
      SetColors(v);
      CreateWindowExW(0, kPaneClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)(INT_PTR)kIdNsPane,
                      GetModuleHandleW(NULL), v->ns);
      CreateWindowExW(0, kPaneClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, h,
                      (HMENU)(INT_PTR)kIdScrollPane, GetModuleHandleW(NULL), v->scroll);
      return 0;
    }
    case WM_SIZE:
      if (v) LayoutFrame(v);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      if (v && !v->buttons.empty()) {
        RECT rc;
        GetClientRect(h, &rc);
        RECT line = {0, kBarHeight - 1, rc.right, kBarHeight};
        FillRect(dc, &line, (HBRUSH)(COLOR_BTNSHADOW + 1));
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ACTIVATE:
      if (v && LOWORD(wp) != WA_INACTIVE && v->scroll && v->scroll->hwnd) SetFocus(v->scroll->hwnd);
      return 0;
    case WM_COMMAND: {
      if (!v) break;
      int id = LOWORD(wp);
      if (id >= kIdButton && id < kIdButton + (int)v->buttons.size()) {
        Button& b = v->buttons[id - kIdButton];
        if (_wcsicmp(b.id.c_str(), L"BTN_OPTIONS") == 0) {
          RECT rc;
          GetWindowRect(b.hwnd, &rc);
          HMENU m = CreatePopupMenu();
          AppendMenuW(m, MF_STRING, kIdCopy, L"&Copy");
          AppendMenuW(m, MF_SEPARATOR, 0, NULL);
          AppendMenuW(m, MF_STRING | (v->zoom < 100 ? MF_CHECKED : 0), kIdFontSmall, L"Font &Small");
          AppendMenuW(m, MF_STRING | (v->zoom == 100 ? MF_CHECKED : 0), kIdFontNormal, L"Font &Normal");
          AppendMenuW(m, MF_STRING | (v->zoom > 100 ? MF_CHECKED : 0), kIdFontLarge, L"Font &Large");
          AppendMenuW(m, MF_STRING | (v->onTop ? MF_CHECKED : 0), kIdOnTop, L"Keep Help on &Top");
          TrackPopupMenu(m, 0, rc.left, rc.bottom, 0, h, NULL);
          DestroyMenu(m);
        } else {
          std::wstring macro = b.macro;
          RunMacros(v, macro);
        }
        if (v->scroll && v->scroll->hwnd && GetFocus() != v->scroll->hwnd && !v->topicsDialog)
          SetFocus(v->scroll->hwnd);
        return 0;
      }
      switch (id) {
        case kIdOpen: {
          wchar_t name[MAX_PATH] = L"";
          OPENFILENAMEW ofn;
          memset(&ofn, 0, sizeof(ofn));
          ofn.lStructSize = sizeof(ofn);
          ofn.hwndOwner = h;
          ofn.lpstrFilter = L"Help Files (*.hlp)\0*.hlp;*.HLP\0All Files (*.*)\0*.*\0";
          ofn.lpstrFile = name;
          ofn.nMaxFile = MAX_PATH;
          ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
          if (GetOpenFileNameW(&ofn)) {
            std::shared_ptr<File> f = OpenFile(PosixPath(name));
            if (!f) {
              MessageBoxW(h, L"This is not a Windows Help file.", kAppName, MB_OK | MB_ICONEXCLAMATION);
            } else if (Viewer* nv = GetViewer(f, std::wstring(), true)) {
              ShowTopic(nv, f->ContentsTopic(), -1);
            }
          }
          return 0;
        }
        case kIdExit: PostMessageW(h, WM_CLOSE, 0, 0); return 0;
        case kIdCopy: {
          std::wstring t = PaneText(v->ns) + PaneText(v->scroll);
          CopyText(h, t);
          return 0;
        }
        case kIdOnTop:
          v->onTop = !v->onTop;
          SetWindowPos(h, v->onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          CheckMenuItem(GetMenu(h), kIdOnTop, v->onTop ? MF_CHECKED : MF_UNCHECKED);
          return 0;
        case kIdFontSmall:
        case kIdFontNormal:
        case kIdFontLarge: {
          v->zoom = id == kIdFontSmall ? 80 : id == kIdFontLarge ? 125 : 100;
          v->fonts.Clear();
          HMENU menu = GetMenu(h);
          for (int k = kIdFontSmall; k <= kIdFontLarge; k++)
            CheckMenuItem(menu, k, k == id ? MF_CHECKED : MF_UNCHECKED);
          if (v->topic >= 0) {
            int y = v->scroll->scrollY;
            v->ns->width = v->scroll->width = -1;
            LayoutFrame(v);
            RECT rc;
            GetClientRect(v->scroll->hwnd, &rc);
            LayoutPane(v->scroll, rc.right - rc.left);
            v->scroll->scrollY = y;
            UpdateScroll(v->scroll);
            InvalidateRect(v->ns->hwnd, NULL, FALSE);
            InvalidateRect(v->scroll->hwnd, NULL, FALSE);
          }
          return 0;
        }
        case kIdTopics: ShowFinder(v, v->hasContents ? 0 : 1, std::wstring()); return 0;
        case kIdAbout: RunMacros(v, L"About()"); return 0;
      }
      break;
    }
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      if (v) {
        ClosePopup();
        if (v->topicsDialog) DestroyWindow(v->topicsDialog);
        if (v->foundDialog) DestroyWindow(v->foundDialog);
      }
      return 0;
    case WM_NCDESTROY:
      if (v) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete v->ns;
        delete v->scroll;
        v->ns = v->scroll = nullptr;
        v->frame = NULL;
        DropViewer(v);
      }
      break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// Makes the main window's menu.
HMENU MakeMenu() {
  HMENU bar = CreateMenu();
  HMENU file = CreatePopupMenu();
  AppendMenuW(file, MF_STRING, kIdOpen, L"&Open...");
  AppendMenuW(file, MF_SEPARATOR, 0, NULL);
  AppendMenuW(file, MF_STRING, kIdExit, L"E&xit");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
  HMENU edit = CreatePopupMenu();
  AppendMenuW(edit, MF_STRING, kIdCopy, L"&Copy\tCtrl+C");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
  HMENU opts = CreatePopupMenu();
  AppendMenuW(opts, MF_STRING, kIdOnTop, L"Keep Help on &Top");
  AppendMenuW(opts, MF_SEPARATOR, 0, NULL);
  AppendMenuW(opts, MF_STRING, kIdFontSmall, L"Font &Small");
  AppendMenuW(opts, MF_STRING | MF_CHECKED, kIdFontNormal, L"Font &Normal");
  AppendMenuW(opts, MF_STRING, kIdFontLarge, L"Font &Large");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)opts, L"&Options");
  HMENU help = CreatePopupMenu();
  AppendMenuW(help, MF_STRING, kIdTopics, L"Help &Topics");
  AppendMenuW(help, MF_SEPARATOR, 0, NULL);
  AppendMenuW(help, MF_STRING, kIdAbout, L"&About Windows Help...");
  AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
  return bar;
}

// Finds the viewer of a help file's window, or makes it.
Viewer* GetViewer(const std::shared_ptr<File>& file, const std::wstring& window, bool create) {
  std::wstring name = window;
  if (_wcsicmp(name.c_str(), L"main") == 0) name.clear();
  for (auto& v : g_viewers)
    if (v->file == file && _wcsicmp(v->windowName.c_str(), name.c_str()) == 0) return v.get();
  if (!create) return nullptr;
  RegisterClasses();
  std::unique_ptr<Viewer> v(new Viewer());
  v->file = file;
  v->windowName = name;
  for (const WindowDef& d : file->Windows())
    if (_wcsicmp(d.name.c_str(), name.empty() ? L"main" : name.c_str()) == 0) {
      v->def = d;
      v->hasDef = true;
    }
  // The contents file: the one |SYSTEM names, else the same name as .cnt.
  std::string dir = DirOf(file->Path());
  std::string cnt;
  if (!file->CntName().empty()) {
    std::string n = PosixPath(file->CntName().c_str());
    size_t slash = n.rfind('/');
    cnt = FindNoCase(dir, slash == std::string::npos ? n : n.substr(slash + 1));
  }
  if (cnt.empty()) {
    std::string base = file->Path();
    size_t dot = base.rfind('.');
    if (dot != std::string::npos && dot > base.rfind('/')) base.resize(dot);
    size_t slash = base.rfind('/');
    std::string leaf = slash == std::string::npos ? base : base.substr(slash + 1);
    cnt = FindNoCase(dir, leaf + ".cnt");
  }
  if (!cnt.empty()) v->hasContents = ReadContents(cnt, &v->contents) && !v->contents.entries.empty();
  // The window: its caption, place and size.
  std::wstring caption = v->hasDef && !v->def.caption.empty() ? v->def.caption : file->Title();
  if (caption.empty()) caption = kAppName;
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT, cx = std::min(640, sw * 2 / 3), cy = std::min(560, sh * 3 / 4);
  if (v->hasDef && v->def.hasPos && v->def.cx > 0 && v->def.cy > 0) {
    x = v->def.x * sw / 1024;
    y = v->def.y * sh / 1024;
    cx = v->def.cx * sw / 1024;
    cy = v->def.cy * sh / 1024;
  } else if (sw > 0) {
    x = std::max(0, sw - cx - 40);
    y = 40;
  }
  bool main = name.empty();
  Viewer* raw = v.get();
  g_viewers.push_back(std::move(v));
  HWND h = CreateWindowExW(raw->hasDef && raw->def.onTop ? WS_EX_TOPMOST : 0, main ? kFrameClass : kDocClass,
                           caption.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, cx, cy, NULL,
                           main ? MakeMenu() : NULL, GetModuleHandleW(NULL), raw);
  if (!h) {
    DropViewer(raw);
    return nullptr;
  }
  if (main) {
    if (raw->hasContents) {
      AddButton(raw, L"BTN_TOPICS", L"Help &Topics", L"Finder()");
      AddButton(raw, L"BTN_BACK", L"&Back", L"Back()");
      AddButton(raw, L"BTN_OPTIONS", L"&Options", L"");
    } else {
      AddButton(raw, L"BTN_CONTENTS", L"&Contents", L"Contents()");
      AddButton(raw, L"BTN_SEARCH", L"&Index", L"Search()");
      AddButton(raw, L"BTN_BACK", L"&Back", L"Back()");
    }
    for (const std::wstring& m : file->ConfigMacros()) RunMacros(raw, m);
    raw->configRun = true;
  }
  LayoutFrame(raw);
  return raw;
}

// ---------------------------------------------------------------------
// The Help Topics dialog box and the Topics Found list

// The Help Topics dialog box's state.
struct Finder {
  Viewer* viewer;
  int tab;
  std::wstring text;
  std::vector<int> indexMap;  // list row -> keyword
  std::vector<int> findMap;   // list row -> topic
  std::vector<int> tabs;      // tab -> page (0 contents, 1 index, 2 find)
  HIMAGELIST images = NULL;   // the Contents tab's books and pages
};

// Shows the controls of a page of the Help Topics dialog box.
void ShowPage(HWND h, Finder* f, int page) {
  static const int pages[3][4] = {{kIdTree, 0, 0, 0},
                                  {kIdIndexLabel1, kIdIndexEdit, kIdIndexLabel2, kIdIndexList},
                                  {kIdFindLabel1, kIdFindEdit, kIdFindLabel2, kIdFindList}};
  for (int p = 0; p < 3; p++)
    for (int id : pages[p])
      if (id) ShowWindow(GetDlgItem(h, id), p == page ? SW_SHOW : SW_HIDE);
  f->tab = page;
  if (page == 0) SetFocus(GetDlgItem(h, kIdTree));
  if (page == 1) SetFocus(GetDlgItem(h, kIdIndexEdit));
  if (page == 2) SetFocus(GetDlgItem(h, kIdFindEdit));
}

// Makes the Contents tab's pictures: a closed book, an open book and a
// page with a question mark, 16 pixels square.
HIMAGELIST BookImages() {
  HDC screen = GetDC(NULL);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bm = CreateCompatibleBitmap(screen, 48, 16);
  HGDIOBJ old = SelectObject(dc, bm);
  const COLORREF key = RGB(255, 0, 255);
  RECT all = {0, 0, 48, 16};
  HBRUSH keyBrush = CreateSolidBrush(key);
  FillRect(dc, &all, keyBrush);
  DeleteObject(keyBrush);
  // Fills a rectangle with a color.
  auto fill = [&](int l, int t, int r, int b, COLORREF c) {
    RECT rc = {l, t, r, b};
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &rc, br);
    DeleteObject(br);
  };
  const COLORREF cover = RGB(128, 0, 128), dark = RGB(0, 0, 0), paper = RGB(255, 255, 255);
  // The closed book: a cover, its pages' edge and its spine.
  fill(3, 2, 14, 14, dark);
  fill(4, 3, 13, 12, cover);
  fill(4, 12, 13, 13, paper);
  fill(3, 2, 5, 14, dark);
  // The open book: two pages spread from the spine.
  fill(16 + 1, 4, 16 + 8, 13, dark);
  fill(16 + 8, 4, 16 + 15, 13, dark);
  fill(16 + 2, 5, 16 + 7, 12, paper);
  fill(16 + 9, 5, 16 + 14, 12, paper);
  fill(16 + 1, 13, 16 + 15, 14, cover);
  // The page: a sheet with a question mark.
  fill(32 + 3, 1, 32 + 13, 15, dark);
  fill(32 + 4, 2, 32 + 12, 14, paper);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(0, 0, 128));
  HFONT f = CreateFontW(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
  HGDIOBJ of = SelectObject(dc, f);
  RECT q = {32 + 4, 2, 32 + 12, 14};
  DrawTextW(dc, L"?", 1, &q, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, of);
  DeleteObject(f);
  SelectObject(dc, old);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
  HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 3, 0);
  ImageList_AddMasked(il, bm, key);
  DeleteObject(bm);
  return il;
}

// Fills the Contents tab's tree from the contents file.
void FillTree(HWND tree, Viewer* v) {
  std::vector<HTREEITEM> parents(10, TVI_ROOT);
  for (size_t i = 0; i < v->contents.entries.size(); i++) {
    const CntEntry& e = v->contents.entries[i];
    int level = std::max(1, std::min(e.level, 9));
    TVINSERTSTRUCTW is;
    memset(&is, 0, sizeof(is));
    is.hParent = parents[level - 1];
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    is.item.iImage = is.item.iSelectedImage = e.book ? 0 : 2;
    is.item.pszText = (LPWSTR)e.text.c_str();
    is.item.lParam = (LPARAM)i;
    HTREEITEM item = (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&is);
    if (e.book && level < 9) {
      parents[level] = item;
      for (int k = level + 1; k < 10; k++) parents[k] = item;
    }
  }
}

// Fills the Index tab's list with the keywords. A keyword "a, b" after
// "a" shows as "b", indented.
void FillIndex(HWND list, Finder* f) {
  const auto& kws = f->viewer->file->Keywords(L'K');
  std::wstring root;
  for (size_t i = 0; i < kws.size(); i++) {
    const std::wstring& k = kws[i].first;
    std::wstring shown = k;
    size_t comma = k.find(L", ");
    if (comma == std::wstring::npos) comma = k.find(L": ");
    if (comma != std::wstring::npos && !root.empty() && _wcsicmp(k.substr(0, comma).c_str(), root.c_str()) == 0)
      shown = L"    " + k.substr(comma + 2);
    else
      root = comma == std::wstring::npos ? k : k.substr(0, comma);
    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)shown.c_str());
    f->indexMap.push_back((int)i);
  }
}

// Selects the first keyword that starts with the text typed.
void SelectKeyword(HWND h, Finder* f) {
  wchar_t buf[256];
  GetDlgItemTextW(h, kIdIndexEdit, buf, 256);
  std::wstring typed = Lower(buf);
  if (typed.empty()) return;
  const auto& kws = f->viewer->file->Keywords(L'K');
  for (size_t row = 0; row < f->indexMap.size(); row++) {
    std::wstring k = Lower(kws[f->indexMap[row]].first);
    if (k.compare(0, typed.size(), typed) >= 0) {
      SendDlgItemMessageW(h, kIdIndexList, LB_SETCURSEL, row, 0);
      SendDlgItemMessageW(h, kIdIndexList, LB_SETTOPINDEX, row, 0);
      return;
    }
  }
}

// Fills the Find tab's list with the topics that have all the words typed.
void FillFind(HWND h, Finder* f) {
  Viewer* v = f->viewer;
  if (v->plain.empty())
    for (size_t i = 0; i < v->file->TopicCount(); i++)
      v->plain.push_back(Lower(v->file->GetTopic(i).title + L"\n" + v->file->PlainText(i)));
  wchar_t buf[256];
  GetDlgItemTextW(h, kIdFindEdit, buf, 256);
  std::vector<std::wstring> words;
  std::wstring w;
  for (const wchar_t* s = buf;; s++) {
    if (!*s || iswspace(*s)) {
      if (!w.empty()) words.push_back(Lower(w));
      w.clear();
      if (!*s) break;
    } else {
      w += *s;
    }
  }
  HWND list = GetDlgItem(h, kIdFindList);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  f->findMap.clear();
  if (words.empty()) return;
  for (size_t i = 0; i < v->plain.size(); i++) {
    bool all = true;
    for (const auto& word : words)
      if (v->plain[i].find(word) == std::wstring::npos) {
        all = false;
        break;
      }
    const std::wstring& title = v->file->GetTopic(i).title;
    if (!all || title.empty()) continue;
    SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)title.c_str());
    f->findMap.push_back((int)i);
  }
}

// Shows what the Help Topics dialog box has selected; whether it did.
bool DisplaySelected(HWND h, Finder* f) {
  Viewer* v = f->viewer;
  if (f->tab == 0) {
    HWND tree = GetDlgItem(h, kIdTree);
    HTREEITEM item = (HTREEITEM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    if (!item) return false;
    TVITEMW ti;
    memset(&ti, 0, sizeof(ti));
    ti.mask = TVIF_PARAM;
    ti.hItem = item;
    SendMessageW(tree, TVM_GETITEMW, 0, (LPARAM)&ti);
    const CntEntry& e = v->contents.entries[ti.lParam];
    if (e.book) {
      SendMessageW(tree, TVM_EXPAND, TVE_TOGGLE, (LPARAM)item);
      return false;
    }
    if (!e.context.empty() && e.context[0] == L'!') {
      RunMacros(v, e.context.substr(1));
      return true;
    }
    std::wstring file = e.file;
    if (!e.window.empty()) file += L">" + e.window;
    JumpTo(v, file, FindByName, e.context, false);
    return true;
  }
  if (f->tab == 1) {
    int row = (int)SendDlgItemMessageW(h, kIdIndexList, LB_GETCURSEL, 0, 0);
    if (row < 0 || row >= (int)f->indexMap.size()) return false;
    const auto& kw = v->file->Keywords(L'K')[f->indexMap[row]];
    std::vector<int> topics;
    for (uint32_t off : kw.second) {
      int t = v->file->TopicAt(off);
      if (t >= 0 && std::find(topics.begin(), topics.end(), t) == topics.end()) topics.push_back(t);
    }
    if (topics.size() == 1) ShowTopic(v, topics[0], -1);
    if (topics.size() > 1) ShowFound(v, topics);
    return !topics.empty();
  }
  int row = (int)SendDlgItemMessageW(h, kIdFindList, LB_GETCURSEL, 0, 0);
  if (row < 0 || row >= (int)f->findMap.size()) return false;
  ShowTopic(v, f->findMap[row], -1);
  return true;
}

// The Help Topics dialog box's procedure.
INT_PTR CALLBACK FinderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Finder* f = (Finder*)GetWindowLongPtrW(h, DWLP_USER);
  switch (msg) {
    case WM_INITDIALOG: {
      f = (Finder*)lp;
      SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)f);
      HWND tab = GetDlgItem(h, kIdTab);
      int n = 0;
      // Inserts a tab of the page.
      auto add = [&](const wchar_t* text, int page) {
        TCITEMW ti;
        memset(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)text;
        SendMessageW(tab, TCM_INSERTITEMW, n++, (LPARAM)&ti);
        f->tabs.push_back(page);
      };
      if (f->viewer->hasContents) add(L"Contents", 0);
      add(L"Index", 1);
      add(L"Find", 2);
      if (f->viewer->hasContents) {
        f->images = BookImages();
        SendDlgItemMessageW(h, kIdTree, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)f->images);
        FillTree(GetDlgItem(h, kIdTree), f->viewer);
      }
      FillIndex(GetDlgItem(h, kIdIndexList), f);
      int page = f->tab;
      if (page == 0 && !f->viewer->hasContents) page = 1;
      for (size_t i = 0; i < f->tabs.size(); i++)
        if (f->tabs[i] == page) SendMessageW(tab, TCM_SETCURSEL, i, 0);
      ShowPage(h, f, page);
      if (!f->text.empty()) {
        SetDlgItemTextW(h, page == 2 ? kIdFindEdit : kIdIndexEdit, f->text.c_str());
        if (page == 1) SelectKeyword(h, f);
      }
      std::wstring title = L"Help Topics: " + f->viewer->file->Title();
      SetWindowTextW(h, title.c_str());
      return FALSE;
    }
    case WM_NOTIFY: {
      NMHDR* nm = (NMHDR*)lp;
      if (nm->idFrom == kIdTab && nm->code == TCN_SELCHANGE) {
        int i = (int)SendMessageW(nm->hwndFrom, TCM_GETCURSEL, 0, 0);
        if (i >= 0 && i < (int)f->tabs.size()) ShowPage(h, f, f->tabs[i]);
      }
      if (nm->idFrom == kIdTree && nm->code == NM_DBLCLK && DisplaySelected(h, f)) DestroyWindow(h);
      if (nm->idFrom == kIdTree && nm->code == TVN_ITEMEXPANDEDW) {
        // A book shows open while its pages show.
        NMTREEVIEWW* tv = (NMTREEVIEWW*)lp;
        TVITEMW ti;
        memset(&ti, 0, sizeof(ti));
        ti.mask = TVIF_HANDLE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        ti.hItem = tv->itemNew.hItem;
        ti.iImage = ti.iSelectedImage = tv->action == TVE_EXPAND ? 1 : 0;
        SendMessageW(nm->hwndFrom, TVM_SETITEMW, 0, (LPARAM)&ti);
      }
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDOK:
          if (DisplaySelected(h, f)) DestroyWindow(h);
          return TRUE;
        case IDCANCEL: DestroyWindow(h); return TRUE;
        case kIdIndexEdit:
          if (HIWORD(wp) == EN_CHANGE) SelectKeyword(h, f);
          return TRUE;
        case kIdFindEdit:
          if (HIWORD(wp) == EN_CHANGE) FillFind(h, f);
          return TRUE;
        case kIdIndexList:
        case kIdFindList:
          if (HIWORD(wp) == LBN_DBLCLK && DisplaySelected(h, f)) DestroyWindow(h);
          return TRUE;
      }
      break;
    case WM_CLOSE: DestroyWindow(h); return TRUE;
    case WM_DESTROY:
      if (f) {
        if (f->viewer->topicsDialog == h) f->viewer->topicsDialog = NULL;
        if (f->images) ImageList_Destroy(f->images);
        delete f;
        SetWindowLongPtrW(h, DWLP_USER, 0);
      }
      return TRUE;
  }
  return FALSE;
}

// Opens the Help Topics dialog box at a tab (0 Contents, 1 Index,
// 2 Find), with text typed in it.
void ShowFinder(Viewer* v, int tab, const std::wstring& text) {
  if (!v->frame) return;
  if (v->topicsDialog) DestroyWindow(v->topicsDialog);
  Template t(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 272, 232, L"Help Topics");
  t.Add(WC_TABCONTROLW, WS_TABSTOP, 4, 4, 264, 202, kIdTab, L"");
  t.Add(WC_TREEVIEWW, WS_BORDER | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        10, 22, 252, 178, kIdTree, L"");
  t.Add(Template::kStatic, SS_LEFT, 10, 22, 252, 8, kIdIndexLabel1, L"&1  Type the first few letters of the word you're looking for.");
  t.Add(Template::kEdit, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 32, 252, 12, kIdIndexEdit, L"");
  t.Add(Template::kStatic, SS_LEFT, 10, 50, 252, 8, kIdIndexLabel2, L"&2  Click the index entry you want, and then click Display.");
  t.Add(Template::kListBox, WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 10, 60, 252,
        140, kIdIndexList, L"");
  t.Add(Template::kStatic, SS_LEFT, 10, 22, 252, 8, kIdFindLabel1, L"&1  Type the word(s) you want to find.");
  t.Add(Template::kEdit, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 32, 252, 12, kIdFindEdit, L"");
  t.Add(Template::kStatic, SS_LEFT, 10, 50, 252, 8, kIdFindLabel2, L"&2  Click a topic, and then click Display.");
  t.Add(Template::kListBox, WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 10, 60, 252,
        140, kIdFindList, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 156, 212, 54, 14, IDOK, L"&Display");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 214, 212, 54, 14, IDCANCEL, L"Cancel");
  Finder* f = new Finder();
  f->viewer = v;
  f->tab = tab;
  f->text = text;
  v->topicsDialog = CreateDialogIndirectParamW(GetModuleHandleW(NULL), t.Get(), v->frame, FinderProc, (LPARAM)f);
  if (!v->topicsDialog) {
    delete f;
    return;
  }
  ShowWindow(v->topicsDialog, SW_SHOW);
}

// The state of the Topics Found list.
struct Found {
  Viewer* viewer;
  std::vector<int> topics;
};

// The Topics Found dialog box's procedure.
INT_PTR CALLBACK FoundProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Found* f = (Found*)GetWindowLongPtrW(h, DWLP_USER);
  switch (msg) {
    case WM_INITDIALOG: {
      f = (Found*)lp;
      SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)f);
      for (int t : f->topics) {
        std::wstring title = f->viewer->file->GetTopic(t).title;
        if (title.empty()) title = L"(untitled)";
        SendDlgItemMessageW(h, kIdFoundList, LB_ADDSTRING, 0, (LPARAM)title.c_str());
      }
      SendDlgItemMessageW(h, kIdFoundList, LB_SETCURSEL, 0, 0);
      SetFocus(GetDlgItem(h, kIdFoundList));
      return FALSE;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || (LOWORD(wp) == kIdFoundList && HIWORD(wp) == LBN_DBLCLK)) {
        int row = (int)SendDlgItemMessageW(h, kIdFoundList, LB_GETCURSEL, 0, 0);
        if (row >= 0 && row < (int)f->topics.size()) {
          Viewer* v = f->viewer;
          int t = f->topics[row];
          DestroyWindow(h);
          ShowTopic(v, t, -1);
        }
        return TRUE;
      }
      if (LOWORD(wp) == IDCANCEL) {
        DestroyWindow(h);
        return TRUE;
      }
      break;
    case WM_CLOSE: DestroyWindow(h); return TRUE;
    case WM_DESTROY:
      if (f) {
        if (f->viewer->foundDialog == h) f->viewer->foundDialog = NULL;
        delete f;
        SetWindowLongPtrW(h, DWLP_USER, 0);
      }
      return TRUE;
  }
  return FALSE;
}

// Lists topics to choose from (a keyword of several topics).
void ShowFound(Viewer* v, const std::vector<int>& topics) {
  if (!v->frame) return;
  if (v->foundDialog) DestroyWindow(v->foundDialog);
  Template t(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 220, 150, L"Topics Found");
  t.Add(Template::kStatic, SS_LEFT, 6, 6, 208, 8, -1, L"Click a topic, and then click Display.");
  t.Add(Template::kListBox, WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 6, 18, 208, 106,
        kIdFoundList, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 104, 130, 54, 14, IDOK, L"&Display");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 160, 130, 54, 14, IDCANCEL, L"Cancel");
  Found* f = new Found();
  f->viewer = v;
  f->topics = topics;
  v->foundDialog = CreateDialogIndirectParamW(GetModuleHandleW(NULL), t.Get(), v->frame, FoundProc, (LPARAM)f);
  if (!v->foundDialog) {
    delete f;
    return;
  }
  ShowWindow(v->foundDialog, SW_SHOW);
}

// A string argument of a WinHelp command, in Unicode.
std::wstring CommandString(ULONG_PTR data, bool wide) {
  if (!data) return std::wstring();
  if (wide) return (const wchar_t*)data;
  return Widen((const char*)data);
}

}  // namespace

// Runs a WinHelp command for a help file.
BOOL Command(HWND caller, const std::string& path, const std::wstring& window, UINT cmd, ULONG_PTR data,
             bool wide) {
  if (cmd == HELP_QUIT) {
    ClosePopup();
    std::vector<HWND> frames;
    for (auto& v : g_viewers)
      if (path.empty() || v->file->Path() == path) frames.push_back(v->frame);
    for (HWND h : frames)
      if (IsWindow(h)) DestroyWindow(h);
    return TRUE;
  }
  std::shared_ptr<File> file = OpenFile(path);
  if (!file) {
    SetLastError(ERROR_BAD_FORMAT);
    return FALSE;
  }
  if (cmd == HELP_SETPOPUP_POS) {
    g_popupPos.x = (short)LOWORD(data);
    g_popupPos.y = (short)HIWORD(data);
    g_hasPopupPos = true;
    return TRUE;
  }
  // A popup by itself: the window it belongs to need not show.
  if (cmd == HELP_CONTEXTPOPUP || cmd == HELP_WM_HELP || cmd == HELP_CONTEXTMENU) {
    Viewer* v = GetViewer(file, std::wstring(), true);
    if (!v) return FALSE;
    POINT at;
    GetCursorPos(&at);
    int topic = -1;
    if (cmd == HELP_CONTEXTPOPUP) {
      topic = file->TopicByMap((uint32_t)data);
    } else {
      // The pairs of a control's identifier and its help's map number.
      int id = caller ? GetDlgCtrlID(caller) : 0;
      if (cmd == HELP_WM_HELP && caller) {
        RECT rc;
        GetWindowRect(caller, &rc);
        at.x = rc.left;
        at.y = rc.bottom;
      }
      const DWORD* ids = (const DWORD*)data;
      bool found = false;
      for (; ids && ids[0]; ids += 2)
        if ((int)ids[0] == id) {
          found = true;
          if (ids[1] == (DWORD)-1) return TRUE;  // IDH_NO_HELP
          topic = file->TopicByMap(ids[1]);
          break;
        }
      if (!found && ids) return TRUE;
    }
    if (topic < 0) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    ShowPopup(v, file, topic, at);
    return TRUE;
  }
  Viewer* v = GetViewer(file, window, true);
  if (!v) return FALSE;
  switch (cmd) {
    case HELP_CONTEXT: {
      int record = -1;
      int t = file->TopicByMap((uint32_t)data, &record);
      if (t < 0) {
        ShowTopic(v, file->ContentsTopic(), -1);
        MessageBoxW(v->frame, L"The topic does not exist. Contact your application vendor for an updated Help file.",
                    kAppName, MB_OK | MB_ICONEXCLAMATION);
        return TRUE;
      }
      ShowTopic(v, t, record);
      return TRUE;
    }
    case HELP_SETCONTENTS: {
      int t = file->TopicByMap((uint32_t)data);
      if (t >= 0) v->contentsOverride = t;
      return TRUE;
    }
    case HELP_FORCEFILE:
      if (v->topic >= 0) {
        ShowWindow(v->frame, SW_SHOW);
        return TRUE;
      }
      // fall through
    case HELP_CONTENTS:
      ShowTopic(v, v->contentsOverride >= 0 ? v->contentsOverride : file->ContentsTopic(), -1);
      return TRUE;
    case HELP_HELPONHELP:
      if (v->topic < 0) ShowTopic(v, file->ContentsTopic(), -1);
      RunMacros(v, L"HelpOn()");
      return TRUE;
    case HELP_FINDER:
      if (v->topic < 0) ShowTopic(v, v->contentsOverride >= 0 ? v->contentsOverride : file->ContentsTopic(), -1);
      ShowFinder(v, v->hasContents ? 0 : 1, std::wstring());
      return TRUE;
    case HELP_KEY:
    case HELP_PARTIALKEY: {
      std::wstring key = CommandString(data, wide);
      if (v->topic < 0) ShowTopic(v, file->ContentsTopic(), -1);
      if (key.empty())
        ShowFinder(v, 1, key);
      else
        ShowKeywordTopics(v, key, L'K');
      return TRUE;
    }
    case HELP_MULTIKEY: {
      if (!data) return FALSE;
      wchar_t index;
      std::wstring key;
      if (wide) {
        const MULTIKEYHELPW* mk = (const MULTIKEYHELPW*)data;
        index = mk->mkKeylist;
        key = mk->szKeyphrase;
      } else {
        const MULTIKEYHELPA* mk = (const MULTIKEYHELPA*)data;
        index = (wchar_t)(BYTE)mk->mkKeylist;
        key = Widen(mk->szKeyphrase);
      }
      if (v->topic < 0) ShowTopic(v, file->ContentsTopic(), -1);
      ShowKeywordTopics(v, key, (wchar_t)towupper(index));
      return TRUE;
    }
    case HELP_COMMAND:
      if (v->topic < 0) ShowTopic(v, file->ContentsTopic(), -1);
      RunMacros(v, CommandString(data, wide));
      return TRUE;
    case HELP_SETWINPOS: {
      if (!data) return FALSE;
      const HELPWININFOA* wi = (const HELPWININFOA*)data;
      int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
      SetWindowPos(v->frame, NULL, wi->x * sw / 1024, wi->y * sh / 1024, wi->dx * sw / 1024, wi->dy * sh / 1024,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      if (wi->wMax == SW_SHOWMAXIMIZED || wi->wMax == SW_MAXIMIZE) ShowWindow(v->frame, SW_SHOWMAXIMIZED);
      return TRUE;
    }
    case HELP_TCARD:
    case HELP_TCARD_DATA:
    case HELP_TCARD_OTHER_CALLER:
      return TRUE;
    default:
      if (v->topic < 0) ShowTopic(v, file->ContentsTopic(), -1);
      return TRUE;
  }
}

}  // namespace help
}  // namespace winport
