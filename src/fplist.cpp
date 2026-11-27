// fplist.cpp - CMFCHeaderCtrl and CMFCListCtrl, and the visual manager's
// header drawing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxlistctrl.h>
#include <afxvisualmanager.h>

#ifndef WM_MOUSELEAVE
#define WM_MOUSELEAVE 0x02A3
#endif

// ---------------------------------------------------------------------
// The visual manager's headers.

void CMFCVisualManager::OnFillHeaderCtrlBackground(CMFCHeaderCtrl*, CDC* pDC, CRect rect) {
  pDC->FillSolidRect(rect, m_bFlat ? m_clrFace : afxGlobalData.clrBtnFace);
}

void CMFCVisualManager::OnDrawHeaderCtrlBorder(CMFCHeaderCtrl*, CDC* pDC, CRect& rect, BOOL bIsPressed,
                                               BOOL bIsHighlighted) {
  if (m_bFlat) {
    if (bIsPressed || bIsHighlighted) {
      CRect r = rect;
      pDC->FillSolidRect(r, bIsPressed ? m_clrPressed : m_clrHighlight);
    }
    // A line on the right and at the bottom.
    pDC->FillSolidRect(rect.right - 1, rect.top + 2, 1, rect.Height() - 4, afxGlobalData.clrBtnShadow);
    pDC->FillSolidRect(rect.left, rect.bottom - 1, rect.Width(), 1, afxGlobalData.clrBtnShadow);
  } else if (bIsPressed) {
    pDC->Draw3dRect(rect, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
  } else {
    pDC->Draw3dRect(rect, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnDkShadow);
    CRect r = rect;
    r.DeflateRect(0, 0, 1, 1);
    pDC->Draw3dRect(r, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnShadow);
  }
}

void CMFCVisualManager::OnDrawHeaderCtrlSortArrow(CMFCHeaderCtrl*, CDC* pDC, CRect& rect, BOOL bIsUp) {
  // A small triangle, point up for ascending.
  int cx = rect.Width() < 8 ? rect.Width() : 8;
  int h = cx / 2 + 1;
  int x = rect.left + (rect.Width() - cx) / 2;
  int y = rect.top + (rect.Height() - h) / 2;
  CPen pen(PS_SOLID, 1, afxGlobalData.clrBtnShadow);
  CPen* pOld = pDC->SelectObject(&pen);
  for (int i = 0; i < h; i++) {
    int yy = bIsUp ? y + i : y + h - 1 - i;
    int l = x + cx / 2 - i, r = x + cx / 2 + i + 1;
    if (l < x) l = x;
    if (r > x + cx) r = x + cx;
    pDC->MoveTo(l, yy);
    pDC->LineTo(r, yy);
  }
  pDC->SelectObject(pOld);
}

// ---------------------------------------------------------------------
// CMFCHeaderCtrl

IMPLEMENT_DYNAMIC(CMFCHeaderCtrl, CHeaderCtrl)

BEGIN_MESSAGE_MAP(CMFCHeaderCtrl, CHeaderCtrl)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_CANCELMODE()
END_MESSAGE_MAP()

CMFCHeaderCtrl::CMFCHeaderCtrl()
    : m_bIsMousePressed(FALSE), m_bMultipleSort(FALSE), m_bAscending(TRUE), m_bTracked(FALSE),
      m_bIsDlgControl(FALSE), m_nHighlightedItem(-1), m_nPressedItem(-1), m_nSortColumn(-1) {}

CMFCHeaderCtrl::~CMFCHeaderCtrl() {}

void CMFCHeaderCtrl::PreSubclassWindow() {
  CHeaderCtrl::PreSubclassWindow();
  TCHAR szClass[64] = {0};
  CWnd* pParent = GetParent();
  if (pParent != NULL) {
    ::GetClassName(pParent->m_hWnd, szClass, 64);
    m_bIsDlgControl = lstrcmp(szClass, _T("#32770")) == 0;
  }
}

void CMFCHeaderCtrl::SetSortColumn(int iColumn, BOOL bAscending, BOOL bAdd) {
  if (!m_bMultipleSort || !bAdd) m_mapColumnsStatus.RemoveAll();
  if (iColumn >= 0) m_mapColumnsStatus[iColumn] = bAscending ? 1 : -1;
  m_nSortColumn = iColumn;
  m_bAscending = bAscending;
  if (m_hWnd != NULL) RedrawWindow();
}

void CMFCHeaderCtrl::RemoveSortColumn(int iColumn) {
  m_mapColumnsStatus.RemoveKey(iColumn);
  if (m_nSortColumn == iColumn) m_nSortColumn = -1;
  if (m_hWnd != NULL) RedrawWindow();
}

void CMFCHeaderCtrl::RemoveAllSortColumns() {
  m_mapColumnsStatus.RemoveAll();
  m_nSortColumn = -1;
  if (m_hWnd != NULL) RedrawWindow();
}

int CMFCHeaderCtrl::GetSortColumn() const {
  if (m_bMultipleSort) return -1;  // several: see GetColumnState
  return m_nSortColumn;
}

BOOL CMFCHeaderCtrl::IsAscending() const { return m_bAscending; }

int CMFCHeaderCtrl::GetColumnState(int iColumn) const {
  int n = 0;
  return m_mapColumnsStatus.Lookup(iColumn, n) ? n : 0;
}

void CMFCHeaderCtrl::EnableMultipleSort(BOOL bEnable) {
  if (m_bMultipleSort == bEnable) return;
  m_bMultipleSort = bEnable;
  if (!bEnable && m_mapColumnsStatus.GetCount() > 1) {
    // Back to one: the last chosen.
    int iColumn = m_nSortColumn;
    BOOL bAsc = m_bAscending;
    m_mapColumnsStatus.RemoveAll();
    if (iColumn >= 0) m_mapColumnsStatus[iColumn] = bAsc ? 1 : -1;
  }
  if (m_hWnd != NULL) RedrawWindow();
}

int CMFCHeaderCtrl::_HitTest(CPoint point) const {
  HDHITTESTINFO hti;
  memset(&hti, 0, sizeof(hti));
  hti.pt = point;
  int n = (int)::SendMessage(m_hWnd, HDM_HITTEST, 0, (LPARAM)&hti);
  return (hti.flags & HHT_ONHEADER) ? n : -1;
}

BOOL CMFCHeaderCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

void CMFCHeaderCtrl::OnPaint() {
  CPaintDC dcPaint(this);
  CRect rectClient;
  GetClientRect(rectClient);
  CDC dc;
  CBitmap bmp;
  CDC* pDC = &dcPaint;
  if (dc.CreateCompatibleDC(&dcPaint) && bmp.CreateCompatibleBitmap(&dcPaint, rectClient.Width(), rectClient.Height()))
    pDC = &dc;
  CBitmap* pOldBmp = pDC == &dc ? dc.SelectObject(&bmp) : NULL;
  OnFillBackground(pDC);
  CFont* pOldFont = pDC->SelectObject(GetFont() != NULL ? GetFont() : &afxGlobalData.fontRegular);
  pDC->SetBkMode(TRANSPARENT);
  pDC->SetTextColor(afxGlobalData.clrBtnText);
  int nCount = GetItemCount();
  int nRight = 0;
  for (int i = 0; i < nCount; i++) {
    CRect rect;
    ::SendMessage(m_hWnd, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&rect);
    if (rect.right > nRight) nRight = rect.right;
    BOOL bPressed = m_bIsMousePressed && m_nPressedItem == i && m_nHighlightedItem == i;
    OnDrawItem(pDC, i, rect, bPressed, m_nHighlightedItem == i);
  }
  // The space after the last column.
  if (nRight < rectClient.right) {
    CRect rect(nRight, rectClient.top, rectClient.right, rectClient.bottom);
    OnDrawItem(pDC, -1, rect, FALSE, FALSE);
  }
  pDC->SelectObject(pOldFont);
  if (pDC == &dc) {
    dcPaint.BitBlt(0, 0, rectClient.Width(), rectClient.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(pOldBmp);
  }
}

void CMFCHeaderCtrl::OnFillBackground(CDC* pDC) {
  CRect rect;
  GetClientRect(rect);
  CMFCVisualManager::GetInstance()->OnFillHeaderCtrlBackground(this, pDC, rect);
}

void CMFCHeaderCtrl::OnDrawItem(CDC* pDC, int iItem, CRect rect, BOOL bIsPressed, BOOL bIsHighlighted) {
  CMFCVisualManager::GetInstance()->OnDrawHeaderCtrlBorder(this, pDC, rect, bIsPressed, bIsHighlighted);
  if (iItem < 0) return;
  TCHAR szText[256] = {0};
  HDITEM hdi;
  memset(&hdi, 0, sizeof(hdi));
  hdi.mask = HDI_TEXT | HDI_FORMAT | HDI_IMAGE;
  hdi.pszText = szText;
  hdi.cchTextMax = 255;
  ::SendMessage(m_hWnd, HDM_GETITEM, (WPARAM)iItem, (LPARAM)&hdi);
  CRect rectText = rect;
  rectText.DeflateRect(6, 0);
  if (bIsPressed) rectText.OffsetRect(1, 1);
  // The image, from the header's image list.
  HIMAGELIST hImages = (HIMAGELIST)::SendMessage(m_hWnd, HDM_GETIMAGELIST, 0, 0);
  if (hImages != NULL && (hdi.fmt & HDF_IMAGE) && hdi.iImage >= 0) {
    int cx = 0, cy = 0;
    ::ImageList_GetIconSize(hImages, &cx, &cy);
    ::ImageList_Draw(hImages, hdi.iImage, pDC->GetSafeHdc(), rectText.left, rectText.top + (rectText.Height() - cy) / 2,
                     ILD_TRANSPARENT);
    rectText.left += cx + 4;
  }
  int nState = GetColumnState(iItem);
  CRect rectArrow;
  if (nState != 0) {
    rectArrow = rectText;
    rectArrow.left = rectArrow.right - 10;
    rectText.right = rectArrow.left - 4;
  }
  UINT uFormat = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
  switch (hdi.fmt & HDF_JUSTIFYMASK) {
    case HDF_RIGHT:
      uFormat |= DT_RIGHT;
      break;
    case HDF_CENTER:
      uFormat |= DT_CENTER;
      break;
    default:
      uFormat |= DT_LEFT;
  }
  if (rectText.Width() > 0) pDC->DrawText(szText, -1, rectText, uFormat);
  if (nState != 0) {
    // Right after the text where it leaves room, as Windows draws it.
    CSize size = pDC->GetTextExtent(szText, lstrlen(szText));
    if ((uFormat & DT_LEFT) == DT_LEFT && !(uFormat & (DT_RIGHT | DT_CENTER)) &&
        rectText.left + size.cx + 14 < rectArrow.left)
      rectArrow.OffsetRect(rectText.left + size.cx + 8 - rectArrow.left, 0);
    OnDrawSortArrow(pDC, rectArrow);
  }
}

void CMFCHeaderCtrl::OnDrawSortArrow(CDC* pDC, CRect rectArrow) {
  // Which way: the column whose arrow this is.
  CPoint pt = rectArrow.CenterPoint();
  int iItem = _HitTest(pt);
  BOOL bUp = iItem >= 0 ? GetColumnState(iItem) > 0 : m_bAscending;
  CMFCVisualManager::GetInstance()->OnDrawHeaderCtrlSortArrow(this, pDC, rectArrow, bUp);
}

void CMFCHeaderCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  int n = _HitTest(point);
  if (n != m_nHighlightedItem) {
    m_nHighlightedItem = n;
    RedrawWindow();
  }
  if (!m_bTracked) {
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = m_hWnd;
    tme.dwHoverTime = 0;
    m_bTracked = ::TrackMouseEvent(&tme);
  }
  CHeaderCtrl::OnMouseMove(nFlags, point);
}

LRESULT CMFCHeaderCtrl::OnMouseLeave(WPARAM, LPARAM) {
  m_bTracked = FALSE;
  if (m_nHighlightedItem >= 0) {
    m_nHighlightedItem = -1;
    RedrawWindow();
  }
  return Default();
}

void CMFCHeaderCtrl::OnLButtonDown(UINT nFlags, CPoint point) {
  m_bIsMousePressed = TRUE;
  m_nPressedItem = _HitTest(point);
  CHeaderCtrl::OnLButtonDown(nFlags, point);
}

void CMFCHeaderCtrl::OnLButtonUp(UINT nFlags, CPoint point) {
  m_bIsMousePressed = FALSE;
  m_nPressedItem = -1;
  CHeaderCtrl::OnLButtonUp(nFlags, point);
  RedrawWindow();
}

void CMFCHeaderCtrl::OnCancelMode() {
  CHeaderCtrl::OnCancelMode();
  m_bIsMousePressed = FALSE;
  m_nPressedItem = -1;
  RedrawWindow();
}

// ---------------------------------------------------------------------
// CMFCListCtrl

IMPLEMENT_DYNAMIC(CMFCListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CMFCListCtrl, CListCtrl)
  ON_WM_CREATE()
  ON_WM_STYLECHANGED()
  ON_NOTIFY_REFLECT_EX(LVN_COLUMNCLICK, OnColumnClickEx)
  ON_NOTIFY_REFLECT_EX(NM_CUSTOMDRAW, OnCustomDrawEx)
END_MESSAGE_MAP()

CMFCListCtrl::CMFCListCtrl()
    : m_iSortedColumn(-1), m_bAscending(TRUE), m_bMarkSortedColumn(FALSE), m_clrSortedColumn((COLORREF)-1),
      m_hOldFont(NULL) {}

CMFCListCtrl::~CMFCListCtrl() {}

BOOL CMFCListCtrl::InitHeader() {
  if (m_wndHeader.m_hWnd != NULL) return TRUE;
  HWND hHeader = (HWND)::SendMessage(m_hWnd, LVM_GETHEADER, 0, 0);
  if (hHeader == NULL) return FALSE;
  return m_wndHeader.SubclassWindow(hHeader);
}

void CMFCListCtrl::PreSubclassWindow() {
  CListCtrl::PreSubclassWindow();
  // Created by a dialog: the header is there now. (Created by Create,
  // OnCreate does it.)
  if (m_hWnd != NULL && (HWND)::SendMessage(m_hWnd, LVM_GETHEADER, 0, 0) != NULL) InitHeader();
}

int CMFCListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CListCtrl::OnCreate(lpCreateStruct) == -1) return -1;
  InitHeader();
  return 0;
}

