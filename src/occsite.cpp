// occsite.cpp - ActiveX control containment: COleControlSite (one control:
// its creation, activation in place, properties, events and ambient
// properties) and COleControlContainer (the controls of one window).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxocc.h>
#include <olectl.h>
#include <afxole.h>

const _AFX_OCC_BIND_HOOKS* _afxOccBindHooks = NULL;

// ---------------------------------------------------------------------
// The extended control: the extender properties, and the control's own
// names and DISPIDs passed on to it.

namespace {

struct ExtProp {
  const WCHAR* pszName;
  DISPID dispid;
};
const ExtProp kExtProps[] = {
    {L"Name", DISPID_NAME},          {L"Object", DISPID_OBJECT},
    {L"Parent", DISPID_PARENT},      {L"Left", AFX_DISPID_EXT_LEFT},
    {L"Top", AFX_DISPID_EXT_TOP},    {L"Width", AFX_DISPID_EXT_WIDTH},
    {L"Height", AFX_DISPID_EXT_HEIGHT}, {L"Visible", AFX_DISPID_EXT_VISIBLE},
    {L"ID", AFX_DISPID_EXT_ID},
};

class CExtendedControl final : public IDispatch {
public:
  explicit CExtendedControl(COleControlSite* pSite) : m_cRef(1), m_pSite(pSite) {}

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IDispatch)) {
      *ppv = static_cast<IDispatch*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)InterlockedIncrement(&m_cRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return (ULONG)c;
  }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) {
    *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** ppTI) {
    *ppTI = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid,
                           DISPID* rgDispId) {
    if (cNames == 1) {
      for (const ExtProp& prop : kExtProps)
        if (::lstrcmpiW(rgszNames[0], prop.pszName) == 0) {
          rgDispId[0] = prop.dispid;
          return S_OK;
        }
    }
    IDispatch* pDisp = Control();
    if (pDisp == NULL) return DISP_E_UNKNOWNNAME;
    return pDisp->GetIDsOfNames(riid, rgszNames, cNames, lcid, rgDispId);
  }
  STDMETHOD(Invoke)(DISPID dispid, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pParams,
                    VARIANT* pResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) {
    if (!IsExtender(dispid)) {
      IDispatch* pDisp = Control();
      if (pDisp == NULL) return DISP_E_MEMBERNOTFOUND;
      return pDisp->Invoke(dispid, riid, lcid, wFlags, pParams, pResult, pExcepInfo, puArgErr);
    }
    if (m_pSite == NULL) return E_UNEXPECTED;
    if (wFlags & DISPATCH_PROPERTYGET) return GetExtender(dispid, pResult);
    if (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
      if (pParams == NULL || pParams->cArgs != 1) return DISP_E_BADPARAMCOUNT;
      return PutExtender(dispid, &pParams->rgvarg[0], puArgErr);
    }
    return DISP_E_MEMBERNOTFOUND;
  }

  LONG m_cRef;
  COleControlSite* m_pSite;  // NULL once the site goes

private:
  IDispatch* Control() const {
    return m_pSite != NULL ? m_pSite->m_dispDriver.m_lpDispatch : NULL;
  }
  static BOOL IsExtender(DISPID dispid) {
    for (const ExtProp& prop : kExtProps)
      if (prop.dispid == dispid) return TRUE;
    return FALSE;
  }
  HRESULT GetExtender(DISPID dispid, VARIANT* pResult) {
    if (pResult == NULL) return S_OK;
    ::VariantInit(pResult);
    const CRect& rect = m_pSite->m_rect;
    switch (dispid) {
      case DISPID_NAME:
        pResult->vt = VT_BSTR;
        pResult->bstrVal = m_pSite->m_strName.AllocSysString();
        return S_OK;
      case DISPID_OBJECT:
        pResult->vt = VT_DISPATCH;
        pResult->pdispVal = Control();
        if (pResult->pdispVal != NULL) pResult->pdispVal->AddRef();
        return S_OK;
      case DISPID_PARENT: {
        // The container window's automation object, when it has one.
        CWnd* pWnd = m_pSite->m_pCtrlCont->m_pWnd;
        pResult->vt = VT_DISPATCH;
        pResult->pdispVal = pWnd != NULL ? pWnd->GetIDispatch(TRUE) : NULL;
        return S_OK;
      }
      case AFX_DISPID_EXT_LEFT: pResult->vt = VT_I4; pResult->lVal = rect.left; return S_OK;
      case AFX_DISPID_EXT_TOP: pResult->vt = VT_I4; pResult->lVal = rect.top; return S_OK;
      case AFX_DISPID_EXT_WIDTH: pResult->vt = VT_I4; pResult->lVal = rect.Width(); return S_OK;
      case AFX_DISPID_EXT_HEIGHT: pResult->vt = VT_I4; pResult->lVal = rect.Height(); return S_OK;
      case AFX_DISPID_EXT_VISIBLE:
        pResult->vt = VT_BOOL;
        pResult->boolVal = (m_pSite->m_dwStyle & WS_VISIBLE) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
      case AFX_DISPID_EXT_ID:
        pResult->vt = VT_I4;
        pResult->lVal = (LONG)m_pSite->GetDlgCtrlID();
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
  }
  HRESULT PutExtender(DISPID dispid, VARIANT* pArg, UINT* puArgErr) {
    VARIANT var;
    ::VariantInit(&var);
    VARTYPE vt = dispid == DISPID_NAME              ? VT_BSTR
                 : dispid == AFX_DISPID_EXT_VISIBLE ? VT_BOOL
                                                    : VT_I4;
    if (dispid == DISPID_OBJECT || dispid == DISPID_PARENT) return DISP_E_MEMBERNOTFOUND;
    if (FAILED(::VariantChangeType(&var, pArg, 0, vt))) {
      if (puArgErr != NULL) *puArgErr = 0;
      return DISP_E_TYPEMISMATCH;
    }
    CRect rect = m_pSite->m_rect;
    switch (dispid) {
      case DISPID_NAME: m_pSite->m_strName = var.bstrVal; break;
      case AFX_DISPID_EXT_LEFT: rect.OffsetRect(var.lVal - rect.left, 0); break;
      case AFX_DISPID_EXT_TOP: rect.OffsetRect(0, var.lVal - rect.top); break;
      case AFX_DISPID_EXT_WIDTH: rect.right = rect.left + var.lVal; break;
      case AFX_DISPID_EXT_HEIGHT: rect.bottom = rect.top + var.lVal; break;
      case AFX_DISPID_EXT_VISIBLE:
        m_pSite->ShowWindow(var.boolVal != VARIANT_FALSE ? SW_SHOWNA : SW_HIDE);
        break;
      case AFX_DISPID_EXT_ID: m_pSite->SetDlgCtrlID((int)var.lVal); break;
    }
    ::VariantClear(&var);
    if (rect != m_pSite->m_rect)
      m_pSite->MoveWindow(rect.left, rect.top, rect.Width(), rect.Height());
    return S_OK;
  }
};

}  // namespace

LPDISPATCH COleControlSite::GetExtendedControl() {
  if (m_pObject == NULL) return NULL;
  if (m_pExtendedControl == NULL) m_pExtendedControl = new CExtendedControl(this);
  m_pExtendedControl->AddRef();
  return m_pExtendedControl;
}

static void _AfxReleaseExtendedControl(COleControlSite* pSite) {
  if (pSite->m_pExtendedControl == NULL) return;
  // The control may still hold it; it no longer reaches the site.
  static_cast<CExtendedControl*>(pSite->m_pExtendedControl)->m_pSite = NULL;
  pSite->m_pExtendedControl->Release();
  pSite->m_pExtendedControl = NULL;
}

// HIMETRIC and pixels, at the screen's resolution.
static void PixelsToHimetric(const SIZE& sizePix, SIZEL* pSizeHm) {
  HDC hDC = ::GetDC(NULL);
  int cxPerInch = ::GetDeviceCaps(hDC, LOGPIXELSX), cyPerInch = ::GetDeviceCaps(hDC, LOGPIXELSY);
  ::ReleaseDC(NULL, hDC);
  pSizeHm->cx = ::MulDiv(sizePix.cx, 2540, cxPerInch);
  pSizeHm->cy = ::MulDiv(sizePix.cy, 2540, cyPerInch);
}

static void HimetricToPixels(const SIZEL& sizeHm, SIZE* pSizePix) {
  HDC hDC = ::GetDC(NULL);
  int cxPerInch = ::GetDeviceCaps(hDC, LOGPIXELSX), cyPerInch = ::GetDeviceCaps(hDC, LOGPIXELSY);
  ::ReleaseDC(NULL, hDC);
  pSizePix->cx = ::MulDiv(sizeHm.cx, cxPerInch, 2540);
  pSizePix->cy = ::MulDiv(sizeHm.cy, cyPerInch, 2540);
}

// =====================================================================
// COleControlSite

BEGIN_INTERFACE_MAP(COleControlSite, CCmdTarget)
  INTERFACE_PART(COleControlSite, IID_IOleClientSite, OleClientSite)
  INTERFACE_PART(COleControlSite, IID_IOleWindow, OleIPSite)
  INTERFACE_PART(COleControlSite, IID_IOleInPlaceSite, OleIPSite)
  INTERFACE_PART(COleControlSite, IID_IOleInPlaceSiteEx, OleIPSite)
  INTERFACE_PART(COleControlSite, IID_IOleInPlaceSiteWindowless, OleIPSite)
  INTERFACE_PART(COleControlSite, IID_IOleControlSite, OleControlSite)
  INTERFACE_PART(COleControlSite, IID_IDispatch, AmbientProps)
  INTERFACE_PART(COleControlSite, IID_IPropertyNotifySink, PropertyNotifySink)
  INTERFACE_PART(COleControlSite, IID_INotifyDBEvents, NotifyDBEvents)
END_INTERFACE_MAP()

COleControlSite::COleControlSite(COleControlContainer* pCtrlCont)
    : m_pCtrlCont(pCtrlCont),
      m_hWnd(NULL),
      m_pWndCtrl(NULL),
      m_nID((UINT)-1),
      m_rect(0, 0, 0, 0),
      m_iidEvents(GUID_NULL),
      m_pObject(NULL),
      m_pInPlaceObject(NULL),
      m_pActiveObject(NULL),
      m_dwEventSink(0),
      m_dwPropNotifySink(0),
      m_dwNotifyDBEvents(0),
      m_dwMiscStatus(0),
      m_dwStyleMask(WS_GROUP | WS_TABSTOP),
      m_dwStyle(0),
      m_bIsWindowless(FALSE),
      m_bInPlaceActive(FALSE),
      m_bUIActive(FALSE),
      m_bSubclassed(FALSE),
      m_pWndOwned(NULL),
      m_clsid(GUID_NULL),
      m_pWindowlessObject(NULL),
      m_nFreezeEvents(0),
      m_pDataSourceControl(NULL),
      m_pBindings(NULL),
      m_bIgnoreNotify(FALSE),
      m_pExtendedControl(NULL) {
  memset(&m_ctlInfo, 0, sizeof(m_ctlInfo));
  m_ctlInfo.cb = sizeof(m_ctlInfo);
}

