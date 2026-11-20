// shlwapi.cpp - the shell's light-weight utilities: the Path functions,
// which take both Windows paths ("C:\dir\file", "\\server\share") and
// POSIX ones ("/dir/file"), and the Str functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <string>
#include <vector>

#include "winport.h"

#include <shlwapi.h>

namespace {

template <typename C>
size_t Len(const C* s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}

inline int Up(char c) { return toupper((unsigned char)c); }
inline int Up(wchar_t c) { return (int)towupper(c); }

template <typename C>
bool IsSep(C c) {
  return c == '\\' || c == '/';
}

// The separator a path uses: a slash when it has slashes and no
// backslashes, as a POSIX path does, a backslash otherwise.
template <typename C>
C SepOf(const C* p) {
  bool slash = false;
  for (; *p; p++) {
    if (*p == '\\') return '\\';
    if (*p == '/') slash = true;
  }
  return slash ? '/' : '\\';
}

template <typename C>
bool HasDrive(const C* p) {
  return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

template <typename C>
bool IsUnc(const C* p) {
  return IsSep(p[0]) && IsSep(p[1]);
}

// The length of a path's root: "C:\" 3, "C:" 2, "\" 1, "\\server\share\"
// all of it, 0 for a relative path.
template <typename C>
size_t RootLen(const C* p) {
  if (HasDrive(p)) return IsSep(p[2]) ? 3 : 2;
  if (IsUnc(p)) {
    size_t i = 2;
    while (p[i] && !IsSep(p[i])) i++;  // the server
    if (!p[i]) return i;
    i++;
    while (p[i] && !IsSep(p[i])) i++;  // the share
    if (IsSep(p[i])) i++;
    return i;
  }
  if (IsSep(p[0])) return 1;
  return 0;
}

template <typename C>
bool EqualI(const C* a, const C* b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (IsSep(a[i]) && IsSep(b[i])) continue;
    if (Up(a[i]) != Up(b[i])) return false;
  }
  return true;
}

template <typename C>
void Copy(C* dst, const std::basic_string<C>& s) {
  memmove(dst, s.c_str(), (s.size() + 1) * sizeof(C));
}

template <typename C>
C* AddBackslash(C* p) {
  size_t n = Len(p);
  if (n > 0 && !IsSep(p[n - 1])) {
    if (n + 1 >= MAX_PATH) return NULL;
    p[n] = SepOf(p);
    p[++n] = 0;
  }
  return p + n;
}

template <typename C>
C* RemoveBackslash(C* p) {
  size_t n = Len(p);
  if (n == 0) return p;
  if (IsSep(p[n - 1]) && n > RootLen(p)) {
    p[--n] = 0;
    return p + n;
  }
  return p + n - 1;
}

template <typename C>
C* FindFileName(const C* p) {
  const C* r = p;
  for (const C* q = p; *q; q++)
    if ((IsSep(*q) || *q == ':') && q[1] && !IsSep(q[1])) r = q + 1;
  return (C*)r;
}

template <typename C>
C* FindExtension(const C* p) {
  const C* dot = NULL;
  const C* q = p;
  for (; *q; q++) {
    if (IsSep(*q) || *q == ' ')
      dot = NULL;
    else if (*q == '.')
      dot = q;
  }
  return (C*)(dot ? dot : q);
}

template <typename C>
BOOL AddExtension(C* p, const C* ext) {
  if (*FindExtension(p)) return FALSE;
  static const C kExe[] = {'.', 'e', 'x', 'e', 0};
  if (!ext || !*ext) ext = kExe;
  size_t n = Len(p), e = Len(ext);
  if (n + e >= MAX_PATH) return FALSE;
  memcpy(p + n, ext, (e + 1) * sizeof(C));
  return TRUE;
}

template <typename C>
void RemoveExtension(C* p) {
  *FindExtension(p) = 0;
}

template <typename C>
BOOL RenameExtension(C* p, const C* ext) {
  C* e = FindExtension(p);
  size_t n = Len(ext);
  if ((size_t)(e - p) + n >= MAX_PATH) return FALSE;
  memcpy(e, ext, (n + 1) * sizeof(C));
  return TRUE;
}

// Splits a path after its root into its components.
template <typename C>
std::vector<std::basic_string<C>> Components(const C* p) {
  std::vector<std::basic_string<C>> v;
  const C* q = p + RootLen(p);
  while (*q) {
    const C* s = q;
    while (*q && !IsSep(*q)) q++;
    if (q > s) v.push_back(std::basic_string<C>(s, q));
    while (IsSep(*q)) q++;
  }
  return v;
}

template <typename C>
BOOL Canonicalize(C* dst, const C* src) {
  if (!dst || !src) return FALSE;
  C sep = SepOf(src);
  size_t root = RootLen(src);
  std::basic_string<C> r(src, src + root);
  std::vector<std::basic_string<C>> out;
  for (auto& c : Components(src)) {
    static const C kDot[] = {'.', 0}, kDotDot[] = {'.', '.', 0};
    if (c == kDot) continue;
    if (c == kDotDot) {
      if (!out.empty())
        out.pop_back();
      continue;
    }
    out.push_back(c);
  }
  for (size_t i = 0; i < out.size(); i++) {
    if (i) r += sep;
    r += out[i];
  }
  // A path that ended with a separator keeps it.
  size_t n = Len(src);
  if (!out.empty() && n && IsSep(src[n - 1])) r += sep;
  if (r.empty() && n) r += sep;
  if (r.size() >= MAX_PATH) return FALSE;
  Copy(dst, r);
  return TRUE;
}

template <typename C>
C* Combine(C* dst, const C* dir, const C* file) {
  if (!dst || (!dir && !file)) return NULL;
  std::basic_string<C> s;
  if (file && *file && (RootLen(file) > 0 || !dir || !*dir)) {
    // An absolute file: just it, but "\x" takes the drive of dir.
    if (dir && IsSep(file[0]) && !IsUnc(file) && HasDrive(dir)) s.assign(dir, dir + 2);
    s += file;
  } else {
    s = dir ? dir : std::basic_string<C>();
    if (file && *file) {
      if (!s.empty() && !IsSep(s.back())) s += SepOf(s.c_str());
      s += file;
    }
  }
  if (s.size() >= MAX_PATH) {
    *dst = 0;
    return NULL;
  }
  if (!Canonicalize(dst, s.c_str())) return NULL;
  return dst;
}

template <typename C>
BOOL Append(C* p, const C* more) {
  if (!p || !more) return FALSE;
  // Leading separators of more are dropped, unless it is a UNC path.
  if (!IsUnc(more))
    while (IsSep(*more)) more++;
  std::basic_string<C> s(p);
  if (!*more) return TRUE;
  if (!s.empty() && !IsSep(s.back())) s += SepOf(p);
  s += more;
  if (s.size() >= MAX_PATH) return FALSE;
  return Canonicalize(p, s.c_str());
}

template <typename C>
C* BuildRoot(C* p, int drive) {
  if (drive < 0 || drive > 25) {
    p[0] = 0;
    return p;
  }
  p[0] = (C)('A' + drive);
  p[1] = ':';
  p[2] = '\\';
  p[3] = 0;
  return p;
}

template <typename C>
BOOL IsRoot(const C* p) {
  if (!p || !*p) return FALSE;
  size_t r = RootLen(p), n = Len(p);
  if (HasDrive(p)) return n == 3 && r == 3;
  if (IsUnc(p)) {
    if (r != n) return FALSE;
    // "\\server" or "\\server\share", with or without the last separator.
    return TRUE;
  }
  return n == 1 && IsSep(p[0]);
}

template <typename C>
int CommonPrefix(const C* a, const C* b, C* out) {
  if (out) *out = 0;
  if (!a || !b) return 0;
  size_t ra = RootLen(a), rb = RootLen(b);
  if (ra != rb || !EqualI(a, b, ra)) return 0;
  size_t last = ra, i = ra;
  for (;; i++) {
    bool ea = !a[i] || IsSep(a[i]), eb = !b[i] || IsSep(b[i]);
    if (ea && eb) {
      last = i;
      if (!a[i] || !b[i]) break;
      continue;
    }
    if (ea != eb || Up(a[i]) != Up(b[i])) break;
  }
  if (out) {
    memcpy(out, a, last * sizeof(C));
    out[last] = 0;
  }
  return (int)last;
}

template <typename C>
BOOL IsRelative(const C* p) {
  if (!p || !*p) return TRUE;
  return !IsSep(p[0]) && !HasDrive(p);
}

template <typename C>
BOOL IsUncServer(const C* p) {
  if (!IsUnc(p)) return FALSE;
  for (const C* q = p + 2; *q; q++)
    if (IsSep(*q)) return FALSE;
  return TRUE;
}

template <typename C>
BOOL IsUncServerShare(const C* p) {
  if (!IsUnc(p)) return FALSE;
  int seps = 0;
  const C* q = p + 2;
  if (!*q || IsSep(*q)) return FALSE;
  for (; *q; q++)
    if (IsSep(*q)) {
      if (++seps > 1 || !q[1]) return FALSE;
    }
  return seps == 1;
}

template <typename C>
bool Match(const C* s, const C* e, const C* p, const C* pe) {
  while (p < pe) {
    if (*p == '*') {
      p++;
      if (p == pe) return true;
      for (const C* t = s; t <= e; t++)
        if (Match(t, e, p, pe)) return true;
      return false;
    }
    if (s == e) return false;
    if (*p != '?' && Up(*p) != Up(*s)) return false;
    p++, s++;
  }
  return s == e;
}

template <typename C>
BOOL MatchSpec(const C* path, const C* spec) {
  if (!path || !spec) return FALSE;
  const C* name = FindFileName(path);
  const C* end = name + Len(name);
  // Several patterns, separated by semicolons; "*.*" matches everything.
  while (*spec) {
    while (*spec == ' ') spec++;
    const C* s = spec;
    while (*spec && *spec != ';') spec++;
    const C* e = spec;
    while (e > s && e[-1] == ' ') e--;
    static const C kAll[] = {'*', '.', '*'};
    if (e - s == 3 && memcmp(s, kAll, sizeof(kAll)) == 0) return TRUE;
    if (e > s && Match(name, end, s, e)) return TRUE;
    if (*spec) spec++;
  }
  return FALSE;
}

template <typename C>
BOOL QuoteSpaces(C* p) {
  size_t n = Len(p);
  bool space = false;
  for (size_t i = 0; i < n; i++)
    if (p[i] == ' ') space = true;
  if (!space) return FALSE;
  if (n + 2 >= MAX_PATH) return FALSE;
  memmove(p + 1, p, n * sizeof(C));
  p[0] = '"';
  p[n + 1] = '"';
  p[n + 2] = 0;
  return TRUE;
}

template <typename C>
void UnquoteSpaces(C* p) {
  size_t n = Len(p);
  if (n >= 2 && p[0] == '"' && p[n - 1] == '"') {
    memmove(p, p + 1, (n - 2) * sizeof(C));
    p[n - 2] = 0;
  }
}

template <typename C>
BOOL RelativePathTo(C* out, const C* from, DWORD attrFrom, const C* to, DWORD) {
  if (!out || !from || !to) return FALSE;
  *out = 0;
  std::basic_string<C> f(from);
  if (!(attrFrom & FILE_ATTRIBUTE_DIRECTORY)) {
    // A file: its directory.
    C* name = FindFileName(f.c_str());
    f.resize(name - f.c_str());
  }
  std::vector<C> prefix(Len(to) + Len(f.c_str()) + 2);
  int n = CommonPrefix(f.c_str(), to, prefix.data());
  if (n == 0) return FALSE;
  C sep = SepOf(to);
  std::basic_string<C> r;
  static const C kUp[] = {'.', '.', 0};
  for (auto& c : Components(f.c_str() + n)) {
    (void)c;
    r += kUp;
    r += sep;
  }
  const C* rest = to + n;
  while (IsSep(*rest)) rest++;
  if (r.empty()) {
    r += '.';
    r += sep;
  }
  r += rest;
  if (r.size() >= MAX_PATH) return FALSE;
  Copy(out, r);
  return TRUE;
}

template <typename C>
void RemoveArgs(C* p) {
  bool quoted = false;
  for (C* q = p; *q; q++) {
    if (*q == '"')
      quoted = !quoted;
    else if (*q == ' ' && !quoted) {
      *q = 0;
      return;
    }
  }
}

template <typename C>
void RemoveBlanks(C* p) {
  size_t n = Len(p), s = 0;
  while (s < n && p[s] == ' ') s++;
  while (n > s && p[n - 1] == ' ') n--;
  memmove(p, p + s, (n - s) * sizeof(C));
  p[n - s] = 0;
}

template <typename C>
BOOL RemoveFileSpec(C* p) {
  if (!p) return FALSE;
  size_t root = RootLen(p), n = Len(p);
  if (n <= root) return FALSE;
  if (IsSep(p[n - 1])) {
    // "dir\": the separator goes.
    while (n > root && IsSep(p[n - 1])) p[--n] = 0;
    return TRUE;
  }
  C* name = FindFileName(p);
  size_t at = name - p;
  if (at <= root) {
    if (name == p && root == 0) {
      // Just a name: nothing left.
      if (!*p) return FALSE;
      *p = 0;
      return TRUE;
    }
    p[root] = 0;
    return TRUE;
  }
  // Drops the separator before the name, unless it ends the root.
  at--;
  while (at > root && IsSep(p[at - 1])) at--;
  p[at] = 0;
  return TRUE;
}

template <typename C>
C* SkipRoot(const C* p) {
  size_t r = RootLen(p);
  return r ? (C*)p + r : NULL;
}

template <typename C>
void StripPath(C* p) {
  C* name = FindFileName(p);
  if (name != p) memmove(p, name, (Len(name) + 1) * sizeof(C));
}

template <typename C>
BOOL StripToRoot(C* p) {
  if (!p) return FALSE;
  size_t r = RootLen(p);
  if (!r) return FALSE;
  p[r] = 0;
  return TRUE;
}

template <typename C>
BOOL MakePretty(C* p) {
  for (C* q = p; *q; q++)
    if (*q >= 'a' && *q <= 'z') return FALSE;
  for (C* q = p; *q; q++)
    if (*q >= 'A' && *q <= 'Z') *q = (C)(*q - 'A' + 'a');
  return TRUE;
}

template <typename C>
BOOL CompactPathEx(C* dst, const C* src, UINT cch, DWORD) {
  if (!dst || !src || cch == 0) return FALSE;
  size_t n = Len(src);
  if (n < cch) {
    memcpy(dst, src, (n + 1) * sizeof(C));
    return TRUE;
  }
  const C* name = FindFileName(src);
  size_t nameLen = Len(name);
  std::basic_string<C> r;
  static const C kDots[] = {'.', '.', '.', 0};
  if (cch < 2) {
    *dst = 0;
    return TRUE;
  }
  if (nameLen + 4 < cch && name != src) {
    // The start of the directory, "...\" and the name.
    size_t keep = cch - 1 - 4 - nameLen;
    r.assign(src, src + keep);
    r += kDots;
    r += SepOf(src);
    r += name;
  } else if (cch > 4) {
    r.assign(name, name + (cch - 4));
    r += kDots;
  } else {
    r.assign(kDots, kDots + (cch - 1));
  }
  Copy(dst, r);
  return TRUE;
}

template <typename C>
int Width(HDC dc, const C* s, size_t n);
template <>
int Width(HDC dc, const char* s, size_t n) {
  SIZE sz = {0, 0};
  GetTextExtentPoint32A(dc, s, (int)n, &sz);
  return sz.cx;
}
template <>
int Width(HDC dc, const wchar_t* s, size_t n) {
  SIZE sz = {0, 0};
  GetTextExtentPoint32W(dc, s, (int)n, &sz);
  return sz.cx;
}

template <typename C>
BOOL CompactPath(HDC dc, C* p, UINT dx) {
  if (!p) return FALSE;
  size_t n = Len(p);
  if (!dc || Width(dc, p, n) <= (int)dx) return TRUE;
  std::basic_string<C> src(p);
  for (UINT cch = (UINT)n; cch > 1; cch--) {
    C buf[MAX_PATH];
    CompactPathEx(buf, src.c_str(), cch, 0);
    if (Width(dc, buf, Len(buf)) <= (int)dx || cch == 2) {
      memcpy(p, buf, (Len(buf) + 1) * sizeof(C));
      return TRUE;
    }
  }
  return TRUE;
}

template <typename C>
C* FindNextComponent(const C* p) {
  if (!p || !*p) return NULL;
  while (*p && !IsSep(*p)) p++;
  while (IsSep(*p)) p++;
  return (C*)p;
}

template <typename C>
BOOL IsUrl(const C* p) {
  if (!p) return FALSE;
  const C* q = p;
  while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') ||
         *q == '+' || *q == '-' || *q == '.')
    q++;
  return q - p > 1 && *q == ':';
}

