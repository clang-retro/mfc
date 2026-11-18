// atltime.h - CTime and CTimeSpan (seconds, as time_t counts them) and
// CFileTime and CFileTimeSpan (100-nanosecond FILETIME units), in the
// headers so that programs without MFC have them too. MFC's afx.h takes
// its CTime from here.
//
// Times are seconds since 1970-01-01 UTC, kept in 64 bits. The conversions
// use Win32's calendar functions rather than the C runtime's, so that they
// behave the same with every C library.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLTIME_H__
#define __ATLTIME_H__

#ifndef __AFX_H__
#include <atlstr.h>
#endif
#include <windows.h>
#include <time.h>
#include <string.h>

// The string the Format functions return: MFC's CString, or ATL's.
#ifdef __AFX_H__
typedef ::CString _AtlTimeString;
#else
typedef ::ATL::CString _AtlTimeString;
#endif

struct _AtlTime {
  static const LONGLONG kEpochFt = 116444736000000000LL;  // 1970 in FILETIME units

  static void ThrowInvalidArg() {
#ifdef __AFX_H__
    AfxThrowInvalidArgException();
#else
    AtlThrow(E_INVALIDARG);  // a macro, as in ATL
#endif
  }
  static __time64_t FromFileTime(const FILETIME& ftUtc) {
    ULARGE_INTEGER u;
    u.LowPart = ftUtc.dwLowDateTime;
    u.HighPart = ftUtc.dwHighDateTime;
    return (__time64_t)(((LONGLONG)u.QuadPart - kEpochFt) / 10000000);
  }
  static FILETIME ToFileTime(__time64_t t) {
    ULARGE_INTEGER u;
    u.QuadPart = (ULONGLONG)(t * 10000000 + kEpochFt);
    FILETIME ft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    return ft;
  }
  static __time64_t FromLocalSystemTime(const SYSTEMTIME& stLocal) {
    FILETIME ftLocal, ftUtc;
    if (!SystemTimeToFileTime(&stLocal, &ftLocal)) ThrowInvalidArg();
    LocalFileTimeToFileTime(&ftLocal, &ftUtc);
    return FromFileTime(ftUtc);
  }
  static void ToLocalSystemTime(__time64_t t, SYSTEMTIME& st) {
    FILETIME ft = ToFileTime(t), ftLocal;
    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
  }
  static void ToTm(const SYSTEMTIME& st, struct tm* ptm) {
    memset(ptm, 0, sizeof *ptm);
    ptm->tm_year = st.wYear - 1900;
    ptm->tm_mon = st.wMonth - 1;
    ptm->tm_mday = st.wDay;
    ptm->tm_hour = st.wHour;
    ptm->tm_min = st.wMinute;
    ptm->tm_sec = st.wSecond;
    ptm->tm_wday = st.wDayOfWeek;
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int y = st.wYear;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    ptm->tm_yday = cum[st.wMonth - 1] + st.wDay - 1 + (leap && st.wMonth > 2 ? 1 : 0);
    ptm->tm_isdst = -1;
  }

