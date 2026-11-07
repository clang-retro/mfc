// linktest.cpp - links to a server's documents and their items, in one
// program. The server's document is a colored square in a file; a link
// names the file (a file moniker) or one of its items ("file!Square").
// The test binds such monikers as a container's link would: to the open
// document through the running object table, and to a document that is
// not open, which COM makes and loads from the file. It checks the item's
// moniker, data and notifications, the Link Source data Paste Link uses,
// the shell registration that finds the file's class, and links to an
// object embedded in a container's document ("file!Embedding 1").
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include "linktest.h"
#include "testutil.h"

// {4C1A7E92-3B5D-4F08-A6C3-9E2D71B05F44}
static const CLSID CLSID_LinkDoc = {
    0x4c1a7e92, 0x3b5d, 0x4f08, {0xa6, 0xc3, 0x9e, 0x2d, 0x71, 0xb0, 0x5f, 0x44}};
static const COLORREF g_colors[] = {RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255)};

static int g_nItemsMade = 0, g_nItemsGone = 0, g_nDocs = 0;

class CLinkDoc;

class CLinkItem : public COleServerItem {
public:
  BOOL m_bNamed;
  CLinkItem(CLinkDoc* pDoc, LPCTSTR lpszName);
  virtual ~CLinkItem() {
    if (m_bNamed) g_nItemsGone++;
  }
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize);
  virtual BOOL OnGetExtent(DVASPECT nDrawAspect, CSize& rSize) {
    if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
    // The square is 1 cm, the whole document 2 by 1.
    rSize = m_bNamed ? CSize(1000, 1000) : CSize(2000, 1000);
    return TRUE;
  }
};

class CLinkDoc : public COleServerDoc {
  DECLARE_DYNCREATE(CLinkDoc)
public:
  int m_nColor;
  CLinkDoc() : m_nColor(0) { g_nDocs++; }
  virtual ~CLinkDoc() { g_nDocs--; }
  virtual COleServerItem* OnGetEmbeddedItem() { return new CLinkItem(this, NULL); }
  // The square is made when a link first asks for it.
  virtual COleServerItem* OnGetLinkedItem(LPCTSTR lpszItemName) {
    COleServerItem* pItem = COleServerDoc::OnGetLinkedItem(lpszItemName);
    if (pItem == NULL && lstrcmp(lpszItemName, _T("Square")) == 0)
      pItem = new CLinkItem(this, lpszItemName);
    return pItem;
  }
  virtual BOOL OnNewDocument() {
    if (!COleServerDoc::OnNewDocument()) return FALSE;
    m_nColor = 0;
    return TRUE;
  }
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring()) {
      ar << (DWORD)m_nColor;
    } else {
      DWORD n;
      ar >> n;
      m_nColor = (int)(n % 3);
    }
  }
  void Recolor() {
    m_nColor = (m_nColor + 1) % 3;
    SetModifiedFlag();
    UpdateAllItems(NULL);
  }
  COleServerItem* FindItem(LPCTSTR lpszName) { return COleServerDoc::OnGetLinkedItem(lpszName); }
};
IMPLEMENT_DYNCREATE(CLinkDoc, COleServerDoc)

CLinkItem::CLinkItem(CLinkDoc* pDoc, LPCTSTR lpszName)
    : COleServerItem(pDoc, TRUE), m_bNamed(lpszName != NULL) {
  if (lpszName != NULL) {
    SetItemName(lpszName);
    g_nItemsMade++;
  }
}

BOOL CLinkItem::OnDraw(CDC* pDC, CSize& rSize) {
  OnGetExtent(DVASPECT_CONTENT, rSize);
  pDC->SetMapMode(MM_ANISOTROPIC);
  pDC->SetWindowOrg(0, 0);
  pDC->SetWindowExt(rSize);
  CBrush brush(g_colors[((CLinkDoc*)GetDocument())->m_nColor]);
  pDC->FillRect(CRect(0, 0, rSize.cx, rSize.cy), &brush);
  return TRUE;
}

