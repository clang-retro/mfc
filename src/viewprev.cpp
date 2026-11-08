// viewprev.cpp - print preview: CPreviewView, CView::DoPrintPreview and
// the frame's preview mode.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxpriv.h>

CPrintPreviewState::CPrintPreviewState() {
  nIDMainPane = AFX_IDW_PANE_FIRST;
  hMenu = NULL;
  dwStates = 0;
  pViewActiveOld = NULL;
  lpfnCloseProc = NULL;
  hAccelTable = NULL;
}

// ---------------------------------------------------------------------
// The frame in preview mode: no menu, no accelerators, the control bars
// hidden except the status bar, and the main pane put aside so the
// preview window can take its place.

void CFrameWnd::OnSetPreviewMode(BOOL bPreview, CPrintPreviewState* pState) {
  if (bPreview) {
    pState->hMenu = ::GetMenu(m_hWnd);
    ::SetMenu(m_hWnd, NULL);
    pState->hAccelTable = m_hAccelTable;
    m_hAccelTable = NULL;

    pState->dwStates = 0;
    POSITION pos = m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
      UINT nID = pBar->GetDlgCtrlID();
      if (nID < AFX_IDW_CONTROLBAR_FIRST || nID > AFX_IDW_CONTROLBAR_FIRST + 31) continue;
      if (nID == AFX_IDW_STATUS_BAR || !(pBar->GetStyle() & WS_VISIBLE)) continue;
      pState->dwStates |= AFX_CONTROLBAR_MASK(nID);
      pBar->DelayShow(FALSE);
    }

    CWnd* pPane = GetDlgItem(pState->nIDMainPane);
    if (pPane != NULL) {
      pPane->ShowWindow(SW_HIDE);
      pPane->SetDlgCtrlID(AFX_IDW_PANE_SAVE);
    }
  } else {
    CWnd* pPane = GetDlgItem(AFX_IDW_PANE_SAVE);
    if (pPane != NULL) {
      pPane->SetDlgCtrlID(pState->nIDMainPane);
      pPane->ShowWindow(SW_SHOW);
    }
    POSITION pos = m_listControlBars.GetHeadPosition();
    while (pos != NULL) {
      CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
      UINT nID = pBar->GetDlgCtrlID();
      if (nID < AFX_IDW_CONTROLBAR_FIRST || nID > AFX_IDW_CONTROLBAR_FIRST + 31) continue;
      if (pState->dwStates & AFX_CONTROLBAR_MASK(nID)) pBar->DelayShow(TRUE);
    }
    m_hAccelTable = pState->hAccelTable;
    ::SetMenu(m_hWnd, pState->hMenu);
  }
  RecalcLayout();
}

// ---------------------------------------------------------------------
// Starting and ending preview from a view

// The preview toolbar, for programs whose resources do not include
// afxprint.rc.
static LPCDLGTEMPLATE _AfxPreviewToolbarTemplate(CAfxDlgTemplate& dt) {
  static const struct {
    WORD nID;
    LPCWSTR lpsz;
  } buttons[] = {
      {AFX_ID_PREVIEW_PRINT, L"&Print..."},  {AFX_ID_PREVIEW_NEXT, L"&Next Page"},
      {AFX_ID_PREVIEW_PREV, L"Pre&v Page"},  {AFX_ID_PREVIEW_NUMPAGE, L"&Two Page"},
      {AFX_ID_PREVIEW_ZOOMIN, L"Zoom &In"},  {AFX_ID_PREVIEW_ZOOMOUT, L"Zoom &Out"},
      {AFX_ID_PREVIEW_CLOSE, L"&Close"},
  };
  int x = 4;
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
    dt.AddItem(0x80, BS_PUSHBUTTON | WS_TABSTOP, x, 2, 44, 12, buttons[i].nID, buttons[i].lpsz);
    x += 47;
  }
  return dt.GetTemplate();
}

void CView::OnFilePrintPreview() {
  CPrintPreviewState* pState = new CPrintPreviewState;
  if (!DoPrintPreview(AFX_IDD_PREVIEW_TOOLBAR, this, RUNTIME_CLASS(CPreviewView), pState)) {
    AfxMessageBox(AFX_IDP_COMMAND_FAILURE);
    delete pState;
  }
}

