// fpshell.cpp - CMFCShellTreeCtrl and CMFCShellListCtrl: the shell's
// namespace as a tree of folders and a list of a folder's items. Each
// tree or list item holds an AFX_SHELLITEMINFO, freed with the item.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxshelltreectrl.h>
#include <afxshelllistctrl.h>
#include <shellapi.h>

UINT AFX_WM_CHANGE_CURRENT_FOLDER = ::RegisterWindowMessage(_T("AFX_WM_CHANGE_CURRENT_FOLDER"));

namespace {

// ----- Item ID lists, from the task allocator as the shell's are.

UINT PidlSize(LPCITEMIDLIST pidl) {
  if (pidl == NULL) return 0;
  UINT cb = 0;
  for (const BYTE* p = (const BYTE*)pidl; ((LPCITEMIDLIST)p)->mkid.cb != 0; p += ((LPCITEMIDLIST)p)->mkid.cb)
    cb += ((LPCITEMIDLIST)p)->mkid.cb;
  return cb;
}

LPITEMIDLIST PidlConcat(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2) {
  UINT cb1 = PidlSize(pidl1), cb2 = PidlSize(pidl2);
  LPITEMIDLIST p = (LPITEMIDLIST)::CoTaskMemAlloc(cb1 + cb2 + sizeof(USHORT));
  if (p == NULL) return NULL;
  if (cb1) memcpy(p, pidl1, cb1);
  if (cb2) memcpy((BYTE*)p + cb1, pidl2, cb2);
  memset((BYTE*)p + cb1 + cb2, 0, sizeof(USHORT));
  return p;
}

LPITEMIDLIST PidlCopy(LPCITEMIDLIST pidl) { return PidlConcat(pidl, NULL); }

BOOL PidlIsEmpty(LPCITEMIDLIST pidl) { return pidl == NULL || pidl->mkid.cb == 0; }

// The list without its last item.
LPITEMIDLIST PidlParent(LPCITEMIDLIST pidl) {
  LPITEMIDLIST p = PidlCopy(pidl);
  if (p == NULL || PidlIsEmpty(p)) return p;
  BYTE* last = (BYTE*)p;
  for (BYTE* q = (BYTE*)p; ((LPITEMIDLIST)q)->mkid.cb != 0; q += ((LPITEMIDLIST)q)->mkid.cb) last = q;
  ((LPITEMIDLIST)last)->mkid.cb = 0;
  return p;
}

// Whether a is b, or (bOrBelow) a folder above it.
BOOL PidlIsPrefix(LPCITEMIDLIST a, LPCITEMIDLIST b, BOOL bOrBelow) {
  UINT ca = PidlSize(a), cb = PidlSize(b);
  if (ca > cb || (!bOrBelow && ca != cb)) return FALSE;
  if (ca != 0 && memcmp(a, b, ca) != 0) return FALSE;
  // The match must end at an item's end in b.
  UINT at = 0;
  for (const BYTE* p = (const BYTE*)b; at < ca; p += ((LPCITEMIDLIST)p)->mkid.cb) at += ((LPCITEMIDLIST)p)->mkid.cb;
  return at == ca;
}

CString StrRetText(STRRET* pStr, LPCITEMIDLIST pidl) {
  CString s;
  switch (pStr->uType) {
    case STRRET_WSTR:
      if (pStr->pOleStr != NULL) {
        s = CString(pStr->pOleStr);
        ::CoTaskMemFree(pStr->pOleStr);
      }
      break;
    case STRRET_OFFSET:
      s = CString((LPCSTR)((const BYTE*)pidl + pStr->uOffset));
      break;
    case STRRET_CSTR:
      s = CString(pStr->cStr);
      break;
  }
  return s;
}

CString DisplayName(LPAFX_SHELLITEMINFO pItem, DWORD uFlags) {
  if (pItem == NULL) return CString();
  if (pItem->pParentFolder == NULL) {
    SHFILEINFO sfi;
    memset(&sfi, 0, sizeof(sfi));
    ::SHGetFileInfo((LPCTSTR)pItem->pidlFQ, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME);
    return sfi.szDisplayName;
  }
  STRRET str;
  memset(&str, 0, sizeof(str));
  if (FAILED(pItem->pParentFolder->GetDisplayNameOf(pItem->pidlRel, uFlags, &str))) return CString();
  return StrRetText(&str, pItem->pidlRel);
}

int ShellIcon(LPCITEMIDLIST pidlFQ, UINT uFlags) {
  SHFILEINFO sfi;
  memset(&sfi, 0, sizeof(sfi));
  ::SHGetFileInfo((LPCTSTR)pidlFQ, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX | uFlags);
  return sfi.iIcon;
}

HIMAGELIST SystemImageList(BOOL bSmall) {
  LPITEMIDLIST pidl = NULL;
  if (FAILED(::SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl))) return NULL;
  SHFILEINFO sfi;
  memset(&sfi, 0, sizeof(sfi));
  HIMAGELIST h = (HIMAGELIST)::SHGetFileInfo((LPCTSTR)pidl, 0, &sfi, sizeof(sfi),
                                             SHGFI_PIDL | SHGFI_SYSICONINDEX | (bSmall ? SHGFI_SMALLICON : 0));
  ::CoTaskMemFree(pidl);
  return h;
}

