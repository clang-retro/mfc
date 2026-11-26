// win16tt.cpp - the tooltip control window for 16-bit Windows, which has
// no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The window does the TTM_ messages in ANSI and sends TTN_GETDISPINFOA,
// TTN_SHOW and TTN_POP to the window of each tool. A tool is a window
// (TTF_IDISHWND) or a rectangle in a window. The tooltip gets the mouse
// messages of the tools through TTM_RELAYEVENT, or from the windows that
// it subclasses (TTF_SUBCLASS). Tracking tooltips (TTF_TRACK), titles,
// the delay times, the colors, the margins and the maximum width with
// word wrap work. 16-bit Windows has no WM_MOUSELEAVE, so a timer finds
// the mouse when it leaves a tool. There are no balloon tooltips.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

// The timers of a tooltip.
enum {
  kShowTimer = 1,     // the initial delay before the tip shows
  kAutoPopTimer = 2,  // the time that the tip stays
  kLeaveTimer = 3,    // looks for the mouse leaving the tool
};
const UINT kLeaveTick = 100;

// The properties of a window that a tooltip subclasses. A property holds
// 16 bits, so the old procedure takes two.
const char kTipProp[] = "AfxTip16";
const char kTipProcLo[] = "AfxTip16Lo";
const char kTipProcHi[] = "AfxTip16Hi";
const char kTipCount[] = "AfxTip16N";

// One tool of a tooltip.
struct W16Tool {
  UINT uFlags;
  HWND hwnd;
  UINT_PTR uId;
  RECT rect;
  HINSTANCE hinst;
  CString text;
  BOOL bCallback;  // the text is LPSTR_TEXTCALLBACK
  LPARAM lParam;
  /// Starts with no window and no text.
  W16Tool() : uFlags(0), hwnd(NULL), uId(0), hinst(NULL), bCallback(FALSE), lParam(0) {
    ::SetRectEmpty(&rect);
  }
};

// The data of a tooltip.
struct W16Tip {
  HWND hwnd;
  CPtrArray tools;   // the tools (W16Tool*)
  BOOL bActive;      // TTM_ACTIVATE
  int iHot;          // the tool under the mouse, or -1
  int iShown;        // the tool whose tip is on the screen, or -1
  int iTrack;        // the tool of an active tracking tip, or -1
  POINT ptTrack;     // TTM_TRACKPOSITION, in screen coordinates
  BOOL bButtonDown;  // a button went down on the hot tool: no tip until it changes
  UINT nInitial, nAutoPop, nReshow;  // the delay times, in milliseconds
  DWORD dwLastPop;   // the time that the last tip went away
  COLORREF clrBk, clrText;
  int nMaxWidth;     // TTM_SETMAXTIPWIDTH, or -1
  RECT rcMargin;     // TTM_SETMARGIN
  HFONT hFont;
  HFONT hBoldFont;   // the font of the title
  CString title;     // TTM_SETTITLEA
  CString shownText; // the text of the tip on the screen
  /// Starts active, with the delays and the colors of comctl32.dll.
  W16Tip()
      : hwnd(NULL), bActive(TRUE), iHot(-1), iShown(-1), iTrack(-1), bButtonDown(FALSE),
        nInitial(500), nAutoPop(5000), nReshow(100), dwLastPop(0), clrBk(RGB(255, 255, 225)),
        clrText(RGB(0, 0, 0)), nMaxWidth(-1), hFont(NULL), hBoldFont(NULL) {
    ptTrack.x = ptTrack.y = 0;
    ::SetRectEmpty(&rcMargin);
  }
  /// Deletes the tools and the title font.
  ~W16Tip() {
    for (int i = 0; i < tools.GetSize(); i++) delete (W16Tool*)tools[i];
    if (hBoldFont != NULL) ::DeleteObject(hBoldFont);
  }
};

/// Gives the tool i.
W16Tool* TipTool(W16Tip* p, int i) {
  return i >= 0 && i < p->tools.GetSize() ? (W16Tool*)p->tools[i] : NULL;
}

