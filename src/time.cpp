// CTime and CTimeSpan.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

// Times are seconds since 1970-01-01 UTC, kept in 64 bits. The conversions
// use Win32's own calendar functions rather than the C runtime's, so that
// they behave the same with every C library.

static const LONGLONG kEpochFt = 116444736000000000LL;  // 1970 in FILETIME units

static __time64_t FileTimeToTime(const FILETIME& ftUtc) {
  ULARGE_INTEGER u;
  u.LowPart = ftUtc.dwLowDateTime;
  u.HighPart = ftUtc.dwHighDateTime;
  return (__time64_t)(((LONGLONG)u.QuadPart - kEpochFt) / 10000000);
}

static FILETIME TimeToFileTime(__time64_t t) {
  ULARGE_INTEGER u;
  u.QuadPart = (ULONGLONG)(t * 10000000 + kEpochFt);
  FILETIME ft;
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  return ft;
}

static __time64_t LocalSystemTimeToTime(const SYSTEMTIME& stLocal) {
  FILETIME ftLocal, ftUtc;
  if (!SystemTimeToFileTime(&stLocal, &ftLocal)) AfxThrowInvalidArgException();
  LocalFileTimeToFileTime(&ftLocal, &ftUtc);
  return FileTimeToTime(ftUtc);
}

static void TimeToLocalSystemTime(__time64_t t, SYSTEMTIME& st) {
  FILETIME ft = TimeToFileTime(t), ftLocal;
  FileTimeToLocalFileTime(&ft, &ftLocal);
  FileTimeToSystemTime(&ftLocal, &st);
}

static void SystemTimeToTm(const SYSTEMTIME& st, struct tm* ptm) {
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

CTime PASCAL CTime::GetCurrentTime() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return CTime(FileTimeToTime(ft));
}

BOOL PASCAL CTime::IsValidFILETIME(const FILETIME& ft) {
  SYSTEMTIME st;
  return FileTimeToSystemTime(&ft, &st);
}

CTime::CTime(int nYear, int nMonth, int nDay, int nHour, int nMin, int nSec, int) {
  SYSTEMTIME st = {};
  st.wYear = (WORD)nYear;
  st.wMonth = (WORD)nMonth;
  st.wDay = (WORD)nDay;
  st.wHour = (WORD)nHour;
  st.wMinute = (WORD)nMin;
  st.wSecond = (WORD)nSec;
  m_time = LocalSystemTimeToTime(st);
}

CTime::CTime(WORD wDosDate, WORD wDosTime, int) {
  FILETIME ftLocal, ftUtc;
  if (!DosDateTimeToFileTime(wDosDate, wDosTime, &ftLocal)) AfxThrowInvalidArgException();
  LocalFileTimeToFileTime(&ftLocal, &ftUtc);
  m_time = FileTimeToTime(ftUtc);
}

CTime::CTime(const SYSTEMTIME& st, int) {
  SYSTEMTIME s = st;
  s.wMilliseconds = 0;
  m_time = LocalSystemTimeToTime(s);
}

CTime::CTime(const FILETIME& ft, int) { m_time = FileTimeToTime(ft); }

struct tm* CTime::GetGmtTm(struct tm* ptm) const {
  FILETIME ft = TimeToFileTime(m_time);
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);
  SystemTimeToTm(st, ptm);
  ptm->tm_isdst = 0;
  return ptm;
}

struct tm* CTime::GetLocalTm(struct tm* ptm) const {
  SYSTEMTIME st;
  TimeToLocalSystemTime(m_time, st);
  SystemTimeToTm(st, ptm);
  return ptm;
}

bool CTime::GetAsSystemTime(SYSTEMTIME& st) const {
  TimeToLocalSystemTime(m_time, st);
  return true;
}

#define CTIME_FIELD(name, field) \
  int CTime::name() const { SYSTEMTIME st; TimeToLocalSystemTime(m_time, st); return field; }
CTIME_FIELD(GetYear, st.wYear)
CTIME_FIELD(GetMonth, st.wMonth)
CTIME_FIELD(GetDay, st.wDay)
CTIME_FIELD(GetHour, st.wHour)
CTIME_FIELD(GetMinute, st.wMinute)
CTIME_FIELD(GetSecond, st.wSecond)
CTIME_FIELD(GetDayOfWeek, st.wDayOfWeek + 1)
#undef CTIME_FIELD