LPAFX_SHELLITEMINFO NewInfo(LPSHELLFOLDER pParent, LPCITEMIDLIST pidlParentFQ, LPCITEMIDLIST pidlRel) {
  LPAFX_SHELLITEMINFO p = new AFX_SHELLITEMINFO;
  p->pParentFolder = pParent;
  if (pParent != NULL) pParent->AddRef();
  p->pidlRel = PidlCopy(pidlRel);
  p->pidlFQ = PidlConcat(pidlParentFQ, pidlRel);
  return p;
}

void FreeInfo(LPAFX_SHELLITEMINFO p) {
  if (p == NULL) return;
  if (p->pParentFolder != NULL) p->pParentFolder->Release();
  ::CoTaskMemFree(p->pidlRel);
  ::CoTaskMemFree(p->pidlFQ);
  delete p;
}

// The folder an item names, bound from its parent (the desktop's own for
// the desktop).
HRESULT BindFolder(LPAFX_SHELLITEMINFO pItem, LPSHELLFOLDER* ppFolder) {
  *ppFolder = NULL;
  if (pItem->pParentFolder == NULL || PidlIsEmpty(pItem->pidlRel)) {
    LPSHELLFOLDER pDesktop = NULL;
    HRESULT hr = ::SHGetDesktopFolder(&pDesktop);
    if (FAILED(hr)) return hr;
    if (PidlIsEmpty(pItem->pidlFQ)) {
      *ppFolder = pDesktop;
      return S_OK;
    }
    hr = pDesktop->BindToObject(pItem->pidlFQ, NULL, IID_IShellFolder, (void**)ppFolder);
    pDesktop->Release();
    return hr;
  }
  return pItem->pParentFolder->BindToObject(pItem->pidlRel, NULL, IID_IShellFolder, (void**)ppFolder);
}

ULONG ItemAttributes(LPAFX_SHELLITEMINFO pItem, ULONG ulWant) {
  ULONG ul = ulWant;
  if (pItem->pParentFolder == NULL) return SFGAO_FOLDER | SFGAO_HASSUBFOLDER;
  LPCITEMIDLIST pidl = pItem->pidlRel;
  if (FAILED(pItem->pParentFolder->GetAttributesOf(1, (LPCITEMIDLIST*)&pidl, &ul))) return 0;
  return ul;
}

BOOL ItemFileData(LPAFX_SHELLITEMINFO pItem, WIN32_FILE_ATTRIBUTE_DATA* pData) {
  TCHAR szPath[MAX_PATH];
  if (pItem == NULL || !::SHGetPathFromIDList(pItem->pidlFQ, szPath)) return FALSE;
  return ::GetFileAttributesEx(szPath, GetFileExInfoStandard, pData);
}

// The shell's menu for one item, at a screen point; FALSE when there is
// none, as on the portable layer, which has no shell menus.
BOOL ShellMenu(CWnd* pWnd, LPSHELLFOLDER pFolder, LPCITEMIDLIST pidlRel, CPoint point) {
#ifdef _WINPORT
  (void)pWnd;
  (void)pFolder;
  (void)pidlRel;
  (void)point;
  return FALSE;
#else
  if (pFolder == NULL) return FALSE;
  IContextMenu* pcm = NULL;
  if (FAILED(pFolder->GetUIObjectOf(pWnd->GetSafeHwnd(), 1, (LPCITEMIDLIST*)&pidlRel, IID_IContextMenu, NULL,
                                    (void**)&pcm)))
    return FALSE;
  HMENU hMenu = ::CreatePopupMenu();
  BOOL bDone = FALSE;
  if (hMenu != NULL && SUCCEEDED(pcm->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE))) {
    UINT idCmd = ::TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                  pWnd->GetSafeHwnd(), NULL);
    if (idCmd != 0) {
      CMINVOKECOMMANDINFO cmi;
      memset(&cmi, 0, sizeof(cmi));
      cmi.cbSize = sizeof(cmi);
      cmi.hwnd = pWnd->GetSafeHwnd();
      cmi.lpVerb = MAKEINTRESOURCEA(idCmd - 1);
      cmi.nShow = SW_SHOWNORMAL;
      pcm->InvokeCommand(&cmi);
    }
    bDone = TRUE;
  }
  if (hMenu != NULL) ::DestroyMenu(hMenu);
  pcm->Release();
  return bDone;
#endif
}

HRESULT PathToPidl(LPCTSTR lpszPath, LPITEMIDLIST* ppidl) {
  *ppidl = NULL;
  LPSHELLFOLDER pDesktop = NULL;
  HRESULT hr = ::SHGetDesktopFolder(&pDesktop);
  if (FAILED(hr)) return hr;
  CStringW strW(lpszPath);
  ULONG chEaten = 0;
  hr = pDesktop->ParseDisplayName(NULL, NULL, strW.GetBuffer(), &chEaten, ppidl, NULL);
  strW.ReleaseBuffer();
  pDesktop->Release();
  return hr;
}

}  // namespace

