// shell_namespace.cpp - the shell namespace over the file system: item
// ID lists, shell folders (IShellFolder) and their enumerators, and the
// functions that make and read ID lists; and SHGetFileInfo, with the
// system image list's document and folder icons.
//
// The desktop, the namespace's root, is the file system's root "/". An
// item ID list names a path under it with one item for each component;
// an item holds its component in UTF-8, ending with a NUL. The empty list
// is the desktop itself. Items are made with the task allocator, as the
// shell makes them, so CoTaskMemFree (or ILFree) frees them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>

#include "shell.h"
#include "winport.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

using namespace winport;

namespace winport {

std::vector<std::string> PidlItems(LPCITEMIDLIST pidl) {
  std::vector<std::string> v;
  if (!pidl) return v;
  const BYTE* p = (const BYTE*)pidl;
  for (;;) {
    USHORT cb;
    memcpy(&cb, p, sizeof cb);
    if (cb == 0) break;
    const char* name = (const char*)p + sizeof(USHORT);
    size_t n = cb - sizeof(USHORT);
    while (n > 0 && name[n - 1] == 0) n--;
    v.push_back(std::string(name, n));
    p += cb;
  }
  return v;
}

LPITEMIDLIST MakePidl(const std::vector<std::string>& items) {
  size_t size = sizeof(USHORT);
  for (const std::string& s : items) size += sizeof(USHORT) + s.size() + 1;
  BYTE* p = (BYTE*)CoTaskMemAlloc(size);
  if (!p) return NULL;
  BYTE* q = p;
  for (const std::string& s : items) {
    USHORT cb = (USHORT)(sizeof(USHORT) + s.size() + 1);
    memcpy(q, &cb, sizeof cb);
    memcpy(q + sizeof cb, s.c_str(), s.size() + 1);
    q += cb;
  }
  USHORT zero = 0;
  memcpy(q, &zero, sizeof zero);
  return (LPITEMIDLIST)p;
}

std::string JoinPath(const std::vector<std::string>& items) {
  std::string path;
  for (const std::string& s : items) path += "/" + s;
  return path.empty() ? "/" : path;
}

std::vector<std::string> SplitPath(const std::string& path) {
  std::vector<std::string> v;
  size_t i = 0;
  while (i < path.size()) {
    size_t j = path.find('/', i);
    if (j == std::string::npos) j = path.size();
    std::string part = path.substr(i, j - i);
    if (part == "..") {
      if (!v.empty()) v.pop_back();
    } else if (!part.empty() && part != ".") {
      v.push_back(part);
    }
    i = j + 1;
  }
  return v;
}

std::string AbsolutePath(const std::wstring& name) {
  std::string p = PosixPath(name.c_str());
  if (p.empty() || p[0] != '/') {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) p = std::string(cwd) + "/" + p;
  }
  return JoinPath(SplitPath(p));
}

bool IsDir(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> ListDir(const std::string& path, bool folders, bool files, bool hidden) {
  std::vector<std::string> names;
  DIR* d = opendir(path.c_str());
  if (!d) return names;
  std::string base = path == "/" ? "" : path;
  while (struct dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    if (!hidden && n[0] == '.') continue;
    bool dir = IsDir(base + "/" + n);
    if (dir ? folders : files) names.push_back(n);
  }
  closedir(d);
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    int c = strcasecmp(a.c_str(), b.c_str());
    return c != 0 ? c < 0 : a < b;
  });
  return names;
}

}  // namespace winport

