// fpbutton.cpp - the Feature Pack's buttons: CMFCButton, CMFCLinkCtrl,
// CMFCMenuButton and CMFCColorButton (with its popup palette).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxbutton.h>
#include <afxlinkctrl.h>
#include <afxmenubutton.h>
#include <afxcolorbutton.h>
#include <afxdlgs.h>
#include <shellapi.h>

#ifndef WM_MOUSELEAVE
#define WM_MOUSELEAVE 0x02A3
#endif

// ---------------------------------------------------------------------
// CMFCButton

IMPLEMENT_DYNCREATE(CMFCButton, CButton)

BOOL CMFCButton::m_bWinXPThemeAll = TRUE;

BEGIN_MESSAGE_MAP(CMFCButton, CButton)
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_CANCELMODE()
  ON_WM_CAPTURECHANGED()
  ON_WM_SETCURSOR()
  ON_WM_ERASEBKGND()
  ON_WM_ENABLE()
  ON_WM_KILLFOCUS()
  ON_WM_KEYDOWN()
  ON_WM_KEYUP()
  ON_WM_GETDLGCODE()
  ON_MESSAGE(BM_SETCHECK, OnSetCheck)
  ON_MESSAGE(BM_GETCHECK, OnGetCheck)
  ON_MESSAGE(BM_SETSTYLE, OnSetStyle)
  ON_MESSAGE(WM_SETTEXT, OnSetText)
  ON_WM_DESTROY()
END_MESSAGE_MAP()

void CMFCButton::CImageSlot::Free() {
  if (bAutoDestroy) {
    if (hIcon != NULL) ::DestroyIcon(hIcon);
    if (hBitmap != NULL) ::DeleteObject(hBitmap);
  }
  hIcon = NULL;
  hBitmap = NULL;
  bAutoDestroy = FALSE;
}

CSize CMFCButton::CImageSlot::GetSize() const {
  if (hBitmap != NULL) {
    BITMAP bm;
    if (::GetObject(hBitmap, sizeof(bm), &bm)) return CSize(bm.bmWidth, bm.bmHeight);
  }
  if (hIcon != NULL) {
    ICONINFO ii;
    if (::GetIconInfo(hIcon, &ii)) {
      BITMAP bm;
      CSize size(16, 16);
      if (ii.hbmColor != NULL && ::GetObject(ii.hbmColor, sizeof(bm), &bm))
        size = CSize(bm.bmWidth, bm.bmHeight);
      else if (ii.hbmMask != NULL && ::GetObject(ii.hbmMask, sizeof(bm), &bm))
        size = CSize(bm.bmWidth, bm.bmHeight / 2);
      if (ii.hbmColor != NULL) ::DeleteObject(ii.hbmColor);
      if (ii.hbmMask != NULL) ::DeleteObject(ii.hbmMask);
      return size;
    }
  }
  return CSize(0, 0);
}

void CMFCButton::CImageSlot::Draw(CDC* pDC, const CPoint& pt, BOOL bDisabled) const {
  CSize size = GetSize();
  if (hIcon != NULL) {
    if (bDisabled)
      pDC->DrawState(pt, size, hIcon, DSS_DISABLED, (CBrush*)NULL);
    else
      ::DrawIconEx(pDC->GetSafeHdc(), pt.x, pt.y, hIcon, size.cx, size.cy, 0, NULL, DI_NORMAL);
  } else if (hBitmap != NULL) {
    if (bDisabled) {
      pDC->DrawState(pt, size, hBitmap, DSS_DISABLED, (CBrush*)NULL);
    } else {
      CDC dcMem;
      dcMem.CreateCompatibleDC(pDC);
      HGDIOBJ hOld = ::SelectObject(dcMem.GetSafeHdc(), hBitmap);
      // The top-left pixel's color is transparent, as with toolbar images.
      COLORREF clrTransparent = dcMem.GetPixel(0, 0);
      pDC->TransparentBlt(pt.x, pt.y, size.cx, size.cy, &dcMem, 0, 0, size.cx, size.cy, clrTransparent);
      ::SelectObject(dcMem.GetSafeHdc(), hOld);
    }
  }
}

CMFCButton::CMFCButton()
    : m_nFlatStyle(BUTTONSTYLE_3D), m_nAlignStyle(ALIGN_CENTER), m_bRightImage(FALSE), m_bTopImage(FALSE),
      m_bTransparent(FALSE), m_bDrawFocus(TRUE), m_bHighlightChecked(TRUE), m_bGrayDisabled(TRUE),
      m_bResponseOnButtonDown(FALSE), m_bDontUseWinXPTheme(FALSE), m_bWasDblClk(FALSE), m_hCursor(NULL),
      m_clrRegular((COLORREF)-1), m_clrHover((COLORREF)-1), m_clrFace((COLORREF)-1), m_hbrFace(NULL),
      m_bHighlighted(FALSE), m_bPushed(FALSE), m_bClickiedInside(FALSE), m_bCaptured(FALSE), m_bKeyDown(FALSE),
      m_bChecked(FALSE), m_bIndeterminate(FALSE), m_bCheckButton(FALSE), m_bRadioButton(FALSE),
      m_bAutoCheck(FALSE), m_b3State(FALSE), m_bDefaultButton(FALSE), m_bMenuFont(FALSE),
      m_bFullTextTooltip(FALSE), m_bInSetStyle(FALSE), m_sizeImage(0, 0) {}

CMFCButton::~CMFCButton() { CleanUp(); }

void CMFCButton::CleanUp() {
  for (int i = 0; i < 3; i++) {
    m_Image[i].Free();
    m_ImageChecked[i].Free();
  }
  m_sizeImage = CSize(0, 0);
  if (m_hbrFace != NULL) ::DeleteObject(m_hbrFace);
  m_hbrFace = NULL;
}

void CMFCButton::_InitStyle(DWORD dwStyle) {
  switch (dwStyle & BS_TYPEMASK) {
    case BS_CHECKBOX:
      m_bCheckButton = TRUE;
      break;
    case BS_AUTOCHECKBOX:
      m_bCheckButton = m_bAutoCheck = TRUE;
      break;
    case BS_3STATE:
      m_bCheckButton = m_b3State = TRUE;
      break;
    case BS_AUTO3STATE:
      m_bCheckButton = m_b3State = m_bAutoCheck = TRUE;
      break;
    case BS_RADIOBUTTON:
      m_bRadioButton = TRUE;
      break;
    case BS_AUTORADIOBUTTON:
      m_bRadioButton = m_bAutoCheck = TRUE;
      break;
    case BS_DEFPUSHBUTTON:
      m_bDefaultButton = TRUE;
      break;
  }
  // A check box's or radio button's text goes beside its mark.
  if ((m_bCheckButton || m_bRadioButton) && m_nAlignStyle == ALIGN_CENTER) {
    DWORD dwAlign = dwStyle & BS_CENTER;
    m_nAlignStyle = dwAlign == BS_CENTER ? ALIGN_CENTER : dwAlign == BS_RIGHT ? ALIGN_RIGHT : ALIGN_LEFT;
  }
}

BOOL CMFCButton::PreCreateWindow(CREATESTRUCT& cs) {
  _InitStyle(cs.style);
  cs.style = (cs.style & ~BS_TYPEMASK) | BS_OWNERDRAW;
  return CButton::PreCreateWindow(cs);
}

void CMFCButton::PreSubclassWindow() {
  DWORD dwStyle = GetStyle();
  if ((dwStyle & BS_TYPEMASK) != BS_OWNERDRAW) {
    _InitStyle(dwStyle);
    // The check state the control had before it became owner drawn.
    if (m_bCheckButton || m_bRadioButton) {
      LRESULT l = ::CallWindowProc(m_pfnSuper != NULL ? m_pfnSuper : (WNDPROC)::GetWindowLongPtr(m_hWnd, GWLP_WNDPROC),
                                   m_hWnd, BM_GETCHECK, 0, 0);
      m_bChecked = l != BST_UNCHECKED;
      m_bIndeterminate = l == BST_INDETERMINATE;
    }
    m_bInSetStyle = TRUE;
    ::SendMessage(m_hWnd, BM_SETSTYLE, (dwStyle & ~BS_TYPEMASK) | BS_OWNERDRAW, FALSE);
    m_bInSetStyle = FALSE;
  }
  CButton::PreSubclassWindow();
}