// =====================================================================
// CMFCShellTreeCtrl

IMPLEMENT_DYNAMIC(CMFCShellTreeCtrl, CTreeCtrl)

BEGIN_MESSAGE_MAP(CMFCShellTreeCtrl, CTreeCtrl)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_CONTEXTMENU()
  ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnItemexpanding)
  ON_NOTIFY_REFLECT(TVN_DELETEITEM, OnDeleteitem)
  ON_NOTIFY_REFLECT_EX(TVN_SELCHANGED, OnSelchanged)
  ON_NOTIFY_REFLECT(NM_RCLICK, OnRclick)
END_MESSAGE_MAP()

CMFCShellTreeCtrl::CMFCShellTreeCtrl(DWORD dwProp)
    : m_pRelatedList(NULL), m_dwFlags(SHCONTF_FOLDERS), m_dwProp(dwProp), m_bContextMenu(FALSE),
      m_bNoNotify(FALSE), m_bInitialized(FALSE) {}

CMFCShellTreeCtrl::~CMFCShellTreeCtrl() {}

int CMFCShellTreeCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CTreeCtrl::OnCreate(lpCreateStruct) == -1) return -1;
  InitTree();
  return 0;
}

void CMFCShellTreeCtrl::PreSubclassWindow() {
  CTreeCtrl::PreSubclassWindow();
  // A tree in a dialog is subclassed after it is made: fill it now. One
  // being created is filled by OnCreate.
  if (AfxGetThreadState()->m_pWndInit == NULL) InitTree();
}

void CMFCShellTreeCtrl::OnDestroy() {
  m_bNoNotify = TRUE;
  DeleteAllItems();
  CTreeCtrl::OnDestroy();
}

void CMFCShellTreeCtrl::InitTree() {
  if (m_bInitialized) return;
  m_bInitialized = TRUE;
  ::SendMessage(m_hWnd, TVM_SETIMAGELIST, TVSIL_NORMAL, (LPARAM)SystemImageList(TRUE));
  GetRootItems();
}

BOOL CMFCShellTreeCtrl::GetRootItems() {
  LPITEMIDLIST pidl = NULL;
  if (FAILED(::SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl))) return FALSE;
  LPAFX_SHELLITEMINFO pItem = NewInfo(NULL, NULL, NULL);
  ::CoTaskMemFree(pItem->pidlFQ);
  pItem->pidlFQ = pidl;

  TVINSERTSTRUCT tvis;
  memset(&tvis, 0, sizeof(tvis));
  tvis.hParent = TVI_ROOT;
  tvis.hInsertAfter = TVI_LAST;
  tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
  CString strText = OnGetItemText(pItem);
  tvis.item.pszText = (LPTSTR)(LPCTSTR)strText;
  tvis.item.iImage = OnGetItemIcon(pItem, FALSE);
  tvis.item.iSelectedImage = OnGetItemIcon(pItem, TRUE);
  tvis.item.cChildren = 1;
  tvis.item.lParam = (LPARAM)pItem;
  HTREEITEM hRoot = InsertItem(&tvis);
  if (hRoot == NULL) {
    FreeInfo(pItem);
    return FALSE;
  }
  Expand(hRoot, TVE_EXPAND);
  return TRUE;
}

BOOL CMFCShellTreeCtrl::GetChildItems(HTREEITEM hParentItem) {
  LPAFX_SHELLITEMINFO pItem = _Info(hParentItem);
  if (pItem == NULL) return FALSE;
  LPSHELLFOLDER pFolder = NULL;
  if (FAILED(BindFolder(pItem, &pFolder))) return FALSE;
  CWaitCursor wait;
  SetRedraw(FALSE);
  HRESULT hr = EnumObjects(hParentItem, pFolder, pItem->pidlFQ);
  TVSORTCB tvs;
  tvs.hParent = hParentItem;
  tvs.lpfnCompare = CompareProc;
  tvs.lParam = (LPARAM)this;
  SortChildrenCB(&tvs);
  SetRedraw(TRUE);
  pFolder->Release();
  return SUCCEEDED(hr);
}

