// olevar.cpp - COleDateTime, COleDateTimeSpan, COleCurrency, COleVariant
// and COleSafeArray, and dialog data exchange for dates and currency.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdisp.h>
#include <afxcmn.h>
#include <math.h>
#include <time.h>

// ---------------------------------------------------------------------
// Dates. A DATE counts days from 30 December 1899; the fraction is the
// time of day, which for a negative date still counts forward from
// midnight (so -1.25 is 29 December 1899, 6 a.m.).

#define AFX_MIN_DATE (-657434L)  // 1 January 100
#define AFX_MAX_DATE 2958465L    // 31 December 9999

// Days from 1 January 1970 in the proleptic Gregorian calendar.
static long _AfxDaysFromCivil(long y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void _AfxCivilFromDays(long z, int& y, int& m, int& d) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2));
}

static const long _afxDaysTo1970 = 25569;  // 30 Dec 1899 to 1 Jan 1970

static int _AfxDaysInMonth(int y, int m) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 29;
  return days[m - 1];
}

static BOOL _AfxOleDateFromParts(int y, int mo, int d, int h, int mi, int s, DATE& dt) {
  if (y < 100 || y > 9999 || mo < 1 || mo > 12) return FALSE;
  if (d < 1 || d > _AfxDaysInMonth(y, mo)) return FALSE;
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return FALSE;
  long nDays = _AfxDaysFromCivil(y, mo, d) + _afxDaysTo1970;
  double dblTime = (h * 3600L + mi * 60L + s) / 86400.0;
  dt = nDays >= 0 ? nDays + dblTime : nDays - dblTime;
  return TRUE;
}

// The calendar fields of a date, rounded to the second.
static BOOL _AfxTmFromOleDate(DATE dt, struct tm& t) {
  if (dt > AFX_MAX_DATE + 1 || dt < AFX_MIN_DATE - 1) return FALSE;
  long nDays = (long)dt;  // towards zero
  double dblFrac = fabs(dt - nDays);
  long nSecs = (long)floor(dblFrac * 86400 + 0.5);
  if (nSecs >= 86400) {
    nSecs -= 86400;
    nDays++;
  }
  if (nDays > AFX_MAX_DATE || nDays < AFX_MIN_DATE) return FALSE;
  int y, m, d;
  _AfxCivilFromDays(nDays - _afxDaysTo1970, y, m, d);
  memset(&t, 0, sizeof(t));
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = (int)(nSecs / 3600);
  t.tm_min = (int)(nSecs / 60 % 60);
  t.tm_sec = (int)(nSecs % 60);
  long nDays1970 = nDays - _afxDaysTo1970;
  t.tm_wday = (int)(((nDays1970 % 7) + 7 + 4) % 7);  // 1 Jan 1970 was a Thursday
  t.tm_yday = (int)(nDays1970 - _AfxDaysFromCivil(y, 1, 1));
  return TRUE;
}

// A date as a plain number of days, and back, for arithmetic.
static double _AfxDoubleFromDate(DATE dt) {
  if (dt >= 0) return dt;
  double dblDays = ceil(dt);
  return dblDays - (dt - dblDays);
}

static DATE _AfxDateFromDouble(double dbl) {
  if (dbl >= 0) return dbl;
  double dblDays = floor(dbl);
  return dblDays - (dbl - dblDays);
}

COleDateTime PASCAL COleDateTime::GetCurrentTime() {
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  st.wMilliseconds = 0;
  return COleDateTime(st);
}

COleDateTime::COleDateTime(const VARIANT& varSrc) { *this = varSrc; }
COleDateTime::COleDateTime(time_t timeSrc) { *this = timeSrc; }
COleDateTime::COleDateTime(const SYSTEMTIME& systimeSrc) { *this = systimeSrc; }
COleDateTime::COleDateTime(const FILETIME& filetimeSrc) { *this = filetimeSrc; }

COleDateTime::COleDateTime(WORD wDosDate, WORD wDosTime) {
  FILETIME ft;
  SYSTEMTIME st;
  if (::DosDateTimeToFileTime(wDosDate, wDosTime, &ft) && ::FileTimeToSystemTime(&ft, &st)) {
    *this = st;
  } else {
    m_dt = 0;
    m_status = invalid;
  }
}

const COleDateTime& COleDateTime::operator=(const VARIANT& varSrc) {
  if (varSrc.vt == VT_DATE) {
    m_dt = varSrc.date;
    m_status = valid;
  } else if (varSrc.vt == VT_NULL) {
    m_dt = 0;
    m_status = null;
  } else {
    VARIANT var;
    ::VariantInit(&var);
    if (SUCCEEDED(::VariantChangeType(&var, (VARIANT*)&varSrc, 0, VT_DATE))) {
      m_dt = var.date;
      m_status = valid;
    } else {
      m_dt = 0;
      m_status = invalid;
    }
    ::VariantClear(&var);
  }
  return *this;
}

const COleDateTime& COleDateTime::operator=(DATE dtSrc) {
  m_dt = dtSrc;
  m_status = valid;
  return *this;
}

// A time_t is local time, as CTime shows it.
const COleDateTime& COleDateTime::operator=(const time_t& timeSrc) {
  SYSTEMTIME st;
  if (CTime(timeSrc).GetAsSystemTime(st))
    *this = st;
  else
    m_status = invalid;
  return *this;
}

const COleDateTime& COleDateTime::operator=(const SYSTEMTIME& st) {
  m_status = _AfxOleDateFromParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                                  st.wSecond, m_dt)
                 ? valid
                 : invalid;
  return *this;
}

// A FILETIME is UTC; the date is local time.
const COleDateTime& COleDateTime::operator=(const FILETIME& filetimeSrc) {
  FILETIME ftLocal;
  SYSTEMTIME st;
  if (::FileTimeToLocalFileTime(&filetimeSrc, &ftLocal) && ::FileTimeToSystemTime(&ftLocal, &st))
    *this = st;
  else
    m_status = invalid;
  return *this;
}

