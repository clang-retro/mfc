// win16cmn.cpp - the common controls that the library draws on 16-bit
// Windows: CImageList, CProgressCtrl, CSliderCtrl, CSpinButtonCtrl and
// CTabCtrl, CHeaderCtrl, CListCtrl, CTreeCtrl, CToolTipCtrl, CHotKeyCtrl,
// CIPAddressCtrl, CPagerCtrl, CLinkCtrl, CComboBoxEx, CMonthCalCtrl and
// CDateTimeCtrl. The frame makes each control, sends it messages and
// checks the notifications that the frame gets. It prints ALL PASSED when each
// check holds, because Wine gives the exit code 0 whatever a 16-bit
// program returns.
#include <afxwin.h>
#include <afxcmn.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static BOOL g_bDone = FALSE;  // the test ran to the end
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

enum { IDC_PROGRESS = 100, IDC_SLIDER, IDC_BUDDY, IDC_SPIN, IDC_TAB, IDC_LIST, IDC_TREE, IDC_HOTKEY, IDC_IPADDR, IDC_PAGER,
       IDC_PAGERCHILD, IDC_LINK, IDC_COMBOEX, IDC_MONTHCAL,
       IDC_DATETIME, IDT_RECT = 500 };

class CMainFrame : public CFrameWnd {
public:
  CProgressCtrl m_progress;
  CSliderCtrl m_slider;
  CEdit m_buddy;
  CSpinButtonCtrl m_spin;
  CTabCtrl m_tab;
  CListCtrl m_list;
  CTreeCtrl m_tree;
  CToolTipCtrl m_tip;
  CHotKeyCtrl m_hotkey;
  CIPAddressCtrl m_ipaddr;
  CPagerCtrl m_pager;
  CLinkCtrl m_link;
  CComboBoxEx m_comboEx;
  CMonthCalCtrl m_cal;
  CDateTimeCtrl m_dtp;
  UINT m_nLastHScroll;    // the code of the last WM_HSCROLL from the slider
  int m_nLastHScrollPos;  // its position
  int m_nVScrolls;        // the WM_VSCROLL messages from the up-down control
  int m_nDeltaPos;        // the UDN_DELTAPOS notifications
  int m_nLastDelta;       // the iDelta of the last one
  int m_nSelChanging;     // the TCN_SELCHANGING notifications
  int m_nSelChange;       // the TCN_SELCHANGE notifications
  BOOL m_bRefuseTab;      // TCN_SELCHANGING refuses the change
  int m_nItemChanged;     // the LVN_ITEMCHANGED notifications
  int m_nExpanding;       // the TVN_ITEMEXPANDING notifications
  int m_nTreeSelChanged;  // the TVN_SELCHANGED notifications
  int m_nTreeDeleted;     // the TVN_DELETEITEM notifications
  int m_nTipText;         // the TTN_NEEDTEXT notifications of IDT_RECT
  int m_nTipShow;         // the TTN_SHOW notifications of IDT_RECT
  int m_nTipPop;          // the TTN_POP notifications of IDT_RECT
  int m_nHotKeyChange;    // the EN_CHANGE notifications of the hot key
  int m_nFieldChanged;    // the IPN_FIELDCHANGED notifications
  int m_nCalcSize;        // the PGN_CALCSIZE notifications
  int m_nLinkClick;       // the NM_CLICK notifications of the link control
  int m_iLastLink;        // the link of the last NM_CLICK
  int m_nCbInserted;      // the CBEN_INSERTITEM notifications
  int m_nCbDeleted;       // the CBEN_DELETEITEM notifications
  int m_nCalSelChange;    // the MCN_SELCHANGE notifications
  int m_nDtChange;        // the DTN_DATETIMECHANGE notifications

  /// Starts with no notifications.
  CMainFrame()
      : m_nLastHScroll(0xFFFF), m_nLastHScrollPos(-1), m_nVScrolls(0), m_nDeltaPos(0),
        m_nLastDelta(0), m_nSelChanging(0), m_nSelChange(0), m_bRefuseTab(FALSE),
        m_nItemChanged(0), m_nExpanding(0), m_nTreeSelChanged(0), m_nTreeDeleted(0),
        m_nTipText(0), m_nTipShow(0), m_nTipPop(0), m_nHotKeyChange(0), m_nFieldChanged(0),
        m_nCalcSize(0), m_nLinkClick(0), m_iLastLink(-1), m_nCbInserted(0), m_nCbDeleted(0),
        m_nCalSelChange(0), m_nDtChange(0) {}

  /// Makes the controls and runs the checks.
  void RunTests() {
    TestImageList();
    TestProgress();
    TestSlider();
    TestSpin();
    TestTab();
    TestListView();
    TestTreeView();
    TestToolTip();
    TestHotKey();
    TestIPAddress();
    TestPager();
    TestLink();
    TestComboBoxEx();
    TestMonthCal();
    TestDateTime();
    g_bDone = TRUE;
  }

protected:
  /// Checks the images of an image list: two images from a bitmap whose
  /// red pixels are transparent, a draw on a white background, and the
  /// removal of an image.
  void TestImageList() {
    CClientDC dcScreen(this);
    CDC dc;
    dc.CreateCompatibleDC(&dcScreen);
    CBitmap bm;
    bm.CreateCompatibleBitmap(&dcScreen, 32, 16);
    CBitmap* pOld = dc.SelectObject(&bm);
    dc.FillSolidRect(0, 0, 32, 16, RGB(255, 0, 0));  // transparent
    dc.FillSolidRect(4, 4, 8, 8, RGB(0, 0, 255));    // the first image
    dc.FillSolidRect(20, 4, 8, 8, RGB(0, 255, 0));   // the second image
    dc.SelectObject(pOld);

    CImageList il;
    CHECK(il.Create(16, 16, ILC_COLOR | ILC_MASK, 2, 2));
    CHECK(il.Add(&bm, RGB(255, 0, 0)) == 0);
    CHECK(il.GetImageCount() == 2);
    int cx = 0, cy = 0;
    CHECK(::ImageList_GetIconSize(il.GetSafeHandle(), &cx, &cy) && cx == 16 && cy == 16);

    CBitmap bmDraw;
    bmDraw.CreateCompatibleBitmap(&dcScreen, 16, 16);
    pOld = dc.SelectObject(&bmDraw);
    dc.FillSolidRect(0, 0, 16, 16, RGB(255, 255, 255));
    CHECK(il.Draw(&dc, 1, CPoint(0, 0), ILD_TRANSPARENT));
    CHECK(dc.GetPixel(0, 0) == RGB(255, 255, 255));  // the red pixels are transparent
    CHECK(dc.GetPixel(8, 8) == RGB(0, 255, 0));
    dc.SelectObject(pOld);

    CHECK(il.Remove(0));
    CHECK(il.GetImageCount() == 1);
    IMAGEINFO info;
    CHECK(il.GetImageInfo(0, &info) && info.rcImage.right == 16);
    CHECK(il.DeleteImageList());
  }