HRESULT CMFCShellTreeCtrl::EnumObjects(HTREEITEM hParentItem, LPSHELLFOLDER pParentFolder, LPITEMIDLIST pidlParent) {
  IEnumIDList* pEnum = NULL;
  HRESULT hr = pParentFolder->EnumObjects(GetSafeHwnd(), m_dwFlags, &pEnum);
  if (FAILED(hr) || pEnum == NULL) return FAILED(hr) ? hr : S_FALSE;
  LPITEMIDLIST pidlTemp = NULL;
  ULONG dwFetched = 0;
  while (pEnum->Next(1, &pidlTemp, &dwFetched) == S_OK && dwFetched == 1) {
    ULONG ulAttrs = SFGAO_HASSUBFOLDER | SFGAO_FOLDER;
    pParentFolder->GetAttributesOf(1, (LPCITEMIDLIST*)&pidlTemp, &ulAttrs);
    LPAFX_SHELLITEMINFO pItem = NewInfo(pParentFolder, pidlParent, pidlTemp);
    ::CoTaskMemFree(pidlTemp);
    pidlTemp = NULL;

    TVINSERTSTRUCT tvis;
    memset(&tvis, 0, sizeof(tvis));
    tvis.hParent = hParentItem;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    CString strText = OnGetItemText(pItem);
    tvis.item.pszText = (LPTSTR)(LPCTSTR)strText;
    tvis.item.iImage = OnGetItemIcon(pItem, FALSE);
    tvis.item.iSelectedImage = OnGetItemIcon(pItem, TRUE);
    // Files shown in the tree have no children; folders may.
    tvis.item.cChildren = (ulAttrs & SFGAO_HASSUBFOLDER) != 0 ||
                                  ((ulAttrs & SFGAO_FOLDER) != 0 && (m_dwFlags & SHCONTF_NONFOLDERS) != 0)
                              ? 1
                              : 0;
    tvis.item.lParam = (LPARAM)pItem;
    if (InsertItem(&tvis) == NULL) FreeInfo(pItem);
  }
  pEnum->Release();
  return S_OK;
}

int CALLBACK CMFCShellTreeCtrl::CompareProc(LPARAM lParam1, LPARAM lParam2, LPARAM) {
  LPAFX_SHELLITEMINFO p1 = (LPAFX_SHELLITEMINFO)lParam1;
  LPAFX_SHELLITEMINFO p2 = (LPAFX_SHELLITEMINFO)lParam2;
  if (p1 == NULL || p2 == NULL || p1->pParentFolder == NULL) return 0;
  HRESULT hr = p1->pParentFolder->CompareIDs(0, p1->pidlRel, p2->pidlRel);
  if (FAILED(hr)) return 0;
  return (short)HRESULT_CODE(hr);
}

CString CMFCShellTreeCtrl::OnGetItemText(LPAFX_SHELLITEMINFO pItem) { return DisplayName(pItem, SHGDN_NORMAL); }

int CMFCShellTreeCtrl::OnGetItemIcon(LPAFX_SHELLITEMINFO pItem, BOOL bSelected) {
  if (pItem == NULL) return 0;
  return ShellIcon(pItem->pidlFQ, SHGFI_SMALLICON | (bSelected ? SHGFI_OPENICON : 0));
}

void CMFCShellTreeCtrl::OnItemexpanding(NMHDR* pNMHDR, LRESULT* pResult) {
  NMTREEVIEW* pnm = (NMTREEVIEW*)pNMHDR;
  *pResult = 0;
  HTREEITEM hItem = pnm->itemNew.hItem;
  if (pnm->action != TVE_EXPAND || hItem == NULL || GetChildItem(hItem) != NULL) return;
  GetChildItems(hItem);
  if (GetChildItem(hItem) == NULL) {
    // Nothing inside after all: take the button away.
    TVITEM tvi;
    memset(&tvi, 0, sizeof(tvi));
    tvi.mask = TVIF_CHILDREN;
    tvi.hItem = hItem;
    tvi.cChildren = 0;
    SetItem(&tvi);
  }
}

void CMFCShellTreeCtrl::OnDeleteitem(NMHDR* pNMHDR, LRESULT* pResult) {
  NMTREEVIEW* pnm = (NMTREEVIEW*)pNMHDR;
  FreeInfo((LPAFX_SHELLITEMINFO)pnm->itemOld.lParam);
  *pResult = 0;
}

BOOL CMFCShellTreeCtrl::OnSelchanged(NMHDR* pNMHDR, LRESULT* pResult) {
  NMTREEVIEW* pnm = (NMTREEVIEW*)pNMHDR;
  *pResult = 0;
  if (m_bNoNotify || m_pRelatedList == NULL || m_pRelatedList->GetSafeHwnd() == NULL) return FALSE;
  LPAFX_SHELLITEMINFO pItem = (LPAFX_SHELLITEMINFO)pnm->itemNew.lParam;
  if (pItem == NULL) return FALSE;
  m_pRelatedList->m_bNoNotify = TRUE;
  m_pRelatedList->DisplayFolder(pItem);
  m_pRelatedList->m_bNoNotify = FALSE;
  return FALSE;  // the parent hears of it too
}

void CMFCShellTreeCtrl::OnRclick(NMHDR*, LRESULT* pResult) {
  *pResult = 0;
  if (!m_bContextMenu) return;
  CPoint point;
  ::GetCursorPos(&point);
  OnShowContextMenu(point);
  *pResult = 1;
}

void CMFCShellTreeCtrl::OnContextMenu(CWnd*, CPoint point) {
  if (!m_bContextMenu) {
    Default();
    return;
  }
  if (point.x == -1 && point.y == -1) {
    CRect rect;
    HTREEITEM hSel = GetSelectedItem();
    if (hSel != NULL && GetItemRect(hSel, &rect, TRUE)) {
      point = rect.CenterPoint();
      ClientToScreen(&point);
    }
  }
  OnShowContextMenu(point);
}

