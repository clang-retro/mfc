// frame.cpp - CFrameWnd: the window that holds views, menus and bars.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

IMPLEMENT_DYNCREATE(CFrameWnd, CWnd)

const CRect CFrameWnd::rectDefault(CW_USEDEFAULT, CW_USEDEFAULT, 0, 0);

BEGIN_MESSAGE_MAP(CFrameWnd, CWnd)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_CLOSE()
  ON_WM_INITMENUPOPUP()
  ON_WM_MENUSELECT()
  ON_WM_ENTERIDLE()
  ON_WM_SETFOCUS()
  ON_WM_SIZE()
  ON_WM_ERASEBKGND()
  ON_WM_ACTIVATE()
  ON_WM_QUERYENDSESSION()
  ON_WM_ENDSESSION()
  ON_WM_DROPFILES()
  ON_MESSAGE(WM_POPMESSAGESTRING, OnPopMessageString)
  ON_MESSAGE(WM_SETMESSAGESTRING, OnSetMessageString)
  ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUIMsg)
  ON_MESSAGE(WM_COMMANDHELP, OnCommandHelp)
  ON_MESSAGE(WM_HELPHITTEST, OnHelpHitTest)
  ON_MESSAGE(WM_ACTIVATETOPLEVEL, OnActivateTopLevel)
  ON_UPDATE_COMMAND_UI(ID_INDICATOR_CAPS, OnUpdateKeyIndicator)
  ON_UPDATE_COMMAND_UI(ID_INDICATOR_NUM, OnUpdateKeyIndicator)
  ON_UPDATE_COMMAND_UI(ID_INDICATOR_SCRL, OnUpdateKeyIndicator)
  ON_UPDATE_COMMAND_UI(ID_INDICATOR_KANA, OnUpdateKeyIndicator)
  ON_UPDATE_COMMAND_UI(ID_VIEW_STATUS_BAR, OnUpdateControlBarMenu_)
  ON_COMMAND_EX(ID_VIEW_STATUS_BAR, OnBarCheck_)
  ON_UPDATE_COMMAND_UI(ID_VIEW_TOOLBAR, OnUpdateControlBarMenu_)
  ON_COMMAND_EX(ID_VIEW_TOOLBAR, OnBarCheck_)
  ON_UPDATE_COMMAND_UI(ID_VIEW_REBAR, OnUpdateControlBarMenu_)
  ON_COMMAND_EX(ID_VIEW_REBAR, OnBarCheck_)
  ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
  ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)
  ON_COMMAND(ID_HELP, OnHelp)
  ON_COMMAND(ID_CONTEXT_HELP, OnContextHelp)
  ON_UPDATE_COMMAND_UI(ID_CONTEXT_HELP, OnUpdateContextHelp)
END_MESSAGE_MAP()

CFrameWnd::CFrameWnd() {
  m_hMenuDefault = NULL;
  m_hAccelTable = NULL;
  m_dwPromptContext = 0;
  m_bHelpMode = FALSE;
  m_pNextFrameWnd = NULL;
  m_rectBorder.SetRectEmpty();
  m_nShowDelay = -1;
  m_nIDHelp = 0;
  m_nIDTracking = 0;
  m_nIDLastMessage = 0;
  m_pViewActive = NULL;
  m_cModalStack = 0;
  m_phWndDisable = NULL;
  m_hMenuAlt = NULL;
  m_bInRecalcLayout = FALSE;
  m_pFloatingFrameClass = NULL;
  m_dwDockStyle = 0;
  m_nIdleFlags = 0;
  m_bAutoMenuEnable = TRUE;
  m_nWindow = -1;
}

CFrameWnd::~CFrameWnd() { delete[] m_phWndDisable; }

// ---------------------------------------------------------------------
// Creation

BOOL CFrameWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WNDFRAMEORVIEW_REG)) return FALSE;
    cs.lpszClass = _afxWndFrameOrView;
  }
  // Windows 95 and later put the document name first.
  if (cs.style & FWS_ADDTOTITLE) cs.style |= FWS_PREFIXTITLE;
  cs.dwExStyle |= WS_EX_CLIENTEDGE;
  return TRUE;
}

