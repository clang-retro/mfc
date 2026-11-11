// wincore.cpp - CWnd: creation, subclassing, the window procedure and
// message dispatch through message maps.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxocc.h>

const _AFX_OCC_HOOKS* _afxOccHooks = NULL;

IMPLEMENT_DYNCREATE(CWnd, CCmdTarget)

const CWnd CWnd::wndTop(HWND_TOP);
const CWnd CWnd::wndBottom(HWND_BOTTOM);
const CWnd CWnd::wndTopMost(HWND_TOPMOST);
const CWnd CWnd::wndNoTopMost(HWND_NOTOPMOST);

const TCHAR _afxWnd[] = AFX_WND;
const TCHAR _afxWndControlBar[] = AFX_WNDCONTROLBAR;
const TCHAR _afxWndMDIFrame[] = AFX_WNDMDIFRAME;
const TCHAR _afxWndFrameOrView[] = AFX_WNDFRAMEORVIEW;

CWnd::CWnd()
    : m_hWnd(NULL), m_pDropTarget(NULL), m_pCtrlCont(NULL), m_pCtrlSite(NULL), m_pOccDialogInfo(NULL), m_pfnSuper(NULL), m_hWndOwner(NULL), m_nFlags(0), m_nModalResult(0) {}

CWnd::CWnd(HWND hWnd)
    : m_hWnd(hWnd), m_pDropTarget(NULL), m_pCtrlCont(NULL), m_pCtrlSite(NULL), m_pOccDialogInfo(NULL), m_pfnSuper(NULL), m_hWndOwner(NULL), m_nFlags(0), m_nModalResult(0) {}

CWnd::~CWnd() {
  // A hosted control's window object takes the control with it.
  if (m_pCtrlSite != NULL && _afxOccHooks != NULL) _afxOccHooks->pfnDestroyControl(this);
  if (m_hWnd != NULL && this != &wndTop && this != &wndBottom && this != &wndTopMost &&
      this != &wndNoTopMost) {
    // Destroying the C++ object first destroys the window it owns.
    CHandleMap* pMap = afxMapHWND();
    if (pMap != NULL && pMap->LookupPermanent(m_hWnd) == this) {
      TRACE(_T("Warning: CWnd destroyed while its window still exists.\n"));
      DestroyWindow();
    }
  }
}

// ---------------------------------------------------------------------
// Handle map

CWnd* PASCAL CWnd::FromHandle(HWND hWnd) {
  CHandleMap* pMap = afxMapHWND(TRUE);
  return (CWnd*)pMap->FromHandle(hWnd);
}

CWnd* PASCAL CWnd::FromHandlePermanent(HWND hWnd) {
  CHandleMap* pMap = afxMapHWND();
  return pMap != NULL && hWnd != NULL ? (CWnd*)pMap->LookupPermanent(hWnd) : NULL;
}

void PASCAL CWnd::DeleteTempMap() {
  CHandleMap* pMap = afxMapHWND();
  if (pMap != NULL) pMap->DeleteTemp();
}

BOOL CWnd::Attach(HWND hWndNew) {
  if (hWndNew == NULL) return FALSE;
  CHandleMap* pMap = afxMapHWND(TRUE);
  m_hWnd = hWndNew;
  pMap->SetPermanent(m_hWnd, this);
  return TRUE;
}

HWND CWnd::Detach() {
  HWND hWnd = m_hWnd;
  if (hWnd != NULL) {
    CHandleMap* pMap = afxMapHWND();
    if (pMap != NULL && pMap->LookupPermanent(hWnd) == this) pMap->RemoveHandle(hWnd);
    m_hWnd = NULL;
  }
  return hWnd;
}

void CWnd::PreSubclassWindow() {}

WNDPROC* CWnd::GetSuperWndProcAddr() { return &m_pfnSuper; }

BOOL CWnd::SubclassWindow(HWND hWnd) {
  if (!Attach(hWnd)) return FALSE;
  PreSubclassWindow();
  WNDPROC* lplpfn = GetSuperWndProcAddr();
  WNDPROC oldWndProc =
      (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)AfxGetAfxWndProc());
  if (oldWndProc == AfxGetAfxWndProc()) {
    // Already one of ours; leave it alone.
    Detach();
    return FALSE;
  }
  if (*lplpfn == NULL) *lplpfn = oldWndProc;
  return TRUE;
}

BOOL CWnd::SubclassDlgItem(UINT nID, CWnd* pParent) {
  HWND hWndControl = ::GetDlgItem(pParent->m_hWnd, (int)nID);
  if (hWndControl == NULL) return FALSE;
  return SubclassWindow(hWndControl);
}

HWND CWnd::UnsubclassWindow() {
  WNDPROC* lplpfn = GetSuperWndProcAddr();
  ::SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)*lplpfn);
  *lplpfn = NULL;
  return Detach();
}