LRESULT CMFCButton::OnSetStyle(WPARAM wParam, LPARAM lParam) {
  if (m_bInSetStyle) return Default();
  // The dialog manager making this the default button (or not): keep
  // it owner drawn.
  UINT uType = (UINT)wParam & BS_TYPEMASK;
  if (uType == BS_DEFPUSHBUTTON || uType == BS_PUSHBUTTON) {
    m_bDefaultButton = uType == BS_DEFPUSHBUTTON;
    m_bInSetStyle = TRUE;
    LRESULT l = DefWindowProc(BM_SETSTYLE, ((UINT)wParam & ~BS_TYPEMASK) | BS_OWNERDRAW, lParam);
    m_bInSetStyle = FALSE;
    Invalidate();
    return l;
  }
  return Default();
}

LRESULT CMFCButton::OnSetText(WPARAM, LPARAM) {
  LRESULT l = Default();
  Invalidate();
  return l;
}

void CMFCButton::_SetImage(CImageSlot* slots, HICON hIcon, BOOL bAutoDestroy, HICON hIconHot,
                           HICON hIconDisabled) {
  HICON icons[3] = {hIcon, hIconHot, hIconDisabled};
  for (int i = 0; i < 3; i++) {
    slots[i].Free();
    slots[i].hIcon = icons[i];
    slots[i].bAutoDestroy = bAutoDestroy;
  }
  m_sizeImage = GetImageSize();
  if (m_hWnd != NULL) Invalidate();
}

void CMFCButton::_SetImage(CImageSlot* slots, HBITMAP hBitmap, BOOL bAutoDestroy, HBITMAP hBitmapHot,
                           HBITMAP hBitmapDisabled) {
  HBITMAP bitmaps[3] = {hBitmap, hBitmapHot, hBitmapDisabled};
  for (int i = 0; i < 3; i++) {
    slots[i].Free();
    slots[i].hBitmap = bitmaps[i];
    slots[i].bAutoDestroy = bAutoDestroy;
  }
  m_sizeImage = GetImageSize();
  if (m_hWnd != NULL) Invalidate();
}

void CMFCButton::SetImage(HICON hIcon, BOOL bAutoDestroy, HICON hIconHot, HICON hIconDisabled, BOOL) {
  _SetImage(m_Image, hIcon, bAutoDestroy, hIconHot, hIconDisabled);
}
void CMFCButton::SetImage(HBITMAP hBitmap, BOOL bAutoDestroy, HBITMAP hBitmapHot, BOOL, HBITMAP hBitmapDisabled) {
  _SetImage(m_Image, hBitmap, bAutoDestroy, hBitmapHot, hBitmapDisabled);
}

static HBITMAP _AfxLoadButtonBitmap(UINT uiResId) {
  if (uiResId == 0) return NULL;
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(uiResId), RT_BITMAP);
  return (HBITMAP)::LoadImage(hInst, MAKEINTRESOURCE(uiResId), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
}

void CMFCButton::SetImage(UINT uiBmpResId, UINT uiBmpHotResId, UINT uiBmpDsblResID) {
  _SetImage(m_Image, _AfxLoadButtonBitmap(uiBmpResId), TRUE, _AfxLoadButtonBitmap(uiBmpHotResId),
            _AfxLoadButtonBitmap(uiBmpDsblResID));
}
void CMFCButton::SetCheckedImage(HICON hIcon, BOOL bAutoDestroy, HICON hIconHot, HICON hIconDisabled, BOOL) {
  _SetImage(m_ImageChecked, hIcon, bAutoDestroy, hIconHot, hIconDisabled);
}
void CMFCButton::SetCheckedImage(HBITMAP hBitmap, BOOL bAutoDestroy, HBITMAP hBitmapHot, BOOL,
                                 HBITMAP hBitmapDisabled) {
  _SetImage(m_ImageChecked, hBitmap, bAutoDestroy, hBitmapHot, hBitmapDisabled);
}
void CMFCButton::SetCheckedImage(UINT uiBmpResId, UINT uiBmpHotResId, UINT uiBmpDsblResID) {
  _SetImage(m_ImageChecked, _AfxLoadButtonBitmap(uiBmpResId), TRUE, _AfxLoadButtonBitmap(uiBmpHotResId),
            _AfxLoadButtonBitmap(uiBmpDsblResID));
}

CSize CMFCButton::GetImageSize() const {
  CSize size = m_Image[0].GetSize();
  CSize sizeChecked = m_ImageChecked[0].GetSize();
  return CSize(size.cx > sizeChecked.cx ? size.cx : sizeChecked.cx,
               size.cy > sizeChecked.cy ? size.cy : sizeChecked.cy);
}

void CMFCButton::SetTooltip(LPCTSTR lpszToolTipText) {
  if (m_hWnd == NULL) return;
  if (lpszToolTipText == NULL || *lpszToolTipText == 0) {
    if (m_wndToolTip.m_hWnd != NULL) m_wndToolTip.DelTool(this, 1);
    return;
  }
  if (m_wndToolTip.m_hWnd == NULL) {
    if (!m_wndToolTip.Create(this, TTS_ALWAYSTIP)) return;
    m_wndToolTip.SetMaxTipWidth(640);
    m_wndToolTip.Activate(TRUE);
  }
  CRect rect;
  GetClientRect(rect);
  if (m_wndToolTip.GetToolCount() == 0)
    m_wndToolTip.AddTool(this, lpszToolTipText, rect, 1);
  else
    m_wndToolTip.UpdateTipText(lpszToolTipText, this, 1);
}

void CMFCButton::SetMouseCursorHand() { m_hCursor = afxGlobalData.GetHandCursor(); }

void CMFCButton::SetFaceColor(COLORREF crFace, BOOL bRepaint) {
  m_clrFace = crFace;
  if (m_hbrFace != NULL) ::DeleteObject(m_hbrFace);
  m_hbrFace = crFace == (COLORREF)-1 ? NULL : ::CreateSolidBrush(crFace);
  if (bRepaint && m_hWnd != NULL) Invalidate();
}

void CMFCButton::EnableMenuFont(BOOL bOn, BOOL bRedraw) {
  m_bMenuFont = bOn;
  if (bRedraw && m_hWnd != NULL) Invalidate();
}

CFont* CMFCButton::SelectFont(CDC* pDC) {
  CFont* pFont = m_bMenuFont ? &afxGlobalData.fontRegular : GetFont();
  if (pFont == NULL) pFont = &afxGlobalData.fontRegular;
  return pDC->SelectObject(pFont);
}

CSize CMFCButton::SizeToContent(BOOL bCalcOnly) {
  CString strText;
  CWnd::GetWindowText(strText);
  CClientDC dc(this);
  CFont* pOld = SelectFont(&dc);
  CSize sizeText(0, 0);
  if (!strText.IsEmpty()) {
    CRect rectText(0, 0, 32767, 0);
    dc.DrawText(strText, rectText, DT_CALCRECT | DT_SINGLELINE);
    sizeText = rectText.Size();
  }
  dc.SelectObject(pOld);
  CSize sizeImage = m_sizeImage;
  int nMargin = GetImageHorzMargin();
  CSize size;
  if (m_bTopImage) {
    size.cx = (sizeImage.cx > sizeText.cx ? sizeImage.cx : sizeText.cx) + 2 * nMargin;
    size.cy = sizeImage.cy + sizeText.cy + 3 * GetVertMargin();
  } else {
    size.cx = sizeImage.cx + sizeText.cx + (sizeImage.cx && sizeText.cx ? nMargin : 0) + 2 * nMargin;
    size.cy = (sizeImage.cy > sizeText.cy ? sizeImage.cy : sizeText.cy) + 2 * GetVertMargin();
  }
  if (m_bCheckButton || m_bRadioButton) size.cx += ::GetSystemMetrics(SM_CXMENUCHECK) + nMargin;
  size.cx += 4;  // the border
  size.cy += 4;
  if (!bCalcOnly && m_hWnd != NULL) SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  return size;
}

