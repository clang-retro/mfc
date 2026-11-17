// ctltest.cpp - writing an ActiveX control with COleControl. The test
// program is both the control's server and its container:
//   - the control (CTestCtrl) has stock and custom properties, a method,
//     stock and custom events, an OnDraw and a DoPropExchange, and is
//     made through its DECLARE_OLECREATE_EX factory;
//   - a frame window hosts it with CWnd::CreateControl and checks the
//     properties, events, ambient properties, activation and drawing;
//   - the control's state goes through IPersistStreamInit and
//     IPersistPropertyBag into new instances;
//   - the factory's UpdateRegistry writes and removes the registry keys.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxctl.h>
#include <afxocc.h>
#include <olectl.h>
#include "ctltest.h"
#include "testutil.h"

// The control's own members and events.
enum { DISPID_VALUE_ = 1, DISPID_LABEL = 2, DISPID_TWICE = 3, DISPID_BUMP = 4, DISPID_PICTURE_ = 5 };
enum { eventidChanged = 1 };

static const DWORD kMiscStatus =
    OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST | OLEMISC_INSIDEOUT |
    OLEMISC_CANTLINKINSIDE | OLEMISC_RECOMPOSEONRESIZE;

// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F61}
static const IID IID_DTestCtrl = {
    0x6a1d3f52, 0x0e4b, 0x4d7a, {0x9f, 0x23, 0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x61}};
// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F62}
static const IID IID_DTestCtrlEvents = {
    0x6a1d3f52, 0x0e4b, 0x4d7a, {0x9f, 0x23, 0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x62}};
// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F63}
static const GUID kTypeLib = {
    0x6a1d3f52, 0x0e4b, 0x4d7a, {0x9f, 0x23, 0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x63}};

// =====================================================================
// The control

class CTestCtrl : public COleControl {
  DECLARE_DYNCREATE(CTestCtrl)
public:
  static int s_nAlive;
  static CTestCtrl* s_pLast;
  // For the drawing and mouse tests: flags added to GetControlFlags's
  // and taken from them, a drawing past the bounds, and what was seen.
  static DWORD s_dwAddFlags, s_dwClearFlags;
  static BOOL s_bDrawOutside, s_bOptimizedSeen;
  static int s_nInactiveMoves;

  long m_nValue;
  CString m_strLabel;
  int m_nDraws;
  int m_nBackChanges;
  int m_nAmbientChanges;
  int m_nValueChanges;
  DISPID m_dispidAmbient;

  CTestCtrl()
      : m_nValue(7), m_nOpenVerbs(0), m_nDraws(0), m_nBackChanges(0), m_nAmbientChanges(0), m_nValueChanges(0),
        m_dispidAmbient(0) {
    InitializeIIDs(&IID_DTestCtrl, &IID_DTestCtrlEvents);
    s_nAlive++;
    s_pLast = this;
  }
  virtual ~CTestCtrl() {
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }

  virtual DWORD GetControlFlags() {
    return (COleControl::GetControlFlags() | s_dwAddFlags) & ~s_dwClearFlags;
  }
  virtual DWORD GetActivationPolicy() { return POINTERINACTIVE_ACTIVATEONENTRY; }
  virtual void OnInactiveMouseMove(LPCRECT, long, long, DWORD) { s_nInactiveMoves++; }
  virtual BOOL OnInactiveSetCursor(LPCRECT, long, long, DWORD, BOOL) { return TRUE; }

  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    m_nDraws++;
    if (s_bDrawOutside) {
      s_bOptimizedSeen = IsOptimizedDraw();
      CRect rc(rcBounds);
      rc.InflateRect(10, 10);
      pDC->FillSolidRect(rc, RGB(255, 0, 0));
      return;
    }
    CBrush brush(TranslateColor(GetBackColor()));
    pDC->FillRect(rcBounds, &brush);
    CFont* pOld = SelectStockFont(pDC);
    pDC->SetTextColor(TranslateColor(GetForeColor()));
    pDC->SetBkMode(TRANSPARENT);
    CString str;
    str.Format(_T("%s: %ld"), (LPCTSTR)InternalGetText(), m_nValue);
    CRect rc(rcBounds);
    pDC->DrawText(str, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (pOld != NULL) pDC->SelectObject(pOld);
  }

  virtual void DoPropExchange(CPropExchange* pPX) {
    ExchangeVersion(pPX, MAKELONG(1, 1));
    COleControl::DoPropExchange(pPX);
    PX_Long(pPX, _T("Value"), m_nValue, 7);
    PX_String(pPX, _T("Label"), m_strLabel, _T("none"));
    PX_Picture(pPX, _T("Picture"), m_pict);
  }
  virtual void OnResetState() {
    COleControl::OnResetState();
  }
  virtual void OnBackColorChanged() {
    m_nBackChanges++;
    COleControl::OnBackColorChanged();
  }
  virtual void OnAmbientPropertyChange(DISPID dispid) {
    m_nAmbientChanges++;
    m_dispidAmbient = dispid;
  }

  long GetValue() { return m_nValue; }
  void SetValue(long n) {
    if (!BoundPropertyRequestEdit(DISPID_VALUE_)) {
      SetNotPermitted();
      return;
    }
    m_nValue = n;
    m_nValueChanges++;
    SetModifiedFlag();
    BoundPropertyChanged(DISPID_VALUE_);
    InvalidateControl();
  }
  // A picture property, which the stock picture page sets.
  CPictureHolder m_pict;
  LPPICTUREDISP GetPicture() { return m_pict.GetPictureDispatch(); }
  void SetPicture(LPPICTUREDISP pDisp) {
    m_pict.SetPictureDispatch(pDisp);
    SetModifiedFlag();
    InvalidateControl();
  }
  long Twice(long n) { return 2 * n; }
  void Bump() {
    m_nValue++;
    FireChanged(m_nValue);
  }
  void FireChanged(long n) { FireEvent(eventidChanged, EVENT_PARAM(VTS_I4), n); }

  // Verbs.
  int m_nOpenVerbs;
  BOOL OnResetVerb(LPMSG, HWND, LPCRECT) {
    m_nValue = 0;
    return TRUE;
  }
  BOOL OnBumpVerb(LPMSG, HWND, LPCRECT) {
    Bump();
    return TRUE;
  }
  BOOL OnOpenVerb(LPMSG, HWND hWndParent, LPCRECT lpRect) {
    m_nOpenVerbs++;
    return hWndParent != NULL && lpRect != NULL;
  }
  DECLARE_MESSAGE_MAP()

  // Property browsing.
  virtual BOOL OnGetDisplayString(DISPID dispid, CString& strValue) {
    if (dispid != DISPID_VALUE_) return COleControl::OnGetDisplayString(dispid, strValue);
    strValue.Format(_T("Value %ld"), m_nValue);
    return TRUE;
  }
  virtual BOOL OnGetPredefinedStrings(DISPID dispid, CStringArray* pStrings,
                                      CDWordArray* pCookies) {
    if (dispid != DISPID_BORDERSTYLE) return FALSE;
    pStrings->Add(_T("0 - None"));
    pCookies->Add(0);
    pStrings->Add(_T("1 - Fixed Single"));
    pCookies->Add(1);
    return TRUE;
  }
  virtual BOOL OnGetPredefinedValue(DISPID dispid, DWORD dwCookie, VARIANT* pvar) {
    if (dispid != DISPID_BORDERSTYLE || dwCookie > 1) return FALSE;
    pvar->vt = VT_I2;
    pvar->iVal = (short)dwCookie;
    return TRUE;
  }

  DECLARE_OLECREATE_EX(CTestCtrl)
  DECLARE_OLECTLTYPE(CTestCtrl)
  DECLARE_PROPPAGEIDS(CTestCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};

int CTestCtrl::s_nAlive = 0;
CTestCtrl* CTestCtrl::s_pLast = NULL;
DWORD CTestCtrl::s_dwAddFlags = 0;
DWORD CTestCtrl::s_dwClearFlags = 0;
BOOL CTestCtrl::s_bDrawOutside = FALSE;
BOOL CTestCtrl::s_bOptimizedSeen = FALSE;
int CTestCtrl::s_nInactiveMoves = 0;

IMPLEMENT_DYNCREATE(CTestCtrl, COleControl)

BEGIN_MESSAGE_MAP(CTestCtrl, COleControl)
  ON_OLEVERB(IDS_VERB_RESET, OnResetVerb)
  ON_OLEVERB(IDS_VERB_BUMP, OnBumpVerb)
  ON_STDOLEVERB(OLEIVERB_OPEN, OnOpenVerb)
END_MESSAGE_MAP()

// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F60}
IMPLEMENT_OLECREATE_EX(CTestCtrl, "CtlTest.TestCtrl.1", 0x6a1d3f52, 0x0e4b, 0x4d7a, 0x9f, 0x23,
                       0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x60)
IMPLEMENT_OLECTLTYPE(CTestCtrl, IDS_TESTCTL, kMiscStatus)

// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F64}
static const CLSID CLSID_TestPage = {
    0x6a1d3f52, 0x0e4b, 0x4d7a, {0x9f, 0x23, 0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x64}};

BEGIN_PROPPAGEIDS(CTestCtrl, 2)
  PROPPAGEID(CLSID_TestPage)
  PROPPAGEID(CLSID_CColorPropPage)
END_PROPPAGEIDS(CTestCtrl)

BEGIN_DISPATCH_MAP(CTestCtrl, COleControl)
  DISP_PROPERTY_EX_ID(CTestCtrl, "Value", DISPID_VALUE_, GetValue, SetValue, VT_I4)
  DISP_PROPERTY_ID(CTestCtrl, "Label", DISPID_LABEL, m_strLabel, VT_BSTR)
  DISP_PROPERTY_EX_ID(CTestCtrl, "Picture", DISPID_PICTURE_, GetPicture, SetPicture, VT_PICTURE)
  DISP_FUNCTION_ID(CTestCtrl, "Twice", DISPID_TWICE, Twice, VT_I4, VTS_I4)
  DISP_FUNCTION_ID(CTestCtrl, "Bump", DISPID_BUMP, Bump, VT_EMPTY, VTS_NONE)
  DISP_STOCKPROP_BACKCOLOR()
  DISP_STOCKPROP_FORECOLOR()
  DISP_STOCKPROP_CAPTION()
  DISP_STOCKPROP_ENABLED()
  DISP_STOCKPROP_FONT()
  DISP_STOCKPROP_BORDERSTYLE()
  DISP_STOCKFUNC_REFRESH()
  DISP_STOCKFUNC_DOCLICK()
END_DISPATCH_MAP()

BEGIN_EVENT_MAP(CTestCtrl, COleControl)
  EVENT_CUSTOM_ID("Changed", eventidChanged, FireChanged, VTS_I4)
  EVENT_STOCK_CLICK()
  EVENT_STOCK_KEYDOWN()
END_EVENT_MAP()

BOOL CTestCtrl::CTestCtrlFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister)
    return AfxOleRegisterControlClass(AfxGetInstanceHandle(), m_clsid, m_lpszProgID, IDS_TESTCTL,
                                      IDB_TESTCTL, afxRegApartmentThreading, kMiscStatus, kTypeLib,
                                      1, 0);
  return AfxOleUnregisterClass(m_clsid, m_lpszProgID);
}

// =====================================================================
// The control's property page

class CTestPropPage : public COlePropertyPage {
  DECLARE_DYNCREATE(CTestPropPage)
public:
  static int s_nAlive;
  static CTestPropPage* s_pLast;
  long m_nValue;
  CString m_strLabel;
  int m_nEnabled;
  int m_nBorder;
  int m_nObjectsChanged;
  CTestPropPage()
      : COlePropertyPage(IDD_TESTPAGE, IDS_TESTPAGE), m_nValue(0), m_nEnabled(0), m_nBorder(0),
        m_nObjectsChanged(0) {
    s_nAlive++;
    s_pLast = this;
  }
  static BOOL s_bDirtyAtEnd;
  virtual ~CTestPropPage() {
    s_bDirtyAtEnd = IsModified();
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }
  virtual void OnObjectsChanged() { m_nObjectsChanged++; }
  virtual void DoDataExchange(CDataExchange* pDX) {
    DDP_Text(pDX, IDC_VALUE, m_nValue, _T("Value"));
    DDX_Text(pDX, IDC_VALUE, m_nValue);
    DDP_Text(pDX, IDC_LABEL, m_strLabel, _T("Label"));
    DDX_Text(pDX, IDC_LABEL, m_strLabel);
    DDP_Check(pDX, IDC_ENABLED, m_nEnabled, _T("Enabled"));
    DDX_Check(pDX, IDC_ENABLED, m_nEnabled);
    DDP_Radio(pDX, IDC_BORDER0, m_nBorder, _T("BorderStyle"));
    DDX_Radio(pDX, IDC_BORDER0, m_nBorder);
    DDP_PostProcessing(pDX);
  }
  DECLARE_OLECREATE_EX(CTestPropPage)
};

// A licensed control: its factory takes a license file beside the
// program, or the run-time key a licensed copy hands out.
static const WCHAR kLicText[] = L"CtlTest license: this copy may be used to build programs.";
static const WCHAR kLicKey[] = L"CtlTest run-time key";

