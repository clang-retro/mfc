// imm.cpp - the input method manager (imm.h). The desktop's input
// method (an IBus or Fcitx engine through Wayland's text-input protocol
// or X's XIM, or macOS's text input system) builds text from keys; its
// backend hands what it is composing and what it has finished here, and
// the window with the focus gets them as Windows would give them:
// WM_IME_STARTCOMPOSITION, WM_IME_COMPOSITION and WM_IME_ENDCOMPOSITION,
// with ImmGetCompositionString for the text. DefWindowProc shows the
// composition in a small window at the caret and turns the finished text
// into WM_IME_CHAR and then WM_CHAR, so a program that knows nothing of
// input methods gets characters.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <imm.h>

#include <map>
#include <string>

#include "user.h"
#include "winport.h"

#ifndef IMM_ERROR_NODATA
#define IMM_ERROR_NODATA (-1)
#define IMM_ERROR_GENERAL (-2)
#endif

namespace winport {
namespace {

// An input context: what is being composed for the windows that use it,
// and where the program wants the input method's windows.
struct Context {
  std::wstring comp, result;  // the composition, and the last text finished
  int cursor = 0;             // in comp, in characters
  bool composing = false;     // WM_IME_STARTCOMPOSITION sent
  bool open = true;
  HWND window = NULL;  // the window composing
  COMPOSITIONFORM form = {};
  CANDIDATEFORM cand[4] = {};
  LOGFONTW font = {};
  bool hasFont = false;
  bool threadDefault = false;
};

// Contexts by handle: small numbers, so that a program of the time that
// keeps one in a DWORD keeps it whole.
std::map<UINT_PTR, Context*>& Contexts() {
  static std::map<UINT_PTR, Context*> m;
  return m;
}
UINT_PTR g_nextContext = 1;
std::map<DWORD, UINT_PTR>& ThreadDefaults() {
  static std::map<DWORD, UINT_PTR> m;
  return m;
}
// ImmAssociateContext's choices; 0 is none, which turns the input method
// off for the window.
std::map<HWND, UINT_PTR>& Associations() {
  static std::map<HWND, UINT_PTR> m;
  return m;
}

Context* Get(HIMC h) {
  auto it = Contexts().find((UINT_PTR)h);
  return it == Contexts().end() ? NULL : it->second;
}

UINT_PTR NewContext(bool threadDefault) {
  Context* c = new Context;
  c->threadDefault = threadDefault;
  UINT_PTR id = g_nextContext++;
  Contexts()[id] = c;
  return id;
}

// The context a window uses (call with the lock held): its own, or its
// thread's.
UINT_PTR ContextIdFor(Window* w) {
  auto a = Associations().find(w->hwnd);
  if (a != Associations().end()) return a->second;
  UINT_PTR& d = ThreadDefaults()[w->tid];
  if (!d) d = NewContext(true);
  return d;
}

Context* ContextFor(HWND h) {
  UserGuard g(UserLock());
  Window* w = GetWindow(h);
  if (!w) return NULL;
  return Get((HIMC)ContextIdFor(w));
}

// The backend's text waiting for its window's thread, by the number in
// kImeUpdate's lParam.
struct Update {
  bool commit;
  std::wstring text;
  int cursor;
};
const UINT kImeUpdate = 0x0298;
std::map<LPARAM, Update>& Updates() {
  static std::map<LPARAM, Update> m;
  return m;
}
LPARAM g_nextUpdate = 1;

// The window that the backend's text goes to: the focus of the window's
// thread, else its active window.
Window* InputTarget(Window* top) {
  Queue* q = QueueFor(top->tid);
  Window* w = GetWindow(q->focus);
  if (!w) w = GetWindow(q->active);
  return w;
}

void QueueUpdate(Window* top, const Update& u) {
  UserGuard g(UserLock());
  if (!GetWindow(top->hwnd)) return;
  Window* w = InputTarget(top);
  if (!w) return;
  LPARAM id = g_nextUpdate++;
  Updates()[id] = u;
  QueueInput(w, kImeUpdate, 0, id);
}

const LPARAM kCompChanged = GCS_COMPSTR | GCS_COMPATTR | GCS_COMPCLAUSE | GCS_CURSORPOS | GCS_DELTASTART;

// Ends a composition: with the text finished (commit), or thrown away.
void Finish(Context* c, HWND h, bool commit, const std::wstring& text) {
  if (!c->composing) {
    if (!commit || text.empty()) return;
    c->composing = true;
    c->window = h;
    SendMessageW(h, WM_IME_STARTCOMPOSITION, 0, 0);
  }
  c->comp.clear();
  c->cursor = 0;
  if (commit && !text.empty()) {
    c->result = text;
    SendMessageW(h, WM_IME_COMPOSITION, (WPARAM)text[0], GCS_RESULTSTR | GCS_RESULTCLAUSE);
  } else {
    // A composition taken back: the composition string is empty.
    SendMessageW(h, WM_IME_COMPOSITION, 0, 0);
  }
  c->composing = false;
  c->window = NULL;
  SendMessageW(h, WM_IME_ENDCOMPOSITION, 0, 0);
}

// The composition window: DefWindowProc's way to show the composition
// for a program that does not draw it. One for each thread, owned by the
// top-level window of the window composing.
thread_local HWND t_compWindow = NULL;

HFONT CompositionFont(HWND target, bool* made) {
  *made = false;
  Context* c = ContextFor(target);
  if (c && c->hasFont) {
    *made = true;
    return CreateFontIndirectW(&c->font);
  }
  HFONT f = (HFONT)SendMessageW(target, WM_GETFONT, 0, 0);
  return f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

// Where the composition goes, in the target's client coordinates.
POINT CompositionPoint(HWND target) {
  Context* c = ContextFor(target);
  if (c && (c->form.dwStyle & (CFS_POINT | CFS_FORCE_POSITION | CFS_RECT))) return c->form.ptCurrentPos;
  HWND owner;
  RECT r;
  if (CaretRect(&owner, &r) && owner == target) return POINT{r.left, r.top};
  return POINT{0, 0};
}

LRESULT CALLBACK CompProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      HWND target = (HWND)GetWindowLongPtrW(h, GWLP_USERDATA);
      Context* c = ContextFor(target);
      if (c) {
        bool made;
        HFONT f = CompositionFont(target, &made);
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        int m = Scale96(2);
        TextOutW(dc, m, m, c->comp.c_str(), (int)c->comp.size());
        SIZE all, before;
        GetTextExtentPoint32W(dc, c->comp.c_str(), (int)c->comp.size(), &all);
        GetTextExtentPoint32W(dc, c->comp.c_str(), c->cursor, &before);
        // The composition is underlined, with a caret where the input
        // method's cursor is.
        int base = m + tm.tmHeight;
        RECT u = {m, base, m + all.cx, base + std::max(1, Scale96(1))};
        FillRect(dc, &u, GetSysColorBrush(COLOR_WINDOWTEXT));
        RECT k = {m + before.cx, m, m + before.cx + std::max(1, Scale96(1)), base};
        FillRect(dc, &k, GetSysColorBrush(COLOR_WINDOWTEXT));
        SelectObject(dc, old);
        if (made) DeleteObject(f);
      }
      EndPaint(h, &ps);
      return 0;
    }
  }
  return DefWindowProcW(h, msg, wp, lp);
}

