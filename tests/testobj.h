// testobj.h - an OLE object class for the container tests: it draws a
// rectangle in its color, saves the color in its storage, and its verb 1
// changes the color. RegisterTestObject makes the class available in the
// process and gives it the registry entries OLE reads (names, verbs).
#pragma once

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

  // An enhanced metafile of the rectangle, as rich edit controls draw
  // objects from their data.
  HENHMETAFILE MakeEnhMetafile() {
    RECT rcFrame = {0, 0, m_size.cx, m_size.cy};
    HDC hDC = ::CreateEnhMetaFile(NULL, NULL, &rcFrame, NULL);
    // The frame is in HIMETRIC; draw in the matching pixels.
    HDC hScreen = ::GetDC(NULL);
    RECTL rcl = {0, 0, ::MulDiv(m_size.cx, ::GetDeviceCaps(hScreen, LOGPIXELSX), 2540),
                 ::MulDiv(m_size.cy, ::GetDeviceCaps(hScreen, LOGPIXELSY), 2540)};
    ::ReleaseDC(NULL, hScreen);
    DrawTo(hDC, &rcl);
    return ::CloseEnhMetaFile(hDC);
  }

  BEGIN_INTERFACE_PART(DataObject, IDataObject)
    STDMETHOD(GetData)(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium);
    STDMETHOD(GetDataHere)(LPFORMATETC, LPSTGMEDIUM) { return E_NOTIMPL; }
    STDMETHOD(QueryGetData)(LPFORMATETC pformatetc) {
      return pformatetc->cfFormat == CF_ENHMETAFILE && (pformatetc->tymed & TYMED_ENHMF) &&
                     pformatetc->dwAspect == DVASPECT_CONTENT
                 ? S_OK
                 : DV_E_FORMATETC;
    }
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC, LPFORMATETC pformatetcOut) {
      pformatetcOut->ptd = NULL;
      return DATA_S_SAMEFORMATETC;
    }
    STDMETHOD(SetData)(LPFORMATETC, LPSTGMEDIUM, BOOL) { return E_NOTIMPL; }
    STDMETHOD(EnumFormatEtc)(DWORD, LPENUMFORMATETC*) { return E_NOTIMPL; }
    STDMETHOD(DAdvise)(LPFORMATETC, DWORD, LPADVISESINK, DWORD*) {
      return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHOD(DUnadvise)(DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
    STDMETHOD(EnumDAdvise)(LPENUMSTATDATA*) { return OLE_E_ADVISENOTSUPPORTED; }
  END_INTERFACE_PART(DataObject)

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
  INTERFACE_PART(CTestObject, IID_IDataObject, DataObject)
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
TEST_IUNKNOWN(DataObject)

STDMETHODIMP CTestObject::XDataObject::GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE(CTestObject, DataObject)
  if (QueryGetData(pformatetcIn) != S_OK) return DV_E_FORMATETC;
  pmedium->tymed = TYMED_ENHMF;
  pmedium->hEnhMetaFile = pThis->MakeEnhMetafile();
  pmedium->pUnkForRelease = NULL;
  return S_OK;
}

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

