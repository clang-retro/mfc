// storage.cpp - structured storage: compound files ("docfiles") read and
// written in the Compound File Binary format, so that documents move
// between these systems and Windows.
//
// A compound file is a tree of storages and streams. It is read whole
// when it is opened, into a tree of elements in memory, and written
// whole: a transacted root when it commits, a direct one when its last
// object goes away (or it commits). A transacted storage works on a
// copy of its elements, which Commit puts in its parent's place and
// Revert throws away. Streams are always direct within their storage.
//
// The layout: a 512-byte header; then sectors (512 bytes in version 3,
// which is what is written; version 4's 4096-byte sectors are read too)
// chained by the file allocation table (FAT), which the header's DIFAT
// locates; a directory of 128-byte entries, siblings in a binary tree;
// and the streams under 4096 bytes in 64-byte mini sectors inside the
// root entry's mini stream, chained by the mini FAT.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <objbase.h>
#include <ole2.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wctype.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace {

std::recursive_mutex& StgLock() {
  static std::recursive_mutex* m = new std::recursive_mutex;
  return *m;
}
typedef std::lock_guard<std::recursive_mutex> Guard;

// ---------------------------------------------------------------------
// Elements

struct Node {
  std::wstring name;
  bool storage = false;
  CLSID clsid = {};
  DWORD stateBits = 0;
  FILETIME ctime = {}, mtime = {};
  std::vector<BYTE> data;                       // a stream's
  std::vector<std::shared_ptr<Node>> children;  // a storage's
  int opens = 0;                                // objects open on it (share-exclusive)
};
typedef std::shared_ptr<Node> NodeP;

NodeP DeepCopy(const Node& n) {
  NodeP c = std::make_shared<Node>();
  c->name = n.name;
  c->storage = n.storage;
  c->clsid = n.clsid;
  c->stateBits = n.stateBits;
  c->ctime = n.ctime;
  c->mtime = n.mtime;
  c->data = n.data;
  for (const NodeP& k : n.children) c->children.push_back(DeepCopy(*k));
  return c;
}

// Takes another element's contents (not its name or open count).
void Assign(Node& to, const Node& from) {
  NodeP c = DeepCopy(from);
  to.storage = c->storage;
  to.clsid = c->clsid;
  to.stateBits = c->stateBits;
  to.ctime = c->ctime;
  to.mtime = c->mtime;
  to.data.swap(c->data);
  to.children.swap(c->children);
}

FILETIME Now() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return ft;
}

// Names compare as the format orders them: shorter first, then by
// upper case.
wchar_t Upper(wchar_t c) { return (wchar_t)towupper((wint_t)c); }
bool SameName(const std::wstring& a, const std::wstring& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (Upper(a[i]) != Upper(b[i])) return false;
  return true;
}
bool NameLess(const std::wstring& a, const std::wstring& b) {
  if (a.size() != b.size()) return a.size() < b.size();
  for (size_t i = 0; i < a.size(); i++) {
    wchar_t x = Upper(a[i]), y = Upper(b[i]);
    if (x != y) return x < y;
  }
  return false;
}

NodeP Find(const Node& parent, const std::wstring& name) {
  for (const NodeP& k : parent.children)
    if (SameName(k->name, name)) return k;
  return NodeP();
}

void Remove(Node& parent, const NodeP& k) {
  auto& v = parent.children;
  v.erase(std::remove(v.begin(), v.end(), k), v.end());
}

// Up to 31 characters, none of \ / : !
HRESULT CheckName(const OLECHAR* name, std::wstring* out) {
  if (name == NULL) return STG_E_INVALIDNAME;
  std::wstring s(name);
  if (s.empty() || s.size() > 31) return STG_E_INVALIDNAME;
  for (wchar_t c : s)
    if (c == L'\\' || c == L'/' || c == L':' || c == L'!') return STG_E_INVALIDNAME;
  *out = s;
  return S_OK;
}

LPOLESTR CoString(const std::wstring& s) {
  LPOLESTR p = (LPOLESTR)CoTaskMemAlloc((s.size() + 1) * sizeof(OLECHAR));
  if (p) memcpy(p, s.c_str(), (s.size() + 1) * sizeof(OLECHAR));
  return p;
}

// ---------------------------------------------------------------------
// The file format

const BYTE kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
const DWORD kFree = 0xFFFFFFFF, kEnd = 0xFFFFFFFE, kFatSect = 0xFFFFFFFD, kDifSect = 0xFFFFFFFC;
const DWORD kNoStream = 0xFFFFFFFF;
const DWORD kMiniCutoff = 4096;

