// atlctltest.cpp - ATL's ActiveX controls and their host, as a program
// that makes its own controls (registered in the process) shows them:
//   - a windowed control (CComControl, stock and custom properties, a
//     property map, events, property notifications) hosted by CAxWindow
//     by CLSID, by ProgID, and attached; its ambient properties; its
//     events to IDispEventSimpleImpl and IDispEventImpl sinks; saved to a
//     stream and a property bag and loaded again, through the host too;
//   - a windowless control, drawn and given input by its host;
//   - a licensed control (CComClassFactory2, CAxWindow2);
//   - dialogs whose templates (DIALOG and DIALOGEX) name the controls by
//     CLSID, their state in DLGINIT records (with a license key, and an
//     _EX record), with a sink map advised by CAxDialogImpl;
//   - a composite control (CComCompositeControl) holding a control;
//   - a property page (IPropertyPageImpl), and a metafile from
//     IDataObject.
// Writes atlctltest.log in MFC_TEST_OUT; exits with 1 on a failure.
#include <atlbase.h>
CComModule _Module;
#include <atlcom.h>
#include <atlhost.h>
#include <atlctl.h>
#include <stdio.h>
#include <stdarg.h>
#include "atlctltest.h"

static int g_nFailures;
static FILE* g_pLog;

static void Log(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (g_pLog != NULL) {
    vfprintf(g_pLog, fmt, args);
    fputc('\n', g_pLog);
    fflush(g_pLog);
  }
  va_end(args);
}

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      Log("FAIL %s:%d: %s", __FILE__, __LINE__, #c);      \
      g_nFailures++;                                      \
    }                                                     \
  } while (0)

static LONG g_nLive;  // the controls alive

// ---------------------------------------------------------------------
// The type library: ICtlTest, _ICtlTestEvents and the coclass.

static ITypeInfo* DispatchTypeInfo() {
  ITypeLib* pStdLib = NULL;
  ITypeInfo* pDispInfo = NULL;
  if (SUCCEEDED(::LoadTypeLib(L"stdole2.tlb", &pStdLib))) {
    pStdLib->GetTypeInfoOfGuid(IID_IDispatch, &pDispInfo);
    pStdLib->Release();
  }
  return pDispInfo;
}

static HRESULT AddFunc(ICreateTypeInfo* pcti, UINT index, MEMBERID memid, INVOKEKIND invkind,
                       FUNCKIND funckind, VARTYPE vtRet, LPCWSTR lpszName, ELEMDESC* pParam,
                       LPCWSTR pszParam) {
  FUNCDESC fd = {};
  fd.memid = memid;
  fd.funckind = funckind;
  fd.invkind = invkind;
  fd.callconv = CC_STDCALL;
  fd.cParams = pParam != NULL ? 1 : 0;
  fd.lprgelemdescParam = pParam;
  fd.elemdescFunc.tdesc.vt = vtRet;
  fd.oVft = (SHORT)((7 + index) * sizeof(void*));
  HRESULT hr = pcti->AddFuncDesc(index, &fd);
  if (FAILED(hr)) return hr;
  LPOLESTR names[2] = {(LPOLESTR)lpszName, (LPOLESTR)pszParam};
  UINT n = invkind == INVOKE_PROPERTYPUT || pParam == NULL ? 1 : 2;
  return pcti->SetFuncAndParamNames(index, names, n);
}

static BOOL WriteTypeLib(LPCWSTR lpszFile) {
  CComPtr<ICreateTypeLib2> spctl;
  if (FAILED(::CreateTypeLib2(SYS_WIN32, lpszFile, &spctl))) return FALSE;
  spctl->SetGuid(LIBID_AtlCtlLib);
  spctl->SetVersion(1, 0);
  spctl->SetName((LPOLESTR)L"AtlCtlLib");
  spctl->SetLcid(LOCALE_NEUTRAL);
  CComPtr<ITypeInfo> spDisp;
  spDisp.p = DispatchTypeInfo();
  if (spDisp == NULL) return FALSE;

  CComPtr<ICreateTypeInfo> spCtl;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"ICtlTest", TKIND_INTERFACE, &spCtl))) return FALSE;
  spCtl->SetGuid(IID_ICtlTest);
  HREFTYPE hRef = 0;
  if (FAILED(spCtl->AddRefTypeInfo(spDisp, &hRef)) || FAILED(spCtl->AddImplType(0, hRef)))
    return FALSE;
  spCtl->SetTypeFlags(TYPEFLAG_FDUAL | TYPEFLAG_FOLEAUTOMATION);
  TYPEDESC tdLong = {}, tdBstr = {};
  tdLong.vt = VT_I4;
  tdBstr.vt = VT_BSTR;
  ELEMDESC inLong = {}, inBstr = {}, outLong = {}, outBstr = {};
  inLong.tdesc.vt = VT_I4;
  inLong.paramdesc.wParamFlags = PARAMFLAG_FIN;
  inBstr.tdesc.vt = VT_BSTR;
  inBstr.paramdesc.wParamFlags = PARAMFLAG_FIN;
  outLong.tdesc.vt = VT_PTR;
  outLong.tdesc.lptdesc = &tdLong;
  outLong.paramdesc.wParamFlags = PARAMFLAG_FOUT | PARAMFLAG_FRETVAL;
  outBstr.tdesc.vt = VT_PTR;
  outBstr.tdesc.lptdesc = &tdBstr;
  outBstr.paramdesc.wParamFlags = PARAMFLAG_FOUT | PARAMFLAG_FRETVAL;
  HRESULT hr = AddFunc(spCtl, 0, DISPID_FILLCOLOR, INVOKE_PROPERTYPUT, FUNC_PUREVIRTUAL,
                       VT_HRESULT, L"FillColor", &inLong, L"clr");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 1, DISPID_FILLCOLOR, INVOKE_PROPERTYGET, FUNC_PUREVIRTUAL, VT_HRESULT,
                 L"FillColor", &outLong, L"pclr");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 2, DISPID_CAPTION, INVOKE_PROPERTYPUT, FUNC_PUREVIRTUAL, VT_HRESULT,
                 L"Caption", &inBstr, L"bstr");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 3, DISPID_CAPTION, INVOKE_PROPERTYGET, FUNC_PUREVIRTUAL, VT_HRESULT,
                 L"Caption", &outBstr, L"pbstr");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 4, 1, INVOKE_PROPERTYPUT, FUNC_PUREVIRTUAL, VT_HRESULT, L"Sides", &inLong,
                 L"n");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 5, 1, INVOKE_PROPERTYGET, FUNC_PUREVIRTUAL, VT_HRESULT, L"Sides",
                 &outLong, L"pn");
  if (SUCCEEDED(hr))
    hr = AddFunc(spCtl, 6, 2, INVOKE_FUNC, FUNC_PUREVIRTUAL, VT_HRESULT, L"Click", &inLong, L"n");
  if (FAILED(hr)) {
    Log("note: type library functions: %08lx", (unsigned long)hr);
    return FALSE;
  }
  spCtl->LayOut();

  CComPtr<ICreateTypeInfo> spEvents;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"_ICtlTestEvents", TKIND_DISPATCH, &spEvents)))
    return FALSE;
  spEvents->SetGuid(DIID__ICtlTestEvents);
  if (FAILED(spEvents->AddRefTypeInfo(spDisp, &hRef)) || FAILED(spEvents->AddImplType(0, hRef)))
    return FALSE;
  FUNCDESC fd = {};
  fd.memid = 1;
  fd.funckind = FUNC_DISPATCH;
  fd.invkind = INVOKE_FUNC;
  fd.callconv = CC_STDCALL;
  fd.cParams = 1;
  fd.lprgelemdescParam = &inLong;
  fd.elemdescFunc.tdesc.vt = VT_VOID;
  if (FAILED(spEvents->AddFuncDesc(0, &fd))) return FALSE;
  LPOLESTR evNames[2] = {(LPOLESTR)L"Clicked", (LPOLESTR)L"n"};
  spEvents->SetFuncAndParamNames(0, evNames, 2);
  spEvents->LayOut();

  CComPtr<ICreateTypeInfo> spClass;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"CtlTest", TKIND_COCLASS, &spClass))) return FALSE;
  spClass->SetGuid(CLSID_CtlTest);
  CComQIPtr<ITypeInfo> spCtlInfo(spCtl), spEvInfo(spEvents);
  HREFTYPE hCtl = 0, hEv = 0;
  if (spCtlInfo == NULL || spEvInfo == NULL || FAILED(spClass->AddRefTypeInfo(spCtlInfo, &hCtl)) ||
      FAILED(spClass->AddRefTypeInfo(spEvInfo, &hEv)) || FAILED(spClass->AddImplType(0, hCtl)) ||
      FAILED(spClass->AddImplType(1, hEv)))
    return FALSE;
  spClass->SetImplTypeFlags(0, IMPLTYPEFLAG_FDEFAULT);
  spClass->SetImplTypeFlags(1, IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE);
  spClass->LayOut();
  return SUCCEEDED(spctl->SaveAllChanges());
}

