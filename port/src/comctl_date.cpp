// comctl_date.cpp - the month calendar and the date and time picker.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "comctl.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

DWORD StyleOf(HWND h) { return (DWORD)GetWindowLongW(h, GWL_STYLE); }

void FillSolid(HDC dc, const RECT& r, COLORREF c) {
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

// ---------------------------------------------------------------------
// Dates

bool Leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int DaysIn(int y, int m) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  return m == 2 && Leap(y) ? 29 : days[m - 1];
}

// 0 for Sunday.
int DayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Days since 1 January 1601, for arithmetic.
long DayNumber(int y, int m, int d) {
  long n = 0;
  int yy = y - 1601;
  n = yy * 365L + yy / 4 - yy / 100 + yy / 400;
  for (int k = 1; k < m; k++) n += DaysIn(y, k);
  return n + d - 1;
}

void FromDayNumber(long n, SYSTEMTIME* st) {
  int y = 1601;
  while (true) {
    int len = Leap(y) ? 366 : 365;
    if (n < len) break;
    n -= len;
    y++;
  }
  int m = 1;
  while (n >= DaysIn(y, m)) n -= DaysIn(y, m++);
  st->wYear = (WORD)y;
  st->wMonth = (WORD)m;
  st->wDay = (WORD)(n + 1);
  st->wDayOfWeek = (WORD)DayOfWeek(y, m, st->wDay);
}

long DayOf(const SYSTEMTIME& st) { return DayNumber(st.wYear, st.wMonth, st.wDay); }

bool ValidDate(const SYSTEMTIME& st) {
  return st.wYear >= 1601 && st.wYear <= 30827 && st.wMonth >= 1 && st.wMonth <= 12 && st.wDay >= 1 &&
         st.wDay <= DaysIn(st.wYear, st.wMonth);
}

bool ValidTime(const SYSTEMTIME& st) {
  return st.wHour < 24 && st.wMinute < 60 && st.wSecond < 60 && st.wMilliseconds < 1000;
}

void FixDayOfWeek(SYSTEMTIME* st) { st->wDayOfWeek = (WORD)DayOfWeek(st->wYear, st->wMonth, st->wDay); }

// Compares dates and times.
int Compare(const SYSTEMTIME& a, const SYSTEMTIME& b, bool withTime) {
  long da = DayOf(a), db = DayOf(b);
  if (da != db) return da < db ? -1 : 1;
  if (!withTime) return 0;
  long ta = (a.wHour * 60L + a.wMinute) * 60 + a.wSecond, tb = (b.wHour * 60L + b.wMinute) * 60 + b.wSecond;
  return ta < tb ? -1 : ta > tb ? 1 : 0;
}

SYSTEMTIME Today() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
  return st;
}

std::wstring LocaleString(LCTYPE t) {
  wchar_t buf[128];
  int n = GetLocaleInfoW(LOCALE_USER_DEFAULT, t, buf, 128);
  return n > 0 ? std::wstring(buf) : std::wstring();
}

std::wstring MonthName(int m, bool abbrev) {
  return LocaleString((abbrev ? LOCALE_SABBREVMONTHNAME1 : LOCALE_SMONTHNAME1) + m - 1);
}

// Sunday is 0 here; the locale counts from Monday.
std::wstring DayName(int dow, bool abbrev) {
  return LocaleString((abbrev ? LOCALE_SABBREVDAYNAME1 : LOCALE_SDAYNAME1) + (dow + 6) % 7);
}

// =====================================================================
// Month calendar

struct MonthCal {
  HWND hwnd = NULL;
  HFONT font = NULL, bold = NULL;
  SYSTEMTIME sel, selEnd, today;
  bool todaySet = false;
  SYSTEMTIME rangeMin, rangeMax;
  DWORD rangeFlags = 0;
  int firstDay = 0;  // 0 Sunday
  bool firstDaySet = false;
  int year = 0, month = 0;  // the month shown
  COLORREF colors[6];
  int maxSel = 7;
  int delta = 0;
  std::vector<MONTHDAYSTATE> dayState;  // for the month before, the month shown and the one after
  // Layout
  int cellW = 0, cellH = 0, titleH = 0, todayH = 0, weekW = 0;
  bool selecting = false;
  long anchor = 0;
};

MonthCal* CalOf(HWND h) { return (MonthCal*)GetWindowLongPtrW(h, 0); }
bool Multi(MonthCal* c) { return (StyleOf(c->hwnd) & MCS_MULTISELECT) != 0; }

SYSTEMTIME TodayOf(MonthCal* c) { return c->todaySet ? c->today : Today(); }

void Measure(MonthCal* c) {
  HDC dc = GetDC(c->hwnd);
  HGDIOBJ old = SelectObject(dc, c->font ? c->font : ControlFont());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SIZE sz;
  GetTextExtentPoint32W(dc, L"00", 2, &sz);
  c->cellW = sz.cx + 8;
  for (int d = 0; d < 7; d++) {
    std::wstring n = DayName(d, true).substr(0, 3);
    GetTextExtentPoint32W(dc, n.c_str(), (int)n.size(), &sz);
    c->cellW = std::max(c->cellW, (int)sz.cx + 6);
  }
  c->cellH = tm.tmHeight + 2;
  c->titleH = tm.tmHeight + 8;
  c->todayH = (StyleOf(c->hwnd) & MCS_NOTODAY) ? 0 : tm.tmHeight + 4;
  c->weekW = (StyleOf(c->hwnd) & MCS_WEEKNUMBERS) ? c->cellW : 0;
  SelectObject(dc, old);
  ReleaseDC(c->hwnd, dc);
}

SIZE MinSize(MonthCal* c) {
  Measure(c);
  SIZE s = {c->weekW + 7 * c->cellW + 8, c->titleH + c->cellH + 6 * c->cellH + 6 + c->todayH};
  return s;
}

// The grid's first cell: the day shown at the top left.
long GridStart(MonthCal* c) {
  long first = DayNumber(c->year, c->month, 1);
  int lead = (DayOfWeek(c->year, c->month, 1) - c->firstDay + 7) % 7;
  if (lead == 0) lead = 7;  // a row of the month before, as Windows shows it
  return first - lead;
}

RECT GridRect(MonthCal* c) {
  RECT r;
  GetClientRect(c->hwnd, &r);
  int w = c->weekW + 7 * c->cellW;
  int x = std::max(4, (int)(r.right - w) / 2) + c->weekW;
  int y = c->titleH + c->cellH + 2;
  RECT g = {x, y, x + 7 * c->cellW, y + 6 * c->cellH};
  return g;
}

bool InRange(MonthCal* c, long day) {
  if ((c->rangeFlags & GDTR_MIN) && day < DayOf(c->rangeMin)) return false;
  if ((c->rangeFlags & GDTR_MAX) && day > DayOf(c->rangeMax)) return false;
  return true;
}

