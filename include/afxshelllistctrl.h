// afxshelllistctrl.h - CMFCShellListCtrl, a list of a shell folder's
// items with their size, type and date, sorted by any column; opening a
// folder shows it, opening a file runs it, Backspace goes up.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSHELLLISTCTRL_H__
#define __AFXSHELLLISTCTRL_H__

#include <afxlistctrl.h>
#include <afxwinappex.h>  // AFX_SHELLITEMINFO

class CMFCShellTreeCtrl;

// Sent to the parent when the list shows another folder.
extern UINT AFX_WM_CHANGE_CURRENT_FOLDER;

#define AFX_ShellList_ColumnName 0
#define AFX_ShellList_ColumnSize 1
#define AFX_ShellList_ColumnType 2
#define AFX_ShellList_ColumnModified 3

class CMFCShellListCtrl : public CMFCListCtrl {
  DECLARE_DYNAMIC(CMFCShellListCtrl)
  friend class CMFCShellTreeCtrl;
public:
  CMFCShellListCtrl();
  virtual ~CMFCShellListCtrl();

  // ----- The folder shown.
  virtual HRESULT DisplayFolder(LPCTSTR lpszPath);
  virtual HRESULT DisplayFolder(LPAFX_SHELLITEMINFO lpItemInfo);
  virtual HRESULT DisplayParentFolder();
  virtual HRESULT Refresh();
  BOOL GetCurrentFolder(CString& strPath) const;
  BOOL GetCurrentFolderName(CString& strName) const;
  LPITEMIDLIST GetCurrentItemIdList() const { return m_pidlCurFQ; }
  LPSHELLFOLDER GetCurrentShellFolder() const { return m_psfCurFolder; }
  BOOL IsDesktop() const;

  // ----- The items.
  BOOL GetItemPath(CString& strPath, int iItem) const;
  void SetItemTypes(SHCONTF nTypes);
  SHCONTF GetItemTypes() const { return m_nTypes; }
  void EnableShellContextMenu(BOOL bEnable = TRUE) { m_bContextMenu = bEnable; }
  CMFCShellTreeCtrl* GetRelatedTree() const { return m_pRelatedTree; }

  // ----- How the items look.
  virtual void OnSetColumns();
  virtual CString OnGetItemText(int iItem, int iColumn, LPAFX_SHELLITEMINFO pItem);
  virtual int OnGetItemIcon(int iItem, LPAFX_SHELLITEMINFO pItem);
  virtual void OnFormatFileSize(ULONGLONG lFileSize, CString& str);
  virtual void OnFormatFileDate(const CTime& tmFile, CString& str);
  virtual int OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn);

protected:
  virtual HRESULT EnumObjects(LPSHELLFOLDER pParentFolder, LPITEMIDLIST pidlParent);
  virtual HRESULT _DisplayFolder(LPCITEMIDLIST pidlFQ);
  virtual void DoDefault(int iItem);
  virtual void OnShowContextMenu(int iItem, CPoint point);
  void _ReleaseCurrent();
  void _Init();
  LPAFX_SHELLITEMINFO _Info(int iItem) const { return iItem >= 0 ? (LPAFX_SHELLITEMINFO)GetItemData(iItem) : NULL; }

  virtual void PreSubclassWindow();
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg BOOL OnDblclk(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg BOOL OnKeydown(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg BOOL OnDeleteitem(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  DECLARE_MESSAGE_MAP()

  CMFCShellTreeCtrl* m_pRelatedTree;
  LPSHELLFOLDER m_psfCurFolder;
  LPITEMIDLIST m_pidlCurFQ;
  SHCONTF m_nTypes;
  BOOL m_bContextMenu, m_bNoNotify, m_bInitialized;
};

#endif  // __AFXSHELLLISTCTRL_H__