// ---------------------------------------------------------------------
// The windowed control, as the wizard writes one.

template <class T>
class CProxy_ICtlTestEvents
    : public IConnectionPointImpl<T, &DIID__ICtlTestEvents, CComDynamicUnkArray> {
public:
  HRESULT Fire_Clicked(long n) {
    T* pT = static_cast<T*>(this);
    int nConnections = this->m_vec.GetSize();
    for (int i = 0; i < nConnections; i++) {
      pT->Lock();
      CComPtr<IUnknown> sp = this->m_vec.GetAt(i);
      pT->Unlock();
      IDispatch* pDispatch = reinterpret_cast<IDispatch*>(sp.p);
      if (pDispatch == NULL) continue;
      CComVariant arg(n);
      DISPPARAMS dp = {&arg, NULL, 1, 0};
      pDispatch->Invoke(1, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    }
    return S_OK;
  }
};

class CCtlTest;
static CCtlTest* g_pLastCtl;

class ATL_NO_VTABLE CCtlTest
    : public CComObjectRootEx<CComSingleThreadModel>,
      public CStockPropImpl<CCtlTest, ICtlTest, &IID_ICtlTest, &LIBID_AtlCtlLib>,
      public CComControl<CCtlTest>,
      public IPersistStreamInitImpl<CCtlTest>,
      public IOleControlImpl<CCtlTest>,
      public IOleObjectImpl<CCtlTest>,
      public IOleInPlaceActiveObjectImpl<CCtlTest>,
      public IViewObjectExImpl<CCtlTest>,
      public IOleInPlaceObjectWindowlessImpl<CCtlTest>,
      public IConnectionPointContainerImpl<CCtlTest>,
      public IPersistStorageImpl<CCtlTest>,
      public ISpecifyPropertyPagesImpl<CCtlTest>,
      public IQuickActivateImpl<CCtlTest>,
      public IDataObjectImpl<CCtlTest>,
      public IProvideClassInfo2Impl<&CLSID_CtlTest, &DIID__ICtlTestEvents, &LIBID_AtlCtlLib>,
      public IPropertyNotifySinkCP<CCtlTest>,
      public CComCoClass<CCtlTest, &CLSID_CtlTest>,
      public CProxy_ICtlTestEvents<CCtlTest>,
      public IPersistPropertyBagImpl<CCtlTest> {
public:
  OLE_COLOR m_clrFillColor;
  CComBSTR m_bstrCaption;
  long m_nSides;
  int m_nDraws;
  int m_nAmbientChanges;
  int m_nFillChanges;

  CCtlTest() : m_clrFillColor(0), m_nSides(3), m_nDraws(0), m_nAmbientChanges(0), m_nFillChanges(0) {
    m_bWindowOnly = TRUE;
  }

  DECLARE_NO_REGISTRY()
  DECLARE_PROTECT_FINAL_CONSTRUCT()
  DECLARE_OLEMISC_STATUS(OLEMISC_RECOMPOSEONRESIZE | OLEMISC_CANTLINKINSIDE | OLEMISC_INSIDEOUT |
                         OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST)

  BEGIN_COM_MAP(CCtlTest)
    COM_INTERFACE_ENTRY(ICtlTest)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY(IViewObjectEx)
    COM_INTERFACE_ENTRY(IViewObject2)
    COM_INTERFACE_ENTRY(IViewObject)
    COM_INTERFACE_ENTRY(IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceObject)
    COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceActiveObject)
    COM_INTERFACE_ENTRY(IOleControl)
    COM_INTERFACE_ENTRY(IOleObject)
    COM_INTERFACE_ENTRY(IPersistStreamInit)
    COM_INTERFACE_ENTRY2(IPersist, IPersistStreamInit)
    COM_INTERFACE_ENTRY(IConnectionPointContainer)
    COM_INTERFACE_ENTRY(ISpecifyPropertyPages)
    COM_INTERFACE_ENTRY(IQuickActivate)
    COM_INTERFACE_ENTRY(IPersistStorage)
    COM_INTERFACE_ENTRY(IDataObject)
    COM_INTERFACE_ENTRY(IProvideClassInfo)
    COM_INTERFACE_ENTRY(IProvideClassInfo2)
    COM_INTERFACE_ENTRY(IPersistPropertyBag)
  END_COM_MAP()

  BEGIN_PROP_MAP(CCtlTest)
    PROP_DATA_ENTRY("_cx", m_sizeExtent.cx, VT_UI4)
    PROP_DATA_ENTRY("_cy", m_sizeExtent.cy, VT_UI4)
    PROP_DATA_ENTRY("Sides", m_nSides, VT_I4)
    PROP_ENTRY("FillColor", DISPID_FILLCOLOR, CLSID_CtlPage)
    PROP_ENTRY("Caption", DISPID_CAPTION, CLSID_NULL)
  END_PROP_MAP()

  BEGIN_CONNECTION_POINT_MAP(CCtlTest)
    CONNECTION_POINT_ENTRY(DIID__ICtlTestEvents)
    CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
  END_CONNECTION_POINT_MAP()

  BEGIN_MSG_MAP(CCtlTest)
    CHAIN_MSG_MAP(CComControl<CCtlTest>)
    DEFAULT_REFLECTION_HANDLER()
  END_MSG_MAP()

  DECLARE_VIEW_STATUS(VIEWSTATUS_SOLIDBKGND | VIEWSTATUS_OPAQUE)

  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    g_pLastCtl = this;
    return S_OK;
  }
  void FinalRelease() {
    if (g_pLastCtl == this) g_pLastCtl = NULL;
    InterlockedDecrement(&g_nLive);
  }

  HRESULT OnDraw(ATL_DRAWINFO& di) override {
    m_nDraws++;
    RECT rc = {di.prcBounds->left, di.prcBounds->top, di.prcBounds->right, di.prcBounds->bottom};
    COLORREF clr;
    ::OleTranslateColor(m_clrFillColor, NULL, &clr);
    HBRUSH hbr = ::CreateSolidBrush(clr);
    ::FillRect(di.hdcDraw, &rc, hbr);
    ::DeleteObject(hbr);
    return S_OK;
  }
  void OnFillColorChanged() { m_nFillChanges++; }

  STDMETHOD(OnAmbientPropertyChange)(DISPID dispid) override {
    m_nAmbientChanges++;
    return IOleControlImpl<CCtlTest>::OnAmbientPropertyChange(dispid);
  }

  STDMETHOD(put_Sides)(long n) override {
    if (n < 3) return Error(L"too few sides", IID_ICtlTest, E_INVALIDARG);
    m_nSides = n;
    SetDirty(TRUE);
    FireOnChanged(1);
    FireViewChange();
    return S_OK;
  }
  STDMETHOD(get_Sides)(long* pn) override {
    if (pn == NULL) return E_POINTER;
    *pn = m_nSides;
    return S_OK;
  }
  STDMETHOD(Click)(long n) override { return Fire_Clicked(n); }
};