BOOL CFrameWnd::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                       const RECT& rect, CWnd* pParentWnd, LPCTSTR lpszMenuName,
                       DWORD dwExStyle, CCreateContext* pContext) {
  HMENU hMenu = NULL;
  if (lpszMenuName != NULL && !(dwStyle & WS_CHILD)) {
    HINSTANCE hInst = AfxFindResourceHandle(lpszMenuName, RT_MENU);
    hMenu = ::LoadMenu(hInst, lpszMenuName);
  }
  m_strTitle = lpszWindowName;

  // CW_USEDEFAULT is INT_MIN, so do the size arithmetic unsigned.
  int cx = (int)((UINT)rect.right - (UINT)rect.left);
  int cy = (int)((UINT)rect.bottom - (UINT)rect.top);
  if (!CreateEx(dwExStyle, lpszClassName, lpszWindowName, dwStyle, rect.left, rect.top, cx, cy,
                pParentWnd->GetSafeHwnd(), hMenu, (LPVOID)pContext)) {
    if (hMenu != NULL) ::DestroyMenu(hMenu);
    return FALSE;
  }
  return TRUE;
}

LPCTSTR CFrameWnd::GetIconWndClass(DWORD dwDefaultStyle, UINT nIDResource) {
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(nIDResource), RT_GROUP_ICON);
  HICON hIcon = ::LoadIcon(hInst, MAKEINTRESOURCE(nIDResource));
  if (hIcon == NULL) return NULL;

  // Ask PreCreateWindow which class it would use, and make a copy of
  // that class with this icon.
  CREATESTRUCT cs;
  memset(&cs, 0, sizeof(cs));
  cs.style = dwDefaultStyle;
  PreCreateWindow(cs);
  WNDCLASS wndcls;
  if (cs.lpszClass != NULL && ::GetClassInfo(AfxGetInstanceHandle(), cs.lpszClass, &wndcls) &&
      wndcls.hIcon != hIcon) {
    return AfxRegisterWndClass(wndcls.style, wndcls.hCursor, wndcls.hbrBackground, hIcon);
  }
  return NULL;
}

BOOL CFrameWnd::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                          CCreateContext* pContext) {
  m_nIDHelp = nIDResource;

  // The first part of the resource string is the frame's title.
  CString strFullString;
  if (strFullString.LoadString(nIDResource)) AfxExtractSubString(m_strTitle, strFullString, 0);

  if (!AfxDeferRegisterClass(AFX_WNDFRAMEORVIEW_REG)) return FALSE;
  LPCTSTR lpszClass = GetIconWndClass(dwDefaultStyle, nIDResource);
  CString strTitle = m_strTitle;
  if (!Create(lpszClass, strTitle, dwDefaultStyle, rectDefault, pParentWnd,
              MAKEINTRESOURCE(nIDResource), 0L, pContext)) {
    return FALSE;
  }

  m_hMenuDefault = ::GetMenu(m_hWnd);
  LoadAccelTable(MAKEINTRESOURCE(nIDResource));
  if (pContext == NULL) SendMessageToDescendants(WM_INITIALUPDATE, 0, 0, TRUE, TRUE);
  return TRUE;
}

BOOL CFrameWnd::LoadAccelTable(LPCTSTR lpszResourceName) {
  if (m_hAccelTable != NULL) return FALSE;
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_ACCELERATOR);
  m_hAccelTable = ::LoadAccelerators(hInst, lpszResourceName);
  return m_hAccelTable != NULL;
}

int CFrameWnd::OnCreate(LPCREATESTRUCT lpcs) {
  return OnCreateHelper(lpcs, (CCreateContext*)lpcs->lpCreateParams);
}

int CFrameWnd::OnCreateHelper(LPCREATESTRUCT lpcs, CCreateContext* pContext) {
  if (CWnd::OnCreate(lpcs) == -1) return -1;
  if (!OnCreateClient(lpcs, pContext)) return -1;
  PostMessage(WM_SETMESSAGESTRING, AFX_IDS_IDLEMESSAGE);
  RecalcLayout();
  return 0;
}

BOOL CFrameWnd::OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext) {
  (void)lpcs;
  if (pContext != NULL && pContext->m_pNewViewClass != NULL) {
    if (CreateView(pContext, AFX_IDW_PANE_FIRST) == NULL) return FALSE;
  }
  return TRUE;
}

