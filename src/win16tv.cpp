// win16tv.cpp - the tree view control window for 16-bit Windows, which
// has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The window does the TVM_ messages in ANSI and sends the TVN_ and NM_
// notifications of comctl32.dll to its parent. It has the lines, the
// buttons, the check boxes (TVS_CHECKBOXES), the normal and state image
// lists, the text, image and children callbacks, label editing, custom
// draw, sorting, the drop highlight, the insert mark, a drag
// notification, the incremental search and the keys and mouse of
// comctl32.dll. It has no tooltips and no hot tracking.
//
// An HTREEITEM is a 16-bit handle. TVI_ROOT, TVI_FIRST, TVI_LAST and
// TVI_SORT become 0, 1, 2 and 3 in 16 bits, so the handles of the items
// start at 4.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

// The first handle of an item. The handles below it are the TVI_ values.
const WORD kFirstHandle = 4;

// The timer of a label edit that a click on the selected item starts.
const UINT kEditTimer = 1;

// The distance that the mouse moves with a button down before a drag.
const int kDragDistance = 4;

/// Gives the 16-bit value of a tree item handle.
inline WORD HW(HTREEITEM h) { return (WORD)(UINT)h; }

/// Gives the tree item handle of a 16-bit value.
inline HTREEITEM HT(WORD h) { return (HTREEITEM)(UINT)h; }

/// Gives the tree item handle in the low word of a message parameter.
inline WORD HL(LPARAM l) { return (WORD)LOWORD(l); }

// One item of a tree.
struct W16TvNode {
  WORD hParent, hFirst, hNext, hPrev;  // 0 when there is none
  CString text;
  BOOL bTextCallback;  // the text is LPSTR_TEXTCALLBACK
  int iImage, iSelImage;
  int cChildren;  // TVIF_CHILDREN: 0, 1 or I_CHILDRENCALLBACK
  int iIntegral;  // the height in item heights (1 or more)
  UINT state;
  LPARAM lParam;
  /// Starts with no links, no text and no images.
  W16TvNode()
      : hParent(0), hFirst(0), hNext(0), hPrev(0), bTextCallback(FALSE), iImage(0), iSelImage(0),
        cChildren(0), iIntegral(1), state(0), lParam(0) {}
};

// The data of a tree view.
struct W16Tree {
  HWND hwnd;
  CPtrArray nodes;     // the items (W16TvNode*) by handle - kFirstHandle, NULL when free
  int nCount;          // the number of items
  WORD hRoot;          // the first item at the root
  WORD hCaret;         // the selected item
  WORD hDrop;          // the drop highlight
  WORD hTop;           // the first visible item
  WORD hInsertMark;    // the item of the insert mark, or 0
  BOOL bInsertAfter;   // the insert mark is after the item
  CWordArray rows;     // the visible items, in order
  CWordArray levels;   // the level of each visible item
  BOOL bRowsDirty;     // the rows must be made again
  int nTopRow;         // the row of hTop
  int xOrigin;         // the horizontal scroll position, in pixels
  int nMaxWidth;       // the width of the widest row
  BOOL bInLayout;      // the scroll bars are changing
  HIMAGELIST himlNormal, himlState;
  BOOL bOwnState;      // the window made himlState for TVS_CHECKBOXES
  HFONT hFont, hBoldFont;
  COLORREF clrBk, clrText, clrLine, clrInsertMark;
  int nIndent;
  int nItemHeight;     // TVM_SETITEMHEIGHT, or 0 for the default
  UINT nScrollTime;
  HWND hwndToolTips;
  HWND hwndEdit;       // the edit control of a label edit, or NULL
  WORD hEdit;          // the item whose label is edited
  WNDPROC pfnEditProc; // the procedure of the edit control before the tree
  BOOL bFocus;
  BOOL bDragPending;   // a button is down on an item
  BOOL bRightDrag;
  POINT ptDown;
  WORD hDown;          // the item under the button
  WORD hEditPending;   // the item whose label edit the timer starts
  char szSearch[32];   // the incremental search string
  int nSearch;
  DWORD dwSearchTime;  // the time of the last character of the search
  HBRUSH hbrDots;      // the pattern brush of the dotted lines
  /// Starts empty, with the colors of the system.
  W16Tree()
      : hwnd(NULL), nCount(0), hRoot(0), hCaret(0), hDrop(0), hTop(0), hInsertMark(0),
        bInsertAfter(FALSE), bRowsDirty(TRUE), nTopRow(0), xOrigin(0), nMaxWidth(0),
        bInLayout(FALSE), himlNormal(NULL), himlState(NULL), bOwnState(FALSE), hFont(NULL),
        hBoldFont(NULL), clrBk(CLR_DEFAULT), clrText(CLR_DEFAULT), clrLine(CLR_DEFAULT),
        clrInsertMark(CLR_DEFAULT), nIndent(19), nItemHeight(0), nScrollTime(300),
        hwndToolTips(NULL), hwndEdit(NULL), hEdit(0), pfnEditProc(NULL), bFocus(FALSE),
        bDragPending(FALSE), bRightDrag(FALSE), hDown(0), hEditPending(0), nSearch(0),
        dwSearchTime(0), hbrDots(NULL) {
    ptDown.x = ptDown.y = 0;
    szSearch[0] = '\0';
  }
  /// Deletes the items and the drawing objects.
  ~W16Tree() {
    for (int i = 0; i < nodes.GetSize(); i++) delete (W16TvNode*)nodes[i];
    if (hBoldFont != NULL) ::DeleteObject(hBoldFont);
    if (hbrDots != NULL) ::DeleteObject(hbrDots);
    if (bOwnState && himlState != NULL) ::ImageList_Destroy(himlState);
  }
};

/// Gives the item of a handle, or NULL when the handle is not an item.
W16TvNode* TvNode(W16Tree* t, WORD h) {
  if (h < kFirstHandle) return NULL;
  int i = (int)(h - kFirstHandle);
  if (i >= t->nodes.GetSize()) return NULL;
  return (W16TvNode*)t->nodes[i];
}

/// Gives the style of a tree.
DWORD TvStyle(W16Tree* t) { return StyleOf(t->hwnd); }

/// Gives the font of the text of a tree.
HFONT TvFont(W16Tree* t) {
  return t->hFont != NULL ? t->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the bold font of a tree (TVIS_BOLD), made from its font.
HFONT TvBoldFont(W16Tree* t) {
  if (t->hBoldFont == NULL) {
    LOGFONT lf;
    if (::GetObject(TvFont(t), sizeof(lf), &lf) != 0) {
      lf.lfWeight = FW_BOLD;
      t->hBoldFont = ::CreateFontIndirect(&lf);
    }
  }
  return t->hBoldFont != NULL ? t->hBoldFont : TvFont(t);
}

/// Gives a color of a tree, or the system color when it is CLR_DEFAULT.
COLORREF TvColor(COLORREF clr, int nSysColor) {
  return clr == CLR_DEFAULT ? ::GetSysColor(nSysColor) : clr;
}

/// Gives the size of the images of an image list, or 0 by 0 without one.
SIZE TvImageSize(HIMAGELIST himl) {
  SIZE size = {0, 0};
  int cx = 0, cy = 0;
  if (himl != NULL && ::ImageList_GetIconSize(himl, &cx, &cy)) {
    size.cx = cx;
    size.cy = cy;
  }
  return size;
}

/// Gives the height of one item: the text or the images, with a margin.
/// Without TVS_NONEVENHEIGHT the height is even.
int TvItemHeight(W16Tree* t) {
  if (t->nItemHeight > 0) return t->nItemHeight;
  int cy = FontHeight(t->hwnd, TvFont(t)) + 3;
  SIZE sizeImage = TvImageSize(t->himlNormal);
  SIZE sizeState = TvImageSize(t->himlState);
  if (sizeImage.cy + 1 > cy) cy = sizeImage.cy + 1;
  if (sizeState.cy + 1 > cy) cy = sizeState.cy + 1;
  if (!(TvStyle(t) & TVS_NONEVENHEIGHT) && (cy & 1)) cy++;
  return cy;
}

/// Gives the height of the row of an item.
int TvRowHeight(W16Tree* t, WORD h) {
  W16TvNode* n = TvNode(t, h);
  int nIntegral = n != NULL && n->iIntegral > 1 ? n->iIntegral : 1;
  return TvItemHeight(t) * nIntegral;
}

/// Gives the first child of an item, or the first item at the root for 0.
WORD TvFirstChild(W16Tree* t, WORD hParent) {
  if (hParent == 0) return t->hRoot;
  W16TvNode* n = TvNode(t, hParent);
  return n != NULL ? n->hFirst : 0;
}

/// Gives the last child of an item, or the last item at the root for 0.
WORD TvLastChild(W16Tree* t, WORD hParent) {
  WORD h = TvFirstChild(t, hParent);
  while (h != 0 && TvNode(t, h)->hNext != 0) h = TvNode(t, h)->hNext;
  return h;
}

/// Gives TRUE when hAncestor is hItem or a parent above it.
BOOL TvIsAncestor(W16Tree* t, WORD hAncestor, WORD hItem) {
  while (hItem != 0) {
    if (hItem == hAncestor) return TRUE;
    W16TvNode* n = TvNode(t, hItem);
    hItem = n != NULL ? n->hParent : 0;
  }
  return FALSE;
}

/// Marks the rows as old and draws the tree again.
void TvChanged(W16Tree* t) {
  t->bRowsDirty = TRUE;
  ::InvalidateRect(t->hwnd, NULL, TRUE);
}

/// Fills a TVITEMA with the handle, the state and the lParam of an item,
/// for an NMTREEVIEWA.
void TvFillNotifyItem(W16Tree* t, WORD h, TVITEMA* p) {
  memset(p, 0, sizeof(*p));
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return;
  p->mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM;
  p->hItem = HT(h);
  p->state = n->state;
  p->stateMask = 0xFFFF;
  p->lParam = n->lParam;
}

/// Sends a notification with an NMTREEVIEWA. Gives the result of the
/// parent.
LRESULT TvNotify(W16Tree* t, UINT nCode, UINT action, WORD hOld, WORD hNew, POINT pt) {
  NMTREEVIEWA nm;
  memset(&nm, 0, sizeof(nm));
  nm.action = action;
  TvFillNotifyItem(t, hOld, &nm.itemOld);
  TvFillNotifyItem(t, hNew, &nm.itemNew);
  nm.ptDrag = pt;
  return SendNotify(t->hwnd, nCode, &nm.hdr);
}

/// Asks the parent for the fields of an item that are callbacks
/// (TVN_GETDISPINFOA). The fields that the parent gives with
/// TVIF_DI_SETITEM stay in the item.
void TvAskParent(W16Tree* t, WORD h, UINT mask, CString* pText, int* piImage, int* piSelImage,
                 int* pcChildren) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL || mask == 0) return;
  char szText[260];
  szText[0] = '\0';
  NMTVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.item.mask = mask;
  nm.item.hItem = HT(h);
  nm.item.state = n->state;
  nm.item.stateMask = 0xFFFF;
  nm.item.lParam = n->lParam;
  nm.item.pszText = szText;
  nm.item.cchTextMax = sizeof(szText);
  nm.item.iImage = n->iImage;
  nm.item.iSelectedImage = n->iSelImage;
  nm.item.cChildren = n->cChildren;
  SendNotify(t->hwnd, TVN_GETDISPINFOA, &nm.hdr);
  n = TvNode(t, h);  // the parent can delete the item
  if (n == NULL) return;
  BOOL bSet = (nm.item.mask & TVIF_DI_SETITEM) != 0;
  if (mask & TVIF_TEXT) {
    CString str(nm.item.pszText != NULL ? (LPCSTR)nm.item.pszText : "");
    if (pText != NULL) *pText = str;
    if (bSet) {
      n->text = str;
      n->bTextCallback = FALSE;
    }
  }
  if (mask & TVIF_IMAGE) {
    if (piImage != NULL) *piImage = nm.item.iImage;
    if (bSet) n->iImage = nm.item.iImage;
  }
  if (mask & TVIF_SELECTEDIMAGE) {
    if (piSelImage != NULL) *piSelImage = nm.item.iSelectedImage;
    if (bSet) n->iSelImage = nm.item.iSelectedImage;
  }
  if (mask & TVIF_CHILDREN) {
    if (pcChildren != NULL) *pcChildren = nm.item.cChildren;
    if (bSet) n->cChildren = nm.item.cChildren;
  }
}

