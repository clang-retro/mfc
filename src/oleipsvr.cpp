// oleipsvr.cpp - in-place activation, the server's side: COleServerDoc's
// IOleInPlaceObject and IOleInPlaceActiveObject, the in-place frame
// (COleIPFrameWnd) in the container's window, the container's frames as
// the server's bars see them (COleCntrFrameWnd), and COleResizeBar.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxole.h>
#include <afxpriv.h>

#define OLE_TRY try {
#define OLE_CATCH(hr)              \
  }                                \
  catch (COleException * e) {      \
    hr = e->m_sc;                  \
    e->Delete();                   \
  }                                \
  catch (CException * e) {         \
    hr = E_UNEXPECTED;             \
    e->Delete();                   \
  }

// ---------------------------------------------------------------------
// CDocTemplate

CFrameWnd* CDocTemplate::CreateOleFrame(CWnd* pParentWnd, CDocument* pDoc, BOOL bCreateView) {
  if (m_pOleFrameClass == NULL) return NULL;
  CCreateContext context;
  context.m_pCurrentDoc = pDoc;
  context.m_pNewViewClass =
      bCreateView ? (m_pOleViewClass != NULL ? m_pOleViewClass : m_pViewClass) : NULL;
  context.m_pNewDocTemplate = this;
  CFrameWnd* pFrame = (CFrameWnd*)m_pOleFrameClass->CreateObject();
  if (pFrame == NULL) return NULL;
  // A frame that fails after its window exists deletes itself.
  if (!pFrame->LoadFrame(m_nIDServerResource, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                         pParentWnd, &context))
    return NULL;
  return pFrame;
}

// ---------------------------------------------------------------------
// The in-place frames, for idle updates: they are not under the program's
// main window, which is hidden while it serves an object.

static CPtrList s_listInPlaceFrames;

static void AFXAPI IdleInPlaceFrames() {
  POSITION pos = s_listInPlaceFrames.GetHeadPosition();
  while (pos != NULL) {
    COleIPFrameWnd* pFrame = (COleIPFrameWnd*)s_listInPlaceFrames.GetNext(pos);
    if (pFrame->m_hWnd == NULL || !pFrame->IsWindowVisible()) continue;
    pFrame->SendMessage(WM_IDLEUPDATECMDUI, (WPARAM)TRUE);
    pFrame->SendMessageToDescendants(WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0, TRUE, TRUE);
  }
}

// ---------------------------------------------------------------------
// COleCntrFrameWnd

IMPLEMENT_DYNAMIC(COleCntrFrameWnd, CFrameWnd)

COleCntrFrameWnd::COleCntrFrameWnd(COleIPFrameWnd* pInPlaceFrame, HWND hWnd,
                                   LPOLEINPLACEUIWINDOW lpWindow)
    : m_pInPlaceFrame(pInPlaceFrame), m_lpWindow(lpWindow) {
  // The container's window, not subclassed: messages to it stay there.
  m_hWnd = hWnd;
  _AfxRegisterInPlaceDockSite(this, TRUE);
}

COleCntrFrameWnd::~COleCntrFrameWnd() {
  // The dock bars made on the container's window go; the bars in them
  // go with them.
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (pBar->IsDockBar() && pBar->m_hWnd != NULL && !((CDockBar*)pBar)->m_bFloating) {
      pBar->DestroyWindow();
      pos = m_listControlBars.GetHeadPosition();  // the list changed
    }
  }
  DestroyDockBars();  // the floating ones
  _AfxRegisterInPlaceDockSite(this, FALSE);
  m_listControlBars.RemoveAll();
  m_hWnd = NULL;
}

CFrameWnd* COleCntrFrameWnd::GetActiveFrame() { return this; }

// The server's bars go in the border the container has left, and the
// container is told how much of it they take.
void COleCntrFrameWnd::RecalcLayout(BOOL) {
  if (m_lpWindow == NULL || m_hWnd == NULL || m_bInRecalcLayout) return;
  if (m_pInPlaceFrame == NULL || !m_pInPlaceFrame->m_bUIActive) return;
  m_bInRecalcLayout = TRUE;
  CRect rectBorder;
  if (SUCCEEDED(m_lpWindow->GetBorder(&rectBorder))) {
    CRect rectLeft;
    RepositionBars(0, 0xFFFF, 0, reposQuery, &rectLeft, &rectBorder, TRUE);
    BORDERWIDTHS widths = {rectLeft.left - rectBorder.left, rectLeft.top - rectBorder.top,
                           rectBorder.right - rectLeft.right,
                           rectBorder.bottom - rectLeft.bottom};
    if (widths.left == 0 && widths.top == 0 && widths.right == 0 && widths.bottom == 0) {
      m_lpWindow->SetBorderSpace(NULL);
    } else if (m_lpWindow->RequestBorderSpace(&widths) == S_OK) {
      m_lpWindow->SetBorderSpace(&widths);
      // The container may have moved its own bars out of the way.
      m_lpWindow->GetBorder(&rectBorder);
      RepositionBars(0, 0xFFFF, 0, reposDefault, NULL, &rectBorder, TRUE);
    }
  }
  m_bInRecalcLayout = FALSE;
}