int COleDateTime::SetDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec) {
  m_status = _AfxOleDateFromParts(nYear, nMonth, nDay, nHour, nMin, nSec, m_dt) ? valid : invalid;
  return m_status == valid ? 0 : 1;
}

BOOL COleDateTime::GetAsSystemTime(SYSTEMTIME& st) const {
  struct tm t;
  if (m_status != valid || !_AfxTmFromOleDate(m_dt, t)) return FALSE;
  st.wYear = (WORD)(t.tm_year + 1900);
  st.wMonth = (WORD)(t.tm_mon + 1);
  st.wDayOfWeek = (WORD)t.tm_wday;
  st.wDay = (WORD)t.tm_mday;
  st.wHour = (WORD)t.tm_hour;
  st.wMinute = (WORD)t.tm_min;
  st.wSecond = (WORD)t.tm_sec;
  st.wMilliseconds = 0;
  return TRUE;
}

#define OLEDATE_FIELD(name, expr) \
  int COleDateTime::name() const { \
    struct tm t; \
    if (m_status != valid || !_AfxTmFromOleDate(m_dt, t)) return -1; \
    return expr; \
  }
OLEDATE_FIELD(GetYear, t.tm_year + 1900)
OLEDATE_FIELD(GetMonth, t.tm_mon + 1)
OLEDATE_FIELD(GetDay, t.tm_mday)
OLEDATE_FIELD(GetHour, t.tm_hour)
OLEDATE_FIELD(GetMinute, t.tm_min)
OLEDATE_FIELD(GetSecond, t.tm_sec)
OLEDATE_FIELD(GetDayOfWeek, t.tm_wday + 1)
OLEDATE_FIELD(GetDayOfYear, t.tm_yday + 1)
#undef OLEDATE_FIELD

BOOL COleDateTime::operator<(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) < _AfxDoubleFromDate(date.m_dt);
}
BOOL COleDateTime::operator>(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) > _AfxDoubleFromDate(date.m_dt);
}
BOOL COleDateTime::operator<=(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) <= _AfxDoubleFromDate(date.m_dt);
}
BOOL COleDateTime::operator>=(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) >= _AfxDoubleFromDate(date.m_dt);
}

COleDateTime COleDateTime::operator+(const COleDateTimeSpan& dateSpan) const {
  COleDateTime date;
  if (m_status != valid || dateSpan.m_status != COleDateTimeSpan::valid) {
    date.m_status = (m_status == null || dateSpan.m_status == COleDateTimeSpan::null) ? null
                                                                                    : invalid;
    return date;
  }
  date.m_dt = _AfxDateFromDouble(_AfxDoubleFromDate(m_dt) + dateSpan.m_span);
  if (date.m_dt > AFX_MAX_DATE + 1 || date.m_dt < AFX_MIN_DATE - 1) date.m_status = invalid;
  return date;
}

COleDateTime COleDateTime::operator-(const COleDateTimeSpan& dateSpan) const {
  return *this + (-dateSpan);
}

COleDateTimeSpan COleDateTime::operator-(const COleDateTime& date) const {
  COleDateTimeSpan span;
  if (m_status != valid || date.m_status != valid) {
    span.m_status = (m_status == null || date.m_status == null) ? COleDateTimeSpan::null
                                                                : COleDateTimeSpan::invalid;
    return span;
  }
  span.m_span = _AfxDoubleFromDate(m_dt) - _AfxDoubleFromDate(date.m_dt);
  return span;
}

static CString _AfxBstrToString(BSTR bstr) {
  CString str(bstr != NULL ? bstr : L"");
  return str;
}

BOOL COleDateTime::ParseDateTime(LPCTSTR lpszDate, DWORD dwFlags, LCID lcid) {
  CStringW strDate(lpszDate);
  DATE dt;
  HRESULT hr = ::VarDateFromStr((LPOLESTR)(LPCWSTR)strDate, lcid, dwFlags, &dt);
  if (FAILED(hr)) {
    m_dt = 0;
    m_status = invalid;
    return FALSE;
  }
  m_dt = dt;
  m_status = valid;
  return TRUE;
}

CString COleDateTime::Format(DWORD dwFlags, LCID lcid) const {
  if (m_status == null) return CString();
  if (m_status == invalid) return CString(_T("Invalid DateTime"));
  BSTR bstr = NULL;
  if (FAILED(::VarBstrFromDate(m_dt, lcid, dwFlags, &bstr))) return CString();
  CString str = _AfxBstrToString(bstr);
  ::SysFreeString(bstr);
  return str;
}

CString COleDateTime::Format(LPCTSTR lpszFormat) const {
  if (m_status == null) return CString();
  struct tm t;
  if (m_status == invalid || !_AfxTmFromOleDate(m_dt, t)) return CString(_T("Invalid DateTime"));
  return _AfxFormatTm(lpszFormat, t);
}

CString COleDateTime::Format(UINT nFormatID) const {
  CString strFormat;
  strFormat.LoadString(nFormatID);
  return Format((LPCTSTR)strFormat);
}

// ---------------------------------------------------------------------
// COleDateTimeSpan

static LONGLONG _AfxSpanSeconds(double dblSpan) {
  return (LONGLONG)(dblSpan >= 0 ? floor(dblSpan * 86400 + 0.5) : ceil(dblSpan * 86400 - 0.5));
}

long COleDateTimeSpan::GetHours() const { return (long)(_AfxSpanSeconds(m_span) / 3600 % 24); }
long COleDateTimeSpan::GetMinutes() const { return (long)(_AfxSpanSeconds(m_span) / 60 % 60); }
long COleDateTimeSpan::GetSeconds() const { return (long)(_AfxSpanSeconds(m_span) % 60); }