namespace {

HRESULT FromErrno(int e) {
  switch (e) {
    case ENOENT: case ENOTDIR: return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    case EACCES: case EPERM: return E_ACCESSDENIED;
    case EEXIST: return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    case ENOMEM: return E_OUTOFMEMORY;
  }
  return E_FAIL;
}

std::string Child(const std::string& dir, const std::string& name) { return dir == "/" ? "/" + name : dir + "/" + name; }

// What the shell says of the file or folder at path.
SFGAOF Attributes(const std::string& path, SFGAOF want) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return 0;
  SFGAOF a = SFGAO_FILESYSTEM | SFGAO_CANCOPY | SFGAO_CANLINK;
  std::string leaf = path.substr(path.rfind('/') + 1);
  if (!leaf.empty() && leaf[0] == '.') a |= SFGAO_HIDDEN;
  if (access(path.c_str(), W_OK) != 0) a |= SFGAO_READONLY;
  // Moving, renaming and deleting change the folder it is in.
  std::string parent = path.substr(0, path.rfind('/'));
  if (parent.empty()) parent = "/";
  if (path != "/" && access(parent.c_str(), W_OK) == 0) a |= SFGAO_CANMOVE | SFGAO_CANRENAME | SFGAO_CANDELETE;
  if (S_ISDIR(st.st_mode)) {
    a |= SFGAO_FOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGE | SFGAO_STORAGEANCESTOR | SFGAO_DROPTARGET;
    if ((want & SFGAO_HASSUBFOLDER) && !ListDir(path, true, false, true).empty()) a |= SFGAO_HASSUBFOLDER;
  } else {
    a |= SFGAO_STREAM;
    if (leaf.size() > 4 && strcasecmp(leaf.c_str() + leaf.size() - 4, ".lnk") == 0) a |= SFGAO_LINK;
  }
  return a;
}

// A wide string from the task allocator, in a STRRET.
HRESULT ToStrRet(const std::string& s, STRRET* out) {
  std::wstring w = Widen(s.c_str());
  out->uType = STRRET_WSTR;
  out->pOleStr = (LPWSTR)CoTaskMemAlloc((w.size() + 1) * sizeof(WCHAR));
  if (!out->pOleStr) return E_OUTOFMEMORY;
  memcpy(out->pOleStr, w.c_str(), (w.size() + 1) * sizeof(WCHAR));
  return S_OK;
}

class EnumIDList final : public IEnumIDList {
 public:
  explicit EnumIDList(std::vector<std::string> names) : names_(std::move(names)) {}
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumIDList)) {
      *ppv = static_cast<IEnumIDList*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
  }
  STDMETHODIMP Next(ULONG n, PITEMID_CHILD* out, ULONG* fetched) override {
    if (!out || (n > 1 && !fetched)) return E_POINTER;
    ULONG k = 0;
    while (k < n && pos_ < names_.size()) {
      out[k] = MakePidl({names_[pos_++]});
      if (!out[k]) break;
      k++;
    }
    if (fetched) *fetched = k;
    return k == n ? S_OK : S_FALSE;
  }
  STDMETHODIMP Skip(ULONG n) override {
    size_t left = names_.size() - pos_;
    pos_ += std::min<size_t>(n, left);
    return n <= left ? S_OK : S_FALSE;
  }
  STDMETHODIMP Reset() override {
    pos_ = 0;
    return S_OK;
  }
  STDMETHODIMP Clone(IEnumIDList** out) override {
    if (!out) return E_POINTER;
    EnumIDList* e = new EnumIDList(names_);
    e->pos_ = pos_;
    *out = e;
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  std::vector<std::string> names_;
  size_t pos_ = 0;
};