/// Gives the font of the text of a tooltip.
HFONT TipFont(W16Tip* p) {
  return p->hFont != NULL ? p->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the bold font of the title, made from the font of the text.
HFONT TipTitleFont(W16Tip* p) {
  if (p->hBoldFont == NULL) {
    LOGFONT lf;
    if (::GetObject(TipFont(p), sizeof(lf), &lf) != 0) {
      lf.lfWeight = FW_BOLD;
      p->hBoldFont = ::CreateFontIndirect(&lf);
    }
  }
  return p->hBoldFont != NULL ? p->hBoldFont : TipFont(p);
}

/// Gives the window that a tool watches: the tool window itself for
/// TTF_IDISHWND, or the window of the rectangle.
HWND TipToolWindow(const W16Tool* t) {
  return (t->uFlags & TTF_IDISHWND) ? (HWND)(UINT)t->uId : t->hwnd;
}

/// Finds the tool of a window and an ID. Gives its index, or -1.
int TipFind(W16Tip* p, HWND hwnd, UINT_PTR uId) {
  for (int i = 0; i < p->tools.GetSize(); i++) {
    W16Tool* t = TipTool(p, i);
    if (t->hwnd == hwnd && t->uId == uId) return i;
  }
  return -1;
}

/// Finds the tool at a point in the client area of a window. Gives its
/// index, or -1. The tracking tools do not count.
int TipHitTest(W16Tip* p, HWND hwnd, POINT pt) {
  for (int i = 0; i < p->tools.GetSize(); i++) {
    W16Tool* t = TipTool(p, i);
    if (t->uFlags & TTF_TRACK) continue;
    if (t->uFlags & TTF_IDISHWND) {
      if ((HWND)(UINT)t->uId == hwnd) return i;
    } else if (t->hwnd == hwnd && ::PtInRect(&t->rect, pt)) {
      return i;
    }
  }
  return -1;
}

/// Copies the text of a TOOLINFOA into a tool: a string,
/// LPSTR_TEXTCALLBACK or a string resource of hinst.
void TipSetText(W16Tool* t, HINSTANCE hinst, LPCSTR lpszText) {
  t->hinst = hinst;
  t->bCallback = FALSE;
  t->text.Empty();
  if (lpszText == LPSTR_TEXTCALLBACKA) {
    t->bCallback = TRUE;
  } else if (lpszText != NULL && HIWORD((DWORD)lpszText) == 0) {
    char sz[256];
    if (::LoadString(hinst, LOWORD((DWORD)lpszText), sz, sizeof(sz)) > 0) t->text = sz;
  } else if (lpszText != NULL) {
    t->text = lpszText;
  }
}

/// Copies a TOOLINFOA into a tool. The lParam is there when cbSize gives
/// more than the first version of the structure.
void TipFromInfo(W16Tool* t, const TOOLINFOA FAR* pti) {
  t->uFlags = pti->uFlags;
  t->hwnd = pti->hwnd;
  t->uId = pti->uId;
  t->rect = pti->rect;
  TipSetText(t, pti->hinst, pti->lpszText);
  if (pti->cbSize > TTTOOLINFOA_V1_SIZE) t->lParam = pti->lParam;
}

/// Fills a TOOLINFOA with a tool. The text goes into lpszText when the
/// caller gives a buffer, which must have 80 characters.
void TipToInfo(const W16Tool* t, TOOLINFOA FAR* pti) {
  pti->uFlags = t->uFlags;
  pti->hwnd = t->hwnd;
  pti->uId = t->uId;
  pti->rect = t->rect;
  pti->hinst = t->hinst;
  if (t->bCallback)
    pti->lpszText = LPSTR_TEXTCALLBACKA;
  else if (pti->lpszText != NULL && pti->lpszText != LPSTR_TEXTCALLBACKA)
    lstrcpyn(pti->lpszText, t->text, 80);
  if (pti->cbSize > TTTOOLINFOA_V1_SIZE) pti->lParam = t->lParam;
}

/// Sends a notification of a tooltip to the window of a tool. Gives the
/// result of the window.
LRESULT TipNotify(W16Tip* p, const W16Tool* t, UINT nCode, NMHDR* pHdr) {
  pHdr->hwndFrom = p->hwnd;
  pHdr->idFrom = (UINT)t->uId;
  pHdr->code = nCode;
  if (t->hwnd == NULL) return 0;
  return ::SendMessage(t->hwnd, WM_NOTIFY, (WPARAM)pHdr->idFrom, (LPARAM)pHdr);
}

/// Gives the text of a tool: its own text, or the text that its window
/// gives for TTN_GETDISPINFOA. With TTF_DI_SETITEM the tool keeps it.
CString TipGetText(W16Tip* p, int i) {
  W16Tool* t = TipTool(p, i);
  if (t == NULL) return CString();
  if (!t->bCallback) return t->text;
  NMTTDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.lpszText = nm.szText;
  nm.hinst = t->hinst;
  nm.uFlags = t->uFlags;
  nm.lParam = t->lParam;
  TipNotify(p, t, TTN_GETDISPINFOA, &nm.hdr);
  t = TipTool(p, i);  // the window can delete the tool
  if (t == NULL) return CString();
  CString str;
  if (nm.lpszText != NULL && HIWORD((DWORD)nm.lpszText) == 0) {
    char sz[256];
    if (::LoadString(nm.hinst, LOWORD((DWORD)nm.lpszText), sz, sizeof(sz)) > 0) str = sz;
  } else if (nm.lpszText != NULL) {
    str = nm.lpszText;
  }
  if (nm.uFlags & TTF_DI_SETITEM) {
    t->text = str;
    t->bCallback = FALSE;
  }
  return str;
}

/// Removes the & characters of a text, as a tooltip without TTS_NOPREFIX
/// does. Two & characters give one.
CString TipStripPrefix(const CString& str) {
  CString strOut;
  for (int i = 0; i < str.GetLength(); i++) {
    char ch = str[i];
    if (ch == '&') {
      if (i + 1 < str.GetLength() && str[i + 1] == '&') {
        strOut += '&';
        i++;
      }
      continue;
    }
    strOut += ch;
  }
  return strOut;
}

/// Gives the DrawText flags of the text of a tooltip.
UINT TipTextFlags(W16Tip* p) {
  return DT_NOPREFIX | (p->nMaxWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE);
}

// The padding between the frame and the text, in pixels.
const int kPadX = 3, kPadY = 1;

/// Gives the size of the window of a tip: the title, the text, the
/// margins, the padding and the frame.
SIZE TipBubbleSize(W16Tip* p, const CString& strText) {
  HDC hdc = ::GetDC(p->hwnd);
  RECT rcText = {0, 0, p->nMaxWidth > 0 ? p->nMaxWidth : 0x7FFF, 0};
  HGDIOBJ hOld = ::SelectObject(hdc, TipFont(p));
  ::DrawText(hdc, strText, strText.GetLength(), &rcText, TipTextFlags(p) | DT_CALCRECT);
  int cx = rcText.right - rcText.left;
  int cy = rcText.bottom - rcText.top;
  if (!p->title.IsEmpty()) {
    RECT rcTitle = {0, 0, 0x7FFF, 0};
    ::SelectObject(hdc, TipTitleFont(p));
    ::DrawText(hdc, p->title, p->title.GetLength(), &rcTitle, DT_NOPREFIX | DT_SINGLELINE | DT_CALCRECT);
    if (rcTitle.right > cx) cx = rcTitle.right;
    cy += rcTitle.bottom + 2;
  }
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(p->hwnd, hdc);
  SIZE size;
  size.cx = cx + 2 * kPadX + 2 + p->rcMargin.left + p->rcMargin.right;
  size.cy = cy + 2 * kPadY + 2 + p->rcMargin.top + p->rcMargin.bottom;
  return size;
}

/// Hides the tip, and sends TTN_POP to the window of its tool.
void TipPop(W16Tip* p) {
  ::KillTimer(p->hwnd, kAutoPopTimer);
  if (p->iShown < 0) return;
  int i = p->iShown;
  p->iShown = -1;
  ::ShowWindow(p->hwnd, SW_HIDE);
  p->dwLastPop = ::GetTickCount();
  W16Tool* t = TipTool(p, i);
  if (t != NULL) {
    NMHDR nm;
    TipNotify(p, t, TTN_POP, &nm);
  }
}

/// Stops following the hot tool: no timers, no tip.
void TipStopHot(W16Tip* p) {
  ::KillTimer(p->hwnd, kShowTimer);
  ::KillTimer(p->hwnd, kLeaveTimer);
  if (p->iShown >= 0 && p->iShown != p->iTrack) TipPop(p);
  p->iHot = -1;
  p->bButtonDown = FALSE;
}

/// Shows the tip of the tool i: the window of the tool gets TTN_SHOW,
/// which can place the tip. Otherwise the tip goes below the mouse, below
/// the middle of the tool (TTF_CENTERTIP), or at the tracking position.
/// The tip stays inside the screen.
void TipShow(W16Tip* p, int i) {
  W16Tool* t = TipTool(p, i);
  if (t == NULL) return;
  CString strText = TipGetText(p, i);
  t = TipTool(p, i);
  if (t == NULL || strText.IsEmpty()) {
    if (p->iShown == i) TipPop(p);
    return;
  }
  if (!(StyleOf(p->hwnd) & TTS_NOPREFIX)) strText = TipStripPrefix(strText);
  if (p->iShown >= 0 && p->iShown != i) TipPop(p);
  p->shownText = strText;
  SIZE size = TipBubbleSize(p, strText);
  POINT pt;
  if (t->uFlags & TTF_TRACK) {
    pt = p->ptTrack;
  } else if (t->uFlags & TTF_CENTERTIP) {
    RECT rcTool = t->rect;
    HWND hwndTool = TipToolWindow(t);
    if (t->uFlags & TTF_IDISHWND) ::GetClientRect(hwndTool, &rcTool);
    POINT ptBottom = {(rcTool.left + rcTool.right) / 2, rcTool.bottom};
    ::ClientToScreen(hwndTool, &ptBottom);
    pt.x = ptBottom.x - size.cx / 2;
    pt.y = ptBottom.y + 2;
  } else {
    ::GetCursorPos(&pt);
    pt.y += ::GetSystemMetrics(SM_CYCURSOR) / 2 + 4;
  }
  int cxScreen = ::GetSystemMetrics(SM_CXSCREEN);
  int cyScreen = ::GetSystemMetrics(SM_CYSCREEN);
  if (pt.x + size.cx > cxScreen) pt.x = cxScreen - size.cx;
  if (pt.x < 0) pt.x = 0;
  if (pt.y + size.cy > cyScreen) pt.y = cyScreen - size.cy;
  if (pt.y < 0) pt.y = 0;
  ::SetWindowPos(p->hwnd, HWND_TOPMOST, pt.x, pt.y, size.cx, size.cy, SWP_NOACTIVATE);
  p->iShown = i;
  NMHDR nm;
  // A window that gives TRUE for TTN_SHOW placed the tip itself.
  TipNotify(p, t, TTN_SHOW, &nm);
  if (p->iShown != i) return;
  ::SetWindowPos(p->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ::InvalidateRect(p->hwnd, NULL, TRUE);
  ::UpdateWindow(p->hwnd);
  if (!(t->uFlags & TTF_TRACK)) ::SetTimer(p->hwnd, kAutoPopTimer, p->nAutoPop, NULL);
}

/// Gives TRUE when a tool can show a tip now: the tooltip is active, and
/// without TTS_ALWAYSTIP the top window of the tool is the active window.
BOOL TipCanShow(W16Tip* p, int i) {
  W16Tool* t = TipTool(p, i);
  if (!p->bActive || t == NULL) return FALSE;
  if (StyleOf(p->hwnd) & TTS_ALWAYSTIP) return TRUE;
  HWND hwndTop = TipToolWindow(t);
  while (hwndTop != NULL && (StyleOf(hwndTop) & WS_CHILD)) hwndTop = ::GetParent(hwndTop);
  return hwndTop != NULL && ::GetActiveWindow() == hwndTop;
}

/// Follows the mouse over a window: a new tool starts the initial delay
/// (or the reshow delay soon after a tip), and the tip of an old tool
/// goes away.
void TipMouseAt(W16Tip* p, HWND hwnd, POINT pt) {
  int i = TipHitTest(p, hwnd, pt);
  if (i == p->iHot) return;
  TipStopHot(p);
  if (i < 0 || !TipCanShow(p, i)) return;
  p->iHot = i;
  BOOL bRecent = ::GetTickCount() - p->dwLastPop < (DWORD)p->nInitial;
  ::SetTimer(p->hwnd, kShowTimer, bRecent ? p->nReshow : p->nInitial, NULL);
  ::SetTimer(p->hwnd, kLeaveTimer, kLeaveTick, NULL);
}

/// Does a mouse message of a tool window (TTM_RELAYEVENT): a move
/// follows the mouse, and a button hides the tip until the mouse goes to
/// another tool.
void TipRelay(W16Tip* p, const MSG FAR* pMsg) {
  POINT pt = {(int)(short)LOWORD(pMsg->lParam), (int)(short)HIWORD(pMsg->lParam)};
  switch (pMsg->message) {
    case WM_MOUSEMOVE:
      if (p->bButtonDown && TipHitTest(p, pMsg->hwnd, pt) == p->iHot) return;
      TipMouseAt(p, pMsg->hwnd, pt);
      break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
      int iHot = TipHitTest(p, pMsg->hwnd, pt);
      TipStopHot(p);
      p->iHot = iHot;
      p->bButtonDown = iHot >= 0;
      if (iHot >= 0) ::SetTimer(p->hwnd, kLeaveTimer, kLeaveTick, NULL);
      break;
    }
  }
}

/// Looks for the mouse when the leave timer ticks: when the mouse is not
/// on the hot tool, the tip goes away.
void TipCheckLeave(W16Tip* p) {
  W16Tool* t = TipTool(p, p->iHot);
  if (t == NULL) {
    TipStopHot(p);
    return;
  }
  POINT pt;
  ::GetCursorPos(&pt);
  HWND hwndUnder = ::WindowFromPoint(pt);
  HWND hwndTool = TipToolWindow(t);
  BOOL bStill = FALSE;
  if (hwndUnder == hwndTool || (hwndUnder != NULL && ::IsChild(hwndTool, hwndUnder))) {
    if (t->uFlags & TTF_IDISHWND) {
      bStill = TRUE;
    } else {
      ::ScreenToClient(hwndTool, &pt);
      bStill = ::PtInRect(&t->rect, pt);
    }
  }
  if (!bStill) TipStopHot(p);
}

LRESULT CALLBACK TipSubclassProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

/// Gives the procedure that a subclassed tool window had before.
WNDPROC TipOldProc(HWND hWnd) {
  DWORD dw = MAKELONG((WORD)(UINT)::GetProp(hWnd, kTipProcLo), (WORD)(UINT)::GetProp(hWnd, kTipProcHi));
  return (WNDPROC)dw;
}

/// Subclasses a tool window (TTF_SUBCLASS), so that the tooltip gets its
/// mouse messages. A window keeps a count of its tools.
void TipSubclass(W16Tip* p, HWND hWnd) {
  if (hWnd == NULL || !::IsWindow(hWnd)) return;
  HWND hwndTip = (HWND)(UINT)::GetProp(hWnd, kTipProp);
  if (hwndTip != NULL && hwndTip != p->hwnd) return;  // another tooltip has it
  UINT nCount = (UINT)::GetProp(hWnd, kTipCount);
  if (nCount == 0) {
    DWORD dwOld = (DWORD)::SetWindowLong(hWnd, GWL_WNDPROC, (LONG)(WNDPROC)TipSubclassProc);
    ::SetProp(hWnd, kTipProcLo, (HANDLE)(UINT)LOWORD(dwOld));
    ::SetProp(hWnd, kTipProcHi, (HANDLE)(UINT)HIWORD(dwOld));
    ::SetProp(hWnd, kTipProp, (HANDLE)(UINT)p->hwnd);
  }
  ::SetProp(hWnd, kTipCount, (HANDLE)(nCount + 1));
}

/// Removes the subclass of a window and its properties.
void TipRemoveSubclass(HWND hWnd) {
  WNDPROC pfnOld = TipOldProc(hWnd);
  if (pfnOld != NULL && (WNDPROC)::GetWindowLong(hWnd, GWL_WNDPROC) == TipSubclassProc)
    ::SetWindowLong(hWnd, GWL_WNDPROC, (LONG)pfnOld);
  ::RemoveProp(hWnd, kTipProcLo);
  ::RemoveProp(hWnd, kTipProcHi);
  ::RemoveProp(hWnd, kTipProp);
  ::RemoveProp(hWnd, kTipCount);
}

/// Removes one tool from the count of a subclassed window. The subclass
/// goes with the last tool.
void TipUnsubclass(W16Tip* p, HWND hWnd) {
  if (hWnd == NULL || !::IsWindow(hWnd)) return;
  if ((HWND)(UINT)::GetProp(hWnd, kTipProp) != p->hwnd) return;
  UINT nCount = (UINT)::GetProp(hWnd, kTipCount);
  if (nCount > 1) {
    ::SetProp(hWnd, kTipCount, (HANDLE)(nCount - 1));
    return;
  }
  TipRemoveSubclass(hWnd);
}

/// The procedure of a subclassed tool window: its mouse messages go to
/// the tooltip, then to the old procedure.
LRESULT CALLBACK TipSubclassProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  WNDPROC pfnOld = TipOldProc(hWnd);
  HWND hwndTip = (HWND)(UINT)::GetProp(hWnd, kTipProp);
  if ((nMsg >= WM_MOUSEFIRST && nMsg <= WM_MOUSELAST) && hwndTip != NULL && ::IsWindow(hwndTip)) {
    MSG msg;
    msg.hwnd = hWnd;
    msg.message = nMsg;
    msg.wParam = wParam;
    msg.lParam = lParam;
    msg.time = ::GetMessageTime();
    DWORD dwPos = ::GetMessagePos();
    msg.pt.x = (int)(short)LOWORD(dwPos);
    msg.pt.y = (int)(short)HIWORD(dwPos);
    ::SendMessage(hwndTip, TTM_RELAYEVENT, 0, (LPARAM)(MSG FAR*)&msg);
  }
  if (pfnOld == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    TipRemoveSubclass(hWnd);
    return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
  }
  return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
}

