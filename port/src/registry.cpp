// registry.cpp - the registry, kept in a text file: the keys and values
// of all the hives, read at the first use and written back when a key is
// flushed or closed after a change, and at exit.
//
// The file is $WINPORT_REGISTRY, else registry.reg in the configuration
// directory ($XDG_CONFIG_HOME/winport or ~/.config/winport). Each key is a
// line "[HIVE\path]" followed by its values, "name"=type:hex bytes, with
// strings in UTF-8. The programs of one user share it, as on Windows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wctype.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

using namespace winport;

namespace {

// Names compare without regard to case.
struct NoCase {
  bool operator()(const std::wstring& a, const std::wstring& b) const {
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++) {
      wint_t x = towlower(a[i]), y = towlower(b[i]);
      if (x != y) return x < y;
    }
    return a.size() < b.size();
  }
};

struct Value {
  std::wstring name;  // as given, for enumeration
  DWORD type = REG_NONE;
  std::vector<BYTE> data;  // strings as UTF-16LE, as Windows keeps them
};

struct Key {
  std::wstring name;
  Key* parent = NULL;
  bool deleted = false;
  std::map<std::wstring, std::unique_ptr<Key>, NoCase> children;
  std::vector<Value> values;
  FILETIME written = {0, 0};
  Value* Find(const std::wstring& n) {
    for (Value& v : values)
      if (!NoCase()(v.name, n) && !NoCase()(n, v.name)) return &v;
    return NULL;
  }
};

const wchar_t* const kHives[] = {L"HKEY_CLASSES_ROOT", L"HKEY_CURRENT_USER", L"HKEY_LOCAL_MACHINE", L"HKEY_USERS",
                                 L"HKEY_PERFORMANCE_DATA", L"HKEY_CURRENT_CONFIG", L"HKEY_DYN_DATA"};
const int kNumHives = 7;

std::recursive_mutex g_lock;
Key g_hives[kNumHives];
bool g_loaded;
bool g_dirty;

// An open key. Predefined hive handles are not these.
struct Handle {
  DWORD magic = 0x6b676572;  // "regk"
  Key* key;
};

std::string RegistryFile() {
  const char* f = getenv("WINPORT_REGISTRY");
  if (f != NULL && *f) return f;
  std::string dir;
  const char* xdg = getenv("XDG_CONFIG_HOME");
  if (xdg != NULL && *xdg) {
    dir = xdg;
  } else {
    const char* home = getenv("HOME");
    dir = std::string(home ? home : "/tmp") + "/.config";
  }
  return dir + "/winport/registry.reg";
}

void MakeParents(const std::string& path) {
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
  }
}