COleControlSite::~COleControlSite() {
  if (m_ctlInfo.hAccel != NULL) ::DestroyAcceleratorTable(m_ctlInfo.hAccel);
}

HRESULT COleControlSite::CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                       DWORD dwStyle, const POINT* ppt, const SIZE* psize,
                                       UINT nID, CFile* pPersist, BOOL bStorage,
                                       BSTR bstrLicKey) {
  CRect rect(0, 0, 0, 0);
  if (ppt != NULL) rect.OffsetRect(*ppt);
  if (psize != NULL) rect.BottomRight() = rect.TopLeft() + CSize(*psize);
  HRESULT hr = CreateControl(pWndCtrl, clsid, lpszWindowName, dwStyle, rect, nID, pPersist,
                             bStorage, bstrLicKey);
  if (SUCCEEDED(hr) && psize == NULL) {
    // The control's own size.
    SIZEL sizeHm;
    if (SUCCEEDED(m_pObject->GetExtent(DVASPECT_CONTENT, &sizeHm))) {
      SIZE size;
      HimetricToPixels(sizeHm, &size);
      MoveWindow(rect.left, rect.top, size.cx, size.cy);
    }
  }
  return hr;
}

HRESULT COleControlSite::CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                       DWORD dwStyle, const RECT& rect, UINT nID,
                                       CFile* pPersist, BOOL bStorage, BSTR bstrLicKey) {
  m_pWndCtrl = pWndCtrl;
  m_nID = nID;
  m_rect = rect;
  m_clsid = clsid;
  m_dwStyle = dwStyle;

  HRESULT hr = CreateOrLoad(clsid, pPersist, bStorage, bstrLicKey);
  if (FAILED(hr)) {
    if (m_pObject != NULL) {
      m_pObject->SetClientSite(NULL);
      m_pObject->Release();
      m_pObject = NULL;
    }
    return hr;
  }

  LPDISPATCH pDisp = NULL;
  if (SUCCEEDED(m_pObject->QueryInterface(IID_IDispatch, (void**)&pDisp)))
    m_dispDriver.AttachDispatch(pDisp, TRUE);
  FindEventsIID();
  if (!::IsEqualIID(m_iidEvents, GUID_NULL))
    ConnectSink(m_iidEvents, &m_xEventSink, &m_dwEventSink);
  ConnectSink(IID_IPropertyNotifySink, &m_xPropertyNotifySink, &m_dwPropNotifySink);
  ConnectSink(IID_INotifyDBEvents, &m_xNotifyDBEvents, &m_dwNotifyDBEvents);
  GetControlInfo();

  // Active in place, with a window, unless it is invisible at run time.
  HWND hWndParent = m_pCtrlCont->m_pWnd->GetSafeHwnd();
  hr = m_pObject->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, &m_xOleClientSite, 0, hWndParent,
                         &m_rect);
  if (FAILED(hr) && !(m_dwMiscStatus & OLEMISC_INVISIBLEATRUNTIME)) {
    DisconnectSink(IID_INotifyDBEvents, m_dwNotifyDBEvents);
    DisconnectSink(IID_IPropertyNotifySink, m_dwPropNotifySink);
    DisconnectSink(m_iidEvents, m_dwEventSink);
    m_dwEventSink = m_dwPropNotifySink = m_dwNotifyDBEvents = 0;
    m_dispDriver.ReleaseDispatch();
    m_pObject->Close(OLECLOSE_NOSAVE);
    m_pObject->SetClientSite(NULL);
    m_pObject->Release();
    m_pObject = NULL;
    return hr;
  }
  if (m_pInPlaceObject != NULL) m_pInPlaceObject->GetWindow(&m_hWnd);
  if (m_hWnd != NULL) {
    ::SetWindowLongPtr(m_hWnd, GWLP_ID, (LONG_PTR)nID);
    // The styles the container decides: visible, disabled, tab stop and
    // group.
    const DWORD dwMask = WS_VISIBLE | WS_DISABLED | WS_TABSTOP | WS_GROUP;
    DWORD dwCur = (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE);
    ::SetWindowLong(m_hWnd, GWL_STYLE, (LONG)((dwCur & ~(dwMask & ~WS_VISIBLE)) |
                                              (dwStyle & (dwMask & ~WS_VISIBLE))));
    if (dwStyle & WS_DISABLED) EnableWindow(FALSE);
    ::ShowWindow(m_hWnd, (dwStyle & WS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
    m_pCtrlCont->m_siteMap.SetAt(m_hWnd, this);
    AttachWindow();
  } else if (m_pWndCtrl != NULL) {
    m_pWndCtrl->m_pCtrlSite = this;
  }
  if (lpszWindowName != NULL && *lpszWindowName != 0) SetWindowText(lpszWindowName);
  return S_OK;
}

HRESULT COleControlSite::CreateOrLoad(REFCLSID clsid, CFile* pPersist, BOOL bStorage,
                                      BSTR bstrLicKey) {
  HRESULT hr;
  const DWORD dwContext = CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER;
  if (bstrLicKey != NULL) {
    IClassFactory2* pFactory = NULL;
    hr = ::CoGetClassObject(clsid, dwContext, NULL, IID_IClassFactory2, (void**)&pFactory);
    if (FAILED(hr)) return hr;
    hr = pFactory->CreateInstanceLic(NULL, NULL, IID_IOleObject, bstrLicKey, (void**)&m_pObject);
    pFactory->Release();
  } else {
    hr = ::CoCreateInstance(clsid, NULL, dwContext, IID_IOleObject, (void**)&m_pObject);
  }
  if (FAILED(hr)) return hr;
  m_pObject->GetMiscStatus(DVASPECT_CONTENT, &m_dwMiscStatus);
  BOOL bSiteFirst = (m_dwMiscStatus & OLEMISC_SETCLIENTSITEFIRST) != 0;
  if (bSiteFirst) m_pObject->SetClientSite(&m_xOleClientSite);

  // The saved state, or a new one.
  HGLOBAL hData = NULL;
  if (pPersist != NULL) {
    ULONGLONG nLen = pPersist->GetLength() - pPersist->GetPosition();
    hData = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)nLen ? (SIZE_T)nLen : 1);
    if (hData == NULL) return E_OUTOFMEMORY;
    void* p = ::GlobalLock(hData);
    pPersist->Read(p, (UINT)nLen);
    ::GlobalUnlock(hData);
  }
  hr = E_NOINTERFACE;
  if (bStorage) {
    IPersistStorage* pPS = NULL;
    if (SUCCEEDED(m_pObject->QueryInterface(IID_IPersistStorage, (void**)&pPS))) {
      ILockBytes* pLockBytes = NULL;
      IStorage* pStg = NULL;
      hr = ::CreateILockBytesOnHGlobal(hData, TRUE, &pLockBytes);
      if (SUCCEEDED(hr)) {
        hData = NULL;  // the lock bytes own it now
        if (pPersist != NULL)
          hr = ::StgOpenStorageOnILockBytes(pLockBytes, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                            NULL, 0, &pStg);
        else
          hr = ::StgCreateDocfileOnILockBytes(
              pLockBytes, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE, 0, &pStg);
        pLockBytes->Release();
      }
      if (SUCCEEDED(hr)) {
        hr = pPersist != NULL ? pPS->Load(pStg) : pPS->InitNew(pStg);
        pStg->Release();
      }
      pPS->Release();
    }
  } else {
    IPersistStreamInit* pPSI = NULL;
    IPersistStream* pPStm = NULL;
    if (SUCCEEDED(m_pObject->QueryInterface(IID_IPersistStreamInit, (void**)&pPSI))) {
      if (pPersist != NULL) {
        IStream* pStm = NULL;
        hr = ::CreateStreamOnHGlobal(hData, TRUE, &pStm);
        if (SUCCEEDED(hr)) {
          hData = NULL;
          hr = pPSI->Load(pStm);
          pStm->Release();
        }
      } else {
        hr = pPSI->InitNew();
      }
      pPSI->Release();
    } else if (pPersist != NULL &&
               SUCCEEDED(m_pObject->QueryInterface(IID_IPersistStream, (void**)&pPStm))) {
      IStream* pStm = NULL;
      hr = ::CreateStreamOnHGlobal(hData, TRUE, &pStm);
      if (SUCCEEDED(hr)) {
        hData = NULL;
        hr = pPStm->Load(pStm);
        pStm->Release();
      }
      pPStm->Release();
    } else if (pPersist == NULL) {
      // Some controls keep no state.
      IPersistStorage* pPS = NULL;
      hr = S_OK;
      if (SUCCEEDED(m_pObject->QueryInterface(IID_IPersistStorage, (void**)&pPS))) {
        ILockBytes* pLockBytes = NULL;
        IStorage* pStg = NULL;
        hr = ::CreateILockBytesOnHGlobal(NULL, TRUE, &pLockBytes);
        if (SUCCEEDED(hr)) {
          hr = ::StgCreateDocfileOnILockBytes(
              pLockBytes, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE, 0, &pStg);
          pLockBytes->Release();
        }
        if (SUCCEEDED(hr)) {
          hr = pPS->InitNew(pStg);
          pStg->Release();
        }
        pPS->Release();
      }
    }
  }
  if (hData != NULL) ::GlobalFree(hData);
  if (FAILED(hr)) return hr;

  if (!bSiteFirst) m_pObject->SetClientSite(&m_xOleClientSite);
  m_pObject->SetHostNames(L"Container", NULL);
  SetExtent();
  return S_OK;
}

BOOL COleControlSite::SetExtent() {
  if (m_pObject == NULL) return FALSE;
  SIZE size = {m_rect.Width(), m_rect.Height()};
  if (size.cx <= 0 || size.cy <= 0) return FALSE;
  SIZEL sizeHm;
  PixelsToHimetric(size, &sizeHm);
  return SUCCEEDED(m_pObject->SetExtent(DVASPECT_CONTENT, &sizeHm));
}