// ---------------------------------------------------------------------
// Creation. A CBT hook attaches the CWnd to its window as soon as the
// window exists, so that the CWnd sees WM_NCCREATE and WM_CREATE.

static LRESULT CALLBACK _AfxCbtFilterHook(int code, WPARAM wParam, LPARAM lParam) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (code == HCBT_CREATEWND && pState->m_pWndInit != NULL) {
    HWND hWnd = (HWND)wParam;
    // Input method editors make their own windows; skip those.
    TCHAR szClass[16];
    if (::GetClassName(hWnd, szClass, 16) && lstrcmpi(szClass, _T("ime")) == 0)
      return ::CallNextHookEx(pState->m_hHookOldCbtFilter, code, wParam, lParam);

    CWnd* pWndInit = pState->m_pWndInit;
    pState->m_pWndInit = NULL;
    pWndInit->Attach(hWnd);
    pWndInit->PreSubclassWindow();
    WNDPROC* pOldWndProc = pWndInit->GetSuperWndProcAddr();
    WNDPROC afxWndProc = AfxGetAfxWndProc();
    WNDPROC oldWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)afxWndProc);
    if (oldWndProc != afxWndProc) *pOldWndProc = oldWndProc;
  }
  return ::CallNextHookEx(pState->m_hHookOldCbtFilter, code, wParam, lParam);
}

void AFXAPI AfxHookWindowCreate(CWnd* pWnd) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pWndInit == pWnd) return;
  if (pState->m_hHookOldCbtFilter == NULL) {
    pState->m_hHookOldCbtFilter =
        ::SetWindowsHookEx(WH_CBT, _AfxCbtFilterHook, NULL, ::GetCurrentThreadId());
    if (pState->m_hHookOldCbtFilter == NULL) AfxThrowMemoryException();
  }
  pState->m_pWndInit = pWnd;
}

BOOL AFXAPI AfxUnhookWindowCreate() {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pWndInit != NULL) {
    pState->m_pWndInit = NULL;
    return FALSE;  // the window was never created
  }
  return TRUE;
}

BOOL CWnd::CreateEx(DWORD dwExStyle, LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                    DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent,
                    HMENU nIDorHMenu, LPVOID lpParam) {
  CREATESTRUCT cs;
  cs.dwExStyle = dwExStyle;
  cs.lpszClass = lpszClassName;
  cs.lpszName = lpszWindowName;
  cs.style = (LONG)dwStyle;
  cs.x = x;
  cs.y = y;
  cs.cx = nWidth;
  cs.cy = nHeight;
  cs.hwndParent = hWndParent;
  cs.hMenu = nIDorHMenu;
  cs.hInstance = AfxGetInstanceHandle();
  cs.lpCreateParams = lpParam;

  if (!PreCreateWindow(cs)) {
    PostNcDestroy();
    return FALSE;
  }

  AfxHookWindowCreate(this);
  HWND hWnd = ::CreateWindowEx(cs.dwExStyle, cs.lpszClass, cs.lpszName, (DWORD)cs.style, cs.x,
                               cs.y, cs.cx, cs.cy, cs.hwndParent, cs.hMenu, cs.hInstance,
                               cs.lpCreateParams);
  if (!AfxUnhookWindowCreate()) PostNcDestroy();  // never got as far as the hook
  return hWnd != NULL;
}

BOOL CWnd::CreateEx(DWORD dwExStyle, LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
                    DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID,
                    LPVOID lpParam) {
  return CreateEx(dwExStyle, lpszClassName, lpszWindowName, dwStyle, rect.left, rect.top,
                  rect.right - rect.left, rect.bottom - rect.top, pParentWnd->GetSafeHwnd(),
                  (HMENU)(UINT_PTR)nID, lpParam);
}

BOOL CWnd::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                  const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext) {
  return CreateEx(0, lpszClassName, lpszWindowName, dwStyle | WS_CHILD, rect.left, rect.top,
                  rect.right - rect.left, rect.bottom - rect.top, pParentWnd->GetSafeHwnd(),
                  (HMENU)(UINT_PTR)nID, (LPVOID)pContext);
}

BOOL CWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WND_REG)) return FALSE;
    cs.lpszClass = _afxWnd;
  }
  return TRUE;
}

BOOL CWnd::DestroyWindow() {
  // A hosted control's window goes with the control.
  if (m_pCtrlSite != NULL && _afxOccHooks != NULL) return _afxOccHooks->pfnDestroyControl(this);
  if (m_hWnd == NULL) return FALSE;
  HWND hWndOrig = m_hWnd;
  CWnd* pWnd = FromHandlePermanent(hWndOrig);
  BOOL bResult = ::DestroyWindow(hWndOrig);
  // A subclassed window was detached by OnNcDestroy, and the object may
  // have been deleted by PostNcDestroy, so only touch it if it is still
  // in the map, or was never in it.
  if (pWnd == NULL) {
    m_hWnd = NULL;
  } else {
    CHandleMap* pMap = afxMapHWND();
    if (pMap != NULL && pMap->LookupPermanent(hWndOrig) == pWnd) pWnd->Detach();
  }
  return bResult;
}

