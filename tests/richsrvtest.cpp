// richsrvtest.cpp - CRichEditDoc as an OLE server document, with the
// container and the server in one program: a container embeds a new rich
// text object, the text typed into the server's view comes back through
// the object's data, a copy of the object saved and loaded again keeps
// the text, and a saved file links like any server document's.
#include <afxwin.h>
#include <afxext.h>
#include <afxrich.h>
#include "richsrvtest.h"
#include "testutil.h"

// {7D3E5A10-2C4B-4E91-8F6A-1B9C0D2E3F47}
static const CLSID CLSID_RichSrv = {
    0x7d3e5a10, 0x2c4b, 0x4e91, {0x8f, 0x6a, 0x1b, 0x9c, 0x0d, 0x2e, 0x3f, 0x47}};

static CLIPFORMAT CfText() {
  static CLIPFORMAT cf = (CLIPFORMAT)::RegisterClipboardFormat(_T("RichSrv Text"));
  return cf;
}

static int g_nDocs = 0;

class CRichSrvDoc;

// The item that stands for the whole document: it draws a frame and hands
// out the text.
class CRichSrvItem : public COleServerItem {
public:
  explicit CRichSrvItem(CRichSrvDoc* pDoc);
  CRichEditDoc* GetDoc() const { return (CRichEditDoc*)GetDocument(); }
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize) {
    rSize = CSize(4000, 1000);
    pDC->Rectangle(0, 0, rSize.cx, rSize.cy);
    return TRUE;
  }
  virtual BOOL OnGetExtent(DVASPECT nDrawAspect, CSize& rSize) {
    if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
    rSize = CSize(4000, 1000);
    return TRUE;
  }
  virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal) {
    if (lpFormatEtc->cfFormat != CfText())
      return COleServerItem::OnRenderGlobalData(lpFormatEtc, phGlobal);
    CString str;
    GetDoc()->GetView()->GetWindowText(str);
    CStringW strW(str);
    UINT cb = (strW.GetLength() + 1) * sizeof(WCHAR);
    *phGlobal = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, cb);
    if (*phGlobal == NULL) return FALSE;
    memcpy(::GlobalLock(*phGlobal), (LPCWSTR)strW, cb);
    ::GlobalUnlock(*phGlobal);
    return TRUE;
  }
};

class CRichSrvDoc : public CRichEditDoc {
  DECLARE_DYNCREATE(CRichSrvDoc)
public:
  CRichSrvDoc() { g_nDocs++; }
  virtual ~CRichSrvDoc() { g_nDocs--; }
  virtual COleServerItem* OnGetEmbeddedItem() { return new CRichSrvItem(this); }
};
IMPLEMENT_DYNCREATE(CRichSrvDoc, CRichEditDoc)

CRichSrvItem::CRichSrvItem(CRichSrvDoc* pDoc) : COleServerItem(pDoc, TRUE) {
  GetDataSource()->DelayRenderData(CfText());
}

class CRichSrvFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CRichSrvFrame)
};
IMPLEMENT_DYNCREATE(CRichSrvFrame, CFrameWnd)

