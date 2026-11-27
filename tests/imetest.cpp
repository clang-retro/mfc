// imetest.cpp - tests for the portable layer's input method support: the
// text an input method composes and finishes reaches the window with the
// focus as WM_IME_ messages, ImmGetCompositionString gives it,
// DefWindowProc shows the composition and makes characters of the
// finished text (in UTF-8 for an ANSI window), a window without an input
// context gets plain characters, ImmNotifyIME ends a composition, and the
// input method's candidates go by the caret or where the program says.
//
// The desktop's input method is not here, so the test hands the text in
// the way a backend does, through the port's own functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <imm.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace winport {
struct Window;
Window* GetWindow(HWND h);
void ImePreedit(Window* top, const std::wstring& text, int cursor);
void ImeCommit(Window* top, const std::wstring& text);
bool ImeCursorRect(Window* top, RECT* r);
}  // namespace winport

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

struct Seen {
  UINT msg;
  WPARAM wp;
  LPARAM lp;
  std::wstring comp, result;
  LONG cursor;
};
static std::vector<Seen> g_seen;

static void Record(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Seen s = {msg, wp, lp, L"", L"", -1};
  if (msg == WM_IME_COMPOSITION) {
    HIMC c = ImmGetContext(h);
    wchar_t buf[64];
    LONG n = ImmGetCompositionStringW(c, GCS_COMPSTR, buf, sizeof buf);
    if (n > 0) s.comp.assign(buf, n / sizeof(wchar_t));
    n = ImmGetCompositionStringW(c, GCS_RESULTSTR, buf, sizeof buf);
    if (n > 0) s.result.assign(buf, n / sizeof(wchar_t));
    s.cursor = ImmGetCompositionStringW(c, GCS_CURSORPOS, NULL, 0);
    ImmReleaseContext(h, c);
  }
  g_seen.push_back(s);
}

static bool Interesting(UINT msg) {
  return msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_ENDCOMPOSITION ||
         msg == WM_IME_CHAR || msg == WM_CHAR;
}

static LRESULT CALLBACK ProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (Interesting(msg)) Record(h, msg, wp, lp);
  return DefWindowProcW(h, msg, wp, lp);
}
static LRESULT CALLBACK ProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (Interesting(msg)) Record(h, msg, wp, lp);
  return DefWindowProcA(h, msg, wp, lp);
}

static void Pump() {
  MSG m;
  for (int i = 0; i < 5; i++) {
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    Sleep(5);
  }
}

static std::vector<Seen> Take() {
  Pump();
  std::vector<Seen> s;
  s.swap(g_seen);
  return s;
}

static int Count(const std::vector<Seen>& s, UINT msg) {
  int n = 0;
  for (const Seen& e : s) n += e.msg == msg;
  return n;
}

static std::wstring Chars(const std::vector<Seen>& s) {
  std::wstring t;
  for (const Seen& e : s)
    if (e.msg == WM_CHAR) t += (wchar_t)e.wp;
  return t;
}

static bool Visible(HWND h) { return h && IsWindowVisible(h); }

