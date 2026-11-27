// fptab.cpp - CMFCBaseTabCtrl and CMFCTabCtrl, and the visual manager's
// tab drawing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxtabctrl.h>
#include <afxvisualmanager.h>

UINT AFX_WM_CHANGING_ACTIVE_TAB = ::RegisterWindowMessage(_T("AFX_WM_ON_CHANGING_ACTIVE_TAB"));
UINT AFX_WM_CHANGE_ACTIVE_TAB = ::RegisterWindowMessage(_T("AFX_WM_ON_CHANGE_ACTIVE_TAB"));
UINT AFX_WM_ON_MOVETABCOMPLETE = ::RegisterWindowMessage(_T("AFX_WM_ON_MOVETABCOMPLETE"));
UINT AFX_WM_ON_RENAME_TAB = ::RegisterWindowMessage(_T("AFX_WM_ON_RENAME_TAB"));
UINT AFX_WM_ON_BEFOREMOVE_TAB = ::RegisterWindowMessage(_T("AFX_WM_ON_BEFOREMOVE_TAB"));

static const int kCloseButton = 14;  // the active tab's close button
static const int kScrollButton = 14;

// ---------------------------------------------------------------------
// The visual manager's tabs.

void CMFCVisualManager::OnEraseTabsArea(CDC* pDC, CRect rect, const CMFCBaseTabCtrl*) {
  pDC->FillSolidRect(rect, m_bFlat ? m_clrFace : afxGlobalData.clrBtnFace);
}

COLORREF CMFCVisualManager::GetTabTextColor(const CMFCBaseTabCtrl* pTabWnd, int iTab, BOOL bIsActive) {
  COLORREF clr = pTabWnd->GetTabTextColor(iTab);
  if (clr != (COLORREF)-1) return clr;
  if (bIsActive && pTabWnd->GetActiveTabTextColor() != (COLORREF)-1) return pTabWnd->GetActiveTabTextColor();
  return afxGlobalData.clrBtnText;
}

void CMFCVisualManager::OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive,
                                  const CMFCBaseTabCtrl* pTabWnd) {
  BOOL bBottom = pTabWnd->GetLocation() == CMFCBaseTabCtrl::LOCATION_BOTTOM;
  COLORREF clrBack = pTabWnd->GetTabBkColor(iTab);
  if (clrBack == (COLORREF)-1)
    clrBack = bIsActive ? (pTabWnd->GetActiveTabColor() != (COLORREF)-1 ? pTabWnd->GetActiveTabColor()
                                                                           : afxGlobalData.clrWindow)
                        : (m_bFlat ? m_clrFace : afxGlobalData.clrBtnFace);
  else if (!bIsActive)
    clrBack = AfxBlendColor(clrBack, afxGlobalData.clrBtnShadow, 15);
  CRect r = rectTab;
  if (pTabWnd->IsFlatTab()) {
    // A trapezoid with a line border.
    POINT pts[4];
    int d = r.Height() / 2;
    if (bBottom) {
      pts[0] = CPoint(r.left, r.top);
      pts[1] = CPoint(r.left + d, r.bottom - 1);
      pts[2] = CPoint(r.right - d, r.bottom - 1);
      pts[3] = CPoint(r.right, r.top);
    } else {
      pts[0] = CPoint(r.left, r.bottom - 1);
      pts[1] = CPoint(r.left + d, r.top);
      pts[2] = CPoint(r.right - d, r.top);
      pts[3] = CPoint(r.right, r.bottom - 1);
    }
    CBrush br(clrBack);
    CPen pen(PS_SOLID, 1, afxGlobalData.clrBtnDkShadow);
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);
    pDC->Polygon(pts, 4);
    pDC->SelectObject(pOldPen);
    pDC->SelectObject(pOldBr);
    return;
  }
  if (!bIsActive) r.DeflateRect(0, bBottom ? 0 : 2, 0, bBottom ? 2 : 0);
  pDC->FillSolidRect(r, clrBack);
  COLORREF clrLight = m_bFlat ? afxGlobalData.clrBtnShadow : afxGlobalData.clrBtnHilite;
  COLORREF clrDark = m_bFlat ? afxGlobalData.clrBtnShadow : afxGlobalData.clrBtnDkShadow;
  // The sides, and the edge away from the window.
  pDC->FillSolidRect(r.left, r.top, 1, r.Height(), clrLight);
  pDC->FillSolidRect(r.right - 1, r.top, 1, r.Height(), clrDark);
  if (bBottom)
    pDC->FillSolidRect(r.left, r.bottom - 1, r.Width(), 1, clrDark);
  else
    pDC->FillSolidRect(r.left, r.top, r.Width(), 1, clrLight);
  if (pTabWnd->IsVS2005Style() || pTabWnd->IsOneNoteStyle()) {
    // A slanted leading edge.
    CPen pen(PS_SOLID, 1, clrDark);
    CPen* pOld = pDC->SelectObject(&pen);
    int y0 = bBottom ? r.top : r.bottom - 1, y1 = bBottom ? r.bottom - 1 : r.top;
    pDC->MoveTo(r.left, y0);
    pDC->LineTo(r.left + 4, y1);
    pDC->SelectObject(pOld);
  }
}

