// bars.cpp - control bars: CControlBar, CToolBar, CStatusBar, CDialogBar,
// and the frame window's bar management.
//
// Bars lay themselves out when the frame sends WM_SIZEPARENT (from
// RepositionBars). Borders and the gripper live in the non-client area,
// so the toolbar and status bar controls only see their own content.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>
#include <afxpriv.h>

IMPLEMENT_DYNAMIC(CControlBar, CWnd)
IMPLEMENT_DYNAMIC(CStatusBar, CControlBar)
IMPLEMENT_DYNAMIC(CToolBar, CControlBar)
IMPLEMENT_DYNAMIC(CDialogBar, CControlBar)

// Sizes in pixels.
static const int CX_EDGE = 2;           // one etched border side
static const int CX_GRIPPER = 3;        // the raised gripper bar
static const int CX_GRIPPER_BORDER = 2; // space either side of the gripper
static const int CX_GRIPPER_ALL = CX_GRIPPER_BORDER + CX_GRIPPER + CX_GRIPPER_BORDER + 1;
static const UINT ID_TIMER_FLYBY = 0xE001;

// ---------------------------------------------------------------------
// CControlBar

BEGIN_MESSAGE_MAP(CControlBar, CWnd)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_NCCALCSIZE()
  ON_WM_NCPAINT()
  ON_WM_TIMER()
  ON_WM_MOUSEMOVE()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_NCHITTEST()
  ON_MESSAGE(WM_SIZEPARENT, OnSizeParent)
  ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
END_MESSAGE_MAP()

CControlBar::CControlBar() {
  m_bAutoDelete = FALSE;
  m_cxLeftBorder = m_cxRightBorder = 6;
  m_cyTopBorder = m_cyBottomBorder = 1;
  m_cxDefaultGap = 2;
  m_nMRUWidth = 32767;
  m_nCount = 0;
  m_pData = NULL;
  m_nStateFlags = 0;
  m_dwStyle = 0;
  m_dwDockStyle = 0;
  m_pDockSite = NULL;
  m_pDockBar = NULL;
  m_pDockContext = NULL;
  m_nHitLast = -1;
}

CControlBar::~CControlBar() {
  DestroyWindow();
  delete m_pDockContext;
  if (m_pData != NULL) ::free(m_pData);
}

BOOL CControlBar::AllocElements(int nElements, int cbElement) {
  if (m_pData != NULL) {
    ::free(m_pData);
    m_pData = NULL;
  }
  if (nElements > 0) {
    m_pData = ::calloc(nElements, cbElement);
    if (m_pData == NULL) return FALSE;
  }
  m_nCount = nElements;
  return TRUE;
}

void CControlBar::SetBorders(int cxLeft, int cyTop, int cxRight, int cyBottom) {
  m_cxLeftBorder = cxLeft;
  m_cyTopBorder = cyTop;
  m_cxRightBorder = cxRight;
  m_cyBottomBorder = cyBottom;
}

void CControlBar::SetBarStyle(DWORD dwStyle) {
  if (m_dwStyle == dwStyle) return;
  DWORD dwOldStyle = m_dwStyle;
  m_dwStyle = dwStyle;
  OnBarStyleChange(dwOldStyle, dwStyle);
  if (m_hWnd != NULL)
    SetWindowPos(NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CControlBar::OnBarStyleChange(DWORD dwOldStyle, DWORD dwNewStyle) {
  (void)dwOldStyle;
  (void)dwNewStyle;
}

void CControlBar::EnableDocking(DWORD dwDockStyle) {
  m_dwDockStyle = dwDockStyle;
  if (m_pDockContext == NULL && dwDockStyle != 0) m_pDockContext = new CDockContext(this);
  // Commands and updates keep going to the frame wherever the bar moves.
  if (m_hWndOwner == NULL && m_hWnd != NULL) m_hWndOwner = ::GetParent(m_hWnd);
}

BOOL CControlBar::IsFloating() const { return m_pDockBar != NULL && m_pDockBar->m_bFloating; }

CFrameWnd* CControlBar::GetDockingFrame() const {
  if (m_pDockSite != NULL) return m_pDockSite;
  return GetParentFrame();
}

BOOL CControlBar::IsVisible() const { return (GetStyle() & WS_VISIBLE) != 0; }

void CControlBar::DelayShow(BOOL bShow) {
  if (m_hWnd == NULL) return;
  ShowWindow(bShow ? SW_SHOWNA : SW_HIDE);
  // A floating bar's frame shows and hides with it.
  if (IsFloating()) {
    CWnd* pMiniFrame = m_pDockBar->GetParent();
    if (pMiniFrame != NULL && pMiniFrame->IsFrameWnd()) ((CFrameWnd*)pMiniFrame)->RecalcLayout();
  }
  CFrameWnd* pFrame = GetDockingFrame();
  if (pFrame != NULL) pFrame->DelayRecalcLayout();
}

BOOL CControlBar::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) {
    if (!AfxDeferRegisterClass(AFX_WNDCONTROLBAR_REG)) return FALSE;
    cs.lpszClass = _afxWndControlBar;
  }
  return CWnd::PreCreateWindow(cs);
}

BOOL CControlBar::PreTranslateMessage(MSG* pMsg) { return CWnd::PreTranslateMessage(pMsg); }

void CControlBar::PostNcDestroy() {
  if (m_bAutoDelete) delete this;
}

LRESULT CControlBar::WindowProc(UINT nMsg, WPARAM wParam, LPARAM lParam) {
  switch (nMsg) {
    case WM_NOTIFY:
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_DELETEITEM:
    case WM_COMPAREITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM: {
      // Notifications from the bar's children go to the bar's owner
      // (normally the frame) when the bar does not handle them.
      LRESULT lResult = 0;
      if (OnWndMsg(nMsg, wParam, lParam, &lResult)) return lResult;
      CWnd* pOwner = GetOwner();
      if (pOwner != NULL) return pOwner->SendMessage(nMsg, wParam, lParam);
      return DefWindowProc(nMsg, wParam, lParam);
    }
  }
  return CWnd::WindowProc(nMsg, wParam, lParam);
}

int CControlBar::OnCreate(LPCREATESTRUCT lpcs) {
  if (CWnd::OnCreate(lpcs) == -1) return -1;
  CWnd* pParent = GetParent();
  if (pParent != NULL && pParent->IsFrameWnd()) {
    m_pDockSite = (CFrameWnd*)pParent;
    m_pDockSite->AddControlBar(this);
  }
  return 0;
}

void CControlBar::OnDestroy() {
  if (m_pDockBar != NULL && !IsDockBar()) m_pDockBar->RemoveControlBar(this);
  m_pDockBar = NULL;
  if (m_pDockSite != NULL) {
    m_pDockSite->RemoveControlBar(this);
    m_pDockSite = NULL;
  }
  CWnd::OnDestroy();
}

// The non-client borders: the etched edges and the gripper.
void CControlBar::CalcNcBorders(CRect& rect, BOOL bHorz) const {
  if (m_dwStyle & CBRS_BORDER_LEFT) rect.left += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_TOP) rect.top += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_RIGHT) rect.right -= CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_BOTTOM) rect.bottom -= CX_EDGE;
  if ((m_dwStyle & (CBRS_GRIPPER | CBRS_FLOATING)) == CBRS_GRIPPER) {
    if (bHorz)
      rect.left += CX_GRIPPER_ALL;
    else
      rect.top += CX_GRIPPER_ALL;
  }
}

void CControlBar::CalcInsideRect(CRect& rect, BOOL bHorz) const {
  CalcNcBorders(rect, bHorz);
  rect.left += m_cxLeftBorder;
  rect.top += m_cyTopBorder;
  rect.right -= m_cxRightBorder;
  rect.bottom -= m_cyBottomBorder;
}

