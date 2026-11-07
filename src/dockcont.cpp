// dockcont.cpp - CDockContext: dragging a control bar to dock or float it.
//
// While the mouse button is down the bar stays where it is and an
// outline drawn over the screen follows the pointer: a thin one in the
// shape the bar would have docked on the side under it, or a thick one
// in the shape of the floating frame. Releasing the button moves the bar
// there.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxext.h>
#include <afxpriv.h>

static const int CX_THIN = 1;   // outline where the bar would dock
static const int CX_THICK = 3;  // outline where it would float

CDockContext::CDockContext(CControlBar* pBar) {
  m_pBar = pBar;
  m_pDockSite = NULL;
  m_dwDockStyle = 0;
  m_dwOverDockStyle = 0;
  m_dwStyle = 0;
  m_bFlip = FALSE;
  m_bForceFrame = FALSE;
  m_bDragging = FALSE;
  m_bMoved = FALSE;
  m_rectLast.SetRectEmpty();
  m_sizeLast = CSize(0, 0);
  m_bDitherLast = FALSE;
  m_pDC = NULL;
  m_uMRUDockID = 0;
  m_rectMRUDockPos.SetRectEmpty();
  m_dwMRUFloatStyle = 0;
  m_ptMRUFloatPos = CPoint(0, 0);
}

CDockContext::~CDockContext() {
  if (m_bDragging) CancelLoop();
}

// Measuring a toolbar in another shape changes its buttons, so put them
// back the way the bar is laid out now.
void CDockContext::_RestoreLayout() {
  if (m_pBar->IsKindOf(RUNTIME_CLASS(CToolBar))) {
    ((CToolBar*)m_pBar)->Layout();
    m_pBar->Invalidate();
  }
}

// Places r (of its own size) so the pointer is at the same spot in it,
// or in its middle when it would fall outside.
static void _FitToPoint(CRect& r, CPoint pt, CPoint ptOffset) {
  int cx = r.Width(), cy = r.Height();
  int ox = ptOffset.x, oy = ptOffset.y;
  if (ox < 0 || ox >= cx) ox = cx / 2;
  if (oy < 0 || oy >= cy) oy = cy / 2;
  r.SetRect(pt.x - ox, pt.y - oy, pt.x - ox + cx, pt.y - oy + cy);
}

