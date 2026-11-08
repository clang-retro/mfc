// dbview.cpp - CRecordView and the DDX_Field functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dbimpl.h"
#include "ddxfield.h"

IMPLEMENT_DYNAMIC(CRecordView, CFormView)

BEGIN_MESSAGE_MAP(CRecordView, CFormView)
  ON_COMMAND_EX(ID_RECORD_FIRST, OnMove)
  ON_COMMAND_EX(ID_RECORD_PREV, OnMove)
  ON_COMMAND_EX(ID_RECORD_NEXT, OnMove)
  ON_COMMAND_EX(ID_RECORD_LAST, OnMove)
  ON_UPDATE_COMMAND_UI(ID_RECORD_FIRST, OnUpdateRecordFirst)
  ON_UPDATE_COMMAND_UI(ID_RECORD_PREV, OnUpdateRecordPrev)
  ON_UPDATE_COMMAND_UI(ID_RECORD_NEXT, OnUpdateRecordNext)
  ON_UPDATE_COMMAND_UI(ID_RECORD_LAST, OnUpdateRecordLast)
END_MESSAGE_MAP()

CRecordView::CRecordView(LPCTSTR lpszTemplateName)
    : CFormView(lpszTemplateName), m_bOnFirstRecord(TRUE), m_bOnLastRecord(FALSE) {}

CRecordView::CRecordView(UINT nIDTemplate)
    : CFormView(nIDTemplate), m_bOnFirstRecord(TRUE), m_bOnLastRecord(FALSE) {}

CRecordView::~CRecordView() {}

void CRecordView::OnInitialUpdate() {
  CRecordset* pSet = OnGetRecordset();
  if (pSet != NULL && !pSet->IsOpen()) {
    CWaitCursor wait;
    pSet->Open();
  }
  CFormView::OnInitialUpdate();
}

BOOL CRecordView::IsOnFirstRecord() {
  CRecordset* pSet = OnGetRecordset();
  if (pSet == NULL || !pSet->IsOpen()) return TRUE;
  CRecordsetStatus status;
  pSet->GetStatus(status);
  return status.m_lCurrentRecord <= 0;
}

BOOL CRecordView::IsOnLastRecord() {
  CRecordset* pSet = OnGetRecordset();
  if (pSet == NULL || !pSet->IsOpen() || pSet->IsEOF()) return TRUE;
  CRecordsetStatus status;
  pSet->GetStatus(status);
  // Like MFC, the last record is known once a move has gone past it.
  return status.m_bRecordCountFinal && status.m_lCurrentRecord == pSet->GetRecordCount() - 1;
}

BOOL CRecordView::OnMove(UINT nIDMoveCommand) {
  CRecordset* pSet = OnGetRecordset();
  if (pSet == NULL || !pSet->IsOpen()) return TRUE;
  // Write the form's values back first.
  if (pSet->CanUpdate() && !pSet->IsBOF() && !pSet->IsEOF() && !pSet->IsDeleted()) {
    pSet->Edit();
    if (!UpdateData()) {
      pSet->CancelUpdate();
      return TRUE;
    }
    pSet->Update();
  }
  switch (nIDMoveCommand) {
  case ID_RECORD_PREV:
    pSet->MovePrev();
    if (pSet->IsBOF()) pSet->MoveFirst();
    break;
  case ID_RECORD_FIRST:
    pSet->MoveFirst();
    break;
  case ID_RECORD_NEXT:
    pSet->MoveNext();
    if (pSet->IsEOF()) {
      if (pSet->CanScroll()) {
        pSet->MoveLast();
      } else {
        pSet->SetFieldNull(NULL);
      }
    }
    break;
  case ID_RECORD_LAST:
    pSet->MoveLast();
    break;
  default:
    return FALSE;
  }
  m_bOnFirstRecord = IsOnFirstRecord();
  m_bOnLastRecord = IsOnLastRecord();
  UpdateData(FALSE);
  return TRUE;
}

void CRecordView::OnUpdateRecordFirst(CCmdUI* pCmdUI) {
  CRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(pSet != NULL && pSet->CanScroll() && !(pSet->IsBOF() && pSet->IsEOF()) &&
                 !IsOnFirstRecord());
}

void CRecordView::OnUpdateRecordPrev(CCmdUI* pCmdUI) { OnUpdateRecordFirst(pCmdUI); }

void CRecordView::OnUpdateRecordNext(CCmdUI* pCmdUI) {
  CRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(pSet != NULL && pSet->IsOpen() && !(pSet->IsBOF() && pSet->IsEOF()) &&
                 !IsOnLastRecord());
}

void CRecordView::OnUpdateRecordLast(CCmdUI* pCmdUI) {
  CRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(pSet != NULL && pSet->IsOpen() && pSet->CanScroll() &&
                 !(pSet->IsBOF() && pSet->IsEOF()) && !IsOnLastRecord());
}

// ---------------------------------------------------------------------
// DDX_Field

_AFX_DDX_FIELD_FUNCTIONS(CRecordset)
