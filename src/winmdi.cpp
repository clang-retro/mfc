// winmdi.cpp - CMDIFrameWnd and CMDIChildWnd.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

// ---------------------------------------------------------------------
// CMDIFrameWnd

IMPLEMENT_DYNCREATE(CMDIFrameWnd, CFrameWnd)

BEGIN_MESSAGE_MAP(CMDIFrameWnd, CFrameWnd)
  ON_WM_DESTROY()
  ON_WM_SIZE()
  ON_UPDATE_COMMAND_UI(ID_WINDOW_ARRANGE, OnUpdateMDIWindowCmd)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_CASCADE, OnUpdateMDIWindowCmd)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_TILE_HORZ, OnUpdateMDIWindowCmd)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_TILE_VERT, OnUpdateMDIWindowCmd)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_NEW, OnUpdateMDIWindowCmd)
  ON_COMMAND_EX(ID_WINDOW_ARRANGE, OnMDIWindowCmd)
  ON_COMMAND_EX(ID_WINDOW_CASCADE, OnMDIWindowCmd)
  ON_COMMAND_EX(ID_WINDOW_TILE_HORZ, OnMDIWindowCmd)
  ON_COMMAND_EX(ID_WINDOW_TILE_VERT, OnMDIWindowCmd)
  ON_COMMAND(ID_WINDOW_NEW, OnWindowNew)
END_MESSAGE_MAP()

CMDIFrameWnd::CMDIFrameWnd() : m_hWndMDIClient(NULL) {}

BOOL CMDIFrameWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WNDMDIFRAME_REG)) return FALSE;
    cs.lpszClass = _afxWndMDIFrame;
  }
  if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
  // The MDI client draws the sunken edge.
  cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
  // An MDI frame's title puts the application first: "App - Doc".
  cs.style &= ~FWS_PREFIXTITLE;
  return TRUE;
}

BOOL CMDIFrameWnd::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                             CCreateContext* pContext) {
  if (!CFrameWnd::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext)) return FALSE;
  // The menu shown while no document is open.
  m_hMenuDefault = ::GetMenu(m_hWnd);
  return TRUE;
}

HMENU CMDIFrameWnd::GetWindowMenuPopup(HMENU hMenuBar) {
  // The Window menu is the popup holding the window arrangement commands.
  if (hMenuBar == NULL) return NULL;
  int nItems = ::GetMenuItemCount(hMenuBar);
  for (int iItem = nItems - 1; iItem >= 0; iItem--) {
    HMENU hMenuPop = ::GetSubMenu(hMenuBar, iItem);
    if (hMenuPop == NULL) continue;
    int nItems2 = ::GetMenuItemCount(hMenuPop);
    for (int iItem2 = 0; iItem2 < nItems2; iItem2++) {
      UINT nID = ::GetMenuItemID(hMenuPop, iItem2);
      if (nID >= AFX_IDM_WINDOW_FIRST && nID <= AFX_IDM_WINDOW_LAST) return hMenuPop;
    }
  }
  return NULL;
}

BOOL CMDIFrameWnd::OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext) {
  (void)pContext;
  CMenu* pMenu = NULL;
  HMENU hMenuBar = ::GetMenu(m_hWnd);
  HMENU hWindowMenu = GetWindowMenuPopup(hMenuBar);
  if (hWindowMenu != NULL) pMenu = CMenu::FromHandle(hWindowMenu);
  return CreateClient(lpcs, pMenu);
}

BOOL CMDIFrameWnd::CreateClient(LPCREATESTRUCT lpCreateStruct, CMenu* pWindowMenu) {
  DWORD dwStyle = WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | MDIS_ALLCHILDSTYLES;
  DWORD dwExStyle = WS_EX_CLIENTEDGE;

  // Scroll bars asked for on the frame belong to the client.
  if (lpCreateStruct->style & (WS_HSCROLL | WS_VSCROLL)) {
    dwStyle |= lpCreateStruct->style & (WS_HSCROLL | WS_VSCROLL);
    ModifyStyle(WS_HSCROLL | WS_VSCROLL, 0, SWP_NOREDRAW | SWP_FRAMECHANGED);
  }

  CLIENTCREATESTRUCT ccs;
  ccs.hWindowMenu = pWindowMenu != NULL ? pWindowMenu->m_hMenu : NULL;
  ccs.idFirstChild = AFX_IDM_FIRST_MDICHILD;

  m_hWndMDIClient = ::CreateWindowEx(dwExStyle, _T("MDICLIENT"), NULL, dwStyle, 0, 0, 0, 0,
                                     m_hWnd, (HMENU)(UINT_PTR)AFX_IDW_PANE_FIRST,
                                     AfxGetInstanceHandle(), (LPVOID)&ccs);
  if (m_hWndMDIClient == NULL) return FALSE;
  ::BringWindowToTop(m_hWndMDIClient);
  return TRUE;
}

