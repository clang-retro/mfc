// afxheaderctrl.h - CMFCHeaderCtrl, the Feature Pack's header control:
// drawn by the visual manager, with sort arrows on one column (or, with
// multiple sort, on several), and hot tracking.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXHEADERCTRL_H__
#define __AFXHEADERCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxtempl.h>

class CMFCHeaderCtrl : public CHeaderCtrl {
  DECLARE_DYNAMIC(CMFCHeaderCtrl)
public:
  CMFCHeaderCtrl();
  virtual ~CMFCHeaderCtrl();

  // The sort: a column ascending or descending, added to the others when
  // multiple sort is on (bAdd), or the only one.
  void SetSortColumn(int iColumn, BOOL bAscending = TRUE, BOOL bAdd = FALSE);
  void RemoveSortColumn(int iColumn);
  void RemoveAllSortColumns();
  int GetSortColumn() const;
  BOOL IsAscending() const;
  // 0 not sorted, 1 ascending, -1 descending.
  int GetColumnState(int iColumn) const;
  BOOL IsMultipleSort() const { return m_bMultipleSort; }
  void EnableMultipleSort(BOOL bEnable = TRUE);
  BOOL IsDialogControl() const { return m_bIsDlgControl; }

protected:
  virtual void OnDrawItem(CDC* pDC, int iItem, CRect rect, BOOL bIsPressed, BOOL bIsHighlighted);
  virtual void OnFillBackground(CDC* pDC);
  virtual void OnDrawSortArrow(CDC* pDC, CRect rectArrow);
  virtual void PreSubclassWindow();

  int _HitTest(CPoint point) const;

  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  DECLARE_MESSAGE_MAP()

  CMap<int, int, int, int> m_mapColumnsStatus;  // column -> 1 or -1
  BOOL m_bIsMousePressed, m_bMultipleSort, m_bAscending, m_bTracked, m_bIsDlgControl;
  int m_nHighlightedItem, m_nPressedItem;
  int m_nSortColumn;  // the one sort column (no multiple sort)
};

#endif  // __AFXHEADERCTRL_H__