void HideComposition() {
  if (t_compWindow && IsWindow(t_compWindow)) ShowWindow(t_compWindow, SW_HIDE);
}

void ShowComposition(HWND target) {
  Context* c = ContextFor(target);
  if (!c || c->comp.empty()) {
    HideComposition();
    return;
  }
  HWND top = GetAncestor(target, GA_ROOT);
  if (t_compWindow && (!IsWindow(t_compWindow) || ::GetWindow(t_compWindow, GW_OWNER) != top)) {
    if (IsWindow(t_compWindow)) DestroyWindow(t_compWindow);
    t_compWindow = NULL;
  }
  if (!t_compWindow) {
    t_compWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"IME", NULL,
                                   WS_POPUP | WS_BORDER, 0, 0, 0, 0, top, NULL, NULL, NULL);
    if (!t_compWindow) return;
  }
  SetWindowLongPtrW(t_compWindow, GWLP_USERDATA, (LONG_PTR)target);
  HDC dc = GetDC(t_compWindow);
  bool made;
  HFONT f = CompositionFont(target, &made);
  HGDIOBJ old = SelectObject(dc, f);
  SIZE s;
  GetTextExtentPoint32W(dc, c->comp.c_str(), (int)c->comp.size(), &s);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  if (made) DeleteObject(f);
  ReleaseDC(t_compWindow, dc);
  int m = Scale96(2), b = GetSystemMetrics(SM_CXBORDER);
  POINT p = CompositionPoint(target);
  ClientToScreen(target, &p);
  // The window's text lies where the program's would: the border and
  // the margin go outside the point.
  SetWindowPos(t_compWindow, HWND_TOPMOST, p.x - m - b, p.y - m - b, s.cx + 2 * m + 2 * b + Scale96(1),
               tm.tmHeight + 2 * m + 2 * b + Scale96(1), SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(t_compWindow, NULL, TRUE);
}

