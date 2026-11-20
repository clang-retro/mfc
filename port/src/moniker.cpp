// moniker.cpp - monikers and binding: file, item and composite monikers,
// bind contexts, the running object table, BindMoniker,
// MkParseDisplayName, and monikers in streams (OleSaveToStream,
// OleLoadFromStream).
//
// A moniker names an object and binds to it. A file moniker binds to the
// object running for its file (the running object table) or makes one
// of the file's class (GetClassFile) and loads the file into it
// (IPersistFile). An item moniker binds through the object to its left:
// that object's IOleItemContainer hands out the item. A composite binds
// its last part with the rest to its left. The running object table is
// the program's own.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "ole.h"

#include <sys/stat.h>
#include <urlmon.h>
#include <wctype.h>

#include <map>
#include <string>
#include <vector>

using namespace winport::ole;

namespace {

const CLSID kFileMoniker = {0x00000303, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const CLSID kItemMoniker = {0x00000304, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
const CLSID kCompositeMoniker = {0x00000309, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

// --- Bytes in a stream

HRESULT ReadExact(IStream* s, void* p, ULONG n) {
  ULONG got = 0;
  HRESULT hr = s->Read(p, n, &got);
  if (FAILED(hr)) return hr;
  return got == n ? S_OK : STG_E_READFAULT;
}
HRESULT WriteExact(IStream* s, const void* p, ULONG n) {
  ULONG put = 0;
  HRESULT hr = s->Write(p, n, &put);
  if (FAILED(hr)) return hr;
  return put == n ? S_OK : STG_E_WRITEFAULT;
}
HRESULT ReadDword(IStream* s, DWORD* d) {
  BYTE b[4];
  HRESULT hr = ReadExact(s, b, 4);
  *d = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24);
  return hr;
}
HRESULT WriteDword(IStream* s, DWORD d) {
  BYTE b[4] = {(BYTE)d, (BYTE)(d >> 8), (BYTE)(d >> 16), (BYTE)(d >> 24)};
  return WriteExact(s, b, 4);
}
HRESULT ReadWord(IStream* s, WORD* w) {
  BYTE b[2];
  HRESULT hr = ReadExact(s, b, 2);
  *w = (WORD)(b[0] | (b[1] << 8));
  return hr;
}
HRESULT WriteWord(IStream* s, WORD w) {
  BYTE b[2] = {(BYTE)w, (BYTE)(w >> 8)};
  return WriteExact(s, b, 2);
}

bool IsAscii(const std::wstring& s) {
  for (wchar_t c : s)
    if ((unsigned)c >= 0x80) return false;
  return true;
}
std::string AsciiOf(const std::wstring& s) {
  std::string out;
  for (wchar_t c : s) out += (unsigned)c < 0x80 ? (char)c : '?';
  return out;
}
std::vector<BYTE> Utf16Of(const std::wstring& s) {
  std::vector<BYTE> v;
  auto put = [&](unsigned u) { v.push_back((BYTE)u), v.push_back((BYTE)(u >> 8)); };
  for (wchar_t wc : s) {
    unsigned c = (unsigned)wc;
    if (c >= 0x10000) {
      c -= 0x10000;
      put(0xD800 + (c >> 10));
      put(0xDC00 + (c & 0x3FF));
    } else {
      put(c);
    }
  }
  return v;
}
std::wstring FromUtf16(const BYTE* p, size_t bytes) {
  std::wstring out;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    unsigned u = p[i] | (p[i + 1] << 8);
    if (u >= 0xDC00 && u < 0xE000 && !out.empty() && (unsigned)out.back() >= 0xD800 &&
        (unsigned)out.back() < 0xDC00)
      out.back() = (wchar_t)(0x10000 + (((unsigned)out.back() - 0xD800) << 10) + (u - 0xDC00));
    else
      out += (wchar_t)u;
  }
  return out;
}

DWORD HashOf(const std::wstring& s) {
  DWORD h = 0;
  for (wchar_t c : s) h = h * 31 + (DWORD)towlower(c);
  return h;
}

IMoniker* LoadMoniker(IStream* s);

// ---------------------------------------------------------------------
// The running object table

struct RotEntry {
  DWORD cookie;
  IUnknown* obj;
  IMoniker* mk;
  FILETIME changed;
};

class RunningObjectTable final : public IRunningObjectTable {
 public:
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IRunningObjectTable)) {
      *out = static_cast<IRunningObjectTable*>(this);
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  // The table lives as long as the program.
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }

  STDMETHOD(Register)(DWORD flags, IUnknown* obj, IMoniker* mk, DWORD* cookie) override {
    if (!obj || !mk || !cookie) return E_INVALIDARG;
    *cookie = 0;
    RotEntry e;
    e.cookie = next_++;
    e.obj = obj;
    e.mk = mk;
    // A weak registration does not keep the object (only the strong one,
    // as Windows does, but either way the program revokes it).
    obj->AddRef();
    mk->AddRef();
    GetSystemTimeAsFileTime(&e.changed);
    HRESULT hr = Find(mk) ? MK_S_MONIKERALREADYREGISTERED : S_OK;
    (void)flags;
    entries_.push_back(e);
    *cookie = e.cookie;
    return hr;
  }
  STDMETHOD(Revoke)(DWORD cookie) override {
    for (size_t i = 0; i < entries_.size(); i++)
      if (entries_[i].cookie == cookie) {
        RotEntry e = entries_[i];
        entries_.erase(entries_.begin() + i);
        e.mk->Release();
        e.obj->Release();
        return S_OK;
      }
    return E_INVALIDARG;
  }
  STDMETHOD(IsRunning)(IMoniker* mk) override {
    if (!mk) return E_INVALIDARG;
    return Find(mk) ? S_OK : S_FALSE;
  }
  STDMETHOD(GetObject)(IMoniker* mk, IUnknown** out) override {
    if (!mk || !out) return E_INVALIDARG;
    *out = NULL;
    RotEntry* e = Find(mk);
    if (!e) return MK_E_UNAVAILABLE;
    *out = e->obj;
    e->obj->AddRef();
    return S_OK;
  }
  STDMETHOD(NoteChangeTime)(DWORD cookie, FILETIME* ft) override {
    if (!ft) return E_INVALIDARG;
    for (RotEntry& e : entries_)
      if (e.cookie == cookie) {
        e.changed = *ft;
        return S_OK;
      }
    return E_INVALIDARG;
  }
  STDMETHOD(GetTimeOfLastChange)(IMoniker* mk, FILETIME* ft) override {
    if (!mk || !ft) return E_INVALIDARG;
    RotEntry* e = Find(mk);
    if (!e) return MK_E_UNAVAILABLE;
    *ft = e->changed;
    return S_OK;
  }
  STDMETHOD(EnumRunning)(IEnumMoniker** out) override;

 private:
  RotEntry* Find(IMoniker* mk) {
    for (RotEntry& e : entries_)
      if (e.mk == mk || e.mk->IsEqual(mk) == S_OK) return &e;
    return NULL;
  }
  std::vector<RotEntry> entries_;
  DWORD next_ = 1;
};

RunningObjectTable& Rot() {
  static RunningObjectTable* t = new RunningObjectTable;
  return *t;
}

