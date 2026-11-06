// winmini.cpp - CMiniFrameWnd, the small-captioned frame, and
// CMiniDockFrameWnd, the frame a floating control bar lives in.
//
// The mini frame is a tool window, so the system draws its small caption
// and close button the way it draws every palette on the desktop.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>
#include <afxpriv.h>

// Posted to a floating frame once its last bar has left it.
static const UINT WM_AFX_DESTROYFLOAT = WM_RESERVED_036F;

IMPLEMENT_DYNCREATE(CMiniFrameWnd, CFrameWnd)
IMPLEMENT_DYNCREATE(CMiniDockFrameWnd, CMiniFrameWnd)

BEGIN_MESSAGE_MAP(CMiniFrameWnd, CFrameWnd)
  ON_WM_NCACTIVATE()
  ON_WM_GETMINMAXINFO()
  ON_MESSAGE(WM_FLOATSTATUS, OnFloatStatus)
END_MESSAGE_MAP()

CMiniFrameWnd::CMiniFrameWnd() {
  m_bSysTracking = FALSE;
  m_bInSys = FALSE;
  m_bActive = FALSE;
}

CMiniFrameWnd::~CMiniFrameWnd() {}

BOOL CMiniFrameWnd::Create(LPCTSTR lpClassName, LPCTSTR lpWindowName, DWORD dwStyle,
                           const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return CreateEx(0, lpClassName, lpWindowName, dwStyle, rect, pParentWnd, nID);
}

BOOL CMiniFrameWnd::CreateEx(DWORD dwExStyle, LPCTSTR lpClassName, LPCTSTR lpWindowName,
                             DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  m_strCaption = lpWindowName;
  m_strTitle = lpWindowName;
  return CWnd::CreateEx(dwExStyle, lpClassName, lpWindowName, dwStyle, rect.left, rect.top,
                        rect.right - rect.left, rect.bottom - rect.top, pParentWnd->GetSafeHwnd(),
                        (HMENU)(UINT_PTR)((dwStyle & WS_CHILD) ? nID : 0), NULL);
}

BOOL CMiniFrameWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WND_REG)) return FALSE;
    cs.lpszClass = _afxWnd;
  }
  // The MFS_ styles become the window styles that do the same.
  if (cs.style & (MFS_THICKFRAME | MFS_4THICKFRAME)) cs.style |= WS_THICKFRAME;
  if (cs.style & WS_CAPTION) cs.dwExStyle |= WS_EX_TOOLWINDOW;
  if (cs.style & WS_CHILD) cs.style &= ~(MFS_SYNCACTIVATE);
  return TRUE;
}

void PASCAL CMiniFrameWnd::CalcBorders(LPRECT lpClientRect, DWORD dwStyle, DWORD dwExStyle) {
  if (dwStyle & (MFS_THICKFRAME | MFS_4THICKFRAME)) dwStyle |= WS_THICKFRAME;
  ::AdjustWindowRectEx(lpClientRect, dwStyle & ~0xFFFF, FALSE, dwExStyle | WS_EX_TOOLWINDOW);
}

// With MFS_SYNCACTIVATE the caption looks active whenever the frame that
// owns it is active.
BOOL CMiniFrameWnd::OnNcActivate(BOOL bActive) {
  if (GetStyle() & MFS_SYNCACTIVATE) {
    HWND hActive = ::GetActiveWindow();
    HWND hOwner = ::GetWindow(m_hWnd, GW_OWNER);
    bActive = bActive || (hActive != NULL && (hActive == hOwner || hActive == m_hWnd));
    m_bActive = bActive;
  }
  return (BOOL)DefWindowProc(WM_NCACTIVATE, bActive, 0);
}

LRESULT CMiniFrameWnd::OnFloatStatus(WPARAM wParam, LPARAM) {
  if ((GetStyle() & MFS_SYNCACTIVATE) && (wParam & (FS_ACTIVATE | FS_DEACTIVATE))) {
    m_bActive = (wParam & FS_ACTIVATE) != 0;
    DefWindowProc(WM_NCACTIVATE, m_bActive, 0);
  }
  if (wParam & (FS_ENABLE | FS_DISABLE)) EnableWindow((wParam & FS_ENABLE) != 0);
  return TRUE;
}

void CMiniFrameWnd::OnGetMinMaxInfo(MINMAXINFO* pMMI) {
  // Tool windows may be smaller than the system's minimum for frames.
  Default();
  pMMI->ptMinTrackSize.x = ::GetSystemMetrics(SM_CXFRAME) * 2 + 8;
  pMMI->ptMinTrackSize.y = ::GetSystemMetrics(SM_CYSMCAPTION) + ::GetSystemMetrics(SM_CYFRAME) * 2;
}

// ---------------------------------------------------------------------
// CMiniDockFrameWnd

BEGIN_MESSAGE_MAP(CMiniDockFrameWnd, CMiniFrameWnd)
  ON_WM_CLOSE()
  ON_WM_NCLBUTTONDOWN()
  ON_WM_NCLBUTTONDBLCLK()
  ON_WM_MOUSEACTIVATE()
  ON_WM_SIZING()
END_MESSAGE_MAP()

CMiniDockFrameWnd::CMiniDockFrameWnd() : m_wndDockBar(TRUE) {}

CMiniDockFrameWnd::~CMiniDockFrameWnd() {}