/// Gives the text and the images that an item shows, with the callbacks.
void TvGetDisplay(W16Tree* t, WORD h, CString* pText, int* piImage, int* piSelImage) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return;
  UINT mask = 0;
  if (pText != NULL) {
    if (n->bTextCallback)
      mask |= TVIF_TEXT;
    else
      *pText = n->text;
  }
  if (piImage != NULL) {
    if (n->iImage == I_IMAGECALLBACK)
      mask |= TVIF_IMAGE;
    else
      *piImage = n->iImage;
  }
  if (piSelImage != NULL) {
    if (n->iSelImage == I_IMAGECALLBACK)
      mask |= TVIF_SELECTEDIMAGE;
    else
      *piSelImage = n->iSelImage;
  }
  TvAskParent(t, h, mask, pText, piImage, piSelImage, NULL);
}

/// Gives TRUE when an item has children, or says that it has them
/// (TVIF_CHILDREN, or the children callback).
BOOL TvHasChildren(W16Tree* t, WORD h) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return FALSE;
  if (n->hFirst != 0) return TRUE;
  if (n->cChildren == I_CHILDRENCALLBACK) {
    int c = 0;
    TvAskParent(t, h, TVIF_CHILDREN, NULL, NULL, NULL, &c);
    return c != 0;
  }
  return n->cChildren != 0;
}

/// Adds an item and its expanded children to the rows.
void TvAddRows(W16Tree* t, WORD h, int nLevel) {
  while (h != 0) {
    W16TvNode* n = TvNode(t, h);
    t->rows.Add(h);
    t->levels.Add((WORD)nLevel);
    if ((n->state & TVIS_EXPANDED) && n->hFirst != 0) TvAddRows(t, n->hFirst, nLevel + 1);
    h = n->hNext;
  }
}

/// Gives the row of a visible item, or -1.
int TvRowOf(W16Tree* t, WORD h) {
  for (int i = 0; i < t->rows.GetSize(); i++)
    if (t->rows[i] == h) return i;
  return -1;
}

/// Gives the x of the content of a row (after the indent and the button)
/// in client coordinates.
int TvContentX(W16Tree* t, int nLevel) {
  DWORD dwStyle = TvStyle(t);
  BOOL bRootCell = (dwStyle & TVS_LINESATROOT) && (dwStyle & (TVS_HASLINES | TVS_HASBUTTONS));
  return (nLevel + (bRootCell ? 1 : 0)) * t->nIndent + 3 - t->xOrigin;
}

/// Gives the width of a text in the font of an item.
int TvTextWidth(W16Tree* t, const CString& str, BOOL bBold) {
  HDC hdc = ::GetDC(t->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, bBold ? TvBoldFont(t) : TvFont(t));
  SIZE size;
  ::GetTextExtentPoint32(hdc, str, str.GetLength(), &size);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(t->hwnd, hdc);
  return size.cx;
}

/// Gives the parts of the row iRow: the row, the state image, the image
/// and the label. The y of the row comes from the first visible row.
void TvRowParts(W16Tree* t, int iRow, int yRow, RECT* prcRow, RECT* prcState, RECT* prcIcon,
                RECT* prcLabel) {
  WORD h = t->rows[iRow];
  W16TvNode* n = TvNode(t, h);
  RECT rcClient;
  ::GetClientRect(t->hwnd, &rcClient);
  int cy = TvRowHeight(t, h);
  int x = TvContentX(t, (int)t->levels[iRow]);
  if (prcRow != NULL) ::SetRect(prcRow, 0, yRow, rcClient.right, yRow + cy);
  SIZE sizeState = TvImageSize(t->himlState);
  BOOL bState = t->himlState != NULL && (n->state & TVIS_STATEIMAGEMASK) != 0;
  if (prcState != NULL) ::SetRect(prcState, x, yRow, bState ? x + sizeState.cx : x, yRow + cy);
  if (bState) x += sizeState.cx;
  SIZE sizeImage = TvImageSize(t->himlNormal);
  if (prcIcon != NULL)
    ::SetRect(prcIcon, x, yRow, t->himlNormal != NULL ? x + sizeImage.cx : x, yRow + cy);
  if (t->himlNormal != NULL) x += sizeImage.cx + 2;
  if (prcLabel != NULL) {
    CString strText;
    TvGetDisplay(t, h, &strText, NULL, NULL);
    int cx = TvTextWidth(t, strText, (n->state & TVIS_BOLD) != 0) + 4;
    ::SetRect(prcLabel, x, yRow, x + cx, yRow + cy);
  }
}

/// Gives the y of the top of a row in client coordinates, from the first
/// visible row.
int TvRowTop(W16Tree* t, int iRow) {
  int y = 0;
  if (iRow >= t->nTopRow) {
    for (int i = t->nTopRow; i < iRow; i++) y += TvRowHeight(t, t->rows[i]);
  } else {
    for (int i = iRow; i < t->nTopRow; i++) y -= TvRowHeight(t, t->rows[i]);
  }
  return y;
}

/// Gives the number of rows that fit in the window from a row.
int TvRowsThatFit(W16Tree* t, int iFrom) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  int y = 0, n = 0;
  for (int i = iFrom; i < t->rows.GetSize(); i++) {
    y += TvRowHeight(t, t->rows[i]);
    if (y > rc.bottom) break;
    n++;
  }
  return n;
}

/// Gives the first row that shows the last row at the bottom of the
/// window: the largest top row.
int TvMaxTopRow(W16Tree* t) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  int y = 0;
  int i = (int)t->rows.GetSize();
  while (i > 0) {
    int cy = TvRowHeight(t, t->rows[i - 1]);
    if (y + cy > rc.bottom) break;
    y += cy;
    i--;
  }
  return i;
}

/// Makes the rows again when they are old, keeps the first visible item
/// and sets the scroll bars.
void TvLayout(W16Tree* t) {
  if (t->bInLayout) return;
  t->bInLayout = TRUE;
  if (t->bRowsDirty) {
    t->rows.RemoveAll();
    t->levels.RemoveAll();
    TvAddRows(t, t->hRoot, 0);
    t->bRowsDirty = FALSE;
    // The first visible item stays, or its nearest visible parent.
    WORD h = t->hTop;
    int iRow = -1;
    while (h != 0 && (iRow = TvRowOf(t, h)) < 0) h = TvNode(t, h)->hParent;
    t->nTopRow = iRow < 0 ? 0 : iRow;
    // The widest row.
    t->nMaxWidth = 0;
    int xSave = t->xOrigin;
    t->xOrigin = 0;
    for (int i = 0; i < t->rows.GetSize(); i++) {
      RECT rcLabel;
      TvRowParts(t, i, 0, NULL, NULL, NULL, &rcLabel);
      if (rcLabel.right > t->nMaxWidth) t->nMaxWidth = rcLabel.right;
    }
    t->xOrigin = xSave;
  }
  int nMaxTop = TvMaxTopRow(t);
  if (t->nTopRow > nMaxTop) t->nTopRow = nMaxTop;
  if (t->nTopRow < 0) t->nTopRow = 0;
  t->hTop = t->nTopRow < t->rows.GetSize() ? t->rows[t->nTopRow] : 0;
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  int nMaxX = t->nMaxWidth - rc.right;
  if (nMaxX < 0) nMaxX = 0;
  if (t->xOrigin > nMaxX) t->xOrigin = nMaxX;
  if (!(TvStyle(t) & TVS_NOSCROLL)) {
    ::SetScrollRange(t->hwnd, SB_VERT, 0, nMaxTop, FALSE);
    ::SetScrollPos(t->hwnd, SB_VERT, t->nTopRow, TRUE);
    ::SetScrollRange(t->hwnd, SB_HORZ, 0, nMaxX, FALSE);
    ::SetScrollPos(t->hwnd, SB_HORZ, t->xOrigin, TRUE);
  }
  t->bInLayout = FALSE;
}

/// Scrolls the tree so that the row iRow is the first visible row.
void TvSetTopRow(W16Tree* t, int iRow) {
  TvLayout(t);
  int nMaxTop = TvMaxTopRow(t);
  if (iRow > nMaxTop) iRow = nMaxTop;
  if (iRow < 0) iRow = 0;
  if (iRow == t->nTopRow) return;
  t->nTopRow = iRow;
  t->hTop = iRow < t->rows.GetSize() ? t->rows[iRow] : 0;
  TvLayout(t);
  ::InvalidateRect(t->hwnd, NULL, TRUE);
}

/// Sends TVN_ITEMEXPANDINGA and TVN_ITEMEXPANDEDA around a change of the
/// TVIS_EXPANDED state of an item.
BOOL TvExpand(W16Tree* t, WORD h, UINT nCode, BOOL bUser);

/// Expands the parents of an item, so that the item is in the rows.
void TvExpandParents(W16Tree* t, WORD h) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return;
  WORD hParent = n->hParent;
  while (hParent != 0) {
    W16TvNode* p = TvNode(t, hParent);
    if (!(p->state & TVIS_EXPANDED)) TvExpand(t, hParent, TVE_EXPAND, FALSE);
    hParent = p->hParent;
  }
}

/// Expands the parents of an item and scrolls the item into the window.
BOOL TvEnsureVisible(W16Tree* t, WORD h) {
  if (TvNode(t, h) == NULL) return FALSE;
  TvExpandParents(t, h);
  TvLayout(t);
  int iRow = TvRowOf(t, h);
  if (iRow < 0) return FALSE;
  if (iRow < t->nTopRow) {
    TvSetTopRow(t, iRow);
  } else {
    int nFit = TvRowsThatFit(t, t->nTopRow);
    if (nFit < 1) nFit = 1;
    if (iRow >= t->nTopRow + nFit) {
      // The first top row that shows the item at the bottom.
      RECT rc;
      ::GetClientRect(t->hwnd, &rc);
      int y = TvRowHeight(t, h);
      int iTop = iRow;
      while (iTop > 0 && y + TvRowHeight(t, t->rows[iTop - 1]) <= rc.bottom) {
        iTop--;
        y += TvRowHeight(t, t->rows[iTop]);
      }
      TvSetTopRow(t, iTop);
    }
  }
  return TRUE;
}