void CMFCShellTreeCtrl::OnShowContextMenu(CPoint point) {
  CPoint ptClient = point;
  ScreenToClient(&ptClient);
  UINT uFlags = 0;
  HTREEITEM hItem = HitTest(ptClient, &uFlags);
  if (hItem == NULL || (uFlags & TVHT_ONITEM) == 0) hItem = GetSelectedItem();
  LPAFX_SHELLITEMINFO pItem = _Info(hItem);
  if (pItem == NULL || pItem->pParentFolder == NULL) return;
  ShellMenu(this, pItem->pParentFolder, pItem->pidlRel, point);
}

BOOL CMFCShellTreeCtrl::SelectPath(LPCTSTR lpszPath) {
  if (lpszPath == NULL) return FALSE;
  LPITEMIDLIST pidl = NULL;
  if (FAILED(PathToPidl(lpszPath, &pidl)) || pidl == NULL) return FALSE;
  BOOL bOK = SelectPath(pidl);
  ::CoTaskMemFree(pidl);
  return bOK;
}

BOOL CMFCShellTreeCtrl::SelectPath(LPCITEMIDLIST lpidl) {
  if (lpidl == NULL || m_hWnd == NULL) return FALSE;
  HTREEITEM hItem = GetRootItem();
  while (hItem != NULL) {
    LPAFX_SHELLITEMINFO pItem = _Info(hItem);
    if (pItem == NULL) return FALSE;
    if (PidlIsPrefix(pItem->pidlFQ, lpidl, FALSE)) {
      SelectItem(hItem);
      EnsureVisible(hItem);
      return TRUE;
    }
    if (PidlIsPrefix(pItem->pidlFQ, lpidl, TRUE)) {
      // Below this one: open it and look among its children.
      Expand(hItem, TVE_EXPAND);
      hItem = GetChildItem(hItem);
      continue;
    }
    hItem = GetNextSiblingItem(hItem);
  }
  return FALSE;
}

BOOL CMFCShellTreeCtrl::GetItemPath(CString& strPath, HTREEITEM htreeItem) const {
  strPath.Empty();
  if (htreeItem == NULL) htreeItem = GetSelectedItem();
  LPAFX_SHELLITEMINFO pItem = _Info(htreeItem);
  if (pItem == NULL) return FALSE;
  TCHAR szPath[MAX_PATH];
  if (!::SHGetPathFromIDList(pItem->pidlFQ, szPath)) return FALSE;
  strPath = szPath;
  return TRUE;
}

void CMFCShellTreeCtrl::SetRelatedList(CMFCShellListCtrl* pShellList) {
  m_pRelatedList = pShellList;
  if (pShellList != NULL) pShellList->m_pRelatedTree = this;
}

void CMFCShellTreeCtrl::Refresh() {
  if (m_hWnd == NULL) return;
  LPITEMIDLIST pidlSel = NULL;
  LPAFX_SHELLITEMINFO pSel = _Info(GetSelectedItem());
  if (pSel != NULL) pidlSel = PidlCopy(pSel->pidlFQ);
  m_bNoNotify = TRUE;
  DeleteAllItems();
  GetRootItems();
  if (pidlSel != NULL) SelectPath(pidlSel);
  m_bNoNotify = FALSE;
  ::CoTaskMemFree(pidlSel);
}

void CMFCShellTreeCtrl::SetFlags(DWORD dwFlags, BOOL bRefresh) {
  m_dwFlags = dwFlags;
  if (bRefresh) Refresh();
}

// =====================================================================
// CMFCShellListCtrl

IMPLEMENT_DYNAMIC(CMFCShellListCtrl, CMFCListCtrl)

BEGIN_MESSAGE_MAP(CMFCShellListCtrl, CMFCListCtrl)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_CONTEXTMENU()
  ON_NOTIFY_REFLECT_EX(NM_DBLCLK, OnDblclk)
  ON_NOTIFY_REFLECT_EX(LVN_KEYDOWN, OnKeydown)
  ON_NOTIFY_REFLECT_EX(LVN_DELETEITEM, OnDeleteitem)
END_MESSAGE_MAP()

CMFCShellListCtrl::CMFCShellListCtrl()
    : m_pRelatedTree(NULL), m_psfCurFolder(NULL), m_pidlCurFQ(NULL), m_nTypes(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS),
      m_bContextMenu(FALSE), m_bNoNotify(FALSE), m_bInitialized(FALSE) {}

CMFCShellListCtrl::~CMFCShellListCtrl() { _ReleaseCurrent(); }

void CMFCShellListCtrl::_ReleaseCurrent() {
  if (m_psfCurFolder != NULL) m_psfCurFolder->Release();
  m_psfCurFolder = NULL;
  ::CoTaskMemFree(m_pidlCurFQ);
  m_pidlCurFQ = NULL;
}

int CMFCShellListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CMFCListCtrl::OnCreate(lpCreateStruct) == -1) return -1;
  _Init();
  return 0;
}

void CMFCShellListCtrl::PreSubclassWindow() {
  CMFCListCtrl::PreSubclassWindow();
  if (AfxGetThreadState()->m_pWndInit == NULL) _Init();
}

