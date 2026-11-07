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

// {6D3A1C52-8B0E-4F3A-9C51-3E2B7A90D101}
static const CLSID CLSID_TestObject = {
    0x6d3a1c52, 0x8b0e, 0x4f3a, {0x9c, 0x51, 0x3e, 0x2b, 0x7a, 0x90, 0xd1, 0x01}};

static const COLORREF g_colors[] = {RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255)};

// ---------------------------------------------------------------------
// The object: draws a rectangle in its color; verb 1 changes the color.

class CTestObject : public CCmdTarget {
  DECLARE_DYNCREATE(CTestObject)
public:
  static int s_nAlive;
  static CStringW s_strHost, s_strHostObj;
  static LONG s_lLastVerb;

  int m_nColor = 0;
  SIZEL m_size = {2540, 1270};
  BOOL m_bDirty = FALSE;
  BOOL m_bOpen = FALSE;
  LPOLECLIENTSITE m_pSite = NULL;
  LPSTORAGE m_pStorage = NULL;
  LPOLEADVISEHOLDER m_pAdviseHolder = NULL;
  LPADVISESINK m_pViewSink = NULL;

  CTestObject() {
    s_nAlive++;
    ::CreateOleAdviseHolder(&m_pAdviseHolder);
  }
  virtual ~CTestObject() {
    if (m_pSite != NULL) m_pSite->Release();
    if (m_pStorage != NULL) m_pStorage->Release();
    if (m_pViewSink != NULL) m_pViewSink->Release();
    if (m_pAdviseHolder != NULL) m_pAdviseHolder->Release();
    s_nAlive--;
  }

  void Recolor() {
    m_nColor = (m_nColor + 1) % 3;
    m_bDirty = TRUE;
    if (m_pViewSink != NULL) m_pViewSink->OnViewChange(DVASPECT_CONTENT, -1);
    if (m_pSite != NULL) m_pSite->SaveObject();
  }

  void DrawTo(HDC hDC, const RECTL* prcl) {
    RECT rc = {prcl->left, prcl->top, prcl->right, prcl->bottom};
    HBRUSH hbr = ::CreateSolidBrush(g_colors[m_nColor]);
    ::FillRect(hDC, &rc, hbr);
    ::DeleteObject(hbr);
  }

  HRESULT Write(LPSTORAGE pStg) {
    LPSTREAM pStm = NULL;
    HRESULT hr = pStg->CreateStream(L"TestData", STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                    0, 0, &pStm);
    if (FAILED(hr)) return hr;
    LONG data[3] = {m_nColor, m_size.cx, m_size.cy};
    hr = pStm->Write(data, sizeof(data), NULL);
    pStm->Release();
    return hr;
  }

  HRESULT Read(LPSTORAGE pStg) {
    LPSTREAM pStm = NULL;
    HRESULT hr = pStg->OpenStream(L"TestData", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &pStm);
    if (FAILED(hr)) return hr;
    LONG data[3];
    ULONG n = 0;
    hr = pStm->Read(data, sizeof(data), &n);
    pStm->Release();
    if (FAILED(hr) || n != sizeof(data)) return STG_E_READFAULT;
    m_nColor = (int)data[0];
    m_size.cx = data[1];
    m_size.cy = data[2];
    return S_OK;
  }

  void SetStorage(LPSTORAGE pStg) {
    if (pStg != NULL) pStg->AddRef();
    if (m_pStorage != NULL) m_pStorage->Release();
    m_pStorage = pStg;
  }