// ---------------------------------------------------------------------
// COleIPFrameWnd

IMPLEMENT_DYNCREATE(COleIPFrameWnd, CFrameWnd)

BEGIN_MESSAGE_MAP(COleIPFrameWnd, CFrameWnd)
  ON_WM_SIZE()
  ON_WM_DESTROY()
  ON_MESSAGE(WM_SETMESSAGESTRING, OnSetMessageString)
END_MESSAGE_MAP()

COleIPFrameWnd::COleIPFrameWnd()
    : m_bUIActive(FALSE),
      m_bInsideRecalc(FALSE),
      m_bBarsCreated(FALSE),
      m_hSharedMenu(NULL),
      m_hOleMenu(NULL),
      m_lpFrame(NULL),
      m_lpDocFrame(NULL),
      m_pMainFrame(NULL),
      m_pDocFrame(NULL) {
  m_rectPos.SetRectEmpty();
  m_rectClip.SetRectEmpty();
  memset(&m_frameInfo, 0, sizeof(m_frameInfo));
  m_frameInfo.cb = sizeof(m_frameInfo);
  s_listInPlaceFrames.AddTail(this);
  _afxpfnInPlaceIdle = IdleInPlaceFrames;
}

COleIPFrameWnd::~COleIPFrameWnd() {
  POSITION pos = s_listInPlaceFrames.Find(this);
  if (pos != NULL) s_listInPlaceFrames.RemoveAt(pos);
  delete m_pMainFrame;
  delete m_pDocFrame;
  if (m_lpFrame != NULL) m_lpFrame->Release();
  if (m_lpDocFrame != NULL) m_lpDocFrame->Release();
}

BOOL COleIPFrameWnd::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                               CCreateContext* pContext) {
  if (!CFrameWnd::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext)) return FALSE;
  m_hMenuDefault = NULL;  // a child has no menu bar; the container shows the menus
  return TRUE;
}

BOOL COleIPFrameWnd::PreCreateWindow(CREATESTRUCT& cs) {
  if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
  cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
  cs.style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_SYSMENU | FWS_ADDTOTITLE);
  cs.style |= WS_CHILD | WS_CLIPSIBLINGS;
  return TRUE;
}

BOOL COleIPFrameWnd::OnCreateControlBars(CFrameWnd*, CFrameWnd*) { return TRUE; }

void COleIPFrameWnd::RepositionFrame(LPCRECT lpPosRect, LPCRECT lpClipRect) {
  m_rectPos = *lpPosRect;
  m_rectClip = *lpClipRect;
  CRect rect(lpPosRect);
  // A resize bar draws its border outside the item.
  CWnd* pBar = GetDlgItem(AFX_IDW_RESIZE_BAR);
  if (pBar != NULL && pBar->IsKindOf(RUNTIME_CLASS(COleResizeBar)) &&
      (pBar->GetStyle() & WS_VISIBLE)) {
    int nWidth = ((COleResizeBar*)pBar)->GetBorderWidth();
    rect.InflateRect(nWidth, nWidth);
  }
  SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(),
               SWP_NOZORDER | SWP_NOACTIVATE);
  CRect rectVisible;
  rectVisible.IntersectRect(rect, lpClipRect);
  if (rectVisible.EqualRect(rect)) {
    ::SetWindowRgn(m_hWnd, NULL, TRUE);
  } else {
    rectVisible.OffsetRect(-rect.left, -rect.top);
    ::SetWindowRgn(m_hWnd, ::CreateRectRgnIndirect(&rectVisible), TRUE);
  }
}

// The server's keys, then the container's.
BOOL COleIPFrameWnd::PreTranslateMessage(MSG* pMsg) {
  if (CWnd::PreTranslateMessage(pMsg)) return TRUE;
  if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
    HACCEL hAccel = GetDefaultAccelerator();
    if (hAccel != NULL && ::TranslateAccelerator(m_hWnd, hAccel, pMsg)) return TRUE;
    if (m_bUIActive && m_lpFrame != NULL &&
        (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN) &&
        ::OleTranslateAccelerator(m_lpFrame, &m_frameInfo, pMsg) == S_OK)
      return TRUE;
  }
  return FALSE;
}