  // strftime with the codes MFC and ATL programs use, done here so that it
  // does not depend on the C library's locale support.
  static _AtlTimeString FormatTm(LPCTSTR pszFormat, const struct tm& t) {
    static const LPCTSTR days[] = {TEXT("Sunday"), TEXT("Monday"), TEXT("Tuesday"),
                                   TEXT("Wednesday"), TEXT("Thursday"), TEXT("Friday"),
                                   TEXT("Saturday")};
    static const LPCTSTR months[] = {TEXT("January"), TEXT("February"), TEXT("March"),
                                     TEXT("April"), TEXT("May"), TEXT("June"),
                                     TEXT("July"), TEXT("August"), TEXT("September"),
                                     TEXT("October"), TEXT("November"), TEXT("December")};
    _AtlTimeString r;
    for (LPCTSTR p = pszFormat; *p; p++) {
      if (*p != '%') {
        r += *p;
        continue;
      }
      p++;
      bool alt = false;
      if (*p == '#') { alt = true; p++; }
      LPCTSTR d2 = alt ? TEXT("%d") : TEXT("%02d");
      switch (*p) {
      case 'a': r += _AtlTimeString(days[t.tm_wday]).Left(3); break;
      case 'A': r += days[t.tm_wday]; break;
      case 'b': r += _AtlTimeString(months[t.tm_mon]).Left(3); break;
      case 'B': r += months[t.tm_mon]; break;
      case 'c':
        r.AppendFormat(TEXT("%02d/%02d/%02d %02d:%02d:%02d"), t.tm_mon + 1, t.tm_mday,
                       t.tm_year % 100, t.tm_hour, t.tm_min, t.tm_sec);
        break;
      case 'd': r.AppendFormat(d2, t.tm_mday); break;
      case 'H': r.AppendFormat(d2, t.tm_hour); break;
      case 'I': r.AppendFormat(d2, t.tm_hour % 12 ? t.tm_hour % 12 : 12); break;
      case 'j': r.AppendFormat(alt ? TEXT("%d") : TEXT("%03d"), t.tm_yday + 1); break;
      case 'm': r.AppendFormat(d2, t.tm_mon + 1); break;
      case 'M': r.AppendFormat(d2, t.tm_min); break;
      case 'p': r += t.tm_hour < 12 ? TEXT("AM") : TEXT("PM"); break;
      case 'S': r.AppendFormat(d2, t.tm_sec); break;
      case 'U': r.AppendFormat(d2, (t.tm_yday + 7 - t.tm_wday) / 7); break;
      case 'w': r.AppendFormat(TEXT("%d"), t.tm_wday); break;
      case 'W': r.AppendFormat(d2, (t.tm_yday + 7 - (t.tm_wday + 6) % 7) / 7); break;
      case 'x':
        r.AppendFormat(TEXT("%02d/%02d/%02d"), t.tm_mon + 1, t.tm_mday, t.tm_year % 100);
        break;
      case 'X': r.AppendFormat(TEXT("%02d:%02d:%02d"), t.tm_hour, t.tm_min, t.tm_sec); break;
      case 'y': r.AppendFormat(d2, t.tm_year % 100); break;
      case 'Y': r.AppendFormat(TEXT("%d"), t.tm_year + 1900); break;
      case 'z':
      case 'Z': {
        TIME_ZONE_INFORMATION tzi;
        DWORD k = GetTimeZoneInformation(&tzi);
        r += _AtlTimeString(k == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightName : tzi.StandardName);
        break;
      }
      case '%': r += TEXT('%'); break;
      case 0: return r;
      default: break;
      }
    }
    return r;
  }
  static _AtlTimeString LoadFormat(UINT nID) {
    _AtlTimeString f;
    f.LoadString(nID);
    return f;
  }
};

// ---------------------------------------------------------------------
// CTimeSpan and CTime