void CMFCVisualManager::OnDrawTabCloseButton(CDC* pDC, CRect rect, const CMFCBaseTabCtrl*, BOOL bIsHighlighted,
                                             BOOL bIsPressed, BOOL bIsDisabled) {
  if (bIsHighlighted || bIsPressed) _FillFramed(pDC, rect, bIsPressed ? m_clrPressed : m_clrHighlight,
                                                m_clrHighlightBorder);
  CPen pen(PS_SOLID, 1, bIsDisabled ? afxGlobalData.clrGrayedText : afxGlobalData.clrBtnText);
  CPen* pOld = pDC->SelectObject(&pen);
  CRect r = rect;
  r.DeflateRect(4, 4);
  for (int i = 0; i < 2; i++) {
    pDC->MoveTo(r.left + i, r.top);
    pDC->LineTo(r.right + i, r.bottom);
    pDC->MoveTo(r.left + i, r.bottom - 1);
    pDC->LineTo(r.right + i, r.top - 1);
  }
  pDC->SelectObject(pOld);
}

// ---------------------------------------------------------------------
// CMFCBaseTabCtrl

IMPLEMENT_DYNAMIC(CMFCBaseTabCtrl, CWnd)

BEGIN_MESSAGE_MAP(CMFCBaseTabCtrl, CWnd)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_SIZE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_WM_CANCELMODE()
  ON_WM_SETFOCUS()
  ON_MESSAGE(WM_SETFONT, OnSetFont)
END_MESSAGE_MAP()

CMFCBaseTabCtrl::CMFCBaseTabCtrl()
    : m_iActiveTab(-1), m_iHighlighted(-1), m_iPressed(-1), m_nNextTabID(1), m_nTabBorderSize(1),
      m_nTabsHeight(0), m_iTabBeforeDrag(-1), m_location(LOCATION_BOTTOM), m_bEnableTabSwap(TRUE),
      m_bIsInPlaceEdit(FALSE), m_bHideSingleTab(FALSE), m_bActiveTabCloseButton(FALSE), m_bDrawFrame(TRUE),
      m_bAutoDestroyWindow(TRUE), m_bFlatFrame(FALSE), m_bIsAutoColor(FALSE), m_bDragging(FALSE),
      m_bCloseButtonPressed(FALSE), m_hImageList(NULL), m_bOwnImageList(FALSE), m_sizeImage(0, 0),
      m_clrActiveTabBk((COLORREF)-1), m_clrActiveTabFg((COLORREF)-1), m_rectTabsArea(0, 0, 0, 0),
      m_rectWndArea(0, 0, 0, 0), m_hFont(NULL) {
  // The automatic colors: soft tints.
  static const COLORREF kAuto[] = {RGB(148, 175, 230), RGB(255, 219, 117), RGB(189, 205, 159), RGB(240, 158, 159),
                                   RGB(186, 166, 225), RGB(154, 191, 180), RGB(247, 182, 131), RGB(216, 171, 192)};
  for (size_t i = 0; i < sizeof(kAuto) / sizeof(kAuto[0]); i++) m_arAutoColors.Add(kAuto[i]);
}

CMFCBaseTabCtrl::~CMFCBaseTabCtrl() {
  for (int i = 0; i < GetTabsNum(); i++) delete m_arTabs[i];
  m_arTabs.RemoveAll();
  if (m_bOwnImageList && m_hImageList != NULL) ::ImageList_Destroy(m_hImageList);
}

int CMFCBaseTabCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;
  RecalcLayout();
  return 0;
}

void CMFCBaseTabCtrl::OnDestroy() {
  if (m_bAutoDestroyWindow) {
    for (int i = 0; i < GetTabsNum(); i++) {
      CWnd* pWnd = m_arTabs[i]->m_pWnd;
      if (pWnd != NULL && pWnd->GetSafeHwnd() != NULL) pWnd->DestroyWindow();
    }
  }
  CWnd::OnDestroy();
}

void CMFCBaseTabCtrl::AddTab(CWnd* pTabWnd, LPCTSTR lpszTabLabel, UINT uiImageId, BOOL bDetachable) {
  InsertTab(pTabWnd, lpszTabLabel, GetTabsNum(), uiImageId, bDetachable);
}

void CMFCBaseTabCtrl::AddTab(CWnd* pTabWnd, UINT uiResTabLabel, UINT uiImageId, BOOL bDetachable) {
  CString str;
  str.LoadString(uiResTabLabel);
  AddTab(pTabWnd, str, uiImageId, bDetachable);
}

void CMFCBaseTabCtrl::InsertTab(CWnd* pNewWnd, LPCTSTR lpszTabLabel, int nInsertAt, UINT uiImageId,
                                BOOL bDetachable) {
  ASSERT(pNewWnd != NULL);
  if (nInsertAt < 0 || nInsertAt > GetTabsNum()) nInsertAt = GetTabsNum();
  if (pNewWnd->GetSafeHwnd() != NULL && ::GetParent(pNewWnd->m_hWnd) != m_hWnd && m_hWnd != NULL)
    pNewWnd->SetParent(this);
  CMFCTabInfo* pTab = new CMFCTabInfo(lpszTabLabel != NULL ? lpszTabLabel : _T(""), uiImageId, pNewWnd,
                                      m_nNextTabID++, bDetachable);
  m_arTabs.InsertAt(nInsertAt, pTab);
  if (m_iActiveTab >= nInsertAt) m_iActiveTab++;
  if (m_iActiveTab < 0) {
    m_iActiveTab = nInsertAt;
    FireChangeActiveTab(m_iActiveTab);
  }
  RecalcLayout();
}