CSize CControlBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  CSize size(0, 0);
  if (bStretch) {
    if (bHorz)
      size.cx = 32767;
    else
      size.cy = 32767;
  }
  CRect rect(0, 0, 0, 0);
  CalcNcBorders(rect, bHorz);
  size.cx -= rect.Width();
  size.cy -= rect.Height();
  return size;
}

CSize CControlBar::CalcDynamicLayout(int nLength, DWORD dwMode) {
  (void)nLength;
  BOOL bHorz = (dwMode & (LM_HORZDOCK | LM_VERTDOCK)) ? (dwMode & LM_HORZDOCK) != 0
                                                      : (dwMode & LM_HORZ) != 0;
  return CalcFixedLayout((dwMode & LM_STRETCH) != 0, bHorz);
}

LRESULT CControlBar::OnSizeParent(WPARAM, LPARAM lParam) {
  AFX_SIZEPARENTPARAMS* lpLayout = (AFX_SIZEPARENTPARAMS*)lParam;
  DWORD dwStyle = m_dwStyle;
  if (!(GetStyle() & WS_VISIBLE) || (dwStyle & CBRS_ALIGN_ANY) == 0) return 0;

  CRect rect(lpLayout->rect);
  CSize sizeAvail = rect.Size();
  DWORD dwMode = lpLayout->bStretch ? LM_STRETCH : 0;
  if (dwStyle & CBRS_ORIENT_HORZ)
    dwMode |= LM_HORZ | LM_HORZDOCK;
  else
    dwMode |= LM_VERTDOCK;
  CSize size = CalcDynamicLayout(-1, dwMode);
  size.cx = min(size.cx, sizeAvail.cx);
  size.cy = min(size.cy, sizeAvail.cy);
  if (size.cx < 0) size.cx = 0;
  if (size.cy < 0) size.cy = 0;

  if (dwStyle & CBRS_ORIENT_HORZ) {
    lpLayout->sizeTotal.cy += size.cy;
    lpLayout->sizeTotal.cx = max(lpLayout->sizeTotal.cx, size.cx);
    if (dwStyle & CBRS_ALIGN_TOP) {
      lpLayout->rect.top += size.cy;
    } else {
      rect.top = rect.bottom - size.cy;
      lpLayout->rect.bottom -= size.cy;
    }
  } else {
    lpLayout->sizeTotal.cx += size.cx;
    lpLayout->sizeTotal.cy = max(lpLayout->sizeTotal.cy, size.cy);
    if (dwStyle & CBRS_ALIGN_LEFT) {
      lpLayout->rect.left += size.cx;
    } else {
      rect.left = rect.right - size.cx;
      lpLayout->rect.right -= size.cx;
    }
  }
  rect.right = rect.left + size.cx;
  rect.bottom = rect.top + size.cy;
  if (lpLayout->hDWP != NULL) AfxRepositionWindow(lpLayout, m_hWnd, &rect);
  return 0;
}

LRESULT CControlBar::OnIdleUpdateCmdUI(WPARAM wParam, LPARAM) {
  if (GetStyle() & WS_VISIBLE) {
    CFrameWnd* pTarget = (CFrameWnd*)GetOwner();
    if (pTarget == NULL || !pTarget->IsFrameWnd()) pTarget = GetParentFrame();
    if (pTarget != NULL) OnUpdateCmdUI(pTarget, (BOOL)wParam);
  }
  return 0;
}

void CControlBar::OnNcCalcSize(BOOL, NCCALCSIZE_PARAMS* lpncsp) {
  CRect rect(lpncsp->rgrc[0]);
  CalcNcBorders(rect, IsHorzBar());
  if (rect.right < rect.left) rect.right = rect.left;
  if (rect.bottom < rect.top) rect.bottom = rect.top;
  lpncsp->rgrc[0] = rect;
}

void CControlBar::OnNcPaint() {
  CRect rectWindow;
  GetWindowRect(&rectWindow);
  CRect rectClient;
  GetClientRect(&rectClient);
  ClientToScreen(&rectClient);
  rectClient.OffsetRect(-rectWindow.left, -rectWindow.top);
  rectWindow.OffsetRect(-rectWindow.left, -rectWindow.top);
  if (rectClient == rectWindow) return;

  CWindowDC dc(this);
  dc.ExcludeClipRect(&rectClient);
  dc.FillSolidRect(&rectWindow, ::GetSysColor(COLOR_BTNFACE));
  CRect rect = rectWindow;
  DrawBorders(&dc, rect);
  DrawGripper(&dc, rectWindow);
}

void CControlBar::DrawBorders(CDC* pDC, CRect& rect) {
  // Each border is an etched line: shadow then highlight.
  COLORREF clrShadow = ::GetSysColor(COLOR_BTNSHADOW);
  COLORREF clrHilite = ::GetSysColor(COLOR_BTNHIGHLIGHT);
  if (m_dwStyle & CBRS_BORDER_TOP) {
    pDC->FillSolidRect(rect.left, rect.top, rect.Width(), 1, clrShadow);
    pDC->FillSolidRect(rect.left, rect.top + 1, rect.Width(), 1, clrHilite);
  }
  if (m_dwStyle & CBRS_BORDER_BOTTOM) {
    pDC->FillSolidRect(rect.left, rect.bottom - 2, rect.Width(), 1, clrShadow);
    pDC->FillSolidRect(rect.left, rect.bottom - 1, rect.Width(), 1, clrHilite);
  }
  if (m_dwStyle & CBRS_BORDER_LEFT) {
    pDC->FillSolidRect(rect.left, rect.top, 1, rect.Height(), clrShadow);
    pDC->FillSolidRect(rect.left + 1, rect.top, 1, rect.Height(), clrHilite);
  }
  if (m_dwStyle & CBRS_BORDER_RIGHT) {
    pDC->FillSolidRect(rect.right - 2, rect.top, 1, rect.Height(), clrShadow);
    pDC->FillSolidRect(rect.right - 1, rect.top, 1, rect.Height(), clrHilite);
  }
  if (m_dwStyle & CBRS_BORDER_LEFT) rect.left += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_TOP) rect.top += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_RIGHT) rect.right -= CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_BOTTOM) rect.bottom -= CX_EDGE;
}

void CControlBar::DrawGripper(CDC* pDC, const CRect& rectWindow) {
  if ((m_dwStyle & (CBRS_GRIPPER | CBRS_FLOATING)) != CBRS_GRIPPER) return;
  CRect rect = rectWindow;
  if (m_dwStyle & CBRS_BORDER_LEFT) rect.left += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_TOP) rect.top += CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_RIGHT) rect.right -= CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_BOTTOM) rect.bottom -= CX_EDGE;
  COLORREF clrHilite = ::GetSysColor(COLOR_BTNHIGHLIGHT);
  COLORREF clrShadow = ::GetSysColor(COLOR_BTNSHADOW);
  if (IsHorzBar())
    pDC->Draw3dRect(rect.left + CX_GRIPPER_BORDER, rect.top + 2, CX_GRIPPER, rect.Height() - 4,
                    clrHilite, clrShadow);
  else
    pDC->Draw3dRect(rect.left + 2, rect.top + CX_GRIPPER_BORDER, rect.Width() - 4, CX_GRIPPER,
                    clrHilite, clrShadow);
}

void CControlBar::OnPaint() {
  CPaintDC dc(this);
  DoPaint(&dc);
}

void CControlBar::DoPaint(CDC* pDC) { (void)pDC; }

BOOL CControlBar::OnEraseBkgnd(CDC* pDC) {
  CRect rect;
  GetClientRect(&rect);
  pDC->FillSolidRect(&rect, ::GetSysColor(COLOR_BTNFACE));
  return TRUE;
}

