// variant.cpp - OLE Automation values: VARIANT copying, clearing and
// conversion, the Var* conversions of dates and currency, and safe arrays.
//
// Conversions follow Automation's rules: numbers round half to even,
// out-of-range values give DISP_E_OVERFLOW, text that does not parse gives
// DISP_E_TYPEMISMATCH, and TRUE is -1. Dates are days since 30 December
// 1899, with the time as the fraction. Text is in the en-US format.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <oleauto.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <string>

using namespace winport;

namespace {

BSTR AllocW(const std::wstring& s) { return SysAllocStringLen(s.data(), (UINT)s.size()); }
std::wstring FromAscii(const char* s) { return std::wstring(s, s + strlen(s)); }

// Days from 1 January 1970 of a civil date, and back.
long long DaysFromCivil(long long y, int m, int d) {
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  long long yoe = y - era * 400;
  long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
void CivilFromDays(long long z, int* y, int* m, int* d) {
  z += 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  long long doe = z - era * 146097;
  long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long long mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yoe + era * 400 + (*m <= 2));
}
const long long kDays1899 = -25569;  // 30 Dec 1899 from 1 Jan 1970

bool DateFromParts(int y, int mo, int d, int h, int mi, int s, DATE* out) {
  if (y < 100 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
      s < 0 || s > 59)
    return false;
  int yy, mm, dd;
  long long days = DaysFromCivil(y, mo, d);
  CivilFromDays(days, &yy, &mm, &dd);
  if (mm != mo) return false;  // 31 April and the like
  days -= kDays1899;
  double frac = (h * 3600 + mi * 60 + s) / 86400.0;
  // Before 1900 the time counts away from zero: -1.25 is 29 Dec 1899 06:00.
  *out = days >= 0 ? days + frac : days - frac;
  return true;
}

// Splits a date into its parts, the time rounded to the second.
void PartsFromDate(DATE dt, int* y, int* mo, int* d, int* h, int* mi, int* s) {
  double whole = dt < 0 ? ceil(dt) : floor(dt);
  double frac = fabs(dt - whole);
  long long secs = (long long)floor(frac * 86400 + 0.5);
  long long days = (long long)whole;
  if (secs >= 86400) {
    secs -= 86400;
    days += dt < 0 ? -1 : 1;
  }
  CivilFromDays(days + kDays1899, y, mo, d);
  *h = (int)(secs / 3600);
  *mi = (int)(secs / 60 % 60);
  *s = (int)(secs % 60);
}

std::wstring TextFromDate(DATE dt, ULONG flags) {
  int y, mo, d, h, mi, s;
  PartsFromDate(dt, &y, &mo, &d, &h, &mi, &s);
  char date[32], time[32];
  snprintf(date, sizeof(date), "%d/%d/%d", mo, d, y);
  snprintf(time, sizeof(time), "%d:%02d:%02d %s", h % 12 == 0 ? 12 : h % 12, mi, s, h < 12 ? "AM" : "PM");
  bool hasTime = h != 0 || mi != 0 || s != 0;
  bool hasDate = (long long)dt != 0;
  if (flags & VAR_DATEVALUEONLY) return FromAscii(date);
  if (flags & VAR_TIMEVALUEONLY) return FromAscii(time);
  if (!hasDate && hasTime) return FromAscii(time);
  if (!hasTime) return FromAscii(date);
  return FromAscii(date) + L" " + FromAscii(time);
}

const char* const kMonths[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

// Dates as "M/D/Y", "Y-M-D", "D Month Y" or "Month D, Y", and times as
// "H:M[:S] [AM|PM]", either or both.
bool DateFromText(const std::wstring& w, DATE* out) {
  std::string s;
  for (wchar_t c : w) s += c < 128 ? (char)tolower((int)c) : ' ';
  int nums[6], nnum = 0, month = 0;
  int time[3] = {0, 0, 0}, ntime = 0;
  bool pm = false, am = false, dateSep = false;
  size_t i = 0;
  while (i < s.size()) {
    if (isdigit((unsigned char)s[i])) {
      int v = 0;
      size_t start = i;
      while (i < s.size() && isdigit((unsigned char)s[i])) v = v * 10 + (s[i++] - '0');
      if (i - start > 4) return false;
      if (i < s.size() && s[i] == ':') {
        // A time: H:M[:S]
        time[0] = v;
        ntime = 1;
        while (i < s.size() && s[i] == ':' && ntime < 3) {
          i++;
          int t = 0;
          if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
          while (i < s.size() && isdigit((unsigned char)s[i])) t = t * 10 + (s[i++] - '0');
          time[ntime++] = t;
        }
        continue;
      }
      if (nnum == 6) return false;
      nums[nnum++] = v;
      if (i < s.size() && (s[i] == '/' || s[i] == '-' || s[i] == '.')) dateSep = true;
    } else if (isalpha((unsigned char)s[i])) {
      size_t start = i;
      while (i < s.size() && isalpha((unsigned char)s[i])) i++;
      std::string word = s.substr(start, i - start);
      if (word == "am" || word == "a") am = true;
      else if (word == "pm" || word == "p") pm = true;
      else {
        int k = 0;
        for (; k < 12; k++)
          if (word.compare(0, 3, kMonths[k]) == 0 && word.size() >= 3) break;
        if (k == 12) return false;
        month = k + 1;
      }
    } else {
      i++;
    }
  }
  int h = time[0], mi = time[1], sec = time[2];
  if (ntime == 0 && (am || pm) && nnum > 0) {
    // "5 PM"
    h = nums[--nnum];
    ntime = 1;
  }
  if (pm && h < 12) h += 12;
  if (am && h == 12) h = 0;
  if (nnum == 0 && month == 0) {
    if (ntime == 0) return false;
    if (h > 23 || mi > 59 || sec > 59) return false;
    *out = (h * 3600 + mi * 60 + sec) / 86400.0;
    return true;
  }
  int y, mo, d;
  if (month != 0) {
    if (nnum != 2) return false;
    mo = month;
    if (nums[0] > 31) {
      y = nums[0];
      d = nums[1];
    } else {
      d = nums[0];
      y = nums[1];
    }
  } else if (nnum == 3 && nums[0] > 31) {
    y = nums[0];  // Y-M-D
    mo = nums[1];
    d = nums[2];
  } else if (nnum == 3) {
    mo = nums[0];  // M/D/Y
    d = nums[1];
    y = nums[2];
  } else if (nnum == 2 && dateSep) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    mo = nums[0];
    d = nums[1];
    y = st.wYear;
  } else {
    return false;
  }
  if (y < 30) y += 2000;
  else if (y < 100) y += 1900;
  return DateFromParts(y, mo, d, h, mi, sec, out);
}

// Rounds half to even.
double RoundEven(double v) {
  double r = floor(v + 0.5);
  if (r - v == 0.5 && fmod(r, 2) != 0) r -= 1;
  return r;
}

// Numbers as text: up to 15 significant digits, no trailing zeros.
std::wstring TextFromDouble(double v) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.15G", v);
  return FromAscii(buf);
}

