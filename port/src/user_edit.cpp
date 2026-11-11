// user_edit.cpp - the Edit control: single-line and multiline text
// editing with the mouse and keyboard, selection, scrolling, undo, the
// clipboard, and the EM_ messages.
//
// The text is kept as UTF-16-free wide characters; lines of a multiline
// control end with CR LF (a lone LF also ends one). A multiline control
// without ES_AUTOHSCROLL or a horizontal scroll bar wraps its lines at
// word breaks to the width of its formatting rectangle.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <string.h>
#include <wctype.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

struct Line {
  int start = 0, len = 0;  // the characters shown
  int brk = 0;             // the characters of the break after them
  int width = 0;
};

struct Edit {
  HWND hwnd = NULL;
  std::wstring text;
  int anchor = 0, caret = 0;
  std::vector<Line> lines;
  int firstLine = 0;  // the top visible line
  int xOffset = 0;    // pixels scrolled to the left
  RECT fmt = {0, 0, 0, 0};
  bool fmtSet = false;  // EM_SETRECT chose it
  int leftMargin = 1, rightMargin = 1;
  HFONT font = NULL;
  int lineHeight = 13, avgWidth = 6;
  UINT limit = 0x7FFFFFFE;
  bool modified = false, focus = false, tracking = false, typing = false;
  wchar_t password = 0;
  std::vector<int> tabs;  // in dialog units
  std::wstring undoText;
  int undoAnchor = 0, undoCaret = 0;
  bool canUndo = false;
  HLOCAL handle = NULL;
  EDITWORDBREAKPROCW wordBreak = NULL;
  int maxWidth = 0;
  bool caretMade = false;

  DWORD Style() const { return (DWORD)GetWindowLongW(hwnd, GWL_STYLE); }
  bool Multi() const { return (Style() & ES_MULTILINE) != 0; }
  bool Wraps() const {
    DWORD s = Style();
    return (s & ES_MULTILINE) && !(s & ES_AUTOHSCROLL) && !(s & WS_HSCROLL);
  }
  int SelStart() const { return std::min(anchor, caret); }
  int SelEnd() const { return std::max(anchor, caret); }
};

Edit* EditOf(HWND h) { return (Edit*)GetWindowLongPtrW(h, 0); }

void Notify(Edit* e, UINT code) {
  HWND parent = GetParent(e->hwnd);
  if (parent) SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(e->hwnd), code), (LPARAM)e->hwnd);
}

// A DC for measuring, with the control's font.
struct MeasureDC {
  HDC dc;
  HGDIOBJ old;
  explicit MeasureDC(Edit* e) {
    dc = CreateCompatibleDC(NULL);
    old = SelectObject(dc, e->font ? (HGDIOBJ)e->font : GetStockObject(SYSTEM_FONT));
  }
  ~MeasureDC() {
    SelectObject(dc, old);
    DeleteDC(dc);
  }
};

std::wstring Shown(Edit* e, int start, int len) {
  if (e->password) return std::wstring(len, e->password);
  return e->text.substr(start, len);
}

int TabWidth(Edit* e, int i) {
  if (e->tabs.empty()) return e->avgWidth * 8;
  int du = e->tabs.size() == 1 ? e->tabs[0] * (i + 1) : (i < (int)e->tabs.size() ? e->tabs[i] : e->tabs.back());
  return MulDiv(du, e->avgWidth, 4);
}

int NextTab(Edit* e, int x) {
  if (e->tabs.size() <= 1) {
    int w = TabWidth(e, 0);
    if (w <= 0) w = 1;
    return (x / w + 1) * w;
  }
  for (size_t i = 0; i < e->tabs.size(); i++) {
    int t = MulDiv(e->tabs[i], e->avgWidth, 4);
    if (t > x) return t;
  }
  return x + e->avgWidth;
}

// The x offset of each character boundary of a run of shown text.
void Positions(Edit* e, HDC dc, const std::wstring& s, std::vector<int>* xs) {
  xs->assign(s.size() + 1, 0);
  int x = 0;
  size_t i = 0;
  std::vector<int> dx;
  while (i < s.size()) {
    if (s[i] == L'\t' && e->Multi()) {
      x = NextTab(e, x);
      (*xs)[++i] = x;
      continue;
    }
    size_t j = i;
    while (j < s.size() && !(s[j] == L'\t' && e->Multi())) j++;
    dx.assign(j - i, 0);
    SIZE sz;
    GetTextExtentExPointW(dc, s.c_str() + i, (int)(j - i), 0, NULL, dx.data(), &sz);
    for (size_t k = 0; k < j - i; k++) (*xs)[i + k + 1] = x + dx[k];
    x += sz.cx;
    i = j;
  }
}

int Width(Edit* e, HDC dc, const std::wstring& s) {
  std::vector<int> xs;
  Positions(e, dc, s, &xs);
  return xs.back();
}

void SetFormatRect(Edit* e) {
  if (e->fmtSet) return;
  RECT rc;
  GetClientRect(e->hwnd, &rc);
  e->fmt = rc;
  e->fmt.left += e->leftMargin;
  e->fmt.right -= e->rightMargin;
  if (!e->Multi()) {
    // One line, in the middle of the control.
    int h = rc.bottom - rc.top;
    int top = h > e->lineHeight ? (h - e->lineHeight) / 2 : 0;
    e->fmt.top = rc.top + top;
    e->fmt.bottom = e->fmt.top + e->lineHeight;
  }
  if (e->fmt.right < e->fmt.left) e->fmt.right = e->fmt.left;
}