class ShellFolder final : public IShellFolder {
 public:
  explicit ShellFolder(std::string path) : path_(std::move(path)) {}

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellFolder)) {
      *ppv = static_cast<IShellFolder*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG r = --refs_;
    if (r == 0) delete this;
    return r;
  }

  // A path from this folder, or an absolute one under it; it must exist.
  STDMETHODIMP ParseDisplayName(HWND, IBindCtx*, LPWSTR name, ULONG* eaten, PIDLIST_RELATIVE* out,
                                ULONG* attrs) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!name) return E_INVALIDARG;
    std::string p = PosixPath(name);
    std::string full = JoinPath(SplitPath(p.empty() || p[0] != '/' ? Child(path_, p) : p));
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return FromErrno(errno);
    std::vector<std::string> mine = SplitPath(path_), all = SplitPath(full);
    if (all.size() < mine.size() || !std::equal(mine.begin(), mine.end(), all.begin())) return E_INVALIDARG;
    *out = MakePidl(std::vector<std::string>(all.begin() + (long)mine.size(), all.end()));
    if (!*out) return E_OUTOFMEMORY;
    if (eaten) *eaten = (ULONG)wcslen(name);
    if (attrs) *attrs &= Attributes(full, *attrs);
    return S_OK;
  }

  STDMETHODIMP EnumObjects(HWND, SHCONTF flags, IEnumIDList** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    DIR* d = opendir(path_.c_str());
    if (!d) return FromErrno(errno);
    closedir(d);
    *out = new EnumIDList(ListDir(path_, (flags & SHCONTF_FOLDERS) != 0, (flags & SHCONTF_NONFOLDERS) != 0,
                                  (flags & SHCONTF_INCLUDEHIDDEN) != 0));
    return S_OK;
  }

  // A folder below this one.
  STDMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx*, REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    std::string full = Full(pidl);
    struct stat st;
    if (stat(full.c_str(), &st) != 0) return FromErrno(errno);
    if (!IsEqualIID(riid, IID_IShellFolder) && !IsEqualIID(riid, IID_IUnknown)) return E_NOINTERFACE;
    if (!S_ISDIR(st.st_mode)) return E_INVALIDARG;
    ShellFolder* f = new ShellFolder(full);
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
  }

  // A file's contents as a stream: a copy, read when bound.
  STDMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx*, REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!IsEqualIID(riid, IID_IStream)) return E_NOINTERFACE;
    std::string full = Full(pidl);
    if (IsDir(full)) return E_INVALIDARG;
    FILE* f = fopen(full.c_str(), "rb");
    if (!f) return FromErrno(errno);
    std::vector<char> data;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) return E_FAIL;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, data.size() ? data.size() : 1);
    if (!g) return E_OUTOFMEMORY;
    if (!data.empty()) {
      memcpy(GlobalLock(g), data.data(), data.size());
      GlobalUnlock(g);
    }
    IStream* s = NULL;
    HRESULT hr = CreateStreamOnHGlobal(g, TRUE, &s);
    if (FAILED(hr)) {
      GlobalFree(g);
      return hr;
    }
    ULARGE_INTEGER size;
    size.QuadPart = data.size();
    s->SetSize(size);
    *ppv = s;
    return S_OK;
  }

  // Item by item: folders before files, then by name, ignoring case (or
  // with SHCIDS_CANONICALONLY exactly).
  STDMETHODIMP CompareIDs(LPARAM lp, PCUIDLIST_RELATIVE a, PCUIDLIST_RELATIVE b) override {
    std::vector<std::string> x = PidlItems(a), y = PidlItems(b);
    std::string dir = path_;
    for (size_t i = 0; i < x.size() || i < y.size(); i++) {
      if (i >= x.size()) return Result(-1);
      if (i >= y.size()) return Result(1);
      if (x[i] == y[i]) {
        dir = Child(dir, x[i]);
        continue;
      }
      if (lp & SHCIDS_CANONICALONLY) return Result(x[i] < y[i] ? -1 : 1);
      bool xd = IsDir(Child(dir, x[i])), yd = IsDir(Child(dir, y[i]));
      if (xd != yd) return Result(xd ? -1 : 1);
      int c = strcasecmp(x[i].c_str(), y[i].c_str());
      if (c == 0) c = x[i] < y[i] ? -1 : 1;
      return Result(c);
    }
    return Result(0);
  }

  // No views, context menus or other UI objects are offered.
  STDMETHODIMP CreateViewObject(HWND, REFIID, void** ppv) override {
    if (ppv) *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP GetUIObjectOf(HWND, UINT, PCUITEMID_CHILD_ARRAY, REFIID, UINT*, void** ppv) override {
    if (ppv) *ppv = NULL;
    return E_NOINTERFACE;
  }

  // What all the items have; with none, the folder itself.
  STDMETHODIMP GetAttributesOf(UINT n, PCUITEMID_CHILD_ARRAY items, SFGAOF* inout) override {
    if (!inout) return E_POINTER;
    SFGAOF want = *inout & ~SFGAO_VALIDATE;
    SFGAOF have = n == 0 ? Attributes(path_, want) : ~(SFGAOF)0;
    for (UINT i = 0; i < n; i++) {
      if (!items || !items[i]) return E_INVALIDARG;
      have &= Attributes(Full(items[i]), want);
    }
    *inout = want & have;
    return S_OK;
  }

  // For parsing, the full path (in this folder, the name); otherwise
  // the name, the root being "/".
  STDMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF flags, STRRET* out) override {
    if (!out) return E_POINTER;
    std::string full = Full(pidl);
    if ((flags & SHGDN_FORPARSING) && !(flags & SHGDN_INFOLDER)) return ToStrRet(full, out);
    std::vector<std::string> items = PidlItems(pidl);
    std::string name = !items.empty() ? items.back() : full.substr(full.rfind('/') + 1);
    if (name.empty()) name = "/";
    return ToStrRet(name, out);
  }

  // Renames the item within its folder.
  STDMETHODIMP SetNameOf(HWND, PCUITEMID_CHILD pidl, LPCWSTR name, SHGDNF, PITEMID_CHILD* out) override {
    if (out) *out = NULL;
    if (!pidl || !name || !*name || wcschr(name, L'/') || wcschr(name, L'\\')) return E_INVALIDARG;
    std::vector<std::string> items = PidlItems(pidl);
    if (items.empty()) return E_INVALIDARG;
    std::string from = Full(pidl);
    std::string to = from.substr(0, from.rfind('/') + 1) + Narrow(name);
    if (rename(from.c_str(), to.c_str()) != 0) return FromErrno(errno);
    if (out) {
      items.back() = Narrow(name);
      *out = MakePidl(items);
    }
    return S_OK;
  }

 private:
  std::string Full(LPCITEMIDLIST pidl) const {
    std::vector<std::string> v = SplitPath(path_), more = PidlItems(pidl);
    v.insert(v.end(), more.begin(), more.end());
    return JoinPath(v);
  }
  static HRESULT Result(int c) { return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(short)(c < 0 ? -1 : c > 0 ? 1 : 0)); }

  std::atomic<ULONG> refs_{1};
  std::string path_;
};

}  // namespace