LRESULT CMDIFrameWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  return ::DefFrameProc(m_hWnd, m_hWndMDIClient, message, wParam, lParam);
}

CMDIChildWnd* CMDIFrameWnd::MDIGetActive(BOOL* pbMaximized) const {
  if (m_hWndMDIClient == NULL) return NULL;
  HWND hWnd = (HWND)::SendMessage(m_hWndMDIClient, WM_MDIGETACTIVE, 0, (LPARAM)pbMaximized);
  CWnd* pWnd = CWnd::FromHandlePermanent(hWnd);
  if (pWnd == NULL || !pWnd->IsKindOf(RUNTIME_CLASS(CMDIChildWnd))) return NULL;
  CMDIChildWnd* pChild = (CMDIChildWnd*)pWnd;
  // A child being torn down or not yet shown does not count.
  if (pChild->m_bPseudoInactive && !(pChild->GetStyle() & WS_VISIBLE)) {
    if (pbMaximized != NULL) *pbMaximized = FALSE;
    return NULL;
  }
  return pChild;
}

CFrameWnd* CMDIFrameWnd::GetActiveFrame() {
  CMDIChildWnd* pActiveChild = MDIGetActive();
  if (pActiveChild == NULL) return this;
  return pActiveChild;
}

CMenu* CMDIFrameWnd::MDISetMenu(CMenu* pFrameMenu, CMenu* pWindowMenu) {
  HMENU hOld = (HMENU)::SendMessage(m_hWndMDIClient, WM_MDISETMENU,
                                    (WPARAM)(pFrameMenu != NULL ? pFrameMenu->m_hMenu : NULL),
                                    (LPARAM)(pWindowMenu != NULL ? pWindowMenu->m_hMenu : NULL));
  return CMenu::FromHandle(hOld);
}

static void _AfxSetMDIMenu(CMDIFrameWnd* pFrame, HMENU hMenu) {
  HMENU hWindowMenu = pFrame->GetWindowMenuPopup(hMenu);
  ::SendMessage(pFrame->m_hWndMDIClient, WM_MDISETMENU, (WPARAM)hMenu, (LPARAM)hWindowMenu);
  ::DrawMenuBar(pFrame->m_hWnd);
}

void CMDIFrameWnd::OnUpdateFrameMenu(HMENU hMenuAlt) {
  if (m_pNotifyHook != NULL && m_pNotifyHook->OnUpdateFrameMenu()) return;
  CMDIChildWnd* pActiveWnd = MDIGetActive();
  if (pActiveWnd != NULL) {
    pActiveWnd->OnUpdateFrameMenu(TRUE, pActiveWnd, hMenuAlt);
  } else {
    if (hMenuAlt == NULL) hMenuAlt = m_hMenuDefault;
    _AfxSetMDIMenu(this, hMenuAlt);
  }
}

void CMDIFrameWnd::DelayUpdateFrameMenu(HMENU hMenuAlt) {
  OnUpdateFrameMenu(hMenuAlt);
  m_nIdleFlags |= idleMenu;
}

void CMDIFrameWnd::OnIdleUpdateCmdUI() {
  if (m_nIdleFlags & idleMenu) {
    m_nIdleFlags &= ~idleMenu;
    DrawMenuBar();
  }
  CFrameWnd::OnIdleUpdateCmdUI();
}

void CMDIFrameWnd::OnUpdateFrameTitle(BOOL bAddToTitle) {
  if ((GetStyle() & FWS_ADDTOTITLE) == 0) return;

  BOOL bMaximized = FALSE;
  CMDIChildWnd* pActiveChild = MDIGetActive(&bMaximized);
  CDocument* pDocument = pActiveChild != NULL ? pActiveChild->GetActiveDocument() : NULL;

  // A maximized child's title is shown by Windows itself, in brackets.
  if (bAddToTitle && pActiveChild != NULL && !bMaximized && pDocument != NULL) {
    UpdateFrameTitleForDocument(pDocument->GetTitle());
  } else if (bAddToTitle && pActiveChild != NULL && !bMaximized) {
    CString strTitle = pActiveChild->GetTitle();
    UpdateFrameTitleForDocument(strTitle.IsEmpty() ? NULL : (LPCTSTR)strTitle);
  } else {
    UpdateFrameTitleForDocument(NULL);
  }
}