CWnd* CFrameWnd::CreateView(CCreateContext* pContext, UINT nID) {
  CWnd* pView = (CWnd*)pContext->m_pNewViewClass->CreateObject();
  if (pView == NULL) return NULL;
  // A view deletes itself when its window goes away.
  if (!pView->Create(NULL, NULL, AFX_WS_DEFAULT_VIEW, CRect(0, 0, 0, 0), this, nID, pContext))
    return NULL;
  // The view draws the sunken edge, so the frame need not.
  if (pView->GetExStyle() & WS_EX_CLIENTEDGE) ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
  return pView;
}

void CFrameWnd::PostNcDestroy() { delete this; }

// ---------------------------------------------------------------------
// Views and documents

CDocument* CFrameWnd::GetActiveDocument() {
  CView* pView = GetActiveView();
  return pView != NULL ? pView->GetDocument() : NULL;
}

void CFrameWnd::SetActiveView(CView* pViewNew, BOOL bNotify) {
  CView* pViewOld = m_pViewActive;
  if (pViewNew == pViewOld) return;
  m_pViewActive = NULL;
  if (pViewOld != NULL && bNotify) pViewOld->OnActivateView(FALSE, pViewNew, pViewOld);
  // The old view may have picked a new active view itself.
  if (m_pViewActive != NULL) return;
  m_pViewActive = pViewNew;
  if (pViewNew != NULL && bNotify) pViewNew->OnActivateView(TRUE, pViewNew, pViewOld);
}

void CFrameWnd::InitialUpdateFrame(CDocument* pDoc, BOOL bMakeVisible) {
  CView* pView = NULL;
  if (GetActiveView() == NULL) {
    CWnd* pWnd = GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE);
    if (pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CView))) {
      pView = (CView*)pWnd;
      SetActiveView(pView, FALSE);
    }
  }

  if (bMakeVisible) {
    SendMessageToDescendants(WM_INITIALUPDATE, 0, 0, TRUE, TRUE);
    if (pView != NULL) pView->OnActivateFrame(WA_INACTIVE, this);

    // The application's show command applies to its first main window.
    int nCmdShow = -1;
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL && pApp->m_pMainWnd == this) {
      nCmdShow = pApp->m_nCmdShow;
      pApp->m_nCmdShow = -1;
    }
    ActivateFrame(nCmdShow);
    if (pView != NULL) pView->OnActivateView(TRUE, pView, pView);
  }

  if (pDoc != NULL) pDoc->UpdateFrameCounts();
  OnUpdateFrameTitle(TRUE);
}

void CFrameWnd::ActivateFrame(int nCmdShow) {
  if (nCmdShow == -1) {
    if (!IsWindowVisible())
      nCmdShow = SW_SHOWNORMAL;
    else if (IsIconic())
      nCmdShow = SW_RESTORE;
  }
  BringToTop(nCmdShow);
  if (nCmdShow != -1) {
    ShowWindow(nCmdShow);
    BringToTop(nCmdShow);
  }
}

void CFrameWnd::BringToTop(int nCmdShow) {
  if (nCmdShow == SW_HIDE || nCmdShow == SW_MINIMIZE || nCmdShow == SW_SHOWMINNOACTIVE ||
      nCmdShow == SW_SHOWNA || nCmdShow == SW_SHOWNOACTIVATE)
    return;
  if (::GetParent(m_hWnd) == NULL && IsWindowVisible()) {
    HWND hWnd = ::GetLastActivePopup(m_hWnd);
    ::BringWindowToTop(hWnd != NULL ? hWnd : m_hWnd);
  }
}

// ---------------------------------------------------------------------
// Titles and menus

void CFrameWnd::OnUpdateFrameTitle(BOOL bAddToTitle) {
  if ((GetStyle() & FWS_ADDTOTITLE) == 0) return;
  CDocument* pDocument = GetActiveDocument();
  if (bAddToTitle && pDocument != NULL)
    UpdateFrameTitleForDocument(pDocument->GetTitle());
  else
    UpdateFrameTitleForDocument(NULL);
}