static void Composing(HWND h) {
  winport::Window* top = winport::GetWindow(h);
  CHECK(top != NULL);
  CHECK(GetFocus() == h);

  // Composing: the composition begins and changes.
  winport::ImePreedit(top, L"n", 1);
  winport::ImePreedit(top, L"ni", 1);
  std::vector<Seen> s = Take();
  CHECK(s.size() == 3);
  if (s.size() == 3) {
    CHECK(s[0].msg == WM_IME_STARTCOMPOSITION);
    CHECK(s[1].msg == WM_IME_COMPOSITION && (s[1].lp & GCS_COMPSTR) && s[1].comp == L"n" && s[1].cursor == 1);
    CHECK(s[2].msg == WM_IME_COMPOSITION && s[2].comp == L"ni" && s[2].cursor == 1 && s[2].wp == L'i');
  }
  HIMC c = ImmGetContext(h);
  CHECK(c != NULL);
  BYTE attr[8] = {};
  CHECK(ImmGetCompositionStringW(c, GCS_COMPATTR, attr, sizeof attr) == 2 && attr[0] == ATTR_INPUT);
  DWORD clause[4] = {};
  CHECK(ImmGetCompositionStringW(c, GCS_COMPCLAUSE, clause, sizeof clause) == 8 && clause[1] == 2);
  CHECK(ImmGetCompositionStringW(c, GCS_COMPSTR, NULL, 0) == (LONG)(2 * sizeof(wchar_t)));
  CHECK(ImmGetCompositionStringW(c, 0x12345, NULL, 0) < 0);
  // DefWindowProc shows it in the composition window.
  HWND ime = ImmGetDefaultIMEWnd(h);
  CHECK(Visible(ime));

  // Finishing: the result, the end, and characters by way of WM_IME_CHAR.
  winport::ImeCommit(top, L"\x4F60\x597D");
  s = Take();
  CHECK(Count(s, WM_IME_STARTCOMPOSITION) == 0);
  CHECK(Count(s, WM_IME_ENDCOMPOSITION) == 1);
  CHECK(Count(s, WM_IME_CHAR) == 2);
  CHECK(Chars(s) == L"\x4F60\x597D");
  bool result = false;
  for (const Seen& e : s)
    if (e.msg == WM_IME_COMPOSITION && (e.lp & GCS_RESULTSTR)) result = e.result == L"\x4F60\x597D" && e.comp.empty();
  CHECK(result);
  CHECK(!Visible(ime));
  char a[16] = {};
  CHECK(ImmGetCompositionStringA(c, GCS_RESULTSTR, a, sizeof a) == 6 && memcmp(a, "\xE4\xBD\xA0\xE5\xA5\xBD", 6) == 0);

  // Text finished at once, with no composition before it.
  winport::ImeCommit(top, L"x");
  s = Take();
  CHECK(Count(s, WM_IME_STARTCOMPOSITION) == 1 && Count(s, WM_IME_ENDCOMPOSITION) == 1);
  CHECK(Chars(s) == L"x");

  // A composition taken back: WM_IME_COMPOSITION with nothing, the end,
  // no characters.
  winport::ImePreedit(top, L"ka", 2);
  winport::ImePreedit(top, L"", 0);
  s = Take();
  CHECK(Count(s, WM_IME_STARTCOMPOSITION) == 1 && Count(s, WM_IME_ENDCOMPOSITION) == 1);
  CHECK(Chars(s).empty());
  CHECK(!s.empty() && s.size() >= 2 && s[s.size() - 2].msg == WM_IME_COMPOSITION && s[s.size() - 2].lp == 0);

  // The program finishes the composition itself.
  winport::ImePreedit(top, L"abc", 3);
  Take();
  CHECK(ImmNotifyIME(c, NI_COMPOSITIONSTR, CPS_COMPLETE, 0));
  s = Take();
  CHECK(Count(s, WM_IME_ENDCOMPOSITION) == 1);
  CHECK(Chars(s) == L"abc");
  // Or throws it away.
  winport::ImePreedit(top, L"def", 3);
  Take();
  CHECK(ImmNotifyIME(c, NI_COMPOSITIONSTR, CPS_CANCEL, 0));
  s = Take();
  CHECK(Count(s, WM_IME_ENDCOMPOSITION) == 1 && Chars(s).empty());
  CHECK(ImmGetCompositionStringW(c, GCS_COMPSTR, NULL, 0) == 0);

  // Where the candidates go: by the caret, in the surface's coordinates.
  CreateCaret(h, NULL, 2, 16);
  SetCaretPos(10, 20);
  RECT win, r;
  GetWindowRect(h, &win);
  POINT p = {10, 20};
  ClientToScreen(h, &p);
  CHECK(winport::ImeCursorRect(top, &r));
  CHECK(r.left == p.x - win.left && r.top == p.y - win.top && r.right == r.left + 2 && r.bottom == r.top + 16);
  // Or where the program puts the composition window, and there the
  // composition window goes.
  COMPOSITIONFORM f = {CFS_POINT, {40, 30}, {0, 0, 0, 0}};
  CHECK(ImmSetCompositionWindow(c, &f));
  COMPOSITIONFORM g = {};
  CHECK(ImmGetCompositionWindow(c, &g) && g.dwStyle == CFS_POINT && g.ptCurrentPos.x == 40);
  CHECK(winport::ImeCursorRect(top, &r));
  p = POINT{40, 30};
  ClientToScreen(h, &p);
  CHECK(r.left == p.x - win.left && r.top == p.y - win.top && r.bottom - r.top == 16);
  winport::ImePreedit(top, L"q", 1);
  Take();
  RECT cw;
  CHECK(Visible(ime) && GetWindowRect(ime, &cw) && cw.left < p.x && cw.left > p.x - 10 && cw.top < p.y);
  winport::ImePreedit(top, L"", 0);
  Take();
  // The candidate window's own place comes first.
  CANDIDATEFORM cf = {0, CFS_CANDIDATEPOS, {5, 6}, {0, 0, 0, 0}};
  CHECK(ImmSetCandidateWindow(c, &cf));
  CANDIDATEFORM cg = {};
  CHECK(ImmGetCandidateWindow(c, 0, &cg) && cg.ptCurrentPos.y == 6);
  CHECK(!ImmGetCandidateWindow(c, 1, &cg));
  CHECK(winport::ImeCursorRect(top, &r));
  p = POINT{5, 6};
  ClientToScreen(h, &p);
  CHECK(r.left == p.x - win.left && r.top == p.y - win.top);
  DestroyCaret();

  // The composition font.
  LOGFONTW lf = {};
  lf.lfHeight = -20;
  wcscpy(lf.lfFaceName, L"Courier New");
  CHECK(ImmSetCompositionFontW(c, &lf));
  LOGFONTA la = {};
  CHECK(ImmGetCompositionFontA(c, &la) && la.lfHeight == -20 && strcmp(la.lfFaceName, "Courier New") == 0);
  ImmReleaseContext(h, c);
}