/// Changes the selected item (TVGN_CARET): the parent gets
/// TVN_SELCHANGINGA, which can refuse it, then TVN_SELCHANGEDA. The item
/// becomes visible. Gives FALSE when the change is refused.
BOOL TvSelect(W16Tree* t, WORD h, UINT action) {
  if (h != 0 && TvNode(t, h) == NULL) return FALSE;
  if (h == t->hCaret) {
    if (h != 0) TvEnsureVisible(t, h);
    return TRUE;
  }
  POINT pt = {0, 0};
  WORD hOld = t->hCaret;
  if (TvNotify(t, TVN_SELCHANGINGA, action, hOld, h, pt) != 0) return FALSE;
  if (h != 0 && TvNode(t, h) == NULL) return FALSE;
  W16TvNode* pOld = TvNode(t, hOld);
  if (pOld != NULL) pOld->state &= ~TVIS_SELECTED;
  W16TvNode* pNew = TvNode(t, h);
  if (pNew != NULL) pNew->state |= TVIS_SELECTED;
  t->hCaret = h;
  if (h != 0) TvEnsureVisible(t, h);
  ::InvalidateRect(t->hwnd, NULL, TRUE);
  TvNotify(t, TVN_SELCHANGEDA, action, hOld, h, pt);
  return TRUE;
}

/// Sets the drop highlight (TVGN_DROPHILITE) on an item, or removes it.
void TvSetDrop(W16Tree* t, WORD h) {
  W16TvNode* pOld = TvNode(t, t->hDrop);
  if (pOld != NULL) pOld->state &= ~TVIS_DROPHILITED;
  W16TvNode* pNew = TvNode(t, h);
  if (pNew != NULL) pNew->state |= TVIS_DROPHILITED;
  t->hDrop = pNew != NULL ? h : 0;
  ::InvalidateRect(t->hwnd, NULL, TRUE);
}

/// Deletes the children of an item.
void TvDeleteChildren(W16Tree* t, WORD hParent);

/// Removes an item from the list of its siblings.
void TvUnlink(W16Tree* t, WORD h) {
  W16TvNode* n = TvNode(t, h);
  if (n->hPrev != 0)
    TvNode(t, n->hPrev)->hNext = n->hNext;
  else if (n->hParent != 0)
    TvNode(t, n->hParent)->hFirst = n->hNext;
  else
    t->hRoot = n->hNext;
  if (n->hNext != 0) TvNode(t, n->hNext)->hPrev = n->hPrev;
  n->hNext = n->hPrev = 0;
}

/// Puts an item in the children of hParent after hAfter, or first when
/// hAfter is 0.
void TvLinkAfter(W16Tree* t, WORD h, WORD hParent, WORD hAfter) {
  W16TvNode* n = TvNode(t, h);
  n->hParent = hParent;
  if (hAfter == 0) {
    WORD hFirst = TvFirstChild(t, hParent);
    n->hPrev = 0;
    n->hNext = hFirst;
    if (hFirst != 0) TvNode(t, hFirst)->hPrev = h;
    if (hParent != 0)
      TvNode(t, hParent)->hFirst = h;
    else
      t->hRoot = h;
  } else {
    W16TvNode* a = TvNode(t, hAfter);
    n->hPrev = hAfter;
    n->hNext = a->hNext;
    if (a->hNext != 0) TvNode(t, a->hNext)->hPrev = h;
    a->hNext = h;
  }
}

/// Deletes an item and its children. The parent gets TVN_DELETEITEMA for
/// each, the children first. When the selected item goes, the next
/// sibling, the previous sibling or the parent becomes selected, unless
/// bAll is set.
void TvDeleteItem(W16Tree* t, WORD h, BOOL bAll) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return;
  if (t->hwndEdit != NULL && TvIsAncestor(t, h, t->hEdit)) {
    HWND hwndEdit = t->hwndEdit;
    t->hwndEdit = NULL;
    t->hEdit = 0;
    ::DestroyWindow(hwndEdit);
  }
  if (!bAll && t->hCaret != 0 && TvIsAncestor(t, h, t->hCaret)) {
    WORD hNew = n->hNext != 0 ? n->hNext : n->hPrev != 0 ? n->hPrev : n->hParent;
    if (!TvSelect(t, hNew, TVC_UNKNOWN)) {
      // The parent refused: no item stays selected.
      W16TvNode* c = TvNode(t, t->hCaret);
      if (c != NULL) c->state &= ~TVIS_SELECTED;
      t->hCaret = 0;
    }
    n = TvNode(t, h);
    if (n == NULL) return;
  }
  TvDeleteChildren(t, h);
  n = TvNode(t, h);
  if (n == NULL) return;
  POINT pt = {0, 0};
  TvNotify(t, TVN_DELETEITEMA, 0, h, 0, pt);
  n = TvNode(t, h);
  if (n == NULL) return;
  WORD hParent = n->hParent;
  TvUnlink(t, h);
  if (t->hCaret == h) t->hCaret = 0;
  if (t->hDrop == h) t->hDrop = 0;
  if (t->hInsertMark == h) t->hInsertMark = 0;
  if (t->hDown == h) t->hDown = 0;
  if (t->hEditPending == h) t->hEditPending = 0;
  if (t->hTop == h) t->hTop = n->hNext != 0 ? n->hNext : n->hPrev != 0 ? n->hPrev : hParent;
  delete n;
  t->nodes[h - kFirstHandle] = NULL;
  t->nCount--;
  // The parent with no more children is not expanded.
  W16TvNode* p = TvNode(t, hParent);
  if (p != NULL && p->hFirst == 0 && p->cChildren != I_CHILDRENCALLBACK)
    p->state &= ~TVIS_EXPANDED;
  t->bRowsDirty = TRUE;
}

/// Deletes the children of an item, the last first.
void TvDeleteChildren(W16Tree* t, WORD hParent) {
  W16TvNode* n = TvNode(t, hParent);
  while (n != NULL && n->hFirst != 0) {
    TvDeleteItem(t, n->hFirst, FALSE);
    n = TvNode(t, hParent);
  }
}

/// Deletes all the items, without a change of the selection.
void TvDeleteAll(W16Tree* t) {
  while (t->hRoot != 0) TvDeleteItem(t, t->hRoot, TRUE);
  t->hCaret = t->hDrop = t->hTop = t->hInsertMark = 0;
  t->nTopRow = 0;
  t->xOrigin = 0;
  TvChanged(t);
}

BOOL TvExpand(W16Tree* t, WORD h, UINT nCode, BOOL bUser) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return FALSE;
  UINT nAction = nCode & 0x0003;
  BOOL bExpanded = (n->state & TVIS_EXPANDED) != 0;
  if (nAction == TVE_TOGGLE) nAction = bExpanded ? TVE_COLLAPSE : TVE_EXPAND;
  if (nAction == TVE_EXPAND) {
    if (bExpanded && !(nCode & TVE_EXPANDPARTIAL)) return TRUE;
    if (!TvHasChildren(t, h)) return FALSE;
  } else if (nAction == TVE_COLLAPSE) {
    if (!bExpanded && !(nCode & TVE_COLLAPSERESET)) return TRUE;
  } else {
    return FALSE;
  }
  // TVM_EXPAND notifies only until the item has TVIS_EXPANDEDONCE.
  BOOL bNotify = bUser || !(n->state & TVIS_EXPANDEDONCE);
  POINT pt = {0, 0};
  if (bNotify && TvNotify(t, TVN_ITEMEXPANDINGA, nAction, 0, h, pt) != 0) return FALSE;
  n = TvNode(t, h);
  if (n == NULL) return FALSE;
  if (nAction == TVE_EXPAND) {
    n->state |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
    if (nCode & TVE_EXPANDPARTIAL)
      n->state |= TVIS_EXPANDPARTIAL;
    else
      n->state &= ~TVIS_EXPANDPARTIAL;
  } else {
    n->state &= ~(TVIS_EXPANDED | TVIS_EXPANDPARTIAL);
    // The selection leaves the hidden children.
    if (t->hCaret != h && t->hCaret != 0 && TvIsAncestor(t, h, t->hCaret))
      TvSelect(t, h, TVC_UNKNOWN);
    if (nCode & TVE_COLLAPSERESET) {
      TvDeleteChildren(t, h);
      n = TvNode(t, h);
      if (n == NULL) return FALSE;
      n->state &= ~TVIS_EXPANDEDONCE;
    }
  }
  TvChanged(t);
  if (bNotify) TvNotify(t, TVN_ITEMEXPANDEDA, nAction, 0, h, pt);
  return TRUE;
}

/// Expands an item and all its children (the * key).
void TvExpandAll(W16Tree* t, WORD h) {
  if (!TvExpand(t, h, TVE_EXPAND, TRUE)) return;
  W16TvNode* n = TvNode(t, h);
  WORD hChild = n != NULL ? n->hFirst : 0;
  while (hChild != 0) {
    WORD hNext = TvNode(t, hChild)->hNext;
    TvExpandAll(t, hChild);
    if (TvNode(t, hChild) == NULL) break;
    hChild = hNext;
  }
}

/// Copies the fields of a TVITEMA that its mask names into an item. With
/// bEx, the TVITEMEXA field iIntegral is there too.
void TvSetFields(W16Tree* t, W16TvNode* n, const TVITEMA FAR* p, BOOL bEx) {
  if (p->mask & TVIF_TEXT) {
    if (p->pszText == LPSTR_TEXTCALLBACKA) {
      n->bTextCallback = TRUE;
      n->text.Empty();
    } else {
      n->bTextCallback = FALSE;
      n->text = p->pszText != NULL ? (LPCSTR)p->pszText : "";
    }
  }
  if (p->mask & TVIF_IMAGE) n->iImage = p->iImage;
  if (p->mask & TVIF_SELECTEDIMAGE) n->iSelImage = p->iSelectedImage;
  if (p->mask & TVIF_CHILDREN) n->cChildren = p->cChildren;
  if (p->mask & TVIF_PARAM) n->lParam = p->lParam;
  if (p->mask & TVIF_STATE) {
    // The selection and the drop highlight change only through
    // TVM_SELECTITEM.
    UINT mask = p->stateMask & ~(TVIS_SELECTED | TVIS_DROPHILITED);
    n->state = (n->state & ~mask) | (p->state & mask);
  }
  if (bEx && (p->mask & TVIF_INTEGRAL)) {
    int nIntegral = ((const TVITEMEXA FAR*)p)->iIntegral;
    n->iIntegral = nIntegral > 0 ? nIntegral : 1;
  }
  (void)t;
}

