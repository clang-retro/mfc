// occtest.cpp - ActiveX control containment. The test has a small control
// of its own (a CCmdTarget with the OLE control interfaces, registered
// with CoRegisterClassObject), and hosts it:
//   - in a frame window, with CWnd::CreateControl: properties and methods
//     through the wrapper, events and property notifications through the
//     frame's event sink map, ambient properties, focus, moving and
//     destroying;
//   - in a dialog template, beside ordinary controls: the control made on
//     WM_INITDIALOG, its saved state from the DLGINIT resource, DDX_Control
//     and the DDX_OC functions, the tab order and the disabled style.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxocc.h>
#include <olectl.h>
#include "occtest.h"
#include "testutil.h"

// {5B7C2E41-9D3A-4C6F-8E12-7A4D0B9C3E51}
static const CLSID CLSID_TestCtl = {
    0x5b7c2e41, 0x9d3a, 0x4c6f, {0x8e, 0x12, 0x7a, 0x4d, 0x0b, 0x9c, 0x3e, 0x51}};
// {5B7C2E41-9D3A-4C6F-8E12-7A4D0B9C3E52}
static const IID DIID_TestEvents = {
    0x5b7c2e41, 0x9d3a, 0x4c6f, {0x8e, 0x12, 0x7a, 0x4d, 0x0b, 0x9c, 0x3e, 0x52}};

// The control's members.
enum { DISPID_VALUE_ = 1, DISPID_FIRE = 2, DISPID_TWICE = 3, DISPID_LOCKED = 4 };
// Its events.
enum { DISPID_CLICKED = 1, DISPID_RENAMED = 2 };

static const TCHAR kCtlClass[] = _T("OccTestCtl");

// =====================================================================
// The control

class CTestCtl : public CCmdTarget {
public:
  static int s_nAlive;
  static CTestCtl* s_pLast;

  CString m_strCaption;
  long m_nValue;
  BOOL m_bLocked;  // OnRequestEdit is not asked, the value is fixed
  LPOLECLIENTSITE m_pClientSite;
  LPOLEINPLACESITE m_pIPSite;
  HWND m_hWnd;
  HWND m_hWndParent;  // what the site's GetWindow said
  CRect m_rect;
  SIZEL m_extent;
  BOOL m_bUIActive;
  BOOL m_bInitNew;
  BOOL m_bLoaded;
  BOOL m_bGotContext;
  int m_nFreeze;
  int m_nDropped;  // events not fired while frozen
  int m_nAmbientChanges;
  BOOL m_bUserMode;
  long m_nBackColor;

  CTestCtl()
      : m_nValue(0),
        m_bLocked(FALSE),
        m_pClientSite(NULL),
        m_pIPSite(NULL),
        m_hWnd(NULL),
        m_hWndParent(NULL),
        m_bUIActive(FALSE),
        m_bInitNew(FALSE),
        m_bLoaded(FALSE),
        m_bGotContext(FALSE),
        m_nFreeze(0),
        m_nDropped(0),
        m_nAmbientChanges(0),
        m_bUserMode(FALSE),
        m_nBackColor(-1) {
    m_extent.cx = m_extent.cy = 0;
    s_nAlive++;
    s_pLast = this;
    EnableAutomation();
    EnableConnections();
  }
  virtual ~CTestCtl() {
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }

  // ----- Properties, methods and events

  BSTR GetCaption() { return m_strCaption.AllocSysString(); }
  void SetCaption(LPCTSTR lpsz) {
    if (!RequestEdit(DISPID_CAPTION)) return;
    m_strCaption = lpsz;
    if (m_hWnd != NULL) ::InvalidateRect(m_hWnd, NULL, TRUE);
    Changed(DISPID_CAPTION);
    FireRenamed();
  }
  long GetValue() { return m_nValue; }
  void SetValue(long n) {
    if (!RequestEdit(DISPID_VALUE_)) return;
    m_nValue = n;
    Changed(DISPID_VALUE_);
  }
  BOOL GetEnabled() { return m_hWnd != NULL ? ::IsWindowEnabled(m_hWnd) : TRUE; }
  void SetEnabled(BOOL b) {
    if (m_hWnd != NULL) ::EnableWindow(m_hWnd, b);
  }
  void Fire(long n) { FireClicked(n); }
  long Twice(long n) { return 2 * n; }

