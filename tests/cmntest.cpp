// cmntest.cpp - the extra control wrappers: CReBar with a toolbar and a
// combo box in its bands, CCheckListBox, CDragListBox, CLinkCtrl and
// CPagerCtrl.
#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include "cmntest.h"
#include "testutil.h"

static CRect ClientRectOf(CWnd* pWnd, CWnd* pIn) {
  CRect rect;
  pWnd->GetWindowRect(&rect);
  pIn->ScreenToClient(&rect);
  return rect;
}

// A check list box that remembers the data of the items it deletes.
class CTestCheckList : public CCheckListBox {
public:
  CDWordArray m_deleted;
  virtual void DeleteItem(LPDELETEITEMSTRUCT lpDIS) { m_deleted.Add((DWORD)lpDIS->itemData); }
};

// The window in the frame's client area that holds the controls, and
// gets their notifications.
class CPane : public CWnd {
public:
  CTestCheckList m_wndCheck;
  CDragListBox m_wndDrag;
  CLinkCtrl m_wndLink;
  CPagerCtrl m_wndPager;
  CButton m_wndPaged;
  int m_nCheckChanges;
  int m_nCalcSize;
  BOOL m_bHaveLink;

  CPane() : m_nCheckChanges(0), m_nCalcSize(0), m_bHaveLink(FALSE) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CWnd::OnCreate(lpcs) == -1) return -1;
    if (!m_wndCheck.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                           CRect(10, 10, 170, 130), this, IDC_CHECKLIST)) {
      TestLog("check list creation failed");
      return -1;
    }
    m_wndCheck.SetFont(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
    // A list box made a drag list as it is subclassed.
    HWND hDrag = ::CreateWindow(_T("LISTBOX"), NULL,
                                WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY, 180, 10, 120, 120,
                                m_hWnd, (HMENU)IDC_DRAGLIST, AfxGetInstanceHandle(), NULL);
    if (hDrag == NULL || !m_wndDrag.SubclassWindow(hDrag)) {
      TestLog("drag list creation failed");
      return -1;
    }
    m_wndDrag.SetFont(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
    // SysLink is only in comctl32 version 6.
    m_bHaveLink = m_wndLink.Create(
        _T("Visit <a href=\"http://example.com/\" id=\"one\">one</a> or <a>two</a>"),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, CRect(10, 140, 300, 160), this, IDC_LINK);
    if (!m_wndPager.Create(WS_CHILD | WS_VISIBLE | PGS_HORZ, CRect(10, 170, 200, 200), this,
                           IDC_PAGER) ||
        !m_wndPaged.Create(_T("A wide button inside a pager, scrolled sideways"),
                           WS_CHILD | WS_VISIBLE, CRect(0, 0, 400, 30), &m_wndPager, IDC_PAGED)) {
      TestLog("pager creation failed");
      return -1;
    }
    m_wndPaged.SetFont(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
    return 0;
  }

  afx_msg void OnCheckChange() { m_nCheckChanges++; }

  afx_msg void OnCalcSize(NMHDR* pNMHDR, LRESULT* pResult) {
    NMPGCALCSIZE* pCalc = (NMPGCALCSIZE*)pNMHDR;
    m_nCalcSize++;
    if (pCalc->dwFlag == PGF_CALCWIDTH) pCalc->iWidth = 400;
    if (pCalc->dwFlag == PGF_CALCHEIGHT) pCalc->iHeight = 30;
    *pResult = 0;
  }

  afx_msg BOOL OnEraseBkgnd(CDC* pDC) {
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(&rect, ::GetSysColor(COLOR_BTNFACE));
    return TRUE;
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CPane, CWnd)
  ON_WM_CREATE()
  ON_WM_ERASEBKGND()
  ON_CONTROL(CLBN_CHKCHANGE, IDC_CHECKLIST, OnCheckChange)
  ON_NOTIFY(PGN_CALCSIZE, IDC_PAGER, OnCalcSize)
END_MESSAGE_MAP()

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  CReBar m_wndReBar;
  CToolBar m_wndToolBar;
  CComboBox m_wndCombo;
  CEdit m_wndBreak;
  CPane m_wndPane;
  int m_nStep;
  int m_nCutUpdates;

  CMainFrame() : m_nStep(0), m_nCutUpdates(0) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    if (!m_wndReBar.Create(this)) {
      TestLog("rebar creation failed");
      return -1;
    }
    if (!m_wndToolBar.CreateEx(&m_wndReBar, TBSTYLE_FLAT,
                               WS_CHILD | WS_VISIBLE | CBRS_ALIGN_TOP | CBRS_TOOLTIPS) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME)) {
      TestLog("toolbar creation failed");
      return -1;
    }
    if (!m_wndCombo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                           CRect(0, 0, 150, 200), &m_wndReBar, IDC_COMBO)) {
      TestLog("combo creation failed");
      return -1;
    }
    m_wndCombo.SetFont(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
    m_wndCombo.AddString(_T("First"));
    m_wndCombo.SetCurSel(0);
    if (!m_wndReBar.AddBar(&m_wndToolBar) || !m_wndReBar.AddBar(&m_wndCombo, _T("Pick"))) {
      TestLog("AddBar failed");
      return -1;
    }
    if (!m_wndPane.Create(NULL, NULL, WS_CHILD | WS_VISIBLE, CRect(0, 0, 0, 0), this,
                          AFX_IDW_PANE_FIRST)) {
      TestLog("pane creation failed");
      return -1;
    }
    SetTimer(1, 250, NULL);
    return 0;
  }

  afx_msg void OnUpdateCut(CCmdUI* pCmdUI) {
    m_nCutUpdates++;
    pCmdUI->Enable(FALSE);
  }
  afx_msg void OnUpdateCombo(CCmdUI* pCmdUI) { pCmdUI->Enable(FALSE); }
  afx_msg void OnCommandStub() {}

  void StepReBar() {
    CReBarCtrl& rebar = m_wndReBar.GetReBarCtrl();
    CHECK(rebar.GetBandCount() == 2);
    CHECK(m_wndToolBar.GetParent() == &m_wndReBar);
    CHECK(m_wndCombo.GetParent() == &m_wndReBar);
    // The frame lays out the rebar across the top, and the pane below it.
    CRect rBar = ClientRectOf(&m_wndReBar, this);
    CRect rPane = ClientRectOf(&m_wndPane, this);
    CRect rClient;
    GetClientRect(&rClient);
    TestLog("rebar %d,%d-%d,%d pane %d,%d-%d,%d", rBar.left, rBar.top, rBar.right,
            rBar.bottom, rPane.left, rPane.top, rPane.right, rPane.bottom);
    CHECK(rBar.top == 0 && rBar.left == 0 && rBar.right == rClient.right);
    CHECK(rBar.Height() > 0);
    CHECK(rPane.top == rBar.bottom);
    CSize size = m_wndReBar.CalcFixedLayout(TRUE, TRUE);
    CHECK(size.cy == rBar.Height());
    // The bands are found by their windows' IDs.
    int nTool = rebar.IDToIndex(AFX_IDW_TOOLBAR);
    int nCombo = rebar.IDToIndex(IDC_COMBO);
    TestLog("bands %d %d", nTool, nCombo);
    CHECK(nTool == 0 && nCombo == 1);
    CRect rBand;
    CHECK(rebar.GetRect(1, &rBand));
    CHECK(rBand.Width() > 0);
    REBARBANDINFO rbbi;
    memset(&rbbi, 0, sizeof(rbbi));
    rbbi.cbSize = sizeof(rbbi);
    rbbi.fMask = RBBIM_CHILD | RBBIM_TEXT;
    TCHAR szText[32] = _T("");
    rbbi.lpText = szText;
    rbbi.cch = 32;
    CHECK(rebar.GetBandInfo(1, &rbbi));
    CHECK(rbbi.hwndChild == m_wndCombo.m_hWnd);
    CHECK(lstrcmp(szText, _T("Pick")) == 0);
    // The band's controls are the frame's: idle update disabled them.
    CHECK(m_nCutUpdates > 0);
    CHECK((m_wndToolBar.GetToolBarCtrl().GetState(ID_EDIT_CUT) & TBSTATE_ENABLED) == 0);
    CHECK((m_wndToolBar.GetToolBarCtrl().GetState(ID_FILE_NEW) & TBSTATE_ENABLED) != 0);
    CHECK(!m_wndCombo.IsWindowEnabled());
    // A command from the toolbar in a band reaches the frame.
    CHECK(m_wndToolBar.GetOwner() == this);
    COLORREF clrOld = rebar.SetBkColor(RGB(255, 0, 0));
    CHECK(rebar.GetBkColor() == RGB(255, 0, 0));
    rebar.SetBkColor(clrOld);
    TestSaveWindowBitmap(m_hWnd, _T("cmntest.bmp"));

    // A band that starts a row of its own makes the rebar deeper, and
    // the frame moves the pane down.
    m_nRows = rebar.GetRowCount();
    m_cyBar = rBar.Height();
    m_wndBreak.Create(WS_CHILD | WS_VISIBLE | WS_BORDER, CRect(0, 0, 200, 22), &m_wndReBar,
                      IDC_BREAK);
    CHECK(m_wndReBar.AddBar(&m_wndBreak, _T("Second row"), NULL, RBBS_BREAK | RBBS_GRIPPERALWAYS));
  }
  int m_nRows, m_cyBar;

  void StepReBarRows() {
    CReBarCtrl& rebar = m_wndReBar.GetReBarCtrl();
    CHECK(rebar.GetBandCount() == 3);
    CRect rBar = ClientRectOf(&m_wndReBar, this);
    CRect rPane = ClientRectOf(&m_wndPane, this);
    TestLog("rows %d -> %u, height %d -> %d", m_nRows, rebar.GetRowCount(), m_cyBar,
            rBar.Height());
    CHECK((int)rebar.GetRowCount() == m_nRows + 1);
    CHECK(rBar.Height() > m_cyBar);
    CHECK(rPane.top == rBar.bottom);
    // Hiding the band takes its row away again.
    CHECK(rebar.ShowBand(2, FALSE));
    rBar = ClientRectOf(&m_wndReBar, this);
    rPane = ClientRectOf(&m_wndPane, this);
    TestLog("hidden: height %d", rBar.Height());
    CHECK(rBar.Height() == m_cyBar);
    CHECK(rPane.top == rBar.bottom);
    rebar.ShowBand(2, TRUE);
    // Moving the combo's band first.
    CHECK(rebar.MoveBand(1, 0));
    CHECK(rebar.IDToIndex(IDC_COMBO) == 0);
    rebar.MoveBand(0, 1);
    CHECK(rebar.IDToIndex(IDC_COMBO) == 1);
  }

  void StepCheckList() {
    CTestCheckList& list = m_wndPane.m_wndCheck;
    list.AddString(_T("Apples"));
    list.AddString(_T("Pears"));
    list.AddString(_T("Plums"));
    list.AddString(_T("Grapes"));
    CHECK(list.GetCount() == 4);
    // The item data is the caller's, kept beside the check state.
    list.SetItemData(1, 42);
    list.SetCheck(1, 1);
    CHECK(list.GetItemData(1) == 42);
    CHECK(list.GetCheck(1) == 1 && list.GetCheck(0) == 0);
    CHECK(list.GetItemData(0) == 0);
    // A click on an item's box checks it and tells the parent.
    CRect rItem;
    list.GetItemRect(2, &rItem);
    CPoint pt(rItem.left + 5, rItem.CenterPoint().y);
    list.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    list.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
    TestLog("check changes %d", m_wndPane.m_nCheckChanges);
    CHECK(list.GetCheck(2) == 1);
    CHECK(m_wndPane.m_nCheckChanges == 1);
    CHECK(list.GetCurSel() == 2);
    // A click on the text selects it and leaves the box alone.
    list.GetItemRect(3, &rItem);
    pt = CPoint(rItem.left + 60, rItem.CenterPoint().y);
    list.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    list.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
    CHECK(list.GetCheck(3) == 0);
    CHECK(list.GetCurSel() == 3);
    // The space bar toggles the item with the caret.
    list.SendMessage(WM_KEYDOWN, VK_SPACE, 0);
    CHECK(list.GetCheck(3) == 1);
    CHECK(m_wndPane.m_nCheckChanges == 2);
    // A disabled item's box can't be clicked.
    list.Enable(0, FALSE);
    CHECK(!list.IsEnabled(0) && list.IsEnabled(1));
    list.GetItemRect(0, &rItem);
    pt = CPoint(rItem.left + 5, rItem.CenterPoint().y);
    list.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    list.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
    CHECK(list.GetCheck(0) == 0);
    CHECK(m_wndPane.m_nCheckChanges == 2);
    list.Enable(0, TRUE);
    // Three states.
    list.SetCheckStyle(BS_AUTO3STATE);
    CHECK(list.GetCheckStyle() == BS_AUTO3STATE);
    list.SetCheck(0, 2);
    CHECK(list.GetCheck(0) == 2);
    list.SetCurSel(0);
    list.SendMessage(WM_KEYDOWN, VK_SPACE, 0);
    TestLog("after 2: %d", list.GetCheck(0));
    CHECK(list.GetCheck(0) == 0);
    list.SendMessage(WM_KEYDOWN, VK_SPACE, 0);
    CHECK(list.GetCheck(0) == 1);
    // A new item has no check, and the items are tall enough for the box.
    int n = list.InsertString(0, _T("Cherries"));
    CHECK(n == 0 && list.GetCheck(0) == 0 && list.GetCheck(2) == 1);
    CHECK(list.GetItemData(2) == 42);
    CHECK(list.GetItemHeight(0) >= 13);
    // Deleting an item hands DeleteItem the caller's data.
    list.DeleteString(2);
    CHECK(list.m_deleted.GetSize() == 1 && list.m_deleted[0] == 42);
    CHECK(list.GetCount() == 4);
  }

  void StepDragList() {
    CDragListBox& list = m_wndPane.m_wndDrag;
    const TCHAR* items[] = {_T("A"), _T("B"), _T("C"), _T("D")};
    for (int i = 0; i < 4; i++) list.SetItemData(list.AddString(items[i]), 100 + i);
    // A drag from the first item, dropped on the last: it goes before it.
    static const UINT nMsg = ::RegisterWindowMessage(DRAGLISTMSGSTRING);
    CRect r0, r3;
    list.GetItemRect(0, &r0);
    list.GetItemRect(3, &r3);
    CPoint pt0 = r0.CenterPoint(), pt3 = r3.CenterPoint();
    list.ClientToScreen(&pt0);
    list.ClientToScreen(&pt3);
    CHECK(list.ItemFromPt(pt0) == 0 && list.ItemFromPt(pt3, FALSE) == 3);
    list.SetCurSel(0);
    DRAGLISTINFO info;
    info.hWnd = list.m_hWnd;
    info.uNotification = DL_BEGINDRAG;
    info.ptCursor = pt0;
    LRESULT lr = m_wndPane.SendMessage(nMsg, IDC_DRAGLIST, (LPARAM)&info);
    CHECK(lr == TRUE);
    info.uNotification = DL_DRAGGING;
    info.ptCursor = pt3;
    lr = m_wndPane.SendMessage(nMsg, IDC_DRAGLIST, (LPARAM)&info);
    CHECK(lr == DL_MOVECURSOR);
    CHECK(list.m_nLast == 3);
    info.uNotification = DL_DROPPED;
    lr = m_wndPane.SendMessage(nMsg, IDC_DRAGLIST, (LPARAM)&info);
    CString s;
    for (int i = 0; i < list.GetCount(); i++) {
      CString str;
      list.GetText(i, str);
      s += str;
    }
    TestLog("after drop: %s", (LPCSTR)CStringA(s));
    CHECK(s == _T("BCAD"));
    CHECK(list.GetItemData(2) == 100 && list.GetCurSel() == 2);
    CHECK(list.m_nLast == -1);
    // Outside the list: the stop cursor.
    info.uNotification = DL_DRAGGING;
    info.ptCursor = CPoint(-1000, -1000);
    lr = m_wndPane.SendMessage(nMsg, IDC_DRAGLIST, (LPARAM)&info);
    CHECK(lr == DL_STOPCURSOR);
    info.uNotification = DL_CANCELDRAG;
    m_wndPane.SendMessage(nMsg, IDC_DRAGLIST, (LPARAM)&info);
  }

  void StepLink() {
    CLinkCtrl& link = m_wndPane.m_wndLink;
    if (!m_wndPane.m_bHaveLink) {
      TestLog("NOTE: no SysLink class; link checks skipped");
      return;
    }
    CString str;
    CHECK(link.GetItemUrl(0, str));
    TestLog("url 0 %s", (LPCSTR)CStringA(str));
    CHECK(str == _T("http://example.com/"));
    CHECK(link.GetItemID(0, str));
    CHECK(str == _T("one"));
    WCHAR sz[64];
    CHECK(link.GetItemUrl(0, sz, 64) && lstrcmpW(sz, L"http://example.com/") == 0);
    CHECK(link.SetItemUrl(1, L"http://example.com/two"));
    CHECK(link.GetItemUrl(1, str) && str == _T("http://example.com/two"));
    CHECK(link.SetItemID(1, L"two"));
    CHECK(link.GetItemID(1, str) && str == _T("two"));
    CHECK(link.SetItemState(1, LIS_ENABLED | LIS_VISITED, LIS_ENABLED | LIS_VISITED));
    UINT nState = 0;
    CHECK(link.GetItemState(1, &nState, LIS_VISITED));
    CHECK((nState & LIS_VISITED) != 0);
    int cy = link.GetIdealHeight();
    TestLog("ideal height %d", cy);
    CHECK(cy > 0);
  }

  void StepPager() {
    CPagerCtrl& pager = m_wndPane.m_wndPager;
    pager.SetChild(m_wndPane.m_wndPaged.m_hWnd);
    pager.RecalcSize();
    TestLog("calc size %d", m_wndPane.m_nCalcSize);
    CHECK(m_wndPane.m_nCalcSize > 0);
    CHECK(m_wndPane.m_wndPaged.GetParent() == &pager);
    // The child is wider than the pager: the right button shows.
    CHECK(pager.GetScrollPos() == 0);
    CHECK(pager.IsButtonInvisible(PGB_TOPORLEFT));
    CHECK(!pager.IsButtonInvisible(PGB_BOTTOMORRIGHT));
    pager.SetScrollPos(50);
    TestLog("pos %d", pager.GetScrollPos());
    CHECK(pager.GetScrollPos() == 50);
    CHECK(!pager.IsButtonInvisible(PGB_TOPORLEFT));
    pager.SetBorder(2);
    CHECK(pager.GetBorder() == 2);
    pager.SetButtonSize(14);
    CHECK(pager.GetButtonSize() == 14);
    COLORREF clr = pager.SetBkColor(RGB(0, 128, 0));
    CHECK(pager.GetBkColor() == RGB(0, 128, 0));
    pager.SetBkColor(clr);
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    switch (m_nStep++) {
      case 0: StepReBar(); break;
      case 1:
        StepReBarRows();
        StepCheckList();
        StepDragList();
        StepLink();
        StepPager();
        break;
      case 2:
        TestSaveWindowBitmap(m_hWnd, _T("cmntest-controls.bmp"));
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
      default:
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
  ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateCut)
  ON_UPDATE_COMMAND_UI(IDC_COMBO, OnUpdateCombo)
  ON_COMMAND(ID_FILE_NEW, OnCommandStub)
END_MESSAGE_MAP()

class CCmnApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("cmntest.log"));
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->LoadFrame(IDR_MAINFRAME)) {
      TestLog("LoadFrame failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->SetWindowPos(NULL, 0, 0, 640, 480, SWP_NOZORDER);
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CCmnApp theApp;