class CLinkView : public CView {
  DECLARE_DYNCREATE(CLinkView)
public:
  virtual void OnDraw(CDC*) {}
};
IMPLEMENT_DYNCREATE(CLinkView, CView)

class CLinkFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CLinkFrame)
};
IMPLEMENT_DYNCREATE(CLinkFrame, CFrameWnd)

// A container's document, which links can name objects in.
class CCntrDoc : public COleLinkingDoc {
public:
  CCntrDoc() { SetTitle(_T("Report")); }
};

// ---------------------------------------------------------------------
// Helpers

// Counts what an object tells its links.
class CSink : public IAdviseSink {
public:
  int m_nData, m_nSave, m_nClose, m_nRename;
  CSink() : m_nData(0), m_nSave(0), m_nClose(0), m_nRename(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IAdviseSink)) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD_(void, OnDataChange)(FORMATETC*, STGMEDIUM*) { m_nData++; }
  STDMETHOD_(void, OnViewChange)(DWORD, LONG) {}
  STDMETHOD_(void, OnRename)(IMoniker*) { m_nRename++; }
  STDMETHOD_(void, OnSave)() { m_nSave++; }
  STDMETHOD_(void, OnClose)() { m_nClose++; }
};

static CString DisplayName(LPMONIKER lpMoniker) {
  if (lpMoniker == NULL) return _T("<null>");
  LPBC lpbc = NULL;
  CString str(_T("<error>"));
  if (SUCCEEDED(::CreateBindCtx(0, &lpbc))) {
    LPOLESTR psz = NULL;
    if (SUCCEEDED(lpMoniker->GetDisplayName(lpbc, NULL, &psz)) && psz != NULL) {
      str = psz;
      ::CoTaskMemFree(psz);
    }
    lpbc->Release();
  }
  return str;
}

static LPMONIKER FileMoniker(LPCTSTR lpszPath) {
  LPMONIKER lpMoniker = NULL;
  CStringW str(lpszPath);
  ::CreateFileMoniker(str, &lpMoniker);
  return lpMoniker;
}

// lpszPath!lpszItem
static LPMONIKER ItemMoniker(LPCTSTR lpszPath, LPCTSTR lpszItem) {
  LPMONIKER lpFile = FileMoniker(lpszPath), lpItem = NULL, lpFull = NULL;
  CStringW str(lpszItem);
  ::CreateItemMoniker(L"!", str, &lpItem);
  if (lpFile != NULL && lpItem != NULL) ::CreateGenericComposite(lpFile, lpItem, &lpFull);
  if (lpFile != NULL) lpFile->Release();
  if (lpItem != NULL) lpItem->Release();
  return lpFull;
}

// Binds lpszPath!lpszItem as a composite moniker binds when the item is not
// in the running object table: the item's moniker with the file's at its
// left. (Wine's composite monikers bind only through the table.)
static HRESULT BindItem(LPCTSTR lpszPath, LPCTSTR lpszItem, REFIID riid, void** ppv) {
  *ppv = NULL;
  LPMONIKER lpFile = FileMoniker(lpszPath), lpItem = NULL;
  CStringW str(lpszItem);
  ::CreateItemMoniker(L"!", str, &lpItem);
  LPBC lpbc = NULL;
  HRESULT hr = ::CreateBindCtx(0, &lpbc);
  if (SUCCEEDED(hr) && lpFile != NULL && lpItem != NULL)
    hr = lpItem->BindToObject(lpbc, lpFile, riid, ppv);
  if (lpbc != NULL) lpbc->Release();
  if (lpFile != NULL) lpFile->Release();
  if (lpItem != NULL) lpItem->Release();
  return hr;
}