// WM_IME_CHAR to an ANSI DefWindowProc comes a UTF-8 byte at a time.
thread_local int t_pending = 0;
thread_local DWORD t_char = 0;

}  // namespace

void RegisterImeClass() {
  static bool done = false;
  if (done) return;
  done = true;
  RegisterSystemClass(L"IME", CompProc, CS_SAVEBITS, 0, (HBRUSH)(COLOR_WINDOW + 1), MAKEINTRESOURCEW(32512));
}

void ImePreedit(Window* top, const std::wstring& text, int cursor) { QueueUpdate(top, Update{false, text, cursor}); }
void ImeCommit(Window* top, const std::wstring& text) { QueueUpdate(top, Update{true, text, 0}); }

bool IsImeUpdate(UINT msg) { return msg == kImeUpdate; }

void DeliverImeUpdate(HWND h, LPARAM id) {
  Update u;
  {
    UserGuard g(UserLock());
    auto it = Updates().find(id);
    if (it == Updates().end()) return;
    u = it->second;
    Updates().erase(it);
  }
  Window* w = GetWindow(h);
  if (!w) return;
  Context* c = ContextFor(h);
  if (!c || !c->open) {
    // No input method for the window: finished text is just characters.
    if (u.commit)
      for (wchar_t ch : u.text) QueueInput(w, WM_CHAR, (WPARAM)ch, 1);
    return;
  }
  if (c->composing && c->window != h) Finish(c, c->window, false, L"");
  if (u.commit || u.text.empty()) {
    Finish(c, h, u.commit, u.text);
    return;
  }
  if (!c->composing) {
    c->composing = true;
    c->window = h;
    SendMessageW(h, WM_IME_STARTCOMPOSITION, 0, 0);
  }
  c->comp = u.text;
  c->cursor = std::max(0, std::min(u.cursor, (int)u.text.size()));
  SendMessageW(h, WM_IME_COMPOSITION, (WPARAM)u.text.back(), kCompChanged);
}

bool DefImeMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode, LRESULT* r) {
  *r = 0;
  switch (msg) {
    case WM_IME_STARTCOMPOSITION:
      return true;
    case WM_IME_COMPOSITION: {
      Context* c = ContextFor(h);
      if (c && (lp & GCS_RESULTSTR)) {
        std::wstring text = c->result;
        for (wchar_t ch : text) SendMessageW(h, WM_IME_CHAR, (WPARAM)ch, 1);
      }
      if (lp == 0 || (lp & GCS_COMPSTR) || (lp & GCS_RESULTSTR)) ShowComposition(h);
      return true;
    }
    case WM_IME_ENDCOMPOSITION:
      HideComposition();
      return true;
    case WM_IME_CHAR: {
      Window* w = GetWindow(h);
      if (!w) return true;
      DWORD ch = (DWORD)wp;
      if (!unicode && ch >= 0x80 && ch < 0x100) {
        BYTE b = (BYTE)ch;
        if ((b & 0xC0) == 0x80) {
          if (t_pending == 0) return true;
          t_char = (t_char << 6) | (b & 0x3F);
          if (--t_pending > 0) return true;
          ch = t_char;
        } else {
          t_pending = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : 0;
          t_char = b & (t_pending == 1 ? 0x1F : t_pending == 2 ? 0x0F : 0x07);
          if (t_pending) return true;
        }
      }
      QueueInput(w, WM_CHAR, (WPARAM)ch, lp);
      return true;
    }
    case WM_IME_KEYDOWN:
      PostMessageW(h, WM_KEYDOWN, wp, lp);
      return true;
    case WM_IME_KEYUP:
      PostMessageW(h, WM_KEYUP, wp, lp);
      return true;
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
    case 0x0288:  // WM_IME_REQUEST
      return true;
  }
  return false;
}

