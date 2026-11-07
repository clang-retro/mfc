// tracker.cpp - CRectTracker: a rectangle with a border and handles that
// the user moves and resizes with the mouse.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>

CRectTracker::CRectTracker() {
  Construct();
}

CRectTracker::CRectTracker(LPCRECT lpSrcRect, UINT nStyle) {
  Construct();
  m_rect.CopyRect(lpSrcRect);
  m_nStyle = nStyle;
}

CRectTracker::~CRectTracker() {}

void CRectTracker::Construct() {
  m_nStyle = 0;
  m_rect.SetRectEmpty();
  // The width of an in-place border, as the system sets it.
  m_nHandleSize = (int)::GetProfileIntA("windows", "oleinplaceborderwidth", 4);
  if (m_nHandleSize <= 0) m_nHandleSize = 4;
  m_sizeMin = CSize(m_nHandleSize * 2, m_nHandleSize * 2);
  m_bAllowInvert = FALSE;
  m_rectLast.SetRectEmpty();
  m_sizeLast = CSize(0, 0);
  m_bErase = FALSE;
  m_bFinalErase = FALSE;
}

// Handles inside a small rectangle shrink so that they do not overlap.
int CRectTracker::GetHandleSize(LPCRECT lpRect) const {
  CRect rect(lpRect != NULL ? *lpRect : (const RECT&)m_rect);
  rect.NormalizeRect();
  int nSize = m_nHandleSize;
  if (!(m_nStyle & resizeOutside)) {
    int nMax = min(rect.Width(), rect.Height()) / 2;
    if (nSize > nMax) nSize = max(nMax, 1);
  }
  return nSize;
}

UINT CRectTracker::GetHandleMask() const {
  UINT mask = 0xFF;  // the four corners and the four sides
  CRect rect(m_rect);
  rect.NormalizeRect();
  int nMin = GetHandleSize() * 3;
  if (rect.Width() < nMin) mask &= ~((1u << hitTop) | (1u << hitBottom));
  if (rect.Height() < nMin) mask &= ~((1u << hitLeft) | (1u << hitRight));
  return mask;
}

// The handle's square, for the rectangle as it is drawn (normalized).
void CRectTracker::GetHandleRect(int nHandle, CRect* pHandleRect) const {
  CRect rect(m_rect);
  rect.NormalizeRect();
  int nSize = GetHandleSize();
  // Outside handles sit beyond the edges; inside ones within them.
  int nOut = (m_nStyle & resizeOutside) ? nSize : 0;
  int xLeft = rect.left - nOut, xRight = rect.right + nOut - nSize;
  int yTop = rect.top - nOut, yBottom = rect.bottom + nOut - nSize;
  int xMid = rect.left + (rect.Width() - nSize) / 2;
  int yMid = rect.top + (rect.Height() - nSize) / 2;
  int x = 0, y = 0;
  switch (nHandle) {
  case hitTopLeft:
    x = xLeft, y = yTop;
    break;
  case hitTopRight:
    x = xRight, y = yTop;
    break;
  case hitBottomRight:
    x = xRight, y = yBottom;
    break;
  case hitBottomLeft:
    x = xLeft, y = yBottom;
    break;
  case hitTop:
    x = xMid, y = yTop;
    break;
  case hitRight:
    x = xRight, y = yMid;
    break;
  case hitBottom:
    x = xMid, y = yBottom;
    break;
  case hitLeft:
    x = xLeft, y = yMid;
    break;
  default:
    pHandleRect->SetRectEmpty();
    return;
  }
  pHandleRect->SetRect(x, y, x + nSize, y + nSize);
}

void CRectTracker::GetTrueRect(LPRECT lpTrueRect) const {
  CRect rect(m_rect);
  rect.NormalizeRect();
  int nSize = GetHandleSize();
  if ((m_nStyle & resizeOutside) || ((m_nStyle & hatchedBorder) && !(m_nStyle & hatchInside)))
    rect.InflateRect(nSize, nSize);
  *lpTrueRect = rect;
}

