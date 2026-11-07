// oledrop.cpp - drag and drop: COleDataSource::DoDragDrop, COleDropSource,
// COleDropTarget, and the CView functions a drop target calls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

IMPLEMENT_DYNAMIC(COleDropSource, CCmdTarget)
IMPLEMENT_DYNAMIC(COleDropTarget, CCmdTarget)

BEGIN_INTERFACE_MAP(COleDropSource, CCmdTarget)
  INTERFACE_PART(COleDropSource, IID_IDropSource, DropSource)
END_INTERFACE_MAP()

BEGIN_INTERFACE_MAP(COleDropTarget, CCmdTarget)
  INTERFACE_PART(COleDropTarget, IID_IDropTarget, DropTarget)
END_INTERFACE_MAP()

UINT COleDropSource::nDragMinDist = (UINT)-1;
UINT COleDropSource::nDragDelay = (UINT)-1;
int COleDropTarget::nScrollInset = -1;
UINT COleDropTarget::nScrollDelay = (UINT)-1;
UINT COleDropTarget::nScrollInterval = (UINT)-1;

static void LoadDragSettings() {
  if (COleDropSource::nDragDelay != (UINT)-1) return;
  COleDropSource::nDragMinDist = ::GetProfileInt(_T("windows"), _T("DragMinDist"), DD_DEFDRAGMINDIST);
  COleDropSource::nDragDelay = ::GetProfileInt(_T("windows"), _T("DragDelay"), DD_DEFDRAGDELAY);
  COleDropTarget::nScrollInset =
      (int)::GetProfileInt(_T("windows"), _T("DragScrollInset"), DD_DEFSCROLLINSET);
  COleDropTarget::nScrollDelay = ::GetProfileInt(_T("windows"), _T("DragScrollDelay"), DD_DEFSCROLLDELAY);
  COleDropTarget::nScrollInterval =
      ::GetProfileInt(_T("windows"), _T("DragScrollInterval"), DD_DEFSCROLLINTERVAL);
}

// ---------------------------------------------------------------------
// COleDropSource

COleDropSource::COleDropSource()
    : m_bDragStarted(FALSE), m_dwButtonCancel(0), m_dwButtonDrop(0), m_rectStartDrag(0, 0, 0, 0) {
  LoadDragSettings();
}

BOOL COleDropSource::OnBeginDrag(CWnd* pWnd) {
  m_bDragStarted = FALSE;
  if (m_rectStartDrag.IsRectNull()) {
    m_bDragStarted = TRUE;
    return TRUE;
  }
  // Wait, with the mouse captured, until it leaves the rectangle or the
  // delay is up.
  HWND hWnd = pWnd != NULL ? pWnd->m_hWnd : NULL;
  if (hWnd != NULL) ::SetCapture(hWnd);
  DWORD dwStart = ::GetTickCount();
  BOOL bResult = FALSE;
  for (;;) {
    if (::GetTickCount() - dwStart >= nDragDelay) {
      bResult = TRUE;
      break;
    }
    BOOL bButton = (::GetAsyncKeyState(m_dwButtonDrop == MK_RBUTTON ? VK_RBUTTON : VK_LBUTTON) &
                    0x8000) != 0;
    if (!bButton) break;
    POINT pt;
    ::GetCursorPos(&pt);
    if (!m_rectStartDrag.PtInRect(pt)) {
      bResult = TRUE;
      break;
    }
    MSG msg;
    if (::PeekMessage(&msg, NULL, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE) && msg.wParam == VK_ESCAPE)
      break;
    ::MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_INPUT);
  }
  if (hWnd != NULL && ::GetCapture() == hWnd) ::ReleaseCapture();
  m_bDragStarted = bResult;
  return bResult;
}

SCODE COleDropSource::QueryContinueDrag(BOOL bEscapePressed, DWORD dwKeyState) {
  if (bEscapePressed || (dwKeyState & m_dwButtonCancel) != 0) {
    m_bDragStarted = FALSE;
    return DRAGDROP_S_CANCEL;
  }
  if ((dwKeyState & m_dwButtonDrop) == 0) return m_bDragStarted ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL;
  return S_OK;
}

SCODE COleDropSource::GiveFeedback(DROPEFFECT) {
  m_bDragStarted = TRUE;
  return DRAGDROP_S_USEDEFAULTCURSORS;
}