  /// Checks the range, the position and the step of the progress bar.
  void TestProgress() {
    CHECK(m_progress.Create(WS_CHILD | WS_VISIBLE, CRect(10, 10, 210, 30), this, IDC_PROGRESS));
    m_progress.SetRange(0, 50);
    int nLower = -1, nUpper = -1;
    m_progress.GetRange(nLower, nUpper);
    CHECK(nLower == 0 && nUpper == 50);
    CHECK(m_progress.SetPos(20) == 0);
    CHECK(m_progress.GetPos() == 20);
    m_progress.SetStep(5);
    CHECK(m_progress.StepIt() == 20);
    CHECK(m_progress.GetPos() == 25);
    m_progress.SetPos(100);  // past the end: the position stops at the end
    CHECK(m_progress.GetPos() == 50);
    m_progress.UpdateWindow();
  }

  /// Checks the range and the position of the slider, and the scroll
  /// messages that its keys send.
  void TestSlider() {
    CHECK(m_slider.Create(WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, CRect(10, 40, 210, 80), this,
                          IDC_SLIDER));
    m_slider.SetRange(0, 10, TRUE);
    CHECK(m_slider.GetRangeMin() == 0 && m_slider.GetRangeMax() == 10);
    m_slider.SetPos(3);
    CHECK(m_slider.GetPos() == 3);
    m_slider.SetPos(20);
    CHECK(m_slider.GetPos() == 10);
    m_slider.SetPos(5);
    m_slider.SetPageSize(4);
    CHECK(m_slider.GetPageSize() == 4);
    m_slider.SendMessage(WM_KEYDOWN, VK_RIGHT, 0);
    CHECK(m_slider.GetPos() == 6);
    CHECK(m_nLastHScroll == TB_LINEDOWN && m_nLastHScrollPos == 6);
    m_slider.SendMessage(WM_KEYDOWN, VK_PRIOR, 0);
    CHECK(m_slider.GetPos() == 2);
    CHECK(m_nLastHScroll == TB_PAGEUP);
    m_slider.SendMessage(WM_KEYUP, VK_PRIOR, 0);
    CHECK(m_nLastHScroll == TB_ENDTRACK);
    CRect rcThumb;
    m_slider.GetThumbRect(&rcThumb);
    CHECK(!rcThumb.IsRectEmpty());
    m_slider.UpdateWindow();
  }

  /// Checks the range and the position of the up-down control, the text
  /// of its buddy, and the notifications of a click on its up arrow.
  void TestSpin() {
    CHECK(m_buddy.Create(WS_CHILD | WS_VISIBLE | WS_BORDER, CRect(10, 90, 110, 112), this,
                         IDC_BUDDY));
    CHECK(m_spin.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                        CRect(0, 0, 16, 22), this, IDC_SPIN));
    m_spin.SetBuddy(&m_buddy);
    CHECK(m_spin.GetBuddy() == &m_buddy);
    CRect rcBuddy, rcSpin;
    m_buddy.GetWindowRect(&rcBuddy);
    m_spin.GetWindowRect(&rcSpin);
    CHECK(rcSpin.left == rcBuddy.right);  // UDS_ALIGNRIGHT
    m_spin.SetRange(0, 2000);
    int nLower = -1, nUpper = -1;
    m_spin.GetRange(nLower, nUpper);
    CHECK(nLower == 0 && nUpper == 2000);
    m_spin.SetPos(1234);
    CHECK(m_spin.GetPos() == 1234);
    CString strText;
    m_buddy.GetWindowText(strText);
    CHECK(strText == "1,234");
    m_buddy.SetWindowText("7");
    CHECK(LOWORD(m_spin.GetPos()) == 7);  // the position comes from the buddy