// An enumerator over monikers.
class MonikerEnum final : public IEnumMoniker {
 public:
  explicit MonikerEnum(std::vector<IMoniker*> v, size_t pos = 0) : v_(std::move(v)), pos_(pos) {
    for (IMoniker* m : v_) m->AddRef();
  }
  ~MonikerEnum() {
    for (IMoniker* m : v_) m->Release();
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumMoniker)) {
      *out = static_cast<IEnumMoniker*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IEnumMoniker)
  STDMETHOD(Next)(ULONG n, IMoniker** out, ULONG* fetched) override {
    if (!out) return E_POINTER;
    ULONG k = 0;
    while (k < n && pos_ < v_.size()) {
      out[k] = v_[pos_++];
      out[k]->AddRef();
      k++;
    }
    if (fetched) *fetched = k;
    return k == n ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG n) override {
    pos_ = std::min(v_.size(), pos_ + n);
    return pos_ < v_.size() ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    pos_ = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnumMoniker** out) override {
    if (!out) return E_POINTER;
    *out = new MonikerEnum(v_, pos_);
    return S_OK;
  }

 private:
  std::vector<IMoniker*> v_;
  size_t pos_;
};

HRESULT RunningObjectTable::EnumRunning(IEnumMoniker** out) {
  if (!out) return E_INVALIDARG;
  std::vector<IMoniker*> v;
  for (RotEntry& e : entries_) v.push_back(e.mk);
  *out = new MonikerEnum(v);
  return S_OK;
}

// ---------------------------------------------------------------------
// Bind contexts

class BindCtx final : public IBindCtx {
 public:
  BindCtx() {
    opts_.cbStruct = sizeof opts_;
    opts_.grfMode = STGM_READWRITE;
  }
  ~BindCtx() {
    ReleaseBoundObjects();
    for (auto& p : params_) p.second->Release();
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IBindCtx)) {
      *out = static_cast<IBindCtx*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  REFCOUNTED(IBindCtx)
  STDMETHOD(RegisterObjectBound)(IUnknown* unk) override {
    if (!unk) return E_INVALIDARG;
    unk->AddRef();
    bound_.push_back(unk);
    return S_OK;
  }
  STDMETHOD(RevokeObjectBound)(IUnknown* unk) override {
    for (size_t i = 0; i < bound_.size(); i++)
      if (bound_[i] == unk) {
        bound_.erase(bound_.begin() + i);
        unk->Release();
        return S_OK;
      }
    return MK_E_NOTBOUND;
  }
  STDMETHOD(ReleaseBoundObjects)() override {
    std::vector<IUnknown*> v;
    v.swap(bound_);
    for (IUnknown* u : v) u->Release();
    return S_OK;
  }
  STDMETHOD(SetBindOptions)(BIND_OPTS* o) override {
    if (!o) return E_INVALIDARG;
    memcpy(&opts_, o, std::min<size_t>(o->cbStruct, sizeof opts_));
    opts_.cbStruct = sizeof opts_;
    return S_OK;
  }
  STDMETHOD(GetBindOptions)(BIND_OPTS* o) override {
    if (!o) return E_INVALIDARG;
    DWORD cb = o->cbStruct;
    memcpy(o, &opts_, std::min<size_t>(cb, sizeof opts_));
    o->cbStruct = cb;
    return S_OK;
  }
  STDMETHOD(GetRunningObjectTable)(IRunningObjectTable** out) override {
    if (!out) return E_INVALIDARG;
    *out = &Rot();
    return S_OK;
  }
  STDMETHOD(RegisterObjectParam)(LPOLESTR key, IUnknown* unk) override {
    if (!key || !unk) return E_INVALIDARG;
    RevokeObjectParam(key);
    unk->AddRef();
    params_.push_back({key, unk});
    return S_OK;
  }
  STDMETHOD(GetObjectParam)(LPOLESTR key, IUnknown** out) override {
    if (!key || !out) return E_INVALIDARG;
    *out = NULL;
    for (auto& p : params_)
      if (p.first == key) {
        *out = p.second;
        p.second->AddRef();
        return S_OK;
      }
    return E_FAIL;
  }
  STDMETHOD(EnumObjectParam)(IEnumString** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(RevokeObjectParam)(LPOLESTR key) override {
    if (!key) return E_INVALIDARG;
    for (size_t i = 0; i < params_.size(); i++)
      if (params_[i].first == key) {
        params_[i].second->Release();
        params_.erase(params_.begin() + i);
        return S_OK;
      }
    return S_FALSE;
  }

 private:
  BIND_OPTS2 opts_ = {};
  std::vector<IUnknown*> bound_;
  std::vector<std::pair<std::wstring, IUnknown*>> params_;
};

// A bind context of one's own when the caller gives none.
struct OwnCtx {
  IBindCtx* pbc;
  bool own;
  explicit OwnCtx(IBindCtx* given) : pbc(given), own(given == NULL) {
    if (own) pbc = new BindCtx;
  }
  ~OwnCtx() {
    if (own) pbc->Release();
  }
};

// ---------------------------------------------------------------------
// What the simple monikers share

class MonikerBase : public IMoniker, public IROTData {
 public:
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IMoniker) || IsEqualIID(riid, IID_IPersist) ||
        IsEqualIID(riid, IID_IPersistStream))
      *out = static_cast<IMoniker*>(this);
    else if (IsEqualIID(riid, IID_IROTData))
      *out = static_cast<IROTData*>(this);
    else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  virtual ~MonikerBase() {}

  STDMETHOD(IsDirty)() override { return S_FALSE; }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* size) override {
    if (!size) return E_POINTER;
    size->QuadPart = 4096;
    return S_OK;
  }
  STDMETHOD(Reduce)(IBindCtx*, DWORD, IMoniker**, IMoniker** reduced) override {
    if (!reduced) return E_POINTER;
    *reduced = this;
    AddRef();
    return MK_S_REDUCED_TO_SELF;
  }
  STDMETHOD(ComposeWith)(IMoniker* right, BOOL onlyIfNotGeneric, IMoniker** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!right) {
      *out = this;
      AddRef();
      return S_OK;
    }
    if (onlyIfNotGeneric) return MK_E_NEEDGENERIC;
    return CreateGenericComposite(this, right, out);
  }
  STDMETHOD(Enum)(BOOL, IEnumMoniker** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    return S_OK;
  }
  STDMETHOD(Hash)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = HashOf(Name());
    return S_OK;
  }
  STDMETHOD(Inverse)(IMoniker** out) override {
    if (out) *out = NULL;
    return MK_E_NOINVERSE;
  }
  STDMETHOD(CommonPrefixWith)(IMoniker* other, IMoniker** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (other && IsEqual(other) == S_OK) {
      *out = this;
      AddRef();
      return MK_S_US;
    }
    return MK_E_NOPREFIX;
  }
  STDMETHOD(RelativePathTo)(IMoniker* other, IMoniker** out) override {
    if (!out) return E_POINTER;
    *out = other;
    if (other) other->AddRef();
    return MK_S_HIM;
  }
  // IROTData: what the running object table compares.
  STDMETHOD(GetComparisonData)(BYTE* data, ULONG max, ULONG* got) override {
    std::vector<BYTE> v = Utf16Of(Name());
    CLSID c;
    GetClassID(&c);
    v.insert(v.begin(), (const BYTE*)&c, (const BYTE*)&c + sizeof c);
    if (got) *got = (ULONG)v.size();
    if (!data || max < v.size()) return E_OUTOFMEMORY;
    memcpy(data, v.data(), v.size());
    return S_OK;
  }

 protected:
  // What tells it from others of its kind.
  virtual std::wstring Name() const = 0;
  ULONG refs_ = 1;
};

// ---------------------------------------------------------------------
// File monikers

