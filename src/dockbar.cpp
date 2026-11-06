// dockbar.cpp - CDockBar, and the frame's side of docking: the dock bars
// along its edges, docking and floating bars, and finding where a bar
// being dragged would dock.
//
// A frame that calls EnableDocking gets one dock bar per side it allows.
// Docked control bars are children of a dock bar, which lays them out in
// rows. A floating bar is the child of the dock bar inside a small frame
// (CMiniDockFrameWnd) owned by the frame. Either way the bar's dock site
// is the frame, and the frame stays the bar's owner, so its commands and
// command updates go where they did.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>
#include <afxpriv.h>

IMPLEMENT_DYNAMIC(CDockBar, CControlBar)

BEGIN_MESSAGE_MAP(CDockBar, CControlBar)
  ON_WM_NCCALCSIZE()
  ON_WM_NCPAINT()
  ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
END_MESSAGE_MAP()

// The dock bars, in the order the frame lays them out: the top and
// bottom ones span the frame, the side ones fit between them.
const DWORD CFrameWnd::dwDockBarMap[4][2] = {
    {AFX_IDW_DOCKBAR_TOP, CBRS_TOP},
    {AFX_IDW_DOCKBAR_BOTTOM, CBRS_BOTTOM},
    {AFX_IDW_DOCKBAR_LEFT, CBRS_LEFT},
    {AFX_IDW_DOCKBAR_RIGHT, CBRS_RIGHT},
};

CDockBar::CDockBar(BOOL bFloating) {
  m_bFloating = bFloating;
  m_bAutoDelete = FALSE;
  m_bLayoutQuery = FALSE;
  m_rectLayout.SetRectEmpty();
  m_cxLeftBorder = m_cxRightBorder = m_cyTopBorder = m_cyBottomBorder = 0;
  m_cxDefaultGap = 0;
  m_arrBars.Add(NULL);
}

CDockBar::~CDockBar() {
  // The bars outlive their dock bar (they belong to the program).
  for (int i = 0; i < m_arrBars.GetSize(); i++) {
    CControlBar* pBar = GetDockedControlBar(i);
    if (pBar != NULL && pBar->m_pDockBar == this) pBar->m_pDockBar = NULL;
  }
  DestroyWindow();
}

BOOL CDockBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  // The dock bar itself has no borders; the bars in it have their own.
  m_dwStyle = dwStyle & CBRS_ALL & ~CBRS_BORDER_ANY;
  return CWnd::Create(NULL, NULL, (dwStyle & ~CBRS_ALL) | WS_CLIPCHILDREN, CRect(0, 0, 0, 0),
                      pParentWnd, nID);
}

CControlBar* CDockBar::GetDockedControlBar(int nPos) const {
  // Small values are placeholders, not bars.
  void* p = m_arrBars[nPos];
  if ((UINT_PTR)p <= 0xFFFF) return NULL;
  return (CControlBar*)p;
}

int CDockBar::GetDockedCount() const {
  int nCount = 0;
  for (int i = 0; i < m_arrBars.GetSize(); i++)
    if (GetDockedControlBar(i) != NULL) nCount++;
  return nCount;
}

int CDockBar::GetDockedVisibleCount() const {
  int nCount = 0;
  for (int i = 0; i < m_arrBars.GetSize(); i++) {
    CControlBar* pBar = GetDockedControlBar(i);
    if (pBar != NULL && pBar->m_hWnd != NULL && (pBar->GetStyle() & WS_VISIBLE)) nCount++;
  }
  return nCount;
}

int CDockBar::FindBar(CControlBar* pBar, int nPosExclude) {
  for (int i = 0; i < m_arrBars.GetSize(); i++)
    if (i != nPosExclude && m_arrBars[i] == pBar) return i;
  return -1;
}

void CDockBar::ShowAll(BOOL bShow) {
  for (int i = 0; i < m_arrBars.GetSize(); i++) {
    CControlBar* pBar = GetDockedControlBar(i);
    if (pBar == NULL || pBar->m_hWnd == NULL) continue;
    CFrameWnd* pFrame = pBar->GetDockingFrame();
    if (pFrame != NULL)
      pFrame->ShowControlBar(pBar, bShow, FALSE);
    else
      pBar->DelayShow(bShow);
  }
}