void COleIPFrameWnd::RecalcLayout(BOOL bNotify) {
  if (m_bInsideRecalc) return;
  m_bInsideRecalc = TRUE;
  CFrameWnd::RecalcLayout(bNotify);
  m_bInsideRecalc = FALSE;
}

// The frame's own updates, then those of the bars on the container's
// frames, which are not its children.
void COleIPFrameWnd::OnIdleUpdateCmdUI() {
  CFrameWnd::OnIdleUpdateCmdUI();
  COleCntrFrameWnd* frames[2] = {m_pMainFrame, m_pDocFrame};
  for (COleCntrFrameWnd* pFrame : frames) {
    if (pFrame == NULL) continue;
    if (pFrame->m_nIdleFlags & idleLayout) {
      pFrame->m_nIdleFlags &= ~(idleLayout | idleNotify);
      pFrame->RecalcLayout();
    }
    if (!m_bUIActive) continue;
    POSITION pos = pFrame->m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)pFrame->m_listControlBars.GetNext(pos);
      if (pBar->m_hWnd != NULL && pBar->IsVisible() && !pBar->IsDockBar())
        pBar->SendMessage(WM_IDLEUPDATECMDUI, (WPARAM)TRUE);
    }
  }
}

void COleIPFrameWnd::OnSize(UINT nType, int cx, int cy) { CFrameWnd::OnSize(nType, cx, cy); }

void COleIPFrameWnd::OnDestroy() {
  DestroySharedMenu();
  // The bars on the container's frames go before this frame's members do.
  delete m_pMainFrame;
  m_pMainFrame = NULL;
  delete m_pDocFrame;
  m_pDocFrame = NULL;
  CFrameWnd::OnDestroy();
}

LRESULT COleIPFrameWnd::OnSetMessageString(WPARAM wParam, LPARAM lParam) {
  UINT nIDLast = m_nIDLastMessage;
  m_nIDLastMessage = (UINT)wParam;
  m_nIDTracking = (UINT)wParam;
  if (m_lpFrame == NULL) return nIDLast;
  CString strMessage;
  if (lParam != 0)
    strMessage = (LPCTSTR)lParam;
  else if (wParam != 0)
    GetMessageString((UINT)wParam, strMessage);
  CStringW strText(strMessage);
  m_lpFrame->SetStatusText(strText);
  return nIDLast;
}

static CDocTemplate* FrameTemplate(CFrameWnd* pFrame) {
  CDocument* pDoc = pFrame->GetActiveDocument();
  return pDoc != NULL ? pDoc->GetDocTemplate() : NULL;
}

BOOL COleIPFrameWnd::BuildSharedMenu() {
  DestroySharedMenu();
  CDocTemplate* pTemplate = FrameTemplate(this);
  m_hSharedMenu = ::CreateMenu();
  if (m_hSharedMenu == NULL) return FALSE;
  OLEMENUGROUPWIDTHS widths;
  memset(&widths, 0, sizeof(widths));
  if (m_lpFrame == NULL || FAILED(m_lpFrame->InsertMenus(m_hSharedMenu, &widths)))
    memset(&widths, 0, sizeof(widths));
  widths.width[1] = widths.width[3] = widths.width[5] = 0;
  _AfxMergeMenus(m_hSharedMenu, pTemplate != NULL ? pTemplate->m_hMenuInPlaceServer : NULL,
                 widths.width, 1);
  m_hOleMenu = ::OleCreateMenuDescriptor(m_hSharedMenu, &widths);
  return TRUE;
}

void COleIPFrameWnd::DestroySharedMenu() {
  if (m_hSharedMenu == NULL) return;
  CDocTemplate* pTemplate = FrameTemplate(this);
  _AfxUnmergeMenus(m_hSharedMenu, pTemplate != NULL ? pTemplate->m_hMenuInPlaceServer : NULL);
  if (m_lpFrame != NULL) m_lpFrame->RemoveMenus(m_hSharedMenu);
  // Only what nobody took back is left to destroy.
  ::DestroyMenu(m_hSharedMenu);
  m_hSharedMenu = NULL;
  if (m_hOleMenu != NULL) {
    ::OleDestroyMenuDescriptor(m_hOleMenu);
    m_hOleMenu = NULL;
  }
}

// ---------------------------------------------------------------------
// COleResizeBar

IMPLEMENT_DYNAMIC(COleResizeBar, CControlBar)

BEGIN_MESSAGE_MAP(COleResizeBar, CControlBar)
  ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_SETCURSOR()
  ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

COleResizeBar::COleResizeBar() {
  m_tracker.m_nStyle = CRectTracker::hatchedBorder | CRectTracker::resizeOutside;
}

COleResizeBar::~COleResizeBar() {}