COleDateTimeSpan COleDateTimeSpan::operator+(const COleDateTimeSpan& s) const {
  COleDateTimeSpan r;
  if (m_status != valid || s.m_status != valid) {
    r.m_status = (m_status == null || s.m_status == null) ? null : invalid;
    return r;
  }
  r.m_span = m_span + s.m_span;
  return r;
}

COleDateTimeSpan COleDateTimeSpan::operator-(const COleDateTimeSpan& s) const {
  COleDateTimeSpan r;
  if (m_status != valid || s.m_status != valid) {
    r.m_status = (m_status == null || s.m_status == null) ? null : invalid;
    return r;
  }
  r.m_span = m_span - s.m_span;
  return r;
}

void COleDateTimeSpan::SetDateTimeSpan(long lDays, int nHours, int nMins, int nSecs) {
  m_span = lDays + ((double)nHours) / 24 + ((double)nMins) / (24 * 60) +
           ((double)nSecs) / (24 * 60 * 60);
  m_status = valid;
}

CString COleDateTimeSpan::Format(LPCTSTR lpszFormat) const {
  if (m_status == null) return CString();
  if (m_status == invalid) return CString(_T("Invalid DateTimeSpan"));
  CString r;
  for (LPCTSTR p = lpszFormat; *p; p++) {
    if (*p != '%') {
      r += *p;
      continue;
    }
    switch (*++p) {
      case 'D': r.AppendFormat(_T("%ld"), GetDays()); break;
      case 'H': r.AppendFormat(_T("%02ld"), GetHours()); break;
      case 'M': r.AppendFormat(_T("%02ld"), GetMinutes()); break;
      case 'S': r.AppendFormat(_T("%02ld"), GetSeconds()); break;
      case '%': r += _T('%'); break;
      case 0: return r;
      default: break;
    }
  }
  return r;
}

CString COleDateTimeSpan::Format(UINT nID) const {
  CString strFormat;
  strFormat.LoadString(nID);
  return Format((LPCTSTR)strFormat);
}

// ---------------------------------------------------------------------
// COleCurrency

COleCurrency::COleCurrency(const VARIANT& varSrc) { *this = varSrc; }

const COleCurrency& COleCurrency::operator=(const VARIANT& varSrc) {
  if (varSrc.vt == VT_CY) {
    m_cur = varSrc.cyVal;
    m_status = valid;
  } else if (varSrc.vt == VT_NULL) {
    m_cur.int64 = 0;
    m_status = null;
  } else {
    VARIANT var;
    ::VariantInit(&var);
    if (SUCCEEDED(::VariantChangeType(&var, (VARIANT*)&varSrc, 0, VT_CY))) {
      m_cur = var.cyVal;
      m_status = valid;
    } else {
      m_cur.int64 = 0;
      m_status = invalid;
    }
    ::VariantClear(&var);
  }
  return *this;
}

void COleCurrency::SetCurrency(long nUnits, long nFractionalUnits) {
  // The fraction is in ten-thousandths, with the same sign as the units.
  if (nFractionalUnits <= -10000 || nFractionalUnits >= 10000) {
    m_cur.int64 = 0;
    m_status = invalid;
    return;
  }
  m_cur.int64 = (LONGLONG)nUnits * 10000 + nFractionalUnits;
  m_status = valid;
}

static COleCurrency _AfxCurrencyResult(const COleCurrency& a, const COleCurrency& b) {
  COleCurrency r;
  r.m_status = (a.m_status == COleCurrency::null || b.m_status == COleCurrency::null)
                   ? COleCurrency::null
                   : COleCurrency::invalid;
  return r;
}

COleCurrency COleCurrency::operator+(const COleCurrency& cur) const {
  if (m_status != valid || cur.m_status != valid) return _AfxCurrencyResult(*this, cur);
  COleCurrency r;
  if (__builtin_add_overflow(m_cur.int64, cur.m_cur.int64, &r.m_cur.int64)) r.m_status = invalid;
  return r;
}

COleCurrency COleCurrency::operator-(const COleCurrency& cur) const {
  if (m_status != valid || cur.m_status != valid) return _AfxCurrencyResult(*this, cur);
  COleCurrency r;
  if (__builtin_sub_overflow(m_cur.int64, cur.m_cur.int64, &r.m_cur.int64)) r.m_status = invalid;
  return r;
}

COleCurrency COleCurrency::operator-() const {
  COleCurrency r(*this);
  if (m_status == valid) {
    if (m_cur.int64 == (LONGLONG)0x8000000000000000ULL)
      r.m_status = invalid;
    else
      r.m_cur.int64 = -m_cur.int64;
  }
  return r;
}

COleCurrency COleCurrency::operator*(long nOperand) const {
  COleCurrency r(*this);
  if (m_status != valid) return r;
  if (__builtin_mul_overflow(m_cur.int64, (LONGLONG)nOperand, &r.m_cur.int64)) r.m_status = invalid;
  return r;
}

COleCurrency COleCurrency::operator/(long nOperand) const {
  COleCurrency r(*this);
  if (m_status != valid) return r;
  if (nOperand == 0) {
    r.m_status = invalid;
    return r;
  }
  r.m_cur.int64 = m_cur.int64 / nOperand;
  return r;
}

BOOL COleCurrency::ParseCurrency(LPCTSTR lpszCurrency, DWORD dwFlags, LCID lcid) {
  CStringW str(lpszCurrency);
  CY cy;
  if (FAILED(::VarCyFromStr((LPOLESTR)(LPCWSTR)str, lcid, dwFlags, &cy))) {
    m_cur.int64 = 0;
    m_status = invalid;
    return FALSE;
  }
  m_cur = cy;
  m_status = valid;
  return TRUE;
}

CString COleCurrency::Format(DWORD dwFlags, LCID lcid) const {
  if (m_status == null) return CString();
  if (m_status == invalid) return CString(_T("Invalid Currency"));
  BSTR bstr = NULL;
  if (FAILED(::VarBstrFromCy(m_cur, lcid, dwFlags, &bstr))) return CString();
  CString str = _AfxBstrToString(bstr);
  ::SysFreeString(bstr);
  return str;
}