/// Removes the tool i: its tip, its subclass and its data.
void TipDelete(W16Tip* p, int i) {
  W16Tool* t = TipTool(p, i);
  if (t == NULL) return;
  if (p->iShown == i) TipPop(p);
  if (p->iHot == i) TipStopHot(p);
  if (t->uFlags & TTF_SUBCLASS) TipUnsubclass(p, TipToolWindow(t));
  delete t;
  p->tools.RemoveAt(i);
  // The indexes after i move down by one.
  if (p->iHot > i) p->iHot--;
  if (p->iShown > i) p->iShown--;
  if (p->iTrack == i)
    p->iTrack = -1;
  else if (p->iTrack > i)
    p->iTrack--;
}

/// Draws a tip: the background, a black frame, the title and the text.
void TipPaint(W16Tip* p, HDC hdc) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  FillColor(hdc, rc, p->clrBk);
  ::FrameRect(hdc, &rc, (HBRUSH)::GetStockObject(BLACK_BRUSH));
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, p->clrText);
  RECT rcText = rc;
  rcText.left += 1 + kPadX + p->rcMargin.left;
  rcText.top += 1 + kPadY + p->rcMargin.top;
  rcText.right -= 1 + kPadX + p->rcMargin.right;
  rcText.bottom -= 1 + kPadY + p->rcMargin.bottom;
  HGDIOBJ hOld = ::SelectObject(hdc, TipFont(p));
  if (!p->title.IsEmpty()) {
    ::SelectObject(hdc, TipTitleFont(p));
    RECT rcTitle = rcText;
    ::DrawText(hdc, p->title, p->title.GetLength(), &rcTitle, DT_NOPREFIX | DT_SINGLELINE | DT_CALCRECT);
    ::DrawText(hdc, p->title, p->title.GetLength(), &rcTitle, DT_NOPREFIX | DT_SINGLELINE);
    rcText.top = rcTitle.bottom + 2;
    ::SelectObject(hdc, TipFont(p));
  }
  ::DrawText(hdc, p->shownText, p->shownText.GetLength(), &rcText, TipTextFlags(p));
  ::SelectObject(hdc, hOld);
}