DWORD Get32(const BYTE* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
WORD Get16(const BYTE* p) { return (WORD)(p[0] | (p[1] << 8)); }
void Put32(BYTE* p, DWORD v) {
  p[0] = (BYTE)v, p[1] = (BYTE)(v >> 8), p[2] = (BYTE)(v >> 16), p[3] = (BYTE)(v >> 24);
}
void Put16(BYTE* p, WORD v) { p[0] = (BYTE)v, p[1] = (BYTE)(v >> 8); }

void GetGuid(const BYTE* p, CLSID* g) {
  g->Data1 = Get32(p);
  g->Data2 = Get16(p + 4);
  g->Data3 = Get16(p + 6);
  memcpy(g->Data4, p + 8, 8);
}
void PutGuid(BYTE* p, const CLSID& g) {
  Put32(p, g.Data1);
  Put16(p + 4, g.Data2);
  Put16(p + 6, g.Data3);
  memcpy(p + 8, g.Data4, 8);
}
void GetTime(const BYTE* p, FILETIME* ft) {
  ft->dwLowDateTime = Get32(p);
  ft->dwHighDateTime = Get32(p + 4);
}
void PutTime(BYTE* p, const FILETIME& ft) {
  Put32(p, ft.dwLowDateTime);
  Put32(p + 4, ft.dwHighDateTime);
}

struct Reader {
  const std::vector<BYTE>& file;
  DWORD shift = 9, size = 512;
  std::vector<DWORD> fat, miniFat;
  std::vector<BYTE> dir, ministream;
  DWORD nEntries = 0;

  explicit Reader(const std::vector<BYTE>& f) : file(f) {}

  DWORD SectorCount() const { return file.size() <= size ? 0 : (DWORD)((file.size() - size + size - 1) / size); }
  const BYTE* Sector(DWORD i) const {
    size_t off = ((size_t)i + 1) << shift;
    if (i >= SectorCount() || off >= file.size()) return NULL;
    return file.data() + off;
  }
  // The bytes of the sector, short at the end of the file.
  size_t SectorBytes(DWORD i) const {
    size_t off = ((size_t)i + 1) << shift;
    return off >= file.size() ? 0 : std::min<size_t>(size, file.size() - off);
  }

  // A chain of sectors (or mini sectors) through a table.
  bool Chain(const std::vector<DWORD>& table, DWORD start, std::vector<DWORD>* out) const {
    out->clear();
    DWORD s = start;
    while (s != kEnd) {
      if (s >= table.size() || out->size() > table.size()) return false;
      out->push_back(s);
      s = table[s];
    }
    return true;
  }

  bool ReadChain(DWORD start, ULONGLONG len, std::vector<BYTE>* out, bool all) const {
    std::vector<DWORD> chain;
    out->clear();
    if (start == kEnd || (len == 0 && !all)) return true;
    if (!Chain(fat, start, &chain)) return false;
    for (DWORD s : chain) {
      const BYTE* p = Sector(s);
      if (p == NULL) return false;
      out->insert(out->end(), p, p + SectorBytes(s));
      if (!all && out->size() >= len) break;
    }
    if (!all) {
      if (out->size() < len) return false;
      out->resize((size_t)len);
    }
    return true;
  }

  bool ReadMini(DWORD start, ULONGLONG len, std::vector<BYTE>* out) const {
    std::vector<DWORD> chain;
    out->clear();
    if (len == 0) return true;
    if (!Chain(miniFat, start, &chain)) return false;
    for (DWORD s : chain) {
      size_t off = (size_t)s * 64;
      if (off + 64 > ministream.size()) {
        if (off >= ministream.size()) return false;
        out->insert(out->end(), ministream.begin() + off, ministream.end());
      } else {
        out->insert(out->end(), ministream.begin() + off, ministream.begin() + off + 64);
      }
      if (out->size() >= len) break;
    }
    if (out->size() < len) return false;
    out->resize((size_t)len);
    return true;
  }

  const BYTE* Entry(DWORD i) const { return i < nEntries ? dir.data() + (size_t)i * 128 : NULL; }

  HRESULT Load(NodeP* root) {
    if (file.size() < 512 || memcmp(file.data(), kSignature, 8) != 0) return STG_E_FILEALREADYEXISTS;
    const BYTE* h = file.data();
    WORD major = Get16(h + 0x1A);
    shift = Get16(h + 0x1E);
    if (!((major == 3 && shift == 9) || (major == 4 && shift == 12)) || Get16(h + 0x20) != 6)
      return STG_E_DOCFILECORRUPT;
    size = 1u << shift;
    DWORD nFat = Get32(h + 0x2C), firstDir = Get32(h + 0x30);
    DWORD firstMiniFat = Get32(h + 0x3C), nMiniFat = Get32(h + 0x40);
    DWORD firstDifat = Get32(h + 0x44), nDifat = Get32(h + 0x48);
    // The FAT's sectors: 109 in the header, the rest in DIFAT sectors.
    std::vector<DWORD> fatSectors;
    for (int i = 0; i < 109 && fatSectors.size() < nFat; i++) fatSectors.push_back(Get32(h + 0x4C + i * 4));
    DWORD d = firstDifat;
    for (DWORD k = 0; k < nDifat && d != kEnd && d != kFree && fatSectors.size() < nFat; k++) {
      const BYTE* p = Sector(d);
      if (p == NULL) return STG_E_DOCFILECORRUPT;
      DWORD per = size / 4 - 1;
      for (DWORD i = 0; i < per && fatSectors.size() < nFat; i++) fatSectors.push_back(Get32(p + i * 4));
      d = Get32(p + per * 4);
    }
    if (fatSectors.size() < nFat) return STG_E_DOCFILECORRUPT;
    for (DWORD s : fatSectors) {
      const BYTE* p = Sector(s);
      if (p == NULL) return STG_E_DOCFILECORRUPT;
      size_t n = SectorBytes(s) / 4;
      for (size_t i = 0; i < n; i++) fat.push_back(Get32(p + i * 4));
    }
    if (!ReadChain(firstDir, 0, &dir, true)) return STG_E_DOCFILECORRUPT;
    nEntries = (DWORD)(dir.size() / 128);
    if (nEntries == 0 || dir[0x42] != 5) return STG_E_DOCFILECORRUPT;
    if (nMiniFat && firstMiniFat != kEnd) {
      std::vector<BYTE> mf;
      if (!ReadChain(firstMiniFat, 0, &mf, true)) return STG_E_DOCFILECORRUPT;
      for (size_t i = 0; i + 4 <= mf.size(); i += 4) miniFat.push_back(Get32(mf.data() + i));
    }
    const BYTE* r = Entry(0);
    ULONGLONG rootSize = Get32(r + 0x78);
    if (major == 4) rootSize |= (ULONGLONG)Get32(r + 0x7C) << 32;
    if (!ReadChain(Get32(r + 0x74), rootSize, &ministream, false)) return STG_E_DOCFILECORRUPT;
    NodeP n = std::make_shared<Node>();
    n->storage = true;
    GetGuid(r + 0x50, &n->clsid);
    n->stateBits = Get32(r + 0x60);
    GetTime(r + 0x64, &n->ctime);
    GetTime(r + 0x6C, &n->mtime);
    std::vector<bool> seen(nEntries, false);
    seen[0] = true;
    if (!Children(Get32(r + 0x4C), n.get(), &seen, major, 0)) return STG_E_DOCFILECORRUPT;
    *root = n;
    return S_OK;
  }

  // The siblings' tree under a storage, in order.
  bool Children(DWORD id, Node* parent, std::vector<bool>* seen, WORD major, int depth) {
    if (id == kNoStream) return true;
    if (depth > 1000 || id >= nEntries || (*seen)[id]) return false;
    (*seen)[id] = true;
    const BYTE* e = Entry(id);
    if (!Children(Get32(e + 0x44), parent, seen, major, depth + 1)) return false;
    BYTE type = e[0x42];
    if (type == 1 || type == 2) {
      NodeP n = std::make_shared<Node>();
      WORD nameLen = Get16(e + 0x40);
      if (nameLen > 64) nameLen = 64;
      for (int i = 0; i + 2 <= nameLen - 2; i += 2) n->name += (wchar_t)Get16(e + i);
      n->storage = type == 1;
      GetGuid(e + 0x50, &n->clsid);
      n->stateBits = Get32(e + 0x60);
      GetTime(e + 0x64, &n->ctime);
      GetTime(e + 0x6C, &n->mtime);
      if (n->storage) {
        if (!Children(Get32(e + 0x4C), n.get(), seen, major, depth + 1)) return false;
      } else {
        ULONGLONG len = Get32(e + 0x78);
        if (major == 4) len |= (ULONGLONG)Get32(e + 0x7C) << 32;
        DWORD start = Get32(e + 0x74);
        bool ok = len < kMiniCutoff ? ReadMini(start, len, &n->data) : ReadChain(start, len, &n->data, false);
        if (!ok) return false;
      }
      parent->children.push_back(n);
    }
    return Children(Get32(e + 0x48), parent, seen, major, depth + 1);
  }
};

// Writes the tree as a version 3 compound file.
struct Writer {
  struct Entry {
    const Node* node;
    DWORD left = kNoStream, right = kNoStream, child = kNoStream;
    DWORD start = kEnd;
  };
  std::vector<Entry> entries;

  // Lays out one storage's children: a balanced tree of them, by name.
  DWORD Tree(std::vector<DWORD>& ids, size_t lo, size_t hi) {
    if (lo >= hi) return kNoStream;
    size_t mid = (lo + hi) / 2;
    DWORD id = ids[mid];
    entries[id].left = Tree(ids, lo, mid);
    entries[id].right = Tree(ids, mid + 1, hi);
    return id;
  }
  void Add(DWORD parent) {
    std::vector<const Node*> kids;
    for (const NodeP& k : entries[parent].node->children) kids.push_back(k.get());
    std::sort(kids.begin(), kids.end(), [](const Node* a, const Node* b) { return NameLess(a->name, b->name); });
    std::vector<DWORD> ids;
    for (const Node* k : kids) {
      ids.push_back((DWORD)entries.size());
      Entry e;
      e.node = k;
      entries.push_back(e);
    }
    entries[parent].child = Tree(ids, 0, ids.size());
    for (DWORD id : ids)
      if (entries[id].node->storage) Add(id);
  }

  std::vector<BYTE> Write(const Node& root) {
    const DWORD S = 512;
    Entry r;
    r.node = &root;
    entries.push_back(r);
    Add(0);
    // The small streams go in the mini stream.
    std::vector<BYTE> mini;
    std::vector<DWORD> miniFat;
    std::vector<DWORD> big;  // the entries with sectors of their own
    for (DWORD i = 1; i < entries.size(); i++) {
      const Node* n = entries[i].node;
      if (n->storage || n->data.empty()) continue;
      if (n->data.size() < kMiniCutoff) {
        DWORD first = (DWORD)(mini.size() / 64), count = (DWORD)((n->data.size() + 63) / 64);
        entries[i].start = first;
        for (DWORD k = 0; k < count; k++) miniFat.push_back(k + 1 < count ? first + k + 1 : kEnd);
        mini.insert(mini.end(), n->data.begin(), n->data.end());
        mini.resize((size_t)(first + count) * 64, 0);
      } else {
        big.push_back(i);
      }
    }
    auto Sectors = [&](size_t bytes) { return (DWORD)((bytes + S - 1) / S); };
    // Sector numbers: the big streams, the mini stream, the mini FAT,
    // the directory, then the FAT and DIFAT sectors.
    DWORD next = 0;
    for (DWORD i : big) {
      entries[i].start = next;
      next += Sectors(entries[i].node->data.size());
    }
    DWORD miniStart = mini.empty() ? kEnd : next;
    next += Sectors(mini.size());
    DWORD nMiniFat = Sectors(miniFat.size() * 4);
    DWORD miniFatStart = nMiniFat ? next : kEnd;
    next += nMiniFat;
    DWORD nDir = Sectors(entries.size() * 128);
    DWORD dirStart = next;
    next += nDir;
    DWORD nFat = 0, nDifat = 0;
    for (;;) {
      DWORD total = next + nFat + nDifat;
      DWORD f = (total + 127) / 128;
      DWORD d = f > 109 ? (f - 109 + 126) / 127 : 0;
      if (f == nFat && d == nDifat) break;
      nFat = f;
      nDifat = d;
    }
    DWORD fatStart = next, difatStart = next + nFat;
    DWORD total = next + nFat + nDifat;
    std::vector<DWORD> fat((size_t)nFat * 128, kFree);
    auto Link = [&](DWORD first, DWORD count) {
      for (DWORD k = 0; k < count; k++) fat[first + k] = k + 1 < count ? first + k + 1 : kEnd;
    };
    for (DWORD i : big) Link(entries[i].start, Sectors(entries[i].node->data.size()));
    if (!mini.empty()) Link(miniStart, Sectors(mini.size()));
    if (nMiniFat) Link(miniFatStart, nMiniFat);
    Link(dirStart, nDir);
    for (DWORD k = 0; k < nFat; k++) fat[fatStart + k] = kFatSect;
    for (DWORD k = 0; k < nDifat; k++) fat[difatStart + k] = kDifSect;

    std::vector<BYTE> out((size_t)(total + 1) * S, 0);
    BYTE* h = out.data();
    memcpy(h, kSignature, 8);
    Put16(h + 0x18, 0x003E);
    Put16(h + 0x1A, 3);
    Put16(h + 0x1C, 0xFFFE);
    Put16(h + 0x1E, 9);
    Put16(h + 0x20, 6);
    Put32(h + 0x2C, nFat);
    Put32(h + 0x30, dirStart);
    Put32(h + 0x38, kMiniCutoff);
    Put32(h + 0x3C, miniFatStart);
    Put32(h + 0x40, nMiniFat);
    Put32(h + 0x44, nDifat ? difatStart : kEnd);
    Put32(h + 0x48, nDifat);
    for (DWORD i = 0; i < 109; i++) Put32(h + 0x4C + i * 4, i < nFat ? fatStart + i : kFree);
    auto Sec = [&](DWORD i) { return out.data() + (size_t)(i + 1) * S; };
    for (DWORD i : big) {
      const std::vector<BYTE>& d = entries[i].node->data;
      memcpy(Sec(entries[i].start), d.data(), d.size());
    }
    if (!mini.empty()) memcpy(Sec(miniStart), mini.data(), mini.size());
    for (size_t k = 0; k < (size_t)nMiniFat * 128; k++)
      Put32(Sec(miniFatStart) + k * 4, k < miniFat.size() ? miniFat[k] : kFree);
    // The directory; unused entries have no siblings or children.
    for (size_t k = 0; k < (size_t)nDir * 4; k++) {
      BYTE* e = Sec(dirStart) + k * 128;
      Put32(e + 0x44, kNoStream);
      Put32(e + 0x48, kNoStream);
      Put32(e + 0x4C, kNoStream);
      if (k >= entries.size()) continue;
      const Entry& en = entries[k];
      const Node* n = en.node;
      std::wstring name = k == 0 ? std::wstring(L"Root Entry") : n->name;
      size_t i = 0;
      for (; i < name.size() && i < 31; i++) {
        unsigned c = (unsigned)name[i];
        Put16(e + i * 2, (WORD)(c > 0xFFFF ? '?' : c));
      }
      Put16(e + 0x40, (WORD)((i + 1) * 2));
      e[0x42] = k == 0 ? 5 : (n->storage ? 1 : 2);
      e[0x43] = 1;  // black
      Put32(e + 0x44, en.left);
      Put32(e + 0x48, en.right);
      Put32(e + 0x4C, en.child);
      PutGuid(e + 0x50, n->clsid);
      Put32(e + 0x60, n->stateBits);
      if (n->storage && k != 0) {
        PutTime(e + 0x64, n->ctime);
        PutTime(e + 0x6C, n->mtime);
      } else if (k == 0) {
        PutTime(e + 0x6C, n->mtime);
      }
      if (k == 0) {
        Put32(e + 0x74, miniStart);
        Put32(e + 0x78, (DWORD)mini.size());
      } else if (!n->storage) {
        Put32(e + 0x74, n->data.empty() ? kEnd : en.start);
        Put32(e + 0x78, (DWORD)n->data.size());
      }
    }
    for (size_t k = 0; k < fat.size(); k++) Put32(Sec(fatStart + (DWORD)(k / 128)) + (k % 128) * 4, fat[k]);
    for (DWORD k = 0; k < nDifat; k++) {
      BYTE* p = Sec(difatStart + k);
      for (DWORD i = 0; i < 127; i++) {
        DWORD f = 109 + k * 127 + i;
        Put32(p + i * 4, f < nFat ? fatStart + f : kFree);
      }
      Put32(p + 127 * 4, k + 1 < nDifat ? difatStart + k + 1 : kEnd);
    }
    return out;
  }
};

// ---------------------------------------------------------------------
// The bytes under a root: a file, or the program's ILockBytes

class FileLockBytes final : public ILockBytes {
 public:
  FileLockBytes(int fd, const std::wstring& name) : fd_(fd), name_(name) {}
  ~FileLockBytes() { close(fd_); }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ILockBytes)) {
      *out = static_cast<ILockBytes*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(ReadAt)(ULARGE_INTEGER off, void* pv, ULONG cb, ULONG* read) override {
    ssize_t n = pread(fd_, pv, cb, (off_t)off.QuadPart);
    if (read) *read = n > 0 ? (ULONG)n : 0;
    return n < 0 ? STG_E_READFAULT : S_OK;
  }
  STDMETHOD(WriteAt)(ULARGE_INTEGER off, const void* pv, ULONG cb, ULONG* written) override {
    ULONG done = 0;
    while (done < cb) {
      ssize_t n = pwrite(fd_, (const BYTE*)pv + done, cb - done, (off_t)(off.QuadPart + done));
      if (n <= 0) break;
      done += (ULONG)n;
    }
    if (written) *written = done;
    return done == cb ? S_OK : (errno == ENOSPC ? STG_E_MEDIUMFULL : STG_E_WRITEFAULT);
  }
  STDMETHOD(Flush)() override { return S_OK; }
  STDMETHOD(SetSize)(ULARGE_INTEGER cb) override {
    return ftruncate(fd_, (off_t)cb.QuadPart) == 0 ? S_OK : STG_E_MEDIUMFULL;
  }
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return S_OK; }
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return S_OK; }
  STDMETHOD(Stat)(STATSTG* st, DWORD flags) override {
    if (st == NULL) return STG_E_INVALIDPOINTER;
    memset(st, 0, sizeof(*st));
    st->type = STGTY_LOCKBYTES;
    struct stat sb;
    if (fstat(fd_, &sb) == 0) {
      st->cbSize.QuadPart = (ULONGLONG)sb.st_size;
      st->mtime = FileTimeFromUnix(sb.st_mtime, 0);
    }
    if (!(flags & STATFLAG_NONAME)) st->pwcsName = CoString(name_);
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  int fd_;
  std::wstring name_;
};

