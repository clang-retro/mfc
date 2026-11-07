// oletest.cpp - OLE containers with afxole.h: COleClientItems embedded in
// a COleDocument, drawn by its view, opened and changed through their
// verbs, saved to flat and compound files and loaded again, copied
// through the clipboard (embedded and static) and cloned.
//
// The embedded objects come from a small object class written here and
// registered in the same process, so no other program is needed.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include "oletest.h"
#include "testutil.h"
#include "testobj.h"

// ---------------------------------------------------------------------
// The container

class CTestItem : public COleClientItem {
  DECLARE_SERIAL(CTestItem)
public:
  CRect m_rect;
  int m_nChanges = 0, m_nStateChanges = 0, m_nCloses = 0, m_nSaves = 0;

  CTestItem(COleDocument* pDoc = NULL) : COleClientItem(pDoc), m_rect(20, 20, 120, 70) {}

  virtual void OnChange(OLE_NOTIFICATION nCode, DWORD dwParam) {
    COleClientItem::OnChange(nCode, dwParam);
    switch (nCode) {
    case OLE_CHANGED: m_nChanges++; break;
    case OLE_CHANGED_STATE: m_nStateChanges++; break;
    case OLE_CLOSED: m_nCloses++; break;
    case OLE_SAVED: m_nSaves++; break;
    default: break;
    }
    if (GetDocument() != NULL) GetDocument()->UpdateAllViews(NULL);
  }
  virtual void OnGetItemPosition(CRect& rPosition) { rPosition = m_rect; }
  virtual void Serialize(CArchive& ar) {
    COleClientItem::Serialize(ar);
    if (ar.IsStoring())
      ar << m_rect.left << m_rect.top << m_rect.right << m_rect.bottom;
    else
      ar >> m_rect.left >> m_rect.top >> m_rect.right >> m_rect.bottom;
  }
  // The object behind the item, when it is one of ours.
  CTestObject* Object() const {
    if (m_lpObject == NULL) return NULL;
    LPPERSISTSTORAGE p = NULL;
    if (FAILED(m_lpObject->QueryInterface(IID_IPersistStorage, (void**)&p))) return NULL;
    CLSID clsid = CLSID_NULL;
    p->GetClassID(&clsid);
    CTestObject* pObject = NULL;
    // Our objects are in this process: find the C++ object behind the
    // interface.
    if (IsEqualCLSID(clsid, CLSID_TestObject))
      pObject = (CTestObject*)((BYTE*)p - offsetof(CTestObject, m_xPersistStorage));
    p->Release();
    return pObject;
  }
};
IMPLEMENT_SERIAL(CTestItem, COleClientItem, 0)

class COleTestDoc : public COleDocument {
  DECLARE_DYNCREATE(COleTestDoc)
public:
  CString m_strNote = _T("note");
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring())
      ar << m_strNote;
    else
      ar >> m_strNote;
    COleDocument::Serialize(ar);
  }
  int CountItems() {
    int n = 0;
    POSITION pos = GetStartPosition();
    while (pos != NULL)
      if (GetNextClientItem(pos) != NULL) n++;
    return n;
  }
  CTestItem* Item(int i) {
    POSITION pos = GetStartPosition();
    while (pos != NULL) {
      COleClientItem* pItem = GetNextClientItem(pos);
      if (pItem != NULL && i-- == 0) return (CTestItem*)pItem;
    }
    return NULL;
  }
  afx_msg void OnUpdateVerb(CCmdUI* pCmdUI) { OnUpdateObjectVerbMenu(pCmdUI); }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(COleTestDoc, COleDocument)
BEGIN_MESSAGE_MAP(COleTestDoc, COleDocument)
  ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdatePasteMenu)
  ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE_LINK, OnUpdatePasteLinkMenu)
  ON_UPDATE_COMMAND_UI(ID_OLE_EDIT_LINKS, OnUpdateEditLinksMenu)
  ON_UPDATE_COMMAND_UI(ID_OLE_VERB_FIRST, OnUpdateVerb)