BOOL CMFCBaseTabCtrl::RemoveTab(int iTab, BOOL bRecalcLayout) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  m_arTabs.RemoveAt(iTab);
  if (pTab->m_pWnd != NULL && pTab->m_pWnd->GetSafeHwnd() != NULL) {
    if (m_bAutoDestroyWindow)
      pTab->m_pWnd->DestroyWindow();
    else
      pTab->m_pWnd->ShowWindow(SW_HIDE);
  }
  delete pTab;
  int nOldActive = m_iActiveTab;
  if (GetTabsNum() == 0) {
    m_iActiveTab = -1;
  } else if (iTab < m_iActiveTab) {
    m_iActiveTab--;
  } else if (iTab == m_iActiveTab) {
    // The next visible one, or the one before.
    m_iActiveTab = -1;
    for (int i = iTab; i < GetTabsNum() && m_iActiveTab < 0; i++)
      if (m_arTabs[i]->m_bVisible) m_iActiveTab = i;
    for (int i = iTab - 1; i >= 0 && m_iActiveTab < 0; i--)
      if (m_arTabs[i]->m_bVisible) m_iActiveTab = i;
  }
  if (bRecalcLayout) RecalcLayout();
  if (iTab == nOldActive && m_iActiveTab >= 0) FireChangeActiveTab(m_iActiveTab);
  return TRUE;
}

void CMFCBaseTabCtrl::RemoveAllTabs() {
  while (GetTabsNum() > 0) RemoveTab(GetTabsNum() - 1, FALSE);
  m_iActiveTab = -1;
  RecalcLayout();
}

int CMFCBaseTabCtrl::GetVisibleTabsNum() const {
  int n = 0;
  for (int i = 0; i < GetTabsNum(); i++)
    if (m_arTabs[i]->m_bVisible) n++;
  return n;
}

BOOL CMFCBaseTabCtrl::ShowTab(int iTab, BOOL bShow, BOOL bRecalcLayout, BOOL bActivate) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_bVisible = bShow;
  if (!bShow && iTab == m_iActiveTab) {
    // Another visible tab becomes active.
    for (int i = 0; i < GetTabsNum(); i++)
      if (m_arTabs[i]->m_bVisible) {
        SetActiveTab(i);
        break;
      }
  } else if (bShow && bActivate) {
    SetActiveTab(iTab);
  }
  if (bRecalcLayout) RecalcLayout();
  return TRUE;
}

BOOL CMFCBaseTabCtrl::IsTabVisible(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL && pTab->m_bVisible;
}

BOOL CMFCBaseTabCtrl::MoveTab(int nSource, int nDest) {
  if (_Tab(nSource) == NULL || nDest < 0 || nDest >= GetTabsNum()) return FALSE;
  if (nSource == nDest) return TRUE;
  CMFCTabInfo* pTab = m_arTabs[nSource];
  m_arTabs.RemoveAt(nSource);
  m_arTabs.InsertAt(nDest, pTab);
  if (m_iActiveTab == nSource)
    m_iActiveTab = nDest;
  else if (nSource < m_iActiveTab && nDest >= m_iActiveTab)
    m_iActiveTab--;
  else if (nSource > m_iActiveTab && nDest <= m_iActiveTab)
    m_iActiveTab++;
  RecalcLayout();
  CWnd* pParent = GetParent();
  if (pParent != NULL) pParent->SendMessage(AFX_WM_ON_MOVETABCOMPLETE, (WPARAM)this, MAKELPARAM(nSource, nDest));
  return TRUE;
}

BOOL CMFCBaseTabCtrl::FireChangingActiveTab(int nNewTab) {
  CWnd* pParent = GetParent();
  if (pParent == NULL || m_hWnd == NULL) return FALSE;
  // Non-zero from the parent keeps the tab that is active.
  return pParent->SendMessage(AFX_WM_CHANGING_ACTIVE_TAB, (WPARAM)nNewTab, (LPARAM)this) != 0;
}

void CMFCBaseTabCtrl::FireChangeActiveTab(int nNewTab) {
  CWnd* pParent = GetParent();
  if (pParent == NULL || m_hWnd == NULL) return;
  pParent->SendMessage(AFX_WM_CHANGE_ACTIVE_TAB, (WPARAM)nNewTab, (LPARAM)this);
}

BOOL CMFCBaseTabCtrl::SetActiveTab(int iTab) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  if (iTab == m_iActiveTab) return TRUE;
  if (FireChangingActiveTab(iTab)) return FALSE;
  if (!pTab->m_bVisible) pTab->m_bVisible = TRUE;
  m_iActiveTab = iTab;
  RecalcLayout();
  FireChangeActiveTab(iTab);
  return TRUE;
}

CWnd* CMFCBaseTabCtrl::GetActiveWnd() const { return GetTabWnd(m_iActiveTab); }

CWnd* CMFCBaseTabCtrl::GetTabWnd(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL ? pTab->m_pWnd : NULL;
}

int CMFCBaseTabCtrl::GetTabFromHwnd(HWND hwnd) const {
  for (int i = 0; i < GetTabsNum(); i++)
    if (m_arTabs[i]->m_pWnd != NULL && m_arTabs[i]->m_pWnd->GetSafeHwnd() == hwnd) return i;
  return -1;
}

int CMFCBaseTabCtrl::GetTabFromPoint(CPoint& pt) const {
  for (int i = 0; i < GetTabsNum(); i++)
    if (m_arTabs[i]->m_bVisible && m_arTabs[i]->m_rect.PtInRect(pt)) return i;
  return -1;
}