/// Makes the image list of the check boxes of TVS_CHECKBOXES: image 1 is
/// a clear box and image 2 a checked box, in the classic look.
void TvMakeCheckImages(W16Tree* t) {
  if (t->himlState != NULL) return;
  const int cx = 16, cy = 16;
  HIMAGELIST himl = ::ImageList_Create(cx, cy, ILC_COLOR | ILC_MASK, 2, 0);
  if (himl == NULL) return;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, cx * 2, cy);
  ::ReleaseDC(NULL, hdcScreen);
  HGDIOBJ hOld = ::SelectObject(hdc, hbm);
  const COLORREF clrMask = RGB(255, 0, 255);
  RECT rcAll = {0, 0, cx * 2, cy};
  FillColor(hdc, rcAll, clrMask);
  COLORREF clrShadow = ::GetSysColor(COLOR_BTNSHADOW);
  COLORREF clrLight = ::GetSysColor(COLOR_BTNHIGHLIGHT);
  COLORREF clrFace = ::GetSysColor(COLOR_BTNFACE);
  COLORREF clrDark = RGB(0, 0, 0);
  for (int k = 0; k < 2; k++) {
    int x = k * cx + 1, y = 1;  // a 13 by 13 box
    // The outer edge: shadow on the top and left, highlight on the
    // bottom and right.
    DrawLine(hdc, x, y, x + 12, y, clrShadow);
    DrawLine(hdc, x, y, x, y + 12, clrShadow);
    DrawLine(hdc, x, y + 12, x + 13, y + 12, clrLight);
    DrawLine(hdc, x + 12, y, x + 12, y + 12, clrLight);
    // The inner edge: dark on the top and left, face on the bottom and
    // right.
    DrawLine(hdc, x + 1, y + 1, x + 11, y + 1, clrDark);
    DrawLine(hdc, x + 1, y + 1, x + 1, y + 11, clrDark);
    DrawLine(hdc, x + 1, y + 11, x + 12, y + 11, clrFace);
    DrawLine(hdc, x + 11, y + 1, x + 11, y + 11, clrFace);
    RECT rcIn = {x + 2, y + 2, x + 11, y + 11};
    FillColor(hdc, rcIn, RGB(255, 255, 255));
    if (k == 1) {
      // The check mark: 7 columns, each 3 pixels high.
      static const int aTop[7] = {2, 3, 4, 3, 2, 1, 0};
      for (int i = 0; i < 7; i++) {
        RECT rcCol = {x + 3 + i, y + 3 + aTop[i], x + 4 + i, y + 6 + aTop[i]};
        FillColor(hdc, rcCol, clrDark);
      }
    }
  }
  ::SelectObject(hdc, hOld);
  ::DeleteDC(hdc);
  ::ImageList_AddMasked(himl, hbm, clrMask);
  ::DeleteObject(hbm);
  t->himlState = himl;
  t->bOwnState = TRUE;
}

/// Adds an item from a TVINSERTSTRUCTA. Gives its handle, or 0.
WORD TvInsert(W16Tree* t, const TVINSERTSTRUCTA FAR* p) {
  WORD hParent = HW(p->hParent);
  if (hParent != 0 && TvNode(t, hParent) == NULL) return 0;
  // A free slot, or a new one.
  int iSlot = -1;
  for (int i = 0; i < t->nodes.GetSize(); i++)
    if (t->nodes[i] == NULL) {
      iSlot = i;
      break;
    }
  if (iSlot < 0) {
    if (t->nodes.GetSize() >= 0xFFF0 - kFirstHandle) return 0;
    iSlot = (int)t->nodes.Add(NULL);
  }
  W16TvNode* n = new W16TvNode;
  t->nodes[iSlot] = n;
  WORD h = (WORD)(iSlot + kFirstHandle);
  TvSetFields(t, n, &p->item, TRUE);
  if (p->item.mask & TVIF_STATE) n->state &= ~(TVIS_SELECTED | TVIS_DROPHILITED);
  if ((TvStyle(t) & TVS_CHECKBOXES) && !(n->state & TVIS_STATEIMAGEMASK)) {
    TvMakeCheckImages(t);
    n->state |= INDEXTOSTATEIMAGEMASK(1);
  }
  // The place among the siblings.
  WORD hInsertAfter = HW(p->hInsertAfter);
  WORD hAfter;
  if (hInsertAfter == HW(TVI_FIRST)) {
    hAfter = 0;
  } else if (hInsertAfter == HW(TVI_SORT)) {
    hAfter = 0;
    CString strText;
    TvGetDisplay(t, h, &strText, NULL, NULL);
    for (WORD s = TvFirstChild(t, hParent); s != 0; s = TvNode(t, s)->hNext) {
      CString strSib;
      TvGetDisplay(t, s, &strSib, NULL, NULL);
      if (strText.CompareNoCase(strSib) < 0) break;
      hAfter = s;
    }
  } else {
    W16TvNode* a = TvNode(t, hInsertAfter);
    hAfter = a != NULL && a->hParent == hParent ? hInsertAfter : TvLastChild(t, hParent);
  }
  TvLinkAfter(t, h, hParent, hAfter);
  t->nCount++;
  if (t->hTop == 0) t->hTop = h;
  TvChanged(t);
  return h;
}

/// Fills a TVITEMA with the fields of an item that its mask names.
BOOL TvGetFields(W16Tree* t, TVITEMA FAR* p) {
  WORD h = HW(p->hItem);
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return FALSE;
  UINT mask = p->mask;
  UINT askMask = 0;
  if ((mask & TVIF_TEXT) && n->bTextCallback) askMask |= TVIF_TEXT;
  if ((mask & TVIF_IMAGE) && n->iImage == I_IMAGECALLBACK) askMask |= TVIF_IMAGE;
  if ((mask & TVIF_SELECTEDIMAGE) && n->iSelImage == I_IMAGECALLBACK)
    askMask |= TVIF_SELECTEDIMAGE;
  if ((mask & TVIF_CHILDREN) && n->hFirst == 0 && n->cChildren == I_CHILDRENCALLBACK)
    askMask |= TVIF_CHILDREN;
  CString strText = n->text;
  int iImage = n->iImage, iSelImage = n->iSelImage, cChildren = n->cChildren;
  TvAskParent(t, h, askMask, &strText, &iImage, &iSelImage, &cChildren);
  n = TvNode(t, h);
  if (n == NULL) return FALSE;
  if ((mask & TVIF_TEXT) && p->pszText != NULL && p->cchTextMax > 0)
    lstrcpyn(p->pszText, strText, p->cchTextMax);
  if (mask & TVIF_IMAGE) p->iImage = iImage;
  if (mask & TVIF_SELECTEDIMAGE) p->iSelectedImage = iSelImage;
  if (mask & TVIF_CHILDREN) p->cChildren = n->hFirst != 0 ? 1 : cChildren;
  if (mask & TVIF_STATE) p->state = n->state;
  if (mask & TVIF_PARAM) p->lParam = n->lParam;
  if (mask & TVIF_INTEGRAL) ((TVITEMEXA FAR*)p)->iIntegral = n->iIntegral;
  return TRUE;
}

// The data of a sort of the children of an item.
struct TvSortData {
  PFNTVCOMPARE pfnCompare;  // NULL: by the text, without case
  LPARAM lParamSort;
};

/// Compares two items for a sort: with the callback of the caller, or by
/// the text without case.
int TvCompare(W16Tree* t, const TvSortData& d, WORD a, WORD b) {
  if (d.pfnCompare != NULL) return d.pfnCompare(TvNode(t, a)->lParam, TvNode(t, b)->lParam, d.lParamSort);
  CString strA, strB;
  TvGetDisplay(t, a, &strA, NULL, NULL);
  TvGetDisplay(t, b, &strB, NULL, NULL);
  return lstrcmpi(strA, strB);
}

/// Sorts the children of an item (an insertion sort, which keeps the
/// order of equal items). With bRecurse the children of the children are
/// sorted too.
BOOL TvSortChildren(W16Tree* t, WORD hParent, const TvSortData& d, BOOL bRecurse) {
  if (hParent != 0 && TvNode(t, hParent) == NULL) return FALSE;
  CWordArray kids;
  for (WORD h = TvFirstChild(t, hParent); h != 0; h = TvNode(t, h)->hNext) kids.Add(h);
  for (int i = 1; i < kids.GetSize(); i++) {
    WORD hKey = kids[i];
    int j = i - 1;
    while (j >= 0 && TvCompare(t, d, kids[j], hKey) > 0) {
      kids[j + 1] = kids[j];
      j--;
    }
    kids[j + 1] = hKey;
  }
  // Link the children again in the new order.
  for (int i = 0; i < kids.GetSize(); i++) {
    W16TvNode* n = TvNode(t, kids[i]);
    n->hPrev = i > 0 ? kids[i - 1] : 0;
    n->hNext = i + 1 < kids.GetSize() ? kids[i + 1] : 0;
  }
  if (kids.GetSize() > 0) {
    if (hParent != 0)
      TvNode(t, hParent)->hFirst = kids[0];
    else
      t->hRoot = kids[0];
  }
  if (bRecurse)
    for (int i = 0; i < kids.GetSize(); i++) TvSortChildren(t, kids[i], d, TRUE);
  TvChanged(t);
  return TRUE;
}

/// Gives the item at a point and the part of it (the TVHT_ flags).
WORD TvHitTest(W16Tree* t, POINT pt, UINT* pFlags) {
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  UINT flags = 0;
  if (pt.y < 0) flags |= TVHT_ABOVE;
  if (pt.y >= rc.bottom) flags |= TVHT_BELOW;
  if (pt.x < 0) flags |= TVHT_TOLEFT;
  if (pt.x >= rc.right) flags |= TVHT_TORIGHT;
  if (flags != 0) {
    *pFlags = flags;
    return 0;
  }
  TvLayout(t);
  int y = 0;
  for (int i = t->nTopRow; i < t->rows.GetSize() && y < rc.bottom; i++) {
    WORD h = t->rows[i];
    int cy = TvRowHeight(t, h);
    if (pt.y >= y && pt.y < y + cy) {
      RECT rcState, rcIcon, rcLabel;
      TvRowParts(t, i, y, NULL, &rcState, &rcIcon, &rcLabel);
      int xButton = rcState.left - 3 - t->nIndent;  // the cell of the button
      if (pt.x < rcState.left) {
        BOOL bButton = (TvStyle(t) & TVS_HASBUTTONS) && rcState.left - 3 + t->xOrigin > 0 &&
                       pt.x >= xButton && TvHasChildren(t, h);
        flags = bButton ? TVHT_ONITEMBUTTON : TVHT_ONITEMINDENT;
      } else if (pt.x < rcState.right) {
        flags = TVHT_ONITEMSTATEICON;
      } else if (pt.x < rcIcon.right) {
        flags = TVHT_ONITEMICON;
      } else if (pt.x < rcLabel.right) {
        flags = TVHT_ONITEMLABEL;
      } else {
        flags = TVHT_ONITEMRIGHT;
      }
      *pFlags = flags;
      return h;
    }
    y += cy;
  }
  *pFlags = TVHT_NOWHERE;
  return 0;
}