HRESULT ReadAll(ILockBytes* lb, std::vector<BYTE>* out) {
  STATSTG st;
  HRESULT hr = lb->Stat(&st, STATFLAG_NONAME);
  if (FAILED(hr)) return hr;
  out->assign((size_t)st.cbSize.QuadPart, 0);
  ULONG n = 0;
  if (!out->empty()) {
    ULARGE_INTEGER z;
    z.QuadPart = 0;
    hr = lb->ReadAt(z, out->data(), (ULONG)out->size(), &n);
    if (FAILED(hr)) return hr;
    out->resize(n);
  }
  return S_OK;
}

// What the objects of one compound file share.
struct RootFile {
  ILockBytes* lb;
  std::wstring path;  // a file's, for Stat and delete-on-release
  bool deleteOnRelease = false;
  bool direct = true;
  bool dirty = false;
  NodeP tree;  // the root's working elements

  ~RootFile() {
    if (direct && dirty) Save();
    lb->Release();
    if (deleteOnRelease && !path.empty()) unlink(Narrow(path.c_str()).c_str());
  }
  HRESULT Save() {
    std::vector<BYTE> bytes = Writer().Write(*tree);
    ULARGE_INTEGER z, size;
    z.QuadPart = 0;
    size.QuadPart = bytes.size();
    ULONG n = 0;
    HRESULT hr = lb->WriteAt(z, bytes.data(), (ULONG)bytes.size(), &n);
    if (SUCCEEDED(hr)) hr = lb->SetSize(size);
    if (SUCCEEDED(hr)) hr = lb->Flush();
    if (SUCCEEDED(hr)) dirty = false;
    return hr;
  }
};
typedef std::shared_ptr<RootFile> RootP;

