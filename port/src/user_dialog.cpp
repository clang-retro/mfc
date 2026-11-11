// user_dialog.cpp - dialog boxes: building them from templates, the
// dialog manager's keyboard handling, the dialog item helpers, and
// message boxes.
//
// A dialog is a window of the "#32770" class (or the class its template
// names) whose procedure is DefDlgProc. DefDlgProc calls the dialog
// procedure kept at DWLP_DLGPROC first and does the default work when
// that returns FALSE. Sizes in templates are dialog units, a quarter of
// the font's average character width across and an eighth of its height
// down.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <stdio.h>
#include <string.h>
#include <wctype.h>

#include <map>
#include <string>
#include <vector>

using namespace winport;

namespace {

// What the dialog manager keeps for a dialog.
struct DlgInfo {
  HFONT font = NULL;
  bool ownFont = false;
  SIZE units = {0, 0};  // the base units: average character width and height
  HWND focus = NULL;    // the control that had the focus when the dialog lost it
  UINT defId = IDOK;
  bool ended = false;
  INT_PTR result = 0;
  bool unicode = true;  // the dialog procedure's character set
};

std::map<HWND, DlgInfo*>& Infos() {
  static auto* m = new std::map<HWND, DlgInfo*>;
  return *m;
}

DlgInfo* InfoOf(HWND h, bool create) {
  UserGuard g(UserLock());
  auto it = Infos().find(h);
  if (it != Infos().end()) return it->second;
  if (!create) return NULL;
  DlgInfo* d = new DlgInfo;
  Infos()[h] = d;
  return d;
}

void FreeInfo(HWND h) {
  DlgInfo* d;
  {
    UserGuard g(UserLock());
    auto it = Infos().find(h);
    if (it == Infos().end()) return;
    d = it->second;
    Infos().erase(it);
  }
  if (d->ownFont && d->font) DeleteObject(d->font);
  delete d;
}

// The average width and height of a font's characters, as Windows works
// out dialog base units.
SIZE UnitsOf(HFONT font) {
  HDC dc = CreateCompatibleDC(NULL);
  HGDIOBJ old = SelectObject(dc, font ? (HGDIOBJ)font : GetStockObject(SYSTEM_FONT));
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SIZE s;
  static const wchar_t kAlphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  GetTextExtentPoint32W(dc, kAlphabet, 52, &s);
  SelectObject(dc, old);
  DeleteDC(dc);
  return SIZE{(s.cx / 26 + 1) / 2, tm.tmHeight};
}

SIZE SystemUnits() {
  static SIZE s = UnitsOf(NULL);
  return s;
}

// ---------------------------------------------------------------------
// Templates

struct ItemTemplate {
  DWORD style = 0, exStyle = 0, helpId = 0;
  short x = 0, y = 0, cx = 0, cy = 0;
  DWORD id = 0;
  std::wstring cls;  // a name; ordinals become the system class names
  std::wstring text;  // an ordinal title is 0xFFFF and the number
  const void* data = NULL;  // the creation data, its size WORD first
};

struct DlgTemplate {
  DWORD style = 0, exStyle = 0, helpId = 0;
  short x = 0, y = 0, cx = 0, cy = 0;
  std::wstring menu, cls, title;
  WORD menuId = 0, clsId = 0;
  bool hasFont = false;
  WORD pointSize = 0, weight = FW_NORMAL;
  BYTE italic = 0, charset = DEFAULT_CHARSET;
  std::wstring face;
  std::vector<ItemTemplate> items;
};

const WORD* ReadSzOrOrd(const WORD* p, std::wstring* s, WORD* ord) {
  s->clear();
  if (ord) *ord = 0;
  if (*p == 0xFFFF) {
    if (ord) *ord = p[1];
    return p + 2;
  }
  while (*p) s->push_back((wchar_t)*p++);
  return p + 1;
}

const WORD* Align4(const void* base, const WORD* p) {
  ptrdiff_t off = (const BYTE*)p - (const BYTE*)base;
  return (const WORD*)((const BYTE*)base + ((off + 3) & ~(ptrdiff_t)3));
}

std::wstring ClassFromOrdinal(WORD ord) {
  switch (ord) {
    case 0x80: return L"Button";
    case 0x81: return L"Edit";
    case 0x82: return L"Static";
    case 0x83: return L"ListBox";
    case 0x84: return L"ScrollBar";
    case 0x85: return L"ComboBox";
  }
  return L"";
}

template <class T>
T Get(const WORD*& p) {
  T v;
  memcpy(&v, p, sizeof v);
  p = (const WORD*)((const BYTE*)p + sizeof v);
  return v;
}

DlgTemplate Parse(const void* tmpl) {
  DlgTemplate t;
  const WORD* p = (const WORD*)tmpl;
  bool ex = p[0] == 1 && p[1] == 0xFFFF;
  WORD count;
  if (ex) {
    p += 2;
    t.helpId = Get<DWORD>(p);
    t.exStyle = Get<DWORD>(p);
    t.style = Get<DWORD>(p);
  } else {
    t.style = Get<DWORD>(p);
    t.exStyle = Get<DWORD>(p);
  }
  count = Get<WORD>(p);
  t.x = Get<short>(p);
  t.y = Get<short>(p);
  t.cx = Get<short>(p);
  t.cy = Get<short>(p);
  p = ReadSzOrOrd(p, &t.menu, &t.menuId);
  p = ReadSzOrOrd(p, &t.cls, &t.clsId);
  p = ReadSzOrOrd(p, &t.title, NULL);
  if (t.style & (DS_SETFONT | DS_SHELLFONT)) {
    t.hasFont = true;
    t.pointSize = Get<WORD>(p);
    if (ex) {
      t.weight = Get<WORD>(p);
      t.italic = Get<BYTE>(p);
      t.charset = Get<BYTE>(p);
    }
    p = ReadSzOrOrd(p, &t.face, NULL);
  }
  for (WORD i = 0; i < count; i++) {
    p = Align4(tmpl, p);
    ItemTemplate it;
    if (ex) {
      it.helpId = Get<DWORD>(p);
      it.exStyle = Get<DWORD>(p);
      it.style = Get<DWORD>(p);
    } else {
      it.style = Get<DWORD>(p);
      it.exStyle = Get<DWORD>(p);
    }
    it.x = Get<short>(p);
    it.y = Get<short>(p);
    it.cx = Get<short>(p);
    it.cy = Get<short>(p);
    it.id = ex ? Get<DWORD>(p) : Get<WORD>(p);
    WORD ord;
    p = ReadSzOrOrd(p, &it.cls, &ord);
    if (ord) it.cls = ClassFromOrdinal(ord);
    p = ReadSzOrOrd(p, &it.text, &ord);
    if (ord) {
      it.text = std::wstring(1, (wchar_t)0xFFFF);
      it.text.push_back((wchar_t)ord);
    }
    WORD extra = Get<WORD>(p);
    if (extra) {
      it.data = (const BYTE*)p - 2;
      p = (const WORD*)((const BYTE*)p + extra);
    }
    t.items.push_back(it);
  }
  return t;
}

// ---------------------------------------------------------------------
// Navigation

// By the control's own style, so that navigation works before the dialog
// is shown, as in WM_INITDIALOG.
bool CanFocus(HWND h) { return (GetWindowLongW(h, GWL_STYLE) & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE; }

// The dialog's controls in tab order: its children, and the children of
// WS_EX_CONTROLPARENT children in their place.
void Controls(HWND parent, std::vector<HWND>* out, int depth = 0) {
  for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
    if ((GetWindowLongW(c, GWL_EXSTYLE) & WS_EX_CONTROLPARENT) && depth < 8 && (GetWindowLongW(c, GWL_STYLE) & WS_VISIBLE) &&
        IsWindowEnabled(c)) {
      Controls(c, out, depth + 1);
      continue;
    }
    out->push_back(c);
  }
}

LRESULT DlgCode(HWND h, MSG* m) { return h ? SendMessageW(h, WM_GETDLGCODE, m ? m->wParam : 0, (LPARAM)m) : 0; }

void FocusControl(HWND dlg, HWND c) {
  if (!c) return;
  // An edit control taking the focus from the keyboard has its text selected.
  if (DlgCode(c, NULL) & DLGC_HASSETSEL) SendMessageW(c, EM_SETSEL, 0, -1);
  SetFocus(c);
  (void)dlg;
}

// The push button to press for Enter.
HWND DefaultButton(HWND dlg) {
  LRESULT r = SendMessageW(dlg, DM_GETDEFID, 0, 0);
  if (HIWORD(r) != DC_HASDEFID) return NULL;
  return GetDlgItem(dlg, LOWORD(r));
}

// Sets which button looks like the default: the focused push button, or
// the dialog's default when the focus is elsewhere.
void UpdateDefault(HWND dlg, HWND focus) {
  HWND def = DefaultButton(dlg);
  std::vector<HWND> all;
  Controls(dlg, &all);
  bool focusIsButton = focus && (DlgCode(focus, NULL) & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON));
  for (HWND c : all) {
    LRESULT code = DlgCode(c, NULL);
    if (!(code & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON))) continue;
    bool want = focusIsButton ? c == focus : c == def;
    bool is = (code & DLGC_DEFPUSHBUTTON) != 0;
    if (want != is) SendMessageW(c, BM_SETSTYLE, want ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
  }
}

