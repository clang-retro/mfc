// listbox.cpp - CCheckListBox, a list box with check boxes, and
// CDragListBox, whose items the user drags into a new order.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxcmn.h>

// ---------------------------------------------------------------------
// CCheckListBox: each item's data is a CHECK_DATA, which keeps its check,
// whether it is enabled, and the program's own item data. The list box
// messages for item data see only the program's.

namespace {
struct CHECK_DATA {
  int nCheck;
  BOOL bEnabled;
  DWORD_PTR dwUserData;
};
}  // namespace

IMPLEMENT_DYNAMIC(CCheckListBox, CListBox)

BEGIN_MESSAGE_MAP(CCheckListBox, CListBox)
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CCheckListBox::CCheckListBox() : m_nStyle(BS_AUTOCHECKBOX), m_cyText(0) {}

CCheckListBox::~CCheckListBox() {}

BOOL CCheckListBox::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  // Owner drawn, with strings.
  if ((dwStyle & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) == 0)
    dwStyle |= LBS_OWNERDRAWFIXED;
  dwStyle |= LBS_HASSTRINGS;
  return CListBox::Create(dwStyle, rect, pParentWnd, nID);
}

void CCheckListBox::SetCheckStyle(UINT nStyle) {
  if (nStyle == BS_CHECKBOX || nStyle == BS_AUTOCHECKBOX || nStyle == BS_3STATE ||
      nStyle == BS_AUTO3STATE)
    m_nStyle = nStyle;
}

// The item's CHECK_DATA, from the list box itself (not through the
// translation in WindowProc).
static CHECK_DATA* GetCheckData(CCheckListBox* pList, int nIndex) {
  LRESULT l = pList->DefWindowProc(LB_GETITEMDATA, nIndex, 0);
  return l == LB_ERR ? NULL : (CHECK_DATA*)l;
}

// An item that has none yet gets one.
static CHECK_DATA* EnsureCheckData(CCheckListBox* pList, int nIndex) {
  CHECK_DATA* pData = GetCheckData(pList, nIndex);
  if (pData != NULL || nIndex < 0 || nIndex >= pList->GetCount()) return pData;
  pData = new CHECK_DATA;
  pData->nCheck = 0;
  pData->bEnabled = TRUE;
  pData->dwUserData = 0;
  pList->DefWindowProc(LB_SETITEMDATA, nIndex, (LPARAM)pData);
  return pData;
}

void CCheckListBox::SetCheck(int nIndex, int nCheck) {
  CHECK_DATA* pData = EnsureCheckData(this, nIndex);
  if (pData == NULL || nCheck < 0 || nCheck > 2) return;
  pData->nCheck = nCheck;
  InvalidateCheck(nIndex);
}

int CCheckListBox::GetCheck(int nIndex) {
  CHECK_DATA* pData = GetCheckData(this, nIndex);
  return pData != NULL ? pData->nCheck : 0;
}

void CCheckListBox::Enable(int nIndex, BOOL bEnabled) {
  CHECK_DATA* pData = EnsureCheckData(this, nIndex);
  if (pData == NULL) return;
  pData->bEnabled = bEnabled;
  CRect rect;
  if (GetItemRect(nIndex, &rect) != LB_ERR) InvalidateRect(&rect);
}

BOOL CCheckListBox::IsEnabled(int nIndex) {
  CHECK_DATA* pData = GetCheckData(this, nIndex);
  return pData == NULL || pData->bEnabled;
}

CSize CCheckListBox::CheckSize() const { return CSize(13, 13); }

CRect CCheckListBox::OnGetCheckPosition(CRect rectItem, CRect rectCheckBox) {
  CRect rect(rectCheckBox);
  rect.OffsetRect(rectItem.left + 1 - rect.left,
                  rectItem.top + (rectItem.Height() - rect.Height()) / 2 - rect.top);
  return rect;
}

void CCheckListBox::InvalidateCheck(int nIndex) {
  CRect rect;
  if (m_hWnd == NULL || GetItemRect(nIndex, &rect) == LB_ERR) return;
  CSize size = CheckSize();
  CRect rectCheck = OnGetCheckPosition(rect, CRect(0, 0, size.cx, size.cy));
  InvalidateRect(&rectCheck);
}

int CCheckListBox::CalcMinimumItemHeight() {
  if (m_cyText == 0) {
    CClientDC dc(this);
    CFont* pFont = GetFont();
    CFont* pOld = pFont != NULL ? dc.SelectObject(pFont) : NULL;
    TEXTMETRIC tm;
    dc.GetTextMetrics(&tm);
    if (pOld != NULL) dc.SelectObject(pOld);
    m_cyText = tm.tmHeight;
  }
  return max(m_cyText, (int)CheckSize().cy + 2);
}

