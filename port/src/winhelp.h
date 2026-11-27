// winhelp.h - the layer's WinHelp viewer: the reader of Windows help
// files (.hlp, of WinHelp 3.0, 3.1 and 4.0), their contents files
// (.cnt) and the viewer windows that show them.
//
// winhelp_file.cpp reads a help file into the structures below.
// winhelp_view.cpp shows its topics in a window and runs WinHelp's
// commands. docs/winhelp.md describes both.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <windows.h>

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace winport {
namespace help {

// A font of the |FONT file. Sizes are in twips.
struct Font {
  std::wstring face;
  int height = 200;  // 10 points
  int weight = FW_NORMAL;
  bool italic = false, underline = false, strike = false;
  bool doubleUnderline = false, smallCaps = false;
  BYTE family = FF_DONTCARE;  // a LOGFONT pitch and family
  BYTE charset = DEFAULT_CHARSET;
  COLORREF color = RGB(0, 0, 0);
};

// What a hotspot does.
struct Link {
  enum Kind { kJump, kPopup, kMacro };
  Kind kind = kJump;
  bool visible = true;   // green and underlined
  bool hasHash = false;  // target is a context hash
  uint32_t target = 0;   // a context hash, or a topic number (WinHelp 3.0)
  std::wstring context;  // a context name (picture hotspots)
  std::wstring file;     // another help file, or empty
  std::wstring window;   // a secondary window, or empty
  std::wstring macro;    // for kMacro
};

// A picture: the bitmap or metafile of a |bmN file or of a topic. A
// bitmap is a bottom-up DIB of 1, 4, 8 or 24 bits per pixel.
struct Picture {
  enum Type { kNone, kBitmap, kMetafile };
  Type type = kNone;
  int width = 0, height = 0;  // pixels (a metafile: its size at 96 dpi)
  int bitCount = 0;
  bool transparent = false;   // white is the background
  std::vector<RGBQUAD> palette;
  std::vector<BYTE> bits;     // DIB rows, each a multiple of 4 bytes
  int mapMode = MM_ANISOTROPIC;
  std::vector<BYTE> metafile;  // a Windows metafile (METAHEADER first)
  struct Hotspot {
    RECT rect;  // in the picture's pixels
    Link link;
  };
  std::vector<Hotspot> hotspots;
};

// One item of a paragraph's text, in order.
struct Item {
  enum Type {
    kText,       // text: the characters in the current font
    kFont,       // the font changes to font
    kLineBreak,  // a new line in the same paragraph
    kParaEnd,    // the paragraph ends; the next has the same format
    kTab,        // to the next tab stop
    kPicture,    // a picture: picture, at align
    kWindow,     // an embedded window or button: text describes it
    kLinkStart,  // a hotspot starts: link
    kLinkEnd,    // the hotspot ends
  };
  Type type = kText;
  std::wstring text;
  int font = 0;
  int align = 0;  // 0 in the line (bmc), 1 at the left (bml), 2 at the right (bmr)
  std::shared_ptr<Picture> picture;
  int pictureNumber = -1;  // a |bmN file, loaded when shown
  Link link;
};

// The format of paragraphs. Distances are in twips.
struct ParaFormat {
  int spaceAbove = 0, spaceBelow = 0, lineSpacing = 0;
  int leftIndent = 0, rightIndent = 0, firstIndent = 0;
  int align = 0;  // 0 left, 1 right, 2 center
  BYTE border = 0;  // bits: 1 box, 2 top, 4 left, 8 bottom, 0x10 right, 0x20 thick, 0x40 double
  int borderWidth = 0;
  struct Tab {
    int pos;
    int type;  // 0 left, 1 right, 2 center
  };
  std::vector<Tab> tabs;
};

// The paragraphs that share a format: a text record, or one cell of a
// table's row.
struct Cell {
  int column = -1;  // the column of a table's row
  ParaFormat format;
  std::vector<Item> items;
};

// A text record (one cell) or a table's row (one cell per column).
struct Record {
  bool table = false;
  bool relativeWidths = false;  // the columns share the window's width
  int minTableWidth = 0;
  struct Column {
    int gap, width;
  };
  std::vector<Column> columns;
  std::vector<Cell> cells;
  uint32_t offset = 0;  // the TOPICOFFSET of the record's start
};

// A topic: its title, the macros it runs and its text.
struct Topic {
  uint32_t offset = 0;  // TOPICOFFSET of its start
  uint32_t pos = 0;     // TOPICPOS of its header
  uint32_t number = 0;  // the topic's number
  std::wstring title;
  std::vector<std::wstring> macros;
  uint32_t browsePrev = 0xFFFFFFFF, browseNext = 0xFFFFFFFF;  // TOPICOFFSETs or -1
  std::vector<Record> nonScroll, scroll;
  bool parsed = false;
  // Where its records are: each one's TOPICPOS and TOPICOFFSET, and
  // whether it is in the region that does not scroll.
  struct Ref {
    uint32_t pos, offset;
    bool nonScroll;
  };
  std::vector<Ref> refs;
};

// A window that the help project defines ([WINDOWS]).
struct WindowDef {
  std::wstring type, name, caption;
  bool hasPos = false;
  int x = 0, y = 0, cx = 0, cy = 0;  // 0..1023 of the screen
  bool maximize = false, onTop = false;
  bool hasColor = false, hasNsrColor = false;
  COLORREF color = RGB(255, 255, 255), nsrColor = RGB(255, 255, 255);
};

// A help file, read whole into memory.
class File {
 public:
  // Reads the help file at the POSIX path. Returns NULL if it is not
  // a help file.
  static std::shared_ptr<File> Open(const std::string& path);

  const std::string& Path() const { return path_; }
  int Version() const { return minor_; }
  const std::wstring& Title() const { return title_; }
  const std::wstring& Copyright() const { return copyright_; }
  const std::wstring& CntName() const { return cnt_; }
  const std::vector<std::wstring>& ConfigMacros() const { return config_; }
  const std::vector<WindowDef>& Windows() const { return windows_; }
  const std::vector<Font>& Fonts() const { return fonts_; }
  UINT CodePage() const { return codePage_; }

  // The topics, in the order of the file.
  size_t TopicCount() const { return topics_.size(); }
  // The topic's text, read from |TOPIC when first asked for.
  Topic& GetTopic(size_t i);
  // The topic at a TOPICOFFSET, or -1. scroll gets the record to show
  // at the top, or -1 for the topic's start.
  int TopicAt(uint32_t offset, int* record = NULL);
  // The contents topic (|SYSTEM's CONTENTS, else the first).
  int ContentsTopic();
  // A context name's topic, or -1; its hash; a [MAP] number's.
  int TopicByName(const std::wstring& name);
  int TopicByHash(uint32_t hash, int* record = NULL);
  int TopicByMap(uint32_t id, int* record = NULL);
  // A link's topic, in this file (a 3.0 topic number or a hash).
  int TopicOfLink(const Link& link, int* record = NULL);
  // The hash WinHelp gives a context name.
  static uint32_t Hash(const std::wstring& name);

  // The keywords of an index (K, or A for ALink): each with its topics'
  // offsets, sorted.
  const std::vector<std::pair<std::wstring, std::vector<uint32_t>>>& Keywords(wchar_t index);

  // A picture by its |bmN number, or NULL.
  std::shared_ptr<Picture> GetPicture(int number);
  // The plain text of a topic (for searching and copying).
  std::wstring PlainText(size_t topic);
  // Text of the file's code page as Unicode.
  std::wstring Decode(const char* s, size_t n) const;

 private:
  struct Internal {
    const BYTE* data;
    uint32_t size;
  };
  bool Parse();
  bool FindInternal(const char* name, Internal* f) const;
  // Calls each(entry, end) for every leaf entry of a B+ tree in order;
  // each returns the size of the entry, or 0 to stop.
  template <class F>
  bool ForEachLeaf(const Internal& tree, F each) const;
  void ReadSystem();
  void ReadPhrases();
  void ReadFonts();
  void ReadTopics();
  void ParseTopic(Topic& t);
  bool ReadLink(uint32_t pos, std::vector<BYTE>* link, uint32_t* next) const;
  void ExpandText(const BYTE* p, size_t n, size_t outSize, std::vector<BYTE>* out) const;
  void ReadRecord(const std::vector<BYTE>& link, Record* r) const;
  std::shared_ptr<Picture> ReadPicture(const BYTE* p, size_t n) const;
  void ReadContextMaps();

  std::string path_;
  std::vector<BYTE> data_;
  std::map<std::string, Internal> dir_;
  int minor_ = 0;
  uint16_t flags_ = 0;
  bool lz_ = false;
  uint32_t blockSize_ = 4096, decompressSize_ = 4084;
  UINT codePage_ = 1252;
  std::wstring title_, copyright_, cnt_;
  uint32_t contents_ = 0xFFFFFFFF;
  std::vector<std::wstring> config_;
  std::vector<WindowDef> windows_;
  std::vector<Font> fonts_;
  bool twips_ = false;  // paragraph distances in twips (else half points)
  std::vector<std::string> phrases_;
  bool hall_ = false;
  std::vector<BYTE> topic_;  // |TOPIC's blocks, expanded and joined
  std::vector<uint32_t> blockStart_;  // each block's place in topic_
  std::vector<Topic> topics_;
  std::vector<uint32_t> offsets_;  // the topics' TOPICOFFSETs, in order
  std::map<uint32_t, uint32_t> hashes_;  // |CONTEXT: hash -> TOPICOFFSET
  std::map<uint32_t, uint32_t> map_;     // |CTXOMAP: [MAP] number -> TOPICOFFSET
  std::vector<uint32_t> tomap_;          // |TOMAP (3.0): topic number -> TOPICPOS
  std::map<wchar_t, std::vector<std::pair<std::wstring, std::vector<uint32_t>>>> keywords_;
  std::map<int, std::shared_ptr<Picture>> pictures_;
};

// A contents (.cnt) file's entries.
struct CntEntry {
  int level = 1;
  bool book = false;  // a heading; the entries below it are its own
  std::wstring text;
  std::wstring context;  // the topic's context name, or !macro
  std::wstring file;     // another help file, or empty
  std::wstring window;
};
struct Contents {
  std::wstring title, base;
  std::vector<CntEntry> entries;
  std::vector<std::wstring> links;  // :Link and :Index files, for keywords
};
// Reads the contents file at the POSIX path; false if there is none.
bool ReadContents(const std::string& path, Contents* out);

// The LZ77 expansion WinHelp uses; at most limit bytes.
std::vector<BYTE> Expand(const BYTE* p, size_t n, size_t limit = (size_t)-1);

// The viewer: runs a WinHelp command for the help file at the POSIX
// path (window, if any, after '>'). Sets the last error and returns
// FALSE when it cannot.
BOOL Command(HWND caller, const std::string& path, const std::wstring& window, UINT cmd, ULONG_PTR data,
             bool wide);

}  // namespace help
}  // namespace winport