STDMETHODIMP_(ULONG) COleDropSource::XDropSource::AddRef() {
  METHOD_PROLOGUE(COleDropSource, DropSource)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleDropSource::XDropSource::Release() {
  METHOD_PROLOGUE(COleDropSource, DropSource)
  return pThis->ExternalRelease();
}
STDMETHODIMP COleDropSource::XDropSource::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE(COleDropSource, DropSource)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleDropSource::XDropSource::QueryContinueDrag(BOOL fEscapePressed,
                                                            DWORD grfKeyState) {
  METHOD_PROLOGUE(COleDropSource, DropSource)
  return pThis->QueryContinueDrag(fEscapePressed, grfKeyState);
}
STDMETHODIMP COleDropSource::XDropSource::GiveFeedback(DWORD dwEffect) {
  METHOD_PROLOGUE(COleDropSource, DropSource)
  return pThis->GiveFeedback(dwEffect);
}

// ---------------------------------------------------------------------
// COleDataSource::DoDragDrop

DROPEFFECT COleDataSource::DoDragDrop(DWORD dwEffects, LPCRECT lpRectStartDrag,
                                      COleDropSource* pDropSource) {
  COleDropSource dropSource;
  if (pDropSource == NULL) pDropSource = &dropSource;

  // The button held now drops; the other one cancels.
  if (::GetKeyState(VK_RBUTTON) < 0 && ::GetKeyState(VK_LBUTTON) >= 0) {
    pDropSource->m_dwButtonDrop = MK_RBUTTON;
    pDropSource->m_dwButtonCancel = MK_LBUTTON | MK_MBUTTON;
  } else {
    pDropSource->m_dwButtonDrop = MK_LBUTTON;
    pDropSource->m_dwButtonCancel = MK_RBUTTON | MK_MBUTTON;
  }
  if (lpRectStartDrag != NULL) {
    pDropSource->m_rectStartDrag = *lpRectStartDrag;
  } else {
    POINT pt;
    ::GetCursorPos(&pt);
    int n = (int)COleDropSource::nDragMinDist;
    pDropSource->m_rectStartDrag.SetRect(pt.x - n, pt.y - n, pt.x + n + 1, pt.y + n + 1);
  }

  HWND hWnd = ::GetCapture();
  if (hWnd == NULL) hWnd = ::GetFocus();
  if (!pDropSource->OnBeginDrag(hWnd != NULL ? CWnd::FromHandle(hWnd) : NULL))
    return DROPEFFECT_NONE;

  DWORD dwResult = DROPEFFECT_NONE;
  HRESULT hr = ::DoDragDrop((LPDATAOBJECT)&m_xDataObject, (LPDROPSOURCE)&pDropSource->m_xDropSource,
                            dwEffects, &dwResult);
  return hr == DRAGDROP_S_DROP ? dwResult : DROPEFFECT_NONE;
}

// ---------------------------------------------------------------------
// COleDropTarget

extern void (AFXAPI* _afxPfnRevokeDropTarget)(COleDropTarget*);

static void AFXAPI RevokeDropTarget(COleDropTarget* pTarget) { pTarget->Revoke(); }

COleDropTarget::COleDropTarget()
    : m_hWnd(NULL), m_lpDataObject(NULL), m_dropEffectCurrent(DROPEFFECT_NONE) {
  LoadDragSettings();
}

COleDropTarget::~COleDropTarget() { Revoke(); }

BOOL COleDropTarget::Register(CWnd* pWnd) {
  if (pWnd == NULL || pWnd->m_hWnd == NULL) return FALSE;
  if (FAILED(::RegisterDragDrop(pWnd->m_hWnd, (LPDROPTARGET)&m_xDropTarget))) return FALSE;
  m_hWnd = pWnd->m_hWnd;
  pWnd->m_pDropTarget = this;
  _afxPfnRevokeDropTarget = RevokeDropTarget;
  return TRUE;
}

void COleDropTarget::Revoke() {
  if (m_hWnd == NULL) return;
  ::RevokeDragDrop(m_hWnd);
  CWnd* pWnd = CWnd::FromHandlePermanent(m_hWnd);
  if (pWnd != NULL && pWnd->m_pDropTarget == this) pWnd->m_pDropTarget = NULL;
  m_hWnd = NULL;
}

DROPEFFECT COleDropTarget::OnDragEnter(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState,
                                       CPoint point) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  return pView != NULL ? pView->OnDragEnter(pDataObject, dwKeyState, point) : DROPEFFECT_NONE;
}

DROPEFFECT COleDropTarget::OnDragOver(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState,
                                      CPoint point) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  return pView != NULL ? pView->OnDragOver(pDataObject, dwKeyState, point) : DROPEFFECT_NONE;
}