static BOOL SameObject(LPUNKNOWN p1, LPUNKNOWN p2) {
  if (p1 == NULL || p2 == NULL) return FALSE;
  LPUNKNOWN u1 = NULL, u2 = NULL;
  p1->QueryInterface(IID_IUnknown, (void**)&u1);
  p2->QueryInterface(IID_IUnknown, (void**)&u2);
  BOOL b = u1 != NULL && u1 == u2;
  if (u1 != NULL) u1->Release();
  if (u2 != NULL) u2->Release();
  return b;
}

// The color at the middle of the object's picture.
static COLORREF PictureColor(LPUNKNOWN lpObject) {
  LPDATAOBJECT lpData = NULL;
  if (lpObject == NULL ||
      FAILED(lpObject->QueryInterface(IID_IDataObject, (void**)&lpData)))
    return (COLORREF)-1;
  FORMATETC fe = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
  STGMEDIUM stg = {};
  COLORREF cr = (COLORREF)-1;
  HRESULT hr = lpData->GetData(&fe, &stg);
  if (SUCCEEDED(hr) && stg.tymed == TYMED_MFPICT) {
    METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(stg.hMetaFilePict);
    CClientDC dcScreen(NULL);
    CDC dc;
    dc.CreateCompatibleDC(&dcScreen);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dcScreen, 40, 20);
    CBitmap* pOld = dc.SelectObject(&bitmap);
    dc.FillSolidRect(0, 0, 40, 20, RGB(255, 255, 255));
    dc.SetMapMode(MM_ANISOTROPIC);
    dc.SetViewportExt(40, 20);
    dc.SetWindowExt(40, 20);
    ::PlayMetaFile(dc.m_hDC, pmfp->hMF);
    dc.SetMapMode(MM_TEXT);
    cr = dc.GetPixel(20, 10);
    dc.SelectObject(pOld);
    ::GlobalUnlock(stg.hMetaFilePict);
    ::ReleaseStgMedium(&stg);
  } else {
    TestLog("GetData: %08lx", (unsigned long)hr);
  }
  lpData->Release();
  return cr;
}

static CString RegValue(LPCTSTR lpszKey) {
  TCHAR sz[MAX_PATH * 2];
  LONG cb = sizeof(sz);
  if (::RegQueryValue(HKEY_CLASSES_ROOT, lpszKey, sz, &cb) != ERROR_SUCCESS) return _T("<none>");
  return sz;
}

// ---------------------------------------------------------------------

class CLinkApp : public CWinApp {
public:
  COleTemplateServer m_server;
  CMultiDocTemplate* m_pTemplate;
  CString m_strPath, m_strCntrPath;
  CLinkApp() : m_pTemplate(NULL) {}

  virtual BOOL InitInstance();
  virtual int ExitInstance();
  void TestRegistry();
  void TestOpenDocument();
  void TestClosedDocument();
  void TestEmbeddedObject();
};

CLinkApp theApp;

void CLinkApp::TestRegistry() {
  // The file's type, found from its extension, and the class it names.
  TestLog(".lkt: %s", (const char*)CStringA(RegValue(_T(".lkt"))));
  CHECK(RegValue(_T(".lkt")) == _T("LinkTest.Document"));
  CHECK(RegValue(_T("LinkTest.Document")) == _T("LinkTest Document"));
  CString strOpen = RegValue(_T("LinkTest.Document\\shell\\open\\command"));
  TestLog("open command: %s", (const char*)CStringA(strOpen));
  CHECK(strOpen.Find(_T("linktest")) >= 0 && strOpen.Right(4) == _T("\"%1\""));
  CHECK(RegValue(_T("LinkTest.Document\\DefaultIcon")).Right(2) == _T(",1"));
  CHECK(RegValue(_T("LinkTest.Document\\shell\\print\\command")) == _T("<none>"));
  CLSID clsid = CLSID_NULL;
  CStringW strPath(m_strPath);
  HRESULT hr = ::GetClassFile(strPath, &clsid);
  TestLog("GetClassFile: %08lx", (unsigned long)hr);
  CHECK(SUCCEEDED(hr) && ::IsEqualCLSID(clsid, CLSID_LinkDoc));
}

