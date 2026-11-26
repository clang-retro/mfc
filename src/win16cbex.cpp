// win16cbex.cpp - the extended combo box (ComboBoxEx) window for 16-bit
// Windows, which has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The window holds a combo box with CBS_OWNERDRAWFIXED and
// CBS_HASSTRINGS and draws each item with its indent, its image and its
// text. It does the CBEM_ messages in ANSI, sends the CBEN_ notifications,
// and gives the CB_ messages to the combo box. The commands of the combo
// box go to the parent with the ID of the window.
//
// On 16-bit Windows the CB_ messages are WM_USER + 0 to WM_USER + 24, the
// same numbers as the CBEM_ messages of comctl32.dll. afxwin16u.h moves
// the CBEM_ messages above them; CBEM_DELETEITEM stays CB_DELETESTRING.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

// The width of one level of indent, in pixels.
const int kIndentWidth = 10;

// One item of an extended combo box.
struct W16CbItem {
  CString text;
  BOOL bCallback;  // the text is LPSTR_TEXTCALLBACK
  int iImage, iSelImage, iOverlay, iIndent;
  LPARAM lParam;
  /// Starts with no text, image 0 and no indent.
  W16CbItem() : bCallback(FALSE), iImage(0), iSelImage(0), iOverlay(0), iIndent(0), lParam(0) {}
};

// The data of an extended combo box.
struct W16CbEx {
  HWND hwnd;
  HWND hwndCombo;
  HWND hwndEdit;     // the edit control of the combo box, or NULL
  WNDPROC pfnEdit;   // the procedure of the edit control before the window
  CPtrArray items;   // W16CbItem*, in the order of the combo box
  HIMAGELIST himl;
  DWORD dwExStyle;
  BOOL bEditChanged; // the user changed the text of the edit control
  BOOL bEditing;     // CBEN_BEGINEDIT went to the parent
  HFONT hFont;
  /// Starts with no combo box and no items.
  W16CbEx()
      : hwnd(NULL), hwndCombo(NULL), hwndEdit(NULL), pfnEdit(NULL), himl(NULL), dwExStyle(0),
        bEditChanged(FALSE), bEditing(FALSE), hFont(NULL) {}
  /// Deletes the items.
  ~W16CbEx() {
    for (int i = 0; i < items.GetSize(); i++) delete (W16CbItem*)items[i];
  }
};

/// Gives the item i, or NULL.
W16CbItem* CbItem(W16CbEx* c, int i) {
  return i >= 0 && i < c->items.GetSize() ? (W16CbItem*)c->items[i] : NULL;
}

/// Gives the size of the images, or 0 by 0 with no image list.
SIZE CbImageSize(W16CbEx* c) {
  SIZE size = {0, 0};
  int cx = 0, cy = 0;
  if (c->himl != NULL && ::ImageList_GetIconSize(c->himl, &cx, &cy)) {
    size.cx = cx;
    size.cy = cy;
  }
  return size;
}

/// Sends a notification with an NMCOMBOBOXEXA for an item.
LRESULT CbNotifyItem(W16CbEx* c, UINT nCode, int i) {
  NMCOMBOBOXEXA nm;
  memset(&nm, 0, sizeof(nm));
  W16CbItem* p = CbItem(c, i);
  nm.ceItem.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_OVERLAY | CBEIF_INDENT |
                   CBEIF_LPARAM;
  nm.ceItem.iItem = i;
  if (p != NULL) {
    nm.ceItem.pszText = p->bCallback ? LPSTR_TEXTCALLBACKA : (LPSTR)(LPCSTR)p->text;
    nm.ceItem.iImage = p->iImage;
    nm.ceItem.iSelectedImage = p->iSelImage;
    nm.ceItem.iOverlay = p->iOverlay;
    nm.ceItem.iIndent = p->iIndent;
    nm.ceItem.lParam = p->lParam;
  }
  return SendNotify(c->hwnd, nCode, &nm.hdr);
}

