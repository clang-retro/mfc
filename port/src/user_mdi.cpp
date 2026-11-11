// user_mdi.cpp - the multiple-document interface: the MDI client window
// and its children, DefFrameProc and DefMDIChildProc, the window menu,
// a maximized child's buttons in the frame's menu bar, and arranging
// windows (cascade, tile, icons).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

const int kMaxListed = 9;  // children named in the window menu

struct Client {
  HWND hwnd = NULL;
  HWND frame = NULL;
  HMENU windowMenu = NULL;
  UINT firstId = 0;
  std::vector<HWND> kids;  // in the order of their IDs
  HWND active = NULL;
  HWND maximized = NULL;
  // The window menu's entries this code added.
  HMENU listedMenu = NULL;
  int listed = 0;
  bool listedSeparator = false;
  // The maximized child's buttons in the frame's menu bar.
  HMENU buttonsMenu = NULL;
  HMENU sysPopup = NULL;
  std::wstring frameTitle;  // the frame's own title, without the child's
  int cascadeStep = 0;
};

Client* ClientOf(HWND h) {
  if (!h || !IsWindow(h)) return NULL;
  wchar_t cls[16];
  if (!GetClassNameW(h, cls, 16) || _wcsicmp(cls, L"MDIClient") != 0) return NULL;
  return (Client*)GetWindowLongPtrW(h, 0);
}

Client* ClientOfChild(HWND child) { return ClientOf(GetParent(child)); }

std::wstring TextOf(HWND h) {
  int n = GetWindowTextLengthW(h);
  std::wstring s(n + 1, L'\0');
  GetWindowTextW(h, &s[0], n + 1);
  s.resize(wcslen(s.c_str()));
  return s;
}

bool IsKid(Client* c, HWND h) { return std::find(c->kids.begin(), c->kids.end(), h) != c->kids.end(); }

// The window menu: a separator, then "&1 Title" for each child.
void Unlist(Client* c) {
  if (!c->listedMenu || !IsMenu(c->listedMenu)) {
    c->listedMenu = NULL;
    c->listed = 0;
    c->listedSeparator = false;
    return;
  }
  for (int i = 0; i < c->listed; i++) DeleteMenu(c->listedMenu, c->firstId + i, MF_BYCOMMAND);
  if (c->listedSeparator) {
    int n = GetMenuItemCount(c->listedMenu);
    if (n > 0 && (GetMenuState(c->listedMenu, n - 1, MF_BYPOSITION) & MF_SEPARATOR))
      DeleteMenu(c->listedMenu, n - 1, MF_BYPOSITION);
  }
  c->listedMenu = NULL;
  c->listed = 0;
  c->listedSeparator = false;
}

void List(Client* c) {
  Unlist(c);
  if (!c->windowMenu || !IsMenu(c->windowMenu)) return;
  std::vector<HWND> shown;
  for (HWND k : c->kids)
    if (IsWindowVisible(k)) shown.push_back(k);
  if (shown.empty()) return;
  c->listedMenu = c->windowMenu;
  int n = GetMenuItemCount(c->windowMenu);
  if (n > 0) {
    AppendMenuW(c->windowMenu, MF_SEPARATOR, 0, NULL);
    c->listedSeparator = true;
  }
  // The IDs follow the window menu's order, so the listed ones come first.
  int i = 0;
  for (HWND k : shown) {
    if (i >= kMaxListed) break;
    std::wstring label = L"&" + std::to_wstring(i + 1) + L" " + TextOf(k);
    UINT flags = MF_STRING | (k == c->active ? MF_CHECKED : 0);
    AppendMenuW(c->windowMenu, flags, c->firstId + i, label.c_str());
    i++;
  }
  c->listed = i;
}

// Gives the children IDs in order: shown ones first, as the menu lists them.
void Renumber(Client* c) {
  std::stable_partition(c->kids.begin(), c->kids.end(), [](HWND k) { return IsWindowVisible(k) != 0; });
  for (size_t i = 0; i < c->kids.size(); i++) SetWindowLongPtrW(c->kids[i], GWLP_ID, c->firstId + i);
}

