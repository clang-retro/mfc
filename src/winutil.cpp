// winutil.cpp - CWnd helpers: window tree, text, scrolling, layout,
// modal loops, dialog data and resource-driven control setup.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>
#include <afxocc.h>

// ---------------------------------------------------------------------
// Styles and ownership

BOOL PASCAL CWnd::ModifyStyle(HWND hWnd, DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  DWORD dwStyle = (DWORD)::GetWindowLong(hWnd, GWL_STYLE);
  DWORD dwNewStyle = (dwStyle & ~dwRemove) | dwAdd;
  if (dwStyle == dwNewStyle) return FALSE;
  ::SetWindowLong(hWnd, GWL_STYLE, (LONG)dwNewStyle);
  if (nFlags != 0)
    ::SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                   SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | nFlags);
  return TRUE;
}

BOOL PASCAL CWnd::ModifyStyleEx(HWND hWnd, DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  DWORD dwStyle = (DWORD)::GetWindowLong(hWnd, GWL_EXSTYLE);
  DWORD dwNewStyle = (dwStyle & ~dwRemove) | dwAdd;
  if (dwStyle == dwNewStyle) return FALSE;
  ::SetWindowLong(hWnd, GWL_EXSTYLE, (LONG)dwNewStyle);
  if (nFlags != 0)
    ::SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                   SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | nFlags);
  return TRUE;
}

CWnd* CWnd::GetOwner() const {
  return m_hWndOwner != NULL ? CWnd::FromHandle(m_hWndOwner) : GetParent();
}

HWND AFXAPI AfxGetParentOwner(HWND hWnd) {
  if (::GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) return ::GetParent(hWnd);
  return ::GetWindow(hWnd, GW_OWNER);
}

BOOL AFXAPI AfxIsDescendant(HWND hWndParent, HWND hWndChild) {
  do {
    if (hWndParent == hWndChild) return TRUE;
    hWndChild = AfxGetParentOwner(hWndChild);
  } while (hWndChild != NULL);
  return FALSE;
}

CFrameWnd* CWnd::GetParentFrame() const {
  if (GetSafeHwnd() == NULL) return NULL;
  CWnd* pParentWnd = GetParent();
  while (pParentWnd != NULL) {
    if (pParentWnd->IsFrameWnd()) return (CFrameWnd*)pParentWnd;
    pParentWnd = pParentWnd->GetParent();
  }
  return NULL;
}

CFrameWnd* CWnd::EnsureParentFrame() const {
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame == NULL) AfxThrowNotSupportedException();
  return pFrame;
}

CWnd* CWnd::GetTopLevelParent() const {
  if (GetSafeHwnd() == NULL) return NULL;
  HWND hWndParent = m_hWnd;
  HWND hWndT;
  while ((hWndT = AfxGetParentOwner(hWndParent)) != NULL) hWndParent = hWndT;
  return CWnd::FromHandle(hWndParent);
}

CWnd* CWnd::EnsureTopLevelParent() const {
  CWnd* pWnd = GetTopLevelParent();
  if (pWnd == NULL) AfxThrowNotSupportedException();
  return pWnd;
}

CWnd* CWnd::GetTopLevelOwner() const {
  if (GetSafeHwnd() == NULL) return NULL;
  HWND hWndOwner = m_hWnd;
  HWND hWndT;
  while ((hWndT = ::GetWindow(hWndOwner, GW_OWNER)) != NULL) hWndOwner = hWndT;
  return CWnd::FromHandle(hWndOwner);
}

CWnd* CWnd::GetParentOwner() const {
  if (GetSafeHwnd() == NULL) return NULL;
  HWND hWndParent = m_hWnd;
  HWND hWndT;
  while ((::GetWindowLong(hWndParent, GWL_STYLE) & WS_CHILD) &&
         (hWndT = ::GetParent(hWndParent)) != NULL)
    hWndParent = hWndT;
  return CWnd::FromHandle(hWndParent);
}

CFrameWnd* CWnd::GetTopLevelFrame() const {
  if (GetSafeHwnd() == NULL) return NULL;
  CFrameWnd* pFrameWnd = IsFrameWnd() ? (CFrameWnd*)this : GetParentFrame();
  if (pFrameWnd != NULL) {
    CFrameWnd* pTemp;
    while ((pTemp = pFrameWnd->GetParentFrame()) != NULL) pFrameWnd = pTemp;
  }
  return pFrameWnd;
}