BOOL COleResizeBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  m_dwStyle = 0;
  return CWnd::Create(NULL, NULL, dwStyle | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pParentWnd, nID);
}

// The bar takes the frame's whole area but for the middle, where the view
// goes; a region keeps it off the view.
LRESULT COleResizeBar::OnSizeParent(WPARAM, LPARAM lParam) {
  AFX_SIZEPARENTPARAMS* lpLayout = (AFX_SIZEPARENTPARAMS*)lParam;
  if (!(GetStyle() & WS_VISIBLE)) return 0;
  CRect rect(lpLayout->rect);
  int nWidth = m_tracker.m_nHandleSize;
  AfxRepositionWindow(lpLayout, m_hWnd, &rect);
  CRect rectInner(0, 0, rect.Width(), rect.Height());
  rectInner.DeflateRect(nWidth, nWidth);
  HRGN hRgn = ::CreateRectRgn(0, 0, rect.Width(), rect.Height());
  HRGN hInner = ::CreateRectRgnIndirect(&rectInner);
  ::CombineRgn(hRgn, hRgn, hInner, RGN_DIFF);
  ::DeleteObject(hInner);
  ::SetWindowRgn(m_hWnd, hRgn, TRUE);
  m_tracker.m_rect = rectInner;
  lpLayout->rect.left += nWidth;
  lpLayout->rect.top += nWidth;
  lpLayout->rect.right -= nWidth;
  lpLayout->rect.bottom -= nWidth;
  return 0;
}

BOOL COleResizeBar::OnEraseBkgnd(CDC* pDC) {
  CRect rect;
  GetClientRect(&rect);
  pDC->FillSolidRect(rect, ::GetSysColor(COLOR_WINDOW));
  return TRUE;
}

void COleResizeBar::OnPaint() {
  CPaintDC dc(this);
  m_tracker.Draw(&dc);
}

BOOL COleResizeBar::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (pWnd == this && m_tracker.SetCursor(this, nHitTest)) return TRUE;
  return CControlBar::OnSetCursor(pWnd, nHitTest, message);
}

// Dragging a handle asks the container to give the item the new size.
void COleResizeBar::OnLButtonDown(UINT nFlags, CPoint point) {
  int nHit = m_tracker.HitTest(point);
  if (nHit < 0 || nHit == CRectTracker::hitMiddle) {
    CControlBar::OnLButtonDown(nFlags, point);
    return;
  }
  CRectTracker tracker(m_tracker.m_rect, m_tracker.m_nStyle);
  if (!tracker.Track(this, point, FALSE, NULL)) return;
  // The new rectangle in the container's window, where the item is.
  CRect rect(tracker.m_rect);
  CWnd* pFrame = GetParent();
  if (pFrame == NULL) return;
  HWND hWndContainer = ::GetParent(pFrame->m_hWnd);
  ::MapWindowPoints(m_hWnd, hWndContainer, (LPPOINT)&rect, 2);
  COleIPFrameWnd* pIPFrame =
      pFrame->IsKindOf(RUNTIME_CLASS(COleIPFrameWnd)) ? (COleIPFrameWnd*)pFrame : NULL;
  CDocument* pDoc = pIPFrame != NULL ? pIPFrame->GetActiveDocument() : NULL;
  if (pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(COleServerDoc)))
    ((COleServerDoc*)pDoc)->RequestPositionChange(&rect);
}

// ---------------------------------------------------------------------
// COleServerDoc: in-place activation