void CMFCButton::SetCheck(int nCheck) {
  BOOL bChecked = nCheck != BST_UNCHECKED;
  BOOL bIndeterminate = nCheck == BST_INDETERMINATE && m_b3State;
  if (bChecked == m_bChecked && bIndeterminate == m_bIndeterminate) return;
  m_bChecked = bChecked;
  m_bIndeterminate = bIndeterminate;
  if (m_bChecked && m_bRadioButton && m_bAutoCheck) _UncheckRadioGroup();
  if (m_hWnd != NULL) Invalidate();
}

LRESULT CMFCButton::OnSetCheck(WPARAM wParam, LPARAM) {
  SetCheck((int)wParam);
  return 0;
}

LRESULT CMFCButton::OnGetCheck(WPARAM, LPARAM) { return GetCheck(); }

// The other radio buttons of the group (up to the next WS_GROUP).
void CMFCButton::_UncheckRadioGroup() {
  CWnd* pParent = GetParent();
  if (pParent == NULL) return;
  CWnd* pFirst = this;
  while (!(pFirst->GetStyle() & WS_GROUP)) {
    CWnd* pPrev = pFirst->GetWindow(GW_HWNDPREV);
    if (pPrev == NULL) break;
    pFirst = pPrev;
  }
  for (CWnd* p = pFirst; p != NULL; p = p->GetWindow(GW_HWNDNEXT)) {
    if (p != pFirst && (p->GetStyle() & WS_GROUP)) break;
    if (p == this) continue;
    CMFCButton* pBtn = DYNAMIC_DOWNCAST(CMFCButton, CWnd::FromHandlePermanent(p->m_hWnd));
    if (pBtn != NULL && pBtn->m_bRadioButton) {
      if (pBtn->m_bChecked) {
        pBtn->m_bChecked = FALSE;
        pBtn->Invalidate();
      }
    } else if ((p->SendMessage(WM_GETDLGCODE) & DLGC_RADIOBUTTON) != 0) {
      p->SendMessage(BM_SETCHECK, BST_UNCHECKED);
    }
  }
}

void CMFCButton::OnClicked() {
  if (m_bAutoCheck) {
    if (m_bRadioButton)
      SetCheck(BST_CHECKED);
    else if (m_b3State)
      SetCheck(m_bIndeterminate ? BST_UNCHECKED : m_bChecked ? BST_INDETERMINATE : BST_CHECKED);
    else
      SetCheck(m_bChecked ? BST_UNCHECKED : BST_CHECKED);
  }
  CWnd* pParent = GetParent();
  if (pParent != NULL)
    pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
}

void CMFCButton::OnMouseMove(UINT nFlags, CPoint point) {
  CRect rect;
  GetClientRect(rect);
  BOOL bIn = rect.PtInRect(point) && (::WindowFromPoint(CPoint(point.x, point.y)) == m_hWnd || !m_bCaptured ||
                                      TRUE);
  if (bIn != m_bHighlighted) {
    m_bHighlighted = bIn;
    Invalidate();
    if (bIn) {
      TRACKMOUSEEVENT tme;
      tme.cbSize = sizeof(tme);
      tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = m_hWnd;
      tme.dwHoverTime = 0;
      ::TrackMouseEvent(&tme);
    }
  }
  CButton::OnMouseMove(nFlags, point);
}

LRESULT CMFCButton::OnMouseLeave(WPARAM, LPARAM) {
  if (m_bHighlighted && !m_bCaptured) {
    m_bHighlighted = FALSE;
    Invalidate();
  }
  return 0;
}

void CMFCButton::OnLButtonDown(UINT, CPoint) {
  if (!IsWindowEnabled()) return;
  SetFocus();
  m_bPushed = TRUE;
  m_bHighlighted = TRUE;
  m_bClickiedInside = TRUE;
  m_bCaptured = TRUE;
  SetCapture();
  Invalidate();
  if (m_bResponseOnButtonDown) {
    m_bPushed = FALSE;
    m_bCaptured = FALSE;
    m_bClickiedInside = FALSE;
    ReleaseCapture();
    Invalidate();
    OnClicked();
  }
}

void CMFCButton::OnLButtonUp(UINT, CPoint point) {
  BOOL bClick = m_bCaptured && m_bClickiedInside;
  CRect rect;
  GetClientRect(rect);
  bClick = bClick && rect.PtInRect(point);
  m_bPushed = FALSE;
  m_bClickiedInside = FALSE;
  if (m_bCaptured) {
    m_bCaptured = FALSE;
    ReleaseCapture();
  }
  m_bHighlighted = rect.PtInRect(point);
  Invalidate();
  if (bClick) OnClicked();
  m_bWasDblClk = FALSE;
}

void CMFCButton::OnLButtonDblClk(UINT nFlags, CPoint point) {
  m_bWasDblClk = TRUE;
  OnLButtonDown(nFlags, point);
}

void CMFCButton::OnCancelMode() {
  CButton::OnCancelMode();
  if (m_bCaptured) {
    m_bCaptured = FALSE;
    ReleaseCapture();
  }
  m_bPushed = m_bHighlighted = m_bClickiedInside = FALSE;
  Invalidate();
}

void CMFCButton::OnCaptureChanged(CWnd* pWnd) {
  if (pWnd != this && m_bCaptured) {
    m_bCaptured = FALSE;
    m_bPushed = FALSE;
    Invalidate();
  }
  CButton::OnCaptureChanged(pWnd);
}

BOOL CMFCButton::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (m_hCursor != NULL && nHitTest == HTCLIENT) {
    ::SetCursor(m_hCursor);
    return TRUE;
  }
  return CButton::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CMFCButton::OnEraseBkgnd(CDC*) { return TRUE; }

void CMFCButton::OnEnable(BOOL bEnable) {
  if (!bEnable) m_bHighlighted = m_bPushed = FALSE;
  CButton::OnEnable(bEnable);
  Invalidate();
}

void CMFCButton::OnKillFocus(CWnd* pNewWnd) {
  CButton::OnKillFocus(pNewWnd);
  if (m_bKeyDown) {
    m_bKeyDown = m_bPushed = FALSE;
  }
  Invalidate();
}

void CMFCButton::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_SPACE && !m_bKeyDown) {
    m_bKeyDown = TRUE;
    m_bPushed = m_bHighlighted = TRUE;
    Invalidate();
    return;
  }
  CButton::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CMFCButton::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_SPACE && m_bKeyDown) {
    m_bKeyDown = FALSE;
    m_bPushed = m_bHighlighted = FALSE;
    Invalidate();
    OnClicked();
    return;
  }
  CButton::OnKeyUp(nChar, nRepCnt, nFlags);
}

UINT CMFCButton::OnGetDlgCode() {
  UINT n = DLGC_BUTTON;
  if (m_bRadioButton) n |= DLGC_RADIOBUTTON;
  else if (!m_bCheckButton) n |= m_bDefaultButton ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON;
  return n;
}

void CMFCButton::OnDestroy() {
  if (m_wndToolTip.m_hWnd != NULL) m_wndToolTip.DestroyWindow();
  CButton::OnDestroy();
}

BOOL CMFCButton::PreTranslateMessage(MSG* pMsg) {
  if (m_wndToolTip.m_hWnd != NULL && pMsg->message >= WM_MOUSEFIRST && pMsg->message <= WM_MOUSELAST)
    m_wndToolTip.RelayEvent(pMsg);
  // Enter clicks a focused push button.
  if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN && !m_bCheckButton && !m_bRadioButton &&
      pMsg->hwnd == m_hWnd) {
    OnClicked();
    return TRUE;
  }
  return CButton::PreTranslateMessage(pMsg);
}