void Refresh(Client* c) {
  Renumber(c);
  List(c);
  if (c->frame && GetMenu(c->frame)) DrawMenuBar(c->frame);
}

// The frame's title: its own, and a maximized child's in brackets.
void ShowFrameTitle(Client* c) {
  if (!c->frame) return;
  std::wstring t = c->frameTitle;
  if (c->maximized && IsWindow(c->maximized)) {
    std::wstring k = TextOf(c->maximized);
    if (!k.empty()) t = t.empty() ? L"[" + k + L"]" : t + L" - [" + k + L"]";
  }
  DefWindowProcW(c->frame, WM_SETTEXT, 0, (LPARAM)t.c_str());
}

// A maximized child's system menu and buttons in the frame's menu bar.
void RemoveButtons(Client* c) {
  HMENU m = c->buttonsMenu;
  c->buttonsMenu = NULL;
  if (m && IsMenu(m)) {
    int n = GetMenuItemCount(m);
    for (int i = n - 1; i >= 0; i--) {
      MENUITEMINFOW mi = {sizeof mi};
      mi.fMask = MIIM_BITMAP | MIIM_SUBMENU;
      if (!GetMenuItemInfoW(m, i, TRUE, &mi)) continue;
      UINT_PTR b = (UINT_PTR)mi.hbmpItem;
      if (b == (UINT_PTR)HBMMENU_SYSTEM || b == (UINT_PTR)HBMMENU_MBAR_MINIMIZE ||
          b == (UINT_PTR)HBMMENU_MBAR_RESTORE || b == (UINT_PTR)HBMMENU_MBAR_CLOSE)
        RemoveMenu(m, i, MF_BYPOSITION);
    }
  }
  if (c->sysPopup) DestroyMenu(c->sysPopup);
  c->sysPopup = NULL;
  if (c->frame && GetMenu(c->frame) == m) DrawMenuBar(c->frame);
}

void AddButtons(Client* c) {
  RemoveButtons(c);
  HMENU m = c->frame ? GetMenu(c->frame) : NULL;
  if (!m || !c->maximized) return;
  // The child's system menu, as plain commands the frame passes on.
  HMENU sys = CreatePopupMenu();
  AppendMenuW(sys, MF_STRING, SC_RESTORE, L"&Restore");
  AppendMenuW(sys, MF_STRING | MF_GRAYED, SC_MOVE, L"&Move");
  AppendMenuW(sys, MF_STRING | MF_GRAYED, SC_SIZE, L"&Size");
  AppendMenuW(sys, MF_STRING, SC_MINIMIZE, L"Mi&nimize");
  AppendMenuW(sys, MF_STRING | MF_GRAYED, SC_MAXIMIZE, L"Ma&ximize");
  AppendMenuW(sys, MF_SEPARATOR, 0, NULL);
  AppendMenuW(sys, MF_STRING, SC_CLOSE, L"&Close\tCtrl+F4");
  AppendMenuW(sys, MF_SEPARATOR, 0, NULL);
  AppendMenuW(sys, MF_STRING, SC_NEXTWINDOW, L"Nex&t\tCtrl+F6");
  DWORD style = GetWindowLongW(c->maximized, GWL_STYLE);
  if (!(style & WS_MINIMIZEBOX)) EnableMenuItem(sys, SC_MINIMIZE, MF_GRAYED);
  c->sysPopup = sys;

  MENUITEMINFOW mi = {sizeof mi};
  mi.fMask = MIIM_SUBMENU | MIIM_BITMAP | MIIM_DATA | MIIM_ID;
  mi.hSubMenu = sys;
  mi.hbmpItem = HBMMENU_SYSTEM;
  mi.dwItemData = (ULONG_PTR)c->maximized;
  InsertMenuItemW(m, 0, TRUE, &mi);
  struct {
    UINT cmd;
    HBITMAP bmp;
    bool want;
  } buttons[] = {
      {SC_MINIMIZE, HBMMENU_MBAR_MINIMIZE, (style & WS_MINIMIZEBOX) != 0},
      {SC_RESTORE, HBMMENU_MBAR_RESTORE, true},
      {SC_CLOSE, HBMMENU_MBAR_CLOSE, true},
  };
  bool first = true;
  for (auto& b : buttons) {
    if (!b.want) continue;
    MENUITEMINFOW bi = {sizeof bi};
    bi.fMask = MIIM_ID | MIIM_BITMAP | MIIM_FTYPE;
    bi.fType = first ? MFT_RIGHTJUSTIFY : 0;
    bi.wID = b.cmd;
    bi.hbmpItem = b.bmp;
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &bi);
    first = false;
  }
  c->buttonsMenu = m;
  DrawMenuBar(c->frame);
}

