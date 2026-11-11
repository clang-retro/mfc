// thread.cpp - CWinThread: threads and the message loop.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CWinThread, CCmdTarget)

BEGIN_MESSAGE_MAP(CWinThread, CCmdTarget)
END_MESSAGE_MAP()

void CWinThread::CommonConstruct() {
  m_pMainWnd = NULL;
  m_pActiveWnd = NULL;
  m_bAutoDelete = TRUE;
  m_hThread = NULL;
  m_nThreadID = 0;
  memset(&m_msgCur, 0, sizeof(m_msgCur));
  m_ptCursorLast = CPoint(-1, -1);
  m_nMsgLast = 0;
  m_pMessageFilter = NULL;
}

CWinThread::CWinThread() : m_pThreadParams(NULL), m_pfnThreadProc(NULL) { CommonConstruct(); }

CWinThread::CWinThread(AFX_THREADPROC pfnThreadProc, LPVOID pParam)
    : m_pThreadParams(pParam), m_pfnThreadProc(pfnThreadProc) {
  CommonConstruct();
}

CWinThread::~CWinThread() {
  if (m_hThread != NULL) ::CloseHandle(m_hThread);
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pCurrentWinThread == this) pState->m_pCurrentWinThread = NULL;
}

void CWinThread::Delete() {
  if (m_bAutoDelete) delete this;
}

static DWORD WINAPI _AfxThreadEntry(LPVOID pParam) {
  CWinThread* pThread = (CWinThread*)pParam;
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  pState->m_pCurrentWinThread = pThread;
  // A thread a regular DLL starts runs in the DLL's state.
  AfxSetModuleState(pThread->m_pModuleState);

  UINT nResult;
  if (pThread->m_pfnThreadProc != NULL) {
    nResult = pThread->m_pfnThreadProc(pThread->m_pThreadParams);
  } else if (!pThread->InitInstance()) {
    nResult = (UINT)pThread->ExitInstance();
  } else {
    nResult = (UINT)pThread->Run();
  }
  AfxEndThread(nResult);
  return 0;
}

BOOL CWinThread::CreateThread(DWORD dwCreateFlags, UINT nStackSize,
                              LPSECURITY_ATTRIBUTES lpSecurityAttrs) {
  DWORD nThreadID = 0;
  m_hThread = ::CreateThread(lpSecurityAttrs, nStackSize, _AfxThreadEntry, this,
                             dwCreateFlags | CREATE_SUSPENDED, &nThreadID);
  if (m_hThread == NULL) return FALSE;
  m_nThreadID = nThreadID;
  if (!(dwCreateFlags & CREATE_SUSPENDED)) {
    // The thread may finish and delete this object as soon as it runs.
    HANDLE hThread = m_hThread;
    ::ResumeThread(hThread);
  }
  return TRUE;
}

CWinThread* AFXAPI AfxBeginThread(AFX_THREADPROC pfnThreadProc, LPVOID pParam, int nPriority,
                                  UINT nStackSize, DWORD dwCreateFlags,
                                  LPSECURITY_ATTRIBUTES lpSecurityAttrs) {
  CWinThread* pThread = new CWinThread(pfnThreadProc, pParam);
  if (!pThread->CreateThread(dwCreateFlags | CREATE_SUSPENDED, nStackSize, lpSecurityAttrs)) {
    pThread->Delete();
    return NULL;
  }
  pThread->SetThreadPriority(nPriority);
  if (!(dwCreateFlags & CREATE_SUSPENDED)) pThread->ResumeThread();
  return pThread;
}

CWinThread* AFXAPI AfxBeginThread(CRuntimeClass* pThreadClass, int nPriority, UINT nStackSize,
                                  DWORD dwCreateFlags, LPSECURITY_ATTRIBUTES lpSecurityAttrs) {
  CWinThread* pThread = (CWinThread*)pThreadClass->CreateObject();
  if (pThread == NULL) AfxThrowMemoryException();
  if (!pThread->CreateThread(dwCreateFlags | CREATE_SUSPENDED, nStackSize, lpSecurityAttrs)) {
    pThread->Delete();
    return NULL;
  }
  pThread->SetThreadPriority(nPriority);
  if (!(dwCreateFlags & CREATE_SUSPENDED)) pThread->ResumeThread();
  return pThread;
}

void AFXAPI AfxEndThread(UINT nExitCode, BOOL bDelete) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  CWinThread* pThread = pState->m_pCurrentWinThread;
  if (pThread != NULL) {
    pState->m_pCurrentWinThread = NULL;
    if (bDelete) pThread->Delete();
  }
  AfxLockTempMaps();
  AfxUnlockTempMaps(TRUE);
  AfxTermThreadState();
  ::ExitThread(nExitCode);
}

BOOL CWinThread::InitInstance() { return FALSE; }

int CWinThread::ExitInstance() { return (int)m_msgCur.wParam; }

CWnd* CWinThread::GetMainWnd() {
  if (m_pActiveWnd != NULL) return m_pActiveWnd;
  return m_pMainWnd;
}

int CWinThread::Run() {
  BOOL bIdle = TRUE;
  LONG lIdleCount = 0;
  for (;;) {
    while (bIdle && !::PeekMessage(&m_msgCur, NULL, 0, 0, PM_NOREMOVE)) {
      if (!OnIdle(lIdleCount++)) bIdle = FALSE;
    }
    do {
      if (!PumpMessage()) return ExitInstance();
      if (IsIdleMessage(&m_msgCur)) {
        bIdle = TRUE;
        lIdleCount = 0;
      }
    } while (::PeekMessage(&m_msgCur, NULL, 0, 0, PM_NOREMOVE));
  }
}