  BOOL RequestEdit(DISPID dispid) {
    BOOL bAllow = TRUE;
    POSITION pos = m_xNotifyCP.GetStartPosition();
    while (pos != NULL) {
      IPropertyNotifySink* pSink = (IPropertyNotifySink*)m_xNotifyCP.GetNextConnection(pos);
      if (pSink != NULL && pSink->OnRequestEdit(dispid) == S_FALSE) bAllow = FALSE;
    }
    return bAllow;
  }
  void Changed(DISPID dispid) {
    POSITION pos = m_xNotifyCP.GetStartPosition();
    while (pos != NULL) {
      IPropertyNotifySink* pSink = (IPropertyNotifySink*)m_xNotifyCP.GetNextConnection(pos);
      if (pSink != NULL) pSink->OnChanged(dispid);
    }
  }
  void FireEvent(DISPID dispid, const BYTE* pbParams, ...) {
    if (m_nFreeze > 0) {
      m_nDropped++;
      return;
    }
    POSITION pos = m_xEventsCP.GetStartPosition();
    while (pos != NULL) {
      LPDISPATCH pDisp = (LPDISPATCH)m_xEventsCP.GetNextConnection(pos);
      if (pDisp == NULL) continue;
      COleDispatchDriver driver(pDisp, FALSE);
      va_list args;
      va_start(args, pbParams);
      driver.InvokeHelperV(dispid, DISPATCH_METHOD, VT_EMPTY, NULL, pbParams, args);
      va_end(args);
    }
  }
  void FireClicked(long n) {
    static const BYTE params[] = VTS_I4;
    FireEvent(DISPID_CLICKED, params, n);
  }
  void FireRenamed() {
    static const BYTE params[] = VTS_BSTR;
    FireEvent(DISPID_RENAMED, params, (LPCTSTR)m_strCaption);
  }

