// metafile.cpp - metafiles: pictures kept as the GDI calls that draw
// them, recorded on a metafile DC and played back on any DC.
//
// A Windows metafile (CreateMetaFile) is recorded in the Windows Metafile
// Format: a header, then records of 16-bit words, each a function number
// and its parameters, in logical coordinates; the objects a picture uses
// are created by records and kept in a table. PlayMetaFile replays the
// records on a DC, so that the window and viewport the player sets up
// (an OLE container does) scale the picture.
//
// An enhanced metafile (CreateEnhMetaFile) is a picture with a frame:
// PlayEnhMetaFile fits it to a rectangle. Here it is recorded as a
// Windows metafile in the reference device's pixels (the DC's mapping
// already applied), which goes in an EMF file as a comment record
// between the EMF header and end; PlayEnhMetaFile maps its frame onto
// the rectangle and plays that.
//
// The GDI functions call the recorders below when they draw on a
// metafile DC (gdi_*.cpp).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace winport {

struct MetaRecorder {
  bool enh = false;
  std::vector<BYTE> rec;  // the records, after the header
  DWORD maxRecord = 0;    // in words
  std::vector<HGDIOBJ> table;
  std::string file;  // a disk metafile's
  // An enhanced metafile's: its frame (0.01 mm) if given, the bounds of
  // what was drawn (reference pixels) and its description.
  bool frameGiven = false;
  RECT frame = {0, 0, 0, 0};
  bool haveBounds = false;
  RECT bounds = {0, 0, 0, 0};
  std::wstring description;
};

namespace {

struct MetaFileObj : GdiObject {
  MetaFileObj() : GdiObject(OBJ_METAFILE) {}
  std::vector<BYTE> bits;
};
struct EnhMetaFileObj : GdiObject {
  EnhMetaFileObj() : GdiObject(OBJ_ENHMETAFILE) {}
  std::vector<BYTE> bits;
};

// The recorders alive, so that a deleted object leaves their tables.
std::mutex& RecLock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::set<MetaRecorder*>& Recorders() {
  static std::set<MetaRecorder*>* s = new std::set<MetaRecorder*>;
  return *s;
}

const int kRefDpi = 96;  // an enhanced metafile's reference pixels per inch

void PutW(std::vector<BYTE>& v, int w) {
  v.push_back((BYTE)w);
  v.push_back((BYTE)(w >> 8));
}
void PutD(std::vector<BYTE>& v, DWORD d) {
  PutW(v, (int)(d & 0xFFFF));
  PutW(v, (int)(d >> 16));
}
WORD GetW(const BYTE* p) { return (WORD)(p[0] | (p[1] << 8)); }
DWORD GetD(const BYTE* p) { return GetW(p) | ((DWORD)GetW(p + 2) << 16); }
short S16(int v) { return (short)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

// Appends a record: its size in words, its function and parameters.
void Record(MetaRecorder* m, WORD fn, const std::vector<BYTE>& params) {
  DWORD words = 3 + (DWORD)((params.size() + 1) / 2);
  PutD(m->rec, words);
  PutW(m->rec, fn);
  m->rec.insert(m->rec.end(), params.begin(), params.end());
  if (params.size() & 1) m->rec.push_back(0);
  if (words > m->maxRecord) m->maxRecord = words;
}
void RecordWords(MetaRecorder* m, WORD fn, std::initializer_list<int> words) {
  std::vector<BYTE> p;
  for (int w : words) PutW(p, S16(w));
  Record(m, fn, p);
}

MetaRecorder* Rec(DCObj* dc) { return dc && dc->kind == kMetaDC ? dc->meta.get() : NULL; }

// An enhanced metafile's coordinates are the reference device's.
POINT Pt(DCObj* dc, MetaRecorder* m, int x, int y) {
  if (!m->enh) return POINT{x, y};
  POINT p = dc->ToDevice(x, y);
  RECT r = {p.x, p.y, p.x + 1, p.y + 1};
  if (!m->haveBounds) {
    m->bounds = r;
    m->haveBounds = true;
  } else {
    m->bounds.left = std::min(m->bounds.left, r.left);
    m->bounds.top = std::min(m->bounds.top, r.top);
    m->bounds.right = std::max(m->bounds.right, r.right);
    m->bounds.bottom = std::max(m->bounds.bottom, r.bottom);
  }
  return p;
}
int Sx(DCObj* dc, MetaRecorder* m, int v) { return m->enh ? dc->ScaleX(v) * (v < 0 ? -1 : 1) : v; }
int Sy(DCObj* dc, MetaRecorder* m, int v) { return m->enh ? dc->ScaleY(v) * (v < 0 ? -1 : 1) : v; }

// The record that makes an object, for the table.
bool CreateRecord(DCObj* dc, MetaRecorder* m, GdiObject* o, WORD* fn, std::vector<BYTE>* p) {
  switch (o->type) {
    case OBJ_PEN: {
      PenObj* pen = (PenObj*)o;
      *fn = META_CREATEPENINDIRECT;
      PutW(*p, (int)(pen->style & 0xFFFF));
      PutW(*p, S16(Sx(dc, m, pen->width)));
      PutW(*p, 0);
      PutD(*p, pen->color);
      return true;
    }
    case OBJ_BRUSH: {
      BrushObj* b = (BrushObj*)o;
      *fn = META_CREATEBRUSHINDIRECT;
      UINT style = b->lb.lbStyle;
      COLORREF color = b->sysColor >= 0 ? GetSysColor(b->sysColor) : b->lb.lbColor;
      // Pattern brushes go as solid ones, in their first pixel's color.
      if (style == BS_PATTERN || style == BS_DIBPATTERN || style == BS_DIBPATTERNPT) {
        style = BS_SOLID;
        color = b->pattern.empty() ? 0 : ToColor(b->pattern[0]);
      }
      PutW(*p, (int)style);
      PutD(*p, color);
      PutW(*p, (int)b->lb.lbHatch);
      return true;
    }
    case OBJ_FONT: {
      const LOGFONTW& lf = ((FontObj*)o)->lf;
      *fn = META_CREATEFONTINDIRECT;
      PutW(*p, S16(Sy(dc, m, lf.lfHeight)));
      PutW(*p, S16(Sx(dc, m, lf.lfWidth)));
      PutW(*p, S16(lf.lfEscapement));
      PutW(*p, S16(lf.lfOrientation));
      PutW(*p, S16(lf.lfWeight));
      BYTE b[8] = {lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfCharSet,
                   lf.lfOutPrecision, lf.lfClipPrecision, lf.lfQuality, lf.lfPitchAndFamily};
      p->insert(p->end(), b, b + 8);
      std::string face = Narrow(lf.lfFaceName);
      face.resize(32, 0);
      face[31] = 0;
      p->insert(p->end(), face.begin(), face.end());
      return true;
    }
  }
  return false;
}

// The object's index in the table, recording its creation the first
// time; -1 for an object a metafile cannot hold.
int ObjectIndex(DCObj* dc, MetaRecorder* m, HGDIOBJ h) {
  for (size_t i = 0; i < m->table.size(); i++)
    if (m->table[i] == h) return (int)i;
  GdiObject* o = GdiObj(h);
  WORD fn;
  std::vector<BYTE> p;
  if (!o || !CreateRecord(dc, m, o, &fn, &p)) return -1;
  Record(m, fn, p);
  // The first free slot, as the player fills them.
  for (size_t i = 0; i < m->table.size(); i++) {
    if (m->table[i] == NULL) {
      m->table[i] = h;
      return (int)i;
    }
  }
  m->table.push_back(h);
  return (int)m->table.size() - 1;
}

std::vector<BYTE> Header(const MetaRecorder* m, bool disk, DWORD totalWords) {
  std::vector<BYTE> h;
  // Windows writes a file's metafile as a memory one (type 1) too, and
  // some readers want that.
  (void)disk;
  PutW(h, 1);
  PutW(h, 9);
  PutW(h, 0x0300);
  PutD(h, totalWords);
  PutW(h, (int)m->table.size());
  PutD(h, m->maxRecord);
  PutW(h, 0);
  return h;
}

// A finished Windows metafile: the header, the records and the end.
std::vector<BYTE> Finish(MetaRecorder* m, bool disk) {
  Record(m, 0, std::vector<BYTE>());  // META_EOF
  std::vector<BYTE> out = Header(m, disk, (DWORD)(18 + m->rec.size()) / 2);
  out.insert(out.end(), m->rec.begin(), m->rec.end());
  return out;
}

bool WriteFile(const std::string& path, const std::vector<BYTE>& v) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (fd < 0) {
    SetErrorFromErrno();
    return false;
  }
  size_t done = 0;
  while (done < v.size()) {
    ssize_t n = write(fd, v.data() + done, v.size() - done);
    if (n <= 0) break;
    done += (size_t)n;
  }
  close(fd);
  return done == v.size();
}

bool ReadFileBytes(const std::string& path, std::vector<BYTE>* v) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    SetErrorFromErrno();
    return false;
  }
  BYTE buf[65536];
  ssize_t n;
  v->clear();
  while ((n = read(fd, buf, sizeof buf)) > 0) v->insert(v->end(), buf, buf + n);
  close(fd);
  return true;
}