BOOL CMiniDockFrameWnd::Create(CWnd* pParent, DWORD dwBarStyle) {
  DWORD dwStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | MFS_MOVEFRAME | MFS_SYNCACTIVATE |
                  MFS_BLOCKSYSMENU | FWS_SNAPTOBARS;
  // A bar that can wrap can be resized; others only moved.
  if (dwBarStyle & CBRS_SIZE_DYNAMIC) dwStyle |= MFS_THICKFRAME;
  if (!CMiniFrameWnd::CreateEx(0, NULL, _T(""), dwStyle, CRect(0, 0, 0, 0), pParent, 0))
    return FALSE;
  // The dock bar is made in the docking frame, so that it is one of that
  // frame's control bars, and then moved in here.
  m_wndDockBar.m_bFloating = TRUE;
  if (!m_wndDockBar.Create(pParent, WS_CHILD | WS_VISIBLE | (dwBarStyle & CBRS_ALIGN_ANY),
                           AFX_IDW_DOCKBAR_FLOAT))
    return FALSE;
  m_wndDockBar.SetParent(this);
  return TRUE;
}

CControlBar* CMiniDockFrameWnd::_GetBar() const {
  for (int i = 0; i < m_wndDockBar.m_arrBars.GetSize(); i++) {
    CControlBar* pBar = m_wndDockBar.GetDockedControlBar(i);
    if (pBar != NULL) return pBar;
  }
  return NULL;
}

// Sizes the frame to fit its bar, and hides it when the bar is hidden.
void CMiniDockFrameWnd::RecalcLayout(BOOL bNotify) {
  (void)bNotify;
  if (m_bInRecalcLayout || m_hWnd == NULL || m_wndDockBar.m_hWnd == NULL) return;
  m_bInRecalcLayout = TRUE;
  if (m_wndDockBar.GetDockedVisibleCount() == 0) {
    if (GetStyle() & WS_VISIBLE) ShowWindow(SW_HIDE);
  } else {
    CSize size = m_wndDockBar.CalcFixedLayout(FALSE, TRUE);
    m_wndDockBar.SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
    CRect rect(0, 0, size.cx, size.cy);
    ::AdjustWindowRectEx(&rect, GetStyle() & ~0xFFFF, FALSE, GetExStyle());
    SetWindowPos(NULL, 0, 0, rect.Width(), rect.Height(),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // The caption is the bar's name.
    CControlBar* pBar = _GetBar();
    if (pBar != NULL) {
      CString strTitle, strOld;
      pBar->GetWindowText(strTitle);
      GetWindowText(strOld);
      if (strTitle != strOld) SetWindowText(strTitle);
    }
    if (!(GetStyle() & WS_VISIBLE)) ShowWindow(SW_SHOWNA);
  }
  m_bInRecalcLayout = FALSE;
}

LRESULT CMiniDockFrameWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_AFX_DESTROYFLOAT) {
    if (m_wndDockBar.GetDockedCount() == 0) DestroyWindow();
    return 0;
  }
  return CMiniFrameWnd::WindowProc(message, wParam, lParam);
}

// The close button hides the bar (View > Toolbar shows it again).
void CMiniDockFrameWnd::OnClose() { m_wndDockBar.ShowAll(FALSE); }

void CMiniDockFrameWnd::OnNcLButtonDown(UINT nHitTest, CPoint point) {
  if (nHitTest == HTCAPTION) {
    CControlBar* pBar = _GetBar();
    if (pBar != NULL && pBar->m_pDockContext != NULL) {
      pBar->m_pDockContext->StartDrag(point);
      return;
    }
  }
  Default();
}

void CMiniDockFrameWnd::OnNcLButtonDblClk(UINT nHitTest, CPoint point) {
  (void)point;
  if (nHitTest == HTCAPTION) {
    CControlBar* pBar = _GetBar();
    if (pBar != NULL && pBar->m_pDockContext != NULL) {
      pBar->m_pDockContext->ToggleDocking();
      return;
    }
  }
  Default();
}

// A floating toolbar does not take the activation (or the focus) from
// the frame it works for.
int CMiniDockFrameWnd::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) {
  (void)pDesktopWnd;
  (void)message;
  CControlBar* pBar = _GetBar();
  if (pBar != NULL && pBar->IsKindOf(RUNTIME_CLASS(CToolBar)) && nHitTest != HTCLOSE)
    return MA_NOACTIVATE;
  return (int)Default();
}

// Resizing a floating bar that wraps snaps the frame to the nearest
// layout of the bar.
void CMiniDockFrameWnd::OnSizing(UINT fwSide, LPRECT pRect) {
  CControlBar* pBar = _GetBar();
  if (pBar == NULL || !(pBar->m_dwStyle & CBRS_SIZE_DYNAMIC)) {
    Default();
    return;
  }
  CRect rectBorder(0, 0, 0, 0);
  ::AdjustWindowRectEx(&rectBorder, GetStyle() & ~0xFFFF, FALSE, GetExStyle());
  int cxClient = (pRect->right - pRect->left) - rectBorder.Width();
  int cyClient = (pRect->bottom - pRect->top) - rectBorder.Height();
  CSize size;
  if (fwSide == WMSZ_TOP || fwSide == WMSZ_BOTTOM)
    size = pBar->CalcDynamicLayout(cyClient, LM_HORZ | LM_LENGTHY | LM_COMMIT);
  else
    size = pBar->CalcDynamicLayout(cxClient, LM_HORZ | LM_COMMIT);
  int cx = size.cx + rectBorder.Width();
  int cy = size.cy + rectBorder.Height();
  // Keep the edge opposite the one being dragged in place.
  if (fwSide == WMSZ_LEFT || fwSide == WMSZ_TOPLEFT || fwSide == WMSZ_BOTTOMLEFT)
    pRect->left = pRect->right - cx;
  else
    pRect->right = pRect->left + cx;
  if (fwSide == WMSZ_TOP || fwSide == WMSZ_TOPLEFT || fwSide == WMSZ_TOPRIGHT)
    pRect->top = pRect->bottom - cy;
  else
    pRect->bottom = pRect->top + cy;
}