BOOL COleDropTarget::OnDrop(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect,
                            CPoint point) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  return pView != NULL ? pView->OnDrop(pDataObject, dropEffect, point) : FALSE;
}

DROPEFFECT COleDropTarget::OnDropEx(CWnd* pWnd, COleDataObject* pDataObject,
                                    DROPEFFECT dropDefault, DROPEFFECT dropList, CPoint point) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  return pView != NULL ? pView->OnDropEx(pDataObject, dropDefault, dropList, point)
                       : (DROPEFFECT)-1;
}

void COleDropTarget::OnDragLeave(CWnd* pWnd) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  if (pView != NULL) pView->OnDragLeave();
}

DROPEFFECT COleDropTarget::OnDragScroll(CWnd* pWnd, DWORD dwKeyState, CPoint point) {
  CView* pView = DYNAMIC_DOWNCAST(CView, pWnd);
  return pView != NULL ? pView->OnDragScroll(dwKeyState, point) : DROPEFFECT_NONE;
}

// The effect a target chose, kept to what the source allows.
static DWORD Allowed(DROPEFFECT effect, DWORD dwAllowed) {
  DWORD dwScroll = effect & DROPEFFECT_SCROLL;
  effect &= ~DROPEFFECT_SCROLL;
  if ((effect & dwAllowed) == 0) {
    // A move the source does not allow may be a copy.
    effect = (effect == DROPEFFECT_MOVE && (dwAllowed & DROPEFFECT_COPY)) ? DROPEFFECT_COPY
                                                                          : DROPEFFECT_NONE;
  } else {
    effect &= dwAllowed;
  }
  return effect | dwScroll;
}