int CMFCBaseTabCtrl::GetTabID(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL ? pTab->m_iTabID : -1;
}

int CMFCBaseTabCtrl::GetTabByID(int id) const {
  for (int i = 0; i < GetTabsNum(); i++)
    if (m_arTabs[i]->m_iTabID == id) return i;
  return -1;
}

BOOL CMFCBaseTabCtrl::GetTabLabel(int iTab, CString& strLabel) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  strLabel = pTab->m_strText;
  return TRUE;
}

BOOL CMFCBaseTabCtrl::SetTabLabel(int iTab, const CString& strLabel) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_strText = strLabel;
  RecalcLayout();
  return TRUE;
}

BOOL CMFCBaseTabCtrl::GetTabRect(int iTab, CRect& rect) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL || !pTab->m_bVisible) return FALSE;
  rect = pTab->m_rect;
  return TRUE;
}

BOOL CMFCBaseTabCtrl::SetImageList(UINT uiID, int cx, COLORREF clrTransp) {
  HIMAGELIST hList = ::ImageList_LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(uiID), cx, 0, clrTransp,
                                           IMAGE_BITMAP, LR_CREATEDIBSECTION);
  if (hList == NULL) return FALSE;
  if (m_bOwnImageList && m_hImageList != NULL) ::ImageList_Destroy(m_hImageList);
  m_hImageList = hList;
  m_bOwnImageList = TRUE;
  int cxImage = 0, cyImage = 0;
  ::ImageList_GetIconSize(hList, &cxImage, &cyImage);
  m_sizeImage = CSize(cxImage, cyImage);
  RecalcLayout();
  return TRUE;
}

BOOL CMFCBaseTabCtrl::SetImageList(HIMAGELIST hImageList) {
  if (m_bOwnImageList && m_hImageList != NULL) ::ImageList_Destroy(m_hImageList);
  m_hImageList = hImageList;
  m_bOwnImageList = FALSE;
  int cxImage = 0, cyImage = 0;
  if (hImageList != NULL) ::ImageList_GetIconSize(hImageList, &cxImage, &cyImage);
  m_sizeImage = CSize(cxImage, cyImage);
  RecalcLayout();
  return TRUE;
}

BOOL CMFCBaseTabCtrl::SetTabIcon(int iTab, UINT uiIcon) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_uiIcon = uiIcon;
  pTab->m_hIcon = NULL;
  RecalcLayout();
  return TRUE;
}

UINT CMFCBaseTabCtrl::GetTabIcon(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL ? pTab->m_uiIcon : (UINT)-1;
}

BOOL CMFCBaseTabCtrl::SetTabHicon(int iTab, HICON hIcon) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_hIcon = hIcon;
  if (hIcon != NULL && m_sizeImage.cx == 0)
    m_sizeImage = CSize(::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
  RecalcLayout();
  return TRUE;
}

HICON CMFCBaseTabCtrl::GetTabHicon(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL ? pTab->m_hIcon : NULL;
}

BOOL CMFCBaseTabCtrl::SetTabIconOnly(int iTab, BOOL bIconOnly, BOOL) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_bIconOnly = bIconOnly;
  RecalcLayout();
  return TRUE;
}

BOOL CMFCBaseTabCtrl::SetTabBkColor(int iTab, COLORREF color) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_clrBack = color;
  if (m_hWnd != NULL) Invalidate();
  return TRUE;
}

COLORREF CMFCBaseTabCtrl::GetTabBkColor(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return (COLORREF)-1;
  if (pTab->m_clrBack == (COLORREF)-1 && m_bIsAutoColor && m_arAutoColors.GetSize() > 0)
    return m_arAutoColors[iTab % m_arAutoColors.GetSize()];
  return pTab->m_clrBack;
}

BOOL CMFCBaseTabCtrl::SetTabTextColor(int iTab, COLORREF color) {
  CMFCTabInfo* pTab = _Tab(iTab);
  if (pTab == NULL) return FALSE;
  pTab->m_clrText = color;
  if (m_hWnd != NULL) Invalidate();
  return TRUE;
}

COLORREF CMFCBaseTabCtrl::GetTabTextColor(int iTab) const {
  CMFCTabInfo* pTab = _Tab(iTab);
  return pTab != NULL ? pTab->m_clrText : (COLORREF)-1;
}