/// Gives the text, the images and the indent that an item shows, with
/// the callbacks (CBEN_GETDISPINFOA). With CBEIF_DI_SETITEM the item
/// keeps what the parent gives.
void CbGetDisplay(W16CbEx* c, int i, CString* pText, int* piImage, int* piSelImage,
                  int* piIndent) {
  W16CbItem* p = CbItem(c, i);
  if (p == NULL) return;
  UINT mask = 0;
  if (pText != NULL) {
    if (p->bCallback)
      mask |= CBEIF_TEXT;
    else
      *pText = p->text;
  }
  if (piImage != NULL) {
    if (p->iImage == I_IMAGECALLBACK)
      mask |= CBEIF_IMAGE;
    else
      *piImage = p->iImage;
  }
  if (piSelImage != NULL) {
    if (p->iSelImage == I_IMAGECALLBACK)
      mask |= CBEIF_SELECTEDIMAGE;
    else
      *piSelImage = p->iSelImage;
  }
  if (piIndent != NULL) {
    if (p->iIndent == I_INDENTCALLBACK)
      mask |= CBEIF_INDENT;
    else
      *piIndent = p->iIndent;
  }
  if (mask == 0) return;
  char szText[CBEMAXSTRLEN];
  szText[0] = '\0';
  NMCOMBOBOXEXA nm;
  memset(&nm, 0, sizeof(nm));
  nm.ceItem.mask = mask;
  nm.ceItem.iItem = i;
  nm.ceItem.pszText = szText;
  nm.ceItem.cchTextMax = sizeof(szText);
  nm.ceItem.lParam = p->lParam;
  SendNotify(c->hwnd, CBEN_GETDISPINFOA, &nm.hdr);
  p = CbItem(c, i);
  if (p == NULL) return;
  BOOL bSet = (nm.ceItem.mask & CBEIF_DI_SETITEM) != 0;
  if (mask & CBEIF_TEXT) {
    CString str(nm.ceItem.pszText != NULL ? (LPCSTR)nm.ceItem.pszText : "");
    if (pText != NULL) *pText = str;
    if (bSet) {
      p->text = str;
      p->bCallback = FALSE;
    }
  }
  if (mask & CBEIF_IMAGE) {
    if (piImage != NULL) *piImage = nm.ceItem.iImage;
    if (bSet) p->iImage = nm.ceItem.iImage;
  }
  if (mask & CBEIF_SELECTEDIMAGE) {
    if (piSelImage != NULL) *piSelImage = nm.ceItem.iSelectedImage;
    if (bSet) p->iSelImage = nm.ceItem.iSelectedImage;
  }
  if (mask & CBEIF_INDENT) {
    if (piIndent != NULL) *piIndent = nm.ceItem.iIndent;
    if (bSet) p->iIndent = nm.ceItem.iIndent;
  }
}

/// Gives the height of an item: the text or the images, with a margin.
int CbItemHeight(W16CbEx* c) {
  int cy = FontHeight(c->hwnd, c->hFont != NULL ? c->hFont : (HFONT)::GetStockObject(SYSTEM_FONT));
  SIZE size = CbImageSize(c);
  if (size.cy > cy) cy = size.cy;
  return cy + 2;
}

/// Copies the fields of a COMBOBOXEXITEMA that its mask names into an
/// item.
void CbSetFields(W16CbItem* p, const COMBOBOXEXITEMA FAR* pItem) {
  if (pItem->mask & CBEIF_TEXT) {
    if (pItem->pszText == LPSTR_TEXTCALLBACKA) {
      p->bCallback = TRUE;
      p->text.Empty();
    } else {
      p->bCallback = FALSE;
      p->text = pItem->pszText != NULL ? (LPCSTR)pItem->pszText : "";
    }
  }
  if (pItem->mask & CBEIF_IMAGE) p->iImage = pItem->iImage;
  if (pItem->mask & CBEIF_SELECTEDIMAGE) p->iSelImage = pItem->iSelectedImage;
  if (pItem->mask & CBEIF_OVERLAY) p->iOverlay = pItem->iOverlay;
  if (pItem->mask & CBEIF_INDENT) p->iIndent = pItem->iIndent;
  if (pItem->mask & CBEIF_LPARAM) p->lParam = pItem->lParam;
}