void CLinkApp::TestOpenDocument() {
  // A document the user has open, saved to a file: links find it in the
  // running object table.
  CLinkDoc* pDoc = (CLinkDoc*)m_pTemplate->OpenDocumentFile(NULL);
  CHECK(pDoc != NULL && !pDoc->IsEmbedded());
  if (pDoc == NULL) return;
  pDoc->m_nColor = 1;
  CHECK(pDoc->DoSave(m_strPath));
  CHECK(pDoc->m_dwRegister != 0);

  // The whole file: the document.
  LPMONIKER lpFile = FileMoniker(m_strPath);
  LPOLEOBJECT lpDocObj = NULL;
  HRESULT hr = ::BindMoniker(lpFile, 0, IID_IOleObject, (void**)&lpDocObj);
  TestLog("bind file: %08lx", (unsigned long)hr);
  CHECK(SUCCEEDED(hr) && SameObject(lpDocObj, pDoc->GetInterface(&IID_IUnknown)));
  if (lpDocObj != NULL) {
    LPMONIKER lpMoniker = NULL;
    CHECK(lpDocObj->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJFULL, &lpMoniker) ==
          S_OK);
    TestLog("document moniker: %s", (const char*)CStringA(DisplayName(lpMoniker)));
    CHECK(lpMoniker != NULL && lpMoniker->IsEqual(lpFile) == S_OK);
    if (lpMoniker != NULL) lpMoniker->Release();
    CHECK(PictureColor(lpDocObj) == g_colors[1]);
    lpDocObj->Release();
  }

  // The square in it, made when the link asks.
  LPMONIKER lpSquare = ItemMoniker(m_strPath, _T("Square"));
  LPOLEOBJECT lpItemObj = NULL;
  hr = BindItem(m_strPath, _T("Square"), IID_IOleObject, (void**)&lpItemObj);
  TestLog("bind item: %08lx, items made %d", (unsigned long)hr, g_nItemsMade);
  CHECK(SUCCEEDED(hr) && lpItemObj != NULL && g_nItemsMade == 1);
  COleServerItem* pItem = pDoc->FindItem(_T("Square"));
  CHECK(pItem != NULL && pItem->IsLinkedItem() && pItem->IsConnected());
  if (lpItemObj == NULL || pItem == NULL) {
    if (lpItemObj != NULL) lpItemObj->Release();
    lpFile->Release();
    lpSquare->Release();
    pDoc->OnCloseDocument();
    return;
  }
  CHECK(SameObject(lpItemObj, pItem->GetInterface(&IID_IUnknown)));
  CHECK(!SameObject(lpItemObj, pDoc->GetInterface(&IID_IUnknown)));
  // Running, the item is in the running object table under its moniker,
  // where the composite finds it.
  {
    LPRUNNINGOBJECTTABLE lpROT = NULL;
    ::GetRunningObjectTable(0, &lpROT);
    CHECK(lpROT != NULL && lpROT->IsRunning(lpSquare) == S_OK);
    if (lpROT != NULL) lpROT->Release();
    LPUNKNOWN lpUnk = NULL;
    hr = ::BindMoniker(lpSquare, 0, IID_IOleObject, (void**)&lpUnk);
    TestLog("bind running item: %08lx", (unsigned long)hr);
    CHECK(SUCCEEDED(hr) && SameObject(lpUnk, lpItemObj));
    if (lpUnk != NULL) lpUnk->Release();
  }
  SIZEL size = {0, 0};
  CHECK(lpItemObj->GetExtent(DVASPECT_CONTENT, &size) == S_OK);
  TestLog("item extent %ld x %ld", size.cx, size.cy);
  CHECK(size.cx == 1000 && size.cy == 1000);
  CLSID clsid = CLSID_NULL;
  CHECK(lpItemObj->GetUserClassID(&clsid) == S_OK && ::IsEqualCLSID(clsid, CLSID_LinkDoc));
  LPMONIKER lpMoniker = NULL;
  CHECK(lpItemObj->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJFULL, &lpMoniker) == S_OK);
  CString strName = DisplayName(lpMoniker);
  TestLog("item moniker: %s", (const char*)CStringA(strName));
  CHECK(lpMoniker != NULL && lpMoniker->IsEqual(lpSquare) == S_OK);
  CHECK(strName.Right(7) == _T("!Square"));
  if (lpMoniker != NULL) lpMoniker->Release();
  lpMoniker = NULL;
  CHECK(lpItemObj->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJREL, &lpMoniker) == S_OK);
  CHECK(DisplayName(lpMoniker) == _T("!Square"));
  if (lpMoniker != NULL) lpMoniker->Release();
  CHECK(PictureColor(lpItemObj) == g_colors[1]);

  // The link hears of changes and saves.
  CSink sink;
  DWORD dwOle = 0, dwData = 0;
  CHECK(lpItemObj->Advise(&sink, &dwOle) == S_OK);
  LPDATAOBJECT lpData = NULL;
  CHECK(SUCCEEDED(lpItemObj->QueryInterface(IID_IDataObject, (void**)&lpData)));
  if (lpData != NULL) {
    FORMATETC fe = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
    CHECK(lpData->DAdvise(&fe, ADVF_NODATA, &sink, &dwData) == S_OK);
  }
  pDoc->Recolor();
  TestLog("data changes %d", sink.m_nData);
  CHECK(sink.m_nData == 1);
  CHECK(PictureColor(lpItemObj) == g_colors[2]);
  CHECK(pDoc->DoFileSave());
  TestLog("saves %d", sink.m_nSave);
  CHECK(sink.m_nSave == 1);
  CString strNew = m_strPath + _T(".renamed");
  pDoc->NotifyRename(strNew);
  CHECK(sink.m_nRename == 1);

  // The document finds its items for item monikers.
  LPOLEITEMCONTAINER lpCont = NULL;
  hr = ::BindMoniker(lpFile, 0, IID_IOleItemContainer, (void**)&lpCont);
  CHECK(SUCCEEDED(hr) && lpCont != NULL);
  if (lpCont != NULL) {
    CHECK(lpCont->IsRunning(const_cast<LPOLESTR>(L"Square")) == S_OK);
    CHECK(lpCont->IsRunning(const_cast<LPOLESTR>(L"Circle")) == MK_E_NOOBJECT);
    LPUNKNOWN lpUnk = NULL;
    CHECK(lpCont->GetObject(const_cast<LPOLESTR>(L"Circle"), BINDSPEED_INDEFINITE, NULL,
                            IID_IUnknown, (void**)&lpUnk) == MK_E_NOOBJECT);
    hr = lpCont->GetObject(const_cast<LPOLESTR>(L"Square"), BINDSPEED_INDEFINITE, NULL,
                           IID_IUnknown, (void**)&lpUnk);
    CHECK(SUCCEEDED(hr) && SameObject(lpUnk, lpItemObj));
    if (lpUnk != NULL) lpUnk->Release();
    ULONG cchEaten = 0;
    LPMONIKER lpParsed = NULL;
    LPBC lpbc = NULL;
    ::CreateBindCtx(0, &lpbc);
    hr = lpCont->ParseDisplayName(lpbc, const_cast<LPOLESTR>(L"!Square"), &cchEaten, &lpParsed);
    TestLog("parsed %08lx, %lu eaten: %s", (unsigned long)hr, cchEaten,
            (const char*)CStringA(DisplayName(lpParsed)));
    CHECK(hr == S_OK && cchEaten == 7 && DisplayName(lpParsed) == _T("!Square"));
    if (lpParsed != NULL) lpParsed->Release();
    lpParsed = NULL;
    CHECK(lpCont->ParseDisplayName(lpbc, const_cast<LPOLESTR>(L"!Circle"), &cchEaten,
                                   &lpParsed) == MK_E_NOOBJECT);
    if (lpbc != NULL) lpbc->Release();
    lpCont->Release();
  }

  // What Paste Link makes a link from: the item's moniker and class.
  COleDataSource* pSource = new COleDataSource;
  pItem->GetClipboardData(pSource, TRUE);
  COleDataObject data;
  data.Attach((LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject), FALSE);
  CHECK(COleClientItem::CanCreateLinkFromData(&data));
  CLIPFORMAT cfLink = (CLIPFORMAT)::RegisterClipboardFormat(_T("Link Source"));
  CLIPFORMAT cfLinkDesc = (CLIPFORMAT)::RegisterClipboardFormat(_T("Link Source Descriptor"));
  CHECK(data.IsDataAvailable(cfLinkDesc));
  FORMATETC feLink = {cfLink, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
  STGMEDIUM stg = {};
  CHECK(data.GetData(cfLink, &stg, &feLink));
  if (stg.tymed == TYMED_ISTREAM && stg.pstm != NULL) {
    LARGE_INTEGER zero = {};
    stg.pstm->Seek(zero, STREAM_SEEK_SET, NULL);
    LPMONIKER lpLinked = NULL;
    hr = ::OleLoadFromStream(stg.pstm, IID_IMoniker, (void**)&lpLinked);
    TestLog("link source: %08lx %s", (unsigned long)hr,
            (const char*)CStringA(DisplayName(lpLinked)));
    CHECK(SUCCEEDED(hr) && lpLinked != NULL && lpLinked->IsEqual(lpSquare) == S_OK);
    if (lpLinked != NULL) lpLinked->Release();
    clsid = CLSID_NULL;
    CHECK(::ReadClassStm(stg.pstm, &clsid) == S_OK && ::IsEqualCLSID(clsid, CLSID_LinkDoc));
    ::ReleaseStgMedium(&stg);
  }
  data.Release();
  pSource->InternalRelease();
  // Without the link, the copy has no Link Source.
  pSource = new COleDataSource;
  pItem->GetClipboardData(pSource, FALSE);
  data.Attach((LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject), FALSE);
  CHECK(!data.IsDataAvailable(cfLink));
  data.Release();
  pSource->InternalRelease();

  // The link lets go: the item, made for it, goes.
  if (lpData != NULL) {
    lpData->DUnadvise(dwData);
    lpData->Release();
  }
  lpItemObj->Unadvise(dwOle);
  lpItemObj->Release();
  TestLog("items gone %d", g_nItemsGone);
  CHECK(g_nItemsGone == 1 && pDoc->FindItem(_T("Square")) == NULL);
  {
    LPRUNNINGOBJECTTABLE lpROT = NULL;
    ::GetRunningObjectTable(0, &lpROT);
    CHECK(lpROT != NULL && lpROT->IsRunning(lpSquare) == S_FALSE);
    if (lpROT != NULL) lpROT->Release();
  }

  // A link that is held when the document closes hears so, and keeps
  // what it holds until it lets go.
  CSink sink2;
  hr = BindItem(m_strPath, _T("Square"), IID_IOleObject, (void**)&lpItemObj);
  CHECK(SUCCEEDED(hr) && g_nItemsMade == 2);
  if (lpItemObj != NULL) lpItemObj->Advise(&sink2, &dwOle);
  pDoc->OnCloseDocument();
  TestLog("closes %d, documents %d", sink2.m_nClose, g_nDocs);
  CHECK(sink2.m_nClose == 1);
  if (lpItemObj != NULL) lpItemObj->Release();
  CHECK(g_nDocs == 0 && g_nItemsGone == 2);
  CHECK(m_pTemplate->GetFirstDocPosition() == NULL);
  lpFile->Release();
  lpSquare->Release();
}