// ---------------------------------------------------------------------
// The windowless control: drawn and given its input by its site.

class CCtlLess;
static CCtlLess* g_pLess;

class ATL_NO_VTABLE CCtlLess : public CComObjectRootEx<CComSingleThreadModel>,
                               public CComControl<CCtlLess>,
                               public IPersistStreamInitImpl<CCtlLess>,
                               public IOleControlImpl<CCtlLess>,
                               public IOleObjectImpl<CCtlLess>,
                               public IOleInPlaceActiveObjectImpl<CCtlLess>,
                               public IViewObjectExImpl<CCtlLess>,
                               public IOleInPlaceObjectWindowlessImpl<CCtlLess>,
                               public CComCoClass<CCtlLess, &CLSID_CtlLess> {
public:
  int m_nDraws;
  int m_nClicks;
  CCtlLess() : m_nDraws(0), m_nClicks(0) {}

  DECLARE_NO_REGISTRY()
  DECLARE_OLEMISC_STATUS(OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST |
                         OLEMISC_INSIDEOUT)
  BEGIN_COM_MAP(CCtlLess)
    COM_INTERFACE_ENTRY(IViewObjectEx)
    COM_INTERFACE_ENTRY(IViewObject2)
    COM_INTERFACE_ENTRY(IViewObject)
    COM_INTERFACE_ENTRY(IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceObject)
    COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceActiveObject)
    COM_INTERFACE_ENTRY(IOleControl)
    COM_INTERFACE_ENTRY(IOleObject)
    COM_INTERFACE_ENTRY(IPersistStreamInit)
  END_COM_MAP()
  BEGIN_PROP_MAP(CCtlLess)
  END_PROP_MAP()
  BEGIN_MSG_MAP(CCtlLess)
    MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
    CHAIN_MSG_MAP(CComControl<CCtlLess>)
  END_MSG_MAP()

  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    g_pLess = this;
    return S_OK;
  }
  void FinalRelease() {
    if (g_pLess == this) g_pLess = NULL;
    InterlockedDecrement(&g_nLive);
  }
  HRESULT OnDraw(ATL_DRAWINFO& di) {
    m_nDraws++;
    return CComControlBase::OnDraw(di);
  }
  LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&) {
    m_nClicks++;
    return 0;
  }
};

// ---------------------------------------------------------------------
// The licensed control: made only with the key "key".

class CTestLicense {
public:
  static BOOL VerifyLicenseKey(BSTR bstr) { return bstr != NULL && lstrcmpW(bstr, L"key") == 0; }
  static BOOL GetLicenseKey(DWORD, BSTR* pbstr) {
    *pbstr = ::SysAllocString(L"key");
    return TRUE;
  }
  static BOOL IsLicenseValid() { return FALSE; }
};

class ATL_NO_VTABLE CCtlLic : public CComObjectRootEx<CComSingleThreadModel>,
                              public CComControl<CCtlLic>,
                              public IPersistStreamInitImpl<CCtlLic>,
                              public IOleControlImpl<CCtlLic>,
                              public IOleObjectImpl<CCtlLic>,
                              public IOleInPlaceActiveObjectImpl<CCtlLic>,
                              public IViewObjectExImpl<CCtlLic>,
                              public IOleInPlaceObjectWindowlessImpl<CCtlLic>,
                              public CComCoClass<CCtlLic, &CLSID_CtlLic> {
public:
  CCtlLic() { m_bWindowOnly = TRUE; }
  DECLARE_NO_REGISTRY()
  DECLARE_CLASSFACTORY2(CTestLicense)
  BEGIN_COM_MAP(CCtlLic)
    COM_INTERFACE_ENTRY(IViewObjectEx)
    COM_INTERFACE_ENTRY(IViewObject2)
    COM_INTERFACE_ENTRY(IViewObject)
    COM_INTERFACE_ENTRY(IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceObject)
    COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceActiveObject)
    COM_INTERFACE_ENTRY(IOleControl)
    COM_INTERFACE_ENTRY(IOleObject)
    COM_INTERFACE_ENTRY(IPersistStreamInit)
  END_COM_MAP()
  BEGIN_PROP_MAP(CCtlLic)
  END_PROP_MAP()
  BEGIN_MSG_MAP(CCtlLic)
    CHAIN_MSG_MAP(CComControl<CCtlLic>)
  END_MSG_MAP()
  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    return S_OK;
  }
  void FinalRelease() { InterlockedDecrement(&g_nLive); }
};

// ---------------------------------------------------------------------
// The composite control: a dialog holding a CtlTest, whose events it
// takes.

class ATL_NO_VTABLE CCtlComp : public CComObjectRootEx<CComSingleThreadModel>,
                               public CComCompositeControl<CCtlComp>,
                               public IPersistStreamInitImpl<CCtlComp>,
                               public IOleControlImpl<CCtlComp>,
                               public IOleObjectImpl<CCtlComp>,
                               public IOleInPlaceActiveObjectImpl<CCtlComp>,
                               public IViewObjectExImpl<CCtlComp>,
                               public IOleInPlaceObjectWindowlessImpl<CCtlComp>,
                               public CComCoClass<CCtlComp, &CLSID_CtlComp>,
                               public IDispEventImpl<IDC_INNER, CCtlComp, &DIID__ICtlTestEvents,
                                                     &LIBID_AtlCtlLib, 1, 0> {
public:
  enum { IDD = IDD_COMPOSITE };
  long m_nInnerClicked;
  CCtlComp() : m_nInnerClicked(0) {
    m_bWindowOnly = TRUE;
    CalcExtent(m_sizeExtent);
  }
  DECLARE_NO_REGISTRY()
  BEGIN_COM_MAP(CCtlComp)
    COM_INTERFACE_ENTRY(IViewObjectEx)
    COM_INTERFACE_ENTRY(IViewObject2)
    COM_INTERFACE_ENTRY(IViewObject)
    COM_INTERFACE_ENTRY(IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceObject)
    COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceObjectWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceActiveObject)
    COM_INTERFACE_ENTRY(IOleControl)
    COM_INTERFACE_ENTRY(IOleObject)
    COM_INTERFACE_ENTRY(IPersistStreamInit)
  END_COM_MAP()
  BEGIN_PROP_MAP(CCtlComp)
  END_PROP_MAP()
  BEGIN_MSG_MAP(CCtlComp)
    CHAIN_MSG_MAP(CComCompositeControl<CCtlComp>)
  END_MSG_MAP()
  BEGIN_SINK_MAP(CCtlComp)
    SINK_ENTRY_EX(IDC_INNER, DIID__ICtlTestEvents, 1, &CCtlComp::OnInnerClicked)
  END_SINK_MAP()

  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    return S_OK;
  }
  void FinalRelease() { InterlockedDecrement(&g_nLive); }
  void __stdcall OnInnerClicked(long n) { m_nInnerClicked += n; }
};