// Where a maximized child goes: its frame just outside the client area.
RECT MaximizedRect(Client* c, HWND child) {
  RECT r;
  GetClientRect(c->hwnd, &r);
  DWORD style = GetWindowLongW(child, GWL_STYLE) & ~(WS_MAXIMIZE | WS_MINIMIZE);
  AdjustWindowRectEx(&r, style, FALSE, GetWindowLongW(child, GWL_EXSTYLE));
  return r;
}

void SetMaximized(Client* c, HWND child) {
  if (c->maximized == child) {
    if (child) AddButtons(c);  // the frame's menu may have changed
    return;
  }
  c->maximized = child;
  if (child)
    AddButtons(c);
  else
    RemoveButtons(c);
  ShowFrameTitle(c);
}

bool FrameHasFocus(Client* c) {
  HWND f = GetFocus();
  return c->frame && (GetActiveWindow() == c->frame || f == c->frame || (f && IsChild(c->frame, f)));
}

void Activate(Client* c, HWND child) {
  if (child && !IsKid(c, child)) return;
  HWND old = c->active;
  if (old == child) {
    if (child && FrameHasFocus(c) && IsWindowVisible(child)) {
      HWND f = GetFocus();
      if (f != child && !IsChild(child, f)) SetFocus(child);
    }
    return;
  }
  // A maximized child hands its state on.
  bool wasMax = old && c->maximized == old && IsZoomed(old);
  if (wasMax && child) {
    ShowWindow(child, SW_SHOWMAXIMIZED);
    ShowWindow(old, SW_SHOWNOACTIVATE);
    ShowWindow(old, SW_RESTORE);
  }
  if (old && IsWindow(old)) {
    SendMessageW(old, WM_NCACTIVATE, FALSE, 0);
    SendMessageW(old, WM_MDIACTIVATE, (WPARAM)old, (LPARAM)child);
  }
  c->active = child;
  if (child) {
    SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(child, WM_NCACTIVATE, GetActiveWindow() == c->frame || !GetActiveWindow(), 0);
    SendMessageW(child, WM_MDIACTIVATE, (WPARAM)old, (LPARAM)child);
    if (c->active == child && FrameHasFocus(c) && IsWindowVisible(child)) SetFocus(child);
    if (IsZoomed(child)) SetMaximized(c, child);
  }
  List(c);
  if (c->frame && GetMenu(c->frame)) DrawMenuBar(c->frame);
}

// The children in z-order, top first, that can take activation.
std::vector<HWND> Stack(Client* c, bool all) {
  std::vector<HWND> out;
  for (HWND k = GetWindow(c->hwnd, GW_CHILD); k; k = GetWindow(k, GW_HWNDNEXT))
    if (IsKid(c, k) && (all || (IsWindowVisible(k) && IsWindowEnabled(k)))) out.push_back(k);
  return out;
}