BOOL CMDIFrameWnd::PreTranslateMessage(MSG* pMsg) {
  if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
    // Ctrl+F4, Ctrl+F6 and the child's system menu.
    CMDIChildWnd* pActiveChild = MDIGetActive();
    if (pActiveChild != NULL && ::TranslateMDISysAccel(m_hWndMDIClient, pMsg)) return TRUE;
    HACCEL hAccel = GetDefaultAccelerator();
    return hAccel != NULL && ::TranslateAccelerator(m_hWnd, hAccel, pMsg);
  }
  return FALSE;
}

BOOL CMDIFrameWnd::OnCommand(WPARAM wParam, LPARAM lParam) {
  // The active child sees commands first.
  CMDIChildWnd* pActiveChild = MDIGetActive();
  if (pActiveChild != NULL &&
      AfxCallWndProc(pActiveChild, pActiveChild->m_hWnd, WM_COMMAND, wParam, lParam) != 0)
    return TRUE;
  if (CFrameWnd::OnCommand(wParam, lParam)) return TRUE;
  return FALSE;  // DefFrameProc handles the Window menu's document list
}

BOOL CMDIFrameWnd::OnCmdMsg(UINT nID, int nCode, void* pExtra,
                            AFX_CMDHANDLERINFO* pHandlerInfo) {
  CMDIChildWnd* pActiveChild = MDIGetActive();
  if (pActiveChild != NULL && pActiveChild->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
    return TRUE;
  return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void CMDIFrameWnd::OnDestroy() { CFrameWnd::OnDestroy(); }

void CMDIFrameWnd::OnSize(UINT nType, int cx, int cy) {
  (void)cx;
  (void)cy;
  // The default would size the MDI client over the bars.
  if (nType != SIZE_MINIMIZED) RecalcLayout();
}

void CMDIFrameWnd::OnUpdateMDIWindowCmd(CCmdUI* pCmdUI) { pCmdUI->Enable(MDIGetActive() != NULL); }

BOOL CMDIFrameWnd::OnMDIWindowCmd(UINT nID) {
  UINT msg;
  WPARAM wParam = 0;
  switch (nID) {
    case ID_WINDOW_ARRANGE: msg = WM_MDIICONARRANGE; break;
    case ID_WINDOW_CASCADE: msg = WM_MDICASCADE; break;
    case ID_WINDOW_TILE_HORZ: msg = WM_MDITILE; wParam = MDITILE_HORIZONTAL; break;
    case ID_WINDOW_TILE_VERT: msg = WM_MDITILE; wParam = MDITILE_VERTICAL; break;
    default: return FALSE;
  }
  ::SendMessage(m_hWndMDIClient, msg, wParam, 0);
  return TRUE;
}

void CMDIFrameWnd::OnWindowNew() {
  CMDIChildWnd* pActiveChild = MDIGetActive();
  CDocument* pDocument;
  if (pActiveChild == NULL || (pDocument = pActiveChild->GetActiveDocument()) == NULL) {
    AfxMessageBox(AFX_IDP_COMMAND_FAILURE);
    return;
  }
  CDocTemplate* pTemplate = pDocument->GetDocTemplate();
  if (pTemplate == NULL) {
    AfxMessageBox(AFX_IDP_COMMAND_FAILURE);
    return;
  }
  CFrameWnd* pFrame = pTemplate->CreateNewFrame(pDocument, pActiveChild);
  if (pFrame == NULL) {
    AfxMessageBox(AFX_IDP_COMMAND_FAILURE);
    return;
  }
  pTemplate->InitialUpdateFrame(pFrame, pDocument);
}

CMDIChildWnd* CMDIFrameWnd::CreateNewChild(CRuntimeClass* pClass, UINT nResource, HMENU hMenu,
                                           HACCEL hAccel) {
  CMDIChildWnd* pFrame = (CMDIChildWnd*)pClass->CreateObject();
  if (pFrame == NULL) return NULL;
  pFrame->SetHandles(hMenu, hAccel);

  CCreateContext context;
  context.m_pCurrentFrame = this;
  if (!pFrame->LoadFrame(nResource, WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, this, &context))
    return NULL;
  pFrame->InitialUpdateFrame(NULL, TRUE);
  return pFrame;
}

// ---------------------------------------------------------------------
// CMDIChildWnd

IMPLEMENT_DYNCREATE(CMDIChildWnd, CFrameWnd)

BEGIN_MESSAGE_MAP(CMDIChildWnd, CFrameWnd)
  ON_WM_MDIACTIVATE()
  ON_WM_CREATE()
  ON_WM_DESTROY()
END_MESSAGE_MAP()

CMDIChildWnd::CMDIChildWnd() : m_hMenuShared(NULL), m_bPseudoInactive(FALSE) {}

BOOL CMDIChildWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WNDMDIFRAME_REG)) return FALSE;
    cs.lpszClass = _afxWndMDIFrame;
  }
  if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
  cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
  cs.style |= WS_CHILD;
  // Children show the document name only.
  cs.style &= ~FWS_PREFIXTITLE;
  return TRUE;
}