// Fly-by help: as the mouse moves over the bar, the frame's status bar
// shows the prompt for the item under it.
BOOL CControlBar::SetStatusText(INT_PTR nHit) {
  CWnd* pOwner = GetOwner();
  if (pOwner == NULL) return FALSE;
  if (nHit == -1) {
    if (m_nStateFlags & statusSet) {
      pOwner->SendMessage(WM_POPMESSAGESTRING, AFX_IDS_IDLEMESSAGE);
      m_nStateFlags &= ~statusSet;
    }
  } else {
    pOwner->SendMessage(WM_SETMESSAGESTRING, (WPARAM)nHit);
    m_nStateFlags |= statusSet;
  }
  return TRUE;
}

void CControlBar::ResetTimer(UINT nEvent, UINT nTime) {
  KillTimer(ID_TIMER_FLYBY);
  SetTimer(nEvent, nTime, NULL);
}

void CControlBar::OnMouseMove(UINT nFlags, CPoint point) {
  if ((m_dwStyle & CBRS_FLYBY) && !(GetParentFrame() && GetParentFrame()->InModalState())) {
    INT_PTR nHit = OnToolHitTest(point, NULL);
    if (nHit != m_nHitLast) {
      m_nHitLast = nHit;
      SetStatusText(nHit);
      if (nHit != -1) ResetTimer(ID_TIMER_FLYBY, 100);
    }
  }
  (void)nFlags;
  Default();
}

// A press on the bar's background (not on a button or control) drags
// the bar; a double click docks or floats it.
void CControlBar::OnLButtonDown(UINT nFlags, CPoint point) {
  if (m_pDockContext != NULL && OnToolHitTest(point, NULL) == -1) {
    ClientToScreen(&point);
    m_pDockContext->StartDrag(point);
    return;
  }
  (void)nFlags;
  Default();
}

void CControlBar::OnLButtonDblClk(UINT nFlags, CPoint point) {
  if (m_pDockContext != NULL && OnToolHitTest(point, NULL) == -1) {
    m_pDockContext->ToggleDocking();
    return;
  }
  (void)nFlags;
  Default();
}

// The gripper and borders are drawn in the non-client area, but they are
// part of what the mouse can grab.
LRESULT CControlBar::OnNcHitTest(CPoint point) {
  if (m_pDockContext != NULL) {
    CRect rect;
    GetWindowRect(&rect);
    if (rect.PtInRect(point)) return HTCLIENT;
  }
  return Default();
}

void CControlBar::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent != ID_TIMER_FLYBY) {
    Default();
    return;
  }
  CPoint pt;
  ::GetCursorPos(&pt);
  if (::WindowFromPoint(pt) != m_hWnd) {
    KillTimer(ID_TIMER_FLYBY);
    m_nHitLast = -1;
    SetStatusText(-1);
  }
}

// ---------------------------------------------------------------------
// CToolBar

BEGIN_MESSAGE_MAP(CToolBar, CControlBar)
  ON_WM_NCCALCSIZE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CToolBar::CToolBar() {
  m_hRsrcImageWell = NULL;
  m_hInstImageWell = NULL;
  m_hbmImageWell = NULL;
  m_bDelayedButtonLayout = TRUE;
  m_sizeImage = CSize(16, 15);
  m_sizeButton = CSize(23, 22);
  m_pStringMap = NULL;
  m_cxLeftBorder = m_cxRightBorder = 1;
  m_cyTopBorder = m_cyBottomBorder = 1;
}

CToolBar::~CToolBar() {
  DestroyWindow();
  if (m_hbmImageWell != NULL) ::DeleteObject(m_hbmImageWell);
  delete m_pStringMap;
}

BOOL CToolBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  return CreateEx(pParentWnd, 0, dwStyle,
                  CRect(m_cxLeftBorder, m_cyTopBorder, m_cxRightBorder, m_cyBottomBorder), nID);
}