bool CanWrite(DWORD mode) { return (mode & 3) != STGM_READ; }
bool CanRead(DWORD mode) { return (mode & 3) != STGM_WRITE; }

// ---------------------------------------------------------------------
// Streams

class Stream final : public IStream {
 public:
  Stream(RootP root, NodeP node, DWORD mode, bool counted) : root_(root), node_(node), mode_(mode), counted_(counted) {
    if (counted_) node_->opens++;
  }
  ~Stream() {
    Guard g(StgLock());
    if (counted_) node_->opens--;
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStream) ||
        IsEqualIID(riid, IID_ISequentialStream)) {
      *out = static_cast<IStream*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(Read)(void* pv, ULONG cb, ULONG* read) override {
    Guard g(StgLock());
    if (read) *read = 0;
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    if (!CanRead(mode_)) return STG_E_ACCESSDENIED;
    const std::vector<BYTE>& d = node_->data;
    ULONG n = pos_ >= d.size() ? 0 : (ULONG)std::min<ULONGLONG>(cb, d.size() - pos_);
    if (n) memcpy(pv, d.data() + pos_, n);
    pos_ += n;
    if (read) *read = n;
    return S_OK;
  }
  STDMETHOD(Write)(const void* pv, ULONG cb, ULONG* written) override {
    Guard g(StgLock());
    if (written) *written = 0;
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    if (cb == 0) return S_OK;
    std::vector<BYTE>& d = node_->data;
    if (pos_ + cb > d.size()) d.resize((size_t)(pos_ + cb), 0);
    memcpy(d.data() + pos_, pv, cb);
    pos_ += cb;
    root_->dirty = true;
    if (written) *written = cb;
    return S_OK;
  }
  STDMETHOD(Seek)(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override {
    Guard g(StgLock());
    LONGLONG base;
    switch (origin) {
      case STREAM_SEEK_SET: base = 0; break;
      case STREAM_SEEK_CUR: base = (LONGLONG)pos_; break;
      case STREAM_SEEK_END: base = (LONGLONG)node_->data.size(); break;
      default: return STG_E_INVALIDFUNCTION;
    }
    LONGLONG p = base + move.QuadPart;
    if (p < 0) return STG_E_INVALIDFUNCTION;
    pos_ = (ULONGLONG)p;
    if (newPos) newPos->QuadPart = pos_;
    return S_OK;
  }
  STDMETHOD(SetSize)(ULARGE_INTEGER size) override {
    Guard g(StgLock());
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    node_->data.resize((size_t)size.QuadPart, 0);
    root_->dirty = true;
    return S_OK;
  }
  STDMETHOD(CopyTo)(IStream* to, ULARGE_INTEGER cb, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
    if (to == NULL) return STG_E_INVALIDPOINTER;
    ULONGLONG want = cb.QuadPart, r = 0, w = 0;
    BYTE buf[16384];
    HRESULT hr = S_OK;
    while (want > 0) {
      ULONG n = 0, m = 0;
      hr = Read(buf, (ULONG)std::min<ULONGLONG>(want, sizeof buf), &n);
      if (FAILED(hr) || n == 0) break;
      r += n;
      hr = to->Write(buf, n, &m);
      w += m;
      if (FAILED(hr)) break;
      want -= n;
    }
    if (read) read->QuadPart = r;
    if (written) written->QuadPart = w;
    return hr;
  }
  STDMETHOD(Commit)(DWORD) override { return S_OK; }
  STDMETHOD(Revert)() override { return S_OK; }
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(Stat)(STATSTG* st, DWORD flags) override {
    Guard g(StgLock());
    if (st == NULL) return STG_E_INVALIDPOINTER;
    memset(st, 0, sizeof(*st));
    if (!(flags & STATFLAG_NONAME)) st->pwcsName = CoString(node_->name);
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = node_->data.size();
    st->grfMode = mode_;
    return S_OK;
  }
  STDMETHOD(Clone)(IStream** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    Stream* s = new Stream(root_, node_, mode_, false);
    s->pos_ = pos_;
    *out = s;
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  RootP root_;
  NodeP node_;
  DWORD mode_;
  bool counted_;
  ULONGLONG pos_ = 0;
};

// ---------------------------------------------------------------------
// Enumerating a storage's elements

STATSTG StatOf(const Node& n, bool name) {
  STATSTG st;
  memset(&st, 0, sizeof st);
  if (name) st.pwcsName = CoString(n.name);
  st.type = n.storage ? STGTY_STORAGE : STGTY_STREAM;
  st.cbSize.QuadPart = n.storage ? 0 : n.data.size();
  st.ctime = n.ctime;
  st.mtime = n.mtime;
  st.clsid = n.clsid;
  st.grfStateBits = n.stateBits;
  return st;
}

class Enum final : public IEnumSTATSTG {
 public:
  explicit Enum(std::vector<NodeP> items) : items_(std::move(items)) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumSTATSTG)) {
      *out = static_cast<IEnumSTATSTG*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(Next)(ULONG celt, STATSTG* out, ULONG* fetched) override {
    Guard g(StgLock());
    if (out == NULL || (celt > 1 && fetched == NULL)) return STG_E_INVALIDPOINTER;
    ULONG n = 0;
    while (n < celt && pos_ < items_.size()) out[n++] = StatOf(*items_[pos_++], true);
    if (fetched) *fetched = n;
    return n == celt ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG celt) override {
    size_t left = items_.size() - pos_;
    pos_ += std::min<size_t>(celt, left);
    return celt <= left ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    pos_ = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnumSTATSTG** out) override {
    if (out == NULL) return STG_E_INVALIDPOINTER;
    Enum* e = new Enum(items_);
    e->pos_ = pos_;
    *out = e;
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  std::vector<NodeP> items_;
  size_t pos_ = 0;
};

// ---------------------------------------------------------------------
// Storages

class Storage final : public IStorage {
 public:
  // node: the elements this object shows. base: for a transacted
  // storage, the elements Commit replaces (node is a copy of them).
  Storage(RootP root, NodeP node, NodeP base, DWORD mode, bool isRoot)
      : root_(root), node_(node), base_(base), mode_(mode), isRoot_(isRoot) {
    if (!isRoot_ && base_) base_->opens++;
  }
  ~Storage() {
    Guard g(StgLock());
    if (!isRoot_ && base_) base_->opens--;
  }

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IStorage)) {
      *out = static_cast<IStorage*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }

  STDMETHOD(CreateStream)(const OLECHAR* name, DWORD mode, DWORD, DWORD, IStream** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    *out = NULL;
    std::wstring n;
    HRESULT hr = CheckName(name, &n);
    if (FAILED(hr)) return hr;
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    if ((mode & 0x70) != STGM_SHARE_EXCLUSIVE || (mode & STGM_TRANSACTED)) return STG_E_INVALIDFLAG;
    NodeP k = Find(*node_, n);
    if (k) {
      if (!(mode & STGM_CREATE)) return STG_E_FILEALREADYEXISTS;
      if (k->opens) return STG_E_ACCESSDENIED;
      Remove(*node_, k);
    }
    k = std::make_shared<Node>();
    k->name = n;
    node_->children.push_back(k);
    Touch();
    *out = new Stream(root_, k, mode, true);
    return S_OK;
  }
  STDMETHOD(OpenStream)(const OLECHAR* name, void*, DWORD mode, DWORD, IStream** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    *out = NULL;
    std::wstring n;
    HRESULT hr = CheckName(name, &n);
    if (FAILED(hr)) return hr;
    if ((mode & 0x70) != STGM_SHARE_EXCLUSIVE || (mode & STGM_TRANSACTED)) return STG_E_INVALIDFLAG;
    if (CanWrite(mode) && !CanWrite(mode_)) return STG_E_ACCESSDENIED;
    NodeP k = Find(*node_, n);
    if (!k || k->storage) return STG_E_FILENOTFOUND;
    if (k->opens) return STG_E_ACCESSDENIED;
    *out = new Stream(root_, k, mode, true);
    return S_OK;
  }
  STDMETHOD(CreateStorage)(const OLECHAR* name, DWORD mode, DWORD, DWORD, IStorage** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    *out = NULL;
    std::wstring n;
    HRESULT hr = CheckName(name, &n);
    if (FAILED(hr)) return hr;
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    if ((mode & 0x70) != STGM_SHARE_EXCLUSIVE) return STG_E_INVALIDFLAG;
    NodeP k = Find(*node_, n);
    if (k) {
      if (!(mode & STGM_CREATE)) return STG_E_FILEALREADYEXISTS;
      if (k->opens) return STG_E_ACCESSDENIED;
      Remove(*node_, k);
    }
    k = std::make_shared<Node>();
    k->name = n;
    k->storage = true;
    k->ctime = k->mtime = Now();
    node_->children.push_back(k);
    Touch();
    *out = Open(k, mode);
    return S_OK;
  }
  STDMETHOD(OpenStorage)(const OLECHAR* name, IStorage*, DWORD mode, SNB, DWORD, IStorage** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    *out = NULL;
    std::wstring n;
    HRESULT hr = CheckName(name, &n);
    if (FAILED(hr)) return hr;
    if ((mode & 0x70) != STGM_SHARE_EXCLUSIVE) return STG_E_INVALIDFLAG;
    if (CanWrite(mode) && !CanWrite(mode_)) return STG_E_ACCESSDENIED;
    NodeP k = Find(*node_, n);
    if (!k || !k->storage) return STG_E_FILENOTFOUND;
    if (k->opens) return STG_E_ACCESSDENIED;
    *out = Open(k, mode);
    return S_OK;
  }

  // Copies this storage's elements into another (any IStorage), merging
  // storages that are there already.
  STDMETHOD(CopyTo)(DWORD ciidExclude, const IID* rgiidExclude, SNB snbExclude, IStorage* dest) override {
    if (dest == NULL) return STG_E_INVALIDPOINTER;
    if (dest == this) return STG_E_ACCESSDENIED;
    bool noStreams = false, noStorages = false;
    for (DWORD i = 0; rgiidExclude && i < ciidExclude; i++) {
      if (IsEqualIID(rgiidExclude[i], IID_IStream)) noStreams = true;
      if (IsEqualIID(rgiidExclude[i], IID_IStorage)) noStorages = true;
    }
    std::vector<NodeP> kids;
    CLSID clsid;
    DWORD bits;
    {
      Guard g(StgLock());
      kids = node_->children;
      clsid = node_->clsid;
      bits = node_->stateBits;
    }
    HRESULT hr = S_OK;
    if (!noStorages) {
      dest->SetClass(clsid);
      dest->SetStateBits(bits, 0xFFFFFFFF);
    }
    for (const NodeP& k : kids) {
      bool skip = false;
      for (SNB p = snbExclude; p && *p; p++) skip |= SameName(k->name, *p);
      if (skip || (k->storage ? noStorages : noStreams)) continue;
      hr = CopyElement(k, dest, k->name.c_str());
      if (FAILED(hr)) return hr;
    }
    return S_OK;
  }
  STDMETHOD(MoveElementTo)(const OLECHAR* name, IStorage* dest, const OLECHAR* newName, DWORD flags) override {
    std::wstring n, nn;
    HRESULT hr = CheckName(name, &n);
    if (SUCCEEDED(hr)) hr = CheckName(newName, &nn);
    if (FAILED(hr)) return hr;
    if (dest == NULL) return STG_E_INVALIDPOINTER;
    NodeP k;
    {
      Guard g(StgLock());
      k = Find(*node_, n);
      if (!k) return STG_E_FILENOTFOUND;
      if (flags == STGMOVE_MOVE && (k->opens || !CanWrite(mode_))) return STG_E_ACCESSDENIED;
    }
    // The destination's element of that name is replaced.
    dest->DestroyElement(nn.c_str());
    hr = CopyElement(k, dest, nn.c_str());
    if (FAILED(hr)) return hr;
    if (flags == STGMOVE_MOVE) {
      Guard g(StgLock());
      Remove(*node_, k);
      Touch();
    }
    return S_OK;
  }

  STDMETHOD(Commit)(DWORD) override {
    Guard g(StgLock());
    if (!CanWrite(mode_)) return S_OK;
    if (isRoot_) {
      if (!root_->direct || root_->dirty) return root_->Save();
      return S_OK;
    }
    if (node_ != base_) {
      Assign(*base_, *node_);
      root_->dirty = true;
    }
    return S_OK;
  }
  STDMETHOD(Revert)() override {
    Guard g(StgLock());
    if (node_ == base_ && !isRoot_) return S_OK;  // direct
    if (isRoot_) {
      if (root_->direct) return S_OK;
      // The file as it was last committed.
      std::vector<BYTE> bytes;
      NodeP tree;
      HRESULT hr = ReadAll(root_->lb, &bytes);
      if (SUCCEEDED(hr)) hr = Reader(bytes).Load(&tree);
      if (FAILED(hr)) return hr;
      Assign(*node_, *tree);
      return S_OK;
    }
    Assign(*node_, *base_);
    return S_OK;
  }
  STDMETHOD(EnumElements)(DWORD, void*, DWORD, IEnumSTATSTG** out) override {
    Guard g(StgLock());
    if (out == NULL) return STG_E_INVALIDPOINTER;
    *out = new Enum(node_->children);
    return S_OK;
  }
  STDMETHOD(DestroyElement)(const OLECHAR* name) override {
    Guard g(StgLock());
    std::wstring n;
    HRESULT hr = CheckName(name, &n);
    if (FAILED(hr)) return hr;
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    NodeP k = Find(*node_, n);
    if (!k) return STG_E_FILENOTFOUND;
    if (k->opens) return STG_E_ACCESSDENIED;
    Remove(*node_, k);
    Touch();
    return S_OK;
  }
  STDMETHOD(RenameElement)(const OLECHAR* oldName, const OLECHAR* newName) override {
    Guard g(StgLock());
    std::wstring o, n;
    HRESULT hr = CheckName(oldName, &o);
    if (SUCCEEDED(hr)) hr = CheckName(newName, &n);
    if (FAILED(hr)) return hr;
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    NodeP k = Find(*node_, o);
    if (!k) return STG_E_FILENOTFOUND;
    NodeP other = Find(*node_, n);
    if (other && other != k) return STG_E_FILEALREADYEXISTS;
    if (k->opens) return STG_E_ACCESSDENIED;
    k->name = n;
    Touch();
    return S_OK;
  }
  STDMETHOD(SetElementTimes)(const OLECHAR* name, const FILETIME* c, const FILETIME*, const FILETIME* m) override {
    Guard g(StgLock());
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    Node* k = node_.get();
    if (name != NULL) {
      std::wstring n;
      HRESULT hr = CheckName(name, &n);
      if (FAILED(hr)) return hr;
      NodeP f = Find(*node_, n);
      if (!f) return STG_E_FILENOTFOUND;
      k = f.get();
    }
    if (c) k->ctime = *c;
    if (m) k->mtime = *m;
    root_->dirty = true;
    return S_OK;
  }
  STDMETHOD(SetClass)(REFCLSID clsid) override {
    Guard g(StgLock());
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    node_->clsid = clsid;
    root_->dirty = true;
    return S_OK;
  }
  STDMETHOD(SetStateBits)(DWORD bits, DWORD mask) override {
    Guard g(StgLock());
    if (!CanWrite(mode_)) return STG_E_ACCESSDENIED;
    node_->stateBits = (node_->stateBits & ~mask) | (bits & mask);
    root_->dirty = true;
    return S_OK;
  }
  STDMETHOD(Stat)(STATSTG* st, DWORD flags) override {
    Guard g(StgLock());
    if (st == NULL) return STG_E_INVALIDPOINTER;
    *st = StatOf(*node_, false);
    if (!(flags & STATFLAG_NONAME)) st->pwcsName = isRoot_ ? (root_->path.empty() ? NULL : CoString(root_->path))
                                                           : CoString(node_->name);
    st->grfMode = mode_;
    return S_OK;
  }

 private:
  void Touch() {
    node_->mtime = Now();
    root_->dirty = true;
  }
  // An object on a child storage: on a copy of it when transacted.
  Storage* Open(const NodeP& k, DWORD mode) {
    NodeP work = (mode & STGM_TRANSACTED) ? DeepCopy(*k) : k;
    if (work != k) work->name = k->name;
    return new Storage(root_, work, k, mode, false);
  }
  HRESULT CopyElement(const NodeP& k, IStorage* dest, const OLECHAR* name) {
    const DWORD rw = STGM_READWRITE | STGM_SHARE_EXCLUSIVE;
    if (!k->storage) {
      IStream* s = NULL;
      HRESULT hr = dest->CreateStream(name, rw | STGM_CREATE, 0, 0, &s);
      if (FAILED(hr)) return hr;
      std::vector<BYTE> data;
      {
        Guard g(StgLock());
        data = k->data;
      }
      ULONG n = 0;
      hr = data.empty() ? S_OK : s->Write(data.data(), (ULONG)data.size(), &n);
      s->Release();
      return hr;
    }
    IStorage* d = NULL;
    HRESULT hr = dest->OpenStorage(name, NULL, rw, NULL, 0, &d);
    if (FAILED(hr)) hr = dest->CreateStorage(name, rw, 0, 0, &d);
    if (FAILED(hr)) return hr;
    // Through an object on the child, read-only and uncounted.
    Storage* src = new Storage(root_, k, NodeP(), STGM_READ, false);
    hr = src->CopyTo(0, NULL, NULL, d);
    src->Release();
    if (SUCCEEDED(hr)) hr = d->Commit(STGC_DEFAULT);
    d->Release();
    return hr;
  }

  std::atomic<ULONG> refs_{1};
  RootP root_;
  NodeP node_, base_;
  DWORD mode_;
  bool isRoot_;
};