bool ImeCursorRect(Window* top, RECT* out) {
  HWND target, caretOwner;
  RECT r;
  {
    UserGuard g(UserLock());
    if (!GetWindow(top->hwnd)) return false;
    Window* w = InputTarget(top);
    if (!w) return false;
    target = w->hwnd;
  }
  Context* c = ContextFor(target);
  if (c && (c->cand[0].dwStyle & (CFS_CANDIDATEPOS | CFS_EXCLUDE))) {
    if (c->cand[0].dwStyle & CFS_EXCLUDE)
      r = c->cand[0].rcArea;
    else
      r = RECT{c->cand[0].ptCurrentPos.x, c->cand[0].ptCurrentPos.y, c->cand[0].ptCurrentPos.x + 1,
               c->cand[0].ptCurrentPos.y + 1};
  } else if (c && (c->form.dwStyle & (CFS_POINT | CFS_FORCE_POSITION | CFS_RECT))) {
    POINT p = c->form.ptCurrentPos;
    int height = 16;
    HWND owner;
    RECT k;
    if (CaretRect(&owner, &k) && owner == target) height = k.bottom - k.top;
    r = RECT{p.x, p.y, p.x + 1, p.y + height};
  } else if (CaretRect(&caretOwner, &r) && caretOwner == target) {
    if (r.right <= r.left) r.right = r.left + 1;
  } else {
    GetClientRect(target, &r);
    r.right = r.left + 1;
  }
  POINT a = {r.left, r.top}, b = {r.right, r.bottom};
  ClientToScreen(target, &a);
  ClientToScreen(target, &b);
  UserGuard g(UserLock());
  if (!GetWindow(top->hwnd)) return false;
  *out = RECT{a.x - top->rect.left, a.y - top->rect.top, b.x - top->rect.left, b.y - top->rect.top};
  return true;
}

// Tells the backend that where the input method's windows go has changed.
static void CursorMoved(HWND h) {
  Window* top;
  {
    UserGuard g(UserLock());
    Window* w = GetWindow(h);
    if (!w) return;
    top = w->TopLevel();
  }
  GetBackend()->ImeCursorMoved(top);
}

static void CursorMovedFor(Context* c) {
  HWND f = GetFocus();
  if (f && ContextFor(f) == c) CursorMoved(f);
}

void ImeCaretMoved(HWND h) {
  if (h && GetFocus() == h) CursorMoved(h);
}

// Ends a composition from the program's side (ImmNotifyIME, closing the
// input method), and has the input method forget it too.
static void EndFromProgram(Context* c, bool commit) {
  HWND h = c->window;
  if (!c->composing || !IsWindow(h)) return;
  std::wstring text = c->comp;
  Finish(c, h, commit, text);
  Window* top;
  {
    UserGuard g(UserLock());
    Window* w = GetWindow(h);
    if (!w) return;
    top = w->TopLevel();
  }
  GetBackend()->ImeReset(top);
}

}  // namespace winport

using namespace winport;