// ---------------------------------------------------------------------
// COleVariant

COleVariant::COleVariant(const VARIANT& varSrc) {
  ::VariantInit(this);
  AfxCheckError(::VariantCopy(this, (VARIANT*)&varSrc));
}

COleVariant::COleVariant(LPCVARIANT pSrc) {
  ::VariantInit(this);
  AfxCheckError(::VariantCopy(this, (VARIANT*)pSrc));
}

COleVariant::COleVariant(const COleVariant& varSrc) : tagVARIANT() {
  ::VariantInit(this);
  AfxCheckError(::VariantCopy(this, (VARIANT*)(const VARIANT*)&varSrc));
}

COleVariant::COleVariant(LPCTSTR lpszSrc) {
  ::VariantInit(this);
  SetString(lpszSrc, VT_BSTR);
}

COleVariant::COleVariant(LPCTSTR lpszSrc, VARTYPE vtSrc) {
  ::VariantInit(this);
  SetString(lpszSrc, vtSrc);
}

COleVariant::COleVariant(CString& strSrc) {
  ::VariantInit(this);
  SetString(strSrc, VT_BSTR);
}

COleVariant::COleVariant(BYTE nSrc) {
  ::VariantInit(this);
  vt = VT_UI1;
  bVal = nSrc;
}

COleVariant::COleVariant(short nSrc, VARTYPE vtSrc) {
  ::VariantInit(this);
  if (vtSrc == VT_BOOL) {
    vt = VT_BOOL;
    boolVal = nSrc ? VARIANT_TRUE : VARIANT_FALSE;
  } else {
    vt = VT_I2;
    iVal = nSrc;
  }
}