BOOL CView::DoPrintPreview(UINT nIDResource, CView* pPrintView, CRuntimeClass* pPreviewViewClass,
                           CPrintPreviewState* pState) {
  // Preview takes over the top-level frame (in MDI, the main window).
  CFrameWnd* pParent = GetTopLevelFrame();
  if (pParent == NULL) pParent = GetParentFrame();
  if (pParent == NULL || pPreviewViewClass == NULL) return FALSE;

  CPreviewView* pView = (CPreviewView*)pPreviewViewClass->CreateObject();
  if (pView == NULL || !pView->IsKindOf(RUNTIME_CLASS(CPreviewView))) {
    delete pView;
    return FALSE;
  }
  pView->m_pPreviewState = pState;

  // The views that were active stop being so.
  CFrameWnd* pActiveFrame = pParent->GetActiveFrame();
  CView* pActiveView = pActiveFrame != NULL ? pActiveFrame->GetActiveView() : NULL;
  if (pActiveView != NULL) pActiveView->OnActivateView(FALSE, pActiveView, pActiveView);
  pState->pViewActiveOld = pParent->GetActiveView();
  pParent->SetActiveView(NULL, FALSE);

  pParent->OnSetPreviewMode(TRUE, pState);

  pView->m_pToolBar = new CDialogBar;
  BOOL bToolBar;
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(nIDResource), RT_DIALOG);
  if (::FindResource(hInst, MAKEINTRESOURCE(nIDResource), RT_DIALOG) != NULL) {
    bToolBar = pView->m_pToolBar->Create(pParent, MAKEINTRESOURCE(nIDResource),
                                         CBRS_TOP | WS_VISIBLE, AFX_IDW_PREVIEW_BAR);
  } else {
    CAfxDlgTemplate dt(WS_CHILD, 4 + 7 * 47, 16, NULL);
    bToolBar = _AfxCreateDialogBarIndirect(pView->m_pToolBar, pParent,
                                           _AfxPreviewToolbarTemplate(dt), CBRS_TOP | WS_VISIBLE,
                                           AFX_IDW_PREVIEW_BAR);
  }
  if (!bToolBar) {
    delete pView->m_pToolBar;
    pView->m_pToolBar = NULL;
    pParent->OnSetPreviewMode(FALSE, pState);
    delete pView;
    return FALSE;
  }
  pView->m_pToolBar->m_bAutoDelete = TRUE;

  // The preview window shows the document but is not one of its views.
  CCreateContext context;
  context.m_pCurrentFrame = pParent;
  context.m_pLastView = this;
  if (!pView->Create(NULL, NULL, AFX_WS_DEFAULT_VIEW, CRect(0, 0, 0, 0), pParent,
                     AFX_IDW_PANE_FIRST, &context)) {
    pView->m_pPreviewState = NULL;
    pView->m_pToolBar->DestroyWindow();
    pParent->OnSetPreviewMode(FALSE, pState);
    return FALSE;
  }
  pView->m_pDocument = GetDocument();
  pView->m_pOrigView = this;

  if (!pView->SetPrintView(pPrintView)) {
    pView->OnPreviewClose();
    return TRUE;  // the preview cleaned up after itself
  }

  pParent->SetActiveView(pView);
  pView->m_pToolBar->SendMessage(WM_IDLEUPDATECMDUI, (WPARAM)TRUE);
  pParent->RecalcLayout();
  pView->UpdateWindow();
  return TRUE;
}

void CView::OnEndPrintPreview(CDC* pDC, CPrintInfo* pInfo, POINT point, CPreviewView* pView) {
  (void)point;
  OnEndPrinting(pDC, pInfo);

  CFrameWnd* pParent = (CFrameWnd*)pView->GetParent();
  CPrintPreviewState* pState = pView->m_pPreviewState;
  pView->m_pPreviewState = NULL;
  if (pView->m_pToolBar != NULL) {
    pView->m_pToolBar->DestroyWindow();
    pView->m_pToolBar = NULL;
  }

  pParent->SetActiveView(NULL, FALSE);
  pView->DestroyWindow();
  pParent->OnSetPreviewMode(FALSE, pState);

  // Back to the views that were active.
  pParent->SetActiveView(pState->pViewActiveOld, FALSE);
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL) {
    pFrame->SetActiveView(this, FALSE);
    OnActivateView(TRUE, this, this);
  }
  pParent->RecalcLayout();
  pParent->SendMessageToDescendants(WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0, TRUE, TRUE);
  pParent->UpdateWindow();
  if (pState->lpfnCloseProc != NULL) (*pState->lpfnCloseProc)(pParent);
  delete pState;
}