/// Does the TTM_ messages of a tooltip.
LRESULT TipMessage(W16Tip* p, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case TTM_ACTIVATE:
      p->bActive = wParam != 0;
      if (!p->bActive) TipStopHot(p);
      return 0;
    case TTM_SETDELAYTIME: {
      int nTime = (int)(short)LOWORD(lParam);
      switch (wParam) {
        case TTDT_AUTOMATIC:
          if (nTime < 0) nTime = 500;
          p->nInitial = (UINT)nTime;
          p->nAutoPop = (UINT)nTime * 10;
          p->nReshow = (UINT)nTime / 5;
          break;
        case TTDT_INITIAL:
          p->nInitial = nTime < 0 ? 500 : (UINT)nTime;
          break;
        case TTDT_AUTOPOP:
          p->nAutoPop = nTime < 0 ? 5000 : (UINT)nTime;
          break;
        case TTDT_RESHOW:
          p->nReshow = nTime < 0 ? 100 : (UINT)nTime;
          break;
      }
      return 0;
    }
    case TTM_GETDELAYTIME:
      return wParam == TTDT_AUTOPOP  ? p->nAutoPop
             : wParam == TTDT_RESHOW ? p->nReshow
                                     : p->nInitial;
    case TTM_ADDTOOLA: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL || pti->cbSize < TTTOOLINFOA_V1_SIZE) return FALSE;
      W16Tool* t = new W16Tool;
      TipFromInfo(t, pti);
      p->tools.Add(t);
      if (t->uFlags & TTF_SUBCLASS) TipSubclass(p, TipToolWindow(t));
      return TRUE;
    }
    case TTM_DELTOOLA: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      TipDelete(p, TipFind(p, pti->hwnd, pti->uId));
      return 0;
    }
    case TTM_NEWTOOLRECTA: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      W16Tool* t = TipTool(p, TipFind(p, pti->hwnd, pti->uId));
      if (t != NULL) t->rect = pti->rect;
      return 0;
    }
    case TTM_RELAYEVENT:
      if (lParam != 0) TipRelay(p, (const MSG FAR*)lParam);
      return 0;
    case TTM_GETTOOLINFOA: {
      TOOLINFOA FAR* pti = (TOOLINFOA FAR*)lParam;
      if (pti == NULL) return FALSE;
      W16Tool* t = TipTool(p, TipFind(p, pti->hwnd, pti->uId));
      if (t == NULL) return FALSE;
      TipToInfo(t, pti);
      return TRUE;
    }
    case TTM_SETTOOLINFOA: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      int i = TipFind(p, pti->hwnd, pti->uId);
      W16Tool* t = TipTool(p, i);
      if (t == NULL) return 0;
      BOOL bWasSub = (t->uFlags & TTF_SUBCLASS) != 0;
      HWND hwndOld = TipToolWindow(t);
      TipFromInfo(t, pti);
      BOOL bIsSub = (t->uFlags & TTF_SUBCLASS) != 0;
      HWND hwndNew = TipToolWindow(t);
      if (bWasSub && (!bIsSub || hwndNew != hwndOld)) TipUnsubclass(p, hwndOld);
      if (bIsSub && (!bWasSub || hwndNew != hwndOld)) TipSubclass(p, hwndNew);
      if (p->iShown == i) TipShow(p, i);
      return 0;
    }
    case TTM_HITTESTA: {
      TTHITTESTINFOA FAR* pht = (TTHITTESTINFOA FAR*)lParam;
      if (pht == NULL) return FALSE;
      int i = TipHitTest(p, pht->hwnd, pht->pt);
      W16Tool* t = TipTool(p, i);
      if (t == NULL) return FALSE;
      if (pht->ti.cbSize >= TTTOOLINFOA_V1_SIZE) TipToInfo(t, &pht->ti);
      return TRUE;
    }
    case TTM_GETTEXTA: {
      TOOLINFOA FAR* pti = (TOOLINFOA FAR*)lParam;
      if (pti == NULL || pti->lpszText == NULL) return 0;
      int i = TipFind(p, pti->hwnd, pti->uId);
      if (i < 0) {
        pti->lpszText[0] = '\0';
        return 0;
      }
      CString str = TipGetText(p, i);
      int cch = wParam != 0 ? (int)wParam : 80;
      lstrcpyn(pti->lpszText, str, cch);
      return lstrlen(pti->lpszText);
    }
    case TTM_UPDATETIPTEXTA: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      int i = TipFind(p, pti->hwnd, pti->uId);
      W16Tool* t = TipTool(p, i);
      if (t == NULL) return 0;
      TipSetText(t, pti->hinst, pti->lpszText);
      if (p->iShown == i) TipShow(p, i);
      return 0;
    }
    case TTM_GETTOOLCOUNT:
      return p->tools.GetSize();
    case TTM_ENUMTOOLSA: {
      TOOLINFOA FAR* pti = (TOOLINFOA FAR*)lParam;
      W16Tool* t = TipTool(p, (int)wParam);
      if (pti == NULL || t == NULL) return FALSE;
      TipToInfo(t, pti);
      return TRUE;
    }
    case TTM_GETCURRENTTOOLA: {
      int i = p->iShown >= 0 ? p->iShown : p->iHot;
      W16Tool* t = TipTool(p, i);
      if (t == NULL) return FALSE;
      if (lParam != 0) TipToInfo(t, (TOOLINFOA FAR*)lParam);
      return TRUE;
    }
    case TTM_WINDOWFROMPOINT:
      if (lParam == 0) return 0;
      return (LRESULT)(UINT)::WindowFromPoint(*(const POINT FAR*)lParam);
    case TTM_TRACKACTIVATE: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      int i = TipFind(p, pti->hwnd, pti->uId);
      if (i < 0) return 0;
      if (wParam) {
        p->iTrack = i;
        TipShow(p, i);
      } else if (p->iTrack == i) {
        p->iTrack = -1;
        if (p->iShown == i) TipPop(p);
      }
      return 0;
    }
    case TTM_TRACKPOSITION:
      p->ptTrack.x = (int)(short)LOWORD(lParam);
      p->ptTrack.y = (int)(short)HIWORD(lParam);
      if (p->iTrack >= 0 && p->iShown == p->iTrack)
        ::SetWindowPos(hWnd, HWND_TOPMOST, p->ptTrack.x, p->ptTrack.y, 0, 0,
                       SWP_NOSIZE | SWP_NOACTIVATE);
      return 0;
    case TTM_SETTIPBKCOLOR:
    case TTM_SETTIPTEXTCOLOR: {
      // A 16-bit wParam cannot hold a COLORREF: a color in lParam wins.
      COLORREF clr = lParam != 0 ? (COLORREF)lParam : (COLORREF)wParam;
      if (nMsg == TTM_SETTIPBKCOLOR)
        p->clrBk = clr;
      else
        p->clrText = clr;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    }
    case TTM_GETTIPBKCOLOR:
      return (LRESULT)p->clrBk;
    case TTM_GETTIPTEXTCOLOR:
      return (LRESULT)p->clrText;
    case TTM_SETMAXTIPWIDTH: {
      int nOld = p->nMaxWidth;
      p->nMaxWidth = (int)(short)LOWORD(lParam);
      return nOld;
    }
    case TTM_GETMAXTIPWIDTH:
      return p->nMaxWidth;
    case TTM_SETMARGIN:
      if (lParam != 0) p->rcMargin = *(const RECT FAR*)lParam;
      return 0;
    case TTM_GETMARGIN:
      if (lParam != 0) *(RECT FAR*)lParam = p->rcMargin;
      return 0;
    case TTM_POP:
      TipStopHot(p);
      if (p->iShown >= 0) TipPop(p);
      return 0;
    case TTM_POPUP:
      if (p->iHot >= 0) {
        ::KillTimer(hWnd, kShowTimer);
        TipShow(p, p->iHot);
      }
      return 0;
    case TTM_UPDATE:
      if (p->iShown >= 0) TipShow(p, p->iShown);
      return 0;
    case TTM_GETBUBBLESIZE: {
      const TOOLINFOA FAR* pti = (const TOOLINFOA FAR*)lParam;
      if (pti == NULL) return 0;
      int i = TipFind(p, pti->hwnd, pti->uId);
      if (i < 0) return 0;
      SIZE size = TipBubbleSize(p, TipGetText(p, i));
      return MAKELONG(size.cx, size.cy);
    }
    case TTM_ADJUSTRECT: {
      RECT FAR* prc = (RECT FAR*)lParam;
      if (prc == NULL) return FALSE;
      int dl = 1 + kPadX + p->rcMargin.left, dt = 1 + kPadY + p->rcMargin.top;
      int dr = 1 + kPadX + p->rcMargin.right, db = 1 + kPadY + p->rcMargin.bottom;
      if (wParam) {
        prc->left -= dl;
        prc->top -= dt;
        prc->right += dr;
        prc->bottom += db;
      } else {
        prc->left += dl;
        prc->top += dt;
        prc->right -= dr;
        prc->bottom -= db;
      }
      return TRUE;
    }
    case TTM_SETTITLEA:
      p->title = lParam != 0 ? (LPCSTR)lParam : "";
      if (p->iShown >= 0) TipShow(p, p->iShown);
      return TRUE;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// Does the window messages of a tooltip: the paint, the timers, the