// The text the object hands out.
static CString ObjectText(LPUNKNOWN lpObject) {
  CString str(_T("<none>"));
  LPDATAOBJECT lpData = NULL;
  if (lpObject == NULL || FAILED(lpObject->QueryInterface(IID_IDataObject, (void**)&lpData)))
    return str;
  FORMATETC fe = {CfText(), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM stg;
  HRESULT hr = lpData->GetData(&fe, &stg);
  if (SUCCEEDED(hr)) {
    str = CString((LPCWSTR)::GlobalLock(stg.hGlobal));
    ::GlobalUnlock(stg.hGlobal);
    ::ReleaseStgMedium(&stg);
  } else {
    TestLog("GetData: %08lx", (unsigned long)hr);
  }
  lpData->Release();
  str.TrimRight();
  return str;
}

class CCntrDoc : public COleDocument {
public:
  CCntrDoc() { SetTitle(_T("Letter")); }
};

class CRichSrvApp : public CWinApp {
public:
  COleTemplateServer m_server;
  CMultiDocTemplate* m_pTemplate;
  CRichSrvApp() : m_pTemplate(NULL) {}

  CRichSrvDoc* LastDoc() {
    CRichSrvDoc* pDoc = NULL;
    POSITION pos = m_pTemplate->GetFirstDocPosition();
    while (pos != NULL) pDoc = (CRichSrvDoc*)m_pTemplate->GetNextDoc(pos);
    return pDoc;
  }

  void TestEmbedding() {
    CCntrDoc* pCntr = new CCntrDoc;
    COleClientItem* pItem = new COleClientItem(pCntr);
    BOOL bCreated = pItem->CreateNewItem(CLSID_RichSrv, OLERENDER_FORMAT, CF_METAFILEPICT);
    TestLog("CreateNewItem: %d (%08lx), documents %d", bCreated,
            (unsigned long)pItem->GetLastStatus(), g_nDocs);
    CHECK(bCreated && g_nDocs == 1);
    CRichSrvDoc* pDoc = LastDoc();
    CHECK(pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(COleServerDoc)));
    if (!bCreated || pDoc == NULL) {
      delete pCntr;
      return;
    }
    CHECK(pDoc->IsEmbedded() && pDoc->GetView() != NULL);
    CHECK(pDoc->GetStreamFormat() == SF_RTF);
    CHECK(pDoc->GetEmbeddedItem() != NULL);
    // The embedded menu, from SetServerInfo.
    CHECK(pDoc->GetDefaultMenu() == m_pTemplate->m_hMenuEmbedding &&
          m_pTemplate->m_hMenuEmbedding != NULL);
    CSize size(0, 0);
    CHECK(pItem->GetExtent(&size) && size == CSize(4000, 1000));

    // Text typed into the server's view comes back through the object.
    CRichEditCtrl& ctrl = pDoc->GetView()->GetRichEditCtrl();
    ctrl.SetWindowText(_T("Dear server,"));
    ctrl.SetSel(0, 4);
    CHARFORMAT cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    ctrl.SetSelectionCharFormat(cf);
    pDoc->SetModifiedFlag();
    CString strText = ObjectText(pItem->m_lpObject);
    TestLog("object text: %s", (const char*)CStringA(strText));
    CHECK(strText == _T("Dear server,"));

    // A copy saved and loaded again: a second document reads the rich
    // text back, formatting included.
    COleClientItem* pClone = new COleClientItem(pCntr);
    BOOL bCloned = pClone->CreateCloneFrom(pItem);
    TestLog("clone: %d (%08lx), documents %d", bCloned, (unsigned long)pClone->GetLastStatus(),
            g_nDocs);
    CHECK(bCloned && g_nDocs == 2);
    CRichSrvDoc* pDoc2 = LastDoc();
    if (bCloned && pDoc2 != NULL && pDoc2 != pDoc) {
      CHECK(pDoc2->IsEmbedded());
      CString strCopy = ObjectText(pClone->m_lpObject);
      TestLog("copy text: %s", (const char*)CStringA(strCopy));
      CHECK(strCopy == _T("Dear server,"));
      CRichEditCtrl& ctrl2 = pDoc2->GetView()->GetRichEditCtrl();
      ctrl2.SetSel(1, 2);
      CHARFORMAT cf2 = {};
      cf2.cbSize = sizeof(cf2);
      cf2.dwMask = CFM_BOLD;
      ctrl2.GetSelectionCharFormat(cf2);
      CHECK((cf2.dwEffects & CFE_BOLD) != 0);
    }
    pClone->Close(OLECLOSE_NOSAVE);
    pItem->Close(OLECLOSE_NOSAVE);
    // The container's items hold the last references to the documents.
    delete pCntr;
    TestLog("after close: documents %d", g_nDocs);
    CHECK(g_nDocs == 0);
  }

  void TestFileLink() {
    // A rich text file saved by the user's document: a link to it binds to
    // the open document.
    CRichSrvDoc* pDoc = (CRichSrvDoc*)m_pTemplate->OpenDocumentFile(NULL);
    CHECK(pDoc != NULL && !pDoc->IsEmbedded());
    if (pDoc == NULL) return;
    pDoc->GetView()->GetRichEditCtrl().SetWindowText(_T("Linked letter"));
    CString strPath = TestOutPath(_T("letter.rsv"));
    CHECK(pDoc->DoSave(strPath));
    CHECK(pDoc->m_dwRegister != 0);
    LPMONIKER lpFile = NULL;
    CStringW strW(strPath);
    ::CreateFileMoniker(strW, &lpFile);
    LPOLEOBJECT lpObject = NULL;
    HRESULT hr = ::BindMoniker(lpFile, 0, IID_IOleObject, (void**)&lpObject);
    TestLog("bind file: %08lx", (unsigned long)hr);
    CHECK(SUCCEEDED(hr));
    CHECK(ObjectText(lpObject) == _T("Linked letter"));
    if (lpObject != NULL) lpObject->Release();
    if (lpFile != NULL) lpFile->Release();
    pDoc->OnCloseDocument();
    CHECK(g_nDocs == 0);
    ::DeleteFile(strPath);
  }

  virtual BOOL InitInstance() {
    TestOpenLog(_T("richsrvtest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    m_pTemplate = new CMultiDocTemplate(IDR_RICHTYPE, RUNTIME_CLASS(CRichSrvDoc),
                                        RUNTIME_CLASS(CRichSrvFrame),
                                        RUNTIME_CLASS(CRichEditView));
    m_pTemplate->SetServerInfo(IDR_SRVR_EMBEDDED);
    AddDocTemplate(m_pTemplate);
    m_server.ConnectTemplate(CLSID_RichSrv, m_pTemplate, FALSE);
    m_server.UpdateRegistry(OAT_SERVER);
    CHECK(COleTemplateServer::RegisterAll());
    CFrameWnd* pFrame = new CFrameWnd;
    pFrame->Create(NULL, _T("RichSrv"));
    m_pMainWnd = pFrame;

    TestEmbedding();
    TestFileLink();

    pFrame->DestroyWindow();
    m_pMainWnd = NULL;
    return FALSE;  // done: ExitInstance reports
  }

  virtual int ExitInstance() {
    m_server.Unregister();
    CWinApp::ExitInstance();
    CHECK(AfxOleCanExitApp());
    return TestFinish();
  }
};

CRichSrvApp theApp;
