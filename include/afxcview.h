// afxcview.h - views that are list view and tree view controls.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXCVIEW_H__
#define __AFXCVIEW_H__

#include "afxwin.h"
#include "afxcmn.h"

class CListView : public CCtrlView {
  DECLARE_DYNCREATE(CListView)
public:
  CListView() : CCtrlView(WC_LISTVIEW, AFX_WS_DEFAULT_VIEW) {}
  CListCtrl& GetListCtrl() const { return *(CListCtrl*)this; }
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  afx_msg void OnNcDestroy();
  DECLARE_MESSAGE_MAP()
};

class CTreeView : public CCtrlView {
  DECLARE_DYNCREATE(CTreeView)
public:
  CTreeView() : CCtrlView(WC_TREEVIEW, AFX_WS_DEFAULT_VIEW) {}
  CTreeCtrl& GetTreeCtrl() const { return *(CTreeCtrl*)this; }
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXCVIEW_H__