// A list box made from a template measured its items before it was ours.
void CCheckListBox::PreSubclassWindow() {
  CListBox::PreSubclassWindow();
  if (GetStyle() & LBS_OWNERDRAWFIXED) {
    int cy = CalcMinimumItemHeight();
    if ((int)DefWindowProc(LB_GETITEMHEIGHT, 0, 0) < cy) DefWindowProc(LB_SETITEMHEIGHT, 0, cy);
  }
}

void CCheckListBox::PreMeasureItem(LPMEASUREITEMSTRUCT lpMIS) {
  CHECK_DATA* pData = (CHECK_DATA*)lpMIS->itemData;
  DWORD_PTR dwSaved = lpMIS->itemData;
  if (pData != NULL && lpMIS->itemID != (UINT)-1) lpMIS->itemData = pData->dwUserData;
  MeasureItem(lpMIS);
  lpMIS->itemData = dwSaved;
  int cy = CalcMinimumItemHeight();
  if ((int)lpMIS->itemHeight < cy) lpMIS->itemHeight = cy;
}

void CCheckListBox::MeasureItem(LPMEASUREITEMSTRUCT lpMIS) {
  lpMIS->itemHeight = CalcMinimumItemHeight();
}

// The check box, then the item (DrawItem) to its right.
void CCheckListBox::PreDrawItem(LPDRAWITEMSTRUCT lpDIS) {
  if (lpDIS->itemID == (UINT)-1) {
    DrawItem(lpDIS);
    return;
  }
  CHECK_DATA* pData = (CHECK_DATA*)lpDIS->itemData;
  CDC* pDC = CDC::FromHandle(lpDIS->hDC);
  CRect rectItem(lpDIS->rcItem);
  CSize size = CheckSize();
  CRect rectCheck = OnGetCheckPosition(rectItem, CRect(0, 0, size.cx, size.cy));
  if (lpDIS->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {
    CRect rectBack(rectItem.left, rectItem.top, rectCheck.right + 2, rectItem.bottom);
    pDC->FillSolidRect(rectBack, ::GetSysColor(COLOR_WINDOW));
    int nCheck = pData != NULL ? pData->nCheck : 0;
    BOOL bEnabled = (pData == NULL || pData->bEnabled) && IsWindowEnabled();
    UINT nState = DFCS_BUTTONCHECK | DFCS_FLAT;
    if (nCheck == 1) nState |= DFCS_CHECKED;
    if (nCheck == 2) nState = DFCS_BUTTON3STATE | DFCS_CHECKED | DFCS_FLAT;
    if (!bEnabled) nState |= DFCS_INACTIVE;
    pDC->DrawFrameControl(rectCheck, DFC_BUTTON, nState);
  }
  // The item, right of the check box, with the program's own data.
  DRAWITEMSTRUCT dis = *lpDIS;
  dis.rcItem.left = rectCheck.right + 2;
  if (pData != NULL) dis.itemData = pData->dwUserData;
  if (pData != NULL && !pData->bEnabled) dis.itemState |= ODS_DISABLED;
  DrawItem(&dis);
}

void CCheckListBox::DrawItem(LPDRAWITEMSTRUCT lpDIS) {
  if (lpDIS->itemID == (UINT)-1) {
    if (lpDIS->itemState & ODS_FOCUS) ::DrawFocusRect(lpDIS->hDC, &lpDIS->rcItem);
    return;
  }
  CDC* pDC = CDC::FromHandle(lpDIS->hDC);
  BOOL bSelected = (lpDIS->itemState & ODS_SELECTED) != 0;
  BOOL bDisabled = (lpDIS->itemState & ODS_DISABLED) != 0 || !IsWindowEnabled();
  COLORREF clrBack = ::GetSysColor(bSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
  COLORREF clrText = ::GetSysColor(bDisabled  ? COLOR_GRAYTEXT
                                   : bSelected ? COLOR_HIGHLIGHTTEXT
                                               : COLOR_WINDOWTEXT);
  if (lpDIS->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {
    pDC->FillSolidRect(&lpDIS->rcItem, clrBack);
    if (GetStyle() & LBS_HASSTRINGS) {
      CString str;
      GetText((int)lpDIS->itemID, str);
      COLORREF clrOldText = pDC->SetTextColor(clrText);
      int nOldMode = pDC->SetBkMode(TRANSPARENT);
      CRect rect(lpDIS->rcItem);
      rect.left += 2;
      pDC->DrawText(str, rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
      pDC->SetBkMode(nOldMode);
      pDC->SetTextColor(clrOldText);
    }
  }
  if ((lpDIS->itemAction & ODA_FOCUS) || (lpDIS->itemState & ODS_FOCUS))
    if (lpDIS->itemState & ODS_FOCUS) ::DrawFocusRect(lpDIS->hDC, &lpDIS->rcItem);
}

BOOL CCheckListBox::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  switch (message) {
    case WM_DRAWITEM:
      PreDrawItem((LPDRAWITEMSTRUCT)lParam);
      return TRUE;
    case WM_MEASUREITEM:
      PreMeasureItem((LPMEASUREITEMSTRUCT)lParam);
      return TRUE;
    case WM_COMPAREITEM: {
      COMPAREITEMSTRUCT cis = *(LPCOMPAREITEMSTRUCT)lParam;
      CHECK_DATA* p1 = (CHECK_DATA*)cis.itemData1;
      CHECK_DATA* p2 = (CHECK_DATA*)cis.itemData2;
      if (p1 != NULL && cis.itemID1 != (UINT)-1) cis.itemData1 = p1->dwUserData;
      if (p2 != NULL && cis.itemID2 != (UINT)-1) cis.itemData2 = p2->dwUserData;
      int n = CompareItem(&cis);
      if (pResult != NULL) *pResult = n;
      return TRUE;
    }
    case WM_DELETEITEM: {
      DELETEITEMSTRUCT dis = *(LPDELETEITEMSTRUCT)lParam;
      CHECK_DATA* pData = (CHECK_DATA*)dis.itemData;
      if (pData != NULL) dis.itemData = pData->dwUserData;
      DeleteItem(&dis);
      delete pData;
      return TRUE;
    }
  }
  return CListBox::OnChildNotify(message, wParam, lParam, pResult);
}

// The program's item data in and out; every new item gets its check data.
LRESULT CCheckListBox::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case LB_GETITEMDATA: {
      LRESULT l = DefWindowProc(LB_GETITEMDATA, wParam, 0);
      if (l == LB_ERR) return l;
      CHECK_DATA* pData = (CHECK_DATA*)l;
      return pData != NULL ? (LRESULT)pData->dwUserData : 0;
    }
    case LB_SETITEMDATA: {
      if ((int)wParam < 0 || (int)wParam >= GetCount()) return LB_ERR;
      CHECK_DATA* pData = EnsureCheckData(this, (int)wParam);
      if (pData == NULL) return LB_ERR;
      pData->dwUserData = (DWORD_PTR)lParam;
      return 0;
    }
    case LB_ADDSTRING:
    case LB_INSERTSTRING: {
      if (!(GetStyle() & LBS_HASSTRINGS)) {
        // Without strings, lParam is the item's data.
        CHECK_DATA* pData = new CHECK_DATA;
        pData->nCheck = 0;
        pData->bEnabled = TRUE;
        pData->dwUserData = (DWORD_PTR)lParam;
        LRESULT l = CListBox::WindowProc(message, wParam, (LPARAM)pData);
        if (l < 0) delete pData;
        return l;
      }
      LRESULT l = CListBox::WindowProc(message, wParam, lParam);
      if (l >= 0) EnsureCheckData(this, (int)l);
      return l;
    }
  }
  return CListBox::WindowProc(message, wParam, lParam);
}

void CCheckListBox::ToggleCheck(int nIndex) {
  if (!IsEnabled(nIndex)) return;
  int nCheck = GetCheck(nIndex);
  if (m_nStyle == BS_AUTO3STATE)
    nCheck = (nCheck + 1) % 3;
  else if (m_nStyle == BS_AUTOCHECKBOX)
    nCheck = nCheck == 1 ? 0 : 1;
  else
    nCheck = -1;  // the program changes it, on CLBN_CHKCHANGE
  if (nCheck >= 0) SetCheck(nIndex, nCheck);
  CWnd* pParent = GetParent();
  if (pParent != NULL)
    pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), CLBN_CHKCHANGE), (LPARAM)m_hWnd);
}