// Breaks the text into lines.
void Layout(Edit* e) {
  e->lines.clear();
  MeasureDC m(e);
  int width = e->fmt.right - e->fmt.left;
  e->maxWidth = 0;
  const std::wstring& t = e->text;
  if (!e->Multi()) {
    Line l;
    l.len = (int)t.size();
    l.width = Width(e, m.dc, Shown(e, 0, l.len));
    e->lines.push_back(l);
    e->maxWidth = l.width;
    return;
  }
  size_t pos = 0;
  for (;;) {
    size_t end = pos;
    while (end < t.size() && t[end] != L'\n' && !(t[end] == L'\r' && end + 1 < t.size() && t[end + 1] == L'\n')) end++;
    int brk = end >= t.size() ? 0 : (t[end] == L'\r' ? 2 : 1);
    // Wrap the hard line.
    size_t s = pos;
    if (e->Wraps() && width > 0) {
      std::vector<int> xs;
      while (true) {
        std::wstring seg = Shown(e, (int)s, (int)(end - s));
        Positions(e, m.dc, seg, &xs);
        if (xs.back() <= width) break;
        // The last break opportunity that fits.
        size_t fit = 0;
        while (fit + 1 < xs.size() && xs[fit + 1] <= width) fit++;
        if (fit == 0) fit = 1;
        size_t cut = fit;
        size_t space = std::wstring::npos;
        for (size_t k = fit; k > 0; k--)
          if (t[s + k - 1] == L' ' || t[s + k - 1] == L'\t') {
            space = k;
            break;
          }
        if (space != std::wstring::npos) cut = space;
        Line l;
        l.start = (int)s;
        l.len = (int)cut;
        l.brk = 0;
        l.width = xs[cut];
        e->lines.push_back(l);
        s += cut;
        if (s >= end) break;
      }
    }
    Line l;
    l.start = (int)s;
    l.len = (int)(end - s);
    l.brk = brk;
    l.width = Width(e, m.dc, Shown(e, l.start, l.len));
    if (s < end || e->lines.empty() || e->lines.back().brk != 0 || s == pos) e->lines.push_back(l);
    if (!brk) break;
    pos = end + brk;
  }
  for (const Line& l : e->lines) e->maxWidth = std::max(e->maxWidth, l.width);
}

int LineOf(Edit* e, int index) {
  for (int i = (int)e->lines.size() - 1; i >= 0; i--) {
    const Line& l = e->lines[i];
    if (index >= l.start) {
      // A position at a soft break belongs to the next line.
      return i;
    }
  }
  return 0;
}

int VisibleLines(Edit* e) {
  int h = e->fmt.bottom - e->fmt.top;
  int n = e->lineHeight > 0 ? h / e->lineHeight : 1;
  return n < 1 ? 1 : n;
}

int AlignOffset(Edit* e, const Line& l) {
  DWORD s = e->Style();
  int width = e->fmt.right - e->fmt.left;
  if ((s & ES_AUTOHSCROLL) && !e->Multi()) return 0;
  if (s & ES_CENTER) return std::max(0, (width - l.width) / 2);
  if (s & ES_RIGHT) return std::max(0, width - l.width);
  return 0;
}

POINT PosOf(Edit* e, int index) {
  int li = LineOf(e, index);
  const Line& l = e->lines[li];
  MeasureDC m(e);
  int col = std::min(index - l.start, l.len);
  std::vector<int> xs;
  Positions(e, m.dc, Shown(e, l.start, l.len), &xs);
  POINT p;
  p.x = e->fmt.left + AlignOffset(e, l) + xs[col] - e->xOffset;
  p.y = e->fmt.top + (li - e->firstLine) * e->lineHeight;
  return p;
}

int IndexAt(Edit* e, int x, int y, int* lineOut = NULL) {
  int li = e->firstLine + (y - e->fmt.top) / std::max(1, e->lineHeight);
  if (y < e->fmt.top) li = e->firstLine - 1 - (e->fmt.top - y - 1) / std::max(1, e->lineHeight);
  if (!e->Multi()) li = 0;
  li = std::max(0, std::min(li, (int)e->lines.size() - 1));
  if (lineOut) *lineOut = li;
  const Line& l = e->lines[li];
  MeasureDC m(e);
  std::vector<int> xs;
  Positions(e, m.dc, Shown(e, l.start, l.len), &xs);
  int rel = x - e->fmt.left - AlignOffset(e, l) + e->xOffset;
  int best = 0;
  for (int k = 0; k <= l.len; k++) {
    if (k == l.len || rel < (xs[k] + xs[k + 1]) / 2) {
      best = k;
      break;
    }
  }
  return l.start + best;
}

// ---------------------------------------------------------------------
// Scrolling and the caret

void UpdateScrollBars(Edit* e) {
  DWORD s = e->Style();
  if (s & WS_VSCROLL) {
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = (int)e->lines.size() - 1;
    si.nPage = VisibleLines(e);
    si.nPos = e->firstLine;
    SetScrollInfo(e->hwnd, SB_VERT, &si, TRUE);
  }
  if (s & WS_HSCROLL) {
    SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    si.nMin = 0;
    si.nMax = e->maxWidth + e->avgWidth;
    si.nPage = std::max(1, (int)(e->fmt.right - e->fmt.left));
    si.nPos = e->xOffset;
    SetScrollInfo(e->hwnd, SB_HORZ, &si, TRUE);
  }
}