std::vector<std::wstring> SplitPath(const std::wstring& path) {
  std::vector<std::wstring> parts;
  std::wstring cur;
  for (wchar_t c : path) {
    if (c == '\\') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

Key* Walk(Key* k, const std::wstring& path, bool create, bool* created) {
  if (created) *created = false;
  for (const std::wstring& part : SplitPath(path)) {
    auto it = k->children.find(part);
    if (it == k->children.end()) {
      if (!create) return NULL;
      std::unique_ptr<Key> n(new Key);
      n->name = part;
      n->parent = k;
      GetSystemTimeAsFileTime(&n->written);
      Key* raw = n.get();
      k->children[part] = std::move(n);
      k = raw;
      if (created) *created = true;
      g_dirty = true;
    } else {
      k = it->second.get();
    }
  }
  return k;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void Load() {
  if (g_loaded) return;
  g_loaded = true;
  for (int i = 0; i < kNumHives; i++) g_hives[i].name = kHives[i];
  FILE* f = fopen(RegistryFile().c_str(), "r");
  if (f == NULL) return;
  Key* cur = NULL;
  std::string line;
  char buf[4096];
  while (fgets(buf, sizeof(buf), f) != NULL) {
    line += buf;
    if (line.empty() || line.back() != '\n') {
      if (!feof(f)) continue;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.size() > 2 && line[0] == '[' && line.back() == ']') {
      std::wstring path = Widen(line.substr(1, line.size() - 2).c_str());
      size_t slash = path.find('\\');
      std::wstring hive = path.substr(0, slash);
      cur = NULL;
      for (int i = 0; i < kNumHives; i++)
        if (!NoCase()(hive, kHives[i]) && !NoCase()(kHives[i], hive)) {
          cur = slash == std::wstring::npos ? &g_hives[i] : Walk(&g_hives[i], path.substr(slash + 1), true, NULL);
          break;
        }
    } else if (cur != NULL && line.size() > 2 && line[0] == '"') {
      // "name"=type:hex; quotes and backslashes in names are escaped.
      std::string name;
      size_t i = 1;
      while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size()) i++;
        name += line[i++];
      }
      if (i + 2 >= line.size() || line[i + 1] != '=') {
        line.clear();
        continue;
      }
      char* end;
      Value v;
      v.name = Widen(name.c_str());
      v.type = (DWORD)strtoul(line.c_str() + i + 2, &end, 16);
      if (*end == ':') {
        for (const char* p = end + 1; p[0] && p[1]; p += 2) {
          int hi = HexValue(p[0]), lo = HexValue(p[1]);
          if (hi < 0 || lo < 0) break;
          v.data.push_back((BYTE)(hi * 16 + lo));
        }
      }
      cur->values.push_back(v);
    }
    line.clear();
  }
  fclose(f);
  g_dirty = false;
}

void SaveKey(FILE* f, Key* k, const std::wstring& path) {
  if (!k->values.empty() || k->children.empty()) {
    fprintf(f, "[%s]\n", Narrow(path.c_str()).c_str());
    for (const Value& v : k->values) {
      std::string n = Narrow(v.name.c_str());
      fputc('"', f);
      for (char c : n) {
        if (c == '"' || c == '\\') fputc('\\', f);
        fputc(c, f);
      }
      fprintf(f, "\"=%lx:", (unsigned long)v.type);
      for (BYTE b : v.data) fprintf(f, "%02x", b);
      fputc('\n', f);
    }
  }
  for (auto& c : k->children) SaveKey(f, c.second.get(), path + L"\\" + c.second->name);
}

void Save() {
  if (!g_loaded || !g_dirty) return;
  std::string file = RegistryFile();
  MakeParents(file);
  std::string tmp = file + ".tmp" + std::to_string(getpid());
  FILE* f = fopen(tmp.c_str(), "w");
  if (f == NULL) return;
  fprintf(f, "; winport registry\n");
  for (int i = 0; i < kNumHives; i++) {
    if (g_hives[i].values.empty() && g_hives[i].children.empty()) continue;
    SaveKey(f, &g_hives[i], kHives[i]);
  }
  bool ok = fclose(f) == 0;
  if (ok && rename(tmp.c_str(), file.c_str()) == 0) g_dirty = false;
  else unlink(tmp.c_str());
}

struct Saver {
  ~Saver() {
    std::lock_guard<std::recursive_mutex> g(g_lock);
    Save();
  }
} g_saver;

Key* KeyOf(HKEY h) {
  ULONG_PTR v = (ULONG_PTR)h;
  if (v >= (ULONG_PTR)HKEY_CLASSES_ROOT && v < (ULONG_PTR)HKEY_CLASSES_ROOT + kNumHives) {
    Load();
    return &g_hives[v - (ULONG_PTR)HKEY_CLASSES_ROOT];
  }
  if (h == NULL) return NULL;
  Handle* hd = (Handle*)h;
  if (hd->magic != 0x6b676572 || hd->key->deleted) return NULL;
  return hd->key;
}

HKEY NewHandle(Key* k) {
  Handle* h = new Handle;
  h->key = k;
  return (HKEY)h;
}

bool IsString(DWORD type) { return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ; }

// Strings are kept as UTF-16LE; the W functions see wchar_t, the A
// functions UTF-8.
std::vector<BYTE> StoreW(DWORD type, const BYTE* data, DWORD cb) {
  std::vector<BYTE> out;
  if (!IsString(type)) return std::vector<BYTE>(data, data + cb);
  size_t n = cb / sizeof(wchar_t);
  const wchar_t* s = (const wchar_t*)data;
  for (size_t i = 0; i < n; i++) {
    UINT c = (UINT)s[i];
    if (c >= 0x10000) {
      c -= 0x10000;
      UINT hi = 0xd800 + (c >> 10), lo = 0xdc00 + (c & 0x3ff);
      out.push_back((BYTE)hi), out.push_back((BYTE)(hi >> 8));
      out.push_back((BYTE)lo), out.push_back((BYTE)(lo >> 8));
    } else {
      out.push_back((BYTE)c), out.push_back((BYTE)(c >> 8));
    }
  }
  return out;
}

std::wstring LoadW(const std::vector<BYTE>& d) {
  std::wstring s;
  for (size_t i = 0; i + 1 < d.size(); i += 2) {
    UINT c = d[i] | (d[i + 1] << 8);
    if (c >= 0xd800 && c < 0xdc00 && i + 3 < d.size()) {
      UINT lo = d[i + 2] | (d[i + 3] << 8);
      if (lo >= 0xdc00 && lo < 0xe000) {
        c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
        i += 2;
      }
    }
    s += (wchar_t)c;
  }
  return s;
}

LONG CopyData(const std::vector<BYTE>& src, LPBYTE data, LPDWORD cb) {
  DWORD n = (DWORD)src.size();
  if (data != NULL) {
    if (cb == NULL) return ERROR_INVALID_PARAMETER;
    if (*cb < n) {
      *cb = n;
      return ERROR_MORE_DATA;
    }
    memcpy(data, src.data(), n);
  }
  if (cb != NULL) *cb = n;
  return ERROR_SUCCESS;
}

// A value's data as the W or A functions return it.
std::vector<BYTE> Outward(const Value& v, bool wide) {
  if (!IsString(v.type)) return v.data;
  std::wstring s = LoadW(v.data);
  if (wide) {
    return std::vector<BYTE>((const BYTE*)s.data(), (const BYTE*)(s.data() + s.size()));
  }
  std::string a = Narrow(s.c_str(), (int)s.size());
  return std::vector<BYTE>(a.begin(), a.end());
}

std::vector<BYTE> InwardA(DWORD type, const BYTE* data, DWORD cb) {
  if (!IsString(type)) return std::vector<BYTE>(data, data + cb);
  std::wstring w = Widen((const char*)data, (int)cb);
  return StoreW(type, (const BYTE*)w.data(), (DWORD)(w.size() * sizeof(wchar_t)));
}

LONG Open(HKEY h, const std::wstring& sub, PHKEY out) {
  if (out == NULL) return ERROR_INVALID_PARAMETER;
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  Key* found = Walk(k, sub, false, NULL);
  if (found == NULL) {
    *out = NULL;
    return ERROR_FILE_NOT_FOUND;
  }
  *out = NewHandle(found);
  return ERROR_SUCCESS;
}

LONG Create(HKEY h, const std::wstring& sub, PHKEY out, LPDWORD disposition) {
  if (out == NULL) return ERROR_INVALID_PARAMETER;
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  bool created;
  Key* found = Walk(k, sub, true, &created);
  *out = NewHandle(found);
  if (disposition) *disposition = created ? REG_CREATED_NEW_KEY : REG_OPENED_EXISTING_KEY;
  return ERROR_SUCCESS;
}

LONG Query(HKEY h, const std::wstring& name, LPDWORD type, LPBYTE data, LPDWORD cb, bool wide) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  Value* v = k->Find(name);
  if (v == NULL) return ERROR_FILE_NOT_FOUND;
  if (type) *type = v->type;
  return CopyData(Outward(*v, wide), data, cb);
}

