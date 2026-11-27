// atlcur.h - CComCurrency, the CURRENCY (CY) type of Automation as a
// class: a 64-bit count of ten-thousandths, with arithmetic that rounds
// as VarCyMul and VarCyDiv do (half to even) and an exception
// (AtlThrow) on overflow.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCUR_H__
#define __ATLCUR_H__

#include <atlbase.h>

namespace ATL {

#define CY_MIN_INTEGER (-922337203685477LL)
#define CY_MAX_INTEGER 922337203685477LL
#define CY_MIN_FRACTION (-9999)
#define CY_MAX_FRACTION 9999
#define CY_SCALE 10000

class CComCurrency {
public:
  CURRENCY m_currency;

  CComCurrency() { m_currency.int64 = 0; }
  CComCurrency(CURRENCY cy) { m_currency.int64 = cy.int64; }
  CComCurrency(const CComCurrency& cy) { m_currency.int64 = cy.m_currency.int64; }
  CComCurrency(LONGLONG nInteger, SHORT nFraction) {
    m_currency.int64 = 0;
    HRESULT hr = SetInteger(nInteger);
    if (SUCCEEDED(hr)) hr = SetFraction(nFraction);
    if (FAILED(hr)) AtlThrow(hr);
  }
  explicit CComCurrency(BYTE n) { _SetWhole(n); }
  explicit CComCurrency(CHAR n) { _SetWhole(n); }
  explicit CComCurrency(SHORT n) { _SetWhole(n); }
  explicit CComCurrency(USHORT n) { _SetWhole(n); }
  explicit CComCurrency(LONG n) { _SetWhole(n); }
  explicit CComCurrency(ULONG n) { _SetWhole(n); }
#ifndef _WINPORT
  explicit CComCurrency(int n) { _SetWhole(n); }
  explicit CComCurrency(unsigned int n) { _SetWhole(n); }
#else  // there LONG is int, and long is a type of its own
  explicit CComCurrency(long n) { _SetWhole(n); }
  explicit CComCurrency(unsigned long n) { _SetWhole((LONGLONG)n); }
#endif
  explicit CComCurrency(float f) { _SetDouble(f); }
  explicit CComCurrency(double d) { _SetDouble(d); }
  explicit CComCurrency(LPCWSTR psz) { _SetString(psz); }
  explicit CComCurrency(LPCSTR psz) {
    USES_CONVERSION;
    _SetString(A2CW(psz));
  }
  explicit CComCurrency(const VARIANT& var, LCID lcid = LOCALE_USER_DEFAULT) {
    m_currency.int64 = 0;
    HRESULT hr = _FromVariant(var, lcid);
    if (FAILED(hr)) AtlThrow(hr);
  }
  explicit CComCurrency(DECIMAL dec) {
    VARIANT v;
    v.decVal = dec;
    v.vt = VT_DECIMAL;
    m_currency.int64 = 0;
    HRESULT hr = _FromVariant(v, LOCALE_USER_DEFAULT);
    if (FAILED(hr)) AtlThrow(hr);
  }

  // The whole part, and the ten-thousandths (with the value's sign).
  LONGLONG GetInteger() const { return m_currency.int64 / CY_SCALE; }
  SHORT GetFraction() const { return (SHORT)(m_currency.int64 % CY_SCALE); }

  HRESULT SetInteger(LONGLONG nInteger) {
    if (nInteger < CY_MIN_INTEGER || nInteger > CY_MAX_INTEGER) return E_INVALIDARG;
    LONGLONG f = GetFraction();
    if (f < 0) f = -f;
    if (nInteger < 0 || (nInteger == 0 && m_currency.int64 < 0)) f = -f;
    m_currency.int64 = nInteger * CY_SCALE + f;
    return S_OK;
  }
  HRESULT SetFraction(SHORT nFraction) {
    if (nFraction < CY_MIN_FRACTION || nFraction > CY_MAX_FRACTION) return E_INVALIDARG;
    LONGLONG i = GetInteger();
    LONGLONG f = nFraction;
    if (i > 0 && f < 0) return E_INVALIDARG;
    if (i < 0 && f > 0) f = -f;
    m_currency.int64 = i * CY_SCALE + f;
    return S_OK;
  }
  // Rounded to nDecimals places (0 to 4), half to even.
  HRESULT Round(int nDecimals) {
    if (nDecimals < 0 || nDecimals > 4) return E_INVALIDARG;
    static const LONGLONG s_scale[] = {10000, 1000, 100, 10, 1};
    LONGLONG d = s_scale[nDecimals];
    if (d == 1) return S_OK;
    bool neg = m_currency.int64 < 0;
    ULONGLONG a = _Abs(m_currency.int64);
    ULONGLONG q = a / d, r = a % d;
    if (r * 2 > (ULONGLONG)d || (r * 2 == (ULONGLONG)d && (q & 1))) q++;
    a = q * d;
    m_currency.int64 = neg ? -(LONGLONG)a : (LONGLONG)a;
    return S_OK;
  }