// ---------------------------------------------------------------------
// The property page: Apply sets the objects' Sides.

class ATL_NO_VTABLE CCtlPage : public CComObjectRootEx<CComSingleThreadModel>,
                               public CComCoClass<CCtlPage, &CLSID_CtlPage>,
                               public IPropertyPageImpl<CCtlPage>,
                               public CDialogImpl<CCtlPage> {
public:
  enum { IDD = IDD_PAGE };
  CCtlPage() { m_dwTitleID = IDS_PAGETITLE; }
  DECLARE_NO_REGISTRY()
  BEGIN_COM_MAP(CCtlPage)
    COM_INTERFACE_ENTRY(IPropertyPage)
  END_COM_MAP()
  BEGIN_MSG_MAP(CCtlPage)
    CHAIN_MSG_MAP(IPropertyPageImpl<CCtlPage>)
  END_MSG_MAP()
  STDMETHOD(Apply)() override {
    for (ULONG i = 0; i < m_nObjects; i++) {
      CComQIPtr<ICtlTest> sp(m_ppUnk[i]);
      if (sp != NULL) sp->put_Sides(GetDlgItemInt(IDC_PAGEEDIT, NULL, FALSE));
    }
    SetDirty(FALSE);
    return S_OK;
  }
};

BEGIN_OBJECT_MAP(ObjectMap)
  OBJECT_ENTRY(CLSID_CtlTest, CCtlTest)
  OBJECT_ENTRY(CLSID_CtlLess, CCtlLess)
  OBJECT_ENTRY(CLSID_CtlLic, CCtlLic)
  OBJECT_ENTRY(CLSID_CtlComp, CCtlComp)
  OBJECT_ENTRY(CLSID_CtlPage, CCtlPage)
END_OBJECT_MAP()

// ---------------------------------------------------------------------
// Sinks and helpers.

static _ATL_FUNC_INFO g_infoClicked = {CC_STDCALL, VT_EMPTY, 1, {VT_I4}};

// Events as the map's types say.
class CSimpleSink : public IDispEventSimpleImpl<1, CSimpleSink, &DIID__ICtlTestEvents> {
public:
  long m_nClicked;
  int m_nCalls;
  CSimpleSink() : m_nClicked(0), m_nCalls(0) {}
  void __stdcall OnClicked(long n) {
    m_nClicked += n;
    m_nCalls++;
  }
  BEGIN_SINK_MAP(CSimpleSink)
    SINK_ENTRY_INFO(1, DIID__ICtlTestEvents, 1, &CSimpleSink::OnClicked, &g_infoClicked)
  END_SINK_MAP()
};

// Events as the type library says, on the source's default interface.
class CTlbSink : public IDispEventImpl<2, CTlbSink> {
public:
  long m_nClicked;
  CTlbSink() : m_nClicked(0) {}
  virtual void __stdcall OnClicked(long n) { m_nClicked += n * 10; }
  BEGIN_SINK_MAP(CTlbSink)
    SINK_ENTRY(2, 1, &CTlbSink::OnClicked)
  END_SINK_MAP()
};

// Counts the control's property notifications; may refuse edits.
class ATL_NO_VTABLE CNotifySink : public CComObjectRootEx<CComSingleThreadModel>,
                                  public IPropertyNotifySink {
public:
  int m_nChanged;
  int m_nRequests;
  DISPID m_dispidLast;
  BOOL m_bRefuse;
  CNotifySink() : m_nChanged(0), m_nRequests(0), m_dispidLast(0), m_bRefuse(FALSE) {}
  BEGIN_COM_MAP(CNotifySink)
    COM_INTERFACE_ENTRY(IPropertyNotifySink)
  END_COM_MAP()
  STDMETHOD(OnChanged)(DISPID dispid) override {
    m_nChanged++;
    m_dispidLast = dispid;
    return S_OK;
  }
  STDMETHOD(OnRequestEdit)(DISPID) override {
    m_nRequests++;
    return m_bRefuse ? S_FALSE : S_OK;
  }
};

// A property bag of a few named values.
class ATL_NO_VTABLE CBag : public CComObjectRootEx<CComSingleThreadModel>, public IPropertyBag {
public:
  enum { kMax = 8 };
  CComBSTR m_names[kMax];
  CComVariant m_values[kMax];
  int m_n;
  CBag() : m_n(0) {}
  BEGIN_COM_MAP(CBag)
    COM_INTERFACE_ENTRY(IPropertyBag)
  END_COM_MAP()
  int Find(LPCOLESTR psz) {
    for (int i = 0; i < m_n; i++)
      if (lstrcmpiW(m_names[i], psz) == 0) return i;
    return -1;
  }
  STDMETHOD(Read)(LPCOLESTR pszPropName, VARIANT* pVar, IErrorLog*) override {
    int i = Find(pszPropName);
    if (i < 0) return E_INVALIDARG;
    VARTYPE vt = pVar->vt;
    VariantClear(pVar);
    if (vt == VT_EMPTY) return VariantCopy(pVar, &m_values[i]);
    return VariantChangeType(pVar, &m_values[i], 0, vt);
  }
  STDMETHOD(Write)(LPCOLESTR pszPropName, VARIANT* pVar) override {
    int i = Find(pszPropName);
    if (i < 0) {
      if (m_n == kMax) return E_OUTOFMEMORY;
      i = m_n++;
      m_names[i] = pszPropName;
    }
    m_values[i] = *pVar;
    return S_OK;
  }
};

// The top-level window the hosts live in.
class CFrame : public CWindowImpl<CFrame, CWindow, CFrameWinTraits> {
public:
  DECLARE_WND_CLASS(_T("AtlCtlTestFrame"))
  BEGIN_MSG_MAP(CFrame)
  END_MSG_MAP()
};