BOOL CWinThread::PumpMessage() {
  if (!::GetMessage(&m_msgCur, NULL, 0, 0)) return FALSE;
  if (m_msgCur.message != WM_KICKIDLE && !PreTranslateMessage(&m_msgCur)) {
    ::TranslateMessage(&m_msgCur);
    ::DispatchMessage(&m_msgCur);
  }
  return TRUE;
}

BOOL AFXAPI AfxPumpMessage() {
  CWinThread* pThread = AfxGetThread();
  if (pThread != NULL) return pThread->PumpMessage();

  MSG* pMsg = &AfxGetThreadState()->m_msgCur;
  if (!::GetMessage(pMsg, NULL, 0, 0)) return FALSE;
  if (pMsg->message != WM_KICKIDLE && !CWnd::WalkPreTranslateTree(NULL, pMsg)) {
    ::TranslateMessage(pMsg);
    ::DispatchMessage(pMsg);
  }
  return TRUE;
}

BOOL CWinThread::DispatchThreadMessageEx(MSG* pMsg) {
  for (const AFX_MSGMAP* pMap = GetMessageMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    const AFX_MSGMAP_ENTRY* lpEntry = NULL;
    if (pMsg->message < 0xC000) {
      lpEntry = FindMessageEntry(pMap->lpEntries, pMsg->message, 0, 0);
    } else {
      for (const AFX_MSGMAP_ENTRY* p = pMap->lpEntries; p->nSig != AfxSig_end; p++) {
        if (p->nSig > AfxSig_registered && p->nMessage == 0xC000 &&
            *(UINT*)p->nSig == pMsg->message) {
          lpEntry = p;
          break;
        }
      }
    }
    if (lpEntry != NULL) {
      lpEntry->pThunk(this, pMsg->message, pMsg->wParam, pMsg->lParam);
      return TRUE;
    }
  }
  return FALSE;
}

BOOL CWinThread::PreTranslateMessage(MSG* pMsg) {
  // Thread messages go to the thread's message map.
  if (pMsg->hwnd == NULL && DispatchThreadMessageEx(pMsg)) return TRUE;

  // A server started by COM may have no main window yet.
  CWnd* pMainWnd = AfxGetMainWnd();
  HWND hWndMain = pMainWnd != NULL ? pMainWnd->m_hWnd : NULL;
  if (CWnd::WalkPreTranslateTree(hWndMain, pMsg)) return TRUE;

  // Modeless dialogs and other top-level windows still get the main
  // window's accelerators.
  if (pMainWnd != NULL && pMsg->hwnd != NULL) {
    CWnd* pWnd = CWnd::FromHandle(pMsg->hwnd);
    if (pWnd->GetTopLevelParent() != pMainWnd) return pMainWnd->PreTranslateMessage(pMsg);
  }
  return FALSE;
}

BOOL CWinThread::IsIdleMessage(MSG* pMsg) {
  // Mouse moves to the same place, and paint and caret blinks, do not
  // restart idle processing.
  if (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_NCMOUSEMOVE) {
    if (m_ptCursorLast == pMsg->pt && pMsg->message == m_nMsgLast) return FALSE;
    m_ptCursorLast = pMsg->pt;
    m_nMsgLast = pMsg->message;
    return TRUE;
  }
  return pMsg->message != WM_PAINT && pMsg->message != 0x0118;
}

void(AFXAPI* _afxpfnInPlaceIdle)() = NULL;

BOOL CWinThread::OnIdle(LONG lCount) {
  if (lCount <= 0) {
    // Let the main window update toolbars, status bars and menus.
    CWnd* pMainWnd = m_pMainWnd;
    if (pMainWnd != NULL && pMainWnd->m_hWnd != NULL && pMainWnd->IsWindowVisible()) {
      AfxCallWndProc(pMainWnd, pMainWnd->m_hWnd, WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0);
      pMainWnd->SendMessageToDescendants(WM_IDLEUPDATECMDUI, (WPARAM)TRUE, 0, TRUE, TRUE);
    }
    if (_afxpfnInPlaceIdle != NULL) _afxpfnInPlaceIdle();
  } else {
    // Free the temporary wrappers made by FromHandle.
    AfxLockTempMaps();
    AfxUnlockTempMaps(TRUE);
  }
  return lCount < 1;
}

LRESULT CWinThread::ProcessWndProcException(CException* e, const MSG* pMsg) {
  if (pMsg->message == WM_CREATE) return -1;  // the window is not created
  if (pMsg->message == WM_PAINT) {
    ::ValidateRect(pMsg->hwnd, NULL);
    return 0;
  }
  if (!e->IsKindOf(RUNTIME_CLASS(CUserException))) {
    if (e->IsKindOf(RUNTIME_CLASS(CMemoryException)))
      AfxMessageBox(AFX_IDP_FAILED_MEMORY_ALLOC, MB_ICONSTOP);
    else
      e->ReportError(MB_ICONSTOP, AFX_IDP_INTERNAL_FAILURE);
  }
  return 0;
}

BOOL CWinThread::ProcessMessageFilter(int code, LPMSG lpMsg) {
  (void)code;
  (void)lpMsg;
  return FALSE;
}