HRESULT CheckRootMode(DWORD mode) {
  if ((mode & 3) == 3) return STG_E_INVALIDFLAG;
  return S_OK;
}

// A root over bytes: a new, empty compound file, or the one there.
HRESULT MakeRoot(ILockBytes* lb, const std::wstring& path, DWORD mode, bool create, bool deleteOnRelease,
                 IStorage** out) {
  RootP root = std::make_shared<RootFile>();
  lb->AddRef();
  root->lb = lb;
  root->path = path;
  root->deleteOnRelease = deleteOnRelease;
  root->direct = !(mode & STGM_TRANSACTED);
  if (create) {
    root->tree = std::make_shared<Node>();
    root->tree->storage = true;
    root->tree->mtime = Now();
    // An empty compound file is there at once.
    HRESULT hr = root->Save();
    if (FAILED(hr)) return hr;
  } else {
    std::vector<BYTE> bytes;
    HRESULT hr = ReadAll(lb, &bytes);
    if (FAILED(hr)) return hr;
    hr = Reader(bytes).Load(&root->tree);
    if (FAILED(hr)) return hr;
  }
  // The root works on the tree itself: a transacted one writes it when
  // it commits and reads the file again when it reverts.
  *out = new Storage(root, root->tree, root->tree, mode, true);
  return S_OK;
}

