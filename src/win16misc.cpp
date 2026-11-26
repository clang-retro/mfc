// win16misc.cpp - the hot key, IP address, pager and link control
// windows for 16-bit Windows, which has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// Each window does the messages of comctl32.dll in ANSI and sends its
// notifications to its parent:
//
//   hot key     HKM_ messages and EN_CHANGE; the key names come from
//               GetKeyNameText.
//   IP address  four edit controls, the IPM_ messages, IPN_FIELDCHANGED
//               and EN_CHANGE.
//   pager       the PGM_ messages, PGN_CALCSIZE and PGN_SCROLL; the
//               notifications of the child go on to the parent.
//   link        the <a> markup, LM_ messages, NM_CLICK and NM_RETURN with
//               an NMLINK. The IDs and URLs of an LITEM are Unicode; the
//               window converts them one character at a time.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

/// Sends WM_COMMAND with a notification code to the parent of a control,
/// packed as 16-bit Windows packs it: the ID in wParam, the window in
/// the low word and the code in the high word of lParam.
void SendCommand(HWND hWnd, UINT nCode) {
  ::SendMessage(::GetParent(hWnd), WM_COMMAND, (WPARAM)::GetDlgCtrlID(hWnd),
                MAKELPARAM((WORD)(UINT)hWnd, nCode));
}

/// Gives the font of a control, or the stock font when it has none.
HFONT CtlFont(HFONT hFont) {
  return hFont != NULL ? hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

// =====================================================================
// The hot key control

// The data of a hot key control.
struct W16HotKey {
  HWND hwnd;
  BYTE vk;           // the virtual key, or 0
  BYTE mods;         // the HOTKEYF_ flags of the key
  WORD wInvalid;     // HKM_SETRULES: the HKCOMB_ flags that are not valid
  WORD wDefaultMods; // the HOTKEYF_ flags that replace a combination that is not valid
  HFONT hFont;
  BOOL bFocus;
  /// Starts with no key and no rules.
  W16HotKey()
      : hwnd(NULL), vk(0), mods(0), wInvalid(0), wDefaultMods(0), hFont(NULL), bFocus(FALSE) {}
};

/// Gives the HOTKEYF_ flags of the modifier keys that are down now.
BYTE HkModsNow() {
  BYTE mods = 0;
  if (::GetKeyState(VK_SHIFT) < 0) mods |= HOTKEYF_SHIFT;
  if (::GetKeyState(VK_CONTROL) < 0) mods |= HOTKEYF_CONTROL;
  if (::GetKeyState(VK_MENU) < 0) mods |= HOTKEYF_ALT;
  return mods;
}

/// Gives the HKCOMB_ flag of a combination of modifiers.
WORD HkComb(BYTE mods) {
  switch (mods & (HOTKEYF_SHIFT | HOTKEYF_CONTROL | HOTKEYF_ALT)) {
    case 0:
      return HKCOMB_NONE;
    case HOTKEYF_SHIFT:
      return HKCOMB_S;
    case HOTKEYF_CONTROL:
      return HKCOMB_C;
    case HOTKEYF_ALT:
      return HKCOMB_A;
    case HOTKEYF_SHIFT | HOTKEYF_CONTROL:
      return HKCOMB_SC;
    case HOTKEYF_SHIFT | HOTKEYF_ALT:
      return HKCOMB_SA;
    case HOTKEYF_CONTROL | HOTKEYF_ALT:
      return HKCOMB_CA;
  }
  return HKCOMB_SCA;
}

/// Gives the text of a key and its modifiers, as "Ctrl + Shift + A", or
/// "None" for no key.
CString HkText(BYTE vk, BYTE mods) {
  CString str;
  if (mods & HOTKEYF_CONTROL) str += "Ctrl + ";
  if (mods & HOTKEYF_SHIFT) str += "Shift + ";
  if (mods & HOTKEYF_ALT) str += "Alt + ";
  if (vk != 0) {
    char sz[64];
    LONG lScan = (LONG)::MapVirtualKey(vk, 0) << 16;
    if (mods & HOTKEYF_EXT) lScan |= 0x01000000L;
    if (::GetKeyNameText(lScan, sz, sizeof(sz)) > 0)
      str += sz;
    else
      str += (char)vk;
  } else if (str.IsEmpty()) {
    str = "None";
  }
  return str;
}

/// Draws a hot key control: a white field with the text and the caret.
void HkPaint(W16HotKey* h, HDC hdc) {
  RECT rc;
  ::GetClientRect(h->hwnd, &rc);
  BOOL bEnabled = ::IsWindowEnabled(h->hwnd);
  FillColor(hdc, rc, ::GetSysColor(bEnabled ? COLOR_WINDOW : COLOR_BTNFACE));
  BYTE mods = h->vk != 0 ? h->mods : (BYTE)(h->bFocus ? HkModsNow() : 0);
  CString str = HkText(h->vk, mods);
  HGDIOBJ hOld = ::SelectObject(hdc, CtlFont(h->hFont));
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, ::GetSysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  ::TextOut(hdc, 2, (rc.bottom - tm.tmHeight) / 2, str, str.GetLength());
  SIZE size;
  ::GetTextExtentPoint32(hdc, str, str.GetLength(), &size);
  ::SelectObject(hdc, hOld);
  if (h->bFocus) ::SetCaretPos(2 + size.cx, (rc.bottom - tm.tmHeight) / 2);
}

/// Sets the key of a hot key control: the rules replace a combination
/// that is not valid, the window draws it again, and the parent gets
/// EN_CHANGE when bNotify is set.
void HkSet(W16HotKey* h, BYTE vk, BYTE mods, BOOL bNotify) {
  if (vk != 0 && (HkComb(mods) & h->wInvalid))
    mods = (BYTE)((mods & HOTKEYF_EXT) | (h->wDefaultMods & ~HOTKEYF_EXT));
  h->vk = vk;
  h->mods = vk != 0 ? mods : 0;
  ::InvalidateRect(h->hwnd, NULL, TRUE);
  if (bNotify) SendCommand(h->hwnd, EN_CHANGE);
}

/// The window procedure of the hot key class.
LRESULT CALLBACK W16HotKeyProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16HotKey* h = StateOf<W16HotKey>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16HotKey>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (h == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, h);
      break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      HkPaint(h, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETFOCUS:
      h->bFocus = TRUE;
      ::CreateCaret(hWnd, NULL, 1, FontHeight(hWnd, CtlFont(h->hFont)));
      ::InvalidateRect(hWnd, NULL, TRUE);
      ::UpdateWindow(hWnd);
      ::ShowCaret(hWnd);
      return 0;
    case WM_KILLFOCUS:
      h->bFocus = FALSE;
      ::DestroyCaret();
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_LBUTTONDOWN:
      ::SetFocus(hWnd);
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      UINT vk = (UINT)wParam;
      if (vk == VK_TAB || vk == VK_RETURN) break;  // the dialog keys
      BYTE mods = HkModsNow();
      if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
        if (h->vk == 0) ::InvalidateRect(hWnd, NULL, TRUE);
        return 0;
      }
      if ((vk == VK_BACK || vk == VK_DELETE || vk == VK_ESCAPE || vk == VK_SPACE) && mods == 0) {
        HkSet(h, 0, 0, TRUE);
        return 0;
      }
      if (lParam & 0x01000000L) mods |= HOTKEYF_EXT;
      HkSet(h, (BYTE)vk, mods, TRUE);
      return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
      if (h->vk == 0) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_CHAR:
    case WM_SYSCHAR:
      return 0;
    case HKM_SETHOTKEY:
      HkSet(h, LOBYTE(wParam), HIBYTE(wParam), FALSE);
      return 0;
    case HKM_GETHOTKEY:
      return MAKEWORD(h->vk, h->mods);
    case HKM_SETRULES:
      h->wInvalid = (WORD)wParam;
      h->wDefaultMods = LOWORD(lParam);
      return 0;
    case WM_SETFONT:
      h->hFont = (HFONT)wParam;
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)h->hFont;
    case WM_ENABLE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The IP address control