// The default source interface: from IProvideClassInfo2, or the class's
// type information.
void COleControlSite::FindEventsIID() {
  m_iidEvents = GUID_NULL;
  IProvideClassInfo2* pPCI2 = NULL;
  if (SUCCEEDED(m_pObject->QueryInterface(IID_IProvideClassInfo2, (void**)&pPCI2))) {
    GUID guid;
    if (SUCCEEDED(pPCI2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &guid))) m_iidEvents = guid;
    pPCI2->Release();
    if (!::IsEqualIID(m_iidEvents, GUID_NULL)) return;
  }
  IProvideClassInfo* pPCI = NULL;
  if (FAILED(m_pObject->QueryInterface(IID_IProvideClassInfo, (void**)&pPCI))) return;
  ITypeInfo* pClassInfo = NULL;
  if (SUCCEEDED(pPCI->GetClassInfo(&pClassInfo))) {
    TYPEATTR* pAttr = NULL;
    if (SUCCEEDED(pClassInfo->GetTypeAttr(&pAttr))) {
      for (UINT i = 0; i < pAttr->cImplTypes; i++) {
        int nFlags = 0;
        HREFTYPE hRef;
        ITypeInfo* pEventInfo = NULL;
        if (FAILED(pClassInfo->GetImplTypeFlags(i, &nFlags))) continue;
        if ((nFlags & (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)) !=
            (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE))
          continue;
        if (SUCCEEDED(pClassInfo->GetRefTypeOfImplType(i, &hRef)) &&
            SUCCEEDED(pClassInfo->GetRefTypeInfo(hRef, &pEventInfo))) {
          TYPEATTR* pEventAttr = NULL;
          if (SUCCEEDED(pEventInfo->GetTypeAttr(&pEventAttr))) {
            m_iidEvents = pEventAttr->guid;
            pEventInfo->ReleaseTypeAttr(pEventAttr);
          }
          pEventInfo->Release();
        }
        break;
      }
      pClassInfo->ReleaseTypeAttr(pAttr);
    }
    pClassInfo->Release();
  }
  pPCI->Release();
}

void COleControlSite::ConnectSink(REFIID iid, LPUNKNOWN pUnkSink, DWORD* pdwCookie) {
  *pdwCookie = 0;
  // The connection holds the site, which the container owns anyway.
  if (!AfxConnectionAdvise(m_pObject, iid, pUnkSink, TRUE, pdwCookie)) *pdwCookie = 0;
}