BOOL CToolBar::CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle, DWORD dwStyle, CRect rcBorders,
                        UINT nID) {
  SetBorders(rcBorders);
  m_dwStyle = dwStyle & CBRS_ALL;
  if (m_dwStyle & CBRS_TOOLTIPS) dwCtrlStyle |= TBSTYLE_TOOLTIPS;
  DWORD dwWndStyle = (dwStyle & ~CBRS_ALL) | dwCtrlStyle | CCS_NOPARENTALIGN | CCS_NOMOVEY |
                     CCS_NODIVIDER | CCS_NORESIZE;
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_BAR_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  if (!CWnd::Create(TOOLBARCLASSNAME, NULL, dwWndStyle, CRect(0, 0, 0, 0), pParentWnd, nID))
    return FALSE;
  DefWindowProc(TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
  SetSizes(m_sizeButton, m_sizeImage);
  return TRUE;
}

void CToolBar::SetSizes(SIZE sizeButton, SIZE sizeImage) {
  m_sizeButton = sizeButton;
  m_sizeImage = sizeImage;
  if (m_hWnd == NULL) return;
  DefWindowProc(TB_SETBITMAPSIZE, 0, MAKELONG(sizeImage.cx, sizeImage.cy));
  DefWindowProc(TB_SETBUTTONSIZE, 0, MAKELONG(sizeButton.cx, sizeButton.cy));
  Invalidate();
}

void CToolBar::SetHeight(int cyHeight) {
  int nHeight = cyHeight;
  if (m_dwStyle & CBRS_BORDER_TOP) cyHeight -= CX_EDGE;
  if (m_dwStyle & CBRS_BORDER_BOTTOM) cyHeight -= CX_EDGE;
  m_cyBottomBorder = cyHeight - m_cyTopBorder - m_sizeButton.cy;
  if (m_cyBottomBorder < 0) m_cyBottomBorder = 0;
  (void)nHeight;
  if (m_hWnd != NULL)
    SetWindowPos(NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The RT_TOOLBAR resource that resource editors write.
struct AFX_TOOLBARDATA {
  WORD wVersion;
  WORD wWidth;
  WORD wHeight;
  WORD wItemCount;
  // WORD aItems[wItemCount]
};

BOOL CToolBar::LoadToolBar(LPCTSTR lpszResourceName) {
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_TOOLBAR);
  HRSRC hRsrc = ::FindResource(hInst, lpszResourceName, RT_TOOLBAR);
  if (hRsrc == NULL) return FALSE;
  HGLOBAL hGlobal = ::LoadResource(hInst, hRsrc);
  if (hGlobal == NULL) return FALSE;
  const AFX_TOOLBARDATA* pData = (const AFX_TOOLBARDATA*)::LockResource(hGlobal);
  if (pData == NULL || pData->wVersion != 1) return FALSE;

  const WORD* pItems = (const WORD*)(pData + 1);
  UINT* pIDs = new UINT[pData->wItemCount + 1];
  for (int i = 0; i < pData->wItemCount; i++) pIDs[i] = pItems[i];
  BOOL bResult = SetButtons(pIDs, pData->wItemCount);
  delete[] pIDs;
  if (bResult) {
    CSize sizeImage(pData->wWidth, pData->wHeight);
    CSize sizeButton(pData->wWidth + 7, pData->wHeight + 7);
    SetSizes(sizeButton, sizeImage);
    bResult = LoadBitmap(lpszResourceName);
  }
  return bResult;
}

BOOL CToolBar::LoadBitmap(LPCTSTR lpszResourceName) {
  HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_BITMAP);
  // Map the classic grays in the bitmap to the system's 3D colors.
  HBITMAP hbm = (HBITMAP)::LoadImage(hInst, lpszResourceName, IMAGE_BITMAP, 0, 0,
                                     LR_LOADMAP3DCOLORS);
  if (hbm == NULL) return FALSE;
  m_hInstImageWell = hInst;
  m_hRsrcImageWell = ::FindResource(hInst, lpszResourceName, RT_BITMAP);
  return SetBitmap(hbm);
}

BOOL CToolBar::SetBitmap(HBITMAP hbmImageWell) {
  HBITMAP hbmOld = m_hbmImageWell;
  m_hbmImageWell = hbmImageWell;
  BITMAP bm;
  if (::GetObject(hbmImageWell, sizeof(bm), &bm) == 0) return FALSE;
  int nCount = m_sizeImage.cx > 0 ? bm.bmWidth / m_sizeImage.cx : 0;
  BOOL bResult;
  if (hbmOld == NULL) {
    TBADDBITMAP tbab;
    tbab.hInst = NULL;
    tbab.nID = (UINT_PTR)hbmImageWell;
    bResult = DefWindowProc(TB_ADDBITMAP, nCount, (LPARAM)&tbab) != -1;
  } else {
    TBREPLACEBITMAP tbrb;
    tbrb.hInstOld = NULL;
    tbrb.nIDOld = (UINT_PTR)hbmOld;
    tbrb.hInstNew = NULL;
    tbrb.nIDNew = (UINT_PTR)hbmImageWell;
    tbrb.nButtons = nCount;
    bResult = (BOOL)DefWindowProc(TB_REPLACEBITMAP, 0, (LPARAM)&tbrb);
    ::DeleteObject(hbmOld);
  }
  Invalidate();
  return bResult;
}

BOOL CToolBar::SetButtons(const UINT* lpIDArray, int nIDCount) {
  int nOld = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  while (nOld-- > 0) DefWindowProc(TB_DELETEBUTTON, 0, 0);

  TBBUTTON button;
  memset(&button, 0, sizeof(button));
  button.iString = -1;
  int iImage = 0;
  for (int i = 0; i < nIDCount; i++) {
    button.fsState = TBSTATE_ENABLED;
    button.idCommand = lpIDArray != NULL ? (int)lpIDArray[i] : 0;
    if (button.idCommand == 0) {
      button.fsStyle = TBSTYLE_SEP;
      button.iBitmap = (GetStyle() & TBSTYLE_FLAT) ? 6 : 8;
    } else {
      button.fsStyle = TBSTYLE_BUTTON;
      button.iBitmap = iImage++;
    }
    if (!DefWindowProc(TB_ADDBUTTONS, 1, (LPARAM)&button)) return FALSE;
  }
  m_nCount = nIDCount;
  m_bDelayedButtonLayout = TRUE;
  return TRUE;
}

void CToolBar::_GetButton(int nIndex, TBBUTTON* pButton) const {
  memset(pButton, 0, sizeof(*pButton));
  ((CToolBar*)this)->DefWindowProc(TB_GETBUTTON, nIndex, (LPARAM)pButton);
}

void CToolBar::_SetButton(int nIndex, TBBUTTON* pButton) {
  TBBUTTON old;
  _GetButton(nIndex, &old);
  if (memcmp(&old, pButton, sizeof(old)) == 0) return;
  if (old.fsStyle == pButton->fsStyle && old.idCommand == pButton->idCommand &&
      old.iBitmap == pButton->iBitmap && old.iString == pButton->iString &&
      pButton->idCommand != 0) {
    // Only the state changed.
    DefWindowProc(TB_SETSTATE, pButton->idCommand, MAKELONG(pButton->fsState, 0));
    return;
  }
  SetRedraw(FALSE);
  DefWindowProc(TB_DELETEBUTTON, nIndex, 0);
  DefWindowProc(TB_INSERTBUTTON, nIndex, (LPARAM)pButton);
  SetRedraw(TRUE);
  Invalidate();
}

int CToolBar::CommandToIndex(UINT nIDFind) const {
  return (int)((CToolBar*)this)->DefWindowProc(TB_COMMANDTOINDEX, nIDFind, 0);
}

UINT CToolBar::GetItemID(int nIndex) const {
  TBBUTTON button;
  _GetButton(nIndex, &button);
  return (UINT)button.idCommand;
}

void CToolBar::GetItemRect(int nIndex, LPRECT lpRect) const {
  if (!((CToolBar*)this)->DefWindowProc(TB_GETITEMRECT, nIndex, (LPARAM)lpRect))
    ::SetRectEmpty(lpRect);
}

UINT CToolBar::GetButtonStyle(int nIndex) const {
  TBBUTTON button;
  _GetButton(nIndex, &button);
  return MAKELONG(button.fsStyle, button.fsState) ^ TBBS_DISABLED;
}

void CToolBar::SetButtonStyle(int nIndex, UINT nStyle) {
  TBBUTTON button;
  _GetButton(nIndex, &button);
  UINT nOld = MAKELONG(button.fsStyle, button.fsState) ^ TBBS_DISABLED;
  if (nOld == nStyle) return;
  nStyle ^= TBBS_DISABLED;
  button.fsStyle = (BYTE)LOWORD(nStyle);
  button.fsState = (BYTE)HIWORD(nStyle);
  _SetButton(nIndex, &button);
  m_bDelayedButtonLayout = TRUE;
}

void CToolBar::GetButtonInfo(int nIndex, UINT& nID, UINT& nStyle, int& iImage) const {
  TBBUTTON button;
  _GetButton(nIndex, &button);
  nID = (UINT)button.idCommand;
  nStyle = MAKELONG(button.fsStyle, button.fsState) ^ TBBS_DISABLED;
  iImage = button.iBitmap;
}

void CToolBar::SetButtonInfo(int nIndex, UINT nID, UINT nStyle, int iImage) {
  TBBUTTON button;
  _GetButton(nIndex, &button);
  nStyle ^= TBBS_DISABLED;
  button.idCommand = (int)nID;
  button.fsStyle = (BYTE)LOWORD(nStyle);
  button.fsState = (BYTE)HIWORD(nStyle);
  button.iBitmap = iImage;
  _SetButton(nIndex, &button);
  m_bDelayedButtonLayout = TRUE;
  CFrameWnd* pFrame = GetDockingFrame();
  if (pFrame != NULL) pFrame->DelayRecalcLayout();
}

BOOL CToolBar::SetButtonText(int nIndex, LPCTSTR lpszText) {
  // The control keeps a list of strings; add the text (double-null
  // terminated) and point the button at it.
  CString str(lpszText);
  int nLen = str.GetLength();
  LPTSTR p = str.GetBuffer(nLen + 1);
  p[nLen + 1] = 0;
  int iString = (int)DefWindowProc(TB_ADDSTRING, 0, (LPARAM)p);
  str.ReleaseBuffer(nLen);
  if (iString == -1) return FALSE;
  TBBUTTON button;
  _GetButton(nIndex, &button);
  button.iString = iString;
  _SetButton(nIndex, &button);
  m_bDelayedButtonLayout = TRUE;
  return TRUE;
}

void CToolBar::GetButtonText(int nIndex, CString& rString) const {
  rString.Empty();
  TBBUTTON button;
  _GetButton(nIndex, &button);
  if (button.idCommand == 0) return;
  int nLen = (int)((CToolBar*)this)->DefWindowProc(TB_GETBUTTONTEXT, button.idCommand, 0);
  if (nLen <= 0) return;
  ((CToolBar*)this)
      ->DefWindowProc(TB_GETBUTTONTEXT, button.idCommand, (LPARAM)rString.GetBuffer(nLen + 1));
  rString.ReleaseBuffer(nLen);
}

CString CToolBar::GetButtonText(int nIndex) const {
  CString str;
  GetButtonText(nIndex, str);
  return str;
}

// Sets which buttons end a row. The control does not lay itself out
// again when only TBSTATE_WRAP changes, but it does when a button is
// inserted, so the last button changed goes in again once the rest are
// set.
void CToolBar::_ApplyWraps(const BYTE* pWrap) {
  int nCount = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  int nReinsert = -1;
  BOOL bRedraw = FALSE;
  for (int i = 0; i < nCount; i++) {
    TBBUTTON button;
    _GetButton(i, &button);
    BOOL bWrap = (button.fsState & TBSTATE_WRAP) != 0;
    if (bWrap == (pWrap[i] != 0)) continue;
    button.fsState ^= TBSTATE_WRAP;
    if (!bRedraw) {
      SetRedraw(FALSE);
      bRedraw = TRUE;
    }
    if ((button.fsStyle & TBSTYLE_SEP) || button.idCommand == 0) {
      DefWindowProc(TB_DELETEBUTTON, i, 0);
      DefWindowProc(TB_INSERTBUTTON, i, (LPARAM)&button);
      nReinsert = -1;
    } else {
      DefWindowProc(TB_SETSTATE, button.idCommand, MAKELONG(button.fsState, 0));
      nReinsert = i;
    }
  }
  if (nReinsert >= 0) {
    TBBUTTON button;
    _GetButton(nReinsert, &button);
    DefWindowProc(TB_DELETEBUTTON, nReinsert, 0);
    DefWindowProc(TB_INSERTBUTTON, nReinsert, (LPARAM)&button);
  }
  if (bRedraw) {
    SetRedraw(TRUE);
    Invalidate();
  }
}

void CToolBar::_SetWraps(BOOL bAll) {
  int nCount = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  if (nCount == 0) return;
  BYTE* pWrap = new BYTE[nCount];
  memset(pWrap, bAll ? 1 : 0, nCount);
  _ApplyWraps(pWrap);
  delete[] pWrap;
}

// Breaks the buttons into rows no wider than nWidth (the client width),
// breaking at a separator where one falls at the end of a row.
void CToolBar::_WrapToWidth(int nWidth) {
  _SetWraps(FALSE);
  int nCount = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  if (nCount == 0) return;
  int* pWidth = new int[nCount];
  BYTE* pWrap = new BYTE[nCount];
  memset(pWrap, 0, nCount);
  for (int i = 0; i < nCount; i++) {
    CRect rect;
    TBBUTTON button;
    _GetButton(i, &button);
    if ((button.fsState & TBSTATE_HIDDEN) || !DefWindowProc(TB_GETITEMRECT, i, (LPARAM)&rect))
      pWidth[i] = 0;
    else
      pWidth[i] = rect.Width();
  }
  int x = 0;
  for (int i = 0; i < nCount; i++) {
    TBBUTTON button;
    _GetButton(i, &button);
    BOOL bSep = (button.fsStyle & TBSTYLE_SEP) != 0;
    if (x > 0 && x + pWidth[i] > nWidth) {
      // The row is full: end it at this separator, or before this button.
      pWrap[bSep ? i : i - 1] = 1;
      x = bSep ? 0 : pWidth[i];
      continue;
    }
    if (x == 0 && bSep) continue;  // no separator at the start of a row
    x += pWidth[i];
  }
  _ApplyWraps(pWrap);
  delete[] pWrap;
  delete[] pWidth;
}

CSize CToolBar::_CalcButtonsSize() {
  CSize size(0, 0);
  int nCount = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  for (int i = 0; i < nCount; i++) {
    TBBUTTON button;
    _GetButton(i, &button);
    if (button.fsState & TBSTATE_HIDDEN) continue;
    CRect rect;
    if (!DefWindowProc(TB_GETITEMRECT, i, (LPARAM)&rect)) continue;
    size.cx = max(size.cx, (LONG)rect.right);
    size.cy = max(size.cy, (LONG)rect.bottom);
  }
  return size;
}

// Lays the buttons out and returns the size of the whole bar. Docked
// along the top or bottom the buttons are in one row, and docked at a
// side in one column. Otherwise (floating) a bar with CBRS_SIZE_DYNAMIC
// wraps into rows: nLength is the width wanted, or with LM_LENGTHY the
// height; LM_MRUWIDTH uses the width last committed with LM_COMMIT.
CSize CToolBar::CalcLayout(DWORD dwMode, int nLength) {
  BOOL bDock = (dwMode & (LM_HORZDOCK | LM_VERTDOCK)) != 0;
  BOOL bHorz = bDock ? (dwMode & LM_HORZDOCK) != 0 : (dwMode & LM_HORZ) != 0;
  BOOL bWrap = !bDock && ((dwMode & (LM_MRUWIDTH | LM_COMMIT)) || nLength != -1);

  CRect rectInside(0, 0, 0, 0);
  CalcInsideRect(rectInside, TRUE);
  int cxExtra = -rectInside.Width(), cyExtra = -rectInside.Height();

  if (bWrap) {
    bHorz = TRUE;
    int nWidth;
    if (dwMode & LM_MRUWIDTH) {
      nWidth = (int)m_nMRUWidth;
    } else if (dwMode & LM_LENGTHY) {
      // The narrowest layout that fits in the height.
      _WrapToWidth(32767);
      CSize sizeRow = _CalcButtonsSize();
      int cxMin = m_sizeButton.cx;
      nWidth = sizeRow.cx;
      for (int cx = cxMin; cx < sizeRow.cx; cx += max(1, (int)m_sizeButton.cx / 2)) {
        _WrapToWidth(cx);
        if (_CalcButtonsSize().cy + cyExtra <= nLength) {
          nWidth = cx;
          break;
        }
      }
      nWidth += cxExtra;
    } else {
      nWidth = nLength;
    }
    _WrapToWidth(max(nWidth - cxExtra, (int)m_sizeButton.cx));
  } else {
    _SetWraps(!bHorz);
  }

  CSize size = _CalcButtonsSize();
  if (bHorz)
    size.cy = max(size.cy, m_sizeButton.cy);
  else
    size.cx = max(size.cx, m_sizeButton.cx);

  CRect rect(0, 0, 0, 0);
  CalcInsideRect(rect, bHorz);
  size.cx -= rect.Width();
  size.cy -= rect.Height();
  // A docked toolbar is as long as its buttons; only a bar laid out by
  // the frame itself (without dock bars) stretches.
  if ((dwMode & LM_STRETCH) && m_pDockBar == NULL) {
    if (bHorz)
      size.cx = 32767;
    else
      size.cy = 32767;
  }
  if (bWrap && (dwMode & LM_COMMIT)) m_nMRUWidth = (UINT)size.cx;
  m_bDelayedButtonLayout = FALSE;
  return size;
}

void CToolBar::Layout() {
  if (IsFloating() && (m_dwStyle & CBRS_SIZE_DYNAMIC))
    CalcLayout(LM_HORZ | LM_MRUWIDTH);
  else if (IsFloating())
    CalcLayout(IsHorzBar() ? LM_HORZ : 0);
  else
    CalcLayout(IsHorzBar() ? LM_HORZ | LM_HORZDOCK : LM_VERTDOCK);
}

CSize CToolBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  DWORD dwMode = bStretch ? LM_STRETCH : 0;
  dwMode |= bHorz ? LM_HORZ : 0;
  return CalcLayout(dwMode);
}

