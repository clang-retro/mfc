// winocc.cpp - the CWnd functions that go to an ActiveX control when the
// window object is a hosted control's (m_pCtrlSite), and to the hosted
// controls of a container window (m_pCtrlCont). Otherwise they are the
// plain Windows calls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#ifdef _AFX_WIN16
// 16-bit Windows has no OLE controls: m_pCtrlSite and m_pCtrlCont are
// always NULL, and each function makes only the plain Windows call.
#else
#include <afxocc.h>
#endif

DWORD CWnd::GetStyle() const {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetStyle();
#endif
  return (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE);
}

DWORD CWnd::GetExStyle() const {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetExStyle();
#endif
  return (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE);
}

BOOL CWnd::ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ModifyStyle(dwRemove, dwAdd, nFlags);
#endif
  return ModifyStyle(m_hWnd, dwRemove, dwAdd, nFlags);
}

BOOL CWnd::ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ModifyStyleEx(dwRemove, dwAdd, nFlags);
#endif
  return ModifyStyleEx(m_hWnd, dwRemove, dwAdd, nFlags);
}

CWnd* CWnd::GetDlgItem(int nID) const {
#ifndef _AFX_WIN16
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItem(nID);
#endif
  return CWnd::FromHandle(::GetDlgItem(m_hWnd, nID));
}

int CWnd::GetDlgCtrlID() const {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetDlgCtrlID();
#endif
  return ::GetDlgCtrlID(m_hWnd);
}

int CWnd::SetDlgCtrlID(int nID) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetDlgCtrlID(nID);
#endif
#ifdef _AFX_WIN16
  return (int)::SetWindowWord(m_hWnd, GWW_ID, (WORD)nID);
#else
  return (int)::SetWindowLong(m_hWnd, GWL_ID, nID);
#endif
}

void CWnd::SetWindowText(LPCTSTR lpszString) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL)
    m_pCtrlSite->SetWindowText(lpszString);
  else
#endif
    ::SetWindowText(m_hWnd, lpszString);
}

int CWnd::GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const {
#ifdef _AFX_WIN16
  return ::GetWindowText(m_hWnd, lpszStringBuf, nMaxCount);
#else
  if (m_pCtrlSite == NULL) return ::GetWindowText(m_hWnd, lpszStringBuf, nMaxCount);
  if (nMaxCount <= 0) return 0;
  CString str;
  m_pCtrlSite->GetWindowText(str);
  lstrcpyn(lpszStringBuf, str, nMaxCount);
  return lstrlen(lpszStringBuf);
#endif
}

int CWnd::GetWindowTextLength() const {
#ifdef _AFX_WIN16
  return ::GetWindowTextLength(m_hWnd);
#else
  if (m_pCtrlSite == NULL) return ::GetWindowTextLength(m_hWnd);
  CString str;
  m_pCtrlSite->GetWindowText(str);
  return str.GetLength();
#endif
}

void CWnd::MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL)
    m_pCtrlSite->MoveWindow(x, y, nWidth, nHeight, bRepaint);
  else
#endif
    ::MoveWindow(m_hWnd, x, y, nWidth, nHeight, bRepaint);
}

BOOL CWnd::SetWindowPos(const CWnd* pWndInsertAfter, int x, int y, int cx, int cy,
                        UINT nFlags) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetWindowPos(pWndInsertAfter, x, y, cx, cy, nFlags);
#endif
  return ::SetWindowPos(m_hWnd, pWndInsertAfter->GetSafeInsertAfter(), x, y, cx, cy, nFlags);
}

BOOL CWnd::ShowWindow(int nCmdShow) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ShowWindow(nCmdShow);
#endif
  return ::ShowWindow(m_hWnd, nCmdShow);
}

BOOL CWnd::IsWindowEnabled() const {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->IsWindowEnabled();
#endif
  return ::IsWindowEnabled(m_hWnd);
}

BOOL CWnd::EnableWindow(BOOL bEnable) {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->EnableWindow(bEnable);
#endif
  return ::EnableWindow(m_hWnd, bEnable);
}

CWnd* CWnd::SetFocus() {
#ifndef _AFX_WIN16
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetFocus();
#endif
  return CWnd::FromHandle(::SetFocus(m_hWnd));
}

UINT CWnd::GetDlgItemInt(int nID, BOOL* lpTrans, BOOL bSigned) const {
#ifndef _AFX_WIN16
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItemInt(nID, lpTrans, bSigned);
#endif
  return ::GetDlgItemInt(m_hWnd, nID, lpTrans, bSigned);
}

int CWnd::GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const {
#ifndef _AFX_WIN16
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItemText(nID, lpStr, nMaxCount);
#endif
  return ::GetDlgItemText(m_hWnd, nID, lpStr, nMaxCount);
}

void CWnd::SetDlgItemInt(int nID, UINT nValue, BOOL bSigned) {
#ifndef _AFX_WIN16
  if (m_pCtrlCont != NULL)
    m_pCtrlCont->SetDlgItemInt(nID, nValue, bSigned);
  else
#endif
    ::SetDlgItemInt(m_hWnd, nID, nValue, bSigned);
}

void CWnd::SetDlgItemText(int nID, LPCTSTR lpszString) {
#ifndef _AFX_WIN16
  if (m_pCtrlCont != NULL)
    m_pCtrlCont->SetDlgItemText(nID, lpszString);
  else
#endif
    ::SetDlgItemText(m_hWnd, nID, lpszString);
}