void CRectTracker::Draw(CDC* pDC) const {
  int nSaved = pDC->SaveDC();
  pDC->SetMapMode(MM_TEXT);
  pDC->SetViewportOrg(0, 0);
  pDC->SetWindowOrg(0, 0);

  CRect rect(m_rect);
  rect.NormalizeRect();
  int nSize = GetHandleSize();

  if (m_nStyle & (solidLine | dottedLine)) {
    CPen pen((m_nStyle & dottedLine) ? PS_DOT : PS_SOLID, 0, RGB(0, 0, 0));
    CPen* pOldPen = pDC->SelectObject(&pen);
    CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
    pDC->SetBkMode(TRANSPARENT);
    pDC->Rectangle(rect);
    pDC->SelectObject(pOldPen);
    pDC->SelectObject(pOldBrush);
  }

  if (m_nStyle & hatchedBorder) {
    CRect outer(rect), inner(rect);
    if (m_nStyle & hatchInside)
      inner.DeflateRect(nSize, nSize);
    else
      outer.InflateRect(nSize, nSize);
    CBrush brush(HS_BDIAGONAL, RGB(0, 0, 0));
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetBrushOrg(outer.left % 8, outer.top % 8);
    pDC->FillRect(CRect(outer.left, outer.top, outer.right, inner.top), &brush);
    pDC->FillRect(CRect(outer.left, inner.bottom, outer.right, outer.bottom), &brush);
    pDC->FillRect(CRect(outer.left, inner.top, inner.left, inner.bottom), &brush);
    pDC->FillRect(CRect(inner.right, inner.top, outer.right, inner.bottom), &brush);
  }

  if (m_nStyle & (resizeInside | resizeOutside)) {
    UINT mask = GetHandleMask();
    for (int i = 0; i < 8; i++) {
      if (!(mask & (1u << i))) continue;
      CRect rectHandle;
      GetHandleRect(i, &rectHandle);
      pDC->FillSolidRect(rectHandle, RGB(0, 0, 0));
    }
  }
  pDC->RestoreDC(nSaved);
}

// A handle of m_rect as it stands (its corners may be swapped) and the
// same handle of the drawn, normalized rectangle are each other's
// mirror images along the inverted axes.
int CRectTracker::NormalizeHit(int nHandle) const {
  if (nHandle < 0 || nHandle == hitMiddle) return nHandle;
  BOOL bInvertX = m_rect.right < m_rect.left;
  BOOL bInvertY = m_rect.bottom < m_rect.top;
  if (bInvertX) {
    static const int swapX[8] = {hitTopRight, hitTopLeft, hitBottomLeft, hitBottomRight,
                                 hitTop, hitLeft, hitBottom, hitRight};
    nHandle = swapX[nHandle];
  }
  if (bInvertY) {
    static const int swapY[8] = {hitBottomLeft, hitBottomRight, hitTopRight, hitTopLeft,
                                 hitBottom, hitRight, hitTop, hitLeft};
    nHandle = swapY[nHandle];
  }
  return nHandle;
}

int CRectTracker::HitTestHandles(CPoint point) const {
  if (m_nStyle & (resizeInside | resizeOutside)) {
    UINT mask = GetHandleMask();
    for (int i = 0; i < 8; i++) {
      if (!(mask & (1u << i))) continue;
      CRect rectHandle;
      GetHandleRect(i, &rectHandle);
      if (rectHandle.PtInRect(point)) return NormalizeHit(i);
    }
  }
  CRect rectTrue;
  GetTrueRect(&rectTrue);
  return rectTrue.PtInRect(point) ? (int)hitMiddle : (int)hitNothing;
}

int CRectTracker::HitTest(CPoint point) const { return HitTestHandles(point); }

BOOL CRectTracker::SetCursor(CWnd* pWnd, UINT nHitTest) const {
  if (nHitTest != HTCLIENT) return FALSE;
  CPoint point;
  ::GetCursorPos(&point);
  pWnd->ScreenToClient(&point);
  int nHandle = HitTest(point);
  if (nHandle < 0) return FALSE;
  nHandle = NormalizeHit(nHandle);
  LPCTSTR lpszCursor = IDC_SIZEALL;
  switch (nHandle) {
  case hitTopLeft:
  case hitBottomRight:
    lpszCursor = IDC_SIZENWSE;
    break;
  case hitTopRight:
  case hitBottomLeft:
    lpszCursor = IDC_SIZENESW;
    break;
  case hitTop:
  case hitBottom:
    lpszCursor = IDC_SIZENS;
    break;
  case hitLeft:
  case hitRight:
    lpszCursor = IDC_SIZEWE;
    break;
  default:
    break;
  }
  ::SetCursor(::LoadCursor(NULL, lpszCursor));
  return TRUE;
}