LONG Set(HKEY h, const std::wstring& name, DWORD type, std::vector<BYTE> data) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  Value* v = k->Find(name);
  if (v == NULL) {
    k->values.push_back(Value());
    v = &k->values.back();
    v->name = name;
  }
  v->type = type;
  v->data = std::move(data);
  GetSystemTimeAsFileTime(&k->written);
  g_dirty = true;
  return ERROR_SUCCESS;
}

LONG DeleteKey(HKEY h, const std::wstring& sub) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  Key* target = Walk(k, sub, false, NULL);
  if (target == NULL || target == k) return ERROR_FILE_NOT_FOUND;
  // Only a key without subkeys, as on Windows NT.
  if (!target->children.empty()) return ERROR_ACCESS_DENIED;
  target->deleted = true;
  // Open handles still point at it; it stays allocated, detached.
  Key* parent = target->parent;
  auto it = parent->children.find(target->name);
  it->second.release();
  parent->children.erase(it);
  g_dirty = true;
  return ERROR_SUCCESS;
}

LONG DeleteValue(HKEY h, const std::wstring& name) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  for (size_t i = 0; i < k->values.size(); i++) {
    if (!NoCase()(k->values[i].name, name) && !NoCase()(name, k->values[i].name)) {
      k->values.erase(k->values.begin() + i);
      g_dirty = true;
      return ERROR_SUCCESS;
    }
  }
  return ERROR_FILE_NOT_FOUND;
}

