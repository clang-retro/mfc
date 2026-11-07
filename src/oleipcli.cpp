// oleipcli.cpp - in-place activation, the container's side: the item's
// IOleInPlaceSite, and COleFrameHook, which is the container's frame
// (IOleInPlaceFrame) to an object active in place there.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

// ---------------------------------------------------------------------
// Shared menus. A container's in-place menu and a server's in-place menu
// are each split into groups by separators: the container's are groups
// 0, 2 and 4 (File, Container, Window) and the object's 1, 3 and 5 (Edit,
// Object, Help). The popups go into the shared menu as they are.

void AFXAPI _AfxMergeMenus(HMENU hMenuShared, HMENU hMenuSource, LONG* lpMenuWidths,
                           int nFirstGroup) {
  if (hMenuShared == NULL || hMenuSource == NULL) return;
  int nGroup = nFirstGroup;
  int nCount = ::GetMenuItemCount(hMenuSource);
  for (int i = 0; i < nCount; i++) {
    TCHAR szText[256];
    MENUITEMINFO mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_TYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE;
    mii.dwTypeData = szText;
    mii.cch = sizeof(szText) / sizeof(szText[0]);
    if (!::GetMenuItemInfo(hMenuSource, i, TRUE, &mii)) continue;
    if (mii.fType & MFT_SEPARATOR) {
      // The next group; extra separators leave the rest in the last one.
      if (nGroup + 2 <= nFirstGroup + 4) nGroup += 2;
      continue;
    }
    int nPos = 0;
    for (int g = 0; g <= nGroup; g++) nPos += (int)lpMenuWidths[g];
    mii.cch = lstrlen(szText);
    if (::InsertMenuItem(hMenuShared, nPos, TRUE, &mii)) lpMenuWidths[nGroup]++;
  }
}

static BOOL MenuHasItem(HMENU hMenu, HMENU hSubMenu, UINT nID) {
  int nCount = ::GetMenuItemCount(hMenu);
  for (int i = 0; i < nCount; i++) {
    HMENU hSub = ::GetSubMenu(hMenu, i);
    if (hSubMenu != NULL ? hSub == hSubMenu
                         : (hSub == NULL && nID != 0 && ::GetMenuItemID(hMenu, i) == nID))
      return TRUE;
  }
  return FALSE;
}

void AFXAPI _AfxUnmergeMenus(HMENU hMenuShared, HMENU hMenuSource) {
  if (hMenuShared == NULL || hMenuSource == NULL) return;
  for (int i = ::GetMenuItemCount(hMenuShared) - 1; i >= 0; i--) {
    HMENU hSub = ::GetSubMenu(hMenuShared, i);
    UINT nID = hSub == NULL ? ::GetMenuItemID(hMenuShared, i) : 0;
    if (MenuHasItem(hMenuSource, hSub, nID)) ::RemoveMenu(hMenuShared, i, MF_BYPOSITION);
  }
}

// ---------------------------------------------------------------------
// COleFrameHook

BEGIN_INTERFACE_MAP(COleFrameHook, CCmdTarget)
  INTERFACE_PART(COleFrameHook, IID_IOleWindow, OleInPlaceFrame)
  INTERFACE_PART(COleFrameHook, IID_IOleInPlaceUIWindow, OleInPlaceFrame)
  INTERFACE_PART(COleFrameHook, IID_IOleInPlaceFrame, OleInPlaceFrame)
END_INTERFACE_MAP()

COleFrameHook::COleFrameHook(CFrameWnd* pFrameWnd, COleClientItem* pItem)
    : m_pFrameWnd(pFrameWnd),
      m_hWnd(pFrameWnd->m_hWnd),
      m_pActiveItem(pItem),
      m_lpActiveObject(NULL),
      m_hSharedMenu(NULL),
      m_hOleMenu(NULL),
      m_bInModalState(FALSE),
      m_bInBorderChange(FALSE) {
  pFrameWnd->m_pNotifyHook = this;
}