HDC NewMetaDC(bool enh) {
  DCObj* dc = new DCObj(kMetaDC);
  dc->type = enh ? OBJ_ENHMETADC : OBJ_METADC;
  dc->size.cx = dc->size.cy = 32767;
  InitDC(dc);
  dc->meta = std::make_shared<MetaRecorder>();
  dc->meta->enh = enh;
  {
    std::lock_guard<std::mutex> g(RecLock());
    Recorders().insert(dc->meta.get());
  }
  return (HDC)NewGdiHandle(dc);
}

// Ends recording on a DC: its recorder, and the DC is gone.
std::shared_ptr<MetaRecorder> EndRecording(HDC hdc, bool enh) {
  DCObj* dc = DCOf(hdc);
  if (!dc || dc->kind != kMetaDC || !dc->meta || dc->meta->enh != enh) {
    SetLastError(ERROR_INVALID_HANDLE);
    return std::shared_ptr<MetaRecorder>();
  }
  std::shared_ptr<MetaRecorder> m = dc->meta;
  {
    std::lock_guard<std::mutex> g(RecLock());
    Recorders().erase(m.get());
  }
  dc->meta.reset();
  FreeDC(dc);
  return m;
}

HMETAFILE NewMetaFile(std::vector<BYTE> bits) {
  MetaFileObj* o = new MetaFileObj;
  o->bits.swap(bits);
  return (HMETAFILE)NewGdiHandle(o);
}

// ---------------------------------------------------------------------
// Enhanced metafiles: the EMF header, the picture as a Windows metafile
// in a comment, and the end.

const DWORD kEmrHeader = 1, kEmrEof = 14, kEmrComment = 70;
const DWORD kEmfSignature = 0x464D4520;  // " EMF"
const DWORD kOurComment = 0x4D575243;    // "CRWM": a Windows metafile follows

void PutL(std::vector<BYTE>& v, LONG l) { PutD(v, (DWORD)l); }