  // The container's ambient properties, through the client site's
  // IDispatch.
  BOOL GetAmbient(DISPID dispid, VARIANT* pvar) {
    ::VariantInit(pvar);
    if (m_pClientSite == NULL) return FALSE;
    LPDISPATCH pDisp = NULL;
    if (FAILED(m_pClientSite->QueryInterface(IID_IDispatch, (void**)&pDisp))) return FALSE;
    DISPPARAMS params = {NULL, NULL, 0, 0};
    HRESULT hr = pDisp->Invoke(dispid, IID_NULL, 0, DISPATCH_PROPERTYGET, &params, pvar, NULL,
                               NULL);
    pDisp->Release();
    return SUCCEEDED(hr);
  }
  void ReadAmbients() {
    VARIANT var;
    if (GetAmbient(DISPID_AMBIENT_USERMODE, &var) && var.vt == VT_BOOL)
      m_bUserMode = var.boolVal != VARIANT_FALSE;
    ::VariantClear(&var);
    if (GetAmbient(DISPID_AMBIENT_BACKCOLOR, &var) && SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_I4)))
      m_nBackColor = var.lVal;
    ::VariantClear(&var);
  }

  // ----- Activation

  HRESULT InPlaceActivate(LPOLECLIENTSITE pSite, HWND hWndParent, LPCRECT lprcPos) {
    if (m_hWnd != NULL) return S_OK;
    if (pSite == NULL) pSite = m_pClientSite;
    if (pSite == NULL) return E_UNEXPECTED;
    LPOLEINPLACESITE pIPSite = NULL;
    if (FAILED(pSite->QueryInterface(IID_IOleInPlaceSite, (void**)&pIPSite))) return E_NOINTERFACE;
    if (pIPSite->CanInPlaceActivate() != S_OK) {
      pIPSite->Release();
      return E_FAIL;
    }
    pIPSite->OnInPlaceActivate();
    LPOLEINPLACEFRAME pFrame = NULL;
    LPOLEINPLACEUIWINDOW pDoc = NULL;
    RECT rcPos, rcClip;
    OLEINPLACEFRAMEINFO info;
    memset(&info, 0, sizeof(info));
    info.cb = sizeof(info);
    if (SUCCEEDED(pIPSite->GetWindowContext(&pFrame, &pDoc, &rcPos, &rcClip, &info))) {
      m_bGotContext = pFrame != NULL;
      if (pFrame != NULL) pFrame->Release();
      if (pDoc != NULL) pDoc->Release();
    } else if (lprcPos != NULL) {
      rcPos = *lprcPos;
    }
    if (lprcPos != NULL) rcPos = *lprcPos;
    pIPSite->GetWindow(&m_hWndParent);
    m_rect = rcPos;
    m_hWnd = ::CreateWindowEx(0, kCtlClass, NULL, WS_CHILD | WS_VISIBLE, rcPos.left, rcPos.top,
                              rcPos.right - rcPos.left, rcPos.bottom - rcPos.top,
                              hWndParent != NULL ? hWndParent : m_hWndParent, NULL,
                              AfxGetInstanceHandle(), this);
    if (m_hWnd == NULL) {
      pIPSite->OnInPlaceDeactivate();
      pIPSite->Release();
      return E_FAIL;
    }
    m_pIPSite = pIPSite;
    return S_OK;
  }
  HRESULT UIActivate() {
    if (m_pIPSite == NULL) return E_UNEXPECTED;
    if (!m_bUIActive) {
      m_bUIActive = TRUE;
      m_pIPSite->OnUIActivate();
    }
    ::SetFocus(m_hWnd);
    return S_OK;
  }
  void UIDeactivate() {
    if (!m_bUIActive) return;
    m_bUIActive = FALSE;
    if (m_pIPSite != NULL) m_pIPSite->OnUIDeactivate(FALSE);
  }
  void InPlaceDeactivate() {
    UIDeactivate();
    if (m_hWnd != NULL) {
      HWND h = m_hWnd;
      m_hWnd = NULL;
      ::DestroyWindow(h);
    }
    if (m_pIPSite != NULL) {
      m_pIPSite->OnInPlaceDeactivate();
      m_pIPSite->Release();
      m_pIPSite = NULL;
    }
  }

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CTestCtl* pThis = (CTestCtl*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (msg) {
      case WM_NCCREATE:
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA,
                           (LONG_PTR)((LPCREATESTRUCT)lParam)->lpCreateParams);
        break;
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(hWnd, &ps);
        RECT rc;
        ::GetClientRect(hWnd, &rc);
        COLORREF cr = RGB(255, 255, 255);
        if (pThis != NULL && pThis->m_nBackColor != -1)
          ::OleTranslateColor((OLE_COLOR)pThis->m_nBackColor, NULL, &cr);
        HBRUSH hbr = ::CreateSolidBrush(cr);
        ::FillRect(hdc, &rc, hbr);
        ::DeleteObject(hbr);
        ::FrameRect(hdc, &rc, (HBRUSH)::GetStockObject(BLACK_BRUSH));
        if (pThis != NULL) {
          ::SetBkMode(hdc, TRANSPARENT);
          ::DrawText(hdc, pThis->m_strCaption, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        ::EndPaint(hWnd, &ps);
        return 0;
      }
      case WM_LBUTTONDOWN:
        if (pThis != NULL) pThis->FireClicked(7);
        return 0;
      case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
  }

  // ----- Interfaces

  BEGIN_INTERFACE_PART(OleObject, IOleObject)
    STDMETHOD(SetClientSite)(LPOLECLIENTSITE pSite) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      if (pSite != NULL) pSite->AddRef();
      if (pThis->m_pClientSite != NULL) pThis->m_pClientSite->Release();
      pThis->m_pClientSite = pSite;
      if (pSite != NULL) pThis->ReadAmbients();
      return S_OK;
    }
    STDMETHOD(GetClientSite)(LPOLECLIENTSITE* ppSite) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      *ppSite = pThis->m_pClientSite;
      if (*ppSite != NULL) (*ppSite)->AddRef();
      return S_OK;
    }
    STDMETHOD(SetHostNames)(LPCOLESTR, LPCOLESTR) { return S_OK; }
    STDMETHOD(Close)(DWORD) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      pThis->InPlaceDeactivate();
      return S_OK;
    }
    STDMETHOD(SetMoniker)(DWORD, LPMONIKER) { return E_NOTIMPL; }
    STDMETHOD(GetMoniker)(DWORD, DWORD, LPMONIKER* ppmk) {
      *ppmk = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(InitFromData)(LPDATAOBJECT, BOOL, DWORD) { return E_NOTIMPL; }
    STDMETHOD(GetClipboardData)(DWORD, LPDATAOBJECT* ppData) {
      *ppData = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG, LPOLECLIENTSITE pSite, LONG, HWND hWndParent,
                      LPCRECT lprcPos) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      switch (iVerb) {
        case OLEIVERB_PRIMARY:
        case OLEIVERB_SHOW:
        case OLEIVERB_UIACTIVATE: {
          HRESULT hr = pThis->InPlaceActivate(pSite, hWndParent, lprcPos);
          return FAILED(hr) ? hr : pThis->UIActivate();
        }
        case OLEIVERB_INPLACEACTIVATE:
          return pThis->InPlaceActivate(pSite, hWndParent, lprcPos);
        case OLEIVERB_HIDE:
          pThis->UIDeactivate();
          if (pThis->m_hWnd != NULL) ::ShowWindow(pThis->m_hWnd, SW_HIDE);
          return S_OK;
      }
      return OLEOBJ_S_INVALIDVERB;
    }
    STDMETHOD(EnumVerbs)(LPENUMOLEVERB* ppEnum) {
      *ppEnum = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(Update)() { return S_OK; }
    STDMETHOD(IsUpToDate)() { return S_OK; }
    STDMETHOD(GetUserClassID)(CLSID* pClsid) {
      *pClsid = CLSID_TestCtl;
      return S_OK;
    }
    STDMETHOD(GetUserType)(DWORD, LPOLESTR* ppsz) {
      *ppsz = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(SetExtent)(DWORD dwAspect, SIZEL* psizel) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      if (dwAspect != DVASPECT_CONTENT) return E_INVALIDARG;
      pThis->m_extent = *psizel;
      return S_OK;
    }
    STDMETHOD(GetExtent)(DWORD dwAspect, SIZEL* psizel) {
      METHOD_PROLOGUE(CTestCtl, OleObject);
      if (dwAspect != DVASPECT_CONTENT) return E_INVALIDARG;
      *psizel = pThis->m_extent;
      return S_OK;
    }
    STDMETHOD(Advise)(LPADVISESINK, DWORD* pdw) {
      *pdw = 0;
      return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHOD(Unadvise)(DWORD) { return OLE_E_NOCONNECTION; }
    STDMETHOD(EnumAdvise)(LPENUMSTATDATA* ppEnum) {
      *ppEnum = NULL;
      return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHOD(GetMiscStatus)(DWORD, DWORD* pdwStatus) {
      *pdwStatus = OLEMISC_RECOMPOSEONRESIZE | OLEMISC_ACTIVATEWHENVISIBLE |
                   OLEMISC_SETCLIENTSITEFIRST | OLEMISC_INSIDEOUT | OLEMISC_CANTLINKINSIDE;
      return S_OK;
    }
    STDMETHOD(SetColorScheme)(LOGPALETTE*) { return E_NOTIMPL; }
  END_INTERFACE_PART(OleObject)

  BEGIN_INTERFACE_PART(OleInPlaceObject, IOleInPlaceObject)
    STDMETHOD(GetWindow)(HWND* phWnd) {
      METHOD_PROLOGUE(CTestCtl, OleInPlaceObject);
      *phWnd = pThis->m_hWnd;
      return pThis->m_hWnd != NULL ? S_OK : E_FAIL;
    }
    STDMETHOD(ContextSensitiveHelp)(BOOL) { return E_NOTIMPL; }
    STDMETHOD(InPlaceDeactivate)() {
      METHOD_PROLOGUE(CTestCtl, OleInPlaceObject);
      pThis->InPlaceDeactivate();
      return S_OK;
    }
    STDMETHOD(UIDeactivate)() {
      METHOD_PROLOGUE(CTestCtl, OleInPlaceObject);
      pThis->UIDeactivate();
      return S_OK;
    }
    STDMETHOD(SetObjectRects)(LPCRECT lprcPos, LPCRECT) {
      METHOD_PROLOGUE(CTestCtl, OleInPlaceObject);
      pThis->m_rect = *lprcPos;
      if (pThis->m_hWnd != NULL)
        ::MoveWindow(pThis->m_hWnd, lprcPos->left, lprcPos->top, lprcPos->right - lprcPos->left,
                     lprcPos->bottom - lprcPos->top, TRUE);
      return S_OK;
    }
    STDMETHOD(ReactivateAndUndo)() { return E_NOTIMPL; }
  END_INTERFACE_PART(OleInPlaceObject)

  BEGIN_INTERFACE_PART(OleControl, IOleControl)
    STDMETHOD(GetControlInfo)(CONTROLINFO* pInfo) {
      pInfo->hAccel = NULL;
      pInfo->cAccel = 0;
      pInfo->dwFlags = 0;
      return S_OK;
    }
    STDMETHOD(OnMnemonic)(LPMSG) { return E_NOTIMPL; }
    STDMETHOD(OnAmbientPropertyChange)(DISPID) {
      METHOD_PROLOGUE(CTestCtl, OleControl);
      pThis->m_nAmbientChanges++;
      pThis->ReadAmbients();
      if (pThis->m_hWnd != NULL) ::InvalidateRect(pThis->m_hWnd, NULL, TRUE);
      return S_OK;
    }
    STDMETHOD(FreezeEvents)(BOOL bFreeze) {
      METHOD_PROLOGUE(CTestCtl, OleControl);
      pThis->m_nFreeze += bFreeze ? 1 : -1;
      return S_OK;
    }
  END_INTERFACE_PART(OleControl)

  BEGIN_INTERFACE_PART(ProvideClassInfo, IProvideClassInfo2)
    STDMETHOD(GetClassInfo)(ITypeInfo** ppTI) {
      *ppTI = NULL;
      return E_NOTIMPL;
    }
    STDMETHOD(GetGUID)(DWORD dwKind, GUID* pGUID) {
      if (dwKind != GUIDKIND_DEFAULT_SOURCE_DISP_IID) return E_INVALIDARG;
      *pGUID = DIID_TestEvents;
      return S_OK;
    }
  END_INTERFACE_PART(ProvideClassInfo)

  BEGIN_INTERFACE_PART(PersistStreamInit, IPersistStreamInit)
    STDMETHOD(GetClassID)(CLSID* pClsid) {
      *pClsid = CLSID_TestCtl;
      return S_OK;
    }
    STDMETHOD(IsDirty)() { return S_FALSE; }
    STDMETHOD(Load)(LPSTREAM pStm) {
      METHOD_PROLOGUE(CTestCtl, PersistStreamInit);
      LONG n = 0;
      ULONG cb = 0;
      HRESULT hr = pStm->Read(&n, sizeof(n), &cb);
      if (FAILED(hr) || cb != sizeof(n)) return E_FAIL;
      pThis->m_nValue = n;
      pThis->m_bLoaded = TRUE;
      return S_OK;
    }
    STDMETHOD(Save)(LPSTREAM pStm, BOOL) {
      METHOD_PROLOGUE(CTestCtl, PersistStreamInit);
      LONG n = pThis->m_nValue;
      return pStm->Write(&n, sizeof(n), NULL);
    }
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER* pcb) {
      pcb->QuadPart = sizeof(LONG);
      return S_OK;
    }
    STDMETHOD(InitNew)() {
      METHOD_PROLOGUE(CTestCtl, PersistStreamInit);
      pThis->m_bInitNew = TRUE;
      return S_OK;
    }
  END_INTERFACE_PART(PersistStreamInit)

  BEGIN_CONNECTION_PART(CTestCtl, EventsCP)
    CONNECTION_IID(DIID_TestEvents)
  END_CONNECTION_PART(EventsCP)
  BEGIN_CONNECTION_PART(CTestCtl, NotifyCP)
    CONNECTION_IID(IID_IPropertyNotifySink)
  END_CONNECTION_PART(NotifyCP)

  DECLARE_DISPATCH_MAP()
  DECLARE_CONNECTION_MAP()
  DECLARE_INTERFACE_MAP()
};
int CTestCtl::s_nAlive = 0;
CTestCtl* CTestCtl::s_pLast = NULL;

// The interface parts' AddRef, Release and QueryInterface.
#define CTL_IUNKNOWN(part)                                                         \
  STDMETHODIMP_(ULONG) CTestCtl::X##part::AddRef() {                               \
    METHOD_PROLOGUE(CTestCtl, part);                                               \
    return pThis->ExternalAddRef();                                                \
  }                                                                                \
  STDMETHODIMP_(ULONG) CTestCtl::X##part::Release() {                              \
    METHOD_PROLOGUE(CTestCtl, part);                                               \
    return pThis->ExternalRelease();                                               \
  }                                                                                \
  STDMETHODIMP CTestCtl::X##part::QueryInterface(REFIID iid, LPVOID* ppv) {        \
    METHOD_PROLOGUE(CTestCtl, part);                                               \
    return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);                      \
  }