class FileMoniker final : public MonikerBase {
 public:
  explicit FileMoniker(const std::wstring& path = std::wstring()) : path_(path) {}
  const std::wstring& Path() const { return path_; }

  STDMETHOD(GetClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    *out = kFileMoniker;
    return S_OK;
  }
  // The anti count, the path in ANSI, the end of the server part, the
  // version, reserved bytes, and the path in UTF-16 if ANSI cannot hold it.
  STDMETHOD(Load)(IStream* s) override {
    if (!s) return E_POINTER;
    WORD anti, endServer, version;
    DWORD ansiLen;
    HRESULT hr = ReadWord(s, &anti);
    if (SUCCEEDED(hr)) hr = ReadDword(s, &ansiLen);
    if (FAILED(hr) || ansiLen > 0x100000) return FAILED(hr) ? hr : E_FAIL;
    std::string ansi(ansiLen, '\0');
    if (ansiLen) hr = ReadExact(s, &ansi[0], ansiLen);
    if (SUCCEEDED(hr)) hr = ReadWord(s, &endServer);
    if (SUCCEEDED(hr)) hr = ReadWord(s, &version);
    BYTE reserved[20];
    if (SUCCEEDED(hr)) hr = ReadExact(s, reserved, sizeof reserved);
    DWORD uniSize = 0;
    if (SUCCEEDED(hr)) hr = ReadDword(s, &uniSize);
    if (FAILED(hr)) return hr;
    if (!ansi.empty() && ansi.back() == '\0') ansi.pop_back();
    path_ = winport::Widen(ansi.c_str());
    if (uniSize >= 6) {
      DWORD bytes;
      WORD key;
      hr = ReadDword(s, &bytes);
      if (SUCCEEDED(hr)) hr = ReadWord(s, &key);
      if (FAILED(hr) || bytes > 0x200000) return FAILED(hr) ? hr : E_FAIL;
      std::vector<BYTE> u(bytes);
      if (bytes) hr = ReadExact(s, u.data(), bytes);
      if (FAILED(hr)) return hr;
      path_ = FromUtf16(u.data(), u.size());
    }
    for (WORD i = 0; i < anti; i++) path_ = L"..\\" + path_;
    return S_OK;
  }
  STDMETHOD(Save)(IStream* s, BOOL) override {
    if (!s) return E_POINTER;
    std::string ansi = IsAscii(path_) ? AsciiOf(path_) : winport::Narrow(path_.c_str());
    HRESULT hr = WriteWord(s, 0);
    if (SUCCEEDED(hr)) hr = WriteDword(s, (DWORD)ansi.size() + 1);
    if (SUCCEEDED(hr)) hr = WriteExact(s, ansi.c_str(), (ULONG)ansi.size() + 1);
    if (SUCCEEDED(hr)) hr = WriteWord(s, 0xFFFF);
    if (SUCCEEDED(hr)) hr = WriteWord(s, 0xDEAD);
    BYTE reserved[20] = {};
    if (SUCCEEDED(hr)) hr = WriteExact(s, reserved, sizeof reserved);
    if (IsAscii(path_)) {
      if (SUCCEEDED(hr)) hr = WriteDword(s, 0);
    } else {
      std::vector<BYTE> u = Utf16Of(path_);
      if (SUCCEEDED(hr)) hr = WriteDword(s, (DWORD)u.size() + 6);
      if (SUCCEEDED(hr)) hr = WriteDword(s, (DWORD)u.size());
      if (SUCCEEDED(hr)) hr = WriteWord(s, 3);
      if (SUCCEEDED(hr) && !u.empty()) hr = WriteExact(s, u.data(), (ULONG)u.size());
    }
    return hr;
  }

  STDMETHOD(BindToObject)(IBindCtx* given, IMoniker* left, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    OwnCtx ctx(given);
    if (!left) {
      IUnknown* running = NULL;
      if (SUCCEEDED(Rot().GetObject(this, &running))) {
        HRESULT hr = running->QueryInterface(riid, out);
        running->Release();
        return hr;
      }
    }
    // Not running: an object of the file's class, loaded from it.
    CLSID clsid;
    HRESULT hr = GetClassFile(path_.c_str(), &clsid);
    if (FAILED(hr)) return MK_E_CANTOPENFILE;
    IPersistFile* pf = NULL;
    hr = CoCreateInstance(clsid, NULL, CLSCTX_SERVER, IID_IPersistFile, (void**)&pf);
    if (FAILED(hr)) return hr;
    BIND_OPTS opts = {sizeof opts};
    ctx.pbc->GetBindOptions(&opts);
    hr = pf->Load(path_.c_str(), opts.grfMode);
    if (SUCCEEDED(hr)) hr = pf->QueryInterface(riid, out);
    if (SUCCEEDED(hr)) ctx.pbc->RegisterObjectBound((IUnknown*)*out);
    pf->Release();
    return hr;
  }
  STDMETHOD(BindToStorage)(IBindCtx* given, IMoniker*, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    OwnCtx ctx(given);
    BIND_OPTS opts = {sizeof opts};
    ctx.pbc->GetBindOptions(&opts);
    if (IsEqualIID(riid, IID_IStorage)) {
      DWORD mode = opts.grfMode;
      if (!(mode & (STGM_SHARE_DENY_NONE | STGM_SHARE_DENY_READ | STGM_SHARE_DENY_WRITE | STGM_SHARE_EXCLUSIVE)))
        mode |= STGM_SHARE_EXCLUSIVE;
      return StgOpenStorage(path_.c_str(), NULL, mode, NULL, 0, (IStorage**)out);
    }
    return MK_E_NOSTORAGE;
  }
  STDMETHOD(IsEqual)(IMoniker* other) override {
    if (!other) return S_FALSE;
    CLSID c;
    if (FAILED(other->GetClassID(&c)) || !IsEqualCLSID(c, kFileMoniker)) return S_FALSE;
    return static_cast<FileMoniker*>(other)->path_ == path_ ? S_OK : S_FALSE;
  }
  STDMETHOD(IsRunning)(IBindCtx*, IMoniker* left, IMoniker* newly) override {
    if (newly && IsEqual(newly) == S_OK) return S_OK;
    if (left) return S_FALSE;
    return Rot().IsRunning(this);
  }
  STDMETHOD(GetTimeOfLastChange)(IBindCtx*, IMoniker*, FILETIME* ft) override {
    if (!ft) return E_POINTER;
    if (SUCCEEDED(Rot().GetTimeOfLastChange(this, ft))) return S_OK;
    struct stat st;
    std::string p = winport::PosixPath(path_.c_str());
    if (stat(p.c_str(), &st) != 0) return MK_E_NOOBJECT;
    ULONGLONG t = ((ULONGLONG)st.st_mtime + 11644473600ULL) * 10000000ULL;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
    return S_OK;
  }
  STDMETHOD(GetDisplayName)(IBindCtx*, IMoniker*, LPOLESTR* out) override {
    if (!out) return E_POINTER;
    *out = CoStrDup(path_.c_str());
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  // The rest of a name after the file: its items, through the object.
  STDMETHOD(ParseDisplayName)(IBindCtx* pbc, IMoniker* left, LPOLESTR name, ULONG* eaten, IMoniker** out) override {
    if (!out || !eaten) return E_POINTER;
    *out = NULL;
    *eaten = 0;
    IParseDisplayName* p = NULL;
    HRESULT hr = BindToObject(pbc, left, IID_IParseDisplayName, (void**)&p);
    if (FAILED(hr)) return hr;
    hr = p->ParseDisplayName(pbc, name, eaten, out);
    p->Release();
    return hr;
  }
  STDMETHOD(IsSystemMoniker)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = MKSYS_FILEMONIKER;
    return S_OK;
  }