void CDockContext::StartDrag(CPoint pt) {
  m_pDockSite = m_pBar->GetDockingFrame();
  if (m_pDockSite == NULL) return;
  m_dwDockStyle = m_pBar->m_dwDockStyle;
  m_dwStyle = m_pBar->m_dwStyle;
  InitLoop();

  BOOL bFloating = m_pBar->IsFloating();
  BOOL bDynamic = (m_dwStyle & CBRS_SIZE_DYNAMIC) != 0;
  CRect rectBar;
  m_pBar->GetWindowRect(&rectBar);
  CPoint ptOffset = pt - rectBar.TopLeft();

  // The shapes the bar would take docked across the top or bottom, down
  // a side, and floating.
  CSize sizeHorz, sizeVert, sizeFloatHorz, sizeFloatVert;
  if (bDynamic) {
    sizeHorz = m_pBar->CalcDynamicLayout(-1, LM_HORZ | LM_HORZDOCK);
    sizeVert = m_pBar->CalcDynamicLayout(-1, LM_VERTDOCK);
    sizeFloatHorz = sizeFloatVert = m_pBar->CalcDynamicLayout(-1, LM_HORZ | LM_MRUWIDTH);
  } else {
    sizeHorz = m_pBar->CalcFixedLayout(TRUE, TRUE);
    sizeVert = m_pBar->CalcFixedLayout(TRUE, FALSE);
    sizeFloatHorz = m_pBar->CalcFixedLayout(FALSE, TRUE);
    sizeFloatVert = m_pBar->CalcFixedLayout(FALSE, FALSE);
  }
  _RestoreLayout();

  // A bar that asks for the whole length gets the frame's.
  CRect rectSite;
  m_pDockSite->GetClientRect(&rectSite);
  sizeHorz.cx = min(sizeHorz.cx, (LONG)rectSite.Width());
  sizeVert.cy = min(sizeVert.cy, (LONG)rectSite.Height());

  m_rectDragHorz = CRect(rectBar.TopLeft(), sizeHorz);
  m_rectDragVert = CRect(rectBar.TopLeft(), sizeVert);
  _FitToPoint(m_rectDragHorz, pt, ptOffset);
  _FitToPoint(m_rectDragVert, pt, ptOffset);

  DWORD dwFrameStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | (bDynamic ? WS_THICKFRAME : 0);
  if (bFloating) {
    CRect rectFrame;
    m_pBar->m_pDockBar->GetParent()->GetWindowRect(&rectFrame);
    m_rectFrameDragHorz = m_rectFrameDragVert = rectFrame;
  } else {
    CRect rectH(0, 0, sizeFloatHorz.cx, sizeFloatHorz.cy);
    CRect rectV(0, 0, sizeFloatVert.cx, sizeFloatVert.cy);
    CMiniFrameWnd::CalcBorders(&rectH, dwFrameStyle);
    CMiniFrameWnd::CalcBorders(&rectV, dwFrameStyle);
    m_rectFrameDragHorz = CRect(rectBar.TopLeft(), rectH.Size());
    m_rectFrameDragVert = CRect(rectBar.TopLeft(), rectV.Size());
    _FitToPoint(m_rectFrameDragHorz, pt, ptOffset);
    _FitToPoint(m_rectFrameDragVert, pt, ptOffset);
  }

  m_ptLast = pt;
  m_dwOverDockStyle = m_bForceFrame ? 0 : CanDock();
  DrawFocusRect();
  Track();
}

void CDockContext::InitLoop() {
  m_bDragging = TRUE;
  m_bMoved = FALSE;
  m_bFlip = FALSE;
  m_bForceFrame = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
  m_rectLast.SetRectEmpty();
  m_sizeLast = CSize(0, 0);
  m_bDitherLast = FALSE;
  // Draw over everything on the screen.
  HDC hDC = ::GetDCEx(::GetDesktopWindow(), NULL, DCX_WINDOW | DCX_CACHE | DCX_LOCKWINDOWUPDATE);
  m_pDC = new CDC;
  m_pDC->Attach(hDC);
}

void CDockContext::CancelLoop() {
  DrawFocusRect(TRUE);
  if (CWnd::GetCapture() == m_pBar) ::ReleaseCapture();
  if (m_pDC != NULL) {
    HDC hDC = m_pDC->Detach();
    ::ReleaseDC(::GetDesktopWindow(), hDC);
    delete m_pDC;
    m_pDC = NULL;
  }
  m_bDragging = FALSE;
}

BOOL CDockContext::Track() {
  m_pBar->SetCapture();
  while (CWnd::GetCapture() == m_pBar) {
    MSG msg;
    if (!::GetMessage(&msg, NULL, 0, 0)) {
      AfxPostQuitMessage((int)msg.wParam);
      break;
    }
    switch (msg.message) {
      case WM_LBUTTONUP: {
        CPoint pt((short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam));
        ::ClientToScreen(msg.hwnd, &pt);
        if (pt != m_ptLast) Move(pt);
        EndDrag();
        return TRUE;
      }
      case WM_MOUSEMOVE: {
        CPoint pt((short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam));
        ::ClientToScreen(msg.hwnd, &pt);
        Move(pt);
        break;
      }
      case WM_KEYDOWN:
      case WM_KEYUP:
        if (msg.wParam == VK_ESCAPE && msg.message == WM_KEYDOWN) {
          CancelLoop();
          return FALSE;
        }
        OnKey((int)msg.wParam, msg.message == WM_KEYDOWN);
        break;
      case WM_RBUTTONDOWN:
        CancelLoop();
        return FALSE;
      default:
        ::DispatchMessage(&msg);
        break;
    }
  }
  CancelLoop();
  return FALSE;
}