COleFrameHook::~COleFrameHook() {
  if (m_lpActiveObject != NULL) {
    m_lpActiveObject->Release();
    m_lpActiveObject = NULL;
  }
  if (m_pFrameWnd != NULL) {
    if (m_bInModalState) m_pFrameWnd->EndModalState();
    if (m_pFrameWnd->m_pNotifyHook == this) m_pFrameWnd->m_pNotifyHook = NULL;
    if (m_hSharedMenu != NULL) {
      m_hSharedMenu = NULL;
      ::OleSetMenuDescriptor(NULL, m_hWnd, NULL, NULL, NULL);
      m_pFrameWnd->OnUpdateFrameMenu(NULL);
    }
    while (!m_listHiddenBars.IsEmpty()) {
      CControlBar* pBar = (CControlBar*)m_listHiddenBars.RemoveHead();
      if (pBar->m_hWnd != NULL) m_pFrameWnd->ShowControlBar(pBar, TRUE, TRUE);
    }
  }
}

void COleFrameHook::OnFrameDestroy() {
  if (m_pFrameWnd != NULL && m_pFrameWnd->m_pNotifyHook == this)
    m_pFrameWnd->m_pNotifyHook = NULL;
  m_pFrameWnd = NULL;
  m_listHiddenBars.RemoveAll();
}

static CDocTemplate* ItemTemplate(COleClientItem* pItem) {
  if (pItem == NULL || pItem->GetDocument() == NULL) return NULL;
  return pItem->GetDocument()->GetDocTemplate();
}

HACCEL COleFrameHook::GetInPlaceAccel() const {
  CDocTemplate* pTemplate = ItemTemplate(m_pActiveItem);
  return pTemplate != NULL ? pTemplate->m_hAccelInPlace : NULL;
}

HMENU COleFrameHook::GetInPlaceMenu() const {
  CDocTemplate* pTemplate = ItemTemplate(m_pActiveItem);
  return pTemplate != NULL ? pTemplate->m_hMenuInPlace : NULL;
}

// The object's keys first, then the container's in-place ones; the
// frame's usual accelerators are not used while the object is active.
BOOL COleFrameHook::OnPreTranslateMessage(MSG* pMsg) {
  if (m_lpActiveObject != NULL && m_lpActiveObject->TranslateAccelerator(pMsg) == S_OK)
    return TRUE;
  HACCEL hAccel = GetInPlaceAccel();
  return hAccel != NULL && ::TranslateAccelerator(m_hWnd, hAccel, pMsg);
}

void COleFrameHook::OnActivate(BOOL bActive) {
  if (m_lpActiveObject != NULL) m_lpActiveObject->OnFrameWindowActivate(bActive);
}

void COleFrameHook::OnDocActivate(BOOL bActive) {
  if (m_lpActiveObject != NULL) m_lpActiveObject->OnDocWindowActivate(bActive);
}

// The frame has laid its own bars out: the object places its bars in
// the border that is left.
void COleFrameHook::OnRecalcLayout() {
  if (m_lpActiveObject == NULL || m_bInBorderChange || m_pFrameWnd == NULL) return;
  CRect rect;
  m_pFrameWnd->NegotiateBorderSpace(1, &rect);
  m_bInBorderChange = TRUE;
  BOOL bFrame = m_pFrameWnd->GetParentFrame() == NULL;
  m_lpActiveObject->ResizeBorder(&rect, (LPOLEINPLACEUIWINDOW)&m_xOleInPlaceFrame, bFrame);
  m_bInBorderChange = FALSE;
}

BOOL COleFrameHook::OnUpdateFrameMenu() {
  if (m_hSharedMenu == NULL || m_pFrameWnd == NULL) return FALSE;
  if (!(m_pFrameWnd->GetStyle() & WS_CHILD) && ::GetMenu(m_hWnd) != m_hSharedMenu)
    ::SetMenu(m_hWnd, m_hSharedMenu);
  return TRUE;
}