extern "C" {

HIMC WINAPI ImmGetContext(HWND h) {
  UserGuard g(UserLock());
  Window* w = GetWindow(h);
  if (!w) return NULL;
  return (HIMC)ContextIdFor(w);
}

BOOL WINAPI ImmReleaseContext(HWND, HIMC) { return TRUE; }

HIMC WINAPI ImmAssociateContext(HWND h, HIMC himc) {
  UserGuard g(UserLock());
  Window* w = GetWindow(h);
  if (!w) return NULL;
  UINT_PTR before = ContextIdFor(w);
  if (himc && !Get(himc)) return NULL;
  Associations()[h] = (UINT_PTR)himc;
  return (HIMC)before;
}

HIMC WINAPI ImmCreateContext(void) {
  UserGuard g(UserLock());
  return (HIMC)NewContext(false);
}

BOOL WINAPI ImmDestroyContext(HIMC himc) {
  UserGuard g(UserLock());
  Context* c = Get(himc);
  if (!c || c->threadDefault) return FALSE;
  for (auto it = Associations().begin(); it != Associations().end();)
    if (it->second == (UINT_PTR)himc)
      it = Associations().erase(it);
    else
      ++it;
  Contexts().erase((UINT_PTR)himc);
  delete c;
  return TRUE;
}

BOOL WINAPI ImmGetOpenStatus(HIMC himc) {
  Context* c = Get(himc);
  return c && c->open;
}

BOOL WINAPI ImmSetOpenStatus(HIMC himc, BOOL open) {
  Context* c = Get(himc);
  if (!c) return FALSE;
  if (!open) EndFromProgram(c, false);
  c->open = open != FALSE;
  return TRUE;
}

BOOL WINAPI ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM f) {
  Context* c = Get(himc);
  if (!c || !f) return FALSE;
  c->form = *f;
  if (c->composing && IsWindow(c->window)) ShowComposition(c->window);
  CursorMovedFor(c);
  return TRUE;
}

BOOL WINAPI ImmGetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM f) {
  Context* c = Get(himc);
  if (!c || !f) return FALSE;
  *f = c->form;
  return TRUE;
}

BOOL WINAPI ImmSetCandidateWindow(HIMC himc, LPCANDIDATEFORM f) {
  Context* c = Get(himc);
  if (!c || !f || f->dwIndex >= 4) return FALSE;
  c->cand[f->dwIndex] = *f;
  CursorMovedFor(c);
  return TRUE;
}

BOOL WINAPI ImmGetCandidateWindow(HIMC himc, DWORD index, LPCANDIDATEFORM f) {
  Context* c = Get(himc);
  if (!c || !f || index >= 4 || !c->cand[index].dwStyle) return FALSE;
  *f = c->cand[index];
  return TRUE;
}

BOOL WINAPI ImmSetCompositionFontW(HIMC himc, LPLOGFONTW f) {
  Context* c = Get(himc);
  if (!c || !f) return FALSE;
  c->font = *f;
  c->hasFont = true;
  if (c->composing && IsWindow(c->window)) ShowComposition(c->window);
  return TRUE;
}

BOOL WINAPI ImmSetCompositionFontA(HIMC himc, LPLOGFONTA f) {
  if (!f) return FALSE;
  LOGFONTW w;
  memcpy(&w, f, offsetof(LOGFONTA, lfFaceName));
  std::wstring face = Widen(f->lfFaceName);
  lstrcpynW(w.lfFaceName, face.c_str(), LF_FACESIZE);
  return ImmSetCompositionFontW(himc, &w);
}

BOOL WINAPI ImmGetCompositionFontW(HIMC himc, LPLOGFONTW f) {
  Context* c = Get(himc);
  if (!c || !f) return FALSE;
  if (c->hasFont) {
    *f = c->font;
  } else {
    GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof *f, f);
  }
  return TRUE;
}

BOOL WINAPI ImmGetCompositionFontA(HIMC himc, LPLOGFONTA f) {
  LOGFONTW w;
  if (!f || !ImmGetCompositionFontW(himc, &w)) return FALSE;
  memcpy(f, &w, offsetof(LOGFONTA, lfFaceName));
  std::string face = Narrow(w.lfFaceName);
  lstrcpynA(f->lfFaceName, face.c_str(), LF_FACESIZE);
  return TRUE;
}