 protected:
  std::wstring Name() const override { return path_; }

 private:
  std::wstring path_;
};

// ---------------------------------------------------------------------
// Item monikers

class ItemMoniker final : public MonikerBase {
 public:
  ItemMoniker(const std::wstring& delim = std::wstring(), const std::wstring& item = std::wstring())
      : delim_(delim), item_(item) {}

  STDMETHOD(GetClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    *out = kItemMoniker;
    return S_OK;
  }
  // The delimiter, then the item: each a length and ANSI text with its
  // end, the UTF-16 text after it when ANSI cannot hold it.
  STDMETHOD(Load)(IStream* s) override {
    if (!s) return E_POINTER;
    HRESULT hr = ReadString(s, &delim_);
    if (SUCCEEDED(hr)) hr = ReadString(s, &item_);
    return hr;
  }
  STDMETHOD(Save)(IStream* s, BOOL) override {
    if (!s) return E_POINTER;
    HRESULT hr = WriteString(s, delim_);
    if (SUCCEEDED(hr)) hr = WriteString(s, item_);
    return hr;
  }

  STDMETHOD(BindToObject)(IBindCtx* given, IMoniker* left, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!left) return E_INVALIDARG;
    OwnCtx ctx(given);
    IOleItemContainer* cont = NULL;
    HRESULT hr = left->BindToObject(ctx.pbc, NULL, IID_IOleItemContainer, (void**)&cont);
    if (FAILED(hr)) return hr;
    hr = cont->GetObject(const_cast<LPOLESTR>(item_.c_str()), BINDSPEED_INDEFINITE, ctx.pbc, riid, out);
    cont->Release();
    return hr;
  }
  STDMETHOD(BindToStorage)(IBindCtx* given, IMoniker* left, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!left) return E_INVALIDARG;
    OwnCtx ctx(given);
    IOleItemContainer* cont = NULL;
    HRESULT hr = left->BindToObject(ctx.pbc, NULL, IID_IOleItemContainer, (void**)&cont);
    if (FAILED(hr)) return hr;
    hr = cont->GetObjectStorage(const_cast<LPOLESTR>(item_.c_str()), ctx.pbc, riid, out);
    cont->Release();
    return hr;
  }
  STDMETHOD(IsEqual)(IMoniker* other) override {
    if (!other) return S_FALSE;
    CLSID c;
    if (FAILED(other->GetClassID(&c)) || !IsEqualCLSID(c, kItemMoniker)) return S_FALSE;
    ItemMoniker* o = static_cast<ItemMoniker*>(other);
    return Lower(o->item_) == Lower(item_) ? S_OK : S_FALSE;
  }
  STDMETHOD(IsRunning)(IBindCtx* given, IMoniker* left, IMoniker* newly) override {
    if (newly && IsEqual(newly) == S_OK) return S_OK;
    if (!left) return Rot().IsRunning(this);
    OwnCtx ctx(given);
    IOleItemContainer* cont = NULL;
    HRESULT hr = left->BindToObject(ctx.pbc, NULL, IID_IOleItemContainer, (void**)&cont);
    if (FAILED(hr)) return S_FALSE;
    hr = cont->IsRunning(const_cast<LPOLESTR>(item_.c_str()));
    cont->Release();
    return hr;
  }
  STDMETHOD(GetTimeOfLastChange)(IBindCtx* pbc, IMoniker* left, FILETIME* ft) override {
    if (!ft) return E_POINTER;
    if (!left) return MK_E_NOTBINDABLE;
    IMoniker* full = NULL;
    HRESULT hr = CreateGenericComposite(left, this, &full);
    if (FAILED(hr)) return hr;
    hr = Rot().GetTimeOfLastChange(full, ft);
    full->Release();
    if (SUCCEEDED(hr)) return hr;
    return left->GetTimeOfLastChange(pbc, NULL, ft);
  }
  STDMETHOD(GetDisplayName)(IBindCtx*, IMoniker*, LPOLESTR* out) override {
    if (!out) return E_POINTER;
    *out = CoStrDup((delim_ + item_).c_str());
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(ParseDisplayName)(IBindCtx* pbc, IMoniker* left, LPOLESTR name, ULONG* eaten, IMoniker** out) override {
    if (!out || !eaten) return E_POINTER;
    *out = NULL;
    *eaten = 0;
    IParseDisplayName* p = NULL;
    HRESULT hr = BindToObject(pbc, left, IID_IParseDisplayName, (void**)&p);
    if (FAILED(hr)) return hr;
    hr = p->ParseDisplayName(pbc, name, eaten, out);
    p->Release();
    return hr;
  }
  STDMETHOD(IsSystemMoniker)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = MKSYS_ITEMMONIKER;
    return S_OK;
  }

 protected:
  std::wstring Name() const override { return delim_ + Lower(item_); }

 private:
  static std::wstring Lower(const std::wstring& s) {
    std::wstring o = s;
    for (wchar_t& c : o) c = (wchar_t)towlower(c);
    return o;
  }
  static HRESULT ReadString(IStream* s, std::wstring* out) {
    DWORD len;
    HRESULT hr = ReadDword(s, &len);
    if (FAILED(hr)) return hr;
    if (len > 0x100000) return E_FAIL;
    std::vector<BYTE> v(len);
    if (len) hr = ReadExact(s, v.data(), len);
    if (FAILED(hr)) return hr;
    size_t z = 0;
    while (z < v.size() && v[z]) z++;
    std::string ansi((const char*)v.data(), z);
    if (z + 1 < v.size())
      *out = FromUtf16(v.data() + z + 1, v.size() - z - 1);
    else
      *out = winport::Widen(ansi.c_str());
    return S_OK;
  }
  static HRESULT WriteString(IStream* s, const std::wstring& str) {
    std::string ansi = AsciiOf(str);
    std::vector<BYTE> v(ansi.begin(), ansi.end());
    v.push_back(0);
    if (!IsAscii(str)) {
      std::vector<BYTE> u = Utf16Of(str);
      v.insert(v.end(), u.begin(), u.end());
    }
    HRESULT hr = WriteDword(s, (DWORD)v.size());
    if (SUCCEEDED(hr)) hr = WriteExact(s, v.data(), (ULONG)v.size());
    return hr;
  }
  std::wstring delim_, item_;
};

// ---------------------------------------------------------------------
// Composite monikers: monikers one after another

class CompositeMoniker final : public MonikerBase {
 public:
  CompositeMoniker() {}
  explicit CompositeMoniker(std::vector<IMoniker*> parts) : parts_(std::move(parts)) {
    for (IMoniker* m : parts_) m->AddRef();
  }
  ~CompositeMoniker() {
    for (IMoniker* m : parts_) m->Release();
  }
  const std::vector<IMoniker*>& Parts() const { return parts_; }

  STDMETHOD(GetClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    *out = kCompositeMoniker;
    return S_OK;
  }
  STDMETHOD(Load)(IStream* s) override {
    if (!s) return E_POINTER;
    DWORD n;
    HRESULT hr = ReadDword(s, &n);
    if (FAILED(hr)) return hr;
    for (IMoniker* m : parts_) m->Release();
    parts_.clear();
    for (DWORD i = 0; i < n && i < 1024; i++) {
      IMoniker* m = LoadMoniker(s);
      if (!m) return E_FAIL;
      parts_.push_back(m);
    }
    return S_OK;
  }
  STDMETHOD(Save)(IStream* s, BOOL clear) override {
    if (!s) return E_POINTER;
    HRESULT hr = WriteDword(s, (DWORD)parts_.size());
    for (IMoniker* m : parts_) {
      if (FAILED(hr)) break;
      hr = OleSaveToStream(m, s);
    }
    (void)clear;
    return hr;
  }

