// shell_link.cpp - shortcuts: CLSID_ShellLink, with IShellLinkA and W,
// IPersistFile and IPersistStream. A shortcut is kept in Windows' .lnk
// format (the Shell Link Binary File Format), so that Windows' own
// shortcuts open here and these open on Windows: the target as the
// LinkInfo's local path (in ANSI, which is UTF-8 here, and in UTF-16),
// and the description, relative path, working directory, arguments and
// icon as UTF-16 strings. Reading also takes the target from the
// environment variables block, as Windows writes it for a path with
// %VARIABLES%.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <shlobj.h>

#include "shell.h"
#include "winport.h"

#include <string.h>

#include <functional>
#include <string>
#include <vector>

using winport::Narrow;
using winport::Widen;

namespace {

enum : DWORD {
  kHasIdList = 0x1,
  kHasLinkInfo = 0x2,
  kHasName = 0x4,
  kHasRelativePath = 0x8,
  kHasWorkingDir = 0x10,
  kHasArguments = 0x20,
  kHasIconLocation = 0x40,
  kIsUnicode = 0x80,
  kHasExpString = 0x200,
};
const DWORD kVolumeAndLocalPath = 1;
const DWORD kEnvironmentBlock = 0xA0000001;

// What a shortcut says.
struct Link {
  std::wstring path, name, relative, dir, args, icon;
  int iconIndex = 0;
  int show = SW_SHOWNORMAL;
  WORD hotkey = 0;
  DWORD attributes = 0;
  FILETIME created = {}, accessed = {}, written = {};
  DWORD size = 0;
};

void Put(std::vector<BYTE>& b, const void* p, size_t n) {
  const BYTE* c = (const BYTE*)p;
  b.insert(b.end(), c, c + n);
}
void Put16(std::vector<BYTE>& b, WORD v) { Put(b, &v, 2); }
void Put32(std::vector<BYTE>& b, DWORD v) { Put(b, &v, 4); }
void PutUtf16(std::vector<BYTE>& b, const std::wstring& s, bool zero) {
  for (wchar_t c : s) {
    if ((DWORD)c >= 0x10000) {
      DWORD v = (DWORD)c - 0x10000;
      Put16(b, (WORD)(0xD800 + (v >> 10)));
      Put16(b, (WORD)(0xDC00 + (v & 0x3FF)));
    } else {
      Put16(b, (WORD)c);
    }
  }
  if (zero) Put16(b, 0);
}
size_t Utf16Length(const std::wstring& s) {
  size_t n = 0;
  for (wchar_t c : s) n += (DWORD)c >= 0x10000 ? 2 : 1;
  return n;
}
std::wstring FromUtf16(const BYTE* p, size_t units) {
  std::wstring s;
  for (size_t i = 0; i < units; i++) {
    DWORD c = p[2 * i] | (p[2 * i + 1] << 8);
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
      DWORD d = p[2 * i + 2] | (p[2 * i + 3] << 8);
      if (d >= 0xDC00 && d < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        i++;
      }
    }
    s += (wchar_t)c;
  }
  return s;
}
// A zero-ended string at off within [p, p + n).
std::string AnsiAt(const BYTE* p, size_t n, size_t off) {
  if (off >= n) return std::string();
  size_t e = off;
  while (e < n && p[e]) e++;
  return std::string((const char*)p + off, e - off);
}
std::wstring Utf16At(const BYTE* p, size_t n, size_t off) {
  if (off >= n) return std::wstring();
  size_t e = off;
  while (e + 1 < n && (p[e] || p[e + 1])) e += 2;
  return FromUtf16(p + off, (e - off) / 2);
}