// Copies n bytes of what a GCS_ index asks for into buf, or gives the
// size it takes when len is 0.
static LONG Give(const void* data, LONG n, LPVOID buf, DWORD len) {
  if (len == 0 || !buf) return n;
  if ((DWORD)n > len) n = (LONG)len;
  memcpy(buf, data, n);
  return n;
}

LONG WINAPI ImmGetCompositionStringW(HIMC himc, DWORD index, LPVOID buf, DWORD len) {
  Context* c = Get(himc);
  if (!c) return IMM_ERROR_GENERAL;
  switch (index) {
    case GCS_COMPSTR:
    case GCS_COMPREADSTR:
      return Give(c->comp.data(), (LONG)(c->comp.size() * sizeof(WCHAR)), buf, len);
    case GCS_RESULTSTR:
    case GCS_RESULTREADSTR:
      return Give(c->result.data(), (LONG)(c->result.size() * sizeof(WCHAR)), buf, len);
    case GCS_COMPATTR:
    case GCS_COMPREADATTR: {
      std::string attr(c->comp.size(), (char)ATTR_INPUT);
      return Give(attr.data(), (LONG)attr.size(), buf, len);
    }
    case GCS_COMPCLAUSE:
    case GCS_COMPREADCLAUSE:
    case GCS_RESULTCLAUSE:
    case GCS_RESULTREADCLAUSE: {
      // One clause: the whole string.
      bool result = index == GCS_RESULTCLAUSE || index == GCS_RESULTREADCLAUSE;
      DWORD clause[2] = {0, (DWORD)(result ? c->result.size() : c->comp.size())};
      if (!clause[1]) return 0;
      return Give(clause, sizeof clause, buf, len);
    }
    case GCS_CURSORPOS:
      return c->cursor;
    case GCS_DELTASTART:
      return 0;
  }
  return IMM_ERROR_GENERAL;
}

LONG WINAPI ImmGetCompositionStringA(HIMC himc, DWORD index, LPVOID buf, DWORD len) {
  Context* c = Get(himc);
  if (!c) return IMM_ERROR_GENERAL;
  std::string comp = Narrow(c->comp.c_str(), (int)c->comp.size());
  std::string result = Narrow(c->result.c_str(), (int)c->result.size());
  switch (index) {
    case GCS_COMPSTR:
    case GCS_COMPREADSTR:
      return Give(comp.data(), (LONG)comp.size(), buf, len);
    case GCS_RESULTSTR:
    case GCS_RESULTREADSTR:
      return Give(result.data(), (LONG)result.size(), buf, len);
    case GCS_COMPATTR:
    case GCS_COMPREADATTR: {
      std::string attr(comp.size(), (char)ATTR_INPUT);
      return Give(attr.data(), (LONG)attr.size(), buf, len);
    }
    case GCS_COMPCLAUSE:
    case GCS_COMPREADCLAUSE:
    case GCS_RESULTCLAUSE:
    case GCS_RESULTREADCLAUSE: {
      bool isResult = index == GCS_RESULTCLAUSE || index == GCS_RESULTREADCLAUSE;
      DWORD clause[2] = {0, (DWORD)(isResult ? result.size() : comp.size())};
      if (!clause[1]) return 0;
      return Give(clause, sizeof clause, buf, len);
    }
    case GCS_CURSORPOS:
      return (LONG)Narrow(c->comp.c_str(), c->cursor).size();
    case GCS_DELTASTART:
      return 0;
  }
  return IMM_ERROR_GENERAL;
}

BOOL WINAPI ImmNotifyIME(HIMC himc, DWORD action, DWORD index, DWORD) {
  Context* c = Get(himc);
  if (!c || action != NI_COMPOSITIONSTR) return FALSE;
  if (index != CPS_COMPLETE && index != CPS_CANCEL) return FALSE;
  EndFromProgram(c, index == CPS_COMPLETE);
  return TRUE;
}

HWND WINAPI ImmGetDefaultIMEWnd(HWND) { return IsWindow(t_compWindow) ? t_compWindow : NULL; }

// The desktop may have an input method for any keyboard.
BOOL WINAPI ImmIsIME(HKL) { return TRUE; }

}  // extern "C"
