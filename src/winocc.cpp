// winocc.cpp - the CWnd functions that go to an ActiveX control when the
// window object is a hosted control's (m_pCtrlSite), and to the hosted
// controls of a container window (m_pCtrlCont). Otherwise they are the
// plain Windows calls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxocc.h>

DWORD CWnd::GetStyle() const {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetStyle();
  return (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE);
}

DWORD CWnd::GetExStyle() const {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetExStyle();
  return (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE);
}

BOOL CWnd::ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ModifyStyle(dwRemove, dwAdd, nFlags);
  return ModifyStyle(m_hWnd, dwRemove, dwAdd, nFlags);
}

BOOL CWnd::ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ModifyStyleEx(dwRemove, dwAdd, nFlags);
  return ModifyStyleEx(m_hWnd, dwRemove, dwAdd, nFlags);
}

CWnd* CWnd::GetDlgItem(int nID) const {
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItem(nID);
  return CWnd::FromHandle(::GetDlgItem(m_hWnd, nID));
}

int CWnd::GetDlgCtrlID() const {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->GetDlgCtrlID();
  return ::GetDlgCtrlID(m_hWnd);
}

int CWnd::SetDlgCtrlID(int nID) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetDlgCtrlID(nID);
  return (int)::SetWindowLong(m_hWnd, GWL_ID, nID);
}

void CWnd::SetWindowText(LPCTSTR lpszString) {
  if (m_pCtrlSite != NULL)
    m_pCtrlSite->SetWindowText(lpszString);
  else
    ::SetWindowText(m_hWnd, lpszString);
}

int CWnd::GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const {
  if (m_pCtrlSite == NULL) return ::GetWindowText(m_hWnd, lpszStringBuf, nMaxCount);
  if (nMaxCount <= 0) return 0;
  CString str;
  m_pCtrlSite->GetWindowText(str);
  lstrcpyn(lpszStringBuf, str, nMaxCount);
  return lstrlen(lpszStringBuf);
}

int CWnd::GetWindowTextLength() const {
  if (m_pCtrlSite == NULL) return ::GetWindowTextLength(m_hWnd);
  CString str;
  m_pCtrlSite->GetWindowText(str);
  return str.GetLength();
}

void CWnd::MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint) {
  if (m_pCtrlSite != NULL)
    m_pCtrlSite->MoveWindow(x, y, nWidth, nHeight, bRepaint);
  else
    ::MoveWindow(m_hWnd, x, y, nWidth, nHeight, bRepaint);
}

BOOL CWnd::SetWindowPos(const CWnd* pWndInsertAfter, int x, int y, int cx, int cy,
                        UINT nFlags) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetWindowPos(pWndInsertAfter, x, y, cx, cy, nFlags);
  return ::SetWindowPos(m_hWnd, pWndInsertAfter->GetSafeInsertAfter(), x, y, cx, cy, nFlags);
}

BOOL CWnd::ShowWindow(int nCmdShow) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->ShowWindow(nCmdShow);
  return ::ShowWindow(m_hWnd, nCmdShow);
}

BOOL CWnd::IsWindowEnabled() const {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->IsWindowEnabled();
  return ::IsWindowEnabled(m_hWnd);
}

BOOL CWnd::EnableWindow(BOOL bEnable) {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->EnableWindow(bEnable);
  return ::EnableWindow(m_hWnd, bEnable);
}

CWnd* CWnd::SetFocus() {
  if (m_pCtrlSite != NULL) return m_pCtrlSite->SetFocus();
  return CWnd::FromHandle(::SetFocus(m_hWnd));
}

UINT CWnd::GetDlgItemInt(int nID, BOOL* lpTrans, BOOL bSigned) const {
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItemInt(nID, lpTrans, bSigned);
  return ::GetDlgItemInt(m_hWnd, nID, lpTrans, bSigned);
}

int CWnd::GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const {
  if (m_pCtrlCont != NULL) return m_pCtrlCont->GetDlgItemText(nID, lpStr, nMaxCount);
  return ::GetDlgItemText(m_hWnd, nID, lpStr, nMaxCount);
}

void CWnd::SetDlgItemInt(int nID, UINT nValue, BOOL bSigned) {
  if (m_pCtrlCont != NULL)
    m_pCtrlCont->SetDlgItemInt(nID, nValue, bSigned);
  else
    ::SetDlgItemInt(m_hWnd, nID, nValue, bSigned);
}

void CWnd::SetDlgItemText(int nID, LPCTSTR lpszString) {
  if (m_pCtrlCont != NULL)
    m_pCtrlCont->SetDlgItemText(nID, lpszString);
  else
    ::SetDlgItemText(m_hWnd, nID, lpszString);
}
