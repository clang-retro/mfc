// splittest.cpp - a dynamic splitter: the split boxes, splitting by
// dragging a box and from the keyboard (Window > Split), the shared
// scroll bars that keep panes in a row or column together, the next
// pane command, and unsplitting by dragging a bar to the edge.
#include <afxwin.h>
#include <afxext.h>
#include "splittest.h"
#include "testutil.h"

// A big grid of labelled cells, so a screenshot shows which part of it
// each pane is looking at.
class CGridView : public CScrollView {
  DECLARE_DYNCREATE(CGridView)
public:
  virtual void OnInitialUpdate() {
    SetScrollSizes(MM_TEXT, CSize(2000, 1500), CSize(200, 150), CSize(20, 15));
  }
  virtual void OnDraw(CDC* pDC) {
    CRect rectClip;
    pDC->GetClipBox(&rectClip);
    pDC->SetBkMode(TRANSPARENT);
    for (int y = 0; y < 1500; y += 50) {
      for (int x = 0; x < 2000; x += 100) {
        CRect rect(x, y, x + 100, y + 50);
        if (!CRect().IntersectRect(&rect, &rectClip)) continue;
        int n = (x / 100 + y / 50) % 2;
        pDC->FillSolidRect(&rect, n ? RGB(224, 232, 255) : RGB(255, 255, 255));
        CString str;
        str.Format(_T("%d,%d"), x / 100, y / 50);
        pDC->TextOut(x + 4, y + 4, str);
      }
    }
  }
};
IMPLEMENT_DYNCREATE(CGridView, CScrollView)

static CRect ClientRectOf(CWnd* pWnd, CWnd* pIn) {
  CRect rect;
  pWnd->GetWindowRect(&rect);
  pIn->ScreenToClient(&rect);
  return rect;
}

