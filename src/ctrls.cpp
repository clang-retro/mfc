// ctrls.cpp - the standard Windows controls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CStatic, CWnd)
IMPLEMENT_DYNAMIC(CButton, CWnd)
IMPLEMENT_DYNAMIC(CListBox, CWnd)
IMPLEMENT_DYNAMIC(CComboBox, CWnd)
IMPLEMENT_DYNAMIC(CEdit, CWnd)
IMPLEMENT_DYNAMIC(CScrollBar, CWnd)

static BOOL _AfxCreateControl(CWnd* pWnd, LPCTSTR lpszClass, LPCTSTR lpszText, DWORD dwStyle,
                              const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return pWnd->CWnd::Create(lpszClass, lpszText, dwStyle, rect, pParentWnd, nID);
}

BOOL CStatic::Create(LPCTSTR lpszText, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                     UINT nID) {
  return _AfxCreateControl(this, _T("STATIC"), lpszText, dwStyle, rect, pParentWnd, nID);
}

void CStatic::DrawItem(LPDRAWITEMSTRUCT) {}

BOOL CStatic::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (message != WM_DRAWITEM) return CWnd::OnChildNotify(message, wParam, lParam, pResult);
  DrawItem((LPDRAWITEMSTRUCT)lParam);
  return TRUE;
}

BOOL CButton::Create(LPCTSTR lpszCaption, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                     UINT nID) {
  return _AfxCreateControl(this, _T("BUTTON"), lpszCaption, dwStyle, rect, pParentWnd, nID);
}

void CButton::DrawItem(LPDRAWITEMSTRUCT) {}

BOOL CButton::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (message != WM_DRAWITEM) return CWnd::OnChildNotify(message, wParam, lParam, pResult);
  DrawItem((LPDRAWITEMSTRUCT)lParam);
  return TRUE;
}

BOOL CListBox::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return _AfxCreateControl(this, _T("LISTBOX"), NULL, dwStyle, rect, pParentWnd, nID);
}

void CListBox::GetText(int nIndex, CString& rString) const {
  int nLen = (int)::SendMessage(m_hWnd, LB_GETTEXTLEN, (WPARAM)nIndex, 0);
  if (nLen == LB_ERR) {
    rString.Empty();
    return;
  }
  ::SendMessage(m_hWnd, LB_GETTEXT, (WPARAM)nIndex, (LPARAM)rString.GetBufferSetLength(nLen));
  rString.ReleaseBuffer();
}

void CListBox::DrawItem(LPDRAWITEMSTRUCT) {}
void CListBox::MeasureItem(LPMEASUREITEMSTRUCT) {}
int CListBox::CompareItem(LPCOMPAREITEMSTRUCT) { return 0; }
void CListBox::DeleteItem(LPDELETEITEMSTRUCT) {}
int CListBox::VKeyToItem(UINT, UINT) { return -1; }
int CListBox::CharToItem(UINT, UINT) { return -1; }

BOOL CListBox::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  switch (message) {
    case WM_DRAWITEM:
      DrawItem((LPDRAWITEMSTRUCT)lParam);
      break;
    case WM_MEASUREITEM:
      MeasureItem((LPMEASUREITEMSTRUCT)lParam);
      break;
    case WM_COMPAREITEM:
      if (pResult != NULL) *pResult = CompareItem((LPCOMPAREITEMSTRUCT)lParam);
      break;
    case WM_DELETEITEM:
      DeleteItem((LPDELETEITEMSTRUCT)lParam);
      break;
    case WM_VKEYTOITEM:
      if (pResult != NULL) *pResult = VKeyToItem(LOWORD(wParam), HIWORD(wParam));
      break;
    case WM_CHARTOITEM:
      if (pResult != NULL) *pResult = CharToItem(LOWORD(wParam), HIWORD(wParam));
      break;
    default:
      return CWnd::OnChildNotify(message, wParam, lParam, pResult);
  }
  return TRUE;
}

BOOL CComboBox::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return _AfxCreateControl(this, _T("COMBOBOX"), NULL, dwStyle, rect, pParentWnd, nID);
}

void CComboBox::GetLBText(int nIndex, CString& rString) const {
  int nLen = (int)::SendMessage(m_hWnd, CB_GETLBTEXTLEN, (WPARAM)nIndex, 0);
  if (nLen == CB_ERR) {
    rString.Empty();
    return;
  }
  ::SendMessage(m_hWnd, CB_GETLBTEXT, (WPARAM)nIndex, (LPARAM)rString.GetBufferSetLength(nLen));
  rString.ReleaseBuffer();
}

void CComboBox::DrawItem(LPDRAWITEMSTRUCT) {}
void CComboBox::MeasureItem(LPMEASUREITEMSTRUCT) {}
int CComboBox::CompareItem(LPCOMPAREITEMSTRUCT) { return 0; }
void CComboBox::DeleteItem(LPDELETEITEMSTRUCT) {}

BOOL CComboBox::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  switch (message) {
    case WM_DRAWITEM:
      DrawItem((LPDRAWITEMSTRUCT)lParam);
      break;
    case WM_MEASUREITEM:
      MeasureItem((LPMEASUREITEMSTRUCT)lParam);
      break;
    case WM_COMPAREITEM:
      if (pResult != NULL) *pResult = CompareItem((LPCOMPAREITEMSTRUCT)lParam);
      break;
    case WM_DELETEITEM:
      DeleteItem((LPDELETEITEMSTRUCT)lParam);
      break;
    default:
      return CWnd::OnChildNotify(message, wParam, lParam, pResult);
  }
  return TRUE;
}

BOOL CEdit::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return _AfxCreateControl(this, _T("EDIT"), NULL, dwStyle, rect, pParentWnd, nID);
}

BOOL CScrollBar::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  return _AfxCreateControl(this, _T("SCROLLBAR"), NULL, dwStyle, rect, pParentWnd, nID);
}

int CScrollBar::GetScrollLimit() {
  SCROLLINFO info;
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE;
  if (!::GetScrollInfo(m_hWnd, SB_CTL, &info)) return 0;
  int nMax = info.nMax;
  if (info.nPage > 1) nMax -= (int)info.nPage - 1;
  return nMax;
}