// ---------------------------------------------------------------------
// CPreviewView

IMPLEMENT_DYNCREATE(CPreviewView, CScrollView)

BEGIN_MESSAGE_MAP(CPreviewView, CScrollView)
  ON_WM_SIZE()
  ON_WM_LBUTTONDOWN()
  ON_WM_SETCURSOR()
  ON_WM_KEYDOWN()
  ON_WM_ERASEBKGND()
  ON_WM_MOUSEWHEEL()
  ON_COMMAND(AFX_ID_PREVIEW_CLOSE, OnPreviewClose)
  ON_COMMAND(AFX_ID_PREVIEW_NUMPAGE, OnNumPageChange)
  ON_COMMAND(AFX_ID_PREVIEW_NEXT, OnNextPage)
  ON_COMMAND(AFX_ID_PREVIEW_PREV, OnPrevPage)
  ON_COMMAND(AFX_ID_PREVIEW_PRINT, OnPreviewPrint)
  ON_COMMAND(AFX_ID_PREVIEW_ZOOMIN, OnZoomIn)
  ON_COMMAND(AFX_ID_PREVIEW_ZOOMOUT, OnZoomOut)
  ON_UPDATE_COMMAND_UI(AFX_ID_PREVIEW_NUMPAGE, OnUpdateNumPageChange)
  ON_UPDATE_COMMAND_UI(AFX_ID_PREVIEW_NEXT, OnUpdateNextPage)
  ON_UPDATE_COMMAND_UI(AFX_ID_PREVIEW_PREV, OnUpdatePrevPage)
  ON_UPDATE_COMMAND_UI(AFX_ID_PREVIEW_ZOOMIN, OnUpdateZoomIn)
  ON_UPDATE_COMMAND_UI(AFX_ID_PREVIEW_ZOOMOUT, OnUpdateZoomOut)
END_MESSAGE_MAP()

static const int _afxPageGap = 8;  // around and between pages, in pixels

CPreviewView::CPreviewView() {
  m_pPreviewState = NULL;
  m_pToolBar = NULL;
  m_pOrigView = NULL;
  m_pPrintView = NULL;
  m_pPreviewDC = NULL;
  m_pPreviewInfo = NULL;
  m_nZoomOutPages = 1;
  m_nZoomState = ZOOM_OUT;
  m_nMaxPages = 2;
  m_nCurrentPage = 1;
  m_nPages = 1;
  m_nSecondPageOffset = 0;
  m_hMagnifyCursor = NULL;
  m_sizePrinterPPI = CSize(0, 0);
  m_ptCenterPoint = CPoint(0, 0);
  m_pPageInfo = m_pageInfoArray;
  memset(m_pageInfoArray, 0, sizeof(m_pageInfoArray));
  m_bPageNumDisplayed = FALSE;
}

CPreviewView::~CPreviewView() {
  // Not one of the document's views (see DoPrintPreview).
  m_pDocument = NULL;
  delete m_pPreviewDC;
  m_dcPrint.Detach();  // the print info owns the printer DC
  delete m_pPreviewInfo;
  delete m_pPreviewState;
  if (m_hMagnifyCursor != NULL) ::DestroyCursor(m_hMagnifyCursor);
}

// A magnifying glass, for programs without the AFX_IDC_MAGNIFY cursor.
static HCURSOR _AfxCreateMagnifyCursor() {
  BYTE andMask[32 * 4], xorMask[32 * 4];
  memset(andMask, 0xFF, sizeof(andMask));
  memset(xorMask, 0, sizeof(xorMask));
  // Lens centred at (12,12), radius 8; handle down to (26,26).
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      int dx = x - 12, dy = y - 12, d2 = dx * dx + dy * dy;
      BOOL bRing = d2 >= 7 * 7 && d2 <= 9 * 9;
      BOOL bInside = d2 < 7 * 7;
      BOOL bHandle = x >= 18 && x <= 27 && y >= 18 && y <= 27 && (x - y <= 1 && y - x <= 1);
      BYTE bit = (BYTE)(0x80 >> (x & 7));
      if (bRing || bHandle) andMask[y * 4 + x / 8] &= (BYTE)~bit;  // black
      if (bInside) {
        andMask[y * 4 + x / 8] &= (BYTE)~bit;  // white
        xorMask[y * 4 + x / 8] |= bit;
      }
    }
  }
  return ::CreateCursor(AfxGetInstanceHandle(), 12, 12, 32, 32, andMask, xorMask);
}