extern "C" {

HRESULT WINAPI SHGetMalloc(IMalloc** out) { return CoGetMalloc(1, out); }

HRESULT WINAPI SHGetDesktopFolder(IShellFolder** out) {
  if (!out) return E_INVALIDARG;
  *out = new ShellFolder("/");
  return S_OK;
}

UINT WINAPI ILGetSize(PCUIDLIST_RELATIVE pidl) {
  if (!pidl) return 0;
  const BYTE* p = (const BYTE*)pidl;
  UINT size = 0;
  for (;;) {
    USHORT cb;
    memcpy(&cb, p + size, sizeof cb);
    size += sizeof cb;
    if (cb == 0) return size;
    size += cb - sizeof cb;
  }
}

// The item after the first, or NULL at the end.
PIDLIST_RELATIVE WINAPI ILGetNext(PCUIDLIST_RELATIVE pidl) {
  if (!pidl || pidl->mkid.cb == 0) return NULL;
  LPITEMIDLIST next = (LPITEMIDLIST)((const BYTE*)pidl + pidl->mkid.cb);
  return next->mkid.cb ? next : NULL;
}

PIDLIST_RELATIVE WINAPI ILClone(PCUIDLIST_RELATIVE pidl) {
  if (!pidl) return NULL;
  UINT n = ILGetSize(pidl);
  void* p = CoTaskMemAlloc(n);
  if (p) memcpy(p, pidl, n);
  return (PIDLIST_RELATIVE)p;
}

PITEMID_CHILD WINAPI ILCloneFirst(PCUIDLIST_RELATIVE pidl) {
  if (!pidl) return NULL;
  std::vector<std::string> v = PidlItems(pidl);
  if (v.size() > 1) v.resize(1);
  return MakePidl(v);
}

PIDLIST_ABSOLUTE WINAPI ILCombine(PCIDLIST_ABSOLUTE a, PCUIDLIST_RELATIVE b) {
  if (!a && !b) return NULL;
  std::vector<std::string> v = PidlItems(a), more = PidlItems(b);
  v.insert(v.end(), more.begin(), more.end());
  return MakePidl(v);
}

// The part of child below parent, or NULL when it is not below.
PIDLIST_RELATIVE WINAPI ILFindChild(PCIDLIST_ABSOLUTE parent, PCIDLIST_ABSOLUTE child) {
  if (!parent || !child) return NULL;
  std::vector<std::string> p = PidlItems(parent), c = PidlItems(child);
  if (c.size() < p.size() || !std::equal(p.begin(), p.end(), c.begin())) return NULL;
  const BYTE* q = (const BYTE*)child;
  for (size_t i = 0; i < p.size(); i++) q += ((LPCITEMIDLIST)q)->mkid.cb;
  return (PIDLIST_RELATIVE)q;
}

PITEMID_CHILD WINAPI ILFindLastID(PCUIDLIST_RELATIVE pidl) {
  if (!pidl) return NULL;
  const BYTE* p = (const BYTE*)pidl;
  const BYTE* last = p;
  while (((LPCITEMIDLIST)p)->mkid.cb) {
    last = p;
    p += ((LPCITEMIDLIST)p)->mkid.cb;
  }
  return (PITEMID_CHILD)last;
}

BOOL WINAPI ILRemoveLastID(PIDLIST_RELATIVE pidl) {
  if (!pidl || pidl->mkid.cb == 0) return FALSE;
  PITEMID_CHILD last = ILFindLastID(pidl);
  last->mkid.cb = 0;
  return TRUE;
}

BOOL WINAPI ILIsEqual(PCIDLIST_ABSOLUTE a, PCIDLIST_ABSOLUTE b) {
  if (!a || !b) return FALSE;
  return PidlItems(a) == PidlItems(b);
}

// Whether child is below parent (immediately below, with immediate).
BOOL WINAPI ILIsParent(PCIDLIST_ABSOLUTE parent, PCIDLIST_ABSOLUTE child, BOOL immediate) {
  if (!parent || !child) return FALSE;
  std::vector<std::string> p = PidlItems(parent), c = PidlItems(child);
  if (c.size() <= p.size() || !std::equal(p.begin(), p.end(), c.begin())) return FALSE;
  return !immediate || c.size() == p.size() + 1;
}

void WINAPI ILFree(PIDLIST_RELATIVE pidl) { CoTaskMemFree(pidl); }

PIDLIST_ABSOLUTE WINAPI ILCreateFromPathW(LPCWSTR path) {
  if (!path) return NULL;
  std::string full = AbsolutePath(path);
  struct stat st;
  if (stat(full.c_str(), &st) != 0) return NULL;
  return MakePidl(SplitPath(full));
}
PIDLIST_ABSOLUTE WINAPI ILCreateFromPathA(LPCSTR path) {
  if (!path) return NULL;
  std::wstring w = Widen(path);
  return ILCreateFromPathW(w.c_str());
}

BOOL WINAPI SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR out) {
  if (!out) return FALSE;
  out[0] = 0;
  if (!pidl) return FALSE;
  std::wstring w = Widen(JoinPath(PidlItems(pidl)).c_str());
  if (w.size() >= MAX_PATH) return FALSE;
  wcscpy(out, w.c_str());
  return TRUE;
}
BOOL WINAPI SHGetPathFromIDListA(PCIDLIST_ABSOLUTE pidl, LPSTR out) {
  if (!out) return FALSE;
  out[0] = 0;
  if (!pidl) return FALSE;
  std::string s = JoinPath(PidlItems(pidl));
  if (s.size() >= MAX_PATH) return FALSE;
  strcpy(out, s.c_str());
  return TRUE;
}