static void PumpMessages() {
  MSG msg;
  while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

static CCtlTest* CtlOf(IUnknown* pUnk) {
  CComQIPtr<ICtlTest> sp(pUnk);
  return sp != NULL ? static_cast<CCtlTest*>(sp.p) : NULL;
}

// ---------------------------------------------------------------------
// The tests.

// A control made directly: its properties, notifications, events and
// persistence, with no host.
static void TestControl() {
  CComPtr<ICtlTest> spCtl;
  CHECK(SUCCEEDED(::CoCreateInstance(CLSID_CtlTest, NULL, CLSCTX_INPROC_SERVER, IID_ICtlTest,
                                     (void**)&spCtl)));
  if (spCtl == NULL) return;
  CCtlTest* pCtl = CtlOf(spCtl);

  // Stock properties, through the vtable and through IDispatch.
  CHECK(spCtl->put_FillColor(RGB(10, 20, 30)) == S_OK);
  OLE_COLOR clr = 0;
  CHECK(spCtl->get_FillColor(&clr) == S_OK && clr == RGB(10, 20, 30));
  CHECK(pCtl->m_nFillChanges == 1 && pCtl->m_bRequiresSave);
  CComVariant v;
  DISPPARAMS dpNone = {NULL, NULL, 0, 0};
  CHECK(SUCCEEDED(spCtl->Invoke(DISPID_FILLCOLOR, IID_NULL, 0, DISPATCH_PROPERTYGET, &dpNone, &v,
                                NULL, NULL)) &&
        v.vt == VT_I4 && v.lVal == (long)RGB(10, 20, 30));
  CComVariant vCap(L"Polygon");
  DISPID dispidPut = DISPID_PROPERTYPUT;
  DISPPARAMS dpPut = {&vCap, &dispidPut, 1, 1};
  CHECK(SUCCEEDED(spCtl->Invoke(DISPID_CAPTION, IID_NULL, 0, DISPATCH_PROPERTYPUT, &dpPut, NULL,
                                NULL, NULL)));
  CHECK(pCtl->m_bstrCaption == CComBSTR(L"Polygon"));
  // A stock property the class has no member for.
  CComQIPtr<IDispatch> spDisp(spCtl);
  typedef CStockPropImpl<CCtlTest, ICtlTest, &IID_ICtlTest, &LIBID_AtlCtlLib> Stock;
  CHECK(static_cast<Stock*>(pCtl)->put_BackStyle(1) == E_NOTIMPL);
  // An error from a custom property.
  CHECK(spCtl->put_Sides(2) == E_INVALIDARG);
  CHECK(spCtl->put_Sides(5) == S_OK);

  // IPropertyNotifySink: told of changes, and asked first.
  CComObject<CNotifySink>* pNotify = NULL;
  CComObject<CNotifySink>::CreateInstance(&pNotify);
  CComPtr<IUnknown> spNotify(pNotify->GetUnknown());
  DWORD dwNotify = 0;
  CHECK(SUCCEEDED(AtlAdvise(spCtl, spNotify, IID_IPropertyNotifySink, &dwNotify)));
  spCtl->put_FillColor(RGB(1, 1, 1));
  CHECK(pNotify->m_nRequests == 1 && pNotify->m_nChanged == 1 &&
        pNotify->m_dispidLast == DISPID_FILLCOLOR);
  spCtl->put_Sides(6);
  CHECK(pNotify->m_nChanged == 2 && pNotify->m_dispidLast == 1);
  pNotify->m_bRefuse = TRUE;
  CHECK(spCtl->put_FillColor(RGB(2, 2, 2)) == S_FALSE);
  CHECK(pCtl->m_clrFillColor == RGB(1, 1, 1));
  pNotify->m_bRefuse = FALSE;
  // Frozen: no notifications.
  CComQIPtr<IOleControl> spOC(spCtl);
  spOC->FreezeEvents(TRUE);
  spCtl->put_FillColor(RGB(3, 3, 3));
  CHECK(pNotify->m_nChanged == 2);
  spOC->FreezeEvents(FALSE);
  AtlUnadvise(spCtl, IID_IPropertyNotifySink, dwNotify);

  // Events: to a sink whose map gives the types, and to one that reads
  // them from the type library, on the default source interface.
  CSimpleSink simple;
  CHECK(SUCCEEDED(simple.DispEventAdvise(spCtl)));
  CTlbSink tlb;
  CHECK(SUCCEEDED(tlb.DispEventAdvise(spCtl)));
  CHECK(InlineIsEqualGUID(tlb.m_iid, DIID__ICtlTestEvents));
  CHECK(InlineIsEqualGUID(tlb.m_libid, LIBID_AtlCtlLib));
  spCtl->Click(4);
  CHECK(simple.m_nClicked == 4 && simple.m_nCalls == 1);
  CHECK(tlb.m_nClicked == 40);
  // Through IDispatch, the argument converted.
  CComVariant vArg(L"3");
  DISPPARAMS dpArg = {&vArg, NULL, 1, 0};
  CHECK(SUCCEEDED(spCtl->Invoke(2, IID_NULL, 0, DISPATCH_METHOD, &dpArg, NULL, NULL, NULL)));
  CHECK(simple.m_nClicked == 7 && tlb.m_nClicked == 70);
  CHECK(SUCCEEDED(simple.DispEventUnadvise(spCtl)));
  CHECK(SUCCEEDED(tlb.DispEventUnadvise(spCtl)));
  spCtl->Click(1);
  CHECK(simple.m_nClicked == 7 && tlb.m_nClicked == 70);

  // The class's status and pages.
  CComQIPtr<IOleObject> spOO(spCtl);
  DWORD dwMisc = 0;
  CHECK(SUCCEEDED(spOO->GetMiscStatus(DVASPECT_CONTENT, &dwMisc)) &&
        (dwMisc & OLEMISC_SETCLIENTSITEFIRST));
  CComQIPtr<IViewObjectEx> spVO(spCtl);
  DWORD dwView = 0;
  CHECK(SUCCEEDED(spVO->GetViewStatus(&dwView)) && dwView == (VIEWSTATUS_SOLIDBKGND | VIEWSTATUS_OPAQUE));
  CComQIPtr<ISpecifyPropertyPages> spSPP(spCtl);
  CAUUID pages = {0, NULL};
  CHECK(SUCCEEDED(spSPP->GetPages(&pages)) && pages.cElems == 1 &&
        InlineIsEqualGUID(pages.pElems[0], CLSID_CtlPage));
  ::CoTaskMemFree(pages.pElems);
  SIZEL sz = {1000, 500};
  CHECK(SUCCEEDED(spOO->SetExtent(DVASPECT_CONTENT, &sz)));
  SIZEL sz2 = {0, 0};
  CHECK(SUCCEEDED(spOO->GetExtent(DVASPECT_CONTENT, &sz2)) && sz2.cx == 1000 && sz2.cy == 500);

  // A picture of it, for the clipboard.
  CComQIPtr<IDataObject> spDO(spCtl);
  FORMATETC fe = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
  STGMEDIUM stg = {};
  CHECK(spDO->QueryGetData(&fe) == S_OK);
  CHECK(SUCCEEDED(spDO->GetData(&fe, &stg)) && stg.tymed == TYMED_MFPICT && stg.hGlobal != NULL);
  if (stg.hGlobal != NULL) {
    METAFILEPICT* pMFP = (METAFILEPICT*)::GlobalLock(stg.hGlobal);
    CHECK(pMFP != NULL && pMFP->hMF != NULL && pMFP->xExt == 1000 && pMFP->yExt == 500);
    ::GlobalUnlock(stg.hGlobal);
    ::ReleaseStgMedium(&stg);
  }

  // Saved to a stream: the version, then the map's entries.
  CComQIPtr<IPersistStreamInit> spPSI(spCtl);
  CHECK(spPSI->IsDirty() == S_OK);
  CComPtr<IStream> spStream;
  ::CreateStreamOnHGlobal(NULL, TRUE, &spStream);
  CHECK(SUCCEEDED(spPSI->Save(spStream, TRUE)));
  CHECK(spPSI->IsDirty() == S_FALSE);
  LARGE_INTEGER li0 = {};
  spStream->Seek(li0, STREAM_SEEK_SET, NULL);
  DWORD dwVer = 0;
  spStream->Read(&dwVer, sizeof(dwVer), NULL);
  CHECK(dwVer == _ATL_VER);
  spStream->Seek(li0, STREAM_SEEK_SET, NULL);
  CComPtr<ICtlTest> spCtl2;
  ::CoCreateInstance(CLSID_CtlTest, NULL, CLSCTX_INPROC_SERVER, IID_ICtlTest, (void**)&spCtl2);
  CComQIPtr<IPersistStreamInit> spPSI2(spCtl2);
  CHECK(spPSI2 != NULL && SUCCEEDED(spPSI2->Load(spStream)));
  CCtlTest* pCtl2 = CtlOf(spCtl2);
  CHECK(pCtl2 != NULL && pCtl2->m_nSides == 6 && pCtl2->m_clrFillColor == RGB(3, 3, 3) &&
        pCtl2->m_bstrCaption == CComBSTR(L"Polygon") && pCtl2->m_sizeExtent.cx == 1000);

  // And to a property bag, by name.
  CComObject<CBag>* pBag = NULL;
  CComObject<CBag>::CreateInstance(&pBag);
  CComPtr<IPropertyBag> spBag;
  pBag->QueryInterface(IID_IPropertyBag, (void**)&spBag);
  CComQIPtr<IPersistPropertyBag> spPPB(spCtl);
  CHECK(SUCCEEDED(spPPB->Save(spBag, TRUE, TRUE)));
  int iSides = pBag->Find(L"Sides");
  CHECK(iSides >= 0 && pBag->m_values[iSides].vt == VT_I4 && pBag->m_values[iSides].lVal == 6);
  int iCap = pBag->Find(L"Caption");
  CHECK(iCap >= 0 && pBag->m_values[iCap].vt == VT_BSTR);
  pBag->m_values[iSides] = 12L;
  pBag->m_values[iCap] = L"Bagged";
  CComPtr<ICtlTest> spCtl3;
  ::CoCreateInstance(CLSID_CtlTest, NULL, CLSCTX_INPROC_SERVER, IID_ICtlTest, (void**)&spCtl3);
  CComQIPtr<IPersistPropertyBag> spPPB3(spCtl3);
  CHECK(spPPB3 != NULL && SUCCEEDED(spPPB3->InitNew()) && SUCCEEDED(spPPB3->Load(spBag, NULL)));
  CCtlTest* pCtl3 = CtlOf(spCtl3);
  CHECK(pCtl3 != NULL && pCtl3->m_nSides == 12 && pCtl3->m_bstrCaption == CComBSTR(L"Bagged") &&
        pCtl3->m_clrFillColor == RGB(3, 3, 3));

  // A property page: its title, and Apply.
  CComPtr<IPropertyPage> spPage;
  CHECK(SUCCEEDED(::CoCreateInstance(CLSID_CtlPage, NULL, CLSCTX_INPROC_SERVER, IID_IPropertyPage,
                                     (void**)&spPage)));
  if (spPage != NULL) {
    PROPPAGEINFO ppi = {};
    CHECK(SUCCEEDED(spPage->GetPageInfo(&ppi)) && ppi.pszTitle != NULL &&
          lstrcmpW(ppi.pszTitle, L"Sides") == 0 && ppi.size.cx > 0);
    ::CoTaskMemFree(ppi.pszTitle);
    IUnknown* apUnk[1] = {spCtl3};
    CHECK(SUCCEEDED(spPage->SetObjects(1, apUnk)));
    CFrame frame;
    RECT rcFrame = {0, 0, 300, 200};
    frame.Create(NULL, rcFrame, _T("page"));
    RECT rcPage = {0, 0, 200, 60};
    CHECK(SUCCEEDED(spPage->Activate(frame, &rcPage, FALSE)));
    CCtlPage* pPage = static_cast<CCtlPage*>(spPage.p);
    CHECK(pPage->m_hWnd != NULL && ::GetParent(pPage->m_hWnd) == frame.m_hWnd);
    pPage->SetDlgItemInt(IDC_PAGEEDIT, 8, FALSE);
    pPage->SetDirty(TRUE);
    CHECK(spPage->IsPageDirty() == S_OK);
    CHECK(SUCCEEDED(spPage->Apply()) && pCtl3->m_nSides == 8);
    CHECK(SUCCEEDED(spPage->Deactivate()));
    spPage->SetObjects(0, NULL);
    frame.DestroyWindow();
  }
}

// Controls in host windows.
static void TestHost() {
  CFrame frame;
  RECT rcFrame = {0, 0, 500, 400};
  CHECK(frame.Create(NULL, rcFrame, _T("atlctltest"), WS_OVERLAPPEDWINDOW | WS_VISIBLE) != NULL);
  RECT rc = {10, 10, 210, 110};

  // By CLSID, the name as the window's text.
  CAxWindow ax;
  CHECK(ax.Create(frame, rc, _T("{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A34}"),
                  WS_CHILD | WS_VISIBLE, 0, 101U) != NULL);
  CComPtr<ICtlTest> spCtl;
  CHECK(SUCCEEDED(ax.QueryControl(&spCtl)));
  CCtlTest* pCtl = CtlOf(spCtl);
  CHECK(pCtl != NULL);
  if (pCtl != NULL) {
    // In place, in its own window in the host's.
    CHECK(pCtl->m_bInPlaceActive && !pCtl->m_bWndLess);
    CHECK(pCtl->m_hWnd != NULL && ::GetParent(pCtl->m_hWnd) == ax.m_hWnd &&
          ::IsWindowVisible(pCtl->m_hWnd));
    RECT rcCtl;
    ::GetClientRect(pCtl->m_hWnd, &rcCtl);
    CHECK(rcCtl.right == 200 && rcCtl.bottom == 100);
    LONG_PTR h = 0;
    CHECK(pCtl->get_HWND(&h) == S_OK && (HWND)h == pCtl->m_hWnd);
    ::InvalidateRect(pCtl->m_hWnd, NULL, TRUE);
    ::UpdateWindow(pCtl->m_hWnd);
    CHECK(pCtl->m_nDraws > 0);
    // The host and the control, through the window and its parent.
    CComPtr<IUnknown> spHost;
    CHECK(SUCCEEDED(AtlAxGetHost(ax, &spHost)));
    CComPtr<IUnknown> spUnk;
    CHECK(SUCCEEDED(CWindow(frame).GetDlgControl(101, IID_IUnknown, (void**)&spUnk)) &&
          spUnk.IsEqualObject(spCtl));
    // Its ambient properties.
    BOOL bUser = FALSE;
    CHECK(SUCCEEDED(pCtl->GetAmbientUserMode(bUser)) && bUser);
    CComPtr<IAxWinAmbientDispatch> spAmb;
    CHECK(SUCCEEDED(ax.QueryHost(&spAmb)));
    if (spAmb != NULL) {
      int nChanges = pCtl->m_nAmbientChanges;
      CHECK(SUCCEEDED(spAmb->put_BackColor(RGB(1, 2, 3))));
      CHECK(pCtl->m_nAmbientChanges == nChanges + 1);
      OLE_COLOR clr = 0;
      CHECK(SUCCEEDED(pCtl->GetAmbientBackColor(clr)) && clr == RGB(1, 2, 3));
      spAmb->put_UserMode(VARIANT_FALSE);
      CHECK(SUCCEEDED(pCtl->GetAmbientUserMode(bUser)) && !bUser);
      spAmb->put_UserMode(VARIANT_TRUE);
      CComPtr<IFontDisp> spFont;
      CHECK(SUCCEEDED(pCtl->GetAmbientFontDisp(&spFont)) && spFont != NULL);
      // By name, as a script would.
      CComQIPtr<IDispatch> spAmbDisp(spAmb);
      LPOLESTR name = (LPOLESTR)L"ForeColor";
      DISPID dispid = 0;
      CHECK(SUCCEEDED(spAmbDisp->GetIDsOfNames(IID_NULL, &name, 1, 0, &dispid)) &&
            dispid == DISPID_AMBIENT_FORECOLOR);
    }
    // UI activation through the host.
    ax.SetFocus();
    PumpMessages();
    CHECK(pCtl->m_bUIActive);
  }

  // By ProgID, made in a host only window, with an event sink.
  CAxWindow ax2;
  RECT rc2 = {220, 10, 420, 110};
  CHECK(ax2.Create(frame, rc2, NULL, WS_CHILD | WS_VISIBLE) != NULL);
  CComObject<CNotifySink>* pDummy = NULL;  // (only the sink below takes events)
  (void)pDummy;
  CSimpleSink sink;
  CComPtr<IUnknown> spContainer, spControl;
  CHECK(SUCCEEDED(ax2.CreateControlEx(L"AtlCtlTest.Ctl", NULL, &spContainer, &spControl,
                                      DIID__ICtlTestEvents,
                                      (IUnknown*)static_cast<_IDispEvent*>(&sink))));
  CComQIPtr<ICtlTest> spCtl2(spControl);
  CHECK(spCtl2 != NULL && spContainer != NULL);
  if (spCtl2 != NULL) {
    spCtl2->Click(9);
    CHECK(sink.m_nClicked == 9);
  }
  // Another control in the same host: the first goes.
  LONG nBefore = g_nLive;
  CHECK(SUCCEEDED(ax2.CreateControl(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A35}")));
  spCtl2.Release();
  spControl.Release();
  CHECK(g_nLive == nBefore);  // one made, one let go

  // A saved control, loaded by the host from the stream.
  if (pCtl != NULL) {
    spCtl->put_Sides(10);
    spCtl->put_Caption(CComBSTR(L"Streamed"));
    CComQIPtr<IPersistStreamInit> spPSI(spCtl);
    CComPtr<IStream> spStream;
    ::CreateStreamOnHGlobal(NULL, TRUE, &spStream);
    CHECK(SUCCEEDED(spPSI->Save(spStream, TRUE)));
    LARGE_INTEGER li0 = {};
    spStream->Seek(li0, STREAM_SEEK_SET, NULL);
    CAxWindow ax3;
    RECT rc3 = {10, 120, 210, 220};
    ax3.Create(frame, rc3, NULL, WS_CHILD | WS_VISIBLE);
    CHECK(SUCCEEDED(ax3.CreateControl(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A34}", spStream)));
    CComPtr<ICtlTest> spCtl3;
    ax3.QueryControl(&spCtl3);
    CCtlTest* pCtl3 = CtlOf(spCtl3);
    CHECK(pCtl3 != NULL && pCtl3->m_nSides == 10 && pCtl3->m_bstrCaption == CComBSTR(L"Streamed"));
    ax3.DestroyWindow();
  }

  // Attached: a control made here.
  CComPtr<ICtlTest> spMine;
  ::CoCreateInstance(CLSID_CtlTest, NULL, CLSCTX_INPROC_SERVER, IID_ICtlTest, (void**)&spMine);
  CAxWindow ax4;
  RECT rc4 = {220, 120, 420, 220};
  ax4.Create(frame, rc4, NULL, WS_CHILD | WS_VISIBLE);
  CComPtr<IUnknown> spCont4;
  CHECK(SUCCEEDED(ax4.AttachControl(spMine, &spCont4)) && spCont4 != NULL);
  CCtlTest* pMine = CtlOf(spMine);
  CHECK(pMine != NULL && pMine->m_bInPlaceActive && pMine->m_hWnd != NULL &&
        ::GetParent(pMine->m_hWnd) == ax4.m_hWnd);
  // Resized with its host.
  ax4.MoveWindow(220, 120, 150, 60);
  if (pMine != NULL && pMine->m_hWnd != NULL) {
    RECT rcMine;
    ::GetClientRect(pMine->m_hWnd, &rcMine);
    CHECK(rcMine.right == 150 && rcMine.bottom == 60);
  }
  ax4.DestroyWindow();
  CHECK(pMine != NULL && !pMine->m_bInPlaceActive && pMine->m_spClientSite == NULL);
  spCont4.Release();
  spMine.Release();

  // Windowless: drawn by its host, and given the host's input.
  CAxWindow ax5;
  RECT rc5 = {10, 230, 210, 330};
  ax5.Create(frame, rc5, NULL, WS_CHILD | WS_VISIBLE);
  CHECK(SUCCEEDED(ax5.CreateControl(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A35}")));
  CCtlLess* pLess = g_pLess;
  CHECK(pLess != NULL && pLess->m_bInPlaceActive && pLess->m_bWndLess && pLess->m_hWnd == NULL);
  if (pLess != NULL) {
    ::InvalidateRect(ax5, NULL, TRUE);
    ::UpdateWindow(ax5);
    CHECK(pLess->m_nDraws > 0);
    ax5.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    CHECK(pLess->m_nClicks == 1);
    // Its view changed: its host redraws it.
    int nDraws = pLess->m_nDraws;
    pLess->FireViewChange();
    ::UpdateWindow(ax5);
    CHECK(pLess->m_nDraws > nDraws);
  }
  // A host that does not allow it: in a window then.
  CComPtr<IAxWinAmbientDispatch> spAmb5;
  ax5.QueryHost(&spAmb5);
  if (spAmb5 != NULL) spAmb5->put_AllowWindowlessActivation(VARIANT_FALSE);
  CHECK(SUCCEEDED(ax5.CreateControl(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A35}")));
  pLess = g_pLess;
  CHECK(pLess != NULL && !pLess->m_bWndLess && pLess->m_hWnd != NULL);
  spAmb5.Release();

  // Licensed: only with the key.
  CAxWindow2 ax6;
  RECT rc6 = {220, 230, 420, 330};
  ax6.Create(frame, rc6, NULL, WS_CHILD | WS_VISIBLE);
  CHECK(ax6.CreateControl(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A36}") == CLASS_E_NOTLICENSED);
  CHECK(SUCCEEDED(ax6.CreateControlLic(L"{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A36}", NULL, NULL,
                                       CComBSTR(L"key"))));
  CComPtr<IOleObject> spLic;
  CHECK(SUCCEEDED(ax6.QueryControl(&spLic)));
  spLic.Release();

  // A name that is nothing.
  CAxWindow ax7;
  RECT rc7 = {0, 0, 10, 10};
  ax7.Create(frame, rc7, NULL, WS_CHILD);
  CHECK(FAILED(ax7.CreateControl(L"{00000000-1111-2222-3333-444444444444}")));

  spCtl.Release();
  frame.DestroyWindow();
  PumpMessages();
}

// A modal dialog from a DIALOG template, its sink map advised by
// CAxDialogImpl.
class CAxDlg : public CAxDialogImpl<CAxDlg>,
               public IDispEventImpl<IDC_CTL, CAxDlg, &DIID__ICtlTestEvents, &LIBID_AtlCtlLib, 1, 0> {
public:
  enum { IDD = IDD_AXDLG };
  long m_nClicked;
  BOOL m_bInit;
  CAxDlg() : m_nClicked(0), m_bInit(FALSE) {}

  BEGIN_MSG_MAP(CAxDlg)
    CHAIN_MSG_MAP(CAxDialogImpl<CAxDlg>)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOK)
  END_MSG_MAP()
  BEGIN_SINK_MAP(CAxDlg)
#ifdef __starts_with_ampersand
    // As Visual C++ 6 code names it.
    SINK_ENTRY_EX(IDC_CTL, DIID__ICtlTestEvents, 1, OnClicked)
#else
    SINK_ENTRY_EX(IDC_CTL, DIID__ICtlTestEvents, 1, &CAxDlg::OnClicked)
#endif
  END_SINK_MAP()

  void __stdcall OnClicked(long n) { m_nClicked += n; }

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    m_bInit = TRUE;
    // The ordinary item, as it was.
    TCHAR sz[32] = _T("");
    GetDlgItemText(IDC_LABEL, sz, 32);
    CHECK(lstrcmp(sz, _T("Label")) == 0);
    // The control, its state from DLGINIT.
    CComPtr<ICtlTest> spCtl;
    CHECK(SUCCEEDED(GetDlgControl(IDC_CTL, IID_ICtlTest, (void**)&spCtl)));
    CCtlTest* pCtl = CtlOf(spCtl);
    CHECK(pCtl != NULL);
    CHECK(pCtl && pCtl->m_nSides == 7);
    CHECK(pCtl && pCtl->m_clrFillColor == 0x123456);
    CHECK(pCtl && pCtl->m_bstrCaption == CComBSTR(L"Hi"));
    // Its extent: the host's window's size, not the saved one.
    RECT rcHost;
    ::GetClientRect(GetDlgItem(IDC_CTL), &rcHost);
    SIZE szPix = {rcHost.right, rcHost.bottom}, szHi;
    AtlPixelToHiMetric(&szPix, &szHi);
    CHECK(pCtl && pCtl->m_sizeExtent.cx == szHi.cx && pCtl->m_sizeExtent.cy == szHi.cy);
    CHECK(pCtl != NULL && pCtl->m_hWnd != NULL && ::GetParent(pCtl->m_hWnd) == GetDlgItem(IDC_CTL));
    // Its host's window class.
    TCHAR szClass[32] = _T("");
    ::GetClassName(GetDlgItem(IDC_CTL), szClass, 32);
    CHECK(lstrcmp(szClass, _T("AtlAxWin")) == 0);
    // The licensed control, made with the key from DLGINIT.
    CComPtr<IOleObject> spLic;
    CHECK(SUCCEEDED(GetDlgControl(IDC_LIC, IID_IOleObject, (void**)&spLic)));
    ::GetClassName(GetDlgItem(IDC_LIC), szClass, 32);
    CHECK(lstrcmp(szClass, _T("AtlAxWinLic")) == 0);
    // Events, through the sink map.
    if (spCtl != NULL) spCtl->Click(5);
    CHECK(m_nClicked == 5);
    PostMessage(WM_COMMAND, IDOK);
    return 1;
  }
  LRESULT OnOK(WORD, WORD wID, HWND, BOOL&) {
    EndDialog(wID);
    return 0;
  }
};

static INT_PTR CALLBACK ExDlgProc(HWND, UINT uMsg, WPARAM, LPARAM) {
  return uMsg == WM_INITDIALOG;
}

static void TestDialogs() {
  CAxDlg dlg;
  CHECK(dlg.DoModal(NULL) == IDOK);
  CHECK(dlg.m_bInit && dlg.m_nClicked == 5);
  // Unadvised when it went.
  CHECK(dlg.m_dwEventCookie == 0xFEFEFEFE);

  // A modeless DIALOGEX, its control's record an _EX one.
  HWND hDlg = AtlAxCreateDialog(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(IDD_AXDLGEX),
                                NULL, ExDlgProc, 0);
  CHECK(hDlg != NULL);
  if (hDlg != NULL) {
    CComPtr<ICtlTest> spCtl;
    CHECK(SUCCEEDED(CWindow(hDlg).GetDlgControl(IDC_CTL, IID_ICtlTest, (void**)&spCtl)));
    CCtlTest* pCtl = CtlOf(spCtl);
    CHECK(pCtl != NULL && pCtl->m_nSides == 11 && pCtl->m_clrFillColor == 0x667788);
    spCtl.Release();
    ::DestroyWindow(hDlg);
  }
}

// A composite control, attached to a host window.
static void TestComposite() {
  CFrame frame;
  RECT rcFrame = {0, 0, 400, 300};
  frame.Create(NULL, rcFrame, _T("composite"), WS_OVERLAPPEDWINDOW | WS_VISIBLE);
  CComObject<CCtlComp>* pComp = NULL;
  CHECK(SUCCEEDED(CComObject<CCtlComp>::CreateInstance(&pComp)));
  if (pComp == NULL) return;
  pComp->AddRef();
  CHECK(pComp->m_sizeExtent.cx > 0 && pComp->m_sizeExtent.cx != 2 * 2540);
  CAxWindow ax;
  RECT rc = {10, 10, 310, 160};
  ax.Create(frame, rc, NULL, WS_CHILD | WS_VISIBLE);
  CHECK(SUCCEEDED(ax.AttachControl(pComp->GetUnknown(), NULL)));
  CHECK(pComp->m_hWnd != NULL && ::GetParent(pComp->m_hWnd) == ax.m_hWnd);
  if (pComp->m_hWnd != NULL) {
    CHECK(pComp->GetDlgItem(IDOK) != NULL);
    CComPtr<ICtlTest> spInner;
    CHECK(SUCCEEDED(pComp->GetDlgControl(IDC_INNER, IID_ICtlTest, (void**)&spInner)));
    if (spInner != NULL) spInner->Click(3);
    CHECK(pComp->m_nInnerClicked == 3);
  }
  frame.DestroyWindow();
  PumpMessages();
  pComp->Release();
}

int main() {
  char szPath[MAX_PATH];
  DWORD n = ::GetEnvironmentVariableA("MFC_TEST_OUT", szPath, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 20) lstrcpyA(szPath, ".");
  lstrcatA(szPath, "\\atlctltest.log");
  g_pLog = fopen(szPath, "w");
  ::OleInitialize(NULL);
  _Module.Init(ObjectMap, ::GetModuleHandle(NULL), &LIBID_AtlCtlLib);
  AtlAxWinInit();

  // The type library, registered; the controls, registered in the
  // process; the ProgID.
  WCHAR szTlb[MAX_PATH];
  ::GetTempPathW(MAX_PATH, szTlb);
  lstrcatW(szTlb, L"atlctltest.tlb");
  ::DeleteFileW(szTlb);
  BOOL bTlb = WriteTypeLib(szTlb);
  CHECK(bTlb);
  CComPtr<ITypeLib> spLib;
  CHECK(bTlb && SUCCEEDED(::LoadTypeLib(szTlb, &spLib)));
  if (spLib != NULL) CHECK(SUCCEEDED(::RegisterTypeLib(spLib, szTlb, NULL)));
  CHECK(SUCCEEDED(_Module.RegisterClassObjects(CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE)));
  CRegKey key;
  if (key.Create(HKEY_CLASSES_ROOT, _T("AtlCtlTest.Ctl\\CLSID")) == ERROR_SUCCESS)
    key.SetStringValue(NULL, _T("{6A1F0C2E-3B5D-4E7A-9C11-2F8D5E0B7A34}"));
  key.Close();

  TestControl();
  Log("control: %d failure(s) so far", g_nFailures);
  TestHost();
  Log("host: %d failure(s) so far", g_nFailures);
  TestDialogs();
  Log("dialogs: %d failure(s) so far", g_nFailures);
  TestComposite();
  Log("composite: %d failure(s) so far", g_nFailures);
  CHECK(g_nLive == 0);
  Log("live controls at the end: %ld", g_nLive);

  ::RegDeleteKey(HKEY_CLASSES_ROOT, _T("AtlCtlTest.Ctl\\CLSID"));
  ::RegDeleteKey(HKEY_CLASSES_ROOT, _T("AtlCtlTest.Ctl"));
  _Module.RevokeClassObjects();
  if (spLib != NULL) {
    ::UnRegisterTypeLib(LIBID_AtlCtlLib, 1, 0, LOCALE_NEUTRAL, SYS_WIN32);
    spLib.Release();
  }
  ::DeleteFileW(szTlb);
  _Module.Term();
  ::OleUninitialize();
  Log("%s: %d failure(s)", g_nFailures ? "FAIL" : "PASS", g_nFailures);
  if (g_pLog != NULL) fclose(g_pLog);
  return g_nFailures ? 1 : 0;
}