STDMETHODIMP_(ULONG) COleFrameHook::XOleInPlaceFrame::AddRef() {
  METHOD_PROLOGUE_EX_(COleFrameHook, OleInPlaceFrame)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleFrameHook::XOleInPlaceFrame::Release() {
  METHOD_PROLOGUE_EX_(COleFrameHook, OleInPlaceFrame)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleFrameHook, OleInPlaceFrame)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE_EX_(COleFrameHook, OleInPlaceFrame)
  if (phwnd == NULL) return E_POINTER;
  *phwnd = pThis->m_hWnd;
  return pThis->m_hWnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::ContextSensitiveHelp(BOOL) { return S_OK; }

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::GetBorder(LPRECT lprectBorder) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  if (lprectBorder == NULL) return E_POINTER;
  if (pThis->m_pFrameWnd == NULL) return E_UNEXPECTED;
  pThis->m_pFrameWnd->NegotiateBorderSpace(1, lprectBorder);
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::RequestBorderSpace(LPCBORDERWIDTHS pborderwidths) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  if (pThis->m_pFrameWnd == NULL) return E_UNEXPECTED;
  if (pborderwidths == NULL) return S_OK;
  CRect rect;
  pThis->m_pFrameWnd->GetClientRect(&rect);
  if (pborderwidths->left + pborderwidths->right > rect.Width() ||
      pborderwidths->top + pborderwidths->bottom > rect.Height())
    return INPLACE_E_NOTOOLSPACE;
  return S_OK;
}

// The object's bars take the border given; the container's own bars make
// way for them while they are up.
STDMETHODIMP COleFrameHook::XOleInPlaceFrame::SetBorderSpace(LPCBORDERWIDTHS pborderwidths) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  CFrameWnd* pFrame = pThis->m_pFrameWnd;
  if (pFrame == NULL) return E_UNEXPECTED;
  BOOL bSaved = pThis->m_bInBorderChange;
  pThis->m_bInBorderChange = TRUE;
  if (pborderwidths == NULL) {
    if (pThis->m_pActiveItem != NULL) pThis->m_pActiveItem->OnShowControlBars(pFrame, TRUE);
    pFrame->NegotiateBorderSpace(3, NULL);
  } else {
    if (pThis->m_pActiveItem != NULL) pThis->m_pActiveItem->OnShowControlBars(pFrame, FALSE);
    CRect rect(pborderwidths->left, pborderwidths->top, pborderwidths->right,
               pborderwidths->bottom);
    pFrame->NegotiateBorderSpace(3, &rect);
  }
  pThis->m_bInBorderChange = bSaved;
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::SetActiveObject(
    LPOLEINPLACEACTIVEOBJECT pActiveObject, LPCOLESTR pszObjName) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  if (pActiveObject != NULL) pActiveObject->AddRef();
  if (pThis->m_lpActiveObject != NULL) pThis->m_lpActiveObject->Release();
  pThis->m_lpActiveObject = pActiveObject;
  pThis->m_strObjName = pszObjName != NULL ? CString(pszObjName) : CString();
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::InsertMenus(HMENU hmenuShared,
                                                          LPOLEMENUGROUPWIDTHS lpMenuWidths) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  if (lpMenuWidths == NULL) return E_POINTER;
  lpMenuWidths->width[0] = lpMenuWidths->width[2] = lpMenuWidths->width[4] = 0;
  _AfxMergeMenus(hmenuShared, pThis->GetInPlaceMenu(), lpMenuWidths->width, 0);
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::SetMenu(HMENU hmenuShared, HOLEMENU holemenu,
                                                      HWND hwndActiveObject) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  CFrameWnd* pFrame = pThis->m_pFrameWnd;
  if (pFrame == NULL) return E_UNEXPECTED;
  pThis->m_hSharedMenu = hmenuShared;
  pThis->m_hOleMenu = holemenu;
  if (hmenuShared == NULL) {
    pFrame->OnUpdateFrameMenu(NULL);
  } else if (pFrame->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd))) {
    ::SendMessage(((CMDIFrameWnd*)pFrame)->m_hWndMDIClient, WM_MDISETMENU, (WPARAM)hmenuShared,
                  0);
  } else if (!(pFrame->GetStyle() & WS_CHILD)) {
    ::SetMenu(pThis->m_hWnd, hmenuShared);
  }
  ::DrawMenuBar(pThis->m_hWnd);
  // The menu descriptor sends the object's commands to its window.
  ::OleSetMenuDescriptor(hmenuShared != NULL ? holemenu : NULL, pThis->m_hWnd,
                         hmenuShared != NULL ? hwndActiveObject : NULL,
                         hmenuShared != NULL ? (LPOLEINPLACEFRAME)&pThis->m_xOleInPlaceFrame : NULL,
                         hmenuShared != NULL ? pThis->m_lpActiveObject : NULL);
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::RemoveMenus(HMENU hmenuShared) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  _AfxUnmergeMenus(hmenuShared, pThis->GetInPlaceMenu());
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::SetStatusText(LPCOLESTR pszStatusText) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  if (pThis->m_pFrameWnd == NULL) return E_UNEXPECTED;
  CString strText(pszStatusText != NULL ? pszStatusText : L"");
  pThis->m_pFrameWnd->SetMessageText(strText);
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::EnableModeless(BOOL fEnable) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  CFrameWnd* pFrame = pThis->m_pFrameWnd;
  if (pFrame == NULL) return E_UNEXPECTED;
  if (!fEnable && !pThis->m_bInModalState) {
    pThis->m_bInModalState = TRUE;
    pFrame->BeginModalState();
  } else if (fEnable && pThis->m_bInModalState) {
    pThis->m_bInModalState = FALSE;
    pFrame->EndModalState();
  }
  return S_OK;
}