  // The last part binds, with the rest to its left.
  STDMETHOD(BindToObject)(IBindCtx* given, IMoniker* left, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    OwnCtx ctx(given);
    if (!left) {
      IUnknown* running = NULL;
      if (SUCCEEDED(Rot().GetObject(this, &running))) {
        HRESULT hr = running->QueryInterface(riid, out);
        running->Release();
        return hr;
      }
    }
    IMoniker* rest = NULL;
    HRESULT hr = AllButLast(left, &rest);
    if (FAILED(hr)) return hr;
    hr = parts_.back()->BindToObject(ctx.pbc, rest, riid, out);
    SafeRelease(rest);
    return hr;
  }
  STDMETHOD(BindToStorage)(IBindCtx* given, IMoniker* left, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    OwnCtx ctx(given);
    IMoniker* rest = NULL;
    HRESULT hr = AllButLast(left, &rest);
    if (FAILED(hr)) return hr;
    hr = parts_.back()->BindToStorage(ctx.pbc, rest, riid, out);
    SafeRelease(rest);
    return hr;
  }
  STDMETHOD(Enum)(BOOL forward, IEnumMoniker** out) override {
    if (!out) return E_POINTER;
    std::vector<IMoniker*> v = parts_;
    if (!forward) std::reverse(v.begin(), v.end());
    *out = new MonikerEnum(v);
    return S_OK;
  }
  STDMETHOD(IsEqual)(IMoniker* other) override {
    if (!other) return S_FALSE;
    CLSID c;
    if (FAILED(other->GetClassID(&c)) || !IsEqualCLSID(c, kCompositeMoniker)) return S_FALSE;
    CompositeMoniker* o = static_cast<CompositeMoniker*>(other);
    if (o->parts_.size() != parts_.size()) return S_FALSE;
    for (size_t i = 0; i < parts_.size(); i++)
      if (parts_[i]->IsEqual(o->parts_[i]) != S_OK) return S_FALSE;
    return S_OK;
  }
  STDMETHOD(IsRunning)(IBindCtx* given, IMoniker* left, IMoniker* newly) override {
    if (newly && IsEqual(newly) == S_OK) return S_OK;
    OwnCtx ctx(given);
    if (left) {
      IMoniker* full = NULL;
      HRESULT hr = CreateGenericComposite(left, this, &full);
      if (FAILED(hr)) return hr;
      hr = full->IsRunning(ctx.pbc, NULL, newly);
      full->Release();
      return hr;
    }
    if (Rot().IsRunning(this) == S_OK) return S_OK;
    IMoniker* rest = NULL;
    HRESULT hr = AllButLast(NULL, &rest);
    if (FAILED(hr)) return hr;
    hr = parts_.back()->IsRunning(ctx.pbc, rest, newly);
    SafeRelease(rest);
    return hr;
  }
  STDMETHOD(GetTimeOfLastChange)(IBindCtx* given, IMoniker* left, FILETIME* ft) override {
    if (!ft) return E_POINTER;
    if (!left && SUCCEEDED(Rot().GetTimeOfLastChange(this, ft))) return S_OK;
    OwnCtx ctx(given);
    IMoniker* rest = NULL;
    HRESULT hr = AllButLast(left, &rest);
    if (FAILED(hr)) return hr;
    hr = parts_.back()->GetTimeOfLastChange(ctx.pbc, rest, ft);
    SafeRelease(rest);
    return hr;
  }
  STDMETHOD(GetDisplayName)(IBindCtx* given, IMoniker*, LPOLESTR* out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    OwnCtx ctx(given);
    std::wstring all;
    for (IMoniker* m : parts_) {
      LPOLESTR s = NULL;
      if (SUCCEEDED(m->GetDisplayName(ctx.pbc, NULL, &s)) && s) all += s;
      CoTaskMemFree(s);
    }
    *out = CoStrDup(all.c_str());
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(ParseDisplayName)(IBindCtx* pbc, IMoniker* left, LPOLESTR name, ULONG* eaten, IMoniker** out) override {
    if (!out || !eaten) return E_POINTER;
    IMoniker* rest = NULL;
    HRESULT hr = AllButLast(left, &rest);
    if (FAILED(hr)) return hr;
    hr = parts_.back()->ParseDisplayName(pbc, rest, name, eaten, out);
    SafeRelease(rest);
    return hr;
  }
  STDMETHOD(IsSystemMoniker)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = MKSYS_GENERICCOMPOSITE;
    return S_OK;
  }
  STDMETHOD(Hash)(DWORD* out) override {
    if (!out) return E_POINTER;
    DWORD h = 0;
    for (IMoniker* m : parts_) {
      DWORD k = 0;
      m->Hash(&k);
      h = h * 31 + k;
    }
    *out = h;
    return S_OK;
  }

 protected:
  std::wstring Name() const override {
    std::wstring all;
    for (IMoniker* m : parts_) {
      LPOLESTR s = NULL;
      if (SUCCEEDED(m->GetDisplayName(NULL, NULL, &s)) && s) all += s;
      CoTaskMemFree(s);
    }
    return all;
  }

 private:
  // The parts but the last, after what is to the left.
  HRESULT AllButLast(IMoniker* left, IMoniker** out) {
    *out = NULL;
    if (parts_.empty()) return E_UNEXPECTED;
    std::vector<IMoniker*> v;
    if (left) v.push_back(left);
    for (size_t i = 0; i + 1 < parts_.size(); i++) v.push_back(parts_[i]);
    if (v.empty()) return S_OK;
    if (v.size() == 1) {
      *out = v[0];
      v[0]->AddRef();
      return S_OK;
    }
    *out = new CompositeMoniker(v);
    return S_OK;
  }
  std::vector<IMoniker*> parts_;
};

// The parts of a moniker, a composite's flattened.
void AddParts(IMoniker* m, std::vector<IMoniker*>& v) {
  CLSID c;
  if (SUCCEEDED(m->GetClassID(&c)) && IsEqualCLSID(c, kCompositeMoniker)) {
    for (IMoniker* p : static_cast<CompositeMoniker*>(m)->Parts()) v.push_back(p);
  } else {
    v.push_back(m);
  }
}

// A moniker of ours, by class, or another class's through COM.
// ---------------------------------------------------------------------
// URL monikers. Only file: URLs (and plain paths) bind: the data comes
// as a stream, at once, or for a binding that asked for it while the
// program runs, in a notice from a timer on the binding thread.

const wchar_t kBscHolder[] = L"_BSCB_Holder_";