void CDockContext::Move(CPoint pt) {
  CPoint ptOffset = pt - m_ptLast;
  if (ptOffset == CPoint(0, 0)) return;
  m_bMoved = TRUE;
  m_rectDragHorz.OffsetRect(ptOffset);
  m_rectDragVert.OffsetRect(ptOffset);
  m_rectFrameDragHorz.OffsetRect(ptOffset);
  m_rectFrameDragVert.OffsetRect(ptOffset);
  m_ptLast = pt;
  m_bForceFrame = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
  m_dwOverDockStyle = m_bForceFrame ? 0 : CanDock();
  DrawFocusRect();
}

void CDockContext::OnKey(int nChar, BOOL bDown) {
  if (nChar == VK_CONTROL) UpdateState(&m_bForceFrame, bDown);
}

void CDockContext::UpdateState(BOOL* pFlag, BOOL bNewValue) {
  if (*pFlag == bNewValue) return;
  *pFlag = bNewValue;
  m_dwOverDockStyle = m_bForceFrame ? 0 : CanDock();
  DrawFocusRect();
}

// The side the outline is over, preferring the bar's shape for it.
DWORD CDockContext::CanDock() {
  DWORD dwDock = m_pDockSite->CanDock(m_rectDragHorz, m_dwDockStyle & CBRS_ORIENT_HORZ);
  if (dwDock != 0) return dwDock;
  return m_pDockSite->CanDock(m_rectDragVert, m_dwDockStyle & CBRS_ORIENT_VERT);
}

CDockBar* CDockContext::GetDockBar(DWORD dwOverDockStyle) {
  CDockBar* pDockBar = NULL;
  if (dwOverDockStyle & CBRS_ORIENT_HORZ)
    m_pDockSite->CanDock(m_rectDragHorz, dwOverDockStyle & CBRS_ORIENT_HORZ, &pDockBar);
  else if (dwOverDockStyle & CBRS_ORIENT_VERT)
    m_pDockSite->CanDock(m_rectDragVert, dwOverDockStyle & CBRS_ORIENT_VERT, &pDockBar);
  return pDockBar;
}

void CDockContext::DrawFocusRect(BOOL bRemoveRect) {
  if (m_pDC == NULL) return;
  CRect rect;
  CSize size;
  BOOL bDither;
  if (m_dwOverDockStyle & CBRS_ORIENT_HORZ) {
    rect = m_rectDragHorz;
    size = CSize(CX_THIN, CX_THIN);
    bDither = FALSE;
  } else if (m_dwOverDockStyle & CBRS_ORIENT_VERT) {
    rect = m_rectDragVert;
    size = CSize(CX_THIN, CX_THIN);
    bDither = FALSE;
  } else {
    rect = (m_dwStyle & CBRS_ORIENT_VERT) && !(m_dwStyle & CBRS_SIZE_DYNAMIC) ? m_rectFrameDragVert
                                                                              : m_rectFrameDragHorz;
    size = CSize(CX_THICK, CX_THICK);
    bDither = TRUE;
  }
  if (bRemoveRect) size = CSize(0, 0);

  CBrush* pWhite = CBrush::FromHandle((HBRUSH)::GetStockObject(WHITE_BRUSH));
  CBrush* pDither = CDC::GetHalftoneBrush();
  CBrush* pBrush = bDither ? pDither : pWhite;
  CBrush* pBrushLast = m_bDitherLast ? pDither : pWhite;
  if (m_rectLast.IsRectEmpty() && m_sizeLast == CSize(0, 0))
    m_pDC->DrawDragRect(&rect, size, NULL, CSize(0, 0), pBrush, pBrush);
  else
    m_pDC->DrawDragRect(&rect, size, &m_rectLast, m_sizeLast, pBrush, pBrushLast);
  m_rectLast = rect;
  m_sizeLast = size;
  m_bDitherLast = bDither;
}

