// olemsgf.cpp - COleMessageFilter: the thread's IMessageFilter, which
// refuses calls while the application is busy and decides what to do
// while its own calls wait or are refused.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>
#include <afxodlgs.h>

IMPLEMENT_DYNAMIC(COleMessageFilter, CCmdTarget)

BEGIN_INTERFACE_MAP(COleMessageFilter, CCmdTarget)
  INTERFACE_PART(COleMessageFilter, IID_IMessageFilter, MessageFilter)
END_INTERFACE_MAP()

COleMessageFilter::COleMessageFilter()
    : m_nBusyCount(0),
      m_bEnableBusy(TRUE),
      m_bEnableNotResponding(TRUE),
      m_bUnblocking(FALSE),
      m_nRetryReply(10),
      m_nBusyReply(SERVERCALL_RETRYLATER),
      m_nTimeout(8000),
      m_bRegistered(FALSE) {}

COleMessageFilter::~COleMessageFilter() { Revoke(); }

BOOL COleMessageFilter::Register() {
  if (m_bRegistered) return TRUE;
  LPMESSAGEFILTER lpOld = NULL;
  if (FAILED(::CoRegisterMessageFilter(&m_xMessageFilter, &lpOld))) return FALSE;
  if (lpOld != NULL) lpOld->Release();
  m_bRegistered = TRUE;
  return TRUE;
}

void COleMessageFilter::Revoke() {
  if (!m_bRegistered) return;
  LPMESSAGEFILTER lpOld = NULL;
  ::CoRegisterMessageFilter(NULL, &lpOld);
  if (lpOld != NULL) lpOld->Release();
  m_bRegistered = FALSE;
}

void COleMessageFilter::BeginBusyState() { m_nBusyCount++; }

void COleMessageFilter::EndBusyState() {
  if (m_nBusyCount > 0) m_nBusyCount--;
}

BOOL COleMessageFilter::OnMessagePending(const MSG* pMsg) {
  if (pMsg->message != WM_PAINT) return FALSE;
  MSG msg;
  if (!::PeekMessage(&msg, pMsg->hwnd, WM_PAINT, WM_PAINT, PM_REMOVE | PM_NOYIELD)) return FALSE;
  ::DispatchMessage(&msg);
  return TRUE;
}

BOOL COleMessageFilter::IsSignificantMessage(MSG* pMsg) {
  UINT n = pMsg->message;
  return (n >= WM_KEYFIRST && n <= WM_KEYLAST) || (n >= WM_MOUSEFIRST && n <= WM_MOUSELAST) ||
         (n >= WM_NCMOUSEMOVE && n <= WM_NCMBUTTONDBLCLK) || n == WM_ACTIVATE ||
         n == WM_ACTIVATEAPP;
}

// The Server Busy dialog: Switch To and Retry retry, Cancel cancels.
int COleMessageFilter::OnBusyDialog(HTASK htaskBusy) {
  COleBusyDialog dlg(htaskBusy, FALSE);
  return dlg.DoModal() == IDOK && dlg.GetSelectionType() != COleBusyDialog::callUnblocked ? 0
                                                                                           : -1;
}

int COleMessageFilter::OnNotRespondingDialog(HTASK htaskBusy) {
  COleBusyDialog dlg(htaskBusy, TRUE);
  return dlg.DoModal() == IDOK && dlg.GetSelectionType() != COleBusyDialog::callUnblocked ? 0
                                                                                           : -1;
}

// ---------------------------------------------------------------------
// IMessageFilter

#define FILTER() METHOD_PROLOGUE_EX_(COleMessageFilter, MessageFilter)

STDMETHODIMP_(ULONG) COleMessageFilter::XMessageFilter::AddRef() {
  FILTER();
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleMessageFilter::XMessageFilter::Release() {
  FILTER();
  return pThis->ExternalRelease();
}

STDMETHODIMP COleMessageFilter::XMessageFilter::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  FILTER();
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

// Busy, a new call is refused; a call made while one of ours waits (a
// callback) is still taken.
STDMETHODIMP_(DWORD) COleMessageFilter::XMessageFilter::HandleInComingCall(DWORD dwCallType, HTASK,
                                                                           DWORD,
                                                                           LPINTERFACEINFO) {
  FILTER();
  if (pThis->m_nBusyCount == 0) return SERVERCALL_ISHANDLED;
  if (dwCallType == CALLTYPE_TOPLEVEL || dwCallType == CALLTYPE_TOPLEVEL_CALLPENDING)
    return pThis->m_nBusyReply;
  return SERVERCALL_ISHANDLED;
}

// Refused for good, a call is cancelled; refused for now, it is retried
// until the timeout, and then the user decides.
STDMETHODIMP_(DWORD) COleMessageFilter::XMessageFilter::RetryRejectedCall(HTASK htaskCallee,
                                                                          DWORD dwTickCount,
                                                                          DWORD dwRejectType) {
  FILTER();
  if (dwRejectType == SERVERCALL_REJECTED) return (DWORD)-1;
  if (dwTickCount < pThis->m_nTimeout || !pThis->m_bEnableBusy || pThis->m_bUnblocking)
    return pThis->m_nRetryReply;
  pThis->m_bUnblocking = TRUE;
  int nResult = pThis->OnBusyDialog(htaskCallee);
  pThis->m_bUnblocking = FALSE;
  return nResult == -1 ? (DWORD)-1 : 0;
}

// A message while a call waits: past the timeout, with the user trying
// to work, Not Responding asks whether to go on waiting.
STDMETHODIMP_(DWORD) COleMessageFilter::XMessageFilter::MessagePending(HTASK htaskCallee,
                                                                       DWORD dwTickCount,
                                                                       DWORD) {
  FILTER();
  MSG msg;
  BOOL bMsg = ::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE | PM_NOYIELD);
  if (bMsg && dwTickCount >= pThis->m_nTimeout && pThis->m_bEnableNotResponding &&
      !pThis->m_bUnblocking && pThis->IsSignificantMessage(&msg)) {
    pThis->m_bUnblocking = TRUE;
    int nResult = pThis->OnNotRespondingDialog(htaskCallee);
    pThis->m_bUnblocking = FALSE;
    if (nResult == -1) return PENDINGMSG_CANCELCALL;
  }
  if (bMsg) pThis->OnMessagePending(&msg);
  return PENDINGMSG_WAITDEFPROCESS;
}

COleMessageFilter* AFXAPI AfxOleGetMessageFilter() {
  CWinThread* pThread = AfxGetThread();
  return pThread != NULL ? pThread->m_pMessageFilter : NULL;
}
