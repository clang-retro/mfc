// docobtest.cpp - Active Documents, with the server and its container in
// one program: the container's COleDocObjectItem is a document site, so
// the server's document becomes a DocObject (CDocObjectServer) and shows
// as a whole document filling the container's view. The test checks the
// activation through IOleDocumentSite::ActivateMe, the view's rectangle,
// the commands the container sends through IOleCommandTarget (the
// standard ones and the program's own OLE command map), the view state
// the container keeps, closing the view, and the DocObject registration.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include <afxdocob.h>
#include "docobtest.h"
#include "testutil.h"

// {5B8E1F37-6A2C-4D90-B3E4-0C7A9D2F6E15}
static const CLSID CLSID_DocOb = {
    0x5b8e1f37, 0x6a2c, 0x4d90, {0xb3, 0xe4, 0x0c, 0x7a, 0x9d, 0x2f, 0x6e, 0x15}};
static const TCHAR g_szClsid[] = _T("{5B8E1F37-6A2C-4D90-B3E4-0C7A9D2F6E15}");
// The program's own command group.
// {9E41C6A2-5F03-4B78-8D1E-2A6C0B4F9D73}
static const GUID CGID_DocObTest = {
    0x9e41c6a2, 0x5f03, 0x4b78, {0x8d, 0x1e, 0x2a, 0x6c, 0x0b, 0x4f, 0x9d, 0x73}};

static int g_nCopies = 0, g_nRecolors = 0, g_nViewsActivated = 0;
static DWORD g_dwSavedState = 0, g_dwAppliedState = 0;

class CDocObDoc;
static CDocObDoc* g_pSrvDoc = NULL;

// The program's DocObject server: its own command, and its view state.
class CDocObServer : public CDocObjectServer {
public:
  CDocObServer(COleServerDoc* pDoc, LPOLEDOCUMENTSITE pSite) : CDocObjectServer(pDoc, pSite) {}
  virtual void OnSaveViewState(CArchive& ar) { ar << (DWORD)0x1234; }
  virtual void OnApplyViewState(CArchive& ar) { ar >> g_dwAppliedState; }
  virtual void OnActivateView() { g_nViewsActivated++; }
  DECLARE_OLECMD_MAP()
};
BEGIN_OLECMD_MAP(CDocObServer, CDocObjectServer)
  ON_OLECMD(&CGID_DocObTest, 1, ID_EDIT_RECOLOR)
END_OLECMD_MAP()

class CDocObItem : public CDocObjectServerItem {
public:
  explicit CDocObItem(COleServerDoc* pDoc) : CDocObjectServerItem(pDoc, TRUE) {}
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize) {
    rSize = CSize(2000, 1000);
    pDC->Rectangle(0, 0, 2000, 1000);
    return TRUE;
  }
};

class CDocObDoc : public COleServerDoc {
  DECLARE_DYNCREATE(CDocObDoc)
public:
  CDocObDoc() { g_pSrvDoc = this; }
  virtual ~CDocObDoc() {
    if (g_pSrvDoc == this) g_pSrvDoc = NULL;
  }
  COleIPFrameWnd* InPlaceFrame() const { return m_pInPlaceFrame; }
  virtual COleServerItem* OnGetEmbeddedItem() { return new CDocObItem(this); }
  virtual CDocObjectServer* GetDocObjectServer(LPOLEDOCUMENTSITE pDocSite) {
    return new CDocObServer(this, pDocSite);
  }
  virtual void Serialize(CArchive&) {}
  afx_msg void OnEditCopy() { g_nCopies++; }
  afx_msg void OnUpdateEditCut(CCmdUI* pCmdUI) { pCmdUI->Enable(FALSE); }
  afx_msg void OnRecolor() { g_nRecolors++; }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CDocObDoc, COleServerDoc)
BEGIN_MESSAGE_MAP(CDocObDoc, COleServerDoc)
  ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
  ON_COMMAND(ID_EDIT_CUT, OnEditCopy)
  ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateEditCut)
  ON_COMMAND(ID_EDIT_RECOLOR, OnRecolor)
END_MESSAGE_MAP()

class CDocObView : public CView {
  DECLARE_DYNCREATE(CDocObView)
public:
  virtual void OnDraw(CDC* pDC) {
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, RGB(0, 0, 255));
  }
};
IMPLEMENT_DYNCREATE(CDocObView, CView)

class CDocObFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CDocObFrame)
};
IMPLEMENT_DYNCREATE(CDocObFrame, CFrameWnd)

class CDocObIPFrame : public COleIPFrameWnd {
  DECLARE_DYNCREATE(CDocObIPFrame)
};
IMPLEMENT_DYNCREATE(CDocObIPFrame, COleIPFrameWnd)

// ---------------------------------------------------------------------
// The container

class CCntrDoc : public COleDocument {
  DECLARE_DYNCREATE(CCntrDoc)
};
IMPLEMENT_DYNCREATE(CCntrDoc, COleDocument)