// The size a bar wants in this dock bar.
CSize CDockBar::_CalcBarSize(CControlBar* pBar, BOOL bHorz) {
  if (m_bFloating) {
    if (pBar->m_dwStyle & CBRS_SIZE_DYNAMIC) return pBar->CalcDynamicLayout(-1, LM_HORZ | LM_MRUWIDTH);
    return pBar->CalcFixedLayout(FALSE, (pBar->m_dwStyle & CBRS_ORIENT_HORZ) != 0);
  }
  if (pBar->m_dwStyle & CBRS_SIZE_DYNAMIC)
    return pBar->CalcDynamicLayout(-1, bHorz ? LM_HORZ | LM_HORZDOCK : LM_VERTDOCK);
  // A fixed bar may ask for the whole length (a dialog bar does).
  return pBar->CalcFixedLayout(TRUE, bHorz);
}

// Lays the docked bars out in rows. Each bar keeps its place along the
// row where it can (so bars stay where they were dragged), and bars that
// would run past the end are pushed back. Unless m_bLayoutQuery is set,
// the bars are moved there.
CSize CDockBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  (void)bHorz;
  BOOL bHorzBar = (m_dwStyle & CBRS_ORIENT_HORZ) != 0;

  if (m_bFloating) {
    // A floating dock bar holds one bar and is exactly its size.
    for (int i = 0; i < m_arrBars.GetSize(); i++) {
      CControlBar* pBar = GetDockedControlBar(i);
      if (pBar == NULL || pBar->m_hWnd == NULL || !(pBar->GetStyle() & WS_VISIBLE)) continue;
      CSize size = _CalcBarSize(pBar, bHorzBar);
      if (!m_bLayoutQuery)
        pBar->SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
      return size;
    }
    return CSize(0, 0);
  }

  int nLengthAvail = bHorzBar ? m_rectLayout.Width() : m_rectLayout.Height();
  if (nLengthAvail <= 0) nLengthAvail = 32767;

  struct Placed {
    CControlBar* pBar;
    CRect rect;
  };
  CArray<Placed, Placed&> arrPlaced;
  int nOffset = 0;      // where the current row starts (across the bar)
  int nMaxLength = 0;   // the longest row
  int nRowStart = 0;    // index in arrPlaced of the row's first bar
  int nRowThick = 0;
  int nPos = 0;         // the end of the last bar in the row

  for (int i = 1; i < m_arrBars.GetSize(); i++) {
    void* p = m_arrBars[i];
    if (p == NULL) {
      // End of a row: pull back bars that run off the end.
      if (nPos > nLengthAvail) {
        int nLimit = nLengthAvail;
        for (int j = (int)arrPlaced.GetSize() - 1; j >= nRowStart; j--) {
          CRect& r = arrPlaced[j].rect;
          int nEnd = bHorzBar ? r.right : r.bottom;
          if (nEnd > nLimit) {
            int d = nLimit - nEnd;
            if (bHorzBar)
              r.OffsetRect(d, 0);
            else
              r.OffsetRect(0, d);
          }
          nLimit = bHorzBar ? r.left : r.top;
        }
        nPos = nLengthAvail;
      }
      nMaxLength = max(nMaxLength, nPos);
      nOffset += nRowThick;
      nRowThick = 0;
      nPos = 0;
      nRowStart = (int)arrPlaced.GetSize();
      continue;
    }
    CControlBar* pBar = GetDockedControlBar(i);
    if (pBar == NULL || pBar->m_hWnd == NULL || !(pBar->GetStyle() & WS_VISIBLE)) continue;

    CSize size = _CalcBarSize(pBar, bHorzBar);
    int nLength = bHorzBar ? size.cx : size.cy;
    int nThick = bHorzBar ? size.cy : size.cx;
    if (nLength > nLengthAvail) nLength = nLengthAvail;

    // Where the bar is now, along the row.
    CRect rectBar;
    pBar->GetWindowRect(&rectBar);
    ScreenToClient(&rectBar);
    int nWant = bHorzBar ? rectBar.left : rectBar.top;
    int nStart = max(nPos, nWant);
    if (nStart + nLength > nLengthAvail) nStart = max(nPos, nLengthAvail - nLength);

    Placed placed;
    placed.pBar = pBar;
    if (bHorzBar)
      placed.rect.SetRect(nStart, nOffset, nStart + nLength, nOffset + nThick);
    else
      placed.rect.SetRect(nOffset, nStart, nOffset + nThick, nStart + nLength);
    arrPlaced.Add(placed);
    nPos = nStart + nLength;
    nRowThick = max(nRowThick, nThick);
  }
  nMaxLength = max(nMaxLength, nPos);
  nOffset += nRowThick;

  if (!m_bLayoutQuery) {
    for (int j = 0; j < arrPlaced.GetSize(); j++) {
      CControlBar* pBar = arrPlaced[j].pBar;
      CRect& r = arrPlaced[j].rect;
      CRect rectOld;
      pBar->GetWindowRect(&rectOld);
      ScreenToClient(&rectOld);
      if (rectOld != r)
        pBar->SetWindowPos(NULL, r.left, r.top, r.Width(), r.Height(),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
  }

  CSize size;
  if (bHorzBar) {
    size.cx = bStretch ? 32767 : nMaxLength;
    size.cy = nOffset;
  } else {
    size.cx = nOffset;
    size.cy = bStretch ? 32767 : nMaxLength;
  }
  return size;
}

LRESULT CDockBar::OnSizeParent(WPARAM wParam, LPARAM lParam) {
  AFX_SIZEPARENTPARAMS* lpLayout = (AFX_SIZEPARENTPARAMS*)lParam;
  m_rectLayout = lpLayout->rect;
  m_bLayoutQuery = lpLayout->hDWP == NULL;
  LRESULT lResult = CControlBar::OnSizeParent(wParam, lParam);
  m_bLayoutQuery = FALSE;
  return lResult;
}

void CDockBar::OnNcCalcSize(BOOL, NCCALCSIZE_PARAMS*) {}  // no non-client area

void CDockBar::OnNcPaint() {}

void CDockBar::DoPaint(CDC* pDC) { (void)pDC; }

void CDockBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  // The bars in it update themselves.
  (void)pTarget;
  (void)bDisableIfNoHndler;
}