void CWnd::PostNcDestroy() {}

void CWnd::OnFinalRelease() {
  if (m_hWnd != NULL)
    DestroyWindow();
  else
    PostNcDestroy();
}

void CWnd::CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType) {
  DWORD dwExStyle = GetExStyle();
  if (nAdjustType == adjustBorder) dwExStyle &= ~WS_EX_CLIENTEDGE;
  ::AdjustWindowRectEx(lpClientRect, GetStyle(), FALSE, dwExStyle);
}

// ---------------------------------------------------------------------
// The window procedure

LRESULT CALLBACK AfxWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  if (nMsg == WM_QUERYAFXWNDPROC) return 1;
  CWnd* pWnd = CWnd::FromHandlePermanent(hWnd);
  if (pWnd == NULL || pWnd->m_hWnd != hWnd) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  return AfxCallWndProc(pWnd, hWnd, nMsg, wParam, lParam);
}

// A regular DLL's windows get a window procedure of the DLL's, which makes
// the DLL's state the current one, so that its handle maps find them.
WNDPROC AFXAPI AfxGetAfxWndProc() {
  WNDPROC pfn = AfxGetModuleState()->m_pfnAfxWndProc;
  return pfn != NULL ? pfn : &AfxWndProc;
}

LRESULT AFXAPI AfxCallWndProc(CWnd* pWnd, HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
#ifdef _AFXDLL
  // A regular DLL's window runs in the DLL's state.
  AFX_MANAGE_STATE(pWnd->m_pModuleState);
#endif
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  MSG oldState = pState->m_lastSentMsg;
  pState->m_lastSentMsg.hwnd = hWnd;
  pState->m_lastSentMsg.message = nMsg;
  pState->m_lastSentMsg.wParam = wParam;
  pState->m_lastSentMsg.lParam = lParam;

  LRESULT lResult = 0;
  try {
    // Dialogs are centred after WM_INITDIALOG unless they moved themselves.
    BOOL bCenter = FALSE;
    CRect rectOld;
    if (nMsg == WM_INITDIALOG) {
      DWORD dwStyle = pWnd->GetStyle();
      bCenter = !(dwStyle & (WS_CHILD | DS_CENTER | DS_CENTERMOUSE | DS_ABSALIGN)) &&
                pWnd->CheckAutoCenter();
      if (bCenter) ::GetWindowRect(hWnd, &rectOld);
    }
    lResult = pWnd->WindowProc(nMsg, wParam, lParam);
    if (bCenter && ::IsWindow(hWnd)) {
      CRect rectNew;
      ::GetWindowRect(hWnd, &rectNew);
      if (rectNew == rectOld) pWnd->CenterWindow();
    }
  } catch (CException* e) {
    CWinThread* pThread = AfxGetThread();
    if (pThread != NULL)
      lResult = pThread->ProcessWndProcException(e, &pState->m_lastSentMsg);
    e->Delete();
  }
  pState->m_lastSentMsg = oldState;
  return lResult;
}

LRESULT CWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  // A dialog template's ActiveX controls are there before OnInitDialog.
  if (message == WM_INITDIALOG && m_pOccDialogInfo != NULL && _afxOccHooks != NULL)
    _afxOccHooks->pfnInitDialog(this);
  LRESULT lResult = 0;
  // Windowless controls get their messages before the window, and draw
  // over what it paints.
  if (m_pCtrlCont != NULL && _afxOccHooks != NULL && _afxOccHooks->pfnHasWindowless(this)) {
    if (message == WM_PAINT) {
      HRGN hRgn = ::CreateRectRgn(0, 0, 0, 0);
      int nRgn = ::GetUpdateRgn(m_hWnd, hRgn, FALSE);
      if (!OnWndMsg(message, wParam, lParam, &lResult))
        lResult = DefWindowProc(message, wParam, lParam);
      if (nRgn != NULLREGION && nRgn != ERROR && m_hWnd != NULL)
        _afxOccHooks->pfnPaintWindowless(this, hRgn);
      ::DeleteObject(hRgn);
      return lResult;
    }
    if (_afxOccHooks->pfnWindowlessMessage(this, message, wParam, lParam, &lResult))
      return lResult;
  }
  if (!OnWndMsg(message, wParam, lParam, &lResult))
    lResult = DefWindowProc(message, wParam, lParam);
  // The hosted controls close after the window's own OnDestroy.
  if (message == WM_DESTROY && m_pCtrlCont != NULL && _afxOccHooks != NULL)
    _afxOccHooks->pfnDestroy(this);
  return lResult;
}