std::vector<BYTE> BuildEmf(const MetaRecorder* m, const std::vector<BYTE>& wmf) {
  RECT b = m->haveBounds ? m->bounds : RECT{0, 0, 0, 0};
  RECT f;
  if (m->frameGiven) {
    f = m->frame;
  } else {
    // The bounds, in 0.01 mm.
    f.left = MulDiv(b.left, 2540, kRefDpi);
    f.top = MulDiv(b.top, 2540, kRefDpi);
    f.right = MulDiv(b.right, 2540, kRefDpi);
    f.bottom = MulDiv(b.bottom, 2540, kRefDpi);
  }
  std::vector<BYTE> desc;
  for (wchar_t c : m->description) PutW(desc, (int)(c > 0xFFFF ? '?' : c));
  if (!m->description.empty()) {
    PutW(desc, 0);
    PutW(desc, 0);  // two terminators: the application's name, then the picture's
  }
  while (desc.size() % 4) desc.push_back(0);
  const DWORD headerSize = 108;
  std::vector<BYTE> comment;
  PutD(comment, kOurComment);
  comment.insert(comment.end(), wmf.begin(), wmf.end());
  while (comment.size() % 4) comment.push_back(0);
  DWORD commentRec = 12 + (DWORD)comment.size();
  DWORD total = headerSize + (DWORD)desc.size() + commentRec + 20;
  std::vector<BYTE> v;
  PutD(v, kEmrHeader);
  PutD(v, headerSize + (DWORD)desc.size());
  PutL(v, b.left), PutL(v, b.top), PutL(v, b.right - 1), PutL(v, b.bottom - 1);
  PutL(v, f.left), PutL(v, f.top), PutL(v, f.right), PutL(v, f.bottom);
  PutD(v, kEmfSignature);
  PutD(v, 0x10000);
  PutD(v, total);
  PutD(v, 3);  // records
  PutW(v, 1);  // handles: the table's entry 0 only
  PutW(v, 0);
  PutD(v, desc.empty() ? 0 : (DWORD)m->description.size() + 2);
  PutD(v, desc.empty() ? 0 : headerSize);
  PutD(v, 0);  // palette entries
  PutL(v, 1024), PutL(v, 768);  // the reference device, pixels
  PutL(v, 271), PutL(v, 203);   // and millimeters, at 96 dpi
  PutD(v, 0), PutD(v, 0), PutD(v, 0);  // no pixel format; not OpenGL
  PutL(v, 1024 * 1000 / kRefDpi * 254 / 10), PutL(v, 768 * 1000 / kRefDpi * 254 / 10);  // micrometers
  v.insert(v.end(), desc.begin(), desc.end());
  PutD(v, kEmrComment);
  PutD(v, commentRec);
  PutD(v, (DWORD)comment.size());
  v.insert(v.end(), comment.begin(), comment.end());
  PutD(v, kEmrEof);
  PutD(v, 20);
  PutD(v, 0);
  PutD(v, 16);
  PutD(v, 20);
  return v;
}

bool ValidEmf(const std::vector<BYTE>& v) {
  return v.size() >= 88 && GetD(v.data()) == kEmrHeader && GetD(v.data() + 40) == kEmfSignature;
}

// The Windows metafile inside one of these EMF files, if it has one.
bool EmfPicture(const std::vector<BYTE>& v, std::vector<BYTE>* wmf) {
  size_t pos = 0;
  while (pos + 8 <= v.size()) {
    DWORD type = GetD(v.data() + pos), size = GetD(v.data() + pos + 4);
    if (size < 8 || pos + size > v.size()) break;
    if (type == kEmrComment && size >= 16 && GetD(v.data() + pos + 12) == kOurComment) {
      DWORD n = GetD(v.data() + pos + 8);
      if (n < 4 || 12 + n > size) return false;
      wmf->assign(v.begin() + pos + 16, v.begin() + pos + 12 + n);
      return true;
    }
    if (type == kEmrEof) break;
    pos += size;
  }
  return false;
}

HENHMETAFILE NewEnhMetaFile(std::vector<BYTE> bits) {
  EnhMetaFileObj* o = new EnhMetaFileObj;
  o->bits.swap(bits);
  return (HENHMETAFILE)NewGdiHandle(o);
}

// ---------------------------------------------------------------------
// Playing a Windows metafile

struct Params {
  const BYTE* p;
  size_t n;  // words
  int W(size_t i) const { return i < n ? (short)GetW(p + i * 2) : 0; }
  DWORD D(size_t i) const { return i + 1 < n ? GetD(p + i * 2) : 0; }
};

HGDIOBJ MakeObject(WORD fn, const Params& a) {
  switch (fn) {
    case META_CREATEPENINDIRECT: {
      LOGPEN lp;
      lp.lopnStyle = (UINT)(WORD)a.W(0);
      lp.lopnWidth.x = a.W(1);
      lp.lopnWidth.y = 0;
      lp.lopnColor = a.D(3);
      return CreatePenIndirect(&lp);
    }
    case META_CREATEBRUSHINDIRECT: {
      LOGBRUSH lb;
      lb.lbStyle = (UINT)(WORD)a.W(0);
      lb.lbColor = a.D(1);
      lb.lbHatch = (ULONG_PTR)(WORD)a.W(3);
      if (lb.lbStyle != BS_SOLID && lb.lbStyle != BS_NULL && lb.lbStyle != BS_HATCHED) lb.lbStyle = BS_SOLID;
      return CreateBrushIndirect(&lb);
    }
    case META_CREATEFONTINDIRECT: {
      if (a.n < 9) return NULL;
      LOGFONTA lf;
      memset(&lf, 0, sizeof lf);
      lf.lfHeight = a.W(0);
      lf.lfWidth = a.W(1);
      lf.lfEscapement = a.W(2);
      lf.lfOrientation = a.W(3);
      lf.lfWeight = a.W(4);
      const BYTE* b = a.p + 10;
      lf.lfItalic = b[0], lf.lfUnderline = b[1], lf.lfStrikeOut = b[2], lf.lfCharSet = b[3];
      lf.lfOutPrecision = b[4], lf.lfClipPrecision = b[5], lf.lfQuality = b[6], lf.lfPitchAndFamily = b[7];
      size_t room = a.n * 2 > 18 ? std::min<size_t>(a.n * 2 - 18, 32) : 0;
      memcpy(lf.lfFaceName, b + 8, std::min<size_t>(room, sizeof lf.lfFaceName - 1));
      return CreateFontIndirectA(&lf);
    }
  }
  return NULL;
}

