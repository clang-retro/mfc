// kernel_time.cpp - kernel32 time: system and file times, the calendar,
// time zones, and the C runtime's 64-bit time functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

using namespace winport;

namespace {

const LONGLONG kTicksPerSec = 10000000LL;
const LONGLONG kTicksPerDay = 86400LL * kTicksPerSec;

bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int DaysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && IsLeap(y) ? 29 : d[m - 1];
}

// Days since 1601-01-01 for a date of the Gregorian calendar.
LONGLONG DaysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  LONGLONG era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  LONGLONG days1970 = era * 146097 + (LONGLONG)doe - 719468;
  return days1970 + 134774;  // 1601 to 1970
}

void CivilFromDays(LONGLONG days1601, int* y, int* m, int* d) {
  LONGLONG z = days1601 - 134774 + 719468;
  LONGLONG era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int yy = (int)(yoe + era * 400);
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = yy + (*m <= 2);
}

bool ToTicks(const SYSTEMTIME* st, LONGLONG* out) {
  if (st->wYear < 1601 || st->wYear > 30827 || st->wMonth < 1 || st->wMonth > 12 || st->wDay < 1 ||
      st->wDay > DaysInMonth(st->wYear, st->wMonth) || st->wHour > 23 || st->wMinute > 59 || st->wSecond > 59 ||
      st->wMilliseconds > 999)
    return false;
  LONGLONG days = DaysFromCivil(st->wYear, st->wMonth, st->wDay);
  *out = days * kTicksPerDay +
         ((LONGLONG)st->wHour * 3600 + st->wMinute * 60 + st->wSecond) * kTicksPerSec + st->wMilliseconds * 10000LL;
  return true;
}

void FromTicks(LONGLONG t, SYSTEMTIME* st) {
  LONGLONG days = t / kTicksPerDay;
  LONGLONG rem = t % kTicksPerDay;
  int y, m, d;
  CivilFromDays(days, &y, &m, &d);
  st->wYear = (WORD)y;
  st->wMonth = (WORD)m;
  st->wDay = (WORD)d;
  st->wDayOfWeek = (WORD)((days + 1) % 7);  // 1601-01-01 was a Monday
  st->wHour = (WORD)(rem / (3600 * kTicksPerSec));
  st->wMinute = (WORD)(rem / (60 * kTicksPerSec) % 60);
  st->wSecond = (WORD)(rem / kTicksPerSec % 60);
  st->wMilliseconds = (WORD)(rem / 10000 % 1000);
}

// The system's offset from UTC, in seconds, at a moment given in Unix
// seconds.
long OffsetAt(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_gmtoff;
}

LONGLONG UnixSeconds(LONGLONG ticks) { return (ticks - (LONGLONG)kEpochDelta) / kTicksPerSec; }

}  // namespace

extern "C" {

void WINAPI GetSystemTimeAsFileTime(LPFILETIME ft) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  *ft = FileTimeFromUnix(ts.tv_sec, ts.tv_nsec);
}

void WINAPI GetSystemTime(LPSYSTEMTIME st) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  FromTicks((LONGLONG)ToU64(ft), st);
}

void WINAPI GetLocalTime(LPSYSTEMTIME st) {
  FILETIME ft, lft;
  GetSystemTimeAsFileTime(&ft);
  FileTimeToLocalFileTime(&ft, &lft);
  FromTicks((LONGLONG)ToU64(lft), st);
}