// strftime with the subset of codes MFC programs use, done here so that
// it does not depend on the C library's locale support.
CString AFXAPI _AfxFormatTm(LPCTSTR pszFormat, const struct tm& t) {
  static const LPCTSTR days[] = {_T("Sunday"), _T("Monday"), _T("Tuesday"), _T("Wednesday"),
                                 _T("Thursday"), _T("Friday"), _T("Saturday")};
  static const LPCTSTR months[] = {_T("January"), _T("February"), _T("March"),
                                   _T("April"), _T("May"), _T("June"), _T("July"),
                                   _T("August"), _T("September"), _T("October"),
                                   _T("November"), _T("December")};
  CString r;
  for (LPCTSTR p = pszFormat; *p; p++) {
    if (*p != '%') {
      r += *p;
      continue;
    }
    p++;
    bool alt = false;
    if (*p == '#') { alt = true; p++; }
    LPCTSTR d2 = alt ? _T("%d") : _T("%02d");
    switch (*p) {
    case 'a': r += CString(days[t.tm_wday]).Left(3); break;
    case 'A': r += days[t.tm_wday]; break;
    case 'b': r += CString(months[t.tm_mon]).Left(3); break;
    case 'B': r += months[t.tm_mon]; break;
    case 'c':
      r.AppendFormat(_T("%02d/%02d/%02d %02d:%02d:%02d"), t.tm_mon + 1, t.tm_mday,
                     t.tm_year % 100, t.tm_hour, t.tm_min, t.tm_sec);
      break;
    case 'd': r.AppendFormat(d2, t.tm_mday); break;
    case 'H': r.AppendFormat(d2, t.tm_hour); break;
    case 'I': r.AppendFormat(d2, t.tm_hour % 12 ? t.tm_hour % 12 : 12); break;
    case 'j': r.AppendFormat(alt ? _T("%d") : _T("%03d"), t.tm_yday + 1); break;
    case 'm': r.AppendFormat(d2, t.tm_mon + 1); break;
    case 'M': r.AppendFormat(d2, t.tm_min); break;
    case 'p': r += t.tm_hour < 12 ? _T("AM") : _T("PM"); break;
    case 'S': r.AppendFormat(d2, t.tm_sec); break;
    case 'U': r.AppendFormat(d2, (t.tm_yday + 7 - t.tm_wday) / 7); break;
    case 'w': r.AppendFormat(_T("%d"), t.tm_wday); break;
    case 'W': r.AppendFormat(d2, (t.tm_yday + 7 - (t.tm_wday + 6) % 7) / 7); break;
    case 'x':
      r.AppendFormat(_T("%02d/%02d/%02d"), t.tm_mon + 1, t.tm_mday, t.tm_year % 100);
      break;
    case 'X': r.AppendFormat(_T("%02d:%02d:%02d"), t.tm_hour, t.tm_min, t.tm_sec); break;
    case 'y': r.AppendFormat(d2, t.tm_year % 100); break;
    case 'Y': r.AppendFormat(_T("%d"), t.tm_year + 1900); break;
    case 'z':
    case 'Z': {
      TIME_ZONE_INFORMATION tzi;
      DWORD k = GetTimeZoneInformation(&tzi);
      r += CString(k == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightName : tzi.StandardName);
      break;
    }
    case '%': r += _T('%'); break;
    case 0: return r;
    default: break;
    }
  }
  return r;
}

CString CTime::Format(LPCTSTR pszFormat) const {
  struct tm t;
  return _AfxFormatTm(pszFormat, *GetLocalTm(&t));
}
CString CTime::FormatGmt(LPCTSTR pszFormat) const {
  struct tm t;
  return _AfxFormatTm(pszFormat, *GetGmtTm(&t));
}
CString CTime::Format(UINT nFormatID) const {
  CString f;
  f.LoadString(nFormatID);
  return Format(f);
}
CString CTime::FormatGmt(UINT nFormatID) const {
  CString f;
  f.LoadString(nFormatID);
  return FormatGmt(f);
}

CString CTimeSpan::Format(LPCTSTR pFormat) const {
  CString r;
  for (LPCTSTR p = pFormat; *p; p++) {
    if (*p != '%') {
      r += *p;
      continue;
    }
    switch (*++p) {
    case 'D': r.AppendFormat(_T("%I64d"), GetDays()); break;
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

CString CTimeSpan::Format(UINT nID) const {
  CString f;
  f.LoadString(nID);
  return Format(f);
}