void PlaceCaret(Edit* e) {
  if (!e->focus) return;
  if (!e->caretMade) {
    CreateCaret(e->hwnd, NULL, 1, e->lineHeight);
    e->caretMade = true;
    ShowCaret(e->hwnd);
  }
  POINT p = PosOf(e, e->caret);
  SetCaretPos(p.x, p.y);
}

void Redraw(Edit* e) { InvalidateRect(e->hwnd, NULL, FALSE); }

// Scrolls so that the caret shows; returns whether anything moved.
bool ScrollToCaret(Edit* e) {
  bool moved = false;
  int li = LineOf(e, e->caret);
  if (e->Multi()) {
    int vis = VisibleLines(e);
    if (li < e->firstLine) {
      e->firstLine = li;
      moved = true;
    } else if (li >= e->firstLine + vis) {
      e->firstLine = li - vis + 1;
      moved = true;
    }
  }
  if (!e->Wraps()) {
    POINT p = PosOf(e, e->caret);
    int width = e->fmt.right - e->fmt.left;
    if (p.x < e->fmt.left) {
      e->xOffset -= e->fmt.left - p.x + width / 3;
      if (e->xOffset < 0) e->xOffset = 0;
      moved = true;
    } else if (p.x >= e->fmt.right) {
      e->xOffset += p.x - e->fmt.right + 1 + width / 3;
      moved = true;
    }
  }
  if (moved) {
    UpdateScrollBars(e);
    Redraw(e);
  }
  PlaceCaret(e);
  return moved;
}

void ScrollLines(Edit* e, int dy, int dx) {
  int maxFirst = std::max(0, (int)e->lines.size() - 1);
  int first = std::max(0, std::min(e->firstLine + dy, maxFirst));
  int x = std::max(0, e->xOffset + dx);
  if (first == e->firstLine && x == e->xOffset) return;
  e->firstLine = first;
  e->xOffset = x;
  UpdateScrollBars(e);
  Redraw(e);
  PlaceCaret(e);
}

// ---------------------------------------------------------------------
// Changing the text

void Relayout(Edit* e) {
  SetFormatRect(e);
  Layout(e);
  int maxFirst = std::max(0, (int)e->lines.size() - 1);
  if (e->firstLine > maxFirst) e->firstLine = maxFirst;
  UpdateScrollBars(e);
  Redraw(e);
}

void SaveUndo(Edit* e) {
  e->undoText = e->text;
  e->undoAnchor = e->anchor;
  e->undoCaret = e->caret;
  e->canUndo = true;
}

// Puts text in place of the selection. typed: from the keyboard, which
// the control's limits and styles apply to.
bool Replace(Edit* e, std::wstring ins, bool undo, bool typed) {
  DWORD style = e->Style();
  if (style & ES_UPPERCASE)
    for (wchar_t& c : ins) c = (wchar_t)towupper(c);
  if (style & ES_LOWERCASE)
    for (wchar_t& c : ins) c = (wchar_t)towlower(c);
  if (!e->Multi()) {
    size_t nl = ins.find_first_of(L"\r\n");
    if (nl != std::wstring::npos) ins.resize(nl);
  }
  int start = e->SelStart(), end = e->SelEnd();
  size_t after = e->text.size() - (end - start);
  bool truncated = false;
  if (after + ins.size() > e->limit) {
    size_t room = e->limit > after ? e->limit - after : 0;
    ins.resize(room);
    truncated = true;
  }
  if (ins.empty() && start == end) {
    if (truncated) Notify(e, EN_MAXTEXT);
    return false;
  }
  if (undo && !(typed && e->typing)) SaveUndo(e);
  std::wstring old = e->text;
  e->text.replace(start, end - start, ins);
  // A single line that does not scroll takes only what fits.
  if (typed && !e->Multi() && !(style & ES_AUTOHSCROLL)) {
    MeasureDC m(e);
    if (Width(e, m.dc, Shown(e, 0, (int)e->text.size())) > e->fmt.right - e->fmt.left) {
      e->text = old;
      Notify(e, EN_MAXTEXT);
      MessageBeep(0);
      return false;
    }
  }
  e->anchor = e->caret = start + (int)ins.size();
  e->modified = true;
  e->typing = typed;
  Layout(e);
  int maxFirst = std::max(0, (int)e->lines.size() - 1);
  if (e->firstLine > maxFirst) e->firstLine = maxFirst;
  UpdateScrollBars(e);
  Redraw(e);
  ScrollToCaret(e);
  Notify(e, EN_UPDATE);
  Notify(e, EN_CHANGE);
  if (truncated) Notify(e, EN_MAXTEXT);
  return true;
}

void SetText(Edit* e, const std::wstring& t) {
  e->text = t;
  if (!e->Multi()) {
    size_t nl = e->text.find_first_of(L"\r\n");
    if (nl != std::wstring::npos) e->text.resize(nl);
  }
  e->anchor = e->caret = 0;
  e->firstLine = 0;
  e->xOffset = 0;
  e->canUndo = false;
  e->modified = false;
  e->typing = false;
  Relayout(e);
  PlaceCaret(e);
}