BOOL CPreviewView::SetPrintView(CView* pPrintView) {
  m_pPrintView = pPrintView;
  m_pPreviewInfo = new CPrintInfo;
  m_pPreviewInfo->m_bPreview = TRUE;
  m_pPreviewInfo->m_pPD->m_pd.Flags |= PD_PRINTSETUP;
  m_pPreviewInfo->m_pPD->m_pd.Flags &= ~PD_RETURNDC;
  if (!m_pPrintView->OnPreparePrinting(m_pPreviewInfo)) return FALSE;
  HDC hPrinterDC = m_pPreviewInfo->m_pPD->m_pd.hDC;
  if (hPrinterDC == NULL) return FALSE;

  m_dcPrint.Attach(hPrinterDC);
  m_dcPrint.m_bPrinting = TRUE;
  m_pPreviewDC = new CPreviewDC;
  m_pPreviewDC->SetAttribDC(hPrinterDC);
  m_sizePrinterPPI.cx = m_dcPrint.GetDeviceCaps(LOGPIXELSX);
  m_sizePrinterPPI.cy = m_dcPrint.GetDeviceCaps(LOGPIXELSY);

  // The view gets ready to print, with the window as the output.
  m_dcPrint.SaveDC();
  CClientDC dc(this);
  m_pPreviewDC->SetOutputDC(dc.m_hDC);
  m_pPrintView->OnBeginPrinting(m_pPreviewDC, m_pPreviewInfo);
  m_pPreviewDC->ReleaseOutputDC();
  m_dcPrint.RestoreDC(-1);
  m_dcPrint.SaveDC();  // the state each page starts from

  m_nPages = m_pPreviewInfo->m_nNumPreviewPages;
  if (m_nPages == 0) m_nPages = AfxGetApp()->m_nNumPreviewPages;
  if (m_nPages == 0) m_nPages = 1;
  if (m_nPages > m_nMaxPages) m_nPages = m_nMaxPages;
  m_nZoomOutPages = m_nPages;
  m_nZoomState = ZOOM_OUT;

  if (AfxGetResourceHandle() != NULL)
    m_hMagnifyCursor = ::LoadCursor(AfxFindResourceHandle(MAKEINTRESOURCE(AFX_IDC_MAGNIFY), RT_GROUP_CURSOR),
                                    MAKEINTRESOURCE(AFX_IDC_MAGNIFY));
  if (m_hMagnifyCursor == NULL)
    m_hMagnifyCursor = _AfxCreateMagnifyCursor();
  else
    m_hMagnifyCursor = (HCURSOR)::CopyImage(m_hMagnifyCursor, IMAGE_CURSOR, 0, 0, 0);

  SetScrollSizes(MM_TEXT, CSize(1, 1));
  m_nCurrentPage = m_pPreviewInfo->m_nCurPage;
  if (m_nCurrentPage < m_pPreviewInfo->GetMinPage()) m_nCurrentPage = m_pPreviewInfo->GetMinPage();
  SetCurrentPage(m_nCurrentPage, TRUE);
  return TRUE;
}

// Whether page nPage exists: within the page range, and the view does not
// end printing before it.
BOOL CPreviewView::CanShowPage(UINT nPage) {
  if (m_pPreviewInfo == NULL) return FALSE;
  if (nPage < m_pPreviewInfo->GetMinPage() || nPage > m_pPreviewInfo->GetMaxPage()) return FALSE;
  UINT nSaveCur = m_pPreviewInfo->m_nCurPage;
  BOOL bSaveContinue = m_pPreviewInfo->m_bContinuePrinting;
  m_pPreviewInfo->m_nCurPage = nPage;
  m_pPreviewInfo->m_bContinuePrinting = TRUE;
  m_dcPrint.SaveDC();
  m_pPrintView->OnPrepareDC(&m_dcPrint, m_pPreviewInfo);
  m_dcPrint.RestoreDC(-1);
  BOOL bOK = m_pPreviewInfo->m_bContinuePrinting;
  m_pPreviewInfo->m_nCurPage = nSaveCur;
  m_pPreviewInfo->m_bContinuePrinting = bSaveContinue;
  return bOK;
}

