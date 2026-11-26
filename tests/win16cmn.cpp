// win16cmn.cpp - the common controls that the library draws on 16-bit
// Windows: CImageList, CProgressCtrl, CSliderCtrl, CSpinButtonCtrl and
// CTabCtrl, CHeaderCtrl and CListCtrl. The frame makes each control, sends it messages and checks the
// notifications that the frame gets. It prints ALL PASSED when each
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

enum { IDC_PROGRESS = 100, IDC_SLIDER, IDC_BUDDY, IDC_SPIN, IDC_TAB, IDC_LIST };

class CMainFrame : public CFrameWnd {
public:
  CProgressCtrl m_progress;
  CSliderCtrl m_slider;
  CEdit m_buddy;
  CSpinButtonCtrl m_spin;
  CTabCtrl m_tab;
  CListCtrl m_list;
  UINT m_nLastHScroll;    // the code of the last WM_HSCROLL from the slider
  int m_nLastHScrollPos;  // its position
  int m_nVScrolls;        // the WM_VSCROLL messages from the up-down control
  int m_nDeltaPos;        // the UDN_DELTAPOS notifications
  int m_nLastDelta;       // the iDelta of the last one
  int m_nSelChanging;     // the TCN_SELCHANGING notifications
  int m_nSelChange;       // the TCN_SELCHANGE notifications
  BOOL m_bRefuseTab;      // TCN_SELCHANGING refuses the change
  int m_nItemChanged;     // the LVN_ITEMCHANGED notifications

  /// Starts with no notifications.
  CMainFrame()
      : m_nLastHScroll(0xFFFF), m_nLastHScrollPos(-1), m_nVScrolls(0), m_nDeltaPos(0),
        m_nLastDelta(0), m_nSelChanging(0), m_nSelChange(0), m_bRefuseTab(FALSE),
        m_nItemChanged(0) {}

  /// Makes the controls and runs the checks.
  void RunTests() {
    TestImageList();
    TestProgress();
    TestSlider();
    TestSpin();
    TestTab();
    TestListView();
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

  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_HSCROLL()
  ON_WM_VSCROLL()
  ON_NOTIFY(UDN_DELTAPOS, IDC_SPIN, OnDeltaPos)
  ON_NOTIFY(TCN_SELCHANGING, IDC_TAB, OnSelChanging)
  ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, OnSelChange)
  ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST, OnItemChanged)
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