BOOL COleServerDoc::ActivateInPlace() {
  if (m_pInPlaceFrame != NULL) return UIActivate() || TRUE;
  if (m_lpClientSite == NULL) return FALSE;
  CDocTemplate* pTemplate = GetDocTemplate();
  if (pTemplate == NULL || pTemplate->m_pOleFrameClass == NULL) return FALSE;

  LPOLEINPLACESITE lpSite = NULL;
  if (FAILED(m_lpClientSite->QueryInterface(IID_IOleInPlaceSite, (void**)&lpSite)) ||
      lpSite == NULL)
    return FALSE;
  if (lpSite->CanInPlaceActivate() != S_OK || FAILED(lpSite->OnInPlaceActivate())) {
    lpSite->Release();
    return FALSE;
  }

  HWND hWndParent = NULL;
  LPOLEINPLACEFRAME lpFrame = NULL;
  LPOLEINPLACEUIWINDOW lpDocFrame = NULL;
  CRect rectPos, rectClip;
  OLEINPLACEFRAMEINFO frameInfo;
  memset(&frameInfo, 0, sizeof(frameInfo));
  frameInfo.cb = sizeof(frameInfo);
  COleIPFrameWnd* pFrame = NULL;
  if (SUCCEEDED(lpSite->GetWindow(&hWndParent)) && hWndParent != NULL &&
      SUCCEEDED(lpSite->GetWindowContext(&lpFrame, &lpDocFrame, &rectPos, &rectClip,
                                         &frameInfo))) {
    try {
      pFrame = CreateInPlaceFrame(CWnd::FromHandle(hWndParent));
    } catch (CException* e) {
      e->Delete();
      pFrame = NULL;
    }
  }
  if (pFrame == NULL) {
    if (lpFrame != NULL) lpFrame->Release();
    if (lpDocFrame != NULL) lpDocFrame->Release();
    lpSite->OnInPlaceDeactivate();
    lpSite->Release();
    return FALSE;
  }

  m_pInPlaceFrame = pFrame;
  m_lpInPlaceSite = lpSite;
  pFrame->m_lpFrame = lpFrame;
  pFrame->m_lpDocFrame = lpDocFrame;
  pFrame->m_frameInfo = frameInfo;

  if (!m_bShownBefore) {
    // The views have their initial update when they first show.
    m_bShownBefore = TRUE;
    pFrame->InitialUpdateFrame(this, FALSE);
  }
  pFrame->RepositionFrame(&rectPos, &rectClip);
  pFrame->ShowWindow(SW_SHOW);
  pFrame->RecalcLayout();
  UIActivate();
  return TRUE;
}

// The object becomes the one the user works with: its menus on the
// container's frame, its bars, and the focus.
BOOL COleServerDoc::UIActivate() {
  COleIPFrameWnd* pFrame = m_pInPlaceFrame;
  if (pFrame == NULL || m_lpInPlaceSite == NULL) return FALSE;
  if (pFrame->m_bUIActive) return TRUE;
  if (FAILED(m_lpInPlaceSite->OnUIActivate())) return FALSE;
  pFrame->m_bUIActive = TRUE;

  CStringW strName(GetTitle());
  LPOLEINPLACEACTIVEOBJECT lpActive = (LPOLEINPLACEACTIVEOBJECT)&m_xOleInPlaceActiveObject;
  if (pFrame->m_lpFrame != NULL) pFrame->m_lpFrame->SetActiveObject(lpActive, strName);
  if (pFrame->m_lpDocFrame != NULL) pFrame->m_lpDocFrame->SetActiveObject(lpActive, strName);

  if (pFrame->m_lpFrame != NULL) {
    pFrame->BuildSharedMenu();
    pFrame->m_lpFrame->SetMenu(pFrame->m_hSharedMenu, pFrame->m_hOleMenu, pFrame->m_hWnd);
  }

  // The bars, made once, on the container's frames.
  HWND hWnd = NULL;
  if (pFrame->m_pMainFrame == NULL && pFrame->m_lpFrame != NULL &&
      SUCCEEDED(pFrame->m_lpFrame->GetWindow(&hWnd)) && hWnd != NULL)
    pFrame->m_pMainFrame = new COleCntrFrameWnd(pFrame, hWnd, pFrame->m_lpFrame);
  hWnd = NULL;
  if (pFrame->m_pDocFrame == NULL && pFrame->m_lpDocFrame != NULL &&
      SUCCEEDED(pFrame->m_lpDocFrame->GetWindow(&hWnd)) && hWnd != NULL)
    pFrame->m_pDocFrame = new COleCntrFrameWnd(pFrame, hWnd, pFrame->m_lpDocFrame);
  if (!pFrame->m_bBarsCreated) {
    pFrame->m_bBarsCreated = TRUE;
    if (pFrame->m_pMainFrame != NULL)
      pFrame->OnCreateControlBars(pFrame->m_pMainFrame, pFrame->m_pDocFrame);
  } else {
    if (pFrame->m_pMainFrame != NULL) OnShowControlBars(pFrame->m_pMainFrame, TRUE);
    if (pFrame->m_pDocFrame != NULL) OnShowControlBars(pFrame->m_pDocFrame, TRUE);
  }
  if (pFrame->m_pMainFrame != NULL) pFrame->m_pMainFrame->RecalcLayout();
  if (pFrame->m_pDocFrame != NULL) pFrame->m_pDocFrame->RecalcLayout();

  pFrame->SetFocus();
  return TRUE;
}