// A control's mnemonic, from its text.
wchar_t MnemonicOf(HWND h) {
  if (!h) return 0;
  wchar_t text[256];
  if (!GetWindowTextW(h, text, 256)) return 0;
  for (wchar_t* p = text; p[0] && p[1]; p++)
    if (*p == L'&') {
      if (p[1] != L'&') return (wchar_t)towupper(p[1]);
      p++;
    }
  return 0;
}

bool IsStaticLabel(HWND h) {
  if (!(DlgCode(h, NULL) & DLGC_STATIC)) return false;
  return !(GetWindowLongW(h, GWL_STYLE) & SS_NOPREFIX);
}

// A mnemonic key: the control it names gets the focus, and a button is
// clicked; a label passes the focus to the next control that takes it.
bool Mnemonic(HWND dlg, HWND from, wchar_t ch) {
  wchar_t up = (wchar_t)towupper(ch);
  std::vector<HWND> all;
  Controls(dlg, &all);
  if (all.empty()) return false;
  size_t start = 0;
  for (size_t i = 0; i < all.size(); i++)
    if (all[i] == from) start = i + 1;
  for (size_t k = 0; k < all.size(); k++) {
    HWND c = all[(start + k) % all.size()];
    if (!IsWindowVisible(c) || MnemonicOf(c) != up) continue;
    if (IsStaticLabel(c)) {
      // The label's control is the next one that can take the focus.
      size_t at = (start + k) % all.size();
      for (size_t j = 1; j < all.size(); j++) {
        HWND n = all[(at + j) % all.size()];
        if (CanFocus(n) && !(DlgCode(n, NULL) & DLGC_STATIC)) {
          FocusControl(dlg, n);
          return true;
        }
      }
      return true;
    }
    if (!IsWindowEnabled(c)) continue;
    LRESULT code = DlgCode(c, NULL);
    if (code & DLGC_BUTTON) {
      SetFocus(c);
      SendMessageW(c, BM_CLICK, 0, 0);
    } else if (code & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) {
      SendMessageW(c, BM_CLICK, 0, 0);
    } else {
      FocusControl(dlg, c);
    }
    return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------
// DefDlgProc

static INT_PTR CallDlgProc(HWND h, DLGPROC proc, bool unicode, UINT msg, WPARAM wp, LPARAM lp) {
  (void)unicode;
  return proc(h, msg, wp, lp);
}

// Messages whose dialog procedure result is the answer itself.
static bool DirectResult(UINT msg) {
  switch (msg) {
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    case WM_COMPAREITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_QUERYDRAGICON:
    case WM_INITDIALOG:
      return true;
  }
  return false;
}

static LRESULT DefDlgCore(HWND h, UINT msg, WPARAM wp, LPARAM lp, bool unicode) {
  DlgInfo* d = InfoOf(h, msg != WM_NCDESTROY);
  DLGPROC proc = (DLGPROC)GetWindowLongPtrW(h, DWLP_DLGPROC);
  if (proc && d) {
    SetWindowLongPtrW(h, DWLP_MSGRESULT, 0);
    INT_PTR r = CallDlgProc(h, proc, d->unicode, msg, wp, lp);
    if (!IsWindow(h)) return 0;
    if (DirectResult(msg)) {
      if (r || msg == WM_INITDIALOG) return r;
    } else if (r) {
      return GetWindowLongPtrW(h, DWLP_MSGRESULT);
    }
  }
  d = InfoOf(h, false);
  switch (msg) {
    case WM_ERASEBKGND: {
      HDC dc = (HDC)wp;
      HBRUSH br = (HBRUSH)SendMessageW(h, WM_CTLCOLORDLG, wp, (LPARAM)h);
      if (!br) br = GetSysColorBrush(COLOR_BTNFACE);
      RECT r;
      GetClientRect(h, &r);
      FillRect(dc, &r, br);
      return 1;
    }
    case WM_NCDESTROY:
      FreeInfo(h);
      break;
    case WM_SHOWWINDOW:
      if (!wp && d) {
        HWND f = GetFocus();
        if (f && IsChild(h, f)) d->focus = f;
      }
      break;
    case WM_ACTIVATE:
      if (!d) break;
      if (LOWORD(wp) != WA_INACTIVE) {
        if (d->focus && IsWindow(d->focus) && IsChild(h, d->focus))
          SetFocus(d->focus);
        else if (HWND first = GetNextDlgTabItem(h, NULL, FALSE))
          FocusControl(h, first);
      } else {
        HWND f = GetFocus();
        if (f && IsChild(h, f)) d->focus = f;
      }
      return 0;
    case WM_SETFOCUS:
      if (d && d->focus && IsWindow(d->focus) && IsChild(h, d->focus)) {
        SetFocus(d->focus);
      } else if (HWND first = GetNextDlgTabItem(h, NULL, FALSE)) {
        FocusControl(h, first);
      }
      return 0;
    case WM_CLOSE: {
      HWND cancel = GetDlgItem(h, IDCANCEL);
      if (cancel && !IsWindowEnabled(cancel)) {
        MessageBeep(0);
        return 0;
      }
      PostMessageW(h, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
      return 0;
    }
    case DM_GETDEFID: {
      if (!d) return 0;
      if (d->defId && GetDlgItem(h, d->defId)) return MAKELRESULT(d->defId, DC_HASDEFID);
      // A button with the default style.
      for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (DlgCode(c, NULL) & DLGC_DEFPUSHBUTTON) return MAKELRESULT(GetDlgCtrlID(c), DC_HASDEFID);
      return 0;
    }
    case DM_SETDEFID:
      if (d) {
        d->defId = (UINT)wp;
        UpdateDefault(h, GetFocus());
      }
      return TRUE;
    case DM_REPOSITION: {
      RECT r;
      GetWindowRect(h, &r);
      int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
      int x = r.left, y = r.top;
      if (r.right > sw) x = sw - (r.right - r.left);
      if (r.bottom > sh) y = sh - (r.bottom - r.top);
      if (x < 0) x = 0;
      if (y < 0) y = 0;
      SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_NEXTDLGCTL: {
      HWND target;
      if (LOWORD(lp))
        target = (HWND)wp;
      else
        target = GetNextDlgTabItem(h, GetFocus(), wp != 0);
      if (target) FocusControl(h, target);
      UpdateDefault(h, target);
      return 0;
    }
    case WM_GETFONT:
      return d ? (LRESULT)d->font : 0;
    case WM_SETFONT:
      if (d) {
        if (d->ownFont && d->font && d->font != (HFONT)wp) DeleteObject(d->font);
        d->font = (HFONT)wp;
        d->ownFont = false;
        d->units = UnitsOf(d->font);
      }
      return 0;
    case WM_CHANGEUISTATE:
    case WM_UPDATEUISTATE:
      return 0;
  }
  return unicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
}

namespace winport {

void RegisterDialogClass() {
  static bool done = false;
  if (done) return;
  done = true;
  RegisterSystemClass(L"#32770", DefDlgProcW, CS_SAVEBITS | CS_DBLCLKS, DLGWINDOWEXTRA, NULL,
                      MAKEINTRESOURCEW(32512));
}

}  // namespace winport

// ---------------------------------------------------------------------
// Creating dialogs

static HWND CreateDialogCore(HINSTANCE inst, const void* tmpl, HWND owner, DLGPROC proc, LPARAM param, bool unicode,
                             bool modal) {
  if (!tmpl) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  RegisterSystemClasses();
  DlgTemplate t = Parse(tmpl);
  // The font, and the units it makes.
  HFONT font = NULL;
  if (t.hasFont) {
    if (t.pointSize == 0x7FFF) {
      font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    } else {
      std::wstring face = t.face;
      if ((t.style & DS_SHELLFONT) == DS_SHELLFONT || face == L"MS Shell Dlg") face = L"MS Shell Dlg";
      int height = -MulDiv(t.pointSize, kLogPixels, 72);
      font = CreateFontW(height, 0, 0, 0, t.weight, t.italic, 0, 0, t.charset, 0, 0, 0, 0, face.c_str());
    }
  }
  SIZE units = font ? UnitsOf(font) : SystemUnits();
  auto mapX = [&](int v) { return MulDiv(v, units.cx, 4); };
  auto mapY = [&](int v) { return MulDiv(v, units.cy, 8); };

  DWORD style = t.style, exStyle = t.exStyle;
  if (style & DS_MODALFRAME) exStyle |= WS_EX_DLGMODALFRAME;
  if (style & DS_CONTROL) {
    style &= ~(WS_CAPTION | WS_SYSMENU);
    style |= WS_CHILD;
    exStyle |= WS_EX_CONTROLPARENT;
  }
  bool child = (style & WS_CHILD) != 0;
  if (!child && owner) owner = GetAncestor(owner, GA_ROOT);

  HMENU menu = NULL;
  if (!child && (t.menuId || !t.menu.empty()))
    menu = LoadMenuW(inst, t.menuId ? MAKEINTRESOURCEW(t.menuId) : t.menu.c_str());

  RECT r = MakeRect(0, 0, mapX(t.cx), mapY(t.cy));
  AdjustWindowRectEx(&r, style, menu != NULL, exStyle);
  int cx = r.right - r.left, cy = r.bottom - r.top;
  int x = (short)t.x == (short)0x8000 ? (int)CW_USEDEFAULT : mapX(t.x) + r.left;
  int y = (short)t.x == (short)0x8000 ? (int)CW_USEDEFAULT : mapY(t.y) + r.top;
  if (x != (int)CW_USEDEFAULT) {
    if (child) {
      x = mapX(t.x);
      y = mapY(t.y);
    } else if (style & DS_CENTER) {
      RECT area = MakeRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
      if (owner && IsWindowVisible(owner) && !IsIconic(owner)) GetWindowRect(owner, &area);
      x = area.left + (area.right - area.left - cx) / 2;
      y = area.top + (area.bottom - area.top - cy) / 2;
    } else if (owner && !(style & DS_ABSALIGN)) {
      POINT p = {x, y};
      ClientToScreen(owner, &p);
      x = p.x;
      y = p.y;
    }
    if (!child) {
      int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
      if (x + cx > sw) x = sw - cx;
      if (y + cy > sh) y = sh - cy;
      if (x < 0) x = 0;
      if (y < 0) y = 0;
    }
  }

  std::wstring cls = t.clsId ? L"" : t.cls;
  LPCWSTR className = t.clsId ? MAKEINTRESOURCEW(t.clsId) : cls.empty() ? L"#32770" : cls.c_str();
  bool visible = (style & WS_VISIBLE) != 0;
  HWND dlg = CreateWindowExW(exStyle, className, t.title.c_str(), style & ~WS_VISIBLE, x, y, cx, cy, owner, menu,
                             inst, (LPVOID)param);
  if (!dlg) {
    if (font) DeleteObject(font);
    if (menu) DestroyMenu(menu);
    return NULL;
  }
  DlgInfo* d = InfoOf(dlg, true);
  d->unicode = unicode;
  d->units = units;
  d->font = font;
  d->ownFont = font && t.pointSize != 0x7FFF;
  SetWindowLongPtrW(dlg, DWLP_DLGPROC, (LONG_PTR)proc);
  if (font) {
    // WM_SETFONT through the dialog procedure; the manager keeps owning the font.
    SendMessageW(dlg, WM_SETFONT, (WPARAM)font, 0);
    if ((d = InfoOf(dlg, false))) {
      d->font = font;
      d->ownFont = t.pointSize != 0x7FFF;
      d->units = units;
    }
  }
  if (!IsWindow(dlg)) return NULL;

  // The controls.
  UINT defId = 0;
  for (const ItemTemplate& it : t.items) {
    DWORD cs = it.style | WS_CHILD;
    DWORD ces = it.exStyle | WS_EX_NOPARENTNOTIFY;
    // Dialogs draw their controls' borders in 3-D.
    if ((cs & WS_BORDER) && !(ces & WS_EX_STATICEDGE)) {
      cs &= ~WS_BORDER;
      ces |= WS_EX_CLIENTEDGE;
    }
    HWND c = CreateWindowExW(ces, it.cls.c_str(), it.text.c_str(), cs, mapX(it.x),
                             mapY(it.y), mapX(it.cx), mapY(it.cy), dlg, (HMENU)(UINT_PTR)it.id, inst,
                             (LPVOID)it.data);
    if (!c) {
      if (!(t.style & DS_NOFAILCREATE)) {
        DestroyWindow(dlg);
        return NULL;
      }
      continue;
    }
    if (!unicode)
      if (Window* cw = winport::GetWindow(c)) cw->ansiCreate = true;
    if (font) SendMessageW(c, WM_SETFONT, (WPARAM)font, FALSE);
    if (SendMessageW(c, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) defId = (UINT)it.id;
    if (!IsWindow(dlg)) return NULL;
  }
  if ((d = InfoOf(dlg, false)) && defId) d->defId = defId;

  HWND first = GetNextDlgTabItem(dlg, NULL, FALSE);
  if (SendMessageW(dlg, WM_INITDIALOG, (WPARAM)first, param) && IsWindow(dlg)) {
    first = GetNextDlgTabItem(dlg, NULL, FALSE);
    if (first) FocusControl(dlg, first);
  }
  if (!IsWindow(dlg)) return NULL;
  if (visible && !modal) {
    ShowWindow(dlg, SW_SHOWNORMAL);
    UpdateWindow(dlg);
  }
  return dlg;
}

static INT_PTR DialogBoxCore(HINSTANCE inst, const void* tmpl, HWND parent, DLGPROC proc, LPARAM param,
                             bool unicode) {
  HWND owner = parent ? GetAncestor(parent, GA_ROOT) : NULL;
  HWND dlg = CreateDialogCore(inst, tmpl, owner, proc, param, unicode, true);
  if (!dlg) return -1;
  bool disabled = false;
  if (owner && IsWindowEnabled(owner)) {
    EnableWindow(owner, FALSE);
    disabled = true;
  }
  DlgInfo* d = InfoOf(dlg, false);
  if (d && !d->ended) {
    ShowWindow(dlg, SW_SHOWNORMAL);
    UpdateWindow(dlg);
  }
  bool idleSent = false;
  MSG m;
  bool noIdle = (GetWindowLongW(dlg, GWL_STYLE) & DS_NOIDLEMSG) != 0;
  while (IsWindow(dlg) && (d = InfoOf(dlg, false)) && !d->ended) {
    if (!idleSent && !PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE)) {
      idleSent = true;
      if (owner && !noIdle) SendMessageW(owner, WM_ENTERIDLE, MSGF_DIALOGBOX, (LPARAM)dlg);
      continue;
    }
    if (!GetMessageW(&m, NULL, 0, 0)) {
      PostQuitMessage((int)m.wParam);
      break;
    }
    idleSent = false;
    if (!IsDialogMessageW(dlg, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  INT_PTR result = (d = InfoOf(dlg, false)) ? d->result : -1;
  if (disabled && IsWindow(owner)) EnableWindow(owner, TRUE);
  if (IsWindow(dlg)) DestroyWindow(dlg);
  if (owner && IsWindow(owner) && GetActiveWindow() == NULL) SetActiveWindow(owner);
  return result;
}

static const void* FindTemplate(HINSTANCE inst, LPCWSTR name) {
  HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_DIALOG);
  if (!r) return NULL;
  return LockResource(LoadResource(inst, r));
}

static LPCWSTR NameW(LPCSTR name, std::wstring& buf) {
  if (IS_INTRESOURCE(name)) return (LPCWSTR)name;
  buf = Widen(name);
  return buf.c_str();
}

// ---------------------------------------------------------------------
// Message boxes

namespace {

struct BoxParams {
  std::wstring text, caption;
  UINT type;
  HICON icon;
};

const int kMsgText = 0xFFFF;

INT_PTR CALLBACK BoxProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_INITDIALOG: {
      BoxParams* p = (BoxParams*)lp;
      SetWindowTextW(h, p->caption.c_str());
      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      NONCLIENTMETRICSW ncm = {sizeof ncm};
      static HFONT msgFont;
      if (!msgFont && SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
        msgFont = CreateFontIndirectW(&ncm.lfMessageFont);
      if (msgFont) font = msgFont;
      // The buttons the type asks for.
      static const int kinds[][3] = {
          {IDOK, 0, 0},       {IDOK, IDCANCEL, 0},    {IDABORT, IDRETRY, IDIGNORE}, {IDYES, IDNO, IDCANCEL},
          {IDYES, IDNO, 0},   {IDRETRY, IDCANCEL, 0}, {IDCANCEL, IDTRYAGAIN, IDCONTINUE},
      };
      UINT kind = p->type & MB_TYPEMASK;
      if (kind > 6) kind = 0;
      std::vector<int> ids;
      for (int id : kinds[kind])
        if (id) ids.push_back(id);
      if (p->type & MB_HELP) ids.push_back(IDHELP);
      // Measure the text.
      HDC dc = GetDC(h);
      HGDIOBJ old = SelectObject(dc, font);
      RECT tr = {0, 0, GetSystemMetrics(SM_CXSCREEN) * 3 / 5, 0};
      DrawTextW(dc, p->text.c_str(), -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
      TEXTMETRICW tm;
      GetTextMetricsW(dc, &tm);
      SelectObject(dc, old);
      ReleaseDC(h, dc);
      int margin = 12, iconSize = p->icon ? GetSystemMetrics(SM_CXICON) : 0;
      int bw = 75, bh = 23, gap = 6;
      int textX = margin + (iconSize ? iconSize + margin : 0);
      int contentW = textX + (tr.right - tr.left) + margin;
      int buttonsW = (int)ids.size() * bw + ((int)ids.size() - 1) * gap + 2 * margin;
      int cw = contentW > buttonsW ? contentW : buttonsW;
      int textH = tr.bottom - tr.top;
      int top = iconSize > textH ? iconSize : textH;
      int ch = margin + top + margin + bh + margin;
      if (p->icon) {
        HWND ic = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE | SS_ICON, margin, margin, iconSize,
                                  iconSize, h, (HMENU)(UINT_PTR)0xFFFE, NULL, NULL);
        SendMessageW(ic, STM_SETICON, (WPARAM)p->icon, 0);
      }
      HWND text = CreateWindowExW(0, L"Static", p->text.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                  textX, margin + (top - textH) / 2, tr.right - tr.left + 2, textH, h,
                                  (HMENU)(UINT_PTR)kMsgText, NULL, NULL);
      SendMessageW(text, WM_SETFONT, (WPARAM)font, FALSE);
      int bx = (cw - ((int)ids.size() * bw + ((int)ids.size() - 1) * gap)) / 2;
      UINT def = (p->type & MB_DEFMASK) >> 8;
      HWND focus = NULL;
      for (size_t i = 0; i < ids.size(); i++) {
        static const wchar_t* names[] = {L"", L"OK", L"Cancel", L"&Abort", L"&Retry", L"&Ignore", L"&Yes", L"&No",
                                         L"&Close", L"Help", L"&Try Again", L"&Continue"};
        int id = ids[i];
        DWORD bs = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        if (i == 0) bs |= WS_GROUP;
        HWND b = CreateWindowExW(0, L"Button", names[id < 12 ? id : 0], bs, bx + (int)i * (bw + gap),
                                 margin + top + margin, bw, bh, h, (HMENU)(UINT_PTR)id, NULL, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, FALSE);
        if (i == def || (!focus && i == 0)) focus = b;
        if (i == def) SendMessageW(h, DM_SETDEFID, id, 0);
      }
      // An OK-only box and boxes with Cancel close with Escape.
      if (!GetDlgItem(h, IDCANCEL) && ids.size() == 1)
        SetWindowLongPtrW(GetDlgItem(h, ids[0]), GWLP_ID, ids[0]);
      RECT wr = {0, 0, cw, ch};
      AdjustWindowRectEx(&wr, GetWindowLongW(h, GWL_STYLE), FALSE, GetWindowLongW(h, GWL_EXSTYLE));
      int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
      HWND owner = GetWindow(h, GW_OWNER);
      RECT area = MakeRect(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
      if (owner && IsWindowVisible(owner) && !IsIconic(owner)) GetWindowRect(owner, &area);
      SetWindowPos(h, (p->type & (MB_TOPMOST | MB_SYSTEMMODAL)) ? HWND_TOPMOST : NULL,
                   area.left + (area.right - area.left - ww) / 2, area.top + (area.bottom - area.top - wh) / 2, ww, wh,
                   SWP_NOACTIVATE | ((p->type & (MB_TOPMOST | MB_SYSTEMMODAL)) ? 0 : SWP_NOZORDER));
      SendMessageW(h, DM_REPOSITION, 0, 0);
      if (focus) SetFocus(focus);
      return FALSE;
    }
    case WM_COMMAND:
      if (HIWORD(wp) == BN_CLICKED || lp == 0) {
        int id = LOWORD(wp);
        if (id == IDCANCEL && !GetDlgItem(h, IDCANCEL)) {
          // Escape: a box without Cancel ends with its only button.
          HWND only = GetDlgItem(h, IDOK);
          if (!only) return TRUE;
          id = IDOK;
        }
        if (id == IDHELP) {
          HELPINFO hi = {sizeof hi};
          hi.iContextType = HELPINFO_WINDOW;
          hi.hItemHandle = h;
          SendMessageW(GetWindow(h, GW_OWNER), WM_HELP, 0, (LPARAM)&hi);
          return TRUE;
        }
        EndDialog(h, id);
      }
      return TRUE;
  }
  return FALSE;
}

// The answer a box gives without asking: the default button.
int DefaultAnswer(UINT type) {
  UINT kind = type & MB_TYPEMASK;
  UINT def = (type & MB_DEFMASK) >> 8;
  static const int answers[][3] = {
      {IDOK, IDOK, IDOK},           {IDOK, IDCANCEL, IDCANCEL},   {IDABORT, IDRETRY, IDIGNORE},
      {IDYES, IDNO, IDCANCEL},      {IDYES, IDNO, IDNO},          {IDRETRY, IDCANCEL, IDCANCEL},
      {IDCANCEL, IDTRYAGAIN, IDCONTINUE},
  };
  if (kind > 6) kind = 0;
  return answers[kind][def < 3 ? def : 0];
}

int MessageBoxCore(HWND owner, const std::wstring& text, const std::wstring& caption, UINT type, HICON custom) {
  // With nothing to show windows on, the text goes to standard error and
  // the default button is the answer, so that programs run unattended.
  if (strcmp(GetBackend()->Name(), "headless") == 0) {
    fprintf(stderr, "%s%s%s\n", Narrow(caption.c_str()).c_str(), caption.empty() ? "" : ": ",
            Narrow(text.c_str()).c_str());
    return DefaultAnswer(type);
  }
  BoxParams p;
  p.text = text;
  p.caption = caption.empty() ? L"Error" : caption;
  p.type = type;
  p.icon = custom;
  if (!p.icon) {
    switch (type & MB_ICONMASK) {
      case MB_ICONHAND: p.icon = LoadIconW(NULL, (LPCWSTR)IDI_HAND); break;
      case MB_ICONQUESTION: p.icon = LoadIconW(NULL, (LPCWSTR)IDI_QUESTION); break;
      case MB_ICONEXCLAMATION: p.icon = LoadIconW(NULL, (LPCWSTR)IDI_EXCLAMATION); break;
      case MB_ICONASTERISK: p.icon = LoadIconW(NULL, (LPCWSTR)IDI_ASTERISK); break;
    }
  }
  // An empty template: the box procedure makes the controls.
  struct {
    DLGTEMPLATE t;
    WORD menu, cls, title;
  } tmpl = {};
  tmpl.t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
  if (type & (MB_TOPMOST | MB_SYSTEMMODAL)) tmpl.t.dwExtendedStyle = WS_EX_TOPMOST;
  if (!owner && !(type & MB_TASKMODAL)) owner = GetActiveWindow();
  if (type & MB_TASKMODAL) owner = NULL;
  MessageBeep(type & MB_ICONMASK);
  INT_PTR r = DialogBoxIndirectParamW(NULL, &tmpl.t, owner, BoxProc, (LPARAM)&p);
  return r <= 0 ? DefaultAnswer(type) : (int)r;
}

}  // namespace

// ---------------------------------------------------------------------
// The API

extern "C" {

LRESULT WINAPI DefDlgProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefDlgCore(h, msg, wp, lp, true); }
LRESULT WINAPI DefDlgProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return DefDlgCore(h, msg, wp, lp, false); }

HWND WINAPI CreateDialogIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATEW tmpl, HWND owner, DLGPROC proc, LPARAM p) {
  return CreateDialogCore(inst, tmpl, owner, proc, p, true, false);
}

HWND WINAPI CreateDialogIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl, HWND owner, DLGPROC proc, LPARAM p) {
  return CreateDialogCore(inst, tmpl, owner, proc, p, false, false);
}

HWND WINAPI CreateDialogParamW(HINSTANCE inst, LPCWSTR name, HWND owner, DLGPROC proc, LPARAM p) {
  const void* t = FindTemplate(inst, name);
  return t ? CreateDialogCore(inst, t, owner, proc, p, true, false) : NULL;
}

HWND WINAPI CreateDialogParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc, LPARAM p) {
  std::wstring buf;
  const void* t = FindTemplate(inst, NameW(name, buf));
  return t ? CreateDialogCore(inst, t, owner, proc, p, false, false) : NULL;
}

INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE inst, LPCDLGTEMPLATEW tmpl, HWND owner, DLGPROC proc, LPARAM p) {
  return DialogBoxCore(inst, tmpl, owner, proc, p, true);
}

INT_PTR WINAPI DialogBoxIndirectParamA(HINSTANCE inst, LPCDLGTEMPLATEA tmpl, HWND owner, DLGPROC proc, LPARAM p) {
  return DialogBoxCore(inst, tmpl, owner, proc, p, false);
}

INT_PTR WINAPI DialogBoxParamW(HINSTANCE inst, LPCWSTR name, HWND owner, DLGPROC proc, LPARAM p) {
  const void* t = FindTemplate(inst, name);
  if (!t) return -1;
  return DialogBoxCore(inst, t, owner, proc, p, true);
}

INT_PTR WINAPI DialogBoxParamA(HINSTANCE inst, LPCSTR name, HWND owner, DLGPROC proc, LPARAM p) {
  std::wstring buf;
  const void* t = FindTemplate(inst, NameW(name, buf));
  if (!t) return -1;
  return DialogBoxCore(inst, t, owner, proc, p, false);
}

BOOL WINAPI EndDialog(HWND h, INT_PTR result) {
  if (!IsWindow(h)) {
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  DlgInfo* d = InfoOf(h, true);
  d->ended = true;
  d->result = result;
  // The owner comes back before the dialog goes, so that activation
  // returns to it.
  HWND owner = GetWindow(h, GW_OWNER);
  if (owner && !IsWindowEnabled(owner)) EnableWindow(owner, TRUE);
  SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (owner && GetActiveWindow() == h) SetActiveWindow(owner);
  // Wake the modal loop.
  PostMessageW(h, WM_NULL, 0, 0);
  return TRUE;
}

LONG WINAPI GetDialogBaseUnits(void) {
  SIZE s = SystemUnits();
  return MAKELONG(s.cx, s.cy);
}

BOOL WINAPI MapDialogRect(HWND h, LPRECT r) {
  if (!r) return FALSE;
  DlgInfo* d = InfoOf(h, false);
  SIZE u = d && d->units.cx ? d->units : SystemUnits();
  r->left = MulDiv(r->left, u.cx, 4);
  r->right = MulDiv(r->right, u.cx, 4);
  r->top = MulDiv(r->top, u.cy, 8);
  r->bottom = MulDiv(r->bottom, u.cy, 8);
  return TRUE;
}

HWND WINAPI GetNextDlgTabItem(HWND dlg, HWND ctl, BOOL previous) {
  std::vector<HWND> all;
  Controls(dlg, &all);
  if (all.empty()) return NULL;
  int n = (int)all.size(), at = -1;
  for (int i = 0; i < n; i++)
    if (all[i] == ctl) at = i;
  for (int k = 1; k <= n; k++) {
    int i = at < 0 ? (previous ? n - k : k - 1) : ((at + (previous ? -k : k)) % n + n) % n;
    HWND c = all[i];
    if (CanFocus(c) && (GetWindowLongW(c, GWL_STYLE) & WS_TABSTOP)) return c;
  }
  return ctl && CanFocus(ctl) ? ctl : NULL;
}

HWND WINAPI GetNextDlgGroupItem(HWND dlg, HWND ctl, BOOL previous) {
  std::vector<HWND> all;
  Controls(dlg, &all);
  int n = (int)all.size(), at = -1;
  for (int i = 0; i < n; i++)
    if (all[i] == ctl) at = i;
  if (at < 0) return n ? all[0] : NULL;
  // The group: back to the control with WS_GROUP, on to the next one.
  int start = at;
  while (start > 0 && !(GetWindowLongW(all[start], GWL_STYLE) & WS_GROUP)) start--;
  int end = at + 1;
  while (end < n && !(GetWindowLongW(all[end], GWL_STYLE) & WS_GROUP)) end++;
  int size = end - start;
  for (int k = 1; k < size; k++) {
    int i = start + (((at - start) + (previous ? -k : k)) % size + size) % size;
    if (CanFocus(all[i])) return all[i];
  }
  return ctl;
}

BOOL WINAPI IsDialogMessageW(HWND dlg, LPMSG m) {
  if (!m || !dlg || !IsWindow(dlg)) return FALSE;
  if (m->hwnd != dlg && !IsChild(dlg, m->hwnd)) return FALSE;
  HWND focus = GetFocus();
  if (!focus || (focus != dlg && !IsChild(dlg, focus))) focus = m->hwnd;
  LRESULT code = DlgCode(m->hwnd, m);
  if (code & (DLGC_WANTMESSAGE | DLGC_WANTALLKEYS)) {
    if (m->message == WM_KEYDOWN || m->message == WM_CHAR) {
      TranslateMessage(m);
      DispatchMessageW(m);
      return TRUE;
    }
  }
  switch (m->message) {
    case WM_KEYDOWN:
      switch (m->wParam) {
        case VK_TAB:
          if (code & DLGC_WANTTAB) break;
          {
            bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            SendMessageW(dlg, WM_NEXTDLGCTL, back, 0);
          }
          return TRUE;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
          if (code & DLGC_WANTARROWS) break;
          {
            bool back = m->wParam == VK_LEFT || m->wParam == VK_UP;
            HWND next = GetNextDlgGroupItem(dlg, m->hwnd, back);
            if (next && next != m->hwnd) {
              LRESULT nc = DlgCode(next, NULL);
              SetFocus(next);
              // An automatic radio button is chosen as the focus reaches it.
              if ((nc & DLGC_RADIOBUTTON) &&
                  (GetWindowLongW(next, GWL_STYLE) & BS_TYPEMASK) == BS_AUTORADIOBUTTON)
                SendMessageW(next, BM_CLICK, 0, 0);
              UpdateDefault(dlg, next);
            }
          }
          return TRUE;
        case VK_RETURN: {
          HWND target;
          int id;
          if (code & (DLGC_DEFPUSHBUTTON)) {
            target = m->hwnd;
            id = GetDlgCtrlID(target);
          } else {
            LRESULT r = SendMessageW(dlg, DM_GETDEFID, 0, 0);
            id = HIWORD(r) == DC_HASDEFID ? LOWORD(r) : IDOK;
            target = GetDlgItem(dlg, id);
          }
          if (target && (code & DLGC_UNDEFPUSHBUTTON) && m->hwnd != target) {
            target = m->hwnd;
            id = GetDlgCtrlID(target);
          }
          if (!target || IsWindowEnabled(target)) SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)target);
          return TRUE;
        }
        case VK_ESCAPE: {
          HWND cancel = GetDlgItem(dlg, IDCANCEL);
          if (!cancel || IsWindowEnabled(cancel))
            SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), (LPARAM)cancel);
          return TRUE;
        }
      }
      break;
    case WM_CHAR:
      if (code & (DLGC_WANTCHARS)) break;
      if (m->wParam == L'\t' && !(code & DLGC_WANTTAB)) return TRUE;
      if (m->wParam < L' ') break;
      if (Mnemonic(dlg, m->hwnd, (wchar_t)m->wParam)) return TRUE;
      break;
    case WM_SYSCHAR:
      if (Mnemonic(dlg, m->hwnd, (wchar_t)m->wParam)) return TRUE;
      break;
  }
  TranslateMessage(m);
  DispatchMessageW(m);
  return TRUE;
}