CMDIFrameWnd* CMDIChildWnd::GetMDIFrame() {
  HWND hWndMDIClient = ::GetParent(m_hWnd);
  CWnd* pWnd = CWnd::FromHandlePermanent(::GetParent(hWndMDIClient));
  if (pWnd == NULL || !pWnd->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd))) return NULL;
  return (CMDIFrameWnd*)pWnd;
}

BOOL CMDIChildWnd::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                          const RECT& rect, CMDIFrameWnd* pParentWnd, CCreateContext* pContext) {
  if (pParentWnd == NULL) {
    CWinThread* pThread = AfxGetThread();
    CWnd* pMainWnd = pThread != NULL ? pThread->m_pMainWnd : NULL;
    if (pMainWnd == NULL || !pMainWnd->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd))) return FALSE;
    pParentWnd = (CMDIFrameWnd*)pMainWnd;
  }

  CREATESTRUCT cs;
  memset(&cs, 0, sizeof(cs));
  cs.style = dwStyle | WS_CHILD;
  cs.lpszClass = lpszClassName;
  cs.lpszName = lpszWindowName;
  cs.x = rect.left;
  cs.y = rect.top;
  cs.cx = (int)((UINT)rect.right - (UINT)rect.left);
  cs.cy = (int)((UINT)rect.bottom - (UINT)rect.top);
  cs.hwndParent = pParentWnd->m_hWnd;
  cs.hInstance = AfxGetInstanceHandle();
  cs.lpCreateParams = (LPVOID)pContext;
  if (!PreCreateWindow(cs)) {
    PostNcDestroy();
    return FALSE;
  }
  m_strTitle = cs.lpszName;

  // A new child is maximized when the active one is.
  BOOL bMaximized = FALSE;
  pParentWnd->MDIGetActive(&bMaximized);
  if (bMaximized) cs.style |= WS_MAXIMIZE;

  // MDI children are always created hidden and shown below.
  MDICREATESTRUCT mcs;
  mcs.szClass = cs.lpszClass;
  mcs.szTitle = cs.lpszName;
  mcs.hOwner = cs.hInstance;
  mcs.x = cs.x;
  mcs.y = cs.y;
  mcs.cx = cs.cx;
  mcs.cy = cs.cy;
  mcs.style = cs.style & ~(WS_MAXIMIZE | WS_VISIBLE);
  mcs.lParam = (LPARAM)cs.lpCreateParams;

  AfxHookWindowCreate(this);
  HWND hWnd = (HWND)::SendMessage(pParentWnd->m_hWndMDIClient, WM_MDICREATE, 0, (LPARAM)&mcs);
  if (!AfxUnhookWindowCreate()) PostNcDestroy();  // the window was never attached
  if (hWnd == NULL) return FALSE;

  if (cs.style & WS_VISIBLE) {
    ::BringWindowToTop(hWnd);
    if (cs.style & WS_MINIMIZE)
      ShowWindow(SW_SHOWMINIMIZED);
    else if (cs.style & WS_MAXIMIZE)
      ShowWindow(SW_SHOWMAXIMIZED);
    else
      ShowWindow(SW_SHOWNORMAL);
    pParentWnd->MDIActivate(this);
    ::SendMessage(pParentWnd->m_hWndMDIClient, WM_MDIREFRESHMENU, 0, 0);
  } else if (cs.style & WS_MAXIMIZE) {
    m_nShowDelay = SW_SHOWMAXIMIZED;
  }
  return TRUE;
}