static void LogRect(const char* pszName, const CRect& r) {
  TestLog("%s %ld,%ld-%ld,%ld", pszName, r.left, r.top, r.right, r.bottom);
}

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  CSplitterWnd m_wndSplitter;
  int m_nStep;

  CMainFrame() : m_nStep(0) {}

  virtual BOOL OnCreateClient(LPCREATESTRUCT, CCreateContext*) {
    CCreateContext context;
    context.m_pNewViewClass = RUNTIME_CLASS(CGridView);
    context.m_pCurrentFrame = this;
    if (!m_wndSplitter.Create(this, 2, 2, CSize(20, 20), &context)) {
      TestLog("splitter creation failed");
      return FALSE;
    }
    m_wndSplitter.GetPane(0, 0)->SendMessage(WM_INITIALUPDATE);
    SetActiveView((CView*)m_wndSplitter.GetPane(0, 0));
    SetTimer(1, 250, NULL);
    return TRUE;
  }

  CGridView* Pane(int row, int col) { return (CGridView*)m_wndSplitter.GetPane(row, col); }
  CWnd* Bar(UINT nID) { return m_wndSplitter.GetDlgItem(nID); }

  // A press, a move and a release on the splitter, in its client
  // coordinates.
  void Drag(CPoint ptFrom, CPoint ptTo) {
    m_wndSplitter.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ptFrom.x, ptFrom.y));
    m_wndSplitter.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(ptTo.x, ptTo.y));
    m_wndSplitter.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(ptTo.x, ptTo.y));
  }

  CPoint BoxCenter(int ht) {
    CRect rect;
    m_wndSplitter.GetHitRect(ht, rect);
    return rect.CenterPoint();
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    CSplitterWnd& split = m_wndSplitter;
    CRect rectInside;
    split.GetInsideRect(rectInside);
    switch (m_nStep++) {
      case 0: {
        // One pane, the splitter's scroll bars, and both split boxes.
        CHECK(split.GetRowCount() == 1 && split.GetColumnCount() == 1);
        CGridView* pView = Pane(0, 0);
        CHECK((pView->GetStyle() & (WS_HSCROLL | WS_VSCROLL)) == 0);
        CWnd* pHBar = Bar(AFX_IDW_HSCROLL_FIRST);
        CWnd* pVBar = Bar(AFX_IDW_VSCROLL_FIRST);
        CHECK(pHBar != NULL && pVBar != NULL);
        if (pHBar == NULL || pVBar == NULL) break;
        CHECK(pView->GetScrollBarCtrl(SB_HORZ)->m_hWnd == pHBar->m_hWnd);
        CHECK(pView->GetScrollBarCtrl(SB_VERT)->m_hWnd == pVBar->m_hWnd);
        CHECK(pHBar->IsWindowEnabled() && pVBar->IsWindowEnabled());
        CRect rView = ClientRectOf(pView, &split);
        CRect rH = ClientRectOf(pHBar, &split), rV = ClientRectOf(pVBar, &split);
        LogRect("inside", rectInside);
        LogRect("view", rView);
        LogRect("hbar", rH);
        LogRect("vbar", rV);
        CHECK(rView == rectInside);
        CHECK(rV.left == rView.right && rH.top == rView.bottom);
        // The boxes sit at the start of each bar.
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::vSplitterBox)) == CSplitterWnd::vSplitterBox);
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::hSplitterBox)) == CSplitterWnd::hSplitterBox);
        CHECK(rV.top == rView.top + split.m_cySplitter);
        CHECK(rH.left == rView.left + split.m_cxSplitter);
        TestSaveWindowBitmap(m_hWnd, _T("splittest.bmp"));

        // Dragging the box on the vertical bar down splits the rows.
        Drag(BoxCenter(CSplitterWnd::vSplitterBox), CPoint(rectInside.CenterPoint().x, 150));
        CHECK(split.GetRowCount() == 2 && split.GetColumnCount() == 1);
        break;
      }
      case 1: {
        CHECK(split.GetRowCount() == 2);
        if (split.GetRowCount() != 2) break;
        int cyCur, cyMin;
        split.GetRowInfo(0, cyCur, cyMin);
        TestLog("row 0 %d", cyCur);
        CHECK(cyCur >= 140 && cyCur <= 150);
        CHECK(Bar(AFX_IDW_VSCROLL_FIRST + 1) != NULL);
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::vSplitterBox)) != CSplitterWnd::vSplitterBox);
        CHECK(Pane(1, 0) != NULL && Pane(1, 0)->IsKindOf(RUNTIME_CLASS(CGridView)));

        // The horizontal bar is shared: both rows scroll sideways.
        CWnd* pHBar = Bar(AFX_IDW_HSCROLL_FIRST);
        split.SendMessage(WM_HSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), (LPARAM)pHBar->m_hWnd);
        split.SendMessage(WM_HSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), (LPARAM)pHBar->m_hWnd);
        CPoint pt0 = Pane(0, 0)->GetDeviceScrollPosition();
        CPoint pt1 = Pane(1, 0)->GetDeviceScrollPosition();
        TestLog("after hscroll %ld,%ld and %ld,%ld", pt0.x, pt0.y, pt1.x, pt1.y);
        CHECK(pt0.x == 220 && pt1.x == 220);
        // Each row has its own vertical bar.
        CWnd* pVBar1 = Bar(AFX_IDW_VSCROLL_FIRST + 1);
        split.SendMessage(WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), (LPARAM)pVBar1->m_hWnd);
        pt0 = Pane(0, 0)->GetDeviceScrollPosition();
        pt1 = Pane(1, 0)->GetDeviceScrollPosition();
        CHECK(pt0.y == 0 && pt1.y == 150);
        break;
      }
      case 2: {
        // Window > Split from the keyboard: the new split starts in the
        // middle, the right arrow moves it, Enter accepts.
        SetActiveView(Pane(0, 0));
        SendMessage(WM_COMMAND, ID_WINDOW_SPLIT);
        CHECK(split.IsTracking());
        split.SendMessage(WM_KEYDOWN, VK_RIGHT);
        split.SendMessage(WM_KEYDOWN, VK_RETURN);
        CHECK(!split.IsTracking());
        CHECK(split.GetRowCount() == 2 && split.GetColumnCount() == 2);
        if (split.GetColumnCount() != 2) break;
        int cxCur, cxMin;
        split.GetColumnInfo(0, cxCur, cxMin);
        TestLog("col 0 %d of %d", cxCur, rectInside.Width());
        CHECK(abs(cxCur - (rectInside.Width() / 2 + 8)) <= 6);
        CHECK(Bar(AFX_IDW_HSCROLL_FIRST + 1) != NULL);
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::hSplitterBox)) != CSplitterWnd::hSplitterBox);
        // The new panes follow their rows' vertical bars.
        CHECK(Pane(1, 1)->GetDeviceScrollPosition().y == 150);
        CHECK(Pane(0, 1)->GetDeviceScrollPosition().y == 0);
        CHECK(Pane(0, 1)->GetDeviceScrollPosition().x == 0);

        // The wheel in one pane scrolls the pane beside it too.
        Pane(1, 1)->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        CPoint pt10 = Pane(1, 0)->GetDeviceScrollPosition();
        CPoint pt11 = Pane(1, 1)->GetDeviceScrollPosition();
        TestLog("after wheel %ld and %ld", pt10.y, pt11.y);
        CHECK(pt10.y > 150 && pt10.y == pt11.y);
        CHECK(Pane(0, 0)->GetDeviceScrollPosition().y == 0);
        TestSaveWindowBitmap(m_hWnd, _T("splittest-split.bmp"));
        break;
      }
      case 3: {
        // Next and previous pane.
        SetActiveView(Pane(0, 0));
        SendMessage(WM_COMMAND, ID_NEXT_PANE);
        CHECK(GetActiveView() == Pane(0, 1));
        SendMessage(WM_COMMAND, ID_PREV_PANE);
        SendMessage(WM_COMMAND, ID_PREV_PANE);
        CHECK(GetActiveView() == Pane(1, 1));
        // Window > Split stays on: it moves the bars that are there.
        BOOL bEnabled = FALSE;
        struct CTestCmdUI : public CCmdUI {
          BOOL* m_pb;
          virtual void Enable(BOOL bOn) { *m_pb = bOn; }
        } ui;
        ui.m_pb = &bEnabled;
        ui.m_nID = ID_WINDOW_SPLIT;
        ui.DoUpdate(this, FALSE);
        CHECK(bEnabled);
        break;
      }
      case 4: {
        // Dragging the row bar to the bottom removes the second row;
        // dragging the column bar to the left removes the first column.
        CRect rBar;
        split.GetHitRect(CSplitterWnd::vSplitterBar1, rBar);
        CPoint ptBar(rectInside.left + 30, rBar.CenterPoint().y);
        Drag(ptBar, CPoint(ptBar.x, rectInside.bottom));
        CHECK(split.GetRowCount() == 1);
        split.GetHitRect(CSplitterWnd::hSplitterBar1, rBar);
        ptBar = CPoint(rBar.CenterPoint().x, rectInside.top + 30);
        Drag(ptBar, CPoint(rectInside.left, ptBar.y));
        CHECK(split.GetColumnCount() == 1);
        if (split.GetRowCount() != 1 || split.GetColumnCount() != 1) break;
        // One pane again, renumbered, with one pair of scroll bars and
        // both boxes back.
        CHECK(Pane(0, 0) != NULL && Pane(0, 0)->GetDlgCtrlID() == AFX_IDW_PANE_FIRST);
        CHECK(Bar(AFX_IDW_HSCROLL_FIRST) != NULL && Bar(AFX_IDW_HSCROLL_FIRST + 1) == NULL);
        CHECK(Bar(AFX_IDW_VSCROLL_FIRST) != NULL && Bar(AFX_IDW_VSCROLL_FIRST + 1) == NULL);
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::vSplitterBox)) == CSplitterWnd::vSplitterBox);
        CHECK(split.HitTest(BoxCenter(CSplitterWnd::hSplitterBox)) == CSplitterWnd::hSplitterBox);
        // The pane left is the one from column 1, row 0: it keeps the
        // horizontal position of its column (0) and the row's vertical (0).
        CPoint pt = Pane(0, 0)->GetDeviceScrollPosition();
        TestLog("left with %ld,%ld", pt.x, pt.y);
        CHECK(pt.x == 0 && pt.y == 0);
        CHECK(ClientRectOf(Pane(0, 0), &split) == rectInside);
        CHECK(GetActiveView() == Pane(0, 0));
        TestSaveWindowBitmap(m_hWnd, _T("splittest-unsplit.bmp"));
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
      }
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
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CSplitApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("splittest.log"));
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

CSplitApp theApp;