BOOL WINAPI IsDialogMessageA(HWND dlg, LPMSG m) {
  // Characters arrive as the queue holds them; mnemonics use ASCII.
  return IsDialogMessageW(dlg, m);
}

UINT WINAPI GetDlgItemInt(HWND dlg, int id, BOOL* ok, BOOL isSigned) {
  if (ok) *ok = FALSE;
  wchar_t text[64];
  if (!GetDlgItem(dlg, id)) return 0;
  GetDlgItemTextW(dlg, id, text, 64);
  const wchar_t* p = text;
  while (*p == L' ' || *p == L'\t') p++;
  bool neg = false;
  if (*p == L'-' && isSigned) {
    neg = true;
    p++;
  } else if (*p == L'+') {
    p++;
  }
  if (*p < L'0' || *p > L'9') return 0;
  unsigned long long v = 0;
  while (*p >= L'0' && *p <= L'9') {
    v = v * 10 + (*p++ - L'0');
    if (v > 0xFFFFFFFFull) return 0;
  }
  while (*p == L' ' || *p == L'\t') p++;
  if (*p) return 0;
  if (isSigned) {
    if (neg ? v > 0x80000000ull : v > 0x7FFFFFFFull) return 0;
    if (ok) *ok = TRUE;
    return neg ? (UINT)(-(long long)v) : (UINT)v;
  }
  if (ok) *ok = TRUE;
  return (UINT)v;
}