STDMETHODIMP COleFrameHook::XOleInPlaceFrame::TranslateAccelerator(LPMSG lpmsg, WORD) {
  METHOD_PROLOGUE_EX(COleFrameHook, OleInPlaceFrame)
  HACCEL hAccel = pThis->GetInPlaceAccel();
  if (lpmsg != NULL && hAccel != NULL && ::TranslateAccelerator(pThis->m_hWnd, hAccel, lpmsg))
    return S_OK;
  return S_FALSE;
}

// ---------------------------------------------------------------------
// COleClientItem

LPOLEINPLACEOBJECT COleClientItem::GetInPlaceObject() {
  if (m_lpInPlaceObject == NULL && m_lpObject != NULL)
    m_lpObject->QueryInterface(IID_IOleInPlaceObject, (void**)&m_lpInPlaceObject);
  // An object handler may not pass the interface on; the active object,
  // which the object gives the frame itself, has it.
  if (m_lpInPlaceObject == NULL && m_pInPlaceFrame != NULL &&
      m_pInPlaceFrame->m_lpActiveObject != NULL)
    m_pInPlaceFrame->m_lpActiveObject->QueryInterface(IID_IOleInPlaceObject,
                                                      (void**)&m_lpInPlaceObject);
  return m_lpInPlaceObject;
}

// The container's side of the activation, given back (also when the
// object went without saying so).
void COleClientItem::ReleaseInPlace() {
  if (m_lpInPlaceObject != NULL) {
    LPOLEINPLACEOBJECT lpObject = m_lpInPlaceObject;
    m_lpInPlaceObject = NULL;
    lpObject->Release();
  }
  COleFrameHook* hooks[2] = {m_pInPlaceDoc, m_pInPlaceFrame};
  m_pInPlaceFrame = m_pInPlaceDoc = NULL;
  for (COleFrameHook* pHook : hooks) {
    if (pHook == NULL) continue;
    if (pHook->m_pActiveItem == this) {
      if (pHook->m_lpActiveObject != NULL) {
        pHook->m_lpActiveObject->Release();
        pHook->m_lpActiveObject = NULL;
      }
      CFrameWnd* pFrame = pHook->m_pFrameWnd;
      if (pFrame != NULL) {
        if (pHook->m_hSharedMenu != NULL) {
          pHook->m_hSharedMenu = NULL;
          ::OleSetMenuDescriptor(NULL, pHook->m_hWnd, NULL, NULL, NULL);
          pFrame->OnUpdateFrameMenu(NULL);
          ::DrawMenuBar(pHook->m_hWnd);
        }
        if (!pHook->m_listHiddenBars.IsEmpty() || !pFrame->m_rectBorder.IsRectNull()) {
          OnShowControlBars(pFrame, TRUE);
          pFrame->NegotiateBorderSpace(3, NULL);
        }
      }
      pHook->m_pActiveItem = NULL;
    }
    pHook->InternalRelease();
  }
}

CWnd* COleClientItem::GetInPlaceWindow() {
  if (!IsInPlaceActive()) return NULL;
  LPOLEINPLACEOBJECT lpObject = GetInPlaceObject();
  HWND hWnd = NULL;
  if (lpObject == NULL || FAILED(lpObject->GetWindow(&hWnd)) || hWnd == NULL) return NULL;
  return CWnd::FromHandle(hWnd);
}

