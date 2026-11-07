// dbview.cpp - CRecordView and the DDX_Field functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dbimpl.h"

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

static BOOL HasField(CRecordset* pSet) { return pSet != NULL && pSet->IsOpen(); }

// Shows a null field as an empty edit control, and stores an empty
// control as null when the column allows it.
template <class T>
static void FieldText(CDataExchange* pDX, int nIDC, T& value, CRecordset* pSet) {
  HWND hWnd = pDX->PrepareEditCtrl(nIDC);
  if (!pDX->m_bSaveAndValidate) {
    if (HasField(pSet) && pSet->IsFieldNull(&value)) {
      ::SetWindowText(hWnd, _T(""));
      return;
    }
    DDX_Text(pDX, nIDC, value);
    return;
  }
  if (HasField(pSet) && ::GetWindowTextLength(hWnd) == 0 && pSet->IsFieldNullable(&value)) {
    if (!pSet->IsFieldNull(&value)) pSet->SetFieldNull(&value);
    return;
  }
  DDX_Text(pDX, nIDC, value);
  if (HasField(pSet) && pSet->IsFieldNull(&value)) pSet->SetFieldNull(&value, FALSE);
}

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, BYTE& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, int& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, UINT& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, long& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, DWORD& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, CString& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, float& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, double& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}
void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, short& value, CRecordset* pSet) {
  FieldText(pDX, nIDC, value, pSet);
}

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, LPTSTR pstrValue, int nMaxLen,
                          CRecordset* pSet) {
  HWND hWnd = pDX->PrepareEditCtrl(nIDC);
  if (!pDX->m_bSaveAndValidate) {
    if (HasField(pSet) && pSet->IsFieldNull(pstrValue)) {
      ::SetWindowText(hWnd, _T(""));
      return;
    }
    DDX_Text(pDX, nIDC, pstrValue, nMaxLen);
    return;
  }
  if (HasField(pSet) && ::GetWindowTextLength(hWnd) == 0 && pSet->IsFieldNullable(pstrValue)) {
    if (!pSet->IsFieldNull(pstrValue)) pSet->SetFieldNull(pstrValue);
    return;
  }
  DDX_Text(pDX, nIDC, pstrValue, nMaxLen);
  if (HasField(pSet) && pSet->IsFieldNull(pstrValue)) pSet->SetFieldNull(pstrValue, FALSE);
}

// The others show a null field as no selection (or an indeterminate
// check box) and store a value as usual.
template <class T, class F>
static void FieldOther(CDataExchange* pDX, int nIDC, T& value, CRecordset* pSet, T nullValue, F ddx) {
  if (!pDX->m_bSaveAndValidate && HasField(pSet) && pSet->IsFieldNull(&value)) {
    T temp = nullValue;
    ddx(pDX, nIDC, temp);
    return;
  }
  BOOL bWasNull = pDX->m_bSaveAndValidate && HasField(pSet) && pSet->IsFieldNull(&value);
  ddx(pDX, nIDC, value);
  if (bWasNull) pSet->SetFieldNull(&value, FALSE);
}

void AFXAPI DDX_FieldCheck(CDataExchange* pDX, int nIDC, int& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, (int)BST_INDETERMINATE, DDX_Check);
}

void AFXAPI DDX_FieldRadio(CDataExchange* pDX, int nIDC, int& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, -1, DDX_Radio);
}

void AFXAPI DDX_FieldLBString(CDataExchange* pDX, int nIDC, CString& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, CString(), DDX_LBString);
}

void AFXAPI DDX_FieldCBString(CDataExchange* pDX, int nIDC, CString& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, CString(), DDX_CBString);
}

void AFXAPI DDX_FieldLBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, CString(), DDX_LBStringExact);
}

void AFXAPI DDX_FieldCBStringExact(CDataExchange* pDX, int nIDC, CString& value,
                                   CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, CString(), DDX_CBStringExact);
}

void AFXAPI DDX_FieldLBIndex(CDataExchange* pDX, int nIDC, int& index, CRecordset* pSet) {
  FieldOther(pDX, nIDC, index, pSet, -1, DDX_LBIndex);
}

void AFXAPI DDX_FieldCBIndex(CDataExchange* pDX, int nIDC, int& index, CRecordset* pSet) {
  FieldOther(pDX, nIDC, index, pSet, -1, DDX_CBIndex);
}

void AFXAPI DDX_FieldScroll(CDataExchange* pDX, int nIDC, int& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, 0, DDX_Scroll);
}

void AFXAPI DDX_FieldSlider(CDataExchange* pDX, int nIDC, int& value, CRecordset* pSet) {
  FieldOther(pDX, nIDC, value, pSet, 0, DDX_Slider);
}