// The data of an IP address control.
struct W16IPAddr {
  HWND hwnd;
  HWND hwndEdit[4];   // the four fields
  BYTE lo[4], hi[4];  // the range of each field
  WNDPROC pfnEdit;    // the procedure of the edit controls before the control
  HFONT hFont;
  BOOL bHasFocus;     // a field has the focus
  BOOL bInSet;        // the control changes the text of a field itself
  /// Starts with no fields and the range 0 to 255 in each.
  W16IPAddr() : hwnd(NULL), pfnEdit(NULL), hFont(NULL), bHasFocus(FALSE), bInSet(FALSE) {
    for (int i = 0; i < 4; i++) {
      hwndEdit[i] = NULL;
      lo[i] = 0;
      hi[i] = 255;
    }
  }
};

/// Gives the field of an edit control of an IP address control, or -1.
int IpField(W16IPAddr* p, HWND hwndEdit) {
  for (int i = 0; i < 4; i++)
    if (p->hwndEdit[i] == hwndEdit) return i;
  return -1;
}

/// Gives the value of a field, or -1 when it is blank.
int IpValue(W16IPAddr* p, int i) {
  char sz[8];
  if (::GetWindowText(p->hwndEdit[i], sz, sizeof(sz)) <= 0) return -1;
  int n = 0;
  for (const char* q = sz; *q != '\0'; q++)
    if (*q >= '0' && *q <= '9') n = n * 10 + (*q - '0');
  return n;
}

/// Sets the text of a field to a number, or to blank for -1.
void IpSetValue(W16IPAddr* p, int i, int nValue) {
  char sz[8];
  sz[0] = '\0';
  if (nValue >= 0) wsprintf(sz, "%d", nValue);
  p->bInSet = TRUE;
  ::SetWindowText(p->hwndEdit[i], sz);
  p->bInSet = FALSE;
}

/// Puts the value of a field in its range when it leaves the field. The
/// parent gets IPN_FIELDCHANGED and can change the value.
void IpCommitField(W16IPAddr* p, int i) {
  int nValue = IpValue(p, i);
  NMIPADDRESS nm;
  memset(&nm, 0, sizeof(nm));
  nm.iField = i;
  nm.iValue = nValue;
  SendNotify(p->hwnd, IPN_FIELDCHANGED, &nm.hdr);
  int nNew = nm.iValue;
  if (nNew >= 0) nNew = (int)ClampL(nNew, p->lo[i], p->hi[i]);
  if (nNew != nValue) IpSetValue(p, i, nNew);
}

/// Moves the focus to a field and selects its text.
void IpFocusField(W16IPAddr* p, int i) {
  if (i < 0 || i > 3) return;
  ::SetFocus(p->hwndEdit[i]);
  ::SendMessage(p->hwndEdit[i], EM_SETSEL, 0, MAKELPARAM(0, -1));
}

/// The procedure of a field of an IP address control: digits only, three
/// digits or a dot move to the next field, and the arrow keys and
/// Backspace cross the fields.
LRESULT CALLBACK IpEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16IPAddr* p = StateOf<W16IPAddr>(::GetParent(hWnd));
  if (p == NULL || p->pfnEdit == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  int i = IpField(p, hWnd);
  switch (nMsg) {
    case WM_CHAR: {
      char ch = (char)wParam;
      if (ch == '.' || ch == ' ') {
        if (::GetWindowTextLength(hWnd) > 0) IpFocusField(p, i + 1);
        return 0;
      }
      if (ch == '\b') {
        DWORD dwSel = (DWORD)::SendMessage(hWnd, EM_GETSEL, 0, 0);
        if (LOWORD(dwSel) == 0 && HIWORD(dwSel) == 0 && i > 0) {
          IpFocusField(p, i - 1);
          ::SendMessage(p->hwndEdit[i - 1], EM_SETSEL, 0, MAKELPARAM(3, 3));
          return 0;
        }
        break;
      }
      if (ch < '0' || ch > '9') {
        ::MessageBeep(0);
        return 0;
      }
      LRESULT lResult = ::CallWindowProc(p->pfnEdit, hWnd, nMsg, wParam, lParam);
      DWORD dwSel = (DWORD)::SendMessage(hWnd, EM_GETSEL, 0, 0);
      if (::GetWindowTextLength(hWnd) >= 3 && LOWORD(dwSel) >= 3 && i < 3) IpFocusField(p, i + 1);
      return lResult;
    }
    case WM_KEYDOWN: {
      DWORD dwSel = (DWORD)::SendMessage(hWnd, EM_GETSEL, 0, 0);
      int nLen = ::GetWindowTextLength(hWnd);
      if (wParam == VK_LEFT && LOWORD(dwSel) == 0 && HIWORD(dwSel) == 0 && i > 0) {
        IpFocusField(p, i - 1);
        return 0;
      }
      if (wParam == VK_RIGHT && LOWORD(dwSel) >= nLen && i < 3) {
        IpFocusField(p, i + 1);
        ::SendMessage(p->hwndEdit[i + 1], EM_SETSEL, 0, 0);
        return 0;
      }
      if (wParam == VK_HOME && ::GetKeyState(VK_CONTROL) < 0) {
        IpFocusField(p, 0);
        return 0;
      }
      if (wParam == VK_END && ::GetKeyState(VK_CONTROL) < 0) {
        IpFocusField(p, 3);
        return 0;
      }
      break;
    }
    case WM_SETFOCUS:
      if (!p->bHasFocus) {
        p->bHasFocus = TRUE;
        SendCommand(p->hwnd, EN_SETFOCUS);
      }
      break;
    case WM_KILLFOCUS: {
      LRESULT lResult = ::CallWindowProc(p->pfnEdit, hWnd, nMsg, wParam, lParam);
      IpCommitField(p, i);
      HWND hwndNew = (HWND)wParam;
      if (IpField(p, hwndNew) < 0) {
        p->bHasFocus = FALSE;
        SendCommand(p->hwnd, EN_KILLFOCUS);
      }
      return lResult;
    }
  }
  return ::CallWindowProc(p->pfnEdit, hWnd, nMsg, wParam, lParam);
}