void Next(Client* c, HWND from, bool previous) {
  if (!from) from = c->active;
  std::vector<HWND> s = Stack(c, false);
  if (s.size() < 2) return;
  auto it = std::find(s.begin(), s.end(), from);
  if (it == s.end()) return;
  HWND to;
  if (previous) {
    to = s.back();
  } else {
    to = (it + 1 == s.end()) ? s.front() : *(it + 1);
  }
  Activate(c, to);
  // Going forward, the old child goes to the bottom of the pile.
  if (!previous) SetWindowPos(from, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// A child is going away: forget it and activate another.
void Forget(Client* c, HWND child) {
  auto it = std::find(c->kids.begin(), c->kids.end(), child);
  if (it == c->kids.end()) return;
  c->kids.erase(it);
  if (c->maximized == child) SetMaximized(c, NULL);
  if (c->active == child) {
    HWND next = NULL;
    for (HWND k : Stack(c, false))
      if (k != child) {
        next = k;
        break;
      }
    SendMessageW(child, WM_NCACTIVATE, FALSE, 0);
    SendMessageW(child, WM_MDIACTIVATE, (WPARAM)child, (LPARAM)next);
    c->active = NULL;
    if (next) {
      Activate(c, next);
    } else if (FrameHasFocus(c)) {
      SetFocus(c->hwnd);
    }
  }
  Refresh(c);
}

HWND Create(Client* c, const MDICREATESTRUCTW* mcs) {
  if (!mcs) return NULL;
  DWORD style = mcs->style;
  if (!(GetWindowLongW(c->hwnd, GWL_STYLE) & MDIS_ALLCHILDSTYLES))
    style = (style & (WS_MINIMIZE | WS_MAXIMIZE | WS_HSCROLL | WS_VSCROLL | WS_VISIBLE)) | WS_CAPTION | WS_SYSMENU |
            WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
  style |= WS_CHILD | WS_CLIPSIBLINGS;
  style &= ~WS_POPUP;
  // A new child is maximized when the active one is.
  bool maximize = (style & WS_MAXIMIZE) || (c->maximized && IsZoomed(c->maximized));
  style &= ~WS_MAXIMIZE;
  RECT rc;
  GetClientRect(c->hwnd, &rc);
  int x = mcs->x, y = mcs->y, cx = mcs->cx, cy = mcs->cy;
  if (x == CW_USEDEFAULT || x == (int)0x80000000) {
    int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
    int fit = std::max(1, (rc.bottom - rc.top) / 3 / std::max(1, step));
    x = y = (c->cascadeStep % fit) * step;
    c->cascadeStep++;
  }
  if (cx == CW_USEDEFAULT || cx == (int)0x80000000 || cx <= 0) {
    cx = (rc.right - rc.left) * 3 / 4;
    cy = (rc.bottom - rc.top) * 3 / 4;
  }
  HWND h = CreateWindowExW(WS_EX_MDICHILD, mcs->szClass, mcs->szTitle, style, x, y, cx, cy, c->hwnd,
                           (HMENU)(UINT_PTR)(c->firstId + c->kids.size()), (HINSTANCE)mcs->hOwner, (LPVOID)mcs);
  if (!h) return NULL;
  if (!IsKid(c, h)) c->kids.push_back(h);
  if (maximize) ShowWindow(h, SW_SHOWMAXIMIZED);
  Activate(c, h);
  Refresh(c);
  return h;
}

LRESULT CALLBACK ClientProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Client* c = (Client*)GetWindowLongPtrW(h, 0);
  switch (msg) {
    case WM_NCCREATE:
      c = new Client;
      c->hwnd = h;
      SetWindowLongPtrW(h, 0, (LONG_PTR)c);
      break;
    case WM_CREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      if (cs && cs->lpCreateParams) {
        CLIENTCREATESTRUCT* ccs = (CLIENTCREATESTRUCT*)cs->lpCreateParams;
        c->windowMenu = (HMENU)ccs->hWindowMenu;
        c->firstId = ccs->idFirstChild;
      }
      c->frame = GetParent(h);
      if (c->frame) c->frameTitle = TextOf(c->frame);
      return 0;
    }
    case WM_NCDESTROY:
      if (c) {
        RemoveButtons(c);
        SetWindowLongPtrW(h, 0, 0);
        delete c;
      }
      break;
  }
  if (!c) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_MDICREATE:
      return (LRESULT)Create(c, (const MDICREATESTRUCTW*)lp);
    case WM_MDIDESTROY: {
      HWND k = (HWND)wp;
      if (!IsKid(c, k)) return 0;
      Forget(c, k);
      DestroyWindow(k);
      return 0;
    }
    case WM_MDIACTIVATE: {
      HWND k = (HWND)wp;
      if (IsKid(c, k)) Activate(c, k);
      return 0;
    }
    case WM_MDIGETACTIVE:
      if (lp) *(BOOL*)lp = c->active && c->maximized == c->active;
      return (LRESULT)c->active;
    case WM_MDINEXT:
      Next(c, (HWND)wp, lp != 0);
      return 0;
    case WM_MDIMAXIMIZE:
      if (IsKid(c, (HWND)wp)) ShowWindow((HWND)wp, SW_MAXIMIZE);
      return 0;
    case WM_MDIRESTORE:
      if (IsKid(c, (HWND)wp)) ShowWindow((HWND)wp, SW_RESTORE);
      return 0;
    case WM_MDICASCADE:
      return CascadeWindows(h, (UINT)wp, NULL, 0, NULL) != 0;
    case WM_MDITILE:
      return TileWindows(h, (UINT)wp, NULL, 0, NULL) != 0;  // TRUE, not the count
    case WM_MDIICONARRANGE:
      ArrangeIconicWindows(h);
      return 0;
    case WM_MDISETMENU: {
      HMENU old = c->frame ? GetMenu(c->frame) : NULL;
      HMENU frameMenu = (HMENU)wp, windowMenu = (HMENU)lp;
      if (frameMenu && frameMenu != old) {
        RemoveButtons(c);
        Unlist(c);
        SetMenu(c->frame, frameMenu);
        if (c->maximized) AddButtons(c);
      }
      if (windowMenu && windowMenu != c->windowMenu) {
        Unlist(c);
        c->windowMenu = windowMenu;
      }
      List(c);
      if (c->frame) DrawMenuBar(c->frame);
      return (LRESULT)old;
    }
    case WM_MDIREFRESHMENU:
      Refresh(c);
      return (LRESULT)(c->frame ? GetMenu(c->frame) : NULL);
    case WM_SETFOCUS:
      if (c->active && IsWindowVisible(c->active) && !IsIconic(c->active)) SetFocus(c->active);
      return 0;
    case WM_SIZE:
      if (c->maximized && IsWindow(c->maximized)) {
        RECT r = MaximizedRect(c, c->maximized);
        SetWindowPos(c->maximized, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      break;
    case WM_PARENTNOTIFY:
      if (LOWORD(wp) == WM_LBUTTONDOWN) {
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        HWND k = ChildWindowFromPoint(h, pt);
        if (k && k != h && IsKid(c, k)) Activate(c, k);
      }
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// The shared part of DefFrameProcA and DefFrameProcW: TRUE if handled.
bool FrameCore(HWND frame, HWND client, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out, bool unicode) {
  Client* c = ClientOf(client);
  *out = 0;
  switch (msg) {
    case WM_COMMAND: {
      if (!c) return false;
      UINT id = LOWORD(wp);
      if (HIWORD(wp) <= 1 && id >= c->firstId && id < c->firstId + (UINT)c->kids.size()) {
        HWND k = c->kids[id - c->firstId];
        if (IsIconic(k)) ShowWindow(k, SW_RESTORE);
        Activate(c, k);
        return true;
      }
      // The maximized child's system menu and buttons in the menu bar.
      if (id >= 0xF000 && c->active) {
        *out = SendMessageW(c->active, WM_SYSCOMMAND, id, lp);
        return true;
      }
      return false;
    }
    case WM_NCACTIVATE:
      if (c && c->active) SendMessageW(c->active, WM_NCACTIVATE, wp, 0);
      return false;
    case WM_SETFOCUS:
      if (client && IsWindow(client)) SetFocus(client);
      return true;
    case WM_SIZE:
      if (client && IsWindow(client)) {
        RECT r;
        GetClientRect(frame, &r);
        MoveWindow(client, 0, 0, r.right, r.bottom, TRUE);
      }
      return false;
    case WM_SETTEXT:
      if (c) {
        if (unicode)
          c->frameTitle = lp ? (const wchar_t*)lp : L"";
        else
          c->frameTitle = lp ? Widen((const char*)lp) : L"";
        ShowFrameTitle(c);
        *out = TRUE;
        return true;
      }
      return false;
    case WM_SYSCOMMAND:
      if (c && (wp & 0xFFF0) == SC_NEXTWINDOW) {
        Next(c, NULL, false);
        return true;
      }
      if (c && (wp & 0xFFF0) == SC_PREVWINDOW) {
        Next(c, NULL, true);
        return true;
      }
      return false;
    case WM_MENUCHAR:
      // Alt+- opens the active child's system menu.
      if (c && c->active && LOWORD(wp) == '-') {
        PostMessageW(c->active, WM_SYSCOMMAND, SC_KEYMENU, '-');
        *out = MAKELRESULT(0, MNC_CLOSE);
        return true;
      }
      return false;
  }
  return false;
}

// The shared part of DefMDIChildProcA and DefMDIChildProcW.
bool ChildCore(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
  Client* c = ClientOfChild(h);
  *out = 0;
  if (!c) return false;
  switch (msg) {
    case WM_SETFOCUS:
      if (c->active != h) Activate(c, h);
      return false;
    case WM_CHILDACTIVATE:
      Activate(c, h);
      return true;
    case WM_CLOSE:
      SendMessageW(c->hwnd, WM_MDIDESTROY, (WPARAM)h, 0);
      return true;
    case WM_SYSCOMMAND:
      switch (wp & 0xFFF0) {
        case SC_NEXTWINDOW:
          Next(c, h, false);
          return true;
        case SC_PREVWINDOW:
          Next(c, h, true);
          return true;
        case SC_MAXIMIZE:
          if (c->maximized && c->maximized != h && IsZoomed(c->maximized)) ShowWindow(c->maximized, SW_RESTORE);
          ShowWindow(h, SW_MAXIMIZE);
          Activate(c, h);
          return true;
        case SC_RESTORE:
          ShowWindow(h, SW_RESTORE);
          Activate(c, h);
          return true;
        case SC_MINIMIZE:
          ShowWindow(h, SW_MINIMIZE);
          return true;
        case SC_KEYMENU:
          if (c->maximized == h && c->frame) {
            // The child's system menu is in the frame's menu bar.
            *out = SendMessageW(c->frame, WM_SYSCOMMAND, wp, lp);
            return true;
          }
          return false;
      }
      return false;
    case WM_SIZE:
      if (wp == SIZE_MAXIMIZED) {
        SetMaximized(c, h);
      } else if (c->maximized == h) {
        SetMaximized(c, NULL);
      }
      if (wp == SIZE_MINIMIZED && c->active == h) {
        // An icon gives up the focus but stays the active child.
        if (FrameHasFocus(c)) SetFocus(c->hwnd);
      }
      return false;
    case WM_GETMINMAXINFO: {
      MINMAXINFO* mmi = (MINMAXINFO*)lp;
      if (mmi) {
        RECT r = MaximizedRect(c, h);
        mmi->ptMaxSize.x = r.right - r.left;
        mmi->ptMaxSize.y = r.bottom - r.top;
        mmi->ptMaxPosition.x = r.left;
        mmi->ptMaxPosition.y = r.top;
      }
      return true;
    }
    case WM_DESTROY:
      Forget(c, h);
      return false;
  }
  return false;
}

void AfterChildText(HWND h) {
  Client* c = ClientOfChild(h);
  if (!c) return;
  List(c);
  if (c->maximized == h) ShowFrameTitle(c);
}

// Only shown children are listed. A hidden child stays active, as MFC
// expects while it hides one.
void AfterChildShow(HWND h) {
  if (Client* c = ClientOfChild(h)) Refresh(c);
}

}  // namespace

namespace winport {

void RegisterMdiClass() {
  RegisterSystemClass(L"MDIClient", ClientProc, 0, sizeof(void*), (HBRUSH)(COLOR_APPWORKSPACE + 1),
                      MAKEINTRESOURCEW(32512));
}

}  // namespace winport

extern "C" {

LRESULT WINAPI DefFrameProcW(HWND frame, HWND client, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r;
  if (FrameCore(frame, client, msg, wp, lp, &r, true)) return r;
  return DefWindowProcW(frame, msg, wp, lp);
}

LRESULT WINAPI DefFrameProcA(HWND frame, HWND client, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r;
  if (FrameCore(frame, client, msg, wp, lp, &r, false)) return r;
  return DefWindowProcA(frame, msg, wp, lp);
}

LRESULT WINAPI DefMDIChildProcW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r;
  if (ChildCore(h, msg, wp, lp, &r)) return r;
  r = DefWindowProcW(h, msg, wp, lp);
  if (msg == WM_SETTEXT) AfterChildText(h);
  if (msg == WM_SHOWWINDOW) AfterChildShow(h);
  return r;
}

LRESULT WINAPI DefMDIChildProcA(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r;
  if (ChildCore(h, msg, wp, lp, &r)) return r;
  r = DefWindowProcA(h, msg, wp, lp);
  if (msg == WM_SETTEXT) AfterChildText(h);
  if (msg == WM_SHOWWINDOW) AfterChildShow(h);
  return r;
}

HWND WINAPI CreateMDIWindowW(LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int cx, int cy, HWND client,
                             HINSTANCE inst, LPARAM lp) {
  MDICREATESTRUCTW mcs = {cls, title, inst, x, y, cx, cy, style, lp};
  return (HWND)SendMessageW(client, WM_MDICREATE, 0, (LPARAM)&mcs);
}

HWND WINAPI CreateMDIWindowA(LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int cx, int cy, HWND client,
                             HINSTANCE inst, LPARAM lp) {
  MDICREATESTRUCTA mcs = {cls, title, inst, x, y, cx, cy, style, lp};
  return (HWND)SendMessageA(client, WM_MDICREATE, 0, (LPARAM)&mcs);
}

BOOL WINAPI TranslateMDISysAccel(HWND client, LPMSG m) {
  if (!m || (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN)) return FALSE;
  Client* c = ClientOf(client);
  if (!c || !c->active || !IsWindowEnabled(c->active)) return FALSE;
  if (!(GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return FALSE;
  UINT cmd;
  switch (m->wParam) {
    case VK_F4:
      cmd = SC_CLOSE;
      break;
    case VK_F6:
    case VK_TAB:
      cmd = (GetKeyState(VK_SHIFT) & 0x8000) ? SC_PREVWINDOW : SC_NEXTWINDOW;
      break;
    default:
      return FALSE;
  }
  SendMessageW(c->active, WM_SYSCOMMAND, cmd, (LPARAM)m->wParam);
  return TRUE;
}

// The windows to arrange: the given ones, or the parent's children (the
// desktop's top-level windows for NULL), skipping hidden, minimized and
// owned ones.
static std::vector<HWND> Arrangeable(HWND parent, UINT count, const HWND* kids, UINT how, bool icons) {
  std::vector<HWND> all, out;
  if (kids && count) {
    all.assign(kids, kids + count);
  } else {
    HWND first = parent ? GetWindow(parent, GW_CHILD) : GetWindow(GetDesktopWindow(), GW_CHILD);
    for (HWND k = first; k; k = GetWindow(k, GW_HWNDNEXT)) all.push_back(k);
  }
  for (HWND k : all) {
    if (!IsWindowVisible(k)) continue;
    if ((bool)IsIconic(k) != icons) continue;
    if (!parent && GetWindow(k, GW_OWNER)) continue;
    if ((how & MDITILE_SKIPDISABLED) && !IsWindowEnabled(k)) continue;
    out.push_back(k);
  }
  return out;
}

static RECT ArrangeArea(HWND parent, const RECT* r) {
  RECT a;
  if (r) {
    a = *r;
  } else if (parent) {
    GetClientRect(parent, &a);
  } else {
    a = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  return a;
}

// Leaves room at the bottom for a row of icons.
static void SpareIcons(HWND parent, RECT* a) {
  if (!Arrangeable(parent, 0, NULL, 0, true).empty()) a->bottom -= GetSystemMetrics(SM_CYCAPTION) + 8;
}

WORD WINAPI CascadeWindows(HWND parent, UINT how, const RECT* r, UINT count, const HWND* kids) {
  if (Client* c = ClientOf(parent))
    if (c->maximized && IsWindow(c->maximized)) ShowWindow(c->maximized, SW_RESTORE);
  std::vector<HWND> list = Arrangeable(parent, count, kids, how, false);
  if (list.empty()) return 0;
  RECT a = ArrangeArea(parent, r);
  SpareIcons(parent, &a);
  // From the bottom of the pile up, so the top one ends last and in front.
  int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
  int n = (int)list.size();
  int w = std::max((a.right - a.left) - step * (n - 1), (a.right - a.left) * 2 / 3);
  int h = std::max((a.bottom - a.top) - step * (n - 1), (a.bottom - a.top) * 2 / 3);
  int fit = std::max(1, ((a.bottom - a.top) - h) / std::max(1, step) + 1);
  for (int i = 0; i < n; i++) {
    HWND k = list[n - 1 - i];
    int off = (i % fit) * step;
    SetWindowPos(k, HWND_TOP, a.left + off, a.top + off, w, h, SWP_NOACTIVATE);
  }
  return (WORD)n;
}

WORD WINAPI TileWindows(HWND parent, UINT how, const RECT* r, UINT count, const HWND* kids) {
  if (Client* c = ClientOf(parent))
    if (c->maximized && IsWindow(c->maximized)) ShowWindow(c->maximized, SW_RESTORE);
  std::vector<HWND> list = Arrangeable(parent, count, kids, how, false);
  if (list.empty()) return 0;
  RECT a = ArrangeArea(parent, r);
  SpareIcons(parent, &a);
  int n = (int)list.size();
  // Rows and columns as near square as they go; the last ones get the extra.
  int major = 1;
  while (major * major < n) major++;
  bool horizontal = (how & MDITILE_HORIZONTAL) != 0;
  int lines = std::min(major, n);
  int perLine = n / lines, extra = n % lines;
  int W = a.right - a.left, H = a.bottom - a.top;
  int k = 0;
  for (int line = 0; line < lines; line++) {
    int inLine = perLine + (line >= lines - extra ? 1 : 0);
    for (int j = 0; j < inLine; j++, k++) {
      int x, y, cx, cy;
      if (horizontal) {
        // Lines are rows stacked down the area.
        y = a.top + H * line / lines;
        cy = a.top + H * (line + 1) / lines - y;
        x = a.left + W * j / inLine;
        cx = a.left + W * (j + 1) / inLine - x;
      } else {
        x = a.left + W * line / lines;
        cx = a.left + W * (line + 1) / lines - x;
        y = a.top + H * j / inLine;
        cy = a.top + H * (j + 1) / inLine - y;
      }
      SetWindowPos(list[k], NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  return (WORD)n;
}

UINT WINAPI ArrangeIconicWindows(HWND parent) {
  std::vector<HWND> icons = Arrangeable(parent, 0, NULL, 0, true);
  if (icons.empty()) return 0;
  RECT a = ArrangeArea(parent, NULL);
  int cx = GetSystemMetrics(SM_CXMINIMIZED) > 0 ? GetSystemMetrics(SM_CXMINIMIZED) : 160;
  int cy = GetSystemMetrics(SM_CYCAPTION) + 6;
  int x = a.left, y = a.bottom - cy;
  for (HWND k : icons) {
    if (x + cx > a.right && x > a.left) {
      x = a.left;
      y -= cy;
    }
    SetWindowPos(k, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    x += cx;
  }
  return (UINT)cy;
}

}  // extern "C"