void COleControlSite::DisconnectSink(REFIID iid, DWORD dwCookie) {
  if (dwCookie == 0 || m_pObject == NULL) return;
  LPCONNECTIONPOINTCONTAINER pCPC = NULL;
  if (FAILED(m_pObject->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) return;
  LPCONNECTIONPOINT pCP = NULL;
  if (SUCCEEDED(pCPC->FindConnectionPoint(iid, &pCP))) {
    pCP->Unadvise(dwCookie);
    pCP->Release();
  }
  pCPC->Release();
}

// The wrapper CWnd: it subclasses the control's window, unless that
// window already belongs to a CWnd of this program (a control made with
// COleControl), when it only takes the handle.
void COleControlSite::AttachWindow() {
  if (m_pWndCtrl == NULL || m_hWnd == NULL) return;
  m_pWndCtrl->m_pCtrlSite = this;
  if (m_pWndCtrl->m_hWnd == m_hWnd) return;
  if (CWnd::FromHandlePermanent(m_hWnd) == NULL) {
    m_bSubclassed = m_pWndCtrl->SubclassWindow(m_hWnd);
    if (m_bSubclassed) return;
  }
  m_bSubclassed = FALSE;
  m_pWndCtrl->m_hWnd = m_hWnd;
}

void COleControlSite::DetachWindow() {
  if (m_pWndCtrl == NULL) return;
  if (m_pWndCtrl->m_hWnd != NULL) {
    if (m_bSubclassed && ::IsWindow(m_pWndCtrl->m_hWnd))
      m_pWndCtrl->UnsubclassWindow();
    else
      m_pWndCtrl->m_hWnd = NULL;
  }
  m_bSubclassed = FALSE;
  m_pWndCtrl->m_pCtrlSite = NULL;
  m_pWndCtrl = NULL;
}

BOOL COleControlSite::DestroyControl() {
  _AfxReleaseExtendedControl(this);
  if (_afxOccBindHooks != NULL) _afxOccBindHooks->pfnRelease(this);
  DetachWindow();
  if (m_hWnd != NULL) m_pCtrlCont->m_siteMap.RemoveKey(m_hWnd);
  if (m_pCtrlCont->m_pSiteUIActive == this) m_pCtrlCont->m_pSiteUIActive = NULL;
  if (m_pCtrlCont->m_pSiteFocus == this) m_pCtrlCont->m_pSiteFocus = NULL;
  if (m_pCtrlCont->m_pSiteCapture == this) {
    m_pCtrlCont->m_pSiteCapture = NULL;
    if (::GetCapture() == m_pCtrlCont->m_pWnd->GetSafeHwnd()) ::ReleaseCapture();
  }
  if (m_pObject != NULL) {
    DisconnectSink(IID_INotifyDBEvents, m_dwNotifyDBEvents);
    DisconnectSink(IID_IPropertyNotifySink, m_dwPropNotifySink);
    DisconnectSink(m_iidEvents, m_dwEventSink);
    m_dwEventSink = m_dwPropNotifySink = m_dwNotifyDBEvents = 0;
    m_dispDriver.ReleaseDispatch();
    // Closing deactivates it, which destroys its window.
    m_pObject->Close(OLECLOSE_NOSAVE);
    if (m_pInPlaceObject != NULL) {
      m_pInPlaceObject->Release();
      m_pInPlaceObject = NULL;
    }
    m_pObject->SetClientSite(NULL);
    m_pObject->Release();
    m_pObject = NULL;
  }
  if (m_pActiveObject != NULL) {
    m_pActiveObject->Release();
    m_pActiveObject = NULL;
  }
  m_hWnd = NULL;
  delete m_pWndOwned;
  m_pWndOwned = NULL;
  POSITION pos = m_pCtrlCont->m_listSitesOrWnds.Find(this);
  if (pos != NULL) m_pCtrlCont->m_listSitesOrWnds.RemoveAt(pos);
  InternalRelease();  // the container's reference
  return TRUE;
}

// ---------------------------------------------------------------------
// Properties and methods

void COleControlSite::InvokeHelperV(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                                    const BYTE* pbParamInfo, va_list argList) {
  if (m_dispDriver.m_lpDispatch == NULL) AfxThrowOleException(E_NOINTERFACE);
  m_dispDriver.InvokeHelperV(dwDispID, wFlags, vtRet, pvRet, pbParamInfo, argList);
}

void AFX_CDECL COleControlSite::InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                             void* pvRet, const BYTE* pbParamInfo, ...) {
  va_list argList;
  va_start(argList, pbParamInfo);
  try {
    InvokeHelperV(dwDispID, wFlags, vtRet, pvRet, pbParamInfo, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}

void COleControlSite::GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const {
  ((COleControlSite*)this)->InvokeHelper(dwDispID, DISPATCH_PROPERTYGET, vtProp, pvProp, NULL);
}

void COleControlSite::SetPropertyV(DISPID dwDispID, VARTYPE vtProp, va_list argList) {
  // A VT_BSTR value is an LPCTSTR.
  BYTE rgbParams[2] = {(BYTE)(vtProp == VT_BSTR ? VT_BSTRT : vtProp), 0};
  WORD wFlags = vtProp == VT_DISPATCH ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT;
  InvokeHelperV(dwDispID, wFlags, VT_EMPTY, NULL, rgbParams, argList);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
void AFX_CDECL COleControlSite::SetProperty(DISPID dwDispID, VARTYPE vtProp, ...) {
  va_list argList;
  va_start(argList, vtProp);
  try {
    SetPropertyV(dwDispID, vtProp, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}

BOOL AFX_CDECL COleControlSite::SafeSetProperty(DISPID dwDispID, VARTYPE vtProp, ...) {
  va_list argList;
  va_start(argList, vtProp);
  BOOL bOK = TRUE;
  try {
    SetPropertyV(dwDispID, vtProp, argList);
  } catch (CException* e) {
    e->Delete();
    bOK = FALSE;
  }
  va_end(argList);
  return bOK;
}
#pragma clang diagnostic pop

// ---------------------------------------------------------------------
// The control's window

DWORD COleControlSite::GetStyle() const {
  return m_hWnd != NULL ? (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE) : m_dwStyle;
}

DWORD COleControlSite::GetExStyle() const {
  return m_hWnd != NULL ? (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE) : 0;
}

BOOL COleControlSite::ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  m_dwStyle = (m_dwStyle & ~dwRemove) | dwAdd;
  if ((dwRemove | dwAdd) & WS_DISABLED) EnableWindow((dwAdd & WS_DISABLED) == 0);
  if (m_hWnd == NULL) return TRUE;
  return CWnd::ModifyStyle(m_hWnd, dwRemove & ~WS_DISABLED, dwAdd & ~WS_DISABLED, nFlags);
}

BOOL COleControlSite::ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
  if (m_hWnd == NULL) return FALSE;
  return CWnd::ModifyStyleEx(m_hWnd, dwRemove, dwAdd, nFlags);
}

// The text is the Caption or Text property, when the control has one.
void COleControlSite::SetWindowText(LPCTSTR lpszString) {
  if (SafeSetProperty(DISPID_CAPTION, VT_BSTR, lpszString)) return;
  if (SafeSetProperty(DISPID_TEXT, VT_BSTR, lpszString)) return;
  if (m_hWnd != NULL) ::SetWindowText(m_hWnd, lpszString);
}

void COleControlSite::GetWindowText(CString& str) const {
  str.Empty();
  DISPID ids[2] = {DISPID_CAPTION, DISPID_TEXT};
  for (int i = 0; i < 2; i++) {
    try {
      GetProperty(ids[i], VT_BSTR, &str);
      return;
    } catch (CException* e) {
      e->Delete();
    }
  }
  if (m_hWnd != NULL) {
    int n = ::GetWindowTextLength(m_hWnd);
    ::GetWindowText(m_hWnd, str.GetBuffer(n + 1), n + 1);
    str.ReleaseBuffer();
  }
}

int COleControlSite::SetDlgCtrlID(int nID) {
  int nOld = (int)m_nID;
  m_nID = (UINT)nID;
  if (m_hWnd != NULL) ::SetWindowLongPtr(m_hWnd, GWLP_ID, (LONG_PTR)nID);
  return nOld;
}

void COleControlSite::MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint) {
  if (m_bIsWindowless && bRepaint) InvalidateWindowless(NULL, TRUE);  // where it was
  m_rect.SetRect(x, y, x + nWidth, y + nHeight);
  if (m_bIsWindowless && bRepaint) InvalidateWindowless(NULL, TRUE);
  SetExtent();
  if (m_pInPlaceObject != NULL)
    m_pInPlaceObject->SetObjectRects(&m_rect, &m_rect);
  else if (m_hWnd != NULL)
    ::MoveWindow(m_hWnd, x, y, nWidth, nHeight, bRepaint);
}

BOOL COleControlSite::SetWindowPos(const CWnd* pWndInsertAfter, int x, int y, int cx, int cy,
                                   UINT nFlags) {
  if (!(nFlags & SWP_NOMOVE) || !(nFlags & SWP_NOSIZE)) {
    CRect rect(m_rect);
    if (!(nFlags & SWP_NOMOVE)) rect.OffsetRect(x - rect.left, y - rect.top);
    if (!(nFlags & SWP_NOSIZE)) rect.BottomRight() = rect.TopLeft() + CSize(cx, cy);
    MoveWindow(rect.left, rect.top, rect.Width(), rect.Height(), !(nFlags & SWP_NOREDRAW));
  }
  if (m_hWnd == NULL) return TRUE;
  return ::SetWindowPos(m_hWnd, pWndInsertAfter->GetSafeHwnd(), 0, 0, 0, 0,
                        nFlags | SWP_NOMOVE | SWP_NOSIZE);
}

BOOL COleControlSite::ShowWindow(int nCmdShow) {
  if (nCmdShow == SW_HIDE && m_bUIActive && m_pInPlaceObject != NULL)
    m_pInPlaceObject->UIDeactivate();
  BOOL bWasVisible = (m_dwStyle & WS_VISIBLE) != 0;
  if (nCmdShow == SW_HIDE)
    m_dwStyle &= ~WS_VISIBLE;
  else
    m_dwStyle |= WS_VISIBLE;
  if (m_bIsWindowless) {
    if (bWasVisible != ((m_dwStyle & WS_VISIBLE) != 0)) InvalidateWindowless(NULL, TRUE);
    return bWasVisible;
  }
  return m_hWnd != NULL ? ::ShowWindow(m_hWnd, nCmdShow) : FALSE;
}

BOOL COleControlSite::IsWindowEnabled() const {
  BOOL bEnabled = TRUE;
  try {
    GetProperty(DISPID_ENABLED, VT_BOOL, &bEnabled);
    return bEnabled;
  } catch (CException* e) {
    e->Delete();
  }
  return m_hWnd != NULL ? ::IsWindowEnabled(m_hWnd) : !(m_dwStyle & WS_DISABLED);
}

BOOL COleControlSite::EnableWindow(BOOL bEnable) {
  BOOL bWasDisabled = !IsWindowEnabled();
  if (bEnable)
    m_dwStyle &= ~WS_DISABLED;
  else
    m_dwStyle |= WS_DISABLED;
  if (!SafeSetProperty(DISPID_ENABLED, VT_BOOL, bEnable) && m_hWnd != NULL)
    ::EnableWindow(m_hWnd, bEnable);
  return bWasDisabled;
}

CWnd* COleControlSite::SetFocus() { return SetFocus(NULL); }

CWnd* COleControlSite::SetFocus(LPMSG lpmsg) {
  CWnd* pOld = CWnd::GetFocus();
  DoVerb(OLEIVERB_UIACTIVATE, lpmsg);
  if (m_hWnd != NULL && ::GetFocus() != m_hWnd && !::IsChild(m_hWnd, ::GetFocus()))
    ::SetFocus(m_hWnd);
  return pOld;
}

BOOL COleControlSite::DoVerb(LONG nVerb, LPMSG lpMsg) {
  if (m_pObject == NULL) return FALSE;
  return SUCCEEDED(m_pObject->DoVerb(nVerb, lpMsg, &m_xOleClientSite, 0,
                                     m_pCtrlCont->m_pWnd->GetSafeHwnd(), &m_rect));
}

void COleControlSite::FreezeEvents(BOOL bFreeze) {
  if (m_pObject == NULL) return;
  IOleControl* pCtl = NULL;
  if (SUCCEEDED(m_pObject->QueryInterface(IID_IOleControl, (void**)&pCtl))) {
    m_nFreezeEvents += bFreeze ? 1 : -1;
    pCtl->FreezeEvents(bFreeze);
    pCtl->Release();
  }
}

void COleControlSite::GetControlInfo() {
  if (m_ctlInfo.hAccel != NULL) ::DestroyAcceleratorTable(m_ctlInfo.hAccel);
  memset(&m_ctlInfo, 0, sizeof(m_ctlInfo));
  m_ctlInfo.cb = sizeof(m_ctlInfo);
  IOleControl* pCtl = NULL;
  if (m_pObject != NULL &&
      SUCCEEDED(m_pObject->QueryInterface(IID_IOleControl, (void**)&pCtl))) {
    if (FAILED(pCtl->GetControlInfo(&m_ctlInfo))) {
      memset(&m_ctlInfo, 0, sizeof(m_ctlInfo));
      m_ctlInfo.cb = sizeof(m_ctlInfo);
    }
    pCtl->Release();
  }
}

BOOL COleControlSite::IsDefaultButton() {
  return (m_dwMiscStatus & OLEMISC_ACTSLIKEBUTTON) && (m_dwStyle & BS_DEFPUSHBUTTON);
}

DWORD COleControlSite::GetDefBtnCode() {
  if (!(m_dwMiscStatus & OLEMISC_ACTSLIKEBUTTON)) return 0;
  return IsDefaultButton() ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON;
}

void COleControlSite::SetDefaultButton(BOOL bDefault) {
  if (!(m_dwMiscStatus & OLEMISC_ACTSLIKEBUTTON)) return;
  if (bDefault == IsDefaultButton()) return;
  if (bDefault)
    m_dwStyle |= BS_DEFPUSHBUTTON;
  else
    m_dwStyle &= ~BS_DEFPUSHBUTTON;
  IOleControl* pCtl = NULL;
  if (SUCCEEDED(m_pObject->QueryInterface(IID_IOleControl, (void**)&pCtl))) {
    pCtl->OnAmbientPropertyChange(DISPID_AMBIENT_DISPLAYASDEFAULT);
    pCtl->Release();
  }
}

BOOL COleControlSite::GetEventIID(IID* piid) {
  *piid = m_iidEvents;
  return !::IsEqualIID(m_iidEvents, GUID_NULL);
}

// The control's own window first (its _REFLECT entries), then the
// container window's event sink map.
HRESULT COleControlSite::FireEvent(AFX_EVENT* pEvent) {
  InternalAddRef();  // a handler may destroy the control
  BOOL bHandled = FALSE;
  if (m_pWndCtrl != NULL)
    bHandled = m_pWndCtrl->OnCmdMsg((UINT)-1, (int)CN_EVENT, pEvent, NULL);
  if (!bHandled && m_pCtrlCont->m_pWnd != NULL)
    _AfxGetOccManager()->OnEvent(m_pCtrlCont->m_pWnd, m_nID, pEvent, NULL);
  HRESULT hr = pEvent->m_hResult;
  InternalRelease();
  return hr;
}

// ---------------------------------------------------------------------
// IOleClientSite

#define SITE(part) METHOD_PROLOGUE(COleControlSite, part)

STDMETHODIMP_(ULONG) COleControlSite::XOleClientSite::AddRef() {
  SITE(OleClientSite);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XOleClientSite::Release() {
  SITE(OleClientSite);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XOleClientSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(OleClientSite);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlSite::XOleClientSite::SaveObject() { return E_NOTIMPL; }
STDMETHODIMP COleControlSite::XOleClientSite::GetMoniker(DWORD, DWORD, LPMONIKER* ppmk) {
  *ppmk = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP COleControlSite::XOleClientSite::GetContainer(LPOLECONTAINER* ppContainer) {
  SITE(OleClientSite);
  return (HRESULT)pThis->m_pCtrlCont->ExternalQueryInterface(&IID_IOleContainer,
                                                              (void**)ppContainer);
}
STDMETHODIMP COleControlSite::XOleClientSite::ShowObject() { return S_OK; }
STDMETHODIMP COleControlSite::XOleClientSite::OnShowWindow(BOOL) { return S_OK; }
STDMETHODIMP COleControlSite::XOleClientSite::RequestNewObjectLayout() { return E_NOTIMPL; }

// ---------------------------------------------------------------------
// IOleInPlaceSite

STDMETHODIMP_(ULONG) COleControlSite::XOleIPSite::AddRef() {
  SITE(OleIPSite);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XOleIPSite::Release() {
  SITE(OleIPSite);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XOleIPSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(OleIPSite);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlSite::XOleIPSite::GetWindow(HWND* phwnd) {
  SITE(OleIPSite);
  *phwnd = pThis->m_pCtrlCont->m_pWnd->GetSafeHwnd();
  return *phwnd != NULL ? S_OK : E_FAIL;
}
STDMETHODIMP COleControlSite::XOleIPSite::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }
STDMETHODIMP COleControlSite::XOleIPSite::CanInPlaceActivate() { return S_OK; }
STDMETHODIMP COleControlSite::XOleIPSite::OnInPlaceActivate() {
  SITE(OleIPSite);
  pThis->OnActivated(0);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::OnUIActivate() {
  SITE(OleIPSite);
  pThis->m_pCtrlCont->OnUIActivate(pThis);
  pThis->m_bUIActive = TRUE;
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::GetWindowContext(LPOLEINPLACEFRAME* ppFrame,
                                                           LPOLEINPLACEUIWINDOW* ppDoc,
                                                           LPRECT prcPosRect,
                                                           LPRECT prcClipRect,
                                                           LPOLEINPLACEFRAMEINFO pFrameInfo) {
  SITE(OleIPSite);
  COleControlContainer* pCont = pThis->m_pCtrlCont;
  *ppFrame = NULL;
  *ppDoc = NULL;
  pCont->ExternalQueryInterface(&IID_IOleInPlaceFrame, (void**)ppFrame);
  *prcPosRect = pThis->m_rect;
  *prcClipRect = pThis->m_rect;
  if (pCont->m_pWnd->GetSafeHwnd() != NULL) ::GetClientRect(pCont->m_pWnd->m_hWnd, prcClipRect);
  if (pFrameInfo != NULL) {
    pFrameInfo->fMDIApp = FALSE;
    HWND hWnd = pCont->m_pWnd->GetSafeHwnd();
    pFrameInfo->hwndFrame = hWnd != NULL ? ::GetAncestor(hWnd, GA_ROOT) : NULL;
    pFrameInfo->haccel = NULL;
    pFrameInfo->cAccelEntries = 0;
  }
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::Scroll(SIZE) { return S_FALSE; }
STDMETHODIMP COleControlSite::XOleIPSite::OnUIDeactivate(BOOL) {
  SITE(OleIPSite);
  pThis->m_bUIActive = FALSE;
  pThis->m_pCtrlCont->OnUIDeactivate(pThis);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::OnInPlaceDeactivate() {
  SITE(OleIPSite);
  pThis->OnDeactivated();
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::DiscardUndoState() { return S_OK; }
STDMETHODIMP COleControlSite::XOleIPSite::DeactivateAndUndo() {
  SITE(OleIPSite);
  if (pThis->m_pInPlaceObject != NULL) pThis->m_pInPlaceObject->UIDeactivate();
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::OnPosRectChange(LPCRECT lprcPosRect) {
  SITE(OleIPSite);
  if (pThis->m_bIsWindowless) pThis->InvalidateWindowless(NULL, TRUE);
  pThis->m_rect = *lprcPosRect;
  if (pThis->m_pInPlaceObject != NULL)
    pThis->m_pInPlaceObject->SetObjectRects(&pThis->m_rect, &pThis->m_rect);
  if (pThis->m_bIsWindowless) pThis->InvalidateWindowless(NULL, TRUE);
  return S_OK;
}

// ----- IOleInPlaceSiteEx

STDMETHODIMP COleControlSite::XOleIPSite::OnInPlaceActivateEx(BOOL* pfNoRedraw, DWORD dwFlags) {
  SITE(OleIPSite);
  if (pfNoRedraw != NULL) *pfNoRedraw = FALSE;
  pThis->OnActivated(dwFlags);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::OnInPlaceDeactivateEx(BOOL) {
  SITE(OleIPSite);
  pThis->OnDeactivated();
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::RequestUIActivate() { return S_OK; }

// ----- IOleInPlaceSiteWindowless: what a windowless control asks of the
// window it lives in.

STDMETHODIMP COleControlSite::XOleIPSite::CanWindowlessActivate() {
  SITE(OleIPSite);
  return pThis->m_pCtrlCont->m_pWnd->GetSafeHwnd() != NULL ? S_OK : S_FALSE;
}
STDMETHODIMP COleControlSite::XOleIPSite::GetCapture() {
  SITE(OleIPSite);
  return pThis->m_pCtrlCont->m_pSiteCapture == pThis ? S_OK : S_FALSE;
}
STDMETHODIMP COleControlSite::XOleIPSite::SetCapture(BOOL fCapture) {
  SITE(OleIPSite);
  COleControlContainer* pCont = pThis->m_pCtrlCont;
  HWND hWnd = pCont->m_pWnd->GetSafeHwnd();
  if (fCapture) {
    pCont->m_pSiteCapture = pThis;
    if (::GetCapture() != hWnd) ::SetCapture(hWnd);
  } else if (pCont->m_pSiteCapture == pThis) {
    pCont->m_pSiteCapture = NULL;
    if (::GetCapture() == hWnd) ::ReleaseCapture();
  }
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::GetFocus() {
  SITE(OleIPSite);
  return pThis->m_pCtrlCont->m_pSiteFocus == pThis ? S_OK : S_FALSE;
}
STDMETHODIMP COleControlSite::XOleIPSite::SetFocus(BOOL fFocus) {
  SITE(OleIPSite);
  COleControlContainer* pCont = pThis->m_pCtrlCont;
  HWND hWnd = pCont->m_pWnd->GetSafeHwnd();
  if (!fFocus) {
    if (pCont->m_pSiteFocus == pThis) pCont->m_pSiteFocus = NULL;
    return S_OK;
  }
  if (pCont->m_pSiteFocus == pThis && ::GetFocus() == hWnd) return S_OK;
  // The old windowless control loses the focus.
  COleControlSite* pOld = pCont->m_pSiteFocus;
  pCont->m_pSiteFocus = pThis;
  LRESULT lResult = 0;
  if (pOld != NULL && pOld != pThis && pOld->m_bIsWindowless)
    pOld->SendWindowlessMessage(WM_KILLFOCUS, (WPARAM)hWnd, 0, &lResult);
  // The window takes the keyboard (the control hears WM_SETFOCUS from
  // it); when it has it already, the control hears it now.
  if (::GetFocus() != hWnd)
    ::SetFocus(hWnd);
  else
    pThis->SendWindowlessMessage(WM_SETFOCUS, (WPARAM)hWnd, 0, &lResult);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::GetDC(LPCRECT pRect, DWORD grfFlags, HDC* phDC) {
  SITE(OleIPSite);
  if (phDC == NULL) return E_POINTER;
  *phDC = NULL;
  HWND hWnd = pThis->m_pCtrlCont->m_pWnd->GetSafeHwnd();
  if (hWnd == NULL) return E_FAIL;
  HDC hDC = ::GetDC(hWnd);
  if (hDC == NULL) return E_FAIL;
  ::SaveDC(hDC);
  // Only the control's part of the window.
  CRect rect(pThis->m_rect);
  if (pRect != NULL) rect.IntersectRect(&rect, pRect);
  ::IntersectClipRect(hDC, rect.left, rect.top, rect.right, rect.bottom);
  if (grfFlags & OLEDC_PAINTBKGND) ::SendMessage(hWnd, WM_ERASEBKGND, (WPARAM)hDC, 0);
  *phDC = hDC;
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::ReleaseDC(HDC hDC) {
  SITE(OleIPSite);
  if (hDC == NULL) return E_INVALIDARG;
  ::RestoreDC(hDC, -1);
  ::ReleaseDC(pThis->m_pCtrlCont->m_pWnd->GetSafeHwnd(), hDC);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::InvalidateRect(LPCRECT pRect, BOOL fErase) {
  SITE(OleIPSite);
  pThis->InvalidateWindowless(pRect, fErase);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::InvalidateRgn(HRGN hRgn, BOOL fErase) {
  SITE(OleIPSite);
  HWND hWnd = pThis->m_pCtrlCont->m_pWnd->GetSafeHwnd();
  if (hWnd == NULL) return E_FAIL;
  if (hRgn == NULL) {
    pThis->InvalidateWindowless(NULL, fErase);
    return S_OK;
  }
  // Within the control's part.
  HRGN hRgnPart = ::CreateRectRgnIndirect(&pThis->m_rect);
  ::CombineRgn(hRgnPart, hRgnPart, hRgn, RGN_AND);
  ::InvalidateRgn(hWnd, hRgnPart, fErase);
  ::DeleteObject(hRgnPart);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::ScrollRect(INT, INT, LPCRECT pRectScroll,
                                                     LPCRECT pRectClip) {
  // The scrolled part is drawn again.
  SITE(OleIPSite);
  CRect rect(pRectScroll != NULL ? *pRectScroll : pThis->m_rect);
  if (pRectClip != NULL) rect.IntersectRect(&rect, pRectClip);
  pThis->InvalidateWindowless(&rect, FALSE);
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleIPSite::AdjustRect(LPRECT prc) {
  return prc != NULL ? S_OK : E_INVALIDARG;
}
STDMETHODIMP COleControlSite::XOleIPSite::OnDefWindowMessage(UINT msg, WPARAM wParam,
                                                             LPARAM lParam, LRESULT* plResult) {
  SITE(OleIPSite);
  CWnd* pWnd = pThis->m_pCtrlCont->m_pWnd;
  if (plResult == NULL) return E_POINTER;
  *plResult = 0;
  if (pWnd->GetSafeHwnd() == NULL) return S_FALSE;
  // The window's default handling, not its message map: the control has
  // had the message.
  *plResult = pWnd->DefWindowProc(msg, wParam, lParam);
  return S_OK;
}

// ----- Windowless controls

void COleControlSite::OnActivated(DWORD dwFlags) {
  m_bInPlaceActive = TRUE;
  if (m_pInPlaceObject == NULL)
    m_pObject->QueryInterface(IID_IOleInPlaceObject, (void**)&m_pInPlaceObject);
  if ((dwFlags & ACTIVATE_WINDOWLESS) && !m_bIsWindowless) {
    IOleInPlaceObjectWindowless* pWindowless = NULL;
    if (SUCCEEDED(m_pObject->QueryInterface(IID_IOleInPlaceObjectWindowless,
                                            (void**)&pWindowless))) {
      m_pWindowlessObject = pWindowless;
      m_bIsWindowless = TRUE;
      m_pCtrlCont->m_nWindowlessControls++;
      m_pCtrlCont->RegisterWindowlessDropTarget(this);
      InvalidateWindowless(NULL, TRUE);
    }
  }
}

void COleControlSite::OnDeactivated() {
  m_bInPlaceActive = FALSE;
  if (m_bIsWindowless) {
    InvalidateWindowless(NULL, TRUE);
    m_bIsWindowless = FALSE;
    m_pCtrlCont->m_nWindowlessControls--;
    if (m_pCtrlCont->m_pSiteCapture == this) {
      m_pCtrlCont->m_pSiteCapture = NULL;
      if (::GetCapture() == m_pCtrlCont->m_pWnd->GetSafeHwnd()) ::ReleaseCapture();
    }
    if (m_pCtrlCont->m_pSiteFocus == this) m_pCtrlCont->m_pSiteFocus = NULL;
    ((IOleInPlaceObjectWindowless*)m_pWindowlessObject)->Release();
    m_pWindowlessObject = NULL;
  }
  if (m_pInPlaceObject != NULL) {
    m_pInPlaceObject->Release();
    m_pInPlaceObject = NULL;
  }
}

void COleControlSite::InvalidateWindowless(LPCRECT lpRect, BOOL bErase) {
  HWND hWnd = m_pCtrlCont->m_pWnd->GetSafeHwnd();
  if (hWnd == NULL) return;
  CRect rect(m_rect);
  if (lpRect != NULL) rect.IntersectRect(&rect, lpRect);
  if (!rect.IsRectEmpty()) ::InvalidateRect(hWnd, &rect, bErase);
}

HRESULT COleControlSite::SendWindowlessMessage(UINT msg, WPARAM wParam, LPARAM lParam,
                                               LRESULT* plResult) {
  *plResult = 0;
  if (!m_bIsWindowless || m_pWindowlessObject == NULL) return S_FALSE;
  InternalAddRef();  // the control may go away
  IOleInPlaceObjectWindowless* pWindowless = (IOleInPlaceObjectWindowless*)m_pWindowlessObject;
  pWindowless->AddRef();
  HRESULT hr = pWindowless->OnWindowMessage(msg, wParam, lParam, plResult);
  pWindowless->Release();
  InternalRelease();
  return hr;
}

// ---------------------------------------------------------------------
// IOleControlSite

STDMETHODIMP_(ULONG) COleControlSite::XOleControlSite::AddRef() {
  SITE(OleControlSite);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XOleControlSite::Release() {
  SITE(OleControlSite);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XOleControlSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(OleControlSite);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlSite::XOleControlSite::OnControlInfoChanged() {
  SITE(OleControlSite);
  pThis->GetControlInfo();
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleControlSite::LockInPlaceActive(BOOL) { return S_OK; }
STDMETHODIMP COleControlSite::XOleControlSite::GetExtendedControl(LPDISPATCH* ppDisp) {
  SITE(OleControlSite);
  if (ppDisp == NULL) return E_POINTER;
  *ppDisp = pThis->GetExtendedControl();
  return *ppDisp != NULL ? S_OK : E_NOTIMPL;
}
STDMETHODIMP COleControlSite::XOleControlSite::TransformCoords(POINTL* pptlHimetric,
                                                               POINTF* pptfContainer,
                                                               DWORD dwFlags) {
  // The container's coordinates are pixels.
  if (dwFlags & XFORMCOORDS_HIMETRICTOCONTAINER) {
    SIZEL sizeHm = {pptlHimetric->x, pptlHimetric->y};
    SIZE size;
    HimetricToPixels(sizeHm, &size);
    pptfContainer->x = (float)size.cx;
    pptfContainer->y = (float)size.cy;
  } else if (dwFlags & XFORMCOORDS_CONTAINERTOHIMETRIC) {
    SIZE size = {(LONG)pptfContainer->x, (LONG)pptfContainer->y};
    SIZEL sizeHm;
    PixelsToHimetric(size, &sizeHm);
    pptlHimetric->x = sizeHm.cx;
    pptlHimetric->y = sizeHm.cy;
  } else {
    return E_INVALIDARG;
  }
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleControlSite::TranslateAccelerator(LPMSG, DWORD) {
  return S_FALSE;
}
STDMETHODIMP COleControlSite::XOleControlSite::OnFocus(BOOL bGotFocus) {
  SITE(OleControlSite);
  if (bGotFocus)
    pThis->m_pCtrlCont->m_pSiteFocus = pThis;
  else if (pThis->m_pCtrlCont->m_pSiteFocus == pThis)
    pThis->m_pCtrlCont->m_pSiteFocus = NULL;
  return S_OK;
}
STDMETHODIMP COleControlSite::XOleControlSite::ShowPropertyFrame() { return E_NOTIMPL; }

// ---------------------------------------------------------------------
// Ambient properties

STDMETHODIMP_(ULONG) COleControlSite::XAmbientProps::AddRef() {
  SITE(AmbientProps);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XAmbientProps::Release() {
  SITE(AmbientProps);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XAmbientProps::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(AmbientProps);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlSite::XAmbientProps::GetTypeInfoCount(UINT* pctinfo) {
  *pctinfo = 0;
  return S_OK;
}
STDMETHODIMP COleControlSite::XAmbientProps::GetTypeInfo(UINT, LCID, ITypeInfo** pptinfo) {
  *pptinfo = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP COleControlSite::XAmbientProps::GetIDsOfNames(REFIID, LPOLESTR*, UINT cNames, LCID,
                                                           DISPID* rgdispid) {
  for (UINT i = 0; i < cNames; i++) rgdispid[i] = DISPID_UNKNOWN;
  return DISP_E_UNKNOWNNAME;
}
STDMETHODIMP COleControlSite::XAmbientProps::Invoke(DISPID dispid, REFIID, LCID, WORD wFlags,
                                                    DISPPARAMS*, LPVARIANT pvarResult,
                                                    LPEXCEPINFO, UINT*) {
  SITE(AmbientProps);
  if (!(wFlags & DISPATCH_PROPERTYGET)) return DISP_E_MEMBERNOTFOUND;
  VARIANT var;
  ::VariantInit(&var);
  CWnd* pWnd = pThis->m_pCtrlCont->m_pWnd;
  if (pWnd == NULL || !pWnd->OnAmbientProperty(pThis, dispid, &var)) return DISP_E_MEMBERNOTFOUND;
  if (pvarResult != NULL)
    *pvarResult = var;
  else
    ::VariantClear(&var);
  return S_OK;
}

// ---------------------------------------------------------------------
// Property notifications and events

STDMETHODIMP_(ULONG) COleControlSite::XPropertyNotifySink::AddRef() {
  SITE(PropertyNotifySink);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XPropertyNotifySink::Release() {
  SITE(PropertyNotifySink);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XPropertyNotifySink::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(PropertyNotifySink);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlSite::XPropertyNotifySink::OnChanged(DISPID dispid) {
  SITE(PropertyNotifySink);
  if (pThis->m_pBindings != NULL && !pThis->m_bIgnoreNotify && _afxOccBindHooks != NULL)
    _afxOccBindHooks->pfnChanged(pThis, dispid);
  AFX_EVENT event(AFX_EVENT::propChanged, dispid);
  pThis->FireEvent(&event);
  return S_OK;
}
STDMETHODIMP COleControlSite::XPropertyNotifySink::OnRequestEdit(DISPID dispid) {
  SITE(PropertyNotifySink);
  AFX_EVENT event(AFX_EVENT::propRequest, dispid);
  return pThis->FireEvent(&event) == S_FALSE ? S_FALSE : S_OK;
}

// INotifyDBEvents: each step of a change to a data source control's
// cursor is an ON_DSCNOTIFY notification, for the changes to the current
// row; the binding (occdb.cpp) then sets the bound controls.
HRESULT AFXAPI _AfxFireDSCNotify(COleControlSite* pSite, DSCSTATE nState, DSCREASON nReason) {
  AFX_EVENT event(AFX_EVENT::propDSCNotify);
  event.m_nDSCState = nState;
  event.m_nDSCReason = nReason;
  return pSite->FireEvent(&event);
}

static DSCREASON DSCReason(DWORD dwReason) {
  switch (dwReason) {
    case DBREASON_CLOSE: return dscClose;
    case DBREASON_FIND:
    case DBREASON_MOVE:
    case DBREASON_MOVEPERCENT:
    case DBREASON_NEWINDEX:
    case DBREASON_NEWPARAMETERS:
    case DBREASON_QUERYSPECCHANGED:
    case DBREASON_REFRESH:
    case DBREASON_SEEK:
    case DBREASON_SETRANGECHANGED: return dscMove;
    case DBREASON_ADDNEW:
    case DBREASON_INSERTED: return dscInsert;
    case DBREASON_EDIT: return dscEdit;
    case DBREASON_MODIFIED:
    case DBREASON_RECALC:
    case DBREASON_ROLLBACK:
    case DBREASON_ROWFIXUP: return dscModify;
    case DBREASON_DELETED: return dscDelete;
    case DBREASON_COMMIT: return dscCommit;
    default: return dscNoReason;
  }
}

HRESULT COleControlSite::XNotifyDBEvents::Notify(DSCSTATE nState, DWORD dwEventWhat,
                                                 ULONG cReasons, DBNOTIFYREASON rgReasons[]) {
  SITE(NotifyDBEvents);
  HRESULT hr = S_OK;
  if (dwEventWhat & (DBEVENT_CURRENT_ROW_CHANGED | DBEVENT_CURRENT_ROW_DATA_CHANGED)) {
    // The first reason that is one of ON_DSCNOTIFY's.
    for (ULONG i = 0; i < cReasons; i++) {
      DSCREASON nReason = DSCReason(rgReasons[i].dwReason);
      if (nReason == dscNoReason) continue;
      hr = _AfxFireDSCNotify(pThis, nState, nReason);
      break;
    }
  }
  if (hr != S_OK) return hr;
  if (pThis->m_pDataSourceControl != NULL && _afxOccBindHooks != NULL)
    hr = _afxOccBindHooks->pfnDBEvent(pThis, nState, dwEventWhat, cReasons, rgReasons);
  return hr;
}

STDMETHODIMP_(ULONG) COleControlSite::XNotifyDBEvents::AddRef() {
  SITE(NotifyDBEvents);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XNotifyDBEvents::Release() {
  SITE(NotifyDBEvents);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XNotifyDBEvents::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(NotifyDBEvents);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
#define _AFX_DBEVENT(name, state)                                                      \
  STDMETHODIMP COleControlSite::XNotifyDBEvents::name(DWORD dwEventWhat, ULONG cReasons, \
                                                      DBNOTIFYREASON rgReasons[]) {      \
    return Notify(state, dwEventWhat, cReasons, rgReasons);                              \
  }
_AFX_DBEVENT(OKToDo, dscOKToDo)
_AFX_DBEVENT(Cancelled, dscCancelled)
_AFX_DBEVENT(SyncBefore, dscSyncBefore)
_AFX_DBEVENT(AboutToDo, dscAboutToDo)
_AFX_DBEVENT(FailedToDo, dscFailedToDo)
_AFX_DBEVENT(SyncAfter, dscSyncAfter)
_AFX_DBEVENT(DidEvent, dscDidEvent)
#undef _AFX_DBEVENT

STDMETHODIMP_(ULONG) COleControlSite::XEventSink::AddRef() {
  SITE(EventSink);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlSite::XEventSink::Release() {
  SITE(EventSink);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlSite::XEventSink::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  SITE(EventSink);
  if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IDispatch) ||
      ::IsEqualIID(iid, pThis->m_iidEvents)) {
    *ppvObj = this;
    AddRef();
    return S_OK;
  }
  *ppvObj = NULL;
  return E_NOINTERFACE;
}
STDMETHODIMP COleControlSite::XEventSink::GetTypeInfoCount(UINT* pctinfo) {
  *pctinfo = 0;
  return S_OK;
}
STDMETHODIMP COleControlSite::XEventSink::GetTypeInfo(UINT, LCID, ITypeInfo** pptinfo) {
  *pptinfo = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP COleControlSite::XEventSink::GetIDsOfNames(REFIID, LPOLESTR*, UINT cNames, LCID,
                                                        DISPID* rgdispid) {
  for (UINT i = 0; i < cNames; i++) rgdispid[i] = DISPID_UNKNOWN;
  return DISP_E_UNKNOWNNAME;
}
STDMETHODIMP COleControlSite::XEventSink::Invoke(DISPID dispid, REFIID, LCID, WORD,
                                                 DISPPARAMS* pDispParams, LPVARIANT pvarResult,
                                                 LPEXCEPINFO pExcepInfo, UINT* puArgErr) {
  SITE(EventSink);
  if (pvarResult != NULL) ::VariantInit(pvarResult);
  AFX_EVENT event(AFX_EVENT::event, dispid, pDispParams, pExcepInfo, puArgErr);
  HRESULT hr = pThis->FireEvent(&event);
  // An event nobody handles is no error.
  return FAILED(hr) ? hr : S_OK;
}

// =====================================================================
// COleControlContainer

BEGIN_INTERFACE_MAP(COleControlContainer, CCmdTarget)
  INTERFACE_PART(COleControlContainer, IID_IOleInPlaceFrame, OleIPFrame)
  INTERFACE_PART(COleControlContainer, IID_IOleInPlaceUIWindow, OleIPFrame)
  INTERFACE_PART(COleControlContainer, IID_IOleWindow, OleIPFrame)
  INTERFACE_PART(COleControlContainer, IID_IOleContainer, OleContainer)
  INTERFACE_PART(COleControlContainer, IID_IParseDisplayName, OleContainer)
END_INTERFACE_MAP()

COleControlContainer::COleControlContainer(CWnd* pWnd)
    : m_pWnd(pWnd),
      m_pSiteUIActive(NULL),
      m_pSiteFocus(NULL),
      m_pSiteCapture(NULL),
      m_pOleFont(NULL),
      m_crBack(0x80000000 | COLOR_BTNFACE),
      m_crFore(0x80000000 | COLOR_WINDOWTEXT),
      m_nWindowlessControls(0),
      m_pActiveObject(NULL),
      m_pWindowlessDropTarget(NULL) {}

COleControlContainer::~COleControlContainer() {
  DestroyAll();
  delete m_pWindowlessDropTarget;
  if (m_pOleFont != NULL) m_pOleFont->Release();
  if (m_pActiveObject != NULL) m_pActiveObject->Release();
}

LPDROPTARGET COleControlContainer::GetWindowlessDropTarget(CPoint point) const {
  COleControlSite* pSite = FindWindowlessSite(point);
  if (pSite == NULL || pSite->m_pWindowlessObject == NULL) return NULL;
  LPDROPTARGET pTarget = NULL;
  if (((IOleInPlaceObjectWindowless*)pSite->m_pWindowlessObject)->GetDropTarget(&pTarget) != S_OK)
    return NULL;
  return pTarget;
}

void COleControlContainer::RegisterWindowlessDropTarget(COleControlSite* pSite) {
  if (m_pWindowlessDropTarget != NULL || m_pWnd->GetSafeHwnd() == NULL ||
      m_pWnd->m_pDropTarget != NULL || pSite->m_pWindowlessObject == NULL)
    return;
  LPDROPTARGET pTarget = NULL;
  if (((IOleInPlaceObjectWindowless*)pSite->m_pWindowlessObject)->GetDropTarget(&pTarget) != S_OK ||
      pTarget == NULL)
    return;
  pTarget->Release();
  m_pWindowlessDropTarget = new COleDropTarget;
  if (!m_pWindowlessDropTarget->Register(m_pWnd)) {
    delete m_pWindowlessDropTarget;
    m_pWindowlessDropTarget = NULL;
  }
}

COleControlSite* COleControlContainer::FindWindowlessSite(CPoint point) const {
  // The last made is on top.
  POSITION pos = m_listSitesOrWnds.GetTailPosition();
  while (pos != NULL) {
    COleControlSite* pSite = (COleControlSite*)m_listSitesOrWnds.GetPrev(pos);
    if (pSite->m_bIsWindowless && (pSite->m_dwStyle & WS_VISIBLE) && pSite->m_rect.PtInRect(point))
      return pSite;
  }
  return NULL;
}

BOOL COleControlContainer::OnPaint(CDC* pDC) {
  BOOL bDrew = FALSE;
  POSITION pos = m_listSitesOrWnds.GetHeadPosition();
  while (pos != NULL) {
    COleControlSite* pSite = (COleControlSite*)m_listSitesOrWnds.GetNext(pos);
    if (!pSite->m_bIsWindowless || !(pSite->m_dwStyle & WS_VISIBLE) || pSite->m_pObject == NULL)
      continue;
    if (!pDC->RectVisible(&pSite->m_rect)) continue;
    IViewObject* pView = NULL;
    if (FAILED(pSite->m_pObject->QueryInterface(IID_IViewObject, (void**)&pView))) continue;
    RECTL rcl = {pSite->m_rect.left, pSite->m_rect.top, pSite->m_rect.right,
                 pSite->m_rect.bottom};
    if (SUCCEEDED(pView->Draw(DVASPECT_CONTENT, -1, NULL, NULL, NULL, pDC->m_hDC, &rcl, NULL,
                              NULL, 0)))
      bDrew = TRUE;
    pView->Release();
  }
  return bDrew;
}

BOOL COleControlContainer::HandleWindowlessMessage(UINT message, WPARAM wParam, LPARAM lParam,
                                                   LRESULT* plResult) {
  if (m_nWindowlessControls <= 0 || m_pWnd->GetSafeHwnd() == NULL) return FALSE;
  COleControlSite* pSite = NULL;
  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    if (message == WM_MOUSEWHEEL) {
      pSite = m_pSiteFocus;  // the wheel goes with the keyboard
    } else {
      pSite = m_pSiteCapture;
      if (pSite == NULL)
        pSite = FindWindowlessSite(CPoint((short)LOWORD(lParam), (short)HIWORD(lParam)));
    }
  } else if (message == WM_SETCURSOR) {
    if ((HWND)wParam != m_pWnd->m_hWnd || LOWORD(lParam) != HTCLIENT) return FALSE;
    pSite = m_pSiteCapture;
    if (pSite == NULL) {
      CPoint point;
      ::GetCursorPos(&point);
      ::ScreenToClient(m_pWnd->m_hWnd, &point);
      pSite = FindWindowlessSite(point);
    }
  } else if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
             (message >= WM_IME_SETCONTEXT && message <= WM_IME_KEYUP) ||
             message == WM_HELP || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    pSite = m_pSiteFocus;
  } else if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED) {
    pSite = m_pSiteCapture;
    // The window lost the mouse, so the control did.
    if (message == WM_CAPTURECHANGED && (HWND)lParam != m_pWnd->m_hWnd) m_pSiteCapture = NULL;
  }
  if (pSite == NULL || !pSite->m_bIsWindowless) return FALSE;
  LRESULT lResult = 0;
  if (pSite->SendWindowlessMessage(message, wParam, lParam, &lResult) != S_OK) return FALSE;
  *plResult = lResult;
  return TRUE;
}

void COleControlContainer::DestroyAll() {
  while (!m_listSitesOrWnds.IsEmpty()) {
    COleControlSite* pSite = (COleControlSite*)m_listSitesOrWnds.GetHead();
    pSite->DestroyControl();  // takes itself off the list
  }
  m_siteMap.RemoveAll();
}

BOOL COleControlContainer::CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                         DWORD dwStyle, const RECT& rect, UINT nID,
                                         CFile* pPersist, BOOL bStorage, BSTR bstrLicKey,
                                         COleControlSite** ppNewSite) {
  POINT pt = {rect.left, rect.top};
  SIZE size = {rect.right - rect.left, rect.bottom - rect.top};
  return CreateControl(pWndCtrl, clsid, lpszWindowName, dwStyle, &pt, &size, nID, pPersist,
                       bStorage, bstrLicKey, ppNewSite);
}

BOOL COleControlContainer::CreateControl(CWnd* pWndCtrl, REFCLSID clsid, LPCTSTR lpszWindowName,
                                         DWORD dwStyle, const POINT* ppt, const SIZE* psize,
                                         UINT nID, CFile* pPersist, BOOL bStorage,
                                         BSTR bstrLicKey, COleControlSite** ppNewSite) {
  if (ppNewSite != NULL) *ppNewSite = NULL;
  COleControlSite* pSite = NULL;
  if (m_pWnd != NULL) m_pWnd->CreateControlSite(this, &pSite, nID, clsid);
  if (pSite == NULL) pSite = _AfxGetOccManager()->CreateSite(this);
  if (pSite == NULL) return FALSE;
  m_listSitesOrWnds.AddTail(pSite);
  HRESULT hr = pSite->CreateControl(pWndCtrl, clsid, lpszWindowName, dwStyle, ppt, psize, nID,
                                    pPersist, bStorage, bstrLicKey);
  if (FAILED(hr)) {
    POSITION pos = m_listSitesOrWnds.Find(pSite);
    if (pos != NULL) m_listSitesOrWnds.RemoveAt(pos);
    pSite->m_pWndCtrl = NULL;
    pSite->InternalRelease();
    TRACE(_T("Warning: could not create an ActiveX control (%08lx).\n"), (unsigned long)hr);
    return FALSE;
  }
  if (ppNewSite != NULL) *ppNewSite = pSite;
  return TRUE;
}

COleControlSite* COleControlContainer::FindItem(UINT nID) const {
  for (POSITION pos = m_listSitesOrWnds.GetHeadPosition(); pos != NULL;) {
    COleControlSite* pSite = (COleControlSite*)m_listSitesOrWnds.GetNext(pos);
    if (pSite->m_nID == nID) return pSite;
  }
  return NULL;
}

void COleControlContainer::AttachControlSite(CWnd* pWnd, UINT nIDC) {
  if (nIDC == 0 && pWnd->m_hWnd != NULL) nIDC = (UINT)::GetDlgCtrlID(pWnd->m_hWnd);
  COleControlSite* pSite = FindItem(nIDC);
  if (pSite == NULL) return;
  if (pSite->m_pWndCtrl != NULL && pSite->m_pWndCtrl != pWnd) pSite->DetachWindow();
  pSite->m_pWndCtrl = pWnd;
  if (pSite->m_hWnd != NULL)
    pSite->AttachWindow();
  else
    pWnd->m_pCtrlSite = pSite;
}

BOOL COleControlContainer::GetAmbientProp(COleControlSite* pSite, DISPID dispid,
                                          VARIANT* pvarResult) {
  switch (dispid) {
    case DISPID_AMBIENT_USERMODE:
    case DISPID_AMBIENT_SUPPORTSMNEMONICS:
    case DISPID_AMBIENT_AUTOCLIP:
      pvarResult->vt = VT_BOOL;
      pvarResult->boolVal = VARIANT_TRUE;
      return TRUE;
    case DISPID_AMBIENT_UIDEAD:
    case DISPID_AMBIENT_SHOWGRABHANDLES:
    case DISPID_AMBIENT_SHOWHATCHING:
    case DISPID_AMBIENT_MESSAGEREFLECT:
      pvarResult->vt = VT_BOOL;
      pvarResult->boolVal = VARIANT_FALSE;
      return TRUE;
    case DISPID_AMBIENT_DISPLAYASDEFAULT:
      pvarResult->vt = VT_BOOL;
      pvarResult->boolVal = pSite != NULL && pSite->IsDefaultButton() ? VARIANT_TRUE : VARIANT_FALSE;
      return TRUE;
    case DISPID_AMBIENT_BACKCOLOR:
      pvarResult->vt = VT_I4;
      pvarResult->lVal = (LONG)m_crBack;
      return TRUE;
    case DISPID_AMBIENT_FORECOLOR:
      pvarResult->vt = VT_I4;
      pvarResult->lVal = (LONG)m_crFore;
      return TRUE;
    case DISPID_AMBIENT_FONT:
      if (m_pOleFont == NULL) CreateOleFont(m_pWnd->GetFont());
      if (m_pOleFont == NULL) return FALSE;
      m_pOleFont->AddRef();
      pvarResult->vt = VT_DISPATCH;
      pvarResult->pdispVal = m_pOleFont;
      return TRUE;
    case DISPID_AMBIENT_DISPLAYNAME:
      pvarResult->vt = VT_BSTR;
      pvarResult->bstrVal = ::SysAllocString(L"");
      return TRUE;
    case DISPID_AMBIENT_LOCALEID:
      pvarResult->vt = VT_I4;
      pvarResult->lVal = (LONG)::GetThreadLocale();
      return TRUE;
    case DISPID_AMBIENT_APPEARANCE:
      pvarResult->vt = VT_I2;
      pvarResult->iVal = 1;  // 3D
      return TRUE;
  }
  return FALSE;
}

void COleControlContainer::BroadcastAmbientPropertyChange(DISPID dispid) {
  for (POSITION pos = m_listSitesOrWnds.GetHeadPosition(); pos != NULL;) {
    COleControlSite* pSite = (COleControlSite*)m_listSitesOrWnds.GetNext(pos);
    IOleControl* pCtl = NULL;
    if (pSite->m_pObject != NULL &&
        SUCCEEDED(pSite->m_pObject->QueryInterface(IID_IOleControl, (void**)&pCtl))) {
      pCtl->OnAmbientPropertyChange(dispid);
      pCtl->Release();
    }
  }
}

void COleControlContainer::FreezeAllEvents(BOOL bFreeze) {
  for (POSITION pos = m_listSitesOrWnds.GetHeadPosition(); pos != NULL;)
    ((COleControlSite*)m_listSitesOrWnds.GetNext(pos))->FreezeEvents(bFreeze);
}

void COleControlContainer::OnUIActivate(COleControlSite* pSite) {
  COleControlSite* pOld = m_pSiteUIActive;
  m_pSiteUIActive = pSite;
  if (pOld != NULL && pOld != pSite && pOld->m_pInPlaceObject != NULL)
    pOld->m_pInPlaceObject->UIDeactivate();
}

void COleControlContainer::OnUIDeactivate(COleControlSite* pSite) {
  if (m_pSiteUIActive == pSite) m_pSiteUIActive = NULL;
}

void COleControlContainer::CreateOleFont(CFont* pFont) {
  if (m_pOleFont != NULL) {
    m_pOleFont->Release();
    m_pOleFont = NULL;
  }
  LOGFONT lf;
  memset(&lf, 0, sizeof(lf));
  HFONT hFont = pFont != NULL ? (HFONT)pFont->GetSafeHandle() : NULL;
  if (hFont == NULL) hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
  ::GetObject(hFont, sizeof(lf), &lf);
  // The height in points, from the screen's resolution.
  HDC hDC = ::GetDC(NULL);
  int cyPerInch = ::GetDeviceCaps(hDC, LOGPIXELSY);
  ::ReleaseDC(NULL, hDC);
  long cyPoints = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
  CStringW strFace(lf.lfFaceName);
  FONTDESC fd;
  fd.cbSizeofstruct = sizeof(fd);
  fd.lpstrName = (LPOLESTR)(LPCWSTR)strFace;
  fd.cySize.int64 = (LONGLONG)::MulDiv(cyPoints, 72, cyPerInch) * 10000;
  fd.sWeight = (short)lf.lfWeight;
  fd.sCharset = lf.lfCharSet;
  fd.fItalic = lf.lfItalic != 0;
  fd.fUnderline = lf.lfUnderline != 0;
  fd.fStrikethrough = lf.lfStrikeOut != 0;
  ::OleCreateFontIndirect(&fd, IID_IFontDisp, (void**)&m_pOleFont);
}

// A control without a window object gets one, so that the program can
// reach its properties through it; the site deletes it with the control.
CWnd* COleControlContainer::GetDlgItem(int nID) const {
  COleControlSite* pSite = FindItem((UINT)nID);
  if (pSite != NULL && pSite->m_pWndCtrl != NULL) return pSite->m_pWndCtrl;
  if (pSite != NULL) {
    delete pSite->m_pWndOwned;
    pSite->m_pWndOwned = new CWnd;
    const_cast<COleControlContainer*>(this)->AttachControlSite(pSite->m_pWndOwned, (UINT)nID);
    return pSite->m_pWndOwned;
  }
  HWND hWnd = NULL;
  GetDlgItem(nID, &hWnd);
  return CWnd::FromHandle(hWnd);
}

void COleControlContainer::GetDlgItem(int nID, HWND* phWnd) const {
  COleControlSite* pSite = FindItem((UINT)nID);
  *phWnd = pSite != NULL ? pSite->m_hWnd : ::GetDlgItem(m_pWnd->GetSafeHwnd(), nID);
}

UINT COleControlContainer::GetDlgItemInt(int nID, BOOL* lpTrans, BOOL bSigned) const {
  COleControlSite* pSite = FindItem((UINT)nID);
  if (pSite == NULL) return ::GetDlgItemInt(m_pWnd->GetSafeHwnd(), nID, lpTrans, bSigned);
  CString str;
  pSite->GetWindowText(str);
  str.TrimLeft();
  LPTSTR pEnd = NULL;
  long n = _tcstol(str, &pEnd, 10);
  BOOL bOK = !str.IsEmpty() && pEnd != NULL && *pEnd == 0 && (bSigned || n >= 0);
  if (lpTrans != NULL) *lpTrans = bOK;
  return bOK ? (UINT)n : 0;
}

int COleControlContainer::GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const {
  COleControlSite* pSite = FindItem((UINT)nID);
  if (pSite == NULL) return ::GetDlgItemText(m_pWnd->GetSafeHwnd(), nID, lpStr, nMaxCount);
  CString str;
  pSite->GetWindowText(str);
  if (nMaxCount <= 0) return 0;
  lstrcpyn(lpStr, str, nMaxCount);
  return lstrlen(lpStr);
}

void COleControlContainer::SetDlgItemInt(int nID, UINT nValue, BOOL bSigned) {
  COleControlSite* pSite = FindItem((UINT)nID);
  if (pSite == NULL) {
    ::SetDlgItemInt(m_pWnd->GetSafeHwnd(), nID, nValue, bSigned);
    return;
  }
  CString str;
  if (bSigned)
    str.Format(_T("%d"), (int)nValue);
  else
    str.Format(_T("%u"), nValue);
  pSite->SetWindowText(str);
}

void COleControlContainer::SetDlgItemText(int nID, LPCTSTR lpszString) {
  COleControlSite* pSite = FindItem((UINT)nID);
  if (pSite == NULL)
    ::SetDlgItemText(m_pWnd->GetSafeHwnd(), nID, lpszString);
  else
    pSite->SetWindowText(lpszString);
}

// ---------------------------------------------------------------------
// IOleInPlaceFrame: a frame without menus or tool space.

#define CONT(part) METHOD_PROLOGUE(COleControlContainer, part)

STDMETHODIMP_(ULONG) COleControlContainer::XOleIPFrame::AddRef() {
  CONT(OleIPFrame);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlContainer::XOleIPFrame::Release() {
  CONT(OleIPFrame);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlContainer::XOleIPFrame::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  CONT(OleIPFrame);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlContainer::XOleIPFrame::GetWindow(HWND* phwnd) {
  CONT(OleIPFrame);
  *phwnd = pThis->m_pWnd->GetSafeHwnd();
  return S_OK;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }
STDMETHODIMP COleControlContainer::XOleIPFrame::GetBorder(LPRECT) {
  return INPLACE_E_NOTOOLSPACE;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::RequestBorderSpace(LPCBORDERWIDTHS) {
  return INPLACE_E_NOTOOLSPACE;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::SetBorderSpace(LPCBORDERWIDTHS pborderwidths) {
  return pborderwidths == NULL ? S_OK : INPLACE_E_NOTOOLSPACE;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::SetActiveObject(
    LPOLEINPLACEACTIVEOBJECT pActiveObject, LPCOLESTR) {
  CONT(OleIPFrame);
  if (pActiveObject != NULL) pActiveObject->AddRef();
  if (pThis->m_pActiveObject != NULL) pThis->m_pActiveObject->Release();
  pThis->m_pActiveObject = pActiveObject;
  return S_OK;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS lpWidths) {
  if (lpWidths != NULL) {
    lpWidths->width[0] = lpWidths->width[2] = lpWidths->width[4] = 0;
  }
  return S_OK;
}
STDMETHODIMP COleControlContainer::XOleIPFrame::SetMenu(HMENU, HOLEMENU, HWND) { return S_OK; }
STDMETHODIMP COleControlContainer::XOleIPFrame::RemoveMenus(HMENU) { return S_OK; }
STDMETHODIMP COleControlContainer::XOleIPFrame::SetStatusText(LPCOLESTR) { return S_OK; }
STDMETHODIMP COleControlContainer::XOleIPFrame::EnableModeless(BOOL) { return S_OK; }
STDMETHODIMP COleControlContainer::XOleIPFrame::TranslateAccelerator(LPMSG, WORD) {
  return S_FALSE;
}

STDMETHODIMP_(ULONG) COleControlContainer::XOleContainer::AddRef() {
  CONT(OleContainer);
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COleControlContainer::XOleContainer::Release() {
  CONT(OleContainer);
  return pThis->ExternalRelease();
}
STDMETHODIMP COleControlContainer::XOleContainer::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  CONT(OleContainer);
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP COleControlContainer::XOleContainer::ParseDisplayName(LPBINDCTX, LPOLESTR, ULONG*,
                                                                   LPMONIKER* ppmkOut) {
  *ppmkOut = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP COleControlContainer::XOleContainer::EnumObjects(DWORD, LPENUMUNKNOWN* ppenum) {
  *ppenum = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP COleControlContainer::XOleContainer::LockContainer(BOOL) { return S_OK; }