// The edges of m_rect a handle moves; px and py give their values.
void CRectTracker::GetModifyPointers(int nHandle, int** ppx, int** ppy, int* px, int* py) {
  *ppx = NULL;
  *ppy = NULL;
  switch (nHandle) {
  case hitTopLeft:
  case hitMiddle:
    *ppx = (int*)&m_rect.left, *ppy = (int*)&m_rect.top;
    break;
  case hitTopRight:
    *ppx = (int*)&m_rect.right, *ppy = (int*)&m_rect.top;
    break;
  case hitBottomRight:
    *ppx = (int*)&m_rect.right, *ppy = (int*)&m_rect.bottom;
    break;
  case hitBottomLeft:
    *ppx = (int*)&m_rect.left, *ppy = (int*)&m_rect.bottom;
    break;
  case hitTop:
    *ppy = (int*)&m_rect.top;
    break;
  case hitRight:
    *ppx = (int*)&m_rect.right;
    break;
  case hitBottom:
    *ppy = (int*)&m_rect.bottom;
    break;
  case hitLeft:
    *ppx = (int*)&m_rect.left;
    break;
  default:
    break;
  }
  if (px != NULL) *px = *ppx != NULL ? **ppx : 0;
  if (py != NULL) *py = *ppy != NULL ? **ppy : 0;
}

// Keeps the rectangle at least m_sizeMin across, and the right way round
// unless inverting is allowed.
void CRectTracker::AdjustRect(int nHandle, LPRECT lpRect) {
  if (nHandle == hitMiddle) return;
  int *px, *py;
  GetModifyPointers(nHandle, &px, &py, NULL, NULL);
  // px and py point into m_rect; work on lpRect's matching fields.
  if (px != NULL) {
    LONG* pMoving = px == (int*)&m_rect.left ? &lpRect->left : &lpRect->right;
    LONG* pFixed = px == (int*)&m_rect.left ? &lpRect->right : &lpRect->left;
    LONG nSign = pMoving == &lpRect->right ? 1 : -1;  // which way the moving edge grows
    LONG nWidth = (*pMoving - *pFixed) * nSign;
    if (!m_bAllowInvert && nWidth < m_sizeMin.cx)
      *pMoving = *pFixed + nSign * m_sizeMin.cx;
    else if (m_bAllowInvert && nWidth > -m_sizeMin.cx && nWidth < m_sizeMin.cx)
      *pMoving = *pFixed + nSign * (nWidth < 0 ? -m_sizeMin.cx : m_sizeMin.cx);
  }
  if (py != NULL) {
    LONG* pMoving = py == (int*)&m_rect.top ? &lpRect->top : &lpRect->bottom;
    LONG* pFixed = py == (int*)&m_rect.top ? &lpRect->bottom : &lpRect->top;
    LONG nSign = pMoving == &lpRect->bottom ? 1 : -1;
    LONG nHeight = (*pMoving - *pFixed) * nSign;
    if (!m_bAllowInvert && nHeight < m_sizeMin.cy)
      *pMoving = *pFixed + nSign * m_sizeMin.cy;
    else if (m_bAllowInvert && nHeight > -m_sizeMin.cy && nHeight < m_sizeMin.cy)
      *pMoving = *pFixed + nSign * (nHeight < 0 ? -m_sizeMin.cy : m_sizeMin.cy);
  }
}

void CRectTracker::OnChangedRect(const CRect&) {}

// The feedback while tracking: a dotted rectangle drawn with XOR, so
// that drawing it again takes it away.
void CRectTracker::DrawTrackerRect(LPCRECT lpRect, CWnd* pWndClipTo, CDC* pDC, CWnd* pWnd) {
  CRect rect(lpRect);
  rect.NormalizeRect();
  if (pWndClipTo != NULL) {
    pWnd->ClientToScreen(&rect);
    pWndClipTo->ScreenToClient(&rect);
  }
  if (m_sizeLast.cx != 0) {
    pDC->DrawFocusRect(m_rectLast);
    m_sizeLast = CSize(0, 0);
  }
  if (m_bErase) return;
  pDC->DrawFocusRect(rect);
  m_rectLast = rect;
  m_sizeLast = CSize(1, 1);
}

BOOL CRectTracker::Track(CWnd* pWnd, CPoint point, BOOL bAllowInvert, CWnd* pWndClipTo) {
  int nHandle = HitTest(point);
  if (nHandle < 0) return FALSE;
  m_bAllowInvert = bAllowInvert;
  return TrackHandle(nHandle, pWnd, point, pWndClipTo);
}