HRESULT OpenFile(const OLECHAR* name, DWORD mode, bool create, std::wstring* path, ILockBytes** out) {
  std::string p;
  if (name == NULL) {
    const char* tmp = getenv("TMPDIR");
    p = std::string(tmp && *tmp ? tmp : "/tmp") + "/docfileXXXXXX";
    std::vector<char> buf(p.begin(), p.end());
    buf.push_back(0);
    int fd = mkstemp(buf.data());
    if (fd < 0) return STG_E_ACCESSDENIED;
    p = buf.data();
    *path = Widen(p.c_str());
    *out = new FileLockBytes(fd, *path);
    return S_OK;
  }
  p = PosixPath(name);
  int flags = CanWrite(mode) ? O_RDWR : O_RDONLY;
  if (create) {
    flags = O_RDWR | O_CREAT | ((mode & STGM_CREATE) ? O_TRUNC : O_EXCL);
  }
  int fd = open(p.c_str(), flags | O_CLOEXEC, 0666);
  if (fd < 0) {
    switch (errno) {
      case ENOENT: return create ? STG_E_PATHNOTFOUND : STG_E_FILENOTFOUND;
      case EEXIST: return STG_E_FILEALREADYEXISTS;
      case EACCES: case EPERM: case EROFS: return STG_E_ACCESSDENIED;
      default: return STG_E_ACCESSDENIED;
    }
  }
  struct stat sb;
  if (fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    close(fd);
    return STG_E_ACCESSDENIED;
  }
  *path = name;
  *out = new FileLockBytes(fd, *path);
  return S_OK;
}