void CMFCButton::DrawItem(LPDRAWITEMSTRUCT lpDIS) {
  CDC* pDCPaint = CDC::FromHandle(lpDIS->hDC);
  CRect rectClient = lpDIS->rcItem;
  // Drawn in memory, then copied, so it does not flicker.
  CDC dcMem;
  CBitmap bmp;
  CDC* pDC = pDCPaint;
  if (dcMem.CreateCompatibleDC(pDCPaint) && bmp.CreateCompatibleBitmap(pDCPaint, rectClient.Width(), rectClient.Height())) {
    dcMem.SelectObject(&bmp);
    pDC = &dcMem;
  }
  UINT uiState = lpDIS->itemState;
  if (m_bPushed && m_bHighlighted) uiState |= ODS_SELECTED;
  OnFillBackground(pDC, rectClient);
  CRect rect = rectClient;
  if (!m_bCheckButton && !m_bRadioButton) OnDrawBorder(pDC, rect, uiState);
  OnDraw(pDC, rect, uiState);
  if ((uiState & ODS_FOCUS) && m_bDrawFocus) OnDrawFocusRect(pDC, rectClient);
  if (pDC != pDCPaint) {
    pDCPaint->BitBlt(rectClient.left, rectClient.top, rectClient.Width(), rectClient.Height(), pDC, 0, 0, SRCCOPY);
  }
}

void CMFCButton::OnDrawParentBackground(CDC* pDC, CRect rectClient) {
  afxGlobalData.DrawParentBackground(this, pDC, rectClient);
}

void CMFCButton::OnFillBackground(CDC* pDC, const CRect& rectClient) {
  if (m_bTransparent || m_bCheckButton || m_bRadioButton) {
    OnDrawParentBackground(pDC, rectClient);
    if (!m_bTransparent && m_hbrFace != NULL) pDC->FillRect(rectClient, CBrush::FromHandle(m_hbrFace));
    if (m_bTransparent || m_bCheckButton || m_bRadioButton) return;
  }
  COLORREF clrText;
  if (m_hbrFace == NULL && CMFCVisualManager::GetInstance()->OnDrawPushButton(pDC, rectClient, this, clrText)) return;
  COLORREF clr = m_clrFace != (COLORREF)-1 ? m_clrFace : afxGlobalData.clrBtnFace;
  if (m_nFlatStyle != BUTTONSTYLE_3D && m_bHighlighted && m_clrFace == (COLORREF)-1)
    clr = CMFCVisualManager::GetInstance()->GetHighlightColor();
  if ((m_bPushed && m_bHighlighted) || (m_bChecked && m_bHighlightChecked && !m_bCheckButton && !m_bRadioButton))
    clr = m_clrFace != (COLORREF)-1 ? AfxBlendColor(m_clrFace, RGB(0, 0, 0), 15)
                                    : CMFCVisualManager::GetInstance()->GetPressedColor();
  pDC->FillSolidRect(rectClient, clr);
}

void CMFCButton::OnDrawBorder(CDC* pDC, CRect& rectClient, UINT uiState) {
  if (m_hbrFace == NULL && CMFCVisualManager::GetInstance()->IsFlat() && m_nFlatStyle == BUTTONSTYLE_3D) {
    // The flat looks frame the button themselves.
    CBrush br(m_bHighlighted ? CMFCVisualManager::GetInstance()->GetHighlightBorderColor()
                             : afxGlobalData.clrBtnShadow);
    pDC->FrameRect(rectClient, &br);
    rectClient.DeflateRect(2, 2);
    return;
  }
  if (m_hbrFace == NULL && m_nFlatStyle == BUTTONSTYLE_3D) {
    // DrawFrameControl drew the border in OnFillBackground.
    if (m_bDefaultButton) {
      CBrush br(afxGlobalData.clrBtnDkShadow);
      pDC->FrameRect(rectClient, &br);
    }
    rectClient.DeflateRect(2, 2);
    return;
  }
  CMFCVisualManager::GetInstance()->OnDrawButtonBorder(pDC, rectClient, this, uiState);
}

void CMFCButton::OnDrawFocusRect(CDC* pDC, const CRect& rectClient) {
  CRect rect = rectClient;
  rect.DeflateRect(3, 3);
  pDC->DrawFocusRect(rect);
}

void CMFCButton::OnDrawCheckMark(CDC* pDC, CRect& rect, UINT uiState) {
  int cx = ::GetSystemMetrics(SM_CXMENUCHECK);
  CRect rectCheck(rect.left, rect.top + (rect.Height() - cx) / 2, rect.left + cx, rect.top + (rect.Height() - cx) / 2 + cx);
  if (m_bRightImage) rectCheck.OffsetRect(rect.Width() - cx, 0);
  BOOL bEnabled = !(uiState & ODS_DISABLED);
  if (m_bCheckButton)
    CMFCVisualManager::GetInstance()->OnDrawCheckBoxEx(pDC, rectCheck, m_bIndeterminate ? 2 : m_bChecked ? 1 : 0,
                                                      m_bHighlighted, m_bPushed && m_bHighlighted, bEnabled);
  else
    CMFCVisualManager::GetInstance()->OnDrawRadioButton(pDC, rectCheck, m_bChecked, m_bHighlighted,
                                                       m_bPushed && m_bHighlighted, bEnabled);
  if (m_bRightImage)
    rect.right = rectCheck.left - GetImageHorzMargin();
  else
    rect.left = rectCheck.right + GetImageHorzMargin();
}

void CMFCButton::OnDraw(CDC* pDC, const CRect& rectIn, UINT uiState) {
  CRect rect = rectIn;
  if (m_bCheckButton || m_bRadioButton) OnDrawCheckMark(pDC, rect, uiState);
  BOOL bDisabled = (uiState & ODS_DISABLED) != 0;
  BOOL bPressedOffset = (uiState & ODS_SELECTED) && !m_bCheckButton && !m_bRadioButton;
  if (bPressedOffset) rect.OffsetRect(1, 1);

  // The image of the state: checked, then hot or disabled, then normal.
  const CImageSlot* pSlots = m_bChecked && m_ImageChecked[0].IsValid() ? m_ImageChecked : m_Image;
  const CImageSlot* pImage = &pSlots[0];
  if (bDisabled && pSlots[2].IsValid())
    pImage = &pSlots[2];
  else if (m_bHighlighted && pSlots[1].IsValid())
    pImage = &pSlots[1];
  CSize sizeImage = pImage->IsValid() ? pImage->GetSize() : CSize(0, 0);

  CString strText;
  CWnd::GetWindowText(strText);
  CFont* pOldFont = SelectFont(pDC);
  CRect rectText = rect;
  int nMargin = GetImageHorzMargin();
  if (sizeImage.cx > 0) {
    CPoint ptImage;
    if (m_bTopImage) {
      ptImage = CPoint(rect.left + (rect.Width() - sizeImage.cx) / 2,
                       strText.IsEmpty() ? rect.top + (rect.Height() - sizeImage.cy) / 2 : rect.top + GetVertMargin());
      rectText.top = ptImage.y + sizeImage.cy + GetVertMargin();
    } else {
      CSize sizeText(0, 0);
      if (!strText.IsEmpty()) {
        CRect r(0, 0, rect.Width(), 0);
        pDC->DrawText(strText, r, DT_CALCRECT | DT_SINGLELINE);
        sizeText = r.Size();
      }
      int cxTotal = sizeImage.cx + (sizeText.cx ? nMargin + sizeText.cx : 0);
      int x;
      if (m_nAlignStyle == ALIGN_LEFT)
        x = rect.left + nMargin;
      else if (m_nAlignStyle == ALIGN_RIGHT)
        x = rect.right - nMargin - cxTotal;
      else
        x = rect.left + (rect.Width() - cxTotal) / 2;
      int y = rect.top + (rect.Height() - sizeImage.cy) / 2;
      if (m_bRightImage) {
        ptImage = CPoint(x + cxTotal - sizeImage.cx, y);
        rectText = CRect(x, rect.top, ptImage.x - (sizeText.cx ? nMargin : 0), rect.bottom);
      } else {
        ptImage = CPoint(x, y);
        rectText = CRect(x + sizeImage.cx + nMargin, rect.top, x + cxTotal, rect.bottom);
      }
    }
    pImage->Draw(pDC, ptImage, bDisabled && m_bGrayDisabled && !pSlots[2].IsValid());
  }
  if (!strText.IsEmpty()) {
    UINT uiDTFlags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (sizeImage.cx > 0 && !m_bTopImage) {
      uiDTFlags |= DT_LEFT;
    } else if (m_bCheckButton || m_bRadioButton) {
      uiDTFlags |= m_nAlignStyle == ALIGN_CENTER ? DT_CENTER : m_nAlignStyle == ALIGN_RIGHT ? DT_RIGHT : DT_LEFT;
    } else {
      uiDTFlags |= m_nAlignStyle == ALIGN_LEFT ? DT_LEFT : m_nAlignStyle == ALIGN_RIGHT ? DT_RIGHT : DT_CENTER;
    }
    if (m_bTopImage && sizeImage.cx > 0) uiDTFlags = (uiDTFlags & ~DT_VCENTER) | DT_TOP;
    OnDrawText(pDC, rectText, strText, uiDTFlags, uiState);
  }
  pDC->SelectObject(pOldFont);
}