HWND AFXAPI _AfxGetSafeOwner(HWND hParent, HWND* pWndTop) {
  HWND hWnd = hParent;
  if (hWnd == NULL) {
    CWnd* pMain = AfxGetMainWnd();
    hWnd = pMain->GetSafeHwnd();
  }
  // A popup cannot be owned by a child window.
  while (hWnd != NULL && (::GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD)) hWnd = ::GetParent(hWnd);

  HWND hWndTop = hWnd;
  HWND hWndTemp = hWnd;
  while (hWndTemp != NULL) {
    hWndTop = hWndTemp;
    hWndTemp = ::GetParent(hWndTop);
  }

  if (hParent == NULL && hWnd != NULL) hWnd = ::GetLastActivePopup(hWnd);

  if (pWndTop != NULL) {
    if (hWndTop != NULL && ::IsWindowEnabled(hWndTop) && hWndTop != hWnd) {
      *pWndTop = hWndTop;
      ::EnableWindow(hWndTop, FALSE);
    } else {
      *pWndTop = NULL;
    }
  }
  return hWnd;
}

CWnd* PASCAL CWnd::GetSafeOwner(CWnd* pParent, HWND* pWndTop) {
  return CWnd::FromHandle(_AfxGetSafeOwner(pParent->GetSafeHwnd(), pWndTop));
}

// ---------------------------------------------------------------------
// Descendants

static CWnd* _AfxGetDescendant(HWND hWnd, int nID, BOOL bOnlyPerm) {
  HWND hWndChild = ::GetDlgItem(hWnd, nID);
  if (hWndChild != NULL) {
    if (::GetTopWindow(hWndChild) != NULL) {
      CWnd* pWnd = _AfxGetDescendant(hWndChild, nID, bOnlyPerm);
      if (pWnd != NULL) return pWnd;
    }
    CWnd* pWnd = bOnlyPerm ? CWnd::FromHandlePermanent(hWndChild) : CWnd::FromHandle(hWndChild);
    if (pWnd != NULL) return pWnd;
  }
  for (hWndChild = ::GetTopWindow(hWnd); hWndChild != NULL;
       hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT)) {
    CWnd* pWnd = _AfxGetDescendant(hWndChild, nID, bOnlyPerm);
    if (pWnd != NULL) return pWnd;
  }
  return NULL;
}

CWnd* CWnd::GetDescendantWindow(int nID, BOOL bOnlyPerm) const {
  return _AfxGetDescendant(m_hWnd, nID, bOnlyPerm);
}

void PASCAL CWnd::SendMessageToDescendants(HWND hWnd, UINT message, WPARAM wParam,
                                           LPARAM lParam, BOOL bDeep, BOOL bOnlyPerm) {
  for (HWND hWndChild = ::GetTopWindow(hWnd); hWndChild != NULL;
       hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT)) {
    if (bOnlyPerm) {
      CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
      if (pWnd != NULL) AfxCallWndProc(pWnd, pWnd->m_hWnd, message, wParam, lParam);
    } else {
      ::SendMessage(hWndChild, message, wParam, lParam);
    }
    if (bDeep && ::GetTopWindow(hWndChild) != NULL)
      SendMessageToDescendants(hWndChild, message, wParam, lParam, bDeep, bOnlyPerm);
  }
}

void CWnd::SendMessageToDescendants(UINT message, WPARAM wParam, LPARAM lParam, BOOL bDeep,
                                    BOOL bOnlyPerm) {
  SendMessageToDescendants(m_hWnd, message, wParam, lParam, bDeep, bOnlyPerm);
}

// ---------------------------------------------------------------------
// Text

void CWnd::GetWindowText(CString& rString) const {
  if (m_pCtrlSite != NULL) {
    m_pCtrlSite->GetWindowText(rString);
    return;
  }
  int nLen = ::GetWindowTextLength(m_hWnd);
  ::GetWindowText(m_hWnd, rString.GetBufferSetLength(nLen), nLen + 1);
  rString.ReleaseBuffer();
}

int CWnd::GetDlgItemText(int nID, CString& rString) const {
  rString.Empty();
  if (m_pCtrlCont != NULL) {
    COleControlSite* pSite = m_pCtrlCont->FindItem((UINT)nID);
    if (pSite != NULL) {
      pSite->GetWindowText(rString);
      return rString.GetLength();
    }
  }
  HWND hWnd = ::GetDlgItem(m_hWnd, nID);
  if (hWnd == NULL) return 0;
  int nLen = ::GetWindowTextLength(hWnd);
  ::GetWindowText(hWnd, rString.GetBufferSetLength(nLen), nLen + 1);
  rString.ReleaseBuffer();
  return rString.GetLength();
}

