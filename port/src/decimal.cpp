// decimal.cpp - DECIMAL, Automation's exact decimal number: a 96-bit
// whole number, a sign, and a power of ten (0 to 28) it is divided by.
// Conversions to and from text, doubles and LONGs, and comparison, all
// exact; where digits must go, they round half to even.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <oleauto.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

namespace {

const int kMaxScale = 28;

// A whole number of up to 256 bits, least significant word first: room
// for a 96-bit mantissa times 10^28 when two numbers are lined up.
struct Big {
  uint32_t w[8] = {};

  static Big Of(const DECIMAL& d) {
    Big b;
    b.w[0] = d.Lo32;
    b.w[1] = d.Mid32;
    b.w[2] = d.Hi32;
    return b;
  }
  bool Zero() const {
    for (uint32_t x : w)
      if (x) return false;
    return true;
  }
  // Fits a DECIMAL's 96 bits.
  bool Fits96() const {
    for (int i = 3; i < 8; i++)
      if (w[i]) return false;
    return true;
  }
  // this = this * m + a; false when it overflows 256 bits.
  bool MulAdd(uint32_t m, uint32_t a) {
    uint64_t carry = a;
    for (uint32_t& x : w) {
      uint64_t v = (uint64_t)x * m + carry;
      x = (uint32_t)v;
      carry = v >> 32;
    }
    return carry == 0;
  }
  // this /= d; the remainder.
  uint32_t Div(uint32_t d) {
    uint64_t r = 0;
    for (int i = 7; i >= 0; i--) {
      uint64_t v = (r << 32) | w[i];
      w[i] = (uint32_t)(v / d);
      r = v % d;
    }
    return (uint32_t)r;
  }
  int Cmp(const Big& o) const {
    for (int i = 7; i >= 0; i--)
      if (w[i] != o.w[i]) return w[i] < o.w[i] ? -1 : 1;
    return 0;
  }
};

void Store(const Big& b, int scale, bool neg, DECIMAL* out) {
  out->wReserved = 0;
  out->scale = (BYTE)scale;
  out->sign = neg && !b.Zero() ? DECIMAL_NEG : 0;
  out->Lo32 = b.w[0];
  out->Mid32 = b.w[1];
  out->Hi32 = b.w[2];
}

bool Valid(const DECIMAL* d) { return d && d->scale <= kMaxScale && (d->sign & ~DECIMAL_NEG) == 0; }

// Parses [space][sign]digits[.digits][e[sign]digits][space]. Digits past
// the 96 bits round off, half to even.
HRESULT Parse(const wchar_t* s, DECIMAL* out) {
  while (*s == ' ' || *s == '\t') s++;
  bool neg = false;
  if (*s == '+' || *s == '-') neg = *s++ == '-';
  std::string digits;  // the significant digits
  int point = 0;       // how many digits came before the point
  bool sawPoint = false, any = false;
  for (;; s++) {
    if (*s >= '0' && *s <= '9') {
      any = true;
      if (digits.empty() && *s == '0') {
        // A leading zero: it counts only as a place after the point.
        if (sawPoint) point--;
        continue;
      }
      digits += (char)*s;
    } else if (*s == ',' && !sawPoint) {
      continue;  // a thousands separator
    } else if (*s == '.' && !sawPoint) {
      sawPoint = true;
      point = (int)digits.size();
    } else {
      break;
    }
  }
  if (!any) return DISP_E_TYPEMISMATCH;
  if (!sawPoint) point = (int)digits.size();
  // point is now how many of the digits come before the point (less than
  // none for zeros after the point before the first digit).
  long exp10 = 0;
  if (*s == 'e' || *s == 'E') {
    s++;
    bool eneg = false;
    if (*s == '+' || *s == '-') eneg = *s++ == '-';
    if (*s < '0' || *s > '9') return DISP_E_TYPEMISMATCH;
    while (*s >= '0' && *s <= '9') {
      if (exp10 < 100000) exp10 = exp10 * 10 + (*s - '0');
      s++;
    }
    if (eneg) exp10 = -exp10;
  }
  while (*s == ' ' || *s == '\t') s++;
  if (*s) return DISP_E_TYPEMISMATCH;
  if (digits.empty()) {
    DECIMAL_SETZERO(*out);
    out->wReserved = 0;
    return S_OK;
  }
  // The value is 0.digits * 10^(point + exp10); as a whole number of
  // digits divided by 10^scale, scale = digits - (point + exp10).
  long scale = (long)digits.size() - (point + exp10);
  // Too many places: the extra digits go, rounding.
  if (scale > kMaxScale) {
    long drop = scale - kMaxScale;
    if (drop > (long)digits.size()) {
      DECIMAL_SETZERO(*out);
      out->wReserved = 0;
      return S_OK;
    }
    std::string rest = digits.substr(digits.size() - (size_t)drop);
    digits.resize(digits.size() - (size_t)drop);
    scale = kMaxScale;
    bool up = rest[0] > '5' || (rest[0] == '5' && (rest.find_first_not_of('0', 1) != std::string::npos ||
                                                    (!digits.empty() && (digits.back() - '0') % 2)));
    if (up) {
      // Add one to the digits.
      int i = (int)digits.size() - 1;
      while (i >= 0 && digits[i] == '9') digits[i--] = '0';
      if (i < 0) digits.insert(digits.begin(), '1');
      else digits[i]++;
    }
  }
  // A whole number with trailing zeros to add.
  while (scale < 0) {
    digits += '0';
    scale++;
    if (digits.size() > 40) return DISP_E_OVERFLOW;
  }
  Big b;
  for (char c : digits)
    if (!b.MulAdd(10, (uint32_t)(c - '0'))) return DISP_E_OVERFLOW;
  // More than 96 bits: give up places (rounding) while there are any.
  bool sticky = false;  // a nonzero digit went below the one rounded on
  while (!b.Fits96()) {
    if (scale == 0) return DISP_E_OVERFLOW;
    uint32_t r = b.Div(10);
    if (r > 5 || (r == 5 && (sticky || (b.w[0] & 1)))) b.MulAdd(1, 1);
    sticky = sticky || r != 0;
    scale--;
  }
  Store(b, (int)scale, neg, out);
  return S_OK;
}

std::wstring Text(const DECIMAL& d) {
  Big b = Big::Of(d);
  std::string digits;
  do {
    digits.insert(digits.begin(), (char)('0' + b.Div(10)));
  } while (!b.Zero());
  int scale = d.scale;
  while ((int)digits.size() <= scale) digits.insert(digits.begin(), '0');
  std::string whole = digits.substr(0, digits.size() - (size_t)scale);
  std::string frac = digits.substr(digits.size() - (size_t)scale);
  while (!frac.empty() && frac.back() == '0') frac.pop_back();
  std::string t = (d.sign & DECIMAL_NEG) && (whole != "0" || !frac.empty()) ? "-" : "";
  t += whole;
  if (!frac.empty()) t += "." + frac;
  return std::wstring(t.begin(), t.end());
}

}  // namespace