/// Sends NM_CUSTOMDRAW for a draw stage of an item. Gives the CDRF_
/// result of the parent. pclrText and pclrTextBk can change.
LRESULT TvCustomDraw(W16Tree* t, HDC hdc, DWORD dwStage, WORD h, int nLevel, const RECT& rc,
                     COLORREF* pclrText, COLORREF* pclrTextBk) {
  NMTVCUSTOMDRAW nm;
  memset(&nm, 0, sizeof(nm));
  nm.nmcd.dwDrawStage = dwStage;
  nm.nmcd.hdc = hdc;
  nm.nmcd.rc = rc;
  nm.nmcd.dwItemSpec = (DWORD_PTR)h;
  W16TvNode* n = TvNode(t, h);
  if (n != NULL) {
    if (n->state & TVIS_SELECTED) nm.nmcd.uItemState |= CDIS_SELECTED;
    if (h == t->hCaret && t->bFocus) nm.nmcd.uItemState |= CDIS_FOCUS;
    nm.nmcd.lItemlParam = n->lParam;
  }
  nm.clrText = pclrText != NULL ? *pclrText : CLR_DEFAULT;
  nm.clrTextBk = pclrTextBk != NULL ? *pclrTextBk : CLR_DEFAULT;
  nm.iLevel = nLevel;
  LRESULT lResult = SendNotify(t->hwnd, NM_CUSTOMDRAW, &nm.nmcd.hdr);
  if (pclrText != NULL) *pclrText = nm.clrText;
  if (pclrTextBk != NULL) *pclrTextBk = nm.clrTextBk;
  return lResult;
}

/// Gives the pattern brush of the dotted lines: every other pixel.
HBRUSH TvDotBrush(W16Tree* t) {
  if (t->hbrDots == NULL) {
    static const WORD aBits[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
    HBITMAP hbm = ::CreateBitmap(8, 8, 1, 1, aBits);
    if (hbm != NULL) {
      t->hbrDots = ::CreatePatternBrush(hbm);
      ::DeleteObject(hbm);
    }
  }
  return t->hbrDots;
}

/// Draws a dotted rectangle (one pixel wide or high) in the line color.
void TvDots(W16Tree* t, HDC hdc, int x, int y, int cx, int cy, COLORREF clrLine,
            COLORREF clrBk) {
  if (cx <= 0 || cy <= 0) return;
  HBRUSH hbr = TvDotBrush(t);
  if (hbr == NULL) return;
  COLORREF clrOldText = ::SetTextColor(hdc, clrLine);
  COLORREF clrOldBk = ::SetBkColor(hdc, clrBk);
  HGDIOBJ hOld = ::SelectObject(hdc, hbr);
  ::PatBlt(hdc, x, y, cx, cy, PATCOPY);
  ::SelectObject(hdc, hOld);
  ::SetTextColor(hdc, clrOldText);
  ::SetBkColor(hdc, clrOldBk);
}

/// Draws the lines of a row: a vertical line for each parent with a next
/// sibling, and the lines to the item.
void TvPaintLines(W16Tree* t, HDC hdc, int iRow, const RECT& rcRow, COLORREF clrLine,
                  COLORREF clrBk) {
  WORD h = t->rows[iRow];
  W16TvNode* n = TvNode(t, h);
  int nLevel = (int)t->levels[iRow];
  int xContent = TvContentX(t, nLevel) - 3;
  BOOL bRootCell = xContent + t->xOrigin > nLevel * t->nIndent;
  int yMid = rcRow.top + TvItemHeight(t) / 2;
  int nHalf = t->nIndent / 2;
  // The item itself: a line from the middle of its cell to its content.
  if (nLevel > 0 || bRootCell) {
    int xCell = xContent - t->nIndent + nHalf;
    TvDots(t, hdc, xCell, yMid, xContent - xCell, 1, clrLine, clrBk);
    // Up to the previous sibling or the parent.
    BOOL bUp = n->hPrev != 0 || n->hParent != 0;
    if (bUp) TvDots(t, hdc, xCell, rcRow.top, 1, yMid - rcRow.top, clrLine, clrBk);
    if (n->hNext != 0) TvDots(t, hdc, xCell, yMid, 1, rcRow.bottom - yMid, clrLine, clrBk);
  }
  // The parents: a line through the row when the parent has a next
  // sibling.
  WORD hAnc = n->hParent;
  int nAncLevel = nLevel - 1;
  while (hAnc != 0) {
    W16TvNode* a = TvNode(t, hAnc);
    if (a->hNext != 0 && (nAncLevel > 0 || bRootCell)) {
      int xCell = TvContentX(t, nAncLevel) - 3 - t->nIndent + nHalf;
      TvDots(t, hdc, xCell, rcRow.top, 1, rcRow.bottom - rcRow.top, clrLine, clrBk);
    }
    hAnc = a->hParent;
    nAncLevel--;
  }
}

/// Draws the button of an item: a box with a minus when the item is
/// expanded, or a plus.
void TvPaintButton(W16Tree* t, HDC hdc, int iRow, const RECT& rcRow, COLORREF clrBk,
                   COLORREF clrText) {
  WORD h = t->rows[iRow];
  int nLevel = (int)t->levels[iRow];
  int xContent = TvContentX(t, nLevel) - 3;
  if (xContent + t->xOrigin <= 0) return;  // no cell for the button
  if (!TvHasChildren(t, h)) return;
  int xMid = xContent - t->nIndent + t->nIndent / 2;
  int yMid = rcRow.top + TvItemHeight(t) / 2;
  RECT rcBox = {xMid - 4, yMid - 4, xMid + 5, yMid + 5};
  FillColor(hdc, rcBox, clrBk);
  COLORREF clrFrame = ::GetSysColor(COLOR_BTNSHADOW);
  DrawLine(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.top, clrFrame);
  DrawLine(hdc, rcBox.left, rcBox.bottom - 1, rcBox.right, rcBox.bottom - 1, clrFrame);
  DrawLine(hdc, rcBox.left, rcBox.top, rcBox.left, rcBox.bottom, clrFrame);
  DrawLine(hdc, rcBox.right - 1, rcBox.top, rcBox.right - 1, rcBox.bottom, clrFrame);
  DrawLine(hdc, xMid - 2, yMid, xMid + 3, yMid, clrText);
  if (!(TvNode(t, h)->state & TVIS_EXPANDED)) DrawLine(hdc, xMid, yMid - 2, xMid, yMid + 3, clrText);
}

/// Draws one row: the lines, the button, the state image, the image, the
/// label with the selection colors, the focus rectangle and the insert
/// mark.
void TvPaintRow(W16Tree* t, HDC hdc, int iRow, int y, BOOL bCustomItems) {
  WORD h = t->rows[iRow];
  W16TvNode* n = TvNode(t, h);
  DWORD dwStyle = TvStyle(t);
  RECT rcRow, rcState, rcIcon, rcLabel;
  TvRowParts(t, iRow, y, &rcRow, &rcState, &rcIcon, &rcLabel);
  COLORREF clrBk = TvColor(t->clrBk, COLOR_WINDOW);
  COLORREF clrText = TvColor(t->clrText, COLOR_WINDOWTEXT);
  COLORREF clrLine = TvColor(t->clrLine, COLOR_BTNSHADOW);
  BOOL bEnabled = ::IsWindowEnabled(t->hwnd);
  // The drop highlight hides the selection while it is there.
  BOOL bHighlight = (n->state & TVIS_DROPHILITED) ||
                    ((n->state & TVIS_SELECTED) && t->bFocus && t->hDrop == 0);
  BOOL bGraySel = !bHighlight && (n->state & TVIS_SELECTED) && (dwStyle & TVS_SHOWSELALWAYS);
  COLORREF clrItemText = !bEnabled ? ::GetSysColor(COLOR_GRAYTEXT)
                         : bHighlight ? ::GetSysColor(COLOR_HIGHLIGHTTEXT)
                         : (n->state & TVIS_CUT) ? ::GetSysColor(COLOR_GRAYTEXT)
                                                 : clrText;
  COLORREF clrItemBk = bHighlight ? ::GetSysColor(COLOR_HIGHLIGHT)
                       : bGraySel ? ::GetSysColor(COLOR_BTNFACE)
                                  : clrBk;
  LRESULT lCustom = 0;
  if (bCustomItems) {
    lCustom = TvCustomDraw(t, hdc, CDDS_ITEMPREPAINT, h, (int)t->levels[iRow], rcRow,
                           &clrItemText, &clrItemBk);
    if (lCustom & CDRF_SKIPDEFAULT) return;
    n = TvNode(t, h);
    if (n == NULL) return;
    if (clrItemText == CLR_DEFAULT) clrItemText = clrText;
    if (clrItemBk == CLR_DEFAULT) clrItemBk = clrBk;
  }
  BOOL bFullRow = (dwStyle & TVS_FULLROWSELECT) && !(dwStyle & TVS_HASLINES);
  if (bFullRow && clrItemBk != clrBk) FillColor(hdc, rcRow, clrItemBk);
  if (dwStyle & TVS_HASLINES) TvPaintLines(t, hdc, iRow, rcRow, clrLine, clrBk);
  if (dwStyle & TVS_HASBUTTONS) TvPaintButton(t, hdc, iRow, rcRow, clrBk, clrText);
  int cyItem = TvItemHeight(t);
  if (t->himlState != NULL && (n->state & TVIS_STATEIMAGEMASK)) {
    SIZE size = TvImageSize(t->himlState);
    ::ImageList_Draw(t->himlState, (int)((n->state & TVIS_STATEIMAGEMASK) >> 12) - 1, hdc,
                     rcState.left, rcRow.top + (cyItem - size.cy) / 2, ILD_TRANSPARENT);
  }
  if (t->himlNormal != NULL) {
    int iImage = 0, iSelImage = 0;
    TvGetDisplay(t, h, NULL, &iImage, &iSelImage);
    n = TvNode(t, h);
    if (n == NULL) return;
    int i = (n->state & TVIS_SELECTED) ? iSelImage : iImage;
    SIZE size = TvImageSize(t->himlNormal);
    UINT fStyle = ILD_TRANSPARENT | (n->state & TVIS_OVERLAYMASK);
    if (i >= 0)
      ::ImageList_Draw(t->himlNormal, i, hdc, rcIcon.left, rcRow.top + (cyItem - size.cy) / 2,
                       fStyle);
  }
  CString strText;
  TvGetDisplay(t, h, &strText, NULL, NULL);
  n = TvNode(t, h);
  if (n == NULL) return;
  HGDIOBJ hOldFont = ::SelectObject(hdc, (n->state & TVIS_BOLD) ? TvBoldFont(t) : TvFont(t));
  COLORREF clrOldText = ::SetTextColor(hdc, clrItemText);
  COLORREF clrOldBk = ::SetBkColor(hdc, clrItemBk);
  RECT rcText = rcLabel;
  rcText.bottom = rcText.top + cyItem;
  if (bFullRow) {
    rcText.right = rcRow.right;
  }
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  int yText = rcText.top + (cyItem - tm.tmHeight) / 2;
  ::ExtTextOut(hdc, rcText.left + 2, yText, ETO_OPAQUE | ETO_CLIPPED, &rcText, strText,
               strText.GetLength(), NULL);
  ::SetTextColor(hdc, clrOldText);
  ::SetBkColor(hdc, clrOldBk);
  ::SelectObject(hdc, hOldFont);
  if (h == t->hCaret && t->bFocus && t->hDrop == 0) ::DrawFocusRect(hdc, &rcText);
  if (h == t->hInsertMark) {
    COLORREF clrMark = TvColor(t->clrInsertMark, COLOR_WINDOWTEXT);
    int yMark = t->bInsertAfter ? rcRow.bottom - 1 : rcRow.top;
    RECT rcMark = {rcLabel.left, yMark - (t->bInsertAfter ? 1 : 0), rcRow.right,
                   yMark + (t->bInsertAfter ? 1 : 2)};
    FillColor(hdc, rcMark, clrMark);
  }
  if (lCustom & CDRF_NOTIFYPOSTPAINT)
    TvCustomDraw(t, hdc, CDDS_ITEMPOSTPAINT, h, (int)t->levels[iRow], rcRow, NULL, NULL);
}

/// Draws a tree: the background and the rows in the update rectangle.
void TvPaint(W16Tree* t, HDC hdc, const RECT& rcPaint) {
  if ((TvStyle(t) & TVS_CHECKBOXES) && t->himlState == NULL) TvMakeCheckImages(t);
  TvLayout(t);
  RECT rcClient;
  ::GetClientRect(t->hwnd, &rcClient);
  LRESULT lPre = TvCustomDraw(t, hdc, CDDS_PREPAINT, 0, 0, rcClient, NULL, NULL);
  if (lPre & CDRF_SKIPDEFAULT) return;
  FillColor(hdc, rcPaint, TvColor(t->clrBk, COLOR_WINDOW));
  int y = 0;
  for (int i = t->nTopRow; i < t->rows.GetSize() && y < rcClient.bottom; i++) {
    int cy = TvRowHeight(t, t->rows[i]);
    if (y + cy > rcPaint.top && y < rcPaint.bottom)
      TvPaintRow(t, hdc, i, y, (lPre & CDRF_NOTIFYITEMDRAW) != 0);
    y += cy;
    if (t->bRowsDirty) break;  // the parent changed the tree while it drew
  }
  if (lPre & CDRF_NOTIFYPOSTPAINT) TvCustomDraw(t, hdc, CDDS_POSTPAINT, 0, 0, rcClient, NULL, NULL);
}

LRESULT CALLBACK TvEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

/// Ends a label edit. With bSave, the parent gets the new text in
/// TVN_ENDLABELEDITA and the item takes it when the parent gives TRUE.
void TvEndEdit(W16Tree* t, BOOL bSave) {
  if (t->hwndEdit == NULL) return;
  HWND hwndEdit = t->hwndEdit;
  WORD h = t->hEdit;
  t->hwndEdit = NULL;
  t->hEdit = 0;
  char szText[260];
  ::GetWindowText(hwndEdit, szText, sizeof(szText));
  NMTVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  W16TvNode* n = TvNode(t, h);
  nm.item.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM | TVIF_STATE;
  nm.item.hItem = HT(h);
  nm.item.pszText = bSave ? szText : NULL;
  nm.item.cchTextMax = sizeof(szText);
  if (n != NULL) {
    nm.item.lParam = n->lParam;
    nm.item.state = n->state;
  }
  LRESULT lResult = SendNotify(t->hwnd, TVN_ENDLABELEDITA, &nm.hdr);
  ::DestroyWindow(hwndEdit);
  n = TvNode(t, h);
  if (bSave && lResult != 0 && n != NULL) {
    n->text = szText;
    n->bTextCallback = FALSE;
  }
  TvChanged(t);
}

/// Starts a label edit of an item: the parent gets TVN_BEGINLABELEDITA,
/// which can refuse it, then an edit control covers the label. Gives the
/// edit control, or NULL.
HWND TvBeginEdit(W16Tree* t, WORD h) {
  if (TvNode(t, h) == NULL) return NULL;
  TvEndEdit(t, TRUE);
  if (!TvSelect(t, h, TVC_UNKNOWN)) return NULL;
  TvEnsureVisible(t, h);
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return NULL;
  CString strText;
  TvGetDisplay(t, h, &strText, NULL, NULL);
  char szText[260];
  lstrcpyn(szText, strText, sizeof(szText));
  NMTVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.item.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM | TVIF_STATE;
  nm.item.hItem = HT(h);
  nm.item.pszText = szText;
  nm.item.cchTextMax = sizeof(szText);
  nm.item.lParam = n->lParam;
  nm.item.state = n->state;
  if (SendNotify(t->hwnd, TVN_BEGINLABELEDITA, &nm.hdr) != 0) return NULL;
  TvLayout(t);
  int iRow = TvRowOf(t, h);
  if (iRow < 0) return NULL;
  RECT rcLabel;
  TvRowParts(t, iRow, TvRowTop(t, iRow), NULL, NULL, NULL, &rcLabel);
  int cx = rcLabel.right - rcLabel.left + 16;
  if (cx < 60) cx = 60;
  int cy = FontHeight(t->hwnd, TvFont(t)) + 4;
  HWND hwndEdit = ::CreateWindow("EDIT", strText, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 rcLabel.left, rcLabel.top, cx, cy, t->hwnd, (HMENU)1,
                                 (HINSTANCE)::GetWindowWord(t->hwnd, GWW_HINSTANCE), NULL);
  if (hwndEdit == NULL) return NULL;
  ::SendMessage(hwndEdit, WM_SETFONT, (WPARAM)TvFont(t), 0);
  ::SendMessage(hwndEdit, EM_SETSEL, 0, MAKELPARAM(0, -1));
  t->hwndEdit = hwndEdit;
  t->hEdit = h;
  t->pfnEditProc = (WNDPROC)::SetWindowLong(hwndEdit, GWL_WNDPROC, (LONG)(WNDPROC)TvEditProc);
  ::SetFocus(hwndEdit);
  return hwndEdit;
}

/// The procedure of the edit control of a label edit: Enter saves the
/// text, Esc cancels, and the loss of the focus saves.
LRESULT CALLBACK TvEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  HWND hwndTree = ::GetParent(hWnd);
  W16Tree* t = StateOf<W16Tree>(hwndTree);
  WNDPROC pfnOld = t != NULL ? t->pfnEditProc : NULL;
  if (pfnOld == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
    case WM_KEYDOWN:
      if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
        TvEndEdit(t, wParam == VK_RETURN);
        ::SetFocus(hwndTree);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wParam == '\r' || wParam == 27) return 0;
      break;
    case WM_KILLFOCUS:
      if (t->hwndEdit == hWnd) {
        LRESULT lResult = ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
        TvEndEdit(t, TRUE);
        return lResult;
      }
      break;
  }
  return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
}