void CPreviewView::OnDisplayPageNumber(UINT nPage, UINT nPagesDisplayed) {
  CString strFormat;
  if (!AfxLoadString(AFX_IDS_PREVIEWPAGEDESC, strFormat)) return;
  // "Page %u\nPages %u-%u\n": the first line for one page, the second for two.
  int nNewline = strFormat.Find(_T('\n'));
  CString strOne = nNewline >= 0 ? strFormat.Left(nNewline) : strFormat;
  CString strTwo = nNewline >= 0 ? strFormat.Mid(nNewline + 1) : strFormat;
  nNewline = strTwo.Find(_T('\n'));
  if (nNewline >= 0) strTwo = strTwo.Left(nNewline);
  CString strText;
  if (nPagesDisplayed == 1)
    strText.Format(strOne, nPage);
  else
    strText.Format(strTwo, nPage, nPage + nPagesDisplayed - 1);
  m_pPreviewInfo->m_strPageDesc = strFormat;
  CFrameWnd* pParent = (CFrameWnd*)GetParent();
  if (pParent != NULL) pParent->SetMessageText(strText);
  m_bPageNumDisplayed = TRUE;
}

// The size of the page in printer pixels, the whole sheet when the printer
// says how big it is.
static CSize _AfxPrinterPageSize(CDC* pDC, CPoint* pptOffset) {
  CSize size(pDC->GetDeviceCaps(PHYSICALWIDTH), pDC->GetDeviceCaps(PHYSICALHEIGHT));
  CPoint ptOffset(pDC->GetDeviceCaps(PHYSICALOFFSETX), pDC->GetDeviceCaps(PHYSICALOFFSETY));
  if (size.cx <= 0 || size.cy <= 0) {
    size = CSize(pDC->GetDeviceCaps(HORZRES), pDC->GetDeviceCaps(VERTRES));
    ptOffset = CPoint(0, 0);
  }
  if (pptOffset != NULL) *pptOffset = ptOffset;
  return size;
}

CSize CPreviewView::CalcScaleRatio(CSize windowSize, CSize actualSize) {
  // The fraction of actualSize that fits in windowSize, as num/den.
  if (actualSize.cx <= 0 || actualSize.cy <= 0) return CSize(1, 1);
  if ((LONGLONG)windowSize.cx * actualSize.cy < (LONGLONG)windowSize.cy * actualSize.cx)
    return CSize(windowSize.cx, actualSize.cx);
  return CSize(windowSize.cy, actualSize.cy);
}

// The size of one page on the screen at the current zoom.
CSize CPreviewView::CalcPageDisplaySize() {
  CSize sizePage = _AfxPrinterPageSize(&m_dcPrint, NULL);
  CClientDC dcScreen(NULL);
  int nScreenX = dcScreen.GetDeviceCaps(LOGPIXELSX), nScreenY = dcScreen.GetDeviceCaps(LOGPIXELSY);
  // Actual size: inches on paper as inches on the screen.
  CSize sizeActual(::MulDiv(sizePage.cx, nScreenX, m_sizePrinterPPI.cx),
                   ::MulDiv(sizePage.cy, nScreenY, m_sizePrinterPPI.cy));
  switch (m_nZoomState) {
    case ZOOM_IN:
      return CSize(sizeActual.cx * 2, sizeActual.cy * 2);
    case ZOOM_MIDDLE:
      return sizeActual;
    default: {
      CRect rect;
      GetClientRect(&rect);
      int nPages = (int)m_nPages;
      CSize sizeAvail((rect.Width() - _afxPageGap * (nPages + 1)) / nPages,
                      rect.Height() - 2 * _afxPageGap);
      if (sizeAvail.cx < 1) sizeAvail.cx = 1;
      if (sizeAvail.cy < 1) sizeAvail.cy = 1;
      CSize ratio = CalcScaleRatio(sizeAvail, sizeActual);
      return CSize(::MulDiv(sizeActual.cx, ratio.cx, ratio.cy),
                   ::MulDiv(sizeActual.cy, ratio.cx, ratio.cy));
    }
  }
}