// Setting the clock needs privileges a program does not have.
BOOL WINAPI SetSystemTime(const SYSTEMTIME*) {
  SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  return FALSE;
}
BOOL WINAPI SetLocalTime(const SYSTEMTIME*) {
  SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  return FALSE;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* st, LPFILETIME ft) {
  LONGLONG t;
  if (!ToTicks(st, &t)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *ft = ToFileTime((ULONGLONG)t);
  return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* ft, LPSYSTEMTIME st) {
  ULONGLONG t = ToU64(*ft);
  if (t >= 0x8000000000000000ULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  FromTicks((LONGLONG)t, st);
  return TRUE;
}

// Local time uses the system's rules for the moment converted, so
// times on the other side of a daylight saving change come out right.
BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* ft, LPFILETIME lft) {
  LONGLONG t = (LONGLONG)ToU64(*ft);
  *lft = ToFileTime((ULONGLONG)(t + (LONGLONG)OffsetAt((time_t)UnixSeconds(t)) * kTicksPerSec));
  return TRUE;
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME* lft, LPFILETIME ft) {
  LONGLONG l = (LONGLONG)ToU64(*lft);
  // Guess with the offset at the local time read as UTC, then correct
  // with the offset at the guess.
  long off = OffsetAt((time_t)UnixSeconds(l));
  LONGLONG u = l - (LONGLONG)off * kTicksPerSec;
  long off2 = OffsetAt((time_t)UnixSeconds(u));
  if (off2 != off) u = l - (LONGLONG)off2 * kTicksPerSec;
  *ft = ToFileTime((ULONGLONG)u);
  return TRUE;
}

LONG WINAPI CompareFileTime(const FILETIME* a, const FILETIME* b) {
  ULONGLONG x = ToU64(*a), y = ToU64(*b);
  return x < y ? -1 : x > y ? 1 : 0;
}

BOOL WINAPI FileTimeToDosDateTime(const FILETIME* ft, LPWORD date, LPWORD time) {
  SYSTEMTIME st;
  if (!FileTimeToSystemTime(ft, &st)) return FALSE;
  if (st.wYear < 1980 || st.wYear > 2107) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (date) *date = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
  if (time) *time = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
  return TRUE;
}

BOOL WINAPI DosDateTimeToFileTime(WORD date, WORD time, LPFILETIME ft) {
  SYSTEMTIME st = {};
  st.wYear = (WORD)(1980 + (date >> 9));
  st.wMonth = (WORD)((date >> 5) & 15);
  st.wDay = (WORD)(date & 31);
  st.wHour = (WORD)(time >> 11);
  st.wMinute = (WORD)((time >> 5) & 63);
  st.wSecond = (WORD)((time & 31) * 2);
  return SystemTimeToFileTime(&st, ft);
}

// ---------------------------------------------------------------------
// Time zones

}  // extern "C"

namespace {

// When a transition rule takes effect in a year, in local ticks. A rule
// with a year is a fixed date; without one, wDay is the week of the
// month (5 meaning the last) and wDayOfWeek the day.
LONGLONG RuleTime(const SYSTEMTIME& r, int year) {
  SYSTEMTIME st = r;
  if (r.wYear == 0) {
    st.wYear = (WORD)year;
    LONGLONG first = DaysFromCivil(year, r.wMonth, 1);
    int dow = (int)((first + 1) % 7);
    int day = 1 + (r.wDayOfWeek - dow + 7) % 7 + (r.wDay - 1) * 7;
    while (day > DaysInMonth(year, r.wMonth)) day -= 7;
    st.wDay = (WORD)day;
  }
  st.wDayOfWeek = 0;
  LONGLONG t = 0;
  ToTicks(&st, &t);
  return t;
}

// Whether a UTC time falls in daylight time under a zone's rules.
bool InDaylight(const TIME_ZONE_INFORMATION& tz, LONGLONG utc) {
  if (tz.DaylightDate.wMonth == 0 || tz.StandardDate.wMonth == 0) return false;
  LONGLONG std = utc - (LONGLONG)(tz.Bias + tz.StandardBias) * 60 * kTicksPerSec;
  SYSTEMTIME st;
  FromTicks(std, &st);
  // The daylight start is in standard time; the standard start is in
  // daylight time, so it moves by the difference.
  LONGLONG dstStart = RuleTime(tz.DaylightDate, st.wYear);
  LONGLONG stdStart = RuleTime(tz.StandardDate, st.wYear) +
                      (LONGLONG)(tz.DaylightBias - tz.StandardBias) * 60 * kTicksPerSec;
  if (dstStart < stdStart) return std >= dstStart && std < stdStart;
  return std >= dstStart || std < stdStart;  // southern hemisphere
}

// Finds the first moment after a in [a, b) where the offset differs
// from the offset at a.
time_t FindChange(time_t a, time_t b) {
  long off = OffsetAt(a);
  while (b - a > 1) {
    time_t mid = a + (b - a) / 2;
    if (OffsetAt(mid) == off) a = mid;
    else b = mid;
  }
  return b;
}

void RuleFrom(time_t change, long offBefore, SYSTEMTIME* r) {
  LONGLONG local = ((LONGLONG)change + offBefore) * kTicksPerSec + (LONGLONG)kEpochDelta;
  SYSTEMTIME st;
  FromTicks(local, &st);
  memset(r, 0, sizeof(*r));
  r->wMonth = st.wMonth;
  r->wDayOfWeek = st.wDayOfWeek;
  r->wDay = (WORD)((st.wDay - 1) / 7 + 1);
  if (st.wDay + 7 > DaysInMonth(st.wYear, st.wMonth)) r->wDay = 5;
  r->wHour = st.wHour;
  r->wMinute = st.wMinute;
}

}  // namespace