void CMFCButton::OnDrawText(CDC* pDC, const CRect& rect, const CString& strText, UINT uiDTFlags, UINT uiState) {
  COLORREF clrText;
  if (uiState & ODS_DISABLED)
    clrText = afxGlobalData.clrGrayedText;
  else if (m_bHighlighted && m_clrHover != (COLORREF)-1)
    clrText = m_clrHover;
  else if (m_clrRegular != (COLORREF)-1)
    clrText = m_clrRegular;
  else
    clrText = CMFCVisualManager::GetInstance()->GetButtonTextColor(this, uiState);
  pDC->SetBkMode(TRANSPARENT);
  pDC->SetTextColor(clrText);
  CRect r = rect;
  pDC->DrawText(strText, r, uiDTFlags);
}

// ---------------------------------------------------------------------
// CMFCLinkCtrl

IMPLEMENT_DYNAMIC(CMFCLinkCtrl, CMFCButton)

CMFCLinkCtrl::CMFCLinkCtrl()
    : m_bMultilineText(FALSE), m_bAlwaysUnderlineText(TRUE), m_bDefaultClickProcess(TRUE), m_bVisited(FALSE) {
  m_nFlatStyle = BUTTONSTYLE_NOBORDERS;
  m_nAlignStyle = ALIGN_LEFT;
  m_bTransparent = TRUE;
  m_bDrawFocus = TRUE;
  m_hCursor = afxGlobalData.GetHandCursor();
}

CMFCLinkCtrl::~CMFCLinkCtrl() {}

void CMFCLinkCtrl::PreSubclassWindow() {
  CMFCButton::PreSubclassWindow();
  if (m_hCursor == NULL) m_hCursor = afxGlobalData.GetHandCursor();
}

void CMFCLinkCtrl::SetURL(LPCTSTR lpszURL) { m_strURL = lpszURL != NULL ? lpszURL : _T(""); }

void CMFCLinkCtrl::SetURLPrefix(LPCTSTR lpszPrefix) { m_strPrefix = lpszPrefix != NULL ? lpszPrefix : _T(""); }

CSize CMFCLinkCtrl::SizeToContent(BOOL bVCenter, BOOL bHCenter) {
  CString strText;
  CWnd::GetWindowText(strText);
  CClientDC dc(this);
  CFont* pOld = dc.SelectObject(&afxGlobalData.fontUnderline);
  CRect rectText(0, 0, m_bMultilineText ? 0 : 32767, 0);
  if (m_bMultilineText) {
    CRect rectClient;
    GetClientRect(rectClient);
    rectText.right = rectClient.Width();
  }
  dc.DrawText(strText, rectText, DT_CALCRECT | (m_bMultilineText ? DT_WORDBREAK : DT_SINGLELINE));
  dc.SelectObject(pOld);
  CSize size(rectText.Width() + 4, rectText.Height() + 4);
  if (m_hWnd != NULL) {
    CRect rect;
    GetWindowRect(rect);
    CWnd* pParent = GetParent();
    if (pParent != NULL) pParent->ScreenToClient(rect);
    int x = rect.left, y = rect.top;
    if (bHCenter) x += (rect.Width() - size.cx) / 2;
    if (bVCenter) y += (rect.Height() - size.cy) / 2;
    SetWindowPos(NULL, x, y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
  }
  return size;
}

BOOL CMFCLinkCtrl::OpenLink() {
  CString strURL = m_strURL;
  if (strURL.IsEmpty()) CWnd::GetWindowText(strURL);
  strURL = m_strPrefix + strURL;
  if (strURL.IsEmpty()) return FALSE;
  HINSTANCE h = ::ShellExecute(NULL, _T("open"), strURL, NULL, NULL, SW_SHOWNORMAL);
  return (INT_PTR)h > 32;
}

void CMFCLinkCtrl::OnClicked() {
  if (m_bDefaultClickProcess) {
    if (OpenLink()) {
      m_bVisited = TRUE;
      Invalidate();
    }
    return;
  }
  m_bVisited = TRUE;
  Invalidate();
  CMFCButton::OnClicked();
}

void CMFCLinkCtrl::OnFillBackground(CDC* pDC, const CRect& rectClient) {
  if (m_bTransparent)
    OnDrawParentBackground(pDC, rectClient);
  else
    pDC->FillSolidRect(rectClient, m_clrFace != (COLORREF)-1 ? m_clrFace : afxGlobalData.clrBtnFace);
}

void CMFCLinkCtrl::OnDraw(CDC* pDC, const CRect& rect, UINT uiState) {
  CString strText;
  CWnd::GetWindowText(strText);
  BOOL bUnderline = m_bAlwaysUnderlineText || m_bHighlighted;
  CFont* pOld = pDC->SelectObject(bUnderline ? &afxGlobalData.fontUnderline : &afxGlobalData.fontRegular);
  COLORREF clr = (uiState & ODS_DISABLED) ? afxGlobalData.clrGrayedText
                 : m_bHighlighted ? (m_clrHover != (COLORREF)-1 ? m_clrHover : afxGlobalData.clrHotLinkHoveredText)
                 : m_bVisited     ? afxGlobalData.clrHotLinkVisitedText
                                  : (m_clrRegular != (COLORREF)-1 ? m_clrRegular : afxGlobalData.clrHotLinkNormalText);
  pDC->SetBkMode(TRANSPARENT);
  pDC->SetTextColor(clr);
  CRect r = rect;
  r.DeflateRect(2, 0);
  pDC->DrawText(strText, r,
                (m_bMultilineText ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER) |
                    (m_nAlignStyle == ALIGN_CENTER ? DT_CENTER : m_nAlignStyle == ALIGN_RIGHT ? DT_RIGHT : DT_LEFT));
  pDC->SelectObject(pOld);
}

void CMFCLinkCtrl::OnDrawFocusRect(CDC* pDC, const CRect& rectClient) { pDC->DrawFocusRect(rectClient); }

// ---------------------------------------------------------------------
// CMFCMenuButton

IMPLEMENT_DYNAMIC(CMFCMenuButton, CMFCButton)

BEGIN_MESSAGE_MAP(CMFCMenuButton, CMFCButton)
  ON_WM_LBUTTONDOWN()
  ON_WM_KEYDOWN()
  ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

CMFCMenuButton::CMFCMenuButton()
    : m_hMenu(NULL), m_nMenuResult(0), m_bOSMenu(TRUE), m_bRightArrow(FALSE), m_bStayPressed(FALSE),
      m_bDefaultClick(FALSE), m_bMenuIsActive(FALSE), m_bClickOnMenu(FALSE) {}

CMFCMenuButton::~CMFCMenuButton() {}

CRect CMFCMenuButton::_ArrowRect() const {
  CRect rect;
  GetClientRect(rect);
  int cx = ::GetSystemMetrics(SM_CXVSCROLL);
  rect.left = rect.right - cx - 2;
  return rect;
}

CSize CMFCMenuButton::SizeToContent(BOOL bCalcOnly) {
  CSize size = CMFCButton::SizeToContent(TRUE);
  size.cx += ::GetSystemMetrics(SM_CXVSCROLL) + 2;
  if (!bCalcOnly && m_hWnd != NULL) SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  return size;
}

void CMFCMenuButton::OnDraw(CDC* pDC, const CRect& rect, UINT uiState) {
  CRect rectArrow = rect;
  int cx = ::GetSystemMetrics(SM_CXVSCROLL);
  rectArrow.left = rectArrow.right - cx;
  CRect rectContent = rect;
  rectContent.right = rectArrow.left;
  CMFCButton::OnDraw(pDC, rectContent, uiState);
  if (m_bDefaultClick) {
    // The line between the button and its arrow.
    CPen pen(PS_SOLID, 1, afxGlobalData.clrBtnShadow);
    CPen* pOld = pDC->SelectObject(&pen);
    pDC->MoveTo(rectArrow.left, rectArrow.top + 2);
    pDC->LineTo(rectArrow.left, rectArrow.bottom - 2);
    pDC->SelectObject(pOld);
  }
  if (m_bRightArrow) {
    // A right-pointing triangle.
    CPoint c = rectArrow.CenterPoint();
    POINT pts[3] = {{c.x - 2, c.y - 4}, {c.x - 2, c.y + 4}, {c.x + 2, c.y}};
    COLORREF clr = (uiState & ODS_DISABLED) ? afxGlobalData.clrGrayedText : afxGlobalData.clrBtnText;
    CBrush br(clr);
    CPen pen(PS_SOLID, 1, clr);
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);
    pDC->Polygon(pts, 3);
    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
  } else {
    CMFCVisualManager::GetInstance()->OnDrawMenuArrow(pDC, rectArrow, !(uiState & ODS_DISABLED));
  }
}