BOOL CWnd::OnAmbientProperty(COleControlSite* pSite, DISPID dispid, VARIANT* pvar) {
  return m_pCtrlCont != NULL && m_pCtrlCont->GetAmbientProp(pSite, dispid, pvar);
}

LRESULT CWnd::DefWindowProc(UINT nMsg, WPARAM wParam, LPARAM lParam) {
  if (m_pfnSuper != NULL) return ::CallWindowProc(m_pfnSuper, m_hWnd, nMsg, wParam, lParam);
  WNDPROC pfnWndProc = *GetSuperWndProcAddr();
  if (pfnWndProc == NULL) return ::DefWindowProc(m_hWnd, nMsg, wParam, lParam);
  return ::CallWindowProc(pfnWndProc, m_hWnd, nMsg, wParam, lParam);
}

LRESULT CWnd::Default() {
  MSG* pMsg = &AfxGetThreadState()->m_lastSentMsg;
  return DefWindowProc(pMsg->message, pMsg->wParam, pMsg->lParam);
}

const MSG* PASCAL CWnd::GetCurrentMessage() { return &AfxGetThreadState()->m_lastSentMsg; }

// Finds the entry for a window message, including registered messages.
static const AFX_MSGMAP_ENTRY* _AfxFindWndMsg(const AFX_MSGMAP_ENTRY* lpEntries, UINT nMsg) {
  if (nMsg < 0xC000) return CCmdTarget::FindMessageEntry(lpEntries, nMsg, 0, 0);
  for (const AFX_MSGMAP_ENTRY* p = lpEntries; p->nSig != AfxSig_end; p++) {
    if (p->nSig > AfxSig_registered && p->nMessage == 0xC000 && *(UINT*)p->nSig == nMsg)
      return p;
  }
  return NULL;
}

BOOL CWnd::OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  LRESULT lResult = 0;

  // Commands and notifications are routed, not looked up.
  if (message == WM_COMMAND) {
    if (!OnCommand(wParam, lParam)) return FALSE;
    lResult = 1;
    goto LReturnTrue;
  }
  if (message == WM_NOTIFY) {
    NMHDR* pNMHDR = (NMHDR*)lParam;
    if (pNMHDR == NULL || pNMHDR->hwndFrom == NULL || !OnNotify(wParam, lParam, &lResult))
      return FALSE;
    goto LReturnTrue;
  }

  // A drag list box's notification goes back to the list box first.
  if (message >= 0xC000) {
    static const UINT s_nDragListMsg = ::RegisterWindowMessage(_T("commctrl_DragListMsg"));
    if (message == s_nDragListMsg && lParam != 0 &&
        ReflectLastMsg(((DRAGLISTINFO*)lParam)->hWnd, &lResult))
      goto LReturnTrue;
  }

  {
    BOOL bCtlColor = message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC;
    for (const AFX_MSGMAP* pMap = GetMessageMap(); pMap != NULL;
         pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
      const AFX_MSGMAP_ENTRY* lpEntry = _AfxFindWndMsg(pMap->lpEntries, message);
      // ON_WM_CTLCOLOR covers all the WM_CTLCOLORxxx messages.
      if (lpEntry == NULL && bCtlColor)
        lpEntry = CCmdTarget::FindMessageEntry(pMap->lpEntries, WM_CTLCOLOR, 0, 0);
      if (lpEntry != NULL) {
        lResult = lpEntry->pThunk(this, message, wParam, lParam);
        goto LReturnTrue;
      }
    }
  }
  return FALSE;

LReturnTrue:
  if (pResult != NULL) *pResult = lResult;
  return TRUE;
}

BOOL CWnd::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam);
  HWND hWndCtrl = (HWND)lParam;
  int nCode = HIWORD(wParam);

  if (hWndCtrl == NULL) {
    // A menu item or accelerator: check it is still enabled.
    if (nID == 0) return FALSE;
    CTestCmdUI state;
    state.m_nID = nID;
    OnCmdMsg(nID, (int)CN_UPDATE_COMMAND_UI, &state, NULL);
    if (!state.m_bEnabled) return TRUE;  // swallow disabled commands
    nCode = CN_COMMAND;
  } else {
    // A control notification: let the control handle it first.
    if (AfxGetThreadState()->m_hLockoutNotifyWindow == m_hWnd) return TRUE;
    if (ReflectLastMsg(hWndCtrl)) return TRUE;
    if (nID == 0) return FALSE;
  }
  return OnCmdMsg(nID, nCode, NULL, NULL);
}