static void _AfxSetWindowTextIfChanged(HWND hWnd, LPCTSTR lpszNew) {
  int nNewLen = lstrlen(lpszNew);
  TCHAR szOld[256];
  if (nNewLen > 255 || ::GetWindowText(hWnd, szOld, 256) != nNewLen ||
      lstrcmp(szOld, lpszNew) != 0) {
    ::SetWindowText(hWnd, lpszNew);
  }
}

void CFrameWnd::UpdateFrameTitleForDocument(LPCTSTR lpszDocName) {
  CString strText;
  CString strDoc;
  if (lpszDocName != NULL) {
    strDoc = lpszDocName;
    if (m_nWindow > 0) {
      TCHAR szNum[16];
      wsprintf(szNum, _T(":%d"), m_nWindow);
      strDoc += szNum;
    }
  }
  if (GetStyle() & FWS_PREFIXTITLE) {
    if (lpszDocName != NULL) {
      strText = strDoc;
      if (!m_strTitle.IsEmpty()) strText += _T(" - ");
    }
    strText += m_strTitle;
  } else {
    strText = m_strTitle;
    if (lpszDocName != NULL) {
      if (!strText.IsEmpty()) strText += _T(" - ");
      strText += strDoc;
    }
  }
  _AfxSetWindowTextIfChanged(m_hWnd, strText);
}

void CFrameWnd::OnUpdateFrameMenu(HMENU hMenuAlt) {
  if (GetStyle() & WS_CHILD) return;
  if (hMenuAlt == NULL) {
    CDocument* pDoc = GetActiveDocument();
    if (pDoc != NULL) hMenuAlt = pDoc->GetDefaultMenu();
    if (hMenuAlt == NULL) hMenuAlt = m_hMenuDefault;
  }
  if (::GetMenu(m_hWnd) != hMenuAlt) ::SetMenu(m_hWnd, hMenuAlt);
}

void CFrameWnd::DelayUpdateFrameMenu(HMENU hMenuAlt) {
  m_hMenuAlt = hMenuAlt;
  m_nIdleFlags |= idleMenu;
}

HACCEL CFrameWnd::GetDefaultAccelerator() {
  HACCEL hAccel = m_hAccelTable;
  CDocument* pDoc = GetActiveDocument();
  if (pDoc != NULL) {
    HACCEL hDocAccel = pDoc->GetDefaultAccelerator();
    if (hDocAccel != NULL) hAccel = hDocAccel;
  }
  return hAccel;
}

void CFrameWnd::OnIdleUpdateCmdUI() {
  if (m_nIdleFlags & idleMenu) {
    m_nIdleFlags &= ~idleMenu;
    OnUpdateFrameMenu(m_hMenuAlt);
  }
  if (m_nIdleFlags & idleTitle) {
    m_nIdleFlags &= ~idleTitle;
    OnUpdateFrameTitle(TRUE);
  }
  if (m_nIdleFlags & idleLayout) {
    BOOL bNotify = (m_nIdleFlags & idleNotify) != 0;
    m_nIdleFlags &= ~(idleLayout | idleNotify);
    RecalcLayout(bNotify);
    UpdateWindow();
  }
}

LRESULT CFrameWnd::OnIdleUpdateCmdUIMsg(WPARAM, LPARAM) {
  OnIdleUpdateCmdUI();
  return 0;
}

// ---------------------------------------------------------------------
// Command routing

BOOL CFrameWnd::PreTranslateMessage(MSG* pMsg) {
  if (CWnd::PreTranslateMessage(pMsg)) return TRUE;
  if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
    HACCEL hAccel = GetDefaultAccelerator();
    return hAccel != NULL && ::TranslateAccelerator(m_hWnd, hAccel, pMsg);
  }
  return FALSE;
}

BOOL CFrameWnd::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  CPushRoutingFrame push(this);

  // The active view (and through it the document) gets the first try.
  CView* pView = GetActiveView();
  if (pView != NULL && pView->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  if (CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL && pApp->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  return FALSE;
}

BOOL CFrameWnd::OnCommand(WPARAM wParam, LPARAM lParam) {
  // A menu command or accelerator puts the idle prompt back.
  if (lParam == 0 && m_nIDTracking != 0) {
    m_nIDTracking = 0;
    SendMessage(WM_SETMESSAGESTRING, AFX_IDS_IDLEMESSAGE);
  }
  return CWnd::OnCommand(wParam, lParam);
}

LRESULT CFrameWnd::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  return CWnd::WindowProc(message, wParam, lParam);
}