void CCheckListBox::OnLButtonDown(UINT nFlags, CPoint point) {
  SetFocus();
  BOOL bOutside = FALSE;
  int nIndex = (int)ItemFromPoint(point, bOutside);
  if (!bOutside && nIndex >= 0 && nIndex < GetCount()) {
    CRect rect;
    GetItemRect(nIndex, &rect);
    CSize size = CheckSize();
    CRect rectCheck = OnGetCheckPosition(rect, CRect(0, 0, size.cx, size.cy));
    if (rectCheck.PtInRect(point)) {
      // A click on the check box: the item is selected, and toggled.
      if ((GetStyle() & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) == 0 && GetCurSel() != nIndex) {
        SetCurSel(nIndex);
        CWnd* pParent = GetParent();
        if (pParent != NULL)
          pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), LBN_SELCHANGE),
                               (LPARAM)m_hWnd);
      }
      ToggleCheck(nIndex);
      return;
    }
  }
  CListBox::OnLButtonDown(nFlags, point);
}

void CCheckListBox::OnLButtonDblClk(UINT nFlags, CPoint point) {
  // A double click on the box is two clicks.
  OnLButtonDown(nFlags, point);
}

void CCheckListBox::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_SPACE) {
    int nIndex = GetCaretIndex();
    if (nIndex >= 0 && nIndex < GetCount()) {
      ToggleCheck(nIndex);
      return;
    }
  }
  CListBox::OnKeyDown(nChar, nRepCnt, nFlags);
}