/// font and the destruction.
LRESULT TooltipMessage(W16Tip* p, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      TipPaint(p, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      switch (wParam) {
        case kShowTimer:
          ::KillTimer(hWnd, kShowTimer);
          if (p->iHot >= 0 && !p->bButtonDown && TipCanShow(p, p->iHot)) TipShow(p, p->iHot);
          break;
        case kAutoPopTimer:
          ::KillTimer(hWnd, kAutoPopTimer);
          if (p->iShown >= 0 && p->iShown != p->iTrack) TipPop(p);
          break;
        case kLeaveTimer:
          TipCheckLeave(p);
          break;
      }
      return 0;
    case WM_SETFONT:
      p->hFont = (HFONT)wParam;
      if (p->hBoldFont != NULL) {
        ::DeleteObject(p->hBoldFont);
        p->hBoldFont = NULL;
      }
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)p->hFont;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_DESTROY:
      ::KillTimer(hWnd, kShowTimer);
      ::KillTimer(hWnd, kAutoPopTimer);
      ::KillTimer(hWnd, kLeaveTimer);
      for (int i = 0; i < p->tools.GetSize(); i++) {
        W16Tool* t = TipTool(p, i);
        if (t->uFlags & TTF_SUBCLASS) TipUnsubclass(p, TipToolWindow(t));
      }
      return 0;
  }
  return TipMessage(p, hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the tooltip class.
LRESULT CALLBACK W16TooltipProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Tip* p = StateOf<W16Tip>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Tip>(hWnd);
    // A tooltip is a popup window with no caption and no border.
    DWORD dwStyle = StyleOf(hWnd) & ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
    ::SetWindowLong(hWnd, GWL_STYLE, (LONG)(dwStyle | WS_POPUP));
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, p);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return TooltipMessage(p, hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the tooltip class. Gives TRUE when the class is there.
BOOL AFXAPI _AfxW16RegisterTipClass() {
  return RegisterCtlClasses(TOOLTIPS_CLASS, "tooltips_class32", W16TooltipProc);
}

#endif  // _AFX_WIN16
