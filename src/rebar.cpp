// rebar.cpp - CReBar, a rebar control as a control bar, with toolbars,
// dialog bars and other windows in its bands.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxext.h>

IMPLEMENT_DYNAMIC(CReBar, CControlBar)

BEGIN_MESSAGE_MAP(CReBar, CControlBar)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_SIZE()
  ON_NOTIFY_REFLECT(RBN_HEIGHTCHANGE, OnHeightChange)
END_MESSAGE_MAP()

CReBar::CReBar() { SetBorders(); }

CReBar::~CReBar() { DestroyWindow(); }

BOOL CReBar::Create(CWnd* pParentWnd, DWORD dwCtrlStyle, DWORD dwStyle, UINT nID) {
  m_dwStyle = dwStyle & CBRS_ALL;
  // The frame's layout places and sizes the bar, not the control itself.
  DWORD dwWndStyle =
      (dwStyle & ~CBRS_ALL) | dwCtrlStyle | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE;
  if (m_dwStyle & (CBRS_ALIGN_LEFT | CBRS_ALIGN_RIGHT)) dwWndStyle |= CCS_VERT;
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_COOL_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  if (!CWnd::Create(REBARCLASSNAME, NULL, dwWndStyle, CRect(0, 0, 0, 0), pParentWnd, nID))
    return FALSE;
  REBARINFO rbi;
  memset(&rbi, 0, sizeof(rbi));
  rbi.cbSize = sizeof(rbi);
  DefWindowProc(RB_SETBARINFO, 0, (LPARAM)&rbi);
  // The bands' windows' commands and notifications go to the frame.
  if (m_hWndOwner == NULL) m_hWndOwner = pParentWnd->GetSafeHwnd();
  return TRUE;
}

// Band information, sized for the oldest comctl32 that has all its fields.
static UINT BandInfoSize() {
#ifdef REBARBANDINFO_V6_SIZE
  return REBARBANDINFO_V6_SIZE;
#else
  return sizeof(REBARBANDINFO);
#endif
}

BOOL CReBar::_AddBar(CWnd* pBar, REBARBANDINFO* pRBBI) {
  if (pBar == NULL || pBar->m_hWnd == NULL) return FALSE;
  // The band's window: its size is what it needs to show itself whole.
  CSize size;
  if (pBar->IsKindOf(RUNTIME_CLASS(CControlBar))) {
    CControlBar* pControlBar = (CControlBar*)pBar;
    BOOL bHorz = (GetStyle() & CCS_VERT) == 0;
    size = pControlBar->CalcFixedLayout(FALSE, bHorz);
    // The band holds it; it is no longer the frame's to place.
    pControlBar->m_dwStyle &= ~CBRS_ALIGN_ANY;
    if (pControlBar->m_hWndOwner == NULL)
      pControlBar->m_hWndOwner = m_hWndOwner != NULL ? m_hWndOwner : ::GetParent(m_hWnd);
  } else {
    CRect rect;
    pBar->GetWindowRect(&rect);
    size = rect.Size();
  }
  pRBBI->cbSize = BandInfoSize();
  pRBBI->fMask |= RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE | RBBIM_ID;
  pRBBI->hwndChild = pBar->m_hWnd;
  pRBBI->cxMinChild = size.cx;
  pRBBI->cyMinChild = size.cy;
  pRBBI->cx = size.cx;
  pRBBI->cxIdeal = size.cx;
  pRBBI->wID = (UINT)pBar->GetDlgCtrlID();
  if (!DefWindowProc(RB_INSERTBAND, (WPARAM)-1, (LPARAM)pRBBI)) return FALSE;
  pBar->ShowWindow(SW_SHOWNA);
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL) pFrame->RecalcLayout();
  return TRUE;
}