CSize CToolBar::CalcDynamicLayout(int nLength, DWORD dwMode) {
  if (nLength == -1 && !(dwMode & (LM_MRUWIDTH | LM_COMMIT)) &&
      (dwMode & (LM_HORZDOCK | LM_VERTDOCK)))
    return CalcFixedLayout((dwMode & LM_STRETCH) != 0, (dwMode & LM_HORZDOCK) != 0);
  return CalcLayout(dwMode, nLength);
}

void CToolBar::OnBarStyleChange(DWORD dwOldStyle, DWORD dwNewStyle) {
  if ((dwOldStyle ^ dwNewStyle) & CBRS_ORIENT_ANY) m_bDelayedButtonLayout = TRUE;
}

INT_PTR CToolBar::OnToolHitTest(CPoint point, TOOLINFO* pTI) const {
  int nHit = (int)((CToolBar*)this)->DefWindowProc(TB_HITTEST, 0, (LPARAM)&point);
  int nCount = (int)((CToolBar*)this)->DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  if (nHit < 0 || nHit >= nCount) return -1;
  TBBUTTON button;
  _GetButton(nHit, &button);
  if ((button.fsStyle & TBSTYLE_SEP) || button.idCommand == 0) return -1;
  if (pTI != NULL) {
    pTI->hwnd = m_hWnd;
    pTI->uId = (UINT_PTR)button.idCommand;
    GetItemRect(nHit, &pTI->rect);
    pTI->lpszText = LPSTR_TEXTCALLBACK;
  }
  return button.idCommand;
}