void CPreviewView::PositionPage(UINT nPage) {
  CSize sizePage = _AfxPrinterPageSize(&m_dcPrint, NULL);
  CSize sizeDisplay = CalcPageDisplaySize();
  PAGE_INFO& info = m_pPageInfo[nPage];
  info.sizeUnscaled = sizePage;
  info.sizeScaleRatio = CSize(sizeDisplay.cx, sizePage.cx);
  if (m_nZoomState == ZOOM_OUT) info.sizeZoomOutRatio = info.sizeScaleRatio;

  CRect rect;
  GetClientRect(&rect);
  int nPages = (int)m_nPages;
  int cxTotal = nPages * sizeDisplay.cx + (nPages + 1) * _afxPageGap;
  int cyTotal = sizeDisplay.cy + 2 * _afxPageGap;
  int x = max(0, (rect.Width() - cxTotal) / 2) + _afxPageGap + (int)nPage * (sizeDisplay.cx + _afxPageGap);
  int y = max(0, (rect.Height() - cyTotal) / 2) + _afxPageGap;
  info.rectScreen.SetRect(x, y, x + sizeDisplay.cx, y + sizeDisplay.cy);
  if (nPage == 1) m_nSecondPageOffset = x;
}

void CPreviewView::SetCurrentPage(UINT nPage, BOOL bClearRatios) {
  (void)bClearRatios;
  if (m_pPreviewInfo == NULL) return;
  if (nPage < m_pPreviewInfo->GetMinPage()) nPage = m_pPreviewInfo->GetMinPage();
  if (nPage > m_pPreviewInfo->GetMaxPage()) nPage = m_pPreviewInfo->GetMaxPage();
  m_nCurrentPage = nPage;
  m_pPreviewInfo->m_nCurPage = nPage;
  for (UINT i = 0; i < m_nPages; i++) PositionPage(i);

  // Zoomed in, the pages scroll.
  if (m_nZoomState == ZOOM_OUT) {
    SetScrollSizes(MM_TEXT, CSize(1, 1));
  } else {
    CSize sizeDisplay = CalcPageDisplaySize();
    int nPages = (int)m_nPages;
    SetScrollSizes(MM_TEXT, CSize(nPages * sizeDisplay.cx + (nPages + 1) * _afxPageGap,
                                  sizeDisplay.cy + 2 * _afxPageGap));
  }
  UINT nShown = (m_nPages > 1 && CanShowPage(nPage + 1)) ? m_nPages : 1;
  OnDisplayPageNumber(nPage, nShown);
  Invalidate();
}

BOOL CPreviewView::FindPageRect(CPoint& point, UINT& nPage) {
  CPoint ptDoc = point + GetDeviceScrollPosition();
  for (UINT i = 0; i < m_nPages; i++) {
    if (m_pPageInfo[i].rectScreen.PtInRect(ptDoc)) {
      nPage = i;
      point = ptDoc - m_pPageInfo[i].rectScreen.TopLeft();
      return TRUE;
    }
  }
  return FALSE;
}

void CPreviewView::SetZoomState(UINT nNewState, UINT nPage, CPoint point) {
  if (nNewState > ZOOM_IN || nNewState == m_nZoomState) return;
  // The point under the mouse, as a fraction of its page.
  CSize sizeOld = m_pPageInfo[nPage].rectScreen.Size();
  double fx = sizeOld.cx > 0 ? (double)point.x / sizeOld.cx : 0.5;
  double fy = sizeOld.cy > 0 ? (double)point.y / sizeOld.cy : 0.5;

  m_nZoomState = nNewState;
  if (m_nZoomState == ZOOM_OUT) {
    m_nPages = m_nZoomOutPages;
  } else {
    // Zoomed, the page clicked is the one shown.
    if (nPage == 1) m_nCurrentPage++;
    m_nPages = 1;
    nPage = 0;
  }
  SetCurrentPage(m_nCurrentPage, TRUE);

  if (m_nZoomState != ZOOM_OUT) {
    CRect rectClient;
    GetClientRect(&rectClient);
    CRect& rectPage = m_pPageInfo[nPage].rectScreen;
    CPoint pt(rectPage.left + (int)(fx * rectPage.Width()) - rectClient.Width() / 2,
              rectPage.top + (int)(fy * rectPage.Height()) - rectClient.Height() / 2);
    ScrollToDevicePosition(CPoint(max(0L, (long)pt.x), max(0L, (long)pt.y)));
  }
  Invalidate();
}

void CPreviewView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo) {
  // Drawing is in client coordinates, scrolled by hand.
  CView::OnPrepareDC(pDC, pInfo);
}

BOOL CPreviewView::OnEraseBkgnd(CDC*) { return TRUE; }  // OnDraw paints everything