void COleClientItem::Deactivate() {
  if (!IsInPlaceActive() && m_pInPlaceFrame == NULL) return;
  LPOLEINPLACEOBJECT lpObject = GetInPlaceObject();
  if (lpObject != NULL) {
    lpObject->AddRef();
    m_scLast = lpObject->InPlaceDeactivate();
    lpObject->Release();
  }
  if (IsInPlaceActive() || m_pInPlaceFrame != NULL) {
    // The object did not call back: tidy up here.
    if (m_nItemState == activeUIState) m_xOleIPSite.OnUIDeactivate(FALSE);
    ReleaseInPlace();
    SetState(loadedState);
    OnDeactivate();
  }
}

void COleClientItem::DeactivateUI() {
  if (m_nItemState != activeUIState) return;
  LPOLEINPLACEOBJECT lpObject = GetInPlaceObject();
  if (lpObject != NULL) {
    lpObject->AddRef();
    m_scLast = lpObject->UIDeactivate();
    lpObject->Release();
  }
  if (m_nItemState == activeUIState) m_xOleIPSite.OnUIDeactivate(FALSE);
}

BOOL COleClientItem::SetItemRects(LPCRECT lpPosRect, LPCRECT lpClipRect) {
  LPOLEINPLACEOBJECT lpObject = GetInPlaceObject();
  if (lpObject == NULL) return FALSE;
  CRect rectPos, rectClip;
  if (lpPosRect == NULL) {
    OnGetItemPosition(rectPos);
    lpPosRect = &rectPos;
  }
  if (lpClipRect == NULL) {
    OnGetClipRect(rectClip);
    lpClipRect = &rectClip;
  }
  m_scLast = lpObject->SetObjectRects(lpPosRect, lpClipRect);
  return SUCCEEDED(m_scLast);
}

BOOL COleClientItem::OnChangeItemPosition(const CRect& rectPos) {
  if (!IsInPlaceActive()) return FALSE;
  return SetItemRects(&rectPos);
}

// Objects shown as icons open in their own window, as do items without a
// view to be active in.
BOOL COleClientItem::CanActivate() {
  if (m_nDrawAspect == DVASPECT_ICON || m_pView == NULL || m_pView->m_hWnd == NULL) return FALSE;
  return m_pView->GetParentFrame() != NULL;
}

void COleClientItem::OnActivate() {}
void COleClientItem::OnActivateUI() {}
void COleClientItem::OnDeactivate() {}
void COleClientItem::OnDeactivateUI(BOOL) {}
BOOL COleClientItem::OnScrollBy(CSize) { return FALSE; }
void COleClientItem::OnDiscardUndoState() {}
void COleClientItem::OnDeactivateAndUndo() { DeactivateUI(); }

BOOL COleClientItem::OnGetWindowContext(CFrameWnd** ppMainFrame, CFrameWnd** ppDocFrame,
                                        LPOLEINPLACEFRAMEINFO lpFrameInfo) {
  if (m_pView == NULL) return FALSE;
  CFrameWnd* pFrame = m_pView->GetParentFrame();
  if (pFrame == NULL) return FALSE;
  CFrameWnd* pTop = pFrame->GetTopLevelFrame();
  if (pTop == NULL) pTop = pFrame;
  *ppMainFrame = pTop;
  *ppDocFrame = pFrame != pTop ? pFrame : NULL;
  if (lpFrameInfo != NULL) {
    lpFrameInfo->fMDIApp = pTop->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd));
    lpFrameInfo->hwndFrame = pTop->m_hWnd;
    CDocTemplate* pTemplate = ItemTemplate(this);
    HACCEL hAccel = pTemplate != NULL ? pTemplate->m_hAccelInPlace : NULL;
    lpFrameInfo->haccel = hAccel;
    lpFrameInfo->cAccelEntries = hAccel != NULL ? ::CopyAcceleratorTable(hAccel, NULL, 0) : 0;
  }
  return TRUE;
}