void CToolBar::OnNcCalcSize(BOOL, NCCALCSIZE_PARAMS* lpncsp) {
  // The control's client area is just the buttons: borders, margins and
  // the gripper all go outside it.
  CRect rect(lpncsp->rgrc[0]);
  CalcInsideRect(rect, IsHorzBar());
  if (rect.right < rect.left) rect.right = rect.left;
  if (rect.bottom < rect.top) rect.bottom = rect.top;
  lpncsp->rgrc[0] = rect;
}

void CToolBar::OnPaint() {
  if (m_bDelayedButtonLayout) Layout();
  Default();
}

// A flat toolbar is transparent and asks its parent for the background,
// but the frame leaves erasing to its view, so the bar paints its own.
BOOL CToolBar::OnEraseBkgnd(CDC* pDC) {
  if (GetStyle() & (TBSTYLE_FLAT | TBSTYLE_TRANSPARENT)) return CControlBar::OnEraseBkgnd(pDC);
  return (BOOL)Default();
}

// Updates a toolbar button during idle time.
class CToolCmdUI : public CCmdUI {
public:
  virtual void Enable(BOOL bOn) {
    m_bEnableChanged = TRUE;
    CToolBar* pToolBar = (CToolBar*)m_pOther;
    UINT nNewStyle = pToolBar->GetButtonStyle(m_nIndex) & ~TBBS_DISABLED;
    if (!bOn) {
      nNewStyle |= TBBS_DISABLED;
      nNewStyle &= ~TBBS_PRESSED;
    }
    pToolBar->SetButtonStyle(m_nIndex, nNewStyle);
  }
  virtual void SetCheck(int nCheck) {
    CToolBar* pToolBar = (CToolBar*)m_pOther;
    UINT nNewStyle = pToolBar->GetButtonStyle(m_nIndex) & ~(TBBS_CHECKED | TBBS_INDETERMINATE);
    if (nCheck == 1)
      nNewStyle |= TBBS_CHECKED;
    else if (nCheck == 2)
      nNewStyle |= TBBS_INDETERMINATE;
    pToolBar->SetButtonStyle(m_nIndex, nNewStyle | TBBS_CHECKBOX);
  }
  virtual void SetText(LPCTSTR) {}
};

void CToolBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  CToolCmdUI state;
  state.m_pOther = this;
  int nCount = (int)DefWindowProc(TB_BUTTONCOUNT, 0, 0);
  state.m_nIndexMax = (UINT)nCount;
  for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax; state.m_nIndex++) {
    TBBUTTON button;
    _GetButton(state.m_nIndex, &button);
    if (button.fsStyle & TBSTYLE_SEP) continue;
    state.m_nID = (UINT)button.idCommand;
    // The toolbar itself may handle updates; then the target does.
    if (CWnd::OnCmdMsg(state.m_nID, CN_UPDATE_COMMAND_UI, &state, NULL)) continue;
    state.DoUpdate(pTarget, bDisableIfNoHndler);
  }
  UpdateDialogControls(pTarget, bDisableIfNoHndler);
}

// ---------------------------------------------------------------------
// CStatusBar

BEGIN_MESSAGE_MAP(CStatusBar, CControlBar)
  ON_WM_NCPAINT()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_SIZE()
  ON_MESSAGE(WM_SETFONT, OnSetFont)
  ON_MESSAGE(WM_SETTEXT, OnSetText)
  ON_MESSAGE(WM_GETTEXT, OnGetText)
  ON_MESSAGE(WM_GETTEXTLENGTH, OnGetTextLength)
END_MESSAGE_MAP()

static const int CX_PANE_PADDING = 8;  // around an indicator's text

CStatusBar::CStatusBar() {
  m_hFont = NULL;
  m_cxSizeBox = 0;
  m_bHideSizeBox = FALSE;
  m_cxLeftBorder = m_cxRightBorder = 0;
  m_cyTopBorder = m_cyBottomBorder = 0;
}

CStatusBar::~CStatusBar() {
  DestroyWindow();
  delete[] (AFX_STATUSPANE*)m_pData;
  m_pData = NULL;
}

BOOL CStatusBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  return CreateEx(pParentWnd, 0, dwStyle, nID);
}

BOOL CStatusBar::CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle, DWORD dwStyle, UINT nID) {
  // Status bars draw their own top edge.
  m_dwStyle = (dwStyle & CBRS_ALL) & ~CBRS_BORDER_ANY;
  DWORD dwWndStyle = (dwStyle & ~CBRS_ALL) | dwCtrlStyle | CCS_NOPARENTALIGN | CCS_NOMOVEY |
                     CCS_NODIVIDER | CCS_NORESIZE;
  if (pParentWnd != NULL && (pParentWnd->GetStyle() & WS_THICKFRAME)) dwWndStyle |= SBARS_SIZEGRIP;
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_BAR_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  if (!CWnd::Create(STATUSCLASSNAME, NULL, dwWndStyle, CRect(0, 0, 0, 0), pParentWnd, nID))
    return FALSE;
  if (dwWndStyle & SBARS_SIZEGRIP) m_cxSizeBox = ::GetSystemMetrics(SM_CXVSCROLL);
  return TRUE;
}

static HFONT _AfxStatusFont(CStatusBar* pBar) {
  HFONT hFont = pBar->m_hFont;
  if (hFont == NULL && pBar->m_hWnd != NULL) hFont = (HFONT)pBar->SendMessage(WM_GETFONT);
  if (hFont == NULL) hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
  return hFont;
}

BOOL CStatusBar::SetIndicators(const UINT* lpIDArray, int nIDCount) {
  delete[] (AFX_STATUSPANE*)m_pData;
  m_pData = NULL;
  m_nCount = 0;
  if (nIDCount <= 0) return TRUE;
  AFX_STATUSPANE* pPanes = new AFX_STATUSPANE[nIDCount];
  m_pData = pPanes;
  m_nCount = nIDCount;

  CClientDC dcScreen(NULL);
  HGDIOBJ hOldFont = dcScreen.SelectObject(_AfxStatusFont(this));
  BOOL bResult = TRUE;
  for (int i = 0; i < nIDCount; i++) {
    AFX_STATUSPANE* pSBP = &pPanes[i];
    pSBP->nID = lpIDArray != NULL ? lpIDArray[i] : ID_SEPARATOR;
    pSBP->nFlags = 0;
    pSBP->cxText = 0;
    if (pSBP->nID == ID_SEPARATOR) {
      // The message pane stretches and has no border.
      pSBP->nStyle = SBPS_STRETCH | SBPS_NOBORDERS;
    } else {
      pSBP->nStyle = SBPS_NORMAL;
      if (!AfxLoadString(pSBP->nID, pSBP->strText)) {
        TRACE(_T("Warning: no string for status bar indicator %u.\n"), pSBP->nID);
        bResult = FALSE;
      }
      pSBP->cxText = dcScreen.GetTextExtent(pSBP->strText).cx;
    }
  }
  dcScreen.SelectObject(hOldFont);
  UpdateAllPanes(TRUE, TRUE);
  return bResult;
}

int CStatusBar::CommandToIndex(UINT nIDFind) const {
  for (int i = 0; i < m_nCount; i++)
    if (_GetPanePtr(i)->nID == nIDFind) return i;
  return -1;
}

UINT CStatusBar::GetItemID(int nIndex) const {
  return nIndex >= 0 && nIndex < m_nCount ? _GetPanePtr(nIndex)->nID : 0;
}

