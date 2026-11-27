// fpctrltest.cpp - the Feature Pack's bigger controls: CMFCListCtrl and
// CMFCHeaderCtrl sorting, CMFCTabCtrl's tabs and their notifications,
// CMFCPropertyGridCtrl's properties and in-place editing, and
// CMFCShellListCtrl and CMFCShellTreeCtrl on a folder made for the test.
#include <afxwinappex.h>
#include <afxcontrolbars.h>
#include "testutil.h"

#define IDC_LIST 1001
#define IDC_TABS 1002
#define IDC_GRID 1003
#define IDC_SHELLLIST 1004
#define IDC_SHELLTREE 1005

// ---------------------------------------------------------------------
// The host window, which records the notifications.

class CHostWnd : public CFrameWnd {
public:
  int m_nChanging, m_nChange, m_nLastTab, m_nVetoTab, m_nMoved, m_nPropChanged, m_nFolderChanged;
  LPARAM m_lLastProp;
  CHostWnd()
      : m_nChanging(0), m_nChange(0), m_nLastTab(-1), m_nVetoTab(-1), m_nMoved(0), m_nPropChanged(0),
        m_nFolderChanged(0), m_lLastProp(0) {}
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == AFX_WM_CHANGING_ACTIVE_TAB) {
      m_nChanging++;
      return (int)wParam == m_nVetoTab ? 1 : 0;
    }
    if (message == AFX_WM_CHANGE_ACTIVE_TAB) {
      m_nChange++;
      m_nLastTab = (int)wParam;
      return 0;
    }
    if (message == AFX_WM_ON_MOVETABCOMPLETE) {
      m_nMoved++;
      return 0;
    }
    if (message == AFX_WM_PROPERTY_CHANGED) {
      m_nPropChanged++;
      m_lLastProp = lParam;
      return 0;
    }
    if (message == AFX_WM_CHANGE_CURRENT_FOLDER) {
      m_nFolderChanged++;
      return 0;
    }
    return CFrameWnd::WindowProc(message, wParam, lParam);
  }
};

// ---------------------------------------------------------------------
// CMFCListCtrl and its header.

class CNumberList : public CMFCListCtrl {
public:
  int m_nCompares;
  CNumberList() : m_nCompares(0) {}
  virtual int OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn) {
    m_nCompares++;
    // Column 0 by the number, column 1 by its last digit.
    int a = (int)lParam1, b = (int)lParam2;
    if (iColumn == 1) {
      a %= 10;
      b %= 10;
    }
    return a < b ? -1 : (a > b ? 1 : 0);
  }
  virtual COLORREF OnGetCellTextColor(int nRow, int) { return nRow == 0 ? RGB(255, 0, 0) : (COLORREF)-1; }
};