void CMFCShellListCtrl::_Init() {
  if (m_bInitialized) return;
  m_bInitialized = TRUE;
  // The system's image lists are shared: the list must not free them.
  ModifyStyle(0, LVS_SHAREIMAGELISTS);
  ::SendMessage(m_hWnd, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)SystemImageList(TRUE));
  ::SendMessage(m_hWnd, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)SystemImageList(FALSE));
  OnSetColumns();
}

void CMFCShellListCtrl::OnDestroy() {
  m_bNoNotify = TRUE;
  DeleteAllItems();
  _ReleaseCurrent();
  CMFCListCtrl::OnDestroy();
}

void CMFCShellListCtrl::OnSetColumns() {
  static const struct {
    LPCTSTR lpszName;
    int nFormat, nWidth;
  } kColumns[] = {{_T("Name"), LVCFMT_LEFT, 180},
                  {_T("Size"), LVCFMT_RIGHT, 80},
                  {_T("Type"), LVCFMT_LEFT, 120},
                  {_T("Modified"), LVCFMT_LEFT, 140}};
  for (int i = 0; i < 4; i++) InsertColumn(i, kColumns[i].lpszName, kColumns[i].nFormat, kColumns[i].nWidth, i);
}

HRESULT CMFCShellListCtrl::DisplayFolder(LPCTSTR lpszPath) {
  if (lpszPath == NULL) return E_INVALIDARG;
  LPITEMIDLIST pidl = NULL;
  HRESULT hr = PathToPidl(lpszPath, &pidl);
  if (FAILED(hr)) return hr;
  hr = _DisplayFolder(pidl);
  ::CoTaskMemFree(pidl);
  return hr;
}

HRESULT CMFCShellListCtrl::DisplayFolder(LPAFX_SHELLITEMINFO lpItemInfo) {
  if (lpItemInfo == NULL) return E_INVALIDARG;
  return _DisplayFolder(lpItemInfo->pidlFQ);
}

HRESULT CMFCShellListCtrl::_DisplayFolder(LPCITEMIDLIST pidlFQ) {
  if (m_hWnd == NULL) return E_FAIL;
  _Init();
  AFX_SHELLITEMINFO info;
  info.pidlFQ = (LPITEMIDLIST)pidlFQ;
  LPSHELLFOLDER pFolder = NULL;
  HRESULT hr = BindFolder(&info, &pFolder);
  if (FAILED(hr)) return hr;
  LPITEMIDLIST pidlCopy = PidlCopy(pidlFQ);

  DeleteAllItems();
  _ReleaseCurrent();
  m_psfCurFolder = pFolder;
  m_pidlCurFQ = pidlCopy;

  CWaitCursor wait;
  SetRedraw(FALSE);
  hr = EnumObjects(m_psfCurFolder, m_pidlCurFQ);
  Sort(m_iSortedColumn >= 0 ? m_iSortedColumn : AFX_ShellList_ColumnName, m_bAscending);
  SetRedraw(TRUE);
  Invalidate();

  if (!m_bNoNotify) {
    if (m_pRelatedTree != NULL && m_pRelatedTree->GetSafeHwnd() != NULL) {
      m_pRelatedTree->m_bNoNotify = TRUE;
      m_pRelatedTree->SelectPath(m_pidlCurFQ);
      m_pRelatedTree->m_bNoNotify = FALSE;
    }
  }
  CWnd* pParent = GetParent();
  if (pParent != NULL) pParent->SendMessage(AFX_WM_CHANGE_CURRENT_FOLDER, 0, 0);
  return hr;
}

HRESULT CMFCShellListCtrl::EnumObjects(LPSHELLFOLDER pParentFolder, LPITEMIDLIST pidlParent) {
  IEnumIDList* pEnum = NULL;
  HRESULT hr = pParentFolder->EnumObjects(GetSafeHwnd(), m_nTypes, &pEnum);
  if (FAILED(hr) || pEnum == NULL) return FAILED(hr) ? hr : S_FALSE;
  LPITEMIDLIST pidlTemp = NULL;
  ULONG dwFetched = 0;
  int iItem = 0;
  while (pEnum->Next(1, &pidlTemp, &dwFetched) == S_OK && dwFetched == 1) {
    LPAFX_SHELLITEMINFO pItem = NewInfo(pParentFolder, pidlParent, pidlTemp);
    ::CoTaskMemFree(pidlTemp);
    pidlTemp = NULL;
    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    lvi.iItem = iItem;
    CString strName = OnGetItemText(iItem, AFX_ShellList_ColumnName, pItem);
    lvi.pszText = (LPTSTR)(LPCTSTR)strName;
    lvi.iImage = OnGetItemIcon(iItem, pItem);
    lvi.lParam = (LPARAM)pItem;
    int i = InsertItem(&lvi);
    if (i < 0) {
      FreeInfo(pItem);
      continue;
    }
    int nColumns = GetHeaderCtrl().GetSafeHwnd() != NULL ? GetHeaderCtrl().GetItemCount() : 1;
    for (int iColumn = 1; iColumn < nColumns; iColumn++) SetItemText(i, iColumn, OnGetItemText(i, iColumn, pItem));
    iItem++;
  }
  pEnum->Release();
  return S_OK;
}