/// Makes the drag image of an item (TVM_CREATEDRAGIMAGE): its image and
/// its label in an image list of one image.
HIMAGELIST TvCreateDragImage(W16Tree* t, WORD h) {
  if (TvNode(t, h) == NULL) return NULL;
  TvLayout(t);
  int iRow = TvRowOf(t, h);
  if (iRow < 0) return NULL;
  RECT rcIcon, rcLabel;
  TvRowParts(t, iRow, 0, NULL, NULL, &rcIcon, &rcLabel);
  int cx = rcLabel.right - rcIcon.left;
  int cy = TvItemHeight(t);
  if (cx <= 0) return NULL;
  HIMAGELIST himl = ::ImageList_Create(cx, cy, ILC_COLOR | ILC_MASK, 1, 0);
  if (himl == NULL) return NULL;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, cx, cy);
  ::ReleaseDC(NULL, hdcScreen);
  HGDIOBJ hOld = ::SelectObject(hdc, hbm);
  COLORREF clrBk = TvColor(t->clrBk, COLOR_WINDOW);
  RECT rc = {0, 0, cx, cy};
  FillColor(hdc, rc, clrBk);
  int iImage = 0;
  TvGetDisplay(t, h, NULL, &iImage, NULL);
  if (t->himlNormal != NULL && iImage >= 0) {
    SIZE size = TvImageSize(t->himlNormal);
    ::ImageList_Draw(t->himlNormal, iImage, hdc, 0, (cy - size.cy) / 2, ILD_TRANSPARENT);
  }
  CString strText;
  TvGetDisplay(t, h, &strText, NULL, NULL);
  HGDIOBJ hOldFont = ::SelectObject(hdc, TvFont(t));
  ::SetTextColor(hdc, TvColor(t->clrText, COLOR_WINDOWTEXT));
  ::SetBkMode(hdc, TRANSPARENT);
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  ::TextOut(hdc, rcLabel.left - rcIcon.left + 2, (cy - tm.tmHeight) / 2, strText,
            strText.GetLength());
  ::SelectObject(hdc, hOldFont);
  ::SelectObject(hdc, hOld);
  ::DeleteDC(hdc);
  ::ImageList_AddMasked(himl, hbm, clrBk);
  ::DeleteObject(hbm);
  return himl;
}

/// Gives the next item for TVM_GETNEXTITEM.
WORD TvGetNext(W16Tree* t, WORD h, UINT nCode) {
  TvLayout(t);
  W16TvNode* n = TvNode(t, h);
  switch (nCode) {
    case TVGN_ROOT:
      return t->hRoot;
    case TVGN_NEXT:
      return n != NULL ? n->hNext : 0;
    case TVGN_PREVIOUS:
      return n != NULL ? n->hPrev : 0;
    case TVGN_PARENT:
      return n != NULL ? n->hParent : 0;
    case TVGN_CHILD:
      return h == 0 ? t->hRoot : n != NULL ? n->hFirst : 0;
    case TVGN_FIRSTVISIBLE:
      return t->nTopRow < t->rows.GetSize() ? t->rows[t->nTopRow] : 0;
    case TVGN_NEXTVISIBLE: {
      int iRow = TvRowOf(t, h);
      return iRow >= 0 && iRow + 1 < t->rows.GetSize() ? t->rows[iRow + 1] : 0;
    }
    case TVGN_PREVIOUSVISIBLE: {
      int iRow = TvRowOf(t, h);
      return iRow > 0 ? t->rows[iRow - 1] : 0;
    }
    case TVGN_DROPHILITE:
      return t->hDrop;
    case TVGN_CARET:
      return t->hCaret;
    case TVGN_LASTVISIBLE:
      return t->rows.GetSize() > 0 ? t->rows[t->rows.GetSize() - 1] : 0;
  }
  return 0;
}

/// Gives the rectangle of an item for TVM_GETITEMRECT: the label, or the
/// whole row. Gives FALSE when the item is not in the rows.
BOOL TvGetItemRect(W16Tree* t, WORD h, BOOL bTextOnly, RECT FAR* prc) {
  if (TvNode(t, h) == NULL) return FALSE;
  TvLayout(t);
  int iRow = TvRowOf(t, h);
  if (iRow < 0) return FALSE;
  RECT rcRow, rcLabel;
  TvRowParts(t, iRow, TvRowTop(t, iRow), &rcRow, NULL, NULL, &rcLabel);
  *prc = bTextOnly ? rcLabel : rcRow;
  return TRUE;
}

/// Toggles the check box of an item (TVS_CHECKBOXES): state image 1 is
/// clear and 2 is checked.
void TvToggleCheck(W16Tree* t, WORD h) {
  W16TvNode* n = TvNode(t, h);
  if (n == NULL) return;
  UINT nImage = (n->state & TVIS_STATEIMAGEMASK) >> 12;
  n->state = (n->state & ~TVIS_STATEIMAGEMASK) | INDEXTOSTATEIMAGEMASK(nImage == 2 ? 1 : 2);
  ::InvalidateRect(t->hwnd, NULL, TRUE);
}