CTL_IUNKNOWN(OleObject)
CTL_IUNKNOWN(OleInPlaceObject)
CTL_IUNKNOWN(OleControl)
CTL_IUNKNOWN(ProvideClassInfo)
CTL_IUNKNOWN(PersistStreamInit)

BEGIN_DISPATCH_MAP(CTestCtl, CCmdTarget)
  DISP_PROPERTY_EX_ID(CTestCtl, "Caption", DISPID_CAPTION, GetCaption, SetCaption, VT_BSTR)
  DISP_PROPERTY_EX_ID(CTestCtl, "Enabled", DISPID_ENABLED, GetEnabled, SetEnabled, VT_BOOL)
  DISP_PROPERTY_EX_ID(CTestCtl, "Value", DISPID_VALUE_, GetValue, SetValue, VT_I4)
  DISP_FUNCTION_ID(CTestCtl, "Fire", DISPID_FIRE, Fire, VT_EMPTY, VTS_I4)
  DISP_FUNCTION_ID(CTestCtl, "Twice", DISPID_TWICE, Twice, VT_I4, VTS_I4)
END_DISPATCH_MAP()

BEGIN_CONNECTION_MAP(CTestCtl, CCmdTarget)
  CONNECTION_PART(CTestCtl, DIID_TestEvents, EventsCP)
  CONNECTION_PART(CTestCtl, IID_IPropertyNotifySink, NotifyCP)