class CLicCtrl : public CTestCtrl {
  DECLARE_DYNCREATE(CLicCtrl)
  BEGIN_OLEFACTORY(CLicCtrl)
    virtual BOOL VerifyUserLicense() {
      return AfxVerifyLicFile(AfxGetInstanceHandle(), _T("ctltest.lic"), kLicText);
    }
    virtual BOOL GetLicenseKey(DWORD, BSTR* pbstrKey) {
      *pbstrKey = ::SysAllocString(kLicKey);
      return *pbstrKey != NULL;
    }
  END_OLEFACTORY(CLicCtrl)
};

IMPLEMENT_DYNCREATE(CLicCtrl, CTestCtrl)
// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F65}
IMPLEMENT_OLECREATE_EX(CLicCtrl, "CtlTest.LicCtrl.1", 0x6a1d3f52, 0x0e4b, 0x4d7a, 0x9f, 0x23, 0x8b,
                       0x5e, 0x1c, 0xad, 0x4f, 0x65)

// The test makes it without the registry.
BOOL CLicCtrl::CLicCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

// Downloads, read at once or while the program runs; a control with a
// data path property.
class CTestAsyncFile : public CAsyncMonikerFile {
public:
  BOOL m_bStarted, m_bStopped, m_bLast;
  HRESULT m_hrStop;
  int m_nData;
  CStringA m_strData;
  CTestAsyncFile() : m_bStarted(FALSE), m_bStopped(FALSE), m_bLast(FALSE), m_hrStop(E_FAIL), m_nData(0) {}

protected:
  virtual void OnStartBinding() { m_bStarted = GetBinding() != NULL; }
  virtual void OnDataAvailable(DWORD dwSize, DWORD bscfFlag) {
    m_nData++;
    if (bscfFlag & BSCF_LASTDATANOTIFICATION) m_bLast = TRUE;
    // Through the CFile side this time.
    char buf[1024];
    while ((DWORD)m_strData.GetLength() < dwSize) {
      UINT n = 0;
      try {
        n = Read(buf, sizeof(buf));
      } catch (CException* e) {
        e->Delete();  // E_PENDING: more to come
        break;
      }
      if (n == 0) break;
      m_strData += CStringA(buf, n);
    }
  }
  virtual void OnStopBinding(HRESULT hr, LPCTSTR) {
    m_hrStop = hr;
    m_bStopped = TRUE;
  }
};

// An object that aggregates a file's bind status callback, as an override
// of CreateBindStatusCallback that adds interfaces of its own would.
class COuterBSC : public IUnknown {
public:
  LONG m_cRef;
  IUnknown* m_pInner;
  int m_nBSCQueries;
  BOOL* m_pbFreed;
  explicit COuterBSC(BOOL* pbFreed) : m_cRef(1), m_pInner(NULL), m_nBSCQueries(0), m_pbFreed(pbFreed) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    if (::IsEqualIID(iid, IID_IBindStatusCallback)) m_nBSCQueries++;
    if (m_pInner != NULL) return m_pInner->QueryInterface(iid, ppv);
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_cRef; }
  STDMETHOD_(ULONG, Release)() {
    LONG n = --m_cRef;
    if (n == 0) {
      if (m_pInner != NULL) m_pInner->Release();
      *m_pbFreed = TRUE;
      delete this;
    }
    return (ULONG)n;
  }
};

class CAggAsyncFile : public CTestAsyncFile {
public:
  COuterBSC* m_pOuter;
  BOOL m_bOuterFreed;
  CAggAsyncFile() : m_pOuter(NULL), m_bOuterFreed(FALSE) {}

protected:
  virtual IUnknown* CreateBindStatusCallback(IUnknown*) {
    m_pOuter = new COuterBSC(&m_bOuterFreed);
    m_pOuter->m_pInner = CAsyncMonikerFile::CreateBindStatusCallback(m_pOuter);
    return m_pOuter;
  }
};

class CTestDataProp : public CCachedDataPathProperty {
public:
  BOOL m_bStopped;
  HRESULT m_hrStop;
  CTestDataProp() : m_bStopped(FALSE), m_hrStop(E_FAIL) {}
  virtual void ResetData() {
    CCachedDataPathProperty::ResetData();
    m_bStopped = FALSE;
  }

protected:
  virtual void OnStopBinding(HRESULT hr, LPCTSTR) {
    m_hrStop = hr;
    m_bStopped = TRUE;
    // A control usually says so through its ready state.
    if (GetControl() != NULL) GetControl()->InternalSetReadyState(READYSTATE_COMPLETE);
  }
};

class CDataCtrl : public CTestCtrl {
  DECLARE_DYNCREATE(CDataCtrl)
public:
  static CDataCtrl* s_pLast;
  CTestDataProp m_data;
  CDataCtrl() {
    m_data.SetControl(this);
    s_pLast = this;
  }
  virtual void DoPropExchange(CPropExchange* pPX) {
    CTestCtrl::DoPropExchange(pPX);
    PX_DataPath(pPX, _T("Data"), m_data);
  }
  DECLARE_OLECREATE_EX(CDataCtrl)
};

CDataCtrl* CDataCtrl::s_pLast = NULL;
IMPLEMENT_DYNCREATE(CDataCtrl, CTestCtrl)
// {6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F66}
IMPLEMENT_OLECREATE_EX(CDataCtrl, "CtlTest.DataCtrl.1", 0x6a1d3f52, 0x0e4b, 0x4d7a, 0x9f, 0x23,
                       0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x66)
BOOL CDataCtrl::CDataCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

int CTestPropPage::s_nAlive = 0;
CTestPropPage* CTestPropPage::s_pLast = NULL;
BOOL CTestPropPage::s_bDirtyAtEnd = FALSE;

IMPLEMENT_DYNCREATE(CTestPropPage, COlePropertyPage)
IMPLEMENT_OLECREATE_EX(CTestPropPage, "CtlTest.TestPropPage.1", 0x6a1d3f52, 0x0e4b, 0x4d7a, 0x9f,
                       0x23, 0x8b, 0x5e, 0x1c, 0xad, 0x4f, 0x64)

BOOL CTestPropPage::CTestPropPageFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister)
    return AfxOleRegisterPropertyPageClass(AfxGetInstanceHandle(), m_clsid, IDS_TESTPAGE_TYPE);
  return AfxOleUnregisterClass(m_clsid, NULL);
}

// The frame's side of a page.
class CTestPageSite : public IPropertyPageSite {
public:
  int m_nDirty;
  int m_nClean;
  CTestPageSite() : m_nDirty(0), m_nClean(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IPropertyPageSite) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(OnStatusChange)(DWORD dwFlags) {
    if (dwFlags & PROPPAGESTATUS_DIRTY) m_nDirty++;
    if (dwFlags & PROPPAGESTATUS_CLEAN) m_nClean++;
    return S_OK;
  }
  STDMETHOD(GetLocaleID)(LCID* pLocaleID) {
    *pLocaleID = LOCALE_USER_DEFAULT;
    return S_OK;
  }
  STDMETHOD(GetPageContainer)(IUnknown** ppUnk) {
    *ppUnk = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(TranslateAccelerator)(MSG*) { return S_FALSE; }
};

// =====================================================================
// A bare container for IQuickActivate: a client site and the sinks.

class CQuickSite : public IOleClientSite {
public:
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IOleClientSite) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(SaveObject)() { return S_OK; }
  STDMETHOD(GetMoniker)(DWORD, DWORD, IMoniker** ppmk) {
    *ppmk = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetContainer)(IOleContainer** ppContainer) {
    *ppContainer = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD(ShowObject)() { return S_OK; }
  STDMETHOD(OnShowWindow)(BOOL) { return S_OK; }
  STDMETHOD(RequestNewObjectLayout)() { return E_NOTIMPL; }
};

class CQuickEvents : public IDispatch {
public:
  int m_nEvents;
  DISPID m_dispidLast;
  CQuickEvents() : m_nEvents(0), m_dispidLast(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_DTestCtrlEvents) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) {
    *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    m_nEvents++;
    m_dispidLast = dispid;
    return S_OK;
  }
};

class CQuickNotify : public IPropertyNotifySink {
public:
  int m_nChanged;
  DISPID m_dispidLast;
  CQuickNotify() : m_nChanged(0), m_dispidLast(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IPropertyNotifySink) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(OnChanged)(DISPID dispid) {
    m_nChanged++;
    m_dispidLast = dispid;
    return S_OK;
  }
  STDMETHOD(OnRequestEdit)(DISPID) { return S_OK; }
};

// =====================================================================
// A property bag in memory

class CTestPropBag : public IPropertyBag {
public:
  CStringArray m_names;
  CArray<COleVariant, const COleVariant&> m_values;

  int Find(LPCOLESTR psz) const {
    CString str(psz);
    for (int i = 0; i < m_names.GetSize(); i++)
      if (m_names[i] == str) return i;
    return -1;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IPropertyBag) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(Read)(LPCOLESTR pszPropName, VARIANT* pVar, IErrorLog*) {
    int i = Find(pszPropName);
    if (i < 0) return E_INVALIDARG;
    VARTYPE vt = pVar->vt;
    ::VariantInit(pVar);
    HRESULT hr = ::VariantCopy(pVar, (LPVARIANT)&m_values[i]);
    if (SUCCEEDED(hr) && vt != VT_EMPTY && vt != pVar->vt && vt != VT_UNKNOWN)
      hr = ::VariantChangeType(pVar, pVar, 0, vt);
    return hr;
  }
  STDMETHOD(Write)(LPCOLESTR pszPropName, VARIANT* pVar) {
    int i = Find(pszPropName);
    if (i < 0) {
      m_names.Add(CString(pszPropName));
      m_values.Add(COleVariant(*pVar));
    } else {
      m_values[i] = *pVar;
    }
    return S_OK;
  }
};

// =====================================================================
// The container

class CMainFrame : public CFrameWnd {
public:
  CWnd m_ctl;
  int m_nClicks;
  int m_nChanged;
  long m_nLastChanged;
  int m_nRequests;
  int m_nChanges;
  BOOL m_bVeto;
  OLE_COLOR m_clrAmbientBack;

  CMainFrame()
      : m_nClicks(0), m_nChanged(0), m_nLastChanged(0), m_nRequests(0), m_nChanges(0),
        m_bVeto(FALSE), m_clrAmbientBack(RGB(10, 20, 30)) {}

  virtual BOOL OnAmbientProperty(COleControlSite* pSite, DISPID dispid, VARIANT* pvar) {
    if (dispid == DISPID_AMBIENT_BACKCOLOR) {
      pvar->vt = VT_I4;
      pvar->lVal = (long)m_clrAmbientBack;
      return TRUE;
    }
    if (dispid == DISPID_AMBIENT_DISPLAYNAME) {
      pvar->vt = VT_BSTR;
      pvar->bstrVal = ::SysAllocString(L"Ctl1");
      return TRUE;
    }
    return CFrameWnd::OnAmbientProperty(pSite, dispid, pvar);
  }

  void OnClick() { m_nClicks++; }
  void OnChanged(long n) {
    m_nChanged++;
    m_nLastChanged = n;
  }
  BOOL OnValueRequest(BOOL* pbAllow) {
    m_nRequests++;
    if (m_bVeto) *pbAllow = FALSE;
    return TRUE;
  }
  BOOL OnValueChanged() {
    m_nChanges++;
    return TRUE;
  }

  void RunTests();
  void TestHosted();
  void TestPersistence();
  void TestRegistry();
  void TestPropertyPage();
  void TestPropertyFrame();
  void TestViewAndPointer();
  void TestPictureHolder();
  void TestStockPages();
  void TestBrowsingAndQuickActivate();
  void TestLicensing();
  void TestDataPath();

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 100, NULL);
    return 0;
  }
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    RunTests();
    PostMessage(WM_CLOSE);
  }
  DECLARE_MESSAGE_MAP()
  DECLARE_EVENTSINK_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

BEGIN_EVENTSINK_MAP(CMainFrame, CFrameWnd)
  ON_EVENT(CMainFrame, IDC_CTL, DISPID_CLICK, OnClick, VTS_NONE)
  ON_EVENT(CMainFrame, IDC_CTL, eventidChanged, OnChanged, VTS_I4)
  ON_PROPNOTIFY(CMainFrame, IDC_CTL, DISPID_VALUE_, OnValueRequest, OnValueChanged)
END_EVENTSINK_MAP()