int CWnd::GetCheckedRadioButton(int nIDFirstButton, int nIDLastButton) {
  for (int nID = nIDFirstButton; nID <= nIDLastButton; nID++) {
    if (::IsDlgButtonChecked(m_hWnd, nID)) return nID;
  }
  return 0;
}

int CWnd::MessageBox(LPCTSTR lpszText, LPCTSTR lpszCaption, UINT nType) {
  if (lpszCaption == NULL) lpszCaption = AfxGetAppName();
  return ::MessageBox(GetSafeHwnd(), lpszText, lpszCaption, nType);
}

typedef BOOL(WINAPI* PFN_FLASHWINDOWEX)(PFLASHWINFO);

BOOL CWnd::FlashWindowEx(DWORD dwFlags, UINT uCount, DWORD dwTimeout) {
  // Not on Windows 95 or NT 4.
  PFN_FLASHWINDOWEX pfn =
      (PFN_FLASHWINDOWEX)(void*)::GetProcAddress(::GetModuleHandleA("user32.dll"), "FlashWindowEx");
  if (pfn == NULL) return ::FlashWindow(m_hWnd, (dwFlags & FLASHW_CAPTION) != 0);
  FLASHWINFO fwi;
  fwi.cbSize = sizeof(fwi);
  fwi.hwnd = m_hWnd;
  fwi.dwFlags = dwFlags;
  fwi.uCount = uCount;
  fwi.dwTimeout = dwTimeout;
  return pfn(&fwi);
}

// ---------------------------------------------------------------------
// Scroll bars

CScrollBar* CWnd::GetScrollBarCtrl(int nBar) const {
  (void)nBar;
  return NULL;
}

// The window and bar a scroll call goes to.
static HWND _AfxScrollTarget(const CWnd* pWnd, int& nBar) {
  if (nBar != SB_CTL) {
    CScrollBar* pScrollBar = pWnd->GetScrollBarCtrl(nBar);
    if (pScrollBar != NULL) {
      nBar = SB_CTL;
      return ((CWnd*)pScrollBar)->m_hWnd;
    }
  }
  return pWnd->m_hWnd;
}

int CWnd::GetScrollPos(int nBar) const {
  HWND hWnd = _AfxScrollTarget(this, nBar);
  return ::GetScrollPos(hWnd, nBar);
}

int CWnd::SetScrollPos(int nBar, int nPos, BOOL bRedraw) {
  HWND hWnd = _AfxScrollTarget(this, nBar);
  return ::SetScrollPos(hWnd, nBar, nPos, bRedraw);
}

void CWnd::GetScrollRange(int nBar, LPINT lpMinPos, LPINT lpMaxPos) const {
  HWND hWnd = _AfxScrollTarget(this, nBar);
  ::GetScrollRange(hWnd, nBar, lpMinPos, lpMaxPos);
}

void CWnd::SetScrollRange(int nBar, int nMinPos, int nMaxPos, BOOL bRedraw) {
  HWND hWnd = _AfxScrollTarget(this, nBar);
  ::SetScrollRange(hWnd, nBar, nMinPos, nMaxPos, bRedraw);
}

void CWnd::EnableScrollBarCtrl(int nBar, BOOL bEnable) {
  if (nBar == SB_BOTH) {
    EnableScrollBarCtrl(SB_HORZ, bEnable);
    EnableScrollBarCtrl(SB_VERT, bEnable);
    return;
  }
  CScrollBar* pScrollBar = GetScrollBarCtrl(nBar);
  if (pScrollBar != NULL)
    ((CWnd*)pScrollBar)->EnableWindow(bEnable);
  else
    ::ShowScrollBar(m_hWnd, nBar, bEnable);
}

BOOL CWnd::SetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, BOOL bRedraw) {
  HWND hWnd = m_hWnd;
  CScrollBar* pScrollBar;
  if (nBar != SB_CTL && (pScrollBar = GetScrollBarCtrl(nBar)) != NULL) {
    hWnd = ((CWnd*)pScrollBar)->m_hWnd;
    nBar = SB_CTL;
  }
  lpScrollInfo->cbSize = sizeof(*lpScrollInfo);
  ::SetScrollInfo(hWnd, nBar, lpScrollInfo, bRedraw);
  return TRUE;
}