BOOL WINAPI SetDlgItemInt(HWND dlg, int id, UINT value, BOOL isSigned) {
  wchar_t text[32];
  if (isSigned)
    swprintf(text, 32, L"%d", (int)value);
  else
    swprintf(text, 32, L"%u", value);
  return SetDlgItemTextW(dlg, id, text);
}

UINT WINAPI GetDlgItemTextW(HWND dlg, int id, LPWSTR out, int max) {
  HWND c = GetDlgItem(dlg, id);
  if (!c) {
    if (out && max > 0) out[0] = 0;
    return 0;
  }
  return (UINT)GetWindowTextW(c, out, max);
}

UINT WINAPI GetDlgItemTextA(HWND dlg, int id, LPSTR out, int max) {
  HWND c = GetDlgItem(dlg, id);
  if (!c) {
    if (out && max > 0) out[0] = 0;
    return 0;
  }
  return (UINT)GetWindowTextA(c, out, max);
}

BOOL WINAPI SetDlgItemTextW(HWND dlg, int id, LPCWSTR text) {
  HWND c = GetDlgItem(dlg, id);
  return c ? SetWindowTextW(c, text) : FALSE;
}

BOOL WINAPI SetDlgItemTextA(HWND dlg, int id, LPCSTR text) {
  HWND c = GetDlgItem(dlg, id);
  return c ? SetWindowTextA(c, text) : FALSE;
}

