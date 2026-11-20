// atlcomtime.h - COleDateTime and COleDateTimeSpan: OLE's DATE (days
// since 30 December 1899, the time of day as the fraction) and a length
// of time in days. In the headers so that ATL programs have them without
// MFC; MFC's afxdisp.h takes them from here.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCOMTIME_H__
#define __ATLCOMTIME_H__

#include <atltime.h>
#include <oleauto.h>
#include <math.h>

#ifdef __AFX_H__
typedef ::CStringW _AtlTimeStringW;
#else
typedef ::ATL::CStringW _AtlTimeStringW;
#endif

class COleDateTime;
class COleDateTimeSpan;

// ---------------------------------------------------------------------
// COleDateTimeSpan: a length of time in days.

class COleDateTimeSpan {
public:
  enum DateTimeSpanStatus { valid = 0, invalid = 1, null = 2 };

  COleDateTimeSpan() : m_span(0), m_status(valid) {}
  COleDateTimeSpan(double dblSpanSrc) : m_span(dblSpanSrc), m_status(valid) {}
  COleDateTimeSpan(long lDays, int nHours, int nMins, int nSecs) {
    SetDateTimeSpan(lDays, nHours, nMins, nSecs);
  }

  double m_span;
  DateTimeSpanStatus m_status;

  void SetStatus(DateTimeSpanStatus status) { m_status = status; }
  DateTimeSpanStatus GetStatus() const { return m_status; }

  double GetTotalDays() const { return m_span; }
  double GetTotalHours() const { return m_span * 24; }
  double GetTotalMinutes() const { return m_span * 24 * 60; }
  double GetTotalSeconds() const { return m_span * 24 * 60 * 60; }
  long GetDays() const { return (long)m_span; }
  long GetHours() const;
  long GetMinutes() const;
  long GetSeconds() const;

  const COleDateTimeSpan& operator=(double dblSpanSrc) {
    m_span = dblSpanSrc;
    m_status = valid;
    return *this;
  }
  BOOL operator==(const COleDateTimeSpan& s) const {
    return m_status == s.m_status && m_span == s.m_span;
  }
  BOOL operator!=(const COleDateTimeSpan& s) const { return !(*this == s); }
  BOOL operator<(const COleDateTimeSpan& s) const { return m_span < s.m_span; }
  BOOL operator>(const COleDateTimeSpan& s) const { return m_span > s.m_span; }
  BOOL operator<=(const COleDateTimeSpan& s) const { return m_span <= s.m_span; }
  BOOL operator>=(const COleDateTimeSpan& s) const { return m_span >= s.m_span; }

  COleDateTimeSpan operator+(const COleDateTimeSpan& s) const;
  COleDateTimeSpan operator-(const COleDateTimeSpan& s) const;
  const COleDateTimeSpan& operator+=(const COleDateTimeSpan& s) { return *this = *this + s; }
  const COleDateTimeSpan& operator-=(const COleDateTimeSpan& s) { return *this = *this - s; }
  COleDateTimeSpan operator-() const { return COleDateTimeSpan(-m_span); }
  operator double() const { return m_span; }

  void SetDateTimeSpan(long lDays, int nHours, int nMins, int nSecs);

  // %D days, %H hours, %M minutes, %S seconds, %% a percent sign.
  _AtlTimeString Format(LPCTSTR lpszFormat) const;
  _AtlTimeString Format(UINT nID) const;
};

// ---------------------------------------------------------------------
// COleDateTime: a DATE, days since 30 December 1899 with the time as the
// fraction, valid from the year 100 to 9999.

class COleDateTime {
public:
  enum DateTimeStatus { valid = 0, invalid = 1, null = 2 };

  static COleDateTime PASCAL GetCurrentTime();

