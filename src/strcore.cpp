// CString storage and printf-style formatting with Microsoft's rules.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

// ---------------------------------------------------------------------
// String data

namespace {
// The shared empty string: a locked header and room for one zero char of
// either width.
union NilString {
  struct {
    CStringData hdr;
    WCHAR zero[2];
  } s;
  double align;
};
NilString g_nil = {{{-1, 0, 0}, {0, 0}}};
}  // namespace

CStringData* AFXAPI AfxStrNil() { return &g_nil.s.hdr; }

CStringData* AFXAPI AfxStrAlloc(int nChars, int nCharSize) {
  if (nChars < 0) AfxThrowInvalidArgException();
  // Round up so that small changes do not reallocate every time.
  int nAlloc = nChars < 64 ? (nChars + 16) & ~15 : (nChars + 64) & ~63;
  CStringData* p =
      (CStringData*)malloc(sizeof(CStringData) + (size_t)(nAlloc + 1) * nCharSize);
  if (!p) AfxThrowMemoryException();
  p->nRefs = 1;
  p->nDataLength = 0;
  p->nAllocLength = nAlloc;
  memset(p->data(), 0, nCharSize);
  return p;
}

void AFXAPI AfxStrRelease(CStringData* p) {
  if (p == &g_nil.s.hdr) return;
  if (p->nRefs < 0) {  // locked buffer: only one owner
    free(p);
    return;
  }
  if (InterlockedDecrement(&p->nRefs) <= 0) free(p);
}

// ---------------------------------------------------------------------
// Formatting

