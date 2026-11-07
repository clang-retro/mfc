// ctlintf.cpp - COleControl's OLE interfaces: IOleObject,
// IOleInPlaceObject, IOleInPlaceActiveObject, IOleControl, IViewObject2,
// IProvideClassInfo2 and ISpecifyPropertyPages. The persistence
// interfaces are in ctlprop.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxctl.h>
#include "ctlimpl.h"

// The primary dispatch interface is the control's IDispatch.
LPUNKNOWN COleControl::GetInterfaceHook(const void* piid) {
  if (m_piidPrimary != NULL && ::IsEqualIID(*(const IID*)piid, *m_piidPrimary))
    return GetIDispatch(FALSE);
  return NULL;
}

// ---------------------------------------------------------------------
// IOleObject

CTL_IUNKNOWN(OleObject)

STDMETHODIMP COleControl::XOleObject::SetClientSite(LPOLECLIENTSITE pClientSite) {
  METHOD_PROLOGUE(COleControl, OleObject);
  if (pClientSite != NULL) pClientSite->AddRef();
  if (pThis->m_pControlSite != NULL) {
    pThis->m_pControlSite->Release();
    pThis->m_pControlSite = NULL;
  }
  pThis->m_ambientDispDriver.ReleaseDispatch();
  if (pThis->m_pClientSite != NULL) pThis->m_pClientSite->Release();
  pThis->m_pClientSite = pClientSite;
  if (pClientSite != NULL) {
    if (FAILED(pClientSite->QueryInterface(IID_IOleControlSite, (void**)&pThis->m_pControlSite)))
      pThis->m_pControlSite = NULL;
    LPDISPATCH pDisp = NULL;
    if (SUCCEEDED(pClientSite->QueryInterface(IID_IDispatch, (void**)&pDisp)))
      pThis->m_ambientDispDriver.AttachDispatch(pDisp, TRUE);
  }
  pThis->OnSetClientSite();
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::GetClientSite(LPOLECLIENTSITE* ppClientSite) {
  METHOD_PROLOGUE(COleControl, OleObject);
  *ppClientSite = pThis->m_pClientSite;
  if (*ppClientSite != NULL) (*ppClientSite)->AddRef();
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::SetHostNames(LPCOLESTR, LPCOLESTR) { return S_OK; }

STDMETHODIMP COleControl::XOleObject::Close(DWORD dwSaveOption) {
  METHOD_PROLOGUE(COleControl, OleObject);
  pThis->ExternalAddRef();
  pThis->OnClose(dwSaveOption);
  pThis->ExternalRelease();
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::SetMoniker(DWORD, LPMONIKER) { return E_NOTIMPL; }

STDMETHODIMP COleControl::XOleObject::GetMoniker(DWORD, DWORD, LPMONIKER* ppmk) {
  *ppmk = NULL;
  return E_NOTIMPL;
}

STDMETHODIMP COleControl::XOleObject::InitFromData(LPDATAOBJECT, BOOL, DWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP COleControl::XOleObject::GetClipboardData(DWORD, LPDATAOBJECT* ppDataObject) {
  *ppDataObject = NULL;
  return E_NOTIMPL;
}

STDMETHODIMP COleControl::XOleObject::DoVerb(LONG iVerb, LPMSG lpmsg, LPOLECLIENTSITE pActiveSite,
                                            LONG, HWND hwndParent, LPCRECT lprcPosRect) {
  METHOD_PROLOGUE(COleControl, OleObject);
  if (pThis->m_pClientSite == NULL && pActiveSite != NULL) SetClientSite(pActiveSite);
  switch (iVerb) {
    case OLEIVERB_PRIMARY:
    case OLEIVERB_SHOW:
    case OLEIVERB_OPEN:
    case OLEIVERB_UIACTIVATE:
    case OLEIVERB_INPLACEACTIVATE:
    case OLEIVERB_HIDE:
    case OLEIVERB_PROPERTIES:
      break;
    default:
      if (iVerb < 0) return E_NOTIMPL;
      if (!pThis->OnDoVerb(iVerb, lpmsg, hwndParent, lprcPosRect)) return OLEOBJ_S_INVALIDVERB;
      return S_OK;
  }
  pThis->ExternalAddRef();
  BOOL bOK = pThis->OnDoVerb(iVerb, lpmsg, hwndParent, lprcPosRect);
  pThis->ExternalRelease();
  if (bOK) return S_OK;
  return iVerb == OLEIVERB_PROPERTIES ? E_NOTIMPL : E_FAIL;
}

STDMETHODIMP COleControl::XOleObject::EnumVerbs(LPENUMOLEVERB* ppEnumOleVerb) {
  METHOD_PROLOGUE(COleControl, OleObject);
  CLSID clsid;
  pThis->GetClassID(&clsid);
  return ::OleRegEnumVerbs(clsid, ppEnumOleVerb);
}

STDMETHODIMP COleControl::XOleObject::Update() { return S_OK; }
STDMETHODIMP COleControl::XOleObject::IsUpToDate() { return S_OK; }

STDMETHODIMP COleControl::XOleObject::GetUserClassID(CLSID* pClsid) {
  METHOD_PROLOGUE(COleControl, OleObject);
  return pThis->GetClassID(pClsid);
}

STDMETHODIMP COleControl::XOleObject::GetUserType(DWORD dwFormOfType, LPOLESTR* pszUserType) {
  METHOD_PROLOGUE(COleControl, OleObject);
  *pszUserType = NULL;
  CLSID clsid;
  pThis->GetClassID(&clsid);
  if (SUCCEEDED(::OleRegGetUserType(clsid, dwFormOfType, pszUserType))) return S_OK;
  // The name from the control's resources.
  CString str;
  UINT nID = pThis->GetUserTypeNameID();
  if (nID == 0 || !str.LoadString(nID)) return OLE_S_USEREG;
  CStringW strW(str);
  size_t cb = (size_t)(strW.GetLength() + 1) * sizeof(WCHAR);
  *pszUserType = (LPOLESTR)::CoTaskMemAlloc(cb);
  if (*pszUserType == NULL) return E_OUTOFMEMORY;
  memcpy(*pszUserType, (LPCWSTR)strW, cb);
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::SetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE(COleControl, OleObject);
  if (dwDrawAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  if (psizel == NULL) return E_POINTER;
  if (!pThis->OnSetExtent(psizel)) return E_FAIL;
  if (pThis->m_cxExtent != psizel->cx || pThis->m_cyExtent != psizel->cy) {
    pThis->m_cxExtent = psizel->cx;
    pThis->m_cyExtent = psizel->cy;
    pThis->SetModifiedFlag();
  }
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::GetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE(COleControl, OleObject);
  if (dwDrawAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  psizel->cx = pThis->m_cxExtent;
  psizel->cy = pThis->m_cyExtent;
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::Advise(LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE(COleControl, OleObject);
  *pdwConnection = 0;
  if (pThis->m_pOleAdviseHolder == NULL &&
      FAILED(::CreateOleAdviseHolder(&pThis->m_pOleAdviseHolder)))
    return E_OUTOFMEMORY;
  return pThis->m_pOleAdviseHolder->Advise(pAdvSink, pdwConnection);
}

STDMETHODIMP COleControl::XOleObject::Unadvise(DWORD dwConnection) {
  METHOD_PROLOGUE(COleControl, OleObject);
  if (pThis->m_pOleAdviseHolder == NULL) return OLE_E_NOCONNECTION;
  return pThis->m_pOleAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP COleControl::XOleObject::EnumAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE(COleControl, OleObject);
  *ppenumAdvise = NULL;
  if (pThis->m_pOleAdviseHolder == NULL) return S_OK;
  return pThis->m_pOleAdviseHolder->EnumAdvise(ppenumAdvise);
}

STDMETHODIMP COleControl::XOleObject::GetMiscStatus(DWORD, DWORD* pdwStatus) {
  METHOD_PROLOGUE(COleControl, OleObject);
  *pdwStatus = pThis->GetMiscStatus();
  return S_OK;
}

STDMETHODIMP COleControl::XOleObject::SetColorScheme(LPLOGPALETTE) { return E_NOTIMPL; }

// ---------------------------------------------------------------------
// IOleInPlaceObject

CTL_IUNKNOWN(OleInPlaceObject)

STDMETHODIMP COleControl::XOleInPlaceObject::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE(COleControl, OleInPlaceObject);
  *phwnd = pThis->GetOuterWindow()->m_hWnd;
  return *phwnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleControl::XOleInPlaceObject::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }

STDMETHODIMP COleControl::XOleInPlaceObject::InPlaceDeactivate() {
  METHOD_PROLOGUE(COleControl, OleInPlaceObject);
  pThis->ExternalAddRef();
  pThis->InPlaceDeactivate();
  pThis->ExternalRelease();
  return S_OK;
}

STDMETHODIMP COleControl::XOleInPlaceObject::UIDeactivate() {
  METHOD_PROLOGUE(COleControl, OleInPlaceObject);
  pThis->UIDeactivate();
  return S_OK;
}

STDMETHODIMP COleControl::XOleInPlaceObject::SetObjectRects(LPCRECT lprcPosRect,
                                                           LPCRECT lprcClipRect) {
  METHOD_PROLOGUE(COleControl, OleInPlaceObject);
  if (lprcPosRect == NULL) return E_POINTER;
  return pThis->OnSetObjectRects(lprcPosRect, lprcClipRect) ? S_OK : E_FAIL;
}

STDMETHODIMP COleControl::XOleInPlaceObject::ReactivateAndUndo() { return E_NOTIMPL; }

// ---------------------------------------------------------------------
// IOleInPlaceActiveObject

CTL_IUNKNOWN(OleInPlaceActiveObject)

STDMETHODIMP COleControl::XOleInPlaceActiveObject::GetWindow(HWND* phwnd) {
  METHOD_PROLOGUE(COleControl, OleInPlaceActiveObject);
  *phwnd = pThis->GetOuterWindow()->m_hWnd;
  return *phwnd != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleControl::XOleInPlaceActiveObject::ContextSensitiveHelp(BOOL) {
  return E_NOTIMPL;
}

// The control's PreTranslateMessage sees its keys first, then the
// container's control site.
STDMETHODIMP COleControl::XOleInPlaceActiveObject::TranslateAccelerator(LPMSG lpmsg) {
  METHOD_PROLOGUE(COleControl, OleInPlaceActiveObject);
  if (lpmsg == NULL) return E_POINTER;
  if (pThis->m_hWnd != NULL &&
      (lpmsg->hwnd == pThis->m_hWnd || ::IsChild(pThis->m_hWnd, lpmsg->hwnd)) &&
      pThis->PreTranslateMessage(lpmsg))
    return S_OK;
  if (pThis->m_pControlSite != NULL) {
    DWORD dwModifiers = 0;
    if (::GetKeyState(VK_SHIFT) < 0) dwModifiers |= 1;
    if (::GetKeyState(VK_CONTROL) < 0) dwModifiers |= 2;
    if (::GetKeyState(VK_MENU) < 0) dwModifiers |= 4;
    return pThis->m_pControlSite->TranslateAccelerator(lpmsg, dwModifiers);
  }
  return S_FALSE;
}

STDMETHODIMP COleControl::XOleInPlaceActiveObject::OnFrameWindowActivate(BOOL) { return S_OK; }
STDMETHODIMP COleControl::XOleInPlaceActiveObject::OnDocWindowActivate(BOOL) { return S_OK; }
STDMETHODIMP COleControl::XOleInPlaceActiveObject::ResizeBorder(LPCRECT, LPOLEINPLACEUIWINDOW,
                                                               BOOL) {
  return S_OK;
}
STDMETHODIMP COleControl::XOleInPlaceActiveObject::EnableModeless(BOOL) { return S_OK; }

// ---------------------------------------------------------------------
// IOleControl

CTL_IUNKNOWN(OleControl)

STDMETHODIMP COleControl::XOleControl::GetControlInfo(LPCONTROLINFO pCI) {
  METHOD_PROLOGUE(COleControl, OleControl);
  if (pCI == NULL) return E_POINTER;
  pThis->OnGetControlInfo(pCI);
  return S_OK;
}

STDMETHODIMP COleControl::XOleControl::OnMnemonic(LPMSG pMsg) {
  METHOD_PROLOGUE(COleControl, OleControl);
  pThis->OnMnemonic(pMsg);
  return S_OK;
}

STDMETHODIMP COleControl::XOleControl::OnAmbientPropertyChange(DISPID dispid) {
  METHOD_PROLOGUE(COleControl, OleControl);
  pThis->OnAmbientPropertyChange(dispid);
  return S_OK;
}

STDMETHODIMP COleControl::XOleControl::FreezeEvents(BOOL bFreeze) {
  METHOD_PROLOGUE(COleControl, OleControl);
  if (bFreeze)
    pThis->m_nFreezeEvents++;
  else if (pThis->m_nFreezeEvents > 0)
    pThis->m_nFreezeEvents--;
  pThis->OnFreezeEvents(bFreeze);
  return S_OK;
}

// ---------------------------------------------------------------------
// IViewObject2: drawing into the container's DC

CTL_IUNKNOWN(ViewObject)

STDMETHODIMP COleControl::XViewObject::Draw(DWORD dwDrawAspect, LONG, void*, DVTARGETDEVICE*,
                                           HDC, HDC hdcDraw, LPCRECTL lprcBounds, LPCRECTL,
                                           BOOL(STDMETHODCALLTYPE*)(ULONG_PTR), ULONG_PTR) {
  METHOD_PROLOGUE(COleControl, ViewObject);
  if (dwDrawAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  if (hdcDraw == NULL) return E_INVALIDARG;
  CRect rcBounds;
  if (lprcBounds != NULL)
    rcBounds.SetRect(lprcBounds->left, lprcBounds->top, lprcBounds->right, lprcBounds->bottom);
  else if (pThis->m_bInPlaceActive)
    rcBounds = pThis->m_rcPos;
  else
    return E_INVALIDARG;
  CDC* pDC = CDC::FromHandle(hdcDraw);
  int nSaved = ::SaveDC(hdcDraw);
  DWORD dwType = ::GetObjectType(hdcDraw);
  if (dwType == OBJ_METADC || dwType == OBJ_ENHMETADC)
    pThis->OnDrawMetafile(pDC, rcBounds);
  else
    pThis->OnDraw(pDC, rcBounds, rcBounds);
  ::RestoreDC(hdcDraw, nSaved);
  return S_OK;
}

STDMETHODIMP COleControl::XViewObject::GetColorSet(DWORD, LONG, void*, DVTARGETDEVICE*, HDC,
                                                  LOGPALETTE** ppColorSet) {
  *ppColorSet = NULL;
  return S_FALSE;
}

STDMETHODIMP COleControl::XViewObject::Freeze(DWORD, LONG, void*, DWORD* pdwFreeze) {
  if (pdwFreeze != NULL) *pdwFreeze = 0;
  return E_NOTIMPL;
}

STDMETHODIMP COleControl::XViewObject::Unfreeze(DWORD) { return E_NOTIMPL; }

STDMETHODIMP COleControl::XViewObject::SetAdvise(DWORD aspects, DWORD advf,
                                                LPADVISESINK pAdvSink) {
  METHOD_PROLOGUE(COleControl, ViewObject);
  if (pAdvSink != NULL) pAdvSink->AddRef();
  if (pThis->m_pAdviseSink != NULL) pThis->m_pAdviseSink->Release();
  pThis->m_pAdviseSink = pAdvSink;
  pThis->m_dwAdviseFlags = advf;
  (void)aspects;
  if (pAdvSink != NULL && (advf & ADVF_PRIMEFIRST)) pAdvSink->OnViewChange(DVASPECT_CONTENT, -1);
  return S_OK;
}

STDMETHODIMP COleControl::XViewObject::GetAdvise(DWORD* pAspects, DWORD* pAdvf,
                                                LPADVISESINK* ppAdvSink) {
  METHOD_PROLOGUE(COleControl, ViewObject);
  if (pAspects != NULL) *pAspects = DVASPECT_CONTENT;
  if (pAdvf != NULL) *pAdvf = pThis->m_dwAdviseFlags;
  if (ppAdvSink != NULL) {
    *ppAdvSink = pThis->m_pAdviseSink;
    if (*ppAdvSink != NULL) (*ppAdvSink)->AddRef();
  }
  return S_OK;
}

STDMETHODIMP COleControl::XViewObject::GetExtent(DWORD dwDrawAspect, LONG, DVTARGETDEVICE*,
                                                LPSIZEL lpsizel) {
  METHOD_PROLOGUE(COleControl, ViewObject);
  if (dwDrawAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
  lpsizel->cx = pThis->m_cxExtent;
  lpsizel->cy = pThis->m_cyExtent;
  return S_OK;
}

// ---------------------------------------------------------------------
// IProvideClassInfo2

CTL_IUNKNOWN(ProvideClassInfo)

STDMETHODIMP COleControl::XProvideClassInfo::GetClassInfo(LPTYPEINFO* ppTypeInfo) {
  METHOD_PROLOGUE(COleControl, ProvideClassInfo);
  *ppTypeInfo = NULL;
  CLSID clsid;
  pThis->GetClassID(&clsid);
  return pThis->GetTypeInfoOfGuid(::GetUserDefaultLCID(), clsid, ppTypeInfo);
}

STDMETHODIMP COleControl::XProvideClassInfo::GetGUID(DWORD dwGuidKind, GUID* pGUID) {
  METHOD_PROLOGUE(COleControl, ProvideClassInfo);
  if (pGUID == NULL) return E_POINTER;
  if (dwGuidKind != GUIDKIND_DEFAULT_SOURCE_DISP_IID || pThis->m_piidEvents == NULL) {
    *pGUID = GUID_NULL;
    return E_INVALIDARG;
  }
  *pGUID = *pThis->m_piidEvents;
  return S_OK;
}

// ---------------------------------------------------------------------
// ISpecifyPropertyPages

CTL_IUNKNOWN(SpecifyPropertyPages)

STDMETHODIMP COleControl::XSpecifyPropertyPages::GetPages(CAUUID* pPages) {
  METHOD_PROLOGUE(COleControl, SpecifyPropertyPages);
  if (pPages == NULL) return E_POINTER;
  ULONG cPages = 0;
  LPCLSID pIDs = pThis->GetPropPageIDs(cPages);
  pPages->cElems = 0;
  pPages->pElems = NULL;
  if (cPages == 0 || pIDs == NULL) return S_OK;
  pPages->pElems = (GUID*)::CoTaskMemAlloc(cPages * sizeof(GUID));
  if (pPages->pElems == NULL) return E_OUTOFMEMORY;
  memcpy(pPages->pElems, pIDs, cPages * sizeof(GUID));
  pPages->cElems = cPages;
  return S_OK;
}

// ---------------------------------------------------------------------
// The font's notifications. The font's connection does not hold the
// control, so these do not count references.

STDMETHODIMP_(ULONG) COleControl::XFontNotification::AddRef() { return 1; }
STDMETHODIMP_(ULONG) COleControl::XFontNotification::Release() { return 1; }

STDMETHODIMP COleControl::XFontNotification::QueryInterface(REFIID iid, LPVOID* ppv) {
  if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IPropertyNotifySink)) {
    *ppv = this;
    return S_OK;
  }
  *ppv = NULL;
  return E_NOINTERFACE;
}

STDMETHODIMP COleControl::XFontNotification::OnChanged(DISPID) {
  METHOD_PROLOGUE(COleControl, FontNotification);
  pThis->SetModifiedFlag();
  pThis->OnFontChanged();
  pThis->BoundPropertyChanged(DISPID_FONT);
  return S_OK;
}

STDMETHODIMP COleControl::XFontNotification::OnRequestEdit(DISPID) { return S_OK; }