static void PumpMessages() {
  MSG msg;
  while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

void CMainFrame::TestHosted() {
  TestLog("hosted control");
  CRect rect(10, 10, 210, 70);
  BOOL bMade = m_ctl.CreateControl(CTestCtrl::guid, _T("Hello"), WS_VISIBLE | WS_TABSTOP, rect,
                                   this, IDC_CTL);
  CHECK(bMade);
  CTestCtrl* pCtl = CTestCtrl::s_pLast;
  CHECK(pCtl != NULL && CTestCtrl::s_nAlive == 1);
  if (!bMade || pCtl == NULL) return;

  // The control's window is the wrapper's, in place in the frame.
  CHECK(pCtl->m_hWnd != NULL && m_ctl.m_hWnd == pCtl->m_hWnd);
  CHECK(::GetParent(pCtl->m_hWnd) == m_hWnd);
  CHECK(pCtl->IsInPlaceActive());
  CHECK(pCtl->GetClientSite() != NULL);
  CHECK(m_ctl.GetDlgCtrlID() == IDC_CTL);
  CRect rectCtl;
  m_ctl.GetWindowRect(&rectCtl);
  ScreenToClient(&rectCtl);
  CHECK(rectCtl == rect);
  int cx = 0, cy = 0;
  pCtl->GetControlSize(&cx, &cy);
  CHECK(cx == 200 && cy == 60);

  // It started new, with its defaults.
  CHECK(pCtl->m_nValue == 7 && pCtl->m_strLabel == _T("none"));
  CHECK(pCtl->IsInitialized());

  // The interfaces of a control.
  LPUNKNOWN pUnk = m_ctl.GetControlUnknown();
  CHECK(pUnk != NULL);
  if (pUnk == NULL) return;
  static const IID* const kIIDs[] = {
      &IID_IOleObject, &IID_IOleInPlaceObject, &IID_IOleInPlaceActiveObject, &IID_IOleControl,
      &IID_IViewObject, &IID_IViewObject2, &IID_IPersistStreamInit, &IID_IPersistStorage,
      &IID_IPersistPropertyBag, &IID_IProvideClassInfo, &IID_IProvideClassInfo2,
      &IID_ISpecifyPropertyPages, &IID_IConnectionPointContainer, &IID_IDispatch};
  for (int i = 0; i < _countof(kIIDs); i++) {
    LPUNKNOWN p = NULL;
    HRESULT hr = pUnk->QueryInterface(*kIIDs[i], (void**)&p);
    if (FAILED(hr)) TestLog("no interface %d", i);
    CHECK(SUCCEEDED(hr) && p != NULL);
    if (p != NULL) p->Release();
  }
  // The primary dispatch IID is the control's IDispatch too.
  LPDISPATCH pDisp = NULL;
  CHECK(pUnk->QueryInterface(IID_DTestCtrl, (void**)&pDisp) == S_OK);
  if (pDisp != NULL) pDisp->Release();
  // The events IID, for the container.
  LPPROVIDECLASSINFO2 pPCI = NULL;
  if (SUCCEEDED(pUnk->QueryInterface(IID_IProvideClassInfo2, (void**)&pPCI))) {
    GUID guid = GUID_NULL;
    CHECK(pPCI->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &guid) == S_OK);
    CHECK(guid == IID_DTestCtrlEvents);
    pPCI->Release();
  }
  // Property pages.
  ISpecifyPropertyPages* pSPP = NULL;
  if (SUCCEEDED(pUnk->QueryInterface(IID_ISpecifyPropertyPages, (void**)&pSPP))) {
    CAUUID pages = {0, NULL};
    CHECK(pSPP->GetPages(&pages) == S_OK);
    CHECK(pages.cElems == 2 && pages.pElems != NULL && pages.pElems[0] == CLSID_TestPage &&
          pages.pElems[1] == CLSID_CColorPropPage);
    ::CoTaskMemFree(pages.pElems);
    pSPP->Release();
  }
  // IOleObject: the class, the misc status and the user type.
  LPOLEOBJECT pObj = NULL;
  if (SUCCEEDED(pUnk->QueryInterface(IID_IOleObject, (void**)&pObj))) {
    CLSID clsid = GUID_NULL;
    CHECK(pObj->GetUserClassID(&clsid) == S_OK && clsid == CTestCtrl::guid);
    DWORD dwMisc = 0;
    CHECK(pObj->GetMiscStatus(DVASPECT_CONTENT, &dwMisc) == S_OK && dwMisc == kMiscStatus);
    LPOLESTR pszType = NULL;
    if (pObj->GetUserType(USERCLASSTYPE_FULL, &pszType) == S_OK && pszType != NULL) {
      CHECK(CString(pszType) == _T("CtlTest Control"));
      ::CoTaskMemFree(pszType);
    } else {
      CHECK(!"GetUserType");
    }
    SIZEL sizel = {0, 0};
    CHECK(pObj->GetExtent(DVASPECT_CONTENT, &sizel) == S_OK && sizel.cx > 0 && sizel.cy > 0);
    pObj->Release();
  }

  // The masks come from the maps.
  CHECK(pCtl->GetStockPropMask() == (STOCKPROP_BACKCOLOR | STOCKPROP_FORECOLOR |
                                     STOCKPROP_CAPTION | STOCKPROP_ENABLED | STOCKPROP_FONT |
                                     STOCKPROP_BORDERSTYLE));
  CHECK(pCtl->GetStockEventMask() == (STOCKEVENT_CLICK | STOCKEVENT_KEYDOWN));

  // Ambient properties.
  CHECK(pCtl->AmbientUserMode());
  CHECK(pCtl->AmbientBackColor() == RGB(10, 20, 30));
  CHECK(pCtl->AmbientDisplayName() == _T("Ctl1"));
  m_clrAmbientBack = RGB(40, 50, 60);
  if (m_pCtrlCont != NULL) m_pCtrlCont->BroadcastAmbientPropertyChange(DISPID_AMBIENT_BACKCOLOR);
  CHECK(pCtl->m_nAmbientChanges == 1 && pCtl->m_dispidAmbient == DISPID_AMBIENT_BACKCOLOR);
  CHECK(pCtl->AmbientBackColor() == RGB(40, 50, 60));

  // Stock properties: Caption from the window name, then through the
  // wrapper.
  CHECK(pCtl->InternalGetText() == _T("Hello"));
  CString str;
  m_ctl.GetWindowText(str);
  CHECK(str == _T("Hello"));
  m_ctl.SetWindowText(_T("World"));
  CHECK(pCtl->InternalGetText() == _T("World"));
  TCHAR szText[32];
  ::GetWindowText(pCtl->m_hWnd, szText, _countof(szText));
  CHECK(lstrcmp(szText, _T("World")) == 0);

  m_ctl.SetProperty(DISPID_BACKCOLOR, VT_I4, (long)RGB(0, 128, 255));
  CHECK(pCtl->GetBackColor() == RGB(0, 128, 255) && pCtl->m_nBackChanges == 1);
  long nColor = 0;
  m_ctl.GetProperty(DISPID_BACKCOLOR, VT_I4, &nColor);
  CHECK(nColor == (long)RGB(0, 128, 255));
  m_ctl.SetProperty(DISPID_FORECOLOR, VT_I4, (long)RGB(255, 255, 255));
  CHECK(pCtl->GetForeColor() == RGB(255, 255, 255));
  CHECK(COleControl::TranslateColor(0x80000000 | COLOR_WINDOW) == ::GetSysColor(COLOR_WINDOW));

  short sBorder = -1;
  m_ctl.SetProperty(DISPID_BORDERSTYLE, VT_I2, 1);
  m_ctl.GetProperty(DISPID_BORDERSTYLE, VT_I2, &sBorder);
  CHECK(sBorder == 1 && (pCtl->GetStyle() & WS_BORDER) != 0);
  m_ctl.SetProperty(DISPID_BORDERSTYLE, VT_I2, 0);
  CHECK((pCtl->GetStyle() & WS_BORDER) == 0);

  // Enabled goes to the window.
  m_ctl.EnableWindow(FALSE);
  CHECK(!pCtl->GetEnabled() && !::IsWindowEnabled(pCtl->m_hWnd));
  m_ctl.EnableWindow(TRUE);
  CHECK(pCtl->GetEnabled() && ::IsWindowEnabled(pCtl->m_hWnd));

  // The font: a stock property, an IFont.
  LPDISPATCH pFontDisp = NULL;
  m_ctl.GetProperty(DISPID_FONT, VT_DISPATCH, &pFontDisp);
  CHECK(pFontDisp != NULL);
  if (pFontDisp != NULL) {
    LPFONT pFont = NULL;
    CHECK(pFontDisp->QueryInterface(IID_IFont, (void**)&pFont) == S_OK);
    if (pFont != NULL) {
      // The control holds this font object, so a change of it reaches
      // the control.
      CY cy;
      cy.Lo = 120000;  // 12 points
      cy.Hi = 0;
      int nDraws = pCtl->m_nDraws;
      CHECK(pFont->put_Size(cy) == S_OK);
      CY cySize;
      cySize.Lo = cySize.Hi = 0;
      LPFONT pHeld = pCtl->InternalGetFont().m_pFont;
      CHECK(pHeld == pFont);
      CHECK(pHeld != NULL && pHeld->get_Size(&cySize) == S_OK);
      // The font object may round the size to whole pixels.
      CHECK(cySize.Lo > 100000 && cySize.Lo <= 130000);
      PumpMessages();
      CHECK(pCtl->m_nDraws > nDraws);
      pFont->Release();
    }
    pFontDisp->Release();
  }
  CHECK(pCtl->InternalGetFont().GetFontHandle() != NULL);

  // A custom property, with property notifications to the container.
  m_ctl.SetProperty(DISPID_VALUE_, VT_I4, 41L);
  long n = 0;
  m_ctl.GetProperty(DISPID_VALUE_, VT_I4, &n);
  CHECK(n == 41 && pCtl->m_nValue == 41);
  CHECK(m_nRequests == 1 && m_nChanges == 1);
  CHECK(pCtl->IsModified());
  m_bVeto = TRUE;
  TRY {
    m_ctl.SetProperty(DISPID_VALUE_, VT_I4, 99L);
  }
  CATCH(COleDispatchException, e) {
    TestLog("vetoed: scode %08lx", (unsigned long)e->m_scError);
  }
  END_CATCH
  CHECK(pCtl->m_nValue == 41 && m_nRequests == 2 && m_nChanges == 1);
  m_bVeto = FALSE;
  // A member property.
  m_ctl.SetProperty(DISPID_LABEL, VT_BSTR, _T("labelled"));
  CHECK(pCtl->m_strLabel == _T("labelled"));

  // Methods.
  static const BYTE paramsI4[] = VTS_I4;
  long nTwice = 0;
  m_ctl.InvokeHelper(DISPID_TWICE, DISPATCH_METHOD, VT_I4, &nTwice, paramsI4, 21L);
  CHECK(nTwice == 42);

  // Verbs: the verb map's, numbered from 1, then Properties.
  LPOLEOBJECT pVerbObj = NULL;
  pUnk->QueryInterface(IID_IOleObject, (void**)&pVerbObj);
  if (pVerbObj != NULL) {
    LPENUMOLEVERB pEnum = NULL;
    CHECK(pVerbObj->EnumVerbs(&pEnum) == S_OK && pEnum != NULL);
    if (pEnum != NULL) {
      OLEVERB verbs[5];
      ULONG nVerbs = 0;
      CHECK(pEnum->Next(5, verbs, &nVerbs) == S_FALSE && nVerbs == 3);
      CHECK(verbs[0].lVerb == 1 && CString(verbs[0].lpszVerbName) == _T("&Reset"));
      CHECK(verbs[1].lVerb == 2 && CString(verbs[1].lpszVerbName) == _T("&Bump"));
      CHECK(verbs[2].lVerb == OLEIVERB_PROPERTIES);
      CHECK((verbs[0].grfAttribs & OLEVERBATTRIB_ONCONTAINERMENU) != 0);
      for (ULONG i = 0; i < nVerbs; i++) ::CoTaskMemFree(verbs[i].lpszVerbName);
      pEnum->Reset();
      CHECK(pEnum->Skip(2) == S_OK && pEnum->Next(1, verbs, NULL) == S_OK &&
            verbs[0].lVerb == OLEIVERB_PROPERTIES);
      ::CoTaskMemFree(verbs[0].lpszVerbName);
      pEnum->Release();
    }
    pCtl->m_nValue = 30;
    CHECK(pVerbObj->DoVerb(1, NULL, NULL, 0, m_hWnd, &rect) == S_OK && pCtl->m_nValue == 0);
    CHECK(pVerbObj->DoVerb(2, NULL, NULL, 0, m_hWnd, &rect) == S_OK && pCtl->m_nValue == 1);
    CHECK(m_nChanged == 1 && m_nLastChanged == 1);
    CHECK(pVerbObj->DoVerb(OLEIVERB_OPEN, NULL, NULL, 0, m_hWnd, &rect) == S_OK);
    CHECK(pCtl->m_nOpenVerbs == 1);
    CHECK(pVerbObj->DoVerb(3, NULL, NULL, 0, m_hWnd, &rect) == OLEOBJ_S_INVALIDVERB);
    pVerbObj->Release();
    pCtl->m_nValue = 41;
    m_nChanged = 0;
  }

  // Events: a custom one from a method, and stock Click from the mouse
  // and from DoClick.
  m_ctl.InvokeHelper(DISPID_BUMP, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  CHECK(m_nChanged == 1 && m_nLastChanged == 42);
  pCtl->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
  pCtl->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
  CHECK(m_nClicks == 1);
  m_ctl.InvokeHelper(DISPID_DOCLICK, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  CHECK(m_nClicks == 2);
  // A release outside the control is not a click.
  pCtl->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
  pCtl->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(500, 5));
  CHECK(m_nClicks == 2);

  // Frozen events are not fired.
  COleControlSite* pSite = m_ctl.m_pCtrlSite;
  CHECK(pSite != NULL);
  if (pSite != NULL) {
    pSite->FreezeEvents(TRUE);
    CHECK(pCtl->AreEventsFrozen());
    pCtl->FireChanged(1);
    CHECK(m_nChanged == 1);
    pSite->FreezeEvents(FALSE);
    CHECK(!pCtl->AreEventsFrozen());
    pCtl->FireChanged(2);
    CHECK(m_nChanged == 2 && m_nLastChanged == 2);
  }

  // Moving the wrapper moves the control.
  m_ctl.MoveWindow(20, 80, 150, 40);
  m_ctl.GetWindowRect(&rectCtl);
  ScreenToClient(&rectCtl);
  CHECK(rectCtl == CRect(20, 80, 170, 120));
  pCtl->GetControlSize(&cx, &cy);
  CHECK(cx == 150 && cy == 40);

  // Focus makes it UI active.
  m_ctl.SetFocus();
  CHECK(::GetFocus() == pCtl->m_hWnd);
  CHECK(pCtl->IsUIActive());
  CHECK(m_pCtrlCont != NULL && m_pCtrlCont->m_pSiteUIActive == pSite);

  // Drawing: in the window, and through IViewObject into another DC.
  int nDraws = pCtl->m_nDraws;
  pCtl->InvalidateControl();
  pCtl->UpdateWindow();
  CHECK(pCtl->m_nDraws > nDraws);
  {
    CClientDC dcScreen(NULL);
    CDC dc;
    dc.CreateCompatibleDC(&dcScreen);
    CBitmap bm;
    bm.CreateCompatibleBitmap(&dcScreen, 100, 60);
    CBitmap* pOld = dc.SelectObject(&bm);
    dc.FillSolidRect(0, 0, 100, 60, RGB(255, 0, 0));
    LPVIEWOBJECT pView = NULL;
    CHECK(pUnk->QueryInterface(IID_IViewObject, (void**)&pView) == S_OK);
    if (pView != NULL) {
      RECTL rcl = {10, 10, 90, 50};
      CHECK(pView->Draw(DVASPECT_CONTENT, -1, NULL, NULL, NULL, dc.GetSafeHdc(), &rcl, NULL, NULL,
                        0) == S_OK);
      pView->Release();
    }
    CHECK(dc.GetPixel(12, 12) == RGB(0, 128, 255));
    CHECK(dc.GetPixel(5, 5) == RGB(255, 0, 0));
    CHECK(dc.GetPixel(95, 55) == RGB(255, 0, 0));
    dc.SelectObject(pOld);
  }

  // The container's extended control: extender properties, and the
  // control's own passed on.
  LPDISPATCH pExt = pCtl->GetExtendedControl();
  CHECK(pExt != NULL);
  if (pExt != NULL) {
    COleDispatchDriver ext(pExt, FALSE);
    LPOLESTR names[] = {(LPOLESTR)L"left", (LPOLESTR)L"Label", (LPOLESTR)L"Nothing"};
    DISPID dispid = 0;
    CHECK(pExt->GetIDsOfNames(IID_NULL, &names[0], 1, 0, &dispid) == S_OK &&
          dispid == AFX_DISPID_EXT_LEFT);
    CHECK(pExt->GetIDsOfNames(IID_NULL, &names[1], 1, 0, &dispid) == S_OK &&
          dispid == DISPID_LABEL);
    CHECK(pExt->GetIDsOfNames(IID_NULL, &names[2], 1, 0, &dispid) == DISP_E_UNKNOWNNAME);
    CString strName;
    ext.GetProperty(DISPID_NAME, VT_BSTR, &strName);
    CHECK(strName.IsEmpty());
    ext.SetProperty(DISPID_NAME, VT_BSTR, _T("Ctl1"));
    ext.GetProperty(DISPID_NAME, VT_BSTR, &strName);
    CHECK(strName == _T("Ctl1") && m_ctl.m_pCtrlSite->m_strName == _T("Ctl1"));
    long nLeft = 0, nTop = 0, nWidth = 0, nHeight = 0, nID = 0;
    ext.GetProperty(AFX_DISPID_EXT_LEFT, VT_I4, &nLeft);
    ext.GetProperty(AFX_DISPID_EXT_TOP, VT_I4, &nTop);
    ext.GetProperty(AFX_DISPID_EXT_WIDTH, VT_I4, &nWidth);
    ext.GetProperty(AFX_DISPID_EXT_HEIGHT, VT_I4, &nHeight);
    ext.GetProperty(AFX_DISPID_EXT_ID, VT_I4, &nID);
    CRect rcNow;
    m_ctl.GetWindowRect(&rcNow);
    ScreenToClient(&rcNow);
    CHECK(nLeft == rcNow.left && nTop == rcNow.top && nWidth == rcNow.Width() &&
          nHeight == rcNow.Height() && nID == IDC_CTL);
    // Moving it through the extender moves the control.
    ext.SetProperty(AFX_DISPID_EXT_WIDTH, VT_I4, 150L);
    ext.SetProperty(AFX_DISPID_EXT_LEFT, VT_I4, 20L);
    m_ctl.GetWindowRect(&rcNow);
    ScreenToClient(&rcNow);
    CHECK(rcNow.left == 20 && rcNow.Width() == 150 && rcNow.top == nTop);
    BOOL bVisible = FALSE;
    ext.GetProperty(AFX_DISPID_EXT_VISIBLE, VT_BOOL, &bVisible);
    CHECK(bVisible);
    ext.SetProperty(AFX_DISPID_EXT_VISIBLE, VT_BOOL, FALSE);
    CHECK(!::IsWindowVisible(pCtl->m_hWnd));
    ext.SetProperty(AFX_DISPID_EXT_VISIBLE, VT_BOOL, TRUE);
    CHECK(::IsWindowVisible(pCtl->m_hWnd));
    // The control itself, and its own properties through the extender.
    LPDISPATCH pObj = NULL;
    ext.GetProperty(DISPID_OBJECT, VT_DISPATCH, &pObj);
    CHECK(pObj != NULL && pObj == m_ctl.m_pCtrlSite->m_dispDriver.m_lpDispatch);
    if (pObj != NULL) pObj->Release();
    ext.SetProperty(DISPID_LABEL, VT_BSTR, _T("through"));
    CHECK(pCtl->m_strLabel == _T("through"));
    long nTwice = 0;
    static const BYTE parms[] = VTS_I4;
    ext.InvokeHelper(DISPID_TWICE, DISPATCH_METHOD, VT_I4, &nTwice, parms, 21L);
    CHECK(nTwice == 42);
    // The same object each time.
    LPDISPATCH pExt2 = pCtl->GetExtendedControl();
    CHECK(pExt2 == pExt);
    if (pExt2 != NULL) pExt2->Release();
    ext.SetProperty(AFX_DISPID_EXT_LEFT, VT_I4, (long)rect.left);
    ext.SetProperty(AFX_DISPID_EXT_WIDTH, VT_I4, (long)rect.Width());
    ext.DetachDispatch();
  }

  PumpMessages();
  TestSaveWindowBitmap(m_hWnd, _T("ctltest-frame.bmp"));

  // Destroying the wrapper closes the control and frees it.
  m_ctl.DestroyWindow();
  CHECK(m_ctl.m_hWnd == NULL && m_ctl.m_pCtrlSite == NULL);
  CHECK(CTestCtrl::s_nAlive == 0);
  // An extended control kept after its control went says so.
  if (pExt != NULL) {
    VARIANT var;
    ::VariantInit(&var);
    DISPPARAMS noArgs = {NULL, NULL, 0, 0};
    CHECK(pExt->Invoke(AFX_DISPID_EXT_LEFT, IID_NULL, 0, DISPATCH_PROPERTYGET, &noArgs, &var,
                       NULL, NULL) == E_UNEXPECTED);
    CHECK(pExt->Invoke(DISPID_LABEL, IID_NULL, 0, DISPATCH_PROPERTYGET, &noArgs, &var, NULL,
                       NULL) == DISP_E_MEMBERNOTFOUND);
    pExt->Release();
  }
}

// An 8x8 bitmap of one color.
static HBITMAP MakeBitmap(COLORREF clr) {
  HDC hdcScreen = ::GetDC(NULL);
  HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, 8, 8);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HGDIOBJ hOld = ::SelectObject(hdc, hbm);
  HBRUSH hBrush = ::CreateSolidBrush(clr);
  RECT rc = {0, 0, 8, 8};
  ::FillRect(hdc, &rc, hBrush);
  ::DeleteObject(hBrush);
  ::SelectObject(hdc, hOld);
  ::DeleteDC(hdc);
  ::ReleaseDC(NULL, hdcScreen);
  return hbm;
}