bool DoubleFromText(const std::wstring& w, double* out) {
  std::string s;
  for (wchar_t c : w) {
    if (c == ',') continue;  // thousands separators
    if (c >= 128) return false;
    s += (char)c;
  }
  size_t a = s.find_first_not_of(" \t");
  size_t b = s.find_last_not_of(" \t");
  if (a == std::string::npos) return false;
  s = s.substr(a, b - a + 1);
  bool neg = false;
  if (s[0] == '(' && s[s.size() - 1] == ')') {
    neg = true;
    s = s.substr(1, s.size() - 2);
  }
  if (!s.empty() && s[0] == '$') s = s.substr(1);
  if (s.size() > 2 && s[0] == '&' && (s[1] == 'h' || s[1] == 'H')) {
    char* end;
    unsigned long long v = strtoull(s.c_str() + 2, &end, 16);
    if (*end) return false;
    *out = (double)(long long)(int)(unsigned)v;
    return true;
  }
  char* end;
  double v = strtod(s.c_str(), &end);
  if (end == s.c_str() || *end) return false;
  *out = neg ? -v : v;
  return true;
}

std::wstring TextFromCy(CY cy) {
  long long v = cy.int64;
  bool neg = v < 0;
  unsigned long long u = neg ? 0 - (unsigned long long)v : (unsigned long long)v;
  char buf[48];
  snprintf(buf, sizeof(buf), "%s%llu", neg ? "-" : "", u / 10000);
  unsigned frac = (unsigned)(u % 10000);
  std::string s = buf;
  if (frac != 0) {
    char f[8];
    snprintf(f, sizeof(f), ".%04u", frac);
    std::string fs = f;
    while (fs.back() == '0') fs.pop_back();
    s += fs;
  }
  return FromAscii(s.c_str());
}

bool CyFromDouble(double v, CY* cy) {
  double scaled = RoundEven(v * 10000);
  if (!(scaled >= -9.2233720368547758e18 && scaled < 9.2233720368547758e18)) return false;
  cy->int64 = (long long)scaled;
  return true;
}

bool CyFromText(const std::wstring& w, CY* cy) {
  // Exact decimal parsing, so "0.1" is 1000 and not 999.99...
  std::string s;
  for (wchar_t c : w)
    if (c != ',' && c != ' ' && c != '$') s += c < 128 ? (char)c : '?';
  bool neg = false;
  size_t i = 0;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
  unsigned long long whole = 0;
  bool any = false;
  while (i < s.size() && isdigit((unsigned char)s[i])) {
    whole = whole * 10 + (s[i++] - '0');
    any = true;
    if (whole > 922337203685477ULL) return false;
  }
  unsigned long long frac = 0;
  int nfrac = 0;
  int roundDigit = -1;
  bool sticky = false;
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
      if (nfrac < 4) frac = frac * 10 + (s[i] - '0'), nfrac++;
      else if (roundDigit < 0) roundDigit = s[i] - '0';
      else if (s[i] != '0') sticky = true;
      i++;
      any = true;
    }
  }
  if (!any || i != s.size()) {
    double d;
    if (!DoubleFromText(w, &d)) return false;
    return CyFromDouble(d, cy);
  }
  while (nfrac < 4) frac *= 10, nfrac++;
  unsigned long long v = whole * 10000 + frac;
  if (roundDigit > 5 || (roundDigit == 5 && (sticky || (v & 1)))) v++;
  if (v > 9223372036854775807ULL) return false;
  cy->int64 = neg ? -(long long)v : (long long)v;
  return true;
}

// The default value of an object, for converting it.
HRESULT ValueOfObject(IDispatch* disp, VARIANT* out) {
  if (disp == NULL) return DISP_E_TYPEMISMATCH;
  DISPPARAMS params = {NULL, NULL, 0, 0};
  VariantInit(out);
  HRESULT hr = disp->Invoke(DISPID_VALUE, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, out, NULL,
                            NULL);
  return FAILED(hr) ? DISP_E_TYPEMISMATCH : S_OK;
}

bool IsNumber(VARTYPE vt) {
  switch (vt) {
    case VT_I1: case VT_I2: case VT_I4: case VT_I8: case VT_UI1: case VT_UI2: case VT_UI4: case VT_UI8:
    case VT_INT: case VT_UINT: case VT_R4: case VT_R8: case VT_CY: case VT_DATE: case VT_BOOL: case VT_ERROR:
      return true;
    default:
      return false;
  }
}

