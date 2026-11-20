// oledbview.cpp - COleDBRecordView.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#ifndef _WINPORT  // OLE DB is Windows' own
#include <afxoledb.h>

IMPLEMENT_DYNAMIC(COleDBRecordView, CFormView)

BEGIN_MESSAGE_MAP(COleDBRecordView, CFormView)
  ON_COMMAND_EX(ID_RECORD_FIRST, OnMove)
  ON_COMMAND_EX(ID_RECORD_PREV, OnMove)
  ON_COMMAND_EX(ID_RECORD_NEXT, OnMove)
  ON_COMMAND_EX(ID_RECORD_LAST, OnMove)
  ON_UPDATE_COMMAND_UI(ID_RECORD_FIRST, OnUpdateRecordFirst)
  ON_UPDATE_COMMAND_UI(ID_RECORD_PREV, OnUpdateRecordPrev)
  ON_UPDATE_COMMAND_UI(ID_RECORD_NEXT, OnUpdateRecordNext)
  ON_UPDATE_COMMAND_UI(ID_RECORD_LAST, OnUpdateRecordLast)
END_MESSAGE_MAP()

COleDBRecordView::COleDBRecordView(LPCTSTR lpszTemplateName)
    : CFormView(lpszTemplateName), m_bOnFirstRow(TRUE), m_bOnLastRow(FALSE) {}

COleDBRecordView::COleDBRecordView(UINT nIDTemplate)
    : CFormView(nIDTemplate), m_bOnFirstRow(TRUE), m_bOnLastRow(FALSE) {}

COleDBRecordView::~COleDBRecordView() {}

void COleDBRecordView::OnInitialUpdate() {
  m_bOnFirstRow = TRUE;
  m_bOnLastRow = FALSE;
  CFormView::OnInitialUpdate();
}

BOOL COleDBRecordView::OnMove(UINT nIDMoveCommand) {
  CRowset<>* pSet = OnGetRowset();
  if (pSet == NULL || pSet->m_spRowset == NULL) return TRUE;
  // The form's values go back to the row first.
  if (pSet->m_spRowsetChange != NULL && pSet->m_hRow != NULL) {
    if (!UpdateData()) return TRUE;
    pSet->SetData();
  }
  HRESULT hr;
  switch (nIDMoveCommand) {
    case ID_RECORD_FIRST:
      pSet->MoveFirst();
      m_bOnFirstRow = TRUE;
      m_bOnLastRow = FALSE;
      break;
    case ID_RECORD_PREV:
      hr = pSet->MovePrev();
      m_bOnLastRow = FALSE;
      if (hr == DB_S_ENDOFROWSET || pSet->m_hRow == NULL) {
        pSet->MoveFirst();
        m_bOnFirstRow = TRUE;
      }
      break;
    case ID_RECORD_NEXT:
      hr = pSet->MoveNext();
      m_bOnFirstRow = FALSE;
      if (hr == DB_S_ENDOFROWSET || pSet->m_hRow == NULL) {
        pSet->MoveLast();
        m_bOnLastRow = TRUE;
      }
      break;
    case ID_RECORD_LAST:
      pSet->MoveLast();
      m_bOnFirstRow = FALSE;
      m_bOnLastRow = TRUE;
      break;
    default:
      return FALSE;
  }
  UpdateData(FALSE);
  return TRUE;
}

void COleDBRecordView::OnUpdateRecordFirst(CCmdUI* pCmdUI) { pCmdUI->Enable(!m_bOnFirstRow); }
void COleDBRecordView::OnUpdateRecordPrev(CCmdUI* pCmdUI) { pCmdUI->Enable(!m_bOnFirstRow); }
void COleDBRecordView::OnUpdateRecordNext(CCmdUI* pCmdUI) { pCmdUI->Enable(!m_bOnLastRow); }
void COleDBRecordView::OnUpdateRecordLast(CCmdUI* pCmdUI) { pCmdUI->Enable(!m_bOnLastRow); }

#endif  // _WINPORT