  CURRENCY* GetCurrencyPtr() { return &m_currency; }
  operator CURRENCY() const { return m_currency; }

  // Assignment.
  CComCurrency& operator=(const CComCurrency& cy) {
    m_currency.int64 = cy.m_currency.int64;
    return *this;
  }
  CComCurrency& operator=(CURRENCY cy) {
    m_currency.int64 = cy.int64;
    return *this;
  }
  CComCurrency& operator=(BYTE n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(CHAR n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(SHORT n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(USHORT n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(LONG n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(ULONG n) { _SetWhole(n); return *this; }
#ifndef _WINPORT
  CComCurrency& operator=(int n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(unsigned int n) { _SetWhole(n); return *this; }
#else
  CComCurrency& operator=(long n) { _SetWhole(n); return *this; }
  CComCurrency& operator=(unsigned long n) { _SetWhole((LONGLONG)n); return *this; }
#endif
  CComCurrency& operator=(float f) { _SetDouble(f); return *this; }
  CComCurrency& operator=(double d) { _SetDouble(d); return *this; }
  CComCurrency& operator=(const VARIANT& var) {
    HRESULT hr = _FromVariant(var, LOCALE_USER_DEFAULT);
    if (FAILED(hr)) AtlThrow(hr);
    return *this;
  }

  // Arithmetic.
  CComCurrency operator-() const {
    if (m_currency.int64 == (LONGLONG)_Min()) AtlThrow(DISP_E_OVERFLOW);
    CComCurrency r;
    r.m_currency.int64 = -m_currency.int64;
    return r;
  }
  CComCurrency& operator+=(const CComCurrency& cy) {
    m_currency.int64 = _Add(m_currency.int64, cy.m_currency.int64);
    return *this;
  }
  CComCurrency& operator-=(const CComCurrency& cy) {
    m_currency.int64 = _Sub(m_currency.int64, cy.m_currency.int64);
    return *this;
  }
  CComCurrency& operator*=(const CComCurrency& cy) {
    m_currency.int64 = _MulDiv(m_currency.int64, cy.m_currency.int64, CY_SCALE);
    return *this;
  }
  CComCurrency& operator*=(long n) {
    m_currency.int64 = _MulDiv(m_currency.int64, n, 1);
    return *this;
  }
  CComCurrency& operator/=(const CComCurrency& cy) {
    if (cy.m_currency.int64 == 0) AtlThrow(DISP_E_DIVBYZERO);
    m_currency.int64 = _MulDiv(m_currency.int64, CY_SCALE, cy.m_currency.int64);
    return *this;
  }
  CComCurrency& operator/=(long n) {
    if (n == 0) AtlThrow(DISP_E_DIVBYZERO);
    m_currency.int64 = _MulDiv(m_currency.int64, 1, n);
    return *this;
  }
  CComCurrency operator+(const CComCurrency& cy) const { CComCurrency r(*this); return r += cy; }
  CComCurrency operator-(const CComCurrency& cy) const { CComCurrency r(*this); return r -= cy; }
  CComCurrency operator*(const CComCurrency& cy) const { CComCurrency r(*this); return r *= cy; }
  CComCurrency operator*(long n) const { CComCurrency r(*this); return r *= n; }
  CComCurrency operator/(const CComCurrency& cy) const { CComCurrency r(*this); return r /= cy; }
  CComCurrency operator/(long n) const { CComCurrency r(*this); return r /= n; }

  // Comparison.
  bool operator==(const CComCurrency& cy) const { return m_currency.int64 == cy.m_currency.int64; }
  bool operator!=(const CComCurrency& cy) const { return m_currency.int64 != cy.m_currency.int64; }
  bool operator<(const CComCurrency& cy) const { return m_currency.int64 < cy.m_currency.int64; }
  bool operator>(const CComCurrency& cy) const { return m_currency.int64 > cy.m_currency.int64; }
  bool operator<=(const CComCurrency& cy) const { return m_currency.int64 <= cy.m_currency.int64; }
  bool operator>=(const CComCurrency& cy) const { return m_currency.int64 >= cy.m_currency.int64; }

private:
  static ULONGLONG _Min() { return 0x8000000000000000ULL; }
  static ULONGLONG _Abs(LONGLONG n) { return n < 0 ? (ULONGLONG)0 - (ULONGLONG)n : (ULONGLONG)n; }

  void _SetWhole(LONGLONG n) {
    if (n < CY_MIN_INTEGER || n > CY_MAX_INTEGER) AtlThrow(DISP_E_OVERFLOW);
    m_currency.int64 = n * CY_SCALE;
  }
  void _SetDouble(double d) {
    HRESULT hr = ::VarCyFromR8(d, &m_currency);
    if (FAILED(hr)) AtlThrow(hr);
  }
  void _SetString(LPCWSTR psz) {
    if (psz == NULL) AtlThrow(E_INVALIDARG);
    HRESULT hr = ::VarCyFromStr((LPOLESTR)psz, LOCALE_USER_DEFAULT, 0, &m_currency);
    if (FAILED(hr)) AtlThrow(hr);
  }
  HRESULT _FromVariant(const VARIANT& var, LCID lcid) {
    VARIANT v;
    ::VariantInit(&v);
    HRESULT hr = ::VariantChangeTypeEx(&v, const_cast<VARIANT*>(&var), lcid, 0, VT_CY);
    if (SUCCEEDED(hr)) m_currency = v.cyVal;
    ::VariantClear(&v);
    return hr;
  }

  static LONGLONG _Add(LONGLONG a, LONGLONG b) {
    LONGLONG r;
    if (__builtin_add_overflow(a, b, &r)) AtlThrow(DISP_E_OVERFLOW);
    return r;
  }
  static LONGLONG _Sub(LONGLONG a, LONGLONG b) {
    LONGLONG r;
    if (__builtin_sub_overflow(a, b, &r)) AtlThrow(DISP_E_OVERFLOW);
    return r;
  }

  // a * b / c, the product kept in 128 bits, rounded half to even.
  static LONGLONG _MulDiv(LONGLONG a, LONGLONG b, LONGLONG c) {
    bool neg = (a < 0) != (b < 0);
    if (c < 0) neg = !neg;
    ULONGLONG ua = _Abs(a), ub = _Abs(b), uc = _Abs(c);
    // The 128-bit product, from 32-bit halves.
    ULONGLONG a0 = ua & 0xffffffffULL, a1 = ua >> 32, b0 = ub & 0xffffffffULL, b1 = ub >> 32;
    ULONGLONG p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    ULONGLONG mid = (p00 >> 32) + (p01 & 0xffffffffULL) + (p10 & 0xffffffffULL);
    ULONGLONG lo = (p00 & 0xffffffffULL) | (mid << 32);
    ULONGLONG hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    // Divided by c, a bit at a time; the quotient must fit in 64 bits.
    if (hi >= uc) AtlThrow(DISP_E_OVERFLOW);
    ULONGLONG q = 0, r = hi;
    for (int i = 63; i >= 0; i--) {
      bool carry = (r >> 63) != 0;
      r = (r << 1) | ((lo >> i) & 1);
      q <<= 1;
      if (carry || r >= uc) {
        r -= uc;
        q |= 1;
      }
    }
    // Half to even: r against c - r, without doubling r past 64 bits.
    ULONGLONG rest = uc - r;
    if (r > rest || (r == rest && (q & 1))) {
      if (q == ~0ULL) AtlThrow(DISP_E_OVERFLOW);
      q++;
    }
    if (neg) {
      if (q > _Min()) AtlThrow(DISP_E_OVERFLOW);
      return (LONGLONG)(0 - q);
    }
    if (q >= _Min()) AtlThrow(DISP_E_OVERFLOW);
    return (LONGLONG)q;
  }
};

}  // namespace ATL

#endif  // __ATLCUR_H__