STDMETHODIMP_(ULONG) COleDropTarget::XDropTarget::AddRef() {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleDropTarget::XDropTarget::Release() {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  return pThis->ExternalRelease();
}
STDMETHODIMP COleDropTarget::XDropTarget::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleDropTarget::XDropTarget::DragEnter(LPDATAOBJECT pDataObj, DWORD grfKeyState,
                                                    POINTL pt, LPDWORD pdwEffect) {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  if (pdwEffect == NULL) return E_INVALIDARG;
  CWnd* pWnd = CWnd::FromHandle(pThis->m_hWnd);
  CPoint point(pt.x, pt.y);
  pWnd->ScreenToClient(&point);
  if (pThis->m_lpDataObject != NULL) pThis->m_lpDataObject->Release();
  pThis->m_lpDataObject = pDataObj;
  if (pDataObj != NULL) pDataObj->AddRef();
  COleDataObject dataObject;
  dataObject.Attach(pDataObj, FALSE);
  DWORD dwAllowed = *pdwEffect;
  DROPEFFECT effect = DROPEFFECT_NONE;
  try {
    effect = pThis->OnDragScroll(pWnd, grfKeyState, point);
    if ((effect & DROPEFFECT_SCROLL) == 0)
      effect = pThis->OnDragEnter(pWnd, &dataObject, grfKeyState, point);
  } catch (CException* e) {
    e->Delete();
    effect = DROPEFFECT_NONE;
  }
  pThis->m_dropEffectCurrent = *pdwEffect = Allowed(effect, dwAllowed);
  return S_OK;
}

STDMETHODIMP COleDropTarget::XDropTarget::DragOver(DWORD grfKeyState, POINTL pt,
                                                   LPDWORD pdwEffect) {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  if (pdwEffect == NULL) return E_INVALIDARG;
  CWnd* pWnd = CWnd::FromHandle(pThis->m_hWnd);
  CPoint point(pt.x, pt.y);
  pWnd->ScreenToClient(&point);
  COleDataObject dataObject;
  dataObject.Attach(pThis->m_lpDataObject, FALSE);
  DWORD dwAllowed = *pdwEffect;
  DROPEFFECT effect = DROPEFFECT_NONE;
  try {
    effect = pThis->OnDragScroll(pWnd, grfKeyState, point);
    if ((effect & DROPEFFECT_SCROLL) == 0)
      effect = pThis->OnDragOver(pWnd, &dataObject, grfKeyState, point);
  } catch (CException* e) {
    e->Delete();
    effect = DROPEFFECT_NONE;
  }
  pThis->m_dropEffectCurrent = *pdwEffect = Allowed(effect, dwAllowed);
  return S_OK;
}

STDMETHODIMP COleDropTarget::XDropTarget::DragLeave() {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  CWnd* pWnd = CWnd::FromHandle(pThis->m_hWnd);
  try {
    pThis->OnDragLeave(pWnd);
  } catch (CException* e) {
    e->Delete();
  }
  if (pThis->m_lpDataObject != NULL) {
    pThis->m_lpDataObject->Release();
    pThis->m_lpDataObject = NULL;
  }
  return S_OK;
}

STDMETHODIMP COleDropTarget::XDropTarget::Drop(LPDATAOBJECT pDataObj, DWORD grfKeyState,
                                               POINTL pt, LPDWORD pdwEffect) {
  METHOD_PROLOGUE(COleDropTarget, DropTarget)
  if (pdwEffect == NULL) return E_INVALIDARG;
  CWnd* pWnd = CWnd::FromHandle(pThis->m_hWnd);
  CPoint point(pt.x, pt.y);
  pWnd->ScreenToClient(&point);
  COleDataObject dataObject;
  dataObject.Attach(pDataObj, FALSE);
  DWORD dwAllowed = *pdwEffect;
  DROPEFFECT effect = DROPEFFECT_NONE;
  try {
    // The effect the target would choose here, then the drop itself.
    effect = Allowed(pThis->OnDragOver(pWnd, &dataObject, grfKeyState, point), dwAllowed) &
             ~DROPEFFECT_SCROLL;
    DROPEFFECT result = pThis->OnDropEx(pWnd, &dataObject, effect, dwAllowed, point);
    if (result == (DROPEFFECT)-1)
      result = (effect != DROPEFFECT_NONE && pThis->OnDrop(pWnd, &dataObject, effect, point))
                   ? effect
                   : DROPEFFECT_NONE;
    effect = result;
  } catch (CException* e) {
    e->Delete();
    effect = DROPEFFECT_NONE;
  }
  *pdwEffect = effect;
  if (pThis->m_lpDataObject != NULL) {
    pThis->m_lpDataObject->Release();
    pThis->m_lpDataObject = NULL;
  }
  return S_OK;
}

// ---------------------------------------------------------------------
// CView: by default a view takes no drops. OnDragScroll scrolls a view
// that has scroll bars when the mouse is near its edge.

DROPEFFECT CView::OnDragEnter(COleDataObject*, DWORD, CPoint) { return DROPEFFECT_NONE; }
DROPEFFECT CView::OnDragOver(COleDataObject*, DWORD, CPoint) { return DROPEFFECT_NONE; }
void CView::OnDragLeave() {}
BOOL CView::OnDrop(COleDataObject*, DROPEFFECT, CPoint) { return FALSE; }
DROPEFFECT CView::OnDropEx(COleDataObject*, DROPEFFECT, DROPEFFECT, CPoint) {
  return (DROPEFFECT)-1;  // use OnDrop
}

DROPEFFECT CView::OnDragScroll(DWORD dwKeyState, CPoint point) {
  DWORD dwStyle = GetStyle();
  CScrollBar* pBar = GetScrollBarCtrl(SB_VERT);
  BOOL bVert = (dwStyle & WS_VSCROLL) || (pBar != NULL && pBar->IsWindowEnabled());
  pBar = GetScrollBarCtrl(SB_HORZ);
  BOOL bHorz = (dwStyle & WS_HSCROLL) || (pBar != NULL && pBar->IsWindowEnabled());
  if (!bVert && !bHorz) return DROPEFFECT_NONE;

  CRect rect;
  GetClientRect(&rect);
  int nInset = COleDropTarget::nScrollInset > 0 ? COleDropTarget::nScrollInset : DD_DEFSCROLLINSET;
  CRect rectInner = rect;
  rectInner.InflateRect(-nInset, -nInset);
  if (!rect.PtInRect(point) || rectInner.PtInRect(point)) return DROPEFFECT_NONE;

  // OnScroll takes the horizontal code in the low byte, the vertical in
  // the high byte; 0xFF is none.
  BYTE h = 0xFF, v = 0xFF;
  if (bHorz) {
    if (point.x < rectInner.left)
      h = SB_LINEUP;
    else if (point.x >= rectInner.right)
      h = SB_LINEDOWN;
  }
  if (bVert) {
    if (point.y < rectInner.top)
      v = SB_LINEUP;
    else if (point.y >= rectInner.bottom)
      v = SB_LINEDOWN;
  }
  UINT nCode = MAKEWORD(h, v);
  if (nCode == 0xFFFF || !OnScroll(nCode, 0, FALSE)) return DROPEFFECT_NONE;
  OnScroll(nCode, 0, TRUE);
  UpdateWindow();
  DROPEFFECT effect = (dwKeyState & MK_CONTROL) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
  return effect | DROPEFFECT_SCROLL;
}