namespace {

template <typename C> struct Out {
  C* buf;
  int max;  // room including the zero
  int len;
  void Put(C c) {
    if (len + 1 < max) buf[len] = c;
    len++;
  }
  void Fill(C c, int n) {
    while (n-- > 0) Put(c);
  }
  void Finish() {
    if (max > 0) buf[len < max ? len : max - 1] = 0;
  }
};

struct Spec {
  bool left, plus, space, alt, zero;
  int width;
  int prec;  // -1 none
};

template <typename C>
void PutPadded(Out<C>& o, const Spec& s, const C* str, int n) {
  int pad = s.width > n ? s.width - n : 0;
  if (!s.left) o.Fill(s.zero ? C('0') : C(' '), pad);
  for (int i = 0; i < n; i++) o.Put(str[i]);
  if (s.left) o.Fill(C(' '), pad);
}

// Narrow string into an output of either width.
template <typename C>
void PutNarrow(Out<C>& o, const Spec& s, const char* str);
template <>
void PutNarrow<char>(Out<char>& o, const Spec& s, const char* str) {
  if (!str) str = "(null)";
  int n = (int)strlen(str);
  if (s.prec >= 0 && n > s.prec) n = s.prec;
  PutPadded<char>(o, s, str, n);
}
template <>
void PutNarrow<WCHAR>(Out<WCHAR>& o, const Spec& s, const char* str) {
  if (!str) str = "(null)";
  int n = (int)strlen(str);
  if (s.prec >= 0 && n > s.prec) n = s.prec;
  int w = MultiByteToWideChar(CP_ACP, 0, str, n, NULL, 0);
  WCHAR stackbuf[256];
  WCHAR* p = w <= 256 ? stackbuf : (WCHAR*)malloc(w * sizeof(WCHAR));
  MultiByteToWideChar(CP_ACP, 0, str, n, p, w);
  PutPadded<WCHAR>(o, s, p, w);
  if (p != stackbuf) free(p);
}

template <typename C>
void PutWide(Out<C>& o, const Spec& s, const WCHAR* str);
template <>
void PutWide<WCHAR>(Out<WCHAR>& o, const Spec& s, const WCHAR* str) {
  static const WCHAR null[] = {'(', 'n', 'u', 'l', 'l', ')', 0};
  if (!str) str = null;
  int n = lstrlenW(str);
  if (s.prec >= 0 && n > s.prec) n = s.prec;
  PutPadded<WCHAR>(o, s, str, n);
}
template <>
void PutWide<char>(Out<char>& o, const Spec& s, const WCHAR* str) {
  if (!str) {
    PutNarrow<char>(o, s, NULL);
    return;
  }
  int n = lstrlenW(str);
  int m = WideCharToMultiByte(CP_ACP, 0, str, n, NULL, 0, NULL, NULL);
  char stackbuf[512];
  char* p = m <= 512 ? stackbuf : (char*)malloc(m);
  WideCharToMultiByte(CP_ACP, 0, str, n, p, m, NULL, NULL);
  int k = m;
  if (s.prec >= 0 && k > s.prec) k = s.prec;
  PutPadded<char>(o, s, p, k);
  if (p != stackbuf) free(p);
}

template <typename C>
void PutAscii(Out<C>& o, const Spec& s, const char* str, int n) {
  C tmp[128];
  if (n > 128) n = 128;
  for (int i = 0; i < n; i++) tmp[i] = (C)(unsigned char)str[i];
  PutPadded<C>(o, s, tmp, n);
}

template <typename C>
void PutInt(Out<C>& o, Spec s, unsigned long long v, bool neg, int base, bool upper,
            bool isSigned) {
  char digits[80];
  int nd = 0;
  const char* set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  while (v) {
    digits[nd++] = set[v % base];
    v /= base;
  }
  int prec = s.prec < 0 ? 1 : s.prec;
  char body[100];
  int n = 0;
  char prefix[3];
  int np = 0;
  if (isSigned) {
    if (neg) prefix[np++] = '-';
    else if (s.plus) prefix[np++] = '+';
    else if (s.space) prefix[np++] = ' ';
  }
  if (s.alt && nd) {
    if (base == 16) { prefix[np++] = '0'; prefix[np++] = upper ? 'X' : 'x'; }
    else if (base == 8 && prec <= nd) prefix[np++] = '0';
  }
  int zeros = prec > nd ? prec - nd : 0;
  if (s.zero && !s.left && s.prec < 0) {
    int total = np + zeros + nd;
    if (s.width > total) zeros += s.width - total;
  }
  for (int i = 0; i < np; i++) body[n++] = prefix[i];
  while (zeros-- > 0 && n < 96) body[n++] = '0';
  while (nd) body[n++] = digits[--nd];
  s.zero = false;
  PutAscii<C>(o, s, body, n);
}

template <typename C>
int VFormat(C* buf, int max, const C* f, va_list args) {
  Out<C> o = {buf, buf ? max : 0, 0};
  const bool wide = sizeof(C) == sizeof(WCHAR);
  while (*f) {
    if (*f != '%') {
      o.Put(*f++);
      continue;
    }
    f++;
    if (*f == '%') {
      o.Put(*f++);
      continue;
    }
    Spec s = {false, false, false, false, false, 0, -1};
    for (;; f++) {
      if (*f == '-') s.left = true;
      else if (*f == '+') s.plus = true;
      else if (*f == ' ') s.space = true;
      else if (*f == '#') s.alt = true;
      else if (*f == '0') s.zero = true;
      else break;
    }
    if (*f == '*') {
      s.width = va_arg(args, int);
      if (s.width < 0) { s.left = true; s.width = -s.width; }
      f++;
    } else {
      while (*f >= '0' && *f <= '9') s.width = s.width * 10 + (*f++ - '0');
    }
    if (*f == '.') {
      f++;
      s.prec = 0;
      if (*f == '*') {
        s.prec = va_arg(args, int);
        if (s.prec < 0) s.prec = -1;
        f++;
      } else {
        while (*f >= '0' && *f <= '9') s.prec = s.prec * 10 + (*f++ - '0');
      }
    }
    // Size: 0 int, 1 short, 2 long, 3 64-bit, 4 pointer-sized; for
    // strings and chars: 'h' narrow, 'l' wide.
    int size = 0;
    int strw = 0;  // 0 default, 1 narrow, 2 wide
    bool ldbl = false;
    for (;;) {
      if (*f == 'h') {
        f++;
        size = 1;
        strw = 1;
        if (*f == 'h') f++;
      } else if (*f == 'l') {
        f++;
        size = 2;
        strw = 2;
        if (*f == 'l') { f++; size = 3; }
      } else if (*f == 'w') {
        f++;
        strw = 2;
      } else if (*f == 'L') {
        f++;
        ldbl = true;
        size = 3;
      } else if (*f == 'I') {
        if (f[1] == '6' && f[2] == '4') { f += 3; size = 3; }
        else if (f[1] == '3' && f[2] == '2') { f += 3; size = 0; }
        else { f++; size = 4; }
      } else if (*f == 'q' || *f == 'j') {
        f++;
        size = 3;
      } else if (*f == 'z' || *f == 't') {
        f++;
        size = 4;
      } else {
        break;
      }
    }
    C t = *f;
    if (!t) break;
    f++;
    switch (t) {
    case 'd':
    case 'i': {
      long long v;
      if (size == 3) v = va_arg(args, long long);
      else if (size == 4) v = (long long)va_arg(args, INT_PTR);
      else if (size == 2) v = va_arg(args, long);
      else v = va_arg(args, int);
      if (size == 1) v = (short)v;
      bool neg = v < 0;
      PutInt(o, s, neg ? 0ULL - (unsigned long long)v : (unsigned long long)v, neg, 10,
             false, true);
      break;
    }
    case 'u':
    case 'o':
    case 'x':
    case 'X': {
      unsigned long long v;
      if (size == 3) v = va_arg(args, unsigned long long);
      else if (size == 4) v = (unsigned long long)va_arg(args, UINT_PTR);
      else if (size == 2) v = va_arg(args, unsigned long);
      else v = va_arg(args, unsigned int);
      if (size == 1) v = (unsigned short)v;
      PutInt(o, s, v, false, t == 'u' ? 10 : t == 'o' ? 8 : 16, t == 'X', false);
      break;
    }
    case 'p': {
      UINT_PTR v = (UINT_PTR)va_arg(args, void*);
      Spec ps = s;
      ps.prec = (int)sizeof(void*) * 2;
      ps.alt = false;
      PutInt(o, ps, v, false, 16, true, false);
      break;
    }
    case 'c':
    case 'C': {
      int v = va_arg(args, int);
      bool w = strw ? strw == 2 : ((t == 'c') == wide);
      if (w) {
        WCHAR ws[2] = {(WCHAR)v, 0};
        Spec cs = s;
        cs.prec = -1;
        PutWide<C>(o, cs, ws);
      } else {
        char ns[2] = {(char)v, 0};
        Spec cs = s;
        cs.prec = -1;
        if (!v) { PutAscii<C>(o, cs, ns, 1); break; }
        PutNarrow<C>(o, cs, ns);
      }
      break;
    }
    case 's':
    case 'S': {
      bool w = strw ? strw == 2 : ((t == 's') == wide);
      if (w) PutWide<C>(o, s, va_arg(args, const WCHAR*));
      else PutNarrow<C>(o, s, va_arg(args, const char*));
      break;
    }
    case 'Z': {
      // A counted string (ANSI_STRING / UNICODE_STRING).
      struct Counted { USHORT Length, MaximumLength; void* Buffer; };
      Counted* c = va_arg(args, Counted*);
      if (!c || !c->Buffer) { PutNarrow<C>(o, s, NULL); break; }
      Spec cs = s;
      if (strw == 2 || (strw == 0 && wide)) {
        int n = c->Length / 2;
        if (cs.prec < 0 || cs.prec > n) cs.prec = n;
        PutWide<C>(o, cs, (const WCHAR*)c->Buffer);
      } else {
        int n = c->Length;
        if (cs.prec < 0 || cs.prec > n) cs.prec = n;
        PutNarrow<C>(o, cs, (const char*)c->Buffer);
      }
      break;
    }
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
      double v = ldbl ? (double)va_arg(args, long double) : va_arg(args, double);
      char spec[32];
      int k = 0;
      spec[k++] = '%';
      if (s.left) spec[k++] = '-';
      if (s.plus) spec[k++] = '+';
      if (s.space) spec[k++] = ' ';
      if (s.alt) spec[k++] = '#';
      if (s.zero) spec[k++] = '0';
      spec[k++] = '*';
      spec[k++] = '.';
      spec[k++] = '*';
      spec[k++] = (char)t;
      spec[k] = 0;
      char tmp[400];
      int n = snprintf(tmp, sizeof tmp, spec, s.width, s.prec < 0 ? 6 : s.prec, v);
      if (n < 0) n = 0;
      if (n >= (int)sizeof tmp) n = sizeof tmp - 1;
      Spec ns = {false, false, false, false, false, 0, -1};
      C wtmp[400];
      for (int i = 0; i < n; i++) wtmp[i] = (C)(unsigned char)tmp[i];
      PutPadded<C>(o, ns, wtmp, n);
      break;
    }
    case 'n':
      (void)va_arg(args, int*);  // not supported, as in newer Microsoft runtimes
      break;
    default:
      o.Put(t);
      break;
    }
  }
  o.Finish();
  return o.len;
}

}  // namespace

int AFX_CDECL AfxVFormatA(char* pszBuf, int nMax, const char* pszFormat, va_list args) {
  return VFormat<char>(pszBuf, nMax, pszFormat, args);
}

int AFX_CDECL AfxVFormatW(WCHAR* pszBuf, int nMax, const WCHAR* pszFormat, va_list args) {
  return VFormat<WCHAR>(pszBuf, nMax, pszFormat, args);
}

// ---------------------------------------------------------------------
// Helpers on strings

BOOL AFXAPI AfxExtractSubString(CString& rString, LPCTSTR lpszFullString,
                                int iSubString, TCHAR chSep) {
  if (lpszFullString == NULL) return FALSE;
  while (iSubString--) {
    lpszFullString = _tcschr(lpszFullString, chSep);
    if (lpszFullString == NULL) {
      rString.Empty();
      return FALSE;
    }
    lpszFullString++;
  }
  LPCTSTR lpchEnd = _tcschr(lpszFullString, chSep);
  int nLen = lpchEnd == NULL ? lstrlen(lpszFullString) : (int)(lpchEnd - lpszFullString);
  rString = CString(lpszFullString, nLen);
  return TRUE;
}
