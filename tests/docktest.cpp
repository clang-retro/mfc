// docktest.cpp - docking and floating control bars: dock bars on each
// side, bars side by side in a row, floating frames, dragging with the
// mouse, double clicks, resizing a floating bar, and saving and loading
// the layout.
#include <afxwin.h>
#include <afxext.h>
#include "docktest.h"
#include <afxpriv.h>
#include "testutil.h"

static const UINT indicators[] = {ID_SEPARATOR, ID_INDICATOR_CAPS};

static CRect ScreenRect(CWnd* pWnd) {
  CRect rect;
  pWnd->GetWindowRect(&rect);
  return rect;
}

static void LogRect(const char* pszName, const CRect& r) {
  TestLog("%s %ld,%ld-%ld,%ld", pszName, r.left, r.top, r.right, r.bottom);
}

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  CToolBar m_wndToolBar;
  CToolBar m_wndTools;
  CStatusBar m_wndStatusBar;
  CEdit m_wndView;
  int m_nStep;
  int m_nToolA;
  CPoint m_ptFloat;

  CMainFrame() : m_nStep(0), m_nToolA(0) {}

  virtual BOOL OnCreateClient(LPCREATESTRUCT, CCreateContext*) {
    return m_wndView.Create(WS_CHILD | WS_VISIBLE | ES_MULTILINE, CRect(0, 0, 0, 0), this,
                            AFX_IDW_PANE_FIRST);
  }

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;
    DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY |
                    CBRS_SIZE_DYNAMIC;
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, dwStyle) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME) ||
        !m_wndTools.CreateEx(this, TBSTYLE_FLAT, dwStyle, CRect(0, 0, 0, 0), IDW_TOOLS) ||
        !m_wndTools.LoadToolBar(IDR_TOOLS)) {
      TestLog("toolbar creation failed");
      return -1;
    }
    m_wndTools.SetWindowText(_T("Tools"));
    if (!m_wndStatusBar.Create(this) || !m_wndStatusBar.SetIndicators(indicators, 2)) {
      TestLog("status bar creation failed");
      return -1;
    }
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    m_wndTools.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockControlBar(&m_wndToolBar);
    DockControlBarLeftOf(&m_wndTools, &m_wndToolBar);
    SetTimer(1, 250, NULL);
    return 0;
  }

  // The usual way to put two toolbars in one row.
  void DockControlBarLeftOf(CControlBar* pBar, CControlBar* pLeftOf) {
    RecalcLayout(TRUE);
    CRect rect;
    pLeftOf->GetWindowRect(&rect);
    rect.OffsetRect(1, 0);
    DWORD dw = pLeftOf->GetBarStyle();
    UINT n = 0;
    n = (dw & CBRS_ALIGN_TOP) ? AFX_IDW_DOCKBAR_TOP : n;
    n = (dw & CBRS_ALIGN_BOTTOM && n == 0) ? AFX_IDW_DOCKBAR_BOTTOM : n;
    n = (dw & CBRS_ALIGN_LEFT && n == 0) ? AFX_IDW_DOCKBAR_LEFT : n;
    n = (dw & CBRS_ALIGN_RIGHT && n == 0) ? AFX_IDW_DOCKBAR_RIGHT : n;
    DockControlBar(pBar, n, &rect);
  }

  CWnd* DockBar(UINT nID) { return GetDlgItem(nID); }

  // Drags a bar with the mouse from ptFrom to ptTo (screen points): the
  // moves and the release are queued for the drag loop to pick up.
  void Drag(CControlBar* pBar, CPoint ptFrom, CPoint ptTo) {
    CPoint ptMid((ptFrom.x + ptTo.x) / 2, (ptFrom.y + ptTo.y) / 2);
    CPoint pts[2] = {ptMid, ptTo};
    for (int i = 0; i < 2; i++) {
      CPoint pt = pts[i];
      pBar->ScreenToClient(&pt);
      pBar->PostMessage(WM_MOUSEMOVE, 0, MAKELPARAM((WORD)pt.x, (WORD)pt.y));
    }
    CPoint pt = ptTo;
    pBar->ScreenToClient(&pt);
    pBar->PostMessage(WM_LBUTTONUP, 0, MAKELPARAM((WORD)pt.x, (WORD)pt.y));
    ::SetCursorPos(ptFrom.x, ptFrom.y);
    pBar->m_pDockContext->StartDrag(ptFrom);
  }

  CMiniDockFrameWnd* FloatFrame(CControlBar* pBar) {
    CFrameWnd* pFrame = pBar->GetParentFrame();
    if (pFrame == NULL || !pFrame->IsKindOf(RUNTIME_CLASS(CMiniDockFrameWnd))) return NULL;
    return (CMiniDockFrameWnd*)pFrame;
  }

  afx_msg void OnToolA() { m_nToolA++; }
  afx_msg void OnUpdateToolB(CCmdUI* pCmdUI) { pCmdUI->Enable(FALSE); }
  afx_msg void OnTool(UINT) {}

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    CRect rcClient;
    GetClientRect(&rcClient);
    ClientToScreen(&rcClient);
    switch (m_nStep++) {
      case 0: {
        // Both toolbars in one row of the top dock bar.
        CWnd* pTop = DockBar(AFX_IDW_DOCKBAR_TOP);
        CHECK(pTop != NULL && pTop->IsKindOf(RUNTIME_CLASS(CDockBar)));
        CHECK(DockBar(AFX_IDW_DOCKBAR_LEFT) != NULL);
        CHECK(m_wndToolBar.GetParent() == pTop && m_wndTools.GetParent() == pTop);
        CHECK(!m_wndToolBar.IsFloating() && !m_wndTools.IsFloating());
        CRect r1 = ScreenRect(&m_wndToolBar), r2 = ScreenRect(&m_wndTools);
        CRect rView = ScreenRect(&m_wndView), rTop = ScreenRect(pTop);
        LogRect("client", rcClient);
        LogRect("toolbar", r1);
        LogRect("tools", r2);
        LogRect("view", rView);
        CHECK(r1.top == rcClient.top && r2.top == r1.top);
        CHECK(r2.left == r1.right);
        CHECK(rTop.Height() == r1.Height() && rTop.Width() == rcClient.Width());
        CHECK(rView.top == r1.bottom && rView.left == rcClient.left);
        CHECK(((CDockBar*)pTop)->GetDockedCount() == 2);
        TestSaveWindowBitmap(m_hWnd, _T("docktest.bmp"));

        // Down the left side: one button per row.
        DockControlBar(&m_wndTools, AFX_IDW_DOCKBAR_LEFT);
        r2 = ScreenRect(&m_wndTools);
        rView = ScreenRect(&m_wndView);
        LogRect("tools left", r2);
        CHECK(m_wndTools.GetParent() == DockBar(AFX_IDW_DOCKBAR_LEFT));
        CHECK(r2.Height() > r2.Width() && r2.left == rcClient.left);
        CHECK(r2.top == ScreenRect(&m_wndToolBar).bottom);
        CHECK(rView.left == r2.right);
        for (int i = 0; i < m_wndTools.GetCount(); i++) {
          CRect rc;
          m_wndTools.GetItemRect(i, &rc);
          TestLog("btn %d style %08x rect %ld,%ld-%ld,%ld", i, m_wndTools.GetButtonStyle(i), rc.left, rc.top, rc.right, rc.bottom);
        }
        CRect rcA, rcB;
        m_wndTools.GetItemRect(0, &rcA);
        m_wndTools.GetItemRect(1, &rcB);
        CHECK(rcB.top >= rcA.bottom && rcB.left == rcA.left);
        CHECK(((CDockBar*)pTop)->GetDockedCount() == 1);
        break;
      }
      case 1: {
        TestSaveWindowBitmap(m_hWnd, _T("docktest-left.bmp"));
        // Floating.
        m_ptFloat = CPoint(rcClient.left + 300, rcClient.top + 150);
        FloatControlBar(&m_wndTools, m_ptFloat);
        CHECK(m_wndTools.IsFloating());
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndTools);
        CHECK(pFloat != NULL);
        if (pFloat == NULL) {
          m_nStep = 99;
          break;
        }
        CRect rFloat = ScreenRect(pFloat);
        LogRect("float", rFloat);
        CHECK(rFloat.TopLeft() == m_ptFloat);
        CHECK(pFloat->IsWindowVisible() && (pFloat->GetExStyle() & WS_EX_TOOLWINDOW));
        CHECK(::GetWindow(pFloat->m_hWnd, GW_OWNER) == m_hWnd);
        CString strTitle;
        pFloat->GetWindowText(strTitle);
        CHECK(strTitle == _T("Tools"));
        // The frame fits the bar, which is in one row.
        CRect rcFloatClient, r2 = ScreenRect(&m_wndTools);
        pFloat->GetClientRect(&rcFloatClient);
        CHECK(rcFloatClient.Size() == r2.Size());
        CHECK(r2.Width() > r2.Height());
        CHECK(m_wndTools.GetBarStyle() & CBRS_FLOATING);
        // The left dock bar is empty again.
        CHECK(ScreenRect(&m_wndView).left == rcClient.left);
        // A click on a floating button reaches the frame.
        CRect rcA;
        m_wndTools.GetItemRect(0, &rcA);
        CPoint pt = rcA.CenterPoint();
        m_wndTools.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
        m_wndTools.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
        CHECK(m_nToolA == 1);
        break;
      }
      case 2: {
        // Idle-time updates reach the floating bar.
        int nB = m_wndTools.CommandToIndex(ID_TOOL_B);
        CHECK((m_wndTools.GetButtonStyle(nB) & TBBS_DISABLED) != 0);
        CHECK((m_wndTools.GetButtonStyle(0) & TBBS_DISABLED) == 0);
        TestSaveWindowBitmap(::GetDesktopWindow(), _T("docktest-float.bmp"));

        // Resizing narrower wraps the buttons into rows.
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndTools);
        CRect rFloat = ScreenRect(pFloat);
        CRect rSizing = rFloat;
        rSizing.right = rSizing.left + rFloat.Width() / 2;
        pFloat->SendMessage(WM_SIZING, WMSZ_RIGHT, (LPARAM)&rSizing);
        LogRect("sizing", rSizing);
        CHECK(rSizing.Width() < rFloat.Width() && rSizing.Height() > rFloat.Height());
        pFloat->SetWindowPos(NULL, 0, 0, rSizing.Width(), rSizing.Height(),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        CRect r2 = ScreenRect(&m_wndTools);
        LogRect("tools wrapped", r2);
        CHECK(r2.Height() > r2.Width() / 3);
        CRect rcA, rcE;
        m_wndTools.GetItemRect(0, &rcA);
        m_wndTools.GetItemRect(m_wndTools.CommandToIndex(ID_TOOL_E), &rcE);
        CHECK(rcE.top > rcA.top);
        CHECK(m_wndTools.m_nMRUWidth < 32767);
        break;
      }
      case 3: {
        TestSaveWindowBitmap(::GetDesktopWindow(), _T("docktest-wrapped.bmp"));
        // Hiding the bar hides its frame; the close button hides the bar.
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndTools);
        ShowControlBar(&m_wndTools, FALSE, FALSE);
        CHECK(!pFloat->IsWindowVisible());
        ShowControlBar(&m_wndTools, TRUE, FALSE);
        CHECK(pFloat->IsWindowVisible() && m_wndTools.IsWindowVisible());
        pFloat->SendMessage(WM_CLOSE);
        CHECK(!m_wndTools.IsWindowVisible() && ::IsWindow(pFloat->m_hWnd));
        ShowControlBar(&m_wndTools, TRUE, FALSE);
        CHECK(pFloat->IsWindowVisible());

        // Double-clicking the caption docks it where it was (on the left).
        HWND hFloat = pFloat->m_hWnd;
        CRect rCap = ScreenRect(pFloat);
        pFloat->SendMessage(WM_NCLBUTTONDBLCLK, HTCAPTION,
                            MAKELPARAM(rCap.left + 10, rCap.top + 5));
        CHECK(!m_wndTools.IsFloating());
        CHECK(m_wndTools.GetParent() == DockBar(AFX_IDW_DOCKBAR_LEFT));
        CHECK(!::IsWindowVisible(hFloat));
        break;
      }
      case 4: {
        // The empty floating frame is gone.
        CHECK(GetControlBar(AFX_IDW_DOCKBAR_FLOAT) == NULL);
        // Dragging the main toolbar by its gripper into the middle floats it.
        CRect r1 = ScreenRect(&m_wndToolBar);
        CPoint ptFrom(r1.left + 3, r1.top + r1.Height() / 2);
        CPoint ptTo(rcClient.left + 250, rcClient.top + 200);
        Drag(&m_wndToolBar, ptFrom, ptTo);
        CHECK(m_wndToolBar.IsFloating());
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndToolBar);
        CHECK(pFloat != NULL);
        if (pFloat != NULL) {
          CRect rFloat = ScreenRect(pFloat);
          LogRect("dragged float", rFloat);
          CHECK(rFloat.PtInRect(ptTo));
        }
        // The top dock bar is empty, so the view moves up.
        CHECK(ScreenRect(&m_wndView).top == rcClient.top);
        break;
      }
      case 5: {
        // Dragging the floating bar's frame to the bottom edge docks it there.
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndToolBar);
        CRect rFloat = ScreenRect(pFloat);
        CRect rBottom = ScreenRect(DockBar(AFX_IDW_DOCKBAR_BOTTOM));
        LogRect("bottom dock", rBottom);
        CPoint ptFrom(rFloat.left + 10, rFloat.top + 5);
        CPoint ptTo(rcClient.left + 100, rBottom.top);
        Drag(&m_wndToolBar, ptFrom, ptTo);
        CHECK(!m_wndToolBar.IsFloating());
        CHECK(m_wndToolBar.GetParent() == DockBar(AFX_IDW_DOCKBAR_BOTTOM));
        CRect r1 = ScreenRect(&m_wndToolBar), rStatus = ScreenRect(&m_wndStatusBar);
        LogRect("toolbar bottom", r1);
        CHECK(r1.bottom == rStatus.top && r1.Width() > r1.Height());
        CHECK(ScreenRect(&m_wndView).bottom == r1.top);

        // A drag with nowhere to go (the pointer does not move) changes nothing.
        CPoint ptGrip(r1.left + 3, r1.top + 4);
        Drag(&m_wndToolBar, ptGrip, ptGrip);
        CHECK(ScreenRect(&m_wndToolBar) == r1);
        break;
      }
      case 6: {
        TestSaveWindowBitmap(m_hWnd, _T("docktest-bottom.bmp"));
        // Double-clicking the gripper floats a docked bar.
        CRect r1 = ScreenRect(&m_wndToolBar);
        CPoint pt(r1.left + 3, r1.top + 4);
        m_wndToolBar.ScreenToClient(&pt);
        m_wndToolBar.SendMessage(WM_LBUTTONDBLCLK, 0, MAKELPARAM((WORD)pt.x, (WORD)pt.y));
        CHECK(m_wndToolBar.IsFloating());
        // ...and again docks it back at the bottom.
        m_wndToolBar.m_pDockContext->ToggleDocking();
        CHECK(m_wndToolBar.GetParent() == DockBar(AFX_IDW_DOCKBAR_BOTTOM));

        // Save the layout: toolbar at the bottom, tools floating.
        FloatControlBar(&m_wndTools, m_ptFloat);
        SaveBarState(_T("DockTest"));
        break;
      }
      case 7: {
        // Change everything, then load the saved layout back.
        DockControlBar(&m_wndTools, AFX_IDW_DOCKBAR_RIGHT);
        DockControlBar(&m_wndToolBar, AFX_IDW_DOCKBAR_TOP);
        ShowControlBar(&m_wndStatusBar, FALSE, FALSE);
        CHECK(!m_wndTools.IsFloating());
        LoadBarState(_T("DockTest"));
        CHECK(m_wndToolBar.GetParent() == DockBar(AFX_IDW_DOCKBAR_BOTTOM));
        CHECK(m_wndTools.IsFloating());
        CHECK(m_wndStatusBar.IsWindowVisible());
        CMiniDockFrameWnd* pFloat = FloatFrame(&m_wndTools);
        if (pFloat != NULL) {
          CRect rFloat = ScreenRect(pFloat);
          LogRect("loaded float", rFloat);
          CHECK(rFloat.TopLeft() == m_ptFloat && pFloat->IsWindowVisible());
        }
        CDockState state;
        GetDockState(state);
        CHECK(state.m_arrBarInfo.GetSize() >= 6);
        break;
      }
      case 8: {
        // The right dock bar the tools left is empty, and only one
        // floating frame is around.
        CHECK(((CDockBar*)DockBar(AFX_IDW_DOCKBAR_RIGHT))->GetDockedCount() == 0);
        int nFloat = 0;
        POSITION pos = m_listControlBars.GetHeadPosition();
        while (pos != NULL) {
          CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
          if (pBar->IsDockBar() && ((CDockBar*)pBar)->m_bFloating) nFloat++;
        }
        CHECK(nFloat == 1);
        TestSaveWindowBitmap(::GetDesktopWindow(), _T("docktest-loaded.bmp"));
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
  ON_WM_CREATE()
  ON_WM_TIMER()
  ON_COMMAND(ID_TOOL_A, OnToolA)
  ON_UPDATE_COMMAND_UI(ID_TOOL_B, OnUpdateToolB)
  ON_COMMAND_RANGE(ID_TOOL_C, ID_TOOL_F, OnTool)
END_MESSAGE_MAP()

class CDockApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("docktest.log"));
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

CDockApp theApp;