std::vector<BYTE> Write(const Link& l) {
  std::vector<BYTE> b;
  DWORD flags = kIsUnicode;
  if (!l.path.empty()) flags |= kHasLinkInfo;
  if (!l.name.empty()) flags |= kHasName;
  if (!l.relative.empty()) flags |= kHasRelativePath;
  if (!l.dir.empty()) flags |= kHasWorkingDir;
  if (!l.args.empty()) flags |= kHasArguments;
  if (!l.icon.empty()) flags |= kHasIconLocation;
  Put32(b, 0x4C);
  Put(b, &CLSID_ShellLink, sizeof(GUID));
  Put32(b, flags);
  Put32(b, l.attributes);
  Put(b, &l.created, 8);
  Put(b, &l.accessed, 8);
  Put(b, &l.written, 8);
  Put32(b, l.size);
  Put32(b, (DWORD)l.iconIndex);
  Put32(b, (DWORD)l.show);
  Put16(b, l.hotkey);
  Put16(b, 0);
  Put32(b, 0);
  Put32(b, 0);
  if (flags & kHasLinkInfo) {
    // The header with the UTF-16 offsets (0x24 bytes), the volume (a
    // fixed drive with no label), the path in ANSI with an empty suffix,
    // and both again in UTF-16.
    std::string ansi = Narrow(l.path.c_str());
    const DWORD header = 0x24, volume = 0x11;
    DWORD volumeAt = header, pathAt = volumeAt + volume, suffixAt = pathAt + (DWORD)ansi.size() + 1;
    DWORD pathUAt = (suffixAt + 1 + 1) & ~1u, suffixUAt = pathUAt + (DWORD)(Utf16Length(l.path) + 1) * 2;
    DWORD total = suffixUAt + 2;
    Put32(b, total);
    Put32(b, header);
    Put32(b, kVolumeAndLocalPath);
    Put32(b, volumeAt);
    Put32(b, pathAt);
    Put32(b, 0);
    Put32(b, suffixAt);
    Put32(b, pathUAt);
    Put32(b, suffixUAt);
    Put32(b, volume);
    Put32(b, DRIVE_FIXED);
    Put32(b, 0);
    Put32(b, 0x10);
    b.push_back(0);
    Put(b, ansi.c_str(), ansi.size() + 1);
    b.push_back(0);
    if ((b.size() & 1) != 0) b.push_back(0);
    PutUtf16(b, l.path, true);
    Put16(b, 0);
  }
  for (const std::wstring* s : {&l.name, &l.relative, &l.dir, &l.args, &l.icon}) {
    if (s->empty()) continue;
    Put16(b, (WORD)Utf16Length(*s));
    PutUtf16(b, *s, false);
  }
  Put32(b, 0);  // no extra data
  return b;
}