static void TestList(CHostWnd* pHost) {
  TestLog("list and header...");
  CNumberList list;
  CHECK(list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT, CRect(10, 10, 300, 150), pHost, IDC_LIST));
  CHECK(list.GetHeaderCtrl().GetSafeHwnd() != NULL);
  CHECK(list.GetHeaderCtrl().IsKindOf(RUNTIME_CLASS(CMFCHeaderCtrl)));
  list.InsertColumn(0, _T("Number"), LVCFMT_LEFT, 120);
  list.InsertColumn(1, _T("Digit"), LVCFMT_LEFT, 120);
  static const int kValues[] = {42, 7, 19, 100, 33};
  for (int i = 0; i < 5; i++) {
    CString s;
    s.Format(_T("%d"), kValues[i]);
    int n = list.InsertItem(i, s);
    list.SetItemData(n, (DWORD_PTR)kValues[i]);
    s.Format(_T("%d"), kValues[i] % 10);
    list.SetItemText(n, 1, s);
  }
  CHECK(list.GetHeaderCtrl().GetSortColumn() == -1);

  list.Sort(0, TRUE);
  CHECK(list.m_nCompares > 0);
  CHECK(list.GetItemData(0) == 7 && list.GetItemData(4) == 100);
  CHECK(list.GetHeaderCtrl().GetSortColumn() == 0);
  CHECK(list.GetHeaderCtrl().IsAscending());
  list.Sort(0, FALSE);
  CHECK(list.GetItemData(0) == 100 && list.GetItemData(4) == 7);
  CHECK(!list.GetHeaderCtrl().IsAscending());
  CHECK(list.GetHeaderCtrl().GetColumnState(0) != 0);
  CHECK(list.GetHeaderCtrl().GetColumnState(1) == 0);

  // A click on a column header sorts by it, ascending; again, descending.
  NMLISTVIEW nm;
  memset(&nm, 0, sizeof(nm));
  nm.hdr.hwndFrom = list.m_hWnd;
  nm.hdr.idFrom = IDC_LIST;
  nm.hdr.code = LVN_COLUMNCLICK;
  nm.iItem = -1;
  nm.iSubItem = 1;
  pHost->SendMessage(WM_NOTIFY, IDC_LIST, (LPARAM)&nm);
  CHECK(list.GetHeaderCtrl().GetSortColumn() == 1);
  CHECK(list.GetHeaderCtrl().IsAscending());
  // By the last digit: 100 (0), 42 (2), 33 (3), 7 (7), 19 (9).
  CHECK(list.GetItemData(0) == 100 && list.GetItemData(1) == 42 && list.GetItemData(4) == 19);
  pHost->SendMessage(WM_NOTIFY, IDC_LIST, (LPARAM)&nm);
  CHECK(!list.GetHeaderCtrl().IsAscending());
  CHECK(list.GetItemData(0) == 19);

  list.RemoveSortColumn(1);
  CHECK(list.GetHeaderCtrl().GetSortColumn() == -1);
  list.EnableMultipleSort(TRUE);
  CHECK(list.IsMultipleSort());
  list.SetSortColumn(0, TRUE, TRUE);
  list.SetSortColumn(1, FALSE, TRUE);
  CHECK(list.GetHeaderCtrl().GetColumnState(0) != 0 && list.GetHeaderCtrl().GetColumnState(1) != 0);
  list.GetHeaderCtrl().RemoveAllSortColumns();
  CHECK(list.GetHeaderCtrl().GetColumnState(0) == 0 && list.GetHeaderCtrl().GetColumnState(1) == 0);
  list.EnableMultipleSort(FALSE);

  list.Sort(0, TRUE);
  list.EnableMarkSortedColumn(TRUE);
  pHost->UpdateWindow();
  list.UpdateWindow();
  TestPump();
  TestSaveWindowBitmap(pHost->m_hWnd, _T("fpctrltest-list.bmp"));
  list.DestroyWindow();
}

// ---------------------------------------------------------------------
// CMFCTabCtrl.