// An 8x8 24-bit .bmp file of one color.
static void WriteBitmapFile(LPCTSTR lpszPath, COLORREF clr) {
  BITMAPFILEHEADER bfh;
  BITMAPINFOHEADER bih;
  memset(&bfh, 0, sizeof(bfh));
  memset(&bih, 0, sizeof(bih));
  bih.biSize = sizeof(bih);
  bih.biWidth = bih.biHeight = 8;
  bih.biPlanes = 1;
  bih.biBitCount = 24;
  BYTE bits[8 * 8 * 3];
  for (int i = 0; i < 64; i++) {
    bits[i * 3] = GetBValue(clr);
    bits[i * 3 + 1] = GetGValue(clr);
    bits[i * 3 + 2] = GetRValue(clr);
  }
  bfh.bfType = 0x4D42;
  bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
  bfh.bfSize = bfh.bfOffBits + sizeof(bits);
  CFile file(lpszPath, CFile::modeCreate | CFile::modeWrite);
  file.Write(&bfh, sizeof(bfh));
  file.Write(&bih, sizeof(bih));
  file.Write(bits, sizeof(bits));
}

// Makes a control with CoCreateInstance; the caller owns the IUnknown.
static LPUNKNOWN NewControl(CTestCtrl** ppCtl) {
  LPUNKNOWN pUnk = NULL;
  HRESULT hr = ::CoCreateInstance(CTestCtrl::guid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                  IID_IUnknown, (void**)&pUnk);
  CHECK(SUCCEEDED(hr) && pUnk != NULL);
  *ppCtl = CTestCtrl::s_pLast;
  return pUnk;
}

void CMainFrame::TestPersistence() {
  TestLog("persistence");
  CTestCtrl* pCtl = NULL;
  LPUNKNOWN pUnk = NewControl(&pCtl);
  if (pUnk == NULL || pCtl == NULL) return;

  // A stream.
  LPPERSISTSTREAMINIT pPSI = NULL;
  CHECK(pUnk->QueryInterface(IID_IPersistStreamInit, (void**)&pPSI) == S_OK);
  if (pPSI == NULL) {
    pUnk->Release();
    return;
  }
  CHECK(pPSI->InitNew() == S_OK);
  CHECK(pCtl->m_nValue == 7 && pCtl->m_strLabel == _T("none"));
  CLSID clsid = GUID_NULL;
  CHECK(pPSI->GetClassID(&clsid) == S_OK && clsid == CTestCtrl::guid);
  pCtl->m_nValue = 55;
  pCtl->m_strLabel = _T("saved");
  pCtl->SetText(_T("Caption!"));
  pCtl->SetBackColor(RGB(1, 2, 3));
  CHECK(pCtl->m_pict.GetType() == PICTYPE_NONE);
  CHECK(pCtl->m_pict.CreateFromBitmap(MakeBitmap(RGB(255, 0, 0)), NULL, TRUE));
  CHECK(pPSI->IsDirty() == S_OK);

  LPSTREAM pStm = NULL;
  CHECK(::CreateStreamOnHGlobal(NULL, TRUE, &pStm) == S_OK);
  CHECK(pPSI->Save(pStm, TRUE) == S_OK);
  CHECK(pPSI->IsDirty() == S_FALSE);
  ULARGE_INTEGER cbMax;
  cbMax.QuadPart = 0;
  CHECK(pPSI->GetSizeMax(&cbMax) == S_OK && cbMax.QuadPart > 0);
  pPSI->Release();
  pUnk->Release();
  CHECK(CTestCtrl::s_nAlive == 0);

  // Into a new instance.
  LARGE_INTEGER li;
  li.QuadPart = 0;
  pStm->Seek(li, STREAM_SEEK_SET, NULL);
  CTestCtrl* pCtl2 = NULL;
  LPUNKNOWN pUnk2 = NewControl(&pCtl2);
  if (pUnk2 != NULL && pCtl2 != NULL) {
    LPPERSISTSTREAMINIT pPSI2 = NULL;
    pUnk2->QueryInterface(IID_IPersistStreamInit, (void**)&pPSI2);
    CHECK(pPSI2 != NULL && pPSI2->Load(pStm) == S_OK);
    CHECK(pCtl2->m_nValue == 55 && pCtl2->m_strLabel == _T("saved"));
    CHECK(pCtl2->InternalGetText() == _T("Caption!"));
    CHECK(pCtl2->GetBackColor() == RGB(1, 2, 3));
    CHECK(pCtl2->InternalGetFont().m_pFont != NULL);
    TestLog("loaded picture type %d", pCtl2->m_pict.GetType());
    CHECK(pCtl2->m_pict.GetType() == PICTYPE_BITMAP);

    // The same state into a property bag; values at their defaults are
    // left out.
    CTestPropBag bag;
    LPPERSISTPROPERTYBAG pPPB = NULL;
    CHECK(pUnk2->QueryInterface(IID_IPersistPropertyBag, (void**)&pPPB) == S_OK);
    if (pPPB != NULL) {
      CHECK(pPPB->Save(&bag, TRUE, FALSE) == S_OK);
      int i = bag.Find(L"Value");
      CHECK(i >= 0 && bag.m_values[i].vt == VT_I4 && bag.m_values[i].lVal == 55);
      i = bag.Find(L"Label");
      CHECK(i >= 0 && bag.m_values[i].vt == VT_BSTR &&
            CString(bag.m_values[i].bstrVal) == _T("saved"));
      i = bag.Find(L"BackColor");
      CHECK(i >= 0 && bag.m_values[i].lVal == (long)RGB(1, 2, 3));
      CHECK(bag.Find(L"ForeColor") < 0);
      CHECK(bag.Find(L"_ExtentX") >= 0 && bag.Find(L"_ExtentY") >= 0);
      i = bag.Find(L"Font");
      CHECK(i >= 0 && bag.m_values[i].vt == VT_UNKNOWN);
      pPPB->Release();
    }
    if (pPSI2 != NULL) pPSI2->Release();
    pUnk2->Release();
    CHECK(CTestCtrl::s_nAlive == 0);

    // And from the bag, with a value given as a string.
    COleVariant varText(_T("123"));
    bag.Write(L"Value", &varText);
    CTestCtrl* pCtl3 = NULL;
    LPUNKNOWN pUnk3 = NewControl(&pCtl3);
    if (pUnk3 != NULL && pCtl3 != NULL) {
      LPPERSISTPROPERTYBAG pPPB3 = NULL;
      pUnk3->QueryInterface(IID_IPersistPropertyBag, (void**)&pPPB3);
      CHECK(pPPB3 != NULL && pPPB3->Load(&bag, NULL) == S_OK);
      CHECK(pCtl3->m_nValue == 123 && pCtl3->m_strLabel == _T("saved"));
      CHECK(pCtl3->GetBackColor() == RGB(1, 2, 3));
      CHECK(pCtl3->InternalGetText() == _T("Caption!"));
      CHECK(pCtl3->InternalGetFont().m_pFont != NULL);
      // A bag without the values gives the defaults.
      CTestPropBag empty;
      CHECK(pPPB3->Load(&empty, NULL) == S_OK);
      CHECK(pCtl3->m_nValue == 7 && pCtl3->m_strLabel == _T("none"));
      if (pPPB3 != NULL) pPPB3->Release();
      pUnk3->Release();
    }
  }
  pStm->Release();
  CHECK(CTestCtrl::s_nAlive == 0);

  // IPersistStorage: a storage with the control's stream.
  CTestCtrl* pCtl4 = NULL;
  LPUNKNOWN pUnk4 = NewControl(&pCtl4);
  if (pUnk4 != NULL && pCtl4 != NULL) {
    LPLOCKBYTES pLock = NULL;
    LPSTORAGE pStg = NULL;
    ::CreateILockBytesOnHGlobal(NULL, TRUE, &pLock);
    ::StgCreateDocfileOnILockBytes(pLock, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0,
                                   &pStg);
    CHECK(pStg != NULL);
    LPPERSISTSTORAGE pPS = NULL;
    pUnk4->QueryInterface(IID_IPersistStorage, (void**)&pPS);
    if (pStg != NULL && pPS != NULL) {
      CHECK(pPS->InitNew(pStg) == S_OK);
      pCtl4->m_nValue = 99;
      CHECK(pPS->Save(pStg, TRUE) == S_OK);
      CHECK(pPS->SaveCompleted(NULL) == S_OK);
      pPS->Release();
      pPS = NULL;
      pUnk4->Release();
      pUnk4 = NULL;
      CTestCtrl* pCtl5 = NULL;
      LPUNKNOWN pUnk5 = NewControl(&pCtl5);
      if (pUnk5 != NULL && pCtl5 != NULL) {
        LPPERSISTSTORAGE pPS5 = NULL;
        pUnk5->QueryInterface(IID_IPersistStorage, (void**)&pPS5);
        CHECK(pPS5 != NULL && pPS5->Load(pStg) == S_OK);
        CHECK(pCtl5->m_nValue == 99);
        if (pPS5 != NULL) pPS5->Release();
        pUnk5->Release();
      }
    }
    if (pPS != NULL) pPS->Release();
    if (pUnk4 != NULL) pUnk4->Release();
    if (pStg != NULL) pStg->Release();
    if (pLock != NULL) pLock->Release();
  }
  CHECK(CTestCtrl::s_nAlive == 0);
}