  BEGIN_INTERFACE_PART(OleObject, IOleObject)
    STDMETHOD(SetClientSite)(LPOLECLIENTSITE pClientSite);
    STDMETHOD(GetClientSite)(LPOLECLIENTSITE* ppClientSite);
    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj);
    STDMETHOD(Close)(DWORD dwSaveOption);
    STDMETHOD(SetMoniker)(DWORD, LPMONIKER) { return S_OK; }
    STDMETHOD(GetMoniker)(DWORD, DWORD, LPMONIKER* ppmk) {
      *ppmk = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(InitFromData)(LPDATAOBJECT, BOOL, DWORD) { return E_NOTIMPL; }
    STDMETHOD(GetClipboardData)(DWORD, LPDATAOBJECT* ppDataObject) {
      *ppDataObject = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, LPOLECLIENTSITE pActiveSite, LONG lindex,
                      HWND hwndParent, LPCRECT lprcPosRect);
    STDMETHOD(EnumVerbs)(LPENUMOLEVERB* ppEnumOleVerb) {
      return ::OleRegEnumVerbs(CLSID_TestObject, ppEnumOleVerb);
    }
    STDMETHOD(Update)() { return S_OK; }
    STDMETHOD(IsUpToDate)() { return S_OK; }
    STDMETHOD(GetUserClassID)(CLSID* pClsid) {
      *pClsid = CLSID_TestObject;
      return S_OK;
    }
    STDMETHOD(GetUserType)(DWORD, LPOLESTR* pszUserType) {
      *pszUserType = NULL;
      return OLE_S_USEREG;
    }
    STDMETHOD(SetExtent)(DWORD dwDrawAspect, SIZEL* psizel);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, SIZEL* psizel);
    STDMETHOD(Advise)(LPADVISESINK pAdvSink, DWORD* pdwConnection);
    STDMETHOD(Unadvise)(DWORD dwConnection);
    STDMETHOD(EnumAdvise)(LPENUMSTATDATA* ppenumAdvise);
    STDMETHOD(GetMiscStatus)(DWORD, DWORD* pdwStatus) {
      *pdwStatus = 0;
      return S_OK;
    }
    STDMETHOD(SetColorScheme)(LOGPALETTE*) { return E_NOTIMPL; }
  END_INTERFACE_PART(OleObject)

  BEGIN_INTERFACE_PART(PersistStorage, IPersistStorage)
    STDMETHOD(GetClassID)(CLSID* pClassID) {
      *pClassID = CLSID_TestObject;
      return S_OK;
    }
    STDMETHOD(IsDirty)();
    STDMETHOD(InitNew)(LPSTORAGE pStg);
    STDMETHOD(Load)(LPSTORAGE pStg);
    STDMETHOD(Save)(LPSTORAGE pStgSave, BOOL fSameAsLoad);
    STDMETHOD(SaveCompleted)(LPSTORAGE pStgNew);
    STDMETHOD(HandsOffStorage)();
  END_INTERFACE_PART(PersistStorage)

  BEGIN_INTERFACE_PART(ViewObject, IViewObject2)
    STDMETHOD(Draw)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd,
                    HDC hdcTargetDev, HDC hdcDraw, LPCRECTL lprcBounds, LPCRECTL lprcWBounds,
                    BOOL(STDMETHODCALLTYPE* pfnContinue)(ULONG_PTR dwContinue),
                    ULONG_PTR dwContinue);
    STDMETHOD(GetColorSet)(DWORD, LONG, void*, DVTARGETDEVICE*, HDC, LOGPALETTE**) {
      return E_NOTIMPL;
    }
    STDMETHOD(Freeze)(DWORD, LONG, void*, DWORD*) { return E_NOTIMPL; }
    STDMETHOD(Unfreeze)(DWORD) { return E_NOTIMPL; }
    STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, IAdviseSink* pAdvSink);
    STDMETHOD(GetAdvise)(DWORD* pAspects, DWORD* pAdvf, IAdviseSink** ppAdvSink);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LONG lindex, DVTARGETDEVICE* ptd, LPSIZEL lpsizel);
  END_INTERFACE_PART(ViewObject)

  DECLARE_INTERFACE_MAP()
};
IMPLEMENT_DYNCREATE(CTestObject, CCmdTarget)
int CTestObject::s_nAlive = 0;
CStringW CTestObject::s_strHost, CTestObject::s_strHostObj;
LONG CTestObject::s_lLastVerb = 999;

BEGIN_INTERFACE_MAP(CTestObject, CCmdTarget)
  INTERFACE_PART(CTestObject, IID_IOleObject, OleObject)
  INTERFACE_PART(CTestObject, IID_IPersistStorage, PersistStorage)
  INTERFACE_PART(CTestObject, IID_IPersist, PersistStorage)
  INTERFACE_PART(CTestObject, IID_IViewObject, ViewObject)
  INTERFACE_PART(CTestObject, IID_IViewObject2, ViewObject)
END_INTERFACE_MAP()