void CStatusBar::GetItemRect(int nIndex, LPRECT lpRect) const {
  if (!((CStatusBar*)this)->DefWindowProc(SB_GETRECT, nIndex, (LPARAM)lpRect))
    ::SetRectEmpty(lpRect);
}

void CStatusBar::GetPaneText(int nIndex, CString& rString) const {
  if (nIndex >= 0 && nIndex < m_nCount)
    rString = _GetPanePtr(nIndex)->strText;
  else
    rString.Empty();
}

CString CStatusBar::GetPaneText(int nIndex) const {
  CString str;
  GetPaneText(nIndex, str);
  return str;
}

static UINT _AfxPaneTextFlags(UINT nStyle) {
  UINT nFlags = 0;
  if (nStyle & SBPS_NOBORDERS) nFlags |= SBT_NOBORDERS;
  if (nStyle & SBPS_POPOUT) nFlags |= SBT_POPOUT;
  if (nStyle & SBPS_OWNERDRAW) nFlags |= SBT_OWNERDRAW;
  return nFlags;
}

BOOL CStatusBar::SetPaneText(int nIndex, LPCTSTR lpszNewText, BOOL bUpdate) {
  if (nIndex < 0 || nIndex >= m_nCount) return FALSE;
  AFX_STATUSPANE* pSBP = _GetPanePtr(nIndex);
  if (pSBP->nStyle & SBPS_OWNERDRAW) {
    if (bUpdate && m_hWnd != NULL)
      DefWindowProc(SB_SETTEXT, nIndex | SBT_OWNERDRAW, (LPARAM)lpszNewText);
    return TRUE;
  }
  pSBP->strText = lpszNewText != NULL ? lpszNewText : _T("");
  if (bUpdate && m_hWnd != NULL) {
    LPCTSTR lpsz = (pSBP->nStyle & SBPS_DISABLED) ? _T("") : (LPCTSTR)pSBP->strText;
    DefWindowProc(SB_SETTEXT, nIndex | _AfxPaneTextFlags(pSBP->nStyle), (LPARAM)lpsz);
  }
  return TRUE;
}

void CStatusBar::GetPaneInfo(int nIndex, UINT& nID, UINT& nStyle, int& cxWidth) const {
  AFX_STATUSPANE* pSBP = _GetPanePtr(nIndex);
  nID = pSBP->nID;
  nStyle = pSBP->nStyle;
  cxWidth = pSBP->cxText;
}

void CStatusBar::SetPaneInfo(int nIndex, UINT nID, UINT nStyle, int cxWidth) {
  AFX_STATUSPANE* pSBP = _GetPanePtr(nIndex);
  BOOL bChanged = pSBP->nStyle != nStyle || pSBP->cxText != cxWidth;
  pSBP->nID = nID;
  pSBP->nStyle = nStyle;
  pSBP->cxText = cxWidth;
  if (m_hWnd != NULL) UpdateAllPanes(bChanged, TRUE);
}

UINT CStatusBar::GetPaneStyle(int nIndex) const { return _GetPanePtr(nIndex)->nStyle; }

void CStatusBar::SetPaneStyle(int nIndex, UINT nStyle) {
  AFX_STATUSPANE* pSBP = _GetPanePtr(nIndex);
  if (pSBP->nStyle == nStyle) return;
  BOOL bLayout = ((pSBP->nStyle ^ nStyle) & SBPS_STRETCH) != 0;
  pSBP->nStyle = nStyle;
  if (m_hWnd == NULL) return;
  if (bLayout) UpdateAllPanes(TRUE, FALSE);
  SetPaneText(nIndex, pSBP->strText, TRUE);
}

void CStatusBar::UpdateAllPanes(BOOL bUpdateRects, BOOL bUpdateText) {
  if (m_hWnd == NULL || m_nCount == 0) return;
  if (bUpdateRects) {
    CRect rect;
    GetClientRect(&rect);
    int nBorders[3] = {0, 0, 2};
    DefWindowProc(SB_GETBORDERS, 0, (LPARAM)nBorders);
    int cxGap = nBorders[2];
    int cxRight = rect.right;
    CWnd* pParent = GetParent();
    if ((GetStyle() & SBARS_SIZEGRIP) && !(pParent != NULL && pParent->IsZoomed()))
      cxRight -= m_cxSizeBox;

    // Fixed panes get their text width; stretchy panes share the rest.
    int cxFixed = 0, nStretch = 0;
    for (int i = 0; i < m_nCount; i++) {
      AFX_STATUSPANE* pSBP = _GetPanePtr(i);
      if (pSBP->nStyle & SBPS_STRETCH)
        nStretch++;
      else
        cxFixed += pSBP->cxText + CX_PANE_PADDING;
      if (i > 0) cxFixed += cxGap;
    }
    int cxExtra = max(0, cxRight - cxFixed);
    int* pRights = new int[m_nCount];
    int x = 0;
    for (int i = 0; i < m_nCount; i++) {
      AFX_STATUSPANE* pSBP = _GetPanePtr(i);
      int cx;
      if (pSBP->nStyle & SBPS_STRETCH) {
        cx = cxExtra / nStretch;
        cxExtra -= cx;
        nStretch--;
      } else {
        cx = pSBP->cxText + CX_PANE_PADDING;
      }
      if (i > 0) x += cxGap;
      x += cx;
      pRights[i] = x;
    }
    DefWindowProc(SB_SETPARTS, m_nCount, (LPARAM)pRights);
    delete[] pRights;
  }
  if (bUpdateText) {
    for (int i = 0; i < m_nCount; i++) {
      AFX_STATUSPANE* pSBP = _GetPanePtr(i);
      if (pSBP->nStyle & SBPS_OWNERDRAW) continue;
      LPCTSTR lpsz = (pSBP->nStyle & SBPS_DISABLED) ? _T("") : (LPCTSTR)pSBP->strText;
      DefWindowProc(SB_SETTEXT, i | _AfxPaneTextFlags(pSBP->nStyle), (LPARAM)lpsz);
    }
  }
}

CSize CStatusBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  CClientDC dcScreen(NULL);
  HGDIOBJ hOldFont = dcScreen.SelectObject(_AfxStatusFont(this));
  TEXTMETRIC tm;
  dcScreen.GetTextMetrics(&tm);
  dcScreen.SelectObject(hOldFont);

  CSize size = CControlBar::CalcFixedLayout(bStretch, bHorz);
  size.cy += tm.tmHeight + 7 + m_cyTopBorder + m_cyBottomBorder;
  return size;
}

void CStatusBar::OnBarStyleChange(DWORD, DWORD) {}

void CStatusBar::DrawItem(LPDRAWITEMSTRUCT) {}

BOOL CStatusBar::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (message != WM_DRAWITEM) return CControlBar::OnChildNotify(message, wParam, lParam, pResult);
  DrawItem((LPDRAWITEMSTRUCT)lParam);
  if (pResult != NULL) *pResult = TRUE;
  return TRUE;
}

void CStatusBar::OnNcPaint() { CControlBar::OnNcPaint(); }

void CStatusBar::OnPaint() { Default(); }

BOOL CStatusBar::OnEraseBkgnd(CDC*) { return (BOOL)Default(); }

void CStatusBar::OnSize(UINT nType, int cx, int cy) {
  (void)nType;
  (void)cx;
  (void)cy;
  Default();
  UpdateAllPanes(TRUE, FALSE);
}