/// Gives the width of the dot between two fields.
int IpDotWidth(W16IPAddr* p) {
  HDC hdc = ::GetDC(p->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, CtlFont(p->hFont));
  SIZE size;
  ::GetTextExtentPoint32(hdc, ".", 1, &size);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(p->hwnd, hdc);
  return size.cx + 4;
}

/// Places the four fields in the window, with room for the dots.
void IpLayout(W16IPAddr* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  int cxDot = IpDotWidth(p);
  int cxField = (rc.right - 3 * cxDot) / 4;
  if (cxField < 4) cxField = 4;
  int cy = FontHeight(p->hwnd, CtlFont(p->hFont)) + 2;
  if (cy > rc.bottom) cy = rc.bottom;
  int y = (rc.bottom - cy) / 2;
  for (int i = 0; i < 4; i++)
    if (p->hwndEdit[i] != NULL)
      ::MoveWindow(p->hwndEdit[i], i * (cxField + cxDot), y, cxField, cy, TRUE);
}

/// Makes the four fields of an IP address control and subclasses them.
BOOL IpCreate(W16IPAddr* p) {
  HINSTANCE hInst = (HINSTANCE)::GetWindowWord(p->hwnd, GWW_HINSTANCE);
  for (int i = 0; i < 4; i++) {
    p->hwndEdit[i] = ::CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | ES_LEFT, 0, 0, 0, 0,
                                    p->hwnd, (HMENU)(UINT)(i + 1), hInst, NULL);
    if (p->hwndEdit[i] == NULL) return FALSE;
    ::SendMessage(p->hwndEdit[i], EM_LIMITTEXT, 3, 0);
    WNDPROC pfnOld = (WNDPROC)::SetWindowLong(p->hwndEdit[i], GWL_WNDPROC, (LONG)(WNDPROC)IpEditProc);
    if (p->pfnEdit == NULL) p->pfnEdit = pfnOld;
  }
  IpLayout(p);
  return TRUE;
}