static void TestTabs(CHostWnd* pHost) {
  TestLog("tabs...");
  CMFCTabCtrl tabs;
  CHECK(tabs.Create(CMFCTabCtrl::STYLE_3D_ONENOTE, CRect(10, 10, 410, 210), pHost, IDC_TABS,
                    CMFCTabCtrl::LOCATION_TOP));
  CHECK(tabs.GetStyle() == CMFCTabCtrl::STYLE_3D_ONENOTE);
  CHECK(tabs.IsOneNoteStyle() && !tabs.IsFlatTab());
  CHECK(tabs.GetLocation() == CMFCTabCtrl::LOCATION_TOP);
  tabs.AutoDestroyWindow(FALSE);

  CEdit page[3];
  static LPCTSTR kNames[] = {_T("First"), _T("Second"), _T("Third")};
  for (int i = 0; i < 3; i++) {
    CHECK(page[i].Create(WS_CHILD | ES_MULTILINE, CRect(0, 0, 10, 10), &tabs, 100 + i));
    page[i].SetWindowText(kNames[i]);
  }
  int nChange = pHost->m_nChange;
  for (int i = 0; i < 3; i++) tabs.AddTab(&page[i], kNames[i]);
  CHECK(tabs.GetTabsNum() == 3);
  CHECK(tabs.GetVisibleTabsNum() == 3);
  CHECK(tabs.GetActiveTab() == 0);
  CHECK(pHost->m_nChange == nChange + 1 && pHost->m_nLastTab == 0);
  CHECK(tabs.GetActiveWnd() == &page[0]);
  CHECK(page[0].IsWindowVisible());
  CHECK(!page[1].IsWindowVisible() && !page[2].IsWindowVisible());
  CString str;
  CHECK(tabs.GetTabLabel(1, str) && str == _T("Second"));
  CHECK(tabs.SetTabLabel(1, _T("Two")));
  CHECK(tabs.GetTabLabel(1, str) && str == _T("Two"));
  CHECK(tabs.GetTabFromHwnd(page[2].m_hWnd) == 2);
  CHECK(tabs.GetTabWnd(2) == &page[2]);
  CHECK(tabs.GetTabByID(tabs.GetTabID(2)) == 2);

  // The window fills the area under the tabs.
  CRect rectWnd, rectPage;
  tabs.GetWndArea(rectWnd);
  page[0].GetWindowRect(rectPage);
  tabs.ScreenToClient(rectPage);
  CHECK(rectPage == rectWnd);
  CHECK(rectWnd.top >= tabs.GetTabsHeight());

  // Activating, and the parent's veto.
  CHECK(tabs.SetActiveTab(2));
  CHECK(tabs.GetActiveTab() == 2 && pHost->m_nLastTab == 2);
  CHECK(page[2].IsWindowVisible() && !page[0].IsWindowVisible());
  pHost->m_nVetoTab = 1;
  int nChanging = pHost->m_nChanging;
  CHECK(!tabs.SetActiveTab(1));
  CHECK(pHost->m_nChanging == nChanging + 1);
  CHECK(tabs.GetActiveTab() == 2);
  pHost->m_nVetoTab = -1;

  // A click on a tab activates it.
  CRect rectTab;
  CHECK(tabs.GetTabRect(0, rectTab));
  CHECK(!rectTab.IsRectEmpty());
  CPoint pt = rectTab.CenterPoint();
  CHECK(tabs.GetTabFromPoint(pt) == 0);
  tabs.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
  tabs.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
  CHECK(tabs.GetActiveTab() == 0);

  // Colors, hiding and moving.
  CHECK(tabs.SetTabBkColor(1, RGB(200, 220, 255)));
  CHECK(tabs.GetTabBkColor(1) == RGB(200, 220, 255));
  CHECK(tabs.SetTabTextColor(1, RGB(0, 0, 128)));
  CHECK(tabs.GetTabTextColor(1) == RGB(0, 0, 128));
  tabs.EnableAutoColor(TRUE);
  CHECK(tabs.IsAutoColor() && tabs.GetAutoColors().GetSize() > 0);
  tabs.SetActiveTabBoldFont(TRUE);
  CHECK(tabs.IsActiveTabBoldFont());
  pHost->UpdateWindow();
  tabs.UpdateWindow();
  TestPump();
  TestSaveWindowBitmap(pHost->m_hWnd, _T("fpctrltest-tabs.bmp"));

  CHECK(tabs.ShowTab(1, FALSE));
  CHECK(!tabs.IsTabVisible(1) && tabs.GetVisibleTabsNum() == 2);
  CHECK(tabs.ShowTab(1, TRUE));
  int nMoved = pHost->m_nMoved;
  CHECK(tabs.MoveTab(0, 2));
  CHECK(pHost->m_nMoved == nMoved + 1);
  CHECK(tabs.GetTabWnd(2) == &page[0] && tabs.GetActiveTab() == 2);
  CHECK(tabs.GetTabWnd(0) == &page[1]);

  // More tabs than fit: scroll buttons, and the active tab kept in view.
  tabs.ModifyTabStyle(CMFCTabCtrl::STYLE_3D_SCROLLED);
  CHECK(tabs.GetStyle() == CMFCTabCtrl::STYLE_3D_SCROLLED);
  CEdit more[12];
  for (int i = 0; i < 12; i++) {
    CHECK(more[i].Create(WS_CHILD, CRect(0, 0, 10, 10), &tabs, 200 + i));
    str.Format(_T("A longer tab %d"), i);
    tabs.AddTab(&more[i], str);
  }
  CHECK(tabs.GetTabsNum() == 15);
  CHECK(tabs.HasScrollButtons());
  CHECK(tabs.SetActiveTab(14));
  CHECK(tabs.GetFirstVisibleTabNum() > 0);
  CHECK(tabs.SetActiveTab(0));
  CHECK(tabs.GetFirstVisibleTabNum() == 0);

  CHECK(tabs.RemoveTab(0));
  CHECK(tabs.GetTabsNum() == 14);
  tabs.RemoveAllTabs();
  CHECK(tabs.GetTabsNum() == 0 && tabs.GetActiveTab() == -1);
  tabs.DestroyWindow();
}

// ---------------------------------------------------------------------
// CMFCPropertyGridCtrl.