  COleDateTime() : m_dt(0), m_status(valid) {}
  COleDateTime(const VARIANT& varSrc);
  COleDateTime(DATE dtSrc) : m_dt(dtSrc), m_status(valid) {}
  COleDateTime(time_t timeSrc);
  COleDateTime(const SYSTEMTIME& systimeSrc);
  COleDateTime(const FILETIME& filetimeSrc);
  COleDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec) {
    SetDateTime(nYear, nMonth, nDay, nHour, nMin, nSec);
  }
  COleDateTime(WORD wDosDate, WORD wDosTime);

  DATE m_dt;
  DateTimeStatus m_status;

  void SetStatus(DateTimeStatus status) { m_status = status; }
  DateTimeStatus GetStatus() const { return m_status; }

  BOOL GetAsSystemTime(SYSTEMTIME& st) const;
  int GetYear() const;
  int GetMonth() const;  // 1 = January
  int GetDay() const;
  int GetHour() const;
  int GetMinute() const;
  int GetSecond() const;
  int GetDayOfWeek() const;  // 1 = Sunday
  int GetDayOfYear() const;  // 1 = 1 January

  const COleDateTime& operator=(const VARIANT& varSrc);
  const COleDateTime& operator=(DATE dtSrc);
  const COleDateTime& operator=(const time_t& timeSrc);
  const COleDateTime& operator=(const SYSTEMTIME& systimeSrc);
  const COleDateTime& operator=(const FILETIME& filetimeSrc);

  BOOL operator==(const COleDateTime& date) const {
    return m_status == date.m_status && m_dt == date.m_dt;
  }
  BOOL operator!=(const COleDateTime& date) const { return !(*this == date); }
  // Negative dates count their time forwards, so compare as real time.
  BOOL operator<(const COleDateTime& date) const;
  BOOL operator>(const COleDateTime& date) const;
  BOOL operator<=(const COleDateTime& date) const;
  BOOL operator>=(const COleDateTime& date) const;

  COleDateTime operator+(const COleDateTimeSpan& dateSpan) const;
  COleDateTime operator-(const COleDateTimeSpan& dateSpan) const;
  const COleDateTime& operator+=(const COleDateTimeSpan& s) { return *this = *this + s; }
  const COleDateTime& operator-=(const COleDateTimeSpan& s) { return *this = *this - s; }
  COleDateTimeSpan operator-(const COleDateTime& date) const;
  operator DATE() const { return m_dt; }

  int SetDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec);
  int SetDate(int nYear, int nMonth, int nDay) { return SetDateTime(nYear, nMonth, nDay, 0, 0, 0); }
  int SetTime(int nHour, int nMin, int nSec) { return SetDateTime(1899, 12, 30, nHour, nMin, nSec); }

  // dwFlags: LOCALE_NOUSEROVERRIDE, VAR_TIMEVALUEONLY, VAR_DATEVALUEONLY.
  BOOL ParseDateTime(LPCTSTR lpszDate, DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT);
  _AtlTimeString Format(DWORD dwFlags = 0, LCID lcid = LANG_USER_DEFAULT) const;
  _AtlTimeString Format(LPCTSTR lpszFormat) const;  // strftime codes
  _AtlTimeString Format(UINT nFormatID) const;
};

// ---------------------------------------------------------------------
// Dates. A DATE counts days from 30 December 1899; the fraction is the
// time of day, which for a negative date still counts forward from
// midnight (so -1.25 is 29 December 1899, 6 a.m.).

#ifndef AFX_MIN_DATE
#define AFX_MIN_DATE (-657434L)  // 1 January 100
#define AFX_MAX_DATE 2958465L    // 31 December 9999
#endif