static void NoContext(HWND h) {
  winport::Window* top = winport::GetWindow(h);
  HIMC before = ImmAssociateContext(h, NULL);
  CHECK(before != NULL);
  CHECK(ImmGetContext(h) == NULL);
  // Without an input context, the composition is not shown and the
  // finished text comes as plain characters.
  winport::ImePreedit(top, L"zz", 2);
  winport::ImeCommit(top, L"\x00E9t\x00E9");
  std::vector<Seen> s = Take();
  CHECK(Count(s, WM_IME_STARTCOMPOSITION) == 0 && Count(s, WM_IME_COMPOSITION) == 0);
  CHECK(Count(s, WM_IME_CHAR) == 0);
  CHECK(Chars(s) == L"\x00E9t\x00E9");
  CHECK(ImmAssociateContext(h, before) == NULL);
  CHECK(ImmGetContext(h) == before);

  // A context of the program's own, and closing the input method.
  HIMC own = ImmCreateContext();
  CHECK(own != NULL && own != before);
  ImmAssociateContext(h, own);
  CHECK(ImmGetContext(h) == own);
  CHECK(ImmGetOpenStatus(own));
  CHECK(ImmSetOpenStatus(own, FALSE) && !ImmGetOpenStatus(own));
  winport::ImeCommit(top, L"k");
  s = Take();
  CHECK(Count(s, WM_IME_COMPOSITION) == 0 && Chars(s) == L"k");
  CHECK(ImmSetOpenStatus(own, TRUE));
  CHECK(ImmDestroyContext(own));
  CHECK(!ImmDestroyContext(own));
  // The window is back on its thread's context.
  CHECK(ImmGetContext(h) == before);
  CHECK(!ImmDestroyContext(before));
}

static void Ansi() {
  WNDCLASSA wc = {};
  wc.lpfnWndProc = ProcA;
  wc.lpszClassName = "ImeTestA";
  wc.hInstance = GetModuleHandleA(NULL);
  RegisterClassA(&wc);
  HWND h = CreateWindowExA(0, "ImeTestA", "ANSI", WS_OVERLAPPEDWINDOW, 50, 50, 300, 200, NULL, NULL, NULL, NULL);
  ShowWindow(h, SW_SHOW);
  SetActiveWindow(h);
  SetFocus(h);
  Take();
  // An ANSI window gets the text in UTF-8, the ANSI code page here: two
  // WM_IME_CHARs, and two WM_CHARs, for the one character.
  winport::ImeCommit(winport::GetWindow(h), L"\x00E9");
  std::vector<Seen> s = Take();
  CHECK(Count(s, WM_IME_CHAR) == 2);
  std::wstring c = Chars(s);
  CHECK(c.size() == 2 && c[0] == 0xC3 && c[1] == 0xA9);
  char a[8] = {};
  HIMC ctx = ImmGetContext(h);
  CHECK(ImmGetCompositionStringA(ctx, GCS_RESULTSTR, a, sizeof a) == 2 && (BYTE)a[0] == 0xC3);
  // The cursor in bytes.
  winport::ImePreedit(winport::GetWindow(h), L"\x00E9\x00E9", 1);
  Take();
  CHECK(ImmGetCompositionStringA(ctx, GCS_CURSORPOS, NULL, 0) == 2);
  CHECK(ImmGetCompositionStringW(ctx, GCS_CURSORPOS, NULL, 0) == 1);
  winport::ImePreedit(winport::GetWindow(h), L"", 0);
  Take();
  ImmReleaseContext(h, ctx);
  DestroyWindow(h);
}

int main() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ProcW;
  wc.lpszClassName = L"ImeTest";
  wc.hInstance = GetModuleHandleW(NULL);
  RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, L"ImeTest", L"IME", WS_OVERLAPPEDWINDOW, 100, 100, 400, 300, NULL, NULL, NULL, NULL);
  ShowWindow(h, SW_SHOW);
  SetActiveWindow(h);
  SetFocus(h);
  Take();
  Composing(h);
  NoContext(h);
  DestroyWindow(h);
  Ansi();
  printf(g_failures ? "FAIL: %d failure(s)\n" : "PASS: %d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