// Strings.

template <typename C>
C* StrChrT(const C* s, C c, bool ci) {
  for (; s && *s; s++)
    if (ci ? Up(*s) == Up(c) : *s == c) return (C*)s;
  return NULL;
}

template <typename C>
C* StrRChrT(const C* s, const C* end, C c) {
  if (!s) return NULL;
  if (!end) end = s + Len(s);
  for (const C* q = end; q > s;)
    if (*--q == c) return (C*)q;
  return NULL;
}

template <typename C>
int CmpN(const C* a, const C* b, int n, bool ci) {
  for (int i = 0; n < 0 || i < n; i++) {
    int x = ci ? Up(a[i]) : (int)a[i], y = ci ? Up(b[i]) : (int)b[i];
    if (x != y) return x < y ? -1 : 1;
    if (!x) return 0;
  }
  return 0;
}

template <typename C>
C* StrStrT(const C* s, const C* f, bool ci) {
  if (!s || !f) return NULL;
  size_t n = Len(f);
  for (; *s; s++)
    if (CmpN(s, f, (int)n, ci) == 0) return (C*)s;
  return n == 0 ? (C*)s : NULL;
}

template <typename C>
BOOL StrToIntExT(const C* s, DWORD flags, int* out) {
  if (!s || !out) return FALSE;
  while (*s == ' ' || *s == '\t') s++;
  bool neg = false;
  if (*s == '-' || *s == '+') neg = *s++ == '-';
  long long v = 0;
  bool any = false;
  if ((flags & STIF_SUPPORT_HEX) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    for (s += 2;; s++) {
      int d = *s >= '0' && *s <= '9' ? *s - '0' : Up(*s) >= 'A' && Up(*s) <= 'F' ? Up(*s) - 'A' + 10 : -1;
      if (d < 0) break;
      v = v * 16 + d, any = true;
    }
  } else {
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0'), any = true;
  }
  *out = (int)(neg ? -v : v);
  return any;
}