// A numeric value as an exact integer when it is one, else a double.
struct Num {
  bool isInt;
  long long i;
  unsigned long long u;  // for VT_UI8 above LLONG_MAX
  bool big;
  double d;
};

Num NumOf(const VARIANT* v) {
  Num n = {true, 0, 0, false, 0};
  switch (v->vt) {
    case VT_I1: n.i = v->cVal; break;
    case VT_I2: n.i = v->iVal; break;
    case VT_I4: n.i = v->lVal; break;
    case VT_ERROR: n.i = v->scode; break;
    case VT_I8: n.i = v->llVal; break;
    case VT_UI1: n.i = v->bVal; break;
    case VT_UI2: n.i = v->uiVal; break;
    case VT_UI4: n.i = v->ulVal; break;
    case VT_UI8:
      if (v->ullVal > 0x7fffffffffffffffULL) {
        n.big = true;
        n.u = v->ullVal;
        n.isInt = false;
        n.d = (double)v->ullVal;
      } else {
        n.i = (long long)v->ullVal;
      }
      break;
    case VT_INT: n.i = v->intVal; break;
    case VT_UINT: n.i = v->uintVal; break;
    case VT_BOOL: n.i = v->boolVal ? -1 : 0; break;
    case VT_R4: n.isInt = false; n.d = v->fltVal; break;
    case VT_R8: n.isInt = false; n.d = v->dblVal; break;
    case VT_DATE: n.isInt = false; n.d = v->date; break;
    case VT_CY:
      if (v->cyVal.int64 % 10000 == 0) n.i = v->cyVal.int64 / 10000;
      else n.isInt = false, n.d = v->cyVal.int64 / 10000.0;
      break;
  }
  if (n.isInt) n.d = (double)n.i;
  return n;
}

// An integer target: rounds a double half to even and checks the range.
HRESULT ToInt(const Num& n, long long lo, long long hi, long long* out) {
  if (n.big) return DISP_E_OVERFLOW;
  if (n.isInt) {
    if (n.i < lo || n.i > hi) return DISP_E_OVERFLOW;
    *out = n.i;
    return S_OK;
  }
  if (!(n.d > -9.3e18 && n.d < 9.3e18)) return DISP_E_OVERFLOW;
  double r = RoundEven(n.d);
  if (r < (double)lo || r > (double)hi) return DISP_E_OVERFLOW;
  *out = (long long)r;
  return S_OK;
}

HRESULT ToUInt64(const Num& n, unsigned long long* out) {
  if (n.big) {
    *out = n.u;
    return S_OK;
  }
  long long v;
  HRESULT hr = ToInt(n, 0, 0x7fffffffffffffffLL, &v);
  if (SUCCEEDED(hr)) *out = (unsigned long long)v;
  return hr;
}

HRESULT FromNumber(const Num& n, VARTYPE vt, VARIANT* out, ULONG flags) {
  long long v = 0;
  HRESULT hr = S_OK;
  switch (vt) {
    case VT_I1: hr = ToInt(n, -128, 127, &v); out->cVal = (CHAR)v; break;
    case VT_I2: hr = ToInt(n, -32768, 32767, &v); out->iVal = (SHORT)v; break;
    case VT_I4: case VT_INT: hr = ToInt(n, -2147483647LL - 1, 2147483647LL, &v); out->lVal = (LONG)v; break;
    case VT_ERROR: hr = ToInt(n, -2147483647LL - 1, 2147483647LL, &v); out->scode = (SCODE)v; break;
    case VT_I8: hr = ToInt(n, -0x7fffffffffffffffLL - 1, 0x7fffffffffffffffLL, &v); out->llVal = v; break;
    case VT_UI1: hr = ToInt(n, 0, 255, &v); out->bVal = (BYTE)v; break;
    case VT_UI2: hr = ToInt(n, 0, 65535, &v); out->uiVal = (USHORT)v; break;
    case VT_UI4: case VT_UINT: hr = ToInt(n, 0, 4294967295LL, &v); out->ulVal = (ULONG)v; break;
    case VT_UI8: hr = ToUInt64(n, &out->ullVal); break;
    case VT_R4:
      if (fabs(n.d) > 3.402823466e38) return DISP_E_OVERFLOW;
      out->fltVal = (FLOAT)n.d;
      break;
    case VT_R8: out->dblVal = n.d; break;
    case VT_DATE:
      if (n.d < -657434.0 || n.d >= 2958466.0) return DISP_E_OVERFLOW;
      out->date = n.d;
      break;
    case VT_CY:
      if (n.isInt) {
        if (n.i > 922337203685477LL || n.i < -922337203685477LL) return DISP_E_OVERFLOW;
        out->cyVal.int64 = n.i * 10000;
      } else if (!CyFromDouble(n.d, &out->cyVal)) {
        return DISP_E_OVERFLOW;
      }
      break;
    case VT_BOOL: out->boolVal = n.d != 0 ? VARIANT_TRUE : VARIANT_FALSE; break;
    default: return DISP_E_TYPEMISMATCH;
  }
  (void)flags;
  if (SUCCEEDED(hr)) out->vt = vt;
  return hr;
}

}  // namespace