void PlayRecord(HDC hdc, std::vector<HGDIOBJ>& table, WORD fn, const Params& a) {
  switch (fn) {
    case META_SETBKCOLOR: SetBkColor(hdc, a.D(0)); break;
    case META_SETTEXTCOLOR: SetTextColor(hdc, a.D(0)); break;
    case META_SETBKMODE: SetBkMode(hdc, a.W(0)); break;
    case META_SETMAPMODE: SetMapMode(hdc, a.W(0)); break;
    case META_SETROP2: SetROP2(hdc, a.W(0)); break;
    case META_SETPOLYFILLMODE: SetPolyFillMode(hdc, a.W(0)); break;
    case META_SETSTRETCHBLTMODE: SetStretchBltMode(hdc, a.W(0)); break;
    case META_SETTEXTALIGN: SetTextAlign(hdc, (UINT)(WORD)a.W(0)); break;
    case META_SETTEXTCHAREXTRA: SetTextCharacterExtra(hdc, a.W(0)); break;
    case META_SETWINDOWORG: SetWindowOrgEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_SETWINDOWEXT: SetWindowExtEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_SETVIEWPORTORG: SetViewportOrgEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_SETVIEWPORTEXT: SetViewportExtEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_OFFSETWINDOWORG: OffsetWindowOrgEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_OFFSETVIEWPORTORG: OffsetViewportOrgEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_SCALEWINDOWEXT: ScaleWindowExtEx(hdc, a.W(3), a.W(2), a.W(1), a.W(0), NULL); break;
    case META_SCALEVIEWPORTEXT: ScaleViewportExtEx(hdc, a.W(3), a.W(2), a.W(1), a.W(0), NULL); break;
    case META_MOVETO: MoveToEx(hdc, a.W(1), a.W(0), NULL); break;
    case META_LINETO: LineTo(hdc, a.W(1), a.W(0)); break;
    case META_RECTANGLE: Rectangle(hdc, a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_ELLIPSE: Ellipse(hdc, a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_ROUNDRECT: RoundRect(hdc, a.W(5), a.W(4), a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_ARC: Arc(hdc, a.W(7), a.W(6), a.W(5), a.W(4), a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_CHORD: Chord(hdc, a.W(7), a.W(6), a.W(5), a.W(4), a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_PIE: Pie(hdc, a.W(7), a.W(6), a.W(5), a.W(4), a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_INTERSECTCLIPRECT: IntersectClipRect(hdc, a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_EXCLUDECLIPRECT: ExcludeClipRect(hdc, a.W(3), a.W(2), a.W(1), a.W(0)); break;
    case META_SAVEDC: SaveDC(hdc); break;
    case META_RESTOREDC: RestoreDC(hdc, a.W(0)); break;
    case META_SETPIXEL: SetPixelV(hdc, a.W(3), a.W(2), a.D(0)); break;
    case META_PATBLT: PatBlt(hdc, a.W(5), a.W(4), a.W(3), a.W(2), a.D(0)); break;
    case META_POLYGON:
    case META_POLYLINE: {
      int n = a.W(0);
      if (n < 0 || (size_t)(1 + n * 2) > a.n) break;
      std::vector<POINT> pts(n);
      for (int i = 0; i < n; i++) pts[i] = POINT{a.W(1 + i * 2), a.W(2 + i * 2)};
      if (fn == META_POLYGON) Polygon(hdc, pts.data(), n);
      else Polyline(hdc, pts.data(), n);
      break;
    }
    case META_POLYPOLYGON: {
      int polys = a.W(0);
      if (polys <= 0 || (size_t)(1 + polys) > a.n) break;
      std::vector<INT> counts(polys);
      size_t total = 0;
      for (int i = 0; i < polys; i++) total += (size_t)(counts[i] = (WORD)a.W(1 + i));
      if (1 + polys + total * 2 > a.n) break;
      std::vector<POINT> pts(total);
      for (size_t i = 0; i < total; i++) pts[i] = POINT{a.W(1 + polys + i * 2), a.W(2 + polys + i * 2)};
      PolyPolygon(hdc, pts.data(), counts.data(), polys);
      break;
    }
    case META_TEXTOUT: {
      int len = a.W(0);
      size_t words = (size_t)(len + 1) / 2;
      if (len < 0 || 1 + words + 2 > a.n) break;
      TextOutA(hdc, a.W(2 + words), a.W(1 + words), (LPCSTR)(a.p + 2), len);
      break;
    }
    case META_EXTTEXTOUT: {
      int y = a.W(0), x = a.W(1), len = a.W(2);
      UINT opt = (UINT)(WORD)a.W(3);
      size_t i = 4;
      RECT r = {0, 0, 0, 0};
      bool hasRect = (opt & (ETO_OPAQUE | ETO_CLIPPED)) != 0;
      if (hasRect) {
        r = RECT{a.W(4), a.W(5), a.W(6), a.W(7)};
        i = 8;
      }
      size_t words = (size_t)(len + 1) / 2;
      if (len < 0 || i + words > a.n) break;
      const char* s = (const char*)(a.p + i * 2);
      std::vector<INT> dx;
      if (i + words + (size_t)len <= a.n) {
        for (int k = 0; k < len; k++) dx.push_back(a.W(i + words + k));
      }
      ExtTextOutA(hdc, x, y, opt, hasRect ? &r : NULL, s, (UINT)len, dx.empty() ? NULL : dx.data());
      break;
    }
    case META_STRETCHDIB: {
      DWORD rop = a.D(0);
      UINT usage = (UINT)(WORD)a.W(2);
      int sh = a.W(3), sw = a.W(4), sy = a.W(5), sx = a.W(6);
      int dh = a.W(7), dw = a.W(8), dy = a.W(9), dx = a.W(10);
      const BITMAPINFO* bmi = (const BITMAPINFO*)(a.p + 22);
      if (a.n * 2 < 22 + sizeof(BITMAPINFOHEADER)) break;
      const BITMAPINFOHEADER& h = bmi->bmiHeader;
      DWORD colors = h.biClrUsed ? h.biClrUsed : (h.biBitCount <= 8 ? 1u << h.biBitCount : 0);
      if (h.biCompression == BI_BITFIELDS) colors = 3;
      size_t headerBytes = h.biSize + colors * (usage == DIB_PAL_COLORS ? 2 : 4);
      if (22 + headerBytes > a.n * 2) break;
      StretchDIBits(hdc, dx, dy, dw, dh, sx, sy, sw, sh, (const BYTE*)bmi + headerBytes, bmi, usage, rop);
      break;
    }
    case META_SELECTOBJECT: {
      int i = a.W(0);
      if (i >= 0 && (size_t)i < table.size() && table[i]) SelectObject(hdc, table[i]);
      break;
    }
    case META_DELETEOBJECT: {
      int i = a.W(0);
      if (i >= 0 && (size_t)i < table.size() && table[i]) {
        DeleteObject(table[i]);
        table[i] = NULL;
      }
      break;
    }
    case META_CREATEPENINDIRECT:
    case META_CREATEBRUSHINDIRECT:
    case META_CREATEFONTINDIRECT: {
      HGDIOBJ h = MakeObject(fn, a);
      // Into the first free slot, as recorded.
      size_t i = 0;
      while (i < table.size() && table[i]) i++;
      if (i == table.size()) table.push_back(h);
      else table[i] = h;
      break;
    }
    default: {
      // Other objects take a slot too, so that the indexes stay right.
      if (fn == META_CREATEPALETTE || fn == META_CREATEPATTERNBRUSH || fn == META_DIBCREATEPATTERNBRUSH ||
          fn == META_CREATEREGION) {
        size_t i = 0;
        while (i < table.size() && table[i]) i++;
        HGDIOBJ h = fn == META_CREATEPALETTE ? NULL : GetStockObject(fn == META_CREATEREGION ? NULL_BRUSH : BLACK_BRUSH);
        if (fn == META_CREATEREGION) h = CreateRectRgn(0, 0, 0, 0);
        else if (fn != META_CREATEPALETTE) {
          LOGBRUSH lb = {BS_SOLID, 0, 0};
          h = CreateBrushIndirect(&lb);
        } else {
          h = CreateRectRgn(0, 0, 0, 0);  // a placeholder
        }
        if (i == table.size()) table.push_back(h);
        else table[i] = h;
      }
      break;
    }
  }
}

BOOL Play(HDC hdc, const std::vector<BYTE>& bits) {
  if (bits.size() < 18) return FALSE;
  WORD headerWords = GetW(bits.data() + 2);
  size_t pos = (size_t)headerWords * 2;
  if (pos < 18) pos = 18;
  std::vector<HGDIOBJ> table(GetW(bits.data() + 10), (HGDIOBJ)NULL);
  int saved = SaveDC(hdc);
  while (pos + 6 <= bits.size()) {
    DWORD words = GetD(bits.data() + pos);
    WORD fn = GetW(bits.data() + pos + 4);
    if (fn == 0 || words < 3 || pos + (size_t)words * 2 > bits.size()) break;
    Params a = {bits.data() + pos + 6, (size_t)words - 3};
    PlayRecord(hdc, table, fn, a);
    pos += (size_t)words * 2;
  }
  RestoreDC(hdc, saved);
  for (HGDIOBJ h : table)
    if (h) DeleteObject(h);
  return TRUE;
}

}  // namespace

// ---------------------------------------------------------------------
// Recording (called by the GDI functions on a metafile DC)

namespace meta {

void Attr(DCObj* dc, WORD fn, DWORD v) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  if (fn == META_SETBKCOLOR || fn == META_SETTEXTCOLOR) {
    std::vector<BYTE> p;
    PutD(p, v);
    Record(m, fn, p);
  } else {
    RecordWords(m, fn, {(int)v});
  }
}

// The mapping: an enhanced metafile has it applied already.
void Mapping(DCObj* dc, WORD fn, int x, int y) {
  MetaRecorder* m = Rec(dc);
  if (!m || m->enh) return;
  RecordWords(m, fn, {y, x});
}
void Scale(DCObj* dc, WORD fn, int xn, int xd, int yn, int yd) {
  MetaRecorder* m = Rec(dc);
  if (!m || m->enh) return;
  RecordWords(m, fn, {yd, yn, xd, xn});
}
void MapMode(DCObj* dc, int mode) {
  MetaRecorder* m = Rec(dc);
  if (!m || m->enh) return;
  RecordWords(m, META_SETMAPMODE, {mode});
}

void Point(DCObj* dc, WORD fn, int x, int y) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT p = Pt(dc, m, x, y);
  RecordWords(m, fn, {p.y, p.x});
}

void Box(DCObj* dc, WORD fn, int l, int t, int r, int b) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT a = Pt(dc, m, l, t), c = Pt(dc, m, r, b);
  RecordWords(m, fn, {c.y, c.x, a.y, a.x});
}

void RoundRect(DCObj* dc, int l, int t, int r, int b, int w, int h) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT a = Pt(dc, m, l, t), c = Pt(dc, m, r, b);
  RecordWords(m, META_ROUNDRECT, {Sy(dc, m, h), Sx(dc, m, w), c.y, c.x, a.y, a.x});
}

void Arc(DCObj* dc, WORD fn, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT a = Pt(dc, m, l, t), c = Pt(dc, m, r, b), s = Pt(dc, m, xs, ys), e = Pt(dc, m, xe, ye);
  RecordWords(m, fn, {e.y, e.x, s.y, s.x, c.y, c.x, a.y, a.x});
}

void Poly(DCObj* dc, WORD fn, const POINT* pts, int n) {
  MetaRecorder* m = Rec(dc);
  if (!m || n < 0) return;
  std::vector<BYTE> p;
  PutW(p, n);
  for (int i = 0; i < n; i++) {
    POINT q = Pt(dc, m, pts[i].x, pts[i].y);
    PutW(p, S16(q.x));
    PutW(p, S16(q.y));
  }
  Record(m, fn, p);
}

void PolyPoly(DCObj* dc, const POINT* pts, const INT* counts, int polys) {
  MetaRecorder* m = Rec(dc);
  if (!m || polys <= 0) return;
  std::vector<BYTE> p;
  PutW(p, polys);
  size_t total = 0;
  for (int i = 0; i < polys; i++) {
    PutW(p, counts[i]);
    total += (size_t)counts[i];
  }
  for (size_t i = 0; i < total; i++) {
    POINT q = Pt(dc, m, pts[i].x, pts[i].y);
    PutW(p, S16(q.x));
    PutW(p, S16(q.y));
  }
  Record(m, META_POLYPOLYGON, p);
}

// Text goes as bytes of the ANSI code page (UTF-8 here), a character's
// advance on its first byte.
void Text(DCObj* dc, int x, int y, UINT opt, const RECT* rc, const wchar_t* s, int n, const INT* dx) {
  MetaRecorder* m = Rec(dc);
  if (!m || n < 0) return;
  std::string bytes;
  std::vector<INT> bdx;
  for (int i = 0; i < n; i++) {
    std::string c = Narrow(s + i, 1);
    if (c.empty()) c = "?";
    bytes += c;
    for (size_t k = 0; k < c.size(); k++) bdx.push_back(k == 0 && dx ? Sx(dc, m, dx[i]) : 0);
  }
  POINT p = Pt(dc, m, x, y);
  std::vector<BYTE> v;
  PutW(v, S16(p.y));
  PutW(v, S16(p.x));
  PutW(v, (int)bytes.size());
  PutW(v, (int)(opt & 0xFFFF));
  if (rc && (opt & (ETO_OPAQUE | ETO_CLIPPED))) {
    POINT a = Pt(dc, m, rc->left, rc->top), c = Pt(dc, m, rc->right, rc->bottom);
    PutW(v, S16(a.x)), PutW(v, S16(a.y)), PutW(v, S16(c.x)), PutW(v, S16(c.y));
  } else {
    opt &= ~(UINT)(ETO_OPAQUE | ETO_CLIPPED);
    v[6] = (BYTE)opt;
    v[7] = (BYTE)(opt >> 8);
  }
  v.insert(v.end(), bytes.begin(), bytes.end());
  if (v.size() & 1) v.push_back(0);
  if (dx)
    for (INT d : bdx) PutW(v, S16(d));
  Record(m, META_EXTTEXTOUT, v);
}

void PatBlt(DCObj* dc, int x, int y, int w, int h, DWORD rop) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT a = Pt(dc, m, x, y), c = Pt(dc, m, x + w, y + h);
  std::vector<BYTE> p;
  PutD(p, rop);
  PutW(p, S16(c.y - a.y));
  PutW(p, S16(c.x - a.x));
  PutW(p, S16(a.y));
  PutW(p, S16(a.x));
  Record(m, META_PATBLT, p);
}

void Pixel(DCObj* dc, int x, int y, COLORREF c) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  POINT a = Pt(dc, m, x, y);
  std::vector<BYTE> p;
  PutD(p, c);
  PutW(p, S16(a.y));
  PutW(p, S16(a.x));
  Record(m, META_SETPIXEL, p);
}

void Save(DCObj* dc) {
  if (MetaRecorder* m = Rec(dc)) RecordWords(m, META_SAVEDC, {});
}
void Restore(DCObj* dc, int level) {
  if (MetaRecorder* m = Rec(dc)) RecordWords(m, META_RESTOREDC, {level});
}

void Select(DCObj* dc, HGDIOBJ h) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  int i = ObjectIndex(dc, m, h);
  if (i >= 0) RecordWords(m, META_SELECTOBJECT, {i});
}