/// Selects the next visible item whose text starts with the search
/// string (the incremental search), from the selected item.
void TvSearch(W16Tree* t, char ch) {
  DWORD dwNow = ::GetTickCount();
  if (dwNow - t->dwSearchTime > 1000 || t->nSearch >= (int)sizeof(t->szSearch) - 1)
    t->nSearch = 0;
  t->dwSearchTime = dwNow;
  t->szSearch[t->nSearch++] = ch;
  t->szSearch[t->nSearch] = '\0';
  TvLayout(t);
  int nRows = (int)t->rows.GetSize();
  if (nRows == 0) return;
  int iStart = TvRowOf(t, t->hCaret);
  // A new search starts after the selected item; a longer one at it.
  int iFirst = t->nSearch == 1 ? iStart + 1 : (iStart < 0 ? 0 : iStart);
  CString strSearch(t->szSearch);
  for (int k = 0; k < nRows; k++) {
    int i = ((iFirst < 0 ? 0 : iFirst) + k) % nRows;
    CString strText;
    TvGetDisplay(t, t->rows[i], &strText, NULL, NULL);
    if (strText.GetLength() >= strSearch.GetLength() &&
        strText.Left(strSearch.GetLength()).CompareNoCase(strSearch) == 0) {
      TvSelect(t, t->rows[i], TVC_BYKEYBOARD);
      return;
    }
  }
}

/// Does a key: the parent gets TVN_KEYDOWN, then the key moves the
/// selection, expands or collapses an item, edits a label or toggles a
/// check box.
void TvKeyDown(W16Tree* t, UINT nKey) {
  NMTVKEYDOWN nm;
  memset(&nm, 0, sizeof(nm));
  nm.wVKey = (WORD)nKey;
  SendNotify(t->hwnd, TVN_KEYDOWN, &nm.hdr);
  TvLayout(t);
  int nRows = (int)t->rows.GetSize();
  if (nRows == 0) return;
  int iRow = TvRowOf(t, t->hCaret);
  WORD h = t->hCaret;
  W16TvNode* n = TvNode(t, h);
  BOOL bCtrl = ::GetKeyState(VK_CONTROL) < 0;
  int nPage = TvRowsThatFit(t, t->nTopRow);
  if (nPage < 1) nPage = 1;
  int iNew = -1;
  switch (nKey) {
    case VK_UP:
      if (bCtrl) {
        TvSetTopRow(t, t->nTopRow - 1);
        return;
      }
      iNew = iRow < 0 ? 0 : (iRow > 0 ? iRow - 1 : 0);
      break;
    case VK_DOWN:
      if (bCtrl) {
        TvSetTopRow(t, t->nTopRow + 1);
        return;
      }
      iNew = iRow < 0 ? 0 : (iRow + 1 < nRows ? iRow + 1 : iRow);
      break;
    case VK_HOME:
      iNew = 0;
      break;
    case VK_END:
      iNew = nRows - 1;
      break;
    case VK_PRIOR:
      iNew = iRow < 0 ? 0 : (int)MaxL(0, iRow - (nPage - 1));
      break;
    case VK_NEXT:
      iNew = iRow < 0 ? 0 : (int)MinL(nRows - 1, iRow + (nPage - 1));
      break;
    case VK_LEFT:
      if (n == NULL) return;
      if (n->state & TVIS_EXPANDED)
        TvExpand(t, h, TVE_COLLAPSE, TRUE);
      else if (n->hParent != 0)
        TvSelect(t, n->hParent, TVC_BYKEYBOARD);
      return;
    case VK_RIGHT:
      if (n == NULL) return;
      if (!(n->state & TVIS_EXPANDED)) {
        TvExpand(t, h, TVE_EXPAND, TRUE);
      } else {
        n = TvNode(t, h);
        if (n != NULL && n->hFirst != 0) TvSelect(t, n->hFirst, TVC_BYKEYBOARD);
      }
      return;
    case VK_ADD:
      if (n != NULL) TvExpand(t, h, TVE_EXPAND, TRUE);
      return;
    case VK_SUBTRACT:
      if (n != NULL) TvExpand(t, h, TVE_COLLAPSE, TRUE);
      return;
    case VK_MULTIPLY:
      if (n != NULL) TvExpandAll(t, h);
      return;
    case VK_SPACE:
      if (n != NULL && (TvStyle(t) & TVS_CHECKBOXES)) TvToggleCheck(t, h);
      return;
    case VK_F2:
      if (n != NULL && (TvStyle(t) & TVS_EDITLABELS)) TvBeginEdit(t, h);
      return;
    case VK_RETURN: {
      NMHDR nmhdr;
      SendNotify(t->hwnd, NM_RETURN, &nmhdr);
      return;
    }
    default:
      return;
  }
  if (iNew >= 0 && iNew < nRows) TvSelect(t, t->rows[iNew], TVC_BYKEYBOARD);
}

/// Scrolls the tree for WM_VSCROLL (by rows) or WM_HSCROLL (by pixels).
void TvScroll(W16Tree* t, BOOL bVert, UINT nCode, int nThumb) {
  TvLayout(t);
  RECT rc;
  ::GetClientRect(t->hwnd, &rc);
  if (bVert) {
    int nPage = TvRowsThatFit(t, t->nTopRow);
    if (nPage < 1) nPage = 1;
    int nNew = t->nTopRow;
    switch (nCode) {
      case SB_LINEUP:
        nNew--;
        break;
      case SB_LINEDOWN:
        nNew++;
        break;
      case SB_PAGEUP:
        nNew -= nPage;
        break;
      case SB_PAGEDOWN:
        nNew += nPage;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION:
        nNew = nThumb;
        break;
      case SB_TOP:
        nNew = 0;
        break;
      case SB_BOTTOM:
        nNew = (int)t->rows.GetSize();
        break;
      default:
        return;
    }
    TvSetTopRow(t, nNew);
  } else {
    int nNew = t->xOrigin;
    int nLine = t->nIndent;
    switch (nCode) {
      case SB_LINEUP:
        nNew -= nLine;
        break;
      case SB_LINEDOWN:
        nNew += nLine;
        break;
      case SB_PAGEUP:
        nNew -= rc.right;
        break;
      case SB_PAGEDOWN:
        nNew += rc.right;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION:
        nNew = nThumb;
        break;
      case SB_TOP:
        nNew = 0;
        break;
      case SB_BOTTOM:
        nNew = t->nMaxWidth;
        break;
      default:
        return;
    }
    int nMax = (int)MaxL(0, t->nMaxWidth - rc.right);
    nNew = (int)ClampL(nNew, 0, nMax);
    if (nNew != t->xOrigin) {
      t->xOrigin = nNew;
      TvLayout(t);
      ::InvalidateRect(t->hwnd, NULL, TRUE);
    }
  }
}

/// Does a press of the left or the right button: the focus, the button
/// of an item, the check box, the selection and the start of a label
/// edit or a drag.
void TvButtonDown(W16Tree* t, POINT pt, BOOL bRight) {
  BOOL bHadFocus = t->bFocus;
  if (::GetFocus() != t->hwnd) ::SetFocus(t->hwnd);
  TvEndEdit(t, TRUE);
  UINT flags = 0;
  WORD h = TvHitTest(t, pt, &flags);
  t->hDown = 0;
  t->bDragPending = FALSE;
  if (h == 0) return;
  if (!bRight && (flags & TVHT_ONITEMBUTTON)) {
    TvExpand(t, h, TVE_TOGGLE, TRUE);
    return;
  }
  BOOL bFullRow = (TvStyle(t) & TVS_FULLROWSELECT) != 0;
  BOOL bOnItem = (flags & TVHT_ONITEM) || (bFullRow && (flags & (TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT)));
  if (!bOnItem) return;
  if (!bRight && (flags & TVHT_ONITEMSTATEICON) && (TvStyle(t) & TVS_CHECKBOXES)) TvToggleCheck(t, h);
  if (!bRight) {
    BOOL bWasCaret = h == t->hCaret;
    TvSelect(t, h, TVC_BYMOUSE);
    if (TvNode(t, h) == NULL) return;
    if ((TvStyle(t) & TVS_SINGLEEXPAND) && !bWasCaret) {
      NMTREEVIEWA nm;
      memset(&nm, 0, sizeof(nm));
      TvFillNotifyItem(t, h, &nm.itemNew);
      if (SendNotify(t->hwnd, TVN_SINGLEEXPAND, &nm.hdr) == 0) TvExpand(t, h, TVE_TOGGLE, TRUE);
    }
    if (bWasCaret && bHadFocus && (flags & TVHT_ONITEMLABEL) && (TvStyle(t) & TVS_EDITLABELS)) {
      t->hEditPending = h;
      ::SetTimer(t->hwnd, kEditTimer, ::GetDoubleClickTime(), NULL);
    }
  }
  t->hDown = h;
  t->ptDown = pt;
  t->bDragPending = TRUE;
  t->bRightDrag = bRight;
  ::SetCapture(t->hwnd);
}

/// Does a move of the mouse with a button down: past a few pixels the
/// parent gets TVN_BEGINDRAGA or TVN_BEGINRDRAGA.
void TvMouseMove(W16Tree* t, POINT pt) {
  if (!t->bDragPending) return;
  if (abs(pt.x - t->ptDown.x) < kDragDistance && abs(pt.y - t->ptDown.y) < kDragDistance) return;
  t->bDragPending = FALSE;
  if (::GetCapture() == t->hwnd) ::ReleaseCapture();
  ::KillTimer(t->hwnd, kEditTimer);
  t->hEditPending = 0;
  WORD h = t->hDown;
  t->hDown = 0;
  if (TvNode(t, h) == NULL || (TvStyle(t) & TVS_DISABLEDRAGDROP)) return;
  TvNotify(t, t->bRightDrag ? TVN_BEGINRDRAGA : TVN_BEGINDRAGA, 0, 0, h, t->ptDown);
}

/// Does the release of a button: NM_CLICK or NM_RCLICK to the parent.
void TvButtonUp(W16Tree* t, BOOL bRight) {
  BOOL bWasDown = t->bDragPending && t->bRightDrag == bRight;
  t->bDragPending = FALSE;
  t->hDown = 0;
  if (::GetCapture() == t->hwnd) ::ReleaseCapture();
  if (!bWasDown) return;
  NMHDR nm;
  SendNotify(t->hwnd, bRight ? NM_RCLICK : NM_CLICK, &nm);
}