void CMFCListCtrl::OnStyleChanged(int nStyleType, LPSTYLESTRUCT lpStyleStruct) {
  CListCtrl::OnStyleChanged(nStyleType, lpStyleStruct);
  InitHeader();  // a header made for the report view
}

int CMFCListCtrl::OnCompareItems(LPARAM, LPARAM, int) { return 0; }

int CALLBACK CMFCListCtrl::_CompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
  CMFCListCtrl* pThis = (CMFCListCtrl*)lParamSort;
  int n = pThis->OnCompareItems(lParam1, lParam2, pThis->m_iSortedColumn);
  return pThis->m_bAscending ? n : -n;
}

void CMFCListCtrl::Sort(int iColumn, BOOL bAscending, BOOL bAdd) {
  CWaitCursor* pWait = GetItemCount() > 1000 ? new CWaitCursor : NULL;
  m_iSortedColumn = iColumn;
  m_bAscending = bAscending;
  m_wndHeader.SetSortColumn(iColumn, bAscending, bAdd);
  SortItems(_CompareProc, (DWORD_PTR)this);
  if (m_bMarkSortedColumn) Invalidate();
  delete pWait;
}

void CMFCListCtrl::SetSortColumn(int iColumn, BOOL bAscending, BOOL bAdd) {
  m_iSortedColumn = iColumn;
  m_bAscending = bAscending;
  m_wndHeader.SetSortColumn(iColumn, bAscending, bAdd);
  if (m_bMarkSortedColumn && m_hWnd != NULL) Invalidate();
}