// A brush for one call: selected, used and replaced by the one before.
void FillRect(DCObj* dc, const RECT& r, HBRUSH brush, DWORD rop) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  RecordWords(m, META_SAVEDC, {});
  if (brush) Select(dc, brush);
  PatBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, rop);
  RecordWords(m, META_RESTOREDC, {-1});
}

void Dib(DCObj* dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
         const BITMAPINFO* bmi, UINT usage, DWORD rop) {
  MetaRecorder* m = Rec(dc);
  if (!m || !bmi || !bits) return;
  const BITMAPINFOHEADER& bh = bmi->bmiHeader;
  DWORD colors = bh.biClrUsed ? bh.biClrUsed : (bh.biBitCount <= 8 ? 1u << bh.biBitCount : 0);
  if (bh.biCompression == BI_BITFIELDS) colors = 3;
  size_t headerBytes = bh.biSize + colors * (usage == DIB_PAL_COLORS ? 2 : 4);
  size_t imageBytes = bh.biSizeImage;
  if (bh.biCompression == BI_RGB || bh.biCompression == BI_BITFIELDS)
    imageBytes = (size_t)((bh.biWidth * bh.biBitCount + 31) / 32) * 4 * (size_t)(bh.biHeight < 0 ? -bh.biHeight : bh.biHeight);
  POINT a = Pt(dc, m, x, y), c = Pt(dc, m, x + w, y + h);
  std::vector<BYTE> p;
  PutD(p, rop);
  PutW(p, (int)usage);
  PutW(p, S16(sh)), PutW(p, S16(sw)), PutW(p, S16(sy)), PutW(p, S16(sx));
  PutW(p, S16(c.y - a.y)), PutW(p, S16(c.x - a.x)), PutW(p, S16(a.y)), PutW(p, S16(a.x));
  p.insert(p.end(), (const BYTE*)bmi, (const BYTE*)bmi + headerBytes);
  p.insert(p.end(), (const BYTE*)bits, (const BYTE*)bits + imageBytes);
  Record(m, META_STRETCHDIB, p);
}