void CLinkApp::TestClosedDocument() {
  // A link to the item of a file nobody has open: COM finds the file's
  // class, makes a document and loads the file into it.
  LPMONIKER lpSquare = ItemMoniker(m_strPath, _T("Square"));
  LPOLEOBJECT lpItemObj = NULL;
  HRESULT hr = BindItem(m_strPath, _T("Square"), IID_IOleObject, (void**)&lpItemObj);
  TestLog("bind closed: %08lx, documents %d", (unsigned long)hr, g_nDocs);
  CHECK(SUCCEEDED(hr) && lpItemObj != NULL && g_nDocs == 1);
  POSITION pos = m_pTemplate->GetFirstDocPosition();
  CLinkDoc* pDoc = pos != NULL ? (CLinkDoc*)m_pTemplate->GetNextDoc(pos) : NULL;
  CHECK(pDoc != NULL && !pDoc->IsEmbedded() && pDoc->m_dwRegister != 0);
  if (pDoc != NULL) {
    TestLog("loaded %s, color %d", (const char*)CStringA(pDoc->GetPathName()), pDoc->m_nColor);
    CHECK(AfxComparePath(pDoc->GetPathName(), m_strPath));
    // Hidden: the user did not open it.
    POSITION posView = pDoc->GetFirstViewPosition();
    CView* pView = posView != NULL ? pDoc->GetNextView(posView) : NULL;
    CHECK(pView != NULL && !pView->GetParentFrame()->IsWindowVisible());
  }
  CHECK(PictureColor(lpItemObj) == g_colors[2]);  // as saved
  LPMONIKER lpMoniker = NULL;
  if (lpItemObj != NULL)
    lpItemObj->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJFULL, &lpMoniker);
  CHECK(lpMoniker != NULL && lpMoniker->IsEqual(lpSquare) == S_OK);
  if (lpMoniker != NULL) lpMoniker->Release();
  // The last link gone, the document closes.
  if (lpItemObj != NULL) lpItemObj->Release();
  TestLog("after release: documents %d", g_nDocs);
  CHECK(g_nDocs == 0 && m_pTemplate->GetFirstDocPosition() == NULL);
  lpSquare->Release();
}