END_CONNECTION_MAP()

BEGIN_INTERFACE_MAP(CTestCtl, CCmdTarget)
  INTERFACE_PART(CTestCtl, IID_IOleObject, OleObject)
  INTERFACE_PART(CTestCtl, IID_IOleInPlaceObject, OleInPlaceObject)
  INTERFACE_PART(CTestCtl, IID_IOleWindow, OleInPlaceObject)
  INTERFACE_PART(CTestCtl, IID_IOleControl, OleControl)
  INTERFACE_PART(CTestCtl, IID_IProvideClassInfo, ProvideClassInfo)
  INTERFACE_PART(CTestCtl, IID_IProvideClassInfo2, ProvideClassInfo)
  INTERFACE_PART(CTestCtl, IID_IPersistStreamInit, PersistStreamInit)
  INTERFACE_PART(CTestCtl, IID_IConnectionPointContainer, ConnPtContainer)
END_INTERFACE_MAP()

// The control's class object, registered for this process only.
class CTestCtlFactory : public IClassFactory {
public:
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IClassFactory) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(CreateInstance)(LPUNKNOWN pOuter, REFIID iid, void** ppv) {
    *ppv = NULL;
    if (pOuter != NULL) return CLASS_E_NOAGGREGATION;
    CTestCtl* pCtl = new CTestCtl;
    HRESULT hr = pCtl->GetControllingUnknown()->QueryInterface(iid, ppv);
    pCtl->InternalRelease();
    return hr;
  }
  STDMETHOD(LockServer)(BOOL) { return S_OK; }
};
static CTestCtlFactory g_factory;

// =====================================================================
// The dialog

class CCtlDlg : public CDialog {
public:
  CWnd m_ctl;
  CString m_strCaption;
  long m_nValue2;
  int m_nClicks;
  long m_nLastClick;
  BOOL m_bCtlAtInit;     // the control was there in OnInitDialog
  BOOL m_bWrapperAtInit;  // and DDX_Control gave it its window
  long m_nValueAtInit;    // the second control's Value before DDX
  CCtlDlg()
      : m_nValue2(0), m_nClicks(0), m_nLastClick(0), m_bCtlAtInit(FALSE), m_bWrapperAtInit(FALSE),
        m_nValueAtInit(-1) {}