void CMFCMenuButton::OnShowMenu() {
  m_nMenuResult = 0;
  if (m_hMenu == NULL) return;
  CRect rect;
  GetWindowRect(rect);
  m_bMenuIsActive = TRUE;
  if (m_bStayPressed) {
    m_bPushed = m_bHighlighted = TRUE;
    Invalidate();
    UpdateWindow();
  }
  int x = m_bRightArrow ? rect.right : rect.left;
  int y = m_bRightArrow ? rect.top : rect.bottom;
  m_nMenuResult = (int)::TrackPopupMenu(m_hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, x, y,
                                        0, m_hWnd, NULL);
  m_bMenuIsActive = FALSE;
  m_bPushed = m_bHighlighted = FALSE;
  Invalidate();
  if (m_nMenuResult != 0) {
    CWnd* pParent = GetParent();
    if (pParent != NULL)
      pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
  }
}

void CMFCMenuButton::OnLButtonDown(UINT nFlags, CPoint point) {
  if (!IsWindowEnabled()) return;
  m_bClickOnMenu = !m_bDefaultClick || _ArrowRect().PtInRect(point);
  if (m_bClickOnMenu) {
    SetFocus();
    OnShowMenu();
    return;
  }
  CMFCButton::OnLButtonDown(nFlags, point);
}

void CMFCMenuButton::OnClicked() {
  // A click on the button part (m_bDefaultClick): no menu command.
  m_nMenuResult = 0;
  CMFCButton::OnClicked();
}

void CMFCMenuButton::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_DOWN || (nChar == VK_SPACE && !m_bDefaultClick)) {
    OnShowMenu();
    return;
  }
  CMFCButton::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CMFCMenuButton::OnGetDlgCode() { return CMFCButton::OnGetDlgCode() | DLGC_WANTARROWS; }

BOOL CMFCMenuButton::PreTranslateMessage(MSG* pMsg) {
  if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_hWnd && (pMsg->wParam == VK_RETURN) && !m_bDefaultClick) {
    OnShowMenu();
    return TRUE;
  }
  return CMFCButton::PreTranslateMessage(pMsg);
}

// ---------------------------------------------------------------------
// CMFCColorButton and its popup

// The standard palette: 5 rows of 8.
static const COLORREF _afxStdColors[] = {
    RGB(0, 0, 0),       RGB(153, 51, 0),    RGB(51, 51, 0),     RGB(0, 51, 0),      RGB(0, 51, 102),
    RGB(0, 0, 128),     RGB(51, 51, 153),   RGB(51, 51, 51),    RGB(128, 0, 0),     RGB(255, 102, 0),
    RGB(128, 128, 0),   RGB(0, 128, 0),     RGB(0, 128, 128),   RGB(0, 0, 255),     RGB(102, 102, 153),
    RGB(128, 128, 128), RGB(255, 0, 0),     RGB(255, 153, 0),   RGB(153, 204, 0),   RGB(51, 153, 102),
    RGB(51, 204, 204),  RGB(51, 102, 255),  RGB(128, 0, 128),   RGB(153, 153, 153), RGB(255, 0, 255),
    RGB(255, 204, 0),   RGB(255, 255, 0),   RGB(0, 255, 0),     RGB(0, 255, 255),   RGB(0, 204, 255),
    RGB(153, 51, 102),  RGB(192, 192, 192), RGB(255, 153, 204), RGB(255, 204, 153), RGB(255, 255, 153),
    RGB(204, 255, 204), RGB(204, 255, 255), RGB(153, 204, 255), RGB(204, 153, 255), RGB(255, 255, 255)};
static const LPCTSTR _afxStdColorNames[] = {
    _T("Black"),       _T("Brown"),       _T("Olive Green"),  _T("Dark Green"), _T("Dark Teal"),
    _T("Dark Blue"),   _T("Indigo"),      _T("Gray-80%"),     _T("Dark Red"),   _T("Orange"),
    _T("Dark Yellow"), _T("Green"),       _T("Teal"),         _T("Blue"),       _T("Blue-Gray"),
    _T("Gray-50%"),    _T("Red"),         _T("Light Orange"), _T("Lime"),       _T("Sea Green"),
    _T("Aqua"),        _T("Light Blue"),  _T("Violet"),       _T("Gray-40%"),   _T("Pink"),
    _T("Gold"),        _T("Yellow"),      _T("Bright Green"), _T("Turquoise"),  _T("Sky Blue"),
    _T("Plum"),        _T("Gray-25%"),    _T("Rose"),         _T("Tan"),        _T("Light Yellow"),
    _T("Light Green"), _T("Light Turquoise"), _T("Pale Blue"), _T("Lavender"),  _T("White")};

static CMap<COLORREF, COLORREF, CString, LPCTSTR>& _AfxColorNames() {
  static CMap<COLORREF, COLORREF, CString, LPCTSTR> s_names;
  if (s_names.IsEmpty())
    for (int i = 0; i < (int)(sizeof(_afxStdColors) / sizeof(_afxStdColors[0])); i++)
      s_names.SetAt(_afxStdColors[i], _afxStdColorNames[i]);
  return s_names;
}

void AFXAPI CMFCColorButton::SetColorName(COLORREF color, const CString& strName) {
  _AfxColorNames().SetAt(color, strName);
}

BOOL AFXAPI CMFCColorButton::GetColorName(COLORREF color, CString& strName) {
  return _AfxColorNames().Lookup(color, strName);
}

// The popup: the colors in a grid, "Automatic" above and "Other..." below.
class CMFCColorPopupWnd_ : public CWnd {
public:
  explicit CMFCColorPopupWnd_(CMFCColorButton* pButton)
      : m_pButton(pButton), m_nHot(-2), m_bDone(FALSE), m_bChose(FALSE), m_clrChosen(0), m_bOther(FALSE) {}