void Show(MonthCal* c, int year, int month) {
  while (month < 1) month += 12, year--;
  while (month > 12) month -= 12, year++;
  if (year < 1601 || year > 30827) return;
  if (year == c->year && month == c->month) return;
  c->year = year;
  c->month = month;
  if (StyleOf(c->hwnd) & MCS_DAYSTATE) {
    MONTHDAYSTATE states[3] = {0, 0, 0};
    NMDAYSTATE nm = {};
    SYSTEMTIME st = {};
    st.wYear = (WORD)(month == 1 ? year - 1 : year);
    st.wMonth = (WORD)(month == 1 ? 12 : month - 1);
    st.wDay = 1;
    FixDayOfWeek(&st);
    nm.stStart = st;
    nm.cDayState = 3;
    nm.prgDayState = states;
    SendNotify(c->hwnd, NULL, MCN_GETDAYSTATE, &nm.nmhdr);
    c->dayState.assign(nm.prgDayState, nm.prgDayState + 3);
  }
  InvalidateRect(c->hwnd, NULL, TRUE);
}

void NotifySel(MonthCal* c, UINT code) {
  NMSELCHANGE nm = {};
  nm.stSelStart = c->sel;
  nm.stSelEnd = Multi(c) ? c->selEnd : c->sel;
  SendNotify(c->hwnd, NULL, code, &nm.nmhdr);
}

void Select(MonthCal* c, long start, long end, bool notify) {
  if (start > end) std::swap(start, end);
  if (Multi(c) && end - start + 1 > c->maxSel) end = start + c->maxSel - 1;
  if (!InRange(c, start) || !InRange(c, end)) return;
  SYSTEMTIME a = c->sel, b = c->selEnd;
  FromDayNumber(start, &c->sel);
  FromDayNumber(end, &c->selEnd);
  c->sel.wHour = c->sel.wMinute = c->sel.wSecond = c->sel.wMilliseconds = 0;
  c->selEnd.wHour = c->selEnd.wMinute = c->selEnd.wSecond = c->selEnd.wMilliseconds = 0;
  Show(c, c->sel.wYear, c->sel.wMonth);
  InvalidateRect(c->hwnd, NULL, TRUE);
  if (notify && (DayOf(a) != start || DayOf(b) != end)) NotifySel(c, MCN_SELCHANGE);
}

UINT HitTest(MonthCal* c, POINT p, SYSTEMTIME* st) {
  RECT r;
  GetClientRect(c->hwnd, &r);
  if (!PtInRect(&r, p)) return MCHT_NOWHERE;
  RECT g = GridRect(c);
  if (p.y < c->titleH) {
    if (p.x < g.left + c->cellW) return MCHT_TITLEBTNPREV;
    if (p.x >= g.right - c->cellW) return MCHT_TITLEBTNNEXT;
    return p.x < (g.left + g.right) / 2 ? MCHT_TITLEMONTH : MCHT_TITLEYEAR;
  }
  if (p.y < g.top) return MCHT_CALENDARDAY;
  if (p.y < g.bottom && p.x >= g.left && p.x < g.right) {
    int col = (p.x - g.left) / c->cellW, row = (p.y - g.top) / c->cellH;
    long day = GridStart(c) + row * 7 + col;
    SYSTEMTIME d = {};
    FromDayNumber(day, &d);
    if (st) *st = d;
    if (d.wMonth != c->month) return (DayNumber(c->year, c->month, 1) > day) ? MCHT_CALENDARDATEPREV : MCHT_CALENDARDATENEXT;
    return MCHT_CALENDARDATE;
  }
  if (p.y < g.bottom && p.x < g.left && c->weekW) return MCHT_CALENDARWEEKNUM;
  if (c->todayH && p.y >= g.bottom + 4) {
    if (st) *st = TodayOf(c);
    return MCHT_TODAYLINK;
  }
  return MCHT_CALENDARBK;
}

int WeekNumber(long day) {
  // ISO weeks, from the Thursday of the week.
  SYSTEMTIME st;
  FromDayNumber(day, &st);
  int dow = (st.wDayOfWeek + 6) % 7;  // Monday 0
  long thursday = day - dow + 3;
  SYSTEMTIME t;
  FromDayNumber(thursday, &t);
  return (int)((thursday - DayNumber(t.wYear, 1, 1)) / 7 + 1);
}

