// btnvista.cpp - the CButton members for the XP and Vista button
// messages (command link notes, split buttons) and CSplitButton.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxcmn.h>

// ---------------------------------------------------------------------
// Command link notes. The control always keeps the note as UTF-16.

BOOL CButton::SetNote(LPCTSTR lpszNote) {
  CStringW strNote(lpszNote != NULL ? lpszNote : _T(""));
  return (BOOL)::SendMessage(m_hWnd, BCM_SETNOTE, 0, (LPARAM)(LPCWSTR)strNote);
}

BOOL CButton::GetNote(LPTSTR lpszNote, UINT* cchNote) const {
  if (cchNote == NULL) return FALSE;
#ifdef _UNICODE
  DWORD cch = *cchNote;
  BOOL bOK = (BOOL)::SendMessage(m_hWnd, BCM_GETNOTE, (WPARAM)&cch, (LPARAM)lpszNote);
  *cchNote = cch;
  return bOK;
#else
  UINT nLen = GetNoteLength();
  CStringW strNote;
  DWORD cch = nLen + 1;
  BOOL bOK = (BOOL)::SendMessage(m_hWnd, BCM_GETNOTE, (WPARAM)&cch,
                                 (LPARAM)strNote.GetBufferSetLength((int)nLen + 1));
  strNote.ReleaseBuffer();
  if (!bOK) return FALSE;
  CString strA(strNote);
  if (lpszNote == NULL || *cchNote <= (UINT)strA.GetLength()) {
    *cchNote = strA.GetLength() + 1;
    return FALSE;
  }
  lstrcpyn(lpszNote, strA, (int)*cchNote);
  *cchNote = strA.GetLength();
  return TRUE;
#endif
}

CString CButton::GetNote() const {
  UINT nLen = GetNoteLength();
  CStringW strNote;
  DWORD cch = nLen + 1;
  ::SendMessage(m_hWnd, BCM_GETNOTE, (WPARAM)&cch,
                (LPARAM)strNote.GetBufferSetLength((int)nLen + 1));
  strNote.ReleaseBuffer();
  return CString(strNote);
}

// ---------------------------------------------------------------------
// Split button parts, each a field of BUTTON_SPLITINFO.

static BOOL _AfxGetSplitInfo(const CButton* pButton, UINT mask, BUTTON_SPLITINFO* pInfo) {
  memset(pInfo, 0, sizeof(*pInfo));
  pInfo->mask = mask;
  return pButton->GetSplitInfo(pInfo);
}

UINT CButton::GetSplitStyle() const {
  BUTTON_SPLITINFO info;
  return _AfxGetSplitInfo(this, BCSIF_STYLE, &info) ? info.uSplitStyle : 0;
}

BOOL CButton::SetSplitStyle(UINT uSplitStyle) {
  BUTTON_SPLITINFO info;
  memset(&info, 0, sizeof(info));
  info.mask = BCSIF_STYLE;
  info.uSplitStyle = uSplitStyle;
  return SetSplitInfo(&info);
}

BOOL CButton::GetSplitSize(LPSIZE pSize) const {
  if (pSize == NULL) return FALSE;
  BUTTON_SPLITINFO info;
  if (!_AfxGetSplitInfo(this, BCSIF_SIZE, &info)) return FALSE;
  *pSize = info.size;
  return TRUE;
}

BOOL CButton::SetSplitSize(LPSIZE pSize) {
  if (pSize == NULL) return FALSE;
  BUTTON_SPLITINFO info;
  memset(&info, 0, sizeof(info));
  info.mask = BCSIF_SIZE;
  info.size = *pSize;
  return SetSplitInfo(&info);
}

CImageList* CButton::GetSplitImageList() const {
  BUTTON_SPLITINFO info;
  if (!_AfxGetSplitInfo(this, BCSIF_IMAGE, &info) || info.himlGlyph == NULL) return NULL;
  return CImageList::FromHandle(info.himlGlyph);
}