void CPreviewView::OnDraw(CDC* pDC) {
  if (m_pPreviewInfo == NULL || m_pPreviewDC == NULL) return;
  CRect rectClient;
  GetClientRect(&rectClient);
  CPoint ptScroll = GetDeviceScrollPosition();

  // Off-screen, so the pages do not flicker.
  CDC dcMem;
  CBitmap bitmap;
  CDC* pDraw = pDC;
  if (dcMem.CreateCompatibleDC(pDC) &&
      bitmap.CreateCompatibleBitmap(pDC, rectClient.Width(), rectClient.Height())) {
    dcMem.SelectObject(&bitmap);
    pDraw = &dcMem;
  }
  pDraw->FillSolidRect(&rectClient, ::GetSysColor(COLOR_APPWORKSPACE));

  CPoint ptOffset;
  _AfxPrinterPageSize(&m_dcPrint, &ptOffset);
  UINT nShown = 0;
  for (UINT i = 0; i < m_nPages; i++) {
    UINT nPage = m_nCurrentPage + i;
    if (i > 0 && !CanShowPage(nPage)) break;
    PAGE_INFO& info = m_pPageInfo[i];
    CRect rect = info.rectScreen;
    rect.OffsetRect(-ptScroll);
    if (rect.right < 0 || rect.bottom < 0) continue;

    // The sheet: a shadow, white paper and a black edge.
    CRect rectShadow = rect;
    rectShadow.OffsetRect(3, 3);
    pDraw->FillSolidRect(&rectShadow, RGB(0, 0, 0));
    pDraw->FillSolidRect(&rect, RGB(255, 255, 255));
    CBrush brushEdge(RGB(0, 0, 0));
    pDraw->FrameRect(&rect, &brushEdge);

    int nSaved = pDraw->SaveDC();
    m_dcPrint.RestoreDC(-1);
    m_dcPrint.SaveDC();
    m_pPreviewInfo->m_nCurPage = nPage;
    m_pPreviewInfo->m_bContinuePrinting = TRUE;
    m_pPreviewDC->SetOutputDC(pDraw->m_hDC);
    m_pPreviewDC->SetScaleRatio(info.sizeScaleRatio.cx, info.sizeScaleRatio.cy);
    CPoint ptTopLeft = ptOffset;
    ptTopLeft.x = ::MulDiv(ptTopLeft.x, info.sizeScaleRatio.cx, info.sizeScaleRatio.cy);
    ptTopLeft.y = ::MulDiv(ptTopLeft.y, info.sizeScaleRatio.cx, info.sizeScaleRatio.cy);
    m_pPreviewDC->SetTopLeftOffset(CSize(rect.left + ptTopLeft.x, rect.top + ptTopLeft.y));
    m_pPreviewDC->ClipToPage();
    m_pPrintView->OnPrepareDC(m_pPreviewDC, m_pPreviewInfo);
    if (m_pPreviewInfo->m_bContinuePrinting) {
      m_pPreviewInfo->m_rectDraw.SetRect(0, 0, m_dcPrint.GetDeviceCaps(HORZRES),
                                         m_dcPrint.GetDeviceCaps(VERTRES));
      m_pPreviewDC->DPtoLP(&m_pPreviewInfo->m_rectDraw);
      m_pPrintView->OnPrint(m_pPreviewDC, m_pPreviewInfo);
      nShown++;
    }
    m_pPreviewDC->ReleaseOutputDC();
    pDraw->RestoreDC(nSaved);
    if (!m_pPreviewInfo->m_bContinuePrinting) break;
  }
  m_pPreviewInfo->m_nCurPage = m_nCurrentPage;

  if (pDraw != pDC)
    pDC->BitBlt(0, 0, rectClient.Width(), rectClient.Height(), pDraw, 0, 0, SRCCOPY);
  (void)nShown;
}

void CPreviewView::OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView) {
  CScrollView::OnActivateView(bActivate, pActivateView, pDeactiveView);
}

BOOL CPreviewView::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  // Only the preview's own commands: the document and the print view's
  // commands wait until preview ends.
  return CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void CPreviewView::OnSize(UINT nType, int cx, int cy) {
  CScrollView::OnSize(nType, cx, cy);
  if (m_pPreviewInfo != NULL && cx > 0 && cy > 0) {
    for (UINT i = 0; i < m_nPages; i++) PositionPage(i);
    if (m_nZoomState == ZOOM_OUT) SetScrollSizes(MM_TEXT, CSize(1, 1));
    Invalidate();
  }
}