void Select(Edit* e, int anchor, int caret, bool scroll = true) {
  int n = (int)e->text.size();
  anchor = std::max(0, std::min(anchor, n));
  caret = std::max(0, std::min(caret, n));
  // Never between the two characters of a CR LF.
  if (caret > 0 && caret < n && e->text[caret - 1] == L'\r' && e->text[caret] == L'\n') caret++;
  if (anchor > 0 && anchor < n && e->text[anchor - 1] == L'\r' && e->text[anchor] == L'\n') anchor++;
  if (anchor == e->anchor && caret == e->caret) {
    if (scroll) ScrollToCaret(e);
    return;
  }
  e->anchor = anchor;
  e->caret = caret;
  e->typing = false;
  Redraw(e);
  if (scroll)
    ScrollToCaret(e);
  else
    PlaceCaret(e);
}

// ---------------------------------------------------------------------
// The clipboard

void Copy(Edit* e) {
  if (e->password || e->SelStart() == e->SelEnd()) return;
  std::wstring s = e->text.substr(e->SelStart(), e->SelEnd() - e->SelStart());
  if (!OpenClipboard(e->hwnd)) return;
  EmptyClipboard();
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
  if (g) {
    memcpy(GlobalLock(g), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(g);
    SetClipboardData(CF_UNICODETEXT, g);
  }
  CloseClipboard();
}

void Paste(Edit* e) {
  if (e->Style() & ES_READONLY) return;
  if (!OpenClipboard(e->hwnd)) return;
  HANDLE g = GetClipboardData(CF_UNICODETEXT);
  std::wstring s;
  if (g) {
    const wchar_t* p = (const wchar_t*)GlobalLock(g);
    if (p) s = p;
    GlobalUnlock(g);
  }
  CloseClipboard();
  if (!s.empty()) Replace(e, s, true, false);
}

void Clear(Edit* e) {
  if ((e->Style() & ES_READONLY) || e->SelStart() == e->SelEnd()) return;
  Replace(e, L"", true, false);
}

void Undo(Edit* e) {
  if (!e->canUndo || (e->Style() & ES_READONLY)) return;
  std::wstring t = e->text;
  int a = e->anchor, c = e->caret;
  e->text = e->undoText;
  e->anchor = e->undoAnchor;
  e->caret = e->undoCaret;
  // Undo again redoes.
  e->undoText = t;
  e->undoAnchor = a;
  e->undoCaret = c;
  e->modified = true;
  e->typing = false;
  Relayout(e);
  ScrollToCaret(e);
  Notify(e, EN_UPDATE);
  Notify(e, EN_CHANGE);
}

// ---------------------------------------------------------------------
// Painting

void Paint(Edit* e, HDC dc) {
  DWORD style = e->Style();
  bool enabled = IsWindowEnabled(e->hwnd) != FALSE;
  HWND parent = GetParent(e->hwnd);
  UINT ctl = (!enabled || (style & ES_READONLY)) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT;
  HBRUSH br = NULL;
  if (parent) br = (HBRUSH)SendMessageW(parent, ctl, (WPARAM)dc, (LPARAM)e->hwnd);
  if (!br) br = (HBRUSH)DefWindowProcW(parent ? parent : e->hwnd, ctl, (WPARAM)dc, (LPARAM)e->hwnd);
  RECT rc;
  GetClientRect(e->hwnd, &rc);
  FillRect(dc, &rc, br);
  HGDIOBJ oldFont = SelectObject(dc, e->font ? (HGDIOBJ)e->font : GetStockObject(SYSTEM_FONT));
  int saved = SaveDC(dc);
  RECT clip = e->fmt;
  if (e->Multi()) {
    clip.top = rc.top;
    clip.bottom = rc.bottom;
  }
  IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
  SetBkMode(dc, TRANSPARENT);
  if (!enabled) SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
  COLORREF normal = GetTextColor(dc);
  bool showSel = (e->focus || (style & ES_NOHIDESEL)) && e->SelStart() != e->SelEnd();
  int vis = VisibleLines(e) + 1;
  std::vector<int> xs;
  for (int li = e->firstLine; li < (int)e->lines.size() && li < e->firstLine + vis; li++) {
    const Line& l = e->lines[li];
    std::wstring s = Shown(e, l.start, l.len);
    int x = e->fmt.left + AlignOffset(e, l) - e->xOffset;
    int y = e->fmt.top + (li - e->firstLine) * e->lineHeight;
    Positions(e, dc, s, &xs);
    auto draw = [&](int from, int to) {
      // Runs between tabs, each at its place.
      int k = from;
      while (k < to) {
        if (s[k] == L'\t' && e->Multi()) {
          k++;
          continue;
        }
        int j = k;
        while (j < to && !(s[j] == L'\t' && e->Multi())) j++;
        ExtTextOutW(dc, x + xs[k], y, 0, NULL, s.c_str() + k, (UINT)(j - k), NULL);
        k = j;
      }
    };
    int a = 0, b = 0;
    if (showSel) {
      a = std::max(e->SelStart(), l.start) - l.start;
      b = std::min(e->SelEnd(), l.start + l.len + l.brk) - l.start;
      if (b > l.len) b = l.len;
    }
    if (showSel && a < b) {
      draw(0, a);
      RECT hi = {x + xs[a], y, x + xs[b], y + e->lineHeight};
      FillRect(dc, &hi, GetSysColorBrush(COLOR_HIGHLIGHT));
      SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
      draw(a, b);
      SetTextColor(dc, normal);
      draw(b, l.len);
    } else {
      draw(0, l.len);
    }
  }
  RestoreDC(dc, saved);
  SelectObject(dc, oldFont);
}

// ---------------------------------------------------------------------
// Words, for Ctrl with the arrows and double clicks

bool IsWordChar(wchar_t c) { return iswalnum(c) || c == L'_'; }

int WordLeft(Edit* e, int i) {
  const std::wstring& t = e->text;
  while (i > 0 && !IsWordChar(t[i - 1])) i--;
  while (i > 0 && IsWordChar(t[i - 1])) i--;
  return i;
}

int WordRight(Edit* e, int i) {
  const std::wstring& t = e->text;
  int n = (int)t.size();
  while (i < n && IsWordChar(t[i])) i++;
  while (i < n && !IsWordChar(t[i]) && t[i] != L'\r' && t[i] != L'\n') i++;
  return i;
}

int LineEnd(Edit* e, int li) { return e->lines[li].start + e->lines[li].len; }

void Key(Edit* e, UINT vk) {
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  int c = e->caret;
  int li = LineOf(e, c);
  int to = -1;
  switch (vk) {
    case VK_LEFT:
      if (!shift && e->SelStart() != e->SelEnd()) to = e->SelStart();
      else if (ctrl) to = WordLeft(e, c);
      else to = c > 0 ? c - 1 - ((c >= 2 && e->text[c - 1] == L'\n' && e->text[c - 2] == L'\r') ? 1 : 0) : 0;
      break;
    case VK_RIGHT:
      if (!shift && e->SelStart() != e->SelEnd()) to = e->SelEnd();
      else if (ctrl) to = WordRight(e, c);
      else to = c < (int)e->text.size() ? c + 1 : c;
      break;
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT: {
      if (!e->Multi()) {
        to = (vk == VK_UP || vk == VK_PRIOR) ? std::max(0, c - 1) : std::min((int)e->text.size(), c + 1);
        break;
      }
      int step = (vk == VK_UP || vk == VK_DOWN) ? 1 : std::max(1, VisibleLines(e) - 1);
      int target = li + ((vk == VK_UP || vk == VK_PRIOR) ? -step : step);
      target = std::max(0, std::min(target, (int)e->lines.size() - 1));
      POINT p = PosOf(e, c);
      if (vk == VK_PRIOR || vk == VK_NEXT) ScrollLines(e, target - li, 0);
      int y = e->fmt.top + (target - e->firstLine) * e->lineHeight;
      to = IndexAt(e, p.x, y);
      if (target != LineOf(e, to)) to = LineEnd(e, target);
      break;
    }
    case VK_HOME:
      to = ctrl ? 0 : e->lines[li].start;
      break;
    case VK_END:
      to = ctrl ? (int)e->text.size() : LineEnd(e, li);
      break;
    case VK_DELETE:
      if (e->Style() & ES_READONLY) {
        MessageBeep(0);
        return;
      }
      if (shift) {
        Copy(e);
        Clear(e);
        return;
      }
      if (e->SelStart() == e->SelEnd()) {
        int n = (int)e->text.size();
        if (c >= n) return;
        int len = (e->text[c] == L'\r' && c + 1 < n && e->text[c + 1] == L'\n') ? 2 : 1;
        e->anchor = c;
        e->caret = c + len;
      }
      Replace(e, L"", true, false);
      return;
    case VK_INSERT:
      if (shift) Paste(e);
      else if (ctrl) Copy(e);
      return;
    default:
      return;
  }
  if (to < 0) return;
  Select(e, shift ? e->anchor : to, to);
}

void Char(Edit* e, wchar_t ch) {
  DWORD style = e->Style();
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  switch (ch) {
    case 1:  // Ctrl+A
      Select(e, 0, (int)e->text.size());
      return;
    case 3:
      Copy(e);
      return;
    case 22:
      Paste(e);
      return;
    case 24:
      if (style & ES_READONLY) return;
      Copy(e);
      Clear(e);
      return;
    case 26:
      Undo(e);
      return;
    case 8: {
      if (style & ES_READONLY) {
        MessageBeep(0);
        return;
      }
      if (e->SelStart() == e->SelEnd()) {
        int c = e->caret;
        if (c == 0) return;
        int from = ctrl ? WordLeft(e, c) : c - ((c >= 2 && e->text[c - 1] == L'\n' && e->text[c - 2] == L'\r') ? 2 : 1);
        e->anchor = from;
        e->caret = c;
      }
      Replace(e, L"", true, true);
      return;
    }
    case L'\r':
    case L'\n':
      if (!e->Multi() || (style & ES_READONLY)) {
        if (!e->Multi()) return;
        MessageBeep(0);
        return;
      }
      Replace(e, L"\r\n", true, true);
      return;
    case L'\t':
      if (!e->Multi() || (style & ES_READONLY)) return;
      Replace(e, L"\t", true, true);
      return;
  }
  if (ch < L' ' || ch == 127) return;
  if (style & ES_READONLY) {
    MessageBeep(0);
    return;
  }
  if ((style & ES_NUMBER) && !(ch >= L'0' && ch <= L'9')) {
    MessageBeep(0);
    return;
  }
  Replace(e, std::wstring(1, ch), true, true);
}

void ContextMenu(Edit* e, POINT p) {
  if (GetFocus() != e->hwnd) SetFocus(e->hwnd);
  DWORD style = e->Style();
  bool ro = (style & ES_READONLY) != 0, sel = e->SelStart() != e->SelEnd();
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING | (e->canUndo && !ro ? 0 : MF_GRAYED), WM_UNDO, L"&Undo");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING | (sel && !ro && !e->password ? 0 : MF_GRAYED), WM_CUT, L"Cu&t");
  AppendMenuW(m, MF_STRING | (sel && !e->password ? 0 : MF_GRAYED), WM_COPY, L"&Copy");
  AppendMenuW(m, MF_STRING | (!ro && IsClipboardFormatAvailable(CF_UNICODETEXT) ? 0 : MF_GRAYED), WM_PASTE,
              L"&Paste");
  AppendMenuW(m, MF_STRING | (sel && !ro ? 0 : MF_GRAYED), WM_CLEAR, L"&Delete");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING | (e->text.empty() ? MF_GRAYED : 0), EM_SETSEL, L"Select &All");
  if (p.x == -1 && p.y == -1) {
    POINT c = PosOf(e, e->caret);
    ClientToScreen(e->hwnd, &c);
    p = c;
  }
  UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, e->hwnd, NULL);
  DestroyMenu(m);
  if (!IsWindow(e->hwnd)) return;
  if (cmd == EM_SETSEL)
    SendMessageW(e->hwnd, EM_SETSEL, 0, -1);
  else if (cmd)
    SendMessageW(e->hwnd, cmd, 0, 0);
}