COleVariant::COleVariant(long lSrc, VARTYPE vtSrc) {
  ::VariantInit(this);
  if (vtSrc == VT_BOOL) {
    vt = VT_BOOL;
    boolVal = lSrc ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (vtSrc == VT_ERROR) {
    vt = VT_ERROR;
    scode = lSrc;
  } else {
    vt = VT_I4;
    lVal = lSrc;
  }
}

COleVariant::COleVariant(const COleCurrency& curSrc) {
  ::VariantInit(this);
  *this = curSrc;
}

COleVariant::COleVariant(float fltSrc) {
  ::VariantInit(this);
  vt = VT_R4;
  fltVal = fltSrc;
}

COleVariant::COleVariant(double dblSrc) {
  ::VariantInit(this);
  vt = VT_R8;
  dblVal = dblSrc;
}

COleVariant::COleVariant(const COleDateTime& timeSrc) {
  ::VariantInit(this);
  *this = timeSrc;
}

COleVariant::COleVariant(const CByteArray& arrSrc) {
  ::VariantInit(this);
  *this = arrSrc;
}

// VT_BSTR holds the text as Unicode; in an ANSI program VT_BSTRT keeps
// the ANSI bytes as they are.
void COleVariant::SetString(LPCTSTR lpszSrc, VARTYPE vtSrc) {
  Clear();
  vt = VT_BSTR;
  if (lpszSrc == NULL) {
    bstrVal = NULL;
    return;
  }
#ifndef _UNICODE
  if (vtSrc == VT_BSTRA) {
    bstrVal = ::SysAllocStringByteLen(lpszSrc, (UINT)lstrlenA(lpszSrc));
    if (bstrVal == NULL) AfxThrowMemoryException();
    return;
  }
#else
  (void)vtSrc;
#endif
  CStringW str(lpszSrc);
  bstrVal = ::SysAllocStringLen(str, (UINT)str.GetLength());
  if (bstrVal == NULL) AfxThrowMemoryException();
}

void COleVariant::ChangeType(VARTYPE vartype, LPVARIANT pSrc) {
  if (pSrc == NULL) pSrc = this;
  if (pSrc != this || vartype != vt) AfxCheckError(::VariantChangeType(this, pSrc, 0, vartype));
}

void COleVariant::Attach(VARIANT& varSrc) {
  Clear();
  memcpy((VARIANT*)this, &varSrc, sizeof(VARIANT));
  varSrc.vt = VT_EMPTY;
}

VARIANT COleVariant::Detach() {
  VARIANT varResult = *this;
  vt = VT_EMPTY;
  return varResult;
}

BOOL COleVariant::operator==(const VARIANT& var) const {
  if (&var == (const VARIANT*)this) return TRUE;
  if (var.vt != vt) return FALSE;
  switch (vt) {
    case VT_EMPTY:
    case VT_NULL: return TRUE;
    case VT_BOOL: return (boolVal != 0) == (var.boolVal != 0);
    case VT_UI1: return bVal == var.bVal;
    case VT_I2: return iVal == var.iVal;
    case VT_I4: return lVal == var.lVal;
    case VT_ERROR: return scode == var.scode;
    case VT_R4: return fltVal == var.fltVal;
    case VT_R8: return dblVal == var.dblVal;
    case VT_DATE: return date == var.date;
    case VT_CY: return cyVal.int64 == var.cyVal.int64;
    case VT_BSTR: {
      UINT n = ::SysStringByteLen(bstrVal);
      return n == ::SysStringByteLen(var.bstrVal) &&
             (n == 0 || memcmp(bstrVal, var.bstrVal, n) == 0);
    }
    case VT_DISPATCH:
    case VT_UNKNOWN: return punkVal == var.punkVal;
    default:
      if ((vt & VT_ARRAY) != 0 && (vt & VT_TYPEMASK) == VT_UI1) {
        long lo1, hi1, lo2, hi2;
        ::SafeArrayGetLBound(parray, 1, &lo1);
        ::SafeArrayGetUBound(parray, 1, &hi1);
        ::SafeArrayGetLBound(var.parray, 1, &lo2);
        ::SafeArrayGetUBound(var.parray, 1, &hi2);
        if (hi1 - lo1 != hi2 - lo2) return FALSE;
        void *p1, *p2;
        ::SafeArrayAccessData(parray, &p1);
        ::SafeArrayAccessData(var.parray, &p2);
        BOOL bEqual = memcmp(p1, p2, (size_t)(hi1 - lo1 + 1)) == 0;
        ::SafeArrayUnaccessData(var.parray);
        ::SafeArrayUnaccessData(parray);
        return bEqual;
      }
      return FALSE;
  }
}

const COleVariant& COleVariant::operator=(const VARIANT& varSrc) {
  if (&varSrc != (const VARIANT*)this) AfxCheckError(::VariantCopy(this, (VARIANT*)&varSrc));
  return *this;
}

const COleVariant& COleVariant::operator=(LPCTSTR lpszSrc) {
  SetString(lpszSrc, VT_BSTR);
  return *this;
}

const COleVariant& COleVariant::operator=(BYTE nSrc) {
  if (vt != VT_UI1) {
    Clear();
    vt = VT_UI1;
  }
  bVal = nSrc;
  return *this;
}

// A boolean stays a boolean; anything else becomes a short.
const COleVariant& COleVariant::operator=(short nSrc) {
  if (vt == VT_BOOL) {
    boolVal = nSrc ? VARIANT_TRUE : VARIANT_FALSE;
    return *this;
  }
  if (vt != VT_I2) {
    Clear();
    vt = VT_I2;
  }
  iVal = nSrc;
  return *this;
}

// A boolean or an error code keeps its type; anything else becomes a
// long.
const COleVariant& COleVariant::operator=(long lSrc) {
  if (vt == VT_BOOL) {
    boolVal = lSrc ? VARIANT_TRUE : VARIANT_FALSE;
  } else if (vt == VT_ERROR) {
    scode = lSrc;
  } else {
    if (vt != VT_I4) {
      Clear();
      vt = VT_I4;
    }
    lVal = lSrc;
  }
  return *this;
}

const COleVariant& COleVariant::operator=(const COleCurrency& curSrc) {
  Clear();
  if (curSrc.GetStatus() == COleCurrency::valid) {
    vt = VT_CY;
    cyVal = curSrc.m_cur;
  } else if (curSrc.GetStatus() == COleCurrency::null) {
    vt = VT_NULL;
  } else {
    vt = VT_ERROR;
    scode = DISP_E_TYPEMISMATCH;
  }
  return *this;
}

const COleVariant& COleVariant::operator=(float fltSrc) {
  if (vt != VT_R4) {
    Clear();
    vt = VT_R4;
  }
  fltVal = fltSrc;
  return *this;
}

const COleVariant& COleVariant::operator=(double dblSrc) {
  if (vt != VT_R8) {
    Clear();
    vt = VT_R8;
  }
  dblVal = dblSrc;
  return *this;
}

const COleVariant& COleVariant::operator=(const COleDateTime& dateSrc) {
  Clear();
  if (dateSrc.GetStatus() == COleDateTime::valid) {
    vt = VT_DATE;
    date = dateSrc.m_dt;
  } else if (dateSrc.GetStatus() == COleDateTime::null) {
    vt = VT_NULL;
  } else {
    vt = VT_ERROR;
    scode = DISP_E_TYPEMISMATCH;
  }
  return *this;
}

// A byte array becomes a one-dimensional SAFEARRAY of VT_UI1.
const COleVariant& COleVariant::operator=(const CByteArray& arrSrc) {
  Clear();
  INT_PTR nSize = arrSrc.GetSize();
  SAFEARRAYBOUND bound = {(ULONG)nSize, 0};
  parray = ::SafeArrayCreate(VT_UI1, 1, &bound);
  if (parray == NULL) AfxThrowMemoryException();
  vt = VT_ARRAY | VT_UI1;
  if (nSize > 0) {
    void* pData;
    AfxCheckError(::SafeArrayAccessData(parray, &pData));
    memcpy(pData, arrSrc.GetData(), (size_t)nSize);
    ::SafeArrayUnaccessData(parray);
  }
  return *this;
}

// The archive form: the VARTYPE as a WORD, then the value.
CArchive& AFXAPI operator<<(CArchive& ar, COleVariant varSrc) {
  ar << (WORD)varSrc.vt;
  switch (varSrc.vt) {
    case VT_EMPTY:
    case VT_NULL: break;
    case VT_BOOL: ar << (WORD)varSrc.boolVal; break;
    case VT_UI1: ar << varSrc.bVal; break;
    case VT_I2: ar << (WORD)varSrc.iVal; break;
    case VT_I4: ar << varSrc.lVal; break;
    case VT_ERROR: ar << (LONG)varSrc.scode; break;
    case VT_R4: ar << varSrc.fltVal; break;
    case VT_R8: ar << varSrc.dblVal; break;
    case VT_DATE: ar << varSrc.date; break;
    case VT_CY: ar << (LONG)varSrc.cyVal.Lo << (LONG)varSrc.cyVal.Hi; break;
    case VT_BSTR: ar << CString(varSrc.bstrVal != NULL ? varSrc.bstrVal : L""); break;
    default:
      if (varSrc.vt == (VT_ARRAY | VT_UI1)) {
        long lo, hi;
        ::SafeArrayGetLBound(varSrc.parray, 1, &lo);
        ::SafeArrayGetUBound(varSrc.parray, 1, &hi);
        DWORD n = (DWORD)(hi - lo + 1);
        ar << n;
        void* pData;
        ::SafeArrayAccessData(varSrc.parray, &pData);
        ar.Write(pData, n);
        ::SafeArrayUnaccessData(varSrc.parray);
        break;
      }
      AfxThrowNotSupportedException();
  }
  return ar;
}

CArchive& AFXAPI operator>>(CArchive& ar, COleVariant& varSrc) {
  varSrc.Clear();
  WORD w;
  ar >> w;
  VARTYPE vt = (VARTYPE)w;
  switch (vt) {
    case VT_EMPTY:
    case VT_NULL: varSrc.vt = vt; break;
    case VT_BOOL: ar >> w; varSrc.vt = VT_BOOL; varSrc.boolVal = (VARIANT_BOOL)w; break;
    case VT_UI1: ar >> varSrc.bVal; varSrc.vt = VT_UI1; break;
    case VT_I2: ar >> w; varSrc.vt = VT_I2; varSrc.iVal = (SHORT)w; break;
    case VT_I4: ar >> varSrc.lVal; varSrc.vt = VT_I4; break;
    case VT_ERROR: {
      LONG l;
      ar >> l;
      varSrc.vt = VT_ERROR;
      varSrc.scode = l;
      break;
    }
    case VT_R4: ar >> varSrc.fltVal; varSrc.vt = VT_R4; break;
    case VT_R8: ar >> varSrc.dblVal; varSrc.vt = VT_R8; break;
    case VT_DATE: ar >> varSrc.date; varSrc.vt = VT_DATE; break;
    case VT_CY: {
      LONG lo, hi;
      ar >> lo >> hi;
      varSrc.vt = VT_CY;
      varSrc.cyVal.Lo = (ULONG)lo;
      varSrc.cyVal.Hi = hi;
      break;
    }
    case VT_BSTR: {
      CString str;
      ar >> str;
      varSrc = (LPCTSTR)str;
      break;
    }
    default:
      if (vt == (VT_ARRAY | VT_UI1)) {
        DWORD n;
        ar >> n;
        CByteArray arr;
        arr.SetSize(n);
        if (n > 0) ar.Read(arr.GetData(), n);
        varSrc = arr;
        break;
      }
      AfxThrowNotSupportedException();
  }
  return ar;
}

CArchive& AFXAPI operator<<(CArchive& ar, COleCurrency curSrc) {
  return ar << (LONG)curSrc.m_status << (LONG)curSrc.m_cur.Lo << (LONG)curSrc.m_cur.Hi;
}

CArchive& AFXAPI operator>>(CArchive& ar, COleCurrency& curSrc) {
  LONG status, lo, hi;
  ar >> status >> lo >> hi;
  curSrc.m_status = (COleCurrency::CurrencyStatus)status;
  curSrc.m_cur.Lo = (ULONG)lo;
  curSrc.m_cur.Hi = hi;
  return ar;
}

CArchive& AFXAPI operator<<(CArchive& ar, COleDateTime dateSrc) {
  return ar << (LONG)dateSrc.m_status << dateSrc.m_dt;
}

CArchive& AFXAPI operator>>(CArchive& ar, COleDateTime& dateSrc) {
  LONG status;
  ar >> status >> dateSrc.m_dt;
  dateSrc.m_status = (COleDateTime::DateTimeStatus)status;
  return ar;
}

CArchive& AFXAPI operator<<(CArchive& ar, COleDateTimeSpan dateSpanSrc) {
  return ar << (LONG)dateSpanSrc.m_status << dateSpanSrc.m_span;
}

CArchive& AFXAPI operator>>(CArchive& ar, COleDateTimeSpan& dateSpanSrc) {
  LONG status;
  ar >> status >> dateSpanSrc.m_span;
  dateSpanSrc.m_status = (COleDateTimeSpan::DateTimeSpanStatus)status;
  return ar;
}

// ---------------------------------------------------------------------
// COleSafeArray

COleSafeArray::COleSafeArray(const SAFEARRAY& saSrc, VARTYPE vtSrc) {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  vt = (VARTYPE)(vtSrc | VT_ARRAY);
  AfxCheckError(::SafeArrayCopy((LPSAFEARRAY)&saSrc, &parray));
  m_dwDims = GetDim();
  m_dwElementSize = GetElemSize();
}

COleSafeArray::COleSafeArray(LPCSAFEARRAY pSrc, VARTYPE vtSrc) {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  vt = (VARTYPE)(vtSrc | VT_ARRAY);
  AfxCheckError(::SafeArrayCopy((LPSAFEARRAY)pSrc, &parray));
  m_dwDims = GetDim();
  m_dwElementSize = GetElemSize();
}

COleSafeArray::COleSafeArray(const COleSafeArray& saSrc) : tagVARIANT() {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  *this = saSrc;
}

COleSafeArray::COleSafeArray(const VARIANT& varSrc) {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  *this = varSrc;
}

COleSafeArray::COleSafeArray(LPCVARIANT pSrc) {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  *this = *pSrc;
}

COleSafeArray::COleSafeArray(const COleVariant& varSrc) {
  ::VariantInit(this);
  m_dwElementSize = m_dwDims = 0;
  *this = (const VARIANT&)varSrc;
}

const COleSafeArray& COleSafeArray::operator=(const VARIANT& varSrc) {
  if (!(varSrc.vt & VT_ARRAY)) AfxThrowInvalidArgException();
  if (&varSrc != (const VARIANT*)this) {
    Destroy();
    AfxCheckError(::VariantCopy(this, (VARIANT*)&varSrc));
  }
  m_dwDims = GetDim();
  m_dwElementSize = GetElemSize();
  return *this;
}

const COleSafeArray& COleSafeArray::operator=(const COleSafeArray& saSrc) {
  return *this = (const VARIANT&)saSrc;
}

void COleSafeArray::Attach(VARIANT& varSrc) {
  if (!(varSrc.vt & VT_ARRAY)) AfxThrowInvalidArgException();
  Destroy();
  memcpy((VARIANT*)this, &varSrc, sizeof(VARIANT));
  varSrc.vt = VT_EMPTY;
  m_dwDims = GetDim();
  m_dwElementSize = GetElemSize();
}

VARIANT COleSafeArray::Detach() {
  VARIANT varResult = *this;
  vt = VT_EMPTY;
  m_dwDims = m_dwElementSize = 0;
  return varResult;
}

void COleSafeArray::Create(VARTYPE vtSrc, DWORD dwDims, DWORD* rgElements) {
  SAFEARRAYBOUND* pBounds = new SAFEARRAYBOUND[dwDims];
  for (DWORD i = 0; i < dwDims; i++) {
    pBounds[i].cElements = rgElements[i];
    pBounds[i].lLbound = 0;
  }
  try {
    Create(vtSrc, dwDims, pBounds);
  } catch (...) {
    delete[] pBounds;
    throw;
  }
  delete[] pBounds;
}

void COleSafeArray::Create(VARTYPE vtSrc, DWORD dwDims, SAFEARRAYBOUND* rgsabounds) {
  Destroy();
  parray = ::SafeArrayCreate(vtSrc, dwDims, rgsabounds);
  if (parray == NULL) AfxThrowMemoryException();
  vt = (VARTYPE)(vtSrc | VT_ARRAY);
  m_dwDims = dwDims;
  m_dwElementSize = GetElemSize();
}

void COleSafeArray::CreateOneDim(VARTYPE vtSrc, DWORD dwElements, const void* pvSrcData,
                                 long nLBound) {
  SAFEARRAYBOUND bound = {dwElements, nLBound};
  Create(vtSrc, 1, &bound);
  if (pvSrcData != NULL && dwElements > 0) {
    void* pvData;
    AccessData(&pvData);
    memcpy(pvData, pvSrcData, (size_t)GetElemSize() * dwElements);
    UnaccessData();
  }
}

void COleSafeArray::Destroy() {
  if (vt != VT_EMPTY) {
    AfxCheckError(::VariantClear(this));
    vt = VT_EMPTY;
  }
  m_dwDims = m_dwElementSize = 0;
}

void COleSafeArray::DestroyData() { AfxCheckError(::SafeArrayDestroyData(parray)); }
void COleSafeArray::DestroyDescriptor() { AfxCheckError(::SafeArrayDestroyDescriptor(parray)); }

DWORD COleSafeArray::GetOneDimSize() {
  long lo, hi;
  GetLBound(1, &lo);
  GetUBound(1, &hi);
  return (DWORD)(hi - lo + 1);
}

void COleSafeArray::ResizeOneDim(DWORD dwElements) {
  long lo;
  GetLBound(1, &lo);
  SAFEARRAYBOUND bound = {dwElements, lo};
  Redim(&bound);
}

void COleSafeArray::Redim(SAFEARRAYBOUND* psaboundNew) {
  AfxCheckError(::SafeArrayRedim(parray, psaboundNew));
}

void COleSafeArray::AccessData(void** ppvData) {
  AfxCheckError(::SafeArrayAccessData(parray, ppvData));
}

void COleSafeArray::UnaccessData() { AfxCheckError(::SafeArrayUnaccessData(parray)); }
void COleSafeArray::AllocData() { AfxCheckError(::SafeArrayAllocData(parray)); }

void COleSafeArray::AllocDescriptor(DWORD dwDims) {
  AfxCheckError(::SafeArrayAllocDescriptor(dwDims, &parray));
}

void COleSafeArray::Copy(LPSAFEARRAY* ppsa) { AfxCheckError(::SafeArrayCopy(parray, ppsa)); }

void COleSafeArray::GetLBound(DWORD dwDim, long* pLBound) {
  AfxCheckError(::SafeArrayGetLBound(parray, dwDim, pLBound));
}

void COleSafeArray::GetUBound(DWORD dwDim, long* pUBound) {
  AfxCheckError(::SafeArrayGetUBound(parray, dwDim, pUBound));
}

void COleSafeArray::GetElement(long* rgIndices, void* pvData) {
  AfxCheckError(::SafeArrayGetElement(parray, rgIndices, pvData));
}

void COleSafeArray::PtrOfIndex(long* rgIndices, void** ppvData) {
  AfxCheckError(::SafeArrayPtrOfIndex(parray, rgIndices, ppvData));
}

void COleSafeArray::PutElement(long* rgIndices, void* pvData) {
  AfxCheckError(::SafeArrayPutElement(parray, rgIndices, pvData));
}

void COleSafeArray::Lock() { AfxCheckError(::SafeArrayLock(parray)); }
void COleSafeArray::Unlock() { AfxCheckError(::SafeArrayUnlock(parray)); }

DWORD COleSafeArray::GetDim() { return (vt & VT_ARRAY) ? ::SafeArrayGetDim(parray) : 0; }

DWORD COleSafeArray::GetElemSize() { return (vt & VT_ARRAY) ? ::SafeArrayGetElemsize(parray) : 0; }

// ---------------------------------------------------------------------
// Dialog data exchange

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, COleDateTime& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    int nLen = ::GetWindowTextLength(hWndCtrl);
    CString str;
    ::GetWindowText(hWndCtrl, str.GetBuffer(nLen + 1), nLen + 1);
    str.ReleaseBuffer();
    if (!value.ParseDateTime(str)) {
      AfxMessageBox(AFX_IDP_PARSE_DATETIME, MB_ICONEXCLAMATION);
      pDX->Fail();
    }
  } else {
    ::SetWindowText(hWndCtrl, value.Format());
  }
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, COleCurrency& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    int nLen = ::GetWindowTextLength(hWndCtrl);
    CString str;
    ::GetWindowText(hWndCtrl, str.GetBuffer(nLen + 1), nLen + 1);
    str.ReleaseBuffer();
    if (!value.ParseCurrency(str)) {
      AfxMessageBox(AFX_IDP_PARSE_CURRENCY, MB_ICONEXCLAMATION);
      pDX->Fail();
    }
  } else {
    ::SetWindowText(hWndCtrl, value.Format());
  }
}