extern "C" {

void WINAPI VariantInit(VARIANTARG* v) {
  if (v == NULL) return;
  v->vt = VT_EMPTY;
  v->wReserved1 = v->wReserved2 = v->wReserved3 = 0;
}

HRESULT WINAPI VariantClear(VARIANTARG* v) {
  if (v == NULL) return E_INVALIDARG;
  if (!(v->vt & VT_BYREF)) {
    if (v->vt & VT_ARRAY) {
      HRESULT hr = SafeArrayDestroy(v->parray);
      if (FAILED(hr)) return hr;
    } else {
      switch (v->vt) {
        case VT_BSTR: SysFreeString(v->bstrVal); break;
        case VT_UNKNOWN:
        case VT_DISPATCH:
          if (v->punkVal != NULL) v->punkVal->Release();
          break;
        case VT_RECORD:
          if (v->pRecInfo != NULL) {
            v->pRecInfo->RecordClear(v->pvRecord);
            v->pRecInfo->Release();
          }
          break;
        default: break;
      }
    }
  }
  v->vt = VT_EMPTY;
  return S_OK;
}

HRESULT WINAPI VariantCopy(VARIANTARG* dst, const VARIANTARG* src) {
  if (dst == NULL || src == NULL) return E_INVALIDARG;
  if (dst == src) return S_OK;
  HRESULT hr = VariantClear(dst);
  if (FAILED(hr)) return hr;
  if (src->vt & VT_BYREF) {
    *dst = *src;
    return S_OK;
  }
  if (src->vt & VT_ARRAY) {
    SAFEARRAY* copy = NULL;
    if (src->parray != NULL) {
      hr = SafeArrayCopy(src->parray, &copy);
      if (FAILED(hr)) return hr;
    }
    *dst = *src;
    dst->parray = copy;
    return S_OK;
  }
  switch (src->vt) {
    case VT_BSTR:
      *dst = *src;
      if (src->bstrVal != NULL) {
        dst->bstrVal = SysAllocStringByteLen((LPCSTR)src->bstrVal, SysStringByteLen(src->bstrVal));
        if (dst->bstrVal == NULL) {
          dst->vt = VT_EMPTY;
          return E_OUTOFMEMORY;
        }
      }
      return S_OK;
    case VT_UNKNOWN:
    case VT_DISPATCH:
      *dst = *src;
      if (dst->punkVal != NULL) dst->punkVal->AddRef();
      return S_OK;
    case VT_RECORD:
      return E_NOTIMPL;
    default:
      *dst = *src;
      return S_OK;
  }
}

HRESULT WINAPI VariantCopyInd(VARIANT* dst, const VARIANTARG* src) {
  if (dst == NULL || src == NULL) return E_INVALIDARG;
  if (!(src->vt & VT_BYREF)) return VariantCopy(dst, src);
  VARIANT tmp;
  VariantInit(&tmp);
  VARTYPE vt = src->vt & ~VT_BYREF;
  if (vt == VT_VARIANT) return VariantCopyInd(dst, src->pvarVal);
  tmp.vt = vt;
  if (vt & VT_ARRAY) {
    tmp.parray = *src->pparray;
  } else {
    switch (vt) {
      case VT_I1: case VT_UI1: tmp.bVal = *src->pbVal; break;
      case VT_I2: case VT_UI2: case VT_BOOL: tmp.iVal = *src->piVal; break;
      case VT_I4: case VT_UI4: case VT_INT: case VT_UINT: case VT_ERROR: case VT_R4: tmp.lVal = *src->plVal; break;
      case VT_I8: case VT_UI8: case VT_R8: case VT_DATE: case VT_CY: tmp.llVal = *src->pllVal; break;
      case VT_BSTR: tmp.bstrVal = *src->pbstrVal; break;
      case VT_UNKNOWN: case VT_DISPATCH: tmp.punkVal = *src->ppunkVal; break;
      case VT_DECIMAL: tmp.decVal = *src->pdecVal; tmp.vt = VT_DECIMAL; break;
      default: return DISP_E_BADVARTYPE;
    }
  }
  // tmp borrows the referenced value; the copy owns its own.
  VARIANT out;
  VariantInit(&out);
  HRESULT hr = VariantCopy(&out, &tmp);
  if (FAILED(hr)) return hr;
  if (dst == src) {
    *dst = out;
    return S_OK;
  }
  VariantClear(dst);
  *dst = out;
  return S_OK;
}

HRESULT WINAPI VariantChangeTypeEx(VARIANTARG* dst, const VARIANTARG* src, LCID, USHORT flags, VARTYPE vt) {
  if (dst == NULL || src == NULL) return E_INVALIDARG;
  VARIANT in;
  VariantInit(&in);
  HRESULT hr = VariantCopyInd(&in, src);
  if (FAILED(hr)) return hr;
  VARIANT out;
  VariantInit(&out);
  if (in.vt == vt) {
    hr = VariantCopy(&out, &in);
  } else if (in.vt == VT_DISPATCH && vt != VT_UNKNOWN) {
    // An object converts through its value property.
    VARIANT value;
    hr = (flags & VARIANT_NOVALUEPROP) ? DISP_E_TYPEMISMATCH : ValueOfObject(in.pdispVal, &value);
    if (SUCCEEDED(hr)) {
      hr = VariantChangeTypeEx(&out, &value, 0, flags, vt);
      VariantClear(&value);
    }
  } else if (vt == VT_EMPTY) {
    hr = S_OK;
  } else if (vt == VT_NULL) {
    hr = in.vt == VT_EMPTY ? S_OK : DISP_E_TYPEMISMATCH;
    out.vt = VT_NULL;
  } else if (in.vt == VT_NULL) {
    hr = DISP_E_TYPEMISMATCH;
  } else if (vt == VT_BSTR) {
    std::wstring s;
    hr = S_OK;
    switch (in.vt) {
      case VT_EMPTY: break;
      case VT_BOOL:
        if (flags & VARIANT_ALPHABOOL) s = in.boolVal ? L"True" : L"False";
        else s = in.boolVal ? L"-1" : L"0";
        break;
      case VT_DATE: s = TextFromDate(in.date, 0); break;
      case VT_CY: s = TextFromCy(in.cyVal); break;
      case VT_R4: {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.7G", in.fltVal);
        s = FromAscii(buf);
        break;
      }
      case VT_R8: s = TextFromDouble(in.dblVal); break;
      case VT_UI8: s = FromAscii(std::to_string(in.ullVal).c_str()); break;
      default:
        if (IsNumber(in.vt)) s = FromAscii(std::to_string(NumOf(&in).i).c_str());
        else hr = DISP_E_TYPEMISMATCH;
    }
    if (SUCCEEDED(hr)) {
      out.vt = VT_BSTR;
      out.bstrVal = AllocW(s);
      if (out.bstrVal == NULL) hr = E_OUTOFMEMORY, out.vt = VT_EMPTY;
    }
  } else if (vt == VT_UNKNOWN || vt == VT_DISPATCH) {
    if ((in.vt == VT_UNKNOWN || in.vt == VT_DISPATCH) && in.punkVal != NULL) {
      void* p = NULL;
      hr = in.punkVal->QueryInterface(vt == VT_UNKNOWN ? IID_IUnknown : IID_IDispatch, &p);
      if (SUCCEEDED(hr)) out.vt = vt, out.punkVal = (IUnknown*)p;
      else hr = DISP_E_TYPEMISMATCH;
    } else {
      hr = DISP_E_TYPEMISMATCH;
    }
  } else if (in.vt == VT_EMPTY) {
    Num zero = {true, 0, 0, false, 0};
    hr = FromNumber(zero, vt, &out, flags);
  } else if (in.vt == VT_BSTR) {
    std::wstring s(in.bstrVal ? in.bstrVal : L"", in.bstrVal ? SysStringLen(in.bstrVal) : 0);
    if (vt == VT_DATE) {
      DATE d;
      double n;
      if (DateFromText(s, &d)) out.vt = VT_DATE, out.date = d, hr = S_OK;
      else if (DoubleFromText(s, &n)) out.vt = VT_DATE, out.date = n, hr = S_OK;
      else hr = DISP_E_TYPEMISMATCH;
    } else if (vt == VT_CY) {
      if (CyFromText(s, &out.cyVal)) out.vt = VT_CY, hr = S_OK;
      else hr = DISP_E_TYPEMISMATCH;
    } else if (vt == VT_BOOL) {
      std::wstring t;
      for (wchar_t c : s)
        if (!iswspace(c)) t += (wchar_t)towlower(c);
      double n;
      if (t == L"true" || t == L"yes" || t == L"on") out.vt = VT_BOOL, out.boolVal = VARIANT_TRUE, hr = S_OK;
      else if (t == L"false" || t == L"no" || t == L"off") out.vt = VT_BOOL, out.boolVal = VARIANT_FALSE, hr = S_OK;
      else if (DoubleFromText(s, &n)) out.vt = VT_BOOL, out.boolVal = n != 0 ? VARIANT_TRUE : VARIANT_FALSE, hr = S_OK;
      else hr = DISP_E_TYPEMISMATCH;
    } else {
      double n;
      if (!DoubleFromText(s, &n)) {
        hr = DISP_E_TYPEMISMATCH;
      } else {
        Num num = {false, 0, 0, false, n};
        // Whole numbers keep their exact value.
        if (n == floor(n) && fabs(n) < 9e15) num.isInt = true, num.i = (long long)n;
        hr = FromNumber(num, vt, &out, flags);
      }
    }
  } else if (IsNumber(in.vt)) {
    hr = FromNumber(NumOf(&in), vt, &out, flags);
  } else {
    hr = DISP_E_TYPEMISMATCH;
  }
  VariantClear(&in);
  if (FAILED(hr)) {
    VariantClear(&out);
    return hr;
  }
  if (dst != src) VariantClear(dst);
  else VariantClear(dst);
  *dst = out;
  return S_OK;
}

HRESULT WINAPI VariantChangeType(VARIANTARG* dst, const VARIANTARG* src, USHORT flags, VARTYPE vt) {
  return VariantChangeTypeEx(dst, src, LOCALE_USER_DEFAULT, flags, vt);
}

// Dates
INT WINAPI SystemTimeToVariantTime(LPSYSTEMTIME st, DOUBLE* out) {
  if (st == NULL || out == NULL) return FALSE;
  DATE d;
  if (!DateFromParts(st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond, &d)) return FALSE;
  *out = d;
  return TRUE;
}

INT WINAPI VariantTimeToSystemTime(DOUBLE dt, LPSYSTEMTIME st) {
  if (st == NULL || dt < -657434.0 || dt >= 2958466.0) return FALSE;
  int y, mo, d, h, mi, s;
  PartsFromDate(dt, &y, &mo, &d, &h, &mi, &s);
  st->wYear = (WORD)y;
  st->wMonth = (WORD)mo;
  st->wDay = (WORD)d;
  st->wHour = (WORD)h;
  st->wMinute = (WORD)mi;
  st->wSecond = (WORD)s;
  st->wMilliseconds = 0;
  long long days = DaysFromCivil(y, mo, d);
  st->wDayOfWeek = (WORD)(((days % 7) + 11) % 7);  // 1 Jan 1970 was a Thursday
  return TRUE;
}

INT WINAPI DosDateTimeToVariantTime(USHORT date, USHORT time, DOUBLE* out) {
  if (out == NULL) return FALSE;
  return DateFromParts(1980 + (date >> 9), (date >> 5) & 15, date & 31, time >> 11, (time >> 5) & 63,
                       (time & 31) * 2, out);
}

INT WINAPI VariantTimeToDosDateTime(DOUBLE dt, USHORT* date, USHORT* time) {
  SYSTEMTIME st;
  if (date == NULL || time == NULL || !VariantTimeToSystemTime(dt, &st) || st.wYear < 1980 || st.wYear > 2107)
    return FALSE;
  *date = (USHORT)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
  *time = (USHORT)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
  return TRUE;
}

HRESULT WINAPI VarDateFromStr(LPCOLESTR s, LCID, ULONG, DATE* out) {
  if (s == NULL || out == NULL) return E_INVALIDARG;
  return DateFromText(s, out) ? S_OK : DISP_E_TYPEMISMATCH;
}

HRESULT WINAPI VarBstrFromDate(DATE dt, LCID, ULONG flags, BSTR* out) {
  if (out == NULL) return E_INVALIDARG;
  if (dt < -657434.0 || dt >= 2958466.0) return E_INVALIDARG;
  *out = AllocW(TextFromDate(dt, flags));
  return *out ? S_OK : E_OUTOFMEMORY;
}

// Currency and doubles
HRESULT WINAPI VarCyFromStr(LPCOLESTR s, LCID, ULONG, CY* out) {
  if (s == NULL || out == NULL) return E_INVALIDARG;
  return CyFromText(s, out) ? S_OK : DISP_E_TYPEMISMATCH;
}
HRESULT WINAPI VarBstrFromCy(CY cy, LCID, ULONG, BSTR* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = AllocW(TextFromCy(cy));
  return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT WINAPI VarR8FromStr(LPCOLESTR s, LCID, ULONG, DOUBLE* out) {
  if (s == NULL || out == NULL) return E_INVALIDARG;
  return DoubleFromText(s, out) ? S_OK : DISP_E_TYPEMISMATCH;
}
HRESULT WINAPI VarBstrFromR8(DOUBLE v, LCID, ULONG, BSTR* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = AllocW(TextFromDouble(v));
  return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT WINAPI VarI4FromStr(LPCOLESTR s, LCID, ULONG, LONG* out) {
  double d;
  if (s == NULL || out == NULL) return E_INVALIDARG;
  if (!DoubleFromText(s, &d)) return DISP_E_TYPEMISMATCH;
  d = RoundEven(d);
  if (d < -2147483648.0 || d > 2147483647.0) return DISP_E_OVERFLOW;
  *out = (LONG)d;
  return S_OK;
}
HRESULT WINAPI VarCyFromR8(DOUBLE v, CY* out) {
  if (out == NULL) return E_INVALIDARG;
  return CyFromDouble(v, out) ? S_OK : DISP_E_OVERFLOW;
}
HRESULT WINAPI VarR8FromCy(CY cy, DOUBLE* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = cy.int64 / 10000.0;
  return S_OK;
}

}  // extern "C"

// Safe arrays. The bounds are kept with the rightmost dimension first
// (rgsabound[0]), and the leftmost index varies fastest in memory. A
// 16-byte prefix before the descriptor holds the element type.
namespace {

const size_t kPrefix = 16;

VARTYPE& TypeOf(SAFEARRAY* psa) { return *(VARTYPE*)((BYTE*)psa - 4); }

ULONG SizeOfType(VARTYPE vt) {
  switch (vt) {
    case VT_I1: case VT_UI1: return 1;
    case VT_I2: case VT_UI2: case VT_BOOL: return 2;
    case VT_I4: case VT_UI4: case VT_INT: case VT_UINT: case VT_R4: case VT_ERROR: return 4;
    case VT_I8: case VT_UI8: case VT_R8: case VT_CY: case VT_DATE: return 8;
    case VT_BSTR: case VT_UNKNOWN: case VT_DISPATCH: return sizeof(void*);
    case VT_VARIANT: return sizeof(VARIANT);
    case VT_DECIMAL: return sizeof(DECIMAL);
    default: return 0;
  }
}

USHORT FeaturesOf(VARTYPE vt) {
  switch (vt) {
    case VT_BSTR: return FADF_BSTR;
    case VT_UNKNOWN: return FADF_UNKNOWN;
    case VT_DISPATCH: return FADF_DISPATCH;
    case VT_VARIANT: return FADF_VARIANT;
    default: return 0;
  }
}

ULONG CountOf(const SAFEARRAY* psa) {
  ULONG n = 1;
  for (USHORT i = 0; i < psa->cDims; i++) n *= psa->rgsabound[i].cElements;
  return n;
}

// Releases what the elements hold, leaving them zero.
void ClearElements(SAFEARRAY* psa) {
  if (psa->pvData == NULL) return;
  ULONG n = CountOf(psa);
  if (psa->fFeatures & FADF_BSTR) {
    BSTR* p = (BSTR*)psa->pvData;
    for (ULONG i = 0; i < n; i++) SysFreeString(p[i]), p[i] = NULL;
  } else if (psa->fFeatures & (FADF_UNKNOWN | FADF_DISPATCH)) {
    IUnknown** p = (IUnknown**)psa->pvData;
    for (ULONG i = 0; i < n; i++)
      if (p[i] != NULL) p[i]->Release(), p[i] = NULL;
  } else if (psa->fFeatures & FADF_VARIANT) {
    VARIANT* p = (VARIANT*)psa->pvData;
    for (ULONG i = 0; i < n; i++) VariantClear(&p[i]);
  }
}

HRESULT CopyElement(SAFEARRAY* psa, void* dst, const void* src) {
  if (psa->fFeatures & FADF_BSTR) {
    BSTR s = *(const BSTR*)src;
    BSTR copy = s ? SysAllocStringByteLen((LPCSTR)s, SysStringByteLen(s)) : NULL;
    if (s != NULL && copy == NULL) return E_OUTOFMEMORY;
    SysFreeString(*(BSTR*)dst);
    *(BSTR*)dst = copy;
  } else if (psa->fFeatures & (FADF_UNKNOWN | FADF_DISPATCH)) {
    IUnknown* p = *(IUnknown* const*)src;
    if (p != NULL) p->AddRef();
    if (*(IUnknown**)dst != NULL) (*(IUnknown**)dst)->Release();
    *(IUnknown**)dst = p;
  } else if (psa->fFeatures & FADF_VARIANT) {
    return VariantCopy((VARIANT*)dst, (const VARIANT*)src);
  } else {
    memcpy(dst, src, psa->cbElements);
  }
  return S_OK;
}

}  // namespace

extern "C" {

HRESULT WINAPI SafeArrayAllocDescriptor(UINT dims, SAFEARRAY** out) {
  if (out == NULL) return E_POINTER;
  if (dims == 0 || dims > 0xffff) return E_INVALIDARG;
  size_t size = kPrefix + sizeof(SAFEARRAY) + (dims - 1) * sizeof(SAFEARRAYBOUND);
  BYTE* p = (BYTE*)calloc(1, size);
  if (p == NULL) return E_OUTOFMEMORY;
  SAFEARRAY* psa = (SAFEARRAY*)(p + kPrefix);
  psa->cDims = (USHORT)dims;
  *out = psa;
  return S_OK;
}

HRESULT WINAPI SafeArrayAllocDescriptorEx(VARTYPE vt, UINT dims, SAFEARRAY** out) {
  HRESULT hr = SafeArrayAllocDescriptor(dims, out);
  if (FAILED(hr)) return hr;
  (*out)->fFeatures = FADF_HAVEVARTYPE | FeaturesOf(vt);
  (*out)->cbElements = SizeOfType(vt);
  TypeOf(*out) = vt;
  return S_OK;
}

HRESULT WINAPI SafeArrayAllocData(SAFEARRAY* psa) {
  if (psa == NULL) return E_INVALIDARG;
  ULONG n = CountOf(psa);
  psa->pvData = calloc(n ? n : 1, psa->cbElements ? psa->cbElements : 1);
  return psa->pvData ? S_OK : E_OUTOFMEMORY;
}

SAFEARRAY* WINAPI SafeArrayCreate(VARTYPE vt, UINT dims, SAFEARRAYBOUND* bounds) {
  if (bounds == NULL || SizeOfType(vt) == 0) return NULL;
  SAFEARRAY* psa;
  if (FAILED(SafeArrayAllocDescriptorEx(vt, dims, &psa))) return NULL;
  // The caller lists the leftmost dimension first.
  for (UINT i = 0; i < dims; i++) psa->rgsabound[dims - 1 - i] = bounds[i];
  if (FAILED(SafeArrayAllocData(psa))) {
    SafeArrayDestroyDescriptor(psa);
    return NULL;
  }
  return psa;
}

SAFEARRAY* WINAPI SafeArrayCreateVector(VARTYPE vt, LONG lbound, ULONG count) {
  SAFEARRAYBOUND b = {count, lbound};
  return SafeArrayCreate(vt, 1, &b);
}

HRESULT WINAPI SafeArrayDestroyData(SAFEARRAY* psa) {
  if (psa == NULL) return E_INVALIDARG;
  if (psa->cLocks > 0) return DISP_E_ARRAYISLOCKED;
  ClearElements(psa);
  if (!(psa->fFeatures & (FADF_STATIC | FADF_AUTO | FADF_EMBEDDED))) {
    free(psa->pvData);
    psa->pvData = NULL;
  }
  return S_OK;
}

HRESULT WINAPI SafeArrayDestroyDescriptor(SAFEARRAY* psa) {
  if (psa == NULL) return E_INVALIDARG;
  if (psa->cLocks > 0) return DISP_E_ARRAYISLOCKED;
  free((BYTE*)psa - kPrefix);
  return S_OK;
}

HRESULT WINAPI SafeArrayDestroy(SAFEARRAY* psa) {
  if (psa == NULL) return S_OK;
  HRESULT hr = SafeArrayDestroyData(psa);
  if (FAILED(hr)) return hr;
  return SafeArrayDestroyDescriptor(psa);
}

UINT WINAPI SafeArrayGetDim(SAFEARRAY* psa) { return psa ? psa->cDims : 0; }
UINT WINAPI SafeArrayGetElemsize(SAFEARRAY* psa) { return psa ? psa->cbElements : 0; }

HRESULT WINAPI SafeArrayGetVartype(SAFEARRAY* psa, VARTYPE* vt) {
  if (psa == NULL || vt == NULL) return E_INVALIDARG;
  if (psa->fFeatures & FADF_HAVEVARTYPE) *vt = TypeOf(psa);
  else if (psa->fFeatures & FADF_BSTR) *vt = VT_BSTR;
  else if (psa->fFeatures & FADF_UNKNOWN) *vt = VT_UNKNOWN;
  else if (psa->fFeatures & FADF_DISPATCH) *vt = VT_DISPATCH;
  else if (psa->fFeatures & FADF_VARIANT) *vt = VT_VARIANT;
  else return E_INVALIDARG;
  return S_OK;
}

HRESULT WINAPI SafeArrayGetUBound(SAFEARRAY* psa, UINT dim, LONG* out) {
  if (psa == NULL || out == NULL) return E_INVALIDARG;
  if (dim < 1 || dim > psa->cDims) return DISP_E_BADINDEX;
  const SAFEARRAYBOUND& b = psa->rgsabound[psa->cDims - dim];
  *out = b.lLbound + (LONG)b.cElements - 1;
  return S_OK;
}

HRESULT WINAPI SafeArrayGetLBound(SAFEARRAY* psa, UINT dim, LONG* out) {
  if (psa == NULL || out == NULL) return E_INVALIDARG;
  if (dim < 1 || dim > psa->cDims) return DISP_E_BADINDEX;
  *out = psa->rgsabound[psa->cDims - dim].lLbound;
  return S_OK;
}

HRESULT WINAPI SafeArrayLock(SAFEARRAY* psa) {
  if (psa == NULL) return E_INVALIDARG;
  if (psa->cLocks == 0xffff) return E_UNEXPECTED;
  psa->cLocks++;
  return S_OK;
}

HRESULT WINAPI SafeArrayUnlock(SAFEARRAY* psa) {
  if (psa == NULL) return E_INVALIDARG;
  if (psa->cLocks == 0) return E_UNEXPECTED;
  psa->cLocks--;
  return S_OK;
}

HRESULT WINAPI SafeArrayAccessData(SAFEARRAY* psa, void** data) {
  if (psa == NULL || data == NULL) return E_INVALIDARG;
  HRESULT hr = SafeArrayLock(psa);
  *data = SUCCEEDED(hr) ? psa->pvData : NULL;
  return hr;
}

HRESULT WINAPI SafeArrayUnaccessData(SAFEARRAY* psa) { return SafeArrayUnlock(psa); }

HRESULT WINAPI SafeArrayPtrOfIndex(SAFEARRAY* psa, LONG* indices, void** out) {
  if (psa == NULL || indices == NULL || out == NULL) return E_INVALIDARG;
  ULONG offset = 0, stride = 1;
  for (USHORT d = 0; d < psa->cDims; d++) {
    // indices[d] is dimension d + 1, kept at rgsabound[cDims - 1 - d].
    const SAFEARRAYBOUND& b = psa->rgsabound[psa->cDims - 1 - d];
    LONG i = indices[d] - b.lLbound;
    if (i < 0 || (ULONG)i >= b.cElements) return DISP_E_BADINDEX;
    offset += (ULONG)i * stride;
    stride *= b.cElements;
  }
  *out = (BYTE*)psa->pvData + (size_t)offset * psa->cbElements;
  return S_OK;
}

HRESULT WINAPI SafeArrayGetElement(SAFEARRAY* psa, LONG* indices, void* out) {
  void* p;
  if (out == NULL) return E_INVALIDARG;
  HRESULT hr = SafeArrayPtrOfIndex(psa, indices, &p);
  if (FAILED(hr)) return hr;
  // The caller's buffer is uninitialized: no old value to release.
  if (psa->fFeatures & FADF_VARIANT) VariantInit((VARIANT*)out);
  else if (psa->fFeatures & (FADF_BSTR | FADF_UNKNOWN | FADF_DISPATCH)) *(void**)out = NULL;
  return CopyElement(psa, out, p);
}

HRESULT WINAPI SafeArrayPutElement(SAFEARRAY* psa, LONG* indices, void* value) {
  void* p;
  HRESULT hr = SafeArrayPtrOfIndex(psa, indices, &p);
  if (FAILED(hr)) return hr;
  // A BSTR and an interface are passed as the value itself.
  if (psa->fFeatures & (FADF_BSTR | FADF_UNKNOWN | FADF_DISPATCH)) return CopyElement(psa, p, &value);
  if (value == NULL) return E_INVALIDARG;
  return CopyElement(psa, p, value);
}

HRESULT WINAPI SafeArrayCopy(SAFEARRAY* psa, SAFEARRAY** out) {
  if (out == NULL) return E_INVALIDARG;
  *out = NULL;
  if (psa == NULL) return S_OK;
  SAFEARRAY* copy;
  HRESULT hr = SafeArrayAllocDescriptor(psa->cDims, &copy);
  if (FAILED(hr)) return hr;
  copy->fFeatures = psa->fFeatures & ~(FADF_STATIC | FADF_AUTO | FADF_EMBEDDED);
  copy->cbElements = psa->cbElements;
  TypeOf(copy) = TypeOf(psa);
  memcpy(copy->rgsabound, psa->rgsabound, psa->cDims * sizeof(SAFEARRAYBOUND));
  if (psa->pvData != NULL) {
    hr = SafeArrayAllocData(copy);
    ULONG n = CountOf(psa);
    for (ULONG i = 0; SUCCEEDED(hr) && i < n; i++)
      hr = CopyElement(copy, (BYTE*)copy->pvData + (size_t)i * copy->cbElements,
                       (BYTE*)psa->pvData + (size_t)i * psa->cbElements);
    if (FAILED(hr)) {
      SafeArrayDestroy(copy);
      return hr;
    }
  }
  *out = copy;
  return S_OK;
}

HRESULT WINAPI SafeArrayRedim(SAFEARRAY* psa, SAFEARRAYBOUND* bound) {
  if (psa == NULL || bound == NULL) return E_INVALIDARG;
  if (psa->cLocks > 0) return DISP_E_ARRAYISLOCKED;
  if (psa->fFeatures & FADF_FIXEDSIZE) return E_INVALIDARG;
  // The rightmost dimension changes; it varies slowest, so the elements
  // that remain keep their places.
  ULONG oldCount = CountOf(psa);
  ULONG slab = 1;
  for (USHORT i = 1; i < psa->cDims; i++) slab *= psa->rgsabound[i].cElements;
  ULONG newCount = slab * bound->cElements;
  if (newCount < oldCount && psa->pvData != NULL) {
    // Release the elements that go.
    SAFEARRAY tail = *psa;
    tail.cDims = 1;
    tail.rgsabound[0].cElements = oldCount - newCount;
    tail.pvData = (BYTE*)psa->pvData + (size_t)newCount * psa->cbElements;
    ClearElements(&tail);
  }
  void* data = realloc(psa->pvData, (newCount ? newCount : 1) * (size_t)psa->cbElements);
  if (data == NULL) return E_OUTOFMEMORY;
  if (newCount > oldCount)
    memset((BYTE*)data + (size_t)oldCount * psa->cbElements, 0, (size_t)(newCount - oldCount) * psa->cbElements);
  psa->pvData = data;
  psa->rgsabound[0] = *bound;
  return S_OK;
}

}  // extern "C"