class CCntrView : public CView {
  DECLARE_DYNCREATE(CCntrView)
public:
  virtual void OnDraw(CDC*) {}
};
IMPLEMENT_DYNCREATE(CCntrView, CView)

static CRect WindowRectIn(HWND hWnd, HWND hWndParent) {
  CRect rect;
  ::GetWindowRect(hWnd, &rect);
  ::MapWindowPoints(NULL, hWndParent, (LPPOINT)&rect, 2);
  return rect;
}

static CString RegValue(LPCTSTR lpszKey) {
  TCHAR sz[MAX_PATH * 2];
  LONG cb = sizeof(sz);
  if (::RegQueryValue(HKEY_CLASSES_ROOT, lpszKey, sz, &cb) != ERROR_SUCCESS) return _T("<none>");
  return sz;
}

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep;
  BOOL m_bBusy;
  COleDocObjectItem* m_pItem;
  CMainFrame() : m_nStep(0), m_bBusy(FALSE), m_pItem(NULL) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 200, NULL);
    return 0;
  }
  CCntrView* View() { return (CCntrView*)GetActiveView(); }

  // The server's view fills the container's view.
  void CheckFills(const CRect& rectExpect, const char* pszWhen) {
    TestLog("check fills: %s", pszWhen);
    CHECK(g_pSrvDoc != NULL && g_pSrvDoc->IsInPlaceActive() && g_pSrvDoc->InPlaceFrame() != NULL);
    if (g_pSrvDoc == NULL || g_pSrvDoc->InPlaceFrame() == NULL) return;
    HWND hWndFrame = g_pSrvDoc->InPlaceFrame()->m_hWnd;
    CHECK(::GetParent(hWndFrame) == View()->m_hWnd && ::IsWindowVisible(hWndFrame));
    POSITION pos = g_pSrvDoc->GetFirstViewPosition();
    CView* pSrvView = pos != NULL ? g_pSrvDoc->GetNextView(pos) : NULL;
    CHECK(pSrvView != NULL && pSrvView->GetParentFrame() == g_pSrvDoc->InPlaceFrame());
    if (pSrvView == NULL) return;
    CRect rect = WindowRectIn(pSrvView->m_hWnd, View()->m_hWnd);
    TestLog("server view %ld,%ld,%ld,%ld", rect.left, rect.top, rect.right, rect.bottom);
    CHECK(rect == rectExpect);
  }

  void Activate() {
    CCntrDoc* pDoc = (CCntrDoc*)GetActiveDocument();
    m_pItem = new COleDocObjectItem(pDoc);
    BOOL bCreated = m_pItem->CreateNewItem(CLSID_DocOb, OLERENDER_FORMAT, CF_METAFILEPICT);
    TestLog("CreateNewItem: %d (%08lx)", bCreated, (unsigned long)m_pItem->GetLastStatus());
    CHECK(bCreated && g_pSrvDoc != NULL);
    if (!bCreated || g_pSrvDoc == NULL) return;
    // A document site makes the document a DocObject.
    CHECK(g_pSrvDoc->IsDocObject());
    LPOLEDOCUMENT lpDoc = NULL;
    CHECK(SUCCEEDED(m_pItem->m_lpObject->QueryInterface(IID_IOleDocument, (void**)&lpDoc)));
    if (lpDoc != NULL) {
      DWORD dwStatus = 99;
      CHECK(lpDoc->GetDocMiscStatus(&dwStatus) == S_OK && dwStatus == 0);
      lpDoc->Release();
    }
    // Showing it: the server asks the site to activate it, the site makes
    // the view and puts it up over the whole view.
    m_pItem->DoVerb(OLEIVERB_SHOW, View());
    TestLog("active view %p, views activated %d", m_pItem->GetActiveView(), g_nViewsActivated);
    CHECK(m_pItem->GetActiveView() != NULL && g_nViewsActivated == 1);
    CRect rectClient;
    View()->GetClientRect(&rectClient);
    CheckFills(rectClient, "activated");
    CHECK(m_pItem->GetItemState() == COleClientItem::activeUIState);
    if (m_pItem->GetActiveView() != NULL) {
      LPUNKNOWN lpUnk = NULL;
      CHECK(m_pItem->GetActiveView()->GetDocument(&lpUnk) == S_OK && lpUnk != NULL);
      if (lpUnk != NULL) lpUnk->Release();
      CRect rect(0, 0, 0, 0);
      m_pItem->GetActiveView()->GetRect(&rect);
      CHECK(rect == rectClient);
    }
  }

  void Commands() {
    if (m_pItem == NULL || m_pItem->GetActiveView() == NULL) return;
    // A new rectangle for the view.
    CRect rectNew(10, 5, 150, 90);
    m_pItem->GetActiveView()->SetRect(&rectNew);
    CheckFills(rectNew, "new rectangle");

    DWORD dw = 0;
    CHECK(m_pItem->QueryCommand(OLECMDID_COPY, &dw) == S_OK);
    TestLog("copy %08lx", dw);
    CHECK(dw == (OLECMDF_SUPPORTED | OLECMDF_ENABLED));
    CHECK(m_pItem->QueryCommand(OLECMDID_CUT, &dw) == S_OK);
    TestLog("cut %08lx", dw);
    CHECK(dw == OLECMDF_SUPPORTED);  // its update handler disables it
    CHECK(m_pItem->QueryCommand(OLECMDID_PASTE, &dw) == S_OK);
    CHECK(dw == OLECMDF_SUPPORTED);  // no handler
    CHECK(m_pItem->QueryCommand(OLECMDID_ZOOM, &dw) == S_OK);
    CHECK(dw == 0);  // not in the map
    CHECK(m_pItem->ExecCommand(OLECMDID_COPY) == S_OK);
    CHECK(g_nCopies == 1);
    CHECK(m_pItem->ExecCommand(OLECMDID_CUT) == OLECMDERR_E_DISABLED);
    CHECK(m_pItem->ExecCommand(OLECMDID_ZOOM) == OLECMDERR_E_NOTSUPPORTED);
    CHECK(g_nCopies == 1);
    // The program's own group.
    CHECK(m_pItem->QueryCommand(1, &dw, NULL, &CGID_DocObTest) == S_OK);
    CHECK(dw == (OLECMDF_SUPPORTED | OLECMDF_ENABLED));
    CHECK(m_pItem->ExecCommand(1, OLECMDEXECOPT_DONTPROMPTUSER, &CGID_DocObTest) == S_OK);
    CHECK(g_nRecolors == 1);
    static const GUID guidOther = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
    CHECK(m_pItem->ExecCommand(1, OLECMDEXECOPT_DONTPROMPTUSER, &guidOther) ==
          OLECMDERR_E_UNKNOWNGROUP);

    // The view's state, kept by the container.
    LPSTREAM lpStream = NULL;
    ::CreateStreamOnHGlobal(NULL, TRUE, &lpStream);
    if (lpStream != NULL) {
      CHECK(m_pItem->GetActiveView()->SaveViewState(lpStream) == S_OK);
      LARGE_INTEGER zero = {};
      lpStream->Seek(zero, STREAM_SEEK_SET, NULL);
      CHECK(m_pItem->GetActiveView()->ApplyViewState(lpStream) == S_OK);
      TestLog("view state %08lx", g_dwAppliedState);
      CHECK(g_dwAppliedState == 0x1234);
      lpStream->Release();
    }
    LONG nFirst = 0, nPages = 0;
    CHECK(!m_pItem->GetPageCount(&nFirst, &nPages));  // not known before printing
  }

  void Close() {
    if (m_pItem == NULL) return;
    // Closing the view takes the document out of the container.
    m_pItem->CloseView();
    CHECK(m_pItem->GetActiveView() == NULL);
    CHECK(g_pSrvDoc == NULL || !g_pSrvDoc->IsInPlaceActive());
    m_pItem->Close(OLECLOSE_NOSAVE);
    m_pItem->Delete();
    m_pItem = NULL;
  }

  afx_msg void OnTimer(UINT_PTR) {
    if (m_bBusy) return;
    m_bBusy = TRUE;
    switch (m_nStep++) {
      case 0: Activate(); break;
      case 1: Commands(); break;
      case 2: Close(); break;
      case 3:
        // Nothing to save: no prompt on the way out.
        GetActiveDocument()->SetModifiedFlag(FALSE);
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
    m_bBusy = FALSE;
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CDocObApp : public CWinApp {
public:
  COleTemplateServer m_server;

  virtual BOOL InitInstance() {
    TestOpenLog(_T("docobtest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    CMultiDocTemplate* pSrvTemplate =
        new CMultiDocTemplate(IDR_SRVR, RUNTIME_CLASS(CDocObDoc), RUNTIME_CLASS(CDocObFrame),
                              RUNTIME_CLASS(CDocObView));
    pSrvTemplate->SetServerInfo(IDR_SRVR_EMBEDDED, IDR_SRVR_INPLACE,
                                RUNTIME_CLASS(CDocObIPFrame));
    m_server.ConnectTemplate(CLSID_DocOb, pSrvTemplate, FALSE);
    CHECK(COleTemplateServer::RegisterAll());
    m_server.UpdateRegistry(OAT_DOC_OBJECT_SERVER);
    CString strKey = CString(_T("CLSID\\")) + g_szClsid + _T("\\DocObject");
    CHECK(RegValue(strKey) != _T("<none>"));

    CSingleDocTemplate* pCntrTemplate =
        new CSingleDocTemplate(IDR_CNTR, RUNTIME_CLASS(CCntrDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(CCntrView));
    pCntrTemplate->SetContainerInfo(IDR_CNTR_INPLACE);
    AddDocTemplate(pCntrTemplate);
    AddDocTemplate(pSrvTemplate);
    if (pCntrTemplate->OpenDocumentFile(NULL) == NULL) {
      TestLog("FAIL: no container document");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->SetWindowPos(NULL, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }

  virtual int ExitInstance() {
    m_server.Unregister();
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CDocObApp theApp;
