// comctl_extra.cpp - the IP address control, the extended combo box
// (ComboBoxEx32) and the animation control.
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

int FontHeightOf(HWND h, HFONT f) {
  HDC dc = GetDC(h);
  HGDIOBJ old = SelectObject(dc, f ? f : ControlFont());
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(h, dc);
  return tm.tmHeight;
}

// The command a control sends its parent.
void Command(HWND h, UINT code) {
  SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), code), (LPARAM)h);
}

// =====================================================================
// IP address

struct IpAddress {
  HWND hwnd = NULL;
  HWND fields[4] = {NULL, NULL, NULL, NULL};
  BYTE lo[4] = {0, 0, 0, 0}, hi[4] = {255, 255, 255, 255};
  HFONT font = NULL;
  bool hasFocus = false;
  bool setting = false;  // changing the fields' text ourselves
};

WNDPROC editProc = NULL;  // the edit class's procedure, behind each field's

IpAddress* IpOf(HWND h) { return (IpAddress*)GetWindowLongPtrW(h, 0); }

int FieldOf(IpAddress* ip, HWND f) {
  for (int i = 0; i < 4; i++)
    if (ip->fields[i] == f) return i;
  return -1;
}

// A field's value, or -1 when it is blank.
int FieldValue(IpAddress* ip, int i) {
  wchar_t buf[8];
  GetWindowTextW(ip->fields[i], buf, 8);
  if (!buf[0]) return -1;
  return (int)wcstol(buf, NULL, 10);
}

void SetField(IpAddress* ip, int i, int v) {
  ip->setting = true;
  if (v < 0) {
    SetWindowTextW(ip->fields[i], L"");
  } else {
    wchar_t buf[8];
    swprintf(buf, 8, L"%d", v);
    SetWindowTextW(ip->fields[i], buf);
  }
  ip->setting = false;
}

// Keeps a field in its range, and tells the parent (which may change the value).
void CheckField(IpAddress* ip, int i) {
  int v = FieldValue(ip, i);
  NMIPADDRESS nm = {};
  nm.iField = i;
  nm.iValue = v;
  SendNotify(ip->hwnd, NULL, IPN_FIELDCHANGED, &nm.hdr);
  v = nm.iValue;
  if (v >= 0) v = std::max((int)ip->lo[i], std::min(v, (int)ip->hi[i]));
  if (v != FieldValue(ip, i)) SetField(ip, i, v);
}