/// Does the TVM_ messages of a tree view.
LRESULT TvMessage(W16Tree* t, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case TVM_INSERTITEMA:
      if (lParam == 0) return 0;
      return (LRESULT)(UINT)TvInsert(t, (const TVINSERTSTRUCTA FAR*)lParam);
    case TVM_DELETEITEM: {
      WORD h = HL(lParam);
      if (h == HW(TVI_ROOT)) {
        TvDeleteAll(t);
        return TRUE;
      }
      if (TvNode(t, h) == NULL) return FALSE;
      TvDeleteItem(t, h, FALSE);
      TvChanged(t);
      return TRUE;
    }
    case TVM_EXPAND:
      return TvExpand(t, HL(lParam), (UINT)wParam, FALSE);
    case TVM_GETITEMRECT:
      if (lParam == 0) return FALSE;
      return TvGetItemRect(t, *(const WORD FAR*)lParam, (BOOL)wParam, (RECT FAR*)lParam);
    case TVM_GETCOUNT:
      return t->nCount;
    case TVM_GETINDENT:
      return t->nIndent;
    case TVM_SETINDENT: {
      int nMin = (int)TvImageSize(t->himlNormal).cx + 3;
      if (nMin < 8) nMin = 8;
      t->nIndent = (int)wParam < nMin ? nMin : (int)wParam;
      TvChanged(t);
      return 0;
    }
    case TVM_GETIMAGELIST:
      return (LRESULT)(wParam == TVSIL_STATE ? t->himlState : t->himlNormal);
    case TVM_SETIMAGELIST: {
      HIMAGELIST himlNew = (HIMAGELIST)lParam;
      HIMAGELIST himlOld;
      if (wParam == TVSIL_STATE) {
        himlOld = t->himlState;
        if (t->bOwnState && himlOld != NULL && himlOld != himlNew) {
          ::ImageList_Destroy(himlOld);
          himlOld = NULL;
        }
        t->himlState = himlNew;
        t->bOwnState = FALSE;
      } else if (wParam == TVSIL_NORMAL) {
        himlOld = t->himlNormal;
        t->himlNormal = himlNew;
        int nMin = (int)TvImageSize(himlNew).cx + 3;
        if (t->nIndent < nMin) t->nIndent = nMin;
      } else {
        return 0;
      }
      TvChanged(t);
      return (LRESULT)himlOld;
    }
    case TVM_GETNEXTITEM:
      return (LRESULT)(UINT)TvGetNext(t, HL(lParam), (UINT)wParam);
    case TVM_SELECTITEM: {
      WORD h = HL(lParam);
      if (h != 0 && TvNode(t, h) == NULL) return FALSE;
      switch (wParam & 0x00FF) {
        case TVGN_CARET:
          return TvSelect(t, h, TVC_UNKNOWN);
        case TVGN_DROPHILITE:
          TvSetDrop(t, h);
          return TRUE;
        case TVGN_FIRSTVISIBLE:
          if (h == 0) return FALSE;
          TvExpandParents(t, h);
          TvLayout(t);
          TvSetTopRow(t, TvRowOf(t, h));
          return TRUE;
      }
      return FALSE;
    }
    case TVM_GETITEMA:
      if (lParam == 0) return FALSE;
      return TvGetFields(t, (TVITEMA FAR*)lParam);
    case TVM_SETITEMA: {
      if (lParam == 0) return FALSE;
      const TVITEMA FAR* p = (const TVITEMA FAR*)lParam;
      W16TvNode* n = TvNode(t, HW(p->hItem));
      if (n == NULL) return FALSE;
      TvSetFields(t, n, p, TRUE);
      TvChanged(t);
      return TRUE;
    }
    case TVM_EDITLABELA:
      return (LRESULT)(UINT)TvBeginEdit(t, HL(lParam));
    case TVM_GETEDITCONTROL:
      return (LRESULT)(UINT)t->hwndEdit;
    case TVM_GETVISIBLECOUNT: {
      RECT rc;
      ::GetClientRect(hWnd, &rc);
      return rc.bottom / TvItemHeight(t);
    }
    case TVM_HITTEST: {
      if (lParam == 0) return 0;
      TVHITTESTINFO FAR* p = (TVHITTESTINFO FAR*)lParam;
      UINT flags = 0;
      WORD h = TvHitTest(t, p->pt, &flags);
      p->flags = flags;
      p->hItem = HT(h);
      return (LRESULT)(UINT)h;
    }
    case TVM_CREATEDRAGIMAGE:
      return (LRESULT)TvCreateDragImage(t, HL(lParam));
    case TVM_SORTCHILDREN: {
      TvSortData d = {NULL, 0};
      WORD h = HL(lParam);
      return TvSortChildren(t, h == HW(TVI_ROOT) ? 0 : h, d, wParam != 0);
    }
    case TVM_SORTCHILDRENCB: {
      if (lParam == 0) return FALSE;
      const TVSORTCB FAR* p = (const TVSORTCB FAR*)lParam;
      if (p->lpfnCompare == NULL) return FALSE;
      TvSortData d = {p->lpfnCompare, p->lParam};
      WORD h = HW(p->hParent);
      return TvSortChildren(t, h == HW(TVI_ROOT) ? 0 : h, d, FALSE);
    }
    case TVM_ENSUREVISIBLE:
      return TvEnsureVisible(t, HL(lParam));
    case TVM_ENDEDITLABELNOW:
      if (t->hwndEdit == NULL) return FALSE;
      TvEndEdit(t, wParam == 0);
      return TRUE;
    case TVM_GETISEARCHSTRINGA:
      if (lParam != 0) lstrcpy((LPSTR)lParam, t->szSearch);
      return t->nSearch;
    case TVM_SETTOOLTIPS: {
      HWND hwndOld = t->hwndToolTips;
      t->hwndToolTips = (HWND)wParam;
      return (LRESULT)(UINT)hwndOld;
    }
    case TVM_GETTOOLTIPS:
      return (LRESULT)(UINT)t->hwndToolTips;
    case TVM_SETINSERTMARK: {
      WORD h = HL(lParam);
      t->hInsertMark = TvNode(t, h) != NULL ? h : 0;
      t->bInsertAfter = wParam != 0;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case TVM_SETITEMHEIGHT: {
      int nOld = TvItemHeight(t);
      int cy = (int)(short)wParam;
      if (cy == -1) {
        t->nItemHeight = 0;
      } else if (cy > 0) {
        if (!(TvStyle(t) & TVS_NONEVENHEIGHT) && (cy & 1)) cy--;
        t->nItemHeight = cy < 1 ? 1 : cy;
      }
      TvChanged(t);
      return nOld;
    }
    case TVM_GETITEMHEIGHT:
      return TvItemHeight(t);
    case TVM_SETBKCOLOR:
    case TVM_SETTEXTCOLOR:
    case TVM_SETLINECOLOR:
    case TVM_SETINSERTMARKCOLOR: {
      COLORREF& clr = nMsg == TVM_SETBKCOLOR     ? t->clrBk
                      : nMsg == TVM_SETTEXTCOLOR ? t->clrText
                      : nMsg == TVM_SETLINECOLOR ? t->clrLine
                                                 : t->clrInsertMark;
      COLORREF clrOld = clr;
      clr = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)clrOld;
    }
    case TVM_GETBKCOLOR:
      return (LRESULT)t->clrBk;
    case TVM_GETTEXTCOLOR:
      return (LRESULT)t->clrText;
    case TVM_GETLINECOLOR:
      return (LRESULT)t->clrLine;
    case TVM_GETINSERTMARKCOLOR:
      return (LRESULT)t->clrInsertMark;
    case TVM_SETSCROLLTIME: {
      UINT nOld = t->nScrollTime;
      t->nScrollTime = (UINT)wParam;
      return nOld;
    }
    case TVM_GETSCROLLTIME:
      return t->nScrollTime;
    case TVM_GETITEMSTATE: {
      W16TvNode* n = TvNode(t, (WORD)wParam);
      return n != NULL ? (n->state & (UINT)lParam) : 0;
    }
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// Does the window messages of a tree view: the paint, the size, the
/// font, the focus, the scroll bars, the keys and the mouse.
LRESULT TreeMessage(W16Tree* t, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_CREATE:
      if (TvStyle(t) & TVS_CHECKBOXES) TvMakeCheckImages(t);
      TvLayout(t);
      return 0;
    case WM_DESTROY: {
      TvEndEdit(t, FALSE);
      ::KillTimer(hWnd, kEditTimer);
      // The parent gets TVN_DELETEITEMA for each item.
      while (t->hRoot != 0) TvDeleteItem(t, t->hRoot, TRUE);
      t->hCaret = 0;
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      TvPaint(t, hdc, ps.rcPaint);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      if (!t->bInLayout) {
        TvLayout(t);
        ::InvalidateRect(hWnd, NULL, TRUE);
      }
      return 0;
    case WM_SETFONT:
      t->hFont = (HFONT)wParam;
      if (t->hBoldFont != NULL) {
        ::DeleteObject(t->hBoldFont);
        t->hBoldFont = NULL;
      }
      t->bRowsDirty = TRUE;
      TvLayout(t);
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)t->hFont;
    case WM_ENABLE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
      t->bFocus = nMsg == WM_SETFOCUS;
      ::InvalidateRect(hWnd, NULL, TRUE);
      NMHDR nm;
      SendNotify(hWnd, t->bFocus ? NM_SETFOCUS : NM_KILLFOCUS, &nm);
      return 0;
    }
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_VSCROLL:
    case WM_HSCROLL:
      TvScroll(t, nMsg == WM_VSCROLL, (UINT)wParam, (int)(short)LOWORD(lParam));
      return 0;
    case WM_KEYDOWN:
      TvKeyDown(t, (UINT)wParam);
      return 0;
    case WM_CHAR:
      if (wParam > ' ' || (wParam == ' ' && !(TvStyle(t) & TVS_CHECKBOXES) && t->nSearch > 0))
        TvSearch(t, (char)wParam);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      TvButtonDown(t, pt, nMsg == WM_RBUTTONDOWN);
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      TvMouseMove(t, pt);
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
      TvButtonUp(t, nMsg == WM_RBUTTONUP);
      return 0;
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK: {
      ::KillTimer(hWnd, kEditTimer);
      t->hEditPending = 0;
      POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
      UINT flags = 0;
      WORD h = TvHitTest(t, pt, &flags);
      NMHDR nm;
      BOOL bRight = nMsg == WM_RBUTTONDBLCLK;
      if (SendNotify(hWnd, bRight ? NM_RDBLCLK : NM_DBLCLK, &nm) == 0 && !bRight && h != 0 &&
          (flags & (TVHT_ONITEM | TVHT_ONITEMBUTTON)))
        TvExpand(t, h, TVE_TOGGLE, TRUE);
      return 0;
    }
    case WM_TIMER:
      if (wParam == kEditTimer) {
        ::KillTimer(hWnd, kEditTimer);
        WORD h = t->hEditPending;
        t->hEditPending = 0;
        if (h != 0 && h == t->hCaret && !t->bDragPending) TvBeginEdit(t, h);
      }
      return 0;
    case WM_CANCELMODE:
      t->bDragPending = FALSE;
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      return 0;
  }
  return TvMessage(t, hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the tree view class.
LRESULT CALLBACK W16TreeProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Tree* t = StateOf<W16Tree>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Tree>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (t == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, t);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return TreeMessage(t, hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the tree view class. Gives TRUE when the class is there.
BOOL AFXAPI _AfxW16RegisterTreeClass() {
  return RegisterCtlClasses(WC_TREEVIEW, "SysTreeView32", W16TreeProc);
}

#endif  // _AFX_WIN16