// The frame's bars, but for its status bar, which the object's status
// text goes to.
BOOL COleClientItem::OnShowControlBars(CFrameWnd* pFrameWnd, BOOL bShow) {
  COleFrameHook* pHook = pFrameWnd->m_pNotifyHook;
  BOOL bChanged = FALSE;
  if (!bShow) {
    POSITION pos = pFrameWnd->m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)pFrameWnd->m_listControlBars.GetNext(pos);
      if (pBar->m_hWnd == NULL || pBar->IsDockBar() || !pBar->IsVisible() ||
          pBar->GetDlgCtrlID() == AFX_IDW_STATUS_BAR)
        continue;
      if (pHook != NULL && pHook->m_listHiddenBars.Find(pBar) != NULL) continue;
      pFrameWnd->ShowControlBar(pBar, FALSE, TRUE);
      if (pHook != NULL) pHook->m_listHiddenBars.AddTail(pBar);
      bChanged = TRUE;
    }
  } else if (pHook != NULL) {
    while (!pHook->m_listHiddenBars.IsEmpty()) {
      CControlBar* pBar = (CControlBar*)pHook->m_listHiddenBars.RemoveHead();
      if (pBar->m_hWnd == NULL) continue;
      pFrameWnd->ShowControlBar(pBar, TRUE, TRUE);
      bChanged = TRUE;
    }
  }
  return bChanged;
}

// The hook for a frame: the one already there, or a new one.
static COleFrameHook* HookFor(CFrameWnd* pFrame, COleClientItem* pItem) {
  COleFrameHook* pHook = pFrame->m_pNotifyHook;
  if (pHook != NULL) {
    pHook->InternalAddRef();
    pHook->m_pActiveItem = pItem;
    return pHook;
  }
  return new COleFrameHook(pFrame, pItem);
}

// ---------------------------------------------------------------------
// IOleInPlaceSite

STDMETHODIMP_(ULONG) COleClientItem::XOleIPSite::AddRef() {
  METHOD_PROLOGUE_EX_(COleClientItem, OleIPSite)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleClientItem::XOleIPSite::Release() {
  METHOD_PROLOGUE_EX_(COleClientItem, OleIPSite)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleClientItem::XOleIPSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleClientItem, OleIPSite)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleClientItem::XOleIPSite::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE_EX_(COleClientItem, OleIPSite)
  if (phwnd == NULL) return E_POINTER;
  *phwnd = pThis->m_pView != NULL ? pThis->m_pView->m_hWnd : NULL;
  return *phwnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleClientItem::XOleIPSite::ContextSensitiveHelp(BOOL) { return S_OK; }

STDMETHODIMP COleClientItem::XOleIPSite::CanInPlaceActivate() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  return pThis->CanActivate() ? S_OK : S_FALSE;
}

STDMETHODIMP COleClientItem::XOleIPSite::OnInPlaceActivate() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  pThis->GetInPlaceObject();
  pThis->SetState(activeState);
  pThis->OnActivate();
  return S_OK;
}

// One object is UI active in a view at a time.
STDMETHODIMP COleClientItem::XOleIPSite::OnUIActivate() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  COleDocument* pDoc = pThis->GetDocument();
  if (pDoc != NULL) {
    POSITION pos = pDoc->GetStartPosition();
    while (pos != NULL) {
      COleClientItem* pItem = pDoc->GetNextClientItem(pos);
      if (pItem != NULL && pItem != pThis && pItem->m_pView == pThis->m_pView &&
          pItem->IsInPlaceActive())
        pItem->Deactivate();
    }
  }
  pThis->SetState(activeUIState);
  pThis->OnActivateUI();
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleIPSite::GetWindowContext(LPOLEINPLACEFRAME* ppFrame,
                                                          LPOLEINPLACEUIWINDOW* ppDoc,
                                                          LPRECT lprcPosRect,
                                                          LPRECT lprcClipRect,
                                                          LPOLEINPLACEFRAMEINFO lpFrameInfo) {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  if (ppFrame == NULL || ppDoc == NULL || lprcPosRect == NULL || lprcClipRect == NULL)
    return E_POINTER;
  *ppFrame = NULL;
  *ppDoc = NULL;
  CFrameWnd* pMainFrame = NULL;
  CFrameWnd* pDocFrame = NULL;
  HRESULT hr = S_OK;
  try {
    if (!pThis->OnGetWindowContext(&pMainFrame, &pDocFrame, lpFrameInfo)) return E_FAIL;
    // Asked again: the frames may have changed.
    if (pThis->m_pInPlaceFrame != NULL) pThis->m_pInPlaceFrame->InternalRelease();
    if (pThis->m_pInPlaceDoc != NULL) pThis->m_pInPlaceDoc->InternalRelease();
    pThis->m_pInPlaceFrame = HookFor(pMainFrame, pThis);
    pThis->m_pInPlaceDoc = pDocFrame != NULL ? HookFor(pDocFrame, pThis) : NULL;
    *ppFrame = (LPOLEINPLACEFRAME)&pThis->m_pInPlaceFrame->m_xOleInPlaceFrame;
    (*ppFrame)->AddRef();
    if (pThis->m_pInPlaceDoc != NULL) {
      *ppDoc = (LPOLEINPLACEUIWINDOW)&pThis->m_pInPlaceDoc->m_xOleInPlaceFrame;
      (*ppDoc)->AddRef();
    }
    CRect rectPos, rectClip;
    pThis->OnGetItemPosition(rectPos);
    pThis->OnGetClipRect(rectClip);
    *lprcPosRect = rectPos;
    *lprcClipRect = rectClip;
  } catch (COleException* e) {
    hr = e->m_sc;
    e->Delete();
  } catch (CException* e) {
    hr = E_FAIL;
    e->Delete();
  }
  return hr;
}