LRESULT WINAPI SendDlgItemMessageW(HWND dlg, int id, UINT msg, WPARAM wp, LPARAM lp) {
  HWND c = GetDlgItem(dlg, id);
  return c ? SendMessageW(c, msg, wp, lp) : 0;
}

LRESULT WINAPI SendDlgItemMessageA(HWND dlg, int id, UINT msg, WPARAM wp, LPARAM lp) {
  HWND c = GetDlgItem(dlg, id);
  return c ? SendMessageA(c, msg, wp, lp) : 0;
}

BOOL WINAPI CheckDlgButton(HWND dlg, int id, UINT check) {
  HWND c = GetDlgItem(dlg, id);
  if (!c) return FALSE;
  SendMessageW(c, BM_SETCHECK, check, 0);
  return TRUE;
}

UINT WINAPI IsDlgButtonChecked(HWND dlg, int id) {
  HWND c = GetDlgItem(dlg, id);
  return c ? (UINT)SendMessageW(c, BM_GETCHECK, 0, 0) : 0;
}

BOOL WINAPI CheckRadioButton(HWND dlg, int first, int last, int check) {
  bool found = false;
  for (int id = first; id <= last; id++) {
    HWND c = GetDlgItem(dlg, id);
    if (!c) continue;
    SendMessageW(c, BM_SETCHECK, id == check ? BST_CHECKED : BST_UNCHECKED, 0);
    if (id == check) found = true;
  }
  return found;
}