#define TEST_IUNKNOWN(iface)                                                   \
  STDMETHODIMP_(ULONG) CTestObject::X##iface::AddRef() {                       \
    METHOD_PROLOGUE(CTestObject, iface)                                        \
    return pThis->ExternalAddRef();                                            \
  }                                                                            \
  STDMETHODIMP_(ULONG) CTestObject::X##iface::Release() {                      \
    METHOD_PROLOGUE(CTestObject, iface)                                        \
    return pThis->ExternalRelease();                                           \
  }                                                                            \
  STDMETHODIMP CTestObject::X##iface::QueryInterface(REFIID iid, void** ppv) { \
    METHOD_PROLOGUE(CTestObject, iface)                                        \
    return pThis->ExternalQueryInterface(&iid, ppv);                           \
  }
TEST_IUNKNOWN(OleObject)
TEST_IUNKNOWN(PersistStorage)
TEST_IUNKNOWN(ViewObject)

STDMETHODIMP CTestObject::XOleObject::SetClientSite(LPOLECLIENTSITE pClientSite) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  if (pClientSite != NULL) pClientSite->AddRef();
  if (pThis->m_pSite != NULL) pThis->m_pSite->Release();
  pThis->m_pSite = pClientSite;
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::GetClientSite(LPOLECLIENTSITE* ppClientSite) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  *ppClientSite = pThis->m_pSite;
  if (pThis->m_pSite != NULL) pThis->m_pSite->AddRef();
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::SetHostNames(LPCOLESTR szContainerApp,
                                                  LPCOLESTR szContainerObj) {
  s_strHost = szContainerApp;
  s_strHostObj = szContainerObj;
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::Close(DWORD dwSaveOption) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  if (pThis->m_bDirty && dwSaveOption == OLECLOSE_SAVEIFDIRTY && pThis->m_pSite != NULL)
    pThis->m_pSite->SaveObject();
  if (pThis->m_bOpen) {
    pThis->m_bOpen = FALSE;
    if (pThis->m_pSite != NULL) pThis->m_pSite->OnShowWindow(FALSE);
    pThis->m_pAdviseHolder->SendOnClose();
  }
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::DoVerb(LONG iVerb, LPMSG, LPOLECLIENTSITE, LONG, HWND,
                                            LPCRECT) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  s_lLastVerb = iVerb;
  switch (iVerb) {
  case OLEIVERB_PRIMARY:
  case OLEIVERB_SHOW:
  case OLEIVERB_OPEN:
    if (!pThis->m_bOpen) {
      pThis->m_bOpen = TRUE;
      if (pThis->m_pSite != NULL) {
        pThis->m_pSite->ShowObject();
        pThis->m_pSite->OnShowWindow(TRUE);
      }
    }
    return S_OK;
  case OLEIVERB_HIDE:
    if (pThis->m_bOpen) {
      pThis->m_bOpen = FALSE;
      if (pThis->m_pSite != NULL) pThis->m_pSite->OnShowWindow(FALSE);
    }
    return S_OK;
  case 1:
    pThis->Recolor();
    return S_OK;
  default:
    return OLEOBJ_E_INVALIDVERB;
  }
}

STDMETHODIMP CTestObject::XOleObject::SetExtent(DWORD, SIZEL* psizel) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  pThis->m_size = *psizel;
  pThis->m_bDirty = TRUE;
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::GetExtent(DWORD, SIZEL* psizel) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  *psizel = pThis->m_size;
  return S_OK;
}

STDMETHODIMP CTestObject::XOleObject::Advise(LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  return pThis->m_pAdviseHolder->Advise(pAdvSink, pdwConnection);
}

STDMETHODIMP CTestObject::XOleObject::Unadvise(DWORD dwConnection) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  return pThis->m_pAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP CTestObject::XOleObject::EnumAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE(CTestObject, OleObject)
  return pThis->m_pAdviseHolder->EnumAdvise(ppenumAdvise);
}

STDMETHODIMP CTestObject::XPersistStorage::IsDirty() {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  return pThis->m_bDirty ? S_OK : S_FALSE;
}

STDMETHODIMP CTestObject::XPersistStorage::InitNew(LPSTORAGE pStg) {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  pThis->SetStorage(pStg);
  return S_OK;
}

STDMETHODIMP CTestObject::XPersistStorage::Load(LPSTORAGE pStg) {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  HRESULT hr = pThis->Read(pStg);
  if (SUCCEEDED(hr)) pThis->SetStorage(pStg);
  return hr;
}

STDMETHODIMP CTestObject::XPersistStorage::Save(LPSTORAGE pStgSave, BOOL) {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  return pThis->Write(pStgSave);
}

