// daoview.cpp - CDaoRecordView and the DDX_Field functions for DAO.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "daoimpl.h"
#include "ddxfield.h"

IMPLEMENT_DYNAMIC(CDaoRecordView, CFormView)

BEGIN_MESSAGE_MAP(CDaoRecordView, CFormView)
  ON_COMMAND_EX(ID_RECORD_FIRST, OnMove)
  ON_COMMAND_EX(ID_RECORD_PREV, OnMove)
  ON_COMMAND_EX(ID_RECORD_NEXT, OnMove)
  ON_COMMAND_EX(ID_RECORD_LAST, OnMove)
  ON_UPDATE_COMMAND_UI(ID_RECORD_FIRST, OnUpdateRecordFirst)
  ON_UPDATE_COMMAND_UI(ID_RECORD_PREV, OnUpdateRecordPrev)
  ON_UPDATE_COMMAND_UI(ID_RECORD_NEXT, OnUpdateRecordNext)
  ON_UPDATE_COMMAND_UI(ID_RECORD_LAST, OnUpdateRecordLast)
END_MESSAGE_MAP()

CDaoRecordView::CDaoRecordView(LPCTSTR lpszTemplateName)
    : CFormView(lpszTemplateName), m_nStatus(0) {}

CDaoRecordView::CDaoRecordView(UINT nIDTemplate) : CFormView(nIDTemplate), m_nStatus(0) {}

CDaoRecordView::~CDaoRecordView() {}

void CDaoRecordView::OnInitialUpdate() {
  CDaoRecordset* pSet = OnGetRecordset();
  if (pSet != NULL && !pSet->IsOpen()) {
    CWaitCursor wait;
    pSet->Open();
  }
  FindBounds();
  CFormView::OnInitialUpdate();
}

static BOOL IsEmptySet(CDaoRecordset* pSet) {
  return pSet == NULL || !pSet->IsOpen() || (pSet->IsBOF() && pSet->IsEOF());
}

// The first and last records' bookmarks, when the recordset has them; the
// current record stays where it was.
void CDaoRecordView::FindBounds() {
  m_varBookmarkFirst.Clear();
  m_varBookmarkLast.Clear();
  m_varBookmarkCurrent.Clear();
  CDaoRecordset* pSet = OnGetRecordset();
  if (IsEmptySet(pSet) || !pSet->CanBookmark() || !pSet->CanScroll()) return;
  BOOL bOnRecord = !pSet->IsBOF() && !pSet->IsEOF();
  if (bOnRecord) m_varBookmarkCurrent = pSet->GetBookmark();
  pSet->MoveLast();
  m_varBookmarkLast = pSet->GetBookmark();
  pSet->MoveFirst();
  m_varBookmarkFirst = pSet->GetBookmark();
  if (bOnRecord) pSet->SetBookmark(m_varBookmarkCurrent);
}

// Bookmarks are byte arrays.
BOOL CDaoRecordView::IsOnBookmark(const COleVariant& var) {
  CDaoRecordset* pSet = OnGetRecordset();
  if (var.vt != (VT_ARRAY | VT_UI1) || pSet->IsBOF() || pSet->IsEOF() || pSet->IsDeleted())
    return FALSE;
  m_varBookmarkCurrent = pSet->GetBookmark();
  const VARIANT& cur = m_varBookmarkCurrent;
  if (cur.vt != var.vt) return FALSE;
  long n1 = cur.parray->rgsabound[0].cElements, n2 = var.parray->rgsabound[0].cElements;
  return n1 == n2 && memcmp(cur.parray->pvData, var.parray->pvData, n1) == 0;
}

BOOL CDaoRecordView::IsOnFirstRecord() {
  CDaoRecordset* pSet = OnGetRecordset();
  if (IsEmptySet(pSet) || pSet->IsBOF()) return TRUE;
  if (m_varBookmarkFirst.vt != VT_EMPTY) return IsOnBookmark(m_varBookmarkFirst);
  if (pSet->GetType() == dbOpenTable) return FALSE;
  return pSet->GetAbsolutePosition() <= 0;
}

BOOL CDaoRecordView::IsOnLastRecord() {
  CDaoRecordset* pSet = OnGetRecordset();
  if (IsEmptySet(pSet) || pSet->IsEOF()) return TRUE;
  if (m_varBookmarkLast.vt != VT_EMPTY) return IsOnBookmark(m_varBookmarkLast);
  return FALSE;
}

BOOL CDaoRecordView::OnMove(UINT nIDMoveCommand) {
  CDaoRecordset* pSet = OnGetRecordset();
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
      if (pSet->CanScroll()) pSet->MoveLast();
      else pSet->SetFieldNull(NULL);
    }
    break;
  case ID_RECORD_LAST:
    pSet->MoveLast();
    break;
  default:
    return FALSE;
  }
  UpdateData(FALSE);
  return TRUE;
}

void CDaoRecordView::OnUpdateRecordFirst(CCmdUI* pCmdUI) {
  CDaoRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(!IsEmptySet(pSet) && pSet->CanScroll() && !IsOnFirstRecord());
}

void CDaoRecordView::OnUpdateRecordPrev(CCmdUI* pCmdUI) { OnUpdateRecordFirst(pCmdUI); }

void CDaoRecordView::OnUpdateRecordNext(CCmdUI* pCmdUI) {
  CDaoRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(!IsEmptySet(pSet) && !IsOnLastRecord());
}

void CDaoRecordView::OnUpdateRecordLast(CCmdUI* pCmdUI) {
  CDaoRecordset* pSet = OnGetRecordset();
  pCmdUI->Enable(!IsEmptySet(pSet) && pSet->CanScroll() && !IsOnLastRecord());
}

// ---------------------------------------------------------------------
// DDX_Field

_AFX_DDX_FIELD_FUNCTIONS(CDaoRecordset)

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleDateTime& value, CDaoRecordset* pSet) {
  _AfxDdxFieldText(pDX, nIDC, value, pSet);
}

void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, COleCurrency& value, CDaoRecordset* pSet) {
  _AfxDdxFieldText(pDX, nIDC, value, pSet);
}