static CString DlgText(HWND hDlg, int nID) {
  TCHAR sz[64];
  sz[0] = 0;
  ::GetDlgItemText(hDlg, nID, sz, _countof(sz));
  return sz;
}

void CMainFrame::TestPropertyPage() {
  TestLog("property page");
  CTestCtrl* pCtl = NULL;
  LPUNKNOWN pUnkCtl = NewControl(&pCtl);
  CTestCtrl* pCtl2 = NULL;
  LPUNKNOWN pUnkCtl2 = NewControl(&pCtl2);
  if (pUnkCtl == NULL || pUnkCtl2 == NULL) return;
  LPPERSISTSTREAMINIT pInit = NULL;
  pUnkCtl->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
  if (pInit != NULL) {
    pInit->InitNew();
    pInit->Release();
  }
  pUnkCtl2->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
  if (pInit != NULL) {
    pInit->InitNew();
    pInit->Release();
  }

  // The page, from its class factory.
  LPPROPERTYPAGE pPage = NULL;
  HRESULT hr = ::CoCreateInstance(CLSID_TestPage, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                  IID_IPropertyPage, (void**)&pPage);
  CHECK(SUCCEEDED(hr) && pPage != NULL);
  CTestPropPage* pObj = CTestPropPage::s_pLast;
  CHECK(pObj != NULL && CTestPropPage::s_nAlive == 1);
  if (pPage == NULL || pObj == NULL) {
    pUnkCtl->Release();
    pUnkCtl2->Release();
    return;
  }
  LPUNKNOWN p2 = NULL;
  CHECK(pPage->QueryInterface(IID_IPropertyPage2, (void**)&p2) == S_OK);
  if (p2 != NULL) p2->Release();

  CTestPageSite site;
  CHECK(pPage->SetPageSite(&site) == S_OK);
  CHECK(pObj->GetPageSite() == &site);

  // The page's title and size, from the caption string and the template.
  PROPPAGEINFO info;
  memset(&info, 0, sizeof(info));
  CHECK(pPage->GetPageInfo(&info) == S_OK);
  CHECK(info.pszTitle != NULL && CString(info.pszTitle) == _T("General"));
  CHECK(info.size.cx > 200 && info.size.cy > 90);
  TestLog("page size %ldx%ld", (long)info.size.cx, (long)info.size.cy);
  ::CoTaskMemFree(info.pszTitle);
  ::CoTaskMemFree(info.pszDocString);
  ::CoTaskMemFree(info.pszHelpFile);

  // The values come from the control.
  pCtl->m_nValue = 31;
  pCtl->m_strLabel = _T("first");
  pCtl->SetBorderStyle(0);
  CHECK(pPage->SetObjects(1, &pUnkCtl) == S_OK);
  CHECK(pObj->m_nObjectsChanged == 1);
  ULONG nObjects = 0;
  CHECK(pObj->GetObjectArray(&nObjects) != NULL && nObjects == 1);
  CRect rcPage(0, 0, info.size.cx, info.size.cy);
  CHECK(pPage->Activate(m_hWnd, &rcPage, FALSE) == S_OK);
  HWND hPage = pObj->m_hWnd;
  CHECK(hPage != NULL && ::GetParent(hPage) == m_hWnd);
  if (hPage == NULL) return;
  CHECK((::GetWindowLong(hPage, GWL_STYLE) & (WS_CHILD | WS_CAPTION)) == WS_CHILD);
  CHECK(pPage->Show(SW_SHOW) == S_OK && ::IsWindowVisible(hPage));
  CHECK(DlgText(hPage, IDC_VALUE) == _T("31"));
  CHECK(DlgText(hPage, IDC_LABEL) == _T("first"));
  CHECK(::IsDlgButtonChecked(hPage, IDC_ENABLED) == BST_CHECKED);
  CHECK(::IsDlgButtonChecked(hPage, IDC_BORDER0) == BST_CHECKED);
  CHECK(pPage->IsPageDirty() == S_FALSE && site.m_nDirty == 0);

  // Changing controls makes them, and the page, dirty.
  ::SetDlgItemText(hPage, IDC_VALUE, _T("123"));
  CHECK(pPage->IsPageDirty() == S_OK && site.m_nDirty == 1);
  CHECK(pObj->GetControlStatus(IDC_VALUE) && !pObj->GetControlStatus(IDC_LABEL));
  ::SendDlgItemMessage(hPage, IDC_BORDER1, BM_CLICK, 0, 0);
  ::SendDlgItemMessage(hPage, IDC_ENABLED, BM_CLICK, 0, 0);
  CHECK(pObj->GetControlStatus(IDC_BORDER0) && pObj->GetControlStatus(IDC_ENABLED));
  CHECK(site.m_nDirty == 1);
  // Apply writes the changed values only.
  pCtl->m_strLabel = _T("changed elsewhere");
  CHECK(pPage->Apply() == S_OK);
  CHECK(pCtl->m_nValue == 123 && pCtl->GetBorderStyle() == 1 && !pCtl->GetEnabled());
  CHECK(pCtl->m_strLabel == _T("changed elsewhere"));
  CHECK(pPage->IsPageDirty() == S_FALSE && site.m_nClean == 1);
  CHECK(!pObj->GetControlStatus(IDC_VALUE));

  // New objects reload the page; a change goes to every object.
  pCtl->m_strLabel = _T("one");
  pCtl2->m_strLabel = _T("two");
  LPUNKNOWN objs[2] = {pUnkCtl, pUnkCtl2};
  CHECK(pPage->SetObjects(2, objs) == S_OK);
  CHECK(pObj->m_nObjectsChanged == 2);
  CHECK(DlgText(hPage, IDC_LABEL) == _T("one"));
  CHECK(pPage->IsPageDirty() == S_FALSE);
  ::SetDlgItemText(hPage, IDC_LABEL, _T("both"));
  CHECK(pPage->Apply() == S_OK);
  CHECK(pCtl->m_strLabel == _T("both") && pCtl2->m_strLabel == _T("both"));
  CHECK(pCtl->m_nValue == 123 && pCtl2->m_nValue == 7);
  // A control the page ignores does not make it dirty.
  pObj->IgnoreApply(IDC_VALUE);
  ::SetDlgItemText(hPage, IDC_VALUE, _T("5"));
  CHECK(pPage->IsPageDirty() == S_FALSE);

  // Keys the page handles.
  MSG msg;
  memset(&msg, 0, sizeof(msg));
  msg.hwnd = ::GetDlgItem(hPage, IDC_VALUE);
  msg.message = WM_KEYDOWN;
  msg.wParam = VK_TAB;
  CHECK(pPage->TranslateAccelerator(&msg) == S_OK);
  msg.wParam = VK_RETURN;
  CHECK(pPage->TranslateAccelerator(&msg) == S_FALSE);

  PumpMessages();
  TestSaveWindowBitmap(hPage, _T("ctltest-page.bmp"));

  CHECK(pPage->Deactivate() == S_OK && pObj->m_hWnd == NULL);
  CHECK(pPage->SetObjects(0, NULL) == S_OK);
  CHECK(pPage->SetPageSite(NULL) == S_OK);
  pPage->Release();
  CHECK(CTestPropPage::s_nAlive == 0);
  pUnkCtl->Release();
  pUnkCtl2->Release();
  CHECK(CTestCtrl::s_nAlive == 0);
}

// The property frame (OleCreatePropertyFrame, from the control's
// Properties verb) runs its own loop; a timer works the page and presses
// OK.
static int s_nFrameTicks = 0;
static BOOL s_bFrameSeen = FALSE;
static int s_nFrameTabs = 0;

static BOOL CALLBACK FindFrame(HWND hWnd, LPARAM lParam) {
  if (::IsWindowVisible(hWnd) && hWnd != AfxGetMainWnd()->m_hWnd) {
    CTestPropPage* pPage = CTestPropPage::s_pLast;
    if (pPage != NULL && pPage->m_hWnd != NULL && ::IsChild(hWnd, pPage->m_hWnd)) {
      *(HWND*)lParam = hWnd;
      return FALSE;
    }
  }
  return TRUE;
}

static void CALLBACK FrameTimer(HWND, UINT, UINT_PTR idEvent, DWORD) {
  s_nFrameTicks++;
  HWND hFrame = NULL;
  ::EnumThreadWindows(::GetCurrentThreadId(), FindFrame, (LPARAM)&hFrame);
  if (hFrame != NULL) {
    ::KillTimer(NULL, idEvent);
    s_bFrameSeen = TRUE;
    HWND hPage = CTestPropPage::s_pLast->m_hWnd;
    ::SetDlgItemText(hPage, IDC_VALUE, _T("77"));
    TestSaveWindowBitmap(hFrame, _T("ctltest-frame-props.bmp"));
    // Both pages, the stock color page included.
    HWND hTab = ::FindWindowEx(hFrame, NULL, WC_TABCONTROL, NULL);
    s_nFrameTabs = hTab != NULL ? (int)::SendMessage(hTab, TCM_GETITEMCOUNT, 0, 0) : -1;
    ::PostMessage(hFrame, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                  (LPARAM)::GetDlgItem(hFrame, IDOK));
  } else if (s_nFrameTicks > 50) {
    // No frame: close whatever dialog came up.
    ::KillTimer(NULL, idEvent);
    HWND hWnd = ::GetActiveWindow();
    if (hWnd != NULL && hWnd != AfxGetMainWnd()->m_hWnd) ::PostMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
  }
}