BOOL CRectTracker::TrackRubberBand(CWnd* pWnd, CPoint point, BOOL bAllowInvert) {
  m_bAllowInvert = bAllowInvert;
  m_rect.SetRect(point.x, point.y, point.x, point.y);
  // A rubber band starts from nothing: no smallest size.
  CSize sizeMin = m_sizeMin;
  m_sizeMin = CSize(0, 0);
  BOOL bResult = TrackHandle(hitBottomRight, pWnd, point, NULL);
  m_sizeMin = sizeMin;
  return bResult;
}

// Follows the mouse until the button goes up (TRUE when the rectangle
// changed) or Escape or the right button cancels.
BOOL CRectTracker::TrackHandle(int nHandle, CWnd* pWnd, CPoint point, CWnd* pWndClipTo) {
  if (::GetCapture() != NULL) return FALSE;

  CRect rectSave(m_rect);
  int nWidth = m_rect.Width(), nHeight = m_rect.Height();
  int *px, *py;
  int xEdge, yEdge;
  GetModifyPointers(nHandle, &px, &py, &xEdge, &yEdge);
  CPoint offset(px != NULL ? point.x - xEdge : 0, py != NULL ? point.y - yEdge : 0);

  pWnd->SetCapture();
  pWnd->UpdateWindow();
  CDC* pDrawDC = pWndClipTo != NULL ? pWndClipTo->GetDCEx(NULL, DCX_CACHE) : pWnd->GetDC();
  m_sizeLast = CSize(0, 0);
  m_bErase = FALSE;
  m_bFinalErase = FALSE;

  BOOL bMoved = FALSE;
  BOOL bCancelled = FALSE;
  BOOL bDone = FALSE;
  while (!bDone) {
    MSG msg;
    if (!::GetMessage(&msg, NULL, 0, 0)) {
      AfxPostQuitMessage((int)msg.wParam);
      bCancelled = TRUE;
      break;
    }
    if (::GetCapture() != pWnd->m_hWnd) {
      bCancelled = TRUE;
      break;
    }
    switch (msg.message) {
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE: {
      CRect rectOld(m_rect);
      CPoint pt((short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam));
      if (msg.hwnd != pWnd->m_hWnd) ::MapWindowPoints(msg.hwnd, pWnd->m_hWnd, &pt, 1);
      if (px != NULL) *px = pt.x - offset.x;
      if (py != NULL) *py = pt.y - offset.y;
      if (nHandle == hitMiddle) {
        m_rect.right = m_rect.left + nWidth;
        m_rect.bottom = m_rect.top + nHeight;
      }
      AdjustRect(nHandle, &m_rect);
      m_bFinalErase = msg.message == WM_LBUTTONUP;
      if (!rectOld.EqualRect(&m_rect) || m_bFinalErase) {
        if (bMoved) {
          m_bErase = TRUE;
          DrawTrackerRect(&rectOld, pWndClipTo, pDrawDC, pWnd);
        }
        OnChangedRect(rectOld);
        if (!m_bFinalErase) bMoved = TRUE;
      }
      if (m_bFinalErase) {
        bDone = TRUE;
        break;
      }
      if (!rectOld.EqualRect(&m_rect)) {
        m_bErase = FALSE;
        DrawTrackerRect(&m_rect, pWndClipTo, pDrawDC, pWnd);
      }
      break;
    }
    case WM_KEYDOWN:
      if (msg.wParam != VK_ESCAPE) break;
      // fallthrough
    case WM_RBUTTONDOWN:
      if (bMoved) {
        m_bErase = m_bFinalErase = TRUE;
        DrawTrackerRect(&m_rect, pWndClipTo, pDrawDC, pWnd);
      }
      bCancelled = TRUE;
      bDone = TRUE;
      break;
    default:
      ::DispatchMessage(&msg);
      break;
    }
  }

  if (pWndClipTo != NULL)
    pWndClipTo->ReleaseDC(pDrawDC);
  else
    pWnd->ReleaseDC(pDrawDC);
  ::ReleaseCapture();

  if (bCancelled || !bMoved) m_rect = rectSave;
  if (!m_bAllowInvert) m_rect.NormalizeRect();
  m_bFinalErase = FALSE;
  m_bErase = FALSE;
  return !rectSave.EqualRect(&m_rect);
}