BOOL CWnd::GetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, UINT nMask) {
  HWND hWnd = m_hWnd;
  CScrollBar* pScrollBar;
  if (nBar != SB_CTL && (pScrollBar = GetScrollBarCtrl(nBar)) != NULL) {
    hWnd = ((CWnd*)pScrollBar)->m_hWnd;
    nBar = SB_CTL;
  }
  lpScrollInfo->cbSize = sizeof(*lpScrollInfo);
  lpScrollInfo->fMask = nMask;
  return ::GetScrollInfo(hWnd, nBar, lpScrollInfo);
}

int CWnd::GetScrollLimit(int nBar) {
  SCROLLINFO info;
  if (!GetScrollInfo(nBar, &info, SIF_RANGE | SIF_PAGE)) return 0;
  int nMax = info.nMax;
  if (info.nPage > 1) nMax -= (int)info.nPage - 1;
  return nMax;
}

// ---------------------------------------------------------------------
// Layout

void AFXAPI AfxRepositionWindow(AFX_SIZEPARENTPARAMS* lpLayout, HWND hWnd, LPCRECT lpRect) {
  HWND hWndParent = ::GetParent(hWnd);
  CRect rectOld;
  ::GetWindowRect(hWnd, &rectOld);
  ::MapWindowPoints(NULL, hWndParent, (LPPOINT)&rectOld, 2);
  if (::EqualRect(&rectOld, lpRect)) return;
  if (lpLayout != NULL && lpLayout->hDWP != NULL) {
    lpLayout->hDWP = ::DeferWindowPos(lpLayout->hDWP, hWnd, NULL, lpRect->left, lpRect->top,
                                      lpRect->right - lpRect->left,
                                      lpRect->bottom - lpRect->top,
                                      SWP_NOACTIVATE | SWP_NOZORDER);
  } else {
    ::SetWindowPos(hWnd, NULL, lpRect->left, lpRect->top, lpRect->right - lpRect->left,
                   lpRect->bottom - lpRect->top, SWP_NOACTIVATE | SWP_NOZORDER);
  }
}

void CWnd::RepositionBars(UINT nIDFirst, UINT nIDLast, UINT nIDLeftOver, UINT nFlags,
                          LPRECT lpRectParam, LPCRECT lpRectClient, BOOL bStretch) {
  AFX_SIZEPARENTPARAMS layout;
  HWND hWndLeftOver = NULL;
  layout.bStretch = bStretch;
  layout.sizeTotal.cx = layout.sizeTotal.cy = 0;
  if (lpRectClient != NULL)
    layout.rect = *lpRectClient;
  else
    ::GetClientRect(m_hWnd, &layout.rect);

  UINT nMode = nFlags & ~(UINT)reposNoPosLeftOver;
  layout.hDWP = nMode != (UINT)reposQuery ? ::BeginDeferWindowPos(8) : NULL;

  for (HWND hWndChild = ::GetTopWindow(m_hWnd); hWndChild != NULL;
       hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT)) {
    UINT nIDC = (UINT)::GetDlgCtrlID(hWndChild);
    CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
    // A bar that an object active in place docked on this window belongs
    // to the other frame object that stands for it (COleCntrFrameWnd),
    // and the other way round: each lays out its own.
    if (pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CControlBar))) {
      CFrameWnd* pSite = ((CControlBar*)pWnd)->m_pDockSite;
      if (pSite != NULL && pSite != this && pSite->m_hWnd == m_hWnd) continue;
    }
    if (nIDC == nIDLeftOver)
      hWndLeftOver = hWndChild;
    else if (nIDC >= nIDFirst && nIDC <= nIDLast && pWnd != NULL)
      ::SendMessage(hWndChild, WM_SIZEPARENT, 0, (LPARAM)&layout);
  }

  if (nMode == (UINT)reposQuery) {
    if (lpRectParam != NULL) {
      if (bStretch) {
        ::CopyRect(lpRectParam, &layout.rect);
      } else {
        lpRectParam->left = lpRectParam->top = 0;
        lpRectParam->right = layout.sizeTotal.cx;
        lpRectParam->bottom = layout.sizeTotal.cy;
      }
    }
    return;
  }

  if (nIDLeftOver != 0 && hWndLeftOver != NULL) {
    CWnd* pLeftOver = CWnd::FromHandle(hWndLeftOver);
    if (nMode == (UINT)reposExtra && lpRectParam != NULL) {
      layout.rect.left += lpRectParam->left;
      layout.rect.top += lpRectParam->top;
      layout.rect.right -= lpRectParam->right;
      layout.rect.bottom -= lpRectParam->bottom;
    }
    if (!(nFlags & reposNoPosLeftOver)) {
      pLeftOver->CalcWindowRect(&layout.rect);
      AfxRepositionWindow(&layout, hWndLeftOver, &layout.rect);
    }
  }

  if (layout.hDWP != NULL) ::EndDeferWindowPos(layout.hDWP);
}