void CMainFrame::TestPropertyFrame() {
  TestLog("property frame");
  CRect rect(10, 10, 210, 70);
  BOOL bMade = m_ctl.CreateControl(CTestCtrl::guid, _T("Props"), WS_VISIBLE, rect, this, IDC_CTL);
  CHECK(bMade);
  CTestCtrl* pCtl = CTestCtrl::s_pLast;
  if (!bMade || pCtl == NULL) return;
  LPOLEOBJECT pObj = NULL;
  m_ctl.GetControlUnknown()->QueryInterface(IID_IOleObject, (void**)&pObj);
  CHECK(pObj != NULL);
  if (pObj != NULL) {
    s_nFrameTicks = 0;
    s_bFrameSeen = FALSE;
    ::SetTimer(NULL, 0, 100, FrameTimer);
    HRESULT hr = pObj->DoVerb(OLEIVERB_PROPERTIES, NULL, NULL, 0, m_hWnd, &rect);
    CHECK(SUCCEEDED(hr));
    CHECK(s_bFrameSeen);
    TestLog("frame tabs %d", s_nFrameTabs);
    CHECK(s_nFrameTabs == 2);
    // OK applies the dirty page. Wine's frame (as of Wine 9) never calls
    // IPropertyPage::Apply, so there the page is still dirty at the end.
    if (::GetProcAddress(::GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL) {
      CHECK(CTestPropPage::s_bDirtyAtEnd && pCtl->m_nValue == 7);
    } else {
      CHECK(!CTestPropPage::s_bDirtyAtEnd && pCtl->m_nValue == 77);
    }
    pObj->Release();
  }
  CHECK(CTestPropPage::s_nAlive == 0);
  // The container's own frame, which the verb also used.
  s_nFrameTicks = 0;
  s_bFrameSeen = FALSE;
  s_nFrameTabs = 0;
  ::SetTimer(NULL, 0, 100, FrameTimer);
  CHECK(m_ctl.m_pCtrlSite != NULL && m_ctl.m_pCtrlSite->ShowPropertyFrame());
  CHECK(s_bFrameSeen && s_nFrameTabs == 2);
  CHECK(CTestPropPage::s_nAlive == 0);
  m_ctl.DestroyWindow();
  CHECK(CTestCtrl::s_nAlive == 0);
}

// IViewObjectEx, the drawing flags and IPointerInactive.
void CMainFrame::TestViewAndPointer() {
  TestLog("view and pointer");
  CTestCtrl* pCtl = NULL;
  LPUNKNOWN pUnk = NewControl(&pCtl);
  if (pUnk == NULL || pCtl == NULL) return;
  LPPERSISTSTREAMINIT pInit = NULL;
  pUnk->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
  if (pInit != NULL) {
    pInit->InitNew();
    pInit->Release();
  }
  IViewObjectEx* pView = NULL;
  CHECK(pUnk->QueryInterface(IID_IViewObjectEx, (void**)&pView) == S_OK);
  if (pView != NULL) {
    DWORD dwStatus = 0;
    CHECK(pView->GetViewStatus(&dwStatus) == S_OK && (dwStatus & VIEWSTATUS_OPAQUE));
    RECT rcBounds = {10, 10, 50, 30};
    DWORD dwHit = 0;
    POINT ptIn = {20, 20}, ptOut = {60, 20};
    CHECK(pView->QueryHitPoint(DVASPECT_CONTENT, &rcBounds, ptIn, 0, &dwHit) == S_OK &&
          dwHit == HITRESULT_HIT);
    CHECK(pView->QueryHitPoint(DVASPECT_CONTENT, &rcBounds, ptOut, 0, &dwHit) == S_OK &&
          dwHit == HITRESULT_OUTSIDE);
    RECT rcLoc = {45, 25, 70, 40};
    CHECK(pView->QueryHitRect(DVASPECT_CONTENT, &rcBounds, &rcLoc, 0, &dwHit) == S_OK &&
          dwHit == HITRESULT_HIT);
    RECTL rcl = {0, 0, 0, 0};
    SIZEL size = {0, 0};
    pView->GetExtent(DVASPECT_CONTENT, -1, NULL, &size);
    CHECK(pView->GetRect(DVASPECT_CONTENT, &rcl) == S_OK && rcl.right == size.cx &&
          rcl.bottom == size.cy);

    // Drawing is kept inside the bounds (clipPaintDC), unless the
    // control says it never draws outside; and it may leave the DC as it
    // is when the container allows it (canOptimizeDraw).
    CClientDC dcScreen(this);
    CDC dc;
    dc.CreateCompatibleDC(&dcScreen);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dcScreen, 80, 60);
    CBitmap* pOldBitmap = dc.SelectObject(&bitmap);
    RECTL rclDraw = {20, 20, 60, 40};
    DVASPECTINFO info = {sizeof(DVASPECTINFO), DVASPECTINFOFLAG_CANOPTIMIZE};
    CTestCtrl::s_bDrawOutside = TRUE;
    dc.FillSolidRect(0, 0, 80, 60, RGB(0, 0, 255));
    CTestCtrl::s_bOptimizedSeen = TRUE;
    CHECK(pView->Draw(DVASPECT_CONTENT, -1, &info, NULL, NULL, dc.m_hDC, &rclDraw, NULL, NULL, 0) ==
          S_OK);
    CHECK(dc.GetPixel(15, 15) == RGB(0, 0, 255) && dc.GetPixel(30, 30) == RGB(255, 0, 0));
    CHECK(!CTestCtrl::s_bOptimizedSeen);  // the control did not ask
    CTestCtrl::s_dwClearFlags = COleControl::clipPaintDC;
    CTestCtrl::s_dwAddFlags = COleControl::canOptimizeDraw;
    CHECK(pView->Draw(DVASPECT_CONTENT, -1, &info, NULL, NULL, dc.m_hDC, &rclDraw, NULL, NULL, 0) ==
          S_OK);
    CHECK(dc.GetPixel(15, 15) == RGB(255, 0, 0));
    CHECK(CTestCtrl::s_bOptimizedSeen);
    CHECK(pView->Draw(DVASPECT_CONTENT, -1, NULL, NULL, NULL, dc.m_hDC, &rclDraw, NULL, NULL, 0) ==
          S_OK);
    CHECK(!CTestCtrl::s_bOptimizedSeen);  // the container did not allow it
    CTestCtrl::s_bDrawOutside = FALSE;
    CTestCtrl::s_dwAddFlags = CTestCtrl::s_dwClearFlags = 0;
    dc.SelectObject(pOldBitmap);
    pView->Release();
  }

  // IPointerInactive, only for a control that asks for it.
  IPointerInactive* pInactive = NULL;
  CHECK(pUnk->QueryInterface(IID_IPointerInactive, (void**)&pInactive) == E_NOINTERFACE);
  CTestCtrl::s_dwAddFlags = COleControl::pointerInactive;
  CHECK(pUnk->QueryInterface(IID_IPointerInactive, (void**)&pInactive) == S_OK);
  if (pInactive != NULL) {
    DWORD dwPolicy = 0;
    CHECK(pInactive->GetActivationPolicy(&dwPolicy) == S_OK &&
          dwPolicy == POINTERINACTIVE_ACTIVATEONENTRY);
    RECT rc = {0, 0, 40, 20};
    CTestCtrl::s_nInactiveMoves = 0;
    CHECK(pInactive->OnInactiveMouseMove(&rc, 5, 5, 0) == S_OK);
    CHECK(CTestCtrl::s_nInactiveMoves == 1);
    CHECK(pInactive->OnInactiveSetCursor(&rc, 5, 5, WM_MOUSEMOVE, FALSE) == S_OK);
    pInactive->Release();
  }
  CTestCtrl::s_dwAddFlags = 0;
  pUnk->Release();
  CHECK(CTestCtrl::s_nAlive == 0);
}

void CMainFrame::TestBrowsingAndQuickActivate() {
  TestLog("property browsing and quick activation");
  CTestCtrl* pCtl = NULL;
  LPUNKNOWN pUnk = NewControl(&pCtl);
  if (pUnk == NULL || pCtl == NULL) return;
  LPPERSISTSTREAMINIT pInit = NULL;
  pUnk->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
  if (pInit != NULL) {
    pInit->InitNew();
    pInit->Release();
  }

  // IPerPropertyBrowsing.
  IPerPropertyBrowsing* pBrowse = NULL;
  CHECK(pUnk->QueryInterface(IID_IPerPropertyBrowsing, (void**)&pBrowse) == S_OK);
  if (pBrowse != NULL) {
    BSTR bstr = NULL;
    CHECK(pBrowse->GetDisplayString(DISPID_VALUE_, &bstr) == S_OK && CString(bstr) == _T("Value 7"));
    ::SysFreeString(bstr);
    bstr = NULL;
    CHECK(pBrowse->GetDisplayString(DISPID_FONT, &bstr) == S_OK && bstr != NULL &&
          ::SysStringLen(bstr) > 0);
    ::SysFreeString(bstr);
    bstr = NULL;
    CHECK(pBrowse->GetDisplayString(DISPID_LABEL, &bstr) == S_FALSE && bstr == NULL);
    CLSID clsid = GUID_NULL;
    CHECK(pBrowse->MapPropertyToPage(DISPID_FONT, &clsid) == S_OK && clsid == CLSID_CFontPropPage);
    CHECK(pBrowse->MapPropertyToPage(DISPID_BACKCOLOR, &clsid) == S_OK &&
          clsid == CLSID_CColorPropPage);
    CHECK(pBrowse->MapPropertyToPage(DISPID_VALUE_, &clsid) == PERPROP_E_NOPAGEAVAILABLE);
    CALPOLESTR strings = {0, NULL};
    CADWORD cookies = {0, NULL};
    CHECK(pBrowse->GetPredefinedStrings(DISPID_BORDERSTYLE, &strings, &cookies) == S_OK);
    CHECK(strings.cElems == 2 && cookies.cElems == 2);
    if (strings.cElems == 2 && cookies.cElems == 2) {
      CHECK(CString(strings.pElems[1]) == _T("1 - Fixed Single") && cookies.pElems[1] == 1);
      VARIANT var;
      CHECK(pBrowse->GetPredefinedValue(DISPID_BORDERSTYLE, cookies.pElems[1], &var) == S_OK);
      CHECK(var.vt == VT_I2 && var.iVal == 1);
      for (ULONG i = 0; i < strings.cElems; i++) ::CoTaskMemFree(strings.pElems[i]);
    }
    ::CoTaskMemFree(strings.pElems);
    ::CoTaskMemFree(cookies.pElems);
    CHECK(pBrowse->GetPredefinedStrings(DISPID_VALUE_, &strings, &cookies) == S_FALSE);
    pBrowse->Release();
  }

  // IQuickActivate.
  IQuickActivate* pQuick = NULL;
  CHECK(pUnk->QueryInterface(IID_IQuickActivate, (void**)&pQuick) == S_OK);
  if (pQuick != NULL) {
    CQuickSite site;
    CQuickEvents events;
    CQuickNotify notify;
    QACONTAINER qa;
    memset(&qa, 0, sizeof(qa));
    qa.cbSize = sizeof(qa);
    qa.pClientSite = &site;
    qa.pUnkEventSink = &events;
    qa.pPropertyNotifySink = &notify;
    QACONTROL qc;
    memset(&qc, 0, sizeof(qc));
    qc.cbSize = sizeof(qc);
    CHECK(pQuick->QuickActivate(&qa, &qc) == S_OK);
    CHECK(pCtl->GetClientSite() == &site);
    CHECK(qc.dwMiscStatus == kMiscStatus && qc.dwEventCookie != 0 && qc.dwPropNotifyCookie != 0);
    pCtl->FireChanged(5);
    CHECK(events.m_nEvents == 1 && events.m_dispidLast == eventidChanged);
    pCtl->SetValue(3);
    CHECK(notify.m_nChanged == 1 && notify.m_dispidLast == DISPID_VALUE_);
    SIZEL sizel = {5000, 2500};
    CHECK(pQuick->SetContentExtent(&sizel) == S_OK);
    SIZEL sizel2 = {0, 0};
    CHECK(pQuick->GetContentExtent(&sizel2) == S_OK && sizel2.cx == 5000 && sizel2.cy == 2500);
    // The cookies undo the connections.
    LPCONNECTIONPOINTCONTAINER pCPC = NULL;
    pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
    if (pCPC != NULL) {
      LPCONNECTIONPOINT pCP = NULL;
      if (SUCCEEDED(pCPC->FindConnectionPoint(IID_DTestCtrlEvents, &pCP))) {
        CHECK(pCP->Unadvise(qc.dwEventCookie) == S_OK);
        pCP->Release();
      }
      if (SUCCEEDED(pCPC->FindConnectionPoint(IID_IPropertyNotifySink, &pCP))) {
        CHECK(pCP->Unadvise(qc.dwPropNotifyCookie) == S_OK);
        pCP->Release();
      }
      pCPC->Release();
    }
    pCtl->FireChanged(6);
    CHECK(events.m_nEvents == 1);
    LPOLEOBJECT pObj = NULL;
    pUnk->QueryInterface(IID_IOleObject, (void**)&pObj);
    if (pObj != NULL) {
      pObj->SetClientSite(NULL);
      pObj->Release();
    }
    pQuick->Release();
  }
  pUnk->Release();
  CHECK(CTestCtrl::s_nAlive == 0);
}

static BOOL ReadKey(LPCTSTR lpszKey, LPCTSTR lpszValueName, CString& str) {
  HKEY hKey;
  str.Empty();
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return FALSE;
  TCHAR sz[512];
  DWORD cb = sizeof(sz);
  DWORD dwType = 0;
  LONG l = ::RegQueryValueEx(hKey, lpszValueName, NULL, &dwType, (LPBYTE)sz, &cb);
  ::RegCloseKey(hKey);
  if (l != ERROR_SUCCESS) return FALSE;
  str = sz;
  return TRUE;
}

