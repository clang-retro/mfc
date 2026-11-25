// win16doc.cpp - document/view on 16-bit Windows: a CSingleDocTemplate
// with a CDocument, a CView and a CFrameWnd from resources, File New,
// the view's OnDraw and OnUpdate, and the command routing from the frame
// to the view and the document. With -DTEST_MDI, a CMultiDocTemplate
// with a CMDIFrameWnd and two CMDIChildWnd windows, and the MDI menus.
// It prints ALL PASSED when each step worked.
#include <afxwin.h>
#include <stdio.h>
#include "win16wnd.h"

static int g_failures = 0;
static BOOL g_bDone = FALSE;  // the last step ran
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

#define ID_TEST_DOC 210
#define ID_TEST_VIEW 211

class CTestDoc : public CDocument {
  DECLARE_DYNCREATE(CTestDoc)
public:
  int m_nNew;
  BOOL m_bDocCommand;
  CString m_strText;
  CTestDoc() : m_nNew(0), m_bDocCommand(FALSE) {}

  /// Counts the new documents and sets the text.
  virtual BOOL OnNewDocument() {
    if (!CDocument::OnNewDocument()) return FALSE;
    m_nNew++;
    m_strText = "document text";
    return TRUE;
  }

protected:
  /// A command that the document handles.
  afx_msg void OnTestDoc() { m_bDocCommand = TRUE; }
  DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNCREATE(CTestDoc, CDocument)
BEGIN_MESSAGE_MAP(CTestDoc, CDocument)
  ON_COMMAND(ID_TEST_DOC, OnTestDoc)
END_MESSAGE_MAP()

class CTestView : public CView {
  DECLARE_DYNCREATE(CTestView)
public:
  int m_nDraws;
  int m_nUpdates;
  BOOL m_bViewCommand;
  CTestView() : m_nDraws(0), m_nUpdates(0), m_bViewCommand(FALSE) {}

  /// Draws the text of the document.
  virtual void OnDraw(CDC* pDC) {
    CTestDoc* pDoc = (CTestDoc*)GetDocument();
    pDC->TextOut(10, 10, pDoc->m_strText);
    m_nDraws++;
  }

  /// Counts the updates, then redraws.
  virtual void OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) {
    m_nUpdates++;
    CView::OnUpdate(pSender, lHint, pHint);
  }

protected:
  /// A command that the view handles.
  afx_msg void OnTestView() { m_bViewCommand = TRUE; }
  DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNCREATE(CTestView, CView)
BEGIN_MESSAGE_MAP(CTestView, CView)
  ON_COMMAND(ID_TEST_VIEW, OnTestView)
END_MESSAGE_MAP()

#ifdef TEST_MDI
class CTestChild : public CMDIChildWnd {
  DECLARE_DYNCREATE(CTestChild)
protected:
  /// Gives the child a position and a size. Wine puts a 16-bit MDI child
  /// with CW_USEDEFAULT outside the client area, so it never paints.
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs) {
    cs.x = cs.y = 10;
    cs.cx = 200;
    cs.cy = 120;
    return CMDIChildWnd::PreCreateWindow(cs);
  }
};
IMPLEMENT_DYNCREATE(CTestChild, CMDIChildWnd)
#define CTestFrameBase CMDIFrameWnd
#else
#define CTestFrameBase CFrameWnd
#endif

class CTestFrame : public CTestFrameBase {
  DECLARE_DYNCREATE(CTestFrame)
public:
  int m_nStep;
  CTestFrame() : m_nStep(0) {}

protected:
  /// Starts the timer that runs the steps.
  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CTestFrameBase::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 50, NULL);
    return 0;
  }

  /// Does one step of the test each time.
  afx_msg void OnTimer(UINT_PTR) {
    switch (m_nStep++) {
      case 0:
#ifdef TEST_MDI
        // A second document, in a second child window.
        AfxGetApp()->OnFileNew();
        break;
      case 1:
#endif
        // Commands from the menu go to the view and to the document.
        PostMessage(WM_COMMAND, ID_TEST_VIEW, 0);
        PostMessage(WM_COMMAND, ID_TEST_DOC, 0);
        break;
#ifdef TEST_MDI
      case 2: {
#else
      case 1: {
#endif
        KillTimer(1);
#ifdef TEST_MDI
        BOOL bMaximized = TRUE;
        CMDIChildWnd* pChild = MDIGetActive(&bMaximized);
        CHECK(pChild != NULL && pChild->IsKindOf(RUNTIME_CLASS(CTestChild)));
        CHECK(!bMaximized);
        if (pChild == NULL) break;
        CTestView* pView = (CTestView*)pChild->GetActiveView();
        // The menu of the document type, with its Window menu.
        CHECK(GetMenu() != NULL && GetMenu()->GetMenuItemCount() == 2);
        // Activate the other child: the frame gets the other document.
        MDINext();
        CMDIChildWnd* pOther = MDIGetActive();
        CHECK(pOther != NULL && pOther != pChild);
        if (pOther != NULL)
          CHECK(pOther->GetActiveDocument() != pChild->GetActiveDocument());
        CHECK(GetActiveFrame() == pOther);
        MDIActivate(pChild);
        CHECK(MDIGetActive() == pChild);
#else
        CTestView* pView = (CTestView*)GetActiveView();
#endif
        CHECK(pView != NULL && pView->IsKindOf(RUNTIME_CLASS(CTestView)));
        if (pView == NULL) break;
        CTestDoc* pDoc = (CTestDoc*)pView->GetDocument();
        CHECK(pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(CTestDoc)));
        CHECK(pDoc->m_nNew == 1);
#ifdef TEST_MDI
        CHECK(pDoc->GetTitle() == "TestDoc2");
#else
        CHECK(pDoc->GetTitle() == "Untitled");
        CHECK(GetMenu() != NULL && GetMenu()->GetMenuItemCount() == 1);
#endif
        CHECK(pView->m_bViewCommand);
        CHECK(pDoc->m_bDocCommand);
        pView->UpdateWindow();
        CHECK(pView->m_nDraws > 0);
        CHECK(pView->m_nUpdates > 0);
        g_bDone = TRUE;
        PostMessage(WM_COMMAND, ID_APP_EXIT, 0);
        break;
      }
    }
  }
  DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNCREATE(CTestFrame, CTestFrameBase)
BEGIN_MESSAGE_MAP(CTestFrame, CTestFrameBase)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CDocApp : public CWinApp {
public:
  /// Adds the document template and makes the first document.
  virtual BOOL InitInstance() {
#ifdef TEST_MDI
    AddDocTemplate(new CMultiDocTemplate(IDR_DOCTYPE, RUNTIME_CLASS(CTestDoc),
                                         RUNTIME_CLASS(CTestChild), RUNTIME_CLASS(CTestView)));
    CTestFrame* pFrame = new CTestFrame;
    if (!pFrame->LoadFrame(IDR_MAINFRAME)) {
      printf("FAIL LoadFrame\n");
      return FALSE;
    }
    m_pMainWnd = pFrame;
#else
    AddDocTemplate(new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CTestDoc),
                                          RUNTIME_CLASS(CTestFrame), RUNTIME_CLASS(CTestView)));
#endif
    OnFileNew();
    if (m_pMainWnd == NULL) {
      printf("FAIL OnFileNew\n");
      return FALSE;
    }
    m_pMainWnd->ShowWindow(m_nCmdShow);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }

  /// Prints the result.
  virtual int ExitInstance() {
    CHECK(g_bDone);
    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILED %d\n", g_failures);
    return CWinApp::ExitInstance();
  }
};

CDocApp theApp;