// ---------------------------------------------------------------------
// CDragListBox

IMPLEMENT_DYNAMIC(CDragListBox, CListBox)

CDragListBox::CDragListBox() : m_nLast(-1) {}

CDragListBox::~CDragListBox() {}

void CDragListBox::PreSubclassWindow() {
  CListBox::PreSubclassWindow();
  ::MakeDragList(m_hWnd);
}

int CDragListBox::ItemFromPt(CPoint pt, BOOL bAutoScroll) const {
  return ::LBItemFromPt(m_hWnd, pt, bAutoScroll);
}

BOOL CDragListBox::BeginDrag(CPoint pt) {
  m_nLast = -1;
  DrawInsert(ItemFromPt(pt));
  return TRUE;
}

void CDragListBox::CancelDrag(CPoint) { DrawInsert(-1); }

UINT CDragListBox::Dragging(CPoint pt) {
  int nIndex = ItemFromPt(pt, FALSE);
  DrawInsert(nIndex);
  return nIndex == -1 ? DL_STOPCURSOR : DL_MOVECURSOR;
}

// The item goes before the one under the cursor.
void CDragListBox::Dropped(int nSrcIndex, CPoint pt) {
  DrawInsert(-1);
  int nDest = ItemFromPt(pt, FALSE);
  if (nSrcIndex < 0 || nDest < 0 || nDest == nSrcIndex || nDest == nSrcIndex + 1) return;
  CString str;
  GetText(nSrcIndex, str);
  DWORD_PTR dwData = GetItemData(nSrcIndex);
  DeleteString(nSrcIndex);
  if (nDest > nSrcIndex) nDest--;
  nDest = InsertString(nDest, str);
  SetItemData(nDest, dwData);
  SetCurSel(nDest);
}

void CDragListBox::DrawInsert(int nItem) {
  if (m_nLast == nItem) return;
  DrawSingle(m_nLast);
  DrawSingle(nItem);
  m_nLast = nItem;
}

// A bar across the top of the item, drawn so that drawing it again takes
// it away.
void CDragListBox::DrawSingle(int nIndex) {
  if (nIndex < 0 || m_hWnd == NULL) return;
  CRect rect;
  if (GetItemRect(nIndex, &rect) == LB_ERR) return;
  CClientDC dc(this);
  CBrush brush(::GetSysColor(COLOR_WINDOWTEXT) ^ ::GetSysColor(COLOR_WINDOW));
  CBrush* pOld = dc.SelectObject(&brush);
  dc.PatBlt(rect.left, rect.top - 1, rect.Width(), 2, PATINVERT);
  dc.SelectObject(pOld);
}

BOOL CDragListBox::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  static const UINT s_nDragListMsg = ::RegisterWindowMessage(DRAGLISTMSGSTRING);
  if (message != s_nDragListMsg) return CListBox::OnChildNotify(message, wParam, lParam, pResult);
  DRAGLISTINFO* pInfo = (DRAGLISTINFO*)lParam;
  CPoint pt(pInfo->ptCursor);
  LRESULT lResult = 0;
  switch (pInfo->uNotification) {
    case DL_BEGINDRAG: lResult = BeginDrag(pt); break;
    case DL_CANCELDRAG: CancelDrag(pt); break;
    case DL_DRAGGING: lResult = Dragging(pt); break;
    case DL_DROPPED: Dropped(GetCurSel(), pt); break;
  }
  if (pResult != NULL) *pResult = lResult;
  return TRUE;
}