template <typename C>
BOOL StrTrimT(C* s, const C* chars) {
  if (!s || !chars) return FALSE;
  size_t n = Len(s), b = 0;
  while (b < n && StrChrT(chars, s[b], false)) b++;
  while (n > b && StrChrT(chars, s[n - 1], false)) n--;
  if (b == 0 && n == Len(s)) return FALSE;
  memmove(s, s + b, (n - b) * sizeof(C));
  s[n - b] = 0;
  return TRUE;
}

template <typename C>
C* StrCpyNT(C* d, const C* s, int n) {
  if (!d || n <= 0) return d;
  int i = 0;
  for (; i < n - 1 && s && s[i]; i++) d[i] = s[i];
  d[i] = 0;
  return d;
}

template <typename C>
C* StrCatBuffT(C* d, const C* s, int n) {
  if (!d || n <= 0) return d;
  int l = (int)Len(d);
  if (l < n) StrCpyNT(d + l, s, n - l);
  return d;
}

std::string ByteSize(LONGLONG v) {
  static const char* const kUnits[] = {"KB", "MB", "GB", "TB", "PB", "EB"};
  char buf[64];
  if (v < 1024) {
    snprintf(buf, sizeof(buf), "%lld bytes", (long long)v);
    return buf;
  }
  double d = (double)v;
  int u = -1;
  while (d >= 1024 && u < 5) d /= 1024, u++;
  // Three significant digits, as Explorer shows them.
  int dec = d < 10 ? 2 : d < 100 ? 1 : 0;
  double scale = dec == 2 ? 100 : dec == 1 ? 10 : 1;
  d = (double)(long long)(d * scale) / scale;  // truncated, not rounded
  snprintf(buf, sizeof(buf), "%.*f %s", dec, d, kUnits[u]);
  return buf;
}

}  // namespace