void CLinkApp::TestEmbeddedObject() {
  // An object embedded in a container's document, which is in a file:
  // its moniker is the file's and its own, and a link to it binds to it.
  CCntrDoc* pCntr = new CCntrDoc;
  CHECK(pCntr->OnSaveDocument(m_strCntrPath));
  pCntr->SetPathName(m_strCntrPath, FALSE);
  CHECK(pCntr->m_dwRegister != 0);
  COleClientItem* pClient = new COleClientItem(pCntr);
  BOOL bCreated = pClient->CreateNewItem(CLSID_LinkDoc, OLERENDER_FORMAT, CF_METAFILEPICT);
  TestLog("CreateNewItem: %d (%08lx), item %lu", bCreated,
          (unsigned long)pClient->GetLastStatus(), pClient->m_dwItemNumber);
  CHECK(bCreated && pClient->m_dwItemNumber != 0);
  if (bCreated) {
    CString strItem;
    strItem.Format(_T("Embedding %lu"), pClient->m_dwItemNumber);
    LPMONIKER lpExpected = ItemMoniker(m_strCntrPath, strItem);
    // The server asks its container for the object's moniker.
    LPMONIKER lpMoniker = NULL;
    HRESULT hr =
        pClient->m_lpObject->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJFULL, &lpMoniker);
    TestLog("embedded moniker %08lx: %s", (unsigned long)hr,
            (const char*)CStringA(DisplayName(lpMoniker)));
    CHECK(hr == S_OK && lpMoniker != NULL && lpMoniker->IsEqual(lpExpected) == S_OK);
    if (lpMoniker != NULL) lpMoniker->Release();
    lpMoniker = NULL;
    CHECK(pClient->GetClientSite()->GetMoniker(OLEGETMONIKER_ONLYIFTHERE, OLEWHICHMK_OBJREL,
                                               &lpMoniker) == S_OK);
    CHECK(DisplayName(lpMoniker) == CString(_T("!")) + strItem);
    if (lpMoniker != NULL) lpMoniker->Release();
    // The server document's own moniker is the container's for it.
    POSITION pos = m_pTemplate->GetFirstDocPosition();
    CLinkDoc* pDoc = pos != NULL ? (CLinkDoc*)m_pTemplate->GetNextDoc(pos) : NULL;
    CHECK(pDoc != NULL && pDoc->IsEmbedded());
    if (pDoc != NULL) {
      lpMoniker = pDoc->GetMoniker(OLEGETMONIKER_ONLYIFTHERE);
      CHECK(lpMoniker != NULL && lpMoniker->IsEqual(lpExpected) == S_OK);
      if (lpMoniker != NULL) lpMoniker->Release();
    }
    // The container hands out its items.
    LPOLECONTAINER lpCont = NULL;
    CHECK(pClient->GetClientSite()->GetContainer(&lpCont) == S_OK && lpCont != NULL);
    if (lpCont != NULL) {
      LPOLEITEMCONTAINER lpItems = NULL;
      CHECK(SUCCEEDED(lpCont->QueryInterface(IID_IOleItemContainer, (void**)&lpItems)));
      if (lpItems != NULL) {
        CStringW strW(strItem);
        CHECK(lpItems->IsRunning(const_cast<LPOLESTR>((LPCWSTR)strW)) == S_OK);
        lpItems->Release();
      }
      lpCont->Release();
    }
    // And a link to the object binds to it.
    LPUNKNOWN lpUnk = NULL;
    hr = BindItem(m_strCntrPath, strItem, IID_IUnknown, (void**)&lpUnk);
    TestLog("bind embedded: %08lx", (unsigned long)hr);
    CHECK(SUCCEEDED(hr) && SameObject(lpUnk, pClient->m_lpObject));
    if (lpUnk != NULL) lpUnk->Release();
    lpExpected->Release();
    pClient->Close(OLECLOSE_NOSAVE);
  }
  pCntr->OnCloseDocument();  // deletes it and its items
  TestLog("documents at the end %d", g_nDocs);
  CHECK(g_nDocs == 0);
}