END_MESSAGE_MAP()

class COleTestView : public CScrollView {
  DECLARE_DYNCREATE(COleTestView)
public:
  CTestItem* m_pSelection = NULL;
  virtual void OnInitialUpdate() {
    CScrollView::OnInitialUpdate();
    SetScrollSizes(MM_TEXT, CSize(800, 600));
    m_pSelection = NULL;
  }
  COleTestDoc* GetDocument() const { return (COleTestDoc*)m_pDocument; }
  virtual void OnDraw(CDC* pDC) {
    POSITION pos = GetDocument()->GetStartPosition();
    while (pos != NULL) {
      CTestItem* pItem = (CTestItem*)GetDocument()->GetNextClientItem(pos);
      if (pItem != NULL) pItem->Draw(pDC, pItem->m_rect);
    }
  }
  virtual BOOL IsSelected(const CObject* pDocItem) const { return pDocItem == m_pSelection; }
  virtual void OnUpdate(CView*, LPARAM, CObject*) { Invalidate(); }
};
IMPLEMENT_DYNCREATE(COleTestView, CScrollView)

// The color on screen at a point of the view.
static COLORREF PixelAt(CWnd* pWnd, CPoint pt) {
  pWnd->UpdateWindow();
  CClientDC dc(pWnd);
  return dc.GetPixel(pt);
}

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep = 0;
  BOOL m_bBusy = FALSE;

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  COleTestView* View() { return (COleTestView*)GetActiveView(); }
  COleTestDoc* Doc() { return (COleTestDoc*)GetActiveDocument(); }

  void TestEmbed() {
    COleTestDoc* pDoc = Doc();
    COleTestView* pView = View();
    CTestItem* pItem = new CTestItem(pDoc);
    CHECK(pItem->GetDocument() == pDoc);
    CHECK(pItem->GetItemState() == COleClientItem::emptyState);
    BOOL bCreated = pItem->CreateNewItem(CLSID_TestObject);
    TestLog("CreateNewItem: %d, sc %08lx", bCreated, (unsigned long)pItem->GetLastStatus());
    CHECK(bCreated);
    if (!bCreated) {
      pItem->Delete();
      return;
    }
    CHECK(CTestObject::s_nAlive == 1);
    CHECK(pItem->GetType() == OT_EMBEDDED);
    CHECK(pItem->GetItemState() == COleClientItem::loadedState);
    CHECK(pItem->Object() != NULL);
    CHECK(CTestObject::s_strHost == CStringW(AfxGetAppName()));
    CHECK(CTestObject::s_strHostObj == CStringW(pDoc->GetTitle()));
    CSize size;
    CHECK(pItem->GetExtent(&size) && size == CSize(2540, 1270));
    CHECK(pItem->GetCachedExtent(&size) && size == CSize(2540, 1270));
    CString str;
    pItem->GetUserType(USERCLASSTYPE_FULL, str);
    TestLog("user type: %s", (const char*)CStringA(str));
    CHECK(str == _T("MfcTest Object"));
    pItem->GetUserType(USERCLASSTYPE_SHORT, str);
    CHECK(str == _T("TestObj"));
    CLSID clsid;
    pItem->GetClassID(&clsid);
    CHECK(IsEqualCLSID(clsid, CLSID_TestObject));
    CHECK(!pItem->IsModified());
    CHECK(pDoc->CountItems() == 1);

    // Drawn by the view through its IViewObject.
    pDoc->UpdateAllViews(NULL);
    CHECK(PixelAt(pView, CPoint(70, 45)) == g_colors[0]);

    // Opening shows the object's window: the item is open until it closes.
    pView->m_pSelection = pItem;
    CHECK(pItem->DoVerb(OLEIVERB_OPEN, pView));
    CHECK(CTestObject::s_lLastVerb == OLEIVERB_OPEN);
    CHECK(pItem->IsOpen() && pItem->GetItemState() == COleClientItem::openState);
    CHECK(pItem->GetActiveView() == pView);
    CHECK(pItem->m_nStateChanges == 1);
    pItem->Close();
    CHECK(pItem->GetItemState() == COleClientItem::loadedState);
    CHECK(pItem->m_nCloses == 1);
    CHECK(pItem->m_nStateChanges == 2);

    // The verb menu, from the registry.
    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_OLE_VERB_FIRST, _T("&Object"));
    AfxOleSetEditMenu(pItem, &menu, 0, ID_OLE_VERB_FIRST, ID_OLE_VERB_LAST, ID_OLE_EDIT_CONVERT);
    CMenu* pVerbs = menu.GetSubMenu(0);
    CString strItem;
    menu.GetMenuString(0, strItem, MF_BYPOSITION);
    TestLog("verb menu: \"%s\", popup %d", (const char*)CStringA(strItem), pVerbs != NULL);
    CHECK(pVerbs != NULL);
    BOOL bRecolor = FALSE;
    if (pVerbs != NULL) {
      for (int i = 0; i < (int)pVerbs->GetMenuItemCount(); i++) {
        pVerbs->GetMenuString(i, strItem, MF_BYPOSITION);
        UINT nID = pVerbs->GetMenuItemID(i);
        TestLog("  %u \"%s\"", nID, (const char*)CStringA(strItem));
        if (strItem == _T("&Recolor")) bRecolor = nID == ID_OLE_VERB_FIRST + 1;
      }
    }
    CHECK(bRecolor);
    // Without an item the menu item is disabled.
    CMenu menu2;
    menu2.CreatePopupMenu();
    menu2.AppendMenu(MF_STRING, ID_OLE_VERB_FIRST, _T("&Object"));
    AfxOleSetEditMenu(NULL, &menu2, 0, ID_OLE_VERB_FIRST);
    CHECK((menu2.GetMenuState(0, MF_BYPOSITION) & MF_GRAYED) != 0);

    // A verb command goes to the selected item: the object changes its
    // color, saves itself through the client site and redraws.
    pDoc->SetModifiedFlag(FALSE);
    SendMessage(WM_COMMAND, ID_OLE_VERB_FIRST + 1);
    CHECK(CTestObject::s_lLastVerb == 1);
    CHECK(pItem->Object() != NULL && pItem->Object()->m_nColor == 1);
    CHECK(pItem->m_nChanges >= 1);
    CHECK(pDoc->IsModified());
    CHECK(PixelAt(pView, CPoint(70, 45)) == g_colors[1]);
    TestSaveWindowBitmap(m_hWnd, _T("oletest-embed.bmp"));

    // The Edit menu commands are enabled as they should be.
    CHECK(!COleClientItem::CanPasteLink());
  }

  void TestFlatFile() {
    COleTestDoc* pDoc = Doc();
    CString strPath = TestOutPath(_T("oletest-flat.olt"));
    pDoc->m_strNote = _T("flat");
    CTestItem* pItem = pDoc->Item(0);
    if (pItem == NULL) return;
    pItem->m_rect = CRect(30, 40, 130, 90);
    CHECK(pDoc->OnSaveDocument(strPath));
    CHECK(!pDoc->IsModified());
    // A flat file is an ordinary one, not a compound file.
    CStringW strPathW(strPath);
    CHECK(::StgIsStorageFile(strPathW) == S_FALSE);

    pDoc->m_strNote.Empty();
    CHECK(pDoc->OnOpenDocument(strPath));
    CHECK(CTestObject::s_nAlive == 1);
    CHECK(pDoc->m_strNote == _T("flat"));
    CHECK(pDoc->CountItems() == 1);
    pItem = pDoc->Item(0);
    if (pItem == NULL) return;
    CHECK(pItem->m_rect == CRect(30, 40, 130, 90));
    CHECK(pItem->GetType() == OT_EMBEDDED);
    CHECK(pItem->Object() != NULL && pItem->Object()->m_nColor == 1);
    CHECK(pItem->m_dwItemNumber == 1);
    pDoc->UpdateAllViews(NULL);
    CHECK(PixelAt(View(), CPoint(80, 65)) == g_colors[1]);
  }

  void TestCompoundFile() {
    COleTestDoc* pDoc = Doc();
    pDoc->EnableCompoundFile();
    CString strPath = TestOutPath(_T("oletest-compound.olt"));
    CStringW strPathW(strPath);
    pDoc->m_strNote = _T("compound");
    CHECK(pDoc->OnSaveDocument(strPath));
    pDoc->SetPathName(strPath, FALSE);
    CHECK(::StgIsStorageFile(strPathW) == S_OK);
    // The document's stream and the object's storage are in the file.
    LPSTORAGE pStg = NULL;
    HRESULT hr = ::StgOpenStorage(strPathW, NULL, STGM_READ | STGM_SHARE_DENY_NONE | STGM_TRANSACTED,
                                  NULL, 0, &pStg);
    TestLog("StgOpenStorage: %08lx", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
      LPSTORAGE pSub = NULL;
      CHECK(pStg->OpenStorage(L"Embedding 1", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0,
                              &pSub) == S_OK);
      if (pSub != NULL) pSub->Release();
      LPSTREAM pStm = NULL;
      CHECK(pStg->OpenStream(L"Contents", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &pStm) == S_OK);
      if (pStm != NULL) pStm->Release();
      pStg->Release();
    }

    pDoc->m_strNote.Empty();
    CHECK(pDoc->OnOpenDocument(strPath));
    CHECK(pDoc->m_strNote == _T("compound"));
    CHECK(pDoc->CountItems() == 1);
    CTestItem* pItem = pDoc->Item(0);
    if (pItem == NULL) return;
    CHECK(pItem->Object() != NULL && pItem->Object()->m_nColor == 1);
    CHECK(pItem->m_lpLockBytes == NULL);

    // A second item, changed and saved into the same file.
    CTestItem* pItem2 = new CTestItem(pDoc);
    CHECK(pItem2->CreateNewItem(CLSID_TestObject));
    CHECK(pItem2->m_dwItemNumber == 2);
    pItem2->m_rect = CRect(200, 20, 300, 70);
    CHECK(pItem2->DoVerb(1, View()));
    CHECK(pItem2->DoVerb(1, View()));
    CHECK(pDoc->OnSaveDocument(strPath));
    CHECK(pDoc->OnOpenDocument(strPath));
    CHECK(pDoc->CountItems() == 2);
    pItem2 = pDoc->Item(1);
    CHECK(pItem2 != NULL && pItem2->Object() != NULL && pItem2->Object()->m_nColor == 2);
    pDoc->UpdateAllViews(NULL);
    CHECK(PixelAt(View(), CPoint(250, 45)) == g_colors[2]);
    TestSaveWindowBitmap(m_hWnd, _T("oletest-compound.bmp"));

    // Deleting an item removes its storage when the file is saved.
    pItem = pDoc->Item(0);
    if (pItem != NULL) pItem->Delete();
    CHECK(CTestObject::s_nAlive == 1);
    CHECK(pDoc->IsModified());
    CHECK(pDoc->OnSaveDocument(strPath));
    CHECK(pDoc->OnOpenDocument(strPath));
    CHECK(pDoc->CountItems() == 1);
    hr = ::StgOpenStorage(strPathW, NULL, STGM_READ | STGM_SHARE_DENY_NONE | STGM_TRANSACTED, NULL,
                          0, &pStg);
    if (SUCCEEDED(hr)) {
      LPSTORAGE pSub = NULL;
      CHECK(pStg->OpenStorage(L"Embedding 1", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0,
                              &pSub) != S_OK);
      if (pSub != NULL) pSub->Release();
      pStg->Release();
    }

    // Save As moves the items into the new file.
    CString strPath2 = TestOutPath(_T("oletest-copy.olt"));
    CHECK(pDoc->OnSaveDocument(strPath2));
    pDoc->SetPathName(strPath2, FALSE);
    pItem = pDoc->Item(0);
    CHECK(pItem != NULL && pItem->DoVerb(1, View()));
    CHECK(pDoc->OnSaveDocument(strPath2));
    CHECK(pDoc->OnOpenDocument(strPath2));
    pItem = pDoc->Item(0);
    CHECK(pItem != NULL && pItem->Object() != NULL && pItem->Object()->m_nColor == 0);
    CHECK(pDoc->OnOpenDocument(strPath));
    pItem = pDoc->Item(0);
    CHECK(pItem != NULL && pItem->Object() != NULL && pItem->Object()->m_nColor == 2);
  }

  void TestClipboard() {
    COleTestDoc* pDoc = Doc();
    CTestItem* pItem = pDoc->Item(0);
    if (pItem == NULL) return;
    pItem->CopyToClipboard();
    COleDataObject data;
    CHECK(data.AttachClipboard());
    CHECK(data.IsDataAvailable((CLIPFORMAT)::RegisterClipboardFormat(_T("Embedded Object"))));
    CHECK(data.IsDataAvailable((CLIPFORMAT)::RegisterClipboardFormat(_T("Object Descriptor"))));
    CHECK(data.IsDataAvailable(CF_METAFILEPICT));
    HRESULT hrQuery = ::OleQueryCreateFromData(data.m_lpDataObject);
    TestLog("OleQueryCreateFromData: %08lx", (unsigned long)hrQuery);
    CHECK(COleClientItem::CanCreateFromData(&data));
    data.Release();
    CHECK(COleClientItem::CanPaste());

    // Pasted, the object is a copy of its own.
    CTestItem* pPasted = new CTestItem(pDoc);
    pPasted->m_rect = CRect(20, 100, 120, 150);
    BOOL bPasted = pPasted->CreateFromClipboard();
    TestLog("CreateFromClipboard: %d, sc %08lx", bPasted, (unsigned long)pPasted->GetLastStatus());
    CHECK(bPasted);
    if (bPasted) {
      CHECK(pPasted->GetType() == OT_EMBEDDED);
      CHECK(pPasted->Object() != NULL && pPasted->Object() != pItem->Object());
      CHECK(pPasted->Object() != NULL && pPasted->Object()->m_nColor == 2);
      CHECK(CTestObject::s_nAlive == 2);
    }

    // A static copy is a picture of it.
    CTestItem* pStatic = new CTestItem(pDoc);
    pStatic->m_rect = CRect(150, 100, 250, 150);
    // From a picture alone, as when pasting from a drawing program.
    COleDataSource picture;
    {
      HDC hMetaDC = ::CreateMetaFile(NULL);
      ::SetMapMode(hMetaDC, MM_ANISOTROPIC);
      ::SetWindowOrgEx(hMetaDC, 0, 0, NULL);
      ::SetWindowExtEx(hMetaDC, 100, 50, NULL);
      RECT rc = {0, 0, 100, 50};
      HBRUSH hbr = ::CreateSolidBrush(g_colors[2]);
      ::FillRect(hMetaDC, &rc, hbr);
      ::DeleteObject(hbr);
      HGLOBAL hPict = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(METAFILEPICT));
      METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(hPict);
      pmfp->mm = MM_ANISOTROPIC;
      pmfp->xExt = 2540;
      pmfp->yExt = 1270;
      pmfp->hMF = ::CloseMetaFile(hMetaDC);
      ::GlobalUnlock(hPict);
      STGMEDIUM medium;
      medium.tymed = TYMED_MFPICT;
      medium.hMetaFilePict = hPict;
      medium.pUnkForRelease = NULL;
      picture.CacheData(CF_METAFILEPICT, &medium);
    }
    COleDataObject pictureData;
    pictureData.Attach((LPDATAOBJECT)picture.GetInterface(&IID_IDataObject), FALSE);
    CHECK(COleClientItem::CanCreateFromData(&pictureData));
    BOOL bStatic = pStatic->CreateStaticFromData(&pictureData);
    TestLog("CreateStaticFromData: %d, sc %08lx", bStatic,
            (unsigned long)pStatic->GetLastStatus());
    if (!bStatic) {
      // Wine makes static objects without IOleObject.
      TestLog("no static objects here");
      CHECK(pStatic->GetLastStatus() == E_NOINTERFACE);
      pStatic->Delete();
    } else {
      CLSID clsid;
      pStatic->GetClassID(&clsid);
      DWORD dwStatus = 0;
      pStatic->m_lpObject->GetMiscStatus(DVASPECT_CONTENT, &dwStatus);
      TestLog("static: type %d, clsid %08lx, status %08lx", pStatic->GetType(),
              (unsigned long)clsid.Data1, (unsigned long)dwStatus);
      CHECK(pStatic->GetType() == OT_STATIC);
    }

    // A clone has its own storage.
    CTestItem* pClone = new CTestItem(pDoc);
    pClone->m_rect = CRect(280, 100, 380, 150);
    CHECK(pClone->CreateCloneFrom(pItem));
    CHECK(pClone->Object() != NULL && pClone->Object()->m_nColor == 2);
    CHECK(pClone->m_dwItemNumber != pItem->m_dwItemNumber);
    pDoc->UpdateAllViews(NULL);
    CHECK(PixelAt(View(), CPoint(70, 125)) == g_colors[2]);
    if (bStatic) CHECK(PixelAt(View(), CPoint(200, 125)) == g_colors[2]);
    CHECK(PixelAt(View(), CPoint(330, 125)) == g_colors[2]);
    TestSaveWindowBitmap(m_hWnd, _T("oletest-clipboard.bmp"));
    ::OleSetClipboard(NULL);

    // All of them survive saving and loading.
    CString strPath = TestOutPath(_T("oletest-all.olt"));
    BOOL bSaved = pDoc->OnSaveDocument(strPath);
    CHECK(bSaved);
    pDoc->SetPathName(strPath, FALSE);
    BOOL bOpened = pDoc->OnOpenDocument(strPath);
    CHECK(bOpened);
    CHECK(pDoc->CountItems() == (bPasted ? 3 : 2) + (bStatic ? 1 : 0));
    pDoc->UpdateAllViews(NULL);
    if (bStatic) CHECK(PixelAt(View(), CPoint(200, 125)) == g_colors[2]);
    CHECK(PixelAt(View(), CPoint(330, 125)) == g_colors[2]);
  }

  void TestRelease() {
    COleTestDoc* pDoc = Doc();
    pDoc->DeleteContents();
    CHECK(pDoc->CountItems() == 0);
    CHECK(CTestObject::s_nAlive == 0);
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    if (m_bBusy) return;
    m_bBusy = TRUE;
    switch (m_nStep++) {
    case 0:
      TestEmbed();
      break;
    case 1:
      TestFlatFile();
      break;
    case 2:
      TestCompoundFile();
      break;
    case 3:
      TestClipboard();
      break;
    case 4:
      TestRelease();
      break;
    default:
      KillTimer(1);
      Doc()->SetModifiedFlag(FALSE);
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

class COleTestApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("oletest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    RegisterTestObject();
    CSingleDocTemplate* pTemplate =
        new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(COleTestDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(COleTestView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    UnregisterTestObject();
    CWinApp::ExitInstance();
    CHECK(CTestObject::s_nAlive == 0);
    return TestFinish();
  }
};

COleTestApp theApp;