static void TestPropertyGrid(CHostWnd* pHost) {
  TestLog("property grid...");
  CMFCPropertyGridCtrl grid;
  CHECK(grid.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, CRect(10, 10, 410, 330), pHost, IDC_GRID));
  grid.EnableHeaderCtrl(TRUE, _T("Name"), _T("Setting"));
  grid.EnableDescriptionArea(TRUE);
  grid.SetVSDotNetLook(TRUE);
  grid.MarkModifiedProperties(TRUE);
  CHECK(grid.IsHeaderCtrl() && grid.IsDescriptionArea() && grid.IsVSDotNetLook());
  CHECK(grid.GetHeaderCtrl().GetSafeHwnd() != NULL);

  CMFCPropertyGridProperty* pGroup = new CMFCPropertyGridProperty(_T("Appearance"));
  CMFCPropertyGridProperty* pName =
      new CMFCPropertyGridProperty(_T("Caption"), COleVariant(_T("Hello")), _T("The window's caption"), 1);
  CMFCPropertyGridProperty* pWidth =
      new CMFCPropertyGridProperty(_T("Width"), COleVariant((long)120), _T("In pixels"), 2);
  CMFCPropertyGridProperty* pVisible =
      new CMFCPropertyGridProperty(_T("Visible"), COleVariant((short)VARIANT_TRUE, VT_BOOL), _T("Shown"), 3);
  CMFCPropertyGridProperty* pBorder =
      new CMFCPropertyGridProperty(_T("Border"), COleVariant(_T("Thin")), _T("The border's kind"), 4);
  CHECK(pBorder->AddOption(_T("None")));
  CHECK(pBorder->AddOption(_T("Thin")));
  CHECK(pBorder->AddOption(_T("Thick")));
  CHECK(!pBorder->AddOption(_T("Thin")));  // unique
  CHECK(pBorder->GetOptionCount() == 3);
  pBorder->AllowEdit(FALSE);
  CHECK(pGroup->AddSubItem(pName));
  CHECK(pGroup->AddSubItem(pWidth));
  CHECK(pGroup->AddSubItem(pVisible));
  CHECK(pGroup->AddSubItem(pBorder));
  CHECK(pGroup->IsGroup() && pGroup->GetSubItemsCount() == 4);
  CHECK(pWidth->GetParent() == pGroup);
  grid.AddProperty(pGroup);

  CMFCPropertyGridProperty* pColors = new CMFCPropertyGridProperty(_T("Colors"));
  CMFCPropertyGridColorProperty* pColor =
      new CMFCPropertyGridColorProperty(_T("Back"), RGB(0x12, 0x34, 0x56), NULL, _T("The background"), 5);
  pColor->EnableOtherButton(_T("More..."));
  pColors->AddSubItem(pColor);
  CMFCPropertyGridProperty* pSize = new CMFCPropertyGridProperty(_T("Size"), 6, TRUE);
  pSize->AddSubItem(new CMFCPropertyGridProperty(_T("cx"), COleVariant((long)10), NULL, 7));
  pSize->AddSubItem(new CMFCPropertyGridProperty(_T("cy"), COleVariant((long)20), NULL, 8));
  pColors->AddSubItem(pSize);
  grid.AddProperty(pColors);

  CHECK(grid.GetPropertyCount() == 2);
  CHECK(grid.GetProperty(0) == pGroup);
  CHECK(grid.FindItemByData(2) == pWidth);
  CHECK(grid.FindItemByData(8) != NULL);
  CHECK(grid.FindItemByData(8, FALSE) == NULL);
  CHECK(pGroup->FindSubItemByData(4) == pBorder);

  CHECK(pName->FormatProperty() == _T("Hello"));
  CHECK(pWidth->FormatProperty() == _T("120"));
  CHECK(pVisible->FormatProperty() == grid.GetTrueLabel());
  CHECK(pColor->FormatProperty() == _T("123456"));
  CHECK(pSize->FormatProperty() == _T("10, 20"));

  pWidth->SetValue(COleVariant((long)200));
  CHECK(pWidth->GetValue().lVal == 200);
  CHECK(pWidth->IsValueChanged());
  pWidth->ResetOriginalValue();
  CHECK(pWidth->GetValue().lVal == 120);
  CHECK(!pWidth->IsValueChanged());

  // Selection, hit testing and collapsing.
  grid.SetCurSel(pWidth);
  CHECK(grid.GetCurSel() == pWidth && pWidth->IsSelected());
  CRect rect = pWidth->GetRect();
  CHECK(!rect.IsRectEmpty());
  CHECK(grid.HitTest(rect.CenterPoint()) == pWidth);
  pGroup->Expand(FALSE);
  CHECK(!pGroup->IsExpanded());
  CHECK(!pWidth->IsParentExpanded());
  grid.ExpandAll(TRUE);
  CHECK(pGroup->IsExpanded() && pWidth->IsParentExpanded());

  // Editing in place: the value from the edit, and the parent told.
  int nChanged = pHost->m_nPropChanged;
  CHECK(grid.EditItem(pWidth));
  CHECK(pWidth->IsInPlaceEditing());
  CWnd* pEdit = grid.GetDlgItem(3);
  CHECK(pEdit != NULL);
  if (pEdit != NULL) pEdit->SetWindowText(_T("321"));
  CHECK(grid.EndEditItem(TRUE));
  CHECK(!pWidth->IsInPlaceEditing());
  CHECK(pWidth->GetValue().lVal == 321);
  CHECK(pWidth->IsModified());
  CHECK(pHost->m_nPropChanged == nChanged + 1);
  CHECK(pHost->m_lLastProp == (LPARAM)pWidth);

  // A bad number is refused and the edit stays.
  CHECK(grid.EditItem(pWidth));
  pEdit = grid.GetDlgItem(3);
  if (pEdit != NULL) pEdit->SetWindowText(_T("abc"));
  CHECK(!grid.EndEditItem(TRUE));
  CHECK(pWidth->GetValue().lVal == 321);
  CHECK(grid.EndEditItem(FALSE));

  // A value list's sub-items through its own text.
  CHECK(grid.EditItem(pSize));
  pEdit = grid.GetDlgItem(3);
  if (pEdit != NULL) pEdit->SetWindowText(_T("30, 40"));
  CHECK(grid.EndEditItem(TRUE));
  CHECK(pSize->GetSubItem(0)->GetValue().lVal == 30 && pSize->GetSubItem(1)->GetValue().lVal == 40);

  // The color as text.
  CHECK(grid.EditItem(pColor));
  pEdit = grid.GetDlgItem(3);
  if (pEdit != NULL) pEdit->SetWindowText(_T("FF8000"));
  CHECK(grid.EndEditItem(TRUE));
  CHECK(pColor->GetColor() == RGB(0xFF, 0x80, 0x00));

  // Alphabetic mode lists the values without their groups.
  grid.SetAlphabeticMode(TRUE);
  CHECK(grid.IsAlphabeticMode());
  grid.SetAlphabeticMode(FALSE);

  pVisible->Enable(FALSE);
  CHECK(!pVisible->IsEnabled());
  CHECK(!grid.EditItem(pVisible));
  pHost->UpdateWindow();
  grid.UpdateWindow();
  TestPump();
  TestSaveWindowBitmap(pHost->m_hWnd, _T("fpctrltest-grid.bmp"));

  CMFCPropertyGridProperty* pDel = pColors;
  CHECK(grid.DeleteProperty(pDel));
  CHECK(pDel == NULL && grid.GetPropertyCount() == 1);
  grid.RemoveAll();
  CHECK(grid.GetPropertyCount() == 0);
  grid.DestroyWindow();
}