void CFrameWnd::OnInitMenuPopup(CMenu* pMenu, UINT nIndex, BOOL bSysMenu) {
  (void)nIndex;
  AfxCancelModes(m_hWnd);
  if (bSysMenu) return;

  CCmdUI state;
  state.m_pMenu = pMenu;
  state.m_nIndexMax = pMenu->GetMenuItemCount();

  // A popup opened from the menu bar has the bar as its parent menu,
  // which is what CCmdUI::Enable needs to grey a popup's title.
  HMENU hMenuBar = ::GetMenu(m_hWnd);
  if (hMenuBar != NULL) {
    int nCount = ::GetMenuItemCount(hMenuBar);
    for (int i = 0; i < nCount; i++) {
      if (::GetSubMenu(hMenuBar, i) == pMenu->m_hMenu) {
        state.m_pParentMenu = CMenu::FromHandle(hMenuBar);
        break;
      }
    }
  }

  for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax; state.m_nIndex++) {
    state.m_nID = pMenu->GetMenuItemID(state.m_nIndex);
    if (state.m_nID == 0) continue;  // separator

    if (state.m_nID == (UINT)-1) {
      // A cascading popup takes the state of its first item.
      state.m_pSubMenu = pMenu->GetSubMenu(state.m_nIndex);
      if (state.m_pSubMenu == NULL ||
          (state.m_nID = state.m_pSubMenu->GetMenuItemID(0)) == 0 ||
          state.m_nID == (UINT)-1)
        continue;
      state.DoUpdate(this, FALSE);
    } else {
      state.m_pSubMenu = NULL;
      state.DoUpdate(this, m_bAutoMenuEnable && state.m_nID < 0xF000);
    }

    // A handler may have added or removed items.
    UINT nCount = pMenu->GetMenuItemCount();
    if (nCount < state.m_nIndexMax) {
      state.m_nIndex -= (state.m_nIndexMax - nCount);
      while (state.m_nIndex < nCount && pMenu->GetMenuItemID(state.m_nIndex) == state.m_nID)
        state.m_nIndex++;
    }
    state.m_nIndexMax = nCount;
  }
}

// ---------------------------------------------------------------------
// Status bar prompts

CWnd* CFrameWnd::GetMessageBar() { return GetDescendantWindow(AFX_IDW_STATUS_BAR, TRUE); }

void CFrameWnd::GetMessageString(UINT nID, CString& rMessage) const {
  // The prompt is the part of the string before the newline (the part
  // after it is the tooltip).
  rMessage.Empty();
  if (AfxLoadString(nID, rMessage)) {
    int i = rMessage.Find(_T('\n'));
    if (i >= 0) rMessage = rMessage.Left(i);
  }
}

void CFrameWnd::SetMessageText(LPCTSTR lpszText) {
  SendMessage(WM_SETMESSAGESTRING, 0, (LPARAM)lpszText);
}

void CFrameWnd::SetMessageText(UINT nID) { SendMessage(WM_SETMESSAGESTRING, (WPARAM)nID); }

LRESULT CFrameWnd::OnSetMessageString(WPARAM wParam, LPARAM lParam) {
  UINT nIDLast = m_nIDLastMessage;
  m_nIDLastMessage = (UINT)wParam;
  m_nIDTracking = (UINT)wParam;

  CWnd* pMessageBar = GetMessageBar();
  if (pMessageBar != NULL) {
    CString strMessage;
    LPCTSTR lpsz = NULL;
    if (lParam != 0) {
      lpsz = (LPCTSTR)lParam;
    } else if (wParam != 0) {
      UINT nID = (UINT)wParam;
      if (nID == AFX_IDS_IDLEMESSAGE && GetTopLevelFrame() != NULL &&
          GetTopLevelFrame()->m_bHelpMode)
        nID = AFX_IDS_HELPMODEMESSAGE;
      GetMessageString(nID, strMessage);
      lpsz = strMessage;
    }
    pMessageBar->SetWindowText(lpsz != NULL ? lpsz : _T(""));
  }
  return nIDLast;
}

