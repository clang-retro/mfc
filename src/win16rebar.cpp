// win16rebar.cpp - the rebar window for 16-bit Windows, which has no
// comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The rebar does the RB_ messages of comctl32.dll in ANSI and sends the
// RBN_ notifications to its parent, or to the window of RB_SETPARENT.
// It puts its bands in rows. Each band has a gripper, an image and a
// text (the header), and a child window. The user drags a gripper to
// change the size of a band or to move it to another row. A click on a
// gripper makes a band as large or as small as it can be. The rebar
// also gives the notifications and the commands of the child windows to
// its parent.
//
// The layout is in "row" coordinates: x is along a row and y goes
// across the rows. With CCS_VERT, the rebar changes x and y when it
// draws and when it gets a point.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>
#include <stddef.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

// RBN_AUTOSIZE gives this structure. The commctrl.h of the library does
// not have it.
struct W16NMRBAUTOSIZE {
  NMHDR hdr;
  BOOL fChanged;
  RECT rcTarget;
  RECT rcActual;
};

const int kGripLen = 3;     // the width of the gripper bar
const int kGripSpace = 4;   // the space after the gripper
const int kHeaderPad = 2;   // the space at each end of the header
const int kBorder = 2;      // the width of a band border (RBS_BANDBORDERS)
const int kChildEdge = 2;   // the space above and below a child (RBBS_CHILDEDGE)
const int kChevronLen = 13; // the width of the chevron button
const int kDivider = 2;     // the height of the divider (no CCS_NODIVIDER)

// The data of a band.
struct W16Band {
  UINT fStyle;
  COLORREF clrFore, clrBack;
  CString text;
  int iImage;
  HWND hwndChild;
  UINT cxMinChild, cyMinChild, cx;
  HBITMAP hbmBack;
  UINT wID;
  UINT cyChild, cyMaxChild, cyIntegral, cxIdeal;
  LPARAM lParam;
  UINT cxHeader;
  BOOL bHeaderSet;  // RBBIM_HEADERSIZE gave cxHeader
  // The layout, in row coordinates.
  int iRow;
  RECT rcBand, rcGrip, rcChild, rcChevron;
  int cxHead;  // the width of the header
  BOOL bChevron;
  /// Starts as a band with no child, no text and no image.
  W16Band()
      : fStyle(0), clrFore(CLR_DEFAULT), clrBack(CLR_DEFAULT), iImage(-1), hwndChild(NULL),
        cxMinChild(0), cyMinChild(0), cx(0), hbmBack(NULL), wID(0), cyChild(0), cyMaxChild(0),
        cyIntegral(0), cxIdeal(0), lParam(0), cxHeader(0), bHeaderSet(FALSE), iRow(0),
        cxHead(0), bChevron(FALSE) {
    ::SetRectEmpty(&rcBand);
    ::SetRectEmpty(&rcGrip);
    ::SetRectEmpty(&rcChild);
    ::SetRectEmpty(&rcChevron);
  }
};

// The data of a rebar.
struct W16ReBar {
  HWND hwnd;
  CPtrArray bands;     // the W16Band of each band, in order
  HIMAGELIST himl;     // RB_SETBARINFO
  HWND hwndNotify;     // RB_SETPARENT, or NULL for the parent
  HWND hwndTip;        // RB_SETTOOLTIPS
  COLORREF clrBk, clrText;
  COLORSCHEME scheme;
  HFONT hFont;
  HPALETTE hPal;
  DWORD dwExStyle;
  int nRows;
  CWordArray rowTop, rowHeight;
  int cyBar;           // the depth of all rows
  int iDrag;           // the band that the user drags, or -1
  POINT ptDown;        // where the drag started, in row coordinates
  int xGrab;           // the distance from the left of the band to the mouse
  BOOL bMoved;         // the drag changed the layout
  int iChevron;        // the band whose chevron is pushed, or -1
  BOOL bInLayout;
  /// Starts with no bands and the default colors.
  W16ReBar()
      : hwnd(NULL), himl(NULL), hwndNotify(NULL), hwndTip(NULL), clrBk(CLR_DEFAULT),
        clrText(CLR_DEFAULT), hFont(NULL), hPal(NULL), dwExStyle(0), nRows(0), cyBar(0),
        iDrag(-1), xGrab(0), bMoved(FALSE), iChevron(-1), bInLayout(FALSE) {
    ptDown.x = ptDown.y = 0;
    scheme.dwSize = sizeof(scheme);
    scheme.clrBtnHighlight = CLR_DEFAULT;
    scheme.clrBtnShadow = CLR_DEFAULT;
  }
};

/// Gives a band of a rebar.
W16Band* RbBand(W16ReBar* p, int i) { return (W16Band*)p->bands[i]; }

/// Gives the number of bands of a rebar.
int RbCount(W16ReBar* p) { return (int)p->bands.GetSize(); }

/// Gives TRUE when the rebar is vertical (CCS_VERT).
BOOL RbVert(W16ReBar* p) { return (StyleOf(p->hwnd) & CCS_VERT) != 0; }

/// Gives TRUE when a band is shown.
BOOL RbVisible(W16Band* b) { return (b->fStyle & RBBS_HIDDEN) == 0; }

/// Changes a rectangle from row coordinates to window coordinates, or
/// back. The change is the same in the two directions.
RECT RbFlip(W16ReBar* p, const RECT& rc) {
  if (!RbVert(p)) return rc;
  RECT r = {rc.top, rc.left, rc.bottom, rc.right};
  return r;
}

/// Changes a point from window coordinates to row coordinates.
POINT RbFlipPt(W16ReBar* p, POINT pt) {
  if (RbVert(p)) {
    int t = pt.x;
    pt.x = pt.y;
    pt.y = t;
  }
  return pt;
}

/// Gives the space before the first row: the divider, when the rebar
/// has one.
int RbTop(W16ReBar* p) { return (StyleOf(p->hwnd) & CCS_NODIVIDER) ? 0 : kDivider; }

/// Gives the length of the rows: the width of the client area, or its
/// height with CCS_VERT.
int RbRowLen(W16ReBar* p) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  return RbVert(p) ? rc.bottom : rc.right;
}

/// Gives the window that gets the notifications of the rebar.
HWND RbNotifyWnd(W16ReBar* p) {
  return p->hwndNotify != NULL ? p->hwndNotify : ::GetParent(p->hwnd);
}

/// Sends WM_NOTIFY to the window of the notifications of the rebar.
/// Gives its result.
LRESULT RbNotify(W16ReBar* p, UINT nCode, NMHDR* pHdr) {
  pHdr->hwndFrom = p->hwnd;
  pHdr->idFrom = (UINT)::GetDlgCtrlID(p->hwnd);
  pHdr->code = nCode;
  return ::SendMessage(RbNotifyWnd(p), WM_NOTIFY, (WPARAM)pHdr->idFrom, (LPARAM)pHdr);
}

/// Sends an NMREBAR notification for a band (RBN_BEGINDRAG, RBN_ENDDRAG,
/// RBN_DELETINGBAND and RBN_DELETEDBAND). Gives the result.
LRESULT RbNotifyBand(W16ReBar* p, UINT nCode, int iBand) {
  NMREBAR nm;
  memset(&nm, 0, sizeof(nm));
  nm.uBand = (UINT)iBand;
  if (iBand >= 0 && iBand < RbCount(p)) {
    W16Band* b = RbBand(p, iBand);
    nm.dwMask = RBNM_ID | RBNM_STYLE | RBNM_LPARAM;
    nm.fStyle = b->fStyle;
    nm.wID = b->wID;
    nm.lParam = b->lParam;
  }
  return RbNotify(p, nCode, &nm.hdr);
}

