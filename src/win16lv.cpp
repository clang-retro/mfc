// win16lv.cpp - the header control and the list view control windows
// for 16-bit Windows, which has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The windows do the HDM_ and LVM_ messages in ANSI and send the HDN_,
// LVN_ and NM_ notifications of comctl32.dll to their parents. The list
// view has the four views (LVS_ICON, LVS_SMALLICON, LVS_LIST and
// LVS_REPORT), virtual lists (LVS_OWNERDATA), the text and image
// callbacks, label editing, check boxes, full row selection, grid lines,
// custom draw, sorting and the keys and mouse of comctl32.dll. The icon
// views always arrange the items in rows.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

// The 16-bit commctrl.h stops before these header messages.
#ifndef HDM_SETBITMAPMARGIN
#define HDM_SETBITMAPMARGIN (HDM_FIRST + 20)
#endif
#ifndef HDM_GETBITMAPMARGIN
#define HDM_GETBITMAPMARGIN (HDM_FIRST + 21)
#endif
#ifndef HDM_SETFILTERCHANGETIMEOUT
#define HDM_SETFILTERCHANGETIMEOUT (HDM_FIRST + 22)
#endif

namespace {

// =====================================================================
// The header control

// One item of a header.
struct W16HdrItem {
  CString strText;
  int cxy;
  int fmt;
  LPARAM lParam;
  int iImage;
  HBITMAP hbm;
};

// The data of a header control.
struct W16Header {
  HWND hwnd;
  CPtrArray items;    // the items (W16HdrItem*), in index order
  CWordArray order;   // the indexes of the items, in display order
  HFONT hFont;
  HIMAGELIST himl;
  int iPressed;       // the item that a click pushes, or -1
  BOOL bPressedIn;    // the mouse is on the pushed item
  int iTrack;         // the item whose divider the user drags, or -1
  int xTrackStart;    // where the drag started
  int cxyStart;       // the width of iTrack when the drag started
  int nMargin;        // HDM_SETBITMAPMARGIN
  /// Starts with no items.
  W16Header()
      : hwnd(NULL), hFont(NULL), himl(NULL), iPressed(-1), bPressedIn(FALSE), iTrack(-1),
        xTrackStart(0), cxyStart(0), nMargin(6) {}
  /// Deletes the items.
  ~W16Header() {
    for (int i = 0; i < items.GetSize(); i++) delete (W16HdrItem*)items[i];
  }
};

const int kDividerZone = 4;  // the pixels on each side of a divider that drag it

/// Gives the item i of a header.
W16HdrItem& HdrItem(W16Header* h, int i) { return *(W16HdrItem*)h->items[i]; }

/// Gives the rectangle of the item i of a header.
RECT HdrItemRect(W16Header* h, int i) {
  RECT rcClient;
  ::GetClientRect(h->hwnd, &rcClient);
  int x = 0;
  RECT rc = {0, 0, 0, rcClient.bottom};
  for (int n = 0; n < h->order.GetSize(); n++) {
    int k = h->order[n];
    if (k == i) {
      rc.left = x;
      rc.right = x + HdrItem(h, k).cxy;
      return rc;
    }
    x += HdrItem(h, k).cxy;
  }
  return rc;
}

/// Gives the item at a point and where on it the point is (HHT_ flags).
/// A point near the right edge of an item is on its divider.
int HdrHitTest(W16Header* h, POINT pt, UINT* pFlags) {
  RECT rcClient;
  ::GetClientRect(h->hwnd, &rcClient);
  *pFlags = HHT_NOWHERE;
  if (pt.y < 0 || pt.y >= rcClient.bottom) {
    *pFlags = pt.y < 0 ? HHT_ABOVE : HHT_BELOW;
    return -1;
  }
  int x = 0;
  for (int n = 0; n < h->order.GetSize(); n++) {
    int k = h->order[n];
    int xRight = x + HdrItem(h, k).cxy;
    if (pt.x >= xRight - kDividerZone && pt.x < xRight + kDividerZone) {
      // The divider at the right of k, unless the next item starts here
      // and k has no width.
      *pFlags = HHT_ONDIVIDER;
      return k;
    }
    if (pt.x >= x && pt.x < xRight) {
      *pFlags = HHT_ONHEADER;
      return k;
    }
    x = xRight;
  }
  if (pt.x >= x) *pFlags = HHT_NOWHERE;
  return -1;
}

/// Fills a HDITEMA with the fields of an item that its mask names.
void HdrGetItem(W16HdrItem& item, HDITEMA FAR* p, int iOrder) {
  if (p->mask & HDI_WIDTH) p->cxy = item.cxy;
  if ((p->mask & HDI_TEXT) && p->pszText != NULL && p->cchTextMax > 0)
    lstrcpyn(p->pszText, item.strText, p->cchTextMax);
  if (p->mask & HDI_FORMAT) p->fmt = item.fmt;
  if (p->mask & HDI_LPARAM) p->lParam = item.lParam;
  if (p->mask & HDI_IMAGE) p->iImage = item.iImage;
  if (p->mask & HDI_BITMAP) p->hbm = item.hbm;
  if (p->mask & HDI_ORDER) p->iOrder = iOrder;
}

/// Copies the fields of a HDITEMA that its mask names into an item.
void HdrSetItem(W16HdrItem& item, const HDITEMA FAR* p) {
  if (p->mask & HDI_WIDTH) item.cxy = p->cxy < 0 ? 0 : p->cxy;
  if ((p->mask & HDI_TEXT) && p->pszText != NULL && p->pszText != LPSTR_TEXTCALLBACKA)
    item.strText = p->pszText;
  if (p->mask & HDI_FORMAT) item.fmt = p->fmt;
  if (p->mask & HDI_LPARAM) item.lParam = p->lParam;
  if (p->mask & HDI_IMAGE) item.iImage = p->iImage;
  if (p->mask & HDI_BITMAP) item.hbm = p->hbm;
}

/// Gives the place of an item in the display order.
int HdrOrderOf(W16Header* h, int i) {
  for (int n = 0; n < h->order.GetSize(); n++)
    if (h->order[n] == i) return n;
  return -1;
}

/// Sends a header notification with an item and, for the item changes,
/// its HDITEMA. Gives the result of the parent.
LRESULT HdrNotify(W16Header* h, UINT nCode, int i, HDITEMA* pItem) {
  NMHEADERA nm;
  memset(&nm, 0, sizeof(nm));
  nm.iItem = i;
  nm.iButton = 0;
  nm.pitem = pItem;
  return SendNotify(h->hwnd, nCode, &nm.hdr);
}

/// Changes the width of an item, as the user does: HDN_ITEMCHANGINGA can
/// refuse it, then HDN_ITEMCHANGEDA.
void HdrSetWidthByUser(W16Header* h, int i, int cxy) {
  if (cxy < 0) cxy = 0;
  HDITEMA item;
  memset(&item, 0, sizeof(item));
  item.mask = HDI_WIDTH;
  item.cxy = cxy;
  if (HdrNotify(h, HDN_ITEMCHANGINGA, i, &item) != 0) return;
  HdrItem(h, i).cxy = cxy;
  ::InvalidateRect(h->hwnd, NULL, TRUE);
  HdrNotify(h, HDN_ITEMCHANGEDA, i, &item);
}

/// Draws one item of a header: a button face, the image, the text and
/// the sort arrow. An owner-drawn item gets WM_DRAWITEM.
void HdrPaintItem(W16Header* h, HDC hdc, int i) {
  W16HdrItem& item = HdrItem(h, i);
  RECT rc = HdrItemRect(h, i);
  if (rc.right <= rc.left) return;
  BOOL bPushed = i == h->iPressed && h->bPressedIn;
  DrawButtonFace(hdc, rc, bPushed);
  if (item.fmt & HDF_OWNERDRAW) {
    DRAWITEMSTRUCT dis;
    memset(&dis, 0, sizeof(dis));
    dis.CtlType = ODT_HEADER;
    dis.CtlID = (UINT)::GetDlgCtrlID(h->hwnd);
    dis.itemID = (UINT)i;
    dis.itemAction = ODA_DRAWENTIRE;
    dis.itemState = bPushed ? ODS_SELECTED : 0;
    dis.hwndItem = h->hwnd;
    dis.hDC = hdc;
    dis.rcItem = rc;
    dis.itemData = (DWORD)item.lParam;
    ::SendMessage(::GetParent(h->hwnd), WM_DRAWITEM, dis.CtlID, (LPARAM)(LPDRAWITEMSTRUCT)&dis);
    return;
  }
  RECT rcText = rc;
  ::InflateRect(&rcText, -h->nMargin, 0);
  if (bPushed) ::OffsetRect(&rcText, 1, 1);
  if ((item.fmt & HDF_IMAGE) && h->himl != NULL && item.iImage >= 0) {
    int cx = 0, cy = 0;
    ::ImageList_GetIconSize(h->himl, &cx, &cy);
    int y = rcText.top + (rcText.bottom - rcText.top - cy) / 2;
    if (item.fmt & HDF_BITMAP_ON_RIGHT) {
      ::ImageList_Draw(h->himl, item.iImage, hdc, rcText.right - cx, y, ILD_TRANSPARENT);
      rcText.right -= cx + 2;
    } else {
      ::ImageList_Draw(h->himl, item.iImage, hdc, rcText.left, y, ILD_TRANSPARENT);
      rcText.left += cx + 2;
    }
  }
  if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
    RECT rcArrow = rcText;
    rcArrow.left = rcArrow.right - 12;
    if (rcArrow.left > rcText.left) {
      DrawArrow(hdc, rcArrow, (item.fmt & HDF_SORTUP) ? 0 : 1, TRUE, FALSE);
      rcText.right = rcArrow.left - 2;
    }
  }
  if ((item.fmt & HDF_STRING) || !(item.fmt & (HDF_BITMAP | HDF_IMAGE))) {
    UINT nAlign = (item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT    ? DT_RIGHT
                  : (item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER
                                                               : DT_LEFT;
    HGDIOBJ hOld = ::SelectObject(hdc, h->hFont != NULL ? (HGDIOBJ)h->hFont
                                                        : ::GetStockObject(ANSI_VAR_FONT));
    int nOldMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF clrOld = ::SetTextColor(hdc, ::GetSysColor(COLOR_BTNTEXT));
    ::DrawText(hdc, item.strText, item.strText.GetLength(), &rcText,
               nAlign | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SetTextColor(hdc, clrOld);
    ::SetBkMode(hdc, nOldMode);
    ::SelectObject(hdc, hOld);
  }
}

/// Draws a header: the items, then a face after the last item.
void PaintHeader(W16Header* h, HDC hdc) {
  RECT rcClient;
  ::GetClientRect(h->hwnd, &rcClient);
  int xEnd = 0;
  for (int n = 0; n < h->order.GetSize(); n++) {
    HdrPaintItem(h, hdc, h->order[n]);
    xEnd += HdrItem(h, h->order[n]).cxy;
  }
  if (xEnd < rcClient.right) {
    RECT rc = rcClient;
    rc.left = xEnd;
    DrawButtonFace(hdc, rc, FALSE);
  }
}

/// Does the mouse and the HDM_ messages of a header.
LRESULT HeaderMessage(W16Header* h, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintHeader(h, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SETFONT:
      h->hFont = (HFONT)wParam;
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)h->hFont;
    case WM_SETCURSOR: {
      POINT pt;
      ::GetCursorPos(&pt);
      ::ScreenToClient(hWnd, &pt);
      UINT nFlags;
      HdrHitTest(h, pt, &nFlags);
      if (nFlags == HHT_ONDIVIDER || h->iTrack >= 0) {
        ::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
        return TRUE;
      }
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      UINT nFlags;
      int i = HdrHitTest(h, pt, &nFlags);
      if (i < 0) return 0;
      if (nFlags == HHT_ONDIVIDER) {
        if (nMsg == WM_LBUTTONDBLCLK) {
          HdrNotify(h, HDN_DIVIDERDBLCLICKA, i, NULL);
          return 0;
        }
        HDITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = HDI_WIDTH;
        item.cxy = HdrItem(h, i).cxy;
        if (HdrNotify(h, HDN_BEGINTRACKA, i, &item) != 0) return 0;
        h->iTrack = i;
        h->xTrackStart = pt.x;
        h->cxyStart = HdrItem(h, i).cxy;
        ::SetCapture(hWnd);
        return 0;
      }
      if (StyleOf(hWnd) & HDS_BUTTONS) {
        h->iPressed = i;
        h->bPressedIn = TRUE;
        ::SetCapture(hWnd);
        ::InvalidateRect(hWnd, NULL, FALSE);
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      if (h->iTrack >= 0) {
        int cxy = h->cxyStart + pt.x - h->xTrackStart;
        if (cxy < 0) cxy = 0;
        HDITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = HDI_WIDTH;
        item.cxy = cxy;
        if (HdrNotify(h, HDN_TRACKA, h->iTrack, &item) == 0 &&
            (StyleOf(hWnd) & HDS_FULLDRAG))
          HdrSetWidthByUser(h, h->iTrack, cxy);
        else if (!(StyleOf(hWnd) & HDS_FULLDRAG))
          HdrItem(h, h->iTrack).cxy = cxy, ::InvalidateRect(hWnd, NULL, TRUE);
      } else if (h->iPressed >= 0) {
        RECT rc = HdrItemRect(h, h->iPressed);
        BOOL bIn = ::PtInRect(&rc, pt);
        if (bIn != h->bPressedIn) {
          h->bPressedIn = bIn;
          ::InvalidateRect(hWnd, &rc, FALSE);
        }
      }
      return 0;
    }
    case WM_LBUTTONUP:
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      if (h->iTrack >= 0) {
        int i = h->iTrack;
        h->iTrack = -1;
        HDITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = HDI_WIDTH;
        item.cxy = HdrItem(h, i).cxy;
        HdrNotify(h, HDN_ENDTRACKA, i, &item);
        int cxyNew = item.cxy;
        HdrItem(h, i).cxy = h->cxyStart;
        HdrSetWidthByUser(h, i, cxyNew);
      } else if (h->iPressed >= 0) {
        int i = h->iPressed;
        BOOL bIn = h->bPressedIn;
        h->iPressed = -1;
        h->bPressedIn = FALSE;
        ::InvalidateRect(hWnd, NULL, FALSE);
        if (bIn) HdrNotify(h, HDN_ITEMCLICKA, i, NULL);
      }
      return 0;
    case HDM_GETITEMCOUNT:
      return h->items.GetSize();
    case HDM_INSERTITEMA: {
      const HDITEMA FAR* p = (const HDITEMA FAR*)lParam;
      if (p == NULL) return -1;
      int i = (int)wParam;
      if (i < 0 || i > h->items.GetSize()) i = (int)h->items.GetSize();
      W16HdrItem* pItem = new W16HdrItem;
      pItem->cxy = 0;
      pItem->fmt = HDF_LEFT | HDF_STRING;
      pItem->lParam = 0;
      pItem->iImage = -1;
      pItem->hbm = NULL;
      HdrSetItem(*pItem, p);
      h->items.InsertAt(i, pItem);
      for (int n = 0; n < h->order.GetSize(); n++)
        if ((int)h->order[n] >= i) h->order[n]++;
      int nOrder = (p->mask & HDI_ORDER) && p->iOrder >= 0 && p->iOrder <= h->order.GetSize()
                       ? p->iOrder
                       : (int)h->order.GetSize();
      h->order.InsertAt(nOrder, (WORD)i);
      ::InvalidateRect(hWnd, NULL, TRUE);
      return i;
    }
    case HDM_DELETEITEM: {
      int i = (int)wParam;
      if (i < 0 || i >= h->items.GetSize()) return FALSE;
      delete &HdrItem(h, i);
      h->items.RemoveAt(i);
      h->order.RemoveAt(HdrOrderOf(h, i));
      for (int n = 0; n < h->order.GetSize(); n++)
        if ((int)h->order[n] > i) h->order[n]--;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case HDM_GETITEMA:
    case HDM_SETITEMA: {
      int i = (int)wParam;
      if (i < 0 || i >= h->items.GetSize() || lParam == 0) return FALSE;
      if (nMsg == HDM_GETITEMA) {
        HdrGetItem(HdrItem(h, i), (HDITEMA FAR*)lParam, HdrOrderOf(h, i));
        return TRUE;
      }
      const HDITEMA FAR* p = (const HDITEMA FAR*)lParam;
      HDITEMA item = *p;
      if (HdrNotify(h, HDN_ITEMCHANGINGA, i, &item) != 0) return FALSE;
      HdrSetItem(HdrItem(h, i), p);
      if ((p->mask & HDI_ORDER) && p->iOrder >= 0 && p->iOrder < h->order.GetSize()) {
        h->order.RemoveAt(HdrOrderOf(h, i));
        h->order.InsertAt(p->iOrder, (WORD)i);
      }
      ::InvalidateRect(hWnd, NULL, TRUE);
      HdrNotify(h, HDN_ITEMCHANGEDA, i, &item);
      return TRUE;
    }
    case HDM_LAYOUT: {
      HDLAYOUT FAR* p = (HDLAYOUT FAR*)lParam;
      if (p == NULL || p->prc == NULL || p->pwpos == NULL) return FALSE;
      int cy = (StyleOf(hWnd) & HDS_HIDDEN) ? 0 : FontHeight(hWnd, h->hFont) + 6;
      p->pwpos->hwnd = hWnd;
      p->pwpos->hwndInsertAfter = NULL;
      p->pwpos->x = p->prc->left;
      p->pwpos->y = p->prc->top;
      p->pwpos->cx = p->prc->right - p->prc->left;
      p->pwpos->cy = cy;
      p->pwpos->flags = SWP_NOZORDER;
      p->prc->top += cy;
      return TRUE;
    }
    case HDM_HITTEST: {
      HDHITTESTINFO FAR* p = (HDHITTESTINFO FAR*)lParam;
      if (p == NULL) return -1;
      p->iItem = HdrHitTest(h, p->pt, &p->flags);
      return p->iItem;
    }
    case HDM_GETITEMRECT: {
      int i = (int)wParam;
      if (i < 0 || i >= h->items.GetSize() || lParam == 0) return FALSE;
      *(RECT FAR*)lParam = HdrItemRect(h, i);
      return TRUE;
    }
    case HDM_SETIMAGELIST: {
      HIMAGELIST himlOld = h->himl;
      h->himl = (HIMAGELIST)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return (LRESULT)himlOld;
    }
    case HDM_GETIMAGELIST:
      return (LRESULT)h->himl;
    case HDM_ORDERTOINDEX:
      return (int)wParam >= 0 && (int)wParam < h->order.GetSize() ? h->order[(int)wParam]
                                                                 : (LRESULT)wParam;
    case HDM_GETORDERARRAY: {
      int FAR* p = (int FAR*)lParam;
      if (p == NULL || (int)wParam < h->order.GetSize()) return FALSE;
      for (int n = 0; n < h->order.GetSize(); n++) p[n] = h->order[n];
      return TRUE;
    }
    case HDM_SETORDERARRAY: {
      const int FAR* p = (const int FAR*)lParam;
      if (p == NULL || (int)wParam != h->order.GetSize()) return FALSE;
      for (int n = 0; n < h->order.GetSize(); n++) h->order[n] = (WORD)p[n];
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case HDM_SETBITMAPMARGIN: {
      int nOld = h->nMargin;
      h->nMargin = (int)wParam;
      return nOld;
    }
    case HDM_GETBITMAPMARGIN:
      return h->nMargin;
    case HDM_SETHOTDIVIDER:
    case HDM_CREATEDRAGIMAGE:
    case HDM_SETFILTERCHANGETIMEOUT:
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the header class.
LRESULT CALLBACK W16HeaderProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Header* h = StateOf<W16Header>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Header>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (h == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, h);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return HeaderMessage(h, hWnd, nMsg, wParam, lParam);
}

// =====================================================================
// The list view control

const int kEditTimer = 1;     // the timer that starts a label edit after a click
const int kIconCellX = 75;    // the width of a cell in the icon view
const int kSmallCellX = 120;  // the width of a cell in the small icon view
const int kCheckSize = 13;    // the size of a check box

// One item of a list view (not in a virtual list).
struct W16LvItem {
  CString strText;
  BOOL bTextCallback;   // the text is LPSTR_TEXTCALLBACK
  int iImage;
  UINT state;
  LPARAM lParam;
  int iIndent;
  CStringArray subText;  // the texts of the subitems 1, 2 and so on
};

// The data of a list view.
struct W16List {
  HWND hwnd;
  HWND hwndHeader;
  HWND hwndEdit;          // the edit control of a label edit, or NULL
  int iEdit;              // the item whose label is edited
  WNDPROC pfnEditProc;    // the procedure of the edit control before the list
  CPtrArray items;        // the items (W16LvItem*), not with LVS_OWNERDATA
  int nOwnerCount;        // the number of items of a virtual list
  CDWordArray ownerState; // the states of the items of a virtual list
  CWordArray colSub;      // the subitem of each column
  HIMAGELIST himlNormal, himlSmall, himlState;
  HFONT hFont;
  COLORREF clrBk, clrText, clrTextBk;
  DWORD dwExStyle;
  UINT nCallbackMask;
  POINT ptOrigin;         // the scroll position of the content, in pixels
  int iFocus;             // the item with LVIS_FOCUSED, or -1
  int iMark;              // the selection mark (the anchor of a shift range)
  int iHot;
  BOOL bFocus;
  BOOL bDragPending;      // the left (or right) button is down on an item
  BOOL bRightDrag;
  POINT ptDown;           // where the button went down
  int iDown;              // the item under the button
  BOOL bWasSelected;      // iDown was selected and had the focus before the press
  DWORD dwLastStyle;      // the style of the last layout
  int nColumnWidth;       // the width of a column of the list view (LVS_LIST)
  SIZE sizeSpacing;       // LVM_SETICONSPACING (0: the default)
  /// Starts empty, with the colors of the system.
  W16List()
      : hwnd(NULL), hwndHeader(NULL), hwndEdit(NULL), iEdit(-1), pfnEditProc(NULL),
        nOwnerCount(0), himlNormal(NULL), himlSmall(NULL), himlState(NULL), hFont(NULL),
        clrBk(CLR_DEFAULT), clrText(CLR_DEFAULT), clrTextBk(CLR_DEFAULT), dwExStyle(0),
        nCallbackMask(0), iFocus(-1), iMark(-1), iHot(-1), bFocus(FALSE),
        bDragPending(FALSE), bRightDrag(FALSE), iDown(-1), bWasSelected(FALSE),
        dwLastStyle(0xFFFFFFFFL), nColumnWidth(0) {
    ptOrigin.x = ptOrigin.y = 0;
    ptDown.x = ptDown.y = 0;
    sizeSpacing.cx = sizeSpacing.cy = 0;
  }
  /// Deletes the items.
  ~W16List() {
    for (int i = 0; i < items.GetSize(); i++) delete (W16LvItem*)items[i];
  }
};

/// Gives TRUE for a virtual list (LVS_OWNERDATA).
BOOL LvOwnerData(W16List* l) { return (StyleOf(l->hwnd) & LVS_OWNERDATA) != 0; }

/// Gives the view of a list (LVS_ICON, LVS_REPORT, LVS_SMALLICON or
/// LVS_LIST).
DWORD LvView(W16List* l) { return StyleOf(l->hwnd) & LVS_TYPEMASK; }

/// Gives the number of items.
int LvCount(W16List* l) { return LvOwnerData(l) ? l->nOwnerCount : (int)l->items.GetSize(); }

/// Gives the item i (not of a virtual list).
W16LvItem& LvItem(W16List* l, int i) { return *(W16LvItem*)l->items[i]; }

/// Gives TRUE when i is an item.
BOOL LvIsItem(W16List* l, int i) { return i >= 0 && i < LvCount(l); }

/// Gives the state of an item that the list keeps.
UINT LvGetState(W16List* l, int i) {
  if (!LvIsItem(l, i)) return 0;
  if (LvOwnerData(l)) return i < l->ownerState.GetSize() ? (UINT)l->ownerState[i] : 0;
  return LvItem(l, i).state;
}

/// Sets the state of an item that the list keeps.
void LvPutState(W16List* l, int i, UINT state) {
  if (!LvIsItem(l, i)) return;
  if (LvOwnerData(l)) {
    if (i >= l->ownerState.GetSize()) l->ownerState.SetSize(i + 1);
    l->ownerState[i] = state;
  } else {
    LvItem(l, i).state = state;
  }
}

/// Gives the font of a list.
HFONT LvFont(W16List* l) {
  return l->hFont != NULL ? l->hFont : (HFONT)::GetStockObject(ANSI_VAR_FONT);
}

/// Gives the size of the images of an image list, or 0 by 0 without one.
SIZE ImageSize(HIMAGELIST himl) {
  SIZE size = {0, 0};
  int cx = 0, cy = 0;
  if (himl != NULL && ::ImageList_GetIconSize(himl, &cx, &cy)) {
    size.cx = cx;
    size.cy = cy;
  }
  return size;
}

/// Gives the text and the image of an item or a subitem: from the item,
/// or from the parent with LVN_GETDISPINFOA for a callback.
void LvGetDisplay(W16List* l, int i, int iSub, CString* pText, int* piImage) {
  BOOL bTextCallback, bImageCallback;
  if (LvOwnerData(l)) {
    bTextCallback = bImageCallback = TRUE;
  } else {
    W16LvItem& item = LvItem(l, i);
    if (iSub == 0) {
      bTextCallback = item.bTextCallback;
      if (pText != NULL && !bTextCallback) *pText = item.strText;
      bImageCallback = item.iImage == I_IMAGECALLBACK;
      if (piImage != NULL && !bImageCallback) *piImage = item.iImage;
    } else {
      bTextCallback = iSub - 1 < item.subText.GetSize() &&
                      item.subText[iSub - 1] == CString((LPCSTR)"\x01" "callback");
      if (pText != NULL && !bTextCallback)
        *pText = iSub - 1 < item.subText.GetSize() ? item.subText[iSub - 1] : CString();
      bImageCallback = FALSE;
      if (piImage != NULL) *piImage = -1;
    }
  }
  if (!(bTextCallback && pText != NULL) && !(bImageCallback && piImage != NULL)) return;
  char szText[260];
  szText[0] = 0;
  NMLVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.item.iItem = i;
  nm.item.iSubItem = iSub;
  if (bTextCallback && pText != NULL) nm.item.mask |= LVIF_TEXT;
  if (bImageCallback && piImage != NULL) nm.item.mask |= LVIF_IMAGE;
  nm.item.pszText = szText;
  nm.item.cchTextMax = sizeof(szText);
  nm.item.iImage = -1;
  if (!LvOwnerData(l)) nm.item.lParam = LvItem(l, i).lParam;
  SendNotify(l->hwnd, LVN_GETDISPINFOA, &nm.hdr);
  if ((nm.item.mask & LVIF_TEXT) && pText != NULL)
    *pText = nm.item.pszText != NULL && nm.item.pszText != LPSTR_TEXTCALLBACKA ? nm.item.pszText
                                                                               : "";
  if ((nm.item.mask & LVIF_IMAGE) && piImage != NULL) *piImage = nm.item.iImage;
}

/// Gives the width of a text in the font of the list.
int LvTextWidth(W16List* l, LPCSTR lpsz) {
  HDC hdc = ::GetDC(l->hwnd);
  HGDIOBJ hOld = ::SelectObject(hdc, LvFont(l));
  SIZE size;
  ::GetTextExtentPoint32(hdc, lpsz, lstrlen(lpsz), &size);
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(l->hwnd, hdc);
  return size.cx;
}

/// Gives the height of a row of the report and list views.
int LvRowHeight(W16List* l) {
  int cy = FontHeight(l->hwnd, l->hFont) + 2;
  SIZE sizeSmall = ImageSize(l->himlSmall);
  if (sizeSmall.cy + 1 > cy) cy = sizeSmall.cy + 1;
  SIZE sizeState = ImageSize(l->himlState);
  if (sizeState.cy + 1 > cy) cy = sizeState.cy + 1;
  if ((l->dwExStyle & LVS_EX_CHECKBOXES) && kCheckSize + 2 > cy) cy = kCheckSize + 2;
  if (l->dwExStyle & LVS_EX_GRIDLINES) cy++;
  return cy;
}

/// Gives the size of a cell of the icon and small icon views.
SIZE LvCellSize(W16List* l) {
  SIZE size;
  if (l->sizeSpacing.cx > 0 && LvView(l) == LVS_ICON) return l->sizeSpacing;
  if (LvView(l) == LVS_ICON) {
    SIZE sizeImage = ImageSize(l->himlNormal);
    if (sizeImage.cx == 0) sizeImage.cx = sizeImage.cy = 32;
    size.cx = kIconCellX > sizeImage.cx + 8 ? kIconCellX : sizeImage.cx + 8;
    size.cy = sizeImage.cy + 4 + 2 * (FontHeight(l->hwnd, l->hFont)) + 6;
  } else {
    size.cx = kSmallCellX;
    size.cy = LvRowHeight(l);
  }
  return size;
}

/// Gives the height of the header in the report view.
int LvHeaderHeight(W16List* l) {
  if (LvView(l) != LVS_REPORT || l->hwndHeader == NULL || (StyleOf(l->hwnd) & LVS_NOCOLUMNHEADER))
    return 0;
  RECT rc;
  ::GetWindowRect(l->hwndHeader, &rc);
  return rc.bottom - rc.top;
}

/// Gives the number of columns.
int LvColumnCount(W16List* l) {
  return l->hwndHeader != NULL ? (int)::SendMessage(l->hwndHeader, HDM_GETITEMCOUNT, 0, 0) : 0;
}

/// Gives the left edge and the width of a column, in content coordinates.
BOOL LvColumnRect(W16List* l, int iCol, int* px, int* pcx) {
  if (l->hwndHeader == NULL || iCol < 0 || iCol >= LvColumnCount(l)) return FALSE;
  RECT rc;
  ::SendMessage(l->hwndHeader, HDM_GETITEMRECT, iCol, (LPARAM)(RECT FAR*)&rc);
  *px = rc.left;
  *pcx = rc.right - rc.left;
  return TRUE;
}

/// Gives the width of all the columns.
int LvColumnsWidth(W16List* l) {
  int cx = 0;
  for (int i = 0; i < LvColumnCount(l); i++) {
    int x, w;
    if (LvColumnRect(l, i, &x, &w) && x + w > cx) cx = x + w;
  }
  return cx;
}

/// Gives the rectangle of the area that shows items (the client area
/// below the header).
RECT LvViewArea(W16List* l) {
  RECT rc;
  ::GetClientRect(l->hwnd, &rc);
  rc.top += LvHeaderHeight(l);
  return rc;
}

/// Gives the number of items in a column of the list view, or in a row
/// of the icon views.
int LvPerLine(W16List* l) {
  RECT rc = LvViewArea(l);
  DWORD dwView = LvView(l);
  if (dwView == LVS_LIST) {
    int n = (rc.bottom - rc.top) / LvRowHeight(l);
    return n > 0 ? n : 1;
  }
  if (dwView == LVS_ICON || dwView == LVS_SMALLICON) {
    int n = (rc.right - rc.left) / LvCellSize(l).cx;
    return n > 0 ? n : 1;
  }
  return 1;
}

/// Gives the rectangle of the whole item i in content coordinates.
RECT LvItemCell(W16List* l, int i) {
  RECT rc;
  DWORD dwView = LvView(l);
  if (dwView == LVS_REPORT) {
    int cy = LvRowHeight(l);
    int cx = LvColumnsWidth(l);
    ::SetRect(&rc, 0, i * cy, cx, (i + 1) * cy);
  } else if (dwView == LVS_LIST) {
    int cy = LvRowHeight(l), n = LvPerLine(l);
    int x = (i / n) * l->nColumnWidth;
    ::SetRect(&rc, x, (i % n) * cy, x + l->nColumnWidth, (i % n + 1) * cy);
  } else {
    SIZE size = LvCellSize(l);
    int n = LvPerLine(l);
    int x = (i % n) * size.cx, y = (i / n) * size.cy;
    ::SetRect(&rc, x, y, x + size.cx, y + size.cy);
  }
  return rc;
}

/// Gives the size of the content of a list, in pixels.
SIZE LvContentSize(W16List* l) {
  SIZE size = {0, 0};
  int n = LvCount(l);
  DWORD dwView = LvView(l);
  if (dwView == LVS_REPORT) {
    size.cx = LvColumnsWidth(l);
    size.cy = n * LvRowHeight(l);
  } else if (dwView == LVS_LIST) {
    int nPer = LvPerLine(l);
    size.cx = ((n + nPer - 1) / nPer) * l->nColumnWidth;
    size.cy = nPer * LvRowHeight(l);
  } else {
    SIZE cell = LvCellSize(l);
    int nPer = LvPerLine(l);
    size.cx = nPer * cell.cx;
    size.cy = ((n + nPer - 1) / nPer) * cell.cy;
  }
  return size;
}

/// Converts content coordinates to client coordinates.
RECT LvToClient(W16List* l, RECT rc) {
  ::OffsetRect(&rc, -l->ptOrigin.x, -l->ptOrigin.y + LvHeaderHeight(l));
  return rc;
}

/// Gives the parts of an item in client coordinates: the state image
/// (or check box), the icon and the label. In the report view these are
/// in the first column.
void LvItemParts(W16List* l, int i, RECT* prcBounds, RECT* prcState, RECT* prcIcon,
                 RECT* prcLabel) {
  RECT rcCell = LvToClient(l, LvItemCell(l, i));
  RECT rcState = rcCell, rcIcon = rcCell, rcLabel = rcCell;
  DWORD dwView = LvView(l);
  if (dwView == LVS_ICON) {
    SIZE sizeImage = ImageSize(l->himlNormal);
    if (sizeImage.cx == 0) sizeImage.cx = sizeImage.cy = 32;
    int x = rcCell.left + (rcCell.right - rcCell.left - sizeImage.cx) / 2;
    ::SetRect(&rcIcon, x, rcCell.top + 2, x + sizeImage.cx, rcCell.top + 2 + sizeImage.cy);
    rcState = rcIcon;
    rcState.right = rcState.left;
    if (l->dwExStyle & LVS_EX_CHECKBOXES) {
      rcState.left -= kCheckSize + 2;
      rcState.top = rcIcon.bottom - kCheckSize;
    }
    rcLabel.top = rcIcon.bottom + 2;
    rcLabel.bottom = rcCell.bottom - 2;
    ::InflateRect(&rcLabel, -2, 0);
  } else {
    int x = rcCell.left;
    if (dwView == LVS_REPORT) {
      int xCol, cxCol;
      if (LvColumnRect(l, 0, &xCol, &cxCol)) {
        x = rcCell.left + xCol;
        rcCell.right = x + cxCol;
      }
      if (!LvOwnerData(l)) x += LvItem(l, i).iIndent * ImageSize(l->himlSmall).cx;
    }
    x += 2;
    int cxState = (l->dwExStyle & LVS_EX_CHECKBOXES) ? kCheckSize + 2 : ImageSize(l->himlState).cx;
    ::SetRect(&rcState, x, rcCell.top, x + cxState, rcCell.bottom);
    x += cxState;
    int cxIcon = ImageSize(l->himlSmall).cx;
    ::SetRect(&rcIcon, x, rcCell.top, x + cxIcon, rcCell.bottom);
    x += cxIcon + (cxIcon > 0 ? 2 : 0);
    ::SetRect(&rcLabel, x, rcCell.top, rcCell.right, rcCell.bottom);
    if (dwView != LVS_REPORT) {
      CString strText;
      LvGetDisplay(l, i, 0, &strText, NULL);
      int cxText = LvTextWidth(l, strText) + 4;
      if (rcLabel.left + cxText < rcLabel.right) rcLabel.right = rcLabel.left + cxText;
    }
  }
  if (prcBounds != NULL) *prcBounds = LvToClient(l, LvItemCell(l, i));
  if (prcState != NULL) *prcState = rcState;
  if (prcIcon != NULL) *prcIcon = rcIcon;
  if (prcLabel != NULL) *prcLabel = rcLabel;
}

/// Gives the rectangle of a subitem in the report view, in client
/// coordinates.
RECT LvSubItemRect(W16List* l, int i, int iCol) {
  RECT rc = LvToClient(l, LvItemCell(l, i));
  int x, cx;
  if (LvColumnRect(l, iCol, &x, &cx)) {
    rc.left = rc.left + x;
    rc.right = rc.left + cx;
  }
  return rc;
}

/// Calculates the width of the columns of the list view (LVS_LIST): the
/// widest item.
void LvMeasureList(W16List* l) {
  int cx = 0;
  int n = LvCount(l);
  if (n > 500) n = 500;  // a virtual list can be very long
  int cxImage = ImageSize(l->himlSmall).cx + ImageSize(l->himlState).cx;
  for (int i = 0; i < n; i++) {
    CString strText;
    LvGetDisplay(l, i, 0, &strText, NULL);
    int w = LvTextWidth(l, strText);
    if (w > cx) cx = w;
  }
  l->nColumnWidth = cx + cxImage + 12;
  if (l->nColumnWidth < 40) l->nColumnWidth = 40;
}

/// Places the header in the report view, hides it in the other views,
/// and sets the scroll bars for the size of the content.
void LvLayout(W16List* l) {
  DWORD dwStyle = StyleOf(l->hwnd);
  DWORD dwView = dwStyle & LVS_TYPEMASK;
  if (l->hwndHeader != NULL) {
    if (dwView == LVS_REPORT && !(dwStyle & LVS_NOCOLUMNHEADER)) {
      RECT rc;
      ::GetClientRect(l->hwnd, &rc);
      WINDOWPOS wp;
      HDLAYOUT hl;
      hl.prc = &rc;
      hl.pwpos = &wp;
      ::SendMessage(l->hwndHeader, HDM_LAYOUT, 0, (LPARAM)(HDLAYOUT FAR*)&hl);
      int xWidth = LvColumnsWidth(l);
      int cx = wp.cx > xWidth ? wp.cx : xWidth;
      ::SetWindowPos(l->hwndHeader, NULL, -l->ptOrigin.x, 0, cx + l->ptOrigin.x, wp.cy,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      ::ShowWindow(l->hwndHeader, SW_SHOWNA);
    } else {
      ::ShowWindow(l->hwndHeader, SW_HIDE);
    }
  }
  if (dwView == LVS_LIST) LvMeasureList(l);
  l->dwLastStyle = dwStyle;
  RECT rcView = LvViewArea(l);
  SIZE sizeContent = LvContentSize(l);
  int cxView = rcView.right - rcView.left, cyView = rcView.bottom - rcView.top;
  int nMaxX = (int)MaxL(0, sizeContent.cx - cxView);
  int nMaxY = (int)MaxL(0, sizeContent.cy - cyView);
  if (dwView == LVS_LIST) nMaxY = 0;
  if (dwView != LVS_LIST && dwView != LVS_REPORT) nMaxX = 0;
  if (l->ptOrigin.x > nMaxX) l->ptOrigin.x = nMaxX;
  if (l->ptOrigin.y > nMaxY) l->ptOrigin.y = nMaxY;
  BOOL bNoScroll = (dwStyle & LVS_NOSCROLL) != 0;
  ::SetScrollRange(l->hwnd, SB_HORZ, 0, bNoScroll ? 0 : nMaxX, FALSE);
  ::SetScrollPos(l->hwnd, SB_HORZ, l->ptOrigin.x, TRUE);
  ::SetScrollRange(l->hwnd, SB_VERT, 0, bNoScroll ? 0 : nMaxY, FALSE);
  ::SetScrollPos(l->hwnd, SB_VERT, l->ptOrigin.y, TRUE);
}

/// Lays out a list again and redraws it.
void LvChanged(W16List* l) {
  LvLayout(l);
  ::InvalidateRect(l->hwnd, NULL, TRUE);
}

/// Redraws one item.
void LvInvalidateItem(W16List* l, int i) {
  if (!LvIsItem(l, i)) return;
  RECT rc;
  LvItemParts(l, i, &rc, NULL, NULL, NULL);
  if (LvView(l) == LVS_REPORT) {
    RECT rcClient;
    ::GetClientRect(l->hwnd, &rcClient);
    rc.left = rcClient.left;
    rc.right = rcClient.right;
  }
  ::InvalidateRect(l->hwnd, &rc, TRUE);
}

/// Gives the item at a point of the client area, and where on it the
/// point is (LVHT_ flags).
int LvHitTest(W16List* l, POINT pt, UINT* pFlags, int* piSub) {
  *pFlags = LVHT_NOWHERE;
  if (piSub != NULL) *piSub = 0;
  RECT rcView = LvViewArea(l);
  if (!::PtInRect(&rcView, pt)) {
    *pFlags = pt.y < rcView.top ? LVHT_ABOVE : pt.y >= rcView.bottom ? LVHT_BELOW
              : pt.x < rcView.left ? LVHT_TOLEFT : LVHT_TORIGHT;
    return -1;
  }
  int n = LvCount(l);
  int iFirst = 0, iLast = n - 1;
  DWORD dwView = LvView(l);
  if (dwView == LVS_REPORT) {
    int cy = LvRowHeight(l);
    int i = (pt.y - rcView.top + l->ptOrigin.y) / cy;
    iFirst = iLast = i;
  }
  for (int i = iFirst; i <= iLast && i < n; i++) {
    if (i < 0) continue;
    RECT rcBounds, rcState, rcIcon, rcLabel;
    LvItemParts(l, i, &rcBounds, &rcState, &rcIcon, &rcLabel);
    if (!::PtInRect(&rcBounds, pt)) continue;
    if (piSub != NULL && dwView == LVS_REPORT) {
      for (int c = 0; c < LvColumnCount(l); c++) {
        RECT rcSub = LvSubItemRect(l, i, c);
        if (pt.x >= rcSub.left && pt.x < rcSub.right) *piSub = l->colSub[c];
      }
    }
    if (::PtInRect(&rcState, pt))
      *pFlags = LVHT_ONITEMSTATEICON;
    else if (::PtInRect(&rcIcon, pt))
      *pFlags = LVHT_ONITEMICON;
    else if (::PtInRect(&rcLabel, pt) || (dwView == LVS_REPORT &&
                                           ((l->dwExStyle & LVS_EX_FULLROWSELECT) ||
                                            (piSub != NULL && *piSub != 0))))
      *pFlags = LVHT_ONITEMLABEL;
    else if (dwView == LVS_REPORT)
      *pFlags = LVHT_ONITEMLABEL;  // the rest of the first column
    else
      continue;
    return i;
  }
  return -1;
}

/// Sends LVN_ITEMCHANGING or LVN_ITEMCHANGED for a change of state.
/// Gives the result of the parent.
LRESULT LvNotifyChange(W16List* l, UINT nCode, int i, UINT uOld, UINT uNew, UINT uChanged) {
  NMLISTVIEW nm;
  memset(&nm, 0, sizeof(nm));
  nm.iItem = i;
  nm.uOldState = uOld;
  nm.uNewState = uNew;
  nm.uChanged = uChanged;
  if (LvIsItem(l, i) && !LvOwnerData(l)) nm.lParam = LvItem(l, i).lParam;
  return SendNotify(l->hwnd, nCode, &nm.hdr);
}

/// Changes the state bits of mask of an item to state. The parent gets
/// LVN_ITEMCHANGING, which can refuse it, then LVN_ITEMCHANGED. With
/// LVS_SINGLESEL, selecting an item clears the others; giving the focus
/// takes it from the other item. Gives FALSE when the parent refused.
BOOL LvSetState(W16List* l, int i, UINT state, UINT mask) {
  if (!LvIsItem(l, i)) return FALSE;
  UINT uOld = LvGetState(l, i);
  UINT uNew = (uOld & ~mask) | (state & mask);
  if (uNew == uOld) return TRUE;
  if (LvNotifyChange(l, LVN_ITEMCHANGING, i, uOld, uNew, LVIF_STATE) != 0) return FALSE;
  if ((uNew & LVIS_SELECTED) && !(uOld & LVIS_SELECTED) && (StyleOf(l->hwnd) & LVS_SINGLESEL)) {
    for (int k = 0; k < LvCount(l); k++)
      if (k != i && (LvGetState(l, k) & LVIS_SELECTED)) LvSetState(l, k, 0, LVIS_SELECTED);
  }
  if ((uNew & LVIS_FOCUSED) && !(uOld & LVIS_FOCUSED)) {
    if (l->iFocus != i && LvIsItem(l, l->iFocus)) LvSetState(l, l->iFocus, 0, LVIS_FOCUSED);
    l->iFocus = i;
  } else if (!(uNew & LVIS_FOCUSED) && l->iFocus == i) {
    l->iFocus = -1;
  }
  LvPutState(l, i, uNew);
  LvInvalidateItem(l, i);
  LvNotifyChange(l, LVN_ITEMCHANGED, i, uOld, uNew, LVIF_STATE);
  return TRUE;
}

/// Clears the selection of all the items but one (-1: of all).
void LvClearSelection(W16List* l, int iKeep) {
  int n = LvCount(l);
  for (int i = 0; i < n; i++)
    if (i != iKeep && (LvGetState(l, i) & LVIS_SELECTED)) LvSetState(l, i, 0, LVIS_SELECTED);
}

/// Scrolls the content by (dx, dy) pixels, in the limits of the content.
void LvScrollBy(W16List* l, int dx, int dy) {
  RECT rcView = LvViewArea(l);
  SIZE sizeContent = LvContentSize(l);
  int nMaxX = (int)MaxL(0, sizeContent.cx - (rcView.right - rcView.left));
  int nMaxY = (int)MaxL(0, sizeContent.cy - (rcView.bottom - rcView.top));
  int x = (int)ClampL(l->ptOrigin.x + dx, 0, nMaxX);
  int y = (int)ClampL(l->ptOrigin.y + dy, 0, nMaxY);
  if (x == l->ptOrigin.x && y == l->ptOrigin.y) return;
  l->ptOrigin.x = x;
  l->ptOrigin.y = y;
  LvChanged(l);
}

/// Scrolls the list so that an item is in the view.
void LvEnsureVisible(W16List* l, int i) {
  if (!LvIsItem(l, i)) return;
  RECT rcView = LvViewArea(l);
  RECT rc = LvToClient(l, LvItemCell(l, i));
  int dx = 0, dy = 0;
  if (rc.bottom > rcView.bottom) dy = rc.bottom - rcView.bottom;
  if (rc.top - dy < rcView.top) dy = rc.top - rcView.top;
  if (LvView(l) == LVS_LIST) {
    if (rc.right > rcView.right) dx = rc.right - rcView.right;
    if (rc.left - dx < rcView.left) dx = rc.left - rcView.left;
  }
  if (dx != 0 || dy != 0) LvScrollBy(l, dx, dy);
}

/// Gives the brush color of the background of the list.
COLORREF LvBkColor(W16List* l) {
  return l->clrBk == CLR_DEFAULT ? ::GetSysColor(COLOR_WINDOW) : l->clrBk;
}

/// Draws a check box (the state image of LVS_EX_CHECKBOXES).
void DrawCheckBox(HDC hdc, const RECT& rcArea, BOOL bChecked) {
  int x = rcArea.left + (rcArea.right - rcArea.left - kCheckSize) / 2;
  int y = rcArea.top + (rcArea.bottom - rcArea.top - kCheckSize) / 2;
  RECT rc = {x, y, x + kCheckSize, y + kCheckSize};
  FillColor(hdc, rc, ::GetSysColor(COLOR_WINDOW));
  ::DrawEdge(hdc, &rc, EDGE_SUNKEN, BF_RECT);
  if (!bChecked) return;
  COLORREF clr = ::GetSysColor(COLOR_WINDOWTEXT);
  for (int d = 0; d < 3; d++) {
    DrawLine(hdc, x + 3, y + 5 + d, x + 5, y + 7 + d, clr);
    DrawLine(hdc, x + 5, y + 7 + d, x + 10, y + 2 + d, clr);
  }
}

/// Sends NM_CUSTOMDRAW for a draw stage of an item. Gives the CDRF_
/// result of the parent. pclrText and pclrTextBk can change.
LRESULT LvCustomDraw(W16List* l, HDC hdc, DWORD dwStage, int i, int iSub, const RECT& rc,
                     COLORREF* pclrText, COLORREF* pclrTextBk) {
  NMLVCUSTOMDRAW nm;
  memset(&nm, 0, sizeof(nm));
  nm.nmcd.dwDrawStage = dwStage;
  nm.nmcd.hdc = hdc;
  nm.nmcd.rc = rc;
  nm.nmcd.dwItemSpec = (DWORD_PTR)i;
  if (LvIsItem(l, i)) {
    UINT state = LvGetState(l, i);
    if (state & LVIS_SELECTED) nm.nmcd.uItemState |= CDIS_SELECTED;
    if (state & LVIS_FOCUSED) nm.nmcd.uItemState |= CDIS_FOCUS;
    if (!LvOwnerData(l)) nm.nmcd.lItemlParam = LvItem(l, i).lParam;
  }
  nm.clrText = pclrText != NULL ? *pclrText : CLR_DEFAULT;
  nm.clrTextBk = pclrTextBk != NULL ? *pclrTextBk : CLR_DEFAULT;
  nm.iSubItem = iSub;
  LRESULT lResult = SendNotify(l->hwnd, NM_CUSTOMDRAW, &nm.nmcd.hdr);
  if (pclrText != NULL) *pclrText = nm.clrText;
  if (pclrTextBk != NULL) *pclrTextBk = nm.clrTextBk;
  return lResult;
}

/// Draws a text in a rectangle, in one line, clipped.
void LvDrawText(HDC hdc, const CString& str, RECT rc, UINT nAlign) {
  ::InflateRect(&rc, -2, 0);
  ::DrawText(hdc, str, str.GetLength(), &rc,
             nAlign | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

/// Draws one item: the state image or check box, the icon, the label
/// with the selection colors, the subitems in the report view, and the
/// focus rectangle.
void LvPaintItem(W16List* l, HDC hdc, int i, BOOL bCustomItems) {
  DWORD dwView = LvView(l);
  UINT state = LvGetState(l, i);
  RECT rcBounds, rcState, rcIcon, rcLabel;
  LvItemParts(l, i, &rcBounds, &rcState, &rcIcon, &rcLabel);
  BOOL bShowSel = l->bFocus || (StyleOf(l->hwnd) & LVS_SHOWSELALWAYS);
  BOOL bSelected = (state & LVIS_SELECTED) && bShowSel;
  BOOL bDropHilited = (state & LVIS_DROPHILITED) != 0;
  COLORREF clrText = l->clrText == CLR_DEFAULT ? ::GetSysColor(COLOR_WINDOWTEXT) : l->clrText;
  COLORREF clrTextBk = l->clrTextBk == CLR_DEFAULT ? CLR_NONE : l->clrTextBk;
  if (bSelected || bDropHilited) {
    BOOL bActive = l->bFocus || bDropHilited;
    clrText = ::GetSysColor(bActive ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
    clrTextBk = ::GetSysColor(bActive ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
  }
  LRESULT lCustom = CDRF_DODEFAULT;
  if (bCustomItems) {
    COLORREF clrT = clrText, clrB = clrTextBk;
    lCustom = LvCustomDraw(l, hdc, CDDS_ITEMPREPAINT, i, 0, rcBounds, &clrT, &clrB);
    if (lCustom & CDRF_SKIPDEFAULT) return;
    if (clrT != CLR_DEFAULT) clrText = clrT;
    if (clrB != CLR_DEFAULT) clrTextBk = clrB;
  }
  CString strText;
  int iImage = -1;
  LvGetDisplay(l, i, 0, &strText, &iImage);
  // The state image, or the check box.
  if (l->dwExStyle & LVS_EX_CHECKBOXES) {
    DrawCheckBox(hdc, rcState, ((state & LVIS_STATEIMAGEMASK) >> 12) == 2);
  } else if (l->himlState != NULL && (state & LVIS_STATEIMAGEMASK)) {
    SIZE size = ImageSize(l->himlState);
    ::ImageList_Draw(l->himlState, (int)((state & LVIS_STATEIMAGEMASK) >> 12) - 1, hdc,
                     rcState.left, rcState.top + (rcState.bottom - rcState.top - size.cy) / 2,
                     ILD_TRANSPARENT);
  }
  // The icon.
  HIMAGELIST himl = dwView == LVS_ICON ? l->himlNormal : l->himlSmall;
  if (himl != NULL && iImage >= 0) {
    SIZE size = ImageSize(himl);
    UINT fStyle = ILD_TRANSPARENT | (state & LVIS_OVERLAYMASK);
    if ((bSelected && l->bFocus) || bDropHilited) fStyle |= ILD_SELECTED;
    ::ImageList_Draw(himl, iImage, hdc, rcIcon.left,
                     rcIcon.top + (rcIcon.bottom - rcIcon.top - size.cy) / 2, fStyle);
  }
  // The label.
  BOOL bFullRow = dwView == LVS_REPORT && (l->dwExStyle & LVS_EX_FULLROWSELECT);
  RECT rcSel = rcLabel;
  if (bFullRow) {
    rcSel.right = rcBounds.right;
  } else if (dwView == LVS_REPORT) {
    int cxText = LvTextWidth(l, strText) + 4;
    if (rcSel.left + cxText < rcSel.right) rcSel.right = rcSel.left + cxText;
  }
  HGDIOBJ hOldFont = ::SelectObject(hdc, LvFont(l));
  int nOldMode = ::SetBkMode(hdc, TRANSPARENT);
  if (clrTextBk != CLR_NONE) FillColor(hdc, rcSel, clrTextBk);
  ::SetTextColor(hdc, clrText);
  if (dwView == LVS_ICON) {
    RECT rcText = rcLabel;
    ::DrawText(hdc, strText, strText.GetLength(), &rcText,
               DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
  } else {
    LvDrawText(hdc, strText, rcLabel, DT_LEFT);
  }
  // The subitems.
  if (dwView == LVS_REPORT) {
    for (int c = 1; c < LvColumnCount(l); c++) {
      RECT rcSub = LvSubItemRect(l, i, c);
      int iSub = l->colSub[c];
      COLORREF clrSubText = bFullRow ? clrText : (l->clrText == CLR_DEFAULT
                                                      ? ::GetSysColor(COLOR_WINDOWTEXT)
                                                      : l->clrText);
      if (lCustom & CDRF_NOTIFYSUBITEMDRAW) {
        COLORREF clrT = clrSubText, clrB = CLR_DEFAULT;
        LRESULT lSub = LvCustomDraw(l, hdc, CDDS_ITEMPREPAINT | CDDS_SUBITEM, i, iSub, rcSub,
                                    &clrT, &clrB);
        if (lSub & CDRF_SKIPDEFAULT) continue;
        if (clrT != CLR_DEFAULT) clrSubText = clrT;
        if (clrB != CLR_DEFAULT) FillColor(hdc, rcSub, clrB);
      }
      CString strSub;
      LvGetDisplay(l, i, iSub, &strSub, NULL);
      HDITEMA hdi;
      memset(&hdi, 0, sizeof(hdi));
      hdi.mask = HDI_FORMAT;
      ::SendMessage(l->hwndHeader, HDM_GETITEMA, c, (LPARAM)(HDITEMA FAR*)&hdi);
      UINT nAlign = (hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT    ? DT_RIGHT
                    : (hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER
                                                                : DT_LEFT;
      ::SetTextColor(hdc, clrSubText);
      LvDrawText(hdc, strSub, rcSub, nAlign);
    }
  }
  ::SetBkMode(hdc, nOldMode);
  ::SelectObject(hdc, hOldFont);
  if ((state & LVIS_FOCUSED) && l->bFocus) ::DrawFocusRect(hdc, &rcSel);
  if (lCustom & CDRF_NOTIFYPOSTPAINT)
    LvCustomDraw(l, hdc, CDDS_ITEMPOSTPAINT, i, 0, rcBounds, NULL, NULL);
}

/// Draws a list: the background, the items in the update rectangle and
/// the grid lines.
void PaintList(W16List* l, HDC hdc, const RECT& rcPaint) {
  if (l->dwLastStyle != StyleOf(l->hwnd)) LvLayout(l);
  RECT rcClient;
  ::GetClientRect(l->hwnd, &rcClient);
  RECT rcView = LvViewArea(l);
  LRESULT lPre = LvCustomDraw(l, hdc, CDDS_PREPAINT, 0, 0, rcClient, NULL, NULL);
  FillColor(hdc, rcClient, LvBkColor(l));
  if (lPre & CDRF_SKIPDEFAULT) return;
  BOOL bCustomItems = (lPre & CDRF_NOTIFYITEMDRAW) != 0;
  HRGN hrgn = ::CreateRectRgn(rcView.left, rcView.top, rcView.right, rcView.bottom);
  ::SelectClipRgn(hdc, hrgn);
  int n = LvCount(l);
  int iFirst = 0, iLast = n - 1;
  if (LvView(l) == LVS_REPORT) {
    int cy = LvRowHeight(l);
    iFirst = (int)MaxL(0, (rcPaint.top - rcView.top + l->ptOrigin.y) / cy);
    iLast = (int)MinL(n - 1, (rcPaint.bottom - rcView.top + l->ptOrigin.y) / cy);
  }
  for (int i = iFirst; i <= iLast; i++) {
    RECT rc = LvToClient(l, LvItemCell(l, i));
    RECT rcTest;
    if (LvView(l) == LVS_REPORT) {
      rc.left = rcClient.left;
      rc.right = rcClient.right;
    }
    if (::IntersectRect(&rcTest, &rc, &rcPaint)) LvPaintItem(l, hdc, i, bCustomItems);
  }
  if (LvView(l) == LVS_REPORT && (l->dwExStyle & LVS_EX_GRIDLINES)) {
    COLORREF clr = ::GetSysColor(COLOR_BTNFACE);
    int cy = LvRowHeight(l);
    for (int y = rcView.top - l->ptOrigin.y % cy + cy - 1; y < rcView.bottom; y += cy)
      DrawLine(hdc, rcView.left, y, rcView.right, y, clr);
    for (int c = 0; c < LvColumnCount(l); c++) {
      int x, cx;
      LvColumnRect(l, c, &x, &cx);
      x += cx - 1 - l->ptOrigin.x;
      DrawLine(hdc, x, rcView.top, x, rcView.bottom, clr);
    }
  }
  ::SelectClipRgn(hdc, NULL);
  ::DeleteObject(hrgn);
  if (lPre & CDRF_NOTIFYPOSTPAINT) LvCustomDraw(l, hdc, CDDS_POSTPAINT, 0, 0, rcClient, NULL, NULL);
}

/// Copies the fields of an LVITEMA that its mask names into an item.
void LvSetItemFields(W16List* l, int i, const LVITEMA FAR* p) {
  if (LvOwnerData(l)) {
    if (p->mask & LVIF_STATE) LvSetState(l, i, p->state, p->stateMask);
    return;
  }
  W16LvItem& item = LvItem(l, i);
  if (p->iSubItem > 0) {
    if (p->mask & LVIF_TEXT) {
      if (item.subText.GetSize() < p->iSubItem) item.subText.SetSize(p->iSubItem);
      item.subText[p->iSubItem - 1] = p->pszText == LPSTR_TEXTCALLBACKA
                                          ? CString((LPCSTR)"\x01" "callback")
                                          : CString(p->pszText != NULL ? p->pszText : "");
    }
    LvInvalidateItem(l, i);
    return;
  }
  if (p->mask & LVIF_TEXT) {
    item.bTextCallback = p->pszText == LPSTR_TEXTCALLBACKA;
    item.strText = item.bTextCallback || p->pszText == NULL ? "" : p->pszText;
  }
  if (p->mask & LVIF_IMAGE) item.iImage = p->iImage;
  if (p->mask & LVIF_PARAM) item.lParam = p->lParam;
  if (p->mask & LVIF_INDENT) item.iIndent = p->iIndent;
  if (p->mask & LVIF_STATE) LvSetState(l, i, p->state, p->stateMask);
  LvInvalidateItem(l, i);
}

/// Fills an LVITEMA with the fields of an item that its mask names.
void LvGetItemFields(W16List* l, LVITEMA FAR* p) {
  int i = p->iItem;
  if (p->mask & LVIF_TEXT) {
    CString strText;
    LvGetDisplay(l, i, p->iSubItem, &strText, NULL);
    if (p->pszText != NULL && p->cchTextMax > 0) lstrcpyn(p->pszText, strText, p->cchTextMax);
  }
  if (p->mask & LVIF_IMAGE) {
    int iImage = -1;
    if (p->iSubItem == 0) LvGetDisplay(l, i, 0, NULL, &iImage);
    p->iImage = iImage;
  }
  if (p->mask & LVIF_STATE) p->state = LvGetState(l, i) & p->stateMask;
  if (!LvOwnerData(l)) {
    if (p->mask & LVIF_PARAM) p->lParam = LvItem(l, i).lParam;
    if (p->mask & LVIF_INDENT) p->iIndent = LvItem(l, i).iIndent;
  }
}

/// Removes the item i and sends LVN_DELETEITEM.
void LvDeleteItem(W16List* l, int i, BOOL bNotify) {
  if (bNotify) {
    NMLISTVIEW nm;
    memset(&nm, 0, sizeof(nm));
    nm.iItem = i;
    if (!LvOwnerData(l)) nm.lParam = LvItem(l, i).lParam;
    SendNotify(l->hwnd, LVN_DELETEITEM, &nm.hdr);
  }
  if (LvOwnerData(l)) {
    if (i < l->ownerState.GetSize()) l->ownerState.RemoveAt(i);
    l->nOwnerCount--;
  } else {
    delete &LvItem(l, i);
    l->items.RemoveAt(i);
  }
  if (l->iFocus == i)
    l->iFocus = -1;
  else if (l->iFocus > i)
    l->iFocus--;
  if (l->iMark >= LvCount(l)) l->iMark = LvCount(l) - 1;
}

// The data of LVM_SORTITEMS, for the comparison of two items.
struct LvSortData {
  W16List* l;
  PFNLVCOMPARE pfnCompare;
  LPARAM lParamSort;
  BOOL bIndexes;  // LVM_SORTITEMSEX: the callback gets the indexes
};

/// Compares two items with the callback of the caller.
int LvCompare(LvSortData* pData, int a, int b, W16LvItem* pa, W16LvItem* pb) {
  if (pData->bIndexes) return pData->pfnCompare((LPARAM)a, (LPARAM)b, pData->lParamSort);
  return pData->pfnCompare(pa->lParam, pb->lParam, pData->lParamSort);
}

/// Sorts the items with the callback of the caller (an insertion sort,
/// which keeps the order of equal items). The selection states move with
/// the items.
void LvSort(W16List* l, PFNLVCOMPARE pfnCompare, LPARAM lParamSort, BOOL bIndexes) {
  if (LvOwnerData(l) || pfnCompare == NULL) return;
  LvSortData data = {l, pfnCompare, lParamSort, bIndexes};
  int n = (int)l->items.GetSize();
  // For LVM_SORTITEMSEX the callback gets the indexes of the items as
  // they were before the sort.
  CWordArray origIndex;
  origIndex.SetSize(n);
  for (int i = 0; i < n; i++) origIndex[i] = (WORD)i;
  for (int i = 1; i < n; i++) {
    void* p = l->items[i];
    WORD w = origIndex[i];
    int j = i - 1;
    while (j >= 0 && LvCompare(&data, origIndex[j], w, (W16LvItem*)l->items[j], (W16LvItem*)p) > 0) {
      l->items[j + 1] = l->items[j];
      origIndex[j + 1] = origIndex[j];
      j--;
    }
    l->items[j + 1] = p;
    origIndex[j + 1] = w;
  }
  l->iFocus = -1;
  for (int i = 0; i < n; i++)
    if (LvItem(l, i).state & LVIS_FOCUSED) l->iFocus = i;
  LvChanged(l);
}

/// Finds an item as LVM_FINDITEMA does: by lParam, or by text (all of it
/// or the start, without case), from the item after iStart.
int LvFind(W16List* l, int iStart, const LVFINDINFOA FAR* p) {
  if (p == NULL) return -1;
  int n = LvCount(l);
  if (LvOwnerData(l)) {
    NMLVFINDITEMA nm;
    memset(&nm, 0, sizeof(nm));
    nm.iStart = iStart + 1;
    nm.lvfi = *p;
    return (int)SendNotify(l->hwnd, LVN_ODFINDITEMA, &nm.hdr);
  }
  for (int k = 0; k < n; k++) {
    int i = iStart + 1 + k;
    if (i >= n) {
      if (!(p->flags & LVFI_WRAP)) break;
      i -= n;
    }
    if (p->flags & LVFI_PARAM) {
      if (LvItem(l, i).lParam == p->lParam) return i;
      continue;
    }
    if ((p->flags & (LVFI_STRING | LVFI_PARTIAL)) && p->psz != NULL) {
      CString strText;
      LvGetDisplay(l, i, 0, &strText, NULL);
      if (p->flags & LVFI_PARTIAL) {
        int nLen = lstrlen(p->psz);
        if (strText.GetLength() >= nLen && strText.Left(nLen).CompareNoCase(p->psz) == 0) return i;
      } else if (strText.CompareNoCase(p->psz) == 0) {
        return i;
      }
    }
  }
  return -1;
}

/// Gives the next item as LVM_GETNEXTITEM does.
int LvGetNext(W16List* l, int iStart, UINT flags) {
  int n = LvCount(l);
  UINT stateWanted = 0;
  if (flags & LVNI_SELECTED) stateWanted |= LVIS_SELECTED;
  if (flags & LVNI_FOCUSED) stateWanted |= LVIS_FOCUSED;
  if (flags & LVNI_CUT) stateWanted |= LVIS_CUT;
  if (flags & LVNI_DROPHILITED) stateWanted |= LVIS_DROPHILITED;
  int nStep = 1;
  if (flags & (LVNI_ABOVE | LVNI_TOLEFT)) nStep = -1;
  if (LvView(l) == LVS_ICON || LvView(l) == LVS_SMALLICON) {
    if (flags & LVNI_ABOVE) nStep = -LvPerLine(l);
    if (flags & LVNI_BELOW) nStep = LvPerLine(l);
  }
  for (int i = iStart + nStep; i >= 0 && i < n; i += nStep) {
    if ((LvGetState(l, i) & stateWanted) == stateWanted) return i;
  }
  if (iStart == -1 && nStep < 0) return -1;
  return -1;
}

/// Gives the item that a key moves the focus to, or -1.
int LvKeyTarget(W16List* l, UINT nKey) {
  int n = LvCount(l);
  if (n == 0) return -1;
  int i = l->iFocus >= 0 ? l->iFocus : 0;
  DWORD dwView = LvView(l);
  int nLine = LvPerLine(l);
  RECT rcView = LvViewArea(l);
  int nPage = dwView == LVS_REPORT ? (int)MaxL(1, (rcView.bottom - rcView.top) / LvRowHeight(l) - 1)
                                   : nLine;
  switch (nKey) {
    case VK_UP:
      i -= (dwView == LVS_ICON || dwView == LVS_SMALLICON) ? nLine : 1;
      break;
    case VK_DOWN:
      i += (dwView == LVS_ICON || dwView == LVS_SMALLICON) ? nLine : 1;
      break;
    case VK_LEFT:
      if (dwView == LVS_REPORT) return -1;
      i -= dwView == LVS_LIST ? nLine : 1;
      break;
    case VK_RIGHT:
      if (dwView == LVS_REPORT) return -1;
      i += dwView == LVS_LIST ? nLine : 1;
      break;
    case VK_PRIOR:
      i -= nPage;
      break;
    case VK_NEXT:
      i += nPage;
      break;
    case VK_HOME:
      i = 0;
      break;
    case VK_END:
      i = n - 1;
      break;
    default:
      return -1;
  }
  return (int)ClampL(i, 0, n - 1);
}

/// Selects the items from the selection mark to i, and only those.
void LvSelectRange(W16List* l, int i) {
  int iMark = l->iMark >= 0 ? l->iMark : 0;
  int a = iMark < i ? iMark : i, b = iMark < i ? i : iMark;
  int n = LvCount(l);
  for (int k = 0; k < n; k++) {
    BOOL bIn = k >= a && k <= b;
    if (bIn != ((LvGetState(l, k) & LVIS_SELECTED) != 0))
      LvSetState(l, k, bIn ? LVIS_SELECTED : 0, LVIS_SELECTED);
  }
}

/// Moves the focus to an item, as a click or a key does: with no
/// modifier the item is the only selected item, with Ctrl the selection
/// stays, and with Shift the items from the mark are selected.
void LvMoveTo(W16List* l, int i, BOOL bShift, BOOL bCtrl, BOOL bToggle) {
  if (!LvIsItem(l, i)) return;
  BOOL bSingle = (StyleOf(l->hwnd) & LVS_SINGLESEL) != 0;
  if (bShift && !bSingle) {
    LvSelectRange(l, i);
    LvSetState(l, i, LVIS_FOCUSED, LVIS_FOCUSED);
  } else if (bCtrl && !bSingle) {
    if (bToggle) {
      UINT state = LvGetState(l, i);
      LvSetState(l, i, (state & LVIS_SELECTED) ? LVIS_FOCUSED : LVIS_SELECTED | LVIS_FOCUSED,
                 LVIS_SELECTED | LVIS_FOCUSED);
    } else {
      LvSetState(l, i, LVIS_FOCUSED, LVIS_FOCUSED);
    }
    l->iMark = i;
  } else {
    LvClearSelection(l, i);
    LvSetState(l, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    l->iMark = i;
  }
  LvEnsureVisible(l, i);
}

/// Sends a notification with an NMITEMACTIVATE for an item and a point.
LRESULT LvNotifyActivate(W16List* l, UINT nCode, int i, int iSub, POINT pt) {
  NMITEMACTIVATE nm;
  memset(&nm, 0, sizeof(nm));
  nm.iItem = i;
  nm.iSubItem = iSub;
  nm.ptAction = pt;
  if (::GetKeyState(VK_SHIFT) < 0) nm.uKeyFlags |= LVKF_SHIFT;
  if (::GetKeyState(VK_CONTROL) < 0) nm.uKeyFlags |= LVKF_CONTROL;
  if (LvIsItem(l, i) && !LvOwnerData(l)) nm.lParam = LvItem(l, i).lParam;
  return SendNotify(l->hwnd, nCode, &nm.hdr);
}

/// Toggles the check box of an item (LVS_EX_CHECKBOXES).
void LvToggleCheck(W16List* l, int i) {
  UINT state = LvGetState(l, i);
  UINT nCheck = ((state & LVIS_STATEIMAGEMASK) >> 12) == 2 ? 1 : 2;
  LvSetState(l, i, INDEXTOSTATEIMAGEMASK(nCheck), LVIS_STATEIMAGEMASK);
}

LRESULT CALLBACK LvEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);

/// Ends a label edit. With bSave, the parent gets the new text in
/// LVN_ENDLABELEDITA and the item takes it when the parent gives TRUE.
void LvEndEdit(W16List* l, BOOL bSave) {
  if (l->hwndEdit == NULL) return;
  HWND hwndEdit = l->hwndEdit;
  int i = l->iEdit;
  l->hwndEdit = NULL;
  l->iEdit = -1;
  char szText[260];
  ::GetWindowText(hwndEdit, szText, sizeof(szText));
  NMLVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.item.iItem = i;
  nm.item.mask = LVIF_TEXT;
  nm.item.pszText = bSave ? szText : NULL;
  nm.item.cchTextMax = sizeof(szText);
  if (LvIsItem(l, i) && !LvOwnerData(l)) nm.item.lParam = LvItem(l, i).lParam;
  LRESULT lResult = SendNotify(l->hwnd, LVN_ENDLABELEDITA, &nm.hdr);
  ::DestroyWindow(hwndEdit);
  if (bSave && lResult != 0 && LvIsItem(l, i) && !LvOwnerData(l)) {
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.pszText = szText;
    LvSetItemFields(l, i, &item);
    LvChanged(l);
  }
}

/// Starts a label edit of an item: the parent gets LVN_BEGINLABELEDITA,
/// which can refuse it, then an edit control covers the label. Gives the
/// edit control, or NULL.
HWND LvBeginEdit(W16List* l, int i) {
  if (!LvIsItem(l, i)) return NULL;
  LvEndEdit(l, TRUE);
  LvEnsureVisible(l, i);
  NMLVDISPINFOA nm;
  memset(&nm, 0, sizeof(nm));
  nm.item.iItem = i;
  nm.item.mask = LVIF_TEXT;
  if (!LvOwnerData(l)) nm.item.lParam = LvItem(l, i).lParam;
  if (SendNotify(l->hwnd, LVN_BEGINLABELEDITA, &nm.hdr) != 0) return NULL;
  RECT rcLabel;
  LvItemParts(l, i, NULL, NULL, NULL, &rcLabel);
  CString strText;
  LvGetDisplay(l, i, 0, &strText, NULL);
  int cx = LvTextWidth(l, strText) + 16;
  if (cx < 60) cx = 60;
  int cy = FontHeight(l->hwnd, l->hFont) + 4;
  HWND hwndEdit = ::CreateWindow("EDIT", strText,
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rcLabel.left,
                                 rcLabel.top, cx, cy, l->hwnd, (HMENU)1,
                                 (HINSTANCE)::GetWindowWord(l->hwnd, GWW_HINSTANCE), NULL);
  if (hwndEdit == NULL) return NULL;
  ::SendMessage(hwndEdit, WM_SETFONT, (WPARAM)LvFont(l), 0);
  ::SendMessage(hwndEdit, EM_SETSEL, 0, MAKELPARAM(0, -1));
  l->hwndEdit = hwndEdit;
  l->iEdit = i;
  l->pfnEditProc = (WNDPROC)::SetWindowLong(hwndEdit, GWL_WNDPROC, (LONG)(WNDPROC)LvEditProc);
  ::SetFocus(hwndEdit);
  return hwndEdit;
}

/// The procedure of the edit control of a label edit: Enter saves the
/// text, Esc cancels, and the loss of the focus saves.
LRESULT CALLBACK LvEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  HWND hwndList = ::GetParent(hWnd);
  W16List* l = StateOf<W16List>(hwndList);
  WNDPROC pfnOld = l != NULL ? l->pfnEditProc : NULL;
  if (pfnOld == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
    case WM_KEYDOWN:
      if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
        LvEndEdit(l, wParam == VK_RETURN);
        ::SetFocus(hwndList);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wParam == '\r' || wParam == 27) return 0;
      break;
    case WM_KILLFOCUS:
      if (l->hwndEdit == hWnd) {
        LRESULT lResult = ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
        LvEndEdit(l, TRUE);
        return lResult;
      }
      break;
  }
  return ::CallWindowProc(pfnOld, hWnd, nMsg, wParam, lParam);
}

/// Makes the header of the report view.
void LvMakeHeader(W16List* l) {
  if (l->hwndHeader != NULL) return;
  DWORD dwStyle = WS_CHILD | HDS_HORZ;
  if (!(StyleOf(l->hwnd) & LVS_NOSORTHEADER)) dwStyle |= HDS_BUTTONS;
  l->hwndHeader = ::CreateWindow(WC_HEADER, "", dwStyle, 0, 0, 0, 0, l->hwnd, (HMENU)0,
                                 (HINSTANCE)::GetWindowWord(l->hwnd, GWW_HINSTANCE), NULL);
  if (l->hwndHeader != NULL && l->hFont != NULL)
    ::SendMessage(l->hwndHeader, WM_SETFONT, (WPARAM)l->hFont, 0);
}

/// Fills an HDITEMA with the fields of an LVCOLUMNA that its mask names.
void ColumnToHeader(const LVCOLUMNA FAR* p, HDITEMA* pItem) {
  memset(pItem, 0, sizeof(*pItem));
  if (p->mask & LVCF_WIDTH) {
    pItem->mask |= HDI_WIDTH;
    pItem->cxy = p->cx;
  }
  if (p->mask & LVCF_TEXT) {
    pItem->mask |= HDI_TEXT;
    pItem->pszText = p->pszText;
  }
  if (p->mask & LVCF_FMT) {
    pItem->mask |= HDI_FORMAT;
    pItem->fmt = HDF_STRING | ((p->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT    ? HDF_RIGHT
                               : (p->fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER ? HDF_CENTER
                                                                                : HDF_LEFT);
    if (p->fmt & LVCFMT_IMAGE) pItem->fmt |= HDF_IMAGE;
    if (p->fmt & LVCFMT_BITMAP_ON_RIGHT) pItem->fmt |= HDF_BITMAP_ON_RIGHT;
  }
  if (p->mask & LVCF_IMAGE) {
    pItem->mask |= HDI_IMAGE;
    pItem->iImage = p->iImage;
  }
  if (p->mask & LVCF_ORDER) {
    pItem->mask |= HDI_ORDER;
    pItem->iOrder = p->iOrder;
  }
}

/// Gives the width that LVSCW_AUTOSIZE gives a column: the widest text,
/// or with LVSCW_AUTOSIZE_USEHEADER also the header text.
int LvAutoWidth(W16List* l, int iCol, BOOL bHeader) {
  int cx = 0;
  int iSub = iCol < l->colSub.GetSize() ? l->colSub[iCol] : iCol;
  int n = LvCount(l);
  if (n > 500) n = 500;
  for (int i = 0; i < n; i++) {
    CString strText;
    LvGetDisplay(l, i, iSub, &strText, NULL);
    int w = LvTextWidth(l, strText) + 8;
    if (iCol == 0) {
      RECT rcLabel, rcBounds;
      LvItemParts(l, i, &rcBounds, NULL, NULL, &rcLabel);
      int xCol, cxCol;
      LvColumnRect(l, 0, &xCol, &cxCol);
      w += rcLabel.left - (rcBounds.left + xCol);
    }
    if (w > cx) cx = w;
  }
  if (bHeader && l->hwndHeader != NULL) {
    char szText[128];
    HDITEMA hdi;
    memset(&hdi, 0, sizeof(hdi));
    hdi.mask = HDI_TEXT;
    hdi.pszText = szText;
    hdi.cchTextMax = sizeof(szText);
    szText[0] = 0;
    ::SendMessage(l->hwndHeader, HDM_GETITEMA, iCol, (LPARAM)(HDITEMA FAR*)&hdi);
    int w = LvTextWidth(l, szText) + 16;
    if (w > cx) cx = w;
  }
  return cx;
}

/// Does the mouse, the keys and the LVM_ messages of a list view.
LRESULT ListMessage(W16List* l, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_CREATE:
      LvMakeHeader(l);
      LvLayout(l);
      return 0;
    case WM_DESTROY:
      LvEndEdit(l, FALSE);
      if (!(StyleOf(hWnd) & LVS_SHAREIMAGELISTS)) {
        if (l->himlNormal != NULL) ::ImageList_Destroy(l->himlNormal);
        if (l->himlSmall != NULL) ::ImageList_Destroy(l->himlSmall);
        if (l->himlState != NULL) ::ImageList_Destroy(l->himlState);
        l->himlNormal = l->himlSmall = l->himlState = NULL;
      }
      {
        int n = LvCount(l);
        NMLISTVIEW nm;
        memset(&nm, 0, sizeof(nm));
        nm.iItem = -1;
        if (n > 0 && SendNotify(hWnd, LVN_DELETEALLITEMS, &nm.hdr) == 0 && !LvOwnerData(l))
          for (int i = 0; i < n; i++) {
            memset(&nm, 0, sizeof(nm));
            nm.iItem = i;
            nm.lParam = LvItem(l, i).lParam;
            SendNotify(hWnd, LVN_DELETEITEM, &nm.hdr);
          }
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      PaintList(l, hdc, ps.rcPaint);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      LvChanged(l);
      return 0;
    case WM_SETFONT:
      l->hFont = (HFONT)wParam;
      if (l->hwndHeader != NULL) ::SendMessage(l->hwndHeader, WM_SETFONT, wParam, lParam);
      LvLayout(l);
      if (LOWORD(lParam)) ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)l->hFont;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
      l->bFocus = nMsg == WM_SETFOCUS;
      ::InvalidateRect(hWnd, NULL, TRUE);
      NMHDR nm;
      SendNotify(hWnd, l->bFocus ? NM_SETFOCUS : NM_KILLFOCUS, &nm);
      return 0;
    }
    case WM_GETDLGCODE:
      return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_HSCROLL:
    case WM_VSCROLL: {
      BOOL bVert = nMsg == WM_VSCROLL;
      RECT rcView = LvViewArea(l);
      int nLine = LvView(l) == LVS_REPORT && bVert ? LvRowHeight(l)
                  : LvView(l) == LVS_LIST && !bVert ? l->nColumnWidth
                  : bVert ? LvCellSize(l).cy : 16;
      int nPage = bVert ? rcView.bottom - rcView.top : rcView.right - rcView.left;
      int nPos = bVert ? l->ptOrigin.y : l->ptOrigin.x;
      int nNew = nPos;
      switch (wParam) {
        case SB_LINEUP:
          nNew -= nLine;
          break;
        case SB_LINEDOWN:
          nNew += nLine;
          break;
        case SB_PAGEUP:
          nNew -= nPage;
          break;
        case SB_PAGEDOWN:
          nNew += nPage;
          break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
          nNew = (short)LOWORD(lParam);
          break;
        case SB_TOP:
          nNew = 0;
          break;
        case SB_BOTTOM:
          nNew = 0x7FFF;
          break;
        default:
          return 0;
      }
      if (LvView(l) == LVS_REPORT && bVert) nNew = nNew / nLine * nLine;
      if (bVert)
        LvScrollBy(l, 0, nNew - nPos);
      else
        LvScrollBy(l, nNew - nPos, 0);
      return 0;
    }
    case WM_NOTIFY: {
      NMHDR FAR* pHdr = (NMHDR FAR*)lParam;
      if (pHdr == NULL || pHdr->hwndFrom != l->hwndHeader) break;
      NMHEADERA FAR* pnm = (NMHEADERA FAR*)lParam;
      if (pHdr->code == HDN_ITEMCLICKA) {
        NMLISTVIEW nm;
        memset(&nm, 0, sizeof(nm));
        nm.iItem = -1;
        nm.iSubItem = pnm->iItem < l->colSub.GetSize() ? l->colSub[pnm->iItem] : pnm->iItem;
        SendNotify(hWnd, LVN_COLUMNCLICK, &nm.hdr);
      } else if (pHdr->code == HDN_ITEMCHANGEDA || pHdr->code == HDN_TRACKA) {
        LvChanged(l);
      } else if (pHdr->code == HDN_DIVIDERDBLCLICKA) {
        int cx = LvAutoWidth(l, pnm->iItem, TRUE);
        HDITEMA hdi;
        memset(&hdi, 0, sizeof(hdi));
        hdi.mask = HDI_WIDTH;
        hdi.cxy = cx;
        ::SendMessage(l->hwndHeader, HDM_SETITEMA, pnm->iItem, (LPARAM)(HDITEMA FAR*)&hdi);
        LvChanged(l);
      }
      // The parent also gets the notifications of the header.
      return ::SendMessage(::GetParent(hWnd), WM_NOTIFY, wParam, lParam);
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      LvEndEdit(l, TRUE);
      ::KillTimer(hWnd, kEditTimer);
      if (::GetFocus() != hWnd) ::SetFocus(hWnd);
      UINT nFlags;
      int iSub;
      int i = LvHitTest(l, pt, &nFlags, &iSub);
      BOOL bShift = (wParam & MK_SHIFT) != 0, bCtrl = (wParam & MK_CONTROL) != 0;
      if (i < 0) {
        if (!bShift && !bCtrl) LvClearSelection(l, -1);
        if (nMsg == WM_LBUTTONDOWN)
          LvNotifyActivate(l, NM_CLICK, -1, 0, pt);
        else
          LvNotifyActivate(l, NM_RCLICK, -1, 0, pt);
        return 0;
      }
      if (nMsg == WM_LBUTTONDOWN && nFlags == LVHT_ONITEMSTATEICON &&
          (l->dwExStyle & LVS_EX_CHECKBOXES)) {
        LvToggleCheck(l, i);
        return 0;
      }
      UINT state = LvGetState(l, i);
      l->bWasSelected = (state & LVIS_SELECTED) && (state & LVIS_FOCUSED) && !bShift && !bCtrl;
      if (nMsg == WM_RBUTTONDOWN && (state & LVIS_SELECTED))
        LvSetState(l, i, LVIS_FOCUSED, LVIS_FOCUSED);
      else if (!((state & LVIS_SELECTED) && !bShift && !bCtrl))
        LvMoveTo(l, i, bShift, bCtrl, TRUE);
      l->bDragPending = TRUE;
      l->bRightDrag = nMsg == WM_RBUTTONDOWN;
      l->ptDown = pt;
      l->iDown = i;
      ::SetCapture(hWnd);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (l->bDragPending && ::GetCapture() == hWnd) {
        POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
        if (pt.x - l->ptDown.x > 4 || l->ptDown.x - pt.x > 4 || pt.y - l->ptDown.y > 4 ||
            l->ptDown.y - pt.y > 4) {
          l->bDragPending = FALSE;
          ::ReleaseCapture();
          NMLISTVIEW nm;
          memset(&nm, 0, sizeof(nm));
          nm.iItem = l->iDown;
          nm.ptAction = l->ptDown;
          if (!LvOwnerData(l) && LvIsItem(l, l->iDown)) nm.lParam = LvItem(l, l->iDown).lParam;
          SendNotify(hWnd, l->bRightDrag ? LVN_BEGINRDRAG : LVN_BEGINDRAG, &nm.hdr);
        }
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      BOOL bPending = l->bDragPending;
      l->bDragPending = FALSE;
      if (::GetCapture() == hWnd) ::ReleaseCapture();
      if (!bPending) return 0;
      UINT nFlags;
      int iSub;
      int i = LvHitTest(l, pt, &nFlags, &iSub);
      BOOL bShift = (wParam & MK_SHIFT) != 0, bCtrl = (wParam & MK_CONTROL) != 0;
      if (i == l->iDown && i >= 0 && !bShift && !bCtrl && nMsg == WM_LBUTTONUP)
        LvMoveTo(l, i, FALSE, FALSE, FALSE);
      LvNotifyActivate(l, nMsg == WM_LBUTTONUP ? NM_CLICK : NM_RCLICK, i, iSub, pt);
      if (nMsg == WM_LBUTTONUP && i >= 0 && i == l->iDown && l->bWasSelected &&
          nFlags == LVHT_ONITEMLABEL && (StyleOf(hWnd) & LVS_EDITLABELS))
        ::SetTimer(hWnd, kEditTimer, ::GetDoubleClickTime(), NULL);
      return 0;
    }
    case WM_TIMER:
      if (wParam == kEditTimer) {
        ::KillTimer(hWnd, kEditTimer);
        if (LvIsItem(l, l->iFocus)) LvBeginEdit(l, l->iFocus);
      }
      return 0;
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK: {
      ::KillTimer(hWnd, kEditTimer);
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      UINT nFlags;
      int iSub;
      int i = LvHitTest(l, pt, &nFlags, &iSub);
      if (nMsg == WM_LBUTTONDBLCLK && i >= 0 && nFlags == LVHT_ONITEMSTATEICON &&
          (l->dwExStyle & LVS_EX_CHECKBOXES)) {
        LvToggleCheck(l, i);
        return 0;
      }
      LvNotifyActivate(l, nMsg == WM_LBUTTONDBLCLK ? NM_DBLCLK : NM_RDBLCLK, i, iSub, pt);
      if (nMsg == WM_LBUTTONDBLCLK && i >= 0) LvNotifyActivate(l, LVN_ITEMACTIVATE, i, iSub, pt);
      return 0;
    }
    case WM_KEYDOWN: {
      NMLVKEYDOWN nm;
      memset(&nm, 0, sizeof(nm));
      nm.wVKey = (WORD)wParam;
      nm.flags = HIWORD(lParam);
      SendNotify(hWnd, LVN_KEYDOWN, &nm.hdr);
      BOOL bShift = ::GetKeyState(VK_SHIFT) < 0, bCtrl = ::GetKeyState(VK_CONTROL) < 0;
      if (wParam == VK_F2) {
        if ((StyleOf(hWnd) & LVS_EDITLABELS) && LvIsItem(l, l->iFocus)) LvBeginEdit(l, l->iFocus);
        return 0;
      }
      if (wParam == VK_RETURN) {
        NMHDR nmhdr;
        SendNotify(hWnd, NM_RETURN, &nmhdr);
        POINT pt = {0, 0};
        if (LvIsItem(l, l->iFocus)) LvNotifyActivate(l, LVN_ITEMACTIVATE, l->iFocus, 0, pt);
        return 0;
      }
      if (wParam == VK_SPACE) {
        if (!LvIsItem(l, l->iFocus)) return 0;
        if (l->dwExStyle & LVS_EX_CHECKBOXES)
          LvToggleCheck(l, l->iFocus);
        else
          LvMoveTo(l, l->iFocus, FALSE, bCtrl, TRUE);
        return 0;
      }
      if (LvView(l) == LVS_REPORT && (wParam == VK_LEFT || wParam == VK_RIGHT)) {
        LvScrollBy(l, wParam == VK_LEFT ? -16 : 16, 0);
        return 0;
      }
      int i = LvKeyTarget(l, (UINT)wParam);
      if (i >= 0) LvMoveTo(l, i, bShift, bCtrl, FALSE);
      return 0;
    }
    case WM_CHAR: {
      // Typing the first letter of an item moves to it.
      if (wParam < ' ') return 0;
      char szPrefix[2] = {(char)wParam, 0};
      LVFINDINFOA fi;
      memset(&fi, 0, sizeof(fi));
      fi.flags = LVFI_PARTIAL | LVFI_WRAP;
      fi.psz = szPrefix;
      int i = LvFind(l, l->iFocus, &fi);
      if (i >= 0) LvMoveTo(l, i, FALSE, FALSE, FALSE);
      return 0;
    }
    case LVM_GETITEMCOUNT:
      return LvCount(l);
    case LVM_SETITEMCOUNT:
      if (LvOwnerData(l)) {
        l->nOwnerCount = (int)wParam;
        if (l->ownerState.GetSize() > l->nOwnerCount) l->ownerState.SetSize(l->nOwnerCount);
        if (l->iFocus >= l->nOwnerCount) l->iFocus = -1;
        LvChanged(l);
      }
      return TRUE;
    case LVM_INSERTITEMA: {
      const LVITEMA FAR* p = (const LVITEMA FAR*)lParam;
      if (p == NULL || p->iSubItem != 0 || LvOwnerData(l)) return -1;
      int i = p->iItem;
      int n = LvCount(l);
      if (i < 0 || i > n) i = n;
      if (StyleOf(hWnd) & (LVS_SORTASCENDING | LVS_SORTDESCENDING)) {
        // A sorted list puts the item in the order of its text.
        BOOL bDesc = (StyleOf(hWnd) & LVS_SORTDESCENDING) != 0;
        CString strNew = (p->mask & LVIF_TEXT) && p->pszText != NULL &&
                                 p->pszText != LPSTR_TEXTCALLBACKA
                             ? CString(p->pszText)
                             : CString();
        for (i = 0; i < n; i++) {
          int nCmp = LvItem(l, i).strText.CompareNoCase(strNew);
          if (bDesc ? nCmp < 0 : nCmp > 0) break;
        }
      }
      W16LvItem* pItem = new W16LvItem;
      pItem->bTextCallback = FALSE;
      pItem->iImage = -1;
      pItem->state = 0;
      pItem->lParam = 0;
      pItem->iIndent = 0;
      l->items.InsertAt(i, pItem);
      if (l->iFocus >= i) l->iFocus++;
      if (l->iMark >= i) l->iMark++;
      LVITEMA item = *p;
      item.iItem = i;
      UINT state = (p->mask & LVIF_STATE) ? (p->state & p->stateMask) : 0;
      item.mask &= ~LVIF_STATE;
      LvSetItemFields(l, i, &item);
      if (l->dwExStyle & LVS_EX_CHECKBOXES) pItem->state = INDEXTOSTATEIMAGEMASK(1);
      pItem->state |= state & ~LVIS_FOCUSED;
      if (state & LVIS_FOCUSED) LvSetState(l, i, LVIS_FOCUSED, LVIS_FOCUSED);
      NMLISTVIEW nm;
      memset(&nm, 0, sizeof(nm));
      nm.iItem = i;
      nm.lParam = pItem->lParam;
      SendNotify(hWnd, LVN_INSERTITEM, &nm.hdr);
      LvChanged(l);
      return i;
    }
    case LVM_DELETEITEM: {
      int i = (int)wParam;
      if (!LvIsItem(l, i)) return FALSE;
      LvEndEdit(l, FALSE);
      LvDeleteItem(l, i, TRUE);
      LvChanged(l);
      return TRUE;
    }
    case LVM_DELETEALLITEMS: {
      LvEndEdit(l, FALSE);
      int n = LvCount(l);
      NMLISTVIEW nm;
      memset(&nm, 0, sizeof(nm));
      nm.iItem = -1;
      BOOL bEach = n > 0 && SendNotify(hWnd, LVN_DELETEALLITEMS, &nm.hdr) == 0;
      for (int i = n - 1; i >= 0; i--) LvDeleteItem(l, i, bEach && !LvOwnerData(l));
      l->nOwnerCount = 0;
      l->ownerState.RemoveAll();
      l->iFocus = l->iMark = -1;
      l->ptOrigin.x = l->ptOrigin.y = 0;
      LvChanged(l);
      return TRUE;
    }
    case LVM_GETITEMA: {
      LVITEMA FAR* p = (LVITEMA FAR*)lParam;
      if (p == NULL || !LvIsItem(l, p->iItem)) return FALSE;
      LvGetItemFields(l, p);
      return TRUE;
    }
    case LVM_SETITEMA: {
      const LVITEMA FAR* p = (const LVITEMA FAR*)lParam;
      if (p == NULL || !LvIsItem(l, p->iItem)) return FALSE;
      LvSetItemFields(l, p->iItem, p);
      if (LvView(l) == LVS_LIST) LvChanged(l);
      return TRUE;
    }
    case LVM_GETITEMTEXTA: {
      LVITEMA FAR* p = (LVITEMA FAR*)lParam;
      int i = (int)wParam;
      if (p == NULL || !LvIsItem(l, i) || p->pszText == NULL || p->cchTextMax <= 0) return 0;
      CString strText;
      LvGetDisplay(l, i, p->iSubItem, &strText, NULL);
      lstrcpyn(p->pszText, strText, p->cchTextMax);
      return lstrlen(p->pszText);
    }
    case LVM_SETITEMTEXTA: {
      const LVITEMA FAR* p = (const LVITEMA FAR*)lParam;
      int i = (int)wParam;
      if (p == NULL || !LvIsItem(l, i)) return FALSE;
      LVITEMA item = *p;
      item.mask = LVIF_TEXT;
      item.iItem = i;
      LvSetItemFields(l, i, &item);
      if (LvView(l) == LVS_LIST) LvChanged(l);
      return TRUE;
    }
    case LVM_SETITEMSTATE: {
      const LVITEMA FAR* p = (const LVITEMA FAR*)lParam;
      if (p == NULL) return FALSE;
      int i = (int)(short)wParam;
      if (i == -1) {
        for (int k = 0; k < LvCount(l); k++) LvSetState(l, k, p->state, p->stateMask);
        return TRUE;
      }
      return LvSetState(l, i, p->state, p->stateMask);
    }
    case LVM_GETITEMSTATE:
      return LvGetState(l, (int)wParam) & (UINT)lParam;
    case LVM_GETNEXTITEM:
      return LvGetNext(l, (int)(short)wParam, LOWORD(lParam));
    case LVM_GETSELECTEDCOUNT: {
      int nCount = 0;
      for (int i = 0; i < LvCount(l); i++)
        if (LvGetState(l, i) & LVIS_SELECTED) nCount++;
      return nCount;
    }
    case LVM_FINDITEMA:
      return LvFind(l, (int)(short)wParam, (const LVFINDINFOA FAR*)lParam);
    case LVM_GETITEMRECT:
    case LVM_GETSUBITEMRECT: {
      RECT FAR* prc = (RECT FAR*)lParam;
      int i = (int)wParam;
      if (prc == NULL || !LvIsItem(l, i)) return FALSE;
      int nCode = prc->left;
      int iSub = nMsg == LVM_GETSUBITEMRECT ? prc->top : 0;
      RECT rcBounds, rcState, rcIcon, rcLabel;
      LvItemParts(l, i, &rcBounds, &rcState, &rcIcon, &rcLabel);
      if (iSub > 0) {
        int iCol = -1;
        for (int c = 0; c < l->colSub.GetSize(); c++)
          if ((int)l->colSub[c] == iSub) iCol = c;
        if (iCol < 0) return FALSE;
        *prc = LvSubItemRect(l, i, iCol);
        return TRUE;
      }
      switch (nCode) {
        case LVIR_ICON:
          *prc = rcIcon;
          break;
        case LVIR_LABEL:
          *prc = rcLabel;
          break;
        case LVIR_SELECTBOUNDS:
          ::UnionRect(prc, &rcIcon, &rcLabel);
          break;
        default:
          *prc = rcBounds;
          break;
      }
      return TRUE;
    }
    case LVM_HITTEST:
    case LVM_SUBITEMHITTEST: {
      LVHITTESTINFO FAR* p = (LVHITTESTINFO FAR*)lParam;
      if (p == NULL) return -1;
      int iSub = 0;
      p->iItem = LvHitTest(l, p->pt, &p->flags, &iSub);
      if (nMsg == LVM_SUBITEMHITTEST) p->iSubItem = iSub;
      return p->iItem;
    }
    case LVM_ENSUREVISIBLE:
      LvEnsureVisible(l, (int)wParam);
      return TRUE;
    case LVM_SCROLL:
      LvScrollBy(l, (int)wParam, (int)lParam);
      return TRUE;
    case LVM_REDRAWITEMS:
      for (int i = (int)wParam; i <= (int)lParam; i++) LvInvalidateItem(l, i);
      return TRUE;
    case LVM_UPDATE:
      LvChanged(l);
      return TRUE;
    case LVM_ARRANGE:
      LvChanged(l);
      return TRUE;
    case LVM_GETTOPINDEX: {
      DWORD dwView = LvView(l);
      if (dwView == LVS_REPORT) return l->ptOrigin.y / LvRowHeight(l);
      if (dwView == LVS_LIST) return l->nColumnWidth > 0 ? l->ptOrigin.x / l->nColumnWidth * LvPerLine(l) : 0;
      return 0;
    }
    case LVM_GETCOUNTPERPAGE: {
      RECT rcView = LvViewArea(l);
      DWORD dwView = LvView(l);
      if (dwView == LVS_REPORT) return (rcView.bottom - rcView.top) / LvRowHeight(l);
      if (dwView == LVS_LIST)
        return LvPerLine(l) * (int)MaxL(1, (rcView.right - rcView.left) / MaxL(1, l->nColumnWidth));
      return LvCount(l);
    }
    case LVM_GETORIGIN: {
      POINT FAR* p = (POINT FAR*)lParam;
      if (p == NULL) return FALSE;
      if (LvView(l) == LVS_REPORT || LvView(l) == LVS_LIST) {
        p->x = p->y = 0;
        return FALSE;
      }
      *p = l->ptOrigin;
      return TRUE;
    }
    case LVM_GETVIEWRECT: {
      RECT FAR* p = (RECT FAR*)lParam;
      if (p == NULL) return FALSE;
      SIZE size = LvContentSize(l);
      ::SetRect(p, -l->ptOrigin.x, -l->ptOrigin.y, size.cx - l->ptOrigin.x, size.cy - l->ptOrigin.y);
      return TRUE;
    }
    case LVM_GETITEMPOSITION: {
      POINT FAR* p = (POINT FAR*)lParam;
      if (p == NULL || !LvIsItem(l, (int)wParam)) return FALSE;
      RECT rc = LvToClient(l, LvItemCell(l, (int)wParam));
      p->x = rc.left;
      p->y = rc.top;
      return TRUE;
    }
    case LVM_SETITEMPOSITION:
    case LVM_SETITEMPOSITION32:
      return FALSE;  // the items are always arranged
    case LVM_GETITEMSPACING: {
      SIZE size = LvCellSize(l);
      return MAKELONG(size.cx, size.cy);
    }
    case LVM_SETICONSPACING: {
      SIZE sizeOld = LvCellSize(l);
      l->sizeSpacing.cx = (short)LOWORD(lParam);
      l->sizeSpacing.cy = (short)HIWORD(lParam);
      if (l->sizeSpacing.cx <= 0 || l->sizeSpacing.cy <= 0) l->sizeSpacing.cx = l->sizeSpacing.cy = 0;
      LvChanged(l);
      return MAKELONG(sizeOld.cx, sizeOld.cy);
    }
    case LVM_GETSTRINGWIDTHA:
      return lParam != 0 ? LvTextWidth(l, (LPCSTR)lParam) : 0;
    case LVM_INSERTCOLUMNA: {
      const LVCOLUMNA FAR* p = (const LVCOLUMNA FAR*)lParam;
      if (p == NULL) return -1;
      LvMakeHeader(l);
      if (l->hwndHeader == NULL) return -1;
      HDITEMA hdi;
      ColumnToHeader(p, &hdi);
      if (!(hdi.mask & HDI_FORMAT)) {
        hdi.mask |= HDI_FORMAT;
        hdi.fmt = HDF_LEFT | HDF_STRING;
      }
      int iCol = (int)::SendMessage(l->hwndHeader, HDM_INSERTITEMA, wParam,
                                    (LPARAM)(HDITEMA FAR*)&hdi);
      if (iCol < 0) return -1;
      int iSub = (p->mask & LVCF_SUBITEM) ? p->iSubItem : iCol;
      l->colSub.InsertAt(iCol, (WORD)iSub);
      LvChanged(l);
      return iCol;
    }
    case LVM_DELETECOLUMN: {
      int iCol = (int)wParam;
      if (l->hwndHeader == NULL || iCol < 0 || iCol >= l->colSub.GetSize()) return FALSE;
      ::SendMessage(l->hwndHeader, HDM_DELETEITEM, iCol, 0);
      l->colSub.RemoveAt(iCol);
      LvChanged(l);
      return TRUE;
    }
    case LVM_GETCOLUMNA: {
      LVCOLUMNA FAR* p = (LVCOLUMNA FAR*)lParam;
      int iCol = (int)wParam;
      if (p == NULL || l->hwndHeader == NULL || iCol < 0 || iCol >= l->colSub.GetSize())
        return FALSE;
      HDITEMA hdi;
      memset(&hdi, 0, sizeof(hdi));
      hdi.mask = HDI_WIDTH | HDI_FORMAT | HDI_IMAGE | HDI_ORDER;
      if (p->mask & LVCF_TEXT) {
        hdi.mask |= HDI_TEXT;
        hdi.pszText = p->pszText;
        hdi.cchTextMax = p->cchTextMax;
      }
      ::SendMessage(l->hwndHeader, HDM_GETITEMA, iCol, (LPARAM)(HDITEMA FAR*)&hdi);
      if (p->mask & LVCF_WIDTH) p->cx = hdi.cxy;
      if (p->mask & LVCF_FMT)
        p->fmt = ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT    ? LVCFMT_RIGHT
                  : (hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? LVCFMT_CENTER
                                                              : LVCFMT_LEFT) |
                 ((hdi.fmt & HDF_IMAGE) ? LVCFMT_IMAGE : 0);
      if (p->mask & LVCF_SUBITEM) p->iSubItem = l->colSub[iCol];
      if (p->mask & LVCF_IMAGE) p->iImage = hdi.iImage;
      if (p->mask & LVCF_ORDER) p->iOrder = hdi.iOrder;
      return TRUE;
    }
    case LVM_SETCOLUMNA: {
      const LVCOLUMNA FAR* p = (const LVCOLUMNA FAR*)lParam;
      int iCol = (int)wParam;
      if (p == NULL || l->hwndHeader == NULL || iCol < 0 || iCol >= l->colSub.GetSize())
        return FALSE;
      HDITEMA hdi;
      ColumnToHeader(p, &hdi);
      ::SendMessage(l->hwndHeader, HDM_SETITEMA, iCol, (LPARAM)(HDITEMA FAR*)&hdi);
      if (p->mask & LVCF_SUBITEM) l->colSub[iCol] = (WORD)p->iSubItem;
      LvChanged(l);
      return TRUE;
    }
    case LVM_GETCOLUMNWIDTH: {
      int x, cx;
      if (LvView(l) == LVS_LIST) return l->nColumnWidth;
      return LvColumnRect(l, (int)wParam, &x, &cx) ? cx : 0;
    }
    case LVM_SETCOLUMNWIDTH: {
      int cx = (int)(short)LOWORD(lParam);
      if (LvView(l) == LVS_LIST) {
        if (cx > 0) l->nColumnWidth = cx;
        ::InvalidateRect(hWnd, NULL, TRUE);
        return TRUE;
      }
      int iCol = (int)wParam;
      if (l->hwndHeader == NULL || iCol < 0 || iCol >= l->colSub.GetSize()) return FALSE;
      if (cx == LVSCW_AUTOSIZE || cx == LVSCW_AUTOSIZE_USEHEADER)
        cx = LvAutoWidth(l, iCol, cx == LVSCW_AUTOSIZE_USEHEADER);
      HDITEMA hdi;
      memset(&hdi, 0, sizeof(hdi));
      hdi.mask = HDI_WIDTH;
      hdi.cxy = cx;
      ::SendMessage(l->hwndHeader, HDM_SETITEMA, iCol, (LPARAM)(HDITEMA FAR*)&hdi);
      LvChanged(l);
      return TRUE;
    }
    case LVM_GETCOLUMNORDERARRAY:
      return l->hwndHeader != NULL ? ::SendMessage(l->hwndHeader, HDM_GETORDERARRAY, wParam, lParam)
                                   : FALSE;
    case LVM_SETCOLUMNORDERARRAY: {
      LRESULT lResult = l->hwndHeader != NULL
                            ? ::SendMessage(l->hwndHeader, HDM_SETORDERARRAY, wParam, lParam)
                            : FALSE;
      LvChanged(l);
      return lResult;
    }
    case LVM_GETHEADER:
      return (LRESULT)(UINT)l->hwndHeader;
    case LVM_SETIMAGELIST: {
      HIMAGELIST himlNew = (HIMAGELIST)lParam;
      HIMAGELIST* pSlot = wParam == LVSIL_SMALL ? &l->himlSmall
                          : wParam == LVSIL_STATE ? &l->himlState
                                                  : &l->himlNormal;
      HIMAGELIST himlOld = *pSlot;
      *pSlot = himlNew;
      LvChanged(l);
      return (LRESULT)himlOld;
    }
    case LVM_GETIMAGELIST:
      return (LRESULT)(wParam == LVSIL_SMALL ? l->himlSmall
                       : wParam == LVSIL_STATE ? l->himlState
                                               : l->himlNormal);
    case LVM_SETBKCOLOR:
    case LVM_SETTEXTCOLOR:
    case LVM_SETTEXTBKCOLOR: {
      COLORREF& clr = nMsg == LVM_SETBKCOLOR     ? l->clrBk
                      : nMsg == LVM_SETTEXTCOLOR ? l->clrText
                                                 : l->clrTextBk;
      clr = (COLORREF)lParam;
      ::InvalidateRect(hWnd, NULL, TRUE);
      return TRUE;
    }
    case LVM_GETBKCOLOR:
      return (LRESULT)LvBkColor(l);
    case LVM_GETTEXTCOLOR:
      return (LRESULT)(l->clrText == CLR_DEFAULT ? ::GetSysColor(COLOR_WINDOWTEXT) : l->clrText);
    case LVM_GETTEXTBKCOLOR:
      return (LRESULT)(l->clrTextBk == CLR_DEFAULT ? LvBkColor(l) : l->clrTextBk);
    case LVM_SETEXTENDEDLISTVIEWSTYLE: {
      DWORD dwOld = l->dwExStyle;
      DWORD dwMask = wParam != 0 ? (DWORD)wParam : 0xFFFFFFFFL;
      l->dwExStyle = (l->dwExStyle & ~dwMask) | ((DWORD)lParam & dwMask);
      if ((l->dwExStyle & LVS_EX_CHECKBOXES) && !(dwOld & LVS_EX_CHECKBOXES)) {
        for (int i = 0; i < LvCount(l); i++)
          if (!(LvGetState(l, i) & LVIS_STATEIMAGEMASK))
            LvPutState(l, i, LvGetState(l, i) | INDEXTOSTATEIMAGEMASK(1));
      }
      LvChanged(l);
      return (LRESULT)dwOld;
    }
    case LVM_GETEXTENDEDLISTVIEWSTYLE:
      return (LRESULT)l->dwExStyle;
    case LVM_SETCALLBACKMASK:
      l->nCallbackMask = (UINT)wParam;
      return TRUE;
    case LVM_GETCALLBACKMASK:
      return l->nCallbackMask;
    case LVM_SORTITEMS:
      LvSort(l, (PFNLVCOMPARE)lParam, (LPARAM)(UINT)wParam, FALSE);
      return TRUE;
    case LVM_SORTITEMSEX:
      LvSort(l, (PFNLVCOMPARE)lParam, (LPARAM)(UINT)wParam, TRUE);
      return TRUE;
    case LVM_EDITLABELA:
      return (LRESULT)(UINT)LvBeginEdit(l, (int)(short)wParam);
    case LVM_GETEDITCONTROL:
      return (LRESULT)(UINT)l->hwndEdit;
    case LVM_CANCELEDITLABEL:
      LvEndEdit(l, FALSE);
      return 0;
    case LVM_GETSELECTIONMARK:
      return l->iMark;
    case LVM_SETSELECTIONMARK: {
      int nOld = l->iMark;
      l->iMark = (int)lParam;
      return nOld;
    }
    case LVM_GETHOTITEM:
      return l->iHot;
    case LVM_SETHOTITEM: {
      int nOld = l->iHot;
      l->iHot = (int)wParam;
      return nOld;
    }
    case LVM_APPROXIMATEVIEWRECT: {
      SIZE size = LvContentSize(l);
      return MAKELONG(size.cx, size.cy + LvHeaderHeight(l));
    }
    case LVM_GETVIEW:
      switch (LvView(l)) {
        case LVS_REPORT:
          return LV_VIEW_DETAILS;
        case LVS_SMALLICON:
          return LV_VIEW_SMALLICON;
        case LVS_LIST:
          return LV_VIEW_LIST;
      }
      return LV_VIEW_ICON;
    case LVM_SETVIEW: {
      DWORD dwView = wParam == LV_VIEW_DETAILS     ? LVS_REPORT
                     : wParam == LV_VIEW_SMALLICON ? LVS_SMALLICON
                     : wParam == LV_VIEW_LIST      ? LVS_LIST
                     : wParam == LV_VIEW_ICON      ? LVS_ICON
                                                   : 0xFFFF;
      if (dwView == 0xFFFF) return -1;
      ::SetWindowLong(hWnd, GWL_STYLE, (LONG)((StyleOf(hWnd) & ~LVS_TYPEMASK) | dwView));
      l->ptOrigin.x = l->ptOrigin.y = 0;
      LvChanged(l);
      return 1;
    }
    case LVM_GETTOOLTIPS:
    case LVM_SETTOOLTIPS:
    case LVM_GETBKIMAGEA:
    case LVM_SETBKIMAGEA:
    case LVM_SETHOVERTIME:
    case LVM_GETHOVERTIME:
    case LVM_GETNUMBEROFWORKAREAS:
    case LVM_SETINFOTIP:
      return 0;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

/// The window procedure of the list view class.
LRESULT CALLBACK W16ListProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16List* l = StateOf<W16List>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16List>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (l == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg == WM_NCDESTROY) {
    DetachState(hWnd, l);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  return ListMessage(l, hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the header and list view classes. The list view needs the
/// header. Gives TRUE when both are there.
BOOL AFXAPI _AfxW16RegisterListClasses() {
  return RegisterCtlClasses(WC_HEADER, "SysHeader32", W16HeaderProc) &&
         RegisterCtlClasses(WC_LISTVIEW, "SysListView32", W16ListProc);
}

#endif  // _AFX_WIN16