BOOL CReBar::AddBar(CWnd* pBar, LPCTSTR pszText, CBitmap* pbmp, DWORD dwStyle) {
  REBARBANDINFO rbbi;
  memset(&rbbi, 0, sizeof(rbbi));
  rbbi.fMask = RBBIM_STYLE;
  rbbi.fStyle = dwStyle;
  if (pszText != NULL) {
    rbbi.fMask |= RBBIM_TEXT;
    rbbi.lpText = const_cast<LPTSTR>(pszText);
  }
  if (pbmp != NULL && pbmp->GetSafeHandle() != NULL) {
    rbbi.fMask |= RBBIM_BACKGROUND;
    rbbi.hbmBack = (HBITMAP)pbmp->GetSafeHandle();
  }
  return _AddBar(pBar, &rbbi);
}

BOOL CReBar::AddBar(CWnd* pBar, COLORREF clrFore, COLORREF clrBack, LPCTSTR pszText,
                    DWORD dwStyle) {
  REBARBANDINFO rbbi;
  memset(&rbbi, 0, sizeof(rbbi));
  rbbi.fMask = RBBIM_STYLE | RBBIM_COLORS;
  rbbi.fStyle = dwStyle;
  rbbi.clrFore = clrFore;
  rbbi.clrBack = clrBack;
  if (pszText != NULL) {
    rbbi.fMask |= RBBIM_TEXT;
    rbbi.lpText = const_cast<LPTSTR>(pszText);
  }
  return _AddBar(pBar, &rbbi);
}

// The rebar's rows across the whole frame: as long as it is given, as
// deep as its rows are.
CSize CReBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  CRect rectBorders(0, 0, 0, 0);
  CalcInsideRect(rectBorders, bHorz);  // the borders, as negative sizes
  int nDepth = (int)DefWindowProc(RB_GETBARHEIGHT, 0, 0);
  if (DefWindowProc(RB_GETBANDCOUNT, 0, 0) == 0) nDepth = 0;
  CRect rect;
  GetWindowRect(&rect);
  CSize size;
  if (bHorz) {
    size.cx = bStretch ? 32767 : rect.Width();
    size.cy = nDepth - rectBorders.Height();
  } else {
    size.cx = nDepth - rectBorders.Width();
    size.cy = bStretch ? 32767 : rect.Height();
  }
  return size;
}

CSize CReBar::CalcDynamicLayout(int, DWORD dwMode) {
  BOOL bHorz = (dwMode & (LM_HORZDOCK | LM_VERTDOCK)) ? (dwMode & LM_HORZDOCK) != 0
                                                      : (dwMode & LM_HORZ) != 0;
  return CalcFixedLayout((dwMode & LM_STRETCH) != 0, bHorz);
}

// Toolbars and dialog bars in the bands update themselves; other windows
// in them are the frame's controls.
void CReBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  for (CWnd* pChild = GetWindow(GW_CHILD); pChild != NULL; pChild = pChild->GetNextWindow()) {
    CWnd* pPerm = CWnd::FromHandlePermanent(pChild->m_hWnd);
    if (pPerm != NULL && pPerm->IsKindOf(RUNTIME_CLASS(CControlBar))) continue;
    UINT nID = (UINT)pChild->GetDlgCtrlID();
    if (nID == 0 || nID > 0xFFFF) continue;
    CCmdUI state;
    state.m_pOther = pChild;
    state.m_nID = nID;
    state.m_nIndex = 0;
    state.m_nIndexMax = 1;
    state.DoUpdate(pTarget, bDisableIfNoHndler && (::SendMessage(pChild->m_hWnd, WM_GETDLGCODE,
                                                                  0, 0) &
                                                   DLGC_BUTTON) != 0);
  }
}

INT_PTR CReBar::OnToolHitTest(CPoint point, TOOLINFO* pTI) const {
  return CControlBar::OnToolHitTest(point, pTI);
}

void CReBar::OnPaint() { Default(); }

BOOL CReBar::OnEraseBkgnd(CDC*) { return (BOOL)Default(); }

void CReBar::OnSize(UINT nType, int cx, int cy) {
  CControlBar::OnSize(nType, cx, cy);
}

// More or fewer rows: the frame lays out its bars and view again.
void CReBar::OnHeightChange(NMHDR*, LRESULT* pResult) {
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL) pFrame->RecalcLayout();
  *pResult = 0;
}