HRESULT WINAPI SHParseDisplayName(LPCWSTR name, IBindCtx*, PIDLIST_ABSOLUTE* out, SFGAOF in, SFGAOF* attrs) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (!name) return E_INVALIDARG;
  std::string full = AbsolutePath(name);
  struct stat st;
  if (stat(full.c_str(), &st) != 0) return FromErrno(errno);
  *out = MakePidl(SplitPath(full));
  if (!*out) return E_OUTOFMEMORY;
  if (attrs) *attrs = in & Attributes(full, in);
  return S_OK;
}

// A standard folder's ID list; CSIDL_DESKTOP is the namespace's root.
HRESULT WINAPI SHGetFolderLocation(HWND hwnd, int csidl, HANDLE token, DWORD, PIDLIST_ABSOLUTE* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if ((csidl & ~CSIDL_FLAG_MASK) == CSIDL_DESKTOP) {
    *out = MakePidl({});
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  WCHAR path[MAX_PATH];
  HRESULT hr = SHGetFolderPathW(hwnd, csidl, token, SHGFP_TYPE_CURRENT, path);
  if (FAILED(hr)) return hr;
  if (hr == S_FALSE) return E_FAIL;
  *out = ILCreateFromPathW(path);
  return *out ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}
HRESULT WINAPI SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE* out) {
  return SHGetFolderLocation(hwnd, csidl, NULL, 0, out);
}

