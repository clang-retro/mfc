// docobtest.cpp - Active Documents, with the server and its container in
// one program: the container's COleDocObjectItem is a document site, so
// the server's document becomes a DocObject (CDocObjectServer) and shows
// as a whole document filling the container's view. The test checks the
// activation through IOleDocumentSite::ActivateMe, the view's rectangle,
// the commands the container sends through IOleCommandTarget (the
// standard ones and the program's own OLE command map), the view state
// the container keeps, printing through IPrint (the page count, page sets,
// the container's printer, the continue callback), closing the view, and
// the DocObject registration.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include <afxdocob.h>
#include <stddef.h>
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
// The pages the server's view printed, as digits.
static CString g_strPrinted;
static int g_nBeginPrinting = 0, g_nEndPrinting = 0;
static BOOL g_bLastWasDocObject = FALSE;

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
  // Text and a check, through IOleCommandTarget::QueryStatus.
  afx_msg void OnUpdateRecolor(CCmdUI* pCmdUI) {
    pCmdUI->SetText(_T("Recolor all"));
    pCmdUI->SetCheck(1);
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CDocObDoc, COleServerDoc)
BEGIN_MESSAGE_MAP(CDocObDoc, COleServerDoc)
  ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
  ON_COMMAND(ID_EDIT_CUT, OnEditCopy)
  ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateEditCut)
  ON_COMMAND(ID_EDIT_RECOLOR, OnRecolor)
  ON_UPDATE_COMMAND_UI(ID_EDIT_RECOLOR, OnUpdateRecolor)
END_MESSAGE_MAP()

class CDocObView : public CView {
  DECLARE_DYNCREATE(CDocObView)
public:
  virtual void OnDraw(CDC* pDC) {
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, RGB(0, 0, 255));
  }
  // Three pages.
  virtual BOOL OnPreparePrinting(CPrintInfo* pInfo) {
    pInfo->SetMaxPage(3);
    g_bLastWasDocObject = pInfo->m_bDocObject;
    return DoPreparePrinting(pInfo);
  }
  virtual void OnBeginPrinting(CDC*, CPrintInfo*) { g_nBeginPrinting++; }
  virtual void OnEndPrinting(CDC*, CPrintInfo*) { g_nEndPrinting++; }
  virtual void OnPrint(CDC* pDC, CPrintInfo* pInfo) {
    g_strPrinted += (TCHAR)(_T('0') + pInfo->m_nCurPage);
    CString str;
    str.Format(_T("page %u"), pInfo->m_nCurPage);
    pDC->TextOut(100, 100, str);
  }
};
IMPLEMENT_DYNCREATE(CDocObView, CView)

// A continue callback that stops after some pages.
class CContinue : public IContinueCallback {
public:
  LONG m_nStopAfter, m_nCalls, m_nLastPage;
  explicit CContinue(LONG nStopAfter) : m_nStopAfter(nStopAfter), m_nCalls(0), m_nLastPage(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IContinueCallback)) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(FContinue)() { return S_OK; }
  STDMETHOD(FContinuePrinting)(LONG nCntPrinted, LONG nCurPage, WCHAR*) {
    m_nCalls++;
    m_nLastPage = nCurPage;
    return nCntPrinted >= m_nStopAfter ? S_FALSE : S_OK;
  }
};

// The default printer as a target device.
static DVTARGETDEVICE* DefaultPrinter() {
  PRINTDLG pd;
  memset(&pd, 0, sizeof(pd));
  pd.lStructSize = sizeof(pd);
  if (!AfxGetApp()->GetPrinterDeviceDefaults(&pd) || pd.hDevNames == NULL) return NULL;
  DEVNAMES* pdn = (DEVNAMES*)::GlobalLock(pd.hDevNames);
  CStringW strDriver((LPCTSTR)pdn + pdn->wDriverOffset);
  CStringW strDevice((LPCTSTR)pdn + pdn->wDeviceOffset);
  ::GlobalUnlock(pd.hDevNames);
  UINT cbHeader = offsetof(DVTARGETDEVICE, tdData);
  UINT cbDriver = (strDriver.GetLength() + 1) * sizeof(WCHAR);
  UINT cbDevice = (strDevice.GetLength() + 1) * sizeof(WCHAR);
  UINT cbTotal = cbHeader + cbDriver + cbDevice + sizeof(WCHAR);
  DVTARGETDEVICE* ptd = (DVTARGETDEVICE*)::CoTaskMemAlloc(cbTotal);
  memset(ptd, 0, cbTotal);
  ptd->tdSize = cbTotal;
  ptd->tdDriverNameOffset = (WORD)cbHeader;
  ptd->tdDeviceNameOffset = (WORD)(cbHeader + cbDriver);
  ptd->tdPortNameOffset = (WORD)(cbHeader + cbDriver + cbDevice);
  memcpy((BYTE*)ptd + ptd->tdDriverNameOffset, (LPCWSTR)strDriver, cbDriver);
  memcpy((BYTE*)ptd + ptd->tdDeviceNameOffset, (LPCWSTR)strDevice, cbDevice);
  return ptd;
}

class CDocObFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CDocObFrame)
};
IMPLEMENT_DYNCREATE(CDocObFrame, CFrameWnd)

// A DocObject's in-place frame: its Help popup goes into the container's
// Help menu.
class CDocObIPFrame : public COleDocIPFrameWnd {
  DECLARE_DYNCREATE(CDocObIPFrame)
};
IMPLEMENT_DYNCREATE(CDocObIPFrame, COleDocIPFrameWnd)

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
  CMainFrame() : m_nStep(0), m_bBusy(FALSE), m_pItem(NULL), m_hHelp(NULL) {}

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
    TestLog("server view %d,%d,%d,%d", rect.left, rect.top, rect.right, rect.bottom);
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
    CheckMenu();
  }

  // The shared menu: the container's File and Help, the object's Edit, and
  // the object's Help popup at the top of the container's Help.
  HMENU m_hHelp;
  void CheckMenu() {
    m_hHelp = NULL;
    COleIPFrameWnd* pFrame = g_pSrvDoc != NULL ? g_pSrvDoc->InPlaceFrame() : NULL;
    CHECK(pFrame != NULL && pFrame->IsKindOf(RUNTIME_CLASS(COleDocIPFrameWnd)));
    if (pFrame == NULL) return;
    HMENU hMenu = pFrame->m_hSharedMenu;
    CHECK(hMenu != NULL && ::GetMenuItemCount(hMenu) == 3);
    TCHAR sz[64];
    for (int i = 0; hMenu != NULL && i < ::GetMenuItemCount(hMenu); i++) {
      ::GetMenuString(hMenu, i, sz, 64, MF_BYPOSITION);
      TestLog("shared menu %d: %s", i, sz);
    }
    if (hMenu == NULL || ::GetMenuItemCount(hMenu) != 3) return;
    ::GetMenuString(hMenu, 1, sz, 64, MF_BYPOSITION);
    CHECK(lstrcmp(sz, _T("&Edit")) == 0);
    ::GetMenuString(hMenu, 2, sz, 64, MF_BYPOSITION);
    CHECK(lstrcmp(sz, _T("&Help")) == 0);
    m_hHelp = ::GetSubMenu(hMenu, 2);
    CHECK(m_hHelp != NULL && ::GetMenuItemCount(m_hHelp) == 2);
    if (m_hHelp == NULL) return;
    ::GetMenuString(m_hHelp, 0, sz, 64, MF_BYPOSITION);
    CHECK(lstrcmp(sz, _T("DocOb &Help")) == 0);
    HMENU hObjHelp = ::GetSubMenu(m_hHelp, 0);
    CHECK(hObjHelp != NULL && ::GetMenuItemID(hObjHelp, 0) == ID_HELP_INDEX);
    CHECK(::GetMenuItemID(m_hHelp, 1) == ID_APP_ABOUT);
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
    struct {
      OLECMDTEXT text;
      WCHAR sz[63];
    } text;
    memset(&text, 0, sizeof(text));
    text.text.cmdtextf = OLECMDTEXTF_NAME;
    text.text.cwBuf = 64;
    CHECK(m_pItem->QueryCommand(1, &dw, &text.text, &CGID_DocObTest) == S_OK);
    TestLog("recolor %08lx, text %ls (%lu)", dw, text.text.rgwz, text.text.cwActual);
    CHECK(dw == (OLECMDF_SUPPORTED | OLECMDF_ENABLED | OLECMDF_LATCHED));
    CHECK(lstrcmpW(text.text.rgwz, L"Recolor all") == 0 && text.text.cwActual == 12);
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
  }

  void Print() {
    if (m_pItem == NULL || m_pItem->GetActiveView() == NULL) return;
    // The page count, from the server view's OnPreparePrinting.
    LONG nFirst = 0, nPages = 0;
    CHECK(m_pItem->GetPageCount(&nFirst, &nPages));
    TestLog("pages %d from %d", nPages, nFirst);
    CHECK(nFirst == 1 && nPages == 3 && g_bLastWasDocObject);
    CPrintInfo info;
    CHECK(COleDocObjectItem::OnPreparePrinting(View(), &info));
    CHECK(info.GetMinPage() == 1 && info.GetMaxPage() == 3);

    LPPRINT lpPrint = NULL;
    m_pItem->m_lpObject->QueryInterface(IID_IPrint, (void**)&lpPrint);
    CHECK(lpPrint != NULL);
    if (lpPrint == NULL) return;
    CHECK(lpPrint->SetInitialPageNum(5) == S_OK);
    CHECK(lpPrint->GetPageInfo(&nFirst, &nPages) == S_OK && nFirst == 5 && nPages == 3);

    // Counting only.
    LONG nPrinted = -1, nLast = -1;
    CHECK(lpPrint->Print(PRINTFLAG_DONTACTUALLYPRINT, NULL, NULL, NULL, NULL, 5, &nPrinted,
                         &nLast) == S_OK);
    CHECK(nPrinted == 3 && nLast == 7 && g_strPrinted.IsEmpty());
    // A page set: the second page on, and the odd pages of all.
    struct {
      PAGESET set;
      PAGERANGE more[1];
    } ps;
    memset(&ps, 0, sizeof(ps));
    ps.set.cbStruct = sizeof(PAGESET);
    ps.set.fOddPages = ps.set.fEvenPages = TRUE;
    ps.set.cPageRange = 1;
    ps.set.rgPages[0].nFromPage = 2;
    ps.set.rgPages[0].nToPage = PAGESET_TOLASTPAGE;
    PAGESET* pSet = &ps.set;
    CHECK(lpPrint->Print(PRINTFLAG_DONTACTUALLYPRINT, NULL, &pSet, NULL, NULL, 1, &nPrinted,
                         &nLast) == S_OK);
    CHECK(nPrinted == 2 && nLast == 3);
    ps.set.fEvenPages = FALSE;
    ps.set.rgPages[0].nFromPage = 1;
    CHECK(lpPrint->Print(PRINTFLAG_DONTACTUALLYPRINT, NULL, &pSet, NULL, NULL, 1, &nPrinted,
                         &nLast) == S_OK);
    CHECK(nPrinted == 2 && nLast == 3);
    ps.set.fEvenPages = TRUE;
    ps.set.rgPages[0].nFromPage = 4;  // past the end
    CHECK(FAILED(lpPrint->Print(PRINTFLAG_DONTACTUALLYPRINT, NULL, &pSet, NULL, NULL, 1,
                                &nPrinted, &nLast)));

    // Printing to the container's printer, into a file.
    DVTARGETDEVICE* ptd = DefaultPrinter();
    if (ptd == NULL) {
      TestLog("note: no printer; IPrint::Print not run");
    } else {
      TCHAR szPath[MAX_PATH], szDir[MAX_PATH];
      ::GetTempPath(MAX_PATH, szDir);
      ::GetTempFileName(szDir, _T("dob"), 0, szPath);
      CStringW strPath(szPath);
      STGMEDIUM stgm;
      memset(&stgm, 0, sizeof(stgm));
      stgm.tymed = TYMED_FILE;
      stgm.lpszFileName = (LPOLESTR)(LPCWSTR)strPath;
      ps.set.rgPages[0].nFromPage = 2;
      CContinue cont(99);
      int nBegin = g_nBeginPrinting;
      HRESULT hr = lpPrint->Print(PRINTFLAG_PRINTTOFILE, &ptd, &pSet, &stgm, &cont, 10,
                                  &nPrinted, &nLast);
      TestLog("Print: %08lx, %d pages to %d, printed %s", (unsigned long)hr, nPrinted, nLast,
              (LPCSTR)CStringA(g_strPrinted));
      CHECK(hr == S_OK && nPrinted == 2 && nLast == 12 && g_strPrinted == _T("23"));
      CHECK(cont.m_nCalls == 2 && cont.m_nLastPage == 12);
      CHECK(g_nBeginPrinting == nBegin + 1 && g_nEndPrinting == g_nBeginPrinting);
      // The callback stops it after the first page.
      g_strPrinted.Empty();
      CContinue stop(1);
      pSet = NULL;
      hr = lpPrint->Print(PRINTFLAG_PRINTTOFILE, &ptd, &pSet, &stgm, &stop, 1, &nPrinted, &nLast);
      CHECK(FAILED(hr) && nPrinted == 1 && g_strPrinted == _T("1"));
      ::DeleteFile(szPath);
      ::CoTaskMemFree(ptd);
    }
    lpPrint->Release();
  }

  void Close() {
    if (m_pItem == NULL) return;
    // Closing the view takes the document out of the container.
    m_pItem->CloseView();
    CHECK(m_pItem->GetActiveView() == NULL);
    CHECK(g_pSrvDoc == NULL || !g_pSrvDoc->IsInPlaceActive());
    // The object's Help popup is out of the container's Help menu again.
    if (m_hHelp != NULL) CHECK(::GetMenuItemCount(m_hHelp) == 1);
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
      case 2: Print(); break;
      case 3: Close(); break;
      case 4:
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