// The class and user type an object's storage records ("\1CompObj").
void PutLenA(std::vector<BYTE>& v, const std::string& s) {
  BYTE b[4];
  if (s.empty()) {
    Put32(b, 0);
    v.insert(v.end(), b, b + 4);
    return;
  }
  Put32(b, (DWORD)s.size() + 1);
  v.insert(v.end(), b, b + 4);
  v.insert(v.end(), s.begin(), s.end());
  v.push_back(0);
}
void PutLenW(std::vector<BYTE>& v, const std::wstring& s) {
  BYTE b[4];
  if (s.empty()) {
    Put32(b, 0);
    v.insert(v.end(), b, b + 4);
    return;
  }
  std::vector<WORD> u;
  for (wchar_t c : s) {
    unsigned x = (unsigned)c;
    if (x > 0xFFFF) {
      x -= 0x10000;
      u.push_back((WORD)(0xD800 + (x >> 10)));
      u.push_back((WORD)(0xDC00 + (x & 0x3FF)));
    } else {
      u.push_back((WORD)x);
    }
  }
  u.push_back(0);
  Put32(b, (DWORD)u.size());
  v.insert(v.end(), b, b + 4);
  for (WORD w : u) {
    v.push_back((BYTE)w);
    v.push_back((BYTE)(w >> 8));
  }
}

std::string FormatName(CLIPFORMAT cf) {
  if (cf < 0xC000) return std::string();
  char buf[256];
  int n = GetClipboardFormatNameA(cf, buf, sizeof buf);
  return n > 0 ? std::string(buf, n) : std::string();
}

struct Cursor {
  const std::vector<BYTE>& d;
  size_t pos = 0;
  bool ok = true;
  explicit Cursor(const std::vector<BYTE>& v) : d(v) {}
  DWORD U32() {
    if (pos + 4 > d.size()) {
      ok = false;
      return 0;
    }
    DWORD v = Get32(d.data() + pos);
    pos += 4;
    return v;
  }
  std::string A(DWORD len) {
    if (len == 0 || pos + len > d.size()) {
      if (len) ok = false;
      return std::string();
    }
    std::string s((const char*)d.data() + pos, len);
    pos += len;
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
  }
  std::wstring W(DWORD chars) {
    std::wstring s;
    if (chars == 0) return s;
    if (pos + (size_t)chars * 2 > d.size()) {
      ok = false;
      return s;
    }
    for (DWORD i = 0; i < chars; i++) {
      unsigned c = Get16(d.data() + pos + i * 2);
      if (c >= 0xD800 && c < 0xDC00 && i + 1 < chars) {
        unsigned lo = Get16(d.data() + pos + (i + 1) * 2);
        if (lo >= 0xDC00 && lo < 0xE000) {
          c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
          i++;
        }
      }
      s += (wchar_t)c;
    }
    pos += (size_t)chars * 2;
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
  }
};

HRESULT ReadStreamBytes(IStorage* stg, const OLECHAR* name, std::vector<BYTE>* out) {
  IStream* s = NULL;
  HRESULT hr = stg->OpenStream(name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &s);
  if (FAILED(hr)) return hr;
  STATSTG st;
  hr = s->Stat(&st, STATFLAG_NONAME);
  if (SUCCEEDED(hr)) {
    out->assign((size_t)st.cbSize.QuadPart, 0);
    ULONG n = 0;
    if (!out->empty()) hr = s->Read(out->data(), (ULONG)out->size(), &n);
    out->resize(n);
  }
  s->Release();
  return hr;
}

HRESULT WriteStreamBytes(IStorage* stg, const OLECHAR* name, const std::vector<BYTE>& v) {
  IStream* s = NULL;
  HRESULT hr = stg->CreateStream(name, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &s);
  if (FAILED(hr)) return hr;
  ULONG n = 0;
  hr = v.empty() ? S_OK : s->Write(v.data(), (ULONG)v.size(), &n);
  s->Release();
  return hr;
}

}  // namespace