void CDockContext::EndDrag() {
  CancelLoop();
  if (!m_bMoved) return;  // a click, not a drag

  if (m_dwOverDockStyle & CBRS_ALIGN_ANY) {
    CDockBar* pDockBar = GetDockBar(m_dwOverDockStyle);
    if (pDockBar == NULL) return;
    CRect rect = (m_dwOverDockStyle & CBRS_ORIENT_HORZ) ? m_rectDragHorz : m_rectDragVert;
    m_pDockSite->DockControlBar(m_pBar, pDockBar, &rect);
    m_rectMRUDockPos = rect;
    pDockBar->ScreenToClient(&m_rectMRUDockPos);
  } else {
    BOOL bVert = (m_dwStyle & CBRS_ORIENT_VERT) && !(m_dwStyle & CBRS_SIZE_DYNAMIC);
    CRect rect = bVert ? m_rectFrameDragVert : m_rectFrameDragHorz;
    DWORD dwFloat = bVert ? CBRS_ALIGN_LEFT : CBRS_ALIGN_TOP;
    if (m_pBar->IsFloating()) dwFloat = m_pBar->m_pDockBar->m_dwStyle & CBRS_ALIGN_ANY;
    m_pDockSite->FloatControlBar(m_pBar, rect.TopLeft(), dwFloat | (m_dwDockStyle & CBRS_FLOAT_MULTI));
  }
  m_pDockSite->RecalcLayout();
}

// Resizing a floating bar: the frame's own sizing border does it, and
// the frame snaps to the bar's layouts as it goes.
void CDockContext::StartResize(int nHitTest, CPoint pt) {
  if (!m_pBar->IsFloating()) return;
  CWnd* pFrame = m_pBar->m_pDockBar->GetParent();
  if (nHitTest < HTSIZEFIRST || nHitTest > HTSIZELAST) return;
  pFrame->SendMessage(WM_SYSCOMMAND, SC_SIZE + (nHitTest - HTSIZEFIRST + 1),
                      MAKELPARAM(pt.x, pt.y));
}

// Docks a floating bar where it was last docked, or floats a docked bar
// where it last floated (or where it is, the first time).
void CDockContext::ToggleDocking() {
  m_pDockSite = m_pBar->GetDockingFrame();
  if (m_pDockSite == NULL) return;
  if (m_pBar->IsFloating()) {
    CDockBar* pDockBar = NULL;
    if (m_uMRUDockID != 0) {
      CControlBar* p = m_pDockSite->GetControlBar(m_uMRUDockID);
      if (p != NULL && p->IsDockBar() && (p->m_dwStyle & m_pBar->m_dwDockStyle & CBRS_ALIGN_ANY))
        pDockBar = (CDockBar*)p;
    }
    if (pDockBar != NULL && !m_rectMRUDockPos.IsRectEmpty()) {
      CRect rect = m_rectMRUDockPos;
      pDockBar->ClientToScreen(&rect);
      m_pDockSite->DockControlBar(m_pBar, pDockBar, &rect);
    } else if (pDockBar != NULL) {
      m_pDockSite->DockControlBar(m_pBar, pDockBar);
    } else {
      m_pDockSite->DockControlBar(m_pBar);
    }
  } else {
    CPoint pt = m_ptMRUFloatPos;
    DWORD dwStyle = m_dwMRUFloatStyle;
    if (dwStyle == 0) {
      CRect rect;
      m_pBar->GetWindowRect(&rect);
      pt = rect.TopLeft();
      dwStyle = CBRS_ALIGN_TOP;
    }
    m_pDockSite->FloatControlBar(m_pBar, pt, dwStyle | (m_pBar->m_dwDockStyle & CBRS_FLOAT_MULTI));
  }
  m_pDockSite->RecalcLayout();
}