// The local file a URL names, or empty when it names none.
std::wstring FileOfUrl(const std::wstring& url) {
  std::wstring rest;
  if (url.size() >= 5 && _wcsnicmp(url.c_str(), L"file:", 5) == 0) {
    rest = url.substr(5);
    if (rest.compare(0, 2, L"//") == 0) {
      rest = rest.substr(2);
      // file://host/path: only the local host.
      size_t slash = rest.find(L'/');
      std::wstring host = rest.substr(0, slash);
      if (!host.empty() && _wcsicmp(host.c_str(), L"localhost") != 0) return std::wstring();
      rest = slash == std::wstring::npos ? std::wstring() : rest.substr(slash);
    }
    // /C:/dir is the drive's.
    if (rest.size() >= 3 && rest[0] == L'/' && iswalpha(rest[1]) && (rest[2] == L':' || rest[2] == L'|')) {
      rest = rest.substr(1);
      rest[1] = L':';
    }
  } else {
    size_t colon = url.find(L':');
    // A scheme is two letters or more; one is a drive.
    if (colon != std::wstring::npos && colon > 1 && url.find_first_of(L"/\\") > colon) return std::wstring();
    return url;
  }
  std::wstring out;
  for (size_t i = 0; i < rest.size(); i++) {
    if (rest[i] == L'%' && i + 2 < rest.size() && iswxdigit(rest[i + 1]) && iswxdigit(rest[i + 2])) {
      // Escapes are of UTF-8 bytes.
      std::string bytes;
      while (i + 2 < rest.size() && rest[i] == L'%' && iswxdigit(rest[i + 1]) && iswxdigit(rest[i + 2])) {
        bytes += (char)wcstol(rest.substr(i + 1, 2).c_str(), NULL, 16);
        i += 3;
      }
      i--;
      out += winport::Widen(bytes.c_str());
    } else {
      out += rest[i];
    }
  }
  return out;
}

// The file's bytes in a stream of their own.
HRESULT OpenUrlStream(const std::wstring& url, IStream** out, ULONG* size) {
  *out = NULL;
  std::wstring path = FileOfUrl(url);
  if (path.empty()) return INET_E_UNKNOWN_PROTOCOL;
  FILE* f = fopen(winport::PosixPath(path.c_str()).c_str(), "rb");
  if (f == NULL) return INET_E_RESOURCE_NOT_FOUND;
  std::vector<BYTE> data;
  BYTE buf[16384];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
  bool bad = ferror(f) != 0;
  fclose(f);
  if (bad) return INET_E_DOWNLOAD_FAILURE;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, data.size() ? data.size() : 1);
  if (h == NULL) return E_OUTOFMEMORY;
  if (!data.empty()) {
    memcpy(GlobalLock(h), data.data(), data.size());
    GlobalUnlock(h);
  }
  HRESULT hr = CreateStreamOnHGlobal(h, TRUE, out);
  if (FAILED(hr)) {
    GlobalFree(h);
    return hr;
  }
  ULARGE_INTEGER len;
  len.QuadPart = data.size();
  (*out)->SetSize(len);
  *size = (ULONG)data.size();
  return S_OK;
}

class UrlBinding;
std::map<UINT_PTR, UrlBinding*>& Bindings() {
  static thread_local std::map<UINT_PTR, UrlBinding*> m;
  return m;
}