void PaintCal(MonthCal* c, HDC dc) {
  Measure(c);
  RECT r;
  GetClientRect(c->hwnd, &r);
  FillSolid(dc, r, c->colors[MCSC_BACKGROUND]);
  RECT g = GridRect(c);
  RECT month = {g.left - c->weekW, 0, g.right, g.bottom + c->todayH + 4};
  FillSolid(dc, month, c->colors[MCSC_MONTHBK]);
  HFONT font = c->font ? c->font : ControlFont();
  if (!c->bold) {
    LOGFONTW lf;
    GetObjectW(font, sizeof lf, &lf);
    lf.lfWeight = FW_BOLD;
    c->bold = CreateFontIndirectW(&lf);
  }
  HGDIOBJ old = SelectObject(dc, c->bold);
  SetBkMode(dc, TRANSPARENT);
  // The title: the month and year, between the arrows.
  RECT title = {g.left - c->weekW, 0, g.right, c->titleH};
  FillSolid(dc, title, c->colors[MCSC_TITLEBK]);
  SetTextColor(dc, c->colors[MCSC_TITLETEXT]);
  std::wstring t = MonthName(c->month, false) + L" " + std::to_wstring(c->year);
  DrawTextW(dc, t.c_str(), (int)t.size(), &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  RECT prev = {g.left, 2, g.left + c->cellW, c->titleH - 2}, next = {g.right - c->cellW, 2, g.right, c->titleH - 2};
  DrawFrameControl(dc, &prev, DFC_SCROLL, DFCS_SCROLLLEFT | DFCS_FLAT);
  DrawFrameControl(dc, &next, DFC_SCROLL, DFCS_SCROLLRIGHT | DFCS_FLAT);
  // The days of the week.
  SelectObject(dc, font);
  SetTextColor(dc, c->colors[MCSC_TITLEBK]);
  for (int k = 0; k < 7; k++) {
    std::wstring n = DayName((c->firstDay + k) % 7, true).substr(0, 3);
    RECT cell = {g.left + k * c->cellW, c->titleH, g.left + (k + 1) * c->cellW, c->titleH + c->cellH};
    DrawTextW(dc, n.c_str(), (int)n.size(), &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
  HGDIOBJ op = SelectObject(dc, pen);
  MoveToEx(dc, g.left, g.top - 2, NULL);
  LineTo(dc, g.right, g.top - 2);
  // The days.
  long start = GridStart(c), selA = DayOf(c->sel), selB = Multi(c) ? DayOf(c->selEnd) : selA;
  long today = DayOf(TodayOf(c));
  for (int row = 0; row < 6; row++) {
    if (c->weekW) {
      RECT wk = {g.left - c->weekW, g.top + row * c->cellH, g.left, g.top + (row + 1) * c->cellH};
      std::wstring n = std::to_wstring(WeekNumber(start + row * 7));
      SetTextColor(dc, c->colors[MCSC_TRAILINGTEXT]);
      DrawTextW(dc, n.c_str(), (int)n.size(), &wk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    for (int col = 0; col < 7; col++) {
      long day = start + row * 7 + col;
      SYSTEMTIME d;
      FromDayNumber(day, &d);
      RECT cell = {g.left + col * c->cellW, g.top + row * c->cellH, g.left + (col + 1) * c->cellW,
                   g.top + (row + 1) * c->cellH};
      bool inMonth = d.wMonth == c->month;
      bool selected = day >= selA && day <= selB;
      if (selected) {
        RECT s = cell;
        InflateRect(&s, -1, 0);
        FillSolid(dc, s, GetSysColor(COLOR_HIGHLIGHT));
      }
      bool bold = false;
      if (!c->dayState.empty()) {
        int which = inMonth ? 1 : (day < DayNumber(c->year, c->month, 1) ? 0 : 2);
        bold = (c->dayState[which] >> (d.wDay - 1)) & 1;
      }
      SelectObject(dc, bold ? c->bold : font);
      COLORREF fc = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                    : !inMonth || !InRange(c, day) ? c->colors[MCSC_TRAILINGTEXT]
                                                   : c->colors[MCSC_TEXT];
      SetTextColor(dc, fc);
      std::wstring n = std::to_wstring(d.wDay);
      DrawTextW(dc, n.c_str(), (int)n.size(), &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      if (day == today && !(StyleOf(c->hwnd) & MCS_NOTODAYCIRCLE)) {
        HPEN red = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
        HGDIOBJ o2 = SelectObject(dc, red);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, o2);
        DeleteObject(red);
      }
    }
  }
  SelectObject(dc, op);
  DeleteObject(pen);
  // Today.
  if (c->todayH) {
    SYSTEMTIME td = TodayOf(c);
    wchar_t buf[64];
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &td, NULL, buf, 64)) buf[0] = 0;
    std::wstring s = std::wstring(L"Today: ") + buf;
    SelectObject(dc, c->bold);
    SetTextColor(dc, c->colors[MCSC_TEXT]);
    RECT tr = {g.left, g.bottom + 4, g.right, g.bottom + 4 + c->todayH};
    DrawTextW(dc, s.c_str(), (int)s.size(), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  SelectObject(dc, old);
}

void MoveSel(MonthCal* c, long days) {
  long at = DayOf(c->sel) + days;
  if (!InRange(c, at)) return;
  Select(c, at, at, true);
}

LRESULT CALLBACK MonthCalProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  MonthCal* c = CalOf(h);
  if (msg == WM_NCCREATE) {
    c = new MonthCal;
    c->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)c);
    c->sel = c->selEnd = Today();
    c->year = c->sel.wYear;
    c->month = c->sel.wMonth;
    memset(&c->rangeMin, 0, sizeof c->rangeMin);
    memset(&c->rangeMax, 0, sizeof c->rangeMax);
    c->firstDay = (int)(wcstol(LocaleString(LOCALE_IFIRSTDAYOFWEEK).c_str(), NULL, 10) + 1) % 7;
    c->colors[MCSC_BACKGROUND] = GetSysColor(COLOR_WINDOW);
    c->colors[MCSC_TEXT] = GetSysColor(COLOR_WINDOWTEXT);
    c->colors[MCSC_TITLEBK] = GetSysColor(COLOR_ACTIVECAPTION);
    c->colors[MCSC_TITLETEXT] = GetSysColor(COLOR_CAPTIONTEXT);
    c->colors[MCSC_MONTHBK] = GetSysColor(COLOR_WINDOW);
    c->colors[MCSC_TRAILINGTEXT] = GetSysColor(COLOR_GRAYTEXT);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!c) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      if (c->bold) DeleteObject(c->bold);
      delete c;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE:
      if (StyleOf(h) & MCS_DAYSTATE) {
        int y = c->year, m = c->month;
        c->year = 0;
        Show(c, y, m);
      }
      return 0;
    case WM_SETFONT:
      c->font = (HFONT)wp;
      if (c->bold) DeleteObject(c->bold);
      c->bold = NULL;
      if (LOWORD(lp)) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)c->font;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintCal(c, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN: {
      SetFocus(h);
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      SYSTEMTIME st = {};
      UINT hit = HitTest(c, p, &st);
      int step = c->delta ? c->delta : 1;
      if (hit == MCHT_TITLEBTNPREV || hit == MCHT_TITLEBTNNEXT) {
        int dm = hit == MCHT_TITLEBTNPREV ? -step : step;
        // The selection moves with the month, staying in it.
        int y = c->year, m = c->month + dm;
        while (m < 1) m += 12, y--;
        while (m > 12) m -= 12, y++;
        int d = std::min((int)c->sel.wDay, DaysIn(y, m));
        long at = DayNumber(y, m, d);
        if (!Multi(c) && InRange(c, at))
          Select(c, at, at, true);
        else
          Show(c, y, m);
      } else if ((hit & MCHT_CALENDARDATE) == MCHT_CALENDARDATE || hit == MCHT_TODAYLINK) {
        long at = DayOf(st);
        if (!InRange(c, at)) return 0;
        c->anchor = at;
        bool extend = Multi(c) && (wp & MK_SHIFT);
        Select(c, extend ? DayOf(c->sel) : at, at, true);
        c->selecting = Multi(c);
        if (c->selecting)
          SetCapture(h);
        else
          NotifySel(c, MCN_SELECT);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (c->selecting) {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        SYSTEMTIME st;
        if ((HitTest(c, p, &st) & MCHT_CALENDARDATE) == MCHT_CALENDARDATE) Select(c, c->anchor, DayOf(st), true);
      }
      return 0;
    case WM_LBUTTONUP:
      if (c->selecting) {
        c->selecting = false;
        ReleaseCapture();
        NotifySel(c, MCN_SELECT);
      }
      return 0;
    case WM_KEYDOWN:
      switch (wp) {
        case VK_LEFT: MoveSel(c, -1); return 0;
        case VK_RIGHT: MoveSel(c, 1); return 0;
        case VK_UP: MoveSel(c, -7); return 0;
        case VK_DOWN: MoveSel(c, 7); return 0;
        case VK_PRIOR:
        case VK_NEXT: {
          int y = c->sel.wYear, m = c->sel.wMonth + (wp == VK_PRIOR ? -1 : 1);
          while (m < 1) m += 12, y--;
          while (m > 12) m -= 12, y++;
          long at = DayNumber(y, m, std::min((int)c->sel.wDay, DaysIn(y, m)));
          if (InRange(c, at)) Select(c, at, at, true);
          return 0;
        }
        case VK_HOME: {
          long at = DayNumber(c->sel.wYear, c->sel.wMonth, 1);
          if (InRange(c, at)) Select(c, at, at, true);
          return 0;
        }
        case VK_END: {
          long at = DayNumber(c->sel.wYear, c->sel.wMonth, DaysIn(c->sel.wYear, c->sel.wMonth));
          if (InRange(c, at)) Select(c, at, at, true);
          return 0;
        }
        case VK_RETURN:
        case VK_SPACE:
          NotifySel(c, MCN_SELECT);
          return 0;
      }
      return DefWindowProcW(h, msg, wp, lp);
    case MCM_GETCURSEL:
      if (Multi(c) || !lp) return FALSE;
      *(SYSTEMTIME*)lp = c->sel;
      return TRUE;
    case MCM_SETCURSEL: {
      if (Multi(c) || !lp) return FALSE;
      SYSTEMTIME st = *(const SYSTEMTIME*)lp;
      if (!ValidDate(st) || !InRange(c, DayOf(st))) return FALSE;
      Select(c, DayOf(st), DayOf(st), false);
      return TRUE;
    }
    case MCM_GETMAXSELCOUNT:
      return c->maxSel;
    case MCM_SETMAXSELCOUNT:
      if (!Multi(c) || (int)wp < 1) return FALSE;
      c->maxSel = (int)wp;
      return TRUE;
    case MCM_GETSELRANGE: {
      if (!Multi(c) || !lp) return FALSE;
      SYSTEMTIME* st = (SYSTEMTIME*)lp;
      st[0] = c->sel;
      st[1] = c->selEnd;
      return TRUE;
    }
    case MCM_SETSELRANGE: {
      if (!Multi(c) || !lp) return FALSE;
      const SYSTEMTIME* st = (const SYSTEMTIME*)lp;
      if (!ValidDate(st[0]) || !ValidDate(st[1])) return FALSE;
      long a = DayOf(st[0]), b = DayOf(st[1]);
      if (a > b) std::swap(a, b);
      if (b - a + 1 > c->maxSel) return FALSE;
      Select(c, a, b, false);
      return TRUE;
    }
    case MCM_GETMONTHRANGE: {
      SYSTEMTIME* st = (SYSTEMTIME*)lp;
      if (st) {
        if (wp == GMR_DAYSTATE) {
          long start = GridStart(c);
          FromDayNumber(start, &st[0]);
          FromDayNumber(start + 41, &st[1]);
        } else {
          FromDayNumber(DayNumber(c->year, c->month, 1), &st[0]);
          FromDayNumber(DayNumber(c->year, c->month, DaysIn(c->year, c->month)), &st[1]);
        }
      }
      return wp == GMR_DAYSTATE ? 3 : 1;
    }
    case MCM_SETDAYSTATE: {
      const MONTHDAYSTATE* s = (const MONTHDAYSTATE*)lp;
      if (!s || (int)wp < 1) return FALSE;
      c->dayState.assign(s, s + std::min((int)wp, 3));
      while (c->dayState.size() < 3) c->dayState.push_back(0);
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case MCM_GETMINREQRECT: {
      RECT* rc = (RECT*)lp;
      if (!rc) return FALSE;
      SIZE s = MinSize(c);
      *rc = RECT{0, 0, s.cx, s.cy};
      return TRUE;
    }
    case MCM_SETCOLOR: {
      if (wp > MCSC_TRAILINGTEXT) return (LRESULT)-1;
      COLORREF old = c->colors[wp];
      c->colors[wp] = (COLORREF)lp;
      InvalidateRect(h, NULL, TRUE);
      return old;
    }
    case MCM_GETCOLOR:
      return wp > MCSC_TRAILINGTEXT ? (LRESULT)-1 : c->colors[wp];
    case MCM_SETTODAY:
      if (lp) {
        c->today = *(const SYSTEMTIME*)lp;
        c->todaySet = true;
      } else {
        c->todaySet = false;
      }
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case MCM_GETTODAY:
      if (!lp) return FALSE;
      *(SYSTEMTIME*)lp = TodayOf(c);
      return TRUE;
    case MCM_HITTEST: {
      MCHITTESTINFO* hi = (MCHITTESTINFO*)lp;
      if (!hi) return (LRESULT)-1;
      Measure(c);
      memset(&hi->st, 0, sizeof hi->st);
      hi->uHit = HitTest(c, hi->pt, &hi->st);
      return hi->uHit;
    }
    case MCM_SETFIRSTDAYOFWEEK: {
      int old = (c->firstDay + 6) % 7;  // Monday 0, as the API counts
      int locale = (int)wcstol(LocaleString(LOCALE_IFIRSTDAYOFWEEK).c_str(), NULL, 10);
      if ((int)lp == -1) {
        c->firstDay = (locale + 1) % 7;
        c->firstDaySet = false;
      } else {
        c->firstDay = ((int)lp + 1) % 7;
        c->firstDaySet = true;
      }
      InvalidateRect(h, NULL, TRUE);
      return MAKELONG(old, (old != locale) ? 1 : 0);
    }
    case MCM_GETFIRSTDAYOFWEEK: {
      int locale = (int)wcstol(LocaleString(LOCALE_IFIRSTDAYOFWEEK).c_str(), NULL, 10);
      int cur = (c->firstDay + 6) % 7;
      return MAKELONG(cur, cur != locale ? 1 : 0);
    }
    case MCM_GETRANGE: {
      SYSTEMTIME* st = (SYSTEMTIME*)lp;
      if (st) {
        memset(st, 0, 2 * sizeof(SYSTEMTIME));
        if (c->rangeFlags & GDTR_MIN) st[0] = c->rangeMin;
        if (c->rangeFlags & GDTR_MAX) st[1] = c->rangeMax;
      }
      return c->rangeFlags;
    }
    case MCM_SETRANGE: {
      const SYSTEMTIME* st = (const SYSTEMTIME*)lp;
      if (!st) return FALSE;
      if ((wp & GDTR_MIN) && !ValidDate(st[0])) return FALSE;
      if ((wp & GDTR_MAX) && !ValidDate(st[1])) return FALSE;
      c->rangeFlags = (DWORD)wp & (GDTR_MIN | GDTR_MAX);
      if (wp & GDTR_MIN) c->rangeMin = st[0];
      if (wp & GDTR_MAX) c->rangeMax = st[1];
      // The selection moves into the range.
      long a = DayOf(c->sel), b = DayOf(c->selEnd);
      long lo = (c->rangeFlags & GDTR_MIN) ? DayOf(c->rangeMin) : a, hi = (c->rangeFlags & GDTR_MAX) ? DayOf(c->rangeMax) : b;
      if (a < lo || b > hi) {
        a = std::max(lo, std::min(a, hi));
        b = std::max(lo, std::min(b, hi));
        Select(c, a, b, false);
      }
      InvalidateRect(h, NULL, TRUE);
      return TRUE;
    }
    case MCM_GETMONTHDELTA:
      return c->delta ? c->delta : 1;
    case MCM_SETMONTHDELTA: {
      int old = c->delta;
      c->delta = (int)wp;
      return old;
    }
    case MCM_GETMAXTODAYWIDTH: {
      HDC dc = GetDC(h);
      HGDIOBJ old = SelectObject(dc, c->font ? c->font : ControlFont());
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(dc, L"Today: 00/00/0000", 17, &sz);
      SelectObject(dc, old);
      ReleaseDC(h, dc);
      return sz.cx;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Date and time picker

enum FieldKind { kLiteral, kDay, kDayName, kMonth, kYear, kHour12, kHour24, kMinute, kSecond, kAmPm, kCallback };

struct Field {
  FieldKind kind = kLiteral;
  int width = 0;  // the letters in the format: d, dd, ddd...
  std::wstring text;  // a literal's text, or a callback field's format
  RECT rect = {0, 0, 0, 0};
};

struct Picker {
  HWND hwnd = NULL;
  HFONT font = NULL;
  SYSTEMTIME st;
  bool valid = true;
  std::wstring format;  // the application's, or empty for the style's
  std::vector<Field> fields;
  int focus = -1;  // the field with the caret
  std::wstring typed;  // digits typed into it so far
  SYSTEMTIME rangeMin, rangeMax;
  DWORD rangeFlags = 0;
  HWND cal = NULL;  // the dropped-down month calendar
  COLORREF calColors[6];
  bool calColorsSet[6] = {};
  HFONT calFont = NULL;
  DWORD calStyle = 0;
  HWND spin = NULL;
  RECT check = {0, 0, 0, 0};
  bool dropping = false;
};

Picker* PickerOf(HWND h) { return (Picker*)GetWindowLongPtrW(h, 0); }

bool UpDown(Picker* p) { return (StyleOf(p->hwnd) & DTS_UPDOWN) != 0; }

std::wstring StyleFormat(Picker* p) {
  DWORD s = StyleOf(p->hwnd);
  if ((s & DTS_TIMEFORMAT) == DTS_TIMEFORMAT) return LocaleString(LOCALE_STIMEFORMAT);
  if ((s & DTS_SHORTDATECENTURYFORMAT) == DTS_SHORTDATECENTURYFORMAT) {
    std::wstring f = LocaleString(LOCALE_SSHORTDATE);
    size_t y = f.find(L'y');
    if (y != std::wstring::npos && f.find(L"yyyy") == std::wstring::npos) {
      size_t e = y;
      while (e < f.size() && f[e] == L'y') e++;
      f.replace(y, e - y, L"yyyy");
    }
    return f;
  }
  if (s & DTS_LONGDATEFORMAT) return LocaleString(LOCALE_SLONGDATE);
  return LocaleString(LOCALE_SSHORTDATE);
}

void ParseFormat(Picker* p) {
  std::wstring f = p->format.empty() ? StyleFormat(p) : p->format;
  p->fields.clear();
  size_t i = 0;
  auto literal = [&](const std::wstring& s) {
    if (!p->fields.empty() && p->fields.back().kind == kLiteral)
      p->fields.back().text += s;
    else {
      Field fl;
      fl.text = s;
      p->fields.push_back(fl);
    }
  };
  while (i < f.size()) {
    wchar_t ch = f[i];
    if (ch == L'\'') {
      size_t e = i + 1;
      std::wstring s;
      while (e < f.size()) {
        if (f[e] == L'\'') {
          if (e + 1 < f.size() && f[e + 1] == L'\'') {
            s += L'\'';
            e += 2;
            continue;
          }
          break;
        }
        s += f[e++];
      }
      literal(s.empty() && e == i + 1 ? L"'" : s);
      i = e + 1;
      continue;
    }
    size_t e = i;
    while (e < f.size() && f[e] == ch) e++;
    int n = (int)(e - i);
    Field fl;
    fl.width = n;
    switch (ch) {
      case L'd': fl.kind = n >= 3 ? kDayName : kDay; break;
      case L'M': fl.kind = kMonth; break;
      case L'y': fl.kind = kYear; break;
      case L'h': fl.kind = kHour12; break;
      case L'H': fl.kind = kHour24; break;
      case L'm': fl.kind = kMinute; break;
      case L's': fl.kind = kSecond; break;
      case L't': fl.kind = kAmPm; break;
      case L'X': fl.kind = kCallback; fl.text = f.substr(i, n); break;
      default:
        literal(f.substr(i, n));
        i = e;
        continue;
    }
    p->fields.push_back(fl);
    i = e;
  }
  if (p->focus >= (int)p->fields.size() || (p->focus >= 0 && p->fields[p->focus].kind == kLiteral)) p->focus = -1;
}

std::wstring Pad(int v, int width) {
  std::wstring s = std::to_wstring(v);
  while ((int)s.size() < width) s = L"0" + s;
  return s;
}

std::wstring FieldText(Picker* p, const Field& f) {
  const SYSTEMTIME& st = p->st;
  switch (f.kind) {
    case kLiteral: return f.text;
    case kDay: return Pad(st.wDay, f.width >= 2 ? 2 : 1);
    case kDayName: return DayName(st.wDayOfWeek, f.width == 3);
    case kMonth:
      if (f.width >= 4) return MonthName(st.wMonth, false);
      if (f.width == 3) return MonthName(st.wMonth, true);
      return Pad(st.wMonth, f.width);
    case kYear:
      if (f.width <= 2) return Pad(st.wYear % 100, f.width);
      return Pad(st.wYear, 4);
    case kHour12: return Pad(st.wHour % 12 == 0 ? 12 : st.wHour % 12, f.width >= 2 ? 2 : 1);
    case kHour24: return Pad(st.wHour, f.width >= 2 ? 2 : 1);
    case kMinute: return Pad(st.wMinute, f.width >= 2 ? 2 : 1);
    case kSecond: return Pad(st.wSecond, f.width >= 2 ? 2 : 1);
    case kAmPm: {
      std::wstring s = LocaleString(st.wHour < 12 ? LOCALE_S1159 : LOCALE_S2359);
      return f.width == 1 ? s.substr(0, 1) : s;
    }
    case kCallback: {
      NMDATETIMEFORMATW nm = {};
      nm.pszFormat = f.text.c_str();
      nm.st = st;
      nm.pszDisplay = nm.szDisplay;
      if (NotifyUnicode(p->hwnd)) {
        SendNotify(p->hwnd, NULL, DTN_FORMATW, &nm.nmhdr);
        return nm.pszDisplay ? nm.pszDisplay : L"";
      }
      std::string fa = Narrow(f.text.c_str());
      NMDATETIMEFORMATA na = {};
      na.pszFormat = fa.c_str();
      na.st = st;
      na.pszDisplay = na.szDisplay;
      SendNotify(p->hwnd, NULL, DTN_FORMATA, &na.nmhdr);
      return na.pszDisplay ? Widen(na.pszDisplay) : L"";
    }
  }
  return L"";
}

std::wstring Text(Picker* p) {
  std::wstring s;
  for (const Field& f : p->fields) s += FieldText(p, f);
  return s;
}

int ButtonWidth() { return GetSystemMetrics(SM_CXVSCROLL); }

void LayoutFields(Picker* p) {
  HDC dc = GetDC(p->hwnd);
  HGDIOBJ old = SelectObject(dc, p->font ? p->font : ControlFont());
  RECT c;
  GetClientRect(p->hwnd, &c);
  int x = 2;
  if (StyleOf(p->hwnd) & DTS_SHOWNONE) {
    int s = std::min(13, (int)c.bottom - 4);
    p->check = RECT{2, (c.bottom - s) / 2, 2 + s, (c.bottom - s) / 2 + s};
    x = p->check.right + 4;
  } else {
    p->check = RECT{0, 0, 0, 0};
  }
  for (Field& f : p->fields) {
    std::wstring t = FieldText(p, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, t.c_str(), (int)t.size(), &sz);
    f.rect = RECT{x, 1, x + sz.cx, c.bottom - 1};
    x += sz.cx;
  }
  SelectObject(dc, old);
  ReleaseDC(p->hwnd, dc);
}

void Changed(Picker* p) {
  LayoutFields(p);
  InvalidateRect(p->hwnd, NULL, TRUE);
}

void NotifyChange(Picker* p) {
  NMDATETIMECHANGE nm = {};
  nm.dwFlags = p->valid ? GDT_VALID : GDT_NONE;
  nm.st = p->st;
  SendNotify(p->hwnd, NULL, DTN_DATETIMECHANGE, &nm.nmhdr);
}

SYSTEMTIME Clamp(Picker* p, SYSTEMTIME st) {
  bool withTime = true;
  if ((p->rangeFlags & GDTR_MIN) && Compare(st, p->rangeMin, withTime) < 0) st = p->rangeMin;
  if ((p->rangeFlags & GDTR_MAX) && Compare(st, p->rangeMax, withTime) > 0) st = p->rangeMax;
  FixDayOfWeek(&st);
  return st;
}

void SetValue(Picker* p, SYSTEMTIME st, bool notify) {
  st = Clamp(p, st);
  bool same = memcmp(&st, &p->st, sizeof st) == 0;
  p->st = st;
  if (!same) {
    Changed(p);
    if (notify) NotifyChange(p);
  }
}

// Steps the focused field by delta, wrapping within it.
void Step(Picker* p, int delta) {
  if (p->focus < 0 || !p->valid) return;
  SYSTEMTIME st = p->st;
  auto wrap = [](int v, int lo, int hi) {
    int n = hi - lo + 1;
    return ((v - lo) % n + n) % n + lo;
  };
  switch (p->fields[p->focus].kind) {
    case kDay:
    case kDayName: st.wDay = (WORD)wrap(st.wDay + delta, 1, DaysIn(st.wYear, st.wMonth)); break;
    case kMonth:
      st.wMonth = (WORD)wrap(st.wMonth + delta, 1, 12);
      st.wDay = (WORD)std::min((int)st.wDay, DaysIn(st.wYear, st.wMonth));
      break;
    case kYear:
      st.wYear = (WORD)std::max(1601, std::min(30827, st.wYear + delta));
      st.wDay = (WORD)std::min((int)st.wDay, DaysIn(st.wYear, st.wMonth));
      break;
    case kHour12:
    case kHour24: st.wHour = (WORD)wrap(st.wHour + delta, 0, 23); break;
    case kMinute: st.wMinute = (WORD)wrap(st.wMinute + delta, 0, 59); break;
    case kSecond: st.wSecond = (WORD)wrap(st.wSecond + delta, 0, 59); break;
    case kAmPm: st.wHour = (WORD)((st.wHour + 12) % 24); break;
    case kCallback: {
      NMDATETIMEWMKEYDOWNW nm = {};
      nm.nVirtKey = delta > 0 ? VK_UP : VK_DOWN;
      nm.pszFormat = p->fields[p->focus].text.c_str();
      nm.st = st;
      SendNotify(p->hwnd, NULL, DTN_WMKEYDOWNW, &nm.nmhdr);
      st = nm.st;
      break;
    }
    default: return;
  }
  FixDayOfWeek(&st);
  SetValue(p, st, true);
}

// A digit typed into the focused field.
void Type(Picker* p, int digit) {
  if (p->focus < 0 || !p->valid) return;
  Field& f = p->fields[p->focus];
  int maxLen = f.kind == kYear ? (f.width <= 2 ? 2 : 4) : 2;
  if (f.kind == kDayName || f.kind == kAmPm || f.kind == kLiteral || f.kind == kCallback) return;
  if (f.kind == kMonth && f.width >= 3) {
    // Typed numbers still choose a month by its number.
  }
  p->typed += (wchar_t)(L'0' + digit);
  int v = (int)wcstol(p->typed.c_str(), NULL, 10);
  SYSTEMTIME st = p->st;
  switch (f.kind) {
    case kDay: if (v >= 1 && v <= DaysIn(st.wYear, st.wMonth)) st.wDay = (WORD)v; break;
    case kMonth:
      if (v >= 1 && v <= 12) {
        st.wMonth = (WORD)v;
        st.wDay = (WORD)std::min((int)st.wDay, DaysIn(st.wYear, st.wMonth));
      }
      break;
    case kYear:
      if ((int)p->typed.size() >= maxLen) {
        int y = maxLen == 2 ? (v < 30 ? 2000 + v : 1900 + v) : v;
        if (y >= 1601 && y <= 30827) {
          st.wYear = (WORD)y;
          st.wDay = (WORD)std::min((int)st.wDay, DaysIn(st.wYear, st.wMonth));
        }
      }
      break;
    case kHour12:
      if (v >= 1 && v <= 12) st.wHour = (WORD)((v % 12) + (st.wHour >= 12 ? 12 : 0));
      break;
    case kHour24: if (v <= 23) st.wHour = (WORD)v; break;
    case kMinute: if (v <= 59) st.wMinute = (WORD)v; break;
    case kSecond: if (v <= 59) st.wSecond = (WORD)v; break;
    default: break;
  }
  FixDayOfWeek(&st);
  SetValue(p, st, true);
  // A full field, or one that cannot take another digit, is done.
  int cap = f.kind == kMonth ? 12 : f.kind == kDay ? 31 : f.kind == kHour24 ? 23 : f.kind == kHour12 ? 12 : 59;
  if ((int)p->typed.size() >= maxLen || (f.kind != kYear && v * 10 > cap)) p->typed.clear();
}

void MoveFocus(Picker* p, int dir) {
  p->typed.clear();
  int n = (int)p->fields.size();
  int i = p->focus;
  for (int k = 0; k < n; k++) {
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    if (p->fields[i].kind != kLiteral) {
      p->focus = i;
      break;
    }
  }
  InvalidateRect(p->hwnd, NULL, TRUE);
}

void CloseUp(Picker* p, bool take) {
  if (!p->cal) return;
  HWND cal = p->cal;
  p->cal = NULL;
  SYSTEMTIME st;
  bool got = take && SendMessageW(cal, MCM_GETCURSEL, 0, (LPARAM)&st);
  DestroyWindow(cal);
  if (got) {
    st.wHour = p->st.wHour;
    st.wMinute = p->st.wMinute;
    st.wSecond = p->st.wSecond;
    st.wMilliseconds = p->st.wMilliseconds;
    bool wasValid = p->valid;
    p->valid = true;
    SYSTEMTIME before = p->st;
    SetValue(p, st, false);
    if (!wasValid || memcmp(&before, &p->st, sizeof before) != 0) {
      Changed(p);
      NotifyChange(p);
    }
  }
  SendNotify(p->hwnd, NULL, DTN_CLOSEUP);
  InvalidateRect(p->hwnd, NULL, TRUE);
}

void DropDown(Picker* p) {
  if (p->cal || UpDown(p)) return;
  RECT wr;
  GetWindowRect(p->hwnd, &wr);
  p->cal = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MONTHCAL_CLASSW, L"",
                           WS_POPUP | WS_BORDER | p->calStyle, wr.left, wr.bottom, 0, 0, p->hwnd, NULL,
                           (HINSTANCE)GetWindowLongPtrW(p->hwnd, GWLP_HINSTANCE), NULL);
  if (!p->cal) return;
  if (p->calFont) SendMessageW(p->cal, WM_SETFONT, (WPARAM)p->calFont, FALSE);
  for (int i = 0; i < 6; i++)
    if (p->calColorsSet[i]) SendMessageW(p->cal, MCM_SETCOLOR, i, p->calColors[i]);
  if (p->rangeFlags) {
    SYSTEMTIME r[2] = {p->rangeMin, p->rangeMax};
    SendMessageW(p->cal, MCM_SETRANGE, p->rangeFlags, (LPARAM)r);
  }
  if (p->valid) SendMessageW(p->cal, MCM_SETCURSEL, 0, (LPARAM)&p->st);
  RECT need;
  SendMessageW(p->cal, MCM_GETMINREQRECT, 0, (LPARAM)&need);
  RECT nc = need;
  AdjustWindowRectEx(&nc, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW);
  int x = (StyleOf(p->hwnd) & DTS_RIGHTALIGN) ? wr.right - (nc.right - nc.left) : wr.left;
  SendNotify(p->hwnd, NULL, DTN_DROPDOWN);
  if (!p->cal) return;
  SetWindowPos(p->cal, HWND_TOPMOST, x, wr.bottom, nc.right - nc.left, nc.bottom - nc.top, SWP_SHOWWINDOW);
  SetFocus(p->cal);
  SetCapture(p->hwnd);
  InvalidateRect(p->hwnd, NULL, TRUE);
}

void PaintPicker(Picker* p, HDC dc) {
  RECT c;
  GetClientRect(p->hwnd, &c);
  bool enabled = IsWindowEnabled(p->hwnd) != 0;
  HBRUSH bg = (HBRUSH)SendMessageW(GetParent(p->hwnd), enabled ? WM_CTLCOLOREDIT : WM_CTLCOLORSTATIC, (WPARAM)dc,
                                   (LPARAM)p->hwnd);
  FillRect(dc, &c, bg ? bg : GetSysColorBrush(enabled ? COLOR_WINDOW : COLOR_BTNFACE));
  HGDIOBJ old = SelectObject(dc, p->font ? p->font : ControlFont());
  SetBkMode(dc, TRANSPARENT);
  bool focused = GetFocus() == p->hwnd;
  if (StyleOf(p->hwnd) & DTS_SHOWNONE)
    DrawFrameControl(dc, &p->check, DFC_BUTTON, DFCS_BUTTONCHECK | DFCS_FLAT | (p->valid ? DFCS_CHECKED : 0));
  for (int i = 0; i < (int)p->fields.size(); i++) {
    const Field& f = p->fields[i];
    std::wstring t = FieldText(p, f);
    RECT r = f.rect;
    bool sel = focused && i == p->focus && p->valid;
    if (sel) FillRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT));
    SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : enabled && p->valid ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT);
  }
  if (!UpDown(p)) {
    int bw = ButtonWidth();
    RECT b = {c.right - bw, c.top, c.right, c.bottom};
    DrawFrameControl(dc, &b, DFC_SCROLL, DFCS_SCROLLCOMBOBOX | (p->cal ? DFCS_PUSHED : 0) | (enabled ? 0 : DFCS_INACTIVE));
  }
  SelectObject(dc, old);
}

LRESULT CALLBACK PickerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Picker* p = PickerOf(h);
  if (msg == WM_NCCREATE) {
    p = new Picker;
    p->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)p);
    GetLocalTime(&p->st);
    p->st.wMilliseconds = 0;
    memset(&p->rangeMin, 0, sizeof p->rangeMin);
    memset(&p->rangeMax, 0, sizeof p->rangeMax);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!p) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      if (p->cal && IsWindow(p->cal)) DestroyWindow(p->cal);
      delete p;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE:
      ParseFormat(p);
      if (UpDown(p)) {
        RECT c;
        GetClientRect(h, &c);
        int bw = ButtonWidth();
        p->spin = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE, c.right - bw, 0, bw, c.bottom, h,
                                  (HMENU)1, (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE), NULL);
        if (p->spin) SendMessageW(p->spin, UDM_SETRANGE32, (WPARAM)-1000000, 1000000);
      }
      LayoutFields(p);
      return 0;
    case WM_SIZE:
      if (p->spin) {
        int bw = ButtonWidth();
        SetWindowPos(p->spin, NULL, LOWORD(lp) - bw, 0, bw, HIWORD(lp), SWP_NOZORDER | SWP_NOACTIVATE);
      }
      LayoutFields(p);
      return 0;
    case WM_STYLECHANGED:
      ParseFormat(p);
      Changed(p);
      return 0;
    case WM_SETFONT:
      p->font = (HFONT)wp;
      LayoutFields(p);
      if (LOWORD(lp)) InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)p->font;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintPicker(p, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_ENABLE:
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_SETFOCUS:
      if (p->focus < 0) MoveFocus(p, 1);
      InvalidateRect(h, NULL, TRUE);
      SendNotify(h, NULL, NM_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      p->typed.clear();
      InvalidateRect(h, NULL, TRUE);
      SendNotify(h, NULL, NM_KILLFOCUS);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN: {
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (p->cal) {
        // A click outside the calendar closes it.
        POINT sp = pt;
        ClientToScreen(h, &sp);
        RECT cr;
        GetWindowRect(p->cal, &cr);
        if (PtInRect(&cr, sp)) {
          ReleaseCapture();
          ScreenToClient(p->cal, &sp);
          SendMessageW(p->cal, WM_LBUTTONDOWN, wp, MAKELPARAM(sp.x, sp.y));
          if (p->cal) SetCapture(h);
          return 0;
        }
        ReleaseCapture();
        CloseUp(p, false);
        return 0;
      }
      SetFocus(h);
      if (PtInRect(&p->check, pt)) {
        p->valid = !p->valid;
        Changed(p);
        NotifyChange(p);
        return 0;
      }
      RECT c;
      GetClientRect(h, &c);
      if (!UpDown(p) && pt.x >= c.right - ButtonWidth()) {
        DropDown(p);
        return 0;
      }
      for (int i = 0; i < (int)p->fields.size(); i++) {
        RECT r = p->fields[i].rect;
        if (pt.x >= r.left && pt.x < r.right) {
          int k = i;
          while (k > 0 && p->fields[k].kind == kLiteral) k--;
          if (p->fields[k].kind != kLiteral) p->focus = k;
          p->typed.clear();
          InvalidateRect(h, NULL, TRUE);
          break;
        }
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      if (p->cal && (HWND)lp != h && (HWND)lp != p->cal) CloseUp(p, false);
      return 0;
    case WM_KEYDOWN:
      if (p->cal) {
        if (wp == VK_ESCAPE) {
          ReleaseCapture();
          CloseUp(p, false);
        } else if (wp == VK_RETURN) {
          ReleaseCapture();
          CloseUp(p, true);
        } else {
          SendMessageW(p->cal, msg, wp, lp);
        }
        return 0;
      }
      switch (wp) {
        case VK_LEFT: MoveFocus(p, -1); return 0;
        case VK_RIGHT: MoveFocus(p, 1); return 0;
        case VK_UP: p->typed.clear(); Step(p, 1); return 0;
        case VK_DOWN:
          if (GetKeyState(VK_MENU) < 0) {
            DropDown(p);
            return 0;
          }
          p->typed.clear();
          Step(p, -1);
          return 0;
        case VK_F4: DropDown(p); return 0;
        case VK_HOME:
          p->focus = -1;
          MoveFocus(p, 1);
          return 0;
        case VK_END:
          p->focus = -1;
          MoveFocus(p, -1);
          return 0;
      }
      return DefWindowProcW(h, msg, wp, lp);
    case WM_SYSKEYDOWN:
      if (wp == VK_DOWN) {
        DropDown(p);
        return 0;
      }
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CHAR:
      if (wp >= L'0' && wp <= L'9') {
        Type(p, (int)(wp - L'0'));
      } else if (wp == L' ' && (StyleOf(h) & DTS_SHOWNONE)) {
        p->valid = !p->valid;
        Changed(p);
        NotifyChange(p);
      } else if (p->focus >= 0 && p->fields[p->focus].kind == kAmPm && (wp == L'a' || wp == L'A' || wp == L'p' || wp == L'P')) {
        bool pm = wp == L'p' || wp == L'P';
        if (pm != (p->st.wHour >= 12)) Step(p, 1);
      } else if (wp == L'/' || wp == L':' || wp == L'.' || wp == L'-') {
        MoveFocus(p, 1);
      }
      return 0;
    case WM_VSCROLL:
    case WM_NOTIFY:
      if (msg == WM_NOTIFY) {
        NMHDR* nm = (NMHDR*)lp;
        if (nm && nm->hwndFrom == p->spin && nm->code == UDN_DELTAPOS) {
          if (p->focus < 0) MoveFocus(p, 1);
          Step(p, -((NMUPDOWN*)lp)->iDelta < 0 ? 1 : -1);
          return 1;  // the spin keeps its position
        }
        if (nm && nm->hwndFrom == p->cal && nm->code == MCN_SELECT) {
          ReleaseCapture();
          CloseUp(p, true);
          return 0;
        }
        return 0;
      }
      return 0;
    case WM_GETTEXTLENGTH:
      return (LRESULT)Text(p).size();
    case WM_GETTEXT:
      return CopyOut(Text(p), (void*)lp, (int)wp, true);
    case DTM_GETSYSTEMTIME:
      if (!lp) return GDT_ERROR;
      if (!p->valid) return GDT_NONE;
      *(SYSTEMTIME*)lp = p->st;
      return GDT_VALID;
    case DTM_SETSYSTEMTIME: {
      if (wp == GDT_NONE) {
        if (!(StyleOf(h) & DTS_SHOWNONE)) return FALSE;
        p->valid = false;
        Changed(p);
        return TRUE;
      }
      if (!lp) return FALSE;
      SYSTEMTIME st = *(const SYSTEMTIME*)lp;
      if (!ValidDate(st) || !ValidTime(st)) return FALSE;
      p->valid = true;
      SetValue(p, st, false);
      Changed(p);
      return TRUE;
    }
    case DTM_GETRANGE: {
      SYSTEMTIME* st = (SYSTEMTIME*)lp;
      if (st) {
        memset(st, 0, 2 * sizeof(SYSTEMTIME));
        if (p->rangeFlags & GDTR_MIN) st[0] = p->rangeMin;
        if (p->rangeFlags & GDTR_MAX) st[1] = p->rangeMax;
      }
      return p->rangeFlags;
    }
    case DTM_SETRANGE: {
      const SYSTEMTIME* st = (const SYSTEMTIME*)lp;
      if (!st && wp) return FALSE;
      if ((wp & GDTR_MIN) && !ValidDate(st[0])) return FALSE;
      if ((wp & GDTR_MAX) && !ValidDate(st[1])) return FALSE;
      p->rangeFlags = (DWORD)wp & (GDTR_MIN | GDTR_MAX);
      if (wp & GDTR_MIN) p->rangeMin = st[0];
      if (wp & GDTR_MAX) p->rangeMax = st[1];
      SetValue(p, p->st, false);
      return TRUE;
    }
    case DTM_SETFORMATA:
    case DTM_SETFORMATW:
      if (!lp)
        p->format.clear();
      else
        p->format = msg == DTM_SETFORMATW ? std::wstring((LPCWSTR)lp) : Widen((LPCSTR)lp);
      ParseFormat(p);
      Changed(p);
      return TRUE;
    case DTM_SETMCCOLOR: {
      if (wp > MCSC_TRAILINGTEXT) return (LRESULT)-1;
      COLORREF old = p->calColorsSet[wp] ? p->calColors[wp] : (COLORREF)-1;
      p->calColors[wp] = (COLORREF)lp;
      p->calColorsSet[wp] = true;
      if (p->cal) SendMessageW(p->cal, MCM_SETCOLOR, wp, lp);
      return old;
    }
    case DTM_GETMCCOLOR:
      if (wp > MCSC_TRAILINGTEXT) return (LRESULT)-1;
      if (p->calColorsSet[wp]) return p->calColors[wp];
      {
        static const int sys[] = {COLOR_WINDOW, COLOR_WINDOWTEXT, COLOR_ACTIVECAPTION, COLOR_CAPTIONTEXT, COLOR_WINDOW,
                                  COLOR_GRAYTEXT};
        return GetSysColor(sys[wp]);
      }
    case DTM_GETMONTHCAL:
      return (LRESULT)p->cal;
    case DTM_SETMCFONT:
      p->calFont = (HFONT)wp;
      if (p->cal) SendMessageW(p->cal, WM_SETFONT, wp, lp);
      return 0;
    case DTM_GETMCFONT:
      return (LRESULT)p->calFont;
    case DTM_SETMCSTYLE: {
      DWORD old = p->calStyle;
      p->calStyle = (DWORD)lp;
      return old;
    }
    case DTM_GETMCSTYLE:
      return p->calStyle;
    case DTM_CLOSEMONTHCAL:
      if (p->cal) {
        if (GetCapture() == h) ReleaseCapture();
        CloseUp(p, false);
      }
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterDateClasses() {
  LPCWSTR arrow = (LPCWSTR)IDC_ARROW;
  RegisterSystemClass(MONTHCAL_CLASSW, MonthCalProc, CS_GLOBALCLASS | CS_DBLCLKS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(DATETIMEPICK_CLASSW, PickerProc, CS_GLOBALCLASS | CS_DBLCLKS, sizeof(void*), NULL, arrow);
}

}  // namespace winport