CString CMFCShellListCtrl::OnGetItemText(int, int iColumn, LPAFX_SHELLITEMINFO pItem) {
  if (pItem == NULL) return CString();
  switch (iColumn) {
    case AFX_ShellList_ColumnName:
      return DisplayName(pItem, SHGDN_INFOLDER);
    case AFX_ShellList_ColumnSize: {
      if (ItemAttributes(pItem, SFGAO_FOLDER) & SFGAO_FOLDER) return CString();
      WIN32_FILE_ATTRIBUTE_DATA fd;
      if (!ItemFileData(pItem, &fd)) return CString();
      CString str;
      OnFormatFileSize(((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, str);
      return str;
    }
    case AFX_ShellList_ColumnType: {
      SHFILEINFO sfi;
      memset(&sfi, 0, sizeof(sfi));
      ::SHGetFileInfo((LPCTSTR)pItem->pidlFQ, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_TYPENAME);
      return sfi.szTypeName;
    }
    case AFX_ShellList_ColumnModified: {
      WIN32_FILE_ATTRIBUTE_DATA fd;
      if (!ItemFileData(pItem, &fd)) return CString();
      CString str;
      OnFormatFileDate(CTime(fd.ftLastWriteTime), str);
      return str;
    }
  }
  return CString();
}

int CMFCShellListCtrl::OnGetItemIcon(int, LPAFX_SHELLITEMINFO pItem) {
  if (pItem == NULL) return 0;
  return ShellIcon(pItem->pidlFQ, SHGFI_SMALLICON);
}

void CMFCShellListCtrl::OnFormatFileSize(ULONGLONG lFileSize, CString& str) {
  // In kilobytes, rounded up, with thousands grouped: "1,234 KB".
  ULONGLONG kb = (lFileSize + 1023) / 1024;
  TCHAR sz[32];
  int n = 0;
  do {
    if (n > 0 && n % 4 == 3) sz[n++] = _T(',');
    sz[n++] = (TCHAR)(_T('0') + (int)(kb % 10));
    kb /= 10;
  } while (kb != 0);
  str.Empty();
  while (n > 0) str += sz[--n];
  str += _T(" KB");
}

void CMFCShellListCtrl::OnFormatFileDate(const CTime& tmFile, CString& str) {
  str = tmFile.Format(_T("%x %H:%M"));
}

int CMFCShellListCtrl::OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn) {
  LPAFX_SHELLITEMINFO p1 = (LPAFX_SHELLITEMINFO)lParam1;
  LPAFX_SHELLITEMINFO p2 = (LPAFX_SHELLITEMINFO)lParam2;
  if (p1 == NULL || p2 == NULL) return 0;
  // Folders come before files.
  BOOL bFolder1 = (ItemAttributes(p1, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
  BOOL bFolder2 = (ItemAttributes(p2, SFGAO_FOLDER) & SFGAO_FOLDER) != 0;
  if (bFolder1 != bFolder2) return bFolder1 ? -1 : 1;
  int n = 0;
  switch (iColumn) {
    case AFX_ShellList_ColumnSize:
    case AFX_ShellList_ColumnModified: {
      WIN32_FILE_ATTRIBUTE_DATA fd1, fd2;
      BOOL b1 = ItemFileData(p1, &fd1), b2 = ItemFileData(p2, &fd2);
      if (!b1 || !b2) {
        n = b1 == b2 ? 0 : (b1 ? 1 : -1);
      } else if (iColumn == AFX_ShellList_ColumnSize) {
        ULONGLONG s1 = ((ULONGLONG)fd1.nFileSizeHigh << 32) | fd1.nFileSizeLow;
        ULONGLONG s2 = ((ULONGLONG)fd2.nFileSizeHigh << 32) | fd2.nFileSizeLow;
        n = s1 < s2 ? -1 : (s1 > s2 ? 1 : 0);
      } else {
        n = ::CompareFileTime(&fd1.ftLastWriteTime, &fd2.ftLastWriteTime);
      }
      break;
    }
    case AFX_ShellList_ColumnType:
      n = OnGetItemText(-1, AFX_ShellList_ColumnType, p1).CompareNoCase(OnGetItemText(-1, AFX_ShellList_ColumnType, p2));
      break;
  }
  // Then by name.
  if (n == 0) {
    n = DisplayName(p1, SHGDN_INFOLDER).CompareNoCase(DisplayName(p2, SHGDN_INFOLDER));
  }
  return n;
}

HRESULT CMFCShellListCtrl::DisplayParentFolder() {
  if (m_pidlCurFQ == NULL || PidlIsEmpty(m_pidlCurFQ)) return E_FAIL;
  LPITEMIDLIST pidl = PidlParent(m_pidlCurFQ);
  if (pidl == NULL) return E_OUTOFMEMORY;
  HRESULT hr = _DisplayFolder(pidl);
  ::CoTaskMemFree(pidl);
  return hr;
}

HRESULT CMFCShellListCtrl::Refresh() {
  if (m_pidlCurFQ == NULL) return E_FAIL;
  LPITEMIDLIST pidl = PidlCopy(m_pidlCurFQ);
  BOOL bNoNotify = m_bNoNotify;
  m_bNoNotify = TRUE;
  HRESULT hr = _DisplayFolder(pidl);
  m_bNoNotify = bNoNotify;
  ::CoTaskMemFree(pidl);
  return hr;
}

BOOL CMFCShellListCtrl::GetCurrentFolder(CString& strPath) const {
  strPath.Empty();
  if (m_pidlCurFQ == NULL) return FALSE;
  TCHAR szPath[MAX_PATH];
  if (!::SHGetPathFromIDList(m_pidlCurFQ, szPath)) return FALSE;
  strPath = szPath;
  return TRUE;
}

BOOL CMFCShellListCtrl::GetCurrentFolderName(CString& strName) const {
  strName.Empty();
  if (m_pidlCurFQ == NULL) return FALSE;
  SHFILEINFO sfi;
  memset(&sfi, 0, sizeof(sfi));
  if (!::SHGetFileInfo((LPCTSTR)m_pidlCurFQ, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_DISPLAYNAME)) return FALSE;
  strName = sfi.szDisplayName;
  return TRUE;
}

BOOL CMFCShellListCtrl::IsDesktop() const { return m_pidlCurFQ != NULL && PidlIsEmpty(m_pidlCurFQ); }

BOOL CMFCShellListCtrl::GetItemPath(CString& strPath, int iItem) const {
  strPath.Empty();
  LPAFX_SHELLITEMINFO pItem = _Info(iItem);
  if (pItem == NULL) return FALSE;
  TCHAR szPath[MAX_PATH];
  if (!::SHGetPathFromIDList(pItem->pidlFQ, szPath)) return FALSE;
  strPath = szPath;
  return TRUE;
}

void CMFCShellListCtrl::SetItemTypes(SHCONTF nTypes) {
  if (m_nTypes == nTypes) return;
  m_nTypes = nTypes;
  if (m_hWnd != NULL && m_pidlCurFQ != NULL) Refresh();
}

void CMFCShellListCtrl::DoDefault(int iItem) {
  LPAFX_SHELLITEMINFO pItem = _Info(iItem);
  if (pItem == NULL) return;
  if (ItemAttributes(pItem, SFGAO_FOLDER) & SFGAO_FOLDER) {
    LPITEMIDLIST pidl = PidlCopy(pItem->pidlFQ);  // the item goes with the list
    _DisplayFolder(pidl);
    ::CoTaskMemFree(pidl);
    return;
  }
  CString strPath;
  if (GetItemPath(strPath, iItem)) ::ShellExecute(GetSafeHwnd(), NULL, strPath, NULL, NULL, SW_SHOWNORMAL);
}

BOOL CMFCShellListCtrl::OnDblclk(NMHDR*, LRESULT* pResult) {
  *pResult = 0;
  int iItem = GetNextItem(-1, LVNI_FOCUSED);
  if (iItem < 0) iItem = GetNextItem(-1, LVNI_SELECTED);
  if (iItem >= 0) DoDefault(iItem);
  return FALSE;
}

BOOL CMFCShellListCtrl::OnKeydown(NMHDR* pNMHDR, LRESULT* pResult) {
  NMLVKEYDOWN* pkd = (NMLVKEYDOWN*)pNMHDR;
  *pResult = 0;
  switch (pkd->wVKey) {
    case VK_RETURN: {
      int iItem = GetNextItem(-1, LVNI_FOCUSED);
      if (iItem >= 0) DoDefault(iItem);
      break;
    }
    case VK_BACK:
      DisplayParentFolder();
      break;
    case VK_F5:
      Refresh();
      break;
  }
  return FALSE;
}

BOOL CMFCShellListCtrl::OnDeleteitem(NMHDR* pNMHDR, LRESULT* pResult) {
  NMLISTVIEW* pnm = (NMLISTVIEW*)pNMHDR;
  FreeInfo((LPAFX_SHELLITEMINFO)pnm->lParam);
  *pResult = 0;
  return FALSE;
}

void CMFCShellListCtrl::OnContextMenu(CWnd*, CPoint point) {
  if (!m_bContextMenu) {
    Default();
    return;
  }
  int iItem = GetNextItem(-1, LVNI_FOCUSED);
  if (point.x != -1 || point.y != -1) {
    CPoint ptClient = point;
    ScreenToClient(&ptClient);
    iItem = HitTest(ptClient);
  } else if (iItem >= 0) {
    CRect rect;
    GetItemRect(iItem, &rect, LVIR_ICON);
    point = rect.CenterPoint();
    ClientToScreen(&point);
  }
  OnShowContextMenu(iItem, point);
}

void CMFCShellListCtrl::OnShowContextMenu(int iItem, CPoint point) {
  LPAFX_SHELLITEMINFO pItem = _Info(iItem);
  if (pItem == NULL) return;
  ShellMenu(this, pItem->pParentFolder, pItem->pidlRel, point);
}