extern "C" {

HRESULT WINAPI VarDecFromStr(LPCOLESTR s, LCID, ULONG, DECIMAL* out) {
  if (!s || !out) return E_INVALIDARG;
  return Parse(s, out);
}

HRESULT WINAPI VarBstrFromDec(const DECIMAL* d, LCID, ULONG, BSTR* out) {
  if (!Valid(d) || !out) return E_INVALIDARG;
  std::wstring t = Text(*d);
  *out = SysAllocStringLen(t.data(), (UINT)t.size());
  return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WINAPI VarDecFromI4(LONG v, DECIMAL* out) {
  if (!out) return E_INVALIDARG;
  Big b;
  b.w[0] = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
  Store(b, 0, v < 0, out);
  return S_OK;
}

// As Windows does, to the double's 15 significant digits.
HRESULT WINAPI VarDecFromR8(DOUBLE v, DECIMAL* out) {
  if (!out) return E_INVALIDARG;
  if (isnan(v) || fabs(v) >= 7.9228162514264338e28) return DISP_E_OVERFLOW;
  char buf[40];
  snprintf(buf, sizeof buf, "%.14e", v);
  wchar_t w[40];
  size_t i = 0;
  for (; buf[i]; i++) w[i] = (wchar_t)buf[i];
  w[i] = 0;
  return Parse(w, out);
}

HRESULT WINAPI VarR8FromDec(const DECIMAL* d, DOUBLE* out) {
  if (!Valid(d) || !out) return E_INVALIDARG;
  // Through the text, for the double nearest the exact value.
  std::wstring t = Text(*d);
  std::string a(t.begin(), t.end());
  *out = strtod(a.c_str(), NULL);
  return S_OK;
}

// VARCMP_LT, VARCMP_EQ or VARCMP_GT; +0 and -0 are equal.
HRESULT WINAPI VarDecCmp(const DECIMAL* a, const DECIMAL* b) {
  if (!Valid(a) || !Valid(b)) return E_INVALIDARG;
  Big x = Big::Of(*a), y = Big::Of(*b);
  bool xn = (a->sign & DECIMAL_NEG) && !x.Zero(), yn = (b->sign & DECIMAL_NEG) && !y.Zero();
  if (xn != yn) return xn ? VARCMP_LT : VARCMP_GT;
  // The same scale: multiply the one with fewer places.
  for (int s = a->scale; s < b->scale; s++) x.MulAdd(10, 0);
  for (int s = b->scale; s < a->scale; s++) y.MulAdd(10, 0);
  int c = x.Cmp(y);
  if (xn) c = -c;
  return c < 0 ? VARCMP_LT : c > 0 ? VARCMP_GT : VARCMP_EQ;
}

}  // extern "C"
