// dlgex.cpp - CDialogEx: a dialog with a background color or bitmap.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdialogex.h>

IMPLEMENT_DYNAMIC(CDialogEx, CDialog)

BEGIN_MESSAGE_MAP(CDialogEx, CDialog)
  ON_WM_ERASEBKGND()
  ON_WM_CTLCOLOR()
  ON_WM_DESTROY()
END_MESSAGE_MAP()

CDialogEx::CDialogEx() { _CommonConstruct(); }

CDialogEx::CDialogEx(UINT nIDTemplate, CWnd* pParent) : CDialog(nIDTemplate, pParent) {
  _CommonConstruct();
}

CDialogEx::CDialogEx(LPCTSTR lpszTemplateName, CWnd* pParentWnd)
    : CDialog(lpszTemplateName, pParentWnd) {
  _CommonConstruct();
}

CDialogEx::~CDialogEx() {
  _FreeBitmap();
  if (m_brBkgr != NULL) ::DeleteObject(m_brBkgr);
}

void CDialogEx::_CommonConstruct() {
  m_brBkgr = NULL;
  m_hBkgrBitmap = NULL;
  m_sizeBkgrBitmap = CSize(0, 0);
  m_BkgrLocation = BACKGR_TILE;
  m_bAutoDestroyBmp = FALSE;
}

void CDialogEx::_FreeBitmap() {
  if (m_hBkgrBitmap != NULL && m_bAutoDestroyBmp) ::DeleteObject(m_hBkgrBitmap);
  m_hBkgrBitmap = NULL;
  m_sizeBkgrBitmap = CSize(0, 0);
}

void CDialogEx::SetBackgroundColor(COLORREF color, BOOL bRepaint) {
  if (m_brBkgr != NULL) ::DeleteObject(m_brBkgr);
  m_brBkgr = color == (COLORREF)-1 ? NULL : ::CreateSolidBrush(color);
  if (bRepaint && m_hWnd != NULL) {
    Invalidate();
    UpdateWindow();
  }
}

void CDialogEx::SetBackgroundImage(HBITMAP hBitmap, BackgroundLocation location,
                                   BOOL bAutoDestroy, BOOL bRepaint) {
  if (hBitmap != m_hBkgrBitmap) _FreeBitmap();
  m_hBkgrBitmap = hBitmap;
  m_BkgrLocation = location;
  m_bAutoDestroyBmp = bAutoDestroy;
  if (hBitmap != NULL) {
    BITMAP bm;
    ::GetObject(hBitmap, sizeof(bm), &bm);
    m_sizeBkgrBitmap = CSize(bm.bmWidth, bm.bmHeight);
  }
  if (bRepaint && m_hWnd != NULL) {
    Invalidate();
    UpdateWindow();
  }
}

BOOL CDialogEx::SetBackgroundImage(UINT uiBmpResId, BackgroundLocation location,
                                   BOOL bRepaint) {
  HBITMAP hBitmap = NULL;
  if (uiBmpResId != 0) {
    LPCTSTR lpszName = MAKEINTRESOURCE(uiBmpResId);
    hBitmap = (HBITMAP)::LoadImage(AfxFindResourceHandle(lpszName, RT_BITMAP), lpszName,
                                   IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (hBitmap == NULL) return FALSE;
  }
  SetBackgroundImage(hBitmap, location, TRUE, bRepaint);
  return TRUE;
}

// The color first (or the dialog's own), then the bitmap over it.
BOOL CDialogEx::OnEraseBkgnd(CDC* pDC) {
  if (m_brBkgr == NULL && m_hBkgrBitmap == NULL) return CDialog::OnEraseBkgnd(pDC);
  CRect rcClient;
  GetClientRect(rcClient);
  HBRUSH hbr = m_brBkgr != NULL ? m_brBkgr : ::GetSysColorBrush(COLOR_BTNFACE);
  ::FillRect(pDC->m_hDC, &rcClient, hbr);
  if (m_hBkgrBitmap == NULL) return TRUE;

  HDC hdcMem = ::CreateCompatibleDC(pDC->m_hDC);
  HGDIOBJ hOld = ::SelectObject(hdcMem, m_hBkgrBitmap);
  int cx = m_sizeBkgrBitmap.cx, cy = m_sizeBkgrBitmap.cy;
  if (m_BkgrLocation == BACKGR_TILE) {
    for (int y = 0; y < rcClient.bottom && cy > 0; y += cy)
      for (int x = 0; x < rcClient.right && cx > 0; x += cx)
        ::BitBlt(pDC->m_hDC, x, y, cx, cy, hdcMem, 0, 0, SRCCOPY);
  } else {
    int x = (m_BkgrLocation == BACKGR_TOPRIGHT || m_BkgrLocation == BACKGR_BOTTOMRIGHT)
                ? rcClient.right - cx
                : 0;
    int y = (m_BkgrLocation == BACKGR_BOTTOMLEFT || m_BkgrLocation == BACKGR_BOTTOMRIGHT)
                ? rcClient.bottom - cy
                : 0;
    ::BitBlt(pDC->m_hDC, x, y, cx, cy, hdcMem, 0, 0, SRCCOPY);
  }
  ::SelectObject(hdcMem, hOld);
  ::DeleteDC(hdcMem);
  return TRUE;
}

// Over a background of its own, the dialog's labels, check boxes and
// radio buttons draw with a transparent background; with only a color,
// the color is their brush.
HBRUSH CDialogEx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  if (m_brBkgr == NULL && m_hBkgrBitmap == NULL) return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
  if (nCtlColor == CTLCOLOR_DLG) return m_brBkgr != NULL ? m_brBkgr : CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
  if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN) {
    // Read-only edit controls send WM_CTLCOLORSTATIC too; they keep theirs.
    TCHAR szClass[16];
    if (pWnd != NULL && ::GetClassName(pWnd->m_hWnd, szClass, 16) &&
        lstrcmpi(szClass, _T("Edit")) == 0)
      return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
    pDC->SetBkMode(TRANSPARENT);
    if (m_hBkgrBitmap != NULL) return (HBRUSH)::GetStockObject(NULL_BRUSH);
    return m_brBkgr;
  }
  return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CDialogEx::OnDestroy() { CDialog::OnDestroy(); }
