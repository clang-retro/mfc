// printf.cpp - wsprintf and wvsprintf, with the Windows rules: %s is a
// string of the function's own kind and %S of the other, h and l pick
// the kind for %c and %s, l is 32 bits and I64 is 64, there is no
// floating point, and the output stops at 1024 characters.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <stdarg.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <vector>

using namespace winport;

namespace {

const int kMax = 1024;

template <class C>
struct Out {
  C* buf;
  int n = 0;
  void Put(C c) {
    if (n < kMax - 1) buf[n++] = c;
  }
};

template <class C>
void PutWide(Out<C>& o, const wchar_t* s, int len);
template <>
void PutWide(Out<wchar_t>& o, const wchar_t* s, int len) {
  for (int i = 0; i < len; i++) o.Put(s[i]);
}
template <>
void PutWide(Out<char>& o, const wchar_t* s, int len) {
  std::string n = Narrow(s, len);
  for (char c : n) o.Put(c);
}

template <class C>
void PutNarrow(Out<C>& o, const char* s, int len);
template <>
void PutNarrow(Out<char>& o, const char* s, int len) {
  for (int i = 0; i < len; i++) o.Put(s[i]);
}
template <>
void PutNarrow(Out<wchar_t>& o, const char* s, int len) {
  std::wstring w = Widen(s, len);
  for (wchar_t c : w) o.Put(c);
}

template <class C>
int Format(C* buf, const C* fmt, va_list ap) {
  Out<C> o;
  o.buf = buf;
  for (const C* p = fmt; *p; p++) {
    if (*p != '%') {
      o.Put(*p);
      continue;
    }
    const C* start = p;
    p++;
    bool left = false, zero = false, alt = false;
    for (;; p++) {
      if (*p == '-') left = true;
      else if (*p == '0') zero = true;
      else if (*p == '#') alt = true;
      else break;
    }
    int width = 0, prec = -1;
    while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
    if (*p == '.') {
      p++;
      prec = 0;
      while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
    }
    // Size: h (short, or narrow for c and s), l (32 bits, or wide),
    // I64.
    int size = 0;  // 0 default, 'h', 'l', 64
    if (*p == 'h') {
      size = 'h';
      p++;
    } else if (*p == 'l') {
      size = 'l';
      p++;
    } else if (p[0] == 'I' && p[1] == '6' && p[2] == '4') {
      size = 64;
      p += 3;
    }
    C conv = *p;
    std::basic_string<C> field;
    bool numeric = false;
    switch (conv) {
      case 'c': case 'C': {
        bool wide = size == 'l' || (size != 'h' && ((conv == 'c') == (sizeof(C) == sizeof(wchar_t))));
        int v = va_arg(ap, int);
        if (wide) {
          wchar_t w = (wchar_t)v;
          Out<C> t;
          C tmp[8];
          t.buf = tmp;
          PutWide(t, &w, 1);
          field.assign(tmp, (size_t)t.n);
        } else {
          char c = (char)v;
          Out<C> t;
          C tmp[8];
          t.buf = tmp;
          PutNarrow(t, &c, 1);
          field.assign(tmp, (size_t)t.n);
        }
        break;
      }
      case 's': case 'S': {
        bool wide = size == 'l' || (size != 'h' && ((conv == 's') == (sizeof(C) == sizeof(wchar_t))));
        const void* s = va_arg(ap, const void*);
        if (!s) s = wide ? (const void*)L"(null)" : (const void*)"(null)";
        std::vector<C> tmp(kMax);
        Out<C> t;
        t.buf = tmp.data();
        if (wide) {
          const wchar_t* w = (const wchar_t*)s;
          int len = 0;
          while (w[len] && (prec < 0 || len < prec)) len++;
          PutWide(t, w, len);
        } else {
          const char* c = (const char*)s;
          int len = 0;
          while (c[len] && (prec < 0 || len < prec)) len++;
          PutNarrow(t, c, len);
        }
        field.assign(tmp.data(), (size_t)t.n);
        break;
      }
      case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
        numeric = true;
        unsigned long long v;
        bool neg = false;
        if (conv == 'p') {
          v = (unsigned long long)(UINT_PTR)va_arg(ap, void*);
          prec = (int)sizeof(void*) * 2;
        } else if (size == 64) {
          v = va_arg(ap, unsigned long long);
          if ((conv == 'd' || conv == 'i') && (long long)v < 0) {
            neg = true;
            v = (unsigned long long)(-(long long)v);
          }
        } else {
          unsigned int u = va_arg(ap, unsigned int);
          if (size == 'h') u &= 0xffff;
          if (conv == 'd' || conv == 'i') {
            int s = size == 'h' ? (short)u : (int)u;
            if (s < 0) {
              neg = true;
              v = (unsigned long long)(-(long long)s);
            } else {
              v = (unsigned long long)s;
            }
          } else {
            v = u;
          }
        }
        unsigned base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : 10;
        const char* digits = (conv == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
        char tmp[32];
        int k = 0;
        do {
          tmp[k++] = digits[v % base];
          v /= base;
        } while (v);
        std::string num;
        while (k < prec) tmp[k++] = '0';
        for (int i = k - 1; i >= 0; i--) num += tmp[i];
        std::string prefix = neg ? "-" : "";
        if (alt && base == 16 && conv != 'p') prefix += conv == 'x' ? "0x" : "0X";
        if (zero && !left && prec < 0)
          while ((int)(prefix.size() + num.size()) < width) num.insert(0, 1, '0');
        num = prefix + num;
        for (char c : num) field += (C)c;
        break;
      }
      case '%':
        field = (C)'%';
        break;
      default:
        // Not a conversion: the text is copied.
        for (const C* q = start; q <= p && *q; q++) o.Put(*q);
        if (!*p) p--;
        continue;
    }
    (void)numeric;
    int pad = width - (int)field.size();
    if (!left)
      for (int i = 0; i < pad; i++) o.Put(' ');
    for (C c : field) o.Put(c);
    if (left)
      for (int i = 0; i < pad; i++) o.Put(' ');
  }
  buf[o.n] = 0;
  return o.n;
}

}  // namespace

extern "C" {

int WINAPI wvsprintfA(LPSTR buf, LPCSTR fmt, va_list ap) { return Format<char>(buf, fmt, ap); }
int WINAPI wvsprintfW(LPWSTR buf, LPCWSTR fmt, va_list ap) { return Format<wchar_t>(buf, fmt, ap); }

int WINAPIV wsprintfA(LPSTR buf, LPCSTR fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = Format<char>(buf, fmt, ap);
  va_end(ap);
  return n;
}

int WINAPIV wsprintfW(LPWSTR buf, LPCWSTR fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = Format<wchar_t>(buf, fmt, ap);
  va_end(ap);
  return n;
}

}  // extern "C"