LRESULT CFrameWnd::OnPopMessageString(WPARAM wParam, LPARAM lParam) {
  return SendMessage(WM_SETMESSAGESTRING, wParam, lParam);
}

void CFrameWnd::OnMenuSelect(UINT nItemID, UINT nFlags, HMENU hSysMenu) {
  if (nFlags == 0xFFFF && hSysMenu == NULL) {
    // The menu closed.
    m_nIDTracking = AFX_IDS_IDLEMESSAGE;
  } else if (nItemID == 0 ||
             (nFlags & (MF_SEPARATOR | MF_POPUP | MF_MENUBREAK | MF_MENUBARBREAK))) {
    m_nIDTracking = 0;
  } else if (nItemID >= 0xF000 && nItemID < 0xF1F0) {
    // System commands have prompts at AFX_IDS_SCFIRST.
    m_nIDTracking = AFX_IDS_SCFIRST + ((nItemID - 0xF000) >> 4);
  } else if (nItemID >= AFX_IDM_FIRST_MDICHILD) {
    m_nIDTracking = AFX_IDS_MDICHILD;
  } else {
    m_nIDTracking = nItemID;
  }
  if (m_nIDTracking != m_nIDLastMessage) SendMessage(WM_SETMESSAGESTRING, m_nIDTracking);
}

void CFrameWnd::OnEnterIdle(UINT nWhy, CWnd* pWho) {
  CWnd::OnEnterIdle(nWhy, pWho);
  if (nWhy != MSGF_MENU || m_nIDTracking == m_nIDLastMessage) return;
  SendMessage(WM_SETMESSAGESTRING, m_nIDTracking);
}

// ---------------------------------------------------------------------
// Layout

void CFrameWnd::RecalcLayout(BOOL bNotify) {
  (void)bNotify;
  if (m_bInRecalcLayout) return;
  m_bInRecalcLayout = TRUE;
  RepositionBars(0, 0xFFFF, AFX_IDW_PANE_FIRST, reposExtra, &m_rectBorder);
  m_bInRecalcLayout = FALSE;
}

BOOL CFrameWnd::NegotiateBorderSpace(UINT nBorderCmd, LPRECT lpRectBorder) {
  switch (nBorderCmd) {
    case 1:  // borderGet
      GetClientRect(lpRectBorder);
      return TRUE;
    case 2:  // borderRequest
      return TRUE;
    case 3:  // borderSet
      if (lpRectBorder != NULL)
        m_rectBorder = *lpRectBorder;
      else
        m_rectBorder.SetRectEmpty();
      RecalcLayout(FALSE);
      return TRUE;
  }
  return FALSE;
}

void CFrameWnd::AddControlBar(CControlBar* pBar) { m_listControlBars.AddTail(pBar); }

void CFrameWnd::RemoveControlBar(CControlBar* pBar) {
  POSITION pos = m_listControlBars.Find(pBar);
  if (pos != NULL) m_listControlBars.RemoveAt(pos);
}

void CFrameWnd::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  if (nType != SIZE_MINIMIZED) RecalcLayout();
}

BOOL CFrameWnd::OnEraseBkgnd(CDC* pDC) {
  // The view covers the client area and paints itself.
  if (m_pViewActive != NULL) return TRUE;
  return CWnd::OnEraseBkgnd(pDC);
}

void CFrameWnd::OnSetFocus(CWnd* pOldWnd) {
  if (m_pViewActive != NULL)
    m_pViewActive->SetFocus();
  else
    CWnd::OnSetFocus(pOldWnd);
}

void CFrameWnd::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized) {
  CWnd::OnActivate(nState, pWndOther, bMinimized);
  CFrameWnd* pActiveFrame = GetActiveFrame();
  CView* pActiveView = pActiveFrame != NULL ? pActiveFrame->GetActiveView() : NULL;
  if (pActiveView != NULL) {
    if (nState != WA_INACTIVE && !bMinimized)
      pActiveView->OnActivateView(TRUE, pActiveView, pActiveView);
    pActiveView->OnActivateFrame(nState, this);
  }
}