    // A click on the up arrow: UDN_DELTAPOS, then WM_VSCROLL, then
    // SB_ENDSCROLL when the button goes up.
    m_spin.GetClientRect(&rcSpin);
    LPARAM lUp = MAKELPARAM(rcSpin.Width() / 2, 2);
    m_spin.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, lUp);
    m_spin.SendMessage(WM_LBUTTONUP, 0, lUp);
    CHECK(m_nDeltaPos == 1);
    CHECK(m_nLastDelta == 1);
    CHECK(m_nVScrolls == 2);
    CHECK(m_spin.GetPos() == 8);
    m_buddy.GetWindowText(strText);
    CHECK(strText == "8");

    // UDS_ARROWKEYS: the down key of the buddy steps down.
    m_buddy.SendMessage(WM_KEYDOWN, VK_DOWN, 0);
    CHECK(m_spin.GetPos() == 7);
    CHECK(m_nLastDelta == -1);
  }

  /// Checks the tabs of the tab control, and the notifications of a click
  /// on a tab.
  void TestTab() {
    CHECK(m_tab.Create(WS_CHILD | WS_VISIBLE, CRect(220, 10, 420, 150), this, IDC_TAB));
    CHECK(m_tab.InsertItem(0, "One") == 0);
    CHECK(m_tab.InsertItem(1, "Two") == 1);
    CHECK(m_tab.InsertItem(2, "Three") == 2);
    CHECK(m_tab.GetItemCount() == 3);
    CHECK(m_tab.GetCurSel() == 0);
    char szText[16];
    TCITEM item;
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = szText;
    item.cchTextMax = sizeof(szText);
    CHECK(m_tab.GetItem(2, &item) && strcmp(szText, "Three") == 0);
    CHECK(m_tab.SetCurSel(1) == 0);
    CHECK(m_tab.GetCurSel() == 1);
    CHECK(m_nSelChange == 0);  // SetCurSel does not notify

    // A click on the third tab.
    CRect rcItem;
    CHECK(m_tab.GetItemRect(2, &rcItem));
    LPARAM lClick = MAKELPARAM(rcItem.CenterPoint().x, rcItem.CenterPoint().y);
    m_tab.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, lClick);
    CHECK(m_nSelChanging == 1 && m_nSelChange == 1);
    CHECK(m_tab.GetCurSel() == 2);

    // A refused change.
    m_bRefuseTab = TRUE;
    CHECK(m_tab.GetItemRect(0, &rcItem));
    lClick = MAKELPARAM(rcItem.CenterPoint().x, rcItem.CenterPoint().y);
    m_tab.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, lClick);
    CHECK(m_nSelChanging == 2 && m_nSelChange == 1);
    CHECK(m_tab.GetCurSel() == 2);

    // The display area is inside the window.
    CRect rcClient, rcDisplay;
    m_tab.GetClientRect(&rcClient);
    rcDisplay = rcClient;
    m_tab.AdjustRect(FALSE, &rcDisplay);
    CHECK(rcDisplay.top > rcClient.top && rcDisplay.bottom < rcClient.bottom);
    m_tab.AdjustRect(TRUE, &rcDisplay);
    CHECK(rcDisplay == rcClient);

    CHECK(m_tab.DeleteItem(0));
    CHECK(m_tab.GetItemCount() == 2);
    CHECK(m_tab.GetCurSel() == 1);
    m_tab.UpdateWindow();
  }

  /// Compares two items by their item data, from the largest to the
  /// smallest.
  static int CALLBACK CompareDown(LPARAM l1, LPARAM l2, LPARAM lSort) {
    (void)lSort;
    return l1 < l2 ? 1 : l1 > l2 ? -1 : 0;
  }

  /// Checks a report view: the columns in its header, the items and their
  /// subitems, a find, a sort, a selection by a click and the
  /// LVN_ITEMCHANGED notification.
  void TestListView() {
    CHECK(m_list.Create(WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                        CRect(10, 160, 410, 300), this, IDC_LIST));
    CHECK(m_list.InsertColumn(0, "Name", LVCFMT_LEFT, 120) == 0);
    CHECK(m_list.InsertColumn(1, "Size", LVCFMT_RIGHT, 80, 1) == 1);
    CHeaderCtrl* pHeader = m_list.GetHeaderCtrl();
    CHECK(pHeader != NULL && pHeader->GetItemCount() == 2);
    CHECK(m_list.GetColumnWidth(0) == 120);
    CHECK(m_list.SetColumnWidth(1, 90) && m_list.GetColumnWidth(1) == 90);

    static const char* const names[] = {"alpha", "beta", "gamma"};
    for (int i = 0; i < 3; i++) {
      CHECK(m_list.InsertItem(i, names[i]) == i);
      char szSize[8];
      sprintf(szSize, "%d", (i + 1) * 10);
      CHECK(m_list.SetItemText(i, 1, szSize));
      CHECK(m_list.SetItemData(i, (DWORD_PTR)(i + 1)));
    }
    CHECK(m_list.GetItemCount() == 3);
    CHECK(m_list.GetItemText(1, 0) == "beta");
    CHECK(m_list.GetItemText(2, 1) == "30");

    LVFINDINFO find;
    memset(&find, 0, sizeof(find));
    find.flags = LVFI_STRING;
    find.psz = "gamma";
    CHECK(m_list.FindItem(&find) == 2);
    find.flags = LVFI_PARTIAL | LVFI_STRING;
    find.psz = "be";
    CHECK(m_list.FindItem(&find) == 1);

    // Sort by the item data, from the largest to the smallest.
    CHECK(m_list.SortItems(CompareDown, 0));
    CHECK(m_list.GetItemText(0, 0) == "gamma");
    CHECK(m_list.GetItemText(2, 0) == "alpha");
    CHECK(m_list.GetItemText(0, 1) == "30");

    // A click on the second item selects it.
    int nChanged = m_nItemChanged;
    CRect rcItem;
    CHECK(m_list.GetItemRect(1, &rcItem, LVIR_LABEL));
    LPARAM lClick = MAKELPARAM(rcItem.left + 4, rcItem.CenterPoint().y);
    m_list.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, lClick);
    m_list.SendMessage(WM_LBUTTONUP, 0, lClick);
    CHECK(m_list.GetSelectedCount() == 1);
    CHECK(m_list.GetNextItem(-1, LVNI_SELECTED) == 1);
    CHECK(m_nItemChanged > nChanged);

    CHECK(m_list.SetItemState(0, LVIS_SELECTED, LVIS_SELECTED));
    CHECK(m_list.GetSelectedCount() == 2);
    CHECK(m_list.DeleteItem(0));
    CHECK(m_list.GetItemCount() == 2);
    CHECK(m_list.GetSelectedCount() == 1);
    CHECK(m_list.DeleteAllItems());
    CHECK(m_list.GetItemCount() == 0);
    CHECK(m_list.DeleteColumn(1) && pHeader->GetItemCount() == 1);
    m_list.UpdateWindow();
  }

  /// Checks a tree view: the links of the items, a sorted insert, the
  /// expansion and its notifications, the selection, the item rectangles,
  /// a click on a button, a sort and the deletion of a branch.
  void TestTreeView() {
    CHECK(m_tree.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS |
                            TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                        CRect(420, 160, 620, 300), this, IDC_TREE));
    HTREEITEM hFruit = m_tree.InsertItem("Fruit");
    HTREEITEM hVeg = m_tree.InsertItem("Vegetables");
    CHECK(hFruit != NULL && hVeg != NULL && hFruit != hVeg);
    HTREEITEM hPear = m_tree.InsertItem("pear", hFruit);
    HTREEITEM hApple = m_tree.InsertItem("apple", hFruit, TVI_FIRST);
    HTREEITEM hFig = m_tree.InsertItem("fig", hFruit, TVI_SORT);  // between apple and pear
    m_tree.InsertItem("leek", hVeg);
    CHECK(m_tree.GetCount() == 6);
    CHECK(m_tree.GetRootItem() == hFruit);
    CHECK(m_tree.GetNextSiblingItem(hFruit) == hVeg);
    CHECK(m_tree.GetChildItem(hFruit) == hApple);
    CHECK(m_tree.GetNextSiblingItem(hApple) == hFig);
    CHECK(m_tree.GetNextSiblingItem(hFig) == hPear);
    CHECK(m_tree.GetParentItem(hPear) == hFruit);
    CHECK(m_tree.GetItemText(hFig) == "fig");
    CHECK(m_tree.ItemHasChildren(hFruit) && !m_tree.ItemHasChildren(hPear));
    CHECK(m_tree.SetItemData(hPear, 3) && m_tree.GetItemData(hPear) == 3);

    // A hidden item has no rectangle until its parent is expanded.
    CRect rc;
    CHECK(!m_tree.GetItemRect(hApple, &rc, TRUE));
    CHECK(m_tree.Expand(hFruit, TVE_EXPAND));
    CHECK(m_nExpanding == 1);
    CHECK(m_tree.GetItemState(hFruit, TVIS_EXPANDED) & TVIS_EXPANDED);
    CHECK(m_tree.GetItemRect(hApple, &rc, TRUE));
    CRect rcFruit;
    CHECK(m_tree.GetItemRect(hFruit, &rcFruit, TRUE));
    CHECK(rc.left > rcFruit.left && rc.top > rcFruit.top);
    CHECK(m_tree.GetNextVisibleItem(hFruit) == hApple);
    // Another TVM_EXPAND does not notify after the first expansion.
    CHECK(m_tree.Expand(hFruit, TVE_COLLAPSE) && m_tree.Expand(hFruit, TVE_EXPAND));
    CHECK(m_nExpanding == 1);

    // The selection.
    CHECK(m_tree.SelectItem(hFig));
    CHECK(m_tree.GetSelectedItem() == hFig);
    CHECK(m_nTreeSelChanged == 1);
    CHECK(m_tree.GetItemState(hFig, TVIS_SELECTED) & TVIS_SELECTED);
    UINT flags = 0;
    CHECK(m_tree.GetItemRect(hFig, &rc, TRUE));
    CHECK(m_tree.HitTest(rc.CenterPoint(), &flags) == hFig && (flags & TVHT_ONITEMLABEL));

    // A click on the button of the second root item expands it.
    CHECK(m_tree.GetItemRect(hVeg, &rc, TRUE));
    CPoint ptButton(rc.left - 3 - (int)m_tree.GetIndent() / 2, rc.CenterPoint().y);
    CHECK(m_tree.HitTest(ptButton, &flags) == hVeg && (flags & TVHT_ONITEMBUTTON));
    m_tree.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ptButton.x, ptButton.y));
    m_tree.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(ptButton.x, ptButton.y));
    CHECK(m_tree.GetItemState(hVeg, TVIS_EXPANDED) & TVIS_EXPANDED);
    CHECK(m_nExpanding == 2);

    // A collapse moves the selection to the collapsed item.
    CHECK(m_tree.Expand(hFruit, TVE_COLLAPSE));
    CHECK(m_tree.GetSelectedItem() == hFruit);

    // A sort by the text, from the largest to the smallest.
    TVSORTCB sort;
    sort.hParent = hFruit;
    sort.lpfnCompare = CompareDown;
    sort.lParam = 0;
    m_tree.SetItemData(hApple, 1);
    m_tree.SetItemData(hFig, 2);
    CHECK(m_tree.SortChildrenCB(&sort));
    CHECK(m_tree.GetChildItem(hFruit) == hPear);
    CHECK(m_tree.SortChildren(hFruit));
    CHECK(m_tree.GetChildItem(hFruit) == hApple);

    // The deletion of a branch notifies for each item.
    int nDeleted = m_nTreeDeleted;
    CHECK(m_tree.DeleteItem(hFruit));
    CHECK(m_nTreeDeleted - nDeleted == 4);
    CHECK(m_tree.GetCount() == 2);
    CHECK(m_tree.GetRootItem() == hVeg);
    CHECK(m_tree.DeleteAllItems());
    CHECK(m_tree.GetCount() == 0 && m_tree.GetRootItem() == NULL);
    m_tree.UpdateWindow();
  }

  /// Checks a tooltip: a window tool and a rectangle tool with a text
  /// callback, the text, a hit test, a relayed mouse move, the show and
  /// the pop, and the deletion of a tool.
  void TestToolTip() {
    CHECK(m_tip.Create(this, TTS_ALWAYSTIP));
    CHECK(m_tip.AddTool(&m_progress, "Progress"));
    CRect rcTool(0, 0, 8, 8);
    CHECK(m_tip.AddTool(this, LPSTR_TEXTCALLBACK, &rcTool, IDT_RECT));
    CHECK(m_tip.GetToolCount() == 2);

    char szText[80];
    TOOLINFO ti;
    m_tip.FillInToolInfo(ti, &m_progress, 0);
    ti.lpszText = szText;
    CHECK(m_tip.SendMessage(TTM_GETTEXT, sizeof(szText), (LPARAM)&ti) == 8);
    CHECK(strcmp(szText, "Progress") == 0);
    m_tip.UpdateTipText("Done", &m_progress);
    ti.lpszText = szText;
    m_tip.SendMessage(TTM_GETTEXT, sizeof(szText), (LPARAM)&ti);
    CHECK(strcmp(szText, "Done") == 0);

    // The callback gives the text of the rectangle tool.
    m_tip.FillInToolInfo(ti, this, IDT_RECT);
    ti.lpszText = szText;
    m_tip.SendMessage(TTM_GETTEXT, sizeof(szText), (LPARAM)&ti);
    CHECK(strcmp(szText, "Corner") == 0 && m_nTipText == 1);

    TTHITTESTINFO hit;
    memset(&hit, 0, sizeof(hit));
    hit.hwnd = m_hWnd;
    hit.pt = CPoint(4, 4);
    hit.ti.cbSize = TTTOOLINFO_V1_SIZE;
    CHECK(m_tip.SendMessage(TTM_HITTEST, 0, (LPARAM)&hit));
    CHECK(hit.ti.uId == IDT_RECT);
    hit.pt = CPoint(20, 20);
    CHECK(!m_tip.SendMessage(TTM_HITTEST, 0, (LPARAM)&hit));

    // A relayed move makes the tool hot; TTM_POPUP shows it now.
    MSG msg;
    memset(&msg, 0, sizeof(msg));
    msg.hwnd = m_hWnd;
    msg.message = WM_MOUSEMOVE;
    msg.lParam = MAKELPARAM(4, 4);
    m_tip.RelayEvent(&msg);
    m_tip.SendMessage(TTM_POPUP, 0, 0);
    CHECK(m_tip.IsWindowVisible());
    CHECK(m_nTipShow == 1);
    m_tip.FillInToolInfo(ti, this, 0);
    CHECK(m_tip.SendMessage(TTM_GETCURRENTTOOL, 0, (LPARAM)&ti) && ti.uId == IDT_RECT);
    m_tip.Pop();
    CHECK(!m_tip.IsWindowVisible());
    CHECK(m_nTipPop == 1);

    m_tip.DelTool(this, IDT_RECT);
    CHECK(m_tip.GetToolCount() == 1);
    CHECK(m_tip.SetMaxTipWidth(100) == -1);
    m_tip.DestroyWindow();
  }

  /// Checks a hot key control: the key and its modifiers, a key press,
  /// and the rules that replace a combination that is not valid.
  void TestHotKey() {
    CHECK(m_hotkey.Create(WS_CHILD | WS_VISIBLE | WS_BORDER, CRect(10, 310, 150, 330), this,
                          IDC_HOTKEY));
    WORD vk = 1, mods = 1;
    m_hotkey.GetHotKey(vk, mods);
    CHECK(vk == 0 && mods == 0);
    m_hotkey.SetHotKey('K', HOTKEYF_CONTROL | HOTKEYF_ALT);
    m_hotkey.GetHotKey(vk, mods);
    CHECK(vk == 'K' && mods == (HOTKEYF_CONTROL | HOTKEYF_ALT));
    CHECK(m_nHotKeyChange == 0);  // HKM_SETHOTKEY does not notify
    // A key with no modifier is not valid: Ctrl replaces none.
    m_hotkey.SetRules(HKCOMB_NONE, HOTKEYF_CONTROL);
    m_hotkey.SendMessage(WM_KEYDOWN, VK_F5, 0);
    m_hotkey.GetHotKey(vk, mods);
    CHECK(vk == VK_F5 && mods == HOTKEYF_CONTROL);
    CHECK(m_nHotKeyChange == 1);
    m_hotkey.SendMessage(WM_KEYDOWN, VK_BACK, 0);
    CHECK(m_hotkey.GetHotKey() == 0);
    m_hotkey.UpdateWindow();
  }

  /// Checks an IP address control: blank, an address, the fields, a range
  /// and IPN_FIELDCHANGED when a field loses the focus.
  void TestIPAddress() {
    CHECK(m_ipaddr.Create(WS_CHILD | WS_VISIBLE | WS_BORDER, CRect(160, 310, 300, 330), this,
                          IDC_IPADDR));
    CHECK(m_ipaddr.IsBlank());
    DWORD dw = 1;
    CHECK(m_ipaddr.GetAddress(dw) == 0 && dw == 0);
    m_ipaddr.SetAddress(192, 168, 1, 20);
    CHECK(!m_ipaddr.IsBlank());
    BYTE b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    CHECK(m_ipaddr.GetAddress(b0, b1, b2, b3) == 4);
    CHECK(b0 == 192 && b1 == 168 && b2 == 1 && b3 == 20);
    // A value above the range of the field goes down to the top of the
    // range when the field loses the focus.
    m_ipaddr.SetFieldRange(3, 1, 100);
    m_ipaddr.SetAddress(10, 0, 0, 200);
    m_ipaddr.SetFieldFocus(3);
    SetFocus();
    CHECK(m_nFieldChanged >= 1);
    CHECK(m_ipaddr.GetAddress(dw) == 4 && FOURTH_IPADDRESS(dw) == 100);
    m_ipaddr.ClearAddress();
    CHECK(m_ipaddr.IsBlank());
    m_ipaddr.UpdateWindow();
  }

  /// Checks a pager: the size of the child from PGN_CALCSIZE, the
  /// buttons and the scroll position.
  void TestPager() {
    CHECK(m_pager.Create(WS_CHILD | WS_VISIBLE | PGS_HORZ, CRect(310, 310, 410, 340), this,
                         IDC_PAGER));
    CWnd wndChild;
    CHECK(wndChild.Create(_T("STATIC"), _T("A wide child window"), WS_CHILD | WS_VISIBLE,
                          CRect(0, 0, 300, 30), &m_pager, IDC_PAGERCHILD));
    m_pager.SetChild(wndChild.m_hWnd);
    CHECK(m_nCalcSize >= 1);
    CHECK(m_pager.GetScrollPos() == 0);
    CHECK(m_pager.IsButtonInvisible(PGB_TOPORLEFT));
    CHECK(m_pager.IsButtonNormal(PGB_BOTTOMORRIGHT));
    m_pager.SetScrollPos(50);
    CHECK(m_pager.GetScrollPos() == 50);
    CHECK(m_pager.IsButtonNormal(PGB_TOPORLEFT));
    CRect rcChild;
    wndChild.GetWindowRect(&rcChild);
    m_pager.ScreenToClient(&rcChild);
    CHECK(rcChild.Width() == 300 && rcChild.left < 0);
    CHECK(m_pager.SetButtonSize(16) == 12 && m_pager.GetButtonSize() == 16);
    m_pager.UpdateWindow();
    wndChild.DestroyWindow();
  }

  /// Checks a link control: the links of the markup, their IDs and URLs,
  /// a hit test, a click and the ideal height.
  void TestLink() {
    CHECK(m_link.Create(_T("See <a href=\"http://a.example/\">one</a> or ")
                            _T("<a id=\"two\">two</a>."),
                        WS_CHILD | WS_VISIBLE, CRect(10, 340, 300, 370), this, IDC_LINK));
    CString str;
    CHECK(m_link.GetItemUrl(0, str) && str == "http://a.example/");
    CHECK(m_link.GetItemID(1, str) && str == "two");
    CHECK(!m_link.GetItemUrl(2, str));
    CHECK(m_link.GetIdealHeight() > 0);
    // Find the first link with hit tests along the first line.
    LHITTESTINFO* pHit = new LHITTESTINFO;
    int xLink = -1;
    for (int x = 0; x < 200 && xLink < 0; x++) {
      memset(pHit, 0, sizeof(*pHit));
      pHit->pt.x = x;
      pHit->pt.y = 4;
      if (m_link.LinkHitTest(pHit) && pHit->item.iLink == 0) xLink = x;
    }
    delete pHit;
    CHECK(xLink > 0);
    m_link.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(xLink + 1, 4));
    m_link.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(xLink + 1, 4));
    CHECK(m_nLinkClick == 1 && m_iLastLink == 0);
    UINT state = 0;
    CHECK(m_link.GetItemState(0, &state, LIS_VISITED) && (state & LIS_VISITED));
    m_link.UpdateWindow();
  }

  /// Checks an extended combo box: items with images, indents and data,
  /// the CB_ messages through the window, and the notifications.
  void TestComboBoxEx() {
    CHECK(m_comboEx.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, CRect(310, 345, 450, 445),
                           this, IDC_COMBOEX));
    CImageList il;
    CHECK(il.Create(16, 16, ILC_COLOR, 2, 0));
    m_comboEx.SetImageList(&il);
    CHECK(m_comboEx.GetImageList() != NULL);
    static const char* const names[] = {"Drive", "Folder", "File"};
    for (int i = 0; i < 3; i++) {
      COMBOBOXEXITEM item;
      memset(&item, 0, sizeof(item));
      item.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE | CBEIF_INDENT | CBEIF_LPARAM;
      item.iItem = i;
      item.pszText = (LPSTR)names[i];
      item.iImage = item.iSelectedImage = i % 2;
      item.iIndent = i;
      item.lParam = 100 + i;
      CHECK(m_comboEx.InsertItem(&item) == i);
    }
    CHECK(m_nCbInserted == 3);
    CHECK(m_comboEx.GetCount() == 3);
    CHECK(m_comboEx.GetItemData(2) == 102);
    char szText[32];
    COMBOBOXEXITEM item;
    memset(&item, 0, sizeof(item));
    item.mask = CBEIF_TEXT | CBEIF_INDENT;
    item.iItem = 1;
    item.pszText = szText;
    item.cchTextMax = sizeof(szText);
    CHECK(m_comboEx.GetItem(&item) && strcmp(szText, "Folder") == 0 && item.iIndent == 1);
    CHECK(m_comboEx.SetCurSel(2) == 2 && m_comboEx.GetCurSel() == 2);
    CHECK(m_comboEx.GetComboBoxCtrl() != NULL);
    CHECK(m_comboEx.GetEditCtrl() == NULL);  // a drop-down list has no edit control
    CHECK(m_comboEx.DeleteItem(0) == 2);
    CHECK(m_nCbDeleted == 1);
    CHECK(m_comboEx.GetItemData(0) == 101);
    m_comboEx.UpdateWindow();
  }

  /// Gives a SYSTEMTIME for a date and a time.
  static SYSTEMTIME MakeTime(int nYear, int nMonth, int nDay, int nHour = 0, int nMinute = 0) {
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)nYear;
    st.wMonth = (WORD)nMonth;
    st.wDay = (WORD)nDay;
    st.wHour = (WORD)nHour;
    st.wMinute = (WORD)nMinute;
    return st;
  }

  /// Checks the month calendar: the selection, today, the range, the
  /// first day of the week, the size, and the hit test of a date and of
  /// the title.
  void TestMonthCal() {
    CHECK(m_cal.Create(WS_CHILD | WS_VISIBLE | MCS_WEEKNUMBERS, CRect(460, 310, 700, 470), this,
                       IDC_MONTHCAL));
    SYSTEMTIME st = MakeTime(1999, 12, 31);
    CHECK(m_cal.SetCurSel(&st));
    SYSTEMTIME stGot;
    CHECK(m_cal.GetCurSel(&stGot) && stGot.wYear == 1999 && stGot.wMonth == 12 && stGot.wDay == 31);
    CHECK(stGot.wDayOfWeek == 5);  // a Friday
    st = MakeTime(2000, 2, 30);
    CHECK(!m_cal.SetCurSel(&st));  // not a date
    st = MakeTime(2000, 1, 1);
    m_cal.SendMessage(MCM_SETTODAY, 0, (LPARAM)&st);
    CHECK(m_cal.SendMessage(MCM_GETTODAY, 0, (LPARAM)&stGot) && stGot.wYear == 2000 &&
          stGot.wDay == 1);
    SYSTEMTIME range[2];
    range[0] = MakeTime(1999, 12, 1);
    range[1] = MakeTime(2000, 1, 31);
    CHECK(m_cal.SendMessage(MCM_SETRANGE, GDTR_MIN | GDTR_MAX, (LPARAM)range));
    st = MakeTime(2000, 2, 1);
    CHECK(!m_cal.SetCurSel(&st));  // out of the range
    CHECK(m_cal.SendMessage(MCM_GETRANGE, 0, (LPARAM)range) == (GDTR_MIN | GDTR_MAX));
    m_cal.SendMessage(MCM_SETFIRSTDAYOFWEEK, 0, 0);
    CHECK(LOWORD(m_cal.SendMessage(MCM_GETFIRSTDAYOFWEEK, 0, 0)) == 0);
    CRect rcMin;
    CHECK(m_cal.GetMinReqRect(&rcMin) && rcMin.Width() > 0 && rcMin.Height() > 0);
    MCHITTESTINFO hit;
    memset(&hit, 0, sizeof(hit));
    hit.cbSize = sizeof(hit);
    hit.pt.x = rcMin.Width() / 2;
    hit.pt.y = 4;
    UINT uHit = (UINT)m_cal.SendMessage(MCM_HITTEST, 0, (LPARAM)&hit);
    CHECK((uHit & MCHT_TITLE) == MCHT_TITLE && uHit == hit.uHit);
    hit.pt.y = rcMin.Height() / 2;
    uHit = (UINT)m_cal.SendMessage(MCM_HITTEST, 0, (LPARAM)&hit);
    CHECK((uHit & MCHT_CALENDAR) == MCHT_CALENDAR);
    m_cal.UpdateWindow();
  }

  /// Checks the date and time picker: the time, its text in a custom
  /// format, the range, DTS_SHOWNONE, and the arrow and digit keys.
  void TestDateTime() {
    CHECK(m_dtp.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHOWNONE, CRect(10, 380, 200, 404),
                       this, IDC_DATETIME));
    SYSTEMTIME st = MakeTime(2001, 3, 9, 14, 5);
    CHECK(m_dtp.SetTime(&st));
    CHECK(m_nDtChange == 0);  // DTM_SETSYSTEMTIME sends no notification
    SYSTEMTIME stGot;
    CHECK(m_dtp.GetTime(&stGot) == GDT_VALID && stGot.wYear == 2001 && stGot.wDay == 9 &&
          stGot.wHour == 14 && stGot.wMinute == 5);
    CHECK(m_dtp.SetFormat("yyyy'-'MM'-'dd HH':'mm"));
    char szText[40];
    m_dtp.GetWindowText(szText, sizeof(szText));
    CHECK(strcmp(szText, "2001-03-09 14:05") == 0);
    m_dtp.SetFocus();
    m_dtp.SendMessage(WM_KEYDOWN, VK_HOME, 0);  // the first field, the year
    m_dtp.SendMessage(WM_KEYDOWN, VK_UP, 0);
    CHECK(m_nDtChange == 1);
    CHECK(m_dtp.GetTime(&stGot) == GDT_VALID && stGot.wYear == 2002);
    m_dtp.SendMessage(WM_KEYDOWN, VK_RIGHT, 0);  // the month
    m_dtp.SendMessage(WM_CHAR, '1', 0);
    m_dtp.SendMessage(WM_CHAR, '2', 0);
    CHECK(m_dtp.GetTime(&stGot) == GDT_VALID && stGot.wMonth == 12);
    SYSTEMTIME range[2];
    range[0] = MakeTime(2002, 1, 1);
    range[1] = MakeTime(2002, 6, 30);
    CHECK(m_dtp.SetRange(GDTR_MIN | GDTR_MAX, range));
    CHECK(m_dtp.GetTime(&stGot) == GDT_VALID && stGot.wMonth == 6 && stGot.wDay == 30);
    CHECK(m_dtp.GetRange(range) == (GDTR_MIN | GDTR_MAX) && range[1].wMonth == 6);
    CHECK(m_dtp.SetTime(NULL));  // DTS_SHOWNONE clears the check box
    CHECK(m_dtp.GetTime(&stGot) == GDT_NONE);
    CHECK(m_dtp.GetWindowTextLength() == 0);
    m_dtp.UpdateWindow();
  }

  /// Keeps the code and the position of the scroll messages of the
  /// slider.
  afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    if ((CWnd*)pScrollBar == &m_slider) {
      m_nLastHScroll = nSBCode;
      m_nLastHScrollPos = (int)nPos;
    }
  }

  /// Counts the scroll messages of the up-down control.
  afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
    (void)nSBCode;
    (void)nPos;
    if ((CWnd*)pScrollBar == &m_spin) m_nVScrolls++;
  }

  /// Counts UDN_DELTAPOS and keeps its iDelta.
  afx_msg void OnDeltaPos(NMHDR* pNMHDR, LRESULT* pResult) {
    m_nDeltaPos++;
    m_nLastDelta = ((NMUPDOWN*)pNMHDR)->iDelta;
    *pResult = 0;
  }

  /// Counts TCN_SELCHANGING, and refuses the change when m_bRefuseTab is
  /// set.
  afx_msg void OnSelChanging(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nSelChanging++;
    *pResult = m_bRefuseTab;
  }

  /// Counts TCN_SELCHANGE.
  afx_msg void OnSelChange(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nSelChange++;
    *pResult = 0;
  }

  /// Counts LVN_ITEMCHANGED.
  afx_msg void OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nItemChanged++;
    *pResult = 0;
  }

  /// Counts TVN_ITEMEXPANDING.
  afx_msg void OnItemExpanding(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nExpanding++;
    *pResult = 0;
  }

  /// Counts TVN_SELCHANGED.
  afx_msg void OnTreeSelChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nTreeSelChanged++;
    *pResult = 0;
  }

  /// Counts TVN_DELETEITEM.
  afx_msg void OnTreeDeleteItem(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nTreeDeleted++;
    *pResult = 0;
  }

  /// Gives the text of the rectangle tool of the tooltip.
  afx_msg void OnTipNeedText(NMHDR* pNMHDR, LRESULT* pResult) {
    NMTTDISPINFO* pInfo = (NMTTDISPINFO*)pNMHDR;
    lstrcpy(pInfo->szText, "Corner");
    pInfo->lpszText = pInfo->szText;
    m_nTipText++;
    *pResult = 0;
  }

  /// Counts TTN_SHOW of the rectangle tool.
  afx_msg void OnTipShow(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nTipShow++;
    *pResult = 0;
  }

  /// Counts TTN_POP of the rectangle tool.
  afx_msg void OnTipPop(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nTipPop++;
    *pResult = 0;
  }

  /// Counts EN_CHANGE of the hot key control.
  afx_msg void OnHotKeyChange() { m_nHotKeyChange++; }

  /// Counts IPN_FIELDCHANGED.
  afx_msg void OnFieldChanged(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nFieldChanged++;
    *pResult = 0;
  }

  /// Gives the pager the width of its child for PGN_CALCSIZE.
  afx_msg void OnCalcSize(NMHDR* pNMHDR, LRESULT* pResult) {
    NMPGCALCSIZE* pCalc = (NMPGCALCSIZE*)pNMHDR;
    if (pCalc->dwFlag & PGF_CALCWIDTH) pCalc->iWidth = 300;
    m_nCalcSize++;
    *pResult = 0;
  }

  /// Keeps the link of NM_CLICK.
  afx_msg void OnLinkClick(NMHDR* pNMHDR, LRESULT* pResult) {
    m_nLinkClick++;
    m_iLastLink = ((NMLINK*)pNMHDR)->item.iLink;
    *pResult = 0;
  }

  /// Counts CBEN_INSERTITEM.
  afx_msg void OnCbInsertItem(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nCbInserted++;
    *pResult = 0;
  }

  /// Counts CBEN_DELETEITEM.
  afx_msg void OnCbDeleteItem(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nCbDeleted++;
    *pResult = 0;
  }

  /// Counts MCN_SELCHANGE.
  afx_msg void OnCalSelChange(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nCalSelChange++;
    *pResult = 0;
  }

  /// Counts DTN_DATETIMECHANGE.
  afx_msg void OnDtChange(NMHDR* pNMHDR, LRESULT* pResult) {
    (void)pNMHDR;
    m_nDtChange++;
    *pResult = 0;
  }

  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_HSCROLL()
  ON_WM_VSCROLL()
  ON_NOTIFY(UDN_DELTAPOS, IDC_SPIN, OnDeltaPos)
  ON_NOTIFY(TCN_SELCHANGING, IDC_TAB, OnSelChanging)
  ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, OnSelChange)
  ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST, OnItemChanged)
  ON_NOTIFY(TVN_ITEMEXPANDING, IDC_TREE, OnItemExpanding)
  ON_NOTIFY(TVN_SELCHANGED, IDC_TREE, OnTreeSelChanged)
  ON_NOTIFY(TVN_DELETEITEM, IDC_TREE, OnTreeDeleteItem)
  ON_NOTIFY(TTN_NEEDTEXT, IDT_RECT, OnTipNeedText)
  ON_NOTIFY(TTN_SHOW, IDT_RECT, OnTipShow)
  ON_NOTIFY(TTN_POP, IDT_RECT, OnTipPop)
  ON_EN_CHANGE(IDC_HOTKEY, OnHotKeyChange)
  ON_NOTIFY(IPN_FIELDCHANGED, IDC_IPADDR, OnFieldChanged)
  ON_NOTIFY(PGN_CALCSIZE, IDC_PAGER, OnCalcSize)
  ON_NOTIFY(NM_CLICK, IDC_LINK, OnLinkClick)
  ON_NOTIFY(CBEN_INSERTITEM, IDC_COMBOEX, OnCbInsertItem)
  ON_NOTIFY(CBEN_DELETEITEM, IDC_COMBOEX, OnCbDeleteItem)
  ON_NOTIFY(MCN_SELCHANGE, IDC_MONTHCAL, OnCalSelChange)
  ON_NOTIFY(DTN_DATETIMECHANGE, IDC_DATETIME, OnDtChange)
END_MESSAGE_MAP()

class CTestApp : public CWinApp {
public:
  /// Makes the main frame, runs the checks and closes the frame.
  virtual BOOL InitInstance() {
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, "MFC Win16 common controls")) {
      printf("FAIL Create\n");
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(m_nCmdShow);
    pFrame->UpdateWindow();
    pFrame->RunTests();
    pFrame->DestroyWindow();
    return TRUE;
  }

  /// Prints the result.
  virtual int ExitInstance() {
    CHECK(g_bDone);
    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILED %d\n", g_failures);
    return CWinApp::ExitInstance();
  }
};

CTestApp theApp;