STDMETHODIMP COleClientItem::XOleIPSite::Scroll(SIZE scrollExtent) {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  return pThis->OnScrollBy(CSize(scrollExtent)) ? S_OK : S_FALSE;
}

// The container's menu and bars come back.
STDMETHODIMP COleClientItem::XOleIPSite::OnUIDeactivate(BOOL fUndoable) {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  COleFrameHook* hooks[2] = {pThis->m_pInPlaceDoc, pThis->m_pInPlaceFrame};
  for (COleFrameHook* pHook : hooks) {
    if (pHook == NULL) continue;
    if (pHook->m_lpActiveObject != NULL) {
      pHook->m_lpActiveObject->Release();
      pHook->m_lpActiveObject = NULL;
    }
    CFrameWnd* pFrame = pHook->m_pFrameWnd;
    if (pFrame == NULL) continue;
    if (pHook->m_hSharedMenu != NULL) {
      pHook->m_hSharedMenu = NULL;
      pHook->m_hOleMenu = NULL;
      ::OleSetMenuDescriptor(NULL, pHook->m_hWnd, NULL, NULL, NULL);
    }
    pThis->OnShowControlBars(pFrame, TRUE);
    pFrame->NegotiateBorderSpace(3, NULL);
    pFrame->OnUpdateFrameMenu(NULL);
    ::DrawMenuBar(pHook->m_hWnd);
  }
  pThis->SetState(activeState);
  pThis->OnDeactivateUI(fUndoable);
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleIPSite::OnInPlaceDeactivate() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  if (pThis->m_nItemState == activeUIState) OnUIDeactivate(FALSE);
  pThis->ReleaseInPlace();
  pThis->SetState(loadedState);
  pThis->OnDeactivate();
  // The container draws the object again.
  if (pThis->m_pView != NULL && pThis->m_pView->m_hWnd != NULL) {
    CRect rect;
    pThis->OnGetItemPosition(rect);
    pThis->m_pView->InvalidateRect(rect.IsRectEmpty() ? NULL : (LPCRECT)&rect);
  }
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleIPSite::DiscardUndoState() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  pThis->OnDiscardUndoState();
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleIPSite::DeactivateAndUndo() {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  pThis->OnDeactivateAndUndo();
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleIPSite::OnPosRectChange(LPCRECT lprcPosRect) {
  METHOD_PROLOGUE_EX(COleClientItem, OleIPSite)
  if (lprcPosRect == NULL) return E_POINTER;
  HRESULT hr = S_OK;
  try {
    pThis->OnChangeItemPosition(CRect(*lprcPosRect));
  } catch (CException* e) {
    hr = E_FAIL;
    e->Delete();
  }
  return hr;
}

// ---------------------------------------------------------------------
// COleDocument: a frame closing takes its objects out of in-place.

void COleDocument::PreCloseFrame(CFrameWnd* pFrame) {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem == NULL || pItem->m_pView == NULL) continue;
    if (pItem->m_pView->GetParentFrame() != pFrame) continue;
    if (pItem->IsInPlaceActive()) pItem->Deactivate();
    pItem->m_pView = NULL;
  }
  CDocument::PreCloseFrame(pFrame);
}