// A blit from another DC: its pixels as a DIB.
void Blt(DCObj* dc, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
  MetaRecorder* m = Rec(dc);
  if (!m) return;
  HBITMAP bm = src ? (HBITMAP)GetCurrentObject(src, OBJ_BITMAP) : NULL;
  if (!bm || sw == 0 || sh == 0) {
    PatBlt(dc, x, y, w, h, rop);
    return;
  }
  // The source rectangle in device pixels.
  POINT a, c;
  a.x = sx, a.y = sy, c.x = sx + sw, c.y = sy + sh;
  LPtoDP(src, &a, 1);
  LPtoDP(src, &c, 1);
  int dw = std::abs(c.x - a.x), dh = std::abs(c.y - a.y);
  if (dw == 0 || dh == 0) return;
  BITMAP info;
  if (!GetObjectW(bm, sizeof info, &info)) return;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = info.bmWidth;
  bi.bmiHeader.biHeight = -info.bmHeight;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  std::vector<DWORD> all((size_t)info.bmWidth * info.bmHeight);
  if (!GetDIBits(src, bm, 0, info.bmHeight, all.data(), &bi, DIB_RGB_COLORS)) return;
  int left = std::min(a.x, c.x), top = std::min(a.y, c.y);
  std::vector<DWORD> part((size_t)dw * dh, 0);
  for (int row = 0; row < dh; row++)
    for (int col = 0; col < dw; col++) {
      int px = left + col, py = top + row;
      if (px >= 0 && py >= 0 && px < info.bmWidth && py < info.bmHeight)
        part[(size_t)row * dw + col] = all[(size_t)py * info.bmWidth + px];
    }
  bi.bmiHeader.biWidth = dw;
  bi.bmiHeader.biHeight = -dh;
  Dib(dc, x, y, w, h, 0, 0, dw, dh, part.data(), &bi, DIB_RGB_COLORS, rop);
}

}  // namespace meta