/// The window procedure of the IP address class.
LRESULT CALLBACK W16IPAddrProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16IPAddr* p = StateOf<W16IPAddr>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16IPAddr>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, p);
      break;
    case WM_CREATE:
      return IpCreate(p) ? 0 : -1;
    case WM_SIZE:
      IpLayout(p);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      BOOL bEnabled = ::IsWindowEnabled(hWnd);
      FillColor(hdc, rc, ::GetSysColor(bEnabled ? COLOR_WINDOW : COLOR_BTNFACE));
      HGDIOBJ hOld = ::SelectObject(hdc, CtlFont(p->hFont));
      ::SetBkMode(hdc, TRANSPARENT);
      ::SetTextColor(hdc, ::GetSysColor(bEnabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
      TEXTMETRIC tm;
      ::GetTextMetrics(hdc, &tm);
      for (int i = 0; i < 3; i++) {
        RECT rcEdit;
        ::GetWindowRect(p->hwndEdit[i], &rcEdit);
        POINT pt = {rcEdit.right, rcEdit.top};
        ::ScreenToClient(hWnd, &pt);
        ::TextOut(hdc, pt.x + 2, (rc.bottom - tm.tmHeight) / 2, ".", 1);
      }
      ::SelectObject(hdc, hOld);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_CTLCOLOR:
      // The fields have the colors of the control.
      if (HIWORD(lParam) == CTLCOLOR_EDIT && !::IsWindowEnabled(hWnd)) {
        ::SetBkColor((HDC)wParam, ::GetSysColor(COLOR_BTNFACE));
        return (LRESULT)(UINT)::GetStockObject(LTGRAY_BRUSH);
      }
      break;
    case WM_COMMAND:
      if (HIWORD(lParam) == EN_CHANGE && !p->bInSet) SendCommand(hWnd, EN_CHANGE);
      return 0;
    case WM_SETFOCUS:
      IpFocusField(p, 0);
      return 0;
    case WM_ENABLE:
      for (int i = 0; i < 4; i++) ::EnableWindow(p->hwndEdit[i], (BOOL)wParam);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      p->hFont = (HFONT)wParam;
      for (int i = 0; i < 4; i++) ::SendMessage(p->hwndEdit[i], WM_SETFONT, wParam, lParam);
      IpLayout(p);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)p->hFont;
    case IPM_CLEARADDRESS:
      for (int i = 0; i < 4; i++) IpSetValue(p, i, -1);
      return 0;
    case IPM_SETADDRESS: {
      DWORD dw = (DWORD)lParam;
      IpSetValue(p, 0, (int)FIRST_IPADDRESS(dw));
      IpSetValue(p, 1, (int)SECOND_IPADDRESS(dw));
      IpSetValue(p, 2, (int)THIRD_IPADDRESS(dw));
      IpSetValue(p, 3, (int)FOURTH_IPADDRESS(dw));
      return 0;
    }
    case IPM_GETADDRESS: {
      DWORD dw = 0;
      int nFilled = 0;
      for (int i = 0; i < 4; i++) {
        int n = IpValue(p, i);
        if (n >= 0) nFilled++;
        dw = (dw << 8) | (DWORD)(n >= 0 ? (n > 255 ? 255 : n) : 0);
      }
      if (lParam != 0) *(DWORD FAR*)lParam = dw;
      return nFilled;
    }
    case IPM_SETRANGE:
      if (wParam > 3) return FALSE;
      p->lo[wParam] = LOBYTE(LOWORD(lParam));
      p->hi[wParam] = HIBYTE(LOWORD(lParam));
      return TRUE;
    case IPM_SETFOCUS: {
      int i = (int)wParam;
      if (i > 3) {
        // The first blank field, or the first field.
        i = 0;
        for (int k = 0; k < 4; k++)
          if (IpValue(p, k) < 0) {
            i = k;
            break;
          }
      }
      IpFocusField(p, i);
      return 0;
    }
    case IPM_ISBLANK:
      for (int i = 0; i < 4; i++)
        if (IpValue(p, i) >= 0) return FALSE;
      return TRUE;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The pager

// The timer of a pressed pager button, which repeats the scroll.
const UINT kPagerTimer = 1;

// The data of a pager.
struct W16Pager {
  HWND hwnd;
  HWND hwndChild;
  int nPos;          // the scroll position of the child
  int nChildLen;     // the width (PGS_HORZ) or height of the child
  int nBorder;
  int nButtonSize;
  COLORREF clrBk;
  BOOL bForward;     // PGM_FORWARDMOUSE
  int iPressed;      // the button that is down (PGB_), or -1
  /// Starts with no child, buttons of 12 pixels and the face color.
  W16Pager()
      : hwnd(NULL), hwndChild(NULL), nPos(0), nChildLen(0), nBorder(0), nButtonSize(12),
        clrBk(CLR_DEFAULT), bForward(FALSE), iPressed(-1) {}
};

/// Gives TRUE for a horizontal pager.
BOOL PgHorz(W16Pager* p) { return (StyleOf(p->hwnd) & PGS_HORZ) != 0; }

/// Gives the length of the client area of a pager in its direction.
int PgClientLen(W16Pager* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  return PgHorz(p) ? rc.right : rc.bottom;
}

/// Gives TRUE when a button of a pager shows: the top or left button
/// when the child can scroll back, the other when it can scroll on.
BOOL PgButtonVisible(W16Pager* p, int iButton) {
  int nClient = PgClientLen(p) - 2 * p->nBorder;
  if (p->nChildLen <= nClient) return FALSE;
  if (iButton == PGB_TOPORLEFT) return p->nPos > 0;
  int nView = nClient - (p->nPos > 0 ? p->nButtonSize : 0) - p->nButtonSize;
  return p->nPos + nView < p->nChildLen;
}

/// Gives the largest scroll position of a pager.
int PgMaxPos(W16Pager* p) {
  int nView = PgClientLen(p) - 2 * p->nBorder - 2 * p->nButtonSize;
  int nMax = p->nChildLen - nView;
  return nMax > 0 ? nMax : 0;
}

/// Gives the rectangle of a button of a pager.
RECT PgButtonRect(W16Pager* p, int iButton) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  if (PgHorz(p)) {
    if (iButton == PGB_TOPORLEFT)
      rc.right = rc.left + p->nButtonSize;
    else
      rc.left = rc.right - p->nButtonSize;
  } else {
    if (iButton == PGB_TOPORLEFT)
      rc.bottom = rc.top + p->nButtonSize;
    else
      rc.top = rc.bottom - p->nButtonSize;
  }
  return rc;
}

/// Places the child of a pager at its scroll position, between the
/// buttons that show.
void PgLayout(W16Pager* p) {
  if (p->hwndChild == NULL || !::IsWindow(p->hwndChild)) return;
  p->nPos = (int)ClampL(p->nPos, 0, PgMaxPos(p));
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  int nStart = p->nBorder + (PgButtonVisible(p, PGB_TOPORLEFT) ? p->nButtonSize : 0);
  BOOL bScroll = p->nChildLen > PgClientLen(p) - 2 * p->nBorder;
  int nOffset = bScroll ? nStart - p->nPos : p->nBorder;
  if (PgHorz(p))
    ::SetWindowPos(p->hwndChild, NULL, nOffset, p->nBorder, p->nChildLen,
                   rc.bottom - 2 * p->nBorder, SWP_NOZORDER | SWP_NOACTIVATE);
  else
    ::SetWindowPos(p->hwndChild, NULL, p->nBorder, nOffset, rc.right - 2 * p->nBorder,
                   p->nChildLen, SWP_NOZORDER | SWP_NOACTIVATE);
  ::InvalidateRect(p->hwnd, NULL, TRUE);
}

/// Asks the parent for the size of the child (PGN_CALCSIZE) and places
/// the child again.
void PgRecalc(W16Pager* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  NMPGCALCSIZE nm;
  memset(&nm, 0, sizeof(nm));
  nm.dwFlag = PgHorz(p) ? PGF_CALCWIDTH : PGF_CALCHEIGHT;
  nm.iWidth = rc.right;
  nm.iHeight = rc.bottom;
  if (p->hwndChild != NULL) {
    RECT rcChild;
    ::GetWindowRect(p->hwndChild, &rcChild);
    nm.iWidth = rcChild.right - rcChild.left;
    nm.iHeight = rcChild.bottom - rcChild.top;
  }
  SendNotify(p->hwnd, PGN_CALCSIZE, &nm.hdr);
  p->nChildLen = PgHorz(p) ? nm.iWidth : nm.iHeight;
  PgLayout(p);
}