// Reads a shortcut through read(buffer, bytes), which says whether it
// got them all.
HRESULT Read(const std::function<bool(void*, size_t)>& read, Link* l) {
  BYTE h[0x4C];
  if (!read(h, sizeof h)) return E_FAIL;
  DWORD size, flags;
  memcpy(&size, h, 4);
  if (size != 0x4C || memcmp(h + 4, &CLSID_ShellLink, sizeof(GUID)) != 0) return E_FAIL;
  memcpy(&flags, h + 0x14, 4);
  memcpy(&l->attributes, h + 0x18, 4);
  memcpy(&l->created, h + 0x1C, 8);
  memcpy(&l->accessed, h + 0x24, 8);
  memcpy(&l->written, h + 0x2C, 8);
  memcpy(&l->size, h + 0x34, 4);
  memcpy(&l->iconIndex, h + 0x38, 4);
  memcpy(&l->show, h + 0x3C, 4);
  memcpy(&l->hotkey, h + 0x40, 2);
  if (flags & kHasIdList) {
    // The item list is the Windows shell's; the LinkInfo has the path.
    WORD n;
    if (!read(&n, 2)) return E_FAIL;
    std::vector<BYTE> skip(n);
    if (n && !read(skip.data(), n)) return E_FAIL;
  }
  if (flags & kHasLinkInfo) {
    DWORD n;
    if (!read(&n, 4) || n < 0x1C) return E_FAIL;
    std::vector<BYTE> info(n);
    memcpy(info.data(), &n, 4);
    if (!read(info.data() + 4, n - 4)) return E_FAIL;
    DWORD header, infoFlags, pathAt, suffixAt, pathUAt = 0, suffixUAt = 0;
    memcpy(&header, &info[4], 4);
    memcpy(&infoFlags, &info[8], 4);
    memcpy(&pathAt, &info[0x10], 4);
    memcpy(&suffixAt, &info[0x18], 4);
    if (header >= 0x24 && n >= 0x24) {
      memcpy(&pathUAt, &info[0x1C], 4);
      memcpy(&suffixUAt, &info[0x20], 4);
    }
    if (infoFlags & kVolumeAndLocalPath) {
      if (pathUAt)
        l->path = Utf16At(info.data(), n, pathUAt) + Utf16At(info.data(), n, suffixUAt);
      else
        l->path = Widen((AnsiAt(info.data(), n, pathAt) + AnsiAt(info.data(), n, suffixAt)).c_str());
    }
  }
  bool unicode = (flags & kIsUnicode) != 0;
  const DWORD kinds[] = {kHasName, kHasRelativePath, kHasWorkingDir, kHasArguments, kHasIconLocation};
  std::wstring* out[] = {&l->name, &l->relative, &l->dir, &l->args, &l->icon};
  for (int i = 0; i < 5; i++) {
    if (!(flags & kinds[i])) continue;
    WORD count;
    if (!read(&count, 2)) return E_FAIL;
    std::vector<BYTE> s(count * (unicode ? 2 : 1));
    if (count && !read(s.data(), s.size())) return E_FAIL;
    *out[i] = unicode ? FromUtf16(s.data(), count) : Widen(std::string(s.begin(), s.end()).c_str());
  }
  // Extra data, to the block of fewer than four bytes that ends it.
  for (;;) {
    DWORD n;
    if (!read(&n, 4) || n < 4) break;
    std::vector<BYTE> block(n);
    memcpy(block.data(), &n, 4);
    if (!read(block.data() + 4, n - 4)) break;
    DWORD sig = 0;
    if (n >= 8) memcpy(&sig, &block[4], 4);
    if (sig == kEnvironmentBlock && n >= 0x314 && l->path.empty()) {
      // The target with %VARIABLES%, in ANSI and then UTF-16.
      std::wstring t = Utf16At(block.data(), n, 8 + 260);
      if (t.empty()) t = Widen(AnsiAt(block.data(), 8 + 260, 8).c_str());
      wchar_t buf[MAX_PATH * 2];
      if (ExpandEnvironmentStringsW(t.c_str(), buf, MAX_PATH * 2)) l->path = buf;
    }
  }
  return S_OK;
}

std::wstring FullPath(const std::wstring& p) {
  if (p.empty()) return p;
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, NULL);
  return n && n < MAX_PATH * 2 ? std::wstring(buf) : p;
}