// The local memory handle EM_GETHANDLE gives, filled with the text in
// the window's character set.
HLOCAL HandleWithText(Edit* e) {
  Window* w = winport::GetWindow(e->hwnd);
  bool ansi = w && w->ansiCreate;
  size_t bytes;
  std::string a;
  if (ansi) {
    a = Narrow(e->text.c_str());
    bytes = a.size() + 1;
  } else {
    bytes = (e->text.size() + 1) * sizeof(wchar_t);
  }
  if (!e->handle)
    e->handle = LocalAlloc(LMEM_MOVEABLE, bytes);
  else if (LocalSize(e->handle) < bytes)
    e->handle = LocalReAlloc(e->handle, bytes, LMEM_MOVEABLE);
  if (!e->handle) return NULL;
  void* p = LocalLock(e->handle);
  if (ansi)
    memcpy(p, a.c_str(), bytes);
  else
    memcpy(p, e->text.c_str(), bytes);
  LocalUnlock(e->handle);
  return e->handle;
}

std::wstring TextOfHandle(Edit* e, HLOCAL h) {
  Window* w = winport::GetWindow(e->hwnd);
  bool ansi = w && w->ansiCreate;
  std::wstring t;
  const void* p = LocalLock(h);
  if (p) t = ansi ? Widen((const char*)p) : std::wstring((const wchar_t*)p);
  LocalUnlock(h);
  return t;
}