  virtual BOOL OnInitDialog() {
    m_bCtlAtInit = GetDlgItem(IDC_DLGCTL) != NULL;
    CWnd* pCtl2 = GetDlgItem(IDC_DLGCTL2);
    if (pCtl2 != NULL) pCtl2->GetProperty(DISPID_VALUE_, VT_I4, &m_nValueAtInit);
    CDialog::OnInitDialog();  // UpdateData(FALSE)
    m_bWrapperAtInit = m_ctl.m_hWnd != NULL && m_ctl.GetControlUnknown() != NULL;
    return TRUE;
  }
  virtual void DoDataExchange(CDataExchange* pDX) {
    DDX_Control(pDX, IDC_DLGCTL, m_ctl);
    DDX_OCText(pDX, IDC_DLGCTL, DISPID_CAPTION, m_strCaption);
    DDX_OCInt(pDX, IDC_DLGCTL2, DISPID_VALUE_, m_nValue2);
  }
  void OnClicked(long n) {
    m_nClicks++;
    m_nLastClick = n;
  }
  DECLARE_EVENTSINK_MAP()
};

BEGIN_EVENTSINK_MAP(CCtlDlg, CDialog)
  ON_EVENT(CCtlDlg, IDC_DLGCTL, DISPID_CLICKED, OnClicked, VTS_I4)
END_EVENTSINK_MAP()

// =====================================================================
// The frame that hosts a control with CreateControl

// A wrapper class that catches its own control's Renamed event first.
class CTestCtlWnd : public CWnd {
public:
  int m_nRenamed;
  CString m_strRenamed;
  CTestCtlWnd() : m_nRenamed(0) {}
  BOOL OnRenamed(LPCTSTR lpsz) {
    m_nRenamed++;
    m_strRenamed = lpsz;
    return TRUE;  // the frame does not see it
  }
  DECLARE_EVENTSINK_MAP()
};

BEGIN_EVENTSINK_MAP(CTestCtlWnd, CWnd)
  ON_EVENT_REFLECT(CTestCtlWnd, DISPID_RENAMED, OnRenamed, VTS_BSTR)
END_EVENTSINK_MAP()

class CMainFrame : public CFrameWnd {
public:
  CTestCtlWnd m_ctl;
  int m_nClicks;
  long m_nLastClick;
  int m_nRenamed;
  int m_nRequests;
  int m_nChanges;
  BOOL m_bVeto;
  long m_nBackColor;

  CMainFrame()
      : m_nClicks(0), m_nLastClick(0), m_nRenamed(0), m_nRequests(0), m_nChanges(0),
        m_bVeto(FALSE), m_nBackColor(RGB(0, 128, 255)) {}

  virtual BOOL OnAmbientProperty(COleControlSite* pSite, DISPID dispid, VARIANT* pvar) {
    if (dispid == DISPID_AMBIENT_BACKCOLOR) {
      pvar->vt = VT_I4;
      pvar->lVal = m_nBackColor;
      return TRUE;
    }
    return CFrameWnd::OnAmbientProperty(pSite, dispid, pvar);
  }