BOOL CLinkApp::InitInstance() {
  TestOpenLog(_T("linktest.log"));
  if (!AfxOleInit()) {
    TestLog("AfxOleInit failed");
    g_nFailures++;
    return FALSE;
  }
  m_pTemplate = new CMultiDocTemplate(IDR_LINKTYPE, RUNTIME_CLASS(CLinkDoc),
                                      RUNTIME_CLASS(CLinkFrame), RUNTIME_CLASS(CLinkView));
  AddDocTemplate(m_pTemplate);
  m_server.ConnectTemplate(CLSID_LinkDoc, m_pTemplate, FALSE);
  m_server.UpdateRegistry(OAT_SERVER);
  RegisterShellFileTypes();
  CHECK(COleTemplateServer::RegisterAll());

  m_strPath = TestOutPath(_T("square.lkt"));
  m_strCntrPath = TestOutPath(_T("report.lkc"));
  ::DeleteFile(m_strPath);
  CFrameWnd* pFrame = new CFrameWnd;
  pFrame->Create(NULL, _T("LinkTest"));
  m_pMainWnd = pFrame;

  TestOpenDocument();
  TestRegistry();
  TestClosedDocument();
  TestEmbeddedObject();

  pFrame->DestroyWindow();
  m_pMainWnd = NULL;
  return FALSE;  // done: ExitInstance reports
}

int CLinkApp::ExitInstance() {
  UnregisterShellFileTypes();
  CHECK(RegValue(_T(".lkt")) == _T("<none>"));
  CHECK(RegValue(_T("LinkTest.Document")) == _T("<none>"));
  m_server.Unregister();
  ::DeleteFile(m_strPath);
  ::DeleteFile(m_strCntrPath);
  CWinApp::ExitInstance();
  return TestFinish();
}