// A binding under way: it holds the callback until the data is in.
class UrlBinding final : public IBinding {
 public:
  UrlBinding(IBindStatusCallback* bsc, IStream* stream, ULONG size, const std::wstring& url)
      : bsc_(bsc), stream_(stream), size_(size), url_(url) {
    bsc_->AddRef();
    stream_->AddRef();
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IBinding)) {
      *out = static_cast<IBinding*>(this);
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
  STDMETHOD(Abort)() override {
    if (done_) return S_FALSE;
    Finish(E_ABORT);
    return S_OK;
  }
  // Suspended, the data waits until the binding resumes.
  STDMETHOD(Suspend)() override {
    if (done_) return E_FAIL;
    if (suspended_) return S_FALSE;
    suspended_ = true;
    StopTimer();
    return S_OK;
  }
  STDMETHOD(Resume)() override {
    if (done_) return E_FAIL;
    if (!suspended_) return S_FALSE;
    suspended_ = false;
    return Schedule() ? S_OK : E_FAIL;
  }
  STDMETHOD(SetPriority)(LONG p) override {
    priority_ = p;
    return S_OK;
  }
  STDMETHOD(GetPriority)(LONG* p) override {
    if (!p) return E_POINTER;
    *p = priority_;
    return S_OK;
  }
  STDMETHOD(GetBindResult)(CLSID* proto, DWORD* result, LPOLESTR* text, DWORD*) override {
    if (proto) *proto = CLSID_NULL;
    if (result) *result = (DWORD)result_;
    if (text) *text = NULL;
    return S_OK;
  }

  // Starts it: the data comes on the next timer.
  HRESULT Start() {
    HRESULT hr = bsc_->OnStartBinding(0, this);
    if (FAILED(hr)) {
      Finish(hr);
      return hr;
    }
    bsc_->OnProgress(0, size_, BINDSTATUS_BEGINDOWNLOADDATA, url_.c_str());
    if (done_ || suspended_) return S_OK;  // aborted or suspended from the callback
    if (!Schedule()) {
      Finish(E_FAIL);
      return E_FAIL;
    }
    return S_OK;
  }
  // Everything at once, for a binding that waits.
  void RunNow() {
    if (SUCCEEDED(bsc_->OnStartBinding(0, this))) Deliver();
    else Finish(E_ABORT);
  }

 private:
  ~UrlBinding() {
    if (bsc_) bsc_->Release();
    if (stream_) stream_->Release();
  }
  static void CALLBACK Tick(HWND, UINT, UINT_PTR id, DWORD) {
    auto it = Bindings().find(id);
    if (it == Bindings().end()) {
      KillTimer(NULL, id);
      return;
    }
    UrlBinding* b = it->second;
    b->Deliver();
  }
  void Deliver() {
    if (done_) return;
    AddRef();
    StopTimer();
    bsc_->OnProgress(size_, size_, BINDSTATUS_ENDDOWNLOADDATA, url_.c_str());
    if (!done_) {
      FORMATETC fmt = {0, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
      STGMEDIUM med = {};
      med.tymed = TYMED_ISTREAM;
      med.pstm = stream_;
      LARGE_INTEGER zero = {};
      stream_->Seek(zero, STREAM_SEEK_SET, NULL);
      bsc_->OnDataAvailable(BSCF_FIRSTDATANOTIFICATION | BSCF_LASTDATANOTIFICATION | BSCF_DATAFULLYAVAILABLE,
                            size_, &fmt, &med);
    }
    if (!done_) Finish(S_OK);
    Release();
  }
  bool Schedule() {
    timer_ = SetTimer(NULL, 0, 0, Tick);
    if (timer_ == 0) return false;
    AddRef();  // the timer's
    Bindings()[timer_] = this;
    return true;
  }
  void StopTimer() {
    if (timer_ == 0) return;
    KillTimer(NULL, timer_);
    Bindings().erase(timer_);
    timer_ = 0;
    Release();  // the timer's
  }
  // Reports the stop, an abort's too, and lets go of the callback.
  void Finish(HRESULT hr) {
    if (done_) return;
    done_ = true;
    result_ = hr;
    AddRef();
    StopTimer();
    IBindStatusCallback* bsc = bsc_;
    bsc_ = NULL;
    bsc->OnStopBinding(hr, NULL);
    bsc->Release();
    if (stream_) {
      stream_->Release();
      stream_ = NULL;
    }
    Release();
  }

  ULONG refs_ = 1;
  IBindStatusCallback* bsc_;
  IStream* stream_;
  ULONG size_;
  std::wstring url_;
  UINT_PTR timer_ = 0;
  LONG priority_ = THREAD_PRIORITY_NORMAL;
  HRESULT result_ = S_OK;
  bool done_ = false;
  bool suspended_ = false;
};

const CLSID kUrlMoniker = {0x79EAC9E0, 0xBAF9, 0x11CE, {0x8C, 0x82, 0x00, 0xAA, 0x00, 0x4B, 0xA9, 0x0B}};

class UrlMoniker final : public MonikerBase {
 public:
  explicit UrlMoniker(const std::wstring& url = std::wstring()) : url_(url) {}
  const std::wstring& Url() const { return url_; }

  STDMETHOD(GetClassID)(CLSID* out) override {
    if (!out) return E_POINTER;
    *out = kUrlMoniker;
    return S_OK;
  }
  // The URL's size in bytes, with its end, then the URL in UTF-16.
  STDMETHOD(Load)(IStream* s) override {
    if (!s) return E_POINTER;
    DWORD bytes;
    HRESULT hr = ReadDword(s, &bytes);
    if (FAILED(hr) || bytes > 0x200000) return FAILED(hr) ? hr : E_FAIL;
    std::vector<BYTE> u(bytes);
    if (bytes) hr = ReadExact(s, u.data(), bytes);
    if (FAILED(hr)) return hr;
    url_ = FromUtf16(u.data(), u.size());
    while (!url_.empty() && url_.back() == L'\0') url_.pop_back();
    return S_OK;
  }
  STDMETHOD(Save)(IStream* s, BOOL) override {
    if (!s) return E_POINTER;
    std::vector<BYTE> u = Utf16Of(url_);
    u.push_back(0);
    u.push_back(0);
    HRESULT hr = WriteDword(s, (DWORD)u.size());
    if (SUCCEEDED(hr)) hr = WriteExact(s, u.data(), (ULONG)u.size());
    return hr;
  }
  STDMETHOD(BindToObject)(IBindCtx*, IMoniker*, REFIID, void** out) override {
    if (out) *out = NULL;
    return MK_E_NOOBJECT;
  }
  STDMETHOD(BindToStorage)(IBindCtx* given, IMoniker*, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(riid, IID_IStream) && !IsEqualIID(riid, IID_IUnknown)) return E_NOINTERFACE;
    OwnCtx ctx(given);
    IBindStatusCallback* bsc = NULL;
    IUnknown* held = NULL;
    if (SUCCEEDED(ctx.pbc->GetObjectParam(const_cast<LPOLESTR>(kBscHolder), &held)) && held) {
      held->QueryInterface(IID_IBindStatusCallback, (void**)&bsc);
      held->Release();
    }
    IStream* stream = NULL;
    ULONG size = 0;
    HRESULT hr = OpenUrlStream(url_, &stream, &size);
    if (bsc == NULL) {
      *out = stream;
      return hr;
    }
    DWORD flags = 0;
    BINDINFO info = {};
    info.cbSize = sizeof info;
    bsc->GetBindInfo(&flags, &info);
    ReleaseStgMedium(&info.stgmedData);
    CoTaskMemFree(info.szExtraInfo);
    CoTaskMemFree(info.szCustomVerb);
    if (info.pUnk) info.pUnk->Release();
    if (FAILED(hr)) {
      bsc->Release();
      return hr;
    }
    UrlBinding* b = new UrlBinding(bsc, stream, size, url_);
    bsc->Release();
    if (flags & BINDF_ASYNCHRONOUS) {
      hr = b->Start();
      b->Release();
      stream->Release();
      return SUCCEEDED(hr) ? MK_S_ASYNCHRONOUS : hr;
    }
    b->RunNow();
    b->Release();
    *out = stream;
    return S_OK;
  }
  STDMETHOD(IsEqual)(IMoniker* other) override {
    if (!other) return S_FALSE;
    CLSID c;
    if (FAILED(other->GetClassID(&c)) || !IsEqualCLSID(c, kUrlMoniker)) return S_FALSE;
    return static_cast<UrlMoniker*>(other)->url_ == url_ ? S_OK : S_FALSE;
  }
  STDMETHOD(IsRunning)(IBindCtx*, IMoniker*, IMoniker* newly) override {
    return newly && IsEqual(newly) == S_OK ? S_OK : S_FALSE;
  }
  STDMETHOD(GetTimeOfLastChange)(IBindCtx*, IMoniker*, FILETIME* ft) override {
    if (!ft) return E_POINTER;
    std::wstring path = FileOfUrl(url_);
    struct stat st;
    if (path.empty() || stat(winport::PosixPath(path.c_str()).c_str(), &st) != 0) return MK_E_UNAVAILABLE;
    ULONGLONG t = ((ULONGLONG)st.st_mtime + 11644473600ULL) * 10000000ULL;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
    return S_OK;
  }
  STDMETHOD(GetDisplayName)(IBindCtx*, IMoniker*, LPOLESTR* out) override {
    if (!out) return E_POINTER;
    *out = CoStrDup(url_.c_str());
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(ParseDisplayName)(IBindCtx*, IMoniker*, LPOLESTR name, ULONG* eaten, IMoniker** out) override {
    if (!out || !eaten || !name) return E_POINTER;
    *out = NULL;
    *eaten = 0;
    HRESULT hr = CreateURLMoniker(this, name, out);
    if (SUCCEEDED(hr)) *eaten = (ULONG)wcslen(name);
    return hr;
  }
  STDMETHOD(IsSystemMoniker)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = 6;  // MKSYS_URLMONIKER
    return S_OK;
  }

 protected:
  std::wstring Name() const override { return url_; }

 private:
  std::wstring url_;
};

// A URL taken against the one it is relative to.
std::wstring JoinUrl(const std::wstring& base, const std::wstring& rel) {
  size_t colon = rel.find(L':');
  if (colon != std::wstring::npos && colon > 1 && rel.find_first_of(L"/\\") > colon) return rel;
  if (rel.empty()) return base;
  size_t start = base.find(L"://");
  start = start == std::wstring::npos ? 0 : base.find(L'/', start + 3);
  if (start == std::wstring::npos) start = base.size();
  if (rel[0] == L'/' || rel[0] == L'\\') return base.substr(0, start) + rel;
  size_t slash = base.find_last_of(L"/\\");
  std::wstring dir = slash == std::wstring::npos || slash < start ? base.substr(0, start) + L"/"
                                                                   : base.substr(0, slash + 1);
  return dir + rel;
}

HRESULT NewByClass(REFCLSID clsid, REFIID riid, void** out) {
  IUnknown* u = NULL;
  if (IsEqualCLSID(clsid, kFileMoniker))
    u = static_cast<IMoniker*>(new FileMoniker);
  else if (IsEqualCLSID(clsid, kItemMoniker))
    u = static_cast<IMoniker*>(new ItemMoniker);
  else if (IsEqualCLSID(clsid, kCompositeMoniker))
    u = static_cast<IMoniker*>(new CompositeMoniker);
  else if (IsEqualCLSID(clsid, kUrlMoniker))
    u = static_cast<IMoniker*>(new UrlMoniker);
  if (!u) return CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, riid, out);
  HRESULT hr = u->QueryInterface(riid, out);
  u->Release();
  return hr;
}

IMoniker* LoadMoniker(IStream* s) {
  IMoniker* m = NULL;
  return SUCCEEDED(OleLoadFromStream(s, IID_IMoniker, (void**)&m)) ? m : NULL;
}

// The message filter registered, which nothing calls here (there are no
// calls from other processes) but programs ask for back.
IMessageFilter* g_filter = NULL;

}  // namespace

