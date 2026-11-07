// occsite.cpp - ActiveX control containment: COleControlSite (one control:
// its creation, activation in place, properties, events and ambient
// properties) and COleControlContainer (the controls of one window).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxocc.h>
#include <olectl.h>

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
  INTERFACE_PART(COleControlSite, IID_IOleControlSite, OleControlSite)
  INTERFACE_PART(COleControlSite, IID_IDispatch, AmbientProps)
  INTERFACE_PART(COleControlSite, IID_IPropertyNotifySink, PropertyNotifySink)
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
      m_nFreezeEvents(0) {
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
  GetControlInfo();

  // Active in place, with a window, unless it is invisible at run time.
  HWND hWndParent = m_pCtrlCont->m_pWnd->GetSafeHwnd();
  hr = m_pObject->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, &m_xOleClientSite, 0, hWndParent,
                         &m_rect);
  if (FAILED(hr) && !(m_dwMiscStatus & OLEMISC_INVISIBLEATRUNTIME)) {
    DisconnectSink(IID_IPropertyNotifySink, m_dwPropNotifySink);
    DisconnectSink(m_iidEvents, m_dwEventSink);
    m_dwEventSink = m_dwPropNotifySink = 0;
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
  DetachWindow();
  if (m_hWnd != NULL) m_pCtrlCont->m_siteMap.RemoveKey(m_hWnd);
  if (m_pCtrlCont->m_pSiteUIActive == this) m_pCtrlCont->m_pSiteUIActive = NULL;
  if (m_pCtrlCont->m_pSiteFocus == this) m_pCtrlCont->m_pSiteFocus = NULL;
  if (m_pObject != NULL) {
    DisconnectSink(IID_IPropertyNotifySink, m_dwPropNotifySink);
    DisconnectSink(m_iidEvents, m_dwEventSink);
    m_dwEventSink = m_dwPropNotifySink = 0;
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
  m_rect.SetRect(x, y, x + nWidth, y + nHeight);
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
  if (nCmdShow == SW_HIDE)
    m_dwStyle &= ~WS_VISIBLE;
  else
    m_dwStyle |= WS_VISIBLE;
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
  pThis->m_bInPlaceActive = TRUE;
  if (pThis->m_pInPlaceObject == NULL)
    pThis->m_pObject->QueryInterface(IID_IOleInPlaceObject, (void**)&pThis->m_pInPlaceObject);
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
  pThis->m_bInPlaceActive = FALSE;
  if (pThis->m_pInPlaceObject != NULL) {
    pThis->m_pInPlaceObject->Release();
    pThis->m_pInPlaceObject = NULL;
  }
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
  pThis->m_rect = *lprcPosRect;
  if (pThis->m_pInPlaceObject != NULL)
    pThis->m_pInPlaceObject->SetObjectRects(&pThis->m_rect, &pThis->m_rect);
  return S_OK;
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
  *ppDisp = NULL;
  return E_NOTIMPL;
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
  AFX_EVENT event(AFX_EVENT::propChanged, dispid);
  pThis->FireEvent(&event);
  return S_OK;
}
STDMETHODIMP COleControlSite::XPropertyNotifySink::OnRequestEdit(DISPID dispid) {
  SITE(PropertyNotifySink);
  AFX_EVENT event(AFX_EVENT::propRequest, dispid);
  return pThis->FireEvent(&event) == S_FALSE ? S_FALSE : S_OK;
}

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
      m_pActiveObject(NULL) {}

COleControlContainer::~COleControlContainer() {
  DestroyAll();
  if (m_pOleFont != NULL) m_pOleFont->Release();
  if (m_pActiveObject != NULL) m_pActiveObject->Release();
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