/// Adds an item at an index (-1: the end) to the items and to the combo
/// box, and sends CBEN_INSERTITEM. Gives the index, or -1.
int CbInsert(W16CbEx* c, const COMBOBOXEXITEMA FAR* pItem) {
  int n = (int)c->items.GetSize();
  int i = (int)pItem->iItem;
  if (i < 0 || i > n) i = n;
  W16CbItem* p = new W16CbItem;
  CbSetFields(p, pItem);
  c->items.InsertAt(i, p);
  LRESULT l = ::SendMessage(c->hwndCombo, CB_INSERTSTRING, (WPARAM)i, (LPARAM)(LPCSTR)p->text);
  if (l < 0) {
    c->items.RemoveAt(i);
    delete p;
    return -1;
  }
  CbNotifyItem(c, CBEN_INSERTITEM, i);
  return i;
}

/// Removes the item i from the items and the combo box, and sends
/// CBEN_DELETEITEM. Gives the number of items left, or CB_ERR.
int CbDelete(W16CbEx* c, int i) {
  if (CbItem(c, i) == NULL) return CB_ERR;
  CbNotifyItem(c, CBEN_DELETEITEM, i);
  W16CbItem* p = CbItem(c, i);
  if (p == NULL) return CB_ERR;
  c->items.RemoveAt(i);
  delete p;
  return (int)::SendMessage(c->hwndCombo, CB_DELETESTRING, (WPARAM)i, 0);
}

/// Fills a COMBOBOXEXITEMA with an item, or with the edit control for
/// iItem -1.
BOOL CbGetItem(W16CbEx* c, COMBOBOXEXITEMA FAR* pItem) {
  int i = (int)pItem->iItem;
  if (i == -1) {
    if (pItem->mask & CBEIF_TEXT && pItem->pszText != NULL && pItem->cchTextMax > 0)
      ::GetWindowText(c->hwndCombo, pItem->pszText, pItem->cchTextMax);
    int iSel = (int)::SendMessage(c->hwndCombo, CB_GETCURSEL, 0, 0);
    W16CbItem* p = CbItem(c, iSel);
    if (p != NULL) {
      if (pItem->mask & CBEIF_IMAGE) pItem->iImage = p->iImage;
      if (pItem->mask & CBEIF_SELECTEDIMAGE) pItem->iSelectedImage = p->iSelImage;
      if (pItem->mask & CBEIF_LPARAM) pItem->lParam = p->lParam;
    }
    return TRUE;
  }
  W16CbItem* p = CbItem(c, i);
  if (p == NULL) return FALSE;
  CString str;
  int iImage = 0, iSelImage = 0, iIndent = 0;
  CbGetDisplay(c, i, &str, &iImage, &iSelImage, &iIndent);
  p = CbItem(c, i);
  if (p == NULL) return FALSE;
  if ((pItem->mask & CBEIF_TEXT) && pItem->pszText != NULL && pItem->cchTextMax > 0)
    lstrcpyn(pItem->pszText, str, pItem->cchTextMax);
  if (pItem->mask & CBEIF_IMAGE) pItem->iImage = iImage;
  if (pItem->mask & CBEIF_SELECTEDIMAGE) pItem->iSelectedImage = iSelImage;
  if (pItem->mask & CBEIF_OVERLAY) pItem->iOverlay = p->iOverlay;
  if (pItem->mask & CBEIF_INDENT) pItem->iIndent = iIndent;
  if (pItem->mask & CBEIF_LPARAM) pItem->lParam = p->lParam;
  return TRUE;
}