extern "C" {

HRESULT WINAPI StgCreateDocfile(const OLECHAR* name, DWORD mode, DWORD, IStorage** out) {
  if (out == NULL) return STG_E_INVALIDPOINTER;
  *out = NULL;
  HRESULT hr = CheckRootMode(mode);
  if (FAILED(hr)) return hr;
  Guard g(StgLock());
  std::wstring path;
  ILockBytes* lb = NULL;
  hr = OpenFile(name, mode, true, &path, &lb);
  if (FAILED(hr)) return hr;
  hr = MakeRoot(lb, path, mode, true, name == NULL || (mode & STGM_DELETEONRELEASE), out);
  lb->Release();
  return hr;
}

HRESULT WINAPI StgCreateDocfileOnILockBytes(ILockBytes* lb, DWORD mode, DWORD, IStorage** out) {
  if (out == NULL || lb == NULL) return STG_E_INVALIDPOINTER;
  *out = NULL;
  HRESULT hr = CheckRootMode(mode);
  if (FAILED(hr)) return hr;
  Guard g(StgLock());
  return MakeRoot(lb, std::wstring(), mode, true, false, out);
}

HRESULT WINAPI StgOpenStorage(const OLECHAR* name, IStorage*, DWORD mode, SNB, DWORD, IStorage** out) {
  if (out == NULL) return STG_E_INVALIDPOINTER;
  *out = NULL;
  if (name == NULL) return STG_E_INVALIDNAME;
  HRESULT hr = CheckRootMode(mode);
  if (FAILED(hr)) return hr;
  Guard g(StgLock());
  std::wstring path;
  ILockBytes* lb = NULL;
  hr = OpenFile(name, mode, false, &path, &lb);
  if (FAILED(hr)) return hr;
  hr = MakeRoot(lb, path, mode, false, (mode & STGM_DELETEONRELEASE) != 0, out);
  lb->Release();
  return hr;
}

HRESULT WINAPI StgOpenStorageOnILockBytes(ILockBytes* lb, IStorage*, DWORD mode, SNB, DWORD, IStorage** out) {
  if (out == NULL || lb == NULL) return STG_E_INVALIDPOINTER;
  *out = NULL;
  HRESULT hr = CheckRootMode(mode);
  if (FAILED(hr)) return hr;
  Guard g(StgLock());
  return MakeRoot(lb, std::wstring(), mode, false, false, out);
}

HRESULT WINAPI StgIsStorageILockBytes(ILockBytes* lb) {
  if (lb == NULL) return STG_E_INVALIDPOINTER;
  BYTE sig[8];
  ULONG n = 0;
  ULARGE_INTEGER z;
  z.QuadPart = 0;
  HRESULT hr = lb->ReadAt(z, sig, 8, &n);
  if (FAILED(hr)) return hr;
  return n == 8 && memcmp(sig, kSignature, 8) == 0 ? S_OK : S_FALSE;
}

HRESULT WINAPI StgIsStorageFile(const OLECHAR* name) {
  if (name == NULL) return STG_E_INVALIDNAME;
  int fd = open(PosixPath(name).c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return STG_E_FILENOTFOUND;
  BYTE sig[8];
  ssize_t n = read(fd, sig, 8);
  close(fd);
  return n == 8 && memcmp(sig, kSignature, 8) == 0 ? S_OK : S_FALSE;
}

HRESULT WINAPI WriteClassStg(LPSTORAGE stg, REFCLSID clsid) {
  if (stg == NULL) return E_INVALIDARG;
  return stg->SetClass(clsid);
}

HRESULT WINAPI ReadClassStg(LPSTORAGE stg, CLSID* clsid) {
  if (stg == NULL || clsid == NULL) return E_INVALIDARG;
  STATSTG st;
  HRESULT hr = stg->Stat(&st, STATFLAG_NONAME);
  if (FAILED(hr)) return hr;
  *clsid = st.clsid;
  return S_OK;
}

// The class of a compound file is its root's.
HRESULT WINAPI GetClassFile(LPCOLESTR name, CLSID* clsid) {
  if (name == NULL || clsid == NULL) return E_INVALIDARG;
  *clsid = CLSID_NULL;
  if (StgIsStorageFile(name) != S_OK) return MK_E_CANTOPENFILE;
  IStorage* stg = NULL;
  HRESULT hr = StgOpenStorage(name, NULL, STGM_READ | STGM_SHARE_DENY_NONE | STGM_TRANSACTED, NULL, 0, &stg);
  if (FAILED(hr)) return MK_E_CANTOPENFILE;
  hr = ReadClassStg(stg, clsid);
  stg->Release();
  if (FAILED(hr) || IsEqualCLSID(*clsid, CLSID_NULL)) return MK_E_INVALIDEXTENSION;
  return S_OK;
}

// "\1CompObj": the class, its user type and clipboard format, ANSI then
// Unicode.
HRESULT WINAPI WriteFmtUserTypeStg(LPSTORAGE stg, CLIPFORMAT cf, LPOLESTR userType) {
  if (stg == NULL) return E_INVALIDARG;
  CLSID clsid = CLSID_NULL;
  ReadClassStg(stg, &clsid);
  std::vector<BYTE> v(28, 0);
  Put16(v.data(), 1);
  Put16(v.data() + 2, 0xFFFE);
  Put32(v.data() + 4, 0x00000A03);  // the version Windows writes
  Put32(v.data() + 8, 0xFFFFFFFF);
  PutGuid(v.data() + 12, clsid);
  std::wstring type = userType ? userType : L"";
  PutLenA(v, Narrow(type.c_str()));
  std::string fmt = FormatName(cf);
  BYTE b[4];
  if (cf == 0) {
    Put32(b, 0);
    v.insert(v.end(), b, b + 4);
  } else if (fmt.empty()) {
    Put32(b, 0xFFFFFFFF);  // a Windows clipboard format (0xFFFFFFFE: a Macintosh one)
    v.insert(v.end(), b, b + 4);
    Put32(b, cf);
    v.insert(v.end(), b, b + 4);
  } else {
    PutLenA(v, fmt);
  }
  std::wstring progId;
  LPOLESTR pid = NULL;
  if (!IsEqualCLSID(clsid, CLSID_NULL) && SUCCEEDED(ProgIDFromCLSID(clsid, &pid)) && pid) {
    progId = pid;
    CoTaskMemFree(pid);
  }
  PutLenA(v, Narrow(progId.c_str()));
  Put32(b, 0x71B239F4);
  v.insert(v.end(), b, b + 4);
  PutLenW(v, type);
  if (cf == 0) {
    Put32(b, 0);
    v.insert(v.end(), b, b + 4);
  } else if (fmt.empty()) {
    Put32(b, 0xFFFFFFFF);  // a Windows clipboard format (0xFFFFFFFE: a Macintosh one)
    v.insert(v.end(), b, b + 4);
    Put32(b, cf);
    v.insert(v.end(), b, b + 4);
  } else {
    PutLenW(v, Widen(fmt.c_str()));
  }
  PutLenW(v, progId);
  return WriteStreamBytes(stg, L"\1CompObj", v);
}

HRESULT WINAPI ReadFmtUserTypeStg(LPSTORAGE stg, CLIPFORMAT* pcf, LPOLESTR* userType) {
  if (stg == NULL) return E_INVALIDARG;
  if (pcf) *pcf = 0;
  if (userType) *userType = NULL;
  std::vector<BYTE> v;
  HRESULT hr = ReadStreamBytes(stg, L"\1CompObj", &v);
  if (FAILED(hr)) return hr;
  Cursor c(v);
  c.pos = 28;
  std::string typeA = c.A(c.U32());
  CLIPFORMAT cf = 0;
  DWORD m = c.U32();
  if (m == 0xFFFFFFFF || m == 0xFFFFFFFE) cf = (CLIPFORMAT)c.U32();
  else if (m) cf = (CLIPFORMAT)RegisterClipboardFormatA(c.A(m).c_str());
  if (!c.ok) return STG_E_DOCFILECORRUPT;
  std::wstring type = Widen(typeA.c_str());
  c.A(c.U32());  // the ProgID
  if (c.ok && c.U32() == 0x71B239F4 && c.ok) {
    std::wstring t = c.W(c.U32());
    if (c.ok) type = t;
    DWORD mw = c.U32();
    if (c.ok && (mw == 0xFFFFFFFF || mw == 0xFFFFFFFE)) {
      cf = (CLIPFORMAT)c.U32();
    } else if (c.ok && mw) {
      std::wstring f = c.W(mw);
      if (c.ok) cf = (CLIPFORMAT)RegisterClipboardFormatW(f.c_str());
    }
  }
  if (pcf) *pcf = cf;
  if (userType) *userType = CoString(type);
  return S_OK;
}

// "\1Ole": the object's flags; one says it is to be converted.
HRESULT WINAPI SetConvertStg(LPSTORAGE stg, BOOL convert) {
  if (stg == NULL) return E_INVALIDARG;
  std::vector<BYTE> v;
  if (FAILED(ReadStreamBytes(stg, L"\1Ole", &v)) || v.size() < 20) {
    v.assign(20, 0);
    Put32(v.data(), 0x02000001);
  }
  DWORD flags = Get32(v.data() + 4);
  flags = convert ? (flags | 4) : (flags & ~4u);
  Put32(v.data() + 4, flags);
  return WriteStreamBytes(stg, L"\1Ole", v);
}

HRESULT WINAPI GetConvertStg(LPSTORAGE stg) {
  if (stg == NULL) return E_INVALIDARG;
  std::vector<BYTE> v;
  HRESULT hr = ReadStreamBytes(stg, L"\1Ole", &v);
  if (FAILED(hr)) return hr;
  if (v.size() < 8) return STG_E_DOCFILECORRUPT;
  return (Get32(v.data() + 4) & 4) ? S_OK : S_FALSE;
}

}  // extern "C"