BOOL CFrameWnd::OnNcActivate(BOOL bActive) { return CWnd::OnNcActivate(bActive); }

void CFrameWnd::OnSysCommand(UINT nID, LPARAM lParam) { CWnd::OnSysCommand(nID, lParam); }

LRESULT CFrameWnd::OnSetText(WPARAM, LPARAM) { return Default(); }

LRESULT CFrameWnd::OnActivateTopLevel(WPARAM, LPARAM) { return 0; }

// ---------------------------------------------------------------------
// Modal state

void CFrameWnd::BeginModalState() {
  if (m_cModalStack++ > 0) return;

  // Disable this frame and every enabled top-level window it owns.
  DWORD dwThread = ::GetCurrentThreadId();
  int nCount = 0;
  for (HWND hWnd = ::GetWindow(::GetDesktopWindow(), GW_CHILD); hWnd != NULL;
       hWnd = ::GetWindow(hWnd, GW_HWNDNEXT))
    nCount++;
  delete[] m_phWndDisable;
  m_phWndDisable = new HWND[nCount + 2];
  int n = 0;
  for (HWND hWnd = ::GetWindow(::GetDesktopWindow(), GW_CHILD); hWnd != NULL && n < nCount;
       hWnd = ::GetWindow(hWnd, GW_HWNDNEXT)) {
    if (hWnd != m_hWnd && ::IsWindowEnabled(hWnd) &&
        ::GetWindowThreadProcessId(hWnd, NULL) == dwThread && AfxIsDescendant(m_hWnd, hWnd) &&
        ::SendMessage(hWnd, WM_DISABLEMODAL, 0, 0) == 0) {
      m_phWndDisable[n++] = hWnd;
    }
  }
  if (::IsWindowEnabled(m_hWnd)) m_phWndDisable[n++] = m_hWnd;
  m_phWndDisable[n] = NULL;
  for (int i = 0; i < n; i++) ::EnableWindow(m_phWndDisable[i], FALSE);
}

void CFrameWnd::EndModalState() {
  if (m_cModalStack == 0 || --m_cModalStack > 0) return;
  if (m_phWndDisable != NULL) {
    for (HWND* p = m_phWndDisable; *p != NULL; p++) {
      if (::IsWindow(*p)) ::EnableWindow(*p, TRUE);
    }
    delete[] m_phWndDisable;
    m_phWndDisable = NULL;
  }
}

void CFrameWnd::ShowOwnedWindows(BOOL bShow) { ::ShowOwnedPopups(m_hWnd, bShow); }

// ---------------------------------------------------------------------
// Closing

void CFrameWnd::OnClose() {
  CDocument* pDocument = GetActiveDocument();
  if (pDocument != NULL && !pDocument->CanCloseFrame(this)) return;

  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL && pApp->m_pMainWnd == this) {
    // Closing the main window ends the application.
    if (pDocument == NULL && !pApp->SaveAllModified()) return;
    pApp->HideApplication();
    pApp->CloseAllDocuments(FALSE);

    // In an SDI application that also destroyed this window.
    if (pApp->m_pMainWnd == NULL) return;
  }

  // Closing the last frame of a document closes the document.
  if (pDocument != NULL && pDocument->m_bAutoDelete) {
    BOOL bOtherFrame = FALSE;
    POSITION pos = pDocument->GetFirstViewPosition();
    while (pos != NULL) {
      CView* pView = pDocument->GetNextView(pos);
      if (pView->GetParentFrame() != this) {
        bOtherFrame = TRUE;
        break;
      }
    }
    if (!bOtherFrame) {
      pDocument->OnCloseDocument();
      return;
    }
    pDocument->PreCloseFrame(this);
  }

  DestroyWindow();
}

void CFrameWnd::OnDestroy() {
  // Put back the menu the frame was created with, so that the menu bar
  // destroyed with the window is that one and not a shared one.
  if (m_hMenuDefault != NULL && !(GetStyle() & WS_CHILD) && ::GetMenu(m_hWnd) != m_hMenuDefault)
    ::SetMenu(m_hWnd, m_hMenuDefault);

  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL && pApp->m_pMainWnd == this && pApp->m_pszHelpFilePath != NULL)
    ::WinHelp(m_hWnd, NULL, HELP_QUIT, 0);

  CWnd::OnDestroy();
}