void AFXAPI DDX_DateTimeCtrl(CDataExchange* pDX, int nIDC, COleDateTime& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  SYSTEMTIME st;
  if (pDX->m_bSaveAndValidate) {
    LRESULT r = ::SendMessage(hWndCtrl, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
    if (r == GDT_VALID)
      value = st;
    else
      value.SetStatus(COleDateTime::null);
  } else if (value.GetStatus() == COleDateTime::valid && value.GetAsSystemTime(st)) {
    ::SendMessage(hWndCtrl, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
  } else {
    ::SendMessage(hWndCtrl, DTM_SETSYSTEMTIME, GDT_NONE, 0);
  }
}

void AFXAPI DDX_MonthCalCtrl(CDataExchange* pDX, int nIDC, COleDateTime& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  SYSTEMTIME st;
  if (pDX->m_bSaveAndValidate) {
    memset(&st, 0, sizeof(st));
    if (::SendMessage(hWndCtrl, MCM_GETCURSEL, 0, (LPARAM)&st)) {
      st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
      value = st;
    } else {
      value.SetStatus(COleDateTime::invalid);
    }
  } else if (value.GetAsSystemTime(st)) {
    ::SendMessage(hWndCtrl, MCM_SETCURSEL, 0, (LPARAM)&st);
  }
}

void AFXAPI DDV_MinMaxDateTime(CDataExchange* pDX, COleDateTime& refValue,
                               const COleDateTime& refMinRange, const COleDateTime& refMaxRange) {
  if (!pDX->m_bSaveAndValidate) return;
  if (refValue.GetStatus() != COleDateTime::valid) return;
  if (refValue < refMinRange || refValue > refMaxRange) {
    AfxMessageBox(AFX_IDP_PARSE_DATETIME, MB_ICONEXCLAMATION);
    pDX->Fail();
  }
}

void AFXAPI DDV_MinMaxMonth(CDataExchange* pDX, COleDateTime& refValue,
                            const COleDateTime& refMinRange, const COleDateTime& refMaxRange) {
  DDV_MinMaxDateTime(pDX, refValue, refMinRange, refMaxRange);
}

// ---------------------------------------------------------------------
// The date and calendar controls with dates

BOOL CDateTimeCtrl::GetTime(COleDateTime& timeDest) const {
  SYSTEMTIME st;
  DWORD dw = (DWORD)::SendMessage(m_hWnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
  if (dw == GDT_VALID) {
    timeDest = st;
    return TRUE;
  }
  timeDest.SetStatus(dw == GDT_NONE ? COleDateTime::null : COleDateTime::invalid);
  return dw == GDT_NONE;
}

BOOL CDateTimeCtrl::SetTime(const COleDateTime& timeNew) {
  SYSTEMTIME st;
  if (timeNew.GetStatus() == COleDateTime::valid && timeNew.GetAsSystemTime(st))
    return (BOOL)::SendMessage(m_hWnd, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
  if (timeNew.GetStatus() == COleDateTime::null)
    return (BOOL)::SendMessage(m_hWnd, DTM_SETSYSTEMTIME, GDT_NONE, 0);
  return FALSE;
}

DWORD CDateTimeCtrl::GetRange(COleDateTime* pMinRange, COleDateTime* pMaxRange) const {
  SYSTEMTIME st[2];
  memset(st, 0, sizeof(st));
  DWORD dw = (DWORD)::SendMessage(m_hWnd, DTM_GETRANGE, 0, (LPARAM)st);
  if (pMinRange != NULL) {
    if (dw & GDTR_MIN)
      *pMinRange = st[0];
    else
      pMinRange->SetStatus(COleDateTime::null);
  }
  if (pMaxRange != NULL) {
    if (dw & GDTR_MAX)
      *pMaxRange = st[1];
    else
      pMaxRange->SetStatus(COleDateTime::null);
  }
  return dw;
}

BOOL CDateTimeCtrl::SetRange(const COleDateTime* pMinRange, const COleDateTime* pMaxRange) {
  SYSTEMTIME st[2];
  memset(st, 0, sizeof(st));
  DWORD dwFlags = 0;
  if (pMinRange != NULL && pMinRange->GetStatus() == COleDateTime::valid &&
      pMinRange->GetAsSystemTime(st[0]))
    dwFlags |= GDTR_MIN;
  if (pMaxRange != NULL && pMaxRange->GetStatus() == COleDateTime::valid &&
      pMaxRange->GetAsSystemTime(st[1]))
    dwFlags |= GDTR_MAX;
  return (BOOL)::SendMessage(m_hWnd, DTM_SETRANGE, dwFlags, (LPARAM)st);
}

BOOL CMonthCalCtrl::GetCurSel(COleDateTime& refDateTime) const {
  SYSTEMTIME st;
  memset(&st, 0, sizeof(st));
  if (!::SendMessage(m_hWnd, MCM_GETCURSEL, 0, (LPARAM)&st)) return FALSE;
  st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
  refDateTime = st;
  return TRUE;
}

BOOL CMonthCalCtrl::SetCurSel(const COleDateTime& refDateTime) {
  SYSTEMTIME st;
  if (!refDateTime.GetAsSystemTime(st)) return FALSE;
  return (BOOL)::SendMessage(m_hWnd, MCM_SETCURSEL, 0, (LPARAM)&st);
}

BOOL CMonthCalCtrl::GetToday(COleDateTime& refDateTime) const {
  SYSTEMTIME st;
  memset(&st, 0, sizeof(st));
  if (!::SendMessage(m_hWnd, MCM_GETTODAY, 0, (LPARAM)&st)) return FALSE;
  st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
  refDateTime = st;
  return TRUE;
}

void CMonthCalCtrl::SetToday(const COleDateTime& refDateTime) {
  SYSTEMTIME st;
  if (refDateTime.GetAsSystemTime(st)) ::SendMessage(m_hWnd, MCM_SETTODAY, 0, (LPARAM)&st);
}