void CMainFrame::TestRegistry() {
  TestLog("registry");
  CHECK(COleObjectFactory::UpdateRegistryAll(TRUE));
  CString strKey = _T("CLSID\\{6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F60}");
  CString str;
  CHECK(ReadKey(strKey, NULL, str) && str == _T("CtlTest Control"));
  CHECK(ReadKey(strKey + _T("\\Control"), NULL, str));
  CHECK(ReadKey(strKey + _T("\\ProgID"), NULL, str) && str == _T("CtlTest.TestCtrl.1"));
  CHECK(ReadKey(strKey + _T("\\MiscStatus\\1"), NULL, str) && _ttol(str) == (long)kMiscStatus);
  CHECK(ReadKey(strKey + _T("\\TypeLib"), NULL, str) &&
        str == _T("{6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F63}"));
  CHECK(ReadKey(strKey + _T("\\Version"), NULL, str) && str == _T("1.0"));
  CHECK(ReadKey(strKey + _T("\\ToolboxBitmap32"), NULL, str) && str.Right(5) == _T(", 101"));
  // The test is an EXE, so the server is a local one.
  CHECK(ReadKey(strKey + _T("\\LocalServer32"), NULL, str) && !str.IsEmpty());
  CHECK(ReadKey(_T("CtlTest.TestCtrl.1\\CLSID"), NULL, str) &&
        str == _T("{6A1D3F52-0E4B-4D7A-9F23-8B5E1CAD4F60}"));
  CHECK(COleObjectFactory::UpdateRegistryAll(FALSE));
  CHECK(!ReadKey(strKey, NULL, str));
  CHECK(!ReadKey(_T("CtlTest.TestCtrl.1"), NULL, str));
}

// Writes ctltest.lic beside the program, or deletes it.
static CString LicPath() {
  TCHAR sz[MAX_PATH];
  ::GetModuleFileName(NULL, sz, MAX_PATH);
  CString str = sz;
  return str.Left(str.ReverseFind('\\') + 1) + _T("ctltest.lic");
}

static void WriteLicFile(LPCSTR pszText) {
  CFile file(LicPath(), CFile::modeCreate | CFile::modeWrite);
  file.Write(pszText, (UINT)strlen(pszText));
}

void CMainFrame::TestLicensing() {
  TestLog("licensing");
  ::DeleteFile(LicPath());
  HINSTANCE hInst = AfxGetInstanceHandle();
  CHECK(!AfxVerifyLicFile(hInst, _T("ctltest.lic"), kLicText));

  LPCLASSFACTORY2 pFactory = NULL;
  HRESULT hr = ::CoGetClassObject(CLicCtrl::guid, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, NULL,
                                  IID_IClassFactory2, (void**)&pFactory);
  CHECK(SUCCEEDED(hr) && pFactory != NULL);
  if (pFactory == NULL) return;

  // No license file: no objects without the key, and no key to hand out.
  LICINFO info = {0};
  CHECK(pFactory->GetLicInfo(&info) == S_OK);
  CHECK(info.cbLicInfo == sizeof(LICINFO) && info.fRuntimeKeyAvail && !info.fLicVerified);
  LPUNKNOWN pUnk = NULL;
  CHECK(pFactory->CreateInstance(NULL, IID_IUnknown, (void**)&pUnk) == CLASS_E_NOTLICENSED);
  CHECK(pUnk == NULL);
  BSTR bstrKey = NULL;
  CHECK(pFactory->RequestLicKey(0, &bstrKey) == CLASS_E_NOTLICENSED && bstrKey == NULL);
  BSTR bstrWrong = ::SysAllocString(L"not the key");
  CHECK(pFactory->CreateInstanceLic(NULL, NULL, IID_IUnknown, bstrWrong, (void**)&pUnk) ==
        CLASS_E_NOTLICENSED);
  ::SysFreeString(bstrWrong);

  // The run-time key makes one anyway, as for a program built with a
  // licensed copy.
  BSTR bstrRight = ::SysAllocString(kLicKey);
  CHECK(pFactory->CreateInstanceLic(NULL, NULL, IID_IUnknown, bstrRight, (void**)&pUnk) == S_OK);
  CHECK(pUnk != NULL && CTestCtrl::s_nAlive == 1);
  if (pUnk != NULL) pUnk->Release();
  pUnk = NULL;
  CHECK(CTestCtrl::s_nAlive == 0);

  // A container passes the key to CreateControl.
  CWnd wnd;
  CHECK(wnd.CreateControl(CLicCtrl::guid, NULL, WS_VISIBLE, CRect(0, 0, 50, 20), this, 3001, NULL,
                          FALSE, bstrRight));
  CHECK(CTestCtrl::s_nAlive == 1 && wnd.GetControlUnknown() != NULL);
  wnd.DestroyWindow();
  PumpMessages();
  CHECK(CTestCtrl::s_nAlive == 0);
  ::SysFreeString(bstrRight);

  // A file that starts differently is not a license; the right one is, and
  // the factory sees it without restarting.
  WriteLicFile("CtlTest license: this copy may not be used.");
  CHECK(!AfxVerifyLicFile(hInst, _T("ctltest.lic"), kLicText));
  CHECK(AfxVerifyLicFile(hInst, _T("ctltest.lic"), kLicText, 17));  // "CtlTest license: "
  WriteLicFile("CtlTest license: this copy may be used to build programs.\r\nMore text.\r\n");
  CHECK(AfxVerifyLicFile(hInst, _T("ctltest.lic"), kLicText));
  CHECK(!AfxVerifyLicFile(hInst, _T("nosuch.lic"), kLicText));
  CHECK(pFactory->GetLicInfo(&info) == S_OK && info.fLicVerified);
  CHECK(pFactory->CreateInstance(NULL, IID_IUnknown, (void**)&pUnk) == S_OK && pUnk != NULL);
  if (pUnk != NULL) pUnk->Release();
  CHECK(pFactory->RequestLicKey(0, &bstrKey) == S_OK && bstrKey != NULL &&
        wcscmp(bstrKey, kLicKey) == 0);
  ::SysFreeString(bstrKey);
  pFactory->Release();
  CHECK(CTestCtrl::s_nAlive == 0);
  ::DeleteFile(LicPath());
}

// Runs the message loop until *pbDone, for at most ten seconds.
static BOOL PumpUntil(const BOOL* pbDone) {
  DWORD dwStart = ::GetTickCount();
  while (!*pbDone && ::GetTickCount() - dwStart < 10000) {
    PumpMessages();
    ::MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
  }
  return *pbDone;
}