/// Scrolls a pager one step for a button: the parent gets PGN_SCROLL and
/// can change the step.
void PgScroll(W16Pager* p, int iButton) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  NMPGSCROLL nm;
  memset(&nm, 0, sizeof(nm));
  nm.fwKeys = 0;
  if (::GetKeyState(VK_SHIFT) < 0) nm.fwKeys |= MK_SHIFT;
  if (::GetKeyState(VK_CONTROL) < 0) nm.fwKeys |= MK_CONTROL;
  nm.rcParent = rc;
  BOOL bBack = iButton == PGB_TOPORLEFT;
  nm.iDir = PgHorz(p) ? (bBack ? PGF_SCROLLLEFT : PGF_SCROLLRIGHT)
                      : (bBack ? PGF_SCROLLUP : PGF_SCROLLDOWN);
  nm.iXpos = PgHorz(p) ? p->nPos : 0;
  nm.iYpos = PgHorz(p) ? 0 : p->nPos;
  nm.iScroll = 2 * p->nButtonSize;
  SendNotify(p->hwnd, PGN_SCROLL, &nm.hdr);
  p->nPos += bBack ? -nm.iScroll : nm.iScroll;
  PgLayout(p);
}

/// Gives the PGF_ state of a button of a pager.
DWORD PgButtonState(W16Pager* p, int iButton) {
  if (!PgButtonVisible(p, iButton)) return PGF_INVISIBLE;
  return p->iPressed == iButton ? (PGF_NORMAL | PGF_DEPRESSED) : PGF_NORMAL;
}

/// Gives the button of a pager at a point, or -1.
int PgHitButton(W16Pager* p, POINT pt) {
  for (int i = PGB_TOPORLEFT; i <= PGB_BOTTOMORRIGHT; i++) {
    RECT rc = PgButtonRect(p, i);
    if (PgButtonVisible(p, i) && ::PtInRect(&rc, pt)) return i;
  }
  return -1;
}

/// The window procedure of the pager class.
LRESULT CALLBACK W16PagerProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Pager* p = StateOf<W16Pager>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Pager>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, p);
      break;
    case WM_SIZE:
      PgRecalc(p);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      FillColor(hdc, rc, p->clrBk == CLR_DEFAULT ? ::GetSysColor(COLOR_BTNFACE) : p->clrBk);
      for (int i = PGB_TOPORLEFT; i <= PGB_BOTTOMORRIGHT; i++) {
        if (!PgButtonVisible(p, i)) continue;
        RECT rcButton = PgButtonRect(p, i);
        BOOL bPushed = p->iPressed == i;
        DrawButtonFace(hdc, rcButton, bPushed);
        int nDir = PgHorz(p) ? (i == PGB_TOPORLEFT ? 2 : 3) : (i == PGB_TOPORLEFT ? 0 : 1);
        DrawArrow(hdc, rcButton, nDir, ::IsWindowEnabled(hWnd), bPushed);
      }
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      int i = PgHitButton(p, pt);
      if (i < 0) return 0;
      p->iPressed = i;
      ::SetCapture(hWnd);
      PgScroll(p, i);
      ::SetTimer(hWnd, kPagerTimer, 300, NULL);
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
      if (p->iPressed >= 0) {
        p->iPressed = -1;
        ::KillTimer(hWnd, kPagerTimer);
        if (::GetCapture() == hWnd) ::ReleaseCapture();
        ::InvalidateRect(hWnd, NULL, TRUE);
      }
      return 0;
    case WM_TIMER:
      if (wParam == kPagerTimer && p->iPressed >= 0) {
        ::SetTimer(hWnd, kPagerTimer, 50, NULL);
        POINT pt;
        ::GetCursorPos(&pt);
        ::ScreenToClient(hWnd, &pt);
        if (PgHitButton(p, pt) == p->iPressed) PgScroll(p, p->iPressed);
        if (!PgButtonVisible(p, p->iPressed)) {
          p->iPressed = -1;
          ::KillTimer(hWnd, kPagerTimer);
          if (::GetCapture() == hWnd) ::ReleaseCapture();
        }
      }
      return 0;
    case WM_NOTIFY:
    case WM_COMMAND:
      // The notifications of the child go to the parent of the pager.
      return ::SendMessage(::GetParent(hWnd), nMsg, wParam, lParam);
    case PGM_SETCHILD:
      p->hwndChild = (HWND)(UINT)lParam;
      p->nPos = 0;
      PgRecalc(p);
      return 0;
    case PGM_RECALCSIZE:
      PgRecalc(p);
      return 0;
    case PGM_FORWARDMOUSE:
      p->bForward = wParam != 0;
      return 0;
    case PGM_SETBKCOLOR: {
      COLORREF clrOld = p->clrBk;
      p->clrBk = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)clrOld;
    }
    case PGM_GETBKCOLOR:
      return (LRESULT)p->clrBk;
    case PGM_SETBORDER: {
      int nOld = p->nBorder;
      p->nBorder = (int)(short)LOWORD(lParam) < 0 ? 0 : (int)(short)LOWORD(lParam);
      PgLayout(p);
      return nOld;
    }
    case PGM_GETBORDER:
      return p->nBorder;
    case PGM_SETPOS:
      p->nPos = (int)(short)LOWORD(lParam);
      PgLayout(p);
      return 0;
    case PGM_GETPOS:
      return p->nPos;
    case PGM_SETBUTTONSIZE: {
      int nOld = p->nButtonSize;
      int n = (int)(short)LOWORD(lParam);
      p->nButtonSize = n < 4 ? 4 : n;
      PgLayout(p);
      return nOld;
    }
    case PGM_GETBUTTONSIZE:
      return p->nButtonSize;
    case PGM_GETBUTTONSTATE:
      return (LRESULT)PgButtonState(p, (int)lParam);
    case PGM_GETDROPTARGET:
      if (lParam != 0) *(void FAR* FAR*)lParam = NULL;
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The link control

// One link of a link control.
struct W16Link {
  CString id, url;
  UINT state;
  /// Starts enabled, with no ID and no URL.
  W16Link() : state(LIS_ENABLED) {}
};

// A run of text: plain text, or the text of a link.
struct W16LinkRun {
  CString text;
  int iLink;  // the link, or -1 for plain text
};

// A part of a run on one line, after the layout.
struct W16LinkBox {
  RECT rc;
  int iRun;
  int iStart, nLen;  // the characters of the run in the box
};

