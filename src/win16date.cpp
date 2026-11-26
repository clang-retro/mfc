// win16date.cpp - the month calendar and the date and time picker
// windows for 16-bit Windows, which has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The month calendar shows as many months as fit in the window, with the
// title, the day names, the trailing days, week numbers, today and the
// day states. It does the MCM_ messages and sends MCN_SELCHANGE,
// MCN_SELECT and MCN_GETDAYSTATE.
//
// The date and time picker shows the date or the time in fields of a
// format: the short date, the long date and the time of the [intl]
// section of WIN.INI, or a format of DTM_SETFORMATA with callback fields
// (X). The keys change the fields, a drop-down button opens a month
// calendar, and DTS_UPDOWN shows an up-down control instead. It does the
// DTM_ messages and sends the DTN_ notifications.
//
// 16-bit Windows has no GetLocalTime: the date of today comes from the C
// library (time and localtime).
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>
#include <time.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

// =====================================================================
// Dates

/// Gives TRUE for a leap year of the Gregorian calendar.
BOOL IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/// Gives the number of days of a month (1 to 12).
int DaysIn(int y, int m) {
  static const BYTE aDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  return m == 2 && IsLeap(y) ? 29 : aDays[m - 1];
}

/// Gives the number of a day: the days from 1 January 1970, in the
/// Gregorian calendar.
long DayNum(int y, int m, int d) {
  long yy = y - (m <= 2 ? 1 : 0);
  long era = (yy >= 0 ? yy : yy - 399) / 400;
  long yoe = yy - era * 400;
  long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

/// Gives the year, the month and the day of a day number.
void FromDayNum(long z, int* py, int* pm, int* pd) {
  z += 719468L;
  long era = (z >= 0 ? z : z - 146096L) / 146097L;
  long doe = z - era * 146097L;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  int d = (int)(doy - (153 * mp + 2) / 5 + 1);
  int m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *py = (int)(y + (m <= 2 ? 1 : 0));
  *pm = m;
  *pd = d;
}

/// Gives the day of the week of a day number: 0 for Sunday.
int DayOfWeek(long z) {
  long w = (z + 4) % 7;  // 1 January 1970 was a Thursday
  return (int)(w < 0 ? w + 7 : w);
}

/// Gives the day number of the date of a SYSTEMTIME.
long StDay(const SYSTEMTIME& st) { return DayNum(st.wYear, st.wMonth, st.wDay); }

/// Sets the date of a SYSTEMTIME from a day number, with the day of the
/// week. The time stays.
void StSetDay(SYSTEMTIME* pst, long z) {
  int y, m, d;
  FromDayNum(z, &y, &m, &d);
  pst->wYear = (WORD)y;
  pst->wMonth = (WORD)m;
  pst->wDay = (WORD)d;
  pst->wDayOfWeek = (WORD)DayOfWeek(z);
}

/// Gives the seconds of the time of a SYSTEMTIME from midnight.
long StSeconds(const SYSTEMTIME& st) {
  return (long)st.wHour * 3600L + (long)st.wMinute * 60L + (long)st.wSecond;
}

/// Compares the dates and times of two SYSTEMTIMEs: less than 0, 0 or
/// more than 0.
int StCompare(const SYSTEMTIME& a, const SYSTEMTIME& b, BOOL bTime) {
  long da = StDay(a), db = StDay(b);
  if (da != db) return da < db ? -1 : 1;
  if (!bTime) return 0;
  long sa = StSeconds(a), sb = StSeconds(b);
  return sa < sb ? -1 : (sa > sb ? 1 : 0);
}

/// Gives TRUE when a SYSTEMTIME holds a date and a time that are valid.
BOOL StValid(const SYSTEMTIME& st) {
  return st.wYear >= 1601 && st.wYear <= 9999 && st.wMonth >= 1 && st.wMonth <= 12 &&
         st.wDay >= 1 && st.wDay <= DaysIn(st.wYear, st.wMonth) && st.wHour < 24 &&
         st.wMinute < 60 && st.wSecond < 60;
}

/// Gives the date and the time of now, from the C library.
SYSTEMTIME StNow() {
  SYSTEMTIME st;
  memset(&st, 0, sizeof(st));
  time_t t = time(NULL);
  struct tm* ptm = localtime(&t);
  if (ptm != NULL) {
    st.wYear = (WORD)(ptm->tm_year + 1900);
    st.wMonth = (WORD)(ptm->tm_mon + 1);
    st.wDay = (WORD)ptm->tm_mday;
    st.wHour = (WORD)ptm->tm_hour;
    st.wMinute = (WORD)ptm->tm_min;
    st.wSecond = (WORD)ptm->tm_sec;
  } else {
    st.wYear = 1992;
    st.wMonth = 4;
    st.wDay = 6;
  }
  st.wDayOfWeek = (WORD)DayOfWeek(StDay(st));
  return st;
}

/// Gives the name of a month (1 to 12), long or short.
LPCSTR MonthName(int m, BOOL bLong) {
  static const char* const aLong[12] = {"January", "February", "March",     "April",
                                        "May",     "June",     "July",      "August",
                                        "September", "October", "November", "December"};
  static const char* const aShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (m < 1 || m > 12) return "";
  return bLong ? aLong[m - 1] : aShort[m - 1];
}

/// Gives the name of a day of the week (0 for Sunday), long or short.
LPCSTR DayName(int w, BOOL bLong) {
  static const char* const aLong[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
  static const char* const aShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return bLong ? aLong[w % 7] : aShort[w % 7];
}

/// Gives the font of a control, or the stock font.
HFONT DtFont(HFONT hFont) {
  return hFont != NULL ? hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the bold form of a font. The caller deletes it.
HFONT DtMakeBold(HFONT hFont) {
  LOGFONT lf;
  if (::GetObject(DtFont(hFont), sizeof(lf), &lf) == 0) return NULL;
  lf.lfWeight = FW_BOLD;
  return ::CreateFontIndirect(&lf);
}

/// Gives the width of a text in a font.
int DtTextWidth(HWND hWnd, HFONT hFont, LPCSTR psz) {
  HDC hdc = ::GetDC(hWnd);
  HGDIOBJ hOld = ::SelectObject(hdc, DtFont(hFont));
  SIZE size;
  ::GetTextExtentPoint32(hdc, psz, lstrlen(psz), &size);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(hWnd, hdc);
  return size.cx;
}

/// Gives the short date of a SYSTEMTIME, as M/d/yyyy.
CString ShortDate(const SYSTEMTIME& st) {
  char sz[32];
  wsprintf(sz, "%d/%d/%d", (int)st.wMonth, (int)st.wDay, (int)st.wYear);
  return CString(sz);
}

// =====================================================================
// The month calendar

// The timer of a pressed arrow of the calendar, which repeats.
const UINT kCalTimer = 1;

// The data of a month calendar.
struct W16Cal {
  HWND hwnd;
  SYSTEMTIME stSel1, stSel2;  // the selection (one day, or a range with MCS_MULTISELECT)
  SYSTEMTIME stToday;
  BOOL bTodaySet;             // MCM_SETTODAY gave the day of today
  int nYear, nMonth;          // the first month that shows
  int nFirstDay;              // MCM_SETFIRSTDAYOFWEEK: 0 for Monday to 6 for Sunday
  int nMaxSel;
  DWORD dwRange;              // GDTR_MIN and GDTR_MAX
  SYSTEMTIME stMin, stMax;
  COLORREF clr[6];            // MCSC_BACKGROUND to MCSC_TRAILINGTEXT
  int nDelta;                 // MCM_SETMONTHDELTA, or 0 for the months that show
  HFONT hFont, hBold;
  CDWordArray dayState;       // the day states of the months of MCN_GETDAYSTATE
  long lAnchor;               // the first day of a drag of the selection
  BOOL bSelecting;
  int nPressed;               // the pressed arrow: -1 back, 1 on, 0 none
  BOOL bFocus;
  /// Starts on the month of today, with today selected.
  W16Cal()
      : hwnd(NULL), bTodaySet(FALSE), nFirstDay(6), nMaxSel(7), dwRange(0), nDelta(0),
        hFont(NULL), hBold(NULL), lAnchor(0), bSelecting(FALSE), nPressed(0), bFocus(FALSE) {
    stToday = StNow();
    stToday.wHour = stToday.wMinute = stToday.wSecond = stToday.wMilliseconds = 0;
    stSel1 = stSel2 = stToday;
    nYear = stToday.wYear;
    nMonth = stToday.wMonth;
    memset(&stMin, 0, sizeof(stMin));
    memset(&stMax, 0, sizeof(stMax));
    for (int i = 0; i < 6; i++) clr[i] = CLR_DEFAULT;
  }
  /// Deletes the bold font.
  ~W16Cal() {
    if (hBold != NULL) ::DeleteObject(hBold);
  }
};

/// Gives a color of a calendar (MCSC_), or the color of the system.
COLORREF CalColor(W16Cal* c, int i) {
  if (c->clr[i] != CLR_DEFAULT) return c->clr[i];
  switch (i) {
    case MCSC_TEXT:
      return ::GetSysColor(COLOR_WINDOWTEXT);
    case MCSC_TITLEBK:
      return ::GetSysColor(COLOR_ACTIVECAPTION);
    case MCSC_TITLETEXT:
      return ::GetSysColor(COLOR_CAPTIONTEXT);
    case MCSC_TRAILINGTEXT:
      return ::GetSysColor(COLOR_GRAYTEXT);
  }
  return ::GetSysColor(COLOR_WINDOW);
}

/// Gives the bold font of a calendar, made from its font.
HFONT CalBold(W16Cal* c) {
  if (c->hBold == NULL) c->hBold = DtMakeBold(c->hFont);
  return c->hBold != NULL ? c->hBold : DtFont(c->hFont);
}

// The sizes of the parts of a calendar, from its font.
struct CalMetrics {
  int cxCell, cyCell;  // a day
  int cyTitle;         // the title of a month
  int cxWeek;          // the week numbers, or 0
  int cxMonth, cyMonth;
  int cyToday;         // the line of today, or 0
  int nCols, nRows;    // the months that show
  int x0, y0;          // the top left of the first month
};

// The gap between two months, in pixels.
const int kMonthGap = 8;

/// Gives the sizes of the parts of a calendar and how many months fit.
CalMetrics CalMeasure(W16Cal* c) {
  CalMetrics mt;
  HDC hdc = ::GetDC(c->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, CalBold(c));
  SIZE sizeDay, sizeNum;
  ::GetTextExtentPoint32(hdc, "Wed", 3, &sizeDay);
  ::GetTextExtentPoint32(hdc, "00", 2, &sizeNum);
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(c->hwnd, hdc);
  DWORD dwStyle = StyleOf(c->hwnd);
  mt.cxCell = (sizeDay.cx > sizeNum.cx ? sizeDay.cx : sizeNum.cx) + 6;
  mt.cyCell = tm.tmHeight + 2;
  mt.cyTitle = tm.tmHeight * 3 / 2 + 4;
  mt.cxWeek = (dwStyle & MCS_WEEKNUMBERS) ? mt.cxCell : 0;
  mt.cxMonth = mt.cxWeek + 7 * mt.cxCell + 8;
  mt.cyMonth = mt.cyTitle + mt.cyCell + 2 + 6 * mt.cyCell + 4;
  mt.cyToday = (dwStyle & MCS_NOTODAY) ? 0 : mt.cyCell + 4;
  RECT rc;
  ::GetClientRect(c->hwnd, &rc);
  mt.nCols = (int)MaxL(1, (rc.right + kMonthGap) / (mt.cxMonth + kMonthGap));
  mt.nRows = (int)MaxL(1, (rc.bottom - mt.cyToday + kMonthGap) / (mt.cyMonth + kMonthGap));
  int cxAll = mt.nCols * mt.cxMonth + (mt.nCols - 1) * kMonthGap;
  int cyAll = mt.nRows * mt.cyMonth + (mt.nRows - 1) * kMonthGap + mt.cyToday;
  mt.x0 = (int)MaxL(0, (rc.right - cxAll) / 2);
  mt.y0 = (int)MaxL(0, (rc.bottom - cyAll) / 2);
  return mt;
}

/// Gives the year and the month of the block k (from 0) of a calendar.
void CalBlockMonth(W16Cal* c, int k, int* py, int* pm) {
  int n = c->nYear * 12 + (c->nMonth - 1) + k;
  *py = n / 12;
  *pm = n % 12 + 1;
}

/// Gives the day of the week (0 for Sunday) of the first column.
int CalStartDow(W16Cal* c) { return (c->nFirstDay + 1) % 7; }

/// Gives the day number of the first cell of the month block k.
long CalFirstCell(W16Cal* c, int k) {
  int y, m;
  CalBlockMonth(c, k, &y, &m);
  long z = DayNum(y, m, 1);
  int nOffset = (DayOfWeek(z) - CalStartDow(c) + 7) % 7;
  return z - nOffset;
}

/// Gives the rectangle of the month block k.
RECT CalBlockRect(const CalMetrics& mt, int k) {
  int col = k % mt.nCols, row = k / mt.nCols;
  RECT rc;
  rc.left = mt.x0 + col * (mt.cxMonth + kMonthGap);
  rc.top = mt.y0 + row * (mt.cyMonth + kMonthGap);
  rc.right = rc.left + mt.cxMonth;
  rc.bottom = rc.top + mt.cyMonth;
  return rc;
}

/// Gives the rectangle of the cell i (0 to 41) of a month block.
RECT CalCellRect(const CalMetrics& mt, const RECT& rcBlock, int i) {
  RECT rc;
  rc.left = rcBlock.left + 4 + mt.cxWeek + (i % 7) * mt.cxCell;
  rc.top = rcBlock.top + mt.cyTitle + mt.cyCell + 2 + (i / 7) * mt.cyCell;
  rc.right = rc.left + mt.cxCell;
  rc.bottom = rc.top + mt.cyCell;
  return rc;
}

/// Gives the rectangles of the back and on arrows in the title of a
/// month block.
void CalArrowRects(const CalMetrics& mt, const RECT& rcBlock, RECT* prcBack, RECT* prcOn) {
  int cy = mt.cyTitle - 6;
  ::SetRect(prcBack, rcBlock.left + 3, rcBlock.top + 3, rcBlock.left + 3 + cy, rcBlock.top + 3 + cy);
  ::SetRect(prcOn, rcBlock.right - 3 - cy, rcBlock.top + 3, rcBlock.right - 3, rcBlock.top + 3 + cy);
}

/// Gives TRUE when a day is in the range of a calendar.
BOOL CalInRange(W16Cal* c, long z) {
  if ((c->dwRange & GDTR_MIN) && z < StDay(c->stMin)) return FALSE;
  if ((c->dwRange & GDTR_MAX) && z > StDay(c->stMax)) return FALSE;
  return TRUE;
}

/// Gives a day in the range of a calendar.
long CalClampDay(W16Cal* c, long z) {
  if ((c->dwRange & GDTR_MIN) && z < StDay(c->stMin)) z = StDay(c->stMin);
  if ((c->dwRange & GDTR_MAX) && z > StDay(c->stMax)) z = StDay(c->stMax);
  return z;
}

/// Gives the number of months that show.
int CalMonthsShown(W16Cal* c) {
  CalMetrics mt = CalMeasure(c);
  return mt.nCols * mt.nRows;
}

/// Asks the parent for the day states of the months that show and the
/// trailing months (MCN_GETDAYSTATE), with MCS_DAYSTATE.
void CalGetDayState(W16Cal* c) {
  if (!(StyleOf(c->hwnd) & MCS_DAYSTATE)) return;
  int n = CalMonthsShown(c) + 2;
  c->dayState.SetSize(n);
  for (int i = 0; i < n; i++) c->dayState[i] = 0;
  CDWordArray buffer;
  buffer.SetSize(n);
  for (int i = 0; i < n; i++) buffer[i] = 0;
  NMDAYSTATE nm;
  memset(&nm, 0, sizeof(nm));
  int y, m;
  CalBlockMonth(c, -1, &y, &m);
  nm.stStart.wYear = (WORD)y;
  nm.stStart.wMonth = (WORD)m;
  nm.stStart.wDay = 1;
  nm.stStart.wDayOfWeek = (WORD)DayOfWeek(DayNum(y, m, 1));
  nm.cDayState = n;
  nm.prgDayState = (LPMONTHDAYSTATE)buffer.GetData();
  SendNotify(c->hwnd, MCN_GETDAYSTATE, &nm.nmhdr);
  for (int i = 0; i < n; i++) c->dayState[i] = buffer[i];
}

/// Gives TRUE when a day of the month block k (-1 to the months shown) is
/// bold by its day state.
BOOL CalDayBold(W16Cal* c, int k, int d) {
  int i = k + 1;
  if (i < 0 || i >= c->dayState.GetSize() || d < 1 || d > 31) return FALSE;
  return (c->dayState[i] & (1L << (d - 1))) != 0;
}

/// Shows the months from a first month, and asks for the day states.
void CalSetFirstMonth(W16Cal* c, int y, int m) {
  while (m < 1) {
    m += 12;
    y--;
  }
  while (m > 12) {
    m -= 12;
    y++;
  }
  if (y < 1601) y = 1601, m = 1;
  if (y > 9999) y = 9999, m = 12;
  c->nYear = y;
  c->nMonth = m;
  CalGetDayState(c);
  ::InvalidateRect(c->hwnd, NULL, TRUE);
}

/// Scrolls the months so that a day shows.
void CalEnsureVisible(W16Cal* c, long z) {
  int y, m, d;
  FromDayNum(z, &y, &m, &d);
  int nShown = CalMonthsShown(c);
  int nFirst = c->nYear * 12 + c->nMonth - 1;
  int nDay = y * 12 + m - 1;
  if (nDay < nFirst)
    CalSetFirstMonth(c, y, m);
  else if (nDay >= nFirst + nShown)
    CalSetFirstMonth(c, y, m - nShown + 1);
}

/// Sends MCN_SELCHANGE or MCN_SELECT with the selection.
void CalNotify(W16Cal* c, UINT nCode) {
  NMSELCHANGE nm;
  memset(&nm, 0, sizeof(nm));
  nm.stSelStart = c->stSel1;
  nm.stSelEnd = c->stSel2;
  SendNotify(c->hwnd, nCode, &nm.nmhdr);
}

/// Selects the days from z1 to z2 (in either order), in the range and no
/// more than the largest selection. Gives TRUE when the selection changed.
BOOL CalSetSel(W16Cal* c, long z1, long z2) {
  BOOL bMulti = (StyleOf(c->hwnd) & MCS_MULTISELECT) != 0;
  if (!bMulti) z2 = z1;
  z1 = CalClampDay(c, z1);
  z2 = CalClampDay(c, z2);
  if (z2 < z1) {
    long t = z1;
    z1 = z2;
    z2 = t;
  }
  if (z2 - z1 + 1 > c->nMaxSel) z2 = z1 + c->nMaxSel - 1;
  if (StDay(c->stSel1) == z1 && StDay(c->stSel2) == z2) return FALSE;
  StSetDay(&c->stSel1, z1);
  StSetDay(&c->stSel2, z2);
  ::InvalidateRect(c->hwnd, NULL, TRUE);
  return TRUE;
}

/// Gives what is at a point of a calendar (MCHT_) and the day there.
UINT CalHitTest(W16Cal* c, POINT pt, SYSTEMTIME* pst) {
  CalMetrics mt = CalMeasure(c);
  int nShown = mt.nCols * mt.nRows;
  RECT rcClient;
  ::GetClientRect(c->hwnd, &rcClient);
  if (pst != NULL) memset(pst, 0, sizeof(*pst));
  for (int k = 0; k < nShown; k++) {
    RECT rcBlock = CalBlockRect(mt, k);
    if (!::PtInRect(&rcBlock, pt)) continue;
    RECT rcTitle = rcBlock;
    rcTitle.bottom = rcTitle.top + mt.cyTitle;
    if (::PtInRect(&rcTitle, pt)) {
      RECT rcBack, rcOn;
      CalArrowRects(mt, rcBlock, &rcBack, &rcOn);
      if (k == 0 && ::PtInRect(&rcBack, pt)) return MCHT_TITLEBTNPREV;
      if (k == mt.nCols - 1 && ::PtInRect(&rcOn, pt)) return MCHT_TITLEBTNNEXT;
      int y, m;
      CalBlockMonth(c, k, &y, &m);
      if (pst != NULL) {
        pst->wYear = (WORD)y;
        pst->wMonth = (WORD)m;
        pst->wDay = 1;
      }
      return pt.x < (rcBlock.left + rcBlock.right) / 2 ? MCHT_TITLEMONTH : MCHT_TITLEYEAR;
    }
    long zFirst = CalFirstCell(c, k);
    int y, m;
    CalBlockMonth(c, k, &y, &m);
    for (int i = 0; i < 42; i++) {
      RECT rcCell = CalCellRect(mt, rcBlock, i);
      if (!::PtInRect(&rcCell, pt)) continue;
      long z = zFirst + i;
      int yy, mm, dd;
      FromDayNum(z, &yy, &mm, &dd);
      UINT uHit = MCHT_CALENDARDATE;
      if (yy * 12 + mm < y * 12 + m) {
        if (k != 0) return MCHT_CALENDARBK;
        uHit = MCHT_CALENDARDATEPREV;
      } else if (yy * 12 + mm > y * 12 + m) {
        if (k != nShown - 1) return MCHT_CALENDARBK;
        uHit = MCHT_CALENDARDATENEXT;
      }
      if (pst != NULL) StSetDay(pst, z);
      return uHit;
    }
    if (mt.cxWeek > 0 && pt.x < rcBlock.left + 4 + mt.cxWeek) return MCHT_CALENDARWEEKNUM;
    return MCHT_CALENDARBK;
  }
  if (mt.cyToday > 0) {
    int yToday = mt.y0 + mt.nRows * mt.cyMonth + (mt.nRows - 1) * kMonthGap;
    RECT rcToday = {mt.x0, yToday, mt.x0 + mt.cxMonth, yToday + mt.cyToday};
    if (::PtInRect(&rcToday, pt)) {
      if (pst != NULL) *pst = c->stToday;
      return MCHT_TODAYLINK;
    }
  }
  return MCHT_NOWHERE;
}

/// Gives the week number of the week that starts on a day (ISO 8601: the
/// week with the first Thursday of the year is week 1).
int CalWeekNumber(long zWeekStart) {
  // The Thursday of the week of the day.
  int nDow = DayOfWeek(zWeekStart);
  long zThu = zWeekStart + (4 - (nDow == 0 ? 7 : nDow));
  int y, m, d;
  FromDayNum(zThu, &y, &m, &d);
  return (int)((zThu - DayNum(y, 1, 1)) / 7 + 1);
}

/// Draws the text of a number or a short text centered in a rectangle.
void CalCenterText(HDC hdc, const RECT& rc, LPCSTR psz) {
  RECT r = rc;
  ::DrawText(hdc, psz, lstrlen(psz), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/// Draws a calendar: each month with its title, the day names, the days
/// and the week numbers, and the line of today.
void CalPaint(W16Cal* c, HDC hdc) {
  RECT rcClient;
  ::GetClientRect(c->hwnd, &rcClient);
  FillColor(hdc, rcClient, CalColor(c, MCSC_BACKGROUND));
  CalMetrics mt = CalMeasure(c);
  int nShown = mt.nCols * mt.nRows;
  DWORD dwStyle = StyleOf(c->hwnd);
  ::SetBkMode(hdc, TRANSPARENT);
  long zSel1 = StDay(c->stSel1), zSel2 = StDay(c->stSel2), zToday = StDay(c->stToday);
  for (int k = 0; k < nShown; k++) {
    RECT rcBlock = CalBlockRect(mt, k);
    FillColor(hdc, rcBlock, CalColor(c, MCSC_MONTHBK));
    int y, m;
    CalBlockMonth(c, k, &y, &m);
    // The title.
    RECT rcTitle = rcBlock;
    rcTitle.bottom = rcTitle.top + mt.cyTitle;
    FillColor(hdc, rcTitle, CalColor(c, MCSC_TITLEBK));
    char sz[40];
    wsprintf(sz, "%s %d", MonthName(m, TRUE), y);
    HGDIOBJ hOld = ::SelectObject(hdc, CalBold(c));
    ::SetTextColor(hdc, CalColor(c, MCSC_TITLETEXT));
    CalCenterText(hdc, rcTitle, sz);
    RECT rcBack, rcOn;
    CalArrowRects(mt, rcBlock, &rcBack, &rcOn);
    if (k == 0) {
      DrawButtonFace(hdc, rcBack, c->nPressed < 0);
      DrawArrow(hdc, rcBack, 2, TRUE, c->nPressed < 0);
    }
    if (k == mt.nCols - 1) {
      DrawButtonFace(hdc, rcOn, c->nPressed > 0);
      DrawArrow(hdc, rcOn, 3, TRUE, c->nPressed > 0);
    }
    // The day names and the line below them.
    ::SelectObject(hdc, DtFont(c->hFont));
    ::SetTextColor(hdc, CalColor(c, MCSC_TITLEBK));
    for (int i = 0; i < 7; i++) {
      RECT rcName = CalCellRect(mt, rcBlock, i);
      ::OffsetRect(&rcName, 0, -(mt.cyCell + 2));
      CalCenterText(hdc, rcName, DayName((CalStartDow(c) + i) % 7, FALSE));
    }
    int yLine = rcBlock.top + mt.cyTitle + mt.cyCell;
    DrawLine(hdc, rcBlock.left + 4 + mt.cxWeek, yLine, rcBlock.right - 4, yLine,
             CalColor(c, MCSC_TRAILINGTEXT));
    // The days.
    long zFirst = CalFirstCell(c, k);
    for (int i = 0; i < 42; i++) {
      long z = zFirst + i;
      int yy, mm, dd;
      FromDayNum(z, &yy, &mm, &dd);
      BOOL bThis = yy == y && mm == m;
      BOOL bTrailing = !bThis;
      if (bTrailing && !((yy * 12 + mm < y * 12 + m && k == 0) ||
                         (yy * 12 + mm > y * 12 + m && k == nShown - 1)))
        continue;
      RECT rcCell = CalCellRect(mt, rcBlock, i);
      BOOL bSel = z >= zSel1 && z <= zSel2 && bThis;
      if (bSel) {
        FillColor(hdc, rcCell, CalColor(c, MCSC_TITLEBK));
        ::SetTextColor(hdc, CalColor(c, MCSC_TITLETEXT));
      } else {
        ::SetTextColor(hdc, bTrailing || !CalInRange(c, z) ? CalColor(c, MCSC_TRAILINGTEXT)
                                                           : CalColor(c, MCSC_TEXT));
      }
      int kState = bThis ? k : (yy * 12 + mm < y * 12 + m ? k - 1 : k + 1);
      ::SelectObject(hdc, CalDayBold(c, kState, dd) ? CalBold(c) : DtFont(c->hFont));
      wsprintf(sz, "%d", dd);
      CalCenterText(hdc, rcCell, sz);
      if (z == zToday && bThis && !(dwStyle & MCS_NOTODAYCIRCLE)) {
        HBRUSH hbr = ::CreateSolidBrush(RGB(255, 0, 0));
        ::FrameRect(hdc, &rcCell, hbr);
        ::DeleteObject(hbr);
      }
      if (bSel && c->bFocus && z == zSel1) ::DrawFocusRect(hdc, &rcCell);
    }
    // The week numbers.
    if (mt.cxWeek > 0) {
      ::SelectObject(hdc, DtFont(c->hFont));
      ::SetTextColor(hdc, CalColor(c, MCSC_TRAILINGTEXT));
      for (int row = 0; row < 6; row++) {
        RECT rcCell = CalCellRect(mt, rcBlock, row * 7);
        RECT rcWeek = {rcBlock.left + 4, rcCell.top, rcBlock.left + 4 + mt.cxWeek, rcCell.bottom};
        wsprintf(sz, "%d", CalWeekNumber(zFirst + row * 7));
        CalCenterText(hdc, rcWeek, sz);
      }
    }
    ::SelectObject(hdc, hOld);
  }
  // The line of today.
  if (mt.cyToday > 0) {
    int yToday = mt.y0 + mt.nRows * mt.cyMonth + (mt.nRows - 1) * kMonthGap;
    int x = mt.x0 + 4;
    if (!(dwStyle & MCS_NOTODAYCIRCLE)) {
      RECT rcMark = {x, yToday + 3, x + mt.cxCell - 4, yToday + mt.cyToday - 3};
      HBRUSH hbr = ::CreateSolidBrush(RGB(255, 0, 0));
      ::FrameRect(hdc, &rcMark, hbr);
      ::DeleteObject(hbr);
      x += mt.cxCell;
    }
    CString str = "Today: " + ShortDate(c->stToday);
    HGDIOBJ hOld = ::SelectObject(hdc, CalBold(c));
    ::SetTextColor(hdc, CalColor(c, MCSC_TEXT));
    RECT rcText = {x, yToday, rcClient.right, yToday + mt.cyToday};
    ::DrawText(hdc, str, str.GetLength(), &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(hdc, hOld);
  }
}

/// Scrolls the months of a calendar back (nDir -1) or on (nDir 1) by the
/// month delta.
void CalScroll(W16Cal* c, int nDir) {
  int nDelta = c->nDelta > 0 ? c->nDelta : CalMonthsShown(c);
  CalSetFirstMonth(c, c->nYear, c->nMonth + nDir * nDelta);
}

/// Moves the selection of a calendar to a day, as a key does: with Shift
/// and MCS_MULTISELECT the range grows from the anchor.
void CalKeyMove(W16Cal* c, long z) {
  BOOL bShift = ::GetKeyState(VK_SHIFT) < 0 && (StyleOf(c->hwnd) & MCS_MULTISELECT);
  if (!bShift) c->lAnchor = z;
  z = CalClampDay(c, z);
  if (CalSetSel(c, bShift ? c->lAnchor : z, z)) {
    CalEnsureVisible(c, z);
    CalNotify(c, MCN_SELCHANGE);
  }
}

/// Does a key of a calendar: the arrows move by a day or a week, Page Up
/// and Page Down by a month (with Ctrl, a year), and Home and End go to
/// the first and the last day of the month.
void CalKeyDown(W16Cal* c, UINT nKey) {
  long z = StDay(c->stSel1);
  if ((StyleOf(c->hwnd) & MCS_MULTISELECT) && z == c->lAnchor) z = StDay(c->stSel2);
  int y, m, d;
  FromDayNum(z, &y, &m, &d);
  BOOL bCtrl = ::GetKeyState(VK_CONTROL) < 0;
  switch (nKey) {
    case VK_LEFT:
      CalKeyMove(c, z - 1);
      break;
    case VK_RIGHT:
      CalKeyMove(c, z + 1);
      break;
    case VK_UP:
      CalKeyMove(c, z - 7);
      break;
    case VK_DOWN:
      CalKeyMove(c, z + 7);
      break;
    case VK_PRIOR:
    case VK_NEXT: {
      int nMonths = (nKey == VK_PRIOR ? -1 : 1) * (bCtrl ? 12 : 1);
      int n = y * 12 + m - 1 + nMonths;
      int yy = n / 12, mm = n % 12 + 1;
      int dd = d > DaysIn(yy, mm) ? DaysIn(yy, mm) : d;
      CalKeyMove(c, DayNum(yy, mm, dd));
      break;
    }
    case VK_HOME:
      CalKeyMove(c, DayNum(y, m, 1));
      break;
    case VK_END:
      CalKeyMove(c, DayNum(y, m, DaysIn(y, m)));
      break;
    case VK_ESCAPE:
    case VK_RETURN:
      if (StyleOf(c->hwnd) & WS_POPUP) {
        if (nKey == VK_RETURN) CalNotify(c, MCN_SELECT);
        ::PostMessage(::GetParent(c->hwnd), DTM_CLOSEMONTHCAL, 0, 0);
      }
      break;
  }
}

/// Does a press of the left button on a calendar: an arrow scrolls (and
/// repeats), a day starts a selection, and the line of today selects
/// today.
void CalButtonDown(W16Cal* c, POINT pt) {
  ::SetFocus(c->hwnd);
  SYSTEMTIME st;
  UINT uHit = CalHitTest(c, pt, &st);
  if (uHit == MCHT_TITLEBTNPREV || uHit == MCHT_TITLEBTNNEXT) {
    c->nPressed = uHit == MCHT_TITLEBTNPREV ? -1 : 1;
    CalScroll(c, c->nPressed);
    ::SetCapture(c->hwnd);
    ::SetTimer(c->hwnd, kCalTimer, 400, NULL);
    return;
  }
  if (uHit == MCHT_TODAYLINK) {
    long z = StDay(c->stToday);
    c->lAnchor = z;
    CalSetSel(c, z, z);
    CalEnsureVisible(c, z);
    CalNotify(c, MCN_SELCHANGE);
    CalNotify(c, MCN_SELECT);
    return;
  }
  if ((uHit & MCHT_CALENDARDATE) == MCHT_CALENDARDATE && (uHit & 0xFFFF) == 1) {
    long z = StDay(st);
    if (!CalInRange(c, z)) return;
    c->lAnchor = z;
    BOOL bChanged = CalSetSel(c, z, z);
    if (uHit & (MCHT_NEXT | MCHT_PREV)) CalEnsureVisible(c, z);
    if (bChanged) CalNotify(c, MCN_SELCHANGE);
    c->bSelecting = TRUE;
    ::SetCapture(c->hwnd);
  }
}

/// The window procedure of the month calendar class.
LRESULT CALLBACK W16CalProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Cal* c = StateOf<W16Cal>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Cal>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (c == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, c);
      break;
    case WM_CREATE:
      CalGetDayState(c);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      CalPaint(c, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      CalGetDayState(c);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      c->hFont = (HFONT)wParam;
      if (c->hBold != NULL) {
        ::DeleteObject(c->hBold);
        c->hBold = NULL;
      }
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)c->hFont;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      c->bFocus = nMsg == WM_SETFOCUS;
      ::InvalidateRect(hWnd, NULL, TRUE);
      // A drop-down calendar closes when it loses the focus.
      if (nMsg == WM_KILLFOCUS && (StyleOf(hWnd) & WS_POPUP))
        ::PostMessage(::GetParent(hWnd), DTM_CLOSEMONTHCAL, 0, 0);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | ((StyleOf(hWnd) & WS_POPUP) ? DLGC_WANTALLKEYS : 0);
    case WM_KEYDOWN:
      CalKeyDown(c, (UINT)wParam);
      return 0;
    case WM_LBUTTONDOWN: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      CalButtonDown(c, pt);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (c->bSelecting && (StyleOf(hWnd) & MCS_MULTISELECT)) {
        POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
        SYSTEMTIME st;
        UINT uHit = CalHitTest(c, pt, &st);
        if ((uHit & MCHT_CALENDARDATE) == MCHT_CALENDARDATE && (uHit & 0xFFFF) == 1 &&
            CalSetSel(c, c->lAnchor, StDay(st)))
          CalNotify(c, MCN_SELCHANGE);
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE: {
      BOOL bWasSelecting = c->bSelecting;
      c->bSelecting = FALSE;
      if (c->nPressed != 0) {
        c->nPressed = 0;
        ::KillTimer(hWnd, kCalTimer);
        ::InvalidateRect(hWnd, NULL, TRUE);
      }
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      if (bWasSelecting && nMsg == WM_LBUTTONUP) CalNotify(c, MCN_SELECT);
      return 0;
    }
    case WM_TIMER:
      if (wParam == kCalTimer && c->nPressed != 0) {
        ::SetTimer(hWnd, kCalTimer, 200, NULL);
        CalScroll(c, c->nPressed);
      }
      return 0;
    case MCM_GETCURSEL:
      if ((StyleOf(hWnd) & MCS_MULTISELECT) || lParam == 0) return FALSE;
      *(SYSTEMTIME FAR*)lParam = c->stSel1;
      return TRUE;
    case MCM_SETCURSEL: {
      if ((StyleOf(hWnd) & MCS_MULTISELECT) || lParam == 0) return FALSE;
      SYSTEMTIME st = *(const SYSTEMTIME FAR*)lParam;
      if (!StValid(st) || !CalInRange(c, StDay(st))) return FALSE;
      long z = StDay(st);
      CalSetSel(c, z, z);
      c->stSel1.wHour = st.wHour;
      c->stSel1.wMinute = st.wMinute;
      c->stSel1.wSecond = st.wSecond;
      c->stSel2 = c->stSel1;
      CalEnsureVisible(c, z);
      return TRUE;
    }
    case MCM_GETMAXSELCOUNT:
      return c->nMaxSel;
    case MCM_SETMAXSELCOUNT:
      if (!(StyleOf(hWnd) & MCS_MULTISELECT) || (int)wParam < 1) return FALSE;
      c->nMaxSel = (int)wParam;
      return TRUE;
    case MCM_GETSELRANGE: {
      if (!(StyleOf(hWnd) & MCS_MULTISELECT) || lParam == 0) return FALSE;
      SYSTEMTIME FAR* p = (SYSTEMTIME FAR*)lParam;
      p[0] = c->stSel1;
      p[1] = c->stSel2;
      return TRUE;
    }
    case MCM_SETSELRANGE: {
      if (!(StyleOf(hWnd) & MCS_MULTISELECT) || lParam == 0) return FALSE;
      const SYSTEMTIME FAR* p = (const SYSTEMTIME FAR*)lParam;
      if (!StValid(p[0]) || !StValid(p[1])) return FALSE;
      long z1 = StDay(p[0]), z2 = StDay(p[1]);
      if ((z2 > z1 ? z2 - z1 : z1 - z2) + 1 > c->nMaxSel) return FALSE;
      c->lAnchor = z1;
      CalSetSel(c, z1, z2);
      CalEnsureVisible(c, z1);
      return TRUE;
    }
    case MCM_GETMONTHRANGE: {
      CalMetrics mt = CalMeasure(c);
      int nShown = mt.nCols * mt.nRows;
      SYSTEMTIME FAR* p = (SYSTEMTIME FAR*)lParam;
      if (wParam == GMR_DAYSTATE) {
        if (p != NULL) {
          memset(&p[0], 0, sizeof(SYSTEMTIME));
          memset(&p[1], 0, sizeof(SYSTEMTIME));
          StSetDay(&p[0], CalFirstCell(c, 0));
          StSetDay(&p[1], CalFirstCell(c, nShown - 1) + 41);
        }
        return nShown + 2;
      }
      if (p != NULL) {
        int y, m;
        memset(&p[0], 0, sizeof(SYSTEMTIME));
        memset(&p[1], 0, sizeof(SYSTEMTIME));
        StSetDay(&p[0], DayNum(c->nYear, c->nMonth, 1));
        CalBlockMonth(c, nShown - 1, &y, &m);
        StSetDay(&p[1], DayNum(y, m, DaysIn(y, m)));
      }
      return nShown;
    }
    case MCM_SETDAYSTATE: {
      const MONTHDAYSTATE FAR* p = (const MONTHDAYSTATE FAR*)lParam;
      if (p == NULL) return FALSE;
      c->dayState.SetSize((int)wParam);
      for (int i = 0; i < (int)wParam; i++) c->dayState[i] = p[i];
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case MCM_GETMINREQRECT: {
      if (lParam == 0) return FALSE;
      CalMetrics mt = CalMeasure(c);
      RECT FAR* prc = (RECT FAR*)lParam;
      prc->left = prc->top = 0;
      prc->right = mt.cxMonth;
      prc->bottom = mt.cyMonth + mt.cyToday;
      return TRUE;
    }
    case MCM_SETCOLOR: {
      if (wParam > MCSC_TRAILINGTEXT) return (LRESULT)CLR_DEFAULT;
      COLORREF clrOld = c->clr[wParam];
      c->clr[wParam] = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)clrOld;
    }
    case MCM_GETCOLOR:
      if (wParam > MCSC_TRAILINGTEXT) return (LRESULT)CLR_DEFAULT;
      return (LRESULT)CalColor(c, (int)wParam);
    case MCM_SETTODAY:
      if (lParam != 0 && StValid(*(const SYSTEMTIME FAR*)lParam)) {
        c->stToday = *(const SYSTEMTIME FAR*)lParam;
        c->bTodaySet = TRUE;
      } else {
        c->stToday = StNow();
        c->bTodaySet = FALSE;
      }
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case MCM_GETTODAY:
      if (lParam == 0) return FALSE;
      *(SYSTEMTIME FAR*)lParam = c->stToday;
      return TRUE;
    case MCM_HITTEST: {
      MCHITTESTINFO FAR* p = (MCHITTESTINFO FAR*)lParam;
      if (p == NULL) return MCHT_NOWHERE;
      SYSTEMTIME st;
      p->uHit = CalHitTest(c, p->pt, &st);
      p->st = st;
      return (LRESULT)p->uHit;
    }
    case MCM_SETFIRSTDAYOFWEEK: {
      int nOld = c->nFirstDay;
      int n = (int)lParam;
      c->nFirstDay = n >= 0 && n <= 6 ? n : 6;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return MAKELONG(nOld, nOld != 6);
    }
    case MCM_GETFIRSTDAYOFWEEK:
      return MAKELONG(c->nFirstDay, c->nFirstDay != 6);
    case MCM_GETRANGE: {
      SYSTEMTIME FAR* p = (SYSTEMTIME FAR*)lParam;
      if (p != NULL) {
        p[0] = c->stMin;
        p[1] = c->stMax;
      }
      return (LRESULT)c->dwRange;
    }
    case MCM_SETRANGE: {
      const SYSTEMTIME FAR* p = (const SYSTEMTIME FAR*)lParam;
      c->dwRange = (DWORD)wParam & (GDTR_MIN | GDTR_MAX);
      if (p != NULL) {
        if (c->dwRange & GDTR_MIN) c->stMin = p[0];
        if (c->dwRange & GDTR_MAX) c->stMax = p[1];
      }
      CalSetSel(c, StDay(c->stSel1), StDay(c->stSel2));
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case MCM_GETMONTHDELTA:
      return c->nDelta > 0 ? c->nDelta : CalMonthsShown(c);
    case MCM_SETMONTHDELTA: {
      int nOld = c->nDelta;
      c->nDelta = (int)wParam;
      return nOld;
    }
    case MCM_GETMAXTODAYWIDTH: {
      CString str = "Today: " + ShortDate(c->stToday);
      if (c->hBold == NULL) CalBold(c);
      return DtTextWidth(hWnd, CalBold(c), str) + CalMeasure(c).cxCell;
    }
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The date and time picker

// The kinds of the fields of a format.
enum DtKind {
  kLiteral,
  kDay,      // d, dd
  kDayName,  // ddd, dddd
  kMonth,    // M, MM, MMM, MMMM
  kYear,     // y, yy, yyyy
  kHour12,   // h, hh
  kHour24,   // H, HH
  kMinute,   // m, mm
  kSecond,   // s, ss
  kAmPm,     // t, tt
  kCallback  // X, XX, ...
};

// One field of a format.
struct W16DtField {
  int kind;      // DtKind
  int nWidth;    // the number of letters
  CString text;  // the text of a literal, or the format of a callback field
};

/// Writes a field of a format to an archive. CArray::Serialize needs this
/// operator. The kind, the width and the text go to the archive.
CArchive& operator<<(CArchive& ar, const W16DtField& f) {
  return ar << (LONG)f.kind << (LONG)f.nWidth << f.text;
}

/// Reads a field of a format from an archive, in the sequence that
/// operator<< writes it.
CArchive& operator>>(CArchive& ar, W16DtField& f) {
  LONG kind, width;
  ar >> kind >> width >> f.text;
  f.kind = (int)kind;
  f.nWidth = (int)width;
  return ar;
}

// The timer of a pressed up-down button, which repeats.
const UINT kSpinTimer = 1;

// The data of a date and time picker.
struct W16Dtp {
  HWND hwnd;
  SYSTEMTIME st;
  BOOL bNone;            // DTS_SHOWNONE and the check box is clear
  CString format;        // DTM_SETFORMATA, or empty for the format of the style
  CArray<W16DtField, const W16DtField&> fields;
  CArray<RECT, const RECT&> rects;  // the rectangle of each field
  int iField;            // the field with the focus, or -1
  int nTyped, nTypedValue;  // the digits typed into the field
  DWORD dwRange;
  SYSTEMTIME stMin, stMax;
  HWND hwndCal;          // the drop-down calendar, or NULL
  HWND hwndEdit;         // the edit control of DTS_APPCANPARSE, or NULL
  HFONT hFont, hMcFont;
  COLORREF mcColors[6];
  DWORD dwMcStyle;
  BOOL bFocus;
  int nSpinPressed;      // the pressed half of the up-down button: 1 up, -1 down
  /// Starts on now, with no format of its own.
  W16Dtp()
      : hwnd(NULL), bNone(FALSE), iField(-1), nTyped(0), nTypedValue(0), dwRange(0),
        hwndCal(NULL), hwndEdit(NULL), hFont(NULL), hMcFont(NULL), dwMcStyle(0), bFocus(FALSE),
        nSpinPressed(0) {
    st = StNow();
    memset(&stMin, 0, sizeof(stMin));
    memset(&stMax, 0, sizeof(stMax));
    for (int i = 0; i < 6; i++) mcColors[i] = CLR_DEFAULT;
  }
};

/// Gives a string of the [intl] section of WIN.INI.
CString IntlString(LPCSTR lpszKey, LPCSTR lpszDefault) {
  char sz[64];
  ::GetProfileString("intl", lpszKey, lpszDefault, sz, sizeof(sz));
  return CString(sz);
}

/// Gives the format of the style of a picker: the short date, the short
/// date with the century, the long date or the time of WIN.INI.
CString DtStyleFormat(W16Dtp* p) {
  DWORD dwStyle = StyleOf(p->hwnd);
  if ((dwStyle & DTS_TIMEFORMAT) == DTS_TIMEFORMAT) {
    CString strSep = IntlString("sTime", ":");
    BOOL b24 = ::GetProfileInt("intl", "iTime", 0) != 0;
    BOOL bLead = ::GetProfileInt("intl", "iTLZero", 0) != 0;
    CString strHour = b24 ? (bLead ? "HH" : "H") : (bLead ? "hh" : "h");
    CString str = strHour + "'" + strSep + "'mm'" + strSep + "'ss";
    if (!b24) str += " tt";
    return str;
  }
  if ((dwStyle & DTS_SHORTDATECENTURYFORMAT) == DTS_SHORTDATECENTURYFORMAT) {
    CString str = IntlString("sShortDate", "M/d/yy");
    if (str.Find("yyyy") < 0) {
      int i = str.Find("yy");
      if (i >= 0) str = str.Left(i) + "yyyy" + str.Mid(i + 2);
    }
    return str;
  }
  if (dwStyle & DTS_LONGDATEFORMAT) return IntlString("sLongDate", "dddd, MMMM dd, yyyy");
  return IntlString("sShortDate", "M/d/yy");
}

/// Reads a format into fields: runs of the same letter, quoted literals
/// ('' is one quote) and other text.
void DtParseFormat(W16Dtp* p) {
  p->fields.RemoveAll();
  CString str = p->format.IsEmpty() ? DtStyleFormat(p) : p->format;
  int i = 0, n = str.GetLength();
  CString strLit;
  while (i < n) {
    char ch = str[i];
    int kind = -1;
    switch (ch) {
      case 'd':
        kind = kDay;
        break;
      case 'M':
        kind = kMonth;
        break;
      case 'y':
        kind = kYear;
        break;
      case 'h':
        kind = kHour12;
        break;
      case 'H':
        kind = kHour24;
        break;
      case 'm':
        kind = kMinute;
        break;
      case 's':
        kind = kSecond;
        break;
      case 't':
        kind = kAmPm;
        break;
      case 'X':
        kind = kCallback;
        break;
    }
    if (ch == '\'') {
      int j = i + 1;
      while (j < n) {
        if (str[j] == '\'') {
          if (j + 1 < n && str[j + 1] == '\'') {
            strLit += '\'';
            j += 2;
            continue;
          }
          break;
        }
        strLit += str[j];
        j++;
      }
      i = j + 1;
      continue;
    }
    if (kind < 0) {
      strLit += ch;
      i++;
      continue;
    }
    if (!strLit.IsEmpty()) {
      W16DtField f;
      f.kind = kLiteral;
      f.nWidth = 0;
      f.text = strLit;
      p->fields.Add(f);
      strLit.Empty();
    }
    int j = i;
    while (j < n && str[j] == ch) j++;
    W16DtField f;
    f.kind = kind;
    f.nWidth = j - i;
    if (kind == kDay && f.nWidth >= 3) f.kind = kDayName;
    if (kind == kCallback) f.text = str.Mid(i, j - i);
    p->fields.Add(f);
    i = j;
  }
  if (!strLit.IsEmpty()) {
    W16DtField f;
    f.kind = kLiteral;
    f.nWidth = 0;
    f.text = strLit;
    p->fields.Add(f);
  }
  p->iField = -1;
  p->nTyped = 0;
}

/// Gives TRUE when a field can take the focus.
BOOL DtEditable(const W16DtField& f) { return f.kind != kLiteral && f.kind != kDayName; }

/// Gives the text of a field for the date and time of the picker. A
/// callback field asks the parent (DTN_FORMATA).
CString DtFieldText(W16Dtp* p, const W16DtField& f) {
  char sz[64];
  const SYSTEMTIME& st = p->st;
  switch (f.kind) {
    case kLiteral:
      return f.text;
    case kDay:
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", (int)st.wDay);
      return CString(sz);
    case kDayName:
      return CString(DayName(DayOfWeek(StDay(st)), f.nWidth >= 4));
    case kMonth:
      if (f.nWidth >= 3) return CString(MonthName(st.wMonth, f.nWidth >= 4));
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", (int)st.wMonth);
      return CString(sz);
    case kYear:
      if (f.nWidth == 1)
        wsprintf(sz, "%d", (int)(st.wYear % 10));
      else if (f.nWidth == 2)
        wsprintf(sz, "%02d", (int)(st.wYear % 100));
      else
        wsprintf(sz, "%d", (int)st.wYear);
      return CString(sz);
    case kHour12: {
      int h = st.wHour % 12;
      if (h == 0) h = 12;
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", h);
      return CString(sz);
    }
    case kHour24:
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", (int)st.wHour);
      return CString(sz);
    case kMinute:
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", (int)st.wMinute);
      return CString(sz);
    case kSecond:
      wsprintf(sz, f.nWidth >= 2 ? "%02d" : "%d", (int)st.wSecond);
      return CString(sz);
    case kAmPm: {
      CString str = st.wHour < 12 ? IntlString("s1159", "AM") : IntlString("s2359", "PM");
      return f.nWidth >= 2 ? str : str.Left(1);
    }
    case kCallback: {
      NMDATETIMEFORMATA nm;
      memset(&nm, 0, sizeof(nm));
      nm.pszFormat = f.text;
      nm.st = st;
      nm.pszDisplay = nm.szDisplay;
      SendNotify(p->hwnd, DTN_FORMATA, &nm.nmhdr);
      return CString(nm.pszDisplay != NULL ? nm.pszDisplay : "");
    }
  }
  return CString();
}

/// Gives the width of the check box of DTS_SHOWNONE, or 0.
int DtCheckWidth(W16Dtp* p) { return (StyleOf(p->hwnd) & DTS_SHOWNONE) ? 18 : 0; }

/// Gives the rectangle of the drop-down or up-down button.
RECT DtButtonRect(W16Dtp* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  rc.left = rc.right - ::GetSystemMetrics(SM_CXVSCROLL);
  return rc;
}

/// Places the fields of a picker after the check box. A callback field
/// is as wide as the parent says (DTN_FORMATQUERYA).
void DtLayout(W16Dtp* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  HDC hdc = ::GetDC(p->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, DtFont(p->hFont));
  int x = 2 + DtCheckWidth(p);
  p->rects.SetSize(p->fields.GetSize());
  for (int i = 0; i < p->fields.GetSize(); i++) {
    const W16DtField& f = p->fields[i];
    int cx;
    if (f.kind == kCallback) {
      NMDATETIMEFORMATQUERYA nm;
      memset(&nm, 0, sizeof(nm));
      nm.pszFormat = f.text;
      SendNotify(p->hwnd, DTN_FORMATQUERYA, &nm.nmhdr);
      cx = nm.szMax.cx;
      if (cx <= 0) {
        CString str = DtFieldText(p, f);
        SIZE size;
        ::GetTextExtentPoint32(hdc, str, str.GetLength(), &size);
        cx = size.cx;
      }
    } else {
      CString str = DtFieldText(p, f);
      SIZE size;
      ::GetTextExtentPoint32(hdc, str, str.GetLength(), &size);
      cx = size.cx;
    }
    RECT r = {x, 2, x + cx, rc.bottom - 2};
    p->rects[i] = r;
    x += cx;
  }
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(p->hwnd, hdc);
}

/// Sends DTN_DATETIMECHANGE with the date and time, or GDT_NONE.
void DtNotifyChange(W16Dtp* p) {
  NMDATETIMECHANGE nm;
  memset(&nm, 0, sizeof(nm));
  nm.dwFlags = p->bNone ? GDT_NONE : GDT_VALID;
  nm.st = p->st;
  SendNotify(p->hwnd, DTN_DATETIMECHANGE, &nm.nmhdr);
}

/// Puts a date and time in the range of a picker.
void DtClamp(W16Dtp* p, SYSTEMTIME* pst) {
  if ((p->dwRange & GDTR_MIN) && StCompare(*pst, p->stMin, TRUE) < 0) *pst = p->stMin;
  if ((p->dwRange & GDTR_MAX) && StCompare(*pst, p->stMax, TRUE) > 0) *pst = p->stMax;
  pst->wDayOfWeek = (WORD)DayOfWeek(StDay(*pst));
}

/// Sets the date and time of a picker. With bNotify, the parent gets
/// DTN_DATETIMECHANGE when they change.
void DtSet(W16Dtp* p, SYSTEMTIME st, BOOL bNotify) {
  if (st.wDay > DaysIn(st.wYear, st.wMonth)) st.wDay = (WORD)DaysIn(st.wYear, st.wMonth);
  DtClamp(p, &st);
  BOOL bChanged = StCompare(st, p->st, TRUE) != 0;
  p->st = st;
  DtLayout(p);
  ::InvalidateRect(p->hwnd, NULL, TRUE);
  if (bChanged && bNotify) DtNotifyChange(p);
}

/// Adds a step (1 or -1, or more) to a field, with the wrap of comctl32:
/// the day goes round in its month, the month in the year, and the time
/// fields in their ranges.
void DtStep(W16Dtp* p, int iField, int nStep) {
  if (iField < 0 || iField >= p->fields.GetSize() || p->bNone) return;
  const W16DtField& f = p->fields[iField];
  SYSTEMTIME st = p->st;
  switch (f.kind) {
    case kDay: {
      int n = DaysIn(st.wYear, st.wMonth);
      st.wDay = (WORD)(((st.wDay - 1 + nStep) % n + n) % n + 1);
      break;
    }
    case kMonth:
      st.wMonth = (WORD)(((st.wMonth - 1 + nStep) % 12 + 12) % 12 + 1);
      break;
    case kYear:
      st.wYear = (WORD)ClampL((long)st.wYear + nStep, 1601, 9999);
      break;
    case kHour12:
    case kHour24:
      st.wHour = (WORD)(((st.wHour + nStep) % 24 + 24) % 24);
      break;
    case kMinute:
      st.wMinute = (WORD)(((st.wMinute + nStep) % 60 + 60) % 60);
      break;
    case kSecond:
      st.wSecond = (WORD)(((st.wSecond + nStep) % 60 + 60) % 60);
      break;
    case kAmPm:
      st.wHour = (WORD)((st.wHour + 12) % 24);
      break;
    default:
      return;
  }
  DtSet(p, st, TRUE);
}

/// Puts the digits typed into the field with the focus into the date or
/// the time, when they make a value that is valid.
void DtCommitTyped(W16Dtp* p) {
  if (p->nTyped == 0 || p->iField < 0) return;
  int n = p->nTypedValue;
  p->nTyped = 0;
  p->nTypedValue = 0;
  const W16DtField& f = p->fields[p->iField];
  SYSTEMTIME st = p->st;
  switch (f.kind) {
    case kDay:
      if (n < 1 || n > DaysIn(st.wYear, st.wMonth)) return;
      st.wDay = (WORD)n;
      break;
    case kMonth:
      if (n < 1 || n > 12) return;
      st.wMonth = (WORD)n;
      break;
    case kYear:
      if (f.nWidth <= 2) n += n < 30 ? 2000 : 1900;
      if (n < 1601 || n > 9999) return;
      st.wYear = (WORD)n;
      break;
    case kHour12:
      if (n < 1 || n > 12) return;
      st.wHour = (WORD)((n % 12) + (st.wHour >= 12 ? 12 : 0));
      break;
    case kHour24:
      if (n > 23) return;
      st.wHour = (WORD)n;
      break;
    case kMinute:
      if (n > 59) return;
      st.wMinute = (WORD)n;
      break;
    case kSecond:
      if (n > 59) return;
      st.wSecond = (WORD)n;
      break;
    default:
      return;
  }
  DtSet(p, st, TRUE);
}

/// Gives the most digits that a field takes.
int DtMaxDigits(const W16DtField& f) { return f.kind == kYear && f.nWidth > 2 ? 4 : 2; }

/// Moves the focus to the next (nDir 1) or the previous (nDir -1)
/// editable field from a field. Gives FALSE when there is none.
BOOL DtMoveField(W16Dtp* p, int nDir) {
  DtCommitTyped(p);
  int i = p->iField;
  int n = (int)p->fields.GetSize();
  for (int k = 0; k < n; k++) {
    i += nDir;
    if (i < 0 || i >= n) return FALSE;
    if (DtEditable(p->fields[i])) {
      p->iField = i;
      ::InvalidateRect(p->hwnd, NULL, TRUE);
      return TRUE;
    }
  }
  return FALSE;
}

/// Gives the first (or the last) editable field, or -1.
int DtEdgeField(W16Dtp* p, BOOL bLast) {
  int n = (int)p->fields.GetSize();
  for (int k = 0; k < n; k++) {
    int i = bLast ? n - 1 - k : k;
    if (DtEditable(p->fields[i])) return i;
  }
  return -1;
}

/// Closes the drop-down calendar and sends DTN_CLOSEUP.
void DtCloseCal(W16Dtp* p) {
  if (p->hwndCal == NULL) return;
  HWND hwndCal = p->hwndCal;
  p->hwndCal = NULL;
  ::DestroyWindow(hwndCal);
  NMHDR nm;
  SendNotify(p->hwnd, DTN_CLOSEUP, &nm);
  if (::IsWindow(p->hwnd)) ::SetFocus(p->hwnd);
}

/// Opens the drop-down calendar below the picker, with the date of the
/// picker, its range, its colors and its font, and sends DTN_DROPDOWN.
void DtDropDown(W16Dtp* p) {
  if (p->hwndCal != NULL || (StyleOf(p->hwnd) & DTS_UPDOWN)) return;
  HINSTANCE hInst = (HINSTANCE)::GetWindowWord(p->hwnd, GWW_HINSTANCE);
  p->hwndCal = ::CreateWindow(MONTHCAL_CLASS, "", WS_POPUP | WS_BORDER | p->dwMcStyle, 0, 0, 10, 10,
                              p->hwnd, NULL, hInst, NULL);
  if (p->hwndCal == NULL) return;
  if (p->hMcFont != NULL) ::SendMessage(p->hwndCal, WM_SETFONT, (WPARAM)p->hMcFont, 0);
  for (int i = 0; i < 6; i++)
    if (p->mcColors[i] != CLR_DEFAULT) ::SendMessage(p->hwndCal, MCM_SETCOLOR, i, (LPARAM)p->mcColors[i]);
  SYSTEMTIME aRange[2] = {p->stMin, p->stMax};
  ::SendMessage(p->hwndCal, MCM_SETRANGE, (WPARAM)p->dwRange, (LPARAM)(SYSTEMTIME FAR*)aRange);
  SYSTEMTIME st = p->st;
  ::SendMessage(p->hwndCal, MCM_SETCURSEL, 0, (LPARAM)(SYSTEMTIME FAR*)&st);
  RECT rcReq;
  ::SendMessage(p->hwndCal, MCM_GETMINREQRECT, 0, (LPARAM)(RECT FAR*)&rcReq);
  int cx = rcReq.right + 2, cy = rcReq.bottom + 2;
  RECT rcWnd;
  ::GetWindowRect(p->hwnd, &rcWnd);
  int x = (StyleOf(p->hwnd) & DTS_RIGHTALIGN) ? rcWnd.right - cx : rcWnd.left;
  int y = rcWnd.bottom;
  if (y + cy > ::GetSystemMetrics(SM_CYSCREEN)) y = rcWnd.top - cy;
  if (x < 0) x = 0;
  NMHDR nm;
  SendNotify(p->hwnd, DTN_DROPDOWN, &nm);
  if (p->hwndCal == NULL) return;
  ::SetWindowPos(p->hwndCal, HWND_TOP, x, y, cx, cy, SWP_SHOWWINDOW);
  ::SetFocus(p->hwndCal);
}

/// Draws a picker: the check box, the fields (the field with the focus
/// highlighted), and the drop-down or up-down button.
void DtPaint(W16Dtp* p, HDC hdc) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  BOOL bEnabled = ::IsWindowEnabled(p->hwnd);
  FillColor(hdc, rc, ::GetSysColor(bEnabled ? COLOR_WINDOW : COLOR_BTNFACE));
  DWORD dwStyle = StyleOf(p->hwnd);
  if (dwStyle & DTS_SHOWNONE) {
    int y = (rc.bottom - 13) / 2;
    RECT rcBox = {3, y, 16, y + 13};
    FillColor(hdc, rcBox, RGB(255, 255, 255));
    ::FrameRect(hdc, &rcBox, (HBRUSH)::GetStockObject(BLACK_BRUSH));
    if (!p->bNone) {
      static const int aTop[7] = {2, 3, 4, 3, 2, 1, 0};
      for (int i = 0; i < 7; i++) {
        RECT rcCol = {rcBox.left + 3 + i, rcBox.top + 3 + aTop[i], rcBox.left + 4 + i,
                      rcBox.top + 6 + aTop[i]};
        FillColor(hdc, rcCol, RGB(0, 0, 0));
      }
    }
  }
  HGDIOBJ hOld = ::SelectObject(hdc, DtFont(p->hFont));
  ::SetBkMode(hdc, TRANSPARENT);
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  int yText = (rc.bottom - tm.tmHeight) / 2;
  for (int i = 0; i < p->fields.GetSize() && i < p->rects.GetSize(); i++) {
    const W16DtField& f = p->fields[i];
    RECT r = p->rects[i];
    BOOL bHot = i == p->iField && p->bFocus && !p->bNone;
    if (bHot) FillColor(hdc, r, ::GetSysColor(COLOR_HIGHLIGHT));
    ::SetTextColor(hdc, !bEnabled || p->bNone ? ::GetSysColor(COLOR_GRAYTEXT)
                        : bHot ? ::GetSysColor(COLOR_HIGHLIGHTTEXT)
                               : ::GetSysColor(COLOR_WINDOWTEXT));
    CString str = DtFieldText(p, f);
    if (bHot && p->nTyped > 0) {
      char sz[8];
      wsprintf(sz, "%d", p->nTypedValue);
      str = sz;
    }
    ::TextOut(hdc, r.left, yText, str, str.GetLength());
  }
  ::SelectObject(hdc, hOld);
  RECT rcButton = DtButtonRect(p);
  if (dwStyle & DTS_UPDOWN) {
    RECT rcUp = rcButton, rcDown = rcButton;
    rcUp.bottom = rcDown.top = (rcButton.top + rcButton.bottom) / 2;
    DrawButtonFace(hdc, rcUp, p->nSpinPressed > 0);
    DrawArrow(hdc, rcUp, 0, bEnabled, p->nSpinPressed > 0);
    DrawButtonFace(hdc, rcDown, p->nSpinPressed < 0);
    DrawArrow(hdc, rcDown, 1, bEnabled, p->nSpinPressed < 0);
  } else {
    DrawButtonFace(hdc, rcButton, p->hwndCal != NULL);
    DrawArrow(hdc, rcButton, 1, bEnabled, p->hwndCal != NULL);
  }
}

LRESULT CALLBACK DtEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

/// Ends the text edit of DTS_APPCANPARSE: the parent gets the text in
/// DTN_USERSTRINGA and gives the date and time for it.
void DtEndEdit(W16Dtp* p, BOOL bSave) {
  if (p->hwndEdit == NULL) return;
  HWND hwndEdit = p->hwndEdit;
  p->hwndEdit = NULL;
  char sz[128];
  ::GetWindowText(hwndEdit, sz, sizeof(sz));
  ::DestroyWindow(hwndEdit);
  if (!bSave) return;
  NMDATETIMESTRINGA nm;
  memset(&nm, 0, sizeof(nm));
  nm.pszUserString = sz;
  nm.st = p->st;
  nm.dwFlags = GDT_VALID;
  SendNotify(p->hwnd, DTN_USERSTRINGA, &nm.nmhdr);
  if (nm.dwFlags == GDT_NONE && (StyleOf(p->hwnd) & DTS_SHOWNONE)) {
    p->bNone = TRUE;
    ::InvalidateRect(p->hwnd, NULL, TRUE);
    DtNotifyChange(p);
  } else if (nm.dwFlags == GDT_VALID && StValid(nm.st)) {
    p->bNone = FALSE;
    DtSet(p, nm.st, TRUE);
  }
}

/// The procedure of the edit control of DTS_APPCANPARSE: Enter saves, Esc
/// cancels, and the loss of the focus saves.
LRESULT CALLBACK DtEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Dtp* p = StateOf<W16Dtp>(::GetParent(hWnd));
  WNDPROC pfnOld = (WNDPROC)MAKELONG((WORD)(UINT)::GetProp(hWnd, "AfxDtp16Lo"),
                                     (WORD)(UINT)::GetProp(hWnd, "AfxDtp16Hi"));
  if (pfnOld == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
    case WM_KEYDOWN:
      if (p != NULL && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        HWND hwndDtp = p->hwnd;
        DtEndEdit(p, wParam == VK_RETURN);
        ::SetFocus(hwndDtp);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wParam == '\r' || wParam == 27) return 0;
      break;
    case WM_KILLFOCUS:
      if (p != NULL && p->hwndEdit == hWnd) {
        LRESULT lResult = ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
        DtEndEdit(p, TRUE);
        return lResult;
      }
      break;
    case WM_NCDESTROY:
      ::RemoveProp(hWnd, "AfxDtp16Lo");
      ::RemoveProp(hWnd, "AfxDtp16Hi");
      ::SetWindowLong(hWnd, GWL_WNDPROC, (LONG)pfnOld);
      break;
  }
  return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
}

/// Starts the text edit of DTS_APPCANPARSE: an edit control with the
/// text of the fields covers them.
void DtBeginEdit(W16Dtp* p) {
  if (p->hwndEdit != NULL) return;
  CString str;
  for (int i = 0; i < p->fields.GetSize(); i++) str += DtFieldText(p, p->fields[i]);
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  rc.left = DtCheckWidth(p);
  rc.right = DtButtonRect(p).left;
  HINSTANCE hInst = (HINSTANCE)::GetWindowWord(p->hwnd, GWW_HINSTANCE);
  p->hwndEdit = ::CreateWindow("EDIT", str, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, rc.left, rc.top,
                               rc.right - rc.left, rc.bottom, p->hwnd, (HMENU)1, hInst, NULL);
  if (p->hwndEdit == NULL) return;
  ::SendMessage(p->hwndEdit, WM_SETFONT, (WPARAM)DtFont(p->hFont), 0);
  DWORD dwOld = (DWORD)::SetWindowLong(p->hwndEdit, GWL_WNDPROC, (LONG)(WNDPROC)DtEditProc);
  ::SetProp(p->hwndEdit, "AfxDtp16Lo", (HANDLE)(UINT)LOWORD(dwOld));
  ::SetProp(p->hwndEdit, "AfxDtp16Hi", (HANDLE)(UINT)HIWORD(dwOld));
  ::SendMessage(p->hwndEdit, EM_SETSEL, 0, MAKELPARAM(0, -1));
  ::SetFocus(p->hwndEdit);
}

/// Does a key of a picker: the arrows change the field or the value,
/// digits type a value, and Alt+Down or F4 open the calendar. A callback
/// field gives its keys to the parent (DTN_WMKEYDOWNA).
void DtKeyDown(W16Dtp* p, UINT nKey) {
  BOOL bAlt = ::GetKeyState(VK_MENU) < 0;
  if ((nKey == VK_F4 || (bAlt && nKey == VK_DOWN)) && !(StyleOf(p->hwnd) & DTS_UPDOWN)) {
    DtDropDown(p);
    return;
  }
  if (nKey == VK_F2 && (StyleOf(p->hwnd) & DTS_APPCANPARSE)) {
    DtBeginEdit(p);
    return;
  }
  if (p->iField < 0) p->iField = DtEdgeField(p, FALSE);
  if (p->iField < 0) return;
  const W16DtField& f = p->fields[p->iField];
  if (f.kind == kCallback && nKey != VK_LEFT && nKey != VK_RIGHT && nKey != VK_TAB) {
    NMDATETIMEWMKEYDOWNA nm;
    memset(&nm, 0, sizeof(nm));
    nm.nVirtKey = (int)nKey;
    nm.pszFormat = f.text;
    nm.st = p->st;
    SendNotify(p->hwnd, DTN_WMKEYDOWNA, &nm.nmhdr);
    if (StValid(nm.st)) DtSet(p, nm.st, TRUE);
    return;
  }
  switch (nKey) {
    case VK_LEFT:
      DtMoveField(p, -1);
      break;
    case VK_RIGHT:
      DtMoveField(p, 1);
      break;
    case VK_HOME:
      DtCommitTyped(p);
      p->iField = DtEdgeField(p, FALSE);
      ::InvalidateRect(p->hwnd, NULL, TRUE);
      break;
    case VK_END:
      DtCommitTyped(p);
      p->iField = DtEdgeField(p, TRUE);
      ::InvalidateRect(p->hwnd, NULL, TRUE);
      break;
    case VK_UP:
    case VK_ADD:
      DtCommitTyped(p);
      DtStep(p, p->iField, 1);
      break;
    case VK_DOWN:
    case VK_SUBTRACT:
      DtCommitTyped(p);
      DtStep(p, p->iField, -1);
      break;
    case VK_PRIOR:
      DtCommitTyped(p);
      DtStep(p, p->iField, 5);
      break;
    case VK_NEXT:
      DtCommitTyped(p);
      DtStep(p, p->iField, -5);
      break;
    case VK_SPACE:
      if (StyleOf(p->hwnd) & DTS_SHOWNONE) {
        p->bNone = !p->bNone;
        ::InvalidateRect(p->hwnd, NULL, TRUE);
        DtNotifyChange(p);
      }
      break;
  }
}

/// Types a character into a picker: a digit goes into a number field (the
/// field moves on when it is full), and A or P sets the half of the day.
void DtChar(W16Dtp* p, char ch) {
  if (p->bNone || p->iField < 0) return;
  const W16DtField& f = p->fields[p->iField];
  if (f.kind == kAmPm) {
    CString strAm = IntlString("s1159", "AM"), strPm = IntlString("s2359", "PM");
    char chUp = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    SYSTEMTIME st = p->st;
    if (!strAm.IsEmpty() && chUp == strAm[0] && st.wHour >= 12) st.wHour -= 12;
    if (!strPm.IsEmpty() && chUp == strPm[0] && st.wHour < 12) st.wHour += 12;
    DtSet(p, st, TRUE);
    return;
  }
  if (ch < '0' || ch > '9') return;
  if (f.kind == kMonth && f.nWidth >= 3) return;
  p->nTypedValue = p->nTypedValue * 10 + (ch - '0');
  p->nTyped++;
  ::InvalidateRect(p->hwnd, NULL, TRUE);
  if (p->nTyped >= DtMaxDigits(f)) {
    DtCommitTyped(p);
    DtMoveField(p, 1);
  }
}

/// Does a press of the left button on a picker: the check box, a field,
/// the drop-down button or a half of the up-down button.
void DtButtonDown(W16Dtp* p, POINT pt) {
  if (::GetFocus() != p->hwnd) ::SetFocus(p->hwnd);
  DtEndEdit(p, TRUE);
  DWORD dwStyle = StyleOf(p->hwnd);
  if ((dwStyle & DTS_SHOWNONE) && pt.x < DtCheckWidth(p)) {
    p->bNone = !p->bNone;
    ::InvalidateRect(p->hwnd, NULL, TRUE);
    DtNotifyChange(p);
    return;
  }
  RECT rcButton = DtButtonRect(p);
  if (::PtInRect(&rcButton, pt)) {
    if (dwStyle & DTS_UPDOWN) {
      p->nSpinPressed = pt.y < (rcButton.top + rcButton.bottom) / 2 ? 1 : -1;
      if (p->iField < 0) p->iField = DtEdgeField(p, FALSE);
      DtCommitTyped(p);
      DtStep(p, p->iField, p->nSpinPressed);
      ::SetCapture(p->hwnd);
      ::SetTimer(p->hwnd, kSpinTimer, 400, NULL);
    } else if (p->hwndCal != NULL) {
      DtCloseCal(p);
    } else {
      DtDropDown(p);
    }
    return;
  }
  for (int i = 0; i < p->rects.GetSize(); i++) {
    RECT r = p->rects[i];
    if (pt.x >= r.left && pt.x < r.right && DtEditable(p->fields[i])) {
      DtCommitTyped(p);
      p->iField = i;
      ::InvalidateRect(p->hwnd, NULL, TRUE);
      return;
    }
  }
}

/// The window procedure of the date and time picker class.
LRESULT CALLBACK W16DtpProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Dtp* p = StateOf<W16Dtp>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Dtp>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, p);
      break;
    case WM_CREATE:
      DtParseFormat(p);
      DtLayout(p);
      return 0;
    case WM_DESTROY:
      if (p->hwndCal != NULL) {
        ::DestroyWindow(p->hwndCal);
        p->hwndCal = NULL;
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      DtPaint(p, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      DtLayout(p);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      p->hFont = (HFONT)wParam;
      DtLayout(p);
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)p->hFont;
    case WM_SETFOCUS:
      p->bFocus = TRUE;
      if (p->iField < 0) p->iField = DtEdgeField(p, FALSE);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_KILLFOCUS:
      p->bFocus = FALSE;
      DtCommitTyped(p);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_ENABLE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH: {
      // The text is the text of the fields, as the picker shows it.
      CString str;
      if (!p->bNone)
        for (int i = 0; i < p->fields.GetSize(); i++) str += DtFieldText(p, p->fields[i]);
      if (nMsg == WM_GETTEXTLENGTH) return str.GetLength();
      if (lParam == 0 || wParam == 0) return 0;
      int n = str.GetLength() < (int)wParam - 1 ? str.GetLength() : (int)wParam - 1;
      _fmemcpy((LPSTR)lParam, (LPCSTR)str, n);
      ((LPSTR)lParam)[n] = '\0';
      return n;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (nMsg == WM_SYSKEYDOWN && wParam != VK_DOWN) break;
      DtKeyDown(p, (UINT)wParam);
      return 0;
    case WM_CHAR:
      DtChar(p, (char)wParam);
      return 0;
    case WM_LBUTTONDOWN: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      DtButtonDown(p, pt);
      return 0;
    }
    case WM_LBUTTONDBLCLK:
      if (StyleOf(hWnd) & DTS_APPCANPARSE) DtBeginEdit(p);
      return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
      if (p->nSpinPressed != 0) {
        p->nSpinPressed = 0;
        ::KillTimer(hWnd, kSpinTimer);
        ::InvalidateRect(hWnd, NULL, TRUE);
      }
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      return 0;
    case WM_TIMER:
      if (wParam == kSpinTimer && p->nSpinPressed != 0) {
        ::SetTimer(hWnd, kSpinTimer, 100, NULL);
        DtStep(p, p->iField, p->nSpinPressed);
      }
      return 0;
    case WM_NOTIFY: {
      // The notifications of the drop-down calendar.
      const NMHDR FAR* pnm = (const NMHDR FAR*)lParam;
      if (pnm != NULL && pnm->hwndFrom == p->hwndCal && p->hwndCal != NULL) {
        if (pnm->code == MCN_SELECT) {
          const NMSELCHANGE FAR* psel = (const NMSELCHANGE FAR*)lParam;
          SYSTEMTIME st = p->st;
          st.wYear = psel->stSelStart.wYear;
          st.wMonth = psel->stSelStart.wMonth;
          st.wDay = psel->stSelStart.wDay;
          p->bNone = FALSE;
          DtSet(p, st, TRUE);
          ::PostMessage(hWnd, DTM_CLOSEMONTHCAL, 0, 0);
        }
        return 0;
      }
      return ::SendMessage(::GetParent(hWnd), nMsg, wParam, lParam);
    }
    case DTM_CLOSEMONTHCAL:
      DtCloseCal(p);
      return 0;
    case DTM_GETSYSTEMTIME:
      if (lParam == 0) return GDT_ERROR;
      *(SYSTEMTIME FAR*)lParam = p->st;
      return p->bNone ? GDT_NONE : GDT_VALID;
    case DTM_SETSYSTEMTIME:
      if (wParam == GDT_NONE) {
        if (!(StyleOf(hWnd) & DTS_SHOWNONE)) return FALSE;
        p->bNone = TRUE;
        ::InvalidateRect(hWnd, NULL, TRUE);
        return TRUE;
      }
      if (lParam == 0 || !StValid(*(const SYSTEMTIME FAR*)lParam)) return FALSE;
      p->bNone = FALSE;
      DtSet(p, *(const SYSTEMTIME FAR*)lParam, FALSE);
      return TRUE;
    case DTM_GETRANGE: {
      SYSTEMTIME FAR* pst = (SYSTEMTIME FAR*)lParam;
      if (pst != NULL) {
        pst[0] = p->stMin;
        pst[1] = p->stMax;
      }
      return (LRESULT)p->dwRange;
    }
    case DTM_SETRANGE: {
      const SYSTEMTIME FAR* pst = (const SYSTEMTIME FAR*)lParam;
      p->dwRange = (DWORD)wParam & (GDTR_MIN | GDTR_MAX);
      if (pst != NULL) {
        if (p->dwRange & GDTR_MIN) p->stMin = pst[0];
        if (p->dwRange & GDTR_MAX) p->stMax = pst[1];
      }
      DtSet(p, p->st, FALSE);
      return TRUE;
    }
    case DTM_SETFORMATA:
      p->format = lParam != 0 ? (LPCSTR)lParam : "";
      DtParseFormat(p);
      DtLayout(p);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    case DTM_SETMCCOLOR: {
      if (wParam > MCSC_TRAILINGTEXT) return (LRESULT)CLR_DEFAULT;
      COLORREF clrOld = p->mcColors[wParam];
      p->mcColors[wParam] = (COLORREF)lParam;
      if (p->hwndCal != NULL) ::SendMessage(p->hwndCal, MCM_SETCOLOR, wParam, lParam);
      return (LRESULT)clrOld;
    }
    case DTM_GETMCCOLOR:
      if (wParam > MCSC_TRAILINGTEXT) return (LRESULT)CLR_DEFAULT;
      return (LRESULT)p->mcColors[wParam];
    case DTM_GETMONTHCAL:
      return (LRESULT)(UINT)p->hwndCal;
    case DTM_SETMCFONT:
      p->hMcFont = (HFONT)wParam;
      if (p->hwndCal != NULL) ::SendMessage(p->hwndCal, WM_SETFONT, wParam, lParam);
      return 0;
    case DTM_GETMCFONT:
      return (LRESULT)(UINT)p->hMcFont;
    case DTM_SETMCSTYLE: {
      DWORD dwOld = p->dwMcStyle;
      p->dwMcStyle = (DWORD)lParam & 0xFFFF;
      return (LRESULT)dwOld;
    }
    case DTM_GETMCSTYLE:
      return (LRESULT)p->dwMcStyle;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the month calendar and the date and time picker classes
/// (AFX_WNDCOMMCTL_DATE_REG). Gives TRUE when both are there.
BOOL AFXAPI _AfxW16RegisterDateClasses() {
  return RegisterCtlClasses(MONTHCAL_CLASS, "SysMonthCal32", W16CalProc) &&
         RegisterCtlClasses(DATETIMEPICK_CLASS, "SysDateTimePick32", W16DtpProc);
}

#endif  // _AFX_WIN16