/// Changes an item, or the text of the edit control for iItem -1. A new
/// text goes into the combo box too, and the selection stays.
BOOL CbSetItem(W16CbEx* c, const COMBOBOXEXITEMA FAR* pItem) {
  int i = (int)pItem->iItem;
  if (i == -1) {
    if ((pItem->mask & CBEIF_TEXT) && pItem->pszText != LPSTR_TEXTCALLBACKA)
      ::SetWindowText(c->hwndCombo, pItem->pszText != NULL ? (LPCSTR)pItem->pszText : "");
    return TRUE;
  }
  W16CbItem* p = CbItem(c, i);
  if (p == NULL) return FALSE;
  CbSetFields(p, pItem);
  if (pItem->mask & CBEIF_TEXT) {
    int iSel = (int)::SendMessage(c->hwndCombo, CB_GETCURSEL, 0, 0);
    ::SendMessage(c->hwndCombo, CB_DELETESTRING, (WPARAM)i, 0);
    ::SendMessage(c->hwndCombo, CB_INSERTSTRING, (WPARAM)i, (LPARAM)(LPCSTR)p->text);
    if (iSel == i) ::SendMessage(c->hwndCombo, CB_SETCURSEL, (WPARAM)i, 0);
  }
  ::InvalidateRect(c->hwndCombo, NULL, TRUE);
  return TRUE;
}

/// Draws an item of the combo box: the indent, the image (the selected
/// image when the item is selected) and the text in the selection colors.
/// The selection field of the combo box has no indent with
/// CBES_EX_NOEDITIMAGEINDENT and no image with CBES_EX_NOEDITIMAGE.
void CbDrawItem(W16CbEx* c, const DRAWITEMSTRUCT FAR* pdis) {
  HDC hdc = pdis->hDC;
  RECT rc = pdis->rcItem;
  BOOL bSelected = (pdis->itemState & ODS_SELECTED) != 0;
  // The selection field of a 16-bit combo box starts inside the frame,
  // and the items of the list start at 0.
  BOOL bField = rc.left > 0;
  BOOL bEnabled = ::IsWindowEnabled(c->hwnd);
  FillColor(hdc, rc, ::GetSysColor(bSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
  int i = (int)(short)pdis->itemID;
  if (i < 0) {
    if (pdis->itemState & ODS_FOCUS) ::DrawFocusRect(hdc, &rc);
    return;
  }
  CString str;
  int iImage = 0, iSelImage = 0, iIndent = 0;
  CbGetDisplay(c, i, &str, &iImage, &iSelImage, &iIndent);
  W16CbItem* p = CbItem(c, i);
  if (p == NULL) return;
  int x = rc.left + 2;
  if (!(bField && (c->dwExStyle & CBES_EX_NOEDITIMAGEINDENT)) && iIndent > 0)
    x += iIndent * kIndentWidth;
  BOOL bImage = c->himl != NULL && !(bField && (c->dwExStyle & CBES_EX_NOEDITIMAGE));
  if (bImage) {
    SIZE size = CbImageSize(c);
    int iDraw = bSelected ? iSelImage : iImage;
    UINT fStyle = ILD_TRANSPARENT | INDEXTOOVERLAYMASK(p->iOverlay & 15);
    if (iDraw >= 0)
      ::ImageList_Draw(c->himl, iDraw, hdc, x, rc.top + (rc.bottom - rc.top - size.cy) / 2, fStyle);
    x += size.cx + 3;
  }
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, !bEnabled ? ::GetSysColor(COLOR_GRAYTEXT)
                      : bSelected ? ::GetSysColor(COLOR_HIGHLIGHTTEXT)
                                  : ::GetSysColor(COLOR_WINDOWTEXT));
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  ::TextOut(hdc, x, rc.top + (rc.bottom - rc.top - tm.tmHeight) / 2, str, str.GetLength());
  if (pdis->itemState & ODS_FOCUS) ::DrawFocusRect(hdc, &rc);
}

/// Sends CBEN_ENDEDITA when an edit of the text ends: the parent gets the
/// text, the item that matches it and the reason (CBENF_). The parent can
/// refuse the text by giving TRUE.
BOOL CbEndEdit(W16CbEx* c, int iWhy) {
  if (!c->bEditing) return FALSE;
  c->bEditing = FALSE;
  NMCBEENDEDITA* pnm = new NMCBEENDEDITA;
  memset(pnm, 0, sizeof(*pnm));
  pnm->fChanged = c->bEditChanged;
  ::GetWindowText(c->hwndCombo, pnm->szText, sizeof(pnm->szText));
  pnm->iNewSelection = (int)::SendMessage(c->hwndCombo, CB_FINDSTRINGEXACT, (WPARAM)-1,
                                          (LPARAM)(LPCSTR)pnm->szText);
  pnm->iWhy = iWhy;
  LRESULT lResult = SendNotify(c->hwnd, CBEN_ENDEDITA, &pnm->hdr);
  delete pnm;
  c->bEditChanged = FALSE;
  return lResult != 0;
}

/// The procedure of the edit control of the combo box: the focus starts
/// an edit (CBEN_BEGINEDIT), and the loss of the focus, Enter and Esc end
/// it (CBEN_ENDEDITA).
LRESULT CALLBACK CbEditProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  HWND hwndEx = ::GetParent(::GetParent(hWnd));
  W16CbEx* c = StateOf<W16CbEx>(hwndEx);
  if (c == NULL || c->pfnEdit == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_SETFOCUS:
      if (!c->bEditing) {
        c->bEditing = TRUE;
        NMHDR nm;
        SendNotify(c->hwnd, CBEN_BEGINEDIT, &nm);
      }
      break;
    case WM_KILLFOCUS: {
      LRESULT lResult = ::CallWindowProc(c->pfnEdit, hWnd, nMsg, wParam, lParam);
      CbEndEdit(c, CBENF_KILLFOCUS);
      return lResult;
    }
    case WM_KEYDOWN:
      if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
        if (!CbEndEdit(c, wParam == VK_RETURN ? CBENF_RETURN : CBENF_ESCAPE)) {
          c->bEditing = TRUE;  // the edit goes on while the focus stays
        }
      }
      break;
  }
  return ::CallWindowProc(c->pfnEdit, hWnd, nMsg, wParam, lParam);
}