class CTimeSpan {
public:
  CTimeSpan() : m_timeSpan(0) {}
  CTimeSpan(__time64_t time) : m_timeSpan(time) {}
  CTimeSpan(LONG lDays, int nHours, int nMins, int nSecs)
      : m_timeSpan(nSecs + 60 * (nMins + 60 * (nHours + (__time64_t)24 * lDays))) {}
  LONGLONG GetDays() const { return m_timeSpan / (24 * 3600); }
  LONGLONG GetTotalHours() const { return m_timeSpan / 3600; }
  LONG GetHours() const { return (LONG)(GetTotalHours() - GetDays() * 24); }
  LONGLONG GetTotalMinutes() const { return m_timeSpan / 60; }
  LONG GetMinutes() const { return (LONG)(GetTotalMinutes() - GetTotalHours() * 60); }
  LONGLONG GetTotalSeconds() const { return m_timeSpan; }
  LONG GetSeconds() const { return (LONG)(GetTotalSeconds() - GetTotalMinutes() * 60); }
  __time64_t GetTimeSpan() const { return m_timeSpan; }
  CTimeSpan operator+(CTimeSpan s) const { return CTimeSpan(m_timeSpan + s.m_timeSpan); }
  CTimeSpan operator-(CTimeSpan s) const { return CTimeSpan(m_timeSpan - s.m_timeSpan); }
  CTimeSpan& operator+=(CTimeSpan s) { m_timeSpan += s.m_timeSpan; return *this; }
  CTimeSpan& operator-=(CTimeSpan s) { m_timeSpan -= s.m_timeSpan; return *this; }
  bool operator==(CTimeSpan s) const { return m_timeSpan == s.m_timeSpan; }
  bool operator!=(CTimeSpan s) const { return m_timeSpan != s.m_timeSpan; }
  bool operator<(CTimeSpan s) const { return m_timeSpan < s.m_timeSpan; }
  bool operator>(CTimeSpan s) const { return m_timeSpan > s.m_timeSpan; }
  bool operator<=(CTimeSpan s) const { return m_timeSpan <= s.m_timeSpan; }
  bool operator>=(CTimeSpan s) const { return m_timeSpan >= s.m_timeSpan; }

  _AtlTimeString Format(LPCTSTR pFormat) const {
    _AtlTimeString r;
    for (LPCTSTR p = pFormat; *p; p++) {
      if (*p != '%') {
        r += *p;
        continue;
      }
      switch (*++p) {
      case 'D': r.AppendFormat(TEXT("%I64d"), GetDays()); break;
      case 'H': r.AppendFormat(TEXT("%02ld"), GetHours()); break;
      case 'M': r.AppendFormat(TEXT("%02ld"), GetMinutes()); break;
      case 'S': r.AppendFormat(TEXT("%02ld"), GetSeconds()); break;
      case '%': r += TEXT('%'); break;
      case 0: return r;
      default: break;
      }
    }
    return r;
  }
  _AtlTimeString Format(UINT nID) const { return Format(_AtlTime::LoadFormat(nID)); }

private:
  __time64_t m_timeSpan;
};