// ---------------------------------------------------------------------
// CMFCShellListCtrl and CMFCShellTreeCtrl.

static BOOL EndsWith(const CString& str, LPCTSTR lpszTail) {
  CString tail(lpszTail);
  return str.GetLength() >= tail.GetLength() && str.Right(tail.GetLength()).CompareNoCase(tail) == 0;
}

static void WriteFile(const CString& strPath, int nBytes) {
  CFile f;
  if (!f.Open(strPath, CFile::modeCreate | CFile::modeWrite)) return;
  BYTE buf[256];
  memset(buf, 'x', sizeof(buf));
  while (nBytes > 0) {
    int n = nBytes > (int)sizeof(buf) ? (int)sizeof(buf) : nBytes;
    f.Write(buf, n);
    nBytes -= n;
  }
  f.Close();
}

static int FindItem(CMFCShellListCtrl& list, LPCTSTR lpszName) {
  for (int i = 0; i < list.GetItemCount(); i++)
    if (list.GetItemText(i, AFX_ShellList_ColumnName).CompareNoCase(lpszName) == 0) return i;
  return -1;
}

static void TestShell(CHostWnd* pHost) {
  TestLog("shell list and tree...");
  TCHAR szTemp[MAX_PATH];
  ::GetTempPath(MAX_PATH, szTemp);
  TCHAR szLong[MAX_PATH];
  if (::GetLongPathName(szTemp, szLong, MAX_PATH) > 0) lstrcpy(szTemp, szLong);
  CString strRoot = szTemp;
  if (!strRoot.IsEmpty() && strRoot.Right(1) != _T("\\") && strRoot.Right(1) != _T("/")) strRoot += _T("\\");
  strRoot += _T("fpctrltest");
  CString strSub = strRoot + _T("\\sub");
  ::CreateDirectory(strRoot, NULL);
  ::CreateDirectory(strSub, NULL);
  WriteFile(strRoot + _T("\\a.txt"), 10);
  WriteFile(strRoot + _T("\\b.dat"), 2000);
  WriteFile(strSub + _T("\\c.txt"), 1);

  CMFCShellListCtrl list;
  CHECK(list.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT, CRect(200, 10, 600, 300), pHost, IDC_SHELLLIST));
  CHECK(list.GetHeaderCtrl().GetItemCount() == 4);
  int nFolder = pHost->m_nFolderChanged;
  CHECK(SUCCEEDED(list.DisplayFolder(strRoot)));
  CHECK(pHost->m_nFolderChanged == nFolder + 1);
  CHECK(list.GetItemCount() == 3);
  CHECK(!list.IsDesktop());
  CString str;
  CHECK(list.GetCurrentFolder(str) && EndsWith(str, _T("fpctrltest")));
  CHECK(list.GetCurrentFolderName(str) && str.CompareNoCase(_T("fpctrltest")) == 0);
  CHECK(list.GetCurrentShellFolder() != NULL && list.GetCurrentItemIdList() != NULL);

  // The folder first, then the files by name.
  CHECK(list.GetItemText(0, AFX_ShellList_ColumnName).CompareNoCase(_T("sub")) == 0);
  CHECK(list.GetItemText(1, AFX_ShellList_ColumnName).CompareNoCase(_T("a.txt")) == 0);
  CHECK(list.GetItemText(2, AFX_ShellList_ColumnName).CompareNoCase(_T("b.dat")) == 0);
  CHECK(list.GetItemPath(str, 1) && EndsWith(str, _T("a.txt")));
  int iB = FindItem(list, _T("b.dat"));
  CHECK(iB >= 0 && list.GetItemText(iB, AFX_ShellList_ColumnSize) == _T("2 KB"));
  CHECK(list.GetItemText(0, AFX_ShellList_ColumnSize).IsEmpty());
  CHECK(!list.GetItemText(iB, AFX_ShellList_ColumnType).IsEmpty());
  CHECK(!list.GetItemText(iB, AFX_ShellList_ColumnModified).IsEmpty());
  CString strSize;
  list.OnFormatFileSize(5000000, strSize);
  CHECK(strSize == _T("4,883 KB"));

  // By size, largest first: the folder still leads.
  list.Sort(AFX_ShellList_ColumnSize, FALSE);
  CHECK(list.GetItemText(1, AFX_ShellList_ColumnName).CompareNoCase(_T("a.txt")) == 0);
  list.Sort(AFX_ShellList_ColumnSize, TRUE);
  CHECK(list.GetItemText(0, AFX_ShellList_ColumnName).CompareNoCase(_T("sub")) == 0);

  // Files only.
  list.SetItemTypes(SHCONTF_NONFOLDERS);
  CHECK(list.GetItemTypes() == SHCONTF_NONFOLDERS);
  CHECK(list.GetItemCount() == 2);
  list.SetItemTypes(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS);
  CHECK(list.GetItemCount() == 3);

  // Opening the folder, and Backspace back up.
  int iSub = FindItem(list, _T("sub"));
  CHECK(iSub >= 0);
  list.SetItemState(iSub, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
  NMLVKEYDOWN kd;
  memset(&kd, 0, sizeof(kd));
  kd.hdr.hwndFrom = list.m_hWnd;
  kd.hdr.idFrom = IDC_SHELLLIST;
  kd.hdr.code = LVN_KEYDOWN;
  kd.wVKey = VK_RETURN;
  pHost->SendMessage(WM_NOTIFY, IDC_SHELLLIST, (LPARAM)&kd);
  CHECK(list.GetCurrentFolder(str) && EndsWith(str, _T("sub")));
  CHECK(list.GetItemCount() == 1);
  kd.wVKey = VK_BACK;
  pHost->SendMessage(WM_NOTIFY, IDC_SHELLLIST, (LPARAM)&kd);
  CHECK(list.GetCurrentFolder(str) && EndsWith(str, _T("fpctrltest")));
  CHECK(SUCCEEDED(list.DisplayParentFolder()));
  CHECK(list.GetCurrentFolder(str) && !EndsWith(str, _T("fpctrltest")));

  // The tree: down to the folder, and the list following it.
  CMFCShellTreeCtrl tree;
  CHECK(tree.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                        TVS_SHOWSELALWAYS,
                    CRect(10, 10, 195, 300), pHost, IDC_SHELLTREE));
  CHECK(tree.GetRootItem() != NULL);
  CHECK(tree.GetChildItem(tree.GetRootItem()) != NULL);  // the desktop opened
  CHECK(tree.GetFlags() == SHCONTF_FOLDERS);
  tree.SetRelatedList(&list);
  CHECK(tree.GetRelatedList() == &list && list.GetRelatedTree() == &tree);
  CHECK(tree.SelectPath(strRoot));
  CHECK(tree.GetItemPath(str) && EndsWith(str, _T("fpctrltest")));
  CHECK(list.GetCurrentFolder(str) && EndsWith(str, _T("fpctrltest")));
  CHECK(list.GetItemCount() == 3);
  HTREEITEM hRoot = tree.GetSelectedItem();
  tree.Expand(hRoot, TVE_EXPAND);
  HTREEITEM hSub = tree.GetChildItem(hRoot);
  CHECK(hSub != NULL && tree.GetItemText(hSub).CompareNoCase(_T("sub")) == 0);
  CHECK(tree.GetNextSiblingItem(hSub) == NULL);  // folders only
  CHECK(tree.SelectPath(strSub));
  CHECK(tree.GetSelectedItem() == hSub);
  CHECK(list.GetCurrentFolder(str) && EndsWith(str, _T("sub")));
  // The list going up moves the tree with it.
  CHECK(SUCCEEDED(list.DisplayParentFolder()));
  CHECK(tree.GetItemPath(str) && EndsWith(str, _T("fpctrltest")));

  tree.SetFlags(SHCONTF_FOLDERS | SHCONTF_NONFOLDERS);
  CHECK(tree.GetItemPath(str) && EndsWith(str, _T("fpctrltest")));
  hRoot = tree.GetSelectedItem();
  tree.Expand(hRoot, TVE_EXPAND);
  int nChildren = 0;
  for (HTREEITEM h = tree.GetChildItem(hRoot); h != NULL; h = tree.GetNextSiblingItem(h)) nChildren++;
  CHECK(nChildren == 3);

  pHost->UpdateWindow();
  tree.UpdateWindow();
  list.UpdateWindow();
  TestPump();
  TestSaveWindowBitmap(pHost->m_hWnd, _T("fpctrltest-shell.bmp"));
  tree.DestroyWindow();
  list.DestroyWindow();

  ::DeleteFile(strSub + _T("\\c.txt"));
  ::DeleteFile(strRoot + _T("\\a.txt"));
  ::DeleteFile(strRoot + _T("\\b.dat"));
  ::RemoveDirectory(strSub);
  CHECK(::RemoveDirectory(strRoot));
}

// ---------------------------------------------------------------------

class CTestApp : public CWinAppEx {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("fpctrltest.log"));
    CWinAppEx::InitInstance();
    ::InitCommonControls();
    ::CoInitialize(NULL);
    CHostWnd* pHost = new CHostWnd;
    if (!pHost->Create(NULL, _T("fpctrltest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 640, 400))) {
      TestLog("Create failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pHost;
    pHost->ShowWindow(SW_SHOW);
    pHost->UpdateWindow();

    TestList(pHost);
    TestTabs(pHost);
    TestPropertyGrid(pHost);
    TestShell(pHost);

    pHost->DestroyWindow();
    m_pMainWnd = NULL;
    ::CoUninitialize();
    return FALSE;
  }
  virtual int ExitInstance() {
    CWinAppEx::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
