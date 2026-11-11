// mditest.cpp - a multiple-document program: an MDI frame with a toolbar
// and status bar, child frames split into a form view and a scroll view,
// several documents and the Window menu commands.
#include <afxwin.h>
#include <afxext.h>
#include "mditest.h"
#include <afxpriv.h>
#include "testutil.h"

class CShapeDoc : public CDocument {
  DECLARE_DYNCREATE(CShapeDoc)
public:
  CString m_strCaption;
  virtual BOOL OnNewDocument() {
    if (!CDocument::OnNewDocument()) return FALSE;
    m_strCaption = _T("Shape");
    return TRUE;
  }
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring())
      ar << m_strCaption;
    else
      ar >> m_strCaption;
  }
};
IMPLEMENT_DYNCREATE(CShapeDoc, CDocument)

// The left pane: a form that edits the caption.
class CFormTestView : public CFormView {
  DECLARE_DYNCREATE(CFormTestView)
public:
  CString m_strCaption;
  CFormTestView() : CFormView(IDD_FORM) {}
  CShapeDoc* GetDocument() const { return (CShapeDoc*)m_pDocument; }
  virtual void DoDataExchange(CDataExchange* pDX) {
    CFormView::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_FORM_EDIT, m_strCaption);
  }
  virtual void OnInitialUpdate() {
    m_strCaption = GetDocument()->m_strCaption;
    CFormView::OnInitialUpdate();
  }
  afx_msg void OnApply() {
    UpdateData(TRUE);
    GetDocument()->m_strCaption = m_strCaption;
    GetDocument()->SetModifiedFlag();
    GetDocument()->UpdateAllViews(this);
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CFormTestView, CFormView)
BEGIN_MESSAGE_MAP(CFormTestView, CFormView)
  ON_BN_CLICKED(IDC_FORM_BUTTON, OnApply)
END_MESSAGE_MAP()