class CTime {
public:
  static CTime WINAPI GetCurrentTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return CTime(_AtlTime::FromFileTime(ft));
  }
  static BOOL WINAPI IsValidFILETIME(const FILETIME& ft) {
    SYSTEMTIME st;
    return FileTimeToSystemTime(&ft, &st);
  }

  CTime() : m_time(0) {}
  CTime(__time64_t time) : m_time(time) {}
  CTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec, int nDST = -1) {
    (void)nDST;
    SYSTEMTIME st = {};
    st.wYear = (WORD)nYear;
    st.wMonth = (WORD)nMonth;
    st.wDay = (WORD)nDay;
    st.wHour = (WORD)nHour;
    st.wMinute = (WORD)nMin;
    st.wSecond = (WORD)nSec;
    m_time = _AtlTime::FromLocalSystemTime(st);
  }
  CTime(WORD wDosDate, WORD wDosTime, int nDST = -1) {
    (void)nDST;
    FILETIME ftLocal, ftUtc;
    if (!DosDateTimeToFileTime(wDosDate, wDosTime, &ftLocal)) _AtlTime::ThrowInvalidArg();
    LocalFileTimeToFileTime(&ftLocal, &ftUtc);
    m_time = _AtlTime::FromFileTime(ftUtc);
  }
  CTime(const SYSTEMTIME& st, int nDST = -1) {
    (void)nDST;
    SYSTEMTIME s = st;
    s.wMilliseconds = 0;
    m_time = _AtlTime::FromLocalSystemTime(s);
  }
  CTime(const FILETIME& ft, int nDST = -1) {
    (void)nDST;
    m_time = _AtlTime::FromFileTime(ft);
  }

  CTime& operator=(__time64_t time) { m_time = time; return *this; }
  CTime& operator+=(CTimeSpan s) { m_time += s.GetTimeSpan(); return *this; }
  CTime& operator-=(CTimeSpan s) { m_time -= s.GetTimeSpan(); return *this; }
  CTimeSpan operator-(CTime t) const { return CTimeSpan(m_time - t.m_time); }
  CTime operator-(CTimeSpan s) const { return CTime(m_time - s.GetTimeSpan()); }
  CTime operator+(CTimeSpan s) const { return CTime(m_time + s.GetTimeSpan()); }
  bool operator==(CTime t) const { return m_time == t.m_time; }
  bool operator!=(CTime t) const { return m_time != t.m_time; }
  bool operator<(CTime t) const { return m_time < t.m_time; }
  bool operator>(CTime t) const { return m_time > t.m_time; }
  bool operator<=(CTime t) const { return m_time <= t.m_time; }
  bool operator>=(CTime t) const { return m_time >= t.m_time; }

  struct tm* GetGmtTm(struct tm* ptm) const {
    FILETIME ft = _AtlTime::ToFileTime(m_time);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    _AtlTime::ToTm(st, ptm);
    ptm->tm_isdst = 0;
    return ptm;
  }
  struct tm* GetLocalTm(struct tm* ptm) const {
    SYSTEMTIME st;
    _AtlTime::ToLocalSystemTime(m_time, st);
    _AtlTime::ToTm(st, ptm);
    return ptm;
  }
  bool GetAsSystemTime(SYSTEMTIME& st) const {
    _AtlTime::ToLocalSystemTime(m_time, st);
    return true;
  }
  __time64_t GetTime() const { return m_time; }
  int GetYear() const { return Local().wYear; }
  int GetMonth() const { return Local().wMonth; }
  int GetDay() const { return Local().wDay; }
  int GetHour() const { return Local().wHour; }
  int GetMinute() const { return Local().wMinute; }
  int GetSecond() const { return Local().wSecond; }
  int GetDayOfWeek() const { return Local().wDayOfWeek + 1; }

  _AtlTimeString Format(LPCTSTR pszFormat) const {
    struct tm t;
    return _AtlTime::FormatTm(pszFormat, *GetLocalTm(&t));
  }
  _AtlTimeString FormatGmt(LPCTSTR pszFormat) const {
    struct tm t;
    return _AtlTime::FormatTm(pszFormat, *GetGmtTm(&t));
  }
  _AtlTimeString Format(UINT nFormatID) const { return Format(_AtlTime::LoadFormat(nFormatID)); }
  _AtlTimeString FormatGmt(UINT nFormatID) const {
    return FormatGmt(_AtlTime::LoadFormat(nFormatID));
  }

private:
  SYSTEMTIME Local() const {
    SYSTEMTIME st;
    _AtlTime::ToLocalSystemTime(m_time, st);
    return st;
  }
  __time64_t m_time;
};

// ---------------------------------------------------------------------
// CFileTimeSpan and CFileTime: a FILETIME and a span of its units

class CFileTimeSpan {
public:
  CFileTimeSpan() : m_nSpan(0) {}
  CFileTimeSpan(LONGLONG nSpan) : m_nSpan(nSpan) {}
  CFileTimeSpan(const CFileTimeSpan& span) : m_nSpan(span.m_nSpan) {}
  CFileTimeSpan& operator=(const CFileTimeSpan& span) {
    m_nSpan = span.m_nSpan;
    return *this;
  }
  CFileTimeSpan& operator+=(CFileTimeSpan span) { m_nSpan += span.m_nSpan; return *this; }
  CFileTimeSpan& operator-=(CFileTimeSpan span) { m_nSpan -= span.m_nSpan; return *this; }
  CFileTimeSpan operator+(CFileTimeSpan span) const { return CFileTimeSpan(m_nSpan + span.m_nSpan); }
  CFileTimeSpan operator-(CFileTimeSpan span) const { return CFileTimeSpan(m_nSpan - span.m_nSpan); }
  bool operator==(CFileTimeSpan span) const { return m_nSpan == span.m_nSpan; }
  bool operator!=(CFileTimeSpan span) const { return m_nSpan != span.m_nSpan; }
  bool operator<(CFileTimeSpan span) const { return m_nSpan < span.m_nSpan; }
  bool operator>(CFileTimeSpan span) const { return m_nSpan > span.m_nSpan; }
  bool operator<=(CFileTimeSpan span) const { return m_nSpan <= span.m_nSpan; }
  bool operator>=(CFileTimeSpan span) const { return m_nSpan >= span.m_nSpan; }
  LONGLONG GetTimeSpan() const { return m_nSpan; }
  void SetTimeSpan(LONGLONG nSpan) { m_nSpan = nSpan; }

protected:
  LONGLONG m_nSpan;
};