class ShellLink final : public IShellLinkW, public IPersistFile, public IPersistStream {
 public:
  // IShellLinkA, a part of its own with the same object behind it.
  class Ansi : public IShellLinkA {
   public:
    explicit Ansi(ShellLink* o) : o_(o) {}
    STDMETHOD(QueryInterface)(REFIID riid, void** out) override { return o_->QueryInterface(riid, out); }
    STDMETHOD_(ULONG, AddRef)() override { return o_->AddRef(); }
    STDMETHOD_(ULONG, Release)() override { return o_->Release(); }
    STDMETHOD(GetPath)(LPSTR buf, int cch, WIN32_FIND_DATAA* fd, DWORD flags) override {
      WIN32_FIND_DATAW w;
      HRESULT hr = o_->GetPath(NULL, 0, fd ? &w : NULL, flags);
      if (fd) {
        memcpy(fd, &w, offsetof(WIN32_FIND_DATAA, cFileName));
        lstrcpynA(fd->cFileName, Narrow(w.cFileName).c_str(), MAX_PATH);
        lstrcpynA(fd->cAlternateFileName, Narrow(w.cAlternateFileName).c_str(), 14);
      }
      return Out(o_->link_.path, buf, cch, hr);
    }
    STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE* pidl) override { return o_->GetIDList(pidl); }
    STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) override { return o_->SetIDList(pidl); }
    STDMETHOD(GetDescription)(LPSTR buf, int cch) override { return Out(o_->link_.name, buf, cch, S_OK); }
    STDMETHOD(SetDescription)(LPCSTR s) override { return o_->SetDescription(In(s).c_str()); }
    STDMETHOD(GetWorkingDirectory)(LPSTR buf, int cch) override { return Out(o_->link_.dir, buf, cch, S_OK); }
    STDMETHOD(SetWorkingDirectory)(LPCSTR s) override { return o_->SetWorkingDirectory(In(s).c_str()); }
    STDMETHOD(GetArguments)(LPSTR buf, int cch) override { return Out(o_->link_.args, buf, cch, S_OK); }
    STDMETHOD(SetArguments)(LPCSTR s) override { return o_->SetArguments(In(s).c_str()); }
    STDMETHOD(GetHotkey)(WORD* k) override { return o_->GetHotkey(k); }
    STDMETHOD(SetHotkey)(WORD k) override { return o_->SetHotkey(k); }
    STDMETHOD(GetShowCmd)(int* c) override { return o_->GetShowCmd(c); }
    STDMETHOD(SetShowCmd)(int c) override { return o_->SetShowCmd(c); }
    STDMETHOD(GetIconLocation)(LPSTR buf, int cch, int* index) override {
      if (index) *index = o_->link_.iconIndex;
      return Out(o_->link_.icon, buf, cch, S_OK);
    }
    STDMETHOD(SetIconLocation)(LPCSTR s, int index) override { return o_->SetIconLocation(In(s).c_str(), index); }
    STDMETHOD(SetRelativePath)(LPCSTR s, DWORD r) override { return o_->SetRelativePath(In(s).c_str(), r); }
    STDMETHOD(Resolve)(HWND h, DWORD flags) override { return o_->Resolve(h, flags); }
    STDMETHOD(SetPath)(LPCSTR s) override { return o_->SetPath(In(s).c_str()); }

   private:
    static std::wstring In(LPCSTR s) { return s ? Widen(s) : std::wstring(); }
    static HRESULT Out(const std::wstring& s, LPSTR buf, int cch, HRESULT hr) {
      if (!buf || cch <= 0) return E_INVALIDARG;
      lstrcpynA(buf, Narrow(s.c_str()).c_str(), cch);
      return hr;
    }
    ShellLink* o_;
  };

  ShellLink() : ansi_(this) {}

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellLinkW))
      *out = static_cast<IShellLinkW*>(this);
    else if (IsEqualIID(riid, IID_IShellLinkA))
      *out = static_cast<IShellLinkA*>(&ansi_);
    else if (IsEqualIID(riid, IID_IPersistFile) || IsEqualIID(riid, IID_IPersist))
      *out = static_cast<IPersistFile*>(this);
    else if (IsEqualIID(riid, IID_IPersistStream))
      *out = static_cast<IPersistStream*>(this);
    else {
      *out = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  // IShellLinkW
  STDMETHOD(GetPath)(LPWSTR buf, int cch, WIN32_FIND_DATAW* fd, DWORD) override {
    if (fd) {
      memset(fd, 0, sizeof *fd);
      fd->dwFileAttributes = link_.attributes;
      fd->ftCreationTime = link_.created;
      fd->ftLastAccessTime = link_.accessed;
      fd->ftLastWriteTime = link_.written;
      fd->nFileSizeLow = link_.size;
      size_t slash = link_.path.find_last_of(L"\\/");
      lstrcpynW(fd->cFileName, link_.path.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), MAX_PATH);
    }
    if (buf && cch > 0) lstrcpynW(buf, link_.path.c_str(), cch);
    else if (buf || cch < 0) return E_INVALIDARG;
    // S_FALSE: a shortcut with no path (to something not in the file
    // system).
    return link_.path.empty() ? S_FALSE : S_OK;
  }
  STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE* pidl) override {
    if (!pidl) return E_POINTER;
    *pidl = link_.path.empty() ? NULL : ILCreateFromPathW(link_.path.c_str());
    return *pidl ? S_OK : S_FALSE;
  }
  STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) override {
    wchar_t buf[MAX_PATH * 2];
    if (!pidl || !SHGetPathFromIDListW(pidl, buf)) return E_INVALIDARG;
    return SetPath(buf);
  }
  STDMETHOD(GetDescription)(LPWSTR buf, int cch) override { return Copy(link_.name, buf, cch); }
  STDMETHOD(SetDescription)(LPCWSTR s) override { return Set(&link_.name, s); }
  STDMETHOD(GetWorkingDirectory)(LPWSTR buf, int cch) override { return Copy(link_.dir, buf, cch); }
  STDMETHOD(SetWorkingDirectory)(LPCWSTR s) override { return Set(&link_.dir, s); }
  STDMETHOD(GetArguments)(LPWSTR buf, int cch) override { return Copy(link_.args, buf, cch); }
  STDMETHOD(SetArguments)(LPCWSTR s) override { return Set(&link_.args, s); }
  STDMETHOD(GetHotkey)(WORD* k) override {
    if (!k) return E_POINTER;
    *k = link_.hotkey;
    return S_OK;
  }
  STDMETHOD(SetHotkey)(WORD k) override {
    link_.hotkey = k;
    dirty_ = true;
    return S_OK;
  }
  STDMETHOD(GetShowCmd)(int* c) override {
    if (!c) return E_POINTER;
    *c = link_.show;
    return S_OK;
  }
  STDMETHOD(SetShowCmd)(int c) override {
    link_.show = c;
    dirty_ = true;
    return S_OK;
  }
  STDMETHOD(GetIconLocation)(LPWSTR buf, int cch, int* index) override {
    if (index) *index = link_.iconIndex;
    return Copy(link_.icon, buf, cch);
  }
  STDMETHOD(SetIconLocation)(LPCWSTR s, int index) override {
    link_.iconIndex = index;
    return Set(&link_.icon, s);
  }
  STDMETHOD(SetRelativePath)(LPCWSTR s, DWORD) override { return Set(&link_.relative, s); }
  // Finds the target: where the shortcut says, or else by its relative
  // path from the shortcut's own folder.
  STDMETHOD(Resolve)(HWND, DWORD) override {
    if (!link_.path.empty() && GetFileAttributesW(link_.path.c_str()) != INVALID_FILE_ATTRIBUTES) return S_OK;
    if (!link_.relative.empty() && !file_.empty()) {
      size_t slash = file_.find_last_of(L"\\/");
      std::wstring p = FullPath(file_.substr(0, slash == std::wstring::npos ? 0 : slash + 1) + link_.relative);
      if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
        link_.path = p;
        dirty_ = true;
        return S_OK;
      }
    }
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }
  STDMETHOD(SetPath)(LPCWSTR s) override {
    if (!s) return E_INVALIDARG;
    // %VARIABLES% are expanded, and the path made whole.
    wchar_t buf[MAX_PATH * 2];
    std::wstring p = s;
    if (ExpandEnvironmentStringsW(s, buf, MAX_PATH * 2)) p = buf;
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    link_.path = FullPath(p);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(link_.path.c_str(), GetFileExInfoStandard, &a)) {
      link_.attributes = a.dwFileAttributes;
      link_.created = a.ftCreationTime;
      link_.accessed = a.ftLastAccessTime;
      link_.written = a.ftLastWriteTime;
      link_.size = a.nFileSizeLow;
    } else {
      link_.attributes = 0;
      link_.size = 0;
    }
    dirty_ = true;
    return S_OK;
  }

  // IPersist
  STDMETHOD(GetClassID)(CLSID* clsid) override {
    if (!clsid) return E_POINTER;
    *clsid = CLSID_ShellLink;
    return S_OK;
  }
  // IPersistFile and IPersistStream
  STDMETHOD(IsDirty)() override { return dirty_ ? S_OK : S_FALSE; }
  STDMETHOD(Load)(LPCOLESTR name, DWORD) override {
    if (!name) return E_INVALIDARG;
    HANDLE f = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    Link l;
    HRESULT hr = ::Read(
        [f](void* p, size_t n) {
          DWORD got = 0;
          return ReadFile(f, p, (DWORD)n, &got, NULL) && got == n;
        },
        &l);
    CloseHandle(f);
    if (FAILED(hr)) return hr;
    link_ = l;
    file_ = FullPath(name);
    dirty_ = false;
    return S_OK;
  }
  STDMETHOD(Save)(LPCOLESTR name, BOOL remember) override {
    std::wstring to = name ? FullPath(name) : file_;
    if (to.empty()) return E_INVALIDARG;
    std::vector<BYTE> b = Write(link_);
    HANDLE f = CreateFileW(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, b.data(), (DWORD)b.size(), &wrote, NULL) && wrote == b.size();
    CloseHandle(f);
    if (!ok) return E_FAIL;
    if (remember || !name) {
      file_ = to;
      dirty_ = false;
    }
    return S_OK;
  }
  STDMETHOD(SaveCompleted)(LPCOLESTR) override { return S_OK; }
  STDMETHOD(GetCurFile)(LPOLESTR* name) override {
    if (!name) return E_POINTER;
    *name = NULL;
    if (file_.empty()) return S_FALSE;
    size_t bytes = (file_.size() + 1) * sizeof(wchar_t);
    *name = (LPOLESTR)CoTaskMemAlloc(bytes);
    if (!*name) return E_OUTOFMEMORY;
    memcpy(*name, file_.c_str(), bytes);
    return S_OK;
  }
  STDMETHOD(Load)(IStream* s) override {
    if (!s) return E_POINTER;
    Link l;
    HRESULT hr = ::Read(
        [s](void* p, size_t n) {
          ULONG got = 0;
          return SUCCEEDED(s->Read(p, (ULONG)n, &got)) && got == n;
        },
        &l);
    if (FAILED(hr)) return hr;
    link_ = l;
    dirty_ = false;
    return S_OK;
  }
  STDMETHOD(Save)(IStream* s, BOOL clear) override {
    if (!s) return E_POINTER;
    std::vector<BYTE> b = Write(link_);
    ULONG wrote = 0;
    HRESULT hr = s->Write(b.data(), (ULONG)b.size(), &wrote);
    if (FAILED(hr)) return hr;
    if (wrote != b.size()) return STG_E_MEDIUMFULL;
    if (clear) dirty_ = false;
    return S_OK;
  }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* size) override {
    if (!size) return E_POINTER;
    size->QuadPart = Write(link_).size();
    return S_OK;
  }

 private:
  HRESULT Copy(const std::wstring& s, LPWSTR buf, int cch) {
    if (!buf || cch <= 0) return E_INVALIDARG;
    lstrcpynW(buf, s.c_str(), cch);
    return S_OK;
  }
  HRESULT Set(std::wstring* to, LPCWSTR s) {
    *to = s ? s : L"";
    dirty_ = true;
    return S_OK;
  }

  LONG refs_ = 1;
  Ansi ansi_;
  Link link_;
  std::wstring file_;  // the file it was loaded from or saved to
  bool dirty_ = false;
};