// The right pane: a scrolling drawing of the caption.
class CDrawView : public CScrollView {
  DECLARE_DYNCREATE(CDrawView)
public:
  int m_nUpdates;
  CDrawView() : m_nUpdates(0) {}
  CShapeDoc* GetDocument() const { return (CShapeDoc*)m_pDocument; }
  virtual void OnInitialUpdate() {
    SetScrollSizes(MM_TEXT, CSize(800, 600));
    CScrollView::OnInitialUpdate();
  }
  virtual void OnUpdate(CView*, LPARAM, CObject*) {
    m_nUpdates++;
    Invalidate();
  }
  virtual BOOL OnPreparePrinting(CPrintInfo* pInfo) {
    pInfo->SetMaxPage(2);
    return DoPreparePrinting(pInfo);
  }
  virtual void OnDraw(CDC* pDC) {
    CBrush brush(RGB(0, 128, 192));
    CBrush* pOld = pDC->SelectObject(&brush);
    pDC->Ellipse(20, 20, 180, 140);
    pDC->SelectObject(pOld);
    pDC->SetBkMode(TRANSPARENT);
    pDC->TextOut(40, 150, GetDocument()->m_strCaption);
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CDrawView, CScrollView)
BEGIN_MESSAGE_MAP(CDrawView, CScrollView)
  ON_COMMAND(ID_FILE_PRINT_PREVIEW, CView::OnFilePrintPreview)
END_MESSAGE_MAP()

class CChildFrame : public CMDIChildWnd {
  DECLARE_DYNCREATE(CChildFrame)
public:
  CSplitterWnd m_wndSplitter;
  virtual BOOL OnCreateClient(LPCREATESTRUCT, CCreateContext* pContext) {
    if (!m_wndSplitter.CreateStatic(this, 1, 2)) return FALSE;
    if (!m_wndSplitter.CreateView(0, 0, RUNTIME_CLASS(CFormTestView), CSize(160, 0), pContext) ||
        !m_wndSplitter.CreateView(0, 1, RUNTIME_CLASS(CDrawView), CSize(0, 0), pContext)) {
      TestLog("splitter views failed");
      return FALSE;
    }
    return TRUE;
  }
};
IMPLEMENT_DYNCREATE(CChildFrame, CMDIChildWnd)

static const UINT indicators[] = {ID_SEPARATOR, ID_INDICATOR_CAPS, ID_INDICATOR_NUM};

static int CountChildren(HWND hWndMDIClient) {
  int n = 0;
  for (HWND h = ::GetWindow(hWndMDIClient, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT))
    if (::GetWindow(h, GW_OWNER) == NULL) n++;  // skip icon titles
  return n;
}

class CMainFrame : public CMDIFrameWnd {
  DECLARE_DYNAMIC(CMainFrame)
public:
  CToolBar m_wndToolBar;
  CStatusBar m_wndStatusBar;
  int m_nStep;
  CView* m_pPrintView;
  CMainFrame() : m_nStep(0), m_pPrintView(NULL) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CMDIFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT,
                               WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS |
                                   CBRS_FLYBY) ||
        !m_wndToolBar.LoadToolBar(IDR_MAINFRAME))
      return -1;
    if (!m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(UINT)))
      return -1;
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    EnableDocking(CBRS_ALIGN_ANY);
    DockControlBar(&m_wndToolBar);
    SetTimer(1, 250, NULL);
    return 0;
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CMDIFrameWnd::OnTimer(nIDEvent);
      return;
    }
    switch (m_nStep++) {
      case 0: {
        // The first document is open in a split child frame.
        CMDIChildWnd* pChild = MDIGetActive();
        CHECK(pChild != NULL && pChild->IsKindOf(RUNTIME_CLASS(CChildFrame)));
        if (pChild == NULL) break;
        CSplitterWnd& split = ((CChildFrame*)pChild)->m_wndSplitter;
        CWnd* pForm = split.GetPane(0, 0);
        CWnd* pDraw = split.GetPane(0, 1);
        CHECK(pForm->IsKindOf(RUNTIME_CLASS(CFormTestView)));
        CHECK(pDraw->IsKindOf(RUNTIME_CLASS(CDrawView)));
        CHECK(pForm->GetDlgCtrlID() == split.IdFromRowCol(0, 0));
        CHECK(pDraw->GetDlgCtrlID() == split.IdFromRowCol(0, 1));
        CHECK(pChild->GetActiveView() == pForm);
        CHECK(GetActiveFrame() == pChild);

        CDocument* pDoc = pChild->GetActiveDocument();
        CHECK(pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(CShapeDoc)));
        CHECK(pDoc->GetTitle() == _T("Shape1"));
        CString str;
        pChild->GetWindowText(str);
        CHECK(str == _T("Shape1"));
        GetWindowText(str);
        TestLog("frame title '%s'", (LPCSTR)CStringA(str));
        CHECK(str == _T("MdiTest - Shape1"));
        // The child's menu replaced the frame's, Window menu and all.
        CHECK(GetMenu()->GetMenuItemCount() == 3);
        CHECK(GetControlBar(AFX_IDW_TOOLBAR) == &m_wndToolBar);

        // The form shows the document; Apply updates the other view.
        pForm->GetDlgItemText(IDC_FORM_EDIT, str);
        CHECK(str == _T("Shape"));
        CDrawView* pDrawView = (CDrawView*)pDraw;
        int nUpdates = pDrawView->m_nUpdates;
        pForm->SetDlgItemText(IDC_FORM_EDIT, _T("Circle"));
        pForm->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_FORM_BUTTON, BN_CLICKED),
                           (LPARAM)pForm->GetDlgItem(IDC_FORM_BUTTON)->m_hWnd);
        CHECK(((CShapeDoc*)pDoc)->m_strCaption == _T("Circle"));
        CHECK(pDrawView->m_nUpdates == nUpdates + 1);
        CHECK(pDoc->IsModified());
        pDoc->SetModifiedFlag(FALSE);

        // Scrolling.
        CHECK(pDrawView->GetTotalSize() == CSize(800, 600));
        pDrawView->ScrollToPosition(CPoint(100, 50));
        CPoint pt = pDrawView->GetScrollPosition();
        TestLog("scroll position %d,%d", pt.x, pt.y);
        CHECK(pt == CPoint(100, 50));
        pDrawView->ScrollToPosition(CPoint(0, 0));

        // Moving the splitter bar.
        int cxCur, cxMin;
        split.GetColumnInfo(0, cxCur, cxMin);
        TestLog("column 0: %d (min %d)", cxCur, cxMin);
        CHECK(cxCur == 160);
        split.SetColumnInfo(0, 200, 10);
        split.RecalcLayout();
        split.GetColumnInfo(0, cxCur, cxMin);
        CHECK(cxCur == 200);
        CRect rcForm, rcDraw;
        pForm->GetWindowRect(&rcForm);
        pDraw->GetWindowRect(&rcDraw);
        TestLog("form %d,%d-%d,%d draw %d,%d-%d,%d", rcForm.left, rcForm.top,
                rcForm.right, rcForm.bottom, rcDraw.left, rcDraw.top, rcDraw.right,
                rcDraw.bottom);
        // The column size is the pane's inside; the pane's edge is around it.
        CRect rcInside;
        pForm->GetClientRect(&rcInside);
        CHECK(rcInside.Width() == 200);
        CHECK(rcDraw.left > rcForm.right && rcDraw.top == rcForm.top);

        // A second document.
        AfxGetApp()->OnCmdMsg(ID_FILE_NEW, 0, NULL, NULL);
        break;
      }
      case 1: {
        CHECK(CountChildren(m_hWndMDIClient) == 2);
        CMDIChildWnd* pChild = MDIGetActive();
        CHECK(pChild != NULL && pChild->GetActiveDocument()->GetTitle() == _T("Shape2"));
        CString str;
        GetWindowText(str);
        CHECK(str == _T("MdiTest - Shape2"));
        MDINext();
        CMDIChildWnd* pNext = MDIGetActive();
        CHECK(pNext != NULL && pNext != pChild);
        CHECK(pNext != NULL && pNext->GetActiveDocument()->GetTitle() == _T("Shape1"));
        SendMessage(WM_COMMAND, ID_WINDOW_TILE_VERT);
        break;
      }
      case 2: {
        // Tiled side by side.
        CMDIChildWnd* pChild = MDIGetActive();
        CRect rc1, rc2;
        pChild->GetWindowRect(&rc1);
        CWnd* pOther = NULL;
        for (CWnd* p = CWnd::FromHandle(::GetWindow(m_hWndMDIClient, GW_CHILD)); p != NULL;
             p = p->GetNextWindow())
          if (p != pChild && p->GetWindow(GW_OWNER) == NULL) pOther = p;
        CHECK(pOther != NULL);
        if (pOther != NULL) {
          pOther->GetWindowRect(&rc2);
          CHECK(rc1.top == rc2.top && (rc1.right <= rc2.left || rc2.right <= rc1.left));
        }
        TestSaveWindowBitmap(m_hWnd, _T("mditest.bmp"));

        // Window > New Window opens a second frame on the same document.
        SendMessage(WM_COMMAND, ID_WINDOW_NEW);
        CHECK(CountChildren(m_hWndMDIClient) == 3);
        CMDIChildWnd* pNew = MDIGetActive();
        CHECK(pNew != NULL && pNew != pChild);
        CDocument* pDoc = pNew->GetActiveDocument();
        CHECK(pDoc == pChild->GetActiveDocument());
        int nViews = 0;
        for (POSITION pos = pDoc->GetFirstViewPosition(); pos != NULL; pDoc->GetNextView(pos))
          nViews++;
        CHECK(nViews == 4);
        CString str;
        pNew->GetWindowText(str);
        TestLog("new window title '%s'", (LPCSTR)CStringA(str));
        CHECK(str == _T("Shape1:2"));
        pChild->GetWindowText(str);
        CHECK(str == _T("Shape1:1"));
        break;
      }
      case 3: {
        // Print preview takes over the main window, MDI client and all.
        CChildFrame* pChild = (CChildFrame*)MDIGetActive();
        m_pPrintView = (CView*)pChild->m_wndSplitter.GetPane(0, 1);
        pChild->SetActiveView(m_pPrintView);
        SendMessage(WM_COMMAND, ID_FILE_PRINT_PREVIEW);
        CView* pView = GetActiveView();
        CHECK(pView != NULL && pView->IsKindOf(RUNTIME_CLASS(CPreviewView)));
        if (pView == NULL || !pView->IsKindOf(RUNTIME_CLASS(CPreviewView))) {
          m_nStep = 5;
          break;
        }
        CHECK(pView->GetParent() == this);
        CHECK(!::IsWindowVisible(m_hWndMDIClient));
        CHECK(::GetDlgCtrlID(m_hWndMDIClient) == AFX_IDW_PANE_SAVE);
        CHECK(::GetMenu(m_hWnd) == NULL);
        CHECK(!m_wndToolBar.IsWindowVisible());
        CHECK(m_wndStatusBar.GetPaneText(0) == _T("Page 1"));
        SendMessage(WM_COMMAND, AFX_ID_PREVIEW_NUMPAGE);
        CHECK(m_wndStatusBar.GetPaneText(0) == _T("Pages 1-2"));
        break;
      }
      case 4: {
        TestSaveWindowBitmap(m_hWnd, _T("mditest-preview.bmp"));
        CControlBar* pBar = GetControlBar(AFX_IDW_PREVIEW_BAR);
        CHECK(pBar != NULL && !pBar->GetDlgItem(AFX_ID_PREVIEW_NEXT)->IsWindowEnabled());
        SendMessage(WM_COMMAND, AFX_ID_PREVIEW_CLOSE);
        CHECK(::IsWindowVisible(m_hWndMDIClient));
        CHECK(::GetDlgCtrlID(m_hWndMDIClient) == AFX_IDW_PANE_FIRST);
        CHECK(GetMenu() != NULL && GetMenu()->GetMenuItemCount() == 3);
        CHECK(m_wndToolBar.IsWindowVisible());
        CHECK(GetActiveView() == NULL);
        CHECK(MDIGetActive() != NULL && MDIGetActive()->GetActiveView() == m_pPrintView);
        break;
      }
      case 5: {
        // Closing the frame windows closes the documents.
        MDIGetActive()->SendMessage(WM_CLOSE);
        CHECK(CountChildren(m_hWndMDIClient) == 2);
        CString str;
        CMDIChildWnd* pChild = MDIGetActive();
        if (pChild != NULL) {
          pChild->GetWindowText(str);
          TestLog("after close: '%s'", (LPCSTR)CStringA(str));
        }
        AfxGetApp()->CloseAllDocuments(FALSE);
        CHECK(MDIGetActive() == NULL);
        CHECK(CountChildren(m_hWndMDIClient) == 0);
        CHECK(GetMenu()->GetMenuItemCount() == 2);
        GetWindowText(str);
        TestLog("frame title after closing '%s'", (LPCSTR)CStringA(str));
        CHECK(str == _T("MdiTest"));
        break;
      }
      case 6:
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CMdiApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("mditest.log"));
    AddDocTemplate(new CMultiDocTemplate(IDR_TESTTYPE, RUNTIME_CLASS(CShapeDoc),
                                         RUNTIME_CLASS(CChildFrame),
                                         RUNTIME_CLASS(CFormTestView)));
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->LoadFrame(IDR_MAINFRAME)) {
      TestLog("LoadFrame failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pFrame;
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    pFrame->SetWindowPos(NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMdiApp, CWinApp)
  ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
  ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
END_MESSAGE_MAP()

CMdiApp theApp;