BOOL CMDIChildWnd::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                             CCreateContext* pContext) {
  m_nIDHelp = nIDResource;

  // A document template supplies the menu and accelerators to share.
  if (pContext != NULL && pContext->m_pNewDocTemplate != NULL &&
      pContext->m_pNewDocTemplate->IsKindOf(RUNTIME_CLASS(CMultiDocTemplate))) {
    CMultiDocTemplate* pTemplate = (CMultiDocTemplate*)pContext->m_pNewDocTemplate;
    m_hMenuShared = pTemplate->m_hMenuShared;
    m_hAccelTable = pTemplate->m_hAccelTable;
  }

  CString strFullString, strTitle;
  if (strFullString.LoadString(nIDResource)) AfxExtractSubString(strTitle, strFullString, 0);

  if (!AfxDeferRegisterClass(AFX_WNDMDIFRAME_REG)) return FALSE;
  LPCTSTR lpszClass = GetIconWndClass(dwDefaultStyle, nIDResource);
  CMDIFrameWnd* pParent = NULL;
  if (pParentWnd != NULL && pParentWnd->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd)))
    pParent = (CMDIFrameWnd*)pParentWnd;
  return Create(lpszClass, strTitle, dwDefaultStyle, rectDefault, pParent, pContext);
}

int CMDIChildWnd::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  // MDI children get their creation data through the MDICREATESTRUCT.
  MDICREATESTRUCT* pmcs = (MDICREATESTRUCT*)lpCreateStruct->lpCreateParams;
  CCreateContext* pContext = pmcs != NULL ? (CCreateContext*)pmcs->lParam : NULL;
  return OnCreateHelper(lpCreateStruct, pContext);
}

LRESULT CMDIChildWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  return ::DefMDIChildProc(m_hWnd, message, wParam, lParam);
}

BOOL CMDIChildWnd::DestroyWindow() {
  if (m_hWnd == NULL) return FALSE;
  // MDI children have to be destroyed through the client.
  CMDIFrameWnd* pFrameWnd = GetMDIFrame();
  HWND hWndFrame = pFrameWnd != NULL ? pFrameWnd->m_hWnd : NULL;
  MDIDestroy();
  if (hWndFrame != NULL && ::IsWindow(hWndFrame)) pFrameWnd->OnUpdateFrameTitle(TRUE);
  return TRUE;
}

void CMDIChildWnd::MDIDestroy() {
  ::SendMessage(::GetParent(m_hWnd), WM_MDIDESTROY, (WPARAM)m_hWnd, 0);
}

void CMDIChildWnd::MDIActivate() {
  ::SendMessage(::GetParent(m_hWnd), WM_MDIACTIVATE, (WPARAM)m_hWnd, 0);
}

void CMDIChildWnd::MDIMaximize() {
  ::SendMessage(::GetParent(m_hWnd), WM_MDIMAXIMIZE, (WPARAM)m_hWnd, 0);
}

void CMDIChildWnd::MDIRestore() {
  ::SendMessage(::GetParent(m_hWnd), WM_MDIRESTORE, (WPARAM)m_hWnd, 0);
}

BOOL CMDIChildWnd::PreTranslateMessage(MSG* pMsg) {
  // Child accelerators work in the frame, which has the menu.
  if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
    CMDIFrameWnd* pFrame = GetMDIFrame();
    HACCEL hAccel = GetDefaultAccelerator();
    if (pFrame != NULL && hAccel != NULL &&
        ::TranslateAccelerator(pFrame->m_hWnd, hAccel, pMsg))
      return TRUE;
  }
  return FALSE;
}