/// Gives the font of the rebar, or the stock font when it has none.
HFONT RbFont(W16ReBar* p) {
  return p->hFont != NULL ? p->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the color of the highlight edges: the color scheme, or the
/// system color.
COLORREF RbHighlight(W16ReBar* p) {
  return p->scheme.clrBtnHighlight != CLR_DEFAULT ? p->scheme.clrBtnHighlight
                                                  : ::GetSysColor(COLOR_BTNHIGHLIGHT);
}

/// Gives the color of the shadow edges: the color scheme, or the system
/// color.
COLORREF RbShadow(W16ReBar* p) {
  return p->scheme.clrBtnShadow != CLR_DEFAULT ? p->scheme.clrBtnShadow
                                               : ::GetSysColor(COLOR_BTNSHADOW);
}

/// Gives the size of the images of the image list, or 0 by 0.
SIZE RbImageSize(W16ReBar* p) {
  SIZE size = {0, 0};
  int cx = 0, cy = 0;
  if (p->himl != NULL && ImageList_GetIconSize(p->himl, &cx, &cy)) {
    size.cx = cx;
    size.cy = cy;
  }
  return size;
}

/// Gives the width and the height of the text of a band, in the font of
/// the rebar.
SIZE RbTextSize(W16ReBar* p, const CString& str) {
  SIZE size = {0, 0};
  if (str.IsEmpty()) return size;
  HDC hdc = ::GetDC(p->hwnd);
  HFONT hOld = (HFONT)::SelectObject(hdc, RbFont(p));
  DWORD dw = ::GetTextExtent(hdc, str, str.GetLength());
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(p->hwnd, hdc);
  size.cx = LOWORD(dw);
  size.cy = HIWORD(dw);
  return size;
}

/// Gives TRUE when a band has a gripper. RBBS_NOGRIPPER removes it, and
/// RBBS_GRIPPERALWAYS keeps it. Else a band of fixed size, or the only
/// band that shows, has no gripper.
BOOL RbHasGripper(W16ReBar* p, W16Band* b) {
  if (b->fStyle & RBBS_NOGRIPPER) return FALSE;
  if (b->fStyle & RBBS_GRIPPERALWAYS) return TRUE;
  if (b->fStyle & RBBS_FIXEDSIZE) return FALSE;
  int nVisible = 0;
  for (int i = 0; i < RbCount(p); i++)
    if (RbVisible(RbBand(p, i))) nVisible++;
  return nVisible > 1;
}

/// Gives the width of the header of a band: the gripper, the image and
/// the text, or the width of RBBIM_HEADERSIZE.
int RbHeaderLen(W16ReBar* p, W16Band* b) {
  if (b->bHeaderSet) return (int)b->cxHeader;
  int n = kHeaderPad;
  if (RbHasGripper(p, b)) n += kGripLen + kGripSpace;
  SIZE sizeImage = RbImageSize(p);
  if (b->iImage >= 0 && sizeImage.cx > 0) n += (RbVert(p) ? sizeImage.cy : sizeImage.cx) + 2;
  if (!b->text.IsEmpty() && !(b->fStyle & RBBS_HIDETITLE)) {
    SIZE sizeText = RbTextSize(p, b->text);
    n += (RbVert(p) ? sizeText.cy : sizeText.cx) + 4;
  }
  return n + kHeaderPad;
}

/// Gives TRUE when a band can show a chevron (RBBS_USECHEVRON with an
/// ideal size).
BOOL RbCanChevron(W16Band* b) { return (b->fStyle & RBBS_USECHEVRON) && b->cxIdeal > 0; }

/// Gives the smallest width of a band: its header and the smallest width
/// of its child.
int RbMinLen(W16ReBar* p, W16Band* b) {
  int n = RbHeaderLen(p, b) + (int)b->cxMinChild;
  if (RbCanChevron(b) && b->cxIdeal > b->cxMinChild) n += kChevronLen;
  return n;
}

/// Gives the width that a band asks for: cx, but not less than its
/// smallest width.
int RbWantLen(W16ReBar* p, W16Band* b) { return (int)MaxL((long)b->cx, RbMinLen(p, b)); }

/// Gives the depth of a band across its row: its child, its header and
/// the edges.
int RbBandDepth(W16ReBar* p, W16Band* b) {
  long n = b->cyMinChild;
  if ((b->fStyle & RBBS_VARIABLEHEIGHT) && b->cyChild > n) {
    n = b->cyChild;
    if (b->cyMaxChild > 0) n = MinL(n, MaxL(b->cyMaxChild, b->cyMinChild));
  }
  if (b->fStyle & RBBS_CHILDEDGE) n += 2 * kChildEdge;
  SIZE sizeImage = RbImageSize(p);
  if (b->iImage >= 0 && sizeImage.cx > 0)
    n = MaxL(n, (RbVert(p) ? sizeImage.cx : sizeImage.cy) + 4);
  if (!b->text.IsEmpty() && !(b->fStyle & RBBS_HIDETITLE)) {
    SIZE sizeText = RbTextSize(p, b->text);
    n = MaxL(n, (RbVert(p) ? sizeText.cx : sizeText.cy) + 4);
  }
  return (int)MaxL(n, 8);
}

/// Puts the bands of one row (iFirst to iLast, the visible bands only)
/// along the row: the last band gets the space that is left over, and a
/// row that is too long takes space from the last bands first.
void RbLayoutRow(W16ReBar* p, int iFirst, int iLast, int nLen) {
  CWordArray widths;
  long nSum = 0;
  for (int i = iFirst; i <= iLast; i++) {
    W16Band* b = RbBand(p, i);
    int w = RbVisible(b) ? RbWantLen(p, b) : 0;
    widths.Add((WORD)w);
    nSum += w;
  }
  int iLastVisible = -1;
  for (int i = iLast; i >= iFirst; i--)
    if (RbVisible(RbBand(p, i))) {
      iLastVisible = i;
      break;
    }
  if (iLastVisible < 0) return;
  if (nSum < nLen) {
    widths[iLastVisible - iFirst] = (WORD)(widths[iLastVisible - iFirst] + (nLen - nSum));
  } else {
    for (int i = iLastVisible; i >= iFirst && nSum > nLen; i--) {
      W16Band* b = RbBand(p, i);
      if (!RbVisible(b)) continue;
      int nMin = RbMinLen(p, b);
      int nCut = (int)MinL(nSum - nLen, (long)widths[i - iFirst] - nMin);
      if (nCut <= 0) continue;
      widths[i - iFirst] = (WORD)(widths[i - iFirst] - nCut);
      nSum -= nCut;
    }
  }
  int x = 0;
  for (int i = iFirst; i <= iLast; i++) {
    W16Band* b = RbBand(p, i);
    if (!RbVisible(b)) continue;
    b->rcBand.left = x;
    b->rcBand.right = x + (int)widths[i - iFirst];
    x = b->rcBand.right;
  }
}

/// Places the parts of a band in its rectangle: the gripper, the child
/// and the chevron.
void RbLayoutBand(W16ReBar* p, W16Band* b, BOOL bFirstInRow) {
  RECT rc = b->rcBand;
  if (!bFirstInRow && (StyleOf(p->hwnd) & RBS_BANDBORDERS)) rc.left += kBorder;
  b->cxHead = RbHeaderLen(p, b);
  ::SetRectEmpty(&b->rcGrip);
  if (RbHasGripper(p, b)) {
    b->rcGrip.left = rc.left + kHeaderPad;
    b->rcGrip.right = b->rcGrip.left + kGripLen;
    b->rcGrip.top = rc.top + 2;
    b->rcGrip.bottom = rc.bottom - 2;
  }
  int xChild = rc.left + b->cxHead;
  int xEnd = rc.right;
  b->bChevron = FALSE;
  ::SetRectEmpty(&b->rcChevron);
  if (RbCanChevron(b) && (long)(xEnd - xChild) < (long)b->cxIdeal) {
    b->bChevron = TRUE;
    b->rcChevron.left = xEnd - kChevronLen;
    b->rcChevron.right = xEnd;
    b->rcChevron.top = rc.top + 1;
    b->rcChevron.bottom = rc.bottom - 1;
    xEnd -= kChevronLen;
  }
  if (xEnd < xChild) xEnd = xChild;
  b->rcChild.left = xChild;
  b->rcChild.right = xEnd;
  int nEdge = (b->fStyle & RBBS_CHILDEDGE) ? kChildEdge : 0;
  int nRoom = (rc.bottom - rc.top) - 2 * nEdge;
  int nHeight = (int)b->cyMinChild;
  if (b->fStyle & RBBS_VARIABLEHEIGHT) {
    nHeight = nRoom;
    if (b->cyMaxChild > 0 && (long)nHeight > (long)b->cyMaxChild)
      nHeight = (int)MaxL(b->cyMaxChild, b->cyMinChild);
    if (b->cyIntegral > 1 && (long)nHeight > (long)b->cyMinChild)
      nHeight = (int)(b->cyMinChild +
                      ((nHeight - b->cyMinChild) / b->cyIntegral) * b->cyIntegral);
  }
  if (nHeight > nRoom) nHeight = nRoom;
  if (nHeight < 0) nHeight = 0;
  int yTop = rc.top + nEdge;
  if (!(b->fStyle & RBBS_TOPALIGN)) yTop += (nRoom - nHeight) / 2;
  b->rcChild.top = yTop;
  b->rcChild.bottom = yTop + nHeight;
}

/// Changes the size of the rebar to the depth of its rows, unless it has
/// CCS_NORESIZE. Without CCS_NOPARENTALIGN, the rebar also goes to the
/// top, the bottom, the left or the right of its parent, across all of
/// it.
void RbAutoSize(W16ReBar* p) {
  DWORD dwStyle = StyleOf(p->hwnd);
  if (dwStyle & CCS_NORESIZE) return;
  int nDepth = p->cyBar + RbTop(p);
  RECT rcWnd;
  ::GetWindowRect(p->hwnd, &rcWnd);
  HWND hwndParent = ::GetParent(p->hwnd);
  POINT pt = {rcWnd.left, rcWnd.top};
  if (hwndParent != NULL && (dwStyle & WS_CHILD)) ::ScreenToClient(hwndParent, &pt);
  int x = pt.x, y = pt.y;
  int cx = rcWnd.right - rcWnd.left, cy = rcWnd.bottom - rcWnd.top;
  BOOL bVert = (dwStyle & CCS_VERT) != 0;
  if (bVert)
    cx = nDepth;
  else
    cy = nDepth;
  if (!(dwStyle & CCS_NOPARENTALIGN) && hwndParent != NULL) {
    RECT rcParent;
    ::GetClientRect(hwndParent, &rcParent);
    BOOL bEnd = (dwStyle & 3) == CCS_BOTTOM;  // CCS_BOTTOM, or CCS_RIGHT with CCS_VERT
    if (bVert) {
      y = 0;
      cy = rcParent.bottom;
      if (!(dwStyle & CCS_NOMOVEY)) x = bEnd ? rcParent.right - cx : 0;
    } else {
      x = 0;
      cx = rcParent.right;
      if (!(dwStyle & CCS_NOMOVEY)) y = bEnd ? rcParent.bottom - cy : 0;
    }
  }
  if (cx != rcWnd.right - rcWnd.left || cy != rcWnd.bottom - rcWnd.top || x != pt.x || y != pt.y)
    ::SetWindowPos(p->hwnd, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

/// Moves each child window to its band, and hides the child of a band
/// that does not show. The parent gets RBN_CHILDSIZE for each band, and
/// can change the rectangle of the child.
void RbPlaceChildren(W16ReBar* p) {
  for (int i = 0; i < RbCount(p); i++) {
    W16Band* b = RbBand(p, i);
    if (b->hwndChild == NULL || !::IsWindow(b->hwndChild)) continue;
    if (!RbVisible(b)) {
      ::ShowWindow(b->hwndChild, SW_HIDE);
      continue;
    }
    NMREBARCHILDSIZE nm;
    memset(&nm, 0, sizeof(nm));
    nm.uBand = (UINT)i;
    nm.wID = b->wID;
    nm.rcChild = RbFlip(p, b->rcChild);
    nm.rcBand = RbFlip(p, b->rcBand);
    RbNotify(p, RBN_CHILDSIZE, &nm.hdr);
    RECT rc = nm.rcChild;
    ::SetWindowPos(b->hwndChild, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

/// Puts the bands in rows for a length of row, and gives the rows their
/// depths. Does not move the child windows.
void RbArrange(W16ReBar* p, int nLen) {
  DWORD dwStyle = StyleOf(p->hwnd);
  int n = RbCount(p);
  // The rows: a band starts a new row with RBBS_BREAK, or when the row
  // does not have the space for its smallest width.
  int iRow = -1;
  long xRow = 0;
  for (int i = 0; i < n; i++) {
    W16Band* b = RbBand(p, i);
    if (!RbVisible(b)) {
      b->iRow = iRow < 0 ? 0 : iRow;
      continue;
    }
    int nMin = RbMinLen(p, b);
    if (iRow < 0 || (b->fStyle & RBBS_BREAK) ||
        (xRow > 0 && xRow + nMin > nLen)) {
      iRow++;
      xRow = 0;
    }
    b->iRow = iRow;
    xRow += nMin;
  }
  p->nRows = iRow + 1;
  p->rowTop.SetSize(p->nRows);
  p->rowHeight.SetSize(p->nRows);
  int nAll = 0;
  for (int r = 0; r < p->nRows; r++) {
    int nDepth = 0;
    for (int i = 0; i < n; i++) {
      W16Band* b = RbBand(p, i);
      if (RbVisible(b) && b->iRow == r) nDepth = (int)MaxL(nDepth, RbBandDepth(p, b));
    }
    p->rowHeight[r] = (WORD)nDepth;
    nAll = (int)MaxL(nAll, nDepth);
  }
  // Without RBS_VARHEIGHT, all the rows are as deep as the deepest row.
  if (!(dwStyle & RBS_VARHEIGHT))
    for (int r = 0; r < p->nRows; r++) p->rowHeight[r] = (WORD)nAll;
  int y = RbTop(p);
  for (int r = 0; r < p->nRows; r++) {
    if (r > 0 && (dwStyle & RBS_BANDBORDERS)) y += kBorder;
    p->rowTop[r] = (WORD)y;
    y += p->rowHeight[r];
  }
  p->cyBar = p->nRows > 0 ? y - RbTop(p) : 0;
  // The bands along each row.
  for (int r = 0; r < p->nRows; r++) {
    int iFirst = -1, iLast = -1;
    for (int i = 0; i < n; i++)
      if (RbBand(p, i)->iRow == r && RbVisible(RbBand(p, i))) {
        if (iFirst < 0) iFirst = i;
        iLast = i;
      }
    if (iFirst < 0) continue;
    RbLayoutRow(p, iFirst, iLast, nLen);
    BOOL bFirst = TRUE;
    for (int i = iFirst; i <= iLast; i++) {
      W16Band* b = RbBand(p, i);
      if (!RbVisible(b)) continue;
      b->rcBand.top = (int)p->rowTop[r];
      b->rcBand.bottom = b->rcBand.top + (int)p->rowHeight[r];
      RbLayoutBand(p, b, bFirst);
      bFirst = FALSE;
    }
  }
}

/// Lays out the rebar again: the rows, the bands and the child windows.
/// The parent gets RBN_HEIGHTCHANGE when the depth of the rows changes.
void RbLayout(W16ReBar* p) {
  if (p->bInLayout) return;
  p->bInLayout = TRUE;
  int cyOld = p->cyBar;
  RbArrange(p, RbRowLen(p));
  BOOL bChanged = p->cyBar != cyOld;
  if (bChanged) RbAutoSize(p);
  // A new size can give the rows a new length.
  RbArrange(p, RbRowLen(p));
  RbPlaceChildren(p);
  ::InvalidateRect(p->hwnd, NULL, TRUE);
  p->bInLayout = FALSE;
  if (bChanged) {
    NMHDR nm;
    RbNotify(p, RBN_HEIGHTCHANGE, &nm);
  }
}

/// Gives the size of a band information structure up to the end of a
/// field.
#define RB_HAS(prbbi, field) \
  ((prbbi)->cbSize >= offsetof(REBARBANDINFOA, field) + sizeof((prbbi)->field))

/// Copies the fields of fMask from a band information structure to a
/// band. A new child window becomes a child of the rebar, and the old
/// child is hidden.
void RbSetInfo(W16ReBar* p, W16Band* b, const REBARBANDINFOA FAR* pInfo) {
  UINT fMask = pInfo->fMask;
  if (fMask & RBBIM_STYLE) b->fStyle = pInfo->fStyle;
  if (fMask & RBBIM_COLORS) {
    b->clrFore = pInfo->clrFore;
    b->clrBack = pInfo->clrBack;
  }
  if (fMask & RBBIM_TEXT) b->text = pInfo->lpText != NULL ? pInfo->lpText : "";
  if (fMask & RBBIM_IMAGE) b->iImage = pInfo->iImage;
  if (fMask & RBBIM_CHILD) {
    if (b->hwndChild != NULL && b->hwndChild != pInfo->hwndChild && ::IsWindow(b->hwndChild))
      ::ShowWindow(b->hwndChild, SW_HIDE);
    b->hwndChild = pInfo->hwndChild;
    if (b->hwndChild != NULL && ::GetParent(b->hwndChild) != p->hwnd)
      ::SetParent(b->hwndChild, p->hwnd);
  }
  if (fMask & RBBIM_CHILDSIZE) {
    b->cxMinChild = pInfo->cxMinChild;
    b->cyMinChild = pInfo->cyMinChild;
    if (RB_HAS(pInfo, cyIntegral)) {
      b->cyChild = pInfo->cyChild;
      b->cyMaxChild = pInfo->cyMaxChild;
      b->cyIntegral = pInfo->cyIntegral;
    }
  }
  if (fMask & RBBIM_SIZE) b->cx = pInfo->cx;
  if (fMask & RBBIM_BACKGROUND) b->hbmBack = pInfo->hbmBack;
  if (fMask & RBBIM_ID) b->wID = pInfo->wID;
  if ((fMask & RBBIM_IDEALSIZE) && RB_HAS(pInfo, cxIdeal)) b->cxIdeal = pInfo->cxIdeal;
  if ((fMask & RBBIM_LPARAM) && RB_HAS(pInfo, lParam)) b->lParam = pInfo->lParam;
  if ((fMask & RBBIM_HEADERSIZE) && RB_HAS(pInfo, cxHeader)) {
    b->cxHeader = pInfo->cxHeader;
    b->bHeaderSet = TRUE;
  }
}

/// Copies the fields of fMask from a band to a band information
/// structure. The size is the width of the band in the layout.
void RbGetInfo(W16ReBar* p, W16Band* b, REBARBANDINFOA FAR* pInfo) {
  UINT fMask = pInfo->fMask;
  if (fMask & RBBIM_STYLE) pInfo->fStyle = b->fStyle;
  if (fMask & RBBIM_COLORS) {
    pInfo->clrFore = b->clrFore;
    pInfo->clrBack = b->clrBack;
  }
  if ((fMask & RBBIM_TEXT) && pInfo->lpText != NULL && pInfo->cch > 0) {
    UINT n = (UINT)MinL(b->text.GetLength(), (long)pInfo->cch - 1);
    _fmemcpy(pInfo->lpText, (LPCSTR)b->text, n);
    pInfo->lpText[n] = '\0';
  }
  if (fMask & RBBIM_IMAGE) pInfo->iImage = b->iImage;
  if (fMask & RBBIM_CHILD) pInfo->hwndChild = b->hwndChild;
  if (fMask & RBBIM_CHILDSIZE) {
    pInfo->cxMinChild = b->cxMinChild;
    pInfo->cyMinChild = b->cyMinChild;
    if (RB_HAS(pInfo, cyIntegral)) {
      pInfo->cyChild = b->cyChild;
      pInfo->cyMaxChild = b->cyMaxChild;
      pInfo->cyIntegral = b->cyIntegral;
    }
  }
  if (fMask & RBBIM_SIZE)
    pInfo->cx = RbVisible(b) && b->rcBand.right > b->rcBand.left
                    ? (UINT)(b->rcBand.right - b->rcBand.left)
                    : b->cx;
  if (fMask & RBBIM_BACKGROUND) pInfo->hbmBack = b->hbmBack;
  if (fMask & RBBIM_ID) pInfo->wID = b->wID;
  if ((fMask & RBBIM_IDEALSIZE) && RB_HAS(pInfo, cxIdeal)) pInfo->cxIdeal = b->cxIdeal;
  if ((fMask & RBBIM_LPARAM) && RB_HAS(pInfo, lParam)) pInfo->lParam = b->lParam;
  if ((fMask & RBBIM_HEADERSIZE) && RB_HAS(pInfo, cxHeader))
    pInfo->cxHeader = (UINT)RbHeaderLen(p, b);
}

/// Removes a band. The parent gets RBN_DELETINGBAND and RBN_DELETEDBAND,
/// and the child window of the band is hidden.
BOOL RbDelete(W16ReBar* p, int i) {
  if (i < 0 || i >= RbCount(p)) return FALSE;
  RbNotifyBand(p, RBN_DELETINGBAND, i);
  W16Band* b = RbBand(p, i);
  if (b->hwndChild != NULL && ::IsWindow(b->hwndChild)) ::ShowWindow(b->hwndChild, SW_HIDE);
  NMREBAR nm;
  memset(&nm, 0, sizeof(nm));
  nm.dwMask = RBNM_ID | RBNM_STYLE | RBNM_LPARAM;
  nm.uBand = (UINT)i;
  nm.fStyle = b->fStyle;
  nm.wID = b->wID;
  nm.lParam = b->lParam;
  p->bands.RemoveAt(i);
  delete b;
  if (p->iDrag == i) p->iDrag = -1;
  RbNotify(p, RBN_DELETEDBAND, &nm.hdr);
  RbLayout(p);
  return TRUE;
}

/// Gives the bands of the row of a band, from the first to the last that
/// show.
void RbRowRange(W16ReBar* p, int iRow, int* piFirst, int* piLast) {
  *piFirst = *piLast = -1;
  for (int i = 0; i < RbCount(p); i++) {
    W16Band* b = RbBand(p, i);
    if (!RbVisible(b) || b->iRow != iRow) continue;
    if (*piFirst < 0) *piFirst = i;
    *piLast = i;
  }
}

/// Makes a band as wide as its row lets it be (RB_MAXIMIZEBAND). With
/// bIdeal, the band gets its ideal width when the row has the space.
void RbMaximize(W16ReBar* p, int i, BOOL bIdeal) {
  if (i < 0 || i >= RbCount(p)) return;
  W16Band* b = RbBand(p, i);
  if (!RbVisible(b)) return;
  int iFirst, iLast;
  RbRowRange(p, b->iRow, &iFirst, &iLast);
  long nOthers = 0;
  for (int k = iFirst; k <= iLast; k++) {
    W16Band* o = RbBand(p, k);
    if (k == i || !RbVisible(o)) continue;
    nOthers += RbMinLen(p, o);
  }
  long nMax = MaxL(RbRowLen(p) - nOthers, RbMinLen(p, b));
  if (bIdeal && b->cxIdeal > 0) nMax = MinL(nMax, RbHeaderLen(p, b) + (long)b->cxIdeal);
  for (int k = iFirst; k <= iLast; k++) {
    W16Band* o = RbBand(p, k);
    if (k != i && RbVisible(o)) o->cx = (UINT)RbMinLen(p, o);
  }
  b->cx = (UINT)nMax;
  // The space that is left goes to the band after it, or else to the
  // band before it.
  long nLeft = RbRowLen(p) - nOthers - nMax;
  if (nLeft > 0) {
    int kTo = -1;
    for (int k = i + 1; k <= iLast && kTo < 0; k++)
      if (RbVisible(RbBand(p, k))) kTo = k;
    for (int k = i - 1; k >= iFirst && kTo < 0; k--)
      if (RbVisible(RbBand(p, k))) kTo = k;
    if (kTo >= 0) RbBand(p, kTo)->cx += (UINT)nLeft;
  }
  RbLayout(p);
}

/// Makes a band as small as it can be (RB_MINIMIZEBAND). The band before
/// it, or else the band after it, gets the space.
void RbMinimize(W16ReBar* p, int i) {
  if (i < 0 || i >= RbCount(p)) return;
  W16Band* b = RbBand(p, i);
  if (!RbVisible(b)) return;
  int iFirst, iLast;
  RbRowRange(p, b->iRow, &iFirst, &iLast);
  int nOld = b->rcBand.right - b->rcBand.left;
  int nMin = RbMinLen(p, b);
  b->cx = (UINT)nMin;
  int kTo = -1;
  for (int k = i - 1; k >= iFirst && kTo < 0; k--)
    if (RbVisible(RbBand(p, k))) kTo = k;
  for (int k = i + 1; k <= iLast && kTo < 0; k++)
    if (RbVisible(RbBand(p, k))) kTo = k;
  if (kTo >= 0 && nOld > nMin) {
    W16Band* o = RbBand(p, kTo);
    o->cx = (UINT)((o->rcBand.right - o->rcBand.left) + (nOld - nMin));
  }
  RbLayout(p);
}

/// Toggles a band between its largest and its smallest width, as a click
/// on its gripper does.
void RbToggle(W16ReBar* p, int i) {
  W16Band* b = RbBand(p, i);
  if (b->rcBand.right - b->rcBand.left > RbMinLen(p, b))
    RbMinimize(p, i);
  else
    RbMaximize(p, i, TRUE);
}

/// Moves a band from one position to another (RB_MOVEBAND).
BOOL RbMove(W16ReBar* p, int iFrom, int iTo) {
  int n = RbCount(p);
  if (iFrom < 0 || iFrom >= n || iTo < 0 || iTo >= n) return FALSE;
  if (iFrom == iTo) return TRUE;
  void* pBand = p->bands[iFrom];
  p->bands.RemoveAt(iFrom);
  p->bands.InsertAt(iTo, pBand);
  RbLayout(p);
  return TRUE;
}

/// Finds the band and its part at a point in window coordinates. Gives
/// the index of the band, or -1, and an RBHT_ code in *pnFlags.
int RbHitTest(W16ReBar* p, POINT ptWnd, UINT* pnFlags) {
  POINT pt = RbFlipPt(p, ptWnd);
  for (int i = 0; i < RbCount(p); i++) {
    W16Band* b = RbBand(p, i);
    if (!RbVisible(b) || !::PtInRect(&b->rcBand, pt)) continue;
    if (!::IsRectEmpty(&b->rcGrip) && pt.x < b->rcGrip.right + kGripSpace / 2 &&
        pt.x >= b->rcBand.left)
      *pnFlags = RBHT_GRABBER;
    else if (b->bChevron && ::PtInRect(&b->rcChevron, pt))
      *pnFlags = RBHT_CHEVRON;
    else if (pt.x < b->rcChild.left)
      *pnFlags = RBHT_CAPTION;
    else
      *pnFlags = RBHT_CLIENT;
    return i;
  }
  *pnFlags = RBHT_NOWHERE;
  return -1;
}

/// Starts a drag of a band at a point in row coordinates. The parent can
/// refuse it with RBN_BEGINDRAG. Gives TRUE when the drag starts.
BOOL RbBeginDrag(W16ReBar* p, int i, POINT pt) {
  if (i < 0 || i >= RbCount(p) || !RbVisible(RbBand(p, i))) return FALSE;
  if (RbNotifyBand(p, RBN_BEGINDRAG, i) != 0) return FALSE;
  p->iDrag = i;
  p->ptDown = pt;
  p->xGrab = pt.x - RbBand(p, i)->rcBand.left;
  p->bMoved = FALSE;
  return TRUE;
}

/// Moves the band of the drag to another row when the point leaves its
/// row. A point above the first row or below the last row makes a new
/// row, when the band is not alone in its row. Gives TRUE when the band
/// moved.
BOOL RbDragRow(W16ReBar* p, POINT pt) {
  if (StyleOf(p->hwnd) & RBS_FIXEDORDER) return FALSE;
  W16Band* b = RbBand(p, p->iDrag);
  int iRow = b->iRow;
  if (pt.y >= b->rcBand.top && pt.y < b->rcBand.bottom) return FALSE;
  int iFirst, iLast;
  RbRowRange(p, iRow, &iFirst, &iLast);
  BOOL bAlone = iFirst == iLast;
  int iTarget = -1;  // the row to go to; -2 is a new first row, nRows a new last row
  if (pt.y < (int)p->rowTop[0]) {
    if (bAlone && iRow == 0) return FALSE;
    iTarget = iRow == 0 ? -2 : 0;
  } else if (pt.y >= (int)p->rowTop[p->nRows - 1] + (int)p->rowHeight[p->nRows - 1]) {
    if (bAlone && iRow == p->nRows - 1) return FALSE;
    iTarget = iRow == p->nRows - 1 ? p->nRows : p->nRows - 1;
  } else {
    for (int r = 0; r < p->nRows; r++)
      if (pt.y >= (int)p->rowTop[r] && pt.y < (int)p->rowTop[r] + (int)p->rowHeight[r]) iTarget = r;
    if (iTarget < 0 || iTarget == iRow) return FALSE;
  }
  // Give the rows breaks that keep them as they are, take the band out,
  // and put it in its new row.
  for (int i = 0; i < RbCount(p); i++) {
    W16Band* o = RbBand(p, i);
    int f, l;
    RbRowRange(p, o->iRow, &f, &l);
    if (i == f && RbVisible(o))
      o->fStyle |= RBBS_BREAK;
    else
      o->fStyle &= ~RBBS_BREAK;
  }
  void* pBand = p->bands[p->iDrag];
  p->bands.RemoveAt(p->iDrag);
  if (!bAlone && iFirst == p->iDrag && p->iDrag < RbCount(p)) RbBand(p, p->iDrag)->fStyle |= RBBS_BREAK;
  int iInsert;
  if (iTarget == -2) {
    iInsert = 0;
    b->fStyle |= RBBS_BREAK;
    if (RbCount(p) > 0) RbBand(p, 0)->fStyle |= RBBS_BREAK;
  } else if (iTarget == p->nRows) {
    iInsert = RbCount(p);
    b->fStyle |= RBBS_BREAK;
  } else {
    // Before the first band of the row that starts right of the point.
    iInsert = -1;
    int iRowLast = -1;
    for (int i = 0; i < RbCount(p); i++) {
      W16Band* o = RbBand(p, i);
      if (!RbVisible(o) || o->iRow != iTarget) continue;
      if (iInsert < 0 && o->rcBand.left + (o->rcBand.right - o->rcBand.left) / 2 > pt.x)
        iInsert = i;
      iRowLast = i;
    }
    if (iInsert < 0) iInsert = iRowLast + 1;
    b->fStyle &= ~RBBS_BREAK;
    if (iInsert < RbCount(p)) {
      W16Band* o = RbBand(p, iInsert);
      if (o->iRow == iTarget && (o->fStyle & RBBS_BREAK)) {
        // The band becomes the first band of the row.
        o->fStyle &= ~RBBS_BREAK;
        b->fStyle |= RBBS_BREAK;
      }
    }
  }
  p->bands.InsertAt(iInsert, pBand);
  if (RbCount(p) > 0) RbBand(p, 0)->fStyle &= ~RBBS_BREAK;
  p->iDrag = iInsert;
  RbLayout(p);
  return TRUE;
}

/// Moves the left edge of the band of the drag along its row to a point
/// in row coordinates. The band before it gets wider or narrower, and
/// the bands after it make space.
void RbDragAlong(W16ReBar* p, POINT pt) {
  W16Band* b = RbBand(p, p->iDrag);
  int iFirst, iLast;
  RbRowRange(p, b->iRow, &iFirst, &iLast);
  if (p->iDrag == iFirst) return;
  int iPrev = -1;
  for (int k = p->iDrag - 1; k >= iFirst && iPrev < 0; k--)
    if (RbVisible(RbBand(p, k))) iPrev = k;
  if (iPrev < 0) return;
  W16Band* prev = RbBand(p, iPrev);
  long nAfterMin = 0;
  for (int k = p->iDrag; k <= iLast; k++)
    if (RbVisible(RbBand(p, k))) nAfterMin += RbMinLen(p, RbBand(p, k));
  long xNew = ClampL(pt.x - p->xGrab, prev->rcBand.left + RbMinLen(p, prev),
                     MaxL(RbRowLen(p) - nAfterMin, prev->rcBand.left + RbMinLen(p, prev)));
  if (xNew == b->rcBand.left) return;
  int xRight = b->rcBand.right;
  prev->cx = (UINT)(xNew - prev->rcBand.left);
  b->cx = (UINT)MaxL(xRight - xNew, RbMinLen(p, b));
  p->bMoved = TRUE;
  RbLayout(p);
}

/// Moves the drag to a point in row coordinates (RB_DRAGMOVE and the
/// mouse).
void RbDragMove(W16ReBar* p, POINT pt) {
  if (p->iDrag < 0 || p->iDrag >= RbCount(p)) return;
  if (RbDragRow(p, pt)) {
    p->bMoved = TRUE;
    return;
  }
  RbDragAlong(p, pt);
}

/// Ends the drag. The parent gets RBN_ENDDRAG, and RBN_LAYOUTCHANGED when
/// the drag changed the layout.
void RbEndDrag(W16ReBar* p) {
  if (p->iDrag < 0) return;
  int i = p->iDrag;
  p->iDrag = -1;
  RbNotifyBand(p, RBN_ENDDRAG, i);
  if (p->bMoved) {
    NMHDR nm;
    RbNotify(p, RBN_LAYOUTCHANGED, &nm);
  }
}

/// Gives a point of a drag message in row coordinates: lParam is a point
/// on the screen (the low word x), or (DWORD)-1 for the mouse now.
POINT RbDragPoint(W16ReBar* p, LPARAM lParam) {
  POINT pt;
  if ((DWORD)lParam == (DWORD)-1) {
    ::GetCursorPos(&pt);
  } else {
    pt.x = (int)(short)LOWORD(lParam);
    pt.y = (int)(short)HIWORD(lParam);
  }
  ::ScreenToClient(p->hwnd, &pt);
  return RbFlipPt(p, pt);
}

/// Sends RBN_CHEVRONPUSHED for the chevron of a band, with the rectangle
/// of the chevron in window coordinates.
void RbPushChevron(W16ReBar* p, int i, LPARAM lParamNM) {
  if (i < 0 || i >= RbCount(p)) return;
  W16Band* b = RbBand(p, i);
  NMREBARCHEVRON nm;
  memset(&nm, 0, sizeof(nm));
  nm.uBand = (UINT)i;
  nm.wID = b->wID;
  nm.lParam = b->lParam;
  nm.rc = RbFlip(p, b->rcChevron);
  nm.lParamNM = lParamNM;
  p->iChevron = i;
  ::InvalidateRect(p->hwnd, &nm.rc, TRUE);
  ::UpdateWindow(p->hwnd);
  RbNotify(p, RBN_CHEVRONPUSHED, &nm.hdr);
  p->iChevron = -1;
  ::InvalidateRect(p->hwnd, &nm.rc, TRUE);
}

/// Fills a rectangle with a bitmap, again and again from an origin.
void RbTile(HDC hdc, HBITMAP hbm, const RECT& rc, int xOrg, int yOrg) {
  BITMAP bm;
  if (::GetObject(hbm, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0) return;
  HDC hdcMem = ::CreateCompatibleDC(hdc);
  HBITMAP hOld = (HBITMAP)::SelectObject(hdcMem, hbm);
  int x0 = xOrg + ((rc.left - xOrg) / bm.bmWidth) * bm.bmWidth;
  int y0 = yOrg + ((rc.top - yOrg) / bm.bmHeight) * bm.bmHeight;
  if (x0 > rc.left) x0 -= bm.bmWidth;
  if (y0 > rc.top) y0 -= bm.bmHeight;
  for (int y = y0; y < rc.bottom; y += bm.bmHeight)
    for (int x = x0; x < rc.right; x += bm.bmWidth) {
      int l = (int)MaxL(x, rc.left), t = (int)MaxL(y, rc.top);
      int r = (int)MinL(x + bm.bmWidth, rc.right), b = (int)MinL(y + bm.bmHeight, rc.bottom);
      if (r > l && b > t) ::BitBlt(hdc, l, t, r - l, b - t, hdcMem, l - x, t - y, SRCCOPY);
    }
  ::SelectObject(hdcMem, hOld);
  ::DeleteDC(hdcMem);
}

/// Draws a raised bar in a rectangle in window coordinates: the
/// highlight on the top and left, the shadow on the bottom and right.
void RbRaised(W16ReBar* p, HDC hdc, const RECT& rc) {
  COLORREF clrHi = RbHighlight(p), clrSh = RbShadow(p);
  DrawLine(hdc, rc.left, rc.top, rc.right - 1, rc.top, clrHi);
  DrawLine(hdc, rc.left, rc.top, rc.left, rc.bottom - 1, clrHi);
  DrawLine(hdc, rc.right - 1, rc.top, rc.right - 1, rc.bottom, clrSh);
  DrawLine(hdc, rc.left, rc.bottom - 1, rc.right, rc.bottom - 1, clrSh);
}

/// Draws an etched line across the rows (bVertical) or along them, from
/// a point in row coordinates.
void RbEtch(W16ReBar* p, HDC hdc, int x, int y, int nLen, BOOL bAcross) {
  RECT rc;
  if (bAcross) {
    RECT r = {x, y, x + kBorder, y + nLen};
    rc = RbFlip(p, r);
  } else {
    RECT r = {x, y, x + nLen, y + kBorder};
    rc = RbFlip(p, r);
  }
  COLORREF clrHi = RbHighlight(p), clrSh = RbShadow(p);
  if (rc.right - rc.left == kBorder) {
    DrawLine(hdc, rc.left, rc.top, rc.left, rc.bottom, clrSh);
    DrawLine(hdc, rc.left + 1, rc.top, rc.left + 1, rc.bottom, clrHi);
  } else {
    DrawLine(hdc, rc.left, rc.top, rc.right, rc.top, clrSh);
    DrawLine(hdc, rc.left, rc.top + 1, rc.right, rc.top + 1, clrHi);
  }
}

/// Draws a band: its background, its gripper, its image, its text and
/// its chevron. The child window draws itself.
void RbPaintBand(W16ReBar* p, HDC hdc, int i) {
  W16Band* b = RbBand(p, i);
  RECT rcBand = RbFlip(p, b->rcBand);
  if (b->hbmBack != NULL) {
    if (b->fStyle & RBBS_FIXEDBMP)
      RbTile(hdc, b->hbmBack, rcBand, rcBand.left, rcBand.top);
    else
      RbTile(hdc, b->hbmBack, rcBand, 0, 0);
  } else if (b->clrBack != CLR_DEFAULT) {
    FillColor(hdc, rcBand, b->clrBack);
  }
  if (!::IsRectEmpty(&b->rcGrip)) RbRaised(p, hdc, RbFlip(p, b->rcGrip));
  int x = b->rcBand.left + kHeaderPad;
  if (!::IsRectEmpty(&b->rcGrip)) x = b->rcGrip.right + kGripSpace;
  SIZE sizeImage = RbImageSize(p);
  if (b->iImage >= 0 && sizeImage.cx > 0 && !b->bHeaderSet) {
    int nAlong = RbVert(p) ? sizeImage.cy : sizeImage.cx;
    int nAcross = RbVert(p) ? sizeImage.cx : sizeImage.cy;
    RECT r = {x, b->rcBand.top + (b->rcBand.bottom - b->rcBand.top - nAcross) / 2, 0, 0};
    RECT rcImage = RbFlip(p, r);
    ImageList_Draw(p->himl, b->iImage, hdc, rcImage.left, rcImage.top, ILD_TRANSPARENT);
    x += nAlong + 2;
  }
  if (!b->text.IsEmpty() && !(b->fStyle & RBBS_HIDETITLE) && x < b->rcChild.left) {
    RECT r = {x, b->rcBand.top, b->rcChild.left, b->rcBand.bottom};
    RECT rcText = RbFlip(p, r);
    HFONT hOld = (HFONT)::SelectObject(hdc, RbFont(p));
    COLORREF clr = b->clrFore != CLR_DEFAULT
                       ? b->clrFore
                       : (p->clrText != CLR_DEFAULT ? p->clrText : ::GetSysColor(COLOR_BTNTEXT));
    ::SetTextColor(hdc, clr);
    ::SetBkMode(hdc, TRANSPARENT);
    ::DrawText(hdc, b->text, b->text.GetLength(), &rcText,
               DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    ::SelectObject(hdc, hOld);
  }
  if (b->bChevron) {
    RECT rc = RbFlip(p, b->rcChevron);
    if (p->iChevron == i) DrawButtonFace(hdc, rc, TRUE);
    // Two arrows, one after the other along the row.
    RECT r1 = rc, r2 = rc;
    if (RbVert(p)) {
      r1.bottom = (rc.top + rc.bottom) / 2 + 1;
      r2.top = r1.bottom - 3;
    } else {
      r1.right = (rc.left + rc.right) / 2 + 1;
      r2.left = r1.right - 3;
    }
    int nDir = RbVert(p) ? 1 : 3;
    DrawArrow(hdc, r1, nDir, TRUE, p->iChevron == i);
    DrawArrow(hdc, r2, nDir, TRUE, p->iChevron == i);
  }
}

/// Draws the rebar: the background, the divider, the bands and the
/// borders between them.
void RbPaint(W16ReBar* p, HDC hdc) {
  RECT rc;
  ::GetClientRect(p->hwnd, &rc);
  HPALETTE hOldPal = NULL;
  if (p->hPal != NULL) {
    hOldPal = ::SelectPalette(hdc, p->hPal, FALSE);
    ::RealizePalette(hdc);
  }
  FillColor(hdc, rc, p->clrBk != CLR_DEFAULT ? p->clrBk : ::GetSysColor(COLOR_BTNFACE));
  DWORD dwStyle = StyleOf(p->hwnd);
  if (!(dwStyle & CCS_NODIVIDER)) RbEtch(p, hdc, 0, 0, RbRowLen(p), FALSE);
  for (int i = 0; i < RbCount(p); i++)
    if (RbVisible(RbBand(p, i))) RbPaintBand(p, hdc, i);
  if (dwStyle & RBS_BANDBORDERS) {
    for (int r = 1; r < p->nRows; r++)
      RbEtch(p, hdc, 0, (int)p->rowTop[r] - kBorder, RbRowLen(p), FALSE);
    for (int r = 0; r < p->nRows; r++) {
      int iFirst, iLast;
      RbRowRange(p, r, &iFirst, &iLast);
      for (int i = iFirst + 1; i <= iLast && iFirst >= 0; i++) {
        W16Band* b = RbBand(p, i);
        if (RbVisible(b))
          RbEtch(p, hdc, b->rcBand.left, b->rcBand.top, b->rcBand.bottom - b->rcBand.top, TRUE);
      }
    }
  }
  if (hOldPal != NULL) ::SelectPalette(hdc, hOldPal, FALSE);
}

/// Gives the borders of a band (RB_GETBANDBORDERS): the header on the
/// left, and the edges above and below the child.
void RbBandBorders(W16ReBar* p, int i, RECT FAR* prc) {
  W16Band* b = RbBand(p, i);
  int nEdge = (b->fStyle & RBBS_CHILDEDGE) ? kChildEdge : 0;
  prc->left = b->rcChild.left - b->rcBand.left;
  prc->top = nEdge;
  prc->right = b->bChevron ? kChevronLen : 0;
  prc->bottom = nEdge;
}

/// Does the messages of a rebar window.
LRESULT CALLBACK W16ReBarProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16ReBar* p = StateOf<W16ReBar>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16ReBar>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (p == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_CREATE:
      RbLayout(p);
      return 0;
    case WM_NCDESTROY:
      for (int i = 0; i < RbCount(p); i++) delete RbBand(p, i);
      p->bands.RemoveAll();
      DetachState(hWnd, p);
      break;
    case WM_SIZE: {
      RECT rcOld = {0, 0, 0, 0};
      for (int i = 0; i < RbCount(p); i++) ::UnionRect(&rcOld, &rcOld, &RbBand(p, i)->rcBand);
      RbLayout(p);
      if (StyleOf(hWnd) & RBS_AUTOSIZE) {
        W16NMRBAUTOSIZE nm;
        memset(&nm, 0, sizeof(nm));
        ::GetClientRect(hWnd, &nm.rcTarget);
        RECT rcNew = {0, 0, 0, 0};
        for (int i = 0; i < RbCount(p); i++) ::UnionRect(&rcNew, &rcNew, &RbBand(p, i)->rcBand);
        nm.rcActual = RbFlip(p, rcNew);
        nm.fChanged = !::EqualRect(&rcOld, &rcNew);
        RbNotify(p, RBN_AUTOSIZE, &nm.hdr);
      }
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      RbPaint(p, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SETFONT:
      p->hFont = (HFONT)wParam;
      RbLayout(p);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)p->hFont;
    case WM_SYSCOLORCHANGE:
    case WM_WININICHANGE:
      RbLayout(p);
      return 0;
    case WM_NOTIFY:
      // The notifications of the child windows go to the parent.
      return ::SendMessage(RbNotifyWnd(p), WM_NOTIFY, wParam, lParam);
    case WM_COMMAND:
      return ::SendMessage(RbNotifyWnd(p), WM_COMMAND, wParam, lParam);
    case WM_LBUTTONDOWN: {
      POINT ptWnd = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      UINT nFlags;
      int i = RbHitTest(p, ptWnd, &nFlags);
      if (i < 0) return 0;
      if (nFlags == RBHT_CHEVRON) {
        RbPushChevron(p, i, 0);
        return 0;
      }
      if (nFlags != RBHT_GRABBER && nFlags != RBHT_CAPTION) return 0;
      if (RbBeginDrag(p, i, RbFlipPt(p, ptWnd))) ::SetCapture(hWnd);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (p->iDrag >= 0 && ::GetCapture() == hWnd) {
        POINT ptWnd = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
        POINT pt = RbFlipPt(p, ptWnd);
        // A small move is still a click.
        if (!p->bMoved && pt.x - p->ptDown.x < 3 && p->ptDown.x - pt.x < 3 &&
            pt.y - p->ptDown.y < 3 && p->ptDown.y - pt.y < 3)
          return 0;
        RbDragMove(p, pt);
        p->bMoved = TRUE;
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
      if (p->iDrag >= 0) {
        int i = p->iDrag;
        BOOL bClick = !p->bMoved && nMsg == WM_LBUTTONUP;
        if (::GetCapture() == hWnd) ::ReleaseCapture();
        RbEndDrag(p);
        if (bClick && !(StyleOf(hWnd) & RBS_DBLCLKTOGGLE) && i < RbCount(p)) RbToggle(p, i);
      }
      return 0;
    case WM_LBUTTONDBLCLK: {
      if (!(StyleOf(hWnd) & RBS_DBLCLKTOGGLE)) return 0;
      POINT ptWnd = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      UINT nFlags;
      int i = RbHitTest(p, ptWnd, &nFlags);
      if (i >= 0 && (nFlags == RBHT_GRABBER || nFlags == RBHT_CAPTION)) RbToggle(p, i);
      return 0;
    }
    case RB_INSERTBANDA: {
      const REBARBANDINFOA FAR* pInfo = (const REBARBANDINFOA FAR*)lParam;
      if (pInfo == NULL || pInfo->cbSize < offsetof(REBARBANDINFOA, cyChild)) return FALSE;
      int n = RbCount(p);
      int i = (wParam == (WPARAM)-1 || (int)wParam > n) ? n : (int)wParam;
      W16Band* b = new W16Band;
      RbSetInfo(p, b, pInfo);
      p->bands.InsertAt(i, b);
      RbLayout(p);
      return TRUE;
    }
    case RB_DELETEBAND:
      return RbDelete(p, (int)wParam);
    case RB_GETBARINFO: {
      REBARINFO FAR* pInfo = (REBARINFO FAR*)lParam;
      if (pInfo == NULL) return FALSE;
      if (pInfo->fMask & RBIM_IMAGELIST) pInfo->himl = p->himl;
      return TRUE;
    }
    case RB_SETBARINFO: {
      const REBARINFO FAR* pInfo = (const REBARINFO FAR*)lParam;
      if (pInfo == NULL) return FALSE;
      if (pInfo->fMask & RBIM_IMAGELIST) {
        p->himl = pInfo->himl;
        RbLayout(p);
      }
      return TRUE;
    }
    case RB_SETBANDINFOA: {
      const REBARBANDINFOA FAR* pInfo = (const REBARBANDINFOA FAR*)lParam;
      if (pInfo == NULL || (int)wParam < 0 || (int)wParam >= RbCount(p)) return FALSE;
      RbSetInfo(p, RbBand(p, (int)wParam), pInfo);
      RbLayout(p);
      return TRUE;
    }
    case RB_GETBANDINFOA: {
      REBARBANDINFOA FAR* pInfo = (REBARBANDINFOA FAR*)lParam;
      if (pInfo == NULL || (int)wParam < 0 || (int)wParam >= RbCount(p)) return FALSE;
      RbGetInfo(p, RbBand(p, (int)wParam), pInfo);
      return TRUE;
    }
    case RB_INSERTBANDW:
    case RB_SETBANDINFOW:
    case RB_GETBANDINFOW:
      return FALSE;  // the window does its messages in ANSI only
    case RB_SETPARENT: {
      HWND hwndOld = RbNotifyWnd(p);
      p->hwndNotify = (HWND)wParam;
      return (LRESULT)(UINT)hwndOld;
    }
    case RB_HITTEST: {
      RBHITTESTINFO FAR* pInfo = (RBHITTESTINFO FAR*)lParam;
      if (pInfo == NULL) return -1;
      UINT nFlags;
      pInfo->iBand = RbHitTest(p, pInfo->pt, &nFlags);
      pInfo->flags = nFlags;
      return pInfo->iBand;
    }
    case RB_GETRECT: {
      if ((int)wParam < 0 || (int)wParam >= RbCount(p) || lParam == 0) return FALSE;
      *(RECT FAR*)lParam = RbFlip(p, RbBand(p, (int)wParam)->rcBand);
      return TRUE;
    }
    case RB_GETBANDCOUNT:
      return RbCount(p);
    case RB_GETROWCOUNT:
      return p->nRows;
    case RB_GETROWHEIGHT: {
      if ((int)wParam < 0 || (int)wParam >= RbCount(p)) return 0;
      int r = RbBand(p, (int)wParam)->iRow;
      return r < p->nRows ? (LRESULT)p->rowHeight[r] : 0;
    }
    case RB_IDTOINDEX:
      for (int i = 0; i < RbCount(p); i++)
        if (RbBand(p, i)->wID == (UINT)wParam) return i;
      return -1;
    case RB_GETTOOLTIPS:
      return (LRESULT)(UINT)p->hwndTip;
    case RB_SETTOOLTIPS:
      p->hwndTip = (HWND)wParam;
      return 0;
    case RB_SETBKCOLOR: {
      COLORREF clrOld = p->clrBk;
      p->clrBk = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)clrOld;
    }
    case RB_GETBKCOLOR:
      return (LRESULT)p->clrBk;
    case RB_SETTEXTCOLOR: {
      COLORREF clrOld = p->clrText;
      p->clrText = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)clrOld;
    }
    case RB_GETTEXTCOLOR:
      return (LRESULT)p->clrText;
    case RB_SIZETORECT: {
      // The rebar takes the size of the rectangle and lays out its bands
      // again in it.
      RECT FAR* prc = (RECT FAR*)lParam;
      if (prc == NULL) return FALSE;
      int nRowsOld = p->nRows;
      ::SetWindowPos(hWnd, NULL, 0, 0, prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
      RbLayout(p);
      return p->nRows != nRowsOld;
    }
    case RB_BEGINDRAG:
      RbBeginDrag(p, (int)wParam, RbDragPoint(p, lParam));
      return 0;
    case RB_DRAGMOVE:
      if (p->iDrag >= 0) {
        RbDragMove(p, RbDragPoint(p, lParam));
        p->bMoved = TRUE;
      }
      return 0;
    case RB_ENDDRAG:
      RbEndDrag(p);
      return 0;
    case RB_GETBARHEIGHT:
      return p->cyBar + RbTop(p);
    case RB_MINIMIZEBAND:
      RbMinimize(p, (int)wParam);
      return 0;
    case RB_MAXIMIZEBAND:
      RbMaximize(p, (int)wParam, lParam != 0);
      return 0;
    case RB_PUSHCHEVRON:
      RbPushChevron(p, (int)wParam, lParam);
      return 0;
    case RB_GETBANDBORDERS:
      if ((int)wParam < 0 || (int)wParam >= RbCount(p) || lParam == 0) return 0;
      RbBandBorders(p, (int)wParam, (RECT FAR*)lParam);
      return 0;
    case RB_SHOWBAND: {
      if ((int)wParam < 0 || (int)wParam >= RbCount(p)) return FALSE;
      W16Band* b = RbBand(p, (int)wParam);
      if (lParam)
        b->fStyle &= ~RBBS_HIDDEN;
      else
        b->fStyle |= RBBS_HIDDEN;
      RbLayout(p);
      return TRUE;
    }
    case RB_MOVEBAND:
      return RbMove(p, (int)wParam, (int)lParam);
    case RB_SETPALETTE: {
      HPALETTE hOld = p->hPal;
      p->hPal = (HPALETTE)(UINT)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)(UINT)hOld;
    }
    case RB_GETPALETTE:
      return (LRESULT)(UINT)p->hPal;
    case RB_SETCOLORSCHEME: {
      const COLORSCHEME FAR* pcs = (const COLORSCHEME FAR*)lParam;
      if (pcs == NULL) return 0;
      p->scheme.clrBtnHighlight = pcs->clrBtnHighlight;
      p->scheme.clrBtnShadow = pcs->clrBtnShadow;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    }
    case RB_GETCOLORSCHEME: {
      COLORSCHEME FAR* pcs = (COLORSCHEME FAR*)lParam;
      if (pcs == NULL) return FALSE;
      pcs->clrBtnHighlight = RbHighlight(p);
      pcs->clrBtnShadow = RbShadow(p);
      return TRUE;
    }
    case RB_SETEXTENDEDSTYLE: {
      DWORD dwOld = p->dwExStyle;
      DWORD dwMask = wParam != 0 ? (DWORD)wParam : 0xFFFFFFFFUL;
      p->dwExStyle = (p->dwExStyle & ~dwMask) | ((DWORD)lParam & dwMask);
      return (LRESULT)dwOld;
    }
    case RB_GETEXTENDEDSTYLE:
      return (LRESULT)p->dwExStyle;
    case RB_GETDROPTARGET:
      return 0;  // there is no drop target (RBS_REGISTERDROP)
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the rebar class of 16-bit Windows, with the name of the
/// library and the name of comctl32.dll. Gives TRUE when the class is
/// there.
BOOL AFXAPI _AfxW16RegisterReBarClass() {
  return RegisterCtlClasses(REBARCLASSNAME, "ReBarWindow32", W16ReBarProc);
}

#endif  // _AFX_WIN16
