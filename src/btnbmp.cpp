// btnbmp.cpp - CBitmapButton, an owner-drawn button showing bitmaps for
// its up, down, focused and disabled states.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>

IMPLEMENT_DYNAMIC(CBitmapButton, CButton)

CBitmapButton::CBitmapButton() {}

CBitmapButton::~CBitmapButton() {}

BOOL CBitmapButton::LoadBitmaps(LPCTSTR lpszBitmapResource, LPCTSTR lpszBitmapResourceSel,
                                LPCTSTR lpszBitmapResourceFocus,
                                LPCTSTR lpszBitmapResourceDisabled) {
  m_bitmap.DeleteObject();
  m_bitmapSel.DeleteObject();
  m_bitmapFocus.DeleteObject();
  m_bitmapDisabled.DeleteObject();

  // Only the "up" bitmap is required.
  if (!m_bitmap.LoadBitmap(lpszBitmapResource)) return FALSE;
  BOOL bAllLoaded = TRUE;
  if (lpszBitmapResourceSel != NULL && !m_bitmapSel.LoadBitmap(lpszBitmapResourceSel))
    bAllLoaded = FALSE;
  if (lpszBitmapResourceFocus != NULL && !m_bitmapFocus.LoadBitmap(lpszBitmapResourceFocus))
    bAllLoaded = FALSE;
  if (lpszBitmapResourceDisabled != NULL &&
      !m_bitmapDisabled.LoadBitmap(lpszBitmapResourceDisabled))
    bAllLoaded = FALSE;
  return bAllLoaded;
}

BOOL CBitmapButton::LoadBitmaps(UINT nIDBitmapResource, UINT nIDBitmapResourceSel,
                                UINT nIDBitmapResourceFocus, UINT nIDBitmapResourceDisabled) {
  return LoadBitmaps(MAKEINTRESOURCE(nIDBitmapResource),
                     nIDBitmapResourceSel ? MAKEINTRESOURCE(nIDBitmapResourceSel) : NULL,
                     nIDBitmapResourceFocus ? MAKEINTRESOURCE(nIDBitmapResourceFocus) : NULL,
                     nIDBitmapResourceDisabled ? MAKEINTRESOURCE(nIDBitmapResourceDisabled) : NULL);
}

// Subclasses a dialog's button and loads the bitmaps named after its
// caption: "NAMEU", "NAMED", "NAMEF" and "NAMEX".
BOOL CBitmapButton::AutoLoad(UINT nID, CWnd* pParent) {
  if (!SubclassDlgItem(nID, pParent)) return FALSE;
  CString strName;
  GetWindowText(strName);
  if (strName.IsEmpty()) return FALSE;
  LoadBitmaps(strName + _T("U"), strName + _T("D"), strName + _T("F"), strName + _T("X"));
  if (m_bitmap.m_hObject == NULL) return FALSE;
  SizeToContent();
  return TRUE;
}

void CBitmapButton::SizeToContent() {
  BITMAP bm;
  if (m_bitmap.m_hObject == NULL || !m_bitmap.GetObject(sizeof(bm), &bm)) return;
  SetWindowPos(NULL, 0, 0, bm.bmWidth, bm.bmHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CBitmapButton::DrawItem(LPDRAWITEMSTRUCT lpDIS) {
  // Pick the bitmap for the state, falling back to the "up" bitmap.
  CBitmap* pBitmap = &m_bitmap;
  UINT state = lpDIS->itemState;
  if ((state & ODS_SELECTED) && m_bitmapSel.m_hObject != NULL)
    pBitmap = &m_bitmapSel;
  else if ((state & ODS_FOCUS) && m_bitmapFocus.m_hObject != NULL)
    pBitmap = &m_bitmapFocus;
  else if ((state & ODS_DISABLED) && m_bitmapDisabled.m_hObject != NULL)
    pBitmap = &m_bitmapDisabled;
  if (pBitmap->m_hObject == NULL) return;

  CDC* pDC = CDC::FromHandle(lpDIS->hDC);
  CDC memDC;
  memDC.CreateCompatibleDC(pDC);
  CBitmap* pOld = memDC.SelectObject(pBitmap);
  CRect rect(lpDIS->rcItem);
  pDC->BitBlt(rect.left, rect.top, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
  memDC.SelectObject(pOld);
}