HRESULT WINAPI SHBindToParent(PCIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv, PCUITEMID_CHILD* last) {
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  if (!pidl) return E_INVALIDARG;
  std::vector<std::string> v = PidlItems(pidl);
  if (!v.empty()) v.pop_back();
  ShellFolder* f = new ShellFolder(JoinPath(v));
  HRESULT hr = f->QueryInterface(riid, ppv);
  f->Release();
  if (SUCCEEDED(hr) && last) *last = ILFindLastID(pidl);
  return hr;
}

}  // extern "C"

// ---------------------------------------------------------------------
// SHGetFileInfo. The system image lists, one small (16) and one large
// (32), hold a document, a closed folder and an open folder, drawn here.

namespace {

enum { kIconDocument = 0, kIconFolder = 1, kIconFolderOpen = 2 };

// The color of the icon's pixel at (x, y) on a 16 by 16 grid, or -1
// where it is clear.
long IconPixel(int kind, int x, int y) {
  const long kEdge = 0x404040, kPaper = 0xFFFFFF, kFold = 0xC0C0C0;
  const long kFolderEdge = 0x806020, kFolder = 0xE8C050, kFolderLight = 0xF8E090;
  if (kind == kIconDocument) {
    if (x < 3 || x > 12 || y < 1 || y > 14) return -1;
    if (x >= 9 && y <= 4) {  // the folded corner
      int dx = x - 8, dy = y;
      if (dy < dx) return -1;
      if (dy == dx || x == 9 || y == 4) return kEdge;
      return kFold;
    }
    if (x == 3 || x == 12 || y == 1 || y == 14) return kEdge;
    if (y >= 7 && y <= 12 && (y % 2) == 1 && x >= 5 && x <= 10) return kFold;
    return kPaper;
  }
  // A folder: a tab, then the body.
  if (y >= 2 && y <= 3 && x >= 1 && x <= 6) return y == 2 || x == 1 || x == 6 ? kFolderEdge : kFolder;
  if (x < 1 || x > 14 || y < 4 || y > 13) return -1;
  if (kind == kIconFolderOpen && y >= 7) {
    // The open folder's front leans out.
    int left = 1 + (13 - y) / 3;
    if (x < left) return x == left - 1 ? kFolderEdge : -1;
    if (x == left || x == 14 || y == 7 || y == 13) return kFolderEdge;
    return kFolderLight;
  }
  if (x == 1 || x == 14 || y == 4 || y == 13) return kFolderEdge;
  return kFolder;
}

void AddIcon(HIMAGELIST il, int kind, int size) {
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = NULL;
  HBITMAP image = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  int stride = ((size + 15) / 16) * 2;
  std::vector<BYTE> maskBits(stride * size, 0);
  if (!image || !bits) {
    if (image) DeleteObject(image);
    return;
  }
  DWORD* px = (DWORD*)bits;
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      long c = IconPixel(kind, x * 16 / size, y * 16 / size);
      if (c < 0) {
        px[y * size + x] = 0;
        maskBits[y * stride + x / 8] |= (BYTE)(0x80 >> (x % 8));
      } else {
        px[y * size + x] = 0xFF000000u | (DWORD)c;
      }
    }
  HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
  ImageList_Add(il, image, mask);
  DeleteObject(image);
  if (mask) DeleteObject(mask);
}

HIMAGELIST SystemImageList(bool small) {
  static HIMAGELIST lists[2];
  HIMAGELIST& il = lists[small ? 0 : 1];
  if (!il) {
    int size = small ? 16 : 32;
    il = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 3, 1);
    if (il)
      for (int kind = kIconDocument; kind <= kIconFolderOpen; kind++) AddIcon(il, kind, size);
  }
  return il;
}

// What SHGetFileInfo answers, in UTF-8.
struct FileInfo {
  int icon = kIconDocument;
  DWORD attributes = 0;
  std::string displayName, typeName;
};

