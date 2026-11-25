// win16bar.cpp - the toolbar and status bar windows for 16-bit Windows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// 16-bit Windows has no comctl32.dll. CToolBar and CStatusBar send the
// messages of the common toolbar and status bar to their windows. This
// file registers the two window classes (TOOLBARCLASSNAME and
// STATUSCLASSNAME of afxwin16u.h) and does the subset of the messages that the
// MFC classes send. The windows draw in the 3D style of the system
// colors.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"

namespace {

// ---------------------------------------------------------------------
// The toolbar

/// The data of one toolbar window.
struct W16Bar {
  HWND hwnd;
  HWND hwndNotify;     // the window that gets WM_COMMAND (NULL: the parent)
  SIZE sizeButton;     // the size of a button
  SIZE sizeImage;      // the size of an image in the bitmap
  HBITMAP hbmImages;   // the image well (the caller owns it)
  int nImages;         // the number of images in the well
  CArray<TBBUTTON, const TBBUTTON&> buttons;
  CArray<RECT, const RECT&> rects;  // the rectangle of each button
  CStringArray strings;             // the strings of TB_ADDSTRING
  int iPressed;        // the button under the mouse button, or -1
  BOOL bPressedIn;     // the mouse is on the pressed button
  BOOL bRedraw;        // WM_SETREDRAW
};

/// Gives the data of a toolbar or a status bar window.
template <class T>
T* StateOf(HWND hWnd) {
  return (T*)::GetWindowLong(hWnd, 0);
}

/// Calculates the rectangle of each button. A button with TBSTATE_WRAP
/// ends its row. A wrapped separator adds its width as space below the
/// row. A hidden button gets an empty rectangle.
void BarLayout(W16Bar* pBar) {
  int nCount = (int)pBar->buttons.GetSize();
  pBar->rects.SetSize(nCount);
  int x = 0, y = 0;
  for (int i = 0; i < nCount; i++) {
    const TBBUTTON& b = pBar->buttons[i];
    RECT& r = pBar->rects[i];
    if (b.fsState & TBSTATE_HIDDEN) {
      ::SetRect(&r, x, y, x, y);
      continue;
    }
    int cx = (b.fsStyle & TBSTYLE_SEP) ? b.iBitmap : pBar->sizeButton.cx;
    ::SetRect(&r, x, y, x + cx, y + pBar->sizeButton.cy);
    x += cx;
    if (b.fsState & TBSTATE_WRAP) {
      x = 0;
      y += pBar->sizeButton.cy;
      if (b.fsStyle & TBSTYLE_SEP) y += b.iBitmap;
    }
  }
}

/// Redraws the bar when WM_SETREDRAW lets it.
void BarInvalidate(W16Bar* pBar) {
  if (pBar->bRedraw) ::InvalidateRect(pBar->hwnd, NULL, TRUE);
}

/// Lays out the buttons again and redraws the bar.
void BarChanged(W16Bar* pBar) {
  BarLayout(pBar);
  BarInvalidate(pBar);
}

/// Gives the index of the button with the command nID, or -1.
int BarIndexOf(W16Bar* pBar, int nID) {
  for (int i = 0; i < pBar->buttons.GetSize(); i++)
    if (pBar->buttons[i].idCommand == nID && !(pBar->buttons[i].fsStyle & TBSTYLE_SEP)) return i;
  return -1;
}

/// Gives the index of the button at pt. For a separator at pt, gives
/// -(index) - 1. Gives -1 when no button is at pt.
int BarHitTest(W16Bar* pBar, POINT pt) {
  for (int i = 0; i < pBar->rects.GetSize(); i++) {
    if (!::PtInRect(&pBar->rects[i], pt)) continue;
    if (pBar->buttons[i].fsStyle & TBSTYLE_SEP) return -i - 1;
    return i;
  }
  return -1;
}

/// Draws the image iImage of the well at (x, y), grayed when bEnabled
/// is FALSE. A grayed image is a white copy and a shadow copy of the
/// dark pixels, as in MFC 2.5.
void BarDrawImage(W16Bar* pBar, HDC hdc, int iImage, int x, int y, BOOL bEnabled) {
  if (pBar->hbmImages == NULL || iImage < 0 || iImage >= pBar->nImages) return;
  int cx = pBar->sizeImage.cx, cy = pBar->sizeImage.cy;
  HDC hdcImage = ::CreateCompatibleDC(hdc);
  if (hdcImage == NULL) return;
  HGDIOBJ hOldImage = ::SelectObject(hdcImage, pBar->hbmImages);
  if (bEnabled) {
    ::BitBlt(hdc, x, y, cx, cy, hdcImage, iImage * cx, 0, SRCCOPY);
  } else {
    // A mask: 1 for the face color and white, 0 for the image.
    HDC hdcMono = ::CreateCompatibleDC(hdc);
    HBITMAP hbmMono = ::CreateBitmap(cx, cy, 1, 1, NULL);
    if (hdcMono != NULL && hbmMono != NULL) {
      HGDIOBJ hOldMono = ::SelectObject(hdcMono, hbmMono);
      ::SetBkColor(hdcImage, ::GetSysColor(COLOR_BTNFACE));
      ::BitBlt(hdcMono, 0, 0, cx, cy, hdcImage, iImage * cx, 0, SRCCOPY);
      ::SetBkColor(hdcImage, RGB(255, 255, 255));
      ::BitBlt(hdcMono, 0, 0, cx, cy, hdcImage, iImage * cx, 0, SRCPAINT);
      // PSDPxax: the brush where the mask is 0, the screen where it is 1.
      const DWORD ropPSDPxax = 0x00B8074AL;
      COLORREF clrText = ::SetTextColor(hdc, RGB(0, 0, 0));
      COLORREF clrBk = ::SetBkColor(hdc, RGB(255, 255, 255));
      HBRUSH hbrHilite = ::CreateSolidBrush(::GetSysColor(COLOR_BTNHIGHLIGHT));
      HBRUSH hbrShadow = ::CreateSolidBrush(::GetSysColor(COLOR_BTNSHADOW));
      HGDIOBJ hOldBrush = ::SelectObject(hdc, hbrHilite);
      ::BitBlt(hdc, x + 1, y + 1, cx, cy, hdcMono, 0, 0, ropPSDPxax);
      ::SelectObject(hdc, hbrShadow);
      ::BitBlt(hdc, x, y, cx, cy, hdcMono, 0, 0, ropPSDPxax);
      ::SelectObject(hdc, hOldBrush);
      ::DeleteObject(hbrHilite);
      ::DeleteObject(hbrShadow);
      ::SetTextColor(hdc, clrText);
      ::SetBkColor(hdc, clrBk);
      ::SelectObject(hdcMono, hOldMono);
    }
    if (hbmMono != NULL) ::DeleteObject(hbmMono);
    if (hdcMono != NULL) ::DeleteDC(hdcMono);
  }
  ::SelectObject(hdcImage, hOldImage);
  ::DeleteDC(hdcImage);
}

/// Fills rc with the dither of a checked button: the face color and
/// the highlight color in alternate pixels.
void BarFillChecked(HDC hdc, const RECT& rc) {
  static const WORD aPattern[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
  HBITMAP hbm = ::CreateBitmap(8, 8, 1, 1, aPattern);
  HBRUSH hbr = hbm != NULL ? ::CreatePatternBrush(hbm) : NULL;
  if (hbr == NULL) {
    HBRUSH hbrSolid = ::CreateSolidBrush(::GetSysColor(COLOR_BTNHIGHLIGHT));
    ::FillRect(hdc, &rc, hbrSolid);
    ::DeleteObject(hbrSolid);
  } else {
    COLORREF clrText = ::SetTextColor(hdc, ::GetSysColor(COLOR_BTNFACE));
    COLORREF clrBk = ::SetBkColor(hdc, ::GetSysColor(COLOR_BTNHIGHLIGHT));
    ::FillRect(hdc, &rc, hbr);
    ::SetTextColor(hdc, clrText);
    ::SetBkColor(hdc, clrBk);
    ::DeleteObject(hbr);
  }
  if (hbm != NULL) ::DeleteObject(hbm);
}

/// Draws the button i: its edge, its face and its image.
void BarPaintButton(W16Bar* pBar, HDC hdc, int i) {
  const TBBUTTON& b = pBar->buttons[i];
  RECT rc = pBar->rects[i];
  if ((b.fsState & TBSTATE_HIDDEN) || ::IsRectEmpty(&rc)) return;
  BOOL bFlat = (::GetWindowLong(pBar->hwnd, GWL_STYLE) & TBSTYLE_FLAT) != 0;
  if (b.fsStyle & TBSTYLE_SEP) {
    if (bFlat && !(b.fsState & TBSTATE_WRAP)) {
      // An etched line in the middle of the separator.
      int x = (rc.left + rc.right) / 2 - 1;
      RECT rcLine = {x, rc.top + 2, x + 2, rc.bottom - 2};
      ::DrawEdge(hdc, &rcLine, EDGE_ETCHED, BF_LEFT);
    }
    return;
  }
  BOOL bEnabled = (b.fsState & TBSTATE_ENABLED) != 0;
  BOOL bPressed = (b.fsState & TBSTATE_PRESSED) || (i == pBar->iPressed && pBar->bPressedIn);
  BOOL bChecked = (b.fsState & TBSTATE_CHECKED) != 0;
  BOOL bDown = bPressed || bChecked;
  if (bChecked && !bPressed) {
    RECT rcFace = rc;
    ::InflateRect(&rcFace, -2, -2);
    BarFillChecked(hdc, rcFace);
  }
  if (bDown)
    ::DrawEdge(hdc, &rc, bFlat ? BDR_SUNKENOUTER : EDGE_SUNKEN, BF_RECT);
  else if (!bFlat)
    ::DrawEdge(hdc, &rc, EDGE_RAISED, BF_RECT);
  int x = rc.left + (rc.right - rc.left - pBar->sizeImage.cx) / 2;
  int y = rc.top + (rc.bottom - rc.top - pBar->sizeImage.cy) / 2;
  if (bDown) {
    x++;
    y++;
  }
  BarDrawImage(pBar, hdc, b.iBitmap, x, y, bEnabled && !(b.fsState & TBSTATE_INDETERMINATE));
}

/// Sends the command of the button i to the notify window, packed as a
/// 16-bit WM_COMMAND with BN_CLICKED.
void BarClick(W16Bar* pBar, int i) {
  TBBUTTON& b = pBar->buttons[i];
  if (b.fsStyle & TBSTYLE_CHECK) {
    b.fsState ^= TBSTATE_CHECKED;
    if ((b.fsStyle & TBSTYLE_GROUP) && (b.fsState & TBSTATE_CHECKED)) {
      // Clear the other buttons of the group.
      for (int j = i - 1; j >= 0 && (pBar->buttons[j].fsStyle & TBSTYLE_GROUP); j--)
        pBar->buttons[j].fsState &= ~TBSTATE_CHECKED;
      for (int j = i + 1; j < pBar->buttons.GetSize() && (pBar->buttons[j].fsStyle & TBSTYLE_GROUP);
           j++)
        pBar->buttons[j].fsState &= ~TBSTATE_CHECKED;
    }
  }
  HWND hwndTarget = pBar->hwndNotify != NULL ? pBar->hwndNotify : ::GetParent(pBar->hwnd);
  if (hwndTarget != NULL)
    ::SendMessage(hwndTarget, WM_COMMAND, (WPARAM)b.idCommand,
                  MAKELPARAM((WORD)(UINT_PTR)pBar->hwnd, BN_CLICKED));
}

/// Adds the strings of a double-null-terminated list, or the string
/// resource nID of hInst. Gives the index of the first new string.
LRESULT BarAddStrings(W16Bar* pBar, HINSTANCE hInst, LPARAM lParam) {
  int iFirst = (int)pBar->strings.GetSize();
  if (HIWORD(lParam) == 0) {
    char sz[256];
    if (::LoadString(hInst, LOWORD(lParam), sz, sizeof(sz)) <= 0) return -1;
    pBar->strings.Add(sz);
    return iFirst;
  }
  for (LPCSTR p = (LPCSTR)lParam; *p != 0; p += lstrlen(p) + 1) pBar->strings.Add(p);
  return iFirst;
}

/// Does the messages of the toolbar window.
LRESULT BarMessage(W16Bar* pBar, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case TB_BUTTONSTRUCTSIZE:
      return 0;
    case TB_SETPARENT: {
      HWND hwndOld = pBar->hwndNotify;
      pBar->hwndNotify = (HWND)wParam;
      return (LRESULT)(UINT_PTR)hwndOld;
    }
    case TB_SETBITMAPSIZE:
      pBar->sizeImage.cx = LOWORD(lParam);
      pBar->sizeImage.cy = HIWORD(lParam);
      BarInvalidate(pBar);
      return TRUE;
    case TB_SETBUTTONSIZE:
      pBar->sizeButton.cx = LOWORD(lParam);
      pBar->sizeButton.cy = HIWORD(lParam);
      BarChanged(pBar);
      return TRUE;
    case TB_ADDBITMAP: {
      // Only a bitmap handle (hInst NULL) is supported.
      const TBADDBITMAP FAR* p = (const TBADDBITMAP FAR*)lParam;
      if (p == NULL || p->hInst != NULL) return -1;
      int iFirst = pBar->nImages;
      pBar->hbmImages = (HBITMAP)p->nID;
      pBar->nImages += (int)wParam;
      BarInvalidate(pBar);
      return iFirst;
    }
    case TB_REPLACEBITMAP: {
      const TBREPLACEBITMAP FAR* p = (const TBREPLACEBITMAP FAR*)lParam;
      if (p == NULL || p->hInstNew != NULL) return FALSE;
      pBar->hbmImages = (HBITMAP)p->nIDNew;
      pBar->nImages = p->nButtons;
      BarInvalidate(pBar);
      return TRUE;
    }
    case TB_ADDBUTTONS: {
      const TBBUTTON FAR* p = (const TBBUTTON FAR*)lParam;
      if (p == NULL) return FALSE;
      for (UINT i = 0; i < (UINT)wParam; i++) pBar->buttons.Add(p[i]);
      BarChanged(pBar);
      return TRUE;
    }
    case TB_INSERTBUTTON: {
      const TBBUTTON FAR* p = (const TBBUTTON FAR*)lParam;
      int i = (int)wParam;
      if (p == NULL || i < 0) return FALSE;
      if (i > pBar->buttons.GetSize()) i = (int)pBar->buttons.GetSize();
      pBar->buttons.InsertAt(i, *p);
      BarChanged(pBar);
      return TRUE;
    }
    case TB_DELETEBUTTON: {
      int i = (int)wParam;
      if (i < 0 || i >= pBar->buttons.GetSize()) return FALSE;
      pBar->buttons.RemoveAt(i);
      if (pBar->iPressed == i) pBar->iPressed = -1;
      BarChanged(pBar);
      return TRUE;
    }
    case TB_GETBUTTON: {
      int i = (int)wParam;
      if (i < 0 || i >= pBar->buttons.GetSize() || lParam == 0) return FALSE;
      *(TBBUTTON FAR*)lParam = pBar->buttons[i];
      return TRUE;
    }
    case TB_BUTTONCOUNT:
      return pBar->buttons.GetSize();
    case TB_COMMANDTOINDEX:
      return BarIndexOf(pBar, (int)wParam);
    case TB_GETITEMRECT: {
      int i = (int)wParam;
      if (i < 0 || i >= pBar->rects.GetSize() || lParam == 0) return FALSE;
      if (pBar->buttons[i].fsState & TBSTATE_HIDDEN) return FALSE;
      *(RECT FAR*)lParam = pBar->rects[i];
      return TRUE;
    }
    case TB_SETSTATE: {
      int i = BarIndexOf(pBar, (int)wParam);
      if (i < 0) return FALSE;
      BYTE fsOld = pBar->buttons[i].fsState;
      pBar->buttons[i].fsState = (BYTE)LOWORD(lParam);
      if ((fsOld ^ pBar->buttons[i].fsState) & (TBSTATE_HIDDEN | TBSTATE_WRAP)) BarLayout(pBar);
      BarInvalidate(pBar);
      return TRUE;
    }
    case TB_GETSTATE: {
      int i = BarIndexOf(pBar, (int)wParam);
      return i < 0 ? -1 : pBar->buttons[i].fsState;
    }
    case TB_HITTEST:
      if (lParam == 0) return -1;
      return BarHitTest(pBar, *(const POINT FAR*)lParam);
    case TB_ADDSTRING:
      return BarAddStrings(pBar, (HINSTANCE)wParam, lParam);
    case TB_GETBUTTONTEXT: {
      int i = BarIndexOf(pBar, (int)wParam);
      if (i < 0) return -1;
      INT_PTR iString = pBar->buttons[i].iString;
      if (iString < 0 || iString >= pBar->strings.GetSize()) return 0;
      const CString& str = pBar->strings[iString];
      if (lParam != 0) lstrcpy((LPSTR)lParam, str);
      return str.GetLength();
    }
    case WM_SETREDRAW:
      pBar->bRedraw = (BOOL)wParam;
      return 0;
    case WM_ERASEBKGND: {
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      HBRUSH hbr = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
      ::FillRect((HDC)wParam, &rc, hbr);
      ::DeleteObject(hbr);
      return TRUE;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      for (int i = 0; i < pBar->buttons.GetSize(); i++) {
        RECT rcAnd;
        if (::IntersectRect(&rcAnd, &pBar->rects[i], &ps.rcPaint)) BarPaintButton(pBar, hdc, i);
      }
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      int i = BarHitTest(pBar, pt);
      if (i < 0 || !(pBar->buttons[i].fsState & TBSTATE_ENABLED)) return 0;
      pBar->iPressed = i;
      pBar->bPressedIn = TRUE;
      ::SetCapture(hWnd);
      ::InvalidateRect(hWnd, &pBar->rects[i], TRUE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (pBar->iPressed < 0) return 0;
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      BOOL bIn = ::PtInRect(&pBar->rects[pBar->iPressed], pt);
      if (bIn != pBar->bPressedIn) {
        pBar->bPressedIn = bIn;
        ::InvalidateRect(hWnd, &pBar->rects[pBar->iPressed], TRUE);
      }
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_CANCELMODE: {
      int i = pBar->iPressed;
      if (i < 0) return 0;
      BOOL bClick = nMsg == WM_LBUTTONUP && pBar->bPressedIn;
      pBar->iPressed = -1;
      pBar->bPressedIn = FALSE;
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      ::InvalidateRect(hWnd, &pBar->rects[i], TRUE);
      if (bClick) BarClick(pBar, i);
      return 0;
    }
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the toolbar class.
LRESULT CALLBACK W16BarProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Bar* pBar = StateOf<W16Bar>(hWnd);
  if (nMsg == WM_NCCREATE) {
    pBar = new W16Bar;
    pBar->hwnd = hWnd;
    pBar->hwndNotify = NULL;
    pBar->sizeButton.cx = 24;
    pBar->sizeButton.cy = 22;
    pBar->sizeImage.cx = 16;
    pBar->sizeImage.cy = 15;
    pBar->hbmImages = NULL;
    pBar->nImages = 0;
    pBar->iPressed = -1;
    pBar->bPressedIn = FALSE;
    pBar->bRedraw = TRUE;
    ::SetWindowLong(hWnd, 0, (LONG)pBar);
  } else if (nMsg == WM_NCDESTROY) {
    ::SetWindowLong(hWnd, 0, 0);
    delete pBar;
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (pBar == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  return BarMessage(pBar, hWnd, nMsg, wParam, lParam);
}

// ---------------------------------------------------------------------
// The status bar

enum { kMaxParts = 64 };

/// The data of one status bar window.
struct W16Status {
  HWND hwnd;
  HFONT hFont;                    // WM_SETFONT, or NULL for the default
  int nParts;                     // the number of parts
  int aRight[kMaxParts];          // the right edge of each part (-1: the end)
  UINT aFlags[kMaxParts];         // the SBT_ flags of each part
  CString aText[kMaxParts];       // the text of each part
  DWORD aData[kMaxParts];         // the value of an owner-drawn part
};

const int kStatusBorderY = 2;  // the space above the parts
const int kStatusGap = 2;      // the space between two parts

/// Gives the font of the status bar.
HFONT StatusFont(W16Status* pStatus) {
  return pStatus->hFont != NULL ? pStatus->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the width of the size grip, or 0 when the bar has none or its
/// parent is maximized.
int StatusGripWidth(W16Status* pStatus) {
  if (!(::GetWindowLong(pStatus->hwnd, GWL_STYLE) & SBARS_SIZEGRIP)) return 0;
  HWND hwndParent = ::GetParent(pStatus->hwnd);
  if (hwndParent != NULL && ::IsZoomed(hwndParent)) return 0;
  return ::GetSystemMetrics(SM_CXVSCROLL);
}

/// Gives the rectangle of the part i. Gives FALSE for a bad index.
BOOL StatusPartRect(W16Status* pStatus, int i, RECT* pRect) {
  if (i < 0 || i >= pStatus->nParts) return FALSE;
  RECT rcClient;
  ::GetClientRect(pStatus->hwnd, &rcClient);
  int nLeft = i == 0 ? 0 : pStatus->aRight[i - 1] + kStatusGap;
  int nRight = pStatus->aRight[i];
  if (nRight < 0 || nRight > rcClient.right) nRight = rcClient.right;
  if (i > 0 && pStatus->aRight[i - 1] < 0) nLeft = nRight;
  ::SetRect(pRect, nLeft, kStatusBorderY, nRight, rcClient.bottom);
  return TRUE;
}

/// Draws the size grip: three diagonal lines in the corner.
void StatusPaintGrip(W16Status* pStatus, HDC hdc) {
  int cx = StatusGripWidth(pStatus);
  if (cx == 0) return;
  RECT rc;
  ::GetClientRect(pStatus->hwnd, &rc);
  HPEN hpenHilite = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_BTNHIGHLIGHT));
  HPEN hpenShadow = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_BTNSHADOW));
  HGDIOBJ hOld = ::SelectObject(hdc, hpenShadow);
  for (int d = 3; d < cx && d < rc.bottom; d += 4) {
    ::SelectObject(hdc, hpenShadow);
    ::MoveTo(hdc, rc.right - d, rc.bottom - 1);
    ::LineTo(hdc, rc.right, rc.bottom - 1 - d);
    ::MoveTo(hdc, rc.right - d + 1, rc.bottom - 1);
    ::LineTo(hdc, rc.right, rc.bottom - d);
    ::SelectObject(hdc, hpenHilite);
    ::MoveTo(hdc, rc.right - d - 1, rc.bottom - 1);
    ::LineTo(hdc, rc.right, rc.bottom - 2 - d);
  }
  ::SelectObject(hdc, hOld);
  ::DeleteObject(hpenHilite);
  ::DeleteObject(hpenShadow);
}

/// Draws the part i: its edge and its text, or sends WM_DRAWITEM to the
/// parent for an owner-drawn part.
void StatusPaintPart(W16Status* pStatus, HDC hdc, int i) {
  RECT rc;
  if (!StatusPartRect(pStatus, i, &rc) || rc.right <= rc.left) return;
  UINT nFlags = pStatus->aFlags[i];
  if (nFlags & SBT_POPOUT)
    ::DrawEdge(hdc, &rc, BDR_RAISEDINNER, BF_RECT);
  else if (!(nFlags & SBT_NOBORDERS))
    ::DrawEdge(hdc, &rc, BDR_SUNKENOUTER, BF_RECT);
  ::InflateRect(&rc, -1, -1);
  if (nFlags & SBT_OWNERDRAW) {
    DRAWITEMSTRUCT dis;
    memset(&dis, 0, sizeof(dis));
    dis.CtlID = ::GetDlgCtrlID(pStatus->hwnd);
    dis.itemID = i;
    dis.itemAction = ODA_DRAWENTIRE;
    dis.hwndItem = pStatus->hwnd;
    dis.hDC = hdc;
    dis.rcItem = rc;
    dis.itemData = pStatus->aData[i];
    ::SendMessage(::GetParent(pStatus->hwnd), WM_DRAWITEM, dis.CtlID, (LPARAM)(LPDRAWITEMSTRUCT)&dis);
    return;
  }
  rc.left += 2;
  rc.right -= 1;
  HGDIOBJ hOldFont = ::SelectObject(hdc, StatusFont(pStatus));
  int nOldMode = ::SetBkMode(hdc, TRANSPARENT);
  COLORREF clrOld = ::SetTextColor(hdc, ::GetSysColor(COLOR_BTNTEXT));
  ::DrawText(hdc, pStatus->aText[i], -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  ::SetTextColor(hdc, clrOld);
  ::SetBkMode(hdc, nOldMode);
  ::SelectObject(hdc, hOldFont);
}

/// Redraws the part i.
void StatusInvalidatePart(W16Status* pStatus, int i) {
  RECT rc;
  if (StatusPartRect(pStatus, i, &rc)) ::InvalidateRect(pStatus->hwnd, &rc, TRUE);
}

/// Gives TRUE when pt (client) is on the size grip.
BOOL StatusOnGrip(W16Status* pStatus, POINT pt) {
  int cx = StatusGripWidth(pStatus);
  if (cx == 0) return FALSE;
  RECT rc;
  ::GetClientRect(pStatus->hwnd, &rc);
  return pt.x >= rc.right - cx && pt.y >= rc.bottom - cx && pt.x < rc.right && pt.y < rc.bottom;
}

/// Does the messages of the status bar window.
LRESULT StatusMessage(W16Status* pStatus, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case SB_SETPARTS: {
      int n = (int)wParam;
      if (n <= 0 || n > kMaxParts || lParam == 0) return FALSE;
      const int FAR* p = (const int FAR*)lParam;
      for (int i = 0; i < n; i++) pStatus->aRight[i] = p[i];
      for (int i = pStatus->nParts; i < n; i++) {
        pStatus->aFlags[i] = 0;
        pStatus->aText[i].Empty();
        pStatus->aData[i] = 0;
      }
      pStatus->nParts = n;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case SB_GETPARTS: {
      int n = (int)wParam;
      if (lParam != 0)
        for (int i = 0; i < n && i < pStatus->nParts; i++) ((int FAR*)lParam)[i] = pStatus->aRight[i];
      return pStatus->nParts;
    }
    case SB_SETTEXT: {
      int i = (int)(wParam & 0xFF);
      if (i >= pStatus->nParts) return FALSE;
      pStatus->aFlags[i] = (UINT)wParam & 0xFF00;
      if (wParam & SBT_OWNERDRAW) {
        pStatus->aData[i] = (DWORD)lParam;
        pStatus->aText[i].Empty();
      } else {
        pStatus->aData[i] = 0;
        pStatus->aText[i] = lParam != 0 ? (LPCSTR)lParam : "";
      }
      StatusInvalidatePart(pStatus, i);
      return TRUE;
    }
    case SB_GETTEXT:
    case SB_GETTEXTLENGTH: {
      int i = (int)wParam;
      if (i < 0 || i >= pStatus->nParts) return 0;
      if (pStatus->aFlags[i] & SBT_OWNERDRAW) return (LRESULT)pStatus->aData[i];
      int nLen = pStatus->aText[i].GetLength();
      if (nMsg == SB_GETTEXT && lParam != 0) lstrcpy((LPSTR)lParam, pStatus->aText[i]);
      return MAKELONG(nLen, pStatus->aFlags[i]);
    }
    case SB_GETRECT:
      if (lParam == 0) return FALSE;
      return StatusPartRect(pStatus, (int)wParam, (RECT FAR*)lParam);
    case SB_GETBORDERS: {
      int FAR* p = (int FAR*)lParam;
      if (p == NULL) return FALSE;
      p[0] = 0;
      p[1] = kStatusBorderY;
      p[2] = kStatusGap;
      return TRUE;
    }
    case WM_SETFONT:
      pStatus->hFont = (HFONT)wParam;
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT_PTR)pStatus->hFont;
    case WM_SIZE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_ERASEBKGND: {
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      HBRUSH hbr = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
      ::FillRect((HDC)wParam, &rc, hbr);
      ::DeleteObject(hbr);
      return TRUE;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      for (int i = 0; i < pStatus->nParts; i++) StatusPaintPart(pStatus, hdc, i);
      StatusPaintGrip(pStatus, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SETCURSOR: {
      POINT pt;
      ::GetCursorPos(&pt);
      ::ScreenToClient(hWnd, &pt);
      if ((HWND)wParam == hWnd && StatusOnGrip(pStatus, pt)) {
        ::SetCursor(::LoadCursor(NULL, IDC_SIZENWSE));
        return TRUE;
      }
      break;
    }
    case WM_LBUTTONDOWN: {
      // A press on the grip sizes the parent from its lower-right corner.
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      HWND hwndParent = ::GetParent(hWnd);
      if (hwndParent == NULL || !StatusOnGrip(pStatus, pt)) break;
      ::ClientToScreen(hWnd, &pt);
      const UINT kSizeBottomRight = 8;  // WMSZ_BOTTOMRIGHT
      ::SendMessage(hwndParent, WM_SYSCOMMAND, SC_SIZE | kSizeBottomRight, MAKELPARAM(pt.x, pt.y));
      return 0;
    }
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the status bar class.
LRESULT CALLBACK W16StatusProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Status* pStatus = StateOf<W16Status>(hWnd);
  if (nMsg == WM_NCCREATE) {
    pStatus = new W16Status;
    pStatus->hwnd = hWnd;
    pStatus->hFont = NULL;
    pStatus->nParts = 1;
    pStatus->aRight[0] = -1;
    pStatus->aFlags[0] = 0;
    pStatus->aData[0] = 0;
    ::SetWindowLong(hWnd, 0, (LONG)pStatus);
  } else if (nMsg == WM_NCDESTROY) {
    ::SetWindowLong(hWnd, 0, 0);
    delete pStatus;
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (pStatus == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  return StatusMessage(pStatus, hWnd, nMsg, wParam, lParam);
}

/// Registers one class of the module with the procedure pfn and four
/// bytes of window data. Gives TRUE when the class is there.
BOOL RegisterBarClass(LPCSTR lpszClass, WNDPROC pfn) {
  WNDCLASS wc;
  HINSTANCE hInst = AfxGetInstanceHandle();
  if (::GetClassInfo(hInst, lpszClass, &wc) && wc.lpfnWndProc == pfn) return TRUE;
  memset(&wc, 0, sizeof(wc));
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = pfn;
  wc.cbWndExtra = sizeof(LONG);
  wc.hInstance = hInst;
  wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = lpszClass;
  return ::RegisterClass(&wc) != 0;
}

}  // namespace

/// Registers the toolbar and status bar classes that CToolBar and
/// CStatusBar make on 16-bit Windows. Gives TRUE when both are there.
BOOL AFXAPI _AfxW16RegisterBarClasses() {
  return RegisterBarClass(TOOLBARCLASSNAME, W16BarProc) &&
         RegisterBarClass(STATUSCLASSNAME, W16StatusProc);
}

#endif  // _AFX_WIN16