class CFileTime : public FILETIME {
public:
  static const ULONGLONG Millisecond = 10000;
  static const ULONGLONG Second = Millisecond * 1000;
  static const ULONGLONG Minute = Second * 60;
  static const ULONGLONG Hour = Minute * 60;
  static const ULONGLONG Day = Hour * 24;
  static const ULONGLONG Week = Day * 7;

  CFileTime() { SetTime(0); }
  CFileTime(const FILETIME& ft) {
    dwLowDateTime = ft.dwLowDateTime;
    dwHighDateTime = ft.dwHighDateTime;
  }
  CFileTime(ULONGLONG nTime) { SetTime(nTime); }

  static CFileTime WINAPI GetCurrentTime() {
    CFileTime ft;
    GetSystemTimeAsFileTime(&ft);
    return ft;
  }

  CFileTime& operator=(const FILETIME& ft) {
    dwLowDateTime = ft.dwLowDateTime;
    dwHighDateTime = ft.dwHighDateTime;
    return *this;
  }
  CFileTime& operator+=(CFileTimeSpan span) {
    SetTime(GetTime() + span.GetTimeSpan());
    return *this;
  }
  CFileTime& operator-=(CFileTimeSpan span) {
    SetTime(GetTime() - span.GetTimeSpan());
    return *this;
  }
  CFileTime operator+(CFileTimeSpan span) const { return CFileTime(GetTime() + span.GetTimeSpan()); }
  CFileTime operator-(CFileTimeSpan span) const { return CFileTime(GetTime() - span.GetTimeSpan()); }
  CFileTimeSpan operator-(const FILETIME& ft) const {
    return CFileTimeSpan((LONGLONG)(GetTime() - CFileTime(ft).GetTime()));
  }
  bool operator==(const FILETIME& ft) const { return GetTime() == CFileTime(ft).GetTime(); }
  bool operator!=(const FILETIME& ft) const { return GetTime() != CFileTime(ft).GetTime(); }
  bool operator<(const FILETIME& ft) const { return GetTime() < CFileTime(ft).GetTime(); }
  bool operator>(const FILETIME& ft) const { return GetTime() > CFileTime(ft).GetTime(); }
  bool operator<=(const FILETIME& ft) const { return GetTime() <= CFileTime(ft).GetTime(); }
  bool operator>=(const FILETIME& ft) const { return GetTime() >= CFileTime(ft).GetTime(); }

  ULONGLONG GetTime() const {
    return ((ULONGLONG)dwHighDateTime << 32) | dwLowDateTime;
  }
  void SetTime(ULONGLONG nTime) {
    dwLowDateTime = (DWORD)nTime;
    dwHighDateTime = (DWORD)(nTime >> 32);
  }
  CFileTime UTCToLocal() const {
    CFileTime ft;
    ::FileTimeToLocalFileTime(this, &ft);
    return ft;
  }
  CFileTime LocalToUTC() const {
    CFileTime ft;
    ::LocalFileTimeToFileTime(this, &ft);
    return ft;
  }
};

// ATL's names for them (ATL::CTime), as in ATL 7.
namespace ATL {
using ::CFileTime;
using ::CFileTimeSpan;
using ::CTime;
using ::CTimeSpan;
}

#endif  // __ATLTIME_H__