void CPreviewView::OnLButtonDown(UINT, CPoint point) {
  UINT nPage;
  if (!FindPageRect(point, nPage)) return;
  SetZoomState(m_nZoomState == ZOOM_IN ? (UINT)ZOOM_OUT : m_nZoomState + 1, nPage, point);
}

BOOL CPreviewView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (nHitTest == HTCLIENT && m_hMagnifyCursor != NULL) {
    CPoint point;
    ::GetCursorPos(&point);
    ScreenToClient(&point);
    UINT nPage;
    if (FindPageRect(point, nPage)) {
      ::SetCursor(m_hMagnifyCursor);
      return TRUE;
    }
  }
  return CScrollView::OnSetCursor(pWnd, nHitTest, message);
}

void CPreviewView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  switch (nChar) {
    case VK_ESCAPE:
      OnPreviewClose();
      return;
    case VK_NEXT:
      if (m_nZoomState == ZOOM_OUT) {
        OnNextPage();
        return;
      }
      break;
    case VK_PRIOR:
      if (m_nZoomState == ZOOM_OUT) {
        OnPrevPage();
        return;
      }
      break;
    case VK_HOME:
      SetCurrentPage(m_pPreviewInfo->GetMinPage(), TRUE);
      return;
  }
  CScrollView::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CPreviewView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  if (m_nZoomState == ZOOM_OUT) {
    if (zDelta < 0)
      OnNextPage();
    else
      OnPrevPage();
    return TRUE;
  }
  return CScrollView::OnMouseWheel(nFlags, zDelta, pt);
}

void CPreviewView::OnPreviewClose() {
  m_pOrigView->OnEndPrintPreview(m_pPreviewDC, m_pPreviewInfo, CPoint(0, 0), this);
}

void CPreviewView::OnNumPageChange() {
  m_nZoomOutPages = m_nZoomOutPages == 1 ? 2 : 1;
  if (m_nZoomState == ZOOM_OUT) m_nPages = m_nZoomOutPages;
  SetCurrentPage(m_nCurrentPage, TRUE);
}

void CPreviewView::OnNextPage() {
  if (CanShowPage(m_nCurrentPage + 1)) SetCurrentPage(m_nCurrentPage + 1, TRUE);
}

void CPreviewView::OnPrevPage() {
  if (m_nCurrentPage > m_pPreviewInfo->GetMinPage()) SetCurrentPage(m_nCurrentPage - 1, TRUE);
}

void CPreviewView::OnPreviewPrint() {
  CView* pOrigView = m_pOrigView;
  OnPreviewClose();  // this window is gone after this
  CWnd* pMainWnd = AfxGetMainWnd();
  if (pMainWnd != NULL)
    pMainWnd->SendMessage(WM_COMMAND, ID_FILE_PRINT);
  else
    pOrigView->SendMessage(WM_COMMAND, ID_FILE_PRINT);
}

void CPreviewView::OnZoomIn() {
  if (m_nZoomState < ZOOM_IN) SetZoomState(m_nZoomState + 1, 0, CPoint(0, 0));
}

void CPreviewView::OnZoomOut() {
  if (m_nZoomState > ZOOM_OUT) SetZoomState(m_nZoomState - 1, 0, CPoint(0, 0));
}

void CPreviewView::OnUpdateNumPageChange(CCmdUI* pCmdUI) {
  CString str;
  AfxLoadString(m_nZoomOutPages == 1 ? AFX_IDS_TWOPAGE : AFX_IDS_ONEPAGE, str);
  pCmdUI->SetText(str);
  pCmdUI->Enable(m_nZoomState == ZOOM_OUT && m_nMaxPages > 1 &&
                 m_pPreviewInfo->GetMinPage() != m_pPreviewInfo->GetMaxPage());
}

void CPreviewView::OnUpdateNextPage(CCmdUI* pCmdUI) {
  pCmdUI->Enable(CanShowPage(m_nCurrentPage + m_nPages));
}

void CPreviewView::OnUpdatePrevPage(CCmdUI* pCmdUI) {
  pCmdUI->Enable(m_nCurrentPage > m_pPreviewInfo->GetMinPage());
}

void CPreviewView::OnUpdateZoomIn(CCmdUI* pCmdUI) { pCmdUI->Enable(m_nZoomState != ZOOM_IN); }

void CPreviewView::OnUpdateZoomOut(CCmdUI* pCmdUI) { pCmdUI->Enable(m_nZoomState != ZOOM_OUT); }