std::wstring W(LPCSTR s) { return s ? Widen(s) : std::wstring(); }
std::wstring W(LPCWSTR s) { return s ? std::wstring(s) : std::wstring(); }

}  // namespace

extern "C" {

LONG WINAPI RegOpenKeyExW(HKEY h, LPCWSTR sub, DWORD, REGSAM, PHKEY out) { return Open(h, W(sub), out); }
LONG WINAPI RegOpenKeyExA(HKEY h, LPCSTR sub, DWORD, REGSAM, PHKEY out) { return Open(h, W(sub), out); }
LONG WINAPI RegOpenKeyW(HKEY h, LPCWSTR sub, PHKEY out) { return Open(h, W(sub), out); }
LONG WINAPI RegOpenKeyA(HKEY h, LPCSTR sub, PHKEY out) { return Open(h, W(sub), out); }

LONG WINAPI RegCreateKeyExW(HKEY h, LPCWSTR sub, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY out,
                            LPDWORD disposition) {
  return Create(h, W(sub), out, disposition);
}
LONG WINAPI RegCreateKeyExA(HKEY h, LPCSTR sub, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY out,
                            LPDWORD disposition) {
  return Create(h, W(sub), out, disposition);
}
LONG WINAPI RegCreateKeyW(HKEY h, LPCWSTR sub, PHKEY out) { return Create(h, W(sub), out, NULL); }
LONG WINAPI RegCreateKeyA(HKEY h, LPCSTR sub, PHKEY out) { return Create(h, W(sub), out, NULL); }

LONG WINAPI RegFlushKey(HKEY h) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  if (KeyOf(h) == NULL) return ERROR_INVALID_HANDLE;
  Save();
  return ERROR_SUCCESS;
}

LONG WINAPI RegCloseKey(HKEY h) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  ULONG_PTR v = (ULONG_PTR)h;
  if (v >= (ULONG_PTR)HKEY_CLASSES_ROOT && v < (ULONG_PTR)HKEY_CLASSES_ROOT + kNumHives) return ERROR_SUCCESS;
  Handle* hd = (Handle*)h;
  if (hd == NULL || hd->magic != 0x6b676572) return ERROR_INVALID_HANDLE;
  hd->magic = 0;
  delete hd;
  Save();
  return ERROR_SUCCESS;
}

LONG WINAPI RegDeleteKeyW(HKEY h, LPCWSTR sub) { return DeleteKey(h, W(sub)); }
LONG WINAPI RegDeleteKeyA(HKEY h, LPCSTR sub) { return DeleteKey(h, W(sub)); }
LONG WINAPI RegDeleteValueW(HKEY h, LPCWSTR name) { return DeleteValue(h, W(name)); }
LONG WINAPI RegDeleteValueA(HKEY h, LPCSTR name) { return DeleteValue(h, W(name)); }

LONG WINAPI RegQueryValueExW(HKEY h, LPCWSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD cb) {
  return Query(h, W(name), type, data, cb, true);
}
LONG WINAPI RegQueryValueExA(HKEY h, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD cb) {
  return Query(h, W(name), type, data, cb, false);
}

LONG WINAPI RegSetValueExW(HKEY h, LPCWSTR name, DWORD, DWORD type, const BYTE* data, DWORD cb) {
  if (data == NULL && cb != 0) return ERROR_INVALID_PARAMETER;
  return Set(h, W(name), type, StoreW(type, data, cb));
}
LONG WINAPI RegSetValueExA(HKEY h, LPCSTR name, DWORD, DWORD type, const BYTE* data, DWORD cb) {
  if (data == NULL && cb != 0) return ERROR_INVALID_PARAMETER;
  return Set(h, W(name), type, InwardA(type, data, cb));
}