BOOL CWnd::OnNotify(WPARAM, LPARAM lParam, LRESULT* pResult) {
  NMHDR* pNMHDR = (NMHDR*)lParam;
  HWND hWndCtrl = pNMHDR->hwndFrom;
  UINT nID = (UINT)pNMHDR->idFrom;
  int nCode = (int)pNMHDR->code;

  if (AfxGetThreadState()->m_hLockoutNotifyWindow == m_hWnd) return TRUE;
  if (ReflectLastMsg(hWndCtrl, pResult)) return TRUE;

  AFX_NOTIFY notify;
  notify.pResult = pResult;
  notify.pNMHDR = pNMHDR;
  return OnCmdMsg(nID, MAKELONG(nCode, WM_NOTIFY), &notify, NULL);
}

BOOL CWnd::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  return CCmdTarget::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

BOOL CWnd::PreTranslateMessage(MSG* pMsg) {
  (void)pMsg;
  return FALSE;
}

BOOL PASCAL CWnd::WalkPreTranslateTree(HWND hWndStop, MSG* pMsg) {
  for (HWND hWnd = pMsg->hwnd; hWnd != NULL; hWnd = ::GetParent(hWnd)) {
    CWnd* pWnd = CWnd::FromHandlePermanent(hWnd);
    if (pWnd != NULL && pWnd->PreTranslateMessage(pMsg)) return TRUE;
    if (hWnd == hWndStop) break;
  }
  return FALSE;
}

// ---------------------------------------------------------------------
// Reflection: controls get first chance at their own notifications.

BOOL PASCAL CWnd::ReflectLastMsg(HWND hWndChild, LRESULT* pResult) {
  CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
  if (pWnd == NULL) return FALSE;
  return pWnd->SendChildNotifyLastMsg(pResult);
}

BOOL CWnd::SendChildNotifyLastMsg(LRESULT* pResult) {
  MSG* pMsg = &AfxGetThreadState()->m_lastSentMsg;
  return OnChildNotify(pMsg->message, pMsg->wParam, pMsg->lParam, pResult);
}

BOOL CWnd::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  return ReflectChildNotify(message, wParam, lParam, pResult);
}

BOOL CWnd::ReflectChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  LRESULT lResult = 0;
  switch (message) {
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_PARENTNOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_DELETEITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_COMPAREITEM:
      if (CWnd::OnWndMsg(WM_REFLECT_BASE + message, wParam, lParam, &lResult)) {
        if (pResult != NULL) *pResult = lResult;
        return TRUE;
      }
      return FALSE;

    case WM_COMMAND: {
      int nCode = HIWORD(wParam);
      if (CCmdTarget::OnCmdMsg(0, MAKELONG(nCode, WM_REFLECT_BASE + WM_COMMAND), NULL, NULL)) {
        if (pResult != NULL) *pResult = 1;
        return TRUE;
      }
      return FALSE;
    }

    case WM_NOTIFY: {
      NMHDR* pNMHDR = (NMHDR*)lParam;
      LRESULT lNotify = 0;
      AFX_NOTIFY notify;
      notify.pResult = pResult != NULL ? pResult : &lNotify;
      notify.pNMHDR = pNMHDR;
      return CCmdTarget::OnCmdMsg(0, MAKELONG((int)pNMHDR->code, WM_REFLECT_BASE + WM_NOTIFY),
                                  &notify, NULL);
    }

    default:
      if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        // Reflected as WM_REFLECT_BASE + WM_CTLCOLOR, wParam = HDC,
        // lParam = CTLCOLOR_xxx. A NULL brush means "not handled".
        if (CWnd::OnWndMsg(WM_REFLECT_BASE + WM_CTLCOLOR, wParam,
                           (LPARAM)(message - WM_CTLCOLORMSGBOX), &lResult) &&
            lResult != 0) {
          if (pResult != NULL) *pResult = lResult;
          return TRUE;
        }
      }
      return FALSE;
  }
}

// ---------------------------------------------------------------------
// CWnd's own message map: default handlers that reflect to controls.

BEGIN_MESSAGE_MAP(CWnd, CCmdTarget)
  ON_WM_DRAWITEM()
  ON_WM_MEASUREITEM()
  ON_WM_CTLCOLOR()
  ON_WM_COMPAREITEM()
  ON_WM_DELETEITEM()
  ON_WM_CHARTOITEM()
  ON_WM_VKEYTOITEM()
  ON_WM_HSCROLL()
  ON_WM_VSCROLL()
  ON_WM_PARENTNOTIFY()
  ON_WM_NCDESTROY()
  ON_WM_SYSCOLORCHANGE()
  ON_MESSAGE(WM_DISPLAYCHANGE, OnDisplayChange)
END_MESSAGE_MAP()

#include "wnd_handlers.inc"

void CWnd::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct) {
  (void)nIDCtl;
  if (lpDrawItemStruct->CtlType == ODT_MENU) {
    CHandleMap* pMap = afxMapHMENU();
    CMenu* pMenu = pMap != NULL ? (CMenu*)pMap->LookupPermanent(lpDrawItemStruct->hwndItem) : NULL;
    if (pMenu != NULL) {
      pMenu->DrawItem(lpDrawItemStruct);
      return;
    }
  } else if (ReflectLastMsg(lpDrawItemStruct->hwndItem)) {
    return;
  }
  Default();
}