  enum { kCell = 18, kMargin = 4, kButtonHeight = 22, kAuto = -3, kOther = -4, kNone = -2 };

  BOOL Show(const CRect& rectButton);
  int HitTest(CPoint pt) const;
  CRect CellRect(int i) const;
  CRect AutoRect() const;
  CRect OtherRect() const;
  int Count() const { return (int)m_pButton->m_Colors.GetSize(); }
  int Rows() const { return (Count() + Columns() - 1) / Columns(); }
  int Columns() const { return m_pButton->m_nColumns > 0 ? m_pButton->m_nColumns : 8; }
  void Choose(int nItem);

  CMFCColorButton* m_pButton;
  int m_nHot;
  BOOL m_bDone, m_bChose;
  COLORREF m_clrChosen;
  BOOL m_bOther;

  afx_msg void OnPaint();
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnCancelMode();
  afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
  afx_msg void OnCaptureChanged(CWnd* pWnd);
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMFCColorPopupWnd_, CWnd)
  ON_WM_PAINT()
  ON_WM_MOUSEMOVE()
  ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDOWN()
  ON_WM_KEYDOWN()
  ON_WM_CANCELMODE()
  ON_WM_ACTIVATEAPP()
  ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

CRect CMFCColorPopupWnd_::AutoRect() const {
  if (!m_pButton->m_bAutoColor) return CRect(0, 0, 0, 0);
  return CRect(kMargin, kMargin, kMargin + Columns() * kCell, kMargin + kButtonHeight);
}

CRect CMFCColorPopupWnd_::CellRect(int i) const {
  int top = kMargin + (m_pButton->m_bAutoColor ? kButtonHeight + kMargin : 0);
  int row = i / Columns(), col = i % Columns();
  return CRect(kMargin + col * kCell, top + row * kCell, kMargin + (col + 1) * kCell, top + (row + 1) * kCell);
}

CRect CMFCColorPopupWnd_::OtherRect() const {
  if (!m_pButton->m_bOtherColor) return CRect(0, 0, 0, 0);
  int top = CellRect(0).top + Rows() * kCell + kMargin;
  return CRect(kMargin, top, kMargin + Columns() * kCell, top + kButtonHeight);
}

int CMFCColorPopupWnd_::HitTest(CPoint pt) const {
  if (AutoRect().PtInRect(pt)) return kAuto;
  if (OtherRect().PtInRect(pt)) return kOther;
  for (int i = 0; i < Count(); i++)
    if (CellRect(i).PtInRect(pt)) return i;
  return kNone;
}

BOOL CMFCColorPopupWnd_::Show(const CRect& rectButton) {
  int cx = 2 * kMargin + Columns() * kCell;
  int cy = kMargin + (m_pButton->m_bAutoColor ? kButtonHeight + kMargin : 0) + Rows() * kCell + kMargin +
           (m_pButton->m_bOtherColor ? kButtonHeight + kMargin : 0);
  CRect rect(rectButton.left, rectButton.bottom, rectButton.left + cx + 2, rectButton.bottom + cy + 2);
  // On the screen: above the button when there is no room below.
  CRect rectScreen(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
  if (rect.bottom > rectScreen.bottom) rect.OffsetRect(0, rectButton.top - rect.bottom);
  if (rect.right > rectScreen.right) rect.OffsetRect(rectScreen.right - rect.right, 0);
  LPCTSTR lpszClass = AfxRegisterWndClass(CS_SAVEBITS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, lpszClass, _T(""), WS_POPUP | WS_BORDER, rect,
                m_pButton->GetParentOwner(), 0))
    return FALSE;
  ShowWindow(SW_SHOWNA);
  UpdateWindow();
  SetCapture();
  // Its own loop, as a menu has, until a color is chosen or it closes.
  while (!m_bDone && m_hWnd != NULL) {
    MSG msg;
    if (!::GetMessage(&msg, NULL, 0, 0)) {
      ::PostQuitMessage((int)msg.wParam);
      break;
    }
    if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_CHAR) msg.hwnd = m_hWnd;
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
  if (m_hWnd != NULL) {
    if (::GetCapture() == m_hWnd) ::ReleaseCapture();
    DestroyWindow();
  }
  return m_bChose;
}

void CMFCColorPopupWnd_::Choose(int nItem) {
  if (nItem == kAuto) {
    m_clrChosen = (COLORREF)-1;
    m_bChose = TRUE;
  } else if (nItem == kOther) {
    m_bOther = TRUE;
    m_bChose = TRUE;
  } else if (nItem >= 0 && nItem < Count()) {
    m_clrChosen = m_pButton->m_Colors[nItem];
    m_bChose = TRUE;
  }
  m_bDone = TRUE;
}