BOOL CFrameWnd::OnQueryEndSession() {
  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL && pApp->m_pMainWnd == this) return pApp->SaveAllModified();
  return TRUE;
}

void CFrameWnd::OnEndSession(BOOL bEnding) {
  if (!bEnding) return;
  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL && pApp->m_pMainWnd == this) {
    pApp->CloseAllDocuments(TRUE);
    pApp->ExitInstance();
  }
}

void CFrameWnd::OnDropFiles(HDROP hDropInfo) {
  SetActiveWindow();
  CWinApp* pApp = AfxGetApp();
  UINT nFiles = ::DragQueryFile(hDropInfo, (UINT)-1, NULL, 0);
  for (UINT i = 0; i < nFiles; i++) {
    TCHAR szFileName[MAX_PATH];
    ::DragQueryFile(hDropInfo, i, szFileName, MAX_PATH);
    if (pApp != NULL) pApp->OpenDocumentFile(szFileName);
  }
  ::DragFinish(hDropInfo);
}

// ---------------------------------------------------------------------
// Help and key indicators

LRESULT CFrameWnd::OnCommandHelp(WPARAM, LPARAM lParam) {
  if (lParam == 0) {
    if (m_nIDTracking > AFX_IDS_IDLEMESSAGE || m_nIDTracking == 0)
      lParam = m_nIDHelp != 0 ? HID_BASE_RESOURCE + m_nIDHelp : 0;
    else
      lParam = HID_BASE_COMMAND + m_nIDTracking;
  }
  if (lParam != 0) {
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL) pApp->WinHelp(lParam);
    return TRUE;
  }
  return FALSE;
}

LRESULT CFrameWnd::OnHelpHitTest(WPARAM, LPARAM) {
  return m_nIDHelp != 0 ? HID_BASE_RESOURCE + m_nIDHelp : 0;
}

void CFrameWnd::OnHelp() {
  if (!SendMessage(WM_COMMANDHELP, 0, 0)) {
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL) pApp->WinHelp(0L, HELP_CONTENTS);
  }
}

void CFrameWnd::OnContextHelp() { OnHelp(); }

void CFrameWnd::OnUpdateContextHelp(CCmdUI* pCmdUI) { pCmdUI->SetCheck(m_bHelpMode); }

void CFrameWnd::OnUpdateKeyIndicator(CCmdUI* pCmdUI) {
  UINT nVK;
  switch (pCmdUI->m_nID) {
    case ID_INDICATOR_CAPS: nVK = VK_CAPITAL; break;
    case ID_INDICATOR_NUM: nVK = VK_NUMLOCK; break;
    case ID_INDICATOR_SCRL: nVK = VK_SCROLL; break;
    case ID_INDICATOR_KANA: nVK = VK_KANA; break;
    default:
      pCmdUI->ContinueRouting();
      return;
  }
  pCmdUI->Enable(::GetKeyState(nVK) & 1);
}

// ---------------------------------------------------------------------
// Helpers

BOOL AFXAPI _AfxIsComboBoxControl(HWND hWnd, UINT nStyle) {
  if (hWnd == NULL) return FALSE;
  TCHAR szClass[16];
  if (::GetClassName(hWnd, szClass, 16) == 0 || lstrcmpi(szClass, _T("combobox")) != 0)
    return FALSE;
  return (::GetWindowLong(hWnd, GWL_STYLE) & 0x0F) == nStyle;
}

void AFXAPI AfxCancelModes(HWND hWndRcvr) {
  // Close an open drop-down list, which would otherwise stay open over
  // a menu.
  HWND hWndCancel = ::GetFocus();
  if (hWndCancel == NULL || hWndCancel == hWndRcvr) return;
  if (_AfxIsComboBoxControl(hWndCancel, CBS_DROPDOWNLIST)) {
    ::SendMessage(hWndCancel, CB_SHOWDROPDOWN, FALSE, 0);
    return;
  }
  HWND hWndParent = ::GetParent(hWndCancel);
  if (_AfxIsComboBoxControl(hWndParent, CBS_DROPDOWN))
    ::SendMessage(hWndParent, CB_SHOWDROPDOWN, FALSE, 0);
}