void FontChanged(Edit* e) {
  MeasureDC m(e);
  TEXTMETRICW tm;
  GetTextMetricsW(m.dc, &tm);
  e->lineHeight = tm.tmHeight;
  e->avgWidth = tm.tmAveCharWidth ? tm.tmAveCharWidth : 6;
  if (e->caretMade) {
    DestroyCaret();
    e->caretMade = false;
  }
}

LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Edit* e = EditOf(h);
  if (!e && msg != WM_NCCREATE) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCCREATE: {
      e = new Edit;
      e->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)e);
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs->style & ES_PASSWORD) e->password = L'*';
      if (cs->style & ES_MULTILINE) {
        // Multiline controls without a horizontal scroll wrap; ES_CENTER
        // and ES_RIGHT stand only for them.
      }
      FontChanged(e);
      DefWindowProcW(h, msg, wp, lp);
      e->text = cs->lpszName ? cs->lpszName : L"";
      if (!(cs->style & ES_MULTILINE)) {
        size_t nl = e->text.find_first_of(L"\r\n");
        if (nl != std::wstring::npos) e->text.resize(nl);
      }
      return TRUE;
    }
    case WM_CREATE:
      Relayout(e);
      return 0;
    case WM_NCDESTROY:
      if (e->handle) LocalFree(e->handle);
      SetWindowLongPtrW(h, 0, 0);
      delete e;
      return DefWindowProcW(h, msg, wp, lp);
    case WM_SIZE:
      Relayout(e);
      ScrollToCaret(e);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      if (e->focus && e->caretMade) HideCaret(h);
      Paint(e, dc);
      if (e->focus && e->caretMade) ShowCaret(h);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_GETDLGCODE: {
      LRESULT r = DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_HASSETSEL;
      MSG* m = (MSG*)lp;
      if (e->Multi() && m && m->message == WM_KEYDOWN) {
        if (m->wParam == VK_RETURN && (e->Style() & ES_WANTRETURN)) r |= DLGC_WANTMESSAGE;
        if (m->wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000)) r |= DLGC_WANTMESSAGE;
      }
      return r;
    }
    case WM_SETFONT:
      e->font = (HFONT)wp;
      FontChanged(e);
      Relayout(e);
      PlaceCaret(e);
      if (!lp) ValidateRect(h, NULL);
      return 0;
    case WM_GETFONT:
      return (LRESULT)e->font;
    case WM_SETTEXT:
      SetText(e, lp ? (LPCWSTR)lp : L"");
      DefWindowProcW(h, msg, wp, (LPARAM)L"");
      Notify(e, EN_UPDATE);
      Notify(e, EN_CHANGE);
      return TRUE;
    case WM_GETTEXT: {
      if (!wp || !lp) return 0;
      size_t n = std::min(e->text.size(), (size_t)wp - 1);
      memcpy((void*)lp, e->text.c_str(), n * sizeof(wchar_t));
      ((wchar_t*)lp)[n] = 0;
      return (LRESULT)n;
    }
    case WM_GETTEXTLENGTH:
      return (LRESULT)e->text.size();
    case WM_ENABLE:
      Redraw(e);
      return 0;
    case WM_SETFOCUS:
      e->focus = true;
      PlaceCaret(e);
      Redraw(e);
      Notify(e, EN_SETFOCUS);
      return 0;
    case WM_KILLFOCUS:
      e->focus = false;
      if (e->caretMade) {
        DestroyCaret();
        e->caretMade = false;
      }
      e->tracking = false;
      Redraw(e);
      Notify(e, EN_KILLFOCUS);
      return 0;
    case WM_LBUTTONDOWN: {
      if (GetFocus() != h) SetFocus(h);
      int i = IndexAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp));
      bool shift = (wp & MK_SHIFT) != 0;
      Select(e, shift ? e->anchor : i, i);
      SetCapture(h);
      e->tracking = true;
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      int i = IndexAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp));
      int a = i, b = i;
      while (a > 0 && IsWordChar(e->text[a - 1])) a--;
      while (b < (int)e->text.size() && IsWordChar(e->text[b])) b++;
      Select(e, a, b);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (e->tracking) {
        int i = IndexAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp));
        Select(e, e->anchor, i);
      }
      return 0;
    case WM_LBUTTONUP:
      if (e->tracking) {
        e->tracking = false;
        if (GetCapture() == h) ReleaseCapture();
      }
      return 0;
    case WM_CAPTURECHANGED:
      e->tracking = false;
      return 0;
    case WM_CONTEXTMENU:
      ContextMenu(e, POINT{(SHORT)LOWORD(lp), (SHORT)HIWORD(lp)});
      return 0;
    case WM_MOUSEWHEEL:
      if (e->Multi()) ScrollLines(e, -3 * (SHORT)HIWORD(wp) / WHEEL_DELTA, 0);
      return 0;
    case WM_KEYDOWN:
      Key(e, (UINT)wp);
      return 0;
    case WM_CHAR:
      Char(e, (wchar_t)wp);
      return 0;
    case WM_VSCROLL:
    case WM_HSCROLL: {
      bool v = msg == WM_VSCROLL;
      int code = LOWORD(wp);
      int page = v ? VisibleLines(e) : std::max(1, (int)(e->fmt.right - e->fmt.left));
      int line = v ? 1 : e->avgWidth;
      int d = 0;
      switch (code) {
        case SB_LINEUP: d = -line; break;
        case SB_LINEDOWN: d = line; break;
        case SB_PAGEUP: d = -page; break;
        case SB_PAGEDOWN: d = page; break;
        case SB_TOP: d = v ? -e->firstLine : -e->xOffset; break;
        case SB_BOTTOM: d = v ? (int)e->lines.size() : e->maxWidth; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: d = (int)(short)HIWORD(wp) - (v ? e->firstLine : e->xOffset); break;
        default: return 0;
      }
      if (v) {
        ScrollLines(e, d, 0);
        Notify(e, EN_VSCROLL);
      } else {
        ScrollLines(e, 0, d);
        Notify(e, EN_HSCROLL);
      }
      return 0;
    }
    case WM_CUT:
      if (!(e->Style() & ES_READONLY)) {
        Copy(e);
        Clear(e);
      }
      return 0;
    case WM_COPY:
      Copy(e);
      return 0;
    case WM_PASTE:
      Paste(e);
      return 0;
    case WM_CLEAR:
      Clear(e);
      return 0;
    case WM_UNDO:
    case EM_UNDO:
      Undo(e);
      return e->canUndo;
    case EM_GETSEL: {
      int a = e->SelStart(), b = e->SelEnd();
      if (wp) *(DWORD*)wp = a;
      if (lp) *(DWORD*)lp = b;
      return (a > 0xFFFF || b > 0xFFFF) ? -1 : MAKELRESULT(a, b);
    }
    case EM_SETSEL: {
      int a = (int)wp, b = (int)lp;
      int n = (int)e->text.size();
      if (a == -1) {
        Select(e, e->caret, e->caret, false);
        return 0;
      }
      if (b == -1 || b > n) b = n;
      if (a < 0 || a > n) a = n;
      Select(e, a, b, false);
      return 0;
    }
    case EM_GETRECT:
      if (lp) *(RECT*)lp = e->fmt;
      return 0;
    case EM_SETRECT:
    case EM_SETRECTNP:
      if (lp) {
        e->fmt = *(const RECT*)lp;
        e->fmtSet = true;
      } else {
        e->fmtSet = false;
      }
      Relayout(e);
      if (msg == EM_SETRECTNP) ValidateRect(h, NULL);
      return 0;
    case EM_SCROLL: {
      if (!e->Multi()) return 0;
      int before = e->firstLine, d;
      switch (wp) {
        case SB_LINEUP: d = -1; break;
        case SB_LINEDOWN: d = 1; break;
        case SB_PAGEUP: d = -VisibleLines(e); break;
        case SB_PAGEDOWN: d = VisibleLines(e); break;
        default: return 0;
      }
      ScrollLines(e, d, 0);
      return MAKELRESULT(e->firstLine - before, TRUE);
    }
    case EM_LINESCROLL:
      if (!e->Multi()) return FALSE;
      ScrollLines(e, (int)lp, (int)wp * e->avgWidth);
      return TRUE;
    case EM_SCROLLCARET:
      ScrollToCaret(e);
      return TRUE;
    case EM_GETMODIFY:
      return e->modified;
    case EM_SETMODIFY:
      e->modified = wp != 0;
      return 0;
    case EM_GETLINECOUNT:
      return e->Multi() ? (LRESULT)e->lines.size() : 1;
    case EM_LINEINDEX: {
      int li = (int)wp;
      if (li == -1) li = LineOf(e, e->caret);
      if (li < 0 || li >= (int)e->lines.size()) return -1;
      return e->lines[li].start;
    }
    case EM_LINELENGTH: {
      int i = (int)wp;
      if (i == -1) {
        // The characters on the selected lines that are not selected.
        int la = LineOf(e, e->SelStart()), lb = LineOf(e, e->SelEnd());
        return (e->SelStart() - e->lines[la].start) + (LineEnd(e, lb) - e->SelEnd());
      }
      if (i < 0 || i > (int)e->text.size()) return 0;
      return e->lines[LineOf(e, i)].len;
    }
    case EM_LINEFROMCHAR: {
      int i = (int)wp;
      if (i == -1) i = e->SelStart();
      if (i > (int)e->text.size()) i = (int)e->text.size();
      return LineOf(e, i);
    }
    case EM_GETLINE: {
      int li = e->Multi() ? (int)wp : 0;
      if (li < 0 || li >= (int)e->lines.size() || !lp) return 0;
      WORD max = *(WORD*)lp;
      const Line& l = e->lines[li];
      int n = std::min((int)max, l.len);
      memcpy((void*)lp, e->text.c_str() + l.start, n * sizeof(wchar_t));
      if (n < max) ((wchar_t*)lp)[n] = 0;
      return n;
    }
    case EM_REPLACESEL:
      Replace(e, lp ? (LPCWSTR)lp : L"", wp != 0, false);
      return 0;
    case EM_LIMITTEXT:
      e->limit = wp ? (UINT)wp : 0x7FFFFFFE;
      return 0;
    case EM_GETLIMITTEXT:
      return e->limit;
    case EM_CANUNDO:
      return e->canUndo;
    case EM_EMPTYUNDOBUFFER:
      e->canUndo = false;
      return 0;
    case EM_FMTLINES:
      return wp;
    case EM_SETTABSTOPS:
      e->tabs.clear();
      if (wp == 0) {
        // The default: every 32 dialog units.
      } else if (lp) {
        const INT* t = (const INT*)lp;
        for (WPARAM i = 0; i < wp; i++) e->tabs.push_back(t[i]);
      }
      Relayout(e);
      return TRUE;
    case EM_SETPASSWORDCHAR: {
      e->password = (wchar_t)wp;
      LONG style = GetWindowLongW(h, GWL_STYLE);
      SetWindowLongW(h, GWL_STYLE, wp ? (style | ES_PASSWORD) : (style & ~ES_PASSWORD));
      Relayout(e);
      return 0;
    }
    case EM_GETPASSWORDCHAR:
      return e->password;
    case EM_GETFIRSTVISIBLELINE:
      if (e->Multi()) return e->firstLine;
      return IndexAt(e, e->fmt.left, e->fmt.top);
    case EM_SETREADONLY: {
      LONG style = GetWindowLongW(h, GWL_STYLE);
      SetWindowLongW(h, GWL_STYLE, wp ? (style | ES_READONLY) : (style & ~ES_READONLY));
      Redraw(e);
      return TRUE;
    }
    case EM_SETWORDBREAKPROC:
      e->wordBreak = (EDITWORDBREAKPROCW)lp;
      return 0;
    case EM_GETWORDBREAKPROC:
      return (LRESULT)e->wordBreak;
    case EM_SETMARGINS: {
      int l = (SHORT)LOWORD(lp), r = (SHORT)HIWORD(lp);
      if (wp & EC_LEFTMARGIN) e->leftMargin = l == (SHORT)EC_USEFONTINFO ? e->avgWidth / 3 + 1 : l;
      if (wp & EC_RIGHTMARGIN) e->rightMargin = r == (SHORT)EC_USEFONTINFO ? e->avgWidth / 3 + 1 : r;
      Relayout(e);
      return 0;
    }
    case EM_GETMARGINS:
      return MAKELRESULT(e->leftMargin, e->rightMargin);
    case EM_POSFROMCHAR: {
      int i = (int)wp;
      if (i < 0 || i > (int)e->text.size()) return -1;
      POINT p = PosOf(e, i);
      return MAKELRESULT((WORD)(SHORT)p.x, (WORD)(SHORT)p.y);
    }
    case EM_CHARFROMPOS: {
      int li;
      int i = IndexAt(e, (SHORT)LOWORD(lp), (SHORT)HIWORD(lp), &li);
      return MAKELRESULT(i, li);
    }
    case EM_GETHANDLE:
      if (!e->Multi()) return 0;
      return (LRESULT)HandleWithText(e);
    case EM_SETHANDLE:
      if (!e->Multi() || !wp) return 0;
      if (e->handle && e->handle != (HLOCAL)wp) LocalFree(e->handle);
      e->handle = (HLOCAL)wp;
      SetText(e, TextOfHandle(e, e->handle));
      return 0;
    case EM_GETTHUMB:
      return e->firstLine;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterEditClass() {
  static bool done = false;
  if (done) return;
  done = true;
  RegisterSystemClass(L"Edit", EditProc, CS_DBLCLKS | CS_PARENTDC, sizeof(void*), NULL, MAKEINTRESOURCEW(32513));
}

}  // namespace winport