// Finds the permanent CMenu that holds a menu item, searching submenus.
static CMenu* _AfxFindPopupMenuFromID(HMENU hMenu, UINT nID) {
  if (hMenu == NULL) return NULL;
  CHandleMap* pMap = afxMapHMENU();
  int nCount = ::GetMenuItemCount(hMenu);
  for (int i = 0; i < nCount; i++) {
    HMENU hSub = ::GetSubMenu(hMenu, i);
    if (hSub != NULL) {
      CMenu* pFound = _AfxFindPopupMenuFromID(hSub, nID);
      if (pFound != NULL) return pFound;
    } else if (::GetMenuItemID(hMenu, i) == nID) {
      return pMap != NULL ? (CMenu*)pMap->LookupPermanent(hMenu) : NULL;
    }
  }
  return NULL;
}

void CWnd::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct) {
  if (lpMeasureItemStruct->CtlType == ODT_MENU) {
    CMenu* pMenu = NULL;
    AFX_THREAD_STATE* pState = AfxGetThreadState();
    if (pState->m_hTrackingWindow == m_hWnd)
      pMenu = _AfxFindPopupMenuFromID(pState->m_hTrackingMenu, lpMeasureItemStruct->itemID);
    if (pMenu == NULL) pMenu = _AfxFindPopupMenuFromID(::GetMenu(m_hWnd), lpMeasureItemStruct->itemID);
    if (pMenu != NULL) {
      pMenu->MeasureItem(lpMeasureItemStruct);
      return;
    }
  } else {
    HWND hWndChild = ::GetDlgItem(m_hWnd, nIDCtl);
    if (hWndChild != NULL && ReflectLastMsg(hWndChild)) return;
  }
  Default();
}

int CWnd::OnCompareItem(int nIDCtl, LPCOMPAREITEMSTRUCT lpCompareItemStruct) {
  (void)nIDCtl;
  LRESULT lResult;
  if (ReflectLastMsg(lpCompareItemStruct->hwndItem, &lResult)) return (int)lResult;
  return (int)Default();
}

void CWnd::OnDeleteItem(int nIDCtl, LPDELETEITEMSTRUCT lpDeleteItemStruct) {
  (void)nIDCtl;
  if (ReflectLastMsg(lpDeleteItemStruct->hwndItem)) return;
  Default();
}

int CWnd::OnCharToItem(UINT nChar, CListBox* pListBox, UINT nIndex) {
  (void)nChar;
  (void)nIndex;
  LRESULT lResult;
  if (pListBox != NULL && ReflectLastMsg(((CWnd*)pListBox)->m_hWnd, &lResult)) return (int)lResult;
  return (int)Default();
}

int CWnd::OnVKeyToItem(UINT nKey, CListBox* pListBox, UINT nIndex) {
  (void)nKey;
  (void)nIndex;
  LRESULT lResult;
  if (pListBox != NULL && ReflectLastMsg(((CWnd*)pListBox)->m_hWnd, &lResult)) return (int)lResult;
  return (int)Default();
}

HBRUSH CWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  (void)pDC;
  (void)nCtlColor;
  LRESULT lResult;
  if (pWnd != NULL && pWnd->m_hWnd != m_hWnd && ReflectLastMsg(pWnd->m_hWnd, &lResult))
    return (HBRUSH)lResult;
  return (HBRUSH)Default();
}

void CWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  (void)nSBCode;
  (void)nPos;
  if (pScrollBar != NULL && ReflectLastMsg(((CWnd*)pScrollBar)->m_hWnd)) return;
  Default();
}

void CWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  (void)nSBCode;
  (void)nPos;
  if (pScrollBar != NULL && ReflectLastMsg(((CWnd*)pScrollBar)->m_hWnd)) return;
  Default();
}

void CWnd::OnParentNotify(UINT message, LPARAM lParam) {
  if ((LOWORD(message) == WM_CREATE || LOWORD(message) == WM_DESTROY)) {
    if (ReflectLastMsg((HWND)lParam)) return;
  }
  Default();
}

void CWnd::OnEnterIdle(UINT nWhy, CWnd* pWho) {
  (void)nWhy;
  (void)pWho;
  Default();
}

void CWnd::OnSysColorChange() {
  if (!(GetStyle() & WS_CHILD)) SendMessageToDescendants(WM_SYSCOLORCHANGE, 0, 0, TRUE, TRUE);
  Default();
}