/// Makes the combo box of an extended combo box, subclasses its edit
/// control, and sets the height of the window to the height of the closed
/// combo box.
BOOL CbCreate(W16CbEx* c, const CREATESTRUCT FAR* pcs) {
  DWORD dwStyle = (DWORD)pcs->style;
  DWORD dwCombo = WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                  CBS_AUTOHSCROLL | (dwStyle & 0x0003);
  if ((dwStyle & 0x0003) == 0) dwCombo |= CBS_DROPDOWN;
  HINSTANCE hInst = (HINSTANCE)::GetWindowWord(c->hwnd, GWW_HINSTANCE);
  c->hwndCombo = ::CreateWindow("COMBOBOX", "", dwCombo, 0, 0, pcs->cx, pcs->cy, c->hwnd,
                                (HMENU)1001, hInst, NULL);
  if (c->hwndCombo == NULL) return FALSE;
  c->hwndEdit = ::GetWindow(c->hwndCombo, GW_CHILD);
  if (c->hwndEdit != NULL)
    c->pfnEdit = (WNDPROC)::SetWindowLong(c->hwndEdit, GWL_WNDPROC, (LONG)(WNDPROC)CbEditProc);
  return TRUE;
}

/// Makes the height of the window the height of the closed combo box, so
/// that the window does not cover what is below it.
void CbFitHeight(W16CbEx* c) {
  RECT rcCombo, rcWnd;
  ::GetWindowRect(c->hwndCombo, &rcCombo);
  ::GetWindowRect(c->hwnd, &rcWnd);
  int cx = rcWnd.right - rcWnd.left;
  int cy = rcCombo.bottom - rcCombo.top;
  RECT rcClient;
  ::GetClientRect(c->hwnd, &rcClient);
  int cyFrame = (rcWnd.bottom - rcWnd.top) - rcClient.bottom;
  if (rcClient.bottom != cy) ::SetWindowPos(c->hwnd, NULL, 0, 0, cx, cy + cyFrame, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/// The window procedure of the extended combo box class.
LRESULT CALLBACK W16CbExProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16CbEx* c = StateOf<W16CbEx>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16CbEx>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (c == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  if (nMsg >= CB_GETEDITSEL && nMsg <= CB_FINDSTRINGEXACT && c->hwndCombo != NULL) {
    // The CB_ messages go to the combo box. The item data is the lParam
    // of the items, and new strings become items.
    switch (nMsg) {
      case CB_ADDSTRING:
      case CB_INSERTSTRING: {
        COMBOBOXEXITEMA item;
        memset(&item, 0, sizeof(item));
        item.mask = CBEIF_TEXT;
        item.iItem = nMsg == CB_ADDSTRING ? -1 : (int)(short)wParam;
        item.pszText = (LPSTR)lParam;
        return CbInsert(c, &item);
      }
      case CB_DELETESTRING:
        return CbDelete(c, (int)(short)wParam);
      case CB_RESETCONTENT:
        while (c->items.GetSize() > 0) CbDelete(c, (int)c->items.GetSize() - 1);
        ::SendMessage(c->hwndCombo, CB_RESETCONTENT, 0, 0);
        return CB_OKAY;
      case CB_GETITEMDATA: {
        W16CbItem* p = CbItem(c, (int)(short)wParam);
        return p != NULL ? p->lParam : CB_ERR;
      }
      case CB_SETITEMDATA: {
        W16CbItem* p = CbItem(c, (int)(short)wParam);
        if (p == NULL) return CB_ERR;
        p->lParam = lParam;
        return CB_OKAY;
      }
      case CB_DIR:
        return CB_ERR;
    }
    return ::SendMessage(c->hwndCombo, nMsg, wParam, lParam);
  }
  switch (nMsg) {
    case WM_NCDESTROY:
      DetachState(hWnd, c);
      break;
    case WM_CREATE:
      if (!CbCreate(c, (const CREATESTRUCT FAR*)lParam)) return -1;
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, (WPARAM)-1, CbItemHeight(c));
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, 0, CbItemHeight(c));
      CbFitHeight(c);
      return 0;
    case WM_DESTROY:
      while (c->items.GetSize() > 0) {
        int i = (int)c->items.GetSize() - 1;
        CbNotifyItem(c, CBEN_DELETEITEM, i);
        delete CbItem(c, i);
        c->items.RemoveAt(i);
      }
      return 0;
    case WM_SIZE: {
      RECT rcCombo;
      ::GetWindowRect(c->hwndCombo, &rcCombo);
      // The dropped height of the combo box stays.
      RECT rcDrop;
      ::SendMessage(c->hwndCombo, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)(RECT FAR*)&rcDrop);
      int cyDrop = rcDrop.bottom - rcCombo.top;
      if (cyDrop < rcCombo.bottom - rcCombo.top) cyDrop = rcCombo.bottom - rcCombo.top;
      ::SetWindowPos(c->hwndCombo, NULL, 0, 0, LOWORD(lParam), cyDrop,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_MEASUREITEM: {
      MEASUREITEMSTRUCT FAR* pmis = (MEASUREITEMSTRUCT FAR*)lParam;
      pmis->itemHeight = (UINT)CbItemHeight(c);
      return TRUE;
    }
    case WM_DRAWITEM:
      CbDrawItem(c, (const DRAWITEMSTRUCT FAR*)lParam);
      return TRUE;
    case WM_COMMAND:
      if ((HWND)(UINT)LOWORD(lParam) == c->hwndCombo) {
        UINT nCode = HIWORD(lParam);
        if (nCode == CBN_EDITCHANGE) c->bEditChanged = TRUE;
        if (nCode == CBN_DROPDOWN && c->bEditing) CbEndEdit(c, CBENF_DROPDOWN);
        ::SendMessage(::GetParent(hWnd), WM_COMMAND, (WPARAM)::GetDlgCtrlID(hWnd),
                      MAKELPARAM((WORD)(UINT)hWnd, nCode));
      }
      return 0;
    case WM_CTLCOLOR:
      return ::SendMessage(::GetParent(hWnd), nMsg, wParam, lParam);
    case WM_SETFOCUS:
      ::SetFocus(c->hwndCombo);
      return 0;
    case WM_ENABLE:
      ::EnableWindow(c->hwndCombo, (BOOL)wParam);
      return 0;
    case WM_SETFONT:
      c->hFont = (HFONT)wParam;
      ::SendMessage(c->hwndCombo, WM_SETFONT, wParam, lParam);
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, (WPARAM)-1, CbItemHeight(c));
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, 0, CbItemHeight(c));
      CbFitHeight(c);
      return 0;
    case WM_GETFONT:
      return (LRESULT)(UINT)c->hFont;
    case WM_SETTEXT:
      return ::SendMessage(c->hwndCombo, nMsg, wParam, lParam);
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
      return ::SendMessage(c->hwndCombo, nMsg, wParam, lParam);
    case CBEM_INSERTITEMA:
      if (lParam == 0) return -1;
      return CbInsert(c, (const COMBOBOXEXITEMA FAR*)lParam);
    case CBEM_SETIMAGELIST: {
      HIMAGELIST himlOld = c->himl;
      c->himl = (HIMAGELIST)lParam;
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, (WPARAM)-1, CbItemHeight(c));
      ::SendMessage(c->hwndCombo, CB_SETITEMHEIGHT, 0, CbItemHeight(c));
      CbFitHeight(c);
      ::InvalidateRect(c->hwndCombo, NULL, TRUE);
      return (LRESULT)himlOld;
    }
    case CBEM_GETIMAGELIST:
      return (LRESULT)c->himl;
    case CBEM_GETITEMA:
      if (lParam == 0) return FALSE;
      return CbGetItem(c, (COMBOBOXEXITEMA FAR*)lParam);
    case CBEM_SETITEMA:
      if (lParam == 0) return FALSE;
      return CbSetItem(c, (const COMBOBOXEXITEMA FAR*)lParam);
    case CBEM_GETCOMBOCONTROL:
      return (LRESULT)(UINT)c->hwndCombo;
    case CBEM_GETEDITCONTROL:
      return (LRESULT)(UINT)c->hwndEdit;
    case CBEM_SETEXSTYLE:
    case CBEM_SETEXTENDEDSTYLE: {
      DWORD dwOld = c->dwExStyle;
      DWORD dwMask = nMsg == CBEM_SETEXTENDEDSTYLE && wParam != 0 ? (DWORD)wParam : 0xFFFFFFFFL;
      c->dwExStyle = (c->dwExStyle & ~dwMask) | ((DWORD)lParam & dwMask);
      ::InvalidateRect(c->hwndCombo, NULL, TRUE);
      return (LRESULT)dwOld;
    }
    case CBEM_GETEXSTYLE:
      return (LRESULT)c->dwExStyle;
    case CBEM_HASEDITCHANGED:
      return c->bEditChanged;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the extended combo box class. Gives TRUE when the class is
/// there.
BOOL AFXAPI _AfxW16RegisterComboExClass() {
  return RegisterCtlClasses(WC_COMBOBOXEX, "ComboBoxEx32", W16CbExProc);
}

#endif  // _AFX_WIN16
