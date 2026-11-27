// afxshelltreectrl.h - CMFCShellTreeCtrl, a tree of the shell's
// namespace from the desktop down, filled as its folders open; with a
// related CMFCShellListCtrl showing the selected folder.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXSHELLTREECTRL_H__
#define __AFXSHELLTREECTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxwinappex.h>  // AFX_SHELLITEMINFO

class CMFCShellListCtrl;

class CMFCShellTreeCtrl : public CTreeCtrl {
  DECLARE_DYNAMIC(CMFCShellTreeCtrl)
  friend class CMFCShellListCtrl;
public:
  CMFCShellTreeCtrl(DWORD dwProp = 0);
  virtual ~CMFCShellTreeCtrl();

  // Opens the tree down to a folder and selects it.
  BOOL SelectPath(LPCTSTR lpszPath);
  BOOL SelectPath(LPCITEMIDLIST lpidl);
  // The file system path of an item (the selected one for NULL).
  BOOL GetItemPath(CString& strPath, HTREEITEM htreeItem = NULL) const;

  void SetRelatedList(CMFCShellListCtrl* pShellList);
  CMFCShellListCtrl* GetRelatedList() const { return m_pRelatedList; }

  // Fills the tree again, keeping the selected folder.
  void Refresh();
  void EnableShellContextMenu(BOOL bEnable = TRUE) { m_bContextMenu = bEnable; }
  // What the folders show (SHCONTF_FOLDERS by default).
  void SetFlags(DWORD dwFlags, BOOL bRefresh = TRUE);
  DWORD GetFlags() const { return m_dwFlags; }

  virtual CString OnGetItemText(LPAFX_SHELLITEMINFO pItem);
  virtual int OnGetItemIcon(LPAFX_SHELLITEMINFO pItem, BOOL bSelected);

protected:
  virtual void InitTree();
  virtual BOOL GetRootItems();
  virtual BOOL GetChildItems(HTREEITEM hParentItem);
  virtual HRESULT EnumObjects(HTREEITEM hParentItem, LPSHELLFOLDER pParentFolder, LPITEMIDLIST pidlParent);
  virtual void OnShowContextMenu(CPoint point);
  static int CALLBACK CompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
  LPAFX_SHELLITEMINFO _Info(HTREEITEM hItem) const { return hItem ? (LPAFX_SHELLITEMINFO)GetItemData(hItem) : NULL; }

  virtual void PreSubclassWindow();
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnItemexpanding(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void OnDeleteitem(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg BOOL OnSelchanged(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void OnRclick(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  DECLARE_MESSAGE_MAP()

  CMFCShellListCtrl* m_pRelatedList;
  DWORD m_dwFlags, m_dwProp;
  BOOL m_bContextMenu, m_bNoNotify, m_bInitialized;
};

#endif  // __AFXSHELLTREECTRL_H__
