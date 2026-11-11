// cmdtarg.cpp - CCmdTarget, message map search, CCmdUI, wait cursors.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CCmdTarget, CObject)

CCmdTarget::CCmdTarget()
    : m_dwRef(1),
      m_pOuterUnknown(NULL),
      m_pModuleState(AfxGetModuleState()),
      m_bAutomation(FALSE),
      m_bConnections(FALSE),
      m_bAggregation(FALSE),
      m_bResultExpected(TRUE) {}

CCmdTarget::~CCmdTarget() {}

const AFX_MSGMAP* CCmdTarget::GetMessageMap() const { return GetThisMessageMap(); }

const AFX_MSGMAP* PASCAL CCmdTarget::GetThisMessageMap() {
  static const AFX_MSGMAP_ENTRY _messageEntries[] = {
      {0, 0, 0, 0, AfxSig_end, (AFX_PMSG)0, (AFX_THUNK)0}};
  static const AFX_MSGMAP messageMap = {NULL, &_messageEntries[0]};
  return &messageMap;
}

const AFX_MSGMAP_ENTRY* PASCAL CCmdTarget::FindMessageEntry(const AFX_MSGMAP_ENTRY* lpEntry,
                                                             UINT nMsg, UINT nCode, UINT nID) {
  for (; lpEntry->nSig != AfxSig_end; lpEntry++) {
    if (lpEntry->nSig > AfxSig_registered) continue;  // registered message
    if (lpEntry->nMessage != nMsg) continue;
    if (nCode == CN_UPDATE_COMMAND_UI) {
      if (lpEntry->nCode != CN_UPDATE_COMMAND_UI) continue;
    } else {
      if (lpEntry->nCode == CN_UPDATE_COMMAND_UI) continue;
      if ((WORD)lpEntry->nCode != (WORD)nCode) continue;
    }
    if (nID < lpEntry->nID || nID > lpEntry->nLastID) continue;
    return lpEntry;
  }
  return NULL;
}

BOOL CCmdTarget::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  // Events of hosted controls go through the event sink map.
  if ((UINT)nCode == CN_EVENT) return OnEvent(nID, (AFX_EVENT*)pExtra, pHandlerInfo);
  // nCode carries the message in its high word (0 means WM_COMMAND),
  // except for CN_UPDATE_COMMAND_UI.
  UINT nMsg = 0;
  UINT nCodeOnly = (UINT)nCode;
  if ((UINT)nCode != CN_UPDATE_COMMAND_UI) {
    nMsg = HIWORD(nCode);
    nCodeOnly = LOWORD(nCode);
  }
  if (nMsg == 0) nMsg = WM_COMMAND;

  for (const AFX_MSGMAP* pMap = GetMessageMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    const AFX_MSGMAP_ENTRY* lpEntry = FindMessageEntry(pMap->lpEntries, nMsg, nCodeOnly, nID);
    if (lpEntry == NULL) continue;
    if (pHandlerInfo != NULL) {
      pHandlerInfo->pTarget = this;
      pHandlerInfo->pmf = lpEntry->pfn;
      return TRUE;
    }
    return (BOOL)lpEntry->pThunk(this, nMsg, (WPARAM)nID, (LPARAM)pExtra);
  }
  return FALSE;
}

void CCmdTarget::BeginWaitCursor() { AfxGetApp()->DoWaitCursor(1); }
void CCmdTarget::EndWaitCursor() { AfxGetApp()->DoWaitCursor(-1); }
void CCmdTarget::RestoreWaitCursor() { AfxGetApp()->DoWaitCursor(0); }

void CWaitCursor::AfxGetAppWait(int n) {
  CWinApp* pApp = AfxGetApp();
  if (pApp == NULL) return;
  pApp->DoWaitCursor(n == 1 ? 1 : n == 0 ? -1 : 0);
}

// ---------------------------------------------------------------------
// CCmdUI

CCmdUI::CCmdUI()
    : m_nID(0),
      m_nIndex(0),
      m_pMenu(NULL),
      m_pSubMenu(NULL),
      m_pOther(NULL),
      m_pParentMenu(NULL),
      m_bEnableChanged(FALSE),
      m_bContinueRouting(FALSE),
      m_nIndexMax(0) {}

void CCmdUI::Enable(BOOL bOn) {
  if (m_pMenu != NULL) {
    if (m_pSubMenu != NULL) return;  // popups are left alone
    m_pMenu->EnableMenuItem(m_nIndex, MF_BYPOSITION | (bOn ? MF_ENABLED : (MF_DISABLED | MF_GRAYED)));
  } else if (m_pOther != NULL) {
    // Move the focus on before disabling the control that has it.
    if (!bOn && ::GetFocus() == m_pOther->m_hWnd) {
      HWND hWndParent = ::GetParent(m_pOther->m_hWnd);
      if (hWndParent != NULL) {
        HWND hNext = ::GetNextDlgTabItem(hWndParent, m_pOther->m_hWnd, FALSE);
        if (hNext != NULL) ::SetFocus(hNext);
      }
    }
    m_pOther->EnableWindow(bOn);
  }
  m_bEnableChanged = TRUE;
}

void CCmdUI::SetCheck(int nCheck) {
  if (m_pMenu != NULL) {
    if (m_pSubMenu != NULL) return;
    m_pMenu->CheckMenuItem(m_nIndex, MF_BYPOSITION | (nCheck ? MF_CHECKED : MF_UNCHECKED));
  } else if (m_pOther != NULL) {
    if (m_pOther->SendMessage(WM_GETDLGCODE) & DLGC_BUTTON)
      m_pOther->SendMessage(BM_SETCHECK, (WPARAM)nCheck);
  }
}

void CCmdUI::SetRadio(BOOL bOn) {
  if (m_pMenu != NULL) {
    if (m_pSubMenu != NULL) return;
    if (bOn)
      m_pMenu->CheckMenuRadioItem(m_nIndex, m_nIndex, m_nIndex, MF_BYPOSITION);
    else
      m_pMenu->CheckMenuItem(m_nIndex, MF_BYPOSITION | MF_UNCHECKED);
  } else {
    SetCheck(bOn ? 1 : 0);
  }
}

void CCmdUI::SetText(LPCTSTR lpszText) {
  if (m_pMenu != NULL) {
    if (m_pSubMenu != NULL) return;
    UINT nState = m_pMenu->GetMenuState(m_nIndex, MF_BYPOSITION);
    nState &= ~(MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR | MF_POPUP | MF_HILITE);
    m_pMenu->ModifyMenu(m_nIndex, MF_BYPOSITION | MF_STRING | nState, m_nID, lpszText);
  } else if (m_pOther != NULL) {
    CString strOld;
    m_pOther->GetWindowText(strOld);
    if (strOld != lpszText) m_pOther->SetWindowText(lpszText);
  }
}

BOOL CCmdUI::DoUpdate(CCmdTarget* pTarget, BOOL bDisableIfNoHndler) {
  if (m_nID == 0 || LOWORD(m_nID) == 0xFFFF) return TRUE;  // separators and IDC_STATIC
  m_bEnableChanged = FALSE;
  BOOL bResult = pTarget->OnCmdMsg(m_nID, (int)CN_UPDATE_COMMAND_UI, this, NULL);
  if (bDisableIfNoHndler && !m_bEnableChanged) {
    AFX_CMDHANDLERINFO info;
    info.pTarget = NULL;
    BOOL bHandler = pTarget->OnCmdMsg(m_nID, CN_COMMAND, this, &info);
    Enable(bHandler);
  }
  return bResult;
}