void CMFCListCtrl::RemoveSortColumn(int iColumn) {
  m_wndHeader.RemoveSortColumn(iColumn);
  if (m_iSortedColumn == iColumn) m_iSortedColumn = -1;
  if (m_bMarkSortedColumn && m_hWnd != NULL) Invalidate();
}

void CMFCListCtrl::EnableMultipleSort(BOOL bEnable) { m_wndHeader.EnableMultipleSort(bEnable); }

BOOL CMFCListCtrl::IsMultipleSort() const { return m_wndHeader.IsMultipleSort(); }

void CMFCListCtrl::EnableMarkSortedColumn(BOOL bMark, BOOL bRedraw) {
  m_bMarkSortedColumn = bMark;
  // A little darker than the window.
  m_clrSortedColumn = AfxBlendColor(afxGlobalData.clrWindow, afxGlobalData.clrBtnShadow, 8);
  if (bRedraw && m_hWnd != NULL) Invalidate();
}

BOOL CMFCListCtrl::OnColumnClickEx(NMHDR* pNMHDR, LRESULT* pResult) {
  NMLISTVIEW* pNMLV = (NMLISTVIEW*)pNMHDR;
  int iColumn = pNMLV->iSubItem;
  BOOL bShift = ::GetKeyState(VK_SHIFT) < 0;
  BOOL bAdd = bShift && IsMultipleSort();
  int nState = m_wndHeader.GetColumnState(iColumn);
  Sort(iColumn, nState != 1, bAdd);
  *pResult = 0;
  return FALSE;  // the parent sees it too
}