COleIPFrameWnd* COleServerDoc::CreateInPlaceFrame(CWnd* pParentWnd) {
  CDocTemplate* pTemplate = GetDocTemplate();
  if (pTemplate == NULL) return NULL;
  // The document's view moves into the in-place frame, unless the
  // template has a view class of its own for in-place editing.
  CView* pView = NULL;
  POSITION pos = GetFirstViewPosition();
  if (pos != NULL) pView = GetNextView(pos);
  BOOL bCreateView = pView == NULL || pTemplate->m_pOleViewClass != NULL;
  CFrameWnd* pNewFrame = pTemplate->CreateOleFrame(pParentWnd, this, bCreateView);
  if (pNewFrame == NULL) return NULL;
  if (!pNewFrame->IsKindOf(RUNTIME_CLASS(COleIPFrameWnd))) {
    pNewFrame->DestroyWindow();
    return NULL;
  }
  COleIPFrameWnd* pFrame = (COleIPFrameWnd*)pNewFrame;
  m_pOrigParent = NULL;
  if (!bCreateView) {
    m_pOrigParent = pView->GetParent();
    m_dwOrigStyle = pView->GetStyle();
    m_dwOrigStyleEx = pView->GetExStyle();
    CFrameWnd* pOrigFrame = pView->GetParentFrame();
    if (pOrigFrame != NULL && pOrigFrame->GetActiveView() == pView)
      pOrigFrame->SetActiveView(NULL, FALSE);
    pView->SetParent(pFrame);
    pView->SetDlgCtrlID(AFX_IDW_PANE_FIRST);
    pView->ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
    pFrame->SetActiveView(pView, FALSE);
  } else {
    CWnd* pWnd = pFrame->GetDescendantWindow(AFX_IDW_PANE_FIRST, TRUE);
    if (pWnd != NULL) {
      pWnd->ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
      if (pWnd->IsKindOf(RUNTIME_CLASS(CView))) pFrame->SetActiveView((CView*)pWnd, FALSE);
    }
  }
  pFrame->RecalcLayout();
  return pFrame;
}

void COleServerDoc::DestroyInPlaceFrame(COleIPFrameWnd* pFrameWnd) {
  if (m_pOrigParent != NULL) {
    // The view goes back where it was.
    CView* pView = pFrameWnd->GetActiveView();
    if (pView == NULL) {
      POSITION pos = GetFirstViewPosition();
      while (pos != NULL && pView == NULL) {
        CView* p = GetNextView(pos);
        if (p->GetParent() == pFrameWnd) pView = p;
      }
    }
    if (pView != NULL) {
      pFrameWnd->SetActiveView(NULL, FALSE);
      pView->SetParent(m_pOrigParent);
      ::SetWindowLong(pView->m_hWnd, GWL_STYLE, (LONG)m_dwOrigStyle);
      ::SetWindowLong(pView->m_hWnd, GWL_EXSTYLE, (LONG)m_dwOrigStyleEx);
      pView->SetWindowPos(NULL, 0, 0, 0, 0,
                          SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                              SWP_NOACTIVATE);
      CFrameWnd* pOrigFrame = pView->GetParentFrame();
      if (pOrigFrame != NULL) {
        pOrigFrame->SetActiveView(pView, FALSE);
        pOrigFrame->RecalcLayout();
      }
    }
    m_pOrigParent = NULL;
  }
  pFrameWnd->DestroyWindow();
}

void COleServerDoc::OnDeactivateUI(BOOL bUndoable) {
  COleIPFrameWnd* pFrame = m_pInPlaceFrame;
  if (pFrame == NULL || !pFrame->m_bUIActive) return;
  pFrame->m_bUIActive = FALSE;
  if (pFrame->m_lpFrame != NULL) pFrame->m_lpFrame->SetMenu(NULL, NULL, NULL);
  pFrame->DestroySharedMenu();
  if (pFrame->m_pMainFrame != NULL) OnShowControlBars(pFrame->m_pMainFrame, FALSE);
  if (pFrame->m_pDocFrame != NULL) OnShowControlBars(pFrame->m_pDocFrame, FALSE);
  if (pFrame->m_lpFrame != NULL) {
    pFrame->m_lpFrame->SetBorderSpace(NULL);
    pFrame->m_lpFrame->SetActiveObject(NULL, NULL);
  }
  if (pFrame->m_lpDocFrame != NULL) {
    pFrame->m_lpDocFrame->SetBorderSpace(NULL);
    pFrame->m_lpDocFrame->SetActiveObject(NULL, NULL);
  }
  if (m_lpInPlaceSite != NULL) m_lpInPlaceSite->OnUIDeactivate(bUndoable);
}

void COleServerDoc::OnDeactivate() {
  COleIPFrameWnd* pFrame = m_pInPlaceFrame;
  if (pFrame == NULL) return;
  InternalAddRef();  // the container may let go of the document on the way
  if (pFrame->m_bUIActive) OnDeactivateUI(FALSE);
  LPOLEINPLACESITE lpSite = m_lpInPlaceSite;
  m_lpInPlaceSite = NULL;
  m_pInPlaceFrame = NULL;
  DestroyInPlaceFrame(pFrame);
  if (lpSite != NULL) {
    lpSite->OnInPlaceDeactivate();
    lpSite->Release();
  }
  InternalRelease();
}