extern "C" {

HRESULT WINAPI CreateBindCtx(DWORD, LPBC* out) {
  if (!out) return E_INVALIDARG;
  *out = new BindCtx;
  return S_OK;
}

HRESULT WINAPI GetRunningObjectTable(DWORD, LPRUNNINGOBJECTTABLE* out) {
  if (!out) return E_INVALIDARG;
  *out = &Rot();
  return S_OK;
}

HRESULT WINAPI CreateFileMoniker(LPCOLESTR path, LPMONIKER* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (!path) return MK_E_SYNTAX;
  *out = new FileMoniker(path);
  return S_OK;
}

HRESULT WINAPI CreateItemMoniker(LPCOLESTR delim, LPCOLESTR item, LPMONIKER* out) {
  if (!out) return E_INVALIDARG;
  *out = new ItemMoniker(delim ? delim : L"", item ? item : L"");
  return S_OK;
}

HRESULT WINAPI CreateGenericComposite(LPMONIKER first, LPMONIKER rest, LPMONIKER* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (!first && !rest) return E_INVALIDARG;
  if (!first || !rest) {
    *out = first ? first : rest;
    (*out)->AddRef();
    return S_OK;
  }
  std::vector<IMoniker*> v;
  AddParts(first, v);
  AddParts(rest, v);
  *out = new CompositeMoniker(v);
  return S_OK;
}

HRESULT WINAPI BindMoniker(LPMONIKER mk, DWORD, REFIID riid, LPVOID* out) {
  if (!mk || !out) return E_INVALIDARG;
  *out = NULL;
  IBindCtx* pbc = NULL;
  HRESULT hr = CreateBindCtx(0, &pbc);
  if (FAILED(hr)) return hr;
  hr = mk->BindToObject(pbc, NULL, riid, out);
  pbc->Release();
  return hr;
}

// "path!item!item": a file moniker, then item monikers ("!" delimits).
HRESULT WINAPI MkParseDisplayName(LPBC pbc, LPCOLESTR name, ULONG* eaten, LPMONIKER* out) {
  if (!name || !eaten || !out) return E_INVALIDARG;
  *out = NULL;
  *eaten = 0;
  std::wstring s = name;
  if (s.empty()) return MK_E_SYNTAX;
  size_t bang = s.find(L'!');
  IMoniker* mk = NULL;
  HRESULT hr = CreateFileMoniker(s.substr(0, bang).c_str(), &mk);
  if (FAILED(hr)) return hr;
  while (bang != std::wstring::npos) {
    size_t next = s.find(L'!', bang + 1);
    std::wstring item = s.substr(bang + 1, next == std::wstring::npos ? std::wstring::npos : next - bang - 1);
    IMoniker* im = NULL;
    IMoniker* both = NULL;
    CreateItemMoniker(L"!", item.c_str(), &im);
    hr = CreateGenericComposite(mk, im, &both);
    im->Release();
    mk->Release();
    if (FAILED(hr)) return hr;
    mk = both;
    bang = next;
  }
  (void)pbc;
  *eaten = (ULONG)s.size();
  *out = mk;
  return S_OK;
}

HRESULT WINAPI OleSaveToStream(LPPERSISTSTREAM ps, LPSTREAM stm) {
  if (!stm) return E_INVALIDARG;
  if (!ps) return WriteClassStm(stm, CLSID_NULL);
  CLSID clsid;
  HRESULT hr = ps->GetClassID(&clsid);
  if (SUCCEEDED(hr)) hr = WriteClassStm(stm, clsid);
  if (SUCCEEDED(hr)) hr = ps->Save(stm, TRUE);
  return hr;
}

HRESULT WINAPI OleLoadFromStream(LPSTREAM stm, REFIID riid, LPVOID* out) {
  if (!stm || !out) return E_INVALIDARG;
  *out = NULL;
  CLSID clsid;
  HRESULT hr = ReadClassStm(stm, &clsid);
  if (FAILED(hr)) return hr;
  if (IsEqualCLSID(clsid, CLSID_NULL)) return S_FALSE;
  IPersistStream* ps = NULL;
  hr = NewByClass(clsid, IID_IPersistStream, (void**)&ps);
  if (FAILED(hr)) return hr;
  hr = ps->Load(stm);
  if (SUCCEEDED(hr)) hr = ps->QueryInterface(riid, out);
  ps->Release();
  return hr;
}

HRESULT WINAPI CoRegisterMessageFilter(LPMESSAGEFILTER filter, LPMESSAGEFILTER* old) {
  if (old)
    *old = g_filter;
  else if (g_filter)
    g_filter->Release();
  g_filter = filter;
  if (filter) filter->AddRef();
  return S_OK;
}

HRESULT WINAPI CreateURLMoniker(LPMONIKER ctx, LPCWSTR url, LPMONIKER* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (!url) return E_INVALIDARG;
  std::wstring full = url;
  CLSID c;
  if (ctx && SUCCEEDED(ctx->GetClassID(&c)) && IsEqualCLSID(c, kUrlMoniker))
    full = JoinUrl(static_cast<UrlMoniker*>(ctx)->Url(), url);
  if (full.empty()) return MK_E_SYNTAX;
  *out = new UrlMoniker(full);
  return S_OK;
}

HRESULT WINAPI RegisterBindStatusCallback(LPBC pbc, IBindStatusCallback* bsc, IBindStatusCallback** prev,
                                          DWORD) {
  if (!pbc || !bsc) return E_INVALIDARG;
  if (prev) {
    *prev = NULL;
    IUnknown* held = NULL;
    if (SUCCEEDED(pbc->GetObjectParam(const_cast<LPOLESTR>(kBscHolder), &held)) && held) {
      held->QueryInterface(IID_IBindStatusCallback, (void**)prev);
      held->Release();
    }
  }
  return pbc->RegisterObjectParam(const_cast<LPOLESTR>(kBscHolder), bsc);
}

HRESULT WINAPI RevokeBindStatusCallback(LPBC pbc, IBindStatusCallback* bsc) {
  if (!pbc || !bsc) return E_INVALIDARG;
  IUnknown* held = NULL;
  if (FAILED(pbc->GetObjectParam(const_cast<LPOLESTR>(kBscHolder), &held)) || !held) return S_FALSE;
  IBindStatusCallback* cur = NULL;
  held->QueryInterface(IID_IBindStatusCallback, (void**)&cur);
  held->Release();
  HRESULT hr = S_FALSE;
  if (cur == bsc) hr = pbc->RevokeObjectParam(const_cast<LPOLESTR>(kBscHolder));
  if (cur) cur->Release();
  return hr;
}

HRESULT WINAPI CreateAsyncBindCtx(DWORD, IBindStatusCallback* bsc, IEnumFORMATETC*, IBindCtx** out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (!bsc) return E_INVALIDARG;
  IBindCtx* pbc = new BindCtx;
  BIND_OPTS opts = {sizeof opts};
  pbc->GetBindOptions(&opts);
  opts.grfFlags |= BIND_MAYBOTHERUSER;
  pbc->SetBindOptions(&opts);
  HRESULT hr = RegisterBindStatusCallback(pbc, bsc, NULL, 0);
  if (FAILED(hr)) {
    pbc->Release();
    return hr;
  }
  *out = pbc;
  return S_OK;
}

HRESULT WINAPI IsAsyncMoniker(IMoniker* mk) {
  if (!mk) return E_INVALIDARG;
  CLSID c;
  return SUCCEEDED(mk->GetClassID(&c)) && IsEqualCLSID(c, kUrlMoniker) ? S_OK : S_FALSE;
}

}  // extern "C"