void LayoutIp(IpAddress* ip) {
  RECT c;
  GetClientRect(ip->hwnd, &c);
  int dot = 6, w = (c.right - 3 * dot) / 4;
  int fh = FontHeightOf(ip->hwnd, ip->font);
  int y = std::max(0, (int)(c.bottom - fh) / 2);
  for (int i = 0; i < 4; i++)
    SetWindowPos(ip->fields[i], NULL, i * (w + dot), y, w, std::min(fh, (int)c.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
}

void FocusField(IpAddress* ip, int i, bool selectAll) {
  if (i < 0 || i > 3) return;
  SetFocus(ip->fields[i]);
  if (selectAll) SendMessageW(ip->fields[i], EM_SETSEL, 0, -1);
}

LRESULT CALLBACK FieldProc(HWND f, UINT msg, WPARAM wp, LPARAM lp) {
  HWND parent = GetParent(f);
  IpAddress* ip = parent ? IpOf(parent) : NULL;
  int i = ip ? FieldOf(ip, f) : -1;
  if (!ip || i < 0) return CallWindowProcW(editProc, f, msg, wp, lp);
  switch (msg) {
    case WM_CHAR: {
      DWORD start = 0, end = 0;
      SendMessageW(f, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
      int len = GetWindowTextLengthW(f);
      if (wp == L'.' || wp == L' ') {
        if (len > 0 && start == end && i < 3) FocusField(ip, i + 1, true);
        return 0;
      }
      if (wp == VK_BACK && start == 0 && end == 0 && i > 0) {
        FocusField(ip, i - 1, false);
        int n = GetWindowTextLengthW(ip->fields[i - 1]);
        SendMessageW(ip->fields[i - 1], EM_SETSEL, n, n);
        return 0;
      }
      if (wp >= L'0' && wp <= L'9') {
        LRESULT r = CallWindowProcW(editProc, f, msg, wp, lp);
        // Three digits, or a value that can take no more, move on.
        int v = FieldValue(ip, i);
        if (i < 3 && (GetWindowTextLengthW(f) >= 3 || v * 10 > 255)) FocusField(ip, i + 1, true);
        return r;
      }
      if (wp < L' ') return CallWindowProcW(editProc, f, msg, wp, lp);
      return 0;  // nothing else goes in an address
    }
    case WM_KEYDOWN: {
      DWORD start = 0, end = 0;
      SendMessageW(f, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
      int len = GetWindowTextLengthW(f);
      if ((wp == VK_RIGHT || wp == VK_DOWN) && end >= (DWORD)len && i < 3) {
        FocusField(ip, i + 1, false);
        SendMessageW(ip->fields[i + 1], EM_SETSEL, 0, 0);
        return 0;
      }
      if ((wp == VK_LEFT || wp == VK_UP) && start == 0 && i > 0) {
        FocusField(ip, i - 1, false);
        int n = GetWindowTextLengthW(ip->fields[i - 1]);
        SendMessageW(ip->fields[i - 1], EM_SETSEL, n, n);
        return 0;
      }
      if (wp == VK_HOME && GetKeyState(VK_CONTROL) < 0) {
        FocusField(ip, 0, false);
        return 0;
      }
      if (wp == VK_END && GetKeyState(VK_CONTROL) < 0) {
        FocusField(ip, 3, false);
        return 0;
      }
      break;
    }
    case WM_KILLFOCUS: {
      LRESULT r = CallWindowProcW(editProc, f, msg, wp, lp);
      CheckField(ip, i);
      HWND to = (HWND)wp;
      if (ip->hasFocus && FieldOf(ip, to) < 0) {
        ip->hasFocus = false;
        Command(ip->hwnd, EN_KILLFOCUS);
      }
      return r;
    }
    case WM_SETFOCUS: {
      LRESULT r = CallWindowProcW(editProc, f, msg, wp, lp);
      if (!ip->hasFocus) {
        ip->hasFocus = true;
        Command(ip->hwnd, EN_SETFOCUS);
      }
      return r;
    }
    case WM_GETDLGCODE:
      return CallWindowProcW(editProc, f, msg, wp, lp) & ~DLGC_HASSETSEL;
  }
  return CallWindowProcW(editProc, f, msg, wp, lp);
}

LRESULT CALLBACK IpProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  IpAddress* ip = IpOf(h);
  if (msg == WM_NCCREATE) {
    ip = new IpAddress;
    ip->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)ip);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!ip) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      delete ip;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE: {
      HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
      for (int i = 0; i < 4; i++) {
        ip->fields[i] = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, 0, 0, 0, 0, h,
                                        (HMENU)(UINT_PTR)(i + 1), inst, NULL);
        if (!ip->fields[i]) return -1;
        SendMessageW(ip->fields[i], EM_LIMITTEXT, 3, 0);
        WNDPROC old = (WNDPROC)SetWindowLongPtrW(ip->fields[i], GWLP_WNDPROC, (LONG_PTR)FieldProc);
        if (!editProc) editProc = old;
        SendMessageW(ip->fields[i], WM_SETFONT, (WPARAM)ControlFont(), FALSE);
      }
      LayoutIp(ip);
      return 0;
    }
    case WM_SIZE:
      LayoutIp(ip);
      return 0;
    case WM_SETFONT:
      ip->font = (HFONT)wp;
      for (HWND f : ip->fields) SendMessageW(f, WM_SETFONT, wp, lp);
      LayoutIp(ip);
      return 0;
    case WM_GETFONT:
      return (LRESULT)ip->font;
    case WM_ENABLE:
      for (HWND f : ip->fields) EnableWindow(f, wp != 0);
      InvalidateRect(h, NULL, TRUE);
      return 0;
    case WM_ERASEBKGND: {
      RECT c;
      GetClientRect(h, &c);
      FillRect((HDC)wp, &c, GetSysColorBrush(IsWindowEnabled(h) ? COLOR_WINDOW : COLOR_BTNFACE));
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      HGDIOBJ old = SelectObject(dc, ip->font ? ip->font : ControlFont());
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, GetSysColor(IsWindowEnabled(h) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
      for (int i = 0; i < 3; i++) {
        RECT fr;
        GetWindowRect(ip->fields[i], &fr);
        MapWindowPoints(NULL, h, (POINT*)&fr, 2);
        RECT d = {fr.right, fr.top, fr.right + 6, fr.bottom};
        DrawTextW(dc, L".", 1, &d, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
      }
      SelectObject(dc, old);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_SETFOCUS:
      FocusField(ip, 0, true);
      return 0;
    case WM_LBUTTONDOWN: {
      // Between fields: the nearest one.
      int x = (short)LOWORD(lp), best = 0, bestDist = 0x7FFFFFFF;
      for (int i = 0; i < 4; i++) {
        RECT fr;
        GetWindowRect(ip->fields[i], &fr);
        MapWindowPoints(NULL, h, (POINT*)&fr, 2);
        int dist = x < fr.left ? fr.left - x : x > fr.right ? x - fr.right : 0;
        if (dist < bestDist) bestDist = dist, best = i;
      }
      FocusField(ip, best, false);
      return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wp) == EN_CHANGE && !ip->setting) Command(h, EN_CHANGE);
      return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      // The fields look like the parent's edit.
      return SendMessageW(GetParent(h), msg == WM_CTLCOLORSTATIC || !IsWindowEnabled(h) ? WM_CTLCOLORSTATIC
                                                                                          : WM_CTLCOLOREDIT,
                          wp, (LPARAM)h);
    case WM_GETTEXTLENGTH:
    case WM_GETTEXT: {
      std::wstring s;
      for (int i = 0; i < 4; i++) {
        int v = FieldValue(ip, i);
        if (i) s += L'.';
        if (v >= 0) s += std::to_wstring(v);
        else s += L'0';
      }
      if (msg == WM_GETTEXTLENGTH) return (LRESULT)s.size();
      return CopyOut(s, (void*)lp, (int)wp, true);
    }
    case WM_SETTEXT: {
      const wchar_t* t = (const wchar_t*)lp;
      int v[4] = {-1, -1, -1, -1}, n = 0;
      while (t && *t && n < 4) {
        if (*t >= L'0' && *t <= L'9') {
          v[n] = (int)wcstol(t, (wchar_t**)&t, 10);
          n++;
        } else {
          t++;
          if (n == 0 && v[0] < 0) continue;
        }
      }
      for (int i = 0; i < 4; i++) SetField(ip, i, v[i] < 0 ? -1 : std::min(v[i], 255));
      return TRUE;
    }
    case IPM_CLEARADDRESS:
      for (int i = 0; i < 4; i++) SetField(ip, i, -1);
      return 0;
    case IPM_SETADDRESS: {
      DWORD a = (DWORD)lp;
      SetField(ip, 0, FIRST_IPADDRESS(a));
      SetField(ip, 1, SECOND_IPADDRESS(a));
      SetField(ip, 2, THIRD_IPADDRESS(a));
      SetField(ip, 3, FOURTH_IPADDRESS(a));
      return TRUE;
    }
    case IPM_GETADDRESS: {
      DWORD a = 0;
      int filled = 0;
      for (int i = 0; i < 4; i++) {
        int v = FieldValue(ip, i);
        if (v >= 0) filled++;
        a = (a << 8) | (DWORD)(v < 0 ? 0 : std::min(v, 255));
      }
      if (lp) *(DWORD*)lp = a;
      return filled;
    }
    case IPM_SETRANGE:
      if (wp > 3) return FALSE;
      ip->lo[wp] = LOBYTE(LOWORD(lp));
      ip->hi[wp] = HIBYTE(LOWORD(lp));
      return TRUE;
    case IPM_SETFOCUS:
      FocusField(ip, wp > 3 ? 0 : (int)wp, true);
      return 0;
    case IPM_ISBLANK:
      for (int i = 0; i < 4; i++)
        if (FieldValue(ip, i) >= 0) return FALSE;
      return TRUE;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// ComboBoxEx

struct ExItem {
  std::wstring text;
  bool textCallback = false;
  int image = -1, selImage = -1, overlay = -1, indent = 0;
  LPARAM lParam = 0;
};

struct ComboEx {
  HWND hwnd = NULL;
  HWND combo = NULL;
  HIMAGELIST himl = NULL;
  HFONT font = NULL;
  DWORD exStyle = 0;
  std::vector<ExItem> items;
  bool editChanged = false;
};

ComboEx* ComboExOf(HWND h) { return (ComboEx*)GetWindowLongPtrW(h, 0); }

HWND EditOf(ComboEx* c) {
  for (HWND w = GetWindow(c->combo, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
    wchar_t cls[16];
    GetClassNameW(w, cls, 16);
    if (_wcsicmp(cls, L"Edit") == 0) return w;
  }
  return NULL;
}

int ItemHeight(ComboEx* c) {
  int h = FontHeightOf(c->hwnd, c->font);
  if (c->himl) {
    int cx = 0, cy = 0;
    ImageList_GetIconSize(c->himl, &cx, &cy);
    h = std::max(h, cy);
  }
  return h + 2;
}

// An item's text and images, asking the parent for those it keeps.
ExItem Resolve(ComboEx* c, int i) {
  ExItem it = c->items[i];
  bool needText = it.textCallback, needImage = it.image == I_IMAGECALLBACK, needSel = it.selImage == I_IMAGECALLBACK;
  if (!needText && !needImage && !needSel) return it;
  bool unicode = NotifyUnicode(c->hwnd);
  wchar_t wbuf[260] = {};
  char abuf[260] = {};
  if (unicode) {
    NMCOMBOBOXEXW nm = {};
    nm.ceItem.iItem = i;
    nm.ceItem.lParam = it.lParam;
    if (needText) nm.ceItem.mask |= CBEIF_TEXT, nm.ceItem.pszText = wbuf, nm.ceItem.cchTextMax = 260;
    if (needImage) nm.ceItem.mask |= CBEIF_IMAGE;
    if (needSel) nm.ceItem.mask |= CBEIF_SELECTEDIMAGE;
    SendNotify(c->hwnd, NULL, CBEN_GETDISPINFOW, &nm.hdr);
    if (needText) it.text = nm.ceItem.pszText ? nm.ceItem.pszText : L"";
    if (needImage) it.image = nm.ceItem.iImage;
    if (needSel) it.selImage = nm.ceItem.iSelectedImage;
  } else {
    NMCOMBOBOXEXA nm = {};
    nm.ceItem.iItem = i;
    nm.ceItem.lParam = it.lParam;
    if (needText) nm.ceItem.mask |= CBEIF_TEXT, nm.ceItem.pszText = abuf, nm.ceItem.cchTextMax = 260;
    if (needImage) nm.ceItem.mask |= CBEIF_IMAGE;
    if (needSel) nm.ceItem.mask |= CBEIF_SELECTEDIMAGE;
    SendNotify(c->hwnd, NULL, CBEN_GETDISPINFOA, &nm.hdr);
    if (needText) it.text = nm.ceItem.pszText ? Widen(nm.ceItem.pszText) : L"";
    if (needImage) it.image = nm.ceItem.iImage;
    if (needSel) it.selImage = nm.ceItem.iSelectedImage;
  }
  return it;
}

template <class ITEM>
void ApplyExItem(ExItem& it, const ITEM* in, bool unicode) {
  if (in->mask & CBEIF_TEXT) {
    it.textCallback = IsCallback(in->pszText, unicode);
    it.text = TextIn(in->pszText, unicode);
  }
  if (in->mask & CBEIF_IMAGE) it.image = in->iImage;
  if (in->mask & CBEIF_SELECTEDIMAGE) it.selImage = in->iSelectedImage;
  if (in->mask & CBEIF_OVERLAY) it.overlay = in->iOverlay;
  if (in->mask & CBEIF_INDENT) it.indent = in->iIndent;
  if (in->mask & CBEIF_LPARAM) it.lParam = in->lParam;
}

template <class ITEM>
LRESULT InsertExItem(ComboEx* c, const ITEM* in, bool unicode) {
  if (!in) return -1;
  int at = (int)in->iItem;
  if (at < 0 || at > (int)c->items.size()) at = (int)c->items.size();
  ExItem it;
  ApplyExItem(it, in, unicode);
  c->items.insert(c->items.begin() + at, it);
  // The combo box keeps the text, so that its edit and searches work.
  LRESULT r = SendMessageW(c->combo, CB_INSERTSTRING, at, (LPARAM)it.text.c_str());
  if (r < 0) {
    c->items.erase(c->items.begin() + at);
    return -1;
  }
  bool notifyUnicode = NotifyUnicode(c->hwnd);
  if (notifyUnicode) {
    NMCOMBOBOXEXW nm = {};
    nm.ceItem.mask = in->mask;
    nm.ceItem.iItem = at;
    nm.ceItem.pszText = (LPWSTR)it.text.c_str();
    nm.ceItem.iImage = it.image;
    nm.ceItem.iSelectedImage = it.selImage;
    nm.ceItem.iOverlay = it.overlay;
    nm.ceItem.iIndent = it.indent;
    nm.ceItem.lParam = it.lParam;
    SendNotify(c->hwnd, NULL, CBEN_INSERTITEM, &nm.hdr);
  } else {
    std::string a = Narrow(it.text.c_str());
    NMCOMBOBOXEXA nm = {};
    nm.ceItem.mask = in->mask;
    nm.ceItem.iItem = at;
    nm.ceItem.pszText = (LPSTR)a.c_str();
    nm.ceItem.iImage = it.image;
    nm.ceItem.iSelectedImage = it.selImage;
    nm.ceItem.iOverlay = it.overlay;
    nm.ceItem.iIndent = it.indent;
    nm.ceItem.lParam = it.lParam;
    SendNotify(c->hwnd, NULL, CBEN_INSERTITEM, &nm.hdr);
  }
  return at;
}

template <class ITEM>
LRESULT SetExItem(ComboEx* c, const ITEM* in, bool unicode) {
  if (!in) return FALSE;
  int i = (int)in->iItem;
  if (i == -1) {
    // The edit's text.
    if (in->mask & CBEIF_TEXT) {
      std::wstring t = TextIn(in->pszText, unicode);
      if (HWND e = EditOf(c)) SetWindowTextW(e, t.c_str());
    }
    return TRUE;
  }
  if (i < 0 || i >= (int)c->items.size()) return FALSE;
  ApplyExItem(c->items[i], in, unicode);
  if (in->mask & CBEIF_TEXT) {
    int sel = (int)SendMessageW(c->combo, CB_GETCURSEL, 0, 0);
    SendMessageW(c->combo, CB_DELETESTRING, i, 0);
    SendMessageW(c->combo, CB_INSERTSTRING, i, (LPARAM)c->items[i].text.c_str());
    if (sel == i) SendMessageW(c->combo, CB_SETCURSEL, i, 0);
  }
  InvalidateRect(c->combo, NULL, TRUE);
  return TRUE;
}

template <class ITEM>
LRESULT GetExItem(ComboEx* c, ITEM* out, bool unicode) {
  if (!out) return FALSE;
  int i = (int)out->iItem;
  if (i == -1) {
    int sel = (int)SendMessageW(c->combo, CB_GETCURSEL, 0, 0);
    HWND e = EditOf(c);
    if (e && (out->mask & CBEIF_TEXT)) {
      int n = GetWindowTextLengthW(e);
      std::wstring t(n + 1, L'\0');
      GetWindowTextW(e, &t[0], n + 1);
      t.resize(n);
      CopyOut(t, out->pszText, out->cchTextMax, unicode);
      if (sel < 0) return TRUE;
    }
    if (sel < 0) return FALSE;
    i = sel;
    if (e) out->mask &= ~CBEIF_TEXT;
  }
  if (i < 0 || i >= (int)c->items.size()) return FALSE;
  ExItem it = Resolve(c, i);
  if (out->mask & CBEIF_TEXT) CopyOut(it.text, out->pszText, out->cchTextMax, unicode);
  if (out->mask & CBEIF_IMAGE) out->iImage = it.image;
  if (out->mask & CBEIF_SELECTEDIMAGE) out->iSelectedImage = it.selImage;
  if (out->mask & CBEIF_OVERLAY) out->iOverlay = it.overlay;
  if (out->mask & CBEIF_INDENT) out->iIndent = it.indent;
  if (out->mask & CBEIF_LPARAM) out->lParam = it.lParam;
  return TRUE;
}

void DeleteExItem(ComboEx* c, int i) {
  NMCOMBOBOXEXW nm = {};
  nm.ceItem.mask = CBEIF_LPARAM;
  nm.ceItem.iItem = i;
  nm.ceItem.lParam = c->items[i].lParam;
  SendNotify(c->hwnd, NULL, CBEN_DELETEITEM, &nm.hdr);
  c->items.erase(c->items.begin() + i);
}

void DrawExItem(ComboEx* c, const DRAWITEMSTRUCT* d) {
  HDC dc = d->hDC;
  RECT r = d->rcItem;
  bool selected = (d->itemState & ODS_SELECTED) != 0;
  bool edit = (d->itemState & ODS_COMBOBOXEDIT) != 0;
  FillRect(dc, &r, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
  if ((int)d->itemID < 0 || (int)d->itemID >= (int)c->items.size()) return;
  ExItem it = Resolve(c, (int)d->itemID);
  int x = r.left + 2 + (edit ? 0 : it.indent * 10);
  if (c->himl && !(edit && (c->exStyle & CBES_EX_NOEDITIMAGE))) {
    int cx = 0, cy = 0;
    ImageList_GetIconSize(c->himl, &cx, &cy);
    int img = selected && it.selImage >= 0 ? it.selImage : it.image;
    if (img >= 0) {
      UINT style = ILD_TRANSPARENT;
      if (it.overlay > 0) style |= INDEXTOOVERLAYMASK(it.overlay);
      ImageList_Draw(c->himl, img, dc, x, r.top + (r.bottom - r.top - cy) / 2, style);
    }
    x += cx + 3;
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
  HGDIOBJ old = SelectObject(dc, c->font ? c->font : ControlFont());
  RECT tr = {x, r.top, r.right, r.bottom};
  DrawTextW(dc, it.text.c_str(), (int)it.text.size(), &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  SelectObject(dc, old);
  if (d->itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}

LRESULT CALLBACK ComboExProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ComboEx* c = ComboExOf(h);
  if (msg == WM_NCCREATE) {
    c = new ComboEx;
    c->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)c);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!c) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      delete c;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_CREATE: {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
      DWORD style = cs->style;
      DWORD kind = style & 3;
      if (!kind) kind = CBS_DROPDOWN;
      DWORD cstyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | kind | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                     (style & (CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL | CBS_NOINTEGRALHEIGHT | CBS_SORT));
      c->combo = CreateWindowExW(0, L"ComboBox", L"", cstyle, 0, 0, cs->cx, cs->cy, h, (HMENU)(UINT_PTR)GetDlgCtrlID(h),
                                 cs->hInstance, NULL);
      if (!c->combo) return -1;
      SendMessageW(c->combo, WM_SETFONT, (WPARAM)ControlFont(), FALSE);
      return 0;
    }
    case WM_DESTROY:
      while (!c->items.empty()) DeleteExItem(c, (int)c->items.size() - 1);
      return 0;
    case WM_SIZE:
      if (c->combo) {
        RECT r;
        GetClientRect(h, &r);
        SetWindowPos(c->combo, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case WM_SETFONT:
      c->font = (HFONT)wp;
      SendMessageW(c->combo, WM_SETFONT, wp, lp);
      SendMessageW(c->combo, CB_SETITEMHEIGHT, (WPARAM)-1, ItemHeight(c));
      SendMessageW(c->combo, CB_SETITEMHEIGHT, 0, ItemHeight(c));
      return 0;
    case WM_GETFONT:
      return (LRESULT)c->font;
    case WM_SETFOCUS:
      SetFocus(c->combo);
      return 0;
    case WM_ENABLE:
      EnableWindow(c->combo, wp != 0);
      return 0;
    case WM_SETTEXT:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
      return SendMessageW(c->combo, msg, wp, lp);
    case WM_MEASUREITEM: {
      MEASUREITEMSTRUCT* m = (MEASUREITEMSTRUCT*)lp;
      m->itemHeight = ItemHeight(c);
      return TRUE;
    }
    case WM_DRAWITEM:
      DrawExItem(c, (const DRAWITEMSTRUCT*)lp);
      return TRUE;
    case WM_COMMAND:
      if ((HWND)lp == c->combo) {
        UINT code = HIWORD(wp);
        if (code == CBN_EDITCHANGE) c->editChanged = true;
        if (code == CBN_KILLFOCUS && c->editChanged && EditOf(c)) {
          NMCBEENDEDITW nm = {};
          nm.fChanged = TRUE;
          nm.iNewSelection = (int)SendMessageW(c->combo, CB_GETCURSEL, 0, 0);
          GetWindowTextW(EditOf(c), nm.szText, CBEMAXSTRLEN);
          nm.iWhy = CBENF_KILLFOCUS;
          SendNotify(h, NULL, CBEN_ENDEDITW, &nm.hdr);
          c->editChanged = false;
        }
        Command(h, code);
        return 0;
      }
      return SendMessageW(GetParent(h), msg, wp, lp);
    case WM_NOTIFY:
      return SendMessageW(GetParent(h), msg, wp, lp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
      return SendMessageW(GetParent(h), msg, wp, lp);
    case CBEM_INSERTITEMA:
      return InsertExItem(c, (const COMBOBOXEXITEMA*)lp, false);
    case CBEM_INSERTITEMW:
      return InsertExItem(c, (const COMBOBOXEXITEMW*)lp, true);
    case CBEM_SETITEMA:
      return SetExItem(c, (const COMBOBOXEXITEMA*)lp, false);
    case CBEM_SETITEMW:
      return SetExItem(c, (const COMBOBOXEXITEMW*)lp, true);
    case CBEM_GETITEMA:
      return GetExItem(c, (COMBOBOXEXITEMA*)lp, false);
    case CBEM_GETITEMW:
      return GetExItem(c, (COMBOBOXEXITEMW*)lp, true);
    case CBEM_DELETEITEM: {  // CB_DELETESTRING
      if (wp >= c->items.size()) return CB_ERR;
      DeleteExItem(c, (int)wp);
      return SendMessageW(c->combo, CB_DELETESTRING, wp, 0);
    }
    case CB_RESETCONTENT:
      while (!c->items.empty()) DeleteExItem(c, (int)c->items.size() - 1);
      return SendMessageW(c->combo, CB_RESETCONTENT, 0, 0);
    case CB_ADDSTRING:
    case CB_INSERTSTRING: {
      COMBOBOXEXITEMW it = {};
      it.mask = CBEIF_TEXT;
      it.iItem = msg == CB_ADDSTRING ? -1 : (INT_PTR)(int)wp;
      it.pszText = (LPWSTR)lp;
      return InsertExItem(c, &it, true);
    }
    case CB_GETITEMDATA:
      return wp < c->items.size() ? c->items[wp].lParam : CB_ERR;
    case CB_SETITEMDATA:
      if (wp >= c->items.size()) return CB_ERR;
      c->items[wp].lParam = lp;
      return TRUE;
    case CBEM_SETIMAGELIST: {
      HIMAGELIST old = c->himl;
      c->himl = (HIMAGELIST)lp;
      SendMessageW(c->combo, CB_SETITEMHEIGHT, (WPARAM)-1, ItemHeight(c));
      SendMessageW(c->combo, CB_SETITEMHEIGHT, 0, ItemHeight(c));
      InvalidateRect(c->combo, NULL, TRUE);
      return (LRESULT)old;
    }
    case CBEM_GETIMAGELIST:
      return (LRESULT)c->himl;
    case CBEM_GETCOMBOCONTROL:
      return (LRESULT)c->combo;
    case CBEM_GETEDITCONTROL:
      return (LRESULT)EditOf(c);
    case CBEM_SETEXSTYLE:
    case CBEM_SETEXTENDEDSTYLE: {
      DWORD old = c->exStyle;
      DWORD mask = msg == CBEM_SETEXTENDEDSTYLE && wp ? (DWORD)wp : 0xFFFFFFFF;
      c->exStyle = (c->exStyle & ~mask) | ((DWORD)lp & mask);
      InvalidateRect(c->combo, NULL, TRUE);
      return old;
    }
    case CBEM_GETEXSTYLE:
      return c->exStyle;
    case CBEM_HASEDITCHANGED:
      return c->editChanged;
    case CBEM_SETUNICODEFORMAT:
    case CBEM_GETUNICODEFORMAT:
      return TRUE;
  }
  // The rest of the combo box messages are the combo box's.
  if (msg >= CB_GETEDITSEL && msg <= CB_GETCOMBOBOXINFO) return SendMessageW(c->combo, msg, wp, lp);
  return DefWindowProcW(h, msg, wp, lp);
}

// =====================================================================
// Animation: uncompressed and RLE8 AVI clips, from resources or files.

struct Clip {
  int width = 0, height = 0;
  DWORD usPerFrame = 66667;
  std::vector<BYTE> header;  // BITMAPINFO for the frames as drawn (8 bpp or the clip's own)
  std::vector<std::vector<BYTE>> frames;
  bool rle = false;
};

struct Animate {
  HWND hwnd = NULL;
  Clip* clip = NULL;
  std::vector<BYTE> pixels;  // the current frame, decoded
  int frame = 0;
  int from = 0, to = 0, repeat = 0;
  bool playing = false;
  HANDLE thread = NULL;
};

Animate* AnimateOf(HWND h) { return (Animate*)GetWindowLongPtrW(h, 0); }

DWORD Read32(const BYTE* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }
bool Is(const BYTE* p, const char* fcc) { return memcmp(p, fcc, 4) == 0; }

void ParseChunks(Clip* c, const BYTE* p, const BYTE* end, int stream, bool* videoStream, int* streamNo) {
  while (p + 8 <= end) {
    DWORD size = Read32(p + 4);
    const BYTE* data = p + 8;
    if (data + size > end) size = (DWORD)(end - data);
    if (Is(p, "LIST") || Is(p, "RIFF")) {
      if (size >= 4) {
        if (Is(data, "strl")) {
          (*streamNo)++;
          *videoStream = false;
        }
        ParseChunks(c, data + 4, data + size, stream, videoStream, streamNo);
      }
    } else if (Is(p, "avih") && size >= 40) {
      c->usPerFrame = Read32(data);
      c->width = (int)Read32(data + 32);
      c->height = (int)Read32(data + 36);
    } else if (Is(p, "strh") && size >= 8) {
      *videoStream = Is(data, "vids");
    } else if (Is(p, "strf") && *videoStream && c->header.empty() && size >= sizeof(BITMAPINFOHEADER)) {
      c->header.assign(data, data + size);
      BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)c->header.data();
      c->rle = bih->biCompression == BI_RLE8;
    } else if (size > 0 && (p[2] == 'd') && (p[3] == 'b' || p[3] == 'c') && p[0] >= '0' && p[0] <= '9') {
      c->frames.emplace_back(data, data + size);
    }
    p = data + size + (size & 1);
  }
}

Clip* LoadClip(const BYTE* data, size_t size) {
  if (size < 12 || !Is(data, "RIFF") || !Is(data + 8, "AVI ")) return NULL;
  Clip* c = new Clip;
  bool video = false;
  int streams = 0;
  ParseChunks(c, data + 12, data + std::min(size, (size_t)Read32(data + 4) + 8), 0, &video, &streams);
  if (c->header.empty() || c->frames.empty()) {
    delete c;
    return NULL;
  }
  BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)c->header.data();
  if (bih->biCompression != BI_RGB && bih->biCompression != BI_RLE8) {
    delete c;
    return NULL;
  }
  if (!c->width) c->width = bih->biWidth;
  if (!c->height) c->height = abs(bih->biHeight);
  if (c->rle) {
    // RLE8 frames decode into an 8-bit picture.
    if (c->header.size() < sizeof(BITMAPINFOHEADER) + 256 * 4) c->header.resize(sizeof(BITMAPINFOHEADER) + 256 * 4);
    bih = (BITMAPINFOHEADER*)c->header.data();
    bih->biCompression = BI_RGB;
    bih->biSizeImage = 0;
  }
  return c;
}

// RLE8, onto the previous frame (bottom row first, like the DIB).
void DecodeRle8(const std::vector<BYTE>& in, std::vector<BYTE>& out, int w, int h) {
  int stride = (w + 3) & ~3;
  int x = 0, y = 0;
  size_t i = 0;
  while (i + 1 < in.size() && y < h) {
    BYTE n = in[i++], v = in[i++];
    if (n) {
      for (int k = 0; k < n && x < w; k++) out[(size_t)y * stride + x++] = v;
    } else if (v == 0) {
      x = 0;
      y++;
    } else if (v == 1) {
      break;
    } else if (v == 2) {
      if (i + 1 >= in.size()) break;
      x += in[i++];
      y += in[i++];
    } else {
      for (int k = 0; k < v && i < in.size(); k++) {
        BYTE b = in[i++];
        if (x < w) out[(size_t)y * stride + x++] = b;
      }
      if (v & 1) i++;
    }
  }
}

void ShowFrame(Animate* a, int f) {
  Clip* c = a->clip;
  if (!c) return;
  f = std::max(0, std::min(f, (int)c->frames.size() - 1));
  BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)c->header.data();
  if (c->rle) {
    int stride = (c->width + 3) & ~3;
    if (a->pixels.size() != (size_t)stride * c->height || f == 0 || f < a->frame) {
      a->pixels.assign((size_t)stride * c->height, 0);
      for (int k = 0; k < f; k++) DecodeRle8(c->frames[k], a->pixels, c->width, c->height);
    } else {
      for (int k = a->frame + 1; k < f; k++) DecodeRle8(c->frames[k], a->pixels, c->width, c->height);
    }
    DecodeRle8(c->frames[f], a->pixels, c->width, c->height);
  } else {
    a->pixels = c->frames[f];
    size_t need = (size_t)(((bih->biWidth * bih->biBitCount + 31) / 32) * 4) * abs(bih->biHeight);
    if (a->pixels.size() < need) a->pixels.resize(need, 0);
  }
  a->frame = f;
  InvalidateRect(a->hwnd, NULL, FALSE);
}

void PaintAnimate(Animate* a, HDC dc) {
  RECT r;
  GetClientRect(a->hwnd, &r);
  HBRUSH bg = (HBRUSH)SendMessageW(GetParent(a->hwnd), WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)a->hwnd);
  if (!bg) bg = GetSysColorBrush(COLOR_BTNFACE);
  Clip* c = a->clip;
  if (!c || a->pixels.empty()) {
    FillRect(dc, &r, bg);
    return;
  }
  int x = 0, y = 0;
  if (GetWindowLongW(a->hwnd, GWL_STYLE) & ACS_CENTER) {
    x = (r.right - c->width) / 2;
    y = (r.bottom - c->height) / 2;
  }
  // Around the picture, and under it where it is transparent.
  HRGN all = CreateRectRgnIndirect(&r), pic = CreateRectRgn(x, y, x + c->width, y + c->height);
  CombineRgn(all, all, pic, RGN_DIFF);
  FillRgn(dc, all, bg);
  DeleteObject(all);
  DeleteObject(pic);
  BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)c->header.data();
  if ((GetWindowLongW(a->hwnd, GWL_STYLE) & ACS_TRANSPARENT) && bih->biBitCount == 8) {
    // The first pixel's color shows the background through.
    std::vector<BYTE> info(c->header);
    BITMAPINFO* bi = (BITMAPINFO*)info.data();
    int stride = (c->width + 3) & ~3;
    BYTE key = a->pixels[(size_t)(c->height - 1) * stride];
    LOGBRUSH lb;
    COLORREF bk = GetObjectW(bg, sizeof lb, &lb) && lb.lbStyle == BS_SOLID ? lb.lbColor : GetSysColor(COLOR_BTNFACE);
    bi->bmiColors[key].rgbRed = GetRValue(bk);
    bi->bmiColors[key].rgbGreen = GetGValue(bk);
    bi->bmiColors[key].rgbBlue = GetBValue(bk);
    StretchDIBits(dc, x, y, c->width, c->height, 0, 0, c->width, c->height, a->pixels.data(), bi, DIB_RGB_COLORS,
                  SRCCOPY);
    return;
  }
  StretchDIBits(dc, x, y, c->width, c->height, 0, 0, c->width, c->height, a->pixels.data(),
                (BITMAPINFO*)c->header.data(), DIB_RGB_COLORS, SRCCOPY);
}

void Stop(Animate* a) {
  if (!a->playing) return;
  a->playing = false;
  KillTimer(a->hwnd, 1);
  Command(a->hwnd, ACN_STOP);
}

void Close(Animate* a) {
  Stop(a);
  delete a->clip;
  a->clip = NULL;
  a->pixels.clear();
  InvalidateRect(a->hwnd, NULL, TRUE);
}

bool Open(Animate* a, HINSTANCE inst, const std::wstring* name, LPCWSTR id) {
  Close(a);
  std::vector<BYTE> data;
  if (!inst) inst = (HINSTANCE)GetWindowLongPtrW(a->hwnd, GWLP_HINSTANCE);
  LPCWSTR resName = id ? id : name->c_str();
  HRSRC res = FindResourceW(inst, resName, L"AVI");
  if (res) {
    HGLOBAL g = LoadResource(inst, res);
    const BYTE* p = (const BYTE*)LockResource(g);
    if (p) data.assign(p, p + SizeofResource(inst, res));
  } else if (!id) {
    HANDLE f = CreateFileW(name->c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD size = GetFileSize(f, NULL), got = 0;
      if (size != INVALID_FILE_SIZE) {
        data.resize(size);
        if (!ReadFile(f, data.data(), size, &got, NULL) || got != size) data.clear();
      }
      CloseHandle(f);
    }
  }
  if (data.empty()) return false;
  a->clip = LoadClip(data.data(), data.size());
  if (!a->clip) return false;
  a->frame = 0;
  a->pixels.clear();
  ShowFrame(a, 0);
  if (!(GetWindowLongW(a->hwnd, GWL_STYLE) & ACS_CENTER)) {
    RECT nc = {0, 0, a->clip->width, a->clip->height};
    AdjustWindowRectEx(&nc, GetWindowLongW(a->hwnd, GWL_STYLE), FALSE, GetWindowLongW(a->hwnd, GWL_EXSTYLE));
    SetWindowPos(a->hwnd, NULL, 0, 0, nc.right - nc.left, nc.bottom - nc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  return true;
}

void Play(Animate* a, int repeat, int from, int to) {
  Clip* c = a->clip;
  if (!c) return;
  int last = (int)c->frames.size() - 1;
  if (to < 0 || to > last) to = last;
  from = std::max(0, std::min(from, last));
  Stop(a);
  ShowFrame(a, from);
  if (from == to) return;
  a->from = from;
  a->to = to;
  a->repeat = repeat;
  a->playing = true;
  Command(a->hwnd, ACN_START);
  UINT ms = std::max(10u, (UINT)(c->usPerFrame / 1000));
  SetTimer(a->hwnd, 1, ms, NULL);
}

LRESULT CALLBACK AnimateProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Animate* a = AnimateOf(h);
  if (msg == WM_NCCREATE) {
    a = new Animate;
    a->hwnd = h;
    SetWindowLongPtrW(h, 0, (LONG_PTR)a);
    return DefWindowProcW(h, msg, wp, lp);
  }
  if (!a) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_NCDESTROY:
      KillTimer(h, 1);
      delete a->clip;
      delete a;
      SetWindowLongPtrW(h, 0, 0);
      return DefWindowProcW(h, msg, wp, lp);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
      PAINTSTRUCT ps;
      HDC dc = wp ? (HDC)wp : BeginPaint(h, &ps);
      PaintAnimate(a, dc);
      if (!wp) EndPaint(h, &ps);
      return 0;
    }
    case WM_TIMER:
      if (wp == 1 && a->playing) {
        if (a->frame < a->to) {
          ShowFrame(a, a->frame + 1);
        } else if (a->repeat == -1 || --a->repeat > 0) {
          ShowFrame(a, a->from);
        } else {
          Stop(a);
        }
      }
      return 0;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case ACM_OPENA:
    case ACM_OPENW: {
      if (!lp) {
        Close(a);
        return TRUE;
      }
      bool ok;
      if (IS_INTRESOURCE(lp)) {
        ok = Open(a, (HINSTANCE)wp, NULL, (LPCWSTR)lp);
      } else {
        std::wstring name = msg == ACM_OPENW ? std::wstring((LPCWSTR)lp) : Widen((LPCSTR)lp);
        ok = Open(a, (HINSTANCE)wp, &name, NULL);
      }
      if (ok && (GetWindowLongW(h, GWL_STYLE) & ACS_AUTOPLAY)) Play(a, -1, 0, -1);
      return ok;
    }
    case ACM_PLAY:
      if (!a->clip) return FALSE;
      Play(a, (int)wp == 0 ? -1 : (int)wp, (short)LOWORD(lp), (short)HIWORD(lp));
      return TRUE;
    case ACM_STOP:
      Stop(a);
      return TRUE;
    case ACM_ISPLAYING:
      return a->playing;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------
// The network address control (shell32's msctls_netaddress on Vista): an
// edit control whose text NCM_GETADDRESS reads as an IPv4 or IPv6
// address, a network, a host name, or any of them with a port, of the
// kinds NCM_SETALLOWTYPE allows.

#define NETADDR_GETADDRESS (WM_USER + 1)
#define NETADDR_SETALLOWTYPE (WM_USER + 2)
#define NETADDR_GETALLOWTYPE (WM_USER + 3)
#define NETADDR_DISPLAYERRORTIP (WM_USER + 4)
enum {
  kIpv4Address = 0x1,
  kIpv4Service = 0x2,
  kIpv4Network = 0x4,
  kIpv6Address = 0x8,
  kIpv6AddressNoScope = 0x10,
  kIpv6Service = 0x20,
  kIpv6ServiceNoScope = 0x40,
  kIpv6Network = 0x80,
  kNamedAddress = 0x100,
  kNamedService = 0x200,
};
const wchar_t kAllowProp[] = L"winport.netaddress.allow";

// NC_ADDRESS, and the NET_ADDRESS_INFO it points at, as Windows lays
// them out: the format, then a host name and port as text, or a socket
// address.
struct NcAddress {
  BYTE* info;
  USHORT port;
  BYTE prefix;
};
const size_t kNameChars = 256, kPortChars = 6;

bool ParseIpv4(const std::wstring& t, BYTE out[4]) {
  int part = 0;
  size_t i = 0;
  while (part < 4) {
    if (i >= t.size() || !iswdigit(t[i])) return false;
    unsigned v = 0;
    size_t start = i;
    while (i < t.size() && iswdigit(t[i])) {
      v = v * 10 + (t[i] - L'0');
      if (v > 255 || i - start > 2) return false;
      i++;
    }
    out[part++] = (BYTE)v;
    if (part < 4) {
      if (i >= t.size() || t[i] != L'.') return false;
      i++;
    }
  }
  return i == t.size();
}

bool ParseIpv6(const std::wstring& t, BYTE out[16]) {
  // Groups of hex digits, one "::" for a run of zeros, and an IPv4
  // address in the last 32 bits.
  std::vector<int> head, tail;
  std::vector<int>* cur = &head;
  bool gap = false;
  size_t i = 0;
  if (t.compare(0, 2, L"::") == 0) {
    gap = true;
    cur = &tail;
    i = 2;
  }
  while (i < t.size()) {
    size_t j = i;
    while (j < t.size() && iswxdigit(t[j]) && j - i < 5) j++;
    if (j < t.size() && t[j] == L'.') {
      BYTE v4[4];
      if (!ParseIpv4(t.substr(i), v4)) return false;
      cur->push_back(v4[0] << 8 | v4[1]);
      cur->push_back(v4[2] << 8 | v4[3]);
      i = t.size();
      break;
    }
    if (j == i || j - i > 4) return false;
    cur->push_back((int)wcstol(t.substr(i, j - i).c_str(), NULL, 16));
    i = j;
    if (i == t.size()) break;
    if (t[i] != L':') return false;
    if (i + 1 < t.size() && t[i + 1] == L':') {
      if (gap) return false;
      gap = true;
      cur = &tail;
      i += 2;
    } else {
      i++;
      if (i == t.size()) return false;
    }
  }
  size_t n = head.size() + tail.size();
  if (n > 8 || (!gap && n != 8) || (gap && n == 8)) return false;
  std::vector<int> all = head;
  all.resize(8 - tail.size(), 0);
  all.insert(all.end(), tail.begin(), tail.end());
  for (int k = 0; k < 8; k++) {
    out[2 * k] = (BYTE)(all[k] >> 8);
    out[2 * k + 1] = (BYTE)all[k];
  }
  return true;
}

bool ParseNumber(const std::wstring& t, unsigned most, unsigned* out) {
  if (t.empty() || t.size() > 5) return false;
  unsigned v = 0;
  for (wchar_t c : t) {
    if (!iswdigit(c)) return false;
    v = v * 10 + (c - L'0');
  }
  if (v > most) return false;
  *out = v;
  return true;
}

bool IsHostName(const std::wstring& t) {
  if (t.empty() || t.size() > 255) return false;
  size_t label = 0;
  bool letter = false;
  for (size_t i = 0; i < t.size(); i++) {
    wchar_t c = t[i];
    if (c == L'.') {
      if (label == 0) return false;
      label = 0;
      continue;
    }
    if (!(iswalnum(c) || c == L'-' || c == L'_')) return false;
    if (!iswdigit(c)) letter = true;
    if (++label > 63) return false;
  }
  return letter;  // all digits and dots is an IPv4 address, or nothing
}

void Put16(BYTE* p, unsigned v) {
  p[0] = (BYTE)v;
  p[1] = (BYTE)(v >> 8);
}

// The text as an address of an allowed kind: the format in info, and the
// port and prefix length; an error code otherwise.
DWORD ParseAddress(std::wstring t, DWORD allow, NcAddress* nc) {
  while (!t.empty() && iswspace(t[0])) t.erase(0, 1);
  while (!t.empty() && iswspace(t.back())) t.pop_back();
  if (t.empty()) return ERROR_INVALID_PARAMETER;
  BYTE info[4 + 2 * (kNameChars + kPortChars)] = {};
  unsigned port = 0, prefix = 0;
  DWORD kind = 0;
  std::wstring host = t, portText;
  // A port after the host: "[v6]:p", "v4:p", "name:p".
  if (t[0] == L'[') {
    size_t close = t.find(L']');
    if (close == std::wstring::npos) return ERROR_INVALID_PARAMETER;
    host = t.substr(1, close - 1);
    std::wstring rest = t.substr(close + 1);
    if (!rest.empty()) {
      if (rest[0] != L':' || !ParseNumber(rest.substr(1), 65535, &port)) return ERROR_INVALID_PARAMETER;
      portText = rest.substr(1);
    }
  } else if (std::count(t.begin(), t.end(), L':') == 1) {
    size_t colon = t.find(L':');
    host = t.substr(0, colon);
    if (!ParseNumber(t.substr(colon + 1), 65535, &port)) return ERROR_INVALID_PARAMETER;
    portText = t.substr(colon + 1);
  }
  bool hasPort = !portText.empty();
  // A network: "address/length".
  size_t slash = host.find(L'/');
  bool network = slash != std::wstring::npos;
  std::wstring addr = network ? host.substr(0, slash) : host;
  std::wstring scope;
  size_t pct = addr.find(L'%');
  if (pct != std::wstring::npos) {
    scope = addr.substr(pct + 1);
    addr = addr.substr(0, pct);
  }
  BYTE v4[4], v6[16];
  if (scope.empty() && ParseIpv4(addr, v4)) {
    if (network) {
      if (hasPort || !ParseNumber(host.substr(slash + 1), 32, &prefix)) return ERROR_INVALID_PARAMETER;
      kind = kIpv4Network;
    } else {
      kind = hasPort ? kIpv4Service : kIpv4Address;
    }
    *(DWORD*)info = 2;  // NET_ADDRESS_IPV4
    Put16(info + 4, 2);  // AF_INET
    info[6] = (BYTE)(port >> 8);
    info[7] = (BYTE)port;
    memcpy(info + 8, v4, 4);
  } else if (ParseIpv6(addr, v6)) {
    unsigned scopeId = 0;
    if (!scope.empty() && !ParseNumber(scope, 0xFFFF, &scopeId)) return ERROR_INVALID_PARAMETER;
    if (network) {
      if (hasPort || !scope.empty() || !ParseNumber(host.substr(slash + 1), 128, &prefix)) return ERROR_INVALID_PARAMETER;
      kind = kIpv6Network;
    } else if (hasPort) {
      kind = scope.empty() ? (kIpv6Service | kIpv6ServiceNoScope) : kIpv6Service;
    } else {
      kind = scope.empty() ? (kIpv6Address | kIpv6AddressNoScope) : kIpv6Address;
    }
    *(DWORD*)info = 3;  // NET_ADDRESS_IPV6
    Put16(info + 4, 23);  // AF_INET6
    info[6] = (BYTE)(port >> 8);
    info[7] = (BYTE)port;
    memcpy(info + 12, v6, 16);
    memcpy(info + 28, &scopeId, 4);
  } else if (!network && scope.empty() && IsHostName(addr)) {
    kind = hasPort ? kNamedService : kNamedAddress;
    *(DWORD*)info = 1;  // NET_ADDRESS_DNS_NAME
    WCHAR* name = (WCHAR*)(info + 4);
    for (size_t i = 0; i < addr.size(); i++) name[i] = addr[i];
    WCHAR* portName = name + kNameChars;
    for (size_t i = 0; i < portText.size() && i < kPortChars - 1; i++) portName[i] = portText[i];
  } else {
    return ERROR_INVALID_PARAMETER;
  }
  if (!(kind & allow)) return ERROR_INVALID_PARAMETER;
  if (nc) {
    if (nc->info) memcpy(nc->info, info, sizeof info);
    nc->port = (USHORT)port;
    nc->prefix = (BYTE)prefix;
  }
  return ERROR_SUCCESS;
}

WNDPROC EditClassProc() {
  static WNDPROC proc = NULL;
  if (!proc) {
    WNDCLASSEXW wc = {sizeof wc};
    if (GetClassInfoExW(NULL, L"Edit", &wc)) proc = wc.lpfnWndProc;
  }
  return proc;
}

LRESULT CALLBACK NetAddressProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  WNDPROC edit = EditClassProc();
  switch (msg) {
    case NETADDR_SETALLOWTYPE:
      if (wp & ~(DWORD)0x3FF) return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
      SetPropW(h, kAllowProp, (HANDLE)(UINT_PTR)(wp | 0x80000000u));
      return S_OK;
    case NETADDR_GETALLOWTYPE: {
      UINT_PTR v = (UINT_PTR)GetPropW(h, kAllowProp);
      return v ? (LRESULT)(v & 0x3FF) : 0x3FF;
    }
    case NETADDR_GETADDRESS: {
      UINT_PTR v = (UINT_PTR)GetPropW(h, kAllowProp);
      DWORD allow = v ? (DWORD)(v & 0x3FF) : 0x3FF;
      int n = GetWindowTextLengthW(h);
      std::wstring text(n + 1, 0);
      GetWindowTextW(h, &text[0], n + 1);
      text.resize(wcslen(text.c_str()));
      DWORD r = ParseAddress(text, allow, (NcAddress*)lp);
      return r == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(r);
    }
    case NETADDR_DISPLAYERRORTIP: {
      EDITBALLOONTIP tip = {sizeof tip, L"Not an address",
                            L"Type an address of the kind this box takes, such as 192.168.1.10 or example.com.",
                            TTI_ERROR};
      SendMessageW(h, EM_SHOWBALLOONTIP, 0, (LPARAM)&tip);
      MessageBeep(MB_ICONEXCLAMATION);
      return S_OK;
    }
    case WM_NCDESTROY:
      RemovePropW(h, kAllowProp);
      break;
  }
  return edit ? CallWindowProcW(edit, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace winport {

void RegisterExtraClasses() {
  LPCWSTR arrow = (LPCWSTR)IDC_ARROW;
  RegisterSystemClass(WC_IPADDRESSW, IpProc, CS_GLOBALCLASS | CS_DBLCLKS, sizeof(void*), NULL, (LPCWSTR)IDC_IBEAM);
  RegisterSystemClass(WC_COMBOBOXEXW, ComboExProc, CS_GLOBALCLASS | CS_DBLCLKS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(ANIMATE_CLASSW, AnimateProc, CS_GLOBALCLASS | CS_DBLCLKS, sizeof(void*), NULL, arrow);
  RegisterSystemClass(L"msctls_netaddress", NetAddressProc, CS_GLOBALCLASS | CS_DBLCLKS | CS_PARENTDC, sizeof(void*),
                      NULL, (LPCWSTR)IDC_IBEAM);
}

}  // namespace winport

// The class is always there; shell32's call to make sure of it.
extern "C" BOOL WINAPI InitNetworkAddressControl(void) {
  winport::RegisterSystemClasses();
  return TRUE;
}