bool GetFileInfo(const std::string& path, DWORD fileAttributes, UINT flags, FileInfo* out) {
  bool folder;
  if (flags & SHGFI_USEFILEATTRIBUTES) {
    folder = (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  } else {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    folder = S_ISDIR(st.st_mode);
    if (flags & SHGFI_ATTRIBUTES) out->attributes = Attributes(path, SFGAO_HASSUBFOLDER);
  }
  out->icon = folder ? ((flags & SHGFI_OPENICON) ? kIconFolderOpen : kIconFolder) : kIconDocument;
  std::string leaf = path == "/" ? "/" : path.substr(path.rfind('/') + 1);
  out->displayName = leaf;
  if (folder) {
    out->typeName = "File folder";
  } else {
    size_t dot = leaf.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == leaf.size()) {
      out->typeName = "File";
    } else {
      std::string ext = leaf.substr(dot + 1);
      for (char& c : ext) c = (char)toupper((unsigned char)c);
      out->typeName = ext + " File";
    }
  }
  return true;
}

DWORD_PTR FileInfoResult(const FileInfo& fi, UINT flags, HICON* icon, int* iIcon, DWORD* attrs) {
  bool small = (flags & SHGFI_SMALLICON) != 0;
  if (flags & (SHGFI_SYSICONINDEX | SHGFI_ICON)) *iIcon = fi.icon;
  if (flags & SHGFI_ATTRIBUTES) *attrs = fi.attributes;
  if (flags & SHGFI_ICON) *icon = ImageList_GetIcon(SystemImageList(small), fi.icon, ILD_NORMAL);
  if (flags & SHGFI_SYSICONINDEX) return (DWORD_PTR)SystemImageList(small);
  return 1;
}

std::string InfoPath(const std::wstring& name, UINT flags, LPCVOID p) {
  if (flags & SHGFI_PIDL) return JoinPath(PidlItems((LPCITEMIDLIST)p));
  return AbsolutePath(name);
}

}  // namespace

extern "C" {

DWORD_PTR WINAPI SHGetFileInfoW(LPCWSTR path, DWORD fileAttributes, SHFILEINFOW* psfi, UINT cbFileInfo, UINT flags) {
  if (!path) return 0;
  FileInfo fi;
  std::string full = InfoPath((flags & SHGFI_PIDL) ? std::wstring() : std::wstring(path), flags, path);
  if (!GetFileInfo(full, fileAttributes, flags, &fi)) return 0;
  if (!psfi || cbFileInfo < sizeof(SHFILEINFOW)) {
    if (flags & SHGFI_SYSICONINDEX) return (DWORD_PTR)SystemImageList((flags & SHGFI_SMALLICON) != 0);
    return psfi ? 0 : 1;
  }
  if (flags & SHGFI_DISPLAYNAME) CopyOut(Widen(fi.displayName.c_str()), psfi->szDisplayName, MAX_PATH);
  if (flags & SHGFI_TYPENAME) CopyOut(Widen(fi.typeName.c_str()), psfi->szTypeName, 80);
  return FileInfoResult(fi, flags, &psfi->hIcon, &psfi->iIcon, &psfi->dwAttributes);
}

DWORD_PTR WINAPI SHGetFileInfoA(LPCSTR path, DWORD fileAttributes, SHFILEINFOA* psfi, UINT cbFileInfo, UINT flags) {
  if (!path) return 0;
  FileInfo fi;
  std::string full = InfoPath((flags & SHGFI_PIDL) ? std::wstring() : Widen(path), flags, path);
  if (!GetFileInfo(full, fileAttributes, flags, &fi)) return 0;
  if (!psfi || cbFileInfo < sizeof(SHFILEINFOA)) {
    if (flags & SHGFI_SYSICONINDEX) return (DWORD_PTR)SystemImageList((flags & SHGFI_SMALLICON) != 0);
    return psfi ? 0 : 1;
  }
  if (flags & SHGFI_DISPLAYNAME) CopyOut(fi.displayName, psfi->szDisplayName, MAX_PATH);
  if (flags & SHGFI_TYPENAME) CopyOut(fi.typeName, psfi->szTypeName, 80);
  return FileInfoResult(fi, flags, &psfi->hIcon, &psfi->iIcon, &psfi->dwAttributes);
}

}  // extern "C"