void CMFCColorPopupWnd_::OnPaint() {
  CPaintDC dc(this);
  CRect rectClient;
  GetClientRect(rectClient);
  dc.FillSolidRect(rectClient, afxGlobalData.clrBarFace);
  CFont* pOld = dc.SelectObject(&afxGlobalData.fontRegular);
  dc.SetBkMode(TRANSPARENT);
  COLORREF clrCurrent = m_pButton->m_Color;
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  auto drawButton = [&](const CRect& r, LPCTSTR lpszText, BOOL bHot, BOOL bSel) {
    if (bHot || bSel) {
      dc.FillSolidRect(r, bHot ? pVM->GetHighlightColor() : afxGlobalData.clrBtnLight);
      CBrush br(pVM->GetHighlightBorderColor());
      dc.FrameRect(r, &br);
    } else {
      dc.Draw3dRect(r, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnShadow);
    }
    dc.SetTextColor(afxGlobalData.clrBtnText);
    CRect rt = r;
    dc.DrawText(lpszText, rt, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  };
  if (m_pButton->m_bAutoColor)
    drawButton(AutoRect(), m_pButton->m_strAutoColorText, m_nHot == kAuto, clrCurrent == (COLORREF)-1);
  for (int i = 0; i < Count(); i++) {
    CRect r = CellRect(i);
    BOOL bSel = m_pButton->m_Colors[i] == clrCurrent;
    if (m_nHot == i || bSel) {
      dc.FillSolidRect(r, m_nHot == i ? pVM->GetHighlightColor() : afxGlobalData.clrBtnLight);
      CBrush br(pVM->GetHighlightBorderColor());
      dc.FrameRect(r, &br);
    }
    r.DeflateRect(3, 3);
    dc.FillSolidRect(r, m_pButton->m_Colors[i]);
    dc.Draw3dRect(r, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
  }
  if (m_pButton->m_bOtherColor) drawButton(OtherRect(), m_pButton->m_strOtherText, m_nHot == kOther, FALSE);
  dc.SelectObject(pOld);
}

void CMFCColorPopupWnd_::OnMouseMove(UINT, CPoint point) {
  int n = HitTest(point);
  if (n != m_nHot) {
    m_nHot = n;
    Invalidate();
  }
}

void CMFCColorPopupWnd_::OnLButtonDown(UINT, CPoint point) {
  CRect rect;
  GetClientRect(rect);
  if (!rect.PtInRect(point)) m_bDone = TRUE;  // outside: closes
}

void CMFCColorPopupWnd_::OnLButtonUp(UINT, CPoint point) {
  int n = HitTest(point);
  if (n != kNone) Choose(n);
}

void CMFCColorPopupWnd_::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  int nCols = Columns(), nCount = Count();
  int nHot = m_nHot;
  switch (nChar) {
    case VK_ESCAPE:
      m_bDone = TRUE;
      return;
    case VK_RETURN:
    case VK_SPACE:
      if (nHot != kNone) Choose(nHot);
      return;
    case VK_LEFT:
      nHot = nHot <= 0 ? nCount - 1 : nHot - 1;
      break;
    case VK_RIGHT:
      nHot = nHot < 0 || nHot >= nCount - 1 ? 0 : nHot + 1;
      break;
    case VK_DOWN:
      if (nHot == kAuto || nHot == kNone)
        nHot = 0;
      else if (nHot >= 0 && nHot + nCols < nCount)
        nHot += nCols;
      else if (nHot >= 0 && m_pButton->m_bOtherColor)
        nHot = kOther;
      break;
    case VK_UP:
      if (nHot == kOther)
        nHot = nCount - 1;
      else if (nHot >= nCols)
        nHot -= nCols;
      else if (nHot >= 0 && m_pButton->m_bAutoColor)
        nHot = kAuto;
      break;
    default:
      CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
      return;
  }
  if (nHot != m_nHot) {
    m_nHot = nHot;
    Invalidate();
  }
}

void CMFCColorPopupWnd_::OnCancelMode() { m_bDone = TRUE; }

void CMFCColorPopupWnd_::OnActivateApp(BOOL bActive, DWORD) {
  if (!bActive) m_bDone = TRUE;
}

void CMFCColorPopupWnd_::OnCaptureChanged(CWnd* pWnd) {
  if (pWnd != this && !m_bDone) m_bDone = TRUE;
}

IMPLEMENT_DYNAMIC(CMFCColorButton, CMFCButton)

BEGIN_MESSAGE_MAP(CMFCColorButton, CMFCButton)
  ON_WM_KEYDOWN()
  ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

CMFCColorButton::CMFCColorButton()
    : m_bEnabledInCustomizeMode(FALSE), m_bAutoSetFocus(TRUE), m_bAltColorDlg(TRUE), m_Color(RGB(0, 0, 0)),
      m_ColorAutomatic(RGB(0, 0, 0)), m_nColumns(-1), m_bAutoColor(FALSE), m_bOtherColor(FALSE),
      m_pPalette(NULL), m_pPopup(NULL) {
  _InitColors();
}

CMFCColorButton::~CMFCColorButton() {}

void CMFCColorButton::_InitColors() {
  m_Colors.RemoveAll();
  if (m_pPalette != NULL && m_pPalette->GetSafeHandle() != NULL) {
    int n = m_pPalette->GetEntryCount();
    for (int i = 0; i < n; i++) {
      PALETTEENTRY pe;
      m_pPalette->GetPaletteEntries(i, 1, &pe);
      m_Colors.Add(RGB(pe.peRed, pe.peGreen, pe.peBlue));
    }
  } else {
    for (int i = 0; i < (int)(sizeof(_afxStdColors) / sizeof(_afxStdColors[0])); i++) m_Colors.Add(_afxStdColors[i]);
  }
}

void CMFCColorButton::SetPalette(CPalette* pPalette) {
  m_pPalette = pPalette;
  _InitColors();
}

void CMFCColorButton::SetColor(COLORREF color) {
  m_Color = color;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCColorButton::EnableAutomaticButton(LPCTSTR lpszLabel, COLORREF colorAutomatic, BOOL bEnable) {
  m_bAutoColor = bEnable;
  m_strAutoColorText = lpszLabel != NULL ? lpszLabel : _T("");
  m_ColorAutomatic = colorAutomatic;
}

void CMFCColorButton::EnableOtherButton(LPCTSTR lpszLabel, BOOL bAltColorDlg, BOOL bEnable) {
  m_bOtherColor = bEnable;
  m_strOtherText = lpszLabel != NULL ? lpszLabel : _T("");
  m_bAltColorDlg = bAltColorDlg;
}

void CMFCColorButton::SetDocumentColors(LPCTSTR lpszLabel, CList<COLORREF, COLORREF>& lstColors) {
  m_strDocColorsText = lpszLabel != NULL ? lpszLabel : _T("");
  m_lstDocColors.RemoveAll();
  m_lstDocColors.AddTail(&lstColors);
}

CSize CMFCColorButton::SizeToContent(BOOL bCalcOnly) {
  CSize size = CMFCButton::SizeToContent(TRUE);
  size.cx += 24 + ::GetSystemMetrics(SM_CXVSCROLL);
  if (size.cy < 22) size.cy = 22;
  if (!bCalcOnly && m_hWnd != NULL) SetWindowPos(NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  return size;
}

void CMFCColorButton::UpdateColor(COLORREF color) {
  SetColor(color);
  CWnd* pParent = GetParent();
  if (pParent != NULL)
    pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
}

BOOL CMFCColorButton::OnOtherColor(COLORREF& color) {
  CColorDialog dlg(m_Color == (COLORREF)-1 ? m_ColorAutomatic : m_Color, CC_FULLOPEN, this);
  if (dlg.DoModal() != IDOK) return FALSE;
  color = dlg.GetColor();
  return TRUE;
}

void CMFCColorButton::OnShowColorPopup() {
  if (m_pPopup != NULL) return;
  if (m_bAutoSetFocus) SetFocus();
  CRect rect;
  GetWindowRect(rect);
  CMFCColorPopupWnd_ popup(this);
  m_pPopup = &popup;
  m_bPushed = TRUE;
  Invalidate();
  BOOL bChose = popup.Show(rect);
  m_pPopup = NULL;
  m_bPushed = m_bHighlighted = FALSE;
  Invalidate();
  if (!bChose) return;
  if (popup.m_bOther) {
    COLORREF color;
    if (OnOtherColor(color)) UpdateColor(color);
    return;
  }
  UpdateColor(popup.m_clrChosen);
}

void CMFCColorButton::OnClicked() { OnShowColorPopup(); }

void CMFCColorButton::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_DOWN || nChar == VK_SPACE || nChar == VK_RETURN) {
    OnShowColorPopup();
    return;
  }
  CMFCButton::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CMFCColorButton::OnGetDlgCode() { return CMFCButton::OnGetDlgCode() | DLGC_WANTARROWS; }

void CMFCColorButton::OnDrawBorder(CDC* pDC, CRect& rectClient, UINT uiState) {
  CMFCButton::OnDrawBorder(pDC, rectClient, uiState);
}

void CMFCColorButton::OnDrawFocusRect(CDC* pDC, const CRect& rectClient) {
  CRect rect = rectClient;
  rect.DeflateRect(3, 3);
  pDC->DrawFocusRect(rect);
}

void CMFCColorButton::OnDraw(CDC* pDC, const CRect& rect, UINT uiState) {
  CRect rectArrow = rect;
  rectArrow.left = rectArrow.right - ::GetSystemMetrics(SM_CXVSCROLL);
  CRect rectColor = rect;
  rectColor.right = rectArrow.left - 2;
  rectColor.DeflateRect(2, 2);
  CString strText;
  CWnd::GetWindowText(strText);
  COLORREF clr = m_Color == (COLORREF)-1 ? m_ColorAutomatic : m_Color;
  if (!strText.IsEmpty()) {
    // A swatch at the left, the text after it.
    CRect rectSwatch(rectColor.left, rectColor.top, rectColor.left + rectColor.Height(), rectColor.bottom);
    rectColor.left = rectSwatch.right + 4;
    if (!(uiState & ODS_DISABLED)) pDC->FillSolidRect(rectSwatch, clr);
    pDC->Draw3dRect(rectSwatch, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
    CFont* pOld = SelectFont(pDC);
    OnDrawText(pDC, rectColor, strText, DT_SINGLELINE | DT_VCENTER | DT_LEFT, uiState);
    pDC->SelectObject(pOld);
  } else {
    if (!(uiState & ODS_DISABLED)) pDC->FillSolidRect(rectColor, clr);
    pDC->Draw3dRect(rectColor, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnShadow);
  }
  CPen pen(PS_SOLID, 1, afxGlobalData.clrBtnShadow);
  CPen* pOldPen = pDC->SelectObject(&pen);
  pDC->MoveTo(rectArrow.left - 1, rect.top + 1);
  pDC->LineTo(rectArrow.left - 1, rect.bottom - 1);
  pDC->SelectObject(pOldPen);
  CMFCVisualManager::GetInstance()->OnDrawMenuArrow(pDC, rectArrow, !(uiState & ODS_DISABLED));
}