class Factory : public IClassFactory {
 public:
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *out = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  // Static, so not counted.
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    ShellLink* l = new ShellLink;
    HRESULT hr = l->QueryInterface(riid, out);
    l->Release();
    return hr;
  }
  STDMETHOD(LockServer)(BOOL) override { return S_OK; }
};

}  // namespace

namespace winport {
namespace ole {

HRESULT GetShellLinkClassObject(REFIID riid, void** out) {
  static Factory factory;
  return factory.QueryInterface(riid, out);
}

}  // namespace ole

// The target of a shortcut file (for ShellExecute), or empty when the
// file is not one.
std::wstring ShortcutTarget(const std::wstring& file, std::wstring* args, std::wstring* dir) {
  ShellLink* l = new ShellLink;
  std::wstring target;
  if (SUCCEEDED(l->Load(file.c_str(), STGM_READ))) {
    l->Resolve(NULL, SLR_NO_UI);
    wchar_t buf[MAX_PATH * 2];
    if (l->GetPath(buf, MAX_PATH * 2, NULL, 0) == S_OK) target = buf;
    if (args && SUCCEEDED(l->GetArguments(buf, MAX_PATH * 2))) *args = buf;
    if (dir && SUCCEEDED(l->GetWorkingDirectory(buf, MAX_PATH * 2))) *dir = buf;
  }
  l->Release();
  return target;
}

}  // namespace winport