  void OnClicked(long n) {
    m_nClicks++;
    m_nLastClick = n;
  }
  void OnRenamed(LPCTSTR) { m_nRenamed++; }
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
  void TestFrameControl();
  void TestDialog();

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
  ON_EVENT(CMainFrame, IDC_FRAMECTL, DISPID_CLICKED, OnClicked, VTS_I4)
  ON_EVENT(CMainFrame, IDC_FRAMECTL, DISPID_RENAMED, OnRenamed, VTS_BSTR)
  ON_PROPNOTIFY(CMainFrame, IDC_FRAMECTL, DISPID_VALUE_, OnValueRequest, OnValueChanged)
END_EVENTSINK_MAP()

static void PumpMessages() {
  MSG msg;
  while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

void CMainFrame::TestFrameControl() {
  TestLog("frame control");
  CRect rect(10, 10, 210, 70);
  BOOL bMade = m_ctl.CreateControl(CLSID_TestCtl, _T("Hello"), WS_VISIBLE | WS_TABSTOP, rect, this,
                                   IDC_FRAMECTL);
  CHECK(bMade);
  CTestCtl* pCtl = CTestCtl::s_pLast;
  CHECK(pCtl != NULL && CTestCtl::s_nAlive == 1);
  if (!bMade || pCtl == NULL) return;

  // The wrapper is the control's window.
  CHECK(m_ctl.m_hWnd != NULL && m_ctl.m_hWnd == pCtl->m_hWnd);
  CHECK(::GetParent(m_ctl.m_hWnd) == m_hWnd && pCtl->m_hWndParent == m_hWnd);
  CHECK(m_ctl.GetDlgCtrlID() == IDC_FRAMECTL && GetDlgItem(IDC_FRAMECTL) == &m_ctl);
  CHECK(m_ctl.IsWindowVisible());
  CHECK(pCtl->m_bInitNew && !pCtl->m_bLoaded && pCtl->m_bGotContext);
  CHECK(pCtl->m_extent.cx > 0 && pCtl->m_extent.cy > 0);
  CRect rectCtl;
  m_ctl.GetWindowRect(&rectCtl);
  ScreenToClient(&rectCtl);
  CHECK(rectCtl == rect);
  LPUNKNOWN pUnk = m_ctl.GetControlUnknown();
  CHECK(pUnk != NULL);
  if (pUnk != NULL) {
    LPOLEOBJECT pObj = NULL;
    CHECK(pUnk->QueryInterface(IID_IOleObject, (void**)&pObj) == S_OK);
    CHECK(pObj == &pCtl->m_xOleObject);
    if (pObj != NULL) pObj->Release();
  }

  // Ambient properties: the frame's own, and the container's.
  CHECK(pCtl->m_bUserMode);
  CHECK(pCtl->m_nBackColor == (long)RGB(0, 128, 255));

  // The window name was the Caption, set through the wrapper.
  CHECK(pCtl->m_strCaption == _T("Hello"));
  CString str;
  m_ctl.GetWindowText(str);
  CHECK(str == _T("Hello"));
  // The Renamed event went to the wrapper's own map, not to the frame.
  CHECK(m_ctl.m_nRenamed == 1 && m_ctl.m_strRenamed == _T("Hello") && m_nRenamed == 0);
  m_ctl.SetWindowText(_T("World"));
  CHECK(pCtl->m_strCaption == _T("World") && m_ctl.m_nRenamed == 2);

  // Properties and methods.
  m_ctl.SetProperty(DISPID_VALUE_, VT_I4, 41L);
  long n = 0;
  m_ctl.GetProperty(DISPID_VALUE_, VT_I4, &n);
  CHECK(n == 41 && pCtl->m_nValue == 41);
  CHECK(m_nRequests == 1 && m_nChanges == 1);
  m_bVeto = TRUE;
  m_ctl.SetProperty(DISPID_VALUE_, VT_I4, 99L);
  CHECK(pCtl->m_nValue == 41 && m_nRequests == 2 && m_nChanges == 1);
  m_bVeto = FALSE;
  static const BYTE paramsI4[] = VTS_I4;
  long nTwice = 0;
  m_ctl.InvokeHelper(DISPID_TWICE, DISPATCH_METHOD, VT_I4, &nTwice, paramsI4, 21L);
  CHECK(nTwice == 42);

  // Events: from a method, and from the control's own window.
  m_ctl.InvokeHelper(DISPID_FIRE, DISPATCH_METHOD, VT_EMPTY, NULL, paramsI4, 5L);
  CHECK(m_nClicks == 1 && m_nLastClick == 5);
  m_ctl.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
  CHECK(m_nClicks == 2 && m_nLastClick == 7);
  // Frozen events do not come.
  COleControlSite* pSite = m_ctl.m_pCtrlSite;
  CHECK(pSite != NULL);
  if (pSite != NULL) {
    pSite->FreezeEvents(TRUE);
    CHECK(pCtl->m_nFreeze == 1);
    pCtl->FireClicked(9);
    CHECK(m_nClicks == 2 && pCtl->m_nDropped == 1);
    pSite->FreezeEvents(FALSE);
    CHECK(pCtl->m_nFreeze == 0);
    pCtl->FireClicked(9);
    CHECK(m_nClicks == 3 && m_nLastClick == 9);
  }

  // A change of an ambient property reaches the control.
  m_nBackColor = RGB(255, 255, 0);
  if (m_pCtrlCont != NULL) m_pCtrlCont->BroadcastAmbientPropertyChange(DISPID_AMBIENT_BACKCOLOR);
  CHECK(pCtl->m_nAmbientChanges == 1 && pCtl->m_nBackColor == (long)RGB(255, 255, 0));

  // Moving the wrapper moves the control through SetObjectRects.
  m_ctl.MoveWindow(20, 80, 150, 40);
  CHECK(pCtl->m_rect == CRect(20, 80, 170, 120));
  m_ctl.GetWindowRect(&rectCtl);
  ScreenToClient(&rectCtl);
  CHECK(rectCtl == CRect(20, 80, 170, 120));

  // Enabling goes through the Enabled property.
  m_ctl.EnableWindow(FALSE);
  CHECK(!::IsWindowEnabled(pCtl->m_hWnd) && !m_ctl.IsWindowEnabled());
  m_ctl.EnableWindow(TRUE);
  CHECK(::IsWindowEnabled(pCtl->m_hWnd));

  // Focus makes it UI active in the container.
  m_ctl.SetFocus();
  CHECK(pCtl->m_bUIActive && ::GetFocus() == pCtl->m_hWnd);
  CHECK(m_pCtrlCont != NULL && m_pCtrlCont->m_pSiteUIActive == pSite);

  PumpMessages();
  TestSaveWindowBitmap(m_hWnd, _T("occtest-frame.bmp"));

  // Destroying the wrapper closes the control and lets go of it.
  m_ctl.DestroyWindow();
  CHECK(m_ctl.m_hWnd == NULL && m_ctl.m_pCtrlSite == NULL);
  CHECK(CTestCtl::s_nAlive == 0);
  CHECK(m_pCtrlCont == NULL || m_pCtrlCont->m_pSiteUIActive == NULL);
  CHECK(GetDlgItem(IDC_FRAMECTL) == NULL);
}

void CMainFrame::TestDialog() {
  TestLog("dialog");
  CCtlDlg dlg;
  dlg.m_strCaption = _T("From DDX");
  BOOL bMade = dlg.Create(IDD_CTLDLG, this);
  CHECK(bMade);
  if (!bMade) return;
  CHECK(CTestCtl::s_nAlive == 2);
  CHECK(dlg.m_bCtlAtInit && dlg.m_bWrapperAtInit);
  CHECK(dlg.m_ctl.GetDlgCtrlID() == IDC_DLGCTL);

  CWnd* pCtl2 = dlg.GetDlgItem(IDC_DLGCTL2);
  CHECK(pCtl2 != NULL);
  if (pCtl2 == NULL) {
    dlg.DestroyWindow();
    return;
  }
  CTestCtl* pObj1 = NULL;
  CTestCtl* pObj2 = NULL;
  {
    // The objects behind the windows.
    CTestCtl* p = (CTestCtl*)::GetWindowLongPtr(dlg.m_ctl.m_hWnd, GWLP_USERDATA);
    pObj1 = p;
    pObj2 = (CTestCtl*)::GetWindowLongPtr(pCtl2->m_hWnd, GWLP_USERDATA);
  }
  CHECK(pObj1 != NULL && pObj2 != NULL && pObj1 != pObj2);
  if (pObj1 == NULL || pObj2 == NULL) {
    dlg.DestroyWindow();
    return;
  }
  // The first started new, with its text and DDX's Caption; the second
  // loaded its state from DLGINIT and is disabled.
  CHECK(pObj1->m_bInitNew && !pObj1->m_bLoaded);
  CHECK(pObj1->m_strCaption == _T("From DDX"));
  CHECK(pObj2->m_bLoaded && !pObj2->m_bInitNew && dlg.m_nValueAtInit == 1234);
  // Then DDX gave it the member's value.
  CHECK(pObj2->m_nValue == 0);
  CHECK(!::IsWindowEnabled(pObj2->m_hWnd));
  CHECK(::IsWindowEnabled(pObj1->m_hWnd));

  // The size: the template's dialog units.
  CRect rectDU(7, 28, 107, 58);
  dlg.MapDialogRect(&rectDU);
  CRect rect;
  dlg.m_ctl.GetWindowRect(&rect);
  dlg.ScreenToClient(&rect);
  CHECK(rect == rectDU);

  // The tab order follows the template: edit, control, control, OK.
  HWND hEdit = ::GetDlgItem(dlg.m_hWnd, IDC_EDIT1);
  CHECK(::GetWindow(hEdit, GW_HWNDNEXT) == dlg.m_ctl.m_hWnd);
  CHECK(::GetWindow(dlg.m_ctl.m_hWnd, GW_HWNDNEXT) == pCtl2->m_hWnd);
  CHECK(::GetWindow(pCtl2->m_hWnd, GW_HWNDNEXT) == ::GetDlgItem(dlg.m_hWnd, IDOK));

  // DDX the other way.
  dlg.m_ctl.SetWindowText(_T("Typed"));
  pObj2->m_nValue = 77;
  CHECK(dlg.UpdateData(TRUE));
  CHECK(dlg.m_strCaption == _T("Typed") && dlg.m_nValue2 == 77);

  // The dialog's event sink map.
  pObj1->FireClicked(3);
  CHECK(dlg.m_nClicks == 1 && dlg.m_nLastClick == 3);
  pObj2->FireClicked(4);  // not in the map
  CHECK(dlg.m_nClicks == 1);

  PumpMessages();
  TestSaveWindowBitmap(dlg.m_hWnd, _T("occtest-dialog.bmp"));

  dlg.DestroyWindow();
  CHECK(CTestCtl::s_nAlive == 0);
  CHECK(dlg.m_ctl.m_hWnd == NULL);
}

void CMainFrame::RunTests() {
  TestFrameControl();
  TestDialog();
  // A control that cannot be made.
  CWnd wnd;
  static const CLSID clsidNone = {0x5b7c2e41, 0x9d3a, 0x4c6f, {0x8e, 0x12, 0x7a, 0x4d, 0x0b, 0x9c, 0x3e, 0x5f}};
  CHECK(!wnd.CreateControl(clsidNone, NULL, WS_VISIBLE, CRect(0, 0, 10, 10), this, 3000));
  CHECK(wnd.m_hWnd == NULL && wnd.m_pCtrlSite == NULL);
}

// =====================================================================

class CTestApp : public CWinApp {
public:
  DWORD m_dwRegister;
  CTestApp() : m_dwRegister(0) {}
  virtual BOOL InitInstance() {
    TestOpenLog(_T("occtest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = CTestCtl::WndProc;
    wc.hInstance = AfxGetInstanceHandle();
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kCtlClass;
    ::RegisterClass(&wc);
    HRESULT hr = ::CoRegisterClassObject(CLSID_TestCtl, &g_factory, CLSCTX_INPROC_SERVER,
                                         REGCLS_MULTIPLEUSE, &m_dwRegister);
    CHECK(SUCCEEDED(hr));
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("occtest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 400, 300))) return FALSE;
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    if (m_dwRegister != 0) ::CoRevokeClassObject(m_dwRegister);
    CHECK(CTestCtl::s_nAlive == 0);
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