// Picks the place in m_arrBars for a bar dropped at rect (client
// coordinates): into the row it overlaps most, in order along the row,
// or into a new row above or below the others.
int CDockBar::Insert(CControlBar* pBar, CRect rect, CPoint ptMid) {
  (void)pBar;
  BOOL bHorzBar = (m_dwStyle & CBRS_ORIENT_HORZ) != 0;
  int nMid = bHorzBar ? ptMid.y : ptMid.x;
  int nAlong = bHorzBar ? rect.left : rect.top;

  int nRowFirst = 1;  // index of the row's first entry
  for (int i = 1; i < m_arrBars.GetSize(); i++) {
    if (m_arrBars[i] != NULL) continue;
    // The row is nRowFirst..i-1; find how far across it reaches.
    int nTop = INT_MAX, nBottom = INT_MIN;
    for (int j = nRowFirst; j < i; j++) {
      CControlBar* pRowBar = GetDockedControlBar(j);
      if (pRowBar == NULL || pRowBar->m_hWnd == NULL || !(pRowBar->GetStyle() & WS_VISIBLE)) continue;
      CRect r;
      pRowBar->GetWindowRect(&r);
      ScreenToClient(&r);
      nTop = min(nTop, (int)(bHorzBar ? r.top : r.left));
      nBottom = max(nBottom, (int)(bHorzBar ? r.bottom : r.right));
    }
    if (nTop <= nBottom) {
      if (nMid < nTop) return -nRowFirst;  // a new row before this one
      if (nMid >= nTop && nMid < nBottom) {
        // Into this row, before the first bar that starts after it.
        for (int j = nRowFirst; j < i; j++) {
          CControlBar* pRowBar = GetDockedControlBar(j);
          if (pRowBar == NULL || pRowBar->m_hWnd == NULL) continue;
          CRect r;
          pRowBar->GetWindowRect(&r);
          ScreenToClient(&r);
          if ((bHorzBar ? r.left : r.top) > nAlong) return j;
        }
        return i;
      }
    }
    nRowFirst = i + 1;
  }
  return 0;  // a new last row
}