STDMETHODIMP CTestObject::XPersistStorage::SaveCompleted(LPSTORAGE pStgNew) {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  if (pStgNew != NULL) pThis->SetStorage(pStgNew);
  pThis->m_bDirty = FALSE;
  pThis->m_pAdviseHolder->SendOnSave();
  return S_OK;
}

STDMETHODIMP CTestObject::XPersistStorage::HandsOffStorage() {
  METHOD_PROLOGUE(CTestObject, PersistStorage)
  pThis->SetStorage(NULL);
  return S_OK;
}

STDMETHODIMP CTestObject::XViewObject::Draw(DWORD, LONG, void*, DVTARGETDEVICE*, HDC, HDC hdcDraw,
                                           LPCRECTL lprcBounds, LPCRECTL,
                                           BOOL(STDMETHODCALLTYPE*)(ULONG_PTR), ULONG_PTR) {
  METHOD_PROLOGUE(CTestObject, ViewObject)
  if (lprcBounds == NULL) return E_INVALIDARG;
  pThis->DrawTo(hdcDraw, lprcBounds);
  return S_OK;
}

STDMETHODIMP CTestObject::XViewObject::SetAdvise(DWORD, DWORD, IAdviseSink* pAdvSink) {
  METHOD_PROLOGUE(CTestObject, ViewObject)
  if (pAdvSink != NULL) pAdvSink->AddRef();
  if (pThis->m_pViewSink != NULL) pThis->m_pViewSink->Release();
  pThis->m_pViewSink = pAdvSink;
  return S_OK;
}

STDMETHODIMP CTestObject::XViewObject::GetAdvise(DWORD* pAspects, DWORD* pAdvf,
                                                IAdviseSink** ppAdvSink) {
  METHOD_PROLOGUE(CTestObject, ViewObject)
  if (pAspects != NULL) *pAspects = DVASPECT_CONTENT;
  if (pAdvf != NULL) *pAdvf = 0;
  if (ppAdvSink != NULL) {
    *ppAdvSink = pThis->m_pViewSink;
    if (pThis->m_pViewSink != NULL) pThis->m_pViewSink->AddRef();
  }
  return S_OK;
}

STDMETHODIMP CTestObject::XViewObject::GetExtent(DWORD, LONG, DVTARGETDEVICE*, LPSIZEL lpsizel) {
  METHOD_PROLOGUE(CTestObject, ViewObject)
  *lpsizel = pThis->m_size;
  return S_OK;
}

static COleObjectFactory g_factory(CLSID_TestObject, RUNTIME_CLASS(CTestObject), FALSE,
                                   _T("MfcTest.Object"));
static DWORD g_dwRegister = 0;

// The registry entries OLE reads for the object: its names and verbs.
static const TCHAR g_szClsidKey[] = _T("CLSID\\{6D3A1C52-8B0E-4F3A-9C51-3E2B7A90D101}");

static void SetKey(LPCTSTR lpszSub, LPCTSTR lpszValue) {
  CString strKey = g_szClsidKey;
  if (lpszSub != NULL) strKey = strKey + _T("\\") + lpszSub;
  HKEY hKey;
  if (::RegCreateKey(HKEY_CLASSES_ROOT, strKey, &hKey) == ERROR_SUCCESS) {
    ::RegSetValue(hKey, NULL, REG_SZ, lpszValue, lstrlen(lpszValue) * sizeof(TCHAR));
    ::RegCloseKey(hKey);
  }
}

static void DeleteKeyTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKey(hParent, lpszKey, &hKey) != ERROR_SUCCESS) return;
  TCHAR szSub[256];
  while (::RegEnumKey(hKey, 0, szSub, 256) == ERROR_SUCCESS) DeleteKeyTree(hKey, szSub);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

static void RegisterTestObject() {
  SetKey(NULL, _T("MfcTest Object"));
  SetKey(_T("AuxUserType\\2"), _T("TestObj"));
  SetKey(_T("Verb\\0"), _T("&Edit,0,2"));
  SetKey(_T("Verb\\1"), _T("&Recolor,0,2"));
  SetKey(_T("ProgID"), _T("MfcTest.Object"));
  ::CoRegisterClassObject(CLSID_TestObject, (LPUNKNOWN)g_factory.GetInterface(&IID_IUnknown),
                          CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &g_dwRegister);
}

static void UnregisterTestObject() {
  if (g_dwRegister != 0) ::CoRevokeClassObject(g_dwRegister);
  g_dwRegister = 0;
  DeleteKeyTree(HKEY_CLASSES_ROOT, g_szClsidKey);
}

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