// The default value of a subkey, as a string.
LONG WINAPI RegQueryValueW(HKEY h, LPCWSTR sub, LPWSTR data, PLONG cb) {
  HKEY k;
  LONG r = Open(h, W(sub), &k);
  if (r != ERROR_SUCCESS) return r;
  DWORD n = cb ? (DWORD)*cb : 0;
  DWORD type;
  r = Query(k, L"", &type, (LPBYTE)data, cb ? &n : NULL, true);
  if (r == ERROR_FILE_NOT_FOUND) {
    // An empty string, when the key has no default value.
    n = sizeof(WCHAR);
    if (data != NULL && cb != NULL && *cb >= (LONG)n) data[0] = 0;
    r = ERROR_SUCCESS;
  }
  if (cb) *cb = (LONG)n;
  RegCloseKey(k);
  return r;
}
LONG WINAPI RegQueryValueA(HKEY h, LPCSTR sub, LPSTR data, PLONG cb) {
  HKEY k;
  LONG r = Open(h, W(sub), &k);
  if (r != ERROR_SUCCESS) return r;
  DWORD n = cb ? (DWORD)*cb : 0;
  DWORD type;
  r = Query(k, L"", &type, (LPBYTE)data, cb ? &n : NULL, false);
  if (r == ERROR_FILE_NOT_FOUND) {
    n = 1;
    if (data != NULL && cb != NULL && *cb >= 1) data[0] = 0;
    r = ERROR_SUCCESS;
  }
  if (cb) *cb = (LONG)n;
  RegCloseKey(k);
  return r;
}

LONG WINAPI RegSetValueW(HKEY h, LPCWSTR sub, DWORD type, LPCWSTR data, DWORD) {
  if (type != REG_SZ || data == NULL) return ERROR_INVALID_PARAMETER;
  HKEY k;
  LONG r = Create(h, W(sub), &k, NULL);
  if (r != ERROR_SUCCESS) return r;
  r = RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE*)data, (DWORD)((wcslen(data) + 1) * sizeof(WCHAR)));
  RegCloseKey(k);
  return r;
}
LONG WINAPI RegSetValueA(HKEY h, LPCSTR sub, DWORD type, LPCSTR data, DWORD) {
  if (type != REG_SZ || data == NULL) return ERROR_INVALID_PARAMETER;
  HKEY k;
  LONG r = Create(h, W(sub), &k, NULL);
  if (r != ERROR_SUCCESS) return r;
  r = RegSetValueExA(k, NULL, 0, REG_SZ, (const BYTE*)data, (DWORD)strlen(data) + 1);
  RegCloseKey(k);
  return r;
}

LONG WINAPI RegEnumKeyExW(HKEY h, DWORD index, LPWSTR name, LPDWORD cch, LPDWORD, LPWSTR cls, LPDWORD cchClass,
                          PFILETIME written) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  if (index >= k->children.size()) return ERROR_NO_MORE_ITEMS;
  auto it = k->children.begin();
  std::advance(it, index);
  const std::wstring& n = it->second->name;
  if (name == NULL || cch == NULL) return ERROR_INVALID_PARAMETER;
  if (*cch <= n.size()) return ERROR_MORE_DATA;
  wmemcpy(name, n.c_str(), n.size() + 1);
  *cch = (DWORD)n.size();
  if (cls != NULL && cchClass != NULL && *cchClass > 0) cls[0] = 0;
  if (cchClass != NULL) *cchClass = 0;
  if (written) *written = it->second->written;
  return ERROR_SUCCESS;
}
LONG WINAPI RegEnumKeyExA(HKEY h, DWORD index, LPSTR name, LPDWORD cch, LPDWORD, LPSTR cls, LPDWORD cchClass,
                          PFILETIME written) {
  WCHAR buf[256];
  DWORD n = 256;
  LONG r = RegEnumKeyExW(h, index, buf, &n, NULL, NULL, NULL, written);
  if (r != ERROR_SUCCESS) return r;
  if (name == NULL || cch == NULL) return ERROR_INVALID_PARAMETER;
  std::string a = Narrow(buf);
  if (*cch <= a.size()) return ERROR_MORE_DATA;
  memcpy(name, a.c_str(), a.size() + 1);
  *cch = (DWORD)a.size();
  if (cls != NULL && cchClass != NULL && *cchClass > 0) cls[0] = 0;
  if (cchClass != NULL) *cchClass = 0;
  return ERROR_SUCCESS;
}
LONG WINAPI RegEnumKeyW(HKEY h, DWORD index, LPWSTR name, DWORD cch) {
  return RegEnumKeyExW(h, index, name, &cch, NULL, NULL, NULL, NULL);
}
LONG WINAPI RegEnumKeyA(HKEY h, DWORD index, LPSTR name, DWORD cch) {
  return RegEnumKeyExA(h, index, name, &cch, NULL, NULL, NULL, NULL);
}