void CMDIChildWnd::ActivateFrame(int nCmdShow) {
  BOOL bVisibleThen = (GetStyle() & WS_VISIBLE) != 0;
  CMDIFrameWnd* pFrameWnd = GetMDIFrame();
  if (pFrameWnd == NULL) {
    CFrameWnd::ActivateFrame(nCmdShow);
    return;
  }

  if (nCmdShow == -1) {
    BOOL bMaximized = FALSE;
    CMDIChildWnd* pActive = pFrameWnd->MDIGetActive(&bMaximized);
    if (!bVisibleThen) {
      if (m_nShowDelay != -1)
        nCmdShow = m_nShowDelay;
      else
        nCmdShow = (pActive != NULL && bMaximized) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
      m_nShowDelay = -1;
    } else if (IsIconic()) {
      nCmdShow = SW_RESTORE;
    }
  }

  if (nCmdShow != -1) ShowWindow(nCmdShow);
  pFrameWnd->MDIActivate(this);
  if (!bVisibleThen) ::SendMessage(pFrameWnd->m_hWndMDIClient, WM_MDIREFRESHMENU, 0, 0);
}

void CMDIChildWnd::OnUpdateFrameMenu(BOOL bActive, CWnd* pActivateWnd, HMENU hMenuAlt) {
  CMDIFrameWnd* pFrame = GetMDIFrame();
  if (pFrame == NULL) return;

  if (bActive) {
    if (hMenuAlt == NULL) {
      CDocument* pDoc = GetActiveDocument();
      if (pDoc != NULL) hMenuAlt = pDoc->GetDefaultMenu();
    }
    if (hMenuAlt == NULL) hMenuAlt = m_hMenuShared;
    if (hMenuAlt == NULL) hMenuAlt = pFrame->m_hMenuDefault;
    _AfxSetMDIMenu(pFrame, hMenuAlt);
  } else if (pActivateWnd == NULL) {
    // The last child is going: back to the frame's own menu.
    _AfxSetMDIMenu(pFrame, pFrame->m_hMenuDefault);
  }
}

void CMDIChildWnd::OnMDIActivate(BOOL bActivate, CWnd* pActivateWnd, CWnd* pDeactivateWnd) {
  (void)pDeactivateWnd;
  m_bPseudoInactive = FALSE;

  CView* pActiveView = GetActiveView();
  if (!bActivate && pActiveView != NULL)
    pActiveView->OnActivateView(FALSE, pActiveView, pActiveView);

  OnUpdateFrameMenu(bActivate, bActivate ? this : pActivateWnd, NULL);
  // An object active in place in this child puts its menu up or down.
  if (m_pNotifyHook != NULL) m_pNotifyHook->OnDocActivate(bActivate);

  if (bActivate && pActiveView != NULL)
    pActiveView->OnActivateView(TRUE, pActiveView, pActiveView);

  CMDIFrameWnd* pFrame = GetMDIFrame();
  if (pFrame != NULL) pFrame->m_nIdleFlags |= idleTitle;
}

void CMDIChildWnd::OnDestroy() {
  CFrameWnd::OnDestroy();
  CMDIFrameWnd* pFrame = GetMDIFrame();
  if (pFrame != NULL) pFrame->m_nIdleFlags |= idleTitle;
}

CFrameWnd* CMDIChildWnd::GetActiveFrame() { return this; }

void CMDIChildWnd::OnUpdateFrameTitle(BOOL bAddToTitle) {
  CMDIFrameWnd* pFrame = GetMDIFrame();
  if (pFrame != NULL) pFrame->OnUpdateFrameTitle(bAddToTitle);

  if ((GetStyle() & FWS_ADDTOTITLE) == 0) return;
  CDocument* pDocument = GetActiveDocument();
  if (bAddToTitle && pDocument != NULL) {
    CString strText = pDocument->GetTitle();
    if (m_nWindow > 0) {
      TCHAR szNum[16];
      wsprintf(szNum, _T(":%d"), m_nWindow);
      strText += szNum;
    }
    TCHAR szOld[256];
    if (::GetWindowText(m_hWnd, szOld, 256) != strText.GetLength() || strText != szOld)
      ::SetWindowText(m_hWnd, strText);
  }
}

int CMDIChildWnd::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) {
  return CFrameWnd::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

BOOL CMDIChildWnd::OnNcActivate(BOOL bActive) { return CWnd::OnNcActivate(bActive); }

void CMDIChildWnd::OnWindowPosChanging(LPWINDOWPOS lpWndPos) {
  CFrameWnd::OnWindowPosChanging(lpWndPos);
}

BOOL CMDIChildWnd::OnNcCreate(LPCREATESTRUCT lpCreateStruct) {
  return CFrameWnd::OnNcCreate(lpCreateStruct);
}