// The data of a link control.
struct W16LinkCtl {
  HWND hwnd;
  CPtrArray links;  // W16Link*
  CPtrArray runs;   // W16LinkRun*
  CArray<W16LinkBox, const W16LinkBox&> boxes;
  int iFocus;       // the link with the focus, or -1
  int iDown;        // the link under the pressed button, or -1
  HFONT hFont, hLinkFont;
  BOOL bFocus;
  /// Starts with no text and no links.
  W16LinkCtl() : hwnd(NULL), iFocus(-1), iDown(-1), hFont(NULL), hLinkFont(NULL), bFocus(FALSE) {}
  /// Deletes the links, the runs and the font of the links.
  ~W16LinkCtl() {
    Clear();
    if (hLinkFont != NULL) ::DeleteObject(hLinkFont);
  }
  /// Deletes the links and the runs.
  void Clear() {
    for (int i = 0; i < links.GetSize(); i++) delete (W16Link*)links[i];
    for (int i = 0; i < runs.GetSize(); i++) delete (W16LinkRun*)runs[i];
    links.RemoveAll();
    runs.RemoveAll();
    boxes.RemoveAll();
  }
};

/// Gives the link i of a link control, or NULL.
W16Link* LkLink(W16LinkCtl* p, int i) {
  return i >= 0 && i < p->links.GetSize() ? (W16Link*)p->links[i] : NULL;
}

/// Gives the run i of a link control.
W16LinkRun* LkRun(W16LinkCtl* p, int i) { return (W16LinkRun*)p->runs[i]; }

/// Gives the font of the links: the font of the control, underlined.
HFONT LkLinkFont(W16LinkCtl* p) {
  if (p->hLinkFont == NULL) {
    LOGFONT lf;
    if (::GetObject(CtlFont(p->hFont), sizeof(lf), &lf) != 0) {
      lf.lfUnderline = TRUE;
      p->hLinkFont = ::CreateFontIndirect(&lf);
    }
  }
  return p->hLinkFont != NULL ? p->hLinkFont : CtlFont(p->hFont);
}

/// Adds a run of text to a link control. Empty runs are not kept.
void LkAddRun(W16LinkCtl* p, const CString& str, int iLink) {
  if (str.IsEmpty()) return;
  W16LinkRun* r = new W16LinkRun;
  r->text = str;
  r->iLink = iLink;
  p->runs.Add(r);
}

/// Gives the value of an attribute (href or id) in the text of an <a>
/// tag, with or without quotes.
CString LkAttribute(const CString& strTag, LPCSTR lpszName) {
  CString strLower = strTag;
  strLower.MakeLower();
  CString strKey = lpszName;
  strKey += "=";
  int i = strLower.Find(strKey);
  if (i < 0) return CString();
  i += strKey.GetLength();
  if (i < strTag.GetLength() && (strTag[i] == '"' || strTag[i] == '\'')) {
    char chQuote = strTag[i];
    int j = i + 1;
    while (j < strTag.GetLength() && strTag[j] != chQuote) j++;
    return strTag.Mid(i + 1, j - i - 1);
  }
  int j = i;
  while (j < strTag.GetLength() && strTag[j] != ' ' && strTag[j] != '>') j++;
  return strTag.Mid(i, j - i);
}

/// Reads the markup of a link control: plain text and <a href="..."
/// id="...">text</a> links. Other text in angle brackets stays as it is.
void LkParse(W16LinkCtl* p, LPCSTR lpszMarkup) {
  p->Clear();
  p->iFocus = -1;
  CString str(lpszMarkup != NULL ? lpszMarkup : "");
  CString strPlain;
  int i = 0;
  while (i < str.GetLength()) {
    if (str[i] == '<' && i + 1 < str.GetLength() && (str[i + 1] == 'a' || str[i + 1] == 'A') &&
        i + 2 < str.GetLength() && (str[i + 2] == '>' || str[i + 2] == ' ')) {
      int iEndTag = str.Find('>', i);
      if (iEndTag < 0) break;
      CString strTag = str.Mid(i, iEndTag - i + 1);
      CString strLower = str.Mid(iEndTag + 1);
      strLower.MakeLower();
      int iClose = strLower.Find("</a>");
      if (iClose < 0) {
        strPlain += str.Mid(i);
        break;
      }
      LkAddRun(p, strPlain, -1);
      strPlain.Empty();
      W16Link* l = new W16Link;
      l->url = LkAttribute(strTag, "href");
      l->id = LkAttribute(strTag, "id");
      int iLink = (int)p->links.Add(l);
      LkAddRun(p, str.Mid(iEndTag + 1, iClose), iLink);
      i = iEndTag + 1 + iClose + 4;
      continue;
    }
    strPlain += str[i];
    i++;
  }
  LkAddRun(p, strPlain, -1);
}

/// Breaks the runs of a link control into boxes on lines no wider than
/// cx. Gives the height of the text.
int LkLayout(W16LinkCtl* p, int cx) {
  p->boxes.RemoveAll();
  HDC hdc = ::GetDC(p->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, CtlFont(p->hFont));
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  int cyLine = tm.tmHeight;
  int x = 0, y = 0;
  for (int iRun = 0; iRun < p->runs.GetSize(); iRun++) {
    W16LinkRun* r = LkRun(p, iRun);
    ::SelectObject(hdc, r->iLink >= 0 ? LkLinkFont(p) : CtlFont(p->hFont));
    int nLen = r->text.GetLength();
    int iStart = 0;
    while (iStart < nLen) {
      if (r->text[iStart] == '\n') {
        x = 0;
        y += cyLine;
        iStart++;
        continue;
      }
      // The longest part that fits, broken after a space when it can be.
      int iEnd = iStart;
      int iBreak = -1;
      int cxPart = 0;
      while (iEnd < nLen && r->text[iEnd] != '\n') {
        SIZE size;
        ::GetTextExtentPoint32(hdc, (LPCSTR)r->text + iStart, iEnd - iStart + 1, &size);
        if (x + size.cx > cx && iEnd > iStart) break;
        cxPart = size.cx;
        if (r->text[iEnd] == ' ') iBreak = iEnd + 1;
        iEnd++;
      }
      if (iEnd < nLen && r->text[iEnd] != '\n' && iBreak > iStart) {
        iEnd = iBreak;
        SIZE size;
        ::GetTextExtentPoint32(hdc, (LPCSTR)r->text + iStart, iEnd - iStart, &size);
        cxPart = size.cx;
      } else if (iEnd == iStart && x > 0) {
        // Nothing fits after the text on this line: go to the next line.
        x = 0;
        y += cyLine;
        continue;
      }
      if (iEnd == iStart) iEnd = iStart + 1;  // one character always goes
      W16LinkBox box;
      ::SetRect(&box.rc, x, y, x + cxPart, y + cyLine);
      box.iRun = iRun;
      box.iStart = iStart;
      box.nLen = iEnd - iStart;
      p->boxes.Add(box);
      x += cxPart;
      iStart = iEnd;
      if (iStart < nLen && r->text[iStart] != '\n' && x >= cx) {
        x = 0;
        y += cyLine;
      }
    }
  }
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(p->hwnd, hdc);
  return p->boxes.GetSize() > 0 ? y + cyLine : 0;
}