LRESULT CWnd::OnDisplayChange(WPARAM, LPARAM) {
  if (!(GetStyle() & WS_CHILD)) {
    const MSG* pMsg = GetCurrentMessage();
    SendMessageToDescendants(pMsg->message, pMsg->wParam, pMsg->lParam, TRUE, TRUE);
  }
  return Default();
}

LRESULT CWnd::OnDragList(WPARAM, LPARAM) { return Default(); }

void (AFXAPI* _afxPfnRevokeDropTarget)(COleDropTarget*) = NULL;  // set by COleDropTarget

void CWnd::OnNcDestroy() {
  if (m_pDropTarget != NULL && _afxPfnRevokeDropTarget != NULL)
    _afxPfnRevokeDropTarget(m_pDropTarget);
  m_pDropTarget = NULL;
  if ((m_pCtrlCont != NULL || m_pCtrlSite != NULL) && _afxOccHooks != NULL)
    _afxOccHooks->pfnNcDestroy(this);

  // The main window going away ends the thread's message loop.
  CWinThread* pThread = AfxGetThread();
  if (pThread != NULL) {
    if (pThread->m_pMainWnd == this) {
      AfxPostQuitMessage(0);
      pThread->m_pMainWnd = NULL;
    }
    if (pThread->m_pActiveWnd == this) pThread->m_pActiveWnd = NULL;
  }

  // Run the default processing, put the old window procedure back and
  // forget the handle.
  WNDPROC pfnWndProc = (WNDPROC)::GetWindowLongPtr(m_hWnd, GWLP_WNDPROC);
  Default();
  if ((WNDPROC)::GetWindowLongPtr(m_hWnd, GWLP_WNDPROC) == pfnWndProc) {
    WNDPROC pfnSuper = *GetSuperWndProcAddr();
    if (pfnSuper != NULL) ::SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)pfnSuper);
  }
  Detach();
  PostNcDestroy();
}

// ---------------------------------------------------------------------
// Window classes

BOOL AFXAPI AfxRegisterClass(WNDCLASS* lpWndClass) {
  WNDCLASS wndcls;
  if (::GetClassInfo(lpWndClass->hInstance, lpWndClass->lpszClassName, &wndcls)) return TRUE;
  if (::RegisterClass(lpWndClass) == 0) return FALSE;
  // Windows keeps a DLL's classes after the DLL unloads, with window
  // procedures that are gone, so a DLL remembers its classes and takes
  // them away in DllMain.
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  if (pModuleState->m_bDLL && !IS_INTRESOURCE(lpWndClass->lpszClassName)) {
    AfxLockGlobals(0);
    if (pModuleState->m_pstrUnregisterList == NULL)
      pModuleState->m_pstrUnregisterList = new CStringArray;
    pModuleState->m_pstrUnregisterList->Add(lpWndClass->lpszClassName);
    AfxUnlockGlobals(0);
  }
  return TRUE;
}

LPCTSTR AFXAPI AfxRegisterWndClass(UINT nClassStyle, HCURSOR hCursor, HBRUSH hbrBackground,
                                   HICON hIcon) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  LPTSTR lpszName = pState->m_szTempClassName;
  HINSTANCE hInst = AfxGetInstanceHandle();
  wsprintf(lpszName, _T("Afx:%p:%x:%p:%p:%p"), (void*)hInst, nClassStyle, (void*)hCursor,
           (void*)hbrBackground, (void*)hIcon);
  WNDCLASS wndcls;
  if (::GetClassInfo(hInst, lpszName, &wndcls)) return lpszName;
  memset(&wndcls, 0, sizeof(wndcls));
  wndcls.style = nClassStyle;
  wndcls.lpfnWndProc = ::DefWindowProc;
  wndcls.hInstance = hInst;
  wndcls.hIcon = hIcon;
  wndcls.hCursor = hCursor;
  wndcls.hbrBackground = hbrBackground;
  wndcls.lpszClassName = lpszName;
  if (!AfxRegisterClass(&wndcls)) AfxThrowResourceException();
  return lpszName;
}

static BOOL _AfxRegisterWithIcon(WNDCLASS* pWndCls, LPCTSTR lpszClassName, UINT nIDIcon) {
  pWndCls->lpszClassName = lpszClassName;
  pWndCls->hIcon = ::LoadIcon(AfxFindResourceHandle(MAKEINTRESOURCE(nIDIcon), RT_GROUP_ICON), MAKEINTRESOURCE(nIDIcon));
  if (pWndCls->hIcon == NULL) pWndCls->hIcon = ::LoadIcon(NULL, IDI_APPLICATION);
  return AfxRegisterClass(pWndCls);
}

typedef BOOL(WINAPI* PFN_INITCOMMONCONTROLSEX)(const INITCOMMONCONTROLSEX*);