// Gives a bar joining this dock bar its parent, style and position.
void CDockBar::_AttachBar(CControlBar* pBar) {
  DWORD dwStyle = pBar->m_dwStyle & ~(CBRS_ALIGN_ANY | CBRS_BORDER_ANY | CBRS_FLOATING);
  dwStyle |= m_dwStyle & CBRS_ALIGN_ANY;
  if (m_bFloating) {
    dwStyle |= CBRS_FLOATING;
  } else {
    dwStyle |= CBRS_BORDER_3D;
    if (m_dwStyle & CBRS_ORIENT_HORZ)
      dwStyle |= CBRS_BORDER_TOP | CBRS_BORDER_BOTTOM;
    else
      dwStyle |= CBRS_BORDER_LEFT | CBRS_BORDER_RIGHT;
  }
  if (pBar->m_hWndOwner == NULL) pBar->m_hWndOwner = ::GetParent(pBar->m_hWnd);
  pBar->m_pDockBar = this;
  if (pBar->GetParent() != this) pBar->SetParent(this);
  pBar->SetBarStyle(dwStyle);
  if (pBar->IsKindOf(RUNTIME_CLASS(CToolBar))) ((CToolBar*)pBar)->m_bDelayedButtonLayout = TRUE;
}

void CDockBar::DockControlBar(CControlBar* pBar, LPCRECT lpRect) {
  if (pBar->m_pDockBar != NULL) pBar->m_pDockBar->RemoveControlBar(pBar);

  CRect rect;
  int nPos;
  if (lpRect != NULL && !m_bFloating) {
    rect = *lpRect;
    ScreenToClient(&rect);
    nPos = Insert(pBar, rect, rect.CenterPoint());
  } else {
    rect.SetRectEmpty();
    nPos = 0;
  }

  if (nPos > 0) {
    m_arrBars.InsertAt(nPos, pBar);
  } else if (nPos < 0) {
    // A new row before the row that starts at -nPos.
    int nAt = -nPos;
    m_arrBars.InsertAt(nAt, (void*)NULL);
    m_arrBars.InsertAt(nAt, pBar);
  } else {
    // A new last row.
    if (m_arrBars[m_arrBars.GetSize() - 1] != NULL) m_arrBars.Add(NULL);
    m_arrBars.Add(pBar);
    m_arrBars.Add(NULL);
  }

  _AttachBar(pBar);

  // The layout keeps a bar where it is along its row, so put it where it
  // was dropped (or at the start of the row).
  CPoint pt = (lpRect != NULL && !m_bFloating) ? rect.TopLeft() : CPoint(0, 0);
  pBar->SetWindowPos(NULL, pt.x, pt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

  if (m_bFloating) {
    CWnd* pParent = GetParent();
    if (pParent != NULL && pParent->IsFrameWnd()) ((CFrameWnd*)pParent)->RecalcLayout();
  }
}

void CDockBar::ReDockControlBar(CControlBar* pBar, LPCRECT lpRect) { DockControlBar(pBar, lpRect); }

BOOL CDockBar::RemoveControlBar(CControlBar* pBar, int nPosExclude, int nAddPlaceHolder) {
  (void)nAddPlaceHolder;
  int nPos = FindBar(pBar, nPosExclude);
  if (nPos <= 0) return FALSE;
  m_arrBars.RemoveAt(nPos);
  // Drop the row if it is now empty.
  if (m_arrBars[nPos - 1] == NULL && nPos < m_arrBars.GetSize() && m_arrBars[nPos] == NULL)
    m_arrBars.RemoveAt(nPos);
  if (pBar->m_pDockBar == this) pBar->m_pDockBar = NULL;

  if (m_bFloating && GetDockedCount() == 0) {
    // The floating frame goes away once it is empty, but not from inside
    // whatever message brought us here.
    CWnd* pParent = GetParent();
    if (pParent != NULL && pParent->m_hWnd != NULL) {
      pParent->ShowWindow(SW_HIDE);
      pParent->PostMessage(WM_RESERVED_036F);
    }
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// The frame's side

void CFrameWnd::EnableDocking(DWORD dwDockStyle) {
  m_dwDockStyle |= dwDockStyle & CBRS_ALIGN_ANY;
  if (m_pFloatingFrameClass == NULL) m_pFloatingFrameClass = RUNTIME_CLASS(CMiniDockFrameWnd);
  for (int i = 0; i < 4; i++) {
    if (!(dwDockBarMap[i][1] & dwDockStyle & CBRS_ALIGN_ANY)) continue;
    if (GetControlBar(dwDockBarMap[i][0]) != NULL) continue;
    CDockBar* pDock = new CDockBar;
    pDock->m_bAutoDelete = TRUE;
    if (!pDock->Create(this, WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CHILD | WS_VISIBLE |
                                 dwDockBarMap[i][1],
                       dwDockBarMap[i][0])) {
      delete pDock;
      AfxThrowResourceException();
    }
  }
}

void CFrameWnd::DockControlBar(CControlBar* pBar, UINT nDockBarID, LPCRECT lpRect) {
  CDockBar* pDockBar = NULL;
  if (nDockBarID != 0) {
    pDockBar = (CDockBar*)GetControlBar(nDockBarID);
  } else {
    DWORD dwAllowed = pBar->m_dwDockStyle & CBRS_ALIGN_ANY;
    for (int i = 0; i < 4 && pDockBar == NULL; i++)
      if (dwDockBarMap[i][1] & dwAllowed) pDockBar = (CDockBar*)GetControlBar(dwDockBarMap[i][0]);
  }
  if (pDockBar != NULL && pDockBar->IsDockBar()) {
    DockControlBar(pBar, pDockBar, lpRect);
    return;
  }

  // No dock bars (the frame never called EnableDocking): the bar stays a
  // child of the frame and the frame's layout puts it on that side.
  DWORD dwAlign = 0;
  for (int i = 0; i < 4; i++)
    if (dwDockBarMap[i][0] == nDockBarID) dwAlign = dwDockBarMap[i][1] & CBRS_ALIGN_ANY;
  if (dwAlign == 0) {
    DWORD dwAllowed = pBar->m_dwDockStyle & CBRS_ALIGN_ANY;
    if (dwAllowed == 0) dwAllowed = CBRS_ALIGN_ANY;
    for (int i = 0; i < 4 && dwAlign == 0; i++)
      if (dwDockBarMap[i][1] & dwAllowed) dwAlign = dwDockBarMap[i][1] & CBRS_ALIGN_ANY;
  }
  DWORD dwStyle = pBar->m_dwStyle & ~(CBRS_ALIGN_ANY | CBRS_BORDER_ANY | CBRS_FLOATING);
  dwStyle |= dwAlign | CBRS_BORDER_3D;
  if (dwAlign & CBRS_ORIENT_HORZ)
    dwStyle |= CBRS_BORDER_TOP | CBRS_BORDER_BOTTOM;
  else
    dwStyle |= CBRS_BORDER_LEFT | CBRS_BORDER_RIGHT;
  pBar->m_pDockSite = this;
  pBar->SetBarStyle(dwStyle);
  if (m_hWnd != NULL) RecalcLayout();
}

void CFrameWnd::DockControlBar(CControlBar* pBar, CDockBar* pDockBar, LPCRECT lpRect) {
  CDockContext* pContext = pBar->m_pDockContext;
  if (pBar->IsFloating() && pContext != NULL) {
    // Remember where it floated, for ToggleDocking.
    CRect rect;
    pBar->m_pDockBar->GetParent()->GetWindowRect(&rect);
    pContext->m_ptMRUFloatPos = rect.TopLeft();
    pContext->m_dwMRUFloatStyle = pBar->m_pDockBar->m_dwStyle & CBRS_ALIGN_ANY;
  }
  pBar->m_pDockSite = this;
  pDockBar->DockControlBar(pBar, lpRect);
  if (pContext != NULL && !pDockBar->m_bFloating) {
    pContext->m_uMRUDockID = pDockBar->GetDlgCtrlID();
    pContext->m_rectMRUDockPos.SetRectEmpty();
  }
  RecalcLayout();
}

void CFrameWnd::ReDockControlBar(CControlBar* pBar, CDockBar* pDockBar, LPCRECT lpRect) {
  DockControlBar(pBar, pDockBar, lpRect);
}

CMiniDockFrameWnd* CFrameWnd::CreateFloatingFrame(DWORD dwStyle) {
  CRuntimeClass* pClass =
      m_pFloatingFrameClass != NULL ? m_pFloatingFrameClass : RUNTIME_CLASS(CMiniDockFrameWnd);
  CMiniDockFrameWnd* pFrame = (CMiniDockFrameWnd*)pClass->CreateObject();
  if (pFrame == NULL) AfxThrowMemoryException();
  if (!pFrame->Create(this, dwStyle)) {
    delete pFrame;
    AfxThrowResourceException();
  }
  return pFrame;
}

void CFrameWnd::FloatControlBar(CControlBar* pBar, CPoint point, DWORD dwStyle) {
  CDockContext* pContext = pBar->m_pDockContext;
  DWORD dwAlign = dwStyle & CBRS_ALIGN_ANY;
  if (dwAlign == 0) dwAlign = CBRS_ALIGN_TOP;

  if (pBar->IsFloating() && pBar->m_pDockBar->GetDockedCount() == 1 &&
      (pBar->m_pDockBar->m_dwStyle & CBRS_ALIGN_ANY) == dwAlign) {
    // Already floating by itself: just move its frame.
    CWnd* pFrame = pBar->m_pDockBar->GetParent();
    pFrame->SetWindowPos(NULL, point.x, point.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    if (pContext != NULL && pBar->m_pDockBar != NULL && !pBar->m_pDockBar->m_bFloating) {
      // Remember where it was docked, for ToggleDocking.
      CRect rect;
      pBar->GetWindowRect(&rect);
      pBar->m_pDockBar->ScreenToClient(&rect);
      pContext->m_uMRUDockID = pBar->m_pDockBar->GetDlgCtrlID();
      pContext->m_rectMRUDockPos = rect;
    }
    CMiniDockFrameWnd* pFrame = CreateFloatingFrame(dwAlign | (pBar->m_dwStyle & CBRS_SIZE_DYNAMIC));
    pFrame->SetWindowPos(NULL, point.x, point.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    pBar->m_pDockSite = this;
    pFrame->m_wndDockBar.DockControlBar(pBar);
    pFrame->RecalcLayout();
  }
  if (pContext != NULL) {
    pContext->m_ptMRUFloatPos = point;
    pContext->m_dwMRUFloatStyle = dwAlign;
  }
  RecalcLayout();
}

DWORD CFrameWnd::CanDock(CRect rect, DWORD dwDockStyle, CDockBar** ppDockBar) {
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (!pBar->IsDockBar() || ((CDockBar*)pBar)->m_bFloating) continue;
    if (!(pBar->m_dwStyle & dwDockStyle & CBRS_ALIGN_ANY)) continue;
    if (pBar->m_hWnd == NULL || !pBar->IsWindowVisible()) continue;
    CRect rectBar;
    pBar->GetWindowRect(&rectBar);
    // An empty dock bar is a line along the edge; give it some width.
    if (rectBar.Width() == 0) rectBar.right++;
    if (rectBar.Height() == 0) rectBar.bottom++;
    CRect rectHit;
    if (rectHit.IntersectRect(&rect, &rectBar)) {
      if (ppDockBar != NULL) *ppDockBar = (CDockBar*)pBar;
      return pBar->m_dwStyle & CBRS_ALIGN_ANY;
    }
  }
  return 0;
}

void CFrameWnd::NotifyFloatingWindows(DWORD dwFlags) {
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (!pBar->IsDockBar() || !((CDockBar*)pBar)->m_bFloating || pBar->m_hWnd == NULL) continue;
    CWnd* pFrame = pBar->GetParent();
    if (pFrame != NULL) pFrame->SendMessage(WM_FLOATSTATUS, dwFlags);
  }
}

// Idle-time updates for the floating frames, which are not children of
// the frame and so are not reached through it.
void CFrameWnd::_UpdateFloatingFrames() {
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (!pBar->IsDockBar() || !((CDockBar*)pBar)->m_bFloating || pBar->m_hWnd == NULL) continue;
    CWnd* pFrame = pBar->GetParent();
    if (pFrame == NULL || !pFrame->IsWindowVisible()) continue;
    pFrame->SendMessage(WM_IDLEUPDATECMDUI, (WPARAM)TRUE);
    pFrame->SendMessageToDescendants(WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0, TRUE, TRUE);
  }
}

void CFrameWnd::DestroyDockBars() {
  // Floating frames are owned windows and go with the frame; docked bars
  // go with their dock bars.
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (pBar->IsDockBar() && ((CDockBar*)pBar)->m_bFloating && pBar->m_hWnd != NULL) {
      CWnd* pFrame = pBar->GetParent();
      if (pFrame != NULL && pFrame != this) pFrame->DestroyWindow();
      pos = m_listControlBars.GetHeadPosition();  // the list changed
    }
  }
}