/// Gives the link at a point of a link control, or -1.
int LkHitTest(W16LinkCtl* p, POINT pt) {
  for (int i = 0; i < p->boxes.GetSize(); i++) {
    const W16LinkBox& box = p->boxes[i];
    int iLink = LkRun(p, box.iRun)->iLink;
    if (iLink >= 0 && ::PtInRect(&box.rc, pt)) return iLink;
  }
  return -1;
}

/// Copies a narrow string into a Unicode buffer, one character at a time.
void LkToWide(WCHAR FAR* pszDest, UINT cch, const CString& str) {
  UINT i = 0;
  for (; i < (UINT)str.GetLength() && i + 1 < cch; i++) pszDest[i] = (WCHAR)(BYTE)str[i];
  if (cch > 0) pszDest[i] = 0;
}

/// Gives the narrow string of a Unicode buffer, one character at a time.
CString LkFromWide(const WCHAR FAR* psz, UINT cch) {
  CString str;
  for (UINT i = 0; i < cch && psz[i] != 0; i++) str += (char)(psz[i] < 256 ? psz[i] : '?');
  return str;
}

/// Fills an LITEM with a link.
void LkFillItem(W16LinkCtl* p, int iLink, LITEM FAR* pItem) {
  W16Link* l = LkLink(p, iLink);
  pItem->iLink = iLink;
  if (l == NULL) return;
  pItem->state = l->state;
  if (iLink == p->iFocus && p->bFocus) pItem->state |= LIS_FOCUSED;
  LkToWide(pItem->szID, MAX_LINKID_TEXT, l->id);
  LkToWide(pItem->szUrl, L_MAX_URL_LENGTH, l->url);
}

/// Sends NM_CLICK or NM_RETURN with an NMLINK for a link. The NMLINK is
/// large, so it is not on the stack.
void LkNotify(W16LinkCtl* p, UINT nCode, int iLink) {
  NMLINK* pnm = new NMLINK;
  memset(pnm, 0, sizeof(*pnm));
  pnm->item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
  LkFillItem(p, iLink, &pnm->item);
  W16Link* l = LkLink(p, iLink);
  if (l != NULL && nCode == NM_CLICK) l->state |= LIS_VISITED;
  SendNotify(p->hwnd, nCode, &pnm->hdr);
  delete pnm;
  ::InvalidateRect(p->hwnd, NULL, TRUE);
}

/// Draws a link control: the plain text, the links underlined in blue
/// (purple when visited), and the focus rectangle of the focused link.
void LkPaint(W16LinkCtl* p, HDC hdc) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  if (!(StyleOf(p->hwnd) & LWS_TRANSPARENT)) FillColor(hdc, rc, ::GetSysColor(COLOR_BTNFACE));
  LkLayout(p, rc.right);
  BOOL bEnabled = ::IsWindowEnabled(p->hwnd);
  ::SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ hOld = ::SelectObject(hdc, CtlFont(p->hFont));
  for (int i = 0; i < p->boxes.GetSize(); i++) {
    const W16LinkBox& box = p->boxes[i];
    W16LinkRun* r = LkRun(p, box.iRun);
    W16Link* l = LkLink(p, r->iLink);
    COLORREF clr = ::GetSysColor(COLOR_BTNTEXT);
    if (!bEnabled || (l != NULL && !(l->state & LIS_ENABLED)))
      clr = ::GetSysColor(COLOR_GRAYTEXT);
    else if (l != NULL)
      clr = (l->state & LIS_VISITED) ? RGB(128, 0, 128) : RGB(0, 0, 255);
    ::SelectObject(hdc, l != NULL ? LkLinkFont(p) : CtlFont(p->hFont));
    ::SetTextColor(hdc, clr);
    ::TextOut(hdc, box.rc.left, box.rc.top, (LPCSTR)r->text + box.iStart, box.nLen);
    if (l != NULL && r->iLink == p->iFocus && p->bFocus) ::DrawFocusRect(hdc, &box.rc);
  }
  ::SelectObject(hdc, hOld);
}

/// Moves the focus to the next (or the previous) enabled link. Gives
/// FALSE when there is none in that direction.
BOOL LkMoveFocus(W16LinkCtl* p, BOOL bBack) {
  int n = (int)p->links.GetSize();
  int i = p->iFocus;
  for (;;) {
    i += bBack ? -1 : 1;
    if (i < 0 || i >= n) return FALSE;
    if (LkLink(p, i)->state & LIS_ENABLED) break;
  }
  p->iFocus = i;
  ::InvalidateRect(p->hwnd, NULL, TRUE);
  return TRUE;
}