static BOOL _AfxInitCommonControls(DWORD dwFlags) {
  // InitCommonControlsEx needs comctl32 4.70; fall back to the old call.
  HMODULE hComCtl = ::GetModuleHandleA("comctl32.dll");
  if (hComCtl == NULL) {
    ::InitCommonControls();
    hComCtl = ::GetModuleHandleA("comctl32.dll");
  }
  PFN_INITCOMMONCONTROLSEX pfn =
      hComCtl != NULL ? (PFN_INITCOMMONCONTROLSEX)(void*)::GetProcAddress(hComCtl, "InitCommonControlsEx") : NULL;
  if (pfn == NULL) {
    ::InitCommonControls();
    return TRUE;
  }
  INITCOMMONCONTROLSEX init;
  init.dwSize = sizeof(init);
  init.dwICC = dwFlags;
  return pfn(&init);
}

BOOL AFXAPI AfxDeferRegisterClass(LONG fToRegister) {
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  if ((pModuleState->m_fRegisteredClasses & fToRegister) == fToRegister) return TRUE;

  LONG fRegisteredClasses = 0;
  WNDCLASS wndcls;
  memset(&wndcls, 0, sizeof(wndcls));
  wndcls.lpfnWndProc = ::DefWindowProc;
  wndcls.hInstance = AfxGetInstanceHandle();
  wndcls.hCursor = ::LoadCursor(NULL, IDC_ARROW);

  if (fToRegister & AFX_WND_REG) {
    wndcls.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wndcls.lpszClassName = _afxWnd;
    if (AfxRegisterClass(&wndcls)) fRegisteredClasses |= AFX_WND_REG;
  }
  if (fToRegister & AFX_WNDCONTROLBAR_REG) {
    wndcls.style = 0;
    wndcls.lpszClassName = _afxWndControlBar;
    wndcls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (AfxRegisterClass(&wndcls)) fRegisteredClasses |= AFX_WNDCONTROLBAR_REG;
  }
  if (fToRegister & AFX_WNDMDIFRAME_REG) {
    wndcls.style = CS_DBLCLKS;
    wndcls.hbrBackground = NULL;
    if (_AfxRegisterWithIcon(&wndcls, _afxWndMDIFrame, AFX_IDI_STD_MDIFRAME))
      fRegisteredClasses |= AFX_WNDMDIFRAME_REG;
  }
  if (fToRegister & AFX_WNDFRAMEORVIEW_REG) {
    wndcls.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wndcls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (_AfxRegisterWithIcon(&wndcls, _afxWndFrameOrView, AFX_IDI_STD_FRAME))
      fRegisteredClasses |= AFX_WNDFRAMEORVIEW_REG;
  }

  // The common controls register their own classes.
  struct {
    LONG fReg;
    DWORD dwICC;
  } static const commctls[] = {
      {AFX_WNDCOMMCTLS_REG, ICC_WIN95_CLASSES},
      {AFX_WNDCOMMCTL_UPDOWN_REG, ICC_UPDOWN_CLASS},
      {AFX_WNDCOMMCTL_TREEVIEW_REG, ICC_TREEVIEW_CLASSES},
      {AFX_WNDCOMMCTL_TAB_REG, ICC_TAB_CLASSES},
      {AFX_WNDCOMMCTL_PROGRESS_REG, ICC_PROGRESS_CLASS},
      {AFX_WNDCOMMCTL_LISTVIEW_REG, ICC_LISTVIEW_CLASSES},
      {AFX_WNDCOMMCTL_HOTKEY_REG, ICC_HOTKEY_CLASS},
      {AFX_WNDCOMMCTL_BAR_REG, ICC_BAR_CLASSES},
      {AFX_WNDCOMMCTL_ANIMATE_REG, ICC_ANIMATE_CLASS},
      {AFX_WNDCOMMCTL_INTERNET_REG, ICC_INTERNET_CLASSES},
      {AFX_WNDCOMMCTL_COOL_REG, ICC_COOL_CLASSES},
      {AFX_WNDCOMMCTL_USEREX_REG, ICC_USEREX_CLASSES},
      {AFX_WNDCOMMCTL_DATE_REG, ICC_DATE_CLASSES},
      {AFX_WNDCOMMCTL_PAGER_REG, ICC_PAGESCROLLER_CLASS},
      {AFX_WNDCOMMCTL_LINK_REG, ICC_LINK_CLASS},
  };
  for (size_t i = 0; i < sizeof(commctls) / sizeof(commctls[0]); i++) {
    if ((fToRegister & commctls[i].fReg) && _AfxInitCommonControls(commctls[i].dwICC))
      fRegisteredClasses |= commctls[i].fReg;
  }

  AfxLockGlobals(0);
  pModuleState->m_fRegisteredClasses |= fRegisteredClasses;
  AfxUnlockGlobals(0);
  return (fRegisteredClasses & fToRegister) == fToRegister;
}

void AFXAPI AfxPostQuitMessage(int nExitCode) { ::PostQuitMessage(nExitCode); }