void COleServerDoc::OnSetItemRects(LPCRECT lpPosRect, LPCRECT lpClipRect) {
  if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->RepositionFrame(lpPosRect, lpClipRect);
}

BOOL COleServerDoc::OnReactivateAndUndo() { return FALSE; }

void COleServerDoc::OnFrameWindowActivate(BOOL) {}

// An MDI container's document windows: the object's menus and bars are
// up only while its window is the active one.
void COleServerDoc::OnDocWindowActivate(BOOL bActivate) {
  COleIPFrameWnd* pFrame = m_pInPlaceFrame;
  if (pFrame == NULL || !pFrame->m_bUIActive || pFrame->m_lpFrame == NULL) return;
  if (bActivate) {
    CStringW strName(GetTitle());
    pFrame->m_lpFrame->SetActiveObject((LPOLEINPLACEACTIVEOBJECT)&m_xOleInPlaceActiveObject,
                                       strName);
    pFrame->m_lpFrame->SetMenu(pFrame->m_hSharedMenu, pFrame->m_hOleMenu, pFrame->m_hWnd);
    if (pFrame->m_pMainFrame != NULL) {
      OnShowControlBars(pFrame->m_pMainFrame, TRUE);
      pFrame->m_pMainFrame->RecalcLayout();
    }
  } else {
    pFrame->m_lpFrame->SetMenu(NULL, NULL, NULL);
    if (pFrame->m_pMainFrame != NULL) OnShowControlBars(pFrame->m_pMainFrame, FALSE);
    pFrame->m_lpFrame->SetBorderSpace(NULL);
    pFrame->m_lpFrame->SetActiveObject(NULL, NULL);
  }
}

void COleServerDoc::OnShowControlBars(CFrameWnd* pFrameWnd, BOOL bShow) {
  if (pFrameWnd == NULL) return;
  if (!pFrameWnd->IsKindOf(RUNTIME_CLASS(COleCntrFrameWnd))) {
    POSITION pos = pFrameWnd->m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)pFrameWnd->m_listControlBars.GetNext(pos);
      if (pBar->m_hWnd != NULL && !pBar->IsDockBar())
        pFrameWnd->ShowControlBar(pBar, bShow, TRUE);
    }
    return;
  }
  // On the container's window: hide them all, dock bars too, since that
  // frame does no layout while the object is not UI active.
  COleCntrFrameWnd* pCntr = (COleCntrFrameWnd*)pFrameWnd;
  if (!bShow) {
    POSITION pos = pCntr->m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)pCntr->m_listControlBars.GetNext(pos);
      if (pBar->m_hWnd == NULL || !(pBar->GetStyle() & WS_VISIBLE)) continue;
      pBar->ShowWindow(SW_HIDE);
      pCntr->m_listHiddenBars.AddTail(pBar);
    }
  } else {
    while (!pCntr->m_listHiddenBars.IsEmpty()) {
      CControlBar* pBar = (CControlBar*)pCntr->m_listHiddenBars.RemoveHead();
      if (pBar->m_hWnd != NULL) pBar->ShowWindow(SW_SHOWNA);
    }
  }
}

void COleServerDoc::OnResizeBorder(LPCRECT, LPOLEINPLACEUIWINDOW, BOOL bFrame) {
  if (m_pInPlaceFrame == NULL) return;
  COleCntrFrameWnd* pFrame = bFrame ? m_pInPlaceFrame->m_pMainFrame : m_pInPlaceFrame->m_pDocFrame;
  if (pFrame != NULL) pFrame->RecalcLayout();
}

void COleServerDoc::RequestPositionChange(LPCRECT lpPosRect) {
  if (m_lpInPlaceSite != NULL) m_lpInPlaceSite->OnPosRectChange(lpPosRect);
}

BOOL COleServerDoc::ScrollContainerBy(CSize sizeScroll) {
  return m_lpInPlaceSite != NULL && m_lpInPlaceSite->Scroll(sizeScroll) == S_OK;
}

BOOL COleServerDoc::DeactivateAndUndo() {
  return m_lpInPlaceSite != NULL && m_lpInPlaceSite->DeactivateAndUndo() == S_OK;
}

BOOL COleServerDoc::DiscardUndoState() {
  return m_lpInPlaceSite != NULL && m_lpInPlaceSite->DiscardUndoState() == S_OK;
}

void COleServerDoc::GetItemPosition(LPRECT lpPosRect) const {
  if (m_pInPlaceFrame != NULL)
    *lpPosRect = m_pInPlaceFrame->m_rectPos;
  else
    ::SetRectEmpty(lpPosRect);
}