int WINAPI MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
  return MessageBoxCore(owner, text ? text : L"", caption ? caption : L"Error", type, NULL);
}

int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
  return MessageBoxCore(owner, text ? Widen(text) : L"", caption ? Widen(caption) : L"Error", type, NULL);
}

int WINAPI MessageBoxExW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type, WORD) {
  return MessageBoxW(owner, text, caption, type);
}

int WINAPI MessageBoxExA(HWND owner, LPCSTR text, LPCSTR caption, UINT type, WORD) {
  return MessageBoxA(owner, text, caption, type);
}

int WINAPI MessageBoxIndirectW(const MSGBOXPARAMSW* p) {
  if (!p) return 0;
  std::wstring text, caption;
  if (p->lpszText) text = IS_INTRESOURCE(p->lpszText) ? L"" : p->lpszText;
  if (IS_INTRESOURCE(p->lpszText) && p->lpszText) {
    wchar_t buf[1024];
    if (LoadStringW(p->hInstance, (UINT)(UINT_PTR)p->lpszText, buf, 1024)) text = buf;
  }
  if (p->lpszCaption) {
    if (IS_INTRESOURCE(p->lpszCaption)) {
      wchar_t buf[256];
      if (LoadStringW(p->hInstance, (UINT)(UINT_PTR)p->lpszCaption, buf, 256)) caption = buf;
    } else {
      caption = p->lpszCaption;
    }
  }
  HICON icon = NULL;
  if ((p->dwStyle & MB_USERICON) && p->lpszIcon) icon = LoadIconW(p->hInstance, p->lpszIcon);
  return MessageBoxCore(p->hwndOwner, text, caption.empty() ? L"Error" : caption, p->dwStyle & ~MB_USERICON, icon);
}