// Days from 1 January 1970 in the proleptic Gregorian calendar.
inline long _AfxDaysFromCivil(long y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline void _AfxCivilFromDays(long z, int& y, int& m, int& d) {
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

const long _afxDaysTo1970 = 25569;  // 30 Dec 1899 to 1 Jan 1970

inline int _AfxDaysInMonth(int y, int m) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 29;
  return days[m - 1];
}

inline BOOL _AfxOleDateFromParts(int y, int mo, int d, int h, int mi, int s, DATE& dt) {
  if (y < 100 || y > 9999 || mo < 1 || mo > 12) return FALSE;
  if (d < 1 || d > _AfxDaysInMonth(y, mo)) return FALSE;
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return FALSE;
  long nDays = _AfxDaysFromCivil(y, mo, d) + _afxDaysTo1970;
  double dblTime = (h * 3600L + mi * 60L + s) / 86400.0;
  dt = nDays >= 0 ? nDays + dblTime : nDays - dblTime;
  return TRUE;
}

// The calendar fields of a date, rounded to the second.
inline BOOL _AfxTmFromOleDate(DATE dt, struct tm& t) {
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
inline double _AfxDoubleFromDate(DATE dt) {
  if (dt >= 0) return dt;
  double dblDays = ceil(dt);
  return dblDays - (dt - dblDays);
}

inline DATE _AfxDateFromDouble(double dbl) {
  if (dbl >= 0) return dbl;
  double dblDays = floor(dbl);
  return dblDays - (dbl - dblDays);
}
inline COleDateTime PASCAL COleDateTime::GetCurrentTime() {
  SYSTEMTIME st;
  ::GetLocalTime(&st);
  st.wMilliseconds = 0;
  return COleDateTime(st);
}

inline COleDateTime::COleDateTime(const VARIANT& varSrc) { *this = varSrc; }
inline COleDateTime::COleDateTime(time_t timeSrc) { *this = timeSrc; }
inline COleDateTime::COleDateTime(const SYSTEMTIME& systimeSrc) { *this = systimeSrc; }
inline COleDateTime::COleDateTime(const FILETIME& filetimeSrc) { *this = filetimeSrc; }

inline COleDateTime::COleDateTime(WORD wDosDate, WORD wDosTime) {
  FILETIME ft;
  SYSTEMTIME st;
  if (::DosDateTimeToFileTime(wDosDate, wDosTime, &ft) && ::FileTimeToSystemTime(&ft, &st)) {
    *this = st;
  } else {
    m_dt = 0;
    m_status = invalid;
  }
}

inline const COleDateTime& COleDateTime::operator=(const VARIANT& varSrc) {
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

inline const COleDateTime& COleDateTime::operator=(DATE dtSrc) {
  m_dt = dtSrc;
  m_status = valid;
  return *this;
}

// A time_t is local time, as CTime shows it.
inline const COleDateTime& COleDateTime::operator=(const time_t& timeSrc) {
  SYSTEMTIME st;
  if (CTime(timeSrc).GetAsSystemTime(st))
    *this = st;
  else
    m_status = invalid;
  return *this;
}

inline const COleDateTime& COleDateTime::operator=(const SYSTEMTIME& st) {
  m_status = _AfxOleDateFromParts(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                                  st.wSecond, m_dt)
                 ? valid
                 : invalid;
  return *this;
}

// A FILETIME is UTC; the date is local time.
inline const COleDateTime& COleDateTime::operator=(const FILETIME& filetimeSrc) {
  FILETIME ftLocal;
  SYSTEMTIME st;
  if (::FileTimeToLocalFileTime(&filetimeSrc, &ftLocal) && ::FileTimeToSystemTime(&ftLocal, &st))
    *this = st;
  else
    m_status = invalid;
  return *this;
}

inline int COleDateTime::SetDateTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec) {
  m_status = _AfxOleDateFromParts(nYear, nMonth, nDay, nHour, nMin, nSec, m_dt) ? valid : invalid;
  return m_status == valid ? 0 : 1;
}

inline BOOL COleDateTime::GetAsSystemTime(SYSTEMTIME& st) const {
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
  inline int COleDateTime::name() const { \
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

inline BOOL COleDateTime::operator<(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) < _AfxDoubleFromDate(date.m_dt);
}
inline BOOL COleDateTime::operator>(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) > _AfxDoubleFromDate(date.m_dt);
}
inline BOOL COleDateTime::operator<=(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) <= _AfxDoubleFromDate(date.m_dt);
}
inline BOOL COleDateTime::operator>=(const COleDateTime& date) const {
  return _AfxDoubleFromDate(m_dt) >= _AfxDoubleFromDate(date.m_dt);
}

inline COleDateTime COleDateTime::operator+(const COleDateTimeSpan& dateSpan) const {
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

inline COleDateTime COleDateTime::operator-(const COleDateTimeSpan& dateSpan) const {
  return *this + (-dateSpan);
}

inline COleDateTimeSpan COleDateTime::operator-(const COleDateTime& date) const {
  COleDateTimeSpan span;
  if (m_status != valid || date.m_status != valid) {
    span.m_status = (m_status == null || date.m_status == null) ? COleDateTimeSpan::null
                                                                : COleDateTimeSpan::invalid;
    return span;
  }
  span.m_span = _AfxDoubleFromDate(m_dt) - _AfxDoubleFromDate(date.m_dt);
  return span;
}

inline _AtlTimeString _AtlBstrToTimeString(BSTR bstr) {
  _AtlTimeString str(bstr != NULL ? bstr : L"");
  return str;
}

inline BOOL COleDateTime::ParseDateTime(LPCTSTR lpszDate, DWORD dwFlags, LCID lcid) {
  _AtlTimeStringW strDate(lpszDate);
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

inline _AtlTimeString COleDateTime::Format(DWORD dwFlags, LCID lcid) const {
  if (m_status == null) return _AtlTimeString();
  if (m_status == invalid) return _AtlTimeString(_T("Invalid DateTime"));
  BSTR bstr = NULL;
  if (FAILED(::VarBstrFromDate(m_dt, lcid, dwFlags, &bstr))) return _AtlTimeString();
  _AtlTimeString str = _AtlBstrToTimeString(bstr);
  ::SysFreeString(bstr);
  return str;
}

inline _AtlTimeString COleDateTime::Format(LPCTSTR lpszFormat) const {
  if (m_status == null) return _AtlTimeString();
  struct tm t;
  if (m_status == invalid || !_AfxTmFromOleDate(m_dt, t)) return _AtlTimeString(_T("Invalid DateTime"));
  return _AtlTime::FormatTm(lpszFormat, t);
}

inline _AtlTimeString COleDateTime::Format(UINT nFormatID) const {
  _AtlTimeString strFormat = _AtlTime::LoadFormat(nFormatID);
  return Format((LPCTSTR)strFormat);
}

// ---------------------------------------------------------------------
// COleDateTimeSpan

inline LONGLONG _AtlSpanSeconds(double dblSpan) {
  return (LONGLONG)(dblSpan >= 0 ? floor(dblSpan * 86400 + 0.5) : ceil(dblSpan * 86400 - 0.5));
}

inline long COleDateTimeSpan::GetHours() const { return (long)(_AtlSpanSeconds(m_span) / 3600 % 24); }
inline long COleDateTimeSpan::GetMinutes() const { return (long)(_AtlSpanSeconds(m_span) / 60 % 60); }
inline long COleDateTimeSpan::GetSeconds() const { return (long)(_AtlSpanSeconds(m_span) % 60); }

inline COleDateTimeSpan COleDateTimeSpan::operator+(const COleDateTimeSpan& s) const {
  COleDateTimeSpan r;
  if (m_status != valid || s.m_status != valid) {
    r.m_status = (m_status == null || s.m_status == null) ? null : invalid;
    return r;
  }
  r.m_span = m_span + s.m_span;
  return r;
}

inline COleDateTimeSpan COleDateTimeSpan::operator-(const COleDateTimeSpan& s) const {
  COleDateTimeSpan r;
  if (m_status != valid || s.m_status != valid) {
    r.m_status = (m_status == null || s.m_status == null) ? null : invalid;
    return r;
  }
  r.m_span = m_span - s.m_span;
  return r;
}

inline void COleDateTimeSpan::SetDateTimeSpan(long lDays, int nHours, int nMins, int nSecs) {
  m_span = lDays + ((double)nHours) / 24 + ((double)nMins) / (24 * 60) +
           ((double)nSecs) / (24 * 60 * 60);
  m_status = valid;
}

inline _AtlTimeString COleDateTimeSpan::Format(LPCTSTR lpszFormat) const {
  if (m_status == null) return _AtlTimeString();
  if (m_status == invalid) return _AtlTimeString(_T("Invalid DateTimeSpan"));
  _AtlTimeString r;
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

inline _AtlTimeString COleDateTimeSpan::Format(UINT nID) const {
  _AtlTimeString strFormat = _AtlTime::LoadFormat(nID);
  return Format((LPCTSTR)strFormat);
}


namespace ATL {
using ::COleDateTime;
using ::COleDateTimeSpan;
}

#endif  // __ATLCOMTIME_H__
