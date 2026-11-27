// afxlistctrl.h - CMFCListCtrl, the Feature Pack's list control: a
// CMFCHeaderCtrl header that sorts on a click (by OnCompareItems), the
// sorted column marked, and colors and fonts for each cell.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXLISTCTRL_H__
#define __AFXLISTCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxheaderctrl.h>

class CMFCListCtrl : public CListCtrl {
  DECLARE_DYNAMIC(CMFCListCtrl)
public:
  CMFCListCtrl();
  virtual ~CMFCListCtrl();

  CMFCHeaderCtrl& GetHeaderCtrl() { return m_wndHeader; }
  const CMFCHeaderCtrl& GetHeaderCtrl() const { return m_wndHeader; }

  // Sorts the items by OnCompareItems on a column, and marks it.
  virtual void Sort(int iColumn, BOOL bAscending = TRUE, BOOL bAdd = FALSE);
  void SetSortColumn(int iColumn, BOOL bAscending = TRUE, BOOL bAdd = FALSE);
  void RemoveSortColumn(int iColumn);
  void EnableMultipleSort(BOOL bEnable = TRUE);
  BOOL IsMultipleSort() const;
  void EnableMarkSortedColumn(BOOL bMark = TRUE, BOOL bRedraw = TRUE);

  // Compares two items' data on a column: the sort's order (0 by
  // default, as there is nothing to go by).
  virtual int OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn);

  // A cell's colors and font; (COLORREF)-1 and NULL for the list's own.
  virtual COLORREF OnGetCellTextColor(int /*nRow*/, int /*nColumn*/) { return (COLORREF)-1; }
  virtual COLORREF OnGetCellBkColor(int /*nRow*/, int /*nColumn*/) { return (COLORREF)-1; }
  virtual HFONT OnGetCellFont(int /*nRow*/, int /*nColumn*/, DWORD /*dwData*/ = 0) { return NULL; }

  virtual BOOL InitHeader();

protected:
  static int CALLBACK _CompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

  virtual void PreSubclassWindow();
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg BOOL OnColumnClickEx(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg BOOL OnCustomDrawEx(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void OnStyleChanged(int nStyleType, LPSTYLESTRUCT lpStyleStruct);
  DECLARE_MESSAGE_MAP()

  CMFCHeaderCtrl m_wndHeader;
  int m_iSortedColumn;
  BOOL m_bAscending, m_bMarkSortedColumn;
  COLORREF m_clrSortedColumn;
  HFONT m_hOldFont;
};

#endif  // __AFXLISTCTRL_H__