BOOL CMFCListCtrl::OnCustomDrawEx(NMHDR* pNMHDR, LRESULT* pResult) {
  NMLVCUSTOMDRAW* pCD = (NMLVCUSTOMDRAW*)pNMHDR;
  switch (pCD->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
      *pResult = CDRF_NOTIFYITEMDRAW;
      return TRUE;
    case CDDS_ITEMPREPAINT:
      *pResult = CDRF_NOTIFYSUBITEMDRAW;
      return TRUE;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
      int nRow = (int)pCD->nmcd.dwItemSpec, nColumn = pCD->iSubItem;
      COLORREF clrText = OnGetCellTextColor(nRow, nColumn);
      COLORREF clrBk = OnGetCellBkColor(nRow, nColumn);
      if (clrBk == (COLORREF)-1 && m_bMarkSortedColumn && nColumn == m_iSortedColumn) clrBk = m_clrSortedColumn;
      pCD->clrText = clrText != (COLORREF)-1 ? clrText : ::GetSysColor(COLOR_WINDOWTEXT);
      pCD->clrTextBk = clrBk != (COLORREF)-1 ? clrBk : ::GetSysColor(COLOR_WINDOW);
      if (::SendMessage(m_hWnd, LVM_GETTEXTBKCOLOR, 0, 0) != (LRESULT)CLR_NONE && clrBk == (COLORREF)-1)
        pCD->clrTextBk = (COLORREF)::SendMessage(m_hWnd, LVM_GETTEXTBKCOLOR, 0, 0);
      if (clrText == (COLORREF)-1) pCD->clrText = (COLORREF)::SendMessage(m_hWnd, LVM_GETTEXTCOLOR, 0, 0);
      HFONT hFont = OnGetCellFont(nRow, nColumn, (DWORD)pCD->nmcd.lItemlParam);
      if (hFont != NULL) {
        ::SelectObject(pCD->nmcd.hdc, hFont);
        *pResult = CDRF_NEWFONT;
      } else {
        // The list's own font, where an earlier cell changed it.
        HFONT hOwn = (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0);
        ::SelectObject(pCD->nmcd.hdc, hOwn != NULL ? hOwn : (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
        *pResult = CDRF_NEWFONT;
      }
      return TRUE;
    }
  }
  *pResult = CDRF_DODEFAULT;
  return TRUE;
}