void COleServerDoc::GetItemClipRect(LPRECT lpClipRect) const {
  if (m_pInPlaceFrame != NULL)
    *lpClipRect = m_pInPlaceFrame->m_rectClip;
  else
    ::SetRectEmpty(lpClipRect);
}

BOOL COleServerDoc::GetZoomFactor(LPSIZE lpSizeNum, LPSIZE lpSizeDenom,
                                  LPCRECT lpPosRect) const {
  CSize sizeNum(1, 1), sizeDenom(1, 1);
  CRect rectPos;
  if (lpPosRect != NULL)
    rectPos = *lpPosRect;
  else
    GetItemPosition(&rectPos);
  COleServerItem* pItem = m_pEmbeddedItem;
  CSize sizeExtent;
  if (!rectPos.IsRectEmpty() && pItem != NULL &&
      pItem->OnGetExtent(DVASPECT_CONTENT, sizeExtent)) {
    CClientDC dc(NULL);
    dc.HIMETRICtoDP(&sizeExtent);
    if (sizeExtent.cx != 0 && sizeExtent.cy != 0) {
      sizeNum = rectPos.Size();
      sizeDenom = sizeExtent;
    }
  }
  if (lpSizeNum != NULL) *lpSizeNum = sizeNum;
  if (lpSizeDenom != NULL) *lpSizeDenom = sizeDenom;
  return sizeNum != sizeDenom;
}

// ---------------------------------------------------------------------
// IOleInPlaceObject

STDMETHODIMP_(ULONG) COleServerDoc::XOleInPlaceObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerDoc::XOleInPlaceObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceObject)
  if (phwnd == NULL) return E_POINTER;
  *phwnd = pThis->m_pInPlaceFrame != NULL ? pThis->m_pInPlaceFrame->m_hWnd : NULL;
  return *phwnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::ContextSensitiveHelp(BOOL) { return S_OK; }

STDMETHODIMP COleServerDoc::XOleInPlaceObject::InPlaceDeactivate() {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceObject)
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnDeactivate();
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::UIDeactivate() {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceObject)
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnDeactivateUI(FALSE);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::SetObjectRects(LPCRECT lprcPosRect,
                                                              LPCRECT lprcClipRect) {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceObject)
  if (lprcPosRect == NULL || lprcClipRect == NULL) return E_INVALIDARG;
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnSetItemRects(lprcPosRect, lprcClipRect);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceObject::ReactivateAndUndo() {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceObject)
  HRESULT hr = INPLACE_E_NOTUNDOABLE;
  OLE_TRY
    if (pThis->OnReactivateAndUndo()) hr = S_OK;
  OLE_CATCH(hr)
  return hr;
}

// ---------------------------------------------------------------------
// IOleInPlaceActiveObject

STDMETHODIMP_(ULONG) COleServerDoc::XOleInPlaceActiveObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceActiveObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerDoc::XOleInPlaceActiveObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceActiveObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::QueryInterface(REFIID iid,
                                                                    LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceActiveObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceActiveObject)
  if (phwnd == NULL) return E_POINTER;
  *phwnd = pThis->m_pInPlaceFrame != NULL ? pThis->m_pInPlaceFrame->m_hWnd : NULL;
  return *phwnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::ContextSensitiveHelp(BOOL) { return S_OK; }

// For a container in the same program: its frame has the keys first.
STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::TranslateAccelerator(LPMSG lpmsg) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleInPlaceActiveObject)
  COleIPFrameWnd* pFrame = pThis->m_pInPlaceFrame;
  if (pFrame == NULL || lpmsg == NULL) return S_FALSE;
  HACCEL hAccel = pFrame->GetDefaultAccelerator();
  return hAccel != NULL && ::TranslateAccelerator(pFrame->m_hWnd, hAccel, lpmsg) ? S_OK : S_FALSE;
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::OnFrameWindowActivate(BOOL fActivate) {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceActiveObject)
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnFrameWindowActivate(fActivate);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::OnDocWindowActivate(BOOL fActivate) {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceActiveObject)
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnDocWindowActivate(fActivate);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::ResizeBorder(LPCRECT prcBorder,
                                                                  LPOLEINPLACEUIWINDOW pUIWindow,
                                                                  BOOL fFrameWindow) {
  METHOD_PROLOGUE_EX(COleServerDoc, OleInPlaceActiveObject)
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnResizeBorder(prcBorder, pUIWindow, fFrameWindow);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleInPlaceActiveObject::EnableModeless(BOOL) { return S_OK; }