extern "C" {

LPSTR WINAPI PathAddBackslashA(LPSTR p) { return AddBackslash(p); }
LPWSTR WINAPI PathAddBackslashW(LPWSTR p) { return AddBackslash(p); }
BOOL WINAPI PathAddExtensionA(LPSTR p, LPCSTR e) { return AddExtension(p, e); }
BOOL WINAPI PathAddExtensionW(LPWSTR p, LPCWSTR e) { return AddExtension(p, e); }
BOOL WINAPI PathAppendA(LPSTR p, LPCSTR m) { return Append(p, m); }
BOOL WINAPI PathAppendW(LPWSTR p, LPCWSTR m) { return Append(p, m); }
LPSTR WINAPI PathBuildRootA(LPSTR p, int d) { return BuildRoot(p, d); }
LPWSTR WINAPI PathBuildRootW(LPWSTR p, int d) { return BuildRoot(p, d); }
BOOL WINAPI PathCanonicalizeA(LPSTR d, LPCSTR s) { return Canonicalize(d, s); }
BOOL WINAPI PathCanonicalizeW(LPWSTR d, LPCWSTR s) { return Canonicalize(d, s); }
LPSTR WINAPI PathCombineA(LPSTR d, LPCSTR a, LPCSTR b) { return Combine(d, a, b); }
LPWSTR WINAPI PathCombineW(LPWSTR d, LPCWSTR a, LPCWSTR b) { return Combine(d, a, b); }
int WINAPI PathCommonPrefixA(LPCSTR a, LPCSTR b, LPSTR o) { return CommonPrefix(a, b, o); }
int WINAPI PathCommonPrefixW(LPCWSTR a, LPCWSTR b, LPWSTR o) { return CommonPrefix(a, b, o); }
BOOL WINAPI PathCompactPathA(HDC dc, LPSTR p, UINT dx) { return CompactPath(dc, p, dx); }
BOOL WINAPI PathCompactPathW(HDC dc, LPWSTR p, UINT dx) { return CompactPath(dc, p, dx); }
BOOL WINAPI PathCompactPathExA(LPSTR d, LPCSTR s, UINT n, DWORD f) { return CompactPathEx(d, s, n, f); }
BOOL WINAPI PathCompactPathExW(LPWSTR d, LPCWSTR s, UINT n, DWORD f) { return CompactPathEx(d, s, n, f); }
BOOL WINAPI PathFileExistsA(LPCSTR p) { return p && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
BOOL WINAPI PathFileExistsW(LPCWSTR p) { return p && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
LPSTR WINAPI PathFindExtensionA(LPCSTR p) { return p ? FindExtension(p) : NULL; }
LPWSTR WINAPI PathFindExtensionW(LPCWSTR p) { return p ? FindExtension(p) : NULL; }
LPSTR WINAPI PathFindFileNameA(LPCSTR p) { return p ? FindFileName(p) : NULL; }
LPWSTR WINAPI PathFindFileNameW(LPCWSTR p) { return p ? FindFileName(p) : NULL; }
LPSTR WINAPI PathFindNextComponentA(LPCSTR p) { return FindNextComponent(p); }
LPWSTR WINAPI PathFindNextComponentW(LPCWSTR p) { return FindNextComponent(p); }
int WINAPI PathGetDriveNumberA(LPCSTR p) { return p && HasDrive(p) ? Up(p[0]) - 'A' : -1; }
int WINAPI PathGetDriveNumberW(LPCWSTR p) { return p && HasDrive(p) ? Up(p[0]) - 'A' : -1; }
BOOL WINAPI PathIsDirectoryA(LPCSTR p) {
  DWORD a = p ? GetFileAttributesA(p) : INVALID_FILE_ATTRIBUTES;
  return a != INVALID_FILE_ATTRIBUTES ? (BOOL)(a & FILE_ATTRIBUTE_DIRECTORY) : FALSE;
}
BOOL WINAPI PathIsDirectoryW(LPCWSTR p) {
  DWORD a = p ? GetFileAttributesW(p) : INVALID_FILE_ATTRIBUTES;
  return a != INVALID_FILE_ATTRIBUTES ? (BOOL)(a & FILE_ATTRIBUTE_DIRECTORY) : FALSE;
}
BOOL WINAPI PathIsFileSpecA(LPCSTR p) { return p && !strpbrk(p, ":\\/"); }
BOOL WINAPI PathIsFileSpecW(LPCWSTR p) { return p && !wcspbrk(p, L":\\/"); }
BOOL WINAPI PathIsPrefixA(LPCSTR pre, LPCSTR p) {
  return pre && p && CommonPrefix(pre, p, (char*)NULL) == (int)strlen(pre);
}
BOOL WINAPI PathIsPrefixW(LPCWSTR pre, LPCWSTR p) {
  return pre && p && CommonPrefix(pre, p, (wchar_t*)NULL) == (int)wcslen(pre);
}
BOOL WINAPI PathIsRelativeA(LPCSTR p) { return IsRelative(p); }
BOOL WINAPI PathIsRelativeW(LPCWSTR p) { return IsRelative(p); }
BOOL WINAPI PathIsRootA(LPCSTR p) { return IsRoot(p); }
BOOL WINAPI PathIsRootW(LPCWSTR p) { return IsRoot(p); }
BOOL WINAPI PathIsSameRootA(LPCSTR a, LPCSTR b) {
  size_t r = a && b ? RootLen(a) : 0;
  return r && r == RootLen(b) && EqualI(a, b, r);
}
BOOL WINAPI PathIsSameRootW(LPCWSTR a, LPCWSTR b) {
  size_t r = a && b ? RootLen(a) : 0;
  return r && r == RootLen(b) && EqualI(a, b, r);
}
BOOL WINAPI PathIsUNCA(LPCSTR p) { return p && IsUnc(p); }
BOOL WINAPI PathIsUNCW(LPCWSTR p) { return p && IsUnc(p); }
BOOL WINAPI PathIsUNCServerA(LPCSTR p) { return p && IsUncServer(p); }
BOOL WINAPI PathIsUNCServerW(LPCWSTR p) { return p && IsUncServer(p); }
BOOL WINAPI PathIsUNCServerShareA(LPCSTR p) { return p && IsUncServerShare(p); }
BOOL WINAPI PathIsUNCServerShareW(LPCWSTR p) { return p && IsUncServerShare(p); }
BOOL WINAPI PathIsURLA(LPCSTR p) { return IsUrl(p); }
BOOL WINAPI PathIsURLW(LPCWSTR p) { return IsUrl(p); }
BOOL WINAPI PathMakePrettyA(LPSTR p) { return p && MakePretty(p); }
BOOL WINAPI PathMakePrettyW(LPWSTR p) { return p && MakePretty(p); }
BOOL WINAPI PathMatchSpecA(LPCSTR p, LPCSTR s) { return MatchSpec(p, s); }
BOOL WINAPI PathMatchSpecW(LPCWSTR p, LPCWSTR s) { return MatchSpec(p, s); }
BOOL WINAPI PathQuoteSpacesA(LPSTR p) { return p && QuoteSpaces(p); }
BOOL WINAPI PathQuoteSpacesW(LPWSTR p) { return p && QuoteSpaces(p); }
BOOL WINAPI PathRelativePathToA(LPSTR o, LPCSTR f, DWORD af, LPCSTR t, DWORD at) {
  return RelativePathTo(o, f, af, t, at);
}
BOOL WINAPI PathRelativePathToW(LPWSTR o, LPCWSTR f, DWORD af, LPCWSTR t, DWORD at) {
  return RelativePathTo(o, f, af, t, at);
}
void WINAPI PathRemoveArgsA(LPSTR p) {
  if (p) RemoveArgs(p);
}
void WINAPI PathRemoveArgsW(LPWSTR p) {
  if (p) RemoveArgs(p);
}
LPSTR WINAPI PathRemoveBackslashA(LPSTR p) { return p ? RemoveBackslash(p) : NULL; }
LPWSTR WINAPI PathRemoveBackslashW(LPWSTR p) { return p ? RemoveBackslash(p) : NULL; }
void WINAPI PathRemoveBlanksA(LPSTR p) {
  if (p) RemoveBlanks(p);
}
void WINAPI PathRemoveBlanksW(LPWSTR p) {
  if (p) RemoveBlanks(p);
}
void WINAPI PathRemoveExtensionA(LPSTR p) {
  if (p) RemoveExtension(p);
}
void WINAPI PathRemoveExtensionW(LPWSTR p) {
  if (p) RemoveExtension(p);
}
BOOL WINAPI PathRemoveFileSpecA(LPSTR p) { return RemoveFileSpec(p); }
BOOL WINAPI PathRemoveFileSpecW(LPWSTR p) { return RemoveFileSpec(p); }
BOOL WINAPI PathRenameExtensionA(LPSTR p, LPCSTR e) { return p && e && RenameExtension(p, e); }
BOOL WINAPI PathRenameExtensionW(LPWSTR p, LPCWSTR e) { return p && e && RenameExtension(p, e); }
LPSTR WINAPI PathSkipRootA(LPCSTR p) { return p ? SkipRoot(p) : NULL; }
LPWSTR WINAPI PathSkipRootW(LPCWSTR p) { return p ? SkipRoot(p) : NULL; }
void WINAPI PathStripPathA(LPSTR p) {
  if (p) StripPath(p);
}
void WINAPI PathStripPathW(LPWSTR p) {
  if (p) StripPath(p);
}
BOOL WINAPI PathStripToRootA(LPSTR p) { return StripToRoot(p); }
BOOL WINAPI PathStripToRootW(LPWSTR p) { return StripToRoot(p); }
void WINAPI PathUnquoteSpacesA(LPSTR p) {
  if (p) UnquoteSpaces(p);
}
void WINAPI PathUnquoteSpacesW(LPWSTR p) {
  if (p) UnquoteSpaces(p);
}

LPSTR WINAPI StrChrA(LPCSTR s, WORD c) { return StrChrT(s, (char)c, false); }
LPWSTR WINAPI StrChrW(LPCWSTR s, WCHAR c) { return StrChrT(s, (wchar_t)c, false); }
LPSTR WINAPI StrChrIA(LPCSTR s, WORD c) { return StrChrT(s, (char)c, true); }
LPWSTR WINAPI StrChrIW(LPCWSTR s, WCHAR c) { return StrChrT(s, (wchar_t)c, true); }
LPSTR WINAPI StrRChrA(LPCSTR s, LPCSTR e, WORD c) { return StrRChrT(s, e, (char)c); }
LPWSTR WINAPI StrRChrW(LPCWSTR s, LPCWSTR e, WCHAR c) { return StrRChrT(s, e, (wchar_t)c); }
LPSTR WINAPI StrStrA(LPCSTR s, LPCSTR f) { return StrStrT(s, f, false); }
LPWSTR WINAPI StrStrW(LPCWSTR s, LPCWSTR f) { return StrStrT(s, f, false); }
LPSTR WINAPI StrStrIA(LPCSTR s, LPCSTR f) { return StrStrT(s, f, true); }
LPWSTR WINAPI StrStrIW(LPCWSTR s, LPCWSTR f) { return StrStrT(s, f, true); }
int WINAPI StrCmpNA(LPCSTR a, LPCSTR b, int n) { return CmpN(a, b, n, false); }
int WINAPI StrCmpNW(LPCWSTR a, LPCWSTR b, int n) { return CmpN(a, b, n, false); }
int WINAPI StrCmpNIA(LPCSTR a, LPCSTR b, int n) { return CmpN(a, b, n, true); }
int WINAPI StrCmpNIW(LPCWSTR a, LPCWSTR b, int n) { return CmpN(a, b, n, true); }
int WINAPI StrCmpIA(LPCSTR a, LPCSTR b) { return CmpN(a, b, -1, true); }
int WINAPI StrCmpIW(LPCWSTR a, LPCWSTR b) { return CmpN(a, b, -1, true); }
int WINAPI StrCmpW(LPCWSTR a, LPCWSTR b) { return CmpN(a, b, -1, false); }
LPSTR WINAPI StrDupA(LPCSTR s) {
  size_t n = s ? strlen(s) + 1 : 1;
  LPSTR d = (LPSTR)LocalAlloc(LMEM_FIXED, n);
  if (d) memcpy(d, s ? s : "", n);
  return d;
}
LPWSTR WINAPI StrDupW(LPCWSTR s) {
  size_t n = (s ? wcslen(s) + 1 : 1) * sizeof(wchar_t);
  LPWSTR d = (LPWSTR)LocalAlloc(LMEM_FIXED, n);
  if (d) memcpy(d, s ? s : L"", n);
  return d;
}
int WINAPI StrToIntA(LPCSTR s) {
  int v = 0;
  StrToIntExT(s, 0, &v);
  return v;
}
int WINAPI StrToIntW(LPCWSTR s) {
  int v = 0;
  StrToIntExT(s, 0, &v);
  return v;
}
BOOL WINAPI StrToIntExA(LPCSTR s, DWORD f, int* o) { return StrToIntExT(s, f, o); }
BOOL WINAPI StrToIntExW(LPCWSTR s, DWORD f, int* o) { return StrToIntExT(s, f, o); }
BOOL WINAPI StrTrimA(LPSTR s, LPCSTR c) { return StrTrimT(s, c); }
BOOL WINAPI StrTrimW(LPWSTR s, LPCWSTR c) { return StrTrimT(s, c); }
LPSTR WINAPI StrCpyNA(LPSTR d, LPCSTR s, int n) { return StrCpyNT(d, s, n); }
LPWSTR WINAPI StrCpyNW(LPWSTR d, LPCWSTR s, int n) { return StrCpyNT(d, s, n); }
LPSTR WINAPI StrCatBuffA(LPSTR d, LPCSTR s, int n) { return StrCatBuffT(d, s, n); }
LPWSTR WINAPI StrCatBuffW(LPWSTR d, LPCWSTR s, int n) { return StrCatBuffT(d, s, n); }
LPSTR WINAPI StrFormatByteSize64A(LONGLONG v, LPSTR buf, UINT n) {
  return StrCpyNT(buf, ByteSize(v).c_str(), (int)n);
}
LPWSTR WINAPI StrFormatByteSize64W(LONGLONG v, LPWSTR buf, UINT n) {
  return StrCpyNT(buf, winport::Widen(ByteSize(v).c_str()).c_str(), (int)n);
}

}  // extern "C"