void CMainFrame::TestDataPath() {
  TestLog("data path properties");
  // A file to download, bigger than one read.
  CString strPath = LicPath();
  strPath = strPath.Left(strPath.ReverseFind('\\') + 1) + _T("ctldata.txt");
  CStringA strData;
  for (int i = 0; i < 2000; i++) strData += "data path line\r\n";
  {
    CFile file(strPath, CFile::modeCreate | CFile::modeWrite);
    file.Write((LPCSTR)strData, strData.GetLength());
  }
  CString strURL = _T("file:///") + strPath;
  strURL.Replace('\\', '/');

  // At once.
  CMonikerFile mf;
  CHECK(mf.Open(strURL));
  CHECK(mf.GetMoniker() != NULL && mf.GetStream() != NULL);
  CStringA strRead;
  char buf[1000];
  UINT n;
  while ((n = mf.Read(buf, sizeof(buf))) > 0) strRead += CStringA(buf, n);
  CHECK(strRead == strData);
  mf.Close();
  CHECK(mf.GetMoniker() == NULL && mf.GetStream() == NULL);
  CFileException fe;
  CString strMissing = strURL.Left(strURL.ReverseFind('/') + 1) + _T("nosuch.txt");
  CHECK(!mf.Open(strMissing, &fe) && fe.m_lOsError != 0);
  CHECK(mf.GetMoniker() == NULL);

  // While the program runs.
  {
    CTestAsyncFile af;
    CHECK(af.Open(strURL));
    CHECK(PumpUntil(&af.m_bStopped));
    CHECK(af.m_bStarted);
    CHECK(af.m_hrStop == S_OK);
    CHECK(af.m_nData > 0);
    // Windows marks the last notice. Wine (as of Wine 9) sends a local
    // file in one notice that is only marked as the first.
    CHECK(af.m_bLast ||
          ::GetProcAddress(::GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL);
    CHECK(af.m_strData == strData);
    CHECK(af.GetFormatEtc() != NULL);
    CHECK(af.GetBinding() == NULL);  // let go when the binding stopped
    af.Close();
  }
  {
    // The callback aggregated in an object of the file's: urlmon gets the
    // callback through that object, which it sees as the callback's own.
    CAggAsyncFile af;
    CHECK(af.Open(strURL));
    CHECK(af.m_pOuter != NULL && af.m_pBSC != NULL);
    if (af.m_pBSC != NULL) {
      IUnknown* pUnk = NULL;
      af.m_pBSC->QueryInterface(IID_IUnknown, (void**)&pUnk);
      CHECK(pUnk == static_cast<IUnknown*>(af.m_pOuter));
      if (pUnk != NULL) pUnk->Release();
    }
    CHECK(PumpUntil(&af.m_bStopped));
    CHECK(af.m_hrStop == S_OK && af.m_strData == strData);
    CHECK(af.m_pOuter->m_nBSCQueries > 0);
    af.Close();
    PumpMessages();
    CHECK(af.m_bOuterFreed);  // and with it the callback
  }
  {
    // Once closed, nothing more is reported. (A local file can come in
    // full while Open runs.)
    CTestAsyncFile af;
    CHECK(af.Open(strURL));
    af.Close();
    int nData = af.m_nData;
    BOOL bStopped = af.m_bStopped;
    PumpMessages();
    ::Sleep(100);
    PumpMessages();
    CHECK(af.m_nData == nData && af.m_bStopped == bStopped);
  }

  // A control's data path property, from a property bag.
  LPUNKNOWN pUnk = NULL;
  HRESULT hr = ::CoCreateInstance(CDataCtrl::guid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                  IID_IUnknown, (void**)&pUnk);
  CHECK(SUCCEEDED(hr) && pUnk != NULL);
  CDataCtrl* pCtl = CDataCtrl::s_pLast;
  if (pUnk != NULL && pCtl != NULL) {
    LPPERSISTPROPERTYBAG pPPB = NULL;
    pUnk->QueryInterface(IID_IPersistPropertyBag, (void**)&pPPB);
    CHECK(pPPB != NULL);
    if (pPPB != NULL) {
      CTestPropBag bag;
      COleVariant var(strURL);
      bag.Write(L"Data", &var);
      pCtl->InternalSetReadyState(READYSTATE_LOADING);
      CHECK(pPPB->Load(&bag, NULL) == S_OK);
      CHECK(pCtl->m_data.GetPath() == strURL);
      CHECK(PumpUntil(&pCtl->m_data.m_bStopped));
      CHECK(pCtl->m_data.m_hrStop == S_OK);
      CHECK(pCtl->m_data.m_Cache.GetLength() == (ULONGLONG)strData.GetLength());
      CStringA strCache;
      pCtl->m_data.m_Cache.SeekToBegin();
      UINT nLen = (UINT)pCtl->m_data.m_Cache.GetLength();
      pCtl->m_data.m_Cache.Read(strCache.GetBuffer(nLen), nLen);
      strCache.ReleaseBuffer(nLen);
      CHECK(strCache == strData);
      CHECK(pCtl->GetReadyState() == READYSTATE_COMPLETE);

      // The path is what is saved.
      CTestPropBag saved;
      CHECK(pPPB->Save(&saved, TRUE, FALSE) == S_OK);
      int i = saved.Find(L"Data");
      CHECK(i >= 0 && saved.m_values[i].vt == VT_BSTR &&
            CString(saved.m_values[i].bstrVal) == strURL);

      // COleControl::Load starts again from nothing.
      CHECK(pCtl->Load(strURL, pCtl->m_data));
      CHECK(PumpUntil(&pCtl->m_data.m_bStopped));
      CHECK(pCtl->m_data.m_Cache.GetLength() == (ULONGLONG)strData.GetLength());
      pPPB->Release();
    }
    pUnk->Release();
  }
  CHECK(CTestCtrl::s_nAlive == 0);
  ::DeleteFile(strPath);
}

// CPictureHolder: the kinds of picture, and drawing one.
void CMainFrame::TestPictureHolder() {
  TestLog("picture holder");
  CPictureHolder pict;
  CHECK(pict.GetType() == PICTYPE_UNINITIALIZED && pict.GetPictureDispatch() == NULL);
  CHECK(pict.CreateEmpty() && pict.GetType() == PICTYPE_NONE);
  CString str;
  CHECK(pict.GetDisplayString(str) && str == _T("(None)"));
  // A bitmap the picture owns, from a CBitmap that lets go of it.
  CBitmap bitmap;
  bitmap.Attach(MakeBitmap(RGB(0, 0, 255)));
  CHECK(pict.CreateFromBitmap(&bitmap) && bitmap.GetSafeHandle() == NULL);
  CHECK(pict.GetType() == PICTYPE_BITMAP && pict.GetDisplayString(str) && str == _T("(Bitmap)"));
  CClientDC dcScreen(this);
  CDC dc;
  dc.CreateCompatibleDC(&dcScreen);
  CBitmap target;
  target.CreateCompatibleBitmap(&dcScreen, 32, 32);
  CBitmap* pOld = dc.SelectObject(&target);
  dc.FillSolidRect(0, 0, 32, 32, RGB(255, 255, 255));
  CRect rc(0, 0, 32, 32);
  pict.Render(&dc, rc, rc);
  COLORREF clr = dc.GetPixel(16, 16);
  TestLog("rendered pixel %06lx", (unsigned long)clr);
  CHECK(clr == RGB(0, 0, 255));
  dc.SelectObject(pOld);
  // The dispatch interface, into another holder.
  LPPICTUREDISP pDisp = pict.GetPictureDispatch();
  CHECK(pDisp != NULL);
  CPictureHolder other;
  other.SetPictureDispatch(pDisp);
  CHECK(other.m_pPict == pict.m_pPict);
  if (pDisp != NULL) pDisp->Release();
  // An icon, and a metafile.
  CHECK(pict.CreateFromIcon(::LoadIcon(NULL, IDI_APPLICATION)) && pict.GetType() == PICTYPE_ICON);
  CHECK(pict.GetDisplayString(str) && str == _T("(Icon)"));
  CMetaFileDC mdc;
  mdc.Create();
  mdc.Rectangle(0, 0, 100, 100);
  HMETAFILE hmf = mdc.Close();
  CHECK(pict.CreateFromMetafile(hmf, 1000, 1000, TRUE) && pict.GetType() == PICTYPE_METAFILE);
}

// The stock pages, as the control's property frame shows them: the
// properties of their kind, and Apply setting them on the control.
static LPPROPERTYPAGE StockPage(CRuntimeClass* pClass, CStockPropPage** ppPage) {
  *ppPage = (CStockPropPage*)pClass->CreateObject();
  if (*ppPage == NULL) return NULL;
  LPPROPERTYPAGE pPage = (LPPROPERTYPAGE)(*ppPage)->GetInterface(&IID_IPropertyPage);
  return pPage;  // the creation reference
}

static void SendCommand(HWND hPage, int nID, UINT nCode) {
  ::SendMessage(hPage, WM_COMMAND, MAKEWPARAM(nID, nCode), (LPARAM)::GetDlgItem(hPage, nID));
}

static CString ComboText(HWND hPage, int nID) {
  CString str;
  CComboBox* pCombo = (CComboBox*)CWnd::FromHandle(::GetDlgItem(hPage, nID));
  int i = pCombo->GetCurSel();
  if (i >= 0) pCombo->GetLBText(i, str);
  return str;
}

void CMainFrame::TestStockPages() {
  TestLog("stock pages");
  CTestCtrl* pCtl = NULL;
  LPUNKNOWN pUnkCtl = NewControl(&pCtl);
  if (pUnkCtl == NULL || pCtl == NULL) return;
  LPPERSISTSTREAMINIT pInit = NULL;
  pUnkCtl->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
  if (pInit != NULL) {
    pInit->InitNew();
    pInit->Release();
  }
  CTestPageSite site;

  // Colors: BackColor and ForeColor, the first shown.
  CStockPropPage* pObj = NULL;
  LPPROPERTYPAGE pPage = StockPage(RUNTIME_CLASS(CColorPropPage), &pObj);
  CHECK(pPage != NULL);
  if (pPage != NULL) {
    CColorPropPage* pColors = (CColorPropPage*)pObj;
    PROPPAGEINFO info;
    memset(&info, 0, sizeof(info));
    CHECK(pPage->GetPageInfo(&info) == S_OK && CString(info.pszTitle) == _T("Colors"));
    CHECK(info.size.cx > 200 && info.size.cy > 80);
    ::CoTaskMemFree(info.pszTitle);
    pCtl->SetBackColor(RGB(0, 128, 0));
    pCtl->SetForeColor(0x80000000 | COLOR_WINDOWTEXT);
    pPage->SetPageSite(&site);
    CHECK(pPage->SetObjects(1, &pUnkCtl) == S_OK);
    CRect rcPage(0, 0, info.size.cx, info.size.cy);
    CHECK(pPage->Activate(m_hWnd, &rcPage, FALSE) == S_OK);
    HWND hPage = pObj->m_hWnd;
    CHECK(hPage != NULL);
    if (hPage != NULL) {
      CComboBox* pProps =
          (CComboBox*)CWnd::FromHandle(::GetDlgItem(hPage, CColorPropPage::IDC_PROPNAME));
      TestLog("color properties %d, first %s", pProps->GetCount(),
              (const char*)CStringA(ComboText(hPage, CColorPropPage::IDC_PROPNAME)));
      CHECK(pProps->GetCount() == 2 && pProps->FindStringExact(-1, _T("ForeColor")) >= 0);
      CHECK(ComboText(hPage, CColorPropPage::IDC_PROPNAME) == _T("BackColor"));
      CHECK(pColors->GetColor() == RGB(0, 128, 0) && pPage->IsPageDirty() == S_FALSE);
      // A palette color for BackColor.
      SendCommand(hPage, CColorPropPage::IDC_COLOR0 + 9, BN_CLICKED);  // red
      CHECK(pColors->GetColor() == RGB(255, 0, 0) && pPage->IsPageDirty() == S_OK);
      // ForeColor shows its system color, then takes another.
      pProps->SelectString(-1, _T("ForeColor"));
      SendCommand(hPage, CColorPropPage::IDC_PROPNAME, CBN_SELCHANGE);
      CHECK(pColors->GetColor() == (0x80000000 | COLOR_WINDOWTEXT));
      CHECK(ComboText(hPage, CColorPropPage::IDC_SYSCOLORS) == _T("Window Text"));
      CComboBox* pSys =
          (CComboBox*)CWnd::FromHandle(::GetDlgItem(hPage, CColorPropPage::IDC_SYSCOLORS));
      pSys->SelectString(-1, _T("Highlight"));
      SendCommand(hPage, CColorPropPage::IDC_SYSCOLORS, CBN_SELCHANGE);
      CHECK(pColors->GetColor() == (0x80000000 | COLOR_HIGHLIGHT));
      CHECK(pCtl->GetBackColor() == RGB(0, 128, 0));  // not until Apply
      pPage->Show(SW_SHOW);
      PumpMessages();
      TestSaveWindowBitmap(hPage, _T("ctltest-colorpage.bmp"));
      CHECK(pPage->Apply() == S_OK);
      TestLog("applied colors %08lx %08lx", (unsigned long)pCtl->GetBackColor(),
              (unsigned long)pCtl->GetForeColor());
      CHECK(pCtl->GetBackColor() == RGB(255, 0, 0));
      CHECK(pCtl->GetForeColor() == (0x80000000 | COLOR_HIGHLIGHT));
      CHECK(pPage->IsPageDirty() == S_FALSE);
      // EditProperty selects the property.
      LPPROPERTYPAGE2 pPage2 = NULL;
      pPage->QueryInterface(IID_IPropertyPage2, (void**)&pPage2);
      CHECK(pPage2 != NULL && pPage2->EditProperty(DISPID_BACKCOLOR) == S_OK);
      if (pPage2 != NULL) pPage2->Release();
      CHECK(ComboText(hPage, CColorPropPage::IDC_PROPNAME) == _T("BackColor"));
    }
    pPage->Deactivate();
    pPage->SetObjects(0, NULL);
    pPage->SetPageSite(NULL);
    pPage->Release();
  }

  // Fonts: the Font property's font object, changed in place.
  pPage = StockPage(RUNTIME_CLASS(CFontPropPage), &pObj);
  CHECK(pPage != NULL);
  if (pPage != NULL) {
    CHECK(pPage->SetObjects(1, &pUnkCtl) == S_OK);
    CRect rcPage(0, 0, 400, 200);
    CHECK(pPage->Activate(m_hWnd, &rcPage, FALSE) == S_OK);
    HWND hPage = pObj->m_hWnd;
    CFontPropPage* pFonts = (CFontPropPage*)pObj;
    LPFONT pFont = pCtl->InternalGetFont().m_pFont;
    CHECK(hPage != NULL && pFont != NULL);
    if (hPage != NULL && pFont != NULL) {
      CHECK(ComboText(hPage, CFontPropPage::IDC_PROPNAME) == _T("Font"));
      BSTR bstr = NULL;
      pFont->get_Name(&bstr);
      CString strName(bstr);
      ::SysFreeString(bstr);
      TestLog("font %s; page %s", (const char*)CStringA(strName),
              (const char*)CStringA(pFonts->GetFontState().strName));
      CHECK(pFonts->GetFontState().strName == strName);
      CHECK(ComboText(hPage, CFontPropPage::IDC_STYLE) == _T("Regular"));
      // Bold Italic, underlined, 14 points.
      CComboBox* pStyle =
          (CComboBox*)CWnd::FromHandle(::GetDlgItem(hPage, CFontPropPage::IDC_STYLE));
      pStyle->SetCurSel(3);
      SendCommand(hPage, CFontPropPage::IDC_STYLE, CBN_SELCHANGE);
      ::SendDlgItemMessage(hPage, CFontPropPage::IDC_UNDERLINE, BM_CLICK, 0, 0);
      CComboBox* pSize =
          (CComboBox*)CWnd::FromHandle(::GetDlgItem(hPage, CFontPropPage::IDC_FONTSIZE));
      pSize->SelectString(-1, _T("14"));
      SendCommand(hPage, CFontPropPage::IDC_FONTSIZE, CBN_SELCHANGE);
      CHECK(pPage->IsPageDirty() == S_OK);
      pPage->Show(SW_SHOW);
      PumpMessages();
      TestSaveWindowBitmap(hPage, _T("ctltest-fontpage.bmp"));
      CHECK(pPage->Apply() == S_OK);
      BOOL bBold = FALSE, bItalic = FALSE, bUnderline = FALSE, bStrike = TRUE;
      CY cy;
      cy.int64 = 0;
      pFont->get_Bold(&bBold);
      pFont->get_Italic(&bItalic);
      pFont->get_Underline(&bUnderline);
      pFont->get_Strikethrough(&bStrike);
      pFont->get_Size(&cy);
      TestLog("font after: bold %d italic %d underline %d strike %d size %ld", bBold, bItalic,
              bUnderline, bStrike, (long)cy.int64);
      // Wine's font object keeps the size the font's pixel height gives.
      CHECK(bBold && bItalic && bUnderline && !bStrike);
      CHECK(cy.int64 >= 135000 && cy.int64 <= 145000);
    }
    pPage->Deactivate();
    pPage->SetObjects(0, NULL);
    pPage->Release();
  }

  // Pictures: from a file, then cleared.
  pPage = StockPage(RUNTIME_CLASS(CPicturePropPage), &pObj);
  CHECK(pPage != NULL);
  if (pPage != NULL) {
    CHECK(pPage->SetObjects(1, &pUnkCtl) == S_OK);
    CRect rcPage(0, 0, 400, 200);
    CHECK(pPage->Activate(m_hWnd, &rcPage, FALSE) == S_OK);
    HWND hPage = pObj->m_hWnd;
    CPicturePropPage* pPictures = (CPicturePropPage*)pObj;
    CHECK(hPage != NULL);
    if (hPage != NULL) {
      CHECK(ComboText(hPage, CPicturePropPage::IDC_PROPNAME) == _T("Picture"));
      CHECK(pCtl->m_pict.GetType() == PICTYPE_NONE);
      // A bitmap file of the test's own.
      CString strPath = TestOutPath(_T("ctltest-picture.bmp"));
      WriteBitmapFile(strPath, RGB(0, 255, 0));
      CHECK(pPictures->LoadPictureFile(strPath));
      CHECK(pPage->IsPageDirty() == S_OK && pCtl->m_pict.GetType() == PICTYPE_NONE);
      pPage->Show(SW_SHOW);
      PumpMessages();
      TestSaveWindowBitmap(hPage, _T("ctltest-picturepage.bmp"));
      CHECK(pPage->Apply() == S_OK);
      TestLog("picture after apply: type %d", pCtl->m_pict.GetType());
      CHECK(pCtl->m_pict.GetType() == PICTYPE_BITMAP);
      SendCommand(hPage, CPicturePropPage::IDC_CLEAR, BN_CLICKED);
      CHECK(pPage->Apply() == S_OK && pCtl->m_pict.GetType() == PICTYPE_NONE);
      ::DeleteFile(strPath);
    }
    pPage->Deactivate();
    pPage->SetObjects(0, NULL);
    pPage->Release();
  }
  pUnkCtl->Release();
  CHECK(CTestCtrl::s_nAlive == 0);
}

void CMainFrame::RunTests() {
  TestHosted();
  TestPropertyPage();
  TestPropertyFrame();
  TestViewAndPointer();
  TestPictureHolder();
  TestStockPages();
  TestBrowsingAndQuickActivate();
  TestLicensing();
  TestDataPath();
  TestPersistence();
  TestRegistry();
}

// =====================================================================

class CTestApp : public COleControlModule {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("ctltest.log"));
    if (!COleControlModule::InitInstance()) return FALSE;
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    CHECK(COleObjectFactory::RegisterAll());
    CHECK(IsKindOf(RUNTIME_CLASS(COleControlModule)));
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("ctltest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 400, 300)))
      return FALSE;
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    COleObjectFactory::RevokeAll();
    CHECK(CTestCtrl::s_nAlive == 0);
    COleControlModule::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
