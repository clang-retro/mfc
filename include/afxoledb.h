// afxoledb.h - MFC over the OLE DB consumer templates: COleDBRecordView,
// a form view that shows a CRowset's current row and moves through the
// rowset with the record commands (ID_RECORD_FIRST and the others).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXOLEDB_H__
#define __AFXOLEDB_H__

#include <afxwin.h>
#include <afxext.h>
#include <atlbase.h>
#include <atldbcli.h>

using namespace ATL;

class COleDBRecordView : public CFormView {
  DECLARE_DYNAMIC(COleDBRecordView)
protected:
  COleDBRecordView(LPCTSTR lpszTemplateName);
  COleDBRecordView(UINT nIDTemplate);

public:
  virtual ~COleDBRecordView();
  // The rowset the view shows; the program opens it.
  virtual CRowset<>* OnGetRowset() = 0;

  // Writes the form back to the row (when the rowset can change), moves,
  // and shows the new row. Past either end it stays on the end row.
  virtual BOOL OnMove(UINT nIDMoveCommand);
  virtual void OnInitialUpdate();

  afx_msg void OnUpdateRecordFirst(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordPrev(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordNext(CCmdUI* pCmdUI);
  afx_msg void OnUpdateRecordLast(CCmdUI* pCmdUI);

protected:
  BOOL m_bOnFirstRow;
  BOOL m_bOnLastRow;
  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXOLEDB_H__