void CMFCBaseTabCtrl::EnableAutoColor(BOOL bEnable) {
  m_bIsAutoColor = bEnable;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCBaseTabCtrl::SetAutoColors(const CArray<COLORREF, COLORREF>& arColors) {
  m_arAutoColors.RemoveAll();
  m_arAutoColors.Append(arColors);
  if (m_hWnd != NULL) Invalidate();
}

void CMFCBaseTabCtrl::SetActiveTabColor(COLORREF clr) {
  m_clrActiveTabBk = clr;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCBaseTabCtrl::SetActiveTabTextColor(COLORREF clr) {
  m_clrActiveTabFg = clr;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCBaseTabCtrl::SetTabBorderSize(int nTabBorderSize, BOOL bRepaint) {
  m_nTabBorderSize = nTabBorderSize;
  if (bRepaint) RecalcLayout();
}

void CMFCBaseTabCtrl::SetLocation(Location location) {
  m_location = location;
  RecalcLayout();
}

void CMFCBaseTabCtrl::HideSingleTab(BOOL bHide) {
  m_bHideSingleTab = bHide;
  RecalcLayout();
}

void CMFCBaseTabCtrl::EnableActiveTabCloseButton(BOOL bEnable) {
  m_bActiveTabCloseButton = bEnable;
  RecalcLayout();
}

void CMFCBaseTabCtrl::SetFlatFrame(BOOL bFlat, BOOL bRepaint) {
  m_bFlatFrame = bFlat;
  if (bRepaint && m_hWnd != NULL) Invalidate();
}

int CMFCBaseTabCtrl::GetTabsHeight() const {
  if (m_bHideSingleTab && GetVisibleTabsNum() <= 1) return 0;
  return m_nTabsHeight;
}

void CMFCBaseTabCtrl::GetTabArea(CRect& rectTabAreaTop, CRect& rectTabAreaBottom) const {
  rectTabAreaTop.SetRectEmpty();
  rectTabAreaBottom.SetRectEmpty();
  if (m_location == LOCATION_TOP)
    rectTabAreaTop = m_rectTabsArea;
  else
    rectTabAreaBottom = m_rectTabsArea;
}

void CMFCBaseTabCtrl::GetWndArea(CRect& rect) const { rect = m_rectWndArea; }

int CMFCBaseTabCtrl::_TabWidth(CDC* pDC, CMFCTabInfo* pTab) const {
  int cx = 2 * CMFCVisualManager::GetInstance()->GetTabHorzMargin(this) + 6;
  BOOL bImage = pTab->m_hIcon != NULL || (m_hImageList != NULL && pTab->m_uiIcon != (UINT)-1);
  if (bImage) cx += m_sizeImage.cx + 4;
  if (!pTab->m_bIconOnly || !bImage) cx += pDC->GetTextExtent(pTab->m_strText).cx;
  return cx;
}

void CMFCBaseTabCtrl::_LayoutTabs(const CRect& rectTabs) {
  CClientDC dc(this);
  CFont* pOld = dc.SelectObject(m_hFont != NULL ? CFont::FromHandle(m_hFont) : &afxGlobalData.fontRegular);
  int x = rectTabs.left;
  for (int i = 0; i < GetTabsNum(); i++) {
    CMFCTabInfo* pTab = m_arTabs[i];
    if (!pTab->m_bVisible) {
      pTab->m_rect.SetRectEmpty();
      continue;
    }
    int cx = _TabWidth(&dc, pTab);
    if (i == m_iActiveTab && m_bActiveTabCloseButton) cx += kCloseButton + 2;
    pTab->m_nFullWidth = cx;
    pTab->m_rect = CRect(x, rectTabs.top, x + cx, rectTabs.bottom);
    x += cx;
  }
  dc.SelectObject(pOld);
}

void CMFCBaseTabCtrl::RecalcLayout() {
  if (m_hWnd == NULL) return;
  CRect rectClient;
  GetClientRect(rectClient);
  // The tabs' height: the font's or the images', whichever is taller.
  {
    CClientDC dc(this);
    CFont* pOld = dc.SelectObject(m_hFont != NULL ? CFont::FromHandle(m_hFont) : &afxGlobalData.fontRegular);
    TEXTMETRIC tm;
    dc.GetTextMetrics(&tm);
    dc.SelectObject(pOld);
    int cy = tm.tmHeight;
    if (m_sizeImage.cy > cy) cy = m_sizeImage.cy;
    m_nTabsHeight = cy + 8;
  }
  int nTabsHeight = GetTabsHeight();
  m_rectTabsArea = rectClient;
  m_rectWndArea = rectClient;
  if (m_location == LOCATION_TOP) {
    m_rectTabsArea.bottom = m_rectTabsArea.top + nTabsHeight;
    m_rectWndArea.top = m_rectTabsArea.bottom;
  } else {
    m_rectTabsArea.top = m_rectTabsArea.bottom - nTabsHeight;
    m_rectWndArea.bottom = m_rectTabsArea.top;
  }
  if (m_bDrawFrame) m_rectWndArea.DeflateRect(m_nTabBorderSize + 1, m_nTabBorderSize + 1);
  if (m_rectWndArea.bottom < m_rectWndArea.top) m_rectWndArea.bottom = m_rectWndArea.top;
  if (nTabsHeight > 0) {
    CRect rectTabs = m_rectTabsArea;
    rectTabs.DeflateRect(2, 0);
    _LayoutTabs(rectTabs);
  } else {
    for (int i = 0; i < GetTabsNum(); i++) m_arTabs[i]->m_rect.SetRectEmpty();
  }
  _UpdateWindows();
  Invalidate();
}

void CMFCBaseTabCtrl::_UpdateWindows() {
  for (int i = 0; i < GetTabsNum(); i++) {
    CWnd* pWnd = m_arTabs[i]->m_pWnd;
    if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL) continue;
    if (i == m_iActiveTab) {
      pWnd->SetWindowPos(NULL, m_rectWndArea.left, m_rectWndArea.top, m_rectWndArea.Width(), m_rectWndArea.Height(),
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (pWnd->IsWindowVisible()) {
      pWnd->ShowWindow(SW_HIDE);
    }
  }
}

CRect CMFCBaseTabCtrl::_CloseButtonRect() const {
  CMFCTabInfo* pTab = _Tab(m_iActiveTab);
  if (pTab == NULL || !m_bActiveTabCloseButton || pTab->m_rect.IsRectEmpty()) return CRect(0, 0, 0, 0);
  CRect r = pTab->m_rect;
  int y = r.top + (r.Height() - kCloseButton) / 2;
  return CRect(r.right - kCloseButton - 3, y, r.right - 3, y + kCloseButton);
}

void CMFCBaseTabCtrl::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  RecalcLayout();
}

BOOL CMFCBaseTabCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

void CMFCBaseTabCtrl::OnPaint() {
  CPaintDC dcPaint(this);
  CRect rectClient;
  GetClientRect(rectClient);
  CDC dc;
  CBitmap bmp;
  CDC* pDC = &dcPaint;
  if (dc.CreateCompatibleDC(&dcPaint) && bmp.CreateCompatibleBitmap(&dcPaint, rectClient.Width(), rectClient.Height()))
    pDC = &dc;
  CBitmap* pOldBmp = pDC == &dc ? dc.SelectObject(&bmp) : NULL;
  OnDraw(pDC);
  if (pDC == &dc) {
    // The window area belongs to the tab windows: leave it out.
    dcPaint.ExcludeClipRect(m_rectWndArea);
    dcPaint.BitBlt(0, 0, rectClient.Width(), rectClient.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(pOldBmp);
  }
}

void CMFCBaseTabCtrl::OnFillBackground(CDC* pDC, CRect rect) {
  CMFCVisualManager::GetInstance()->OnEraseTabsArea(pDC, rect, this);
}

void CMFCBaseTabCtrl::OnDraw(CDC* pDC) {
  CRect rectClient;
  GetClientRect(rectClient);
  OnFillBackground(pDC, rectClient);
  if (GetActiveWnd() == NULL) pDC->FillSolidRect(m_rectWndArea, afxGlobalData.clrBtnFace);
  if (m_bDrawFrame) {
    CRect r = m_rectWndArea;
    r.InflateRect(1, 1);
    if (m_bFlatFrame)
      pDC->Draw3dRect(r, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
    else
      pDC->Draw3dRect(r, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnHilite);
  }
  if (GetTabsHeight() == 0) return;
  CFont* pOld = pDC->SelectObject(m_hFont != NULL ? CFont::FromHandle(m_hFont) : &afxGlobalData.fontRegular);
  pDC->SetBkMode(TRANSPARENT);
  for (int i = 0; i < GetTabsNum(); i++)
    if (i != m_iActiveTab && m_arTabs[i]->m_bVisible && !m_arTabs[i]->m_rect.IsRectEmpty())
      OnDrawTab(pDC, m_arTabs[i]->m_rect, i, FALSE);
  if (_Tab(m_iActiveTab) != NULL && !m_arTabs[m_iActiveTab]->m_rect.IsRectEmpty())
    OnDrawTab(pDC, m_arTabs[m_iActiveTab]->m_rect, m_iActiveTab, TRUE);
  pDC->SelectObject(pOld);
}

void CMFCBaseTabCtrl::OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive) {
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  pVM->OnDrawTab(pDC, rectTab, iTab, bIsActive, this);
  CMFCTabInfo* pTab = m_arTabs[iTab];
  CRect rect = rectTab;
  rect.DeflateRect(pVM->GetTabHorzMargin(this) + 3, 0);
  if (bIsActive && m_bActiveTabCloseButton) {
    CRect rectClose = _CloseButtonRect();
    pVM->OnDrawTabCloseButton(pDC, rectClose, this, FALSE, m_bCloseButtonPressed, FALSE);
    rect.right = rectClose.left - 2;
  }
  BOOL bImage = FALSE;
  if (pTab->m_hIcon != NULL) {
    ::DrawIconEx(pDC->GetSafeHdc(), rect.left, rect.top + (rect.Height() - m_sizeImage.cy) / 2, pTab->m_hIcon,
                 m_sizeImage.cx, m_sizeImage.cy, 0, NULL, DI_NORMAL);
    bImage = TRUE;
  } else if (m_hImageList != NULL && pTab->m_uiIcon != (UINT)-1) {
    ::ImageList_Draw(m_hImageList, (int)pTab->m_uiIcon, pDC->GetSafeHdc(), rect.left,
                     rect.top + (rect.Height() - m_sizeImage.cy) / 2, ILD_TRANSPARENT);
    bImage = TRUE;
  }
  if (bImage) rect.left += m_sizeImage.cx + 4;
  if (pTab->m_bIconOnly && bImage) return;
  pDC->SetTextColor(pVM->GetTabTextColor(this, iTab, bIsActive));
  pDC->DrawText(pTab->m_strText, rect,
                DT_SINGLELINE | DT_VCENTER | (bImage ? DT_LEFT : DT_CENTER) | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void CMFCBaseTabCtrl::OnLButtonDown(UINT nFlags, CPoint point) {
  CWnd::OnLButtonDown(nFlags, point);
  if (_CloseButtonRect().PtInRect(point)) {
    m_bCloseButtonPressed = TRUE;
    SetCapture();
    Invalidate();
    return;
  }
  int iTab = GetTabFromPoint(point);
  if (iTab < 0) return;
  if (iTab != m_iActiveTab) SetActiveTab(iTab);
  if (m_bEnableTabSwap && iTab == m_iActiveTab) {
    m_bDragging = TRUE;
    m_iTabBeforeDrag = iTab;
    SetCapture();
  }
}

void CMFCBaseTabCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  if (m_bDragging && m_iActiveTab >= 0) {
    int iTab = GetTabFromPoint(point);
    if (iTab >= 0 && iTab != m_iActiveTab) {
      CWnd* pParent = GetParent();
      if (pParent == NULL || pParent->SendMessage(AFX_WM_ON_BEFOREMOVE_TAB, (WPARAM)m_iActiveTab, 0) == 0)
        MoveTab(m_iActiveTab, iTab);
    }
  }
  CWnd::OnMouseMove(nFlags, point);
}

void CMFCBaseTabCtrl::OnLButtonUp(UINT nFlags, CPoint point) {
  if (m_bCloseButtonPressed) {
    m_bCloseButtonPressed = FALSE;
    ReleaseCapture();
    Invalidate();
    // The active tab's window closes.
    CWnd* pWnd = GetActiveWnd();
    if (_CloseButtonRect().PtInRect(point) && pWnd != NULL && pWnd->GetSafeHwnd() != NULL)
      pWnd->SendMessage(WM_CLOSE);
    return;
  }
  if (m_bDragging) {
    m_bDragging = FALSE;
    if (::GetCapture() == m_hWnd) ReleaseCapture();
  }
  CWnd::OnLButtonUp(nFlags, point);
}

void CMFCBaseTabCtrl::OnCancelMode() {
  CWnd::OnCancelMode();
  m_bDragging = FALSE;
  m_bCloseButtonPressed = FALSE;
  if (::GetCapture() == m_hWnd) ReleaseCapture();
}

void CMFCBaseTabCtrl::OnSetFocus(CWnd* pOldWnd) {
  CWnd::OnSetFocus(pOldWnd);
  CWnd* pWnd = GetActiveWnd();
  if (pWnd != NULL && pWnd->GetSafeHwnd() != NULL) pWnd->SetFocus();
}

LRESULT CMFCBaseTabCtrl::OnSetFont(WPARAM wParam, LPARAM lParam) {
  m_hFont = (HFONT)wParam;
  if (LOWORD(lParam)) RecalcLayout();
  return 0;
}

BOOL CMFCBaseTabCtrl::OnCommand(WPARAM wParam, LPARAM lParam) {
  // The tab windows' controls notify the tab control's parent, as they
  // would if it were theirs.
  CWnd* pParent = GetParent();
  if (pParent != NULL) {
    pParent->SendMessage(WM_COMMAND, wParam, lParam);
    return TRUE;
  }
  return CWnd::OnCommand(wParam, lParam);
}

BOOL CMFCBaseTabCtrl::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (CWnd::OnNotify(wParam, lParam, pResult)) return TRUE;
  CWnd* pParent = GetParent();
  if (pParent == NULL) return FALSE;
  *pResult = pParent->SendMessage(WM_NOTIFY, wParam, lParam);
  return TRUE;
}

// ---------------------------------------------------------------------
// CMFCTabCtrl

IMPLEMENT_DYNCREATE(CMFCTabCtrl, CMFCBaseTabCtrl)

BEGIN_MESSAGE_MAP(CMFCTabCtrl, CMFCBaseTabCtrl)
  ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

CMFCTabCtrl::CMFCTabCtrl()
    : m_style(STYLE_3D), m_nFirstVisibleTab(0), m_bScroll(FALSE), m_bTabDocumentsMenu(FALSE),
      m_bIsActiveTabBold(FALSE) {}

CMFCTabCtrl::~CMFCTabCtrl() {}

BOOL CMFCTabCtrl::Create(Style style, const RECT& rect, CWnd* pParentWnd, UINT nID, Location location,
                         BOOL bCloseBtn) {
  m_style = style;
  m_location = location;
  m_bActiveTabCloseButton = bCloseBtn;
  if (style == STYLE_FLAT || style == STYLE_FLAT_SHARED_HORZ_SCROLL) m_bFlatFrame = TRUE;
  LPCTSTR lpszClass = AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW),
                                          NULL, NULL);
  return CWnd::Create(lpszClass, _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rect,
                      pParentWnd, nID);
}

void CMFCTabCtrl::ModifyTabStyle(Style style) {
  m_style = style;
  m_bFlatFrame = style == STYLE_FLAT || style == STYLE_FLAT_SHARED_HORZ_SCROLL;
  RecalcLayout();
}

void CMFCTabCtrl::RecalcLayout() { CMFCBaseTabCtrl::RecalcLayout(); }

BOOL CMFCTabCtrl::SetActiveTab(int iTab) {
  if (!CMFCBaseTabCtrl::SetActiveTab(iTab)) return FALSE;
  EnsureVisible(iTab);
  return TRUE;
}

void CMFCTabCtrl::SetFirstVisibleTab(int iTab) {
  if (iTab < 0 || iTab >= GetTabsNum()) return;
  m_nFirstVisibleTab = iTab;
  RecalcLayout();
}

BOOL CMFCTabCtrl::EnsureVisible(int iTab) {
  if (iTab < 0 || iTab >= GetTabsNum()) return FALSE;
  if (!m_bScroll) return TRUE;
  if (iTab < m_nFirstVisibleTab) {
    SetFirstVisibleTab(iTab);
    return TRUE;
  }
  // Scroll forward until the tab is whole.
  CRect rectArea = m_rectTabsArea;
  rectArea.right = _ScrollRect(0).left;
  while (m_nFirstVisibleTab < iTab) {
    CRect r = m_arTabs[iTab]->m_rect;
    if (!r.IsRectEmpty() && r.right <= rectArea.right) break;
    m_nFirstVisibleTab++;
    RecalcLayout();
  }
  return TRUE;
}

CRect CMFCTabCtrl::_ScrollRect(int nButton) const {
  if (!m_bScroll) return CRect(0, 0, 0, 0);
  CRect r = m_rectTabsArea;
  int y = r.top + (r.Height() - kScrollButton) / 2;
  int x = r.right - 2 * kScrollButton - 2 + nButton * kScrollButton;
  return CRect(x, y, x + kScrollButton, y + kScrollButton);
}

void CMFCTabCtrl::_LayoutTabs(const CRect& rectTabs) {
  CClientDC dc(this);
  CFont* pOld = dc.SelectObject(m_hFont != NULL ? CFont::FromHandle(m_hFont) : &afxGlobalData.fontRegular);
  // The full widths.
  int nTotal = 0;
  for (int i = 0; i < GetTabsNum(); i++) {
    CMFCTabInfo* pTab = m_arTabs[i];
    int cx = 0;
    if (pTab->m_bVisible) {
      CFont* pTabFont = NULL;
      if (i == m_iActiveTab && m_bIsActiveTabBold) pTabFont = dc.SelectObject(&afxGlobalData.fontBold);
      cx = _TabWidth(&dc, pTab);
      if (pTabFont != NULL) dc.SelectObject(pTabFont);
      if (i == m_iActiveTab && m_bActiveTabCloseButton) cx += kCloseButton + 2;
      if (IsFlatTab() || IsVS2005Style() || IsOneNoteStyle()) cx += m_nTabsHeight / 2;
    }
    pTab->m_nFullWidth = cx;
    nTotal += cx;
  }
  dc.SelectObject(pOld);
  BOOL bCanScroll = m_style != STYLE_3D && m_style != STYLE_3D_ROUNDED;
  int nAvail = rectTabs.Width();
  m_bScroll = bCanScroll && nTotal > nAvail;
  if (!m_bScroll) m_nFirstVisibleTab = 0;
  if (m_nFirstVisibleTab >= GetTabsNum()) m_nFirstVisibleTab = 0;
  if (m_bScroll) nAvail -= 2 * kScrollButton + 4;
  // Without scrolling, the tabs shrink to fit.
  double dScale = (!m_bScroll && nTotal > nAvail && nTotal > 0) ? (double)nAvail / nTotal : 1.0;
  int x = rectTabs.left;
  int nOverlap = (IsFlatTab() || IsVS2005Style() || IsOneNoteStyle()) ? m_nTabsHeight / 4 : 0;
  for (int i = 0; i < GetTabsNum(); i++) {
    CMFCTabInfo* pTab = m_arTabs[i];
    if (!pTab->m_bVisible || i < m_nFirstVisibleTab || (m_bScroll && x >= rectTabs.left + nAvail)) {
      pTab->m_rect.SetRectEmpty();
      continue;
    }
    int cx = (int)(pTab->m_nFullWidth * dScale);
    pTab->m_rect = CRect(x, rectTabs.top, x + cx, rectTabs.bottom);
    if (m_location == LOCATION_TOP)
      pTab->m_rect.top += 1;
    else
      pTab->m_rect.bottom -= 1;
    x += cx - nOverlap;
  }
}

void CMFCTabCtrl::OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive) {
  CFont* pOld = NULL;
  if (bIsActive && m_bIsActiveTabBold) pOld = pDC->SelectObject(&afxGlobalData.fontBold);
  CMFCBaseTabCtrl::OnDrawTab(pDC, rectTab, iTab, bIsActive);
  if (pOld != NULL) pDC->SelectObject(pOld);
}

void CMFCTabCtrl::OnDraw(CDC* pDC) {
  CMFCBaseTabCtrl::OnDraw(pDC);
  if (m_bScroll && GetTabsHeight() > 0) {
    int nLast = -1;
    for (int i = GetTabsNum() - 1; i >= 0 && nLast < 0; i--)
      if (m_arTabs[i]->m_bVisible) nLast = i;
    BOOL bLastShown = nLast >= 0 && !m_arTabs[nLast]->m_rect.IsRectEmpty() &&
                      m_arTabs[nLast]->m_rect.right <= _ScrollRect(0).left;
    CRect rLeft = _ScrollRect(0), rRight = _ScrollRect(1);
    pDC->FillSolidRect(CRect(rLeft.left - 2, m_rectTabsArea.top, m_rectTabsArea.right, m_rectTabsArea.bottom),
                       afxGlobalData.clrBtnFace);
    pDC->DrawFrameControl(rLeft, DFC_SCROLL,
                          DFCS_SCROLLLEFT | DFCS_FLAT | (m_nFirstVisibleTab == 0 ? DFCS_INACTIVE : 0));
    pDC->DrawFrameControl(rRight, DFC_SCROLL, DFCS_SCROLLRIGHT | DFCS_FLAT | (bLastShown ? DFCS_INACTIVE : 0));
  }
}

void CMFCTabCtrl::OnLButtonDown(UINT nFlags, CPoint point) {
  if (m_bScroll) {
    if (_ScrollRect(0).PtInRect(point)) {
      if (m_nFirstVisibleTab > 0) SetFirstVisibleTab(m_nFirstVisibleTab - 1);
      return;
    }
    if (_ScrollRect(1).PtInRect(point)) {
      int nLast = GetTabsNum() - 1;
      if (m_nFirstVisibleTab < nLast && m_arTabs[nLast]->m_rect.IsRectEmpty())
        SetFirstVisibleTab(m_nFirstVisibleTab + 1);
      return;
    }
  }
  CMFCBaseTabCtrl::OnLButtonDown(nFlags, point);
}