LRESULT CStatusBar::OnSetFont(WPARAM wParam, LPARAM lParam) {
  m_hFont = (HFONT)wParam;
  LRESULT lResult = DefWindowProc(WM_SETFONT, wParam, lParam);
  // Indicator widths depend on the font.
  CClientDC dcScreen(NULL);
  HGDIOBJ hOldFont = dcScreen.SelectObject(_AfxStatusFont(this));
  for (int i = 0; i < m_nCount; i++) {
    AFX_STATUSPANE* pSBP = _GetPanePtr(i);
    if (pSBP->nID != ID_SEPARATOR && !(pSBP->nStyle & SBPS_STRETCH))
      pSBP->cxText = dcScreen.GetTextExtent(pSBP->strText).cx;
  }
  dcScreen.SelectObject(hOldFont);
  UpdateAllPanes(TRUE, FALSE);
  return lResult;
}

// The frame shows prompts by setting the status bar's window text, which
// goes to the first stretchy pane.
static int _AfxMessagePane(const CStatusBar* pBar) {
  int nIndex = pBar->CommandToIndex(ID_SEPARATOR);
  return nIndex < 0 ? 0 : nIndex;
}

LRESULT CStatusBar::OnSetText(WPARAM, LPARAM lParam) {
  if (m_nCount == 0) return DefWindowProc(WM_SETTEXT, 0, lParam);
  return SetPaneText(_AfxMessagePane(this), (LPCTSTR)lParam, TRUE);
}

LRESULT CStatusBar::OnGetText(WPARAM wParam, LPARAM lParam) {
  if (m_nCount == 0) return DefWindowProc(WM_GETTEXT, wParam, lParam);
  CString str;
  GetPaneText(_AfxMessagePane(this), str);
  int nLen = str.GetLength();
  if (wParam == 0 || lParam == 0) return 0;
  if (nLen > (int)wParam - 1) nLen = (int)wParam - 1;
  memcpy((LPTSTR)lParam, (LPCTSTR)str, nLen * sizeof(TCHAR));
  ((LPTSTR)lParam)[nLen] = 0;
  return nLen;
}

LRESULT CStatusBar::OnGetTextLength(WPARAM wParam, LPARAM lParam) {
  if (m_nCount == 0) return DefWindowProc(WM_GETTEXTLENGTH, wParam, lParam);
  return GetPaneText(_AfxMessagePane(this)).GetLength();
}

void CStatusCmdUI::Enable(BOOL bOn) {
  m_bEnableChanged = TRUE;
  CStatusBar* pStatusBar = (CStatusBar*)m_pOther;
  UINT nStyle = pStatusBar->GetPaneStyle(m_nIndex) & ~SBPS_DISABLED;
  if (!bOn) nStyle |= SBPS_DISABLED;
  pStatusBar->SetPaneStyle(m_nIndex, nStyle);
}

void CStatusCmdUI::SetCheck(int nCheck) {
  CStatusBar* pStatusBar = (CStatusBar*)m_pOther;
  UINT nStyle = pStatusBar->GetPaneStyle(m_nIndex) & ~SBPS_POPOUT;
  if (nCheck != 0) nStyle |= SBPS_POPOUT;
  pStatusBar->SetPaneStyle(m_nIndex, nStyle);
}

void CStatusCmdUI::SetText(LPCTSTR lpszText) {
  CStatusBar* pStatusBar = (CStatusBar*)m_pOther;
  if (pStatusBar->GetPaneText(m_nIndex) != (lpszText != NULL ? lpszText : _T("")))
    pStatusBar->SetPaneText(m_nIndex, lpszText);
}

void CStatusBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  CStatusCmdUI state;
  state.m_pOther = this;
  state.m_nIndexMax = (UINT)m_nCount;
  for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax; state.m_nIndex++) {
    state.m_nID = _GetPanePtr(state.m_nIndex)->nID;
    if (state.m_nID == ID_SEPARATOR) continue;
    if (CWnd::OnCmdMsg(state.m_nID, CN_UPDATE_COMMAND_UI, &state, NULL)) continue;
    // Panes without a handler stay as they are.
    state.DoUpdate(pTarget, FALSE);
  }
  UpdateDialogControls(pTarget, bDisableIfNoHndler);
}

// ---------------------------------------------------------------------
// CDialogBar

CDialogBar::CDialogBar() : m_sizeDefault(0, 0) {
  m_cxLeftBorder = m_cxRightBorder = 0;
  m_cyTopBorder = m_cyBottomBorder = 0;
}

CDialogBar::~CDialogBar() { DestroyWindow(); }

BOOL CDialogBar::Create(CWnd* pParentWnd, LPCTSTR lpszTemplateName, UINT nStyle, UINT nID) {
  m_dwStyle = nStyle & CBRS_ALL;
  if (!CreateDlg(lpszTemplateName, pParentWnd)) return FALSE;
  SetDlgCtrlID(nID);
  CRect rect;
  GetWindowRect(&rect);
  m_sizeDefault = rect.Size();
  ExecuteDlgInit(lpszTemplateName);
  // Recompute the non-client area now that the bar style is known.
  SetWindowPos(NULL, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (nStyle & WS_VISIBLE) ShowWindow(SW_SHOWNOACTIVATE);
  return TRUE;
}

CSize CDialogBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  CSize size = CControlBar::CalcFixedLayout(FALSE, bHorz);
  size += m_sizeDefault;
  if (bStretch) {
    if (bHorz)
      size.cx = 32767;
    else
      size.cy = 32767;
  }
  return size;
}

void CDialogBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  UpdateDialogControls(pTarget, bDisableIfNoHndler);
}

// ---------------------------------------------------------------------
// The frame's side of control bars

CControlBar* CFrameWnd::GetControlBar(UINT nID) {
  if (nID == 0) return NULL;
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (pBar->m_hWnd != NULL && (UINT)pBar->GetDlgCtrlID() == nID) return pBar;
  }
  return NULL;
}

void CFrameWnd::ShowControlBar(CControlBar* pBar, BOOL bShow, BOOL bDelay) {
  pBar->DelayShow(bShow);
  if (!bDelay) RecalcLayout();
}

void CFrameWnd::OnUpdateControlBarMenu(CCmdUI* pCmdUI) {
  CControlBar* pBar = GetControlBar(pCmdUI->m_nID);
  if (pBar != NULL) {
    pCmdUI->SetCheck((pBar->GetStyle() & WS_VISIBLE) != 0);
    return;
  }
  pCmdUI->ContinueRouting();
}

BOOL CFrameWnd::OnBarCheck(UINT nID) {
  CControlBar* pBar = GetControlBar(nID);
  if (pBar == NULL) return FALSE;
  ShowControlBar(pBar, (pBar->GetStyle() & WS_VISIBLE) == 0, FALSE);
  return TRUE;
}

// Tooltip text for toolbar buttons: the part of the command's string
// after the newline.
BOOL CFrameWnd::OnToolTipText(UINT, NMHDR* pNMHDR, LRESULT* pResult) {
  TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pNMHDR;
  TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pNMHDR;
  BOOL bWide = pNMHDR->code == TTN_NEEDTEXTW;
  UINT uFlags = bWide ? pTTTW->uFlags : pTTTA->uFlags;
  UINT_PTR nID = pNMHDR->idFrom;
  if (uFlags & TTF_IDISHWND) nID = (UINT_PTR)::GetDlgCtrlID((HWND)nID);

  CString strTip;
  if (nID != 0 && !(nID >= 0xF000 && nID < 0xF1F0)) {
    CString strFull;
    if (AfxLoadString((UINT)nID, strFull)) {
      int i = strFull.Find(_T('\n'));
      if (i >= 0) strTip = strFull.Mid(i + 1);
    }
  }
  if (bWide) {
    CStringW strW(strTip);
    lstrcpynW(pTTTW->szText, strW, sizeof(pTTTW->szText) / sizeof(WCHAR));
  } else {
    CStringA strA(strTip);
    lstrcpynA(pTTTA->szText, strA, sizeof(pTTTA->szText));
  }
  *pResult = 0;
  // Tooltips should sit above everything else.
  ::SetWindowPos(pNMHDR->hwndFrom, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER);
  return TRUE;
}