/// The window procedure of the link class.
LRESULT CALLBACK W16LinkProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16LinkCtl* p = StateOf<W16LinkCtl>(hWnd);
  if (nMsg == WM_NCCREATE) {
    p = AttachState<W16LinkCtl>(hWnd);
    LRESULT lResult = ::DefWindowProc(hWnd, nMsg, wParam, lParam);
    const CREATESTRUCT FAR* pcs = (const CREATESTRUCT FAR*)lParam;
    LkParse(p, pcs != NULL ? pcs->lpszName : NULL);
    return lResult;
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, p);
      break;
    case WM_SETTEXT: {
      LRESULT lResult = ::DefWindowProc(hWnd, nMsg, wParam, lParam);
      LkParse(p, (LPCSTR)lParam);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return lResult;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      LkPaint(p, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFONT:
      p->hFont = (HFONT)wParam;
      if (p->hLinkFont != NULL) {
        ::DeleteObject(p->hLinkFont);
        p->hLinkFont = NULL;
      }
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)p->hFont;
    case WM_SETFOCUS:
      p->bFocus = TRUE;
      if (p->iFocus < 0) {
        BOOL bBack = ::GetKeyState(VK_SHIFT) < 0;
        p->iFocus = bBack ? (int)p->links.GetSize() : -1;
        if (!LkMoveFocus(p, bBack)) p->iFocus = -1;
      }
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_KILLFOCUS:
      p->bFocus = FALSE;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETDLGCODE: {
      // Tab stays in the control while another link follows.
      LRESULT l = DLGC_WANTCHARS;
      const MSG FAR* pMsg = (const MSG FAR*)lParam;
      if (pMsg != NULL && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB) {
        BOOL bBack = ::GetKeyState(VK_SHIFT) < 0;
        int i = p->iFocus;
        for (;;) {
          i += bBack ? -1 : 1;
          if (i < 0 || i >= p->links.GetSize()) break;
          if (LkLink(p, i)->state & LIS_ENABLED) {
            l |= DLGC_WANTTAB;
            break;
          }
        }
      }
      if (pMsg != NULL && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN &&
          !(StyleOf(hWnd) & LWS_IGNORERETURN) && p->iFocus >= 0)
        l |= DLGC_WANTALLKEYS;
      return l;
    }
    case WM_KEYDOWN:
      if (wParam == VK_TAB) {
        LkMoveFocus(p, ::GetKeyState(VK_SHIFT) < 0);
        return 0;
      }
      if (wParam == VK_RETURN && p->iFocus >= 0) {
        LkNotify(p, (StyleOf(hWnd) & LWS_IGNORERETURN) ? NM_RETURN : NM_CLICK, p->iFocus);
        return 0;
      }
      if (wParam == VK_SPACE && p->iFocus >= 0) {
        LkNotify(p, NM_CLICK, p->iFocus);
        return 0;
      }
      return 0;
    case WM_LBUTTONDOWN: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      LkLayout(p, rc.right);
      int iLink = LkHitTest(p, pt);
      W16Link* l = LkLink(p, iLink);
      if (l == NULL || !(l->state & LIS_ENABLED)) return 0;
      p->iDown = iLink;
      p->iFocus = iLink;
      ::SetFocus(hWnd);
      ::SetCapture(hWnd);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    }
    case WM_LBUTTONUP: {
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      int iDown = p->iDown;
      p->iDown = -1;
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      if (iDown >= 0 && LkHitTest(p, pt) == iDown) LkNotify(p, NM_CLICK, iDown);
      return 0;
    }
    case LM_HITTEST: {
      LHITTESTINFO FAR* pht = (LHITTESTINFO FAR*)lParam;
      if (pht == NULL) return FALSE;
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      LkLayout(p, rc.right);
      int iLink = LkHitTest(p, pht->pt);
      if (iLink < 0) return FALSE;
      pht->item.mask = LIF_ITEMINDEX | LIF_STATE | LIF_ITEMID | LIF_URL;
      LkFillItem(p, iLink, &pht->item);
      return TRUE;
    }
    case LM_GETIDEALHEIGHT: {
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      return LkLayout(p, rc.right);
    }
    case LM_SETITEM: {
      const LITEM FAR* pItem = (const LITEM FAR*)lParam;
      if (pItem == NULL || !(pItem->mask & LIF_ITEMINDEX)) return FALSE;
      W16Link* l = LkLink(p, pItem->iLink);
      if (l == NULL) return FALSE;
      if (pItem->mask & LIF_STATE) {
        UINT mask = pItem->stateMask;
        l->state = (l->state & ~mask) | (pItem->state & mask);
        if ((mask & LIS_FOCUSED) && (pItem->state & LIS_FOCUSED)) p->iFocus = pItem->iLink;
        l->state &= ~LIS_FOCUSED;
      }
      if (pItem->mask & LIF_ITEMID) l->id = LkFromWide(pItem->szID, MAX_LINKID_TEXT);
      if (pItem->mask & LIF_URL) l->url = LkFromWide(pItem->szUrl, L_MAX_URL_LENGTH);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case LM_GETITEM: {
      LITEM FAR* pItem = (LITEM FAR*)lParam;
      if (pItem == NULL || !(pItem->mask & LIF_ITEMINDEX)) return FALSE;
      W16Link* l = LkLink(p, pItem->iLink);
      if (l == NULL) return FALSE;
      UINT state = l->state | (pItem->iLink == p->iFocus && p->bFocus ? LIS_FOCUSED : 0);
      if (pItem->mask & LIF_STATE) pItem->state = state & pItem->stateMask;
      if (pItem->mask & LIF_ITEMID) LkToWide(pItem->szID, MAX_LINKID_TEXT, l->id);
      if (pItem->mask & LIF_URL) LkToWide(pItem->szUrl, L_MAX_URL_LENGTH, l->url);
      return TRUE;
    }
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the hot key (AFX_WNDCOMMCTL_HOTKEY_REG), IP address
/// (AFX_WNDCOMMCTL_INTERNET_REG), pager (AFX_WNDCOMMCTL_PAGER_REG) and
/// link (AFX_WNDCOMMCTL_LINK_REG) classes for the flags in fReg. Gives
/// the flags of the classes that are there.
LONG AFXAPI _AfxW16RegisterMiscClasses(LONG fReg) {
  LONG fDone = 0;
  if ((fReg & AFX_WNDCOMMCTL_HOTKEY_REG) &&
      RegisterCtlClasses(HOTKEY_CLASS, "msctls_hotkey32", W16HotKeyProc))
    fDone |= AFX_WNDCOMMCTL_HOTKEY_REG;
  if ((fReg & AFX_WNDCOMMCTL_INTERNET_REG) &&
      RegisterCtlClasses(WC_IPADDRESS, "SysIPAddress32", W16IPAddrProc))
    fDone |= AFX_WNDCOMMCTL_INTERNET_REG;
  if ((fReg & AFX_WNDCOMMCTL_PAGER_REG) &&
      RegisterCtlClasses(WC_PAGESCROLLER, "SysPager", W16PagerProc))
    fDone |= AFX_WNDCOMMCTL_PAGER_REG;
  // CLinkCtrl makes its window with the name of comctl32.dll.
  if ((fReg & AFX_WNDCOMMCTL_LINK_REG) && RegisterCtlClass("SysLink", W16LinkProc))
    fDone |= AFX_WNDCOMMCTL_LINK_REG;
  return fDone;
}

#endif  // _AFX_WIN16