void MetaObjectDeleted(HGDIOBJ h) {
  std::lock_guard<std::mutex> g(RecLock());
  for (MetaRecorder* m : Recorders()) {
    for (size_t i = 0; i < m->table.size(); i++) {
      if (m->table[i] == h) {
        RecordWords(m, META_DELETEOBJECT, {(int)i});
        m->table[i] = NULL;
      }
    }
  }
}

void MetaDCFreed(DCObj* dc) {
  if (!dc->meta) return;
  std::lock_guard<std::mutex> g(RecLock());
  Recorders().erase(dc->meta.get());
  dc->meta.reset();
}

}  // namespace winport

using namespace winport;

extern "C" {

HDC WINAPI CreateMetaFileW(LPCWSTR file) {
  HDC h = NewMetaDC(false);
  if (h && file) DCOf(h)->meta->file = PosixPath(file);
  return h;
}
HDC WINAPI CreateMetaFileA(LPCSTR file) {
  HDC h = NewMetaDC(false);
  if (h && file) DCOf(h)->meta->file = PosixPath(file);
  return h;
}

HMETAFILE WINAPI CloseMetaFile(HDC hdc) {
  std::shared_ptr<MetaRecorder> m = EndRecording(hdc, false);
  if (!m) return NULL;
  std::vector<BYTE> bits = Finish(m.get(), !m->file.empty());
  if (!m->file.empty() && !WriteFile(m->file, bits)) return NULL;
  return NewMetaFile(std::move(bits));
}

BOOL WINAPI DeleteMetaFile(HMETAFILE h) {
  if (!GdiAs<MetaFileObj>(h, OBJ_METAFILE)) return FALSE;
  FreeGdiHandle(h);
  return TRUE;
}

HMETAFILE WINAPI CopyMetaFileW(HMETAFILE h, LPCWSTR file) {
  MetaFileObj* o = GdiAs<MetaFileObj>(h, OBJ_METAFILE);
  if (!o) return NULL;
  std::vector<BYTE> bits = o->bits;
  if (file && !WriteFile(PosixPath(file), bits)) return NULL;
  return NewMetaFile(std::move(bits));
}
HMETAFILE WINAPI CopyMetaFileA(HMETAFILE h, LPCSTR file) {
  if (!file) return CopyMetaFileW(h, NULL);
  std::wstring w = Widen(file);
  return CopyMetaFileW(h, w.c_str());
}

HMETAFILE WINAPI GetMetaFileW(LPCWSTR file) {
  std::vector<BYTE> bits;
  if (!file || !ReadFileBytes(PosixPath(file), &bits)) return NULL;
  // A placeable metafile's 22-byte header goes.
  if (bits.size() >= 22 && GetD(bits.data()) == 0x9AC6CDD7) bits.erase(bits.begin(), bits.begin() + 22);
  if (bits.size() < 18) {
    SetLastError(ERROR_INVALID_DATA);
    return NULL;
  }
  return NewMetaFile(std::move(bits));
}
HMETAFILE WINAPI GetMetaFileA(LPCSTR file) {
  if (!file) return NULL;
  std::wstring w = Widen(file);
  return GetMetaFileW(w.c_str());
}

UINT WINAPI GetMetaFileBitsEx(HMETAFILE h, UINT n, LPVOID out) {
  MetaFileObj* o = GdiAs<MetaFileObj>(h, OBJ_METAFILE);
  if (!o) return 0;
  if (!out) return (UINT)o->bits.size();
  UINT k = std::min<UINT>(n, (UINT)o->bits.size());
  memcpy(out, o->bits.data(), k);
  return k;
}

HMETAFILE WINAPI SetMetaFileBitsEx(UINT n, const BYTE* bits) {
  if (!bits || n < 18) {
    SetLastError(ERROR_INVALID_DATA);
    return NULL;
  }
  return NewMetaFile(std::vector<BYTE>(bits, bits + n));
}

BOOL WINAPI PlayMetaFile(HDC hdc, HMETAFILE h) {
  MetaFileObj* o = GdiAs<MetaFileObj>(h, OBJ_METAFILE);
  if (!o || !DCOf(hdc)) return FALSE;
  std::vector<BYTE> bits = o->bits;
  return Play(hdc, bits);
}

HDC WINAPI CreateEnhMetaFileW(HDC ref, LPCWSTR file, const RECT* frame, LPCWSTR desc) {
  (void)ref;
  HDC h = NewMetaDC(true);
  if (!h) return NULL;
  MetaRecorder* m = DCOf(h)->meta.get();
  if (file) m->file = PosixPath(file);
  if (frame) {
    m->frameGiven = true;
    m->frame = *frame;
  }
  if (desc) {
    // The application's name, a zero, the picture's name (two strings).
    const wchar_t* p = desc;
    m->description = p;
    p += wcslen(p) + 1;
    m->description += L'\0';
    m->description += p;
  }
  return h;
}
HDC WINAPI CreateEnhMetaFileA(HDC ref, LPCSTR file, const RECT* frame, LPCSTR desc) {
  std::wstring f = file ? Widen(file) : std::wstring();
  std::wstring d;
  if (desc) {
    d = Widen(desc);
    d += L'\0';
    d += Widen(desc + strlen(desc) + 1);
    d += L'\0';
  }
  return CreateEnhMetaFileW(ref, file ? f.c_str() : NULL, frame, desc ? d.c_str() : NULL);
}

HENHMETAFILE WINAPI CloseEnhMetaFile(HDC hdc) {
  std::shared_ptr<MetaRecorder> m = EndRecording(hdc, true);
  if (!m) return NULL;
  std::vector<BYTE> wmf = Finish(m.get(), false);
  std::vector<BYTE> bits = BuildEmf(m.get(), wmf);
  if (!m->file.empty() && !WriteFile(m->file, bits)) return NULL;
  return NewEnhMetaFile(std::move(bits));
}

BOOL WINAPI DeleteEnhMetaFile(HENHMETAFILE h) {
  if (!GdiAs<EnhMetaFileObj>(h, OBJ_ENHMETAFILE)) return FALSE;
  FreeGdiHandle(h);
  return TRUE;
}

HENHMETAFILE WINAPI CopyEnhMetaFileW(HENHMETAFILE h, LPCWSTR file) {
  EnhMetaFileObj* o = GdiAs<EnhMetaFileObj>(h, OBJ_ENHMETAFILE);
  if (!o) return NULL;
  if (file && !WriteFile(PosixPath(file), o->bits)) return NULL;
  return NewEnhMetaFile(o->bits);
}
HENHMETAFILE WINAPI CopyEnhMetaFileA(HENHMETAFILE h, LPCSTR file) {
  if (!file) return CopyEnhMetaFileW(h, NULL);
  std::wstring w = Widen(file);
  return CopyEnhMetaFileW(h, w.c_str());
}

HENHMETAFILE WINAPI GetEnhMetaFileW(LPCWSTR file) {
  std::vector<BYTE> bits;
  if (!file || !ReadFileBytes(PosixPath(file), &bits)) return NULL;
  if (!ValidEmf(bits)) {
    SetLastError(ERROR_INVALID_DATA);
    return NULL;
  }
  return NewEnhMetaFile(std::move(bits));
}
HENHMETAFILE WINAPI GetEnhMetaFileA(LPCSTR file) {
  if (!file) return NULL;
  std::wstring w = Widen(file);
  return GetEnhMetaFileW(w.c_str());
}

UINT WINAPI GetEnhMetaFileBits(HENHMETAFILE h, UINT n, LPBYTE out) {
  EnhMetaFileObj* o = GdiAs<EnhMetaFileObj>(h, OBJ_ENHMETAFILE);
  if (!o) return 0;
  if (!out) return (UINT)o->bits.size();
  UINT k = std::min<UINT>(n, (UINT)o->bits.size());
  memcpy(out, o->bits.data(), k);
  return k;
}

HENHMETAFILE WINAPI SetEnhMetaFileBits(UINT n, const BYTE* bits) {
  if (!bits) return NULL;
  std::vector<BYTE> v(bits, bits + n);
  if (!ValidEmf(v)) {
    SetLastError(ERROR_INVALID_DATA);
    return NULL;
  }
  return NewEnhMetaFile(std::move(v));
}

UINT WINAPI GetEnhMetaFileHeader(HENHMETAFILE h, UINT n, LPENHMETAHEADER out) {
  EnhMetaFileObj* o = GdiAs<EnhMetaFileObj>(h, OBJ_ENHMETAFILE);
  if (!o) return 0;
  UINT size = std::min<UINT>(GetD(o->bits.data() + 4), (UINT)sizeof(ENHMETAHEADER));
  if (!out) return size;
  UINT k = std::min(n, size);
  memset(out, 0, std::min<UINT>(n, sizeof(ENHMETAHEADER)));
  memcpy(out, o->bits.data(), k);
  return k;
}

// The frame (0.01 mm) maps onto the rectangle; the picture inside is in
// reference pixels.
BOOL WINAPI PlayEnhMetaFile(HDC hdc, HENHMETAFILE h, const RECT* rc) {
  EnhMetaFileObj* o = GdiAs<EnhMetaFileObj>(h, OBJ_ENHMETAFILE);
  if (!o || !rc || !DCOf(hdc)) return FALSE;
  std::vector<BYTE> wmf;
  if (!EmfPicture(o->bits, &wmf)) return TRUE;  // a picture of other records: not drawn
  const BYTE* p = o->bits.data();
  LONG fl = (LONG)GetD(p + 24), ft = (LONG)GetD(p + 28), fr = (LONG)GetD(p + 32), fb = (LONG)GetD(p + 36);
  int x0 = MulDiv(fl, kRefDpi, 2540), y0 = MulDiv(ft, kRefDpi, 2540);
  int w = MulDiv(fr - fl, kRefDpi, 2540), hgt = MulDiv(fb - ft, kRefDpi, 2540);
  if (w == 0) w = 1;
  if (hgt == 0) hgt = 1;
  POINT d[2] = {{rc->left, rc->top}, {rc->right, rc->bottom}};
  LPtoDP(hdc, d, 2);
  int saved = SaveDC(hdc);
  XFORM identity = {1, 0, 0, 1, 0, 0};
  SetWorldTransform(hdc, &identity);
  SetMapMode(hdc, MM_ANISOTROPIC);
  SetWindowOrgEx(hdc, x0, y0, NULL);
  SetWindowExtEx(hdc, w, hgt, NULL);
  SetViewportOrgEx(hdc, d[0].x, d[0].y, NULL);
  SetViewportExtEx(hdc, d[1].x - d[0].x, d[1].y - d[0].y, NULL);
  BOOL ok = Play(hdc, wmf);
  RestoreDC(hdc, saved);
  return ok;
}

}  // extern "C"