extern "C" {

DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tz) {
  memset(tz, 0, sizeof(*tz));
  tzset();
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  int year = tm.tm_year + 1900;
  struct tm jan = {};
  jan.tm_year = year - 1900;
  jan.tm_mday = 1;
  jan.tm_isdst = -1;
  time_t t0 = timegm(&jan);
  struct tm jan2 = jan;
  jan2.tm_year++;
  time_t t1 = timegm(&jan2);
  long offJan = OffsetAt(t0), offJul = OffsetAt(t0 + 182 * 86400);
  long stdOff = offJan < offJul ? offJan : offJul;
  long dstOff = offJan < offJul ? offJul : offJan;
  tz->Bias = (LONG)(-stdOff / 60);
  std::wstring sn = Widen(tzname[0]), dn = Widen(tzname[1]);
  wcsncpy(tz->StandardName, sn.c_str(), 31);
  wcsncpy(tz->DaylightName, dn.c_str(), 31);
  if (stdOff == dstOff) return TIME_ZONE_ID_UNKNOWN;
  tz->DaylightBias = (LONG)(-(dstOff - stdOff) / 60);
  // The two changes of this year, found by stepping a day at a time.
  time_t prev = t0;
  long prevOff = OffsetAt(t0);
  int found = 0;
  for (time_t t = t0 + 86400; t <= t1 && found < 2; t += 86400) {
    long off = OffsetAt(t);
    if (off != prevOff) {
      time_t c = FindChange(prev, t);
      RuleFrom(c, prevOff, off == dstOff ? &tz->DaylightDate : &tz->StandardDate);
      found++;
      prevOff = off;
    }
    prev = t;
  }
  return tm.tm_isdst > 0 ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

BOOL WINAPI SystemTimeToTzSpecificLocalTime(const TIME_ZONE_INFORMATION* tz, const SYSTEMTIME* utc,
                                            LPSYSTEMTIME local) {
  LONGLONG t;
  if (!ToTicks(utc, &t)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!tz) {
    FILETIME ft = ToFileTime((ULONGLONG)t), lft;
    FileTimeToLocalFileTime(&ft, &lft);
    FromTicks((LONGLONG)ToU64(lft), local);
    return TRUE;
  }
  LONG bias = tz->Bias + (InDaylight(*tz, t) ? tz->DaylightBias : tz->StandardBias);
  FromTicks(t - (LONGLONG)bias * 60 * kTicksPerSec, local);
  return TRUE;
}

BOOL WINAPI TzSpecificLocalTimeToSystemTime(const TIME_ZONE_INFORMATION* tz, const SYSTEMTIME* local,
                                            LPSYSTEMTIME utc) {
  LONGLONG t;
  if (!ToTicks(local, &t)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!tz) {
    FILETIME lft = ToFileTime((ULONGLONG)t), ft;
    LocalFileTimeToFileTime(&lft, &ft);
    FromTicks((LONGLONG)ToU64(ft), utc);
    return TRUE;
  }
  LONGLONG u = t + (LONGLONG)(tz->Bias + tz->StandardBias) * 60 * kTicksPerSec;
  if (InDaylight(*tz, u)) u = t + (LONGLONG)(tz->Bias + tz->DaylightBias) * 60 * kTicksPerSec;
  FromTicks(u, utc);
  return TRUE;
}

// ---------------------------------------------------------------------
// The C runtime's 64-bit time functions. time_t is 64 bits on every
// system the layer runs on, except 32-bit Mac OS X, where the range is
// that of the system's functions.

__time64_t _time64(__time64_t* p) {
  __time64_t t = (__time64_t)time(NULL);
  if (p) *p = t;
  return t;
}

int _localtime64_s(struct tm* out, const __time64_t* t) {
  if (!out || !t || *t < 0) return 22;  // EINVAL
  time_t tt = (time_t)*t;
  return localtime_r(&tt, out) ? 0 : 22;
}

int _gmtime64_s(struct tm* out, const __time64_t* t) {
  if (!out || !t || *t < 0) return 22;
  time_t tt = (time_t)*t;
  return gmtime_r(&tt, out) ? 0 : 22;
}

struct tm* _localtime64(const __time64_t* t) {
  static thread_local struct tm r;
  return _localtime64_s(&r, t) == 0 ? &r : NULL;
}

struct tm* _gmtime64(const __time64_t* t) {
  static thread_local struct tm r;
  return _gmtime64_s(&r, t) == 0 ? &r : NULL;
}

__time64_t _mktime64(struct tm* tm) { return (__time64_t)mktime(tm); }
__time64_t _mkgmtime64(struct tm* tm) { return (__time64_t)timegm(tm); }

}  // extern "C"