int WINAPI MessageBoxIndirectA(const MSGBOXPARAMSA* p) {
  if (!p) return 0;
  MSGBOXPARAMSW w;
  memset(&w, 0, sizeof w);
  w.cbSize = sizeof w;
  w.hwndOwner = p->hwndOwner;
  w.hInstance = p->hInstance;
  w.dwStyle = p->dwStyle;
  std::wstring text, caption, icon;
  if (p->lpszText && !IS_INTRESOURCE(p->lpszText)) {
    text = Widen(p->lpszText);
    w.lpszText = text.c_str();
  } else {
    w.lpszText = (LPCWSTR)p->lpszText;
  }
  if (p->lpszCaption && !IS_INTRESOURCE(p->lpszCaption)) {
    caption = Widen(p->lpszCaption);
    w.lpszCaption = caption.c_str();
  } else {
    w.lpszCaption = (LPCWSTR)p->lpszCaption;
  }
  if (p->lpszIcon && !IS_INTRESOURCE(p->lpszIcon)) {
    icon = Widen(p->lpszIcon);
    w.lpszIcon = icon.c_str();
  } else {
    w.lpszIcon = (LPCWSTR)p->lpszIcon;
  }
  w.dwContextHelpId = p->dwContextHelpId;
  w.lpfnMsgBoxCallback = p->lpfnMsgBoxCallback;
  w.dwLanguageId = p->dwLanguageId;
  return MessageBoxIndirectW(&w);
}

}  // extern "C"