LONG WINAPI RegEnumValueW(HKEY h, DWORD index, LPWSTR name, LPDWORD cch, LPDWORD, LPDWORD type, LPBYTE data,
                          LPDWORD cb) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  if (index >= k->values.size()) return ERROR_NO_MORE_ITEMS;
  const Value& v = k->values[index];
  if (name == NULL || cch == NULL) return ERROR_INVALID_PARAMETER;
  if (*cch <= v.name.size()) return ERROR_MORE_DATA;
  wmemcpy(name, v.name.c_str(), v.name.size() + 1);
  *cch = (DWORD)v.name.size();
  if (type) *type = v.type;
  return CopyData(Outward(v, true), data, cb);
}
LONG WINAPI RegEnumValueA(HKEY h, DWORD index, LPSTR name, LPDWORD cch, LPDWORD, LPDWORD type, LPBYTE data,
                          LPDWORD cb) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  if (index >= k->values.size()) return ERROR_NO_MORE_ITEMS;
  const Value& v = k->values[index];
  if (name == NULL || cch == NULL) return ERROR_INVALID_PARAMETER;
  std::string a = Narrow(v.name.c_str());
  if (*cch <= a.size()) return ERROR_MORE_DATA;
  memcpy(name, a.c_str(), a.size() + 1);
  *cch = (DWORD)a.size();
  if (type) *type = v.type;
  return CopyData(Outward(v, false), data, cb);
}

LONG WINAPI RegQueryInfoKeyW(HKEY h, LPWSTR cls, LPDWORD cchClass, LPDWORD, LPDWORD subKeys, LPDWORD maxSubKeyLen,
                             LPDWORD maxClassLen, LPDWORD values, LPDWORD maxValueNameLen, LPDWORD maxValueLen,
                             LPDWORD securityDescriptor, PFILETIME written) {
  std::lock_guard<std::recursive_mutex> g(g_lock);
  Key* k = KeyOf(h);
  if (k == NULL) return ERROR_INVALID_HANDLE;
  if (cls != NULL && cchClass != NULL && *cchClass > 0) cls[0] = 0;
  if (cchClass) *cchClass = 0;
  if (subKeys) *subKeys = (DWORD)k->children.size();
  if (maxSubKeyLen) {
    DWORD m = 0;
    for (auto& c : k->children)
      if (c.second->name.size() > m) m = (DWORD)c.second->name.size();
    *maxSubKeyLen = m;
  }
  if (maxClassLen) *maxClassLen = 0;
  if (values) *values = (DWORD)k->values.size();
  DWORD mn = 0, mv = 0;
  for (const Value& v : k->values) {
    if (v.name.size() > mn) mn = (DWORD)v.name.size();
    DWORD n = (DWORD)Outward(v, true).size();
    if (n > mv) mv = n;
  }
  if (maxValueNameLen) *maxValueNameLen = mn;
  if (maxValueLen) *maxValueLen = mv;
  if (securityDescriptor) *securityDescriptor = 0;
  if (written) *written = k->written;
  return ERROR_SUCCESS;
}
LONG WINAPI RegQueryInfoKeyA(HKEY h, LPSTR cls, LPDWORD cchClass, LPDWORD reserved, LPDWORD subKeys,
                             LPDWORD maxSubKeyLen, LPDWORD maxClassLen, LPDWORD values, LPDWORD maxValueNameLen,
                             LPDWORD maxValueLen, LPDWORD securityDescriptor, PFILETIME written) {
  if (cls != NULL && cchClass != NULL && *cchClass > 0) cls[0] = 0;
  return RegQueryInfoKeyW(h, NULL, cchClass, reserved, subKeys, maxSubKeyLen, maxClassLen, values, maxValueNameLen,
                          maxValueLen, securityDescriptor, written);
}

}  // extern "C"