typedef HMONITOR(WINAPI* PFN_MONITORFROMWINDOW)(HWND, DWORD);
typedef BOOL(WINAPI* PFN_GETMONITORINFO)(HMONITOR, LPMONITORINFO);

// The work area of the monitor a window is on (the whole desktop's work
// area on systems without multiple-monitor support).
static void _AfxGetWorkArea(HWND hWnd, LPRECT lpRect) {
  HMODULE hUser = ::GetModuleHandleA("user32.dll");
  PFN_MONITORFROMWINDOW pfnFrom =
      (PFN_MONITORFROMWINDOW)(void*)::GetProcAddress(hUser, "MonitorFromWindow");
  PFN_GETMONITORINFO pfnInfo =
      (PFN_GETMONITORINFO)(void*)::GetProcAddress(hUser, "GetMonitorInfoA");
  if (pfnFrom != NULL && pfnInfo != NULL) {
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (pfnInfo(pfnFrom(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) {
      *lpRect = mi.rcWork;
      return;
    }
  }
  ::SystemParametersInfo(SPI_GETWORKAREA, 0, lpRect, 0);
}

void CWnd::CenterWindow(CWnd* pAlternateOwner) {
  DWORD dwStyle = GetStyle();
  HWND hWndCenter = pAlternateOwner->GetSafeHwnd();
  if (pAlternateOwner == NULL) {
    if (dwStyle & WS_CHILD)
      hWndCenter = ::GetParent(m_hWnd);
    else
      hWndCenter = ::GetWindow(m_hWnd, GW_OWNER);
    if (hWndCenter != NULL) {
      // The owner may name another window to centre on.
      HWND hWndTemp = (HWND)::SendMessage(hWndCenter, WM_QUERYCENTERWND, 0, 0);
      if (hWndTemp != NULL) hWndCenter = hWndTemp;
    }
  }

  CRect rcDlg;
  GetWindowRect(&rcDlg);
  CRect rcArea;
  CRect rcCenter;
  if (!(dwStyle & WS_CHILD)) {
    if (hWndCenter != NULL) {
      DWORD dwAltStyle = (DWORD)::GetWindowLong(hWndCenter, GWL_STYLE);
      if (!(dwAltStyle & WS_VISIBLE) || (dwAltStyle & WS_MINIMIZE)) hWndCenter = NULL;
    }
    _AfxGetWorkArea(hWndCenter != NULL ? hWndCenter : m_hWnd, &rcArea);
    if (hWndCenter == NULL)
      rcCenter = rcArea;
    else
      ::GetWindowRect(hWndCenter, &rcCenter);
  } else {
    HWND hWndParent = ::GetParent(m_hWnd);
    ::GetClientRect(hWndParent, &rcArea);
    ::GetClientRect(hWndCenter, &rcCenter);
    ::MapWindowPoints(hWndCenter, hWndParent, (POINT*)&rcCenter, 2);
  }

  int xLeft = (rcCenter.left + rcCenter.right) / 2 - rcDlg.Width() / 2;
  int yTop = (rcCenter.top + rcCenter.bottom) / 2 - rcDlg.Height() / 2;
  if (xLeft + rcDlg.Width() > rcArea.right) xLeft = rcArea.right - rcDlg.Width();
  if (xLeft < rcArea.left) xLeft = rcArea.left;
  if (yTop + rcDlg.Height() > rcArea.bottom) yTop = rcArea.bottom - rcDlg.Height();
  if (yTop < rcArea.top) yTop = rcArea.top;

  SetWindowPos(NULL, xLeft, yTop, -1, -1, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------
// Modal loops

int CWnd::RunModalLoop(DWORD dwFlags) {
  BOOL bIdle = TRUE;
  LONG lIdleCount = 0;
  BOOL bShowIdle = (dwFlags & MLF_SHOWONIDLE) && !(GetStyle() & WS_VISIBLE);
  HWND hWndParent = ::GetParent(m_hWnd);
  m_nFlags |= (WF_MODALLOOP | WF_CONTINUEMODAL);
  MSG* pMsg = _AfxGetCurrentMsg();
  CWinThread* pThread = AfxGetThread();

  for (;;) {
    // Idle until a message arrives.
    while (bIdle && !::PeekMessage(pMsg, NULL, 0, 0, PM_NOREMOVE)) {
      if (bShowIdle) {
        ShowWindow(SW_SHOWNORMAL);
        UpdateWindow();
        bShowIdle = FALSE;
      }
      if (!(dwFlags & MLF_NOIDLEMSG) && hWndParent != NULL && lIdleCount == 0)
        ::SendMessage(hWndParent, WM_ENTERIDLE, MSGF_DIALOGBOX, (LPARAM)m_hWnd);
      if ((dwFlags & MLF_NOKICKIDLE) || !SendMessage(WM_KICKIDLE, MSGF_DIALOGBOX, lIdleCount++))
        bIdle = FALSE;
    }

    // Pump messages while they are there.
    do {
      if (!AfxPumpMessage()) {
        AfxPostQuitMessage(0);
        m_nFlags &= ~(WF_MODALLOOP | WF_CONTINUEMODAL);
        return -1;
      }
      if (bShowIdle && (pMsg->message == 0x118 || pMsg->message == WM_SYSKEYDOWN)) {
        ShowWindow(SW_SHOWNORMAL);
        UpdateWindow();
        bShowIdle = FALSE;
      }
      if (!ContinueModal()) goto ExitModal;
      if (pThread == NULL || pThread->IsIdleMessage(pMsg)) {
        bIdle = TRUE;
        lIdleCount = 0;
      }
    } while (::PeekMessage(pMsg, NULL, 0, 0, PM_NOREMOVE));
  }

ExitModal:
  m_nFlags &= ~(WF_MODALLOOP | WF_CONTINUEMODAL);
  return m_nModalResult;
}

BOOL CWnd::ContinueModal() { return (m_nFlags & WF_CONTINUEMODAL) != 0; }

void CWnd::EndModalLoop(int nResult) {
  m_nModalResult = nResult;
  if (m_nFlags & WF_CONTINUEMODAL) {
    m_nFlags &= ~WF_CONTINUEMODAL;
    PostMessage(WM_NULL);
  }
}

// ---------------------------------------------------------------------
// Dialog data and command UI for controls

void CWnd::DoDataExchange(CDataExchange* pDX) { (void)pDX; }

BOOL CWnd::UpdateData(BOOL bSaveAndValidate) {
  CDataExchange dx(this, bSaveAndValidate);
  // Controls send notifications while their text is set; keep those away
  // from the handlers during the exchange.
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  HWND hWndOldLockout = pState->m_hLockoutNotifyWindow;
  pState->m_hLockoutNotifyWindow = m_hWnd;

  BOOL bOK = FALSE;
  try {
    DoDataExchange(&dx);
    bOK = TRUE;
  } catch (CUserException* e) {
    // Validation failed; the user has been told already.
    e->Delete();
  } catch (CException* e) {
    AfxMessageBox(AFX_IDP_INTERNAL_FAILURE);
    e->Delete();
  }
  pState->m_hLockoutNotifyWindow = hWndOldLockout;
  return bOK;
}

void CWnd::UpdateDialogControls(CCmdTarget* pTarget, BOOL bDisableIfNoHndler) {
  CCmdUI state;
  CWnd wndTemp;  // stands in for controls we do not wrap
  for (HWND hWndChild = ::GetTopWindow(m_hWnd); hWndChild != NULL;
       hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT)) {
    wndTemp.m_hWnd = hWndChild;
    state.m_nID = (UINT)::GetDlgCtrlID(hWndChild);
    state.m_pOther = &wndTemp;

    // A control with ON_UPDATE_COMMAND_UI_REFLECT updates itself.
    CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
    BOOL bReflected = FALSE;
    if (pWnd != NULL) {
      for (const AFX_MSGMAP* pMap = pWnd->GetMessageMap(); pMap != NULL && !bReflected;
           pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
        const AFX_MSGMAP_ENTRY* lpEntry = CCmdTarget::FindMessageEntry(
            pMap->lpEntries, WM_REFLECT_BASE + WM_COMMAND, CN_UPDATE_COMMAND_UI, 0);
        if (lpEntry != NULL) {
          lpEntry->pThunk(pWnd, WM_REFLECT_BASE + WM_COMMAND, 0, (LPARAM)&state);
          bReflected = TRUE;
        }
      }
    }
    if (bReflected) continue;

    // Handlers in this window come first.
    if (CWnd::OnCmdMsg(state.m_nID, (int)CN_UPDATE_COMMAND_UI, &state, NULL)) continue;

    // Only push buttons are disabled for want of a handler.
    BOOL bDisableTemp = bDisableIfNoHndler;
    if (bDisableTemp) {
      if ((wndTemp.SendMessage(WM_GETDLGCODE) & DLGC_BUTTON) == 0) {
        bDisableTemp = FALSE;
      } else {
        UINT nStyle = (UINT)(wndTemp.GetStyle() & 0x0F);
        if (nStyle == BS_AUTOCHECKBOX || nStyle == BS_AUTO3STATE || nStyle == BS_GROUPBOX ||
            nStyle == BS_AUTORADIOBUTTON)
          bDisableTemp = FALSE;
      }
    }
    state.DoUpdate(pTarget, bDisableTemp);
  }
  wndTemp.m_hWnd = NULL;
}

INT_PTR CWnd::OnToolHitTest(CPoint point, TOOLINFO* pTI) const {
  (void)point;
  (void)pTI;
  return -1;
}

// ---------------------------------------------------------------------
// Help

void CWnd::WinHelp(DWORD_PTR dwData, UINT nCmd) {
  CWinApp* pApp = AfxGetApp();
  CWnd* pWnd = GetTopLevelParent();
  if (pApp == NULL || pApp->m_pszHelpFilePath == NULL) return;
  if (!::WinHelp(pWnd->GetSafeHwnd(), pApp->m_pszHelpFilePath, nCmd, dwData))
    AfxMessageBox(AFX_IDP_FAILED_TO_LAUNCH_HELP);
}

void CWnd::HtmlHelp(DWORD_PTR dwData, UINT nCmd) {
  // HTML Help needs hhctrl.ocx; WinHelp is used instead.
  (void)nCmd;
  WinHelp(dwData, HELP_CONTEXT);
}

// ---------------------------------------------------------------------
// RT_DLGINIT: initial strings for list and combo boxes

BOOL CWnd::ExecuteDlgInit(LPCTSTR lpszResourceName) {
  if (lpszResourceName == NULL) return TRUE;
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_DLGINIT);
  HRSRC hResource = ::FindResource(hInst, lpszResourceName, RT_DLGINIT);
  if (hResource == NULL) return TRUE;  // nothing to do
  HGLOBAL hDlgInit = ::LoadResource(hInst, hResource);
  if (hDlgInit == NULL) return FALSE;
  LPVOID lpResource = ::LockResource(hDlgInit);
  BOOL bResult = ExecuteDlgInit(lpResource);
  return bResult;
}

BOOL CWnd::ExecuteDlgInit(LPVOID lpResource) {
  if (lpResource == NULL) return TRUE;
  BYTE* p = (BYTE*)lpResource;
  BOOL bSuccess = TRUE;
  for (;;) {
    WORD nIDC = *(WORD*)p;
    if (nIDC == 0) break;
    WORD nMsg = *(WORD*)(p + 2);
    DWORD dwLen = *(DWORD*)(p + 4);
    p += 8;
    // The resource compiler writes the 16-bit message numbers.
    UINT nMsg32 = nMsg;
    if (nMsg == 0x0401) nMsg32 = LB_ADDSTRING;
    else if (nMsg == 0x0403) nMsg32 = CB_ADDSTRING;
    if (nMsg32 == LB_ADDSTRING || nMsg32 == CB_ADDSTRING) {
      CString str((LPCSTR)p);  // data is ANSI text
      if (::SendDlgItemMessage(m_hWnd, nIDC, nMsg32, 0, (LPARAM)(LPCTSTR)str) < 0)
        bSuccess = FALSE;
    }
    p += dwLen;
  }
  return bSuccess;
}

MSG* AFXAPI _AfxGetCurrentMsg() {
  CWinThread* pThread = AfxGetThread();
  return pThread != NULL ? &pThread->m_msgCur : &AfxGetThreadState()->m_msgCur;
}