BOOL CButton::SetSplitImageList(CImageList* pSplitImageList) {
  BUTTON_SPLITINFO info;
  memset(&info, 0, sizeof(info));
  info.mask = BCSIF_IMAGE;
  info.himlGlyph = pSplitImageList != NULL ? pSplitImageList->GetSafeHandle() : NULL;
  return SetSplitInfo(&info);
}

// With BCSIF_GLYPH, himlGlyph holds a Marlett character, not an image list.
TCHAR CButton::GetSplitGlyph() const {
  BUTTON_SPLITINFO info;
  if (!_AfxGetSplitInfo(this, BCSIF_GLYPH, &info)) return 0;
  return (TCHAR)(UINT_PTR)info.himlGlyph;
}

BOOL CButton::SetSplitGlyph(TCHAR chGlyph) {
  BUTTON_SPLITINFO info;
  memset(&info, 0, sizeof(info));
  info.mask = BCSIF_GLYPH;
  info.himlGlyph = (HIMAGELIST)(UINT_PTR)chGlyph;
  return SetSplitInfo(&info);
}

// ---------------------------------------------------------------------
// CSplitButton

IMPLEMENT_DYNAMIC(CSplitButton, CButton)

BEGIN_MESSAGE_MAP(CSplitButton, CButton)
  ON_NOTIFY_REFLECT(BCN_DROPDOWN, OnDropDown)
END_MESSAGE_MAP()

CSplitButton::CSplitButton() : m_pMenu(NULL), m_nMenuId((UINT)-1), m_nSubMenuId((UINT)-1) {}

CSplitButton::CSplitButton(UINT nMenuId, UINT nSubMenuId)
    : m_pMenu(NULL), m_nMenuId(nMenuId), m_nSubMenuId(nSubMenuId) {}

CSplitButton::CSplitButton(CMenu* pMenu)
    : m_pMenu(pMenu), m_nMenuId((UINT)-1), m_nSubMenuId((UINT)-1) {}

CSplitButton::~CSplitButton() {}

BOOL CSplitButton::Create(LPCTSTR lpszCaption, DWORD dwStyle, const RECT& rect,
                          CWnd* pParentWnd, UINT nID) {
  // Keep a default split button default; any other type becomes a split button.
  if ((dwStyle & BS_TYPEMASK) != BS_DEFSPLITBUTTON)
    dwStyle = (dwStyle & ~BS_TYPEMASK) | BS_SPLITBUTTON;
  return CButton::Create(lpszCaption, dwStyle, rect, pParentWnd, nID);
}

void CSplitButton::SetDropDownMenu(UINT nMenuId, UINT nSubMenuId) {
  m_pMenu = NULL;
  m_nMenuId = nMenuId;
  m_nSubMenuId = nSubMenuId;
}

void CSplitButton::SetDropDownMenu(CMenu* pMenu) {
  m_pMenu = pMenu;
  m_nMenuId = (UINT)-1;
  m_nSubMenuId = (UINT)-1;
}

void CSplitButton::OnDropDown(NMHDR* pNMHDR, LRESULT* pResult) {
  *pResult = 0;
  LPNMBCDROPDOWN pDropDown = (LPNMBCDROPDOWN)pNMHDR;
  CMenu menuLoaded;
  CMenu* pPopup = m_pMenu;
  if (pPopup == NULL) {
    if (m_nMenuId == (UINT)-1 || !menuLoaded.LoadMenu(m_nMenuId)) return;
    pPopup = menuLoaded.GetSubMenu((int)m_nSubMenuId);
    if (pPopup == NULL) return;
  }

  // The menu drops from the bottom left of the button.
  CPoint pt(pDropDown->rcButton.left, pDropDown->rcButton.bottom);
  ClientToScreen(&pt);
  CWnd* pOwner = GetParent();
  SetDropDownState(TRUE);
  pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, pt.x, pt.y,
                         pOwner != NULL ? pOwner : this);
  SetDropDownState(FALSE);
}
