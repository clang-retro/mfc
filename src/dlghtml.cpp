// dlghtml.cpp - CDHtmlDialog, a dialog whose face is an HTML page shown by
// the WebBrowser control; CDHtmlEventSink, which connects a DHTML event
// map to the page's elements; and CDHtmlControlSite, the control's site
// that answers the browser's IDocHostUIHandler questions through the
// dialog.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"
#include "afxhtml.h"

// Not in every import library.
static const IID kDIID_HTMLElementEvents2 = {
    0x3050f60f, 0x98b5, 0x11cf, {0xbb, 0x82, 0x00, 0xaa, 0x00, 0xbd, 0xce, 0x0b}};
static const IID kDIID_HTMLDocumentEvents2 = {
    0x3050f613, 0x98b5, 0x11cf, {0xbb, 0x82, 0x00, 0xaa, 0x00, 0xbd, 0xce, 0x0b}};

static CString StringFromBSTR(BSTR bstr) {
  CString str;
  if (bstr != NULL) str = CStringW(bstr, (int)::SysStringLen(bstr));
  return str;
}

static CString StringFromVariant(const VARIANT* pVar) {
  CString str;
  if (pVar == NULL) return str;
  if (pVar->vt == (VT_BYREF | VT_VARIANT)) pVar = pVar->pvarVal;
  VARIANT var;
  ::VariantInit(&var);
  if (SUCCEEDED(::VariantChangeType(&var, const_cast<VARIANT*>(pVar), 0, VT_BSTR)))
    str = StringFromBSTR(var.bstrVal);
  ::VariantClear(&var);
  return str;
}

static VARIANT StringVariant(LPCTSTR lpsz) {
  VARIANT var;
  ::VariantInit(&var);
  var.vt = VT_BSTR;
  var.bstrVal = ::SysAllocString(CStringW(lpsz != NULL ? lpsz : _T("")));
  return var;
}

// A property of an object, through its IDispatch.
static HRESULT GetDispProperty(IDispatch* pDisp, DISPID dispid, VARIANT* pVar) {
  ::VariantInit(pVar);
  if (pDisp == NULL) return E_POINTER;
  DISPPARAMS dp = {NULL, NULL, 0, 0};
  return pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dp, pVar,
                       NULL, NULL);
}

static HRESULT PutDispProperty(IDispatch* pDisp, DISPID dispid, VARIANT* pVar) {
  if (pDisp == NULL || pVar == NULL) return E_POINTER;
  DISPID dispidPut = DISPID_PROPERTYPUT;
  DISPPARAMS dp = {pVar, &dispidPut, 1, 1};
  WORD wFlags = (pVar->vt == VT_DISPATCH || pVar->vt == VT_UNKNOWN) ? DISPATCH_PROPERTYPUTREF
                                                                     : DISPATCH_PROPERTYPUT;
  HRESULT hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, wFlags, &dp, NULL, NULL, NULL);
  if (FAILED(hr) && wFlags == DISPATCH_PROPERTYPUTREF)
    hr = pDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dp, NULL,
                       NULL, NULL);
  return hr;
}

static DISPID DispidOfName(IDispatch* pDisp, LPCTSTR lpszName) {
  DISPID dispid = DISPID_UNKNOWN;
  if (pDisp == NULL || lpszName == NULL) return dispid;
  CStringW strName(lpszName);
  LPOLESTR psz = strName.GetBuffer();
  if (FAILED(pDisp->GetIDsOfNames(IID_NULL, &psz, 1, LOCALE_USER_DEFAULT, &dispid)))
    dispid = DISPID_UNKNOWN;
  strName.ReleaseBuffer();
  return dispid;
}

// Two interface pointers on one object.
static BOOL SameObject(IUnknown* p1, IUnknown* p2) {
  if (p1 == NULL || p2 == NULL) return p1 == p2;
  IUnknown* pUnk1 = NULL;
  IUnknown* pUnk2 = NULL;
  p1->QueryInterface(IID_IUnknown, (void**)&pUnk1);
  p2->QueryInterface(IID_IUnknown, (void**)&pUnk2);
  BOOL b = pUnk1 != NULL && pUnk1 == pUnk2;
  if (pUnk1 != NULL) pUnk1->Release();
  if (pUnk2 != NULL) pUnk2->Release();
  return b;
}

// ---------------------------------------------------------------------
// CDHtmlSinkHandler: the events of one element (or the document), for
// the sink that connected it.

class CDHtmlSinkHandler : public IDispatch {
public:
  LONG m_nRef;
  CDHtmlEventSink* m_pOwner;
  IDispatch* m_pElement;
  IID m_iid;
  BOOL m_bDocument;
  IConnectionPoint* m_pPoint;
  DWORD m_dwCookie;
  CString m_strControl;  // for an ActiveX control's events: its <object>'s id

  CDHtmlSinkHandler(CDHtmlEventSink* pOwner, IDispatch* pElement, REFIID iid, BOOL bDocument,
                    LPCTSTR lpszControl = NULL)
      : m_nRef(1),
        m_pOwner(pOwner),
        m_pElement(pElement),
        m_iid(iid),
        m_bDocument(bDocument),
        m_pPoint(NULL),
        m_dwCookie(0),
        m_strControl(lpszControl) {
    m_pElement->AddRef();
  }
  virtual ~CDHtmlSinkHandler() {
    if (m_pElement != NULL) m_pElement->Release();
  }

  HRESULT Connect() {
    IConnectionPointContainer* pCPC = NULL;
    HRESULT hr = m_pElement->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
    if (FAILED(hr)) return hr;
    hr = pCPC->FindConnectionPoint(m_iid, &m_pPoint);
    pCPC->Release();
    if (FAILED(hr)) {
      m_pPoint = NULL;
      return hr;
    }
    hr = m_pPoint->Advise(this, &m_dwCookie);
    if (FAILED(hr)) {
      m_pPoint->Release();
      m_pPoint = NULL;
    }
    return hr;
  }
  void Disconnect() {
    m_pOwner = NULL;
    if (m_pPoint != NULL) {
      m_pPoint->Unadvise(m_dwCookie);
      m_pPoint->Release();
      m_pPoint = NULL;
    }
  }

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == m_iid) {
      *ppv = static_cast<IDispatch*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) {
    if (pctinfo != NULL) *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** ppTInfo) {
    if (ppTInfo != NULL) *ppTInfo = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, WORD, DISPPARAMS* pDispParams,
                    VARIANT* pVarResult, EXCEPINFO*, UINT*) {
    if (m_pOwner == NULL) return S_OK;
    AddRef();  // a handler may disconnect us
    HRESULT hr = S_OK;
    m_pOwner->FireFor(this, dispid, pDispParams, pVarResult, &hr);
    Release();
    return hr;
  }
};

// ---------------------------------------------------------------------
// CDHtmlEventSink

CDHtmlEventSink::CDHtmlEventSink() : m_pCurrentEvent(NULL), m_pPageMap(NULL) {}

int CDHtmlEventSink::GetMaps(const DHtmlEventMapEntry* apMaps[2]) {
  int n = 0;
  const DHtmlEventMapEntry* pMap = GetDHtmlEventMap();
  if (pMap != NULL) apMaps[n++] = pMap;
  if (m_pPageMap != NULL && m_pPageMap != pMap) apMaps[n++] = m_pPageMap;
  return n;
}

// A DHTML_EVENT_AXCONTROL argument: its value, and the VARIANT itself
// for a VARIANT* parameter.
void AFXAPI _AfxDHtmlAxArg(AFX_DISPARG& a, VARIANT* pSrc) {
  a.pVar = pSrc->vt == (VT_BYREF | VT_VARIANT) ? pSrc->pvarVal : pSrc;
  ::VariantCopyInd(&a.v, pSrc);
  if (a.v.vt == VT_BOOL) a.bVal = a.v.boolVal != VARIANT_FALSE;
}

CDHtmlEventSink::~CDHtmlEventSink() {
  DisconnectDHtmlEvents();
  DisconnectDHtmlElementEvents();
}

// The element's id, class names or tag against an entry's name.
static BOOL ElementMatches(IHTMLElement* pElem, const DHtmlEventMapEntry* pEntry) {
  if (pElem == NULL || pEntry->szName == NULL) return FALSE;
  BSTR bstr = NULL;
  BOOL bMatch = FALSE;
  switch (pEntry->nType) {
    case DHTMLEVENTMAPENTRY_NAME:
    case DHTMLEVENTMAPENTRY_ELEMENT:
      if (SUCCEEDED(pElem->get_id(&bstr)))
        bMatch = StringFromBSTR(bstr) == pEntry->szName;
      break;
    case DHTMLEVENTMAPENTRY_TAG:
      if (SUCCEEDED(pElem->get_tagName(&bstr)))
        bMatch = StringFromBSTR(bstr).CompareNoCase(pEntry->szName) == 0;
      break;
    case DHTMLEVENTMAPENTRY_CLASS:
      // class="a b" is in both classes.
      if (SUCCEEDED(pElem->get_className(&bstr))) {
        CString strClasses = StringFromBSTR(bstr);
        int nPos = 0;
        CString strClass = strClasses.Tokenize(_T(" \t\r\n"), nPos);
        while (!strClass.IsEmpty()) {
          if (strClass == pEntry->szName) {
            bMatch = TRUE;
            break;
          }
          strClass = strClasses.Tokenize(_T(" \t\r\n"), nPos);
        }
      }
      break;
    default:
      break;
  }
  ::SysFreeString(bstr);
  return bMatch;
}

// Calls the handlers of the entries for this event of pElement (the
// document's entries when bDocument).
static BOOL CallHandlers(CDHtmlEventSink* pSink, const DHtmlEventMapEntry* pMap, DISPID dispid,
                         IHTMLElement* pElement, IHTMLElement* pMatch, BOOL bDocument,
                         HRESULT* phr) {
  BOOL bHandled = FALSE;
  for (; pMap != NULL && pMap->nType != DHTMLEVENTMAPENTRY_END; pMap++) {
    if (pMap->dispId != dispid || pMap->pfnEventFunc == NULL) continue;
    BOOL bFor = bDocument ? pMap->szName == NULL : ElementMatches(pMatch, pMap);
    if (!bFor) continue;
    HRESULT hr = (pSink->*pMap->pfnEventFunc)(pElement);
    if (phr != NULL) *phr = hr;
    bHandled = TRUE;
  }
  return bHandled;
}

void CDHtmlEventSink::FireFor(CDHtmlSinkHandler* pHandler, DISPID dispid,
                              DISPPARAMS* pDispParams, VARIANT* pVarResult, HRESULT* phr) {
  // An ActiveX control's event: its own arguments, to the entries for
  // the control.
  if (!pHandler->m_strControl.IsEmpty()) {
    const DHtmlEventMapEntry* apMaps[2];
    int nMaps = GetMaps(apMaps);
    for (int i = 0; i < nMaps; i++)
      for (const DHtmlEventMapEntry* p = apMaps[i]; p->nType != DHTMLEVENTMAPENTRY_END; p++)
        if (p->nType == DHTMLEVENTMAPENTRY_CONTROL && p->dispId == dispid &&
            p->pfnAxInvoke != NULL && p->szName != NULL && pHandler->m_strControl == p->szName) {
          HRESULT hr = p->pfnAxInvoke(this, pDispParams, pVarResult);
          if (phr != NULL) *phr = hr;
        }
    return;
  }
  // The event object comes as the one argument.
  IHTMLEventObj* pEvent = NULL;
  if (pDispParams != NULL && pDispParams->cArgs >= 1) {
    VARIANT* pArg = &pDispParams->rgvarg[pDispParams->cArgs - 1];
    if (pArg->vt == (VT_BYREF | VT_VARIANT)) pArg = pArg->pvarVal;
    if ((pArg->vt == VT_DISPATCH || pArg->vt == VT_UNKNOWN) && pArg->punkVal != NULL)
      pArg->punkVal->QueryInterface(IID_IHTMLEventObj, (void**)&pEvent);
  }
  IHTMLEventObj* pOldEvent = m_pCurrentEvent;
  m_pCurrentEvent = pEvent;

  // The element the handlers get: the one connected, or for the
  // document's events the one where the event happened.
  IHTMLElement* pElement = NULL;
  if (!pHandler->m_bDocument)
    pHandler->m_pElement->QueryInterface(IID_IHTMLElement, (void**)&pElement);
  else if (pEvent != NULL)
    pEvent->get_srcElement(&pElement);
  IHTMLElement* pMatch = NULL;
  pHandler->m_pElement->QueryInterface(IID_IHTMLElement, (void**)&pMatch);

  const DHtmlEventMapEntry* apMaps[2];
  int nMaps = GetMaps(apMaps);
  for (int i = 0; i < nMaps; i++)
    CallHandlers(this, apMaps[i], dispid, pElement, pMatch, pHandler->m_bDocument, phr);

  if (pMatch != NULL) pMatch->Release();
  if (pElement != NULL) pElement->Release();
  m_pCurrentEvent = pOldEvent;
  if (pEvent != NULL) pEvent->Release();
}

BOOL CDHtmlEventSink::DHtmlEventHook(HRESULT* phr, DISPID dispIdMember, DISPPARAMS* pDispParams,
                                     VARIANT* pVarResult, EXCEPINFO*, UINT*) {
  (void)pVarResult;
  // The event's own element decides the entries.
  IHTMLEventObj* pEvent = NULL;
  if (pDispParams != NULL && pDispParams->cArgs >= 1) {
    VARIANT* pArg = &pDispParams->rgvarg[pDispParams->cArgs - 1];
    if ((pArg->vt == VT_DISPATCH || pArg->vt == VT_UNKNOWN) && pArg->punkVal != NULL)
      pArg->punkVal->QueryInterface(IID_IHTMLEventObj, (void**)&pEvent);
  }
  IHTMLElement* pElement = NULL;
  if (pEvent != NULL) pEvent->get_srcElement(&pElement);
  IHTMLEventObj* pOldEvent = m_pCurrentEvent;
  m_pCurrentEvent = pEvent;
  const DHtmlEventMapEntry* apMaps[2];
  int nMaps = GetMaps(apMaps);
  BOOL bHandled = FALSE;
  for (int i = 0; i < nMaps; i++)
    bHandled |= CallHandlers(this, apMaps[i], dispIdMember, pElement, pElement, FALSE, phr);
  for (int i = 0; i < nMaps && !bHandled; i++)
    bHandled |= CallHandlers(this, apMaps[i], dispIdMember, pElement, NULL, TRUE, phr);
  m_pCurrentEvent = pOldEvent;
  if (pElement != NULL) pElement->Release();
  if (pEvent != NULL) pEvent->Release();
  return bHandled;
}

HRESULT CDHtmlEventSink::GetEvent(IHTMLEventObj** ppEventObj) {
  if (ppEventObj == NULL) return E_POINTER;
  *ppEventObj = m_pCurrentEvent;
  if (m_pCurrentEvent == NULL) return E_FAIL;
  m_pCurrentEvent->AddRef();
  return S_OK;
}

BOOL CDHtmlEventSink::IsSinkedElement(IDispatch* pdispElem) {
  for (INT_PTR i = 0; i < m_aSinks.GetSize(); i++) {
    CDHtmlSinkHandler* p = (CDHtmlSinkHandler*)m_aSinks[i];
    if (SameObject(p->m_pElement, pdispElem)) return TRUE;
  }
  return FALSE;
}

HRESULT CDHtmlEventSink::ConnectElement(IDispatch* pdispElem, REFIID iidEvents, BOOL bDocument) {
  if (pdispElem == NULL) return E_POINTER;
  if (IsSinkedElement(pdispElem)) return S_FALSE;
  CDHtmlSinkHandler* pHandler = new CDHtmlSinkHandler(this, pdispElem, iidEvents, bDocument);
  HRESULT hr = pHandler->Connect();
  if (FAILED(hr)) {
    pHandler->m_pOwner = NULL;
    pHandler->Release();
    return hr;
  }
  m_aSinks.Add(pHandler);
  return S_OK;
}

HRESULT CDHtmlEventSink::ConnectDHtmlEvents(IUnknown* punkDoc) {
  if (punkDoc == NULL) return E_POINTER;
  // Only when the map has entries for the document.
  BOOL bAny = FALSE;
  const DHtmlEventMapEntry* apMaps[2];
  int nMaps = GetMaps(apMaps);
  for (int i = 0; i < nMaps; i++)
    for (const DHtmlEventMapEntry* p = apMaps[i]; p->nType != DHTMLEVENTMAPENTRY_END; p++)
      if (p->szName == NULL) bAny = TRUE;
  if (!bAny) return S_OK;
  IDispatch* pDisp = NULL;
  HRESULT hr = punkDoc->QueryInterface(IID_IDispatch, (void**)&pDisp);
  if (FAILED(hr)) return hr;
  hr = ConnectElement(pDisp, kDIID_HTMLDocumentEvents2, TRUE);
  pDisp->Release();
  return hr;
}

static void DisconnectSinks(CPtrArray& a, BOOL bDocument) {
  for (INT_PTR i = a.GetSize() - 1; i >= 0; i--) {
    CDHtmlSinkHandler* p = (CDHtmlSinkHandler*)a[i];
    if (p->m_bDocument != bDocument) continue;
    a.RemoveAt(i);
    p->Disconnect();
    p->Release();
  }
}

void CDHtmlEventSink::DisconnectDHtmlEvents() { DisconnectSinks(m_aSinks, TRUE); }

void CDHtmlEventSink::DisconnectDHtmlElementEvents() { DisconnectSinks(m_aSinks, FALSE); }

// Connects one element, or each element of a collection.
static void ConnectOneOrAll(CDHtmlEventSink* pSink, IDispatch* pDisp,
                            HRESULT (CDHtmlEventSink::*pfnConnect)(IDispatch*, REFIID, BOOL)) {
  if (pDisp == NULL) return;
  IHTMLElementCollection* pColl = NULL;
  if (FAILED(pDisp->QueryInterface(IID_IHTMLElementCollection, (void**)&pColl))) {
    (pSink->*pfnConnect)(pDisp, kDIID_HTMLElementEvents2, FALSE);
    return;
  }
  LONG n = 0;
  pColl->get_length(&n);
  for (LONG i = 0; i < n; i++) {
    VARIANT vIndex, vEmpty;
    ::VariantInit(&vIndex);
    ::VariantInit(&vEmpty);
    vIndex.vt = VT_I4;
    vIndex.lVal = i;
    IDispatch* pItem = NULL;
    if (SUCCEEDED(pColl->item(vIndex, vEmpty, &pItem)) && pItem != NULL) {
      (pSink->*pfnConnect)(pItem, kDIID_HTMLElementEvents2, FALSE);
      pItem->Release();
    }
  }
  pColl->Release();
}

HRESULT CDHtmlEventSink::ConnectDHtmlElementEvents(DWORD_PTR) {
  IHTMLDocument2* pDoc = NULL;
  HRESULT hr = GetDHtmlDocument(&pDoc);
  if (FAILED(hr) || pDoc == NULL) return FAILED(hr) ? hr : E_FAIL;
  IHTMLElementCollection* pAll = NULL;
  hr = pDoc->get_all(&pAll);
  pDoc->Release();
  if (FAILED(hr) || pAll == NULL) return FAILED(hr) ? hr : E_FAIL;

  const DHtmlEventMapEntry* apMaps[2];
  int nMaps = GetMaps(apMaps);
  for (int iMap = 0; iMap < nMaps; iMap++)
  for (const DHtmlEventMapEntry* p = apMaps[iMap]; p->nType != DHTMLEVENTMAPENTRY_END; p++) {
    if (p->szName == NULL) continue;  // the document's
    switch (p->nType) {
      case DHTMLEVENTMAPENTRY_CONTROL:
        ConnectControl(pAll, p->szName);
        break;
      case DHTMLEVENTMAPENTRY_NAME:
      case DHTMLEVENTMAPENTRY_ELEMENT: {
        VARIANT vName = StringVariant(p->szName), vEmpty;
        ::VariantInit(&vEmpty);
        IDispatch* pDisp = NULL;
        if (SUCCEEDED(pAll->item(vName, vEmpty, &pDisp)) && pDisp != NULL) {
          ConnectOneOrAll(this, pDisp, &CDHtmlEventSink::ConnectElement);
          pDisp->Release();
        }
        ::VariantClear(&vName);
        break;
      }
      case DHTMLEVENTMAPENTRY_TAG: {
        VARIANT vTag = StringVariant(p->szName);
        IDispatch* pDisp = NULL;
        if (SUCCEEDED(pAll->tags(vTag, &pDisp)) && pDisp != NULL) {
          ConnectOneOrAll(this, pDisp, &CDHtmlEventSink::ConnectElement);
          pDisp->Release();
        }
        ::VariantClear(&vTag);
        break;
      }
      case DHTMLEVENTMAPENTRY_CLASS: {
        LONG n = 0;
        pAll->get_length(&n);
        for (LONG i = 0; i < n; i++) {
          VARIANT vIndex, vEmpty;
          ::VariantInit(&vIndex);
          ::VariantInit(&vEmpty);
          vIndex.vt = VT_I4;
          vIndex.lVal = i;
          IDispatch* pItem = NULL;
          if (FAILED(pAll->item(vIndex, vEmpty, &pItem)) || pItem == NULL) continue;
          IHTMLElement* pElem = NULL;
          if (SUCCEEDED(pItem->QueryInterface(IID_IHTMLElement, (void**)&pElem))) {
            if (ElementMatches(pElem, p)) ConnectElement(pItem, kDIID_HTMLElementEvents2, FALSE);
            pElem->Release();
          }
          pItem->Release();
        }
        break;
      }
      default:
        break;
    }
  }
  pAll->Release();
  return S_OK;
}

// The events' interface of a control: its default source.
static BOOL GetSourceIID(IDispatch* pCtl, IID* piid) {
  IProvideClassInfo2* pPCI2 = NULL;
  if (SUCCEEDED(pCtl->QueryInterface(IID_IProvideClassInfo2, (void**)&pPCI2))) {
    HRESULT hr = pPCI2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, piid);
    pPCI2->Release();
    if (SUCCEEDED(hr)) return TRUE;
  }
  // The class's type information: its default source interface.
  BOOL bFound = FALSE;
  IProvideClassInfo* pPCI = NULL;
  if (SUCCEEDED(pCtl->QueryInterface(IID_IProvideClassInfo, (void**)&pPCI))) {
    ITypeInfo* pClass = NULL;
    if (SUCCEEDED(pPCI->GetClassInfo(&pClass)) && pClass != NULL) {
      TYPEATTR* pAttr = NULL;
      if (SUCCEEDED(pClass->GetTypeAttr(&pAttr))) {
        for (UINT i = 0; i < pAttr->cImplTypes && !bFound; i++) {
          INT nFlags = 0;
          if (FAILED(pClass->GetImplTypeFlags(i, &nFlags))) continue;
          if ((nFlags & (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)) !=
              (IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE))
            continue;
          HREFTYPE hRef = 0;
          ITypeInfo* pSource = NULL;
          if (SUCCEEDED(pClass->GetRefTypeOfImplType(i, &hRef)) &&
              SUCCEEDED(pClass->GetRefTypeInfo(hRef, &pSource))) {
            TYPEATTR* pSrcAttr = NULL;
            if (SUCCEEDED(pSource->GetTypeAttr(&pSrcAttr))) {
              *piid = pSrcAttr->guid;
              bFound = TRUE;
              pSource->ReleaseTypeAttr(pSrcAttr);
            }
            pSource->Release();
          }
        }
        pClass->ReleaseTypeAttr(pAttr);
      }
      pClass->Release();
    }
    pPCI->Release();
  }
  if (bFound) return TRUE;
  // Otherwise its first connection point.
  IConnectionPointContainer* pCPC = NULL;
  if (FAILED(pCtl->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) return FALSE;
  IEnumConnectionPoints* pEnum = NULL;
  if (SUCCEEDED(pCPC->EnumConnectionPoints(&pEnum)) && pEnum != NULL) {
    IConnectionPoint* pCP = NULL;
    if (pEnum->Next(1, &pCP, NULL) == S_OK && pCP != NULL) {
      bFound = SUCCEEDED(pCP->GetConnectionInterface(piid));
      pCP->Release();
    }
    pEnum->Release();
  }
  pCPC->Release();
  return bFound;
}

// The events of the ActiveX control of the <object> element szName.
HRESULT CDHtmlEventSink::ConnectControl(IHTMLElementCollection* pAll, LPCTSTR szName) {
  VARIANT vName = StringVariant(szName), vEmpty;
  ::VariantInit(&vEmpty);
  IDispatch* pElem = NULL;
  HRESULT hr = pAll->item(vName, vEmpty, &pElem);
  ::VariantClear(&vName);
  if (FAILED(hr) || pElem == NULL) return FAILED(hr) ? hr : E_FAIL;
  VARIANT var;
  hr = GetDispProperty(pElem, DISPID_IHTMLOBJECTELEMENT_OBJECT, &var);
  pElem->Release();
  if (FAILED(hr)) return hr;
  if (var.vt != VT_DISPATCH || var.pdispVal == NULL) {
    ::VariantClear(&var);
    return E_NOINTERFACE;
  }
  IDispatch* pCtl = var.pdispVal;
  IID iid;
  hr = E_NOINTERFACE;
  if (!IsSinkedElement(pCtl) && GetSourceIID(pCtl, &iid)) {
    CDHtmlSinkHandler* pHandler = new CDHtmlSinkHandler(this, pCtl, iid, FALSE, szName);
    hr = pHandler->Connect();
    if (FAILED(hr)) {
      pHandler->m_pOwner = NULL;
      pHandler->Release();
    } else {
      m_aSinks.Add(pHandler);
    }
  }
  pCtl->Release();
  return hr;
}

// ---------------------------------------------------------------------
// CDHtmlControlSite

BEGIN_INTERFACE_MAP(CDHtmlControlSite, COleControlSite)
  INTERFACE_PART(CDHtmlControlSite, IID_IDocHostUIHandler, DocHostUIHandler)
END_INTERFACE_MAP()

CDHtmlControlSite::CDHtmlControlSite(COleControlContainer* pParentWnd, CDHtmlDialog* pHandler)
    : COleControlSite(pParentWnd), m_pHandler(pHandler) {}

STDMETHODIMP_(ULONG) CDHtmlControlSite::XDocHostUIHandler::AddRef() {
  METHOD_PROLOGUE_EX_(CDHtmlControlSite, DocHostUIHandler)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CDHtmlControlSite::XDocHostUIHandler::Release() {
  METHOD_PROLOGUE_EX_(CDHtmlControlSite, DocHostUIHandler)
  return pThis->ExternalRelease();
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::QueryInterface(REFIID iid, LPVOID* ppv) {
  METHOD_PROLOGUE_EX_(CDHtmlControlSite, DocHostUIHandler)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}

#define DHTMLSITE_DIALOG(failValue)                             \
  METHOD_PROLOGUE_EX_(CDHtmlControlSite, DocHostUIHandler)      \
  CDHtmlDialog* pDlg = pThis->m_pHandler;                       \
  if (pDlg == NULL) return failValue;

STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::ShowContextMenu(DWORD dwID, LPPOINT ppt,
                                                                   LPUNKNOWN pcmdt,
                                                                   LPDISPATCH pdisp) {
  DHTMLSITE_DIALOG(S_FALSE)
  return pDlg->ShowContextMenu(dwID, ppt, pcmdt, pdisp);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::GetHostInfo(DOCHOSTUIINFO* pInfo) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->GetHostInfo(pInfo);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::ShowUI(DWORD dwID,
                                                          LPOLEINPLACEACTIVEOBJECT pActiveObject,
                                                          LPOLECOMMANDTARGET pCommandTarget,
                                                          LPOLEINPLACEFRAME pFrame,
                                                          LPOLEINPLACEUIWINDOW pDoc) {
  DHTMLSITE_DIALOG(S_FALSE)
  return pDlg->ShowUI(dwID, pActiveObject, pCommandTarget, pFrame, pDoc);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::HideUI() {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->HideUI();
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::UpdateUI() {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->UpdateUI();
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::EnableModeless(BOOL fEnable) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->EnableModeless(fEnable);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::OnDocWindowActivate(BOOL fActivate) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->OnDocWindowActivate(fActivate);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::OnFrameWindowActivate(BOOL fActivate) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->OnFrameWindowActivate(fActivate);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::ResizeBorder(LPCRECT prcBorder,
                                                                LPOLEINPLACEUIWINDOW pUIWindow,
                                                                BOOL fFrameWindow) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->ResizeBorder(prcBorder, pUIWindow, fFrameWindow);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::TranslateAccelerator(LPMSG lpMsg,
                                                                        const GUID* pguidCmdGroup,
                                                                        DWORD nCmdID) {
  DHTMLSITE_DIALOG(S_FALSE)
  return pDlg->TranslateAccelerator(lpMsg, pguidCmdGroup, nCmdID);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::GetOptionKeyPath(LPOLESTR* pchKey,
                                                                    DWORD dwReserved) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->GetOptionKeyPath(pchKey, dwReserved);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::GetDropTarget(LPDROPTARGET pDropTarget,
                                                                 LPDROPTARGET* ppDropTarget) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->GetDropTarget(pDropTarget, ppDropTarget);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::GetExternal(LPDISPATCH* lppDispatch) {
  DHTMLSITE_DIALOG(E_NOTIMPL)
  return pDlg->GetExternal(lppDispatch);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::TranslateUrl(DWORD dwTranslate,
                                                                OLECHAR* pchURLIn,
                                                                OLECHAR** ppchURLOut) {
  DHTMLSITE_DIALOG(S_FALSE)
  return pDlg->TranslateUrl(dwTranslate, pchURLIn, ppchURLOut);
}
STDMETHODIMP CDHtmlControlSite::XDocHostUIHandler::FilterDataObject(LPDATAOBJECT pDataObject,
                                                                    LPDATAOBJECT* ppDataObject) {
  DHTMLSITE_DIALOG(S_FALSE)
  return pDlg->FilterDataObject(pDataObject, ppDataObject);
}

// ---------------------------------------------------------------------
// CDHtmlDialog

IMPLEMENT_DYNAMIC(CDHtmlDialog, CDialog)

BEGIN_DHTML_EVENT_MAP(CDHtmlDialog)
END_DHTML_EVENT_MAP()

BEGIN_MESSAGE_MAP(CDHtmlDialog, CDialog)
  ON_WM_DESTROY()
END_MESSAGE_MAP()

BEGIN_EVENTSINK_MAP(CDHtmlDialog, CDialog)
  ON_EVENT(CDHtmlDialog, AFX_IDC_BROWSER, DISPID_BEFORENAVIGATE2, _OnBeforeNavigate2,
           VTS_DISPATCH VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PBOOL)
  ON_EVENT(CDHtmlDialog, AFX_IDC_BROWSER, DISPID_NAVIGATECOMPLETE2, _OnNavigateComplete2,
           VTS_DISPATCH VTS_PVARIANT)
  ON_EVENT(CDHtmlDialog, AFX_IDC_BROWSER, DISPID_DOCUMENTCOMPLETE, _OnDocumentComplete,
           VTS_DISPATCH VTS_PVARIANT)
END_EVENTSINK_MAP()

void CDHtmlDialog::Initialize() {
  m_szHtmlResID = NULL;
  m_nHtmlResID = 0;
  m_bUseHtmlTitle = FALSE;
  m_dwHostFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_SCROLL_NO | DOCHOSTUIFLAG_DIALOG;
}

CDHtmlDialog::CDHtmlDialog() { Initialize(); }

CDHtmlDialog::CDHtmlDialog(LPCTSTR lpszTemplateName, LPCTSTR szHtmlResID, CWnd* pParentWnd)
    : CDialog(lpszTemplateName, pParentWnd) {
  Initialize();
  if (szHtmlResID != NULL) {
    // A name is kept as a copy; a number as it is.
    if (IS_INTRESOURCE(szHtmlResID)) {
      m_szHtmlResID = (LPTSTR)szHtmlResID;
    } else {
      size_t n = _tcslen(szHtmlResID) + 1;
      m_szHtmlResID = new TCHAR[n];
      memcpy(m_szHtmlResID, szHtmlResID, n * sizeof(TCHAR));
    }
  }
}

CDHtmlDialog::CDHtmlDialog(UINT nIDTemplate, UINT nHtmlResID, CWnd* pParentWnd)
    : CDialog(nIDTemplate, pParentWnd) {
  Initialize();
  m_nHtmlResID = nHtmlResID;
}

CDHtmlDialog::~CDHtmlDialog() {
  DisconnectDHtmlEvents();
  DisconnectDHtmlElementEvents();
  if (m_szHtmlResID != NULL && !IS_INTRESOURCE(m_szHtmlResID)) delete[] m_szHtmlResID;
}

BOOL CDHtmlDialog::CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite,
                                     UINT, REFCLSID) {
  if (ppSite == NULL) return FALSE;
  *ppSite = new CDHtmlControlSite(pContainer, this);
  return TRUE;
}

BOOL CDHtmlDialog::OnInitDialog() {
  CDialog::OnInitDialog();

  // The page is the dialog's face: the browser covers the client area.
  CRect rect;
  GetClientRect(&rect);
  if (m_wndBrowser.CreateControl(CLSID_WebBrowser, NULL, WS_VISIBLE | WS_CHILD, rect, this,
                                 AFX_IDC_BROWSER)) {
    LPUNKNOWN pUnk = m_wndBrowser.GetControlUnknown();
    if (pUnk != NULL) pUnk->QueryInterface(IID_IWebBrowser2, (void**)&m_pBrowserApp);
  } else {
    TRACE(_T("Warning: CDHtmlDialog could not make the WebBrowser control.\n"));
  }
  if (m_pBrowserApp != NULL) {
    if (m_nHtmlResID != 0)
      LoadFromResource(m_nHtmlResID);
    else if (m_szHtmlResID != NULL)
      LoadFromResource(m_szHtmlResID);
  }
  return TRUE;
}

void CDHtmlDialog::OnDestroy() {
  DisconnectDHtmlEvents();
  DisconnectDHtmlElementEvents();
  m_spHtmlDoc.Release();
  m_pBrowserApp.Release();
  CDialog::OnDestroy();
}

void CDHtmlDialog::DestroyModeless() { DestroyWindow(); }

BOOL CDHtmlDialog::PreTranslateMessage(MSG* pMsg) {
  // The page gets its keys before the dialog's own: tab between its
  // fields, Enter in a text area.
  if (m_pBrowserApp != NULL && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
      (pMsg->hwnd == m_hWnd || ::IsChild(m_hWnd, pMsg->hwnd))) {
    IOleInPlaceActiveObject* pActive = NULL;
    if (SUCCEEDED(m_pBrowserApp->QueryInterface(IID_IOleInPlaceActiveObject,
                                                (void**)&pActive))) {
      HRESULT hr = pActive->TranslateAccelerator(pMsg);
      pActive->Release();
      if (hr == S_OK) return TRUE;
    }
  }
  return CDialog::PreTranslateMessage(pMsg);
}

// ----- The page

BOOL CDHtmlDialog::LoadFromResource(LPCTSTR lpszResource) {
  TCHAR szModule[MAX_PATH];
  if (::GetModuleFileName(AfxGetResourceHandle(), szModule, MAX_PATH) == 0) return FALSE;
  CString strURL;
  if (IS_INTRESOURCE(lpszResource))
    strURL.Format(_T("res://%s/%u"), szModule, (UINT)(UINT_PTR)lpszResource);
  else
    strURL.Format(_T("res://%s/%s"), szModule, lpszResource);
  Navigate(strURL);
  return m_pBrowserApp != NULL;
}

BOOL CDHtmlDialog::LoadFromResource(UINT nRes) { return LoadFromResource(MAKEINTRESOURCE(nRes)); }

void CDHtmlDialog::Navigate(LPCTSTR lpszURL, DWORD dwFlags, LPCTSTR lpszTargetFrameName,
                            LPCTSTR lpszHeaders, LPVOID lpvPostData, DWORD dwPostDataLen) {
  if (m_pBrowserApp == NULL) return;
  VARIANT varURL = StringVariant(lpszURL);
  VARIANT varFlags, varTarget, varPost, varHeaders;
  ::VariantInit(&varFlags);
  varFlags.vt = VT_I4;
  varFlags.lVal = (LONG)dwFlags;
  ::VariantInit(&varTarget);
  if (lpszTargetFrameName != NULL) varTarget = StringVariant(lpszTargetFrameName);
  ::VariantInit(&varHeaders);
  if (lpszHeaders != NULL) varHeaders = StringVariant(lpszHeaders);
  ::VariantInit(&varPost);
  if (lpvPostData != NULL && dwPostDataLen != 0) {
    SAFEARRAY* psa = ::SafeArrayCreateVector(VT_UI1, 0, dwPostDataLen);
    void* pv = NULL;
    if (psa != NULL && SUCCEEDED(::SafeArrayAccessData(psa, &pv))) {
      memcpy(pv, lpvPostData, dwPostDataLen);
      ::SafeArrayUnaccessData(psa);
      varPost.vt = VT_ARRAY | VT_UI1;
      varPost.parray = psa;
    }
  }
  m_pBrowserApp->Navigate2(&varURL, &varFlags, &varTarget, &varPost, &varHeaders);
  ::VariantClear(&varURL);
  ::VariantClear(&varTarget);
  ::VariantClear(&varPost);
  ::VariantClear(&varHeaders);
}

void CDHtmlDialog::GetCurrentUrl(CString& szUrl) { szUrl = m_strCurrentUrl; }

HRESULT CDHtmlDialog::GetDHtmlDocument(IHTMLDocument2** pphtmlDoc) {
  if (pphtmlDoc == NULL) return E_POINTER;
  *pphtmlDoc = NULL;
  if (m_spHtmlDoc == NULL) return E_NOINTERFACE;
  return m_spHtmlDoc.CopyTo(pphtmlDoc);
}

// ----- The browser's events

// Only the top-level page counts, not its frames.
static BOOL IsTopLevel(IWebBrowser2* pBrowser, LPDISPATCH pDisp) {
  return pDisp == NULL || SameObject(pBrowser, pDisp);
}

void CDHtmlDialog::_OnBeforeNavigate2(LPDISPATCH pDisp, VARIANT* URL, VARIANT*, VARIANT*,
                                      VARIANT*, VARIANT*, BOOL*) {
  OnBeforeNavigate(pDisp, StringFromVariant(URL));
}

void CDHtmlDialog::_OnNavigateComplete2(LPDISPATCH pDisp, VARIANT* URL) {
  OnNavigateComplete(pDisp, StringFromVariant(URL));
}

void CDHtmlDialog::_OnDocumentComplete(LPDISPATCH pDisp, VARIANT* URL) {
  OnDocumentComplete(pDisp, StringFromVariant(URL));
}

void CDHtmlDialog::OnBeforeNavigate(LPDISPATCH pDisp, LPCTSTR) {
  // The old page's elements go with it.
  if (IsTopLevel(m_pBrowserApp, pDisp)) {
    DisconnectDHtmlEvents();
    DisconnectDHtmlElementEvents();
    m_spHtmlDoc.Release();
  }
}

void CDHtmlDialog::OnNavigateComplete(LPDISPATCH, LPCTSTR) {}

void CDHtmlDialog::OnDocumentComplete(LPDISPATCH pDisp, LPCTSTR szUrl) {
  if (m_pBrowserApp == NULL || !IsTopLevel(m_pBrowserApp, pDisp)) return;
  m_strCurrentUrl = szUrl;
  m_spHtmlDoc.Release();
  CComPtr<IDispatch> spDisp;
  m_pBrowserApp->get_Document(&spDisp);
  if (spDisp != NULL) spDisp->QueryInterface(IID_IHTMLDocument2, (void**)&m_spHtmlDoc);
  if (m_spHtmlDoc == NULL) return;

  DisconnectDHtmlEvents();
  DisconnectDHtmlElementEvents();
  ConnectDHtmlEvents(m_spHtmlDoc);
  ConnectDHtmlElementEvents();

  if (m_bUseHtmlTitle) {
    BSTR bstrTitle = NULL;
    if (SUCCEEDED(m_spHtmlDoc->get_title(&bstrTitle)) && bstrTitle != NULL)
      SetWindowText(StringFromBSTR(bstrTitle));
    ::SysFreeString(bstrTitle);
  }
  // The elements show the dialog's data.
  UpdateData(FALSE);
}

// ----- Elements

HRESULT CDHtmlDialog::GetElement(LPCTSTR szElementId, IDispatch** ppdisp, BOOL* pbCollection) {
  if (ppdisp == NULL) return E_POINTER;
  *ppdisp = NULL;
  if (pbCollection != NULL) *pbCollection = FALSE;
  if (m_spHtmlDoc == NULL) return E_NOINTERFACE;
  CComPtr<IHTMLElementCollection> spAll;
  HRESULT hr = m_spHtmlDoc->get_all(&spAll);
  if (FAILED(hr) || spAll == NULL) return FAILED(hr) ? hr : E_NOINTERFACE;
  VARIANT vName = StringVariant(szElementId), vEmpty;
  ::VariantInit(&vEmpty);
  hr = spAll->item(vName, vEmpty, ppdisp);
  ::VariantClear(&vName);
  if (SUCCEEDED(hr) && *ppdisp == NULL) hr = E_NOINTERFACE;  // no such element
  if (SUCCEEDED(hr) && pbCollection != NULL) {
    IHTMLElementCollection* pColl = NULL;
    if (SUCCEEDED((*ppdisp)->QueryInterface(IID_IHTMLElementCollection, (void**)&pColl))) {
      *pbCollection = TRUE;
      pColl->Release();
    }
  }
  return hr;
}

HRESULT CDHtmlDialog::GetElement(LPCTSTR szElementId, IHTMLElement** pphtmlElement) {
  return GetElementInterface(szElementId, IID_IHTMLElement, (void**)pphtmlElement);
}

HRESULT CDHtmlDialog::GetElementInterface(LPCTSTR szElementId, REFIID riid, void** ppvObj) {
  if (ppvObj == NULL) return E_POINTER;
  *ppvObj = NULL;
  CComPtr<IDispatch> spDisp;
  HRESULT hr = GetElement(szElementId, &spDisp);
  if (FAILED(hr)) return hr;
  return spDisp->QueryInterface(riid, ppvObj);
}

VARIANT CDHtmlDialog::GetElementProperty(LPCTSTR szElementId, DISPID dispid) {
  VARIANT var;
  ::VariantInit(&var);
  CComPtr<IDispatch> spDisp;
  if (SUCCEEDED(GetElement(szElementId, &spDisp))) GetDispProperty(spDisp, dispid, &var);
  return var;
}

void CDHtmlDialog::SetElementProperty(LPCTSTR szElementId, DISPID dispid, VARIANT* pVar) {
  CComPtr<IDispatch> spDisp;
  if (SUCCEEDED(GetElement(szElementId, &spDisp))) PutDispProperty(spDisp, dispid, pVar);
}

BSTR CDHtmlDialog::GetElementText(LPCTSTR szElementId) {
  BSTR bstr = NULL;
  CComPtr<IHTMLElement> spElem;
  if (SUCCEEDED(GetElement(szElementId, &spElem))) spElem->get_innerText(&bstr);
  return bstr;
}

void CDHtmlDialog::SetElementText(LPCTSTR szElementId, BSTR bstrText) {
  CComPtr<IHTMLElement> spElem;
  if (SUCCEEDED(GetElement(szElementId, &spElem))) spElem->put_innerText(bstrText);
}

void CDHtmlDialog::SetElementText(IUnknown* punkElem, BSTR bstrText) {
  CComQIPtr<IHTMLElement> spElem(punkElem);
  if (spElem != NULL) spElem->put_innerText(bstrText);
}

BSTR CDHtmlDialog::GetElementHtml(LPCTSTR szElementId) {
  BSTR bstr = NULL;
  CComPtr<IHTMLElement> spElem;
  if (SUCCEEDED(GetElement(szElementId, &spElem))) spElem->get_innerHTML(&bstr);
  return bstr;
}

void CDHtmlDialog::SetElementHtml(LPCTSTR szElementId, BSTR bstrText) {
  CComPtr<IHTMLElement> spElem;
  if (SUCCEEDED(GetElement(szElementId, &spElem))) spElem->put_innerHTML(bstrText);
}

void CDHtmlDialog::SetElementHtml(IUnknown* punkElem, BSTR bstrText) {
  CComQIPtr<IHTMLElement> spElem(punkElem);
  if (spElem != NULL) spElem->put_innerHTML(bstrText);
}

// ----- ActiveX controls

HRESULT CDHtmlDialog::GetControlDispatch(LPCTSTR szId, IDispatch** ppdisp) {
  if (ppdisp == NULL) return E_POINTER;
  *ppdisp = NULL;
  CComPtr<IDispatch> spElem;
  HRESULT hr = GetElement(szId, &spElem);
  if (FAILED(hr)) return hr;
  // The <object> element's object property is the control.
  VARIANT var;
  hr = GetDispProperty(spElem, DISPID_IHTMLOBJECTELEMENT_OBJECT, &var);
  if (FAILED(hr)) return hr;
  if (var.vt == VT_DISPATCH && var.pdispVal != NULL) {
    *ppdisp = var.pdispVal;
    return S_OK;
  }
  ::VariantClear(&var);
  return E_NOINTERFACE;
}

VARIANT CDHtmlDialog::GetControlProperty(IDispatch* pdispControl, DISPID dispid) {
  VARIANT var;
  GetDispProperty(pdispControl, dispid, &var);
  return var;
}

VARIANT CDHtmlDialog::GetControlProperty(LPCTSTR szId, DISPID dispid) {
  CComPtr<IDispatch> spCtl;
  VARIANT var;
  ::VariantInit(&var);
  if (SUCCEEDED(GetControlDispatch(szId, &spCtl))) var = GetControlProperty(spCtl, dispid);
  return var;
}

VARIANT CDHtmlDialog::GetControlProperty(LPCTSTR szId, LPCTSTR szPropName) {
  CComPtr<IDispatch> spCtl;
  VARIANT var;
  ::VariantInit(&var);
  if (SUCCEEDED(GetControlDispatch(szId, &spCtl))) {
    DISPID dispid = DispidOfName(spCtl, szPropName);
    if (dispid != DISPID_UNKNOWN) var = GetControlProperty(spCtl, dispid);
  }
  return var;
}

void CDHtmlDialog::SetControlProperty(IDispatch* pdispControl, DISPID dispid, VARIANT* pVar) {
  PutDispProperty(pdispControl, dispid, pVar);
}

void CDHtmlDialog::SetControlProperty(LPCTSTR szElementId, DISPID dispid, VARIANT* pVar) {
  CComPtr<IDispatch> spCtl;
  if (SUCCEEDED(GetControlDispatch(szElementId, &spCtl))) PutDispProperty(spCtl, dispid, pVar);
}

void CDHtmlDialog::SetControlProperty(LPCTSTR szElementId, LPCTSTR szPropName, VARIANT* pVar) {
  CComPtr<IDispatch> spCtl;
  if (SUCCEEDED(GetControlDispatch(szElementId, &spCtl))) {
    DISPID dispid = DispidOfName(spCtl, szPropName);
    if (dispid != DISPID_UNKNOWN) PutDispProperty(spCtl, dispid, pVar);
  }
}

// ----- Dialog data exchange

void CDHtmlDialog::DDX_DHtml_ElementText(CDataExchange* pDX, LPCTSTR szId, DISPID dispid,
                                         CString& value, BOOL bSave) {
  if (bSave) {
    VARIANT var = GetElementProperty(szId, dispid);
    value = StringFromVariant(&var);
    ::VariantClear(&var);
  } else {
    VARIANT var = StringVariant(value);
    SetElementProperty(szId, dispid, &var);
    ::VariantClear(&var);
  }
  (void)pDX;
}

// A number, a date or an amount as an element's text: VARIANT
// conversions both ways; text that is not one fails the exchange.
void CDHtmlDialog::DDX_DHtml_ElementVariant(CDataExchange* pDX, LPCTSTR szId, DISPID dispid,
                                            VARIANT& var, VARTYPE vt) {
  if (pDX->m_bSaveAndValidate) {
    CString str;
    DDX_DHtml_ElementText(pDX, szId, dispid, str, TRUE);
    str.Trim();
    VARIANT varText = StringVariant(str);
    VARIANT varValue;
    ::VariantInit(&varValue);
    HRESULT hr = ::VariantChangeType(&varValue, &varText, 0, vt);
    ::VariantClear(&varText);
    if (FAILED(hr)) {
      UINT nID = vt == VT_R4 || vt == VT_R8 ? AFX_IDP_PARSE_REAL
                 : vt == VT_DATE           ? AFX_IDP_PARSE_DATETIME
                 : vt == VT_CY             ? AFX_IDP_PARSE_CURRENCY
                 : vt == VT_UI4            ? AFX_IDP_PARSE_UINT
                                           : AFX_IDP_PARSE_INT;
      AfxMessageBox(nID, MB_ICONEXCLAMATION);
      pDX->Fail();
    }
    ::VariantClear(&var);
    var = varValue;
  } else {
    VARIANT varText;
    ::VariantInit(&varText);
    CString str;
    if (SUCCEEDED(::VariantChangeType(&varText, &var, 0, VT_BSTR)))
      str = StringFromBSTR(varText.bstrVal);
    ::VariantClear(&varText);
    DDX_DHtml_ElementText(pDX, szId, dispid, str, FALSE);
  }
}

// The numeric DDX functions, for a property of the element.
#define DHTML_DDX_NUMBER(fn, dispid, type, vtype, member) \
  void CDHtmlDialog::fn(CDataExchange* pDX, LPCTSTR szId, type& value) { \
    VARIANT var;                                                          \
    ::VariantInit(&var);                                                  \
    var.vt = vtype;                                                          \
    var.member = value;                                                   \
    DDX_DHtml_ElementVariant(pDX, szId, dispid, var, vtype);                 \
    if (pDX->m_bSaveAndValidate) value = (type)var.member;                \
  }

#define DHTML_DDX_ALL(fn, dispid)                                                  \
  void CDHtmlDialog::fn(CDataExchange* pDX, LPCTSTR szId, CString& value) {        \
    DDX_DHtml_ElementText(pDX, szId, dispid, value, pDX->m_bSaveAndValidate);      \
  }                                                                                \
  DHTML_DDX_NUMBER(fn, dispid, short, VT_I2, iVal)                                 \
  DHTML_DDX_NUMBER(fn, dispid, int, VT_I4, lVal)                                   \
  DHTML_DDX_NUMBER(fn, dispid, long, VT_I4, lVal)                                  \
  DHTML_DDX_NUMBER(fn, dispid, DWORD, VT_UI4, ulVal)                               \
  DHTML_DDX_NUMBER(fn, dispid, float, VT_R4, fltVal)                               \
  DHTML_DDX_NUMBER(fn, dispid, double, VT_R8, dblVal)                              \
  void CDHtmlDialog::fn(CDataExchange* pDX, LPCTSTR szId, COleDateTime& value) {   \
    VARIANT var;                                                                   \
    ::VariantInit(&var);                                                           \
    var.vt = VT_DATE;                                                              \
    var.date = value.m_dt;                                                         \
    DDX_DHtml_ElementVariant(pDX, szId, dispid, var, VT_DATE);                     \
    if (pDX->m_bSaveAndValidate) value = COleDateTime(var.date);                   \
  }                                                                                \
  void CDHtmlDialog::fn(CDataExchange* pDX, LPCTSTR szId, COleCurrency& value) {   \
    VARIANT var;                                                                   \
    ::VariantInit(&var);                                                           \
    var.vt = VT_CY;                                                                \
    var.cyVal = value.m_cur;                                                       \
    DDX_DHtml_ElementVariant(pDX, szId, dispid, var, VT_CY);                       \
    if (pDX->m_bSaveAndValidate) value = COleCurrency(var.cyVal);                  \
  }

DHTML_DDX_ALL(DDX_DHtml_ElementValue, DISPID_IHTMLINPUTELEMENT_VALUE)
DHTML_DDX_ALL(DDX_DHtml_ElementInnerText, DISPID_IHTMLELEMENT_INNERTEXT)

void CDHtmlDialog::DDX_DHtml_ElementInnerHtml(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLELEMENT_INNERHTML, value, pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_Anchor_Href(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLANCHORELEMENT_HREF, value, pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_Anchor_Target(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLANCHORELEMENT_TARGET, value,
                        pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_Img_Src(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLIMGELEMENT_SRC, value, pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_Frame_Src(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLFRAMEBASE_SRC, value, pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_IFrame_Src(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLFRAMEBASE_SRC, value, pDX->m_bSaveAndValidate);
}

// A check box: 0 unchecked, 1 checked, 2 indeterminate.
void CDHtmlDialog::DDX_DHtml_CheckBox(CDataExchange* pDX, LPCTSTR szId, int& value) {
  CComPtr<IDispatch> spElem;
  if (FAILED(GetElement(szId, &spElem))) return;
  VARIANT var;
  if (pDX->m_bSaveAndValidate) {
    BOOL bIndeterminate = FALSE;
    if (SUCCEEDED(GetDispProperty(spElem, DISPID_IHTMLINPUTELEMENT_INDETERMINATE, &var)) &&
        var.vt == VT_BOOL)
      bIndeterminate = var.boolVal != VARIANT_FALSE;
    ::VariantClear(&var);
    if (bIndeterminate) {
      value = 2;
      return;
    }
    value = 0;
    if (SUCCEEDED(GetDispProperty(spElem, DISPID_IHTMLINPUTELEMENT_CHECKED, &var)) &&
        SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_BOOL)))
      value = var.boolVal != VARIANT_FALSE ? 1 : 0;
    ::VariantClear(&var);
  } else {
    ::VariantInit(&var);
    var.vt = VT_BOOL;
    var.boolVal = value == 2 ? VARIANT_TRUE : VARIANT_FALSE;
    PutDispProperty(spElem, DISPID_IHTMLINPUTELEMENT_INDETERMINATE, &var);
    var.boolVal = value == 1 ? VARIANT_TRUE : VARIANT_FALSE;
    PutDispProperty(spElem, DISPID_IHTMLINPUTELEMENT_CHECKED, &var);
  }
}

// A group of radio buttons that share the id: the index of the checked
// one, or -1.
void CDHtmlDialog::DDX_DHtml_Radio(CDataExchange* pDX, LPCTSTR szId, long& value) {
  CComPtr<IDispatch> spDisp;
  BOOL bCollection = FALSE;
  if (FAILED(GetElement(szId, &spDisp, &bCollection))) return;
  CComQIPtr<IHTMLElementCollection> spColl(spDisp);
  LONG n = 1;
  if (spColl != NULL) spColl->get_length(&n);
  if (pDX->m_bSaveAndValidate) value = -1;
  for (LONG i = 0; i < n; i++) {
    CComPtr<IDispatch> spItem;
    if (spColl != NULL) {
      VARIANT vIndex, vEmpty;
      ::VariantInit(&vIndex);
      ::VariantInit(&vEmpty);
      vIndex.vt = VT_I4;
      vIndex.lVal = i;
      spColl->item(vIndex, vEmpty, &spItem);
    } else {
      spItem = spDisp;
    }
    if (spItem == NULL) continue;
    VARIANT var;
    if (pDX->m_bSaveAndValidate) {
      if (SUCCEEDED(GetDispProperty(spItem, DISPID_IHTMLINPUTELEMENT_CHECKED, &var)) &&
          SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_BOOL)) && var.boolVal != VARIANT_FALSE) {
        value = i;
        ::VariantClear(&var);
        break;
      }
      ::VariantClear(&var);
    } else {
      ::VariantInit(&var);
      var.vt = VT_BOOL;
      var.boolVal = i == value ? VARIANT_TRUE : VARIANT_FALSE;
      PutDispProperty(spItem, DISPID_IHTMLINPUTELEMENT_CHECKED, &var);
    }
  }
}

void CDHtmlDialog::DDX_DHtml_SelectValue(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  DDX_DHtml_ElementText(pDX, szId, DISPID_IHTMLSELECTELEMENT_VALUE, value,
                        pDX->m_bSaveAndValidate);
}

void CDHtmlDialog::DDX_DHtml_SelectIndex(CDataExchange* pDX, LPCTSTR szId, long& value) {
  if (pDX->m_bSaveAndValidate) {
    VARIANT var = GetElementProperty(szId, DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX);
    value = -1;
    if (SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_I4))) value = var.lVal;
    ::VariantClear(&var);
  } else {
    VARIANT var;
    ::VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = (LONG)value;
    SetElementProperty(szId, DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, &var);
  }
}

// The text of the selected option; setting it selects the option with
// that text.
void CDHtmlDialog::DDX_DHtml_SelectString(CDataExchange* pDX, LPCTSTR szId, CString& value) {
  CComPtr<IDispatch> spSelect;
  if (FAILED(GetElement(szId, &spSelect))) return;
  VARIANT var;
  LONG nCount = 0, nSel = -1;
  if (SUCCEEDED(GetDispProperty(spSelect, DISPID_IHTMLSELECTELEMENT_LENGTH, &var)) &&
      SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_I4)))
    nCount = var.lVal;
  ::VariantClear(&var);
  if (SUCCEEDED(GetDispProperty(spSelect, DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, &var)) &&
      SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_I4)))
    nSel = var.lVal;
  ::VariantClear(&var);

  if (pDX->m_bSaveAndValidate) value.Empty();
  for (LONG i = 0; i < nCount; i++) {
    if (pDX->m_bSaveAndValidate && i != nSel) continue;
    // select.item(i) is the option.
    VARIANT args[2], varOption;
    ::VariantInit(&args[0]);  // the index argument, unused
    ::VariantInit(&args[1]);
    args[1].vt = VT_I4;
    args[1].lVal = i;
    ::VariantInit(&varOption);
    DISPPARAMS dp = {args, NULL, 2, 0};
    if (FAILED(spSelect->Invoke(DISPID_IHTMLSELECTELEMENT_ITEM, IID_NULL, LOCALE_USER_DEFAULT,
                                DISPATCH_METHOD, &dp, &varOption, NULL, NULL)) ||
        varOption.vt != VT_DISPATCH || varOption.pdispVal == NULL) {
      ::VariantClear(&varOption);
      continue;
    }
    VARIANT varText;
    GetDispProperty(varOption.pdispVal, DISPID_IHTMLOPTIONELEMENT_TEXT, &varText);
    CString strText = StringFromVariant(&varText);
    ::VariantClear(&varText);
    ::VariantClear(&varOption);
    if (pDX->m_bSaveAndValidate) {
      value = strText;
      break;
    }
    if (strText == value) {
      VARIANT varIndex;
      ::VariantInit(&varIndex);
      varIndex.vt = VT_I4;
      varIndex.lVal = i;
      PutDispProperty(spSelect, DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, &varIndex);
      break;
    }
  }
}

void CDHtmlDialog::DDX_DHtml_AxControl(CDataExchange* pDX, LPCTSTR szId, DISPID dispid,
                                       VARIANT& var) {
  CComPtr<IDispatch> spCtl;
  if (FAILED(GetControlDispatch(szId, &spCtl))) return;
  if (pDX->m_bSaveAndValidate) {
    VARIANT varNew;
    if (SUCCEEDED(GetDispProperty(spCtl, dispid, &varNew))) {
      ::VariantClear(&var);
      var = varNew;
    }
  } else {
    PutDispProperty(spCtl, dispid, &var);
  }
}

void CDHtmlDialog::DDX_DHtml_AxControl(CDataExchange* pDX, LPCTSTR szId, LPCTSTR szPropName,
                                       VARIANT& var) {
  CComPtr<IDispatch> spCtl;
  if (FAILED(GetControlDispatch(szId, &spCtl))) return;
  DISPID dispid = DispidOfName(spCtl, szPropName);
  if (dispid != DISPID_UNKNOWN) DDX_DHtml_AxControl(pDX, szId, dispid, var);
}

// ----- window.external

void CDHtmlDialog::SetExternalDispatch(IDispatch* pdispExternal) {
  m_spExternalDisp = pdispExternal;
}

void CDHtmlDialog::SetHostFlags(DWORD dwFlags) { m_dwHostFlags = dwFlags; }

BOOL CDHtmlDialog::IsExternalDispatchSafe() { return FALSE; }

// A page may reach the external object when the object is safe for any
// page, or when the page is on this computer: the program's resources or
// a local file.
BOOL CDHtmlDialog::CanAccessExternal() {
  if (IsExternalDispatchSafe()) return TRUE;
  CString strUrl = m_strCurrentUrl;
  if (strUrl.IsEmpty() && m_pBrowserApp != NULL) {
    BSTR bstr = NULL;
    m_pBrowserApp->get_LocationURL(&bstr);
    strUrl = StringFromBSTR(bstr);
    ::SysFreeString(bstr);
  }
  if (strUrl.IsEmpty()) return FALSE;
  // The program's own resources.
  if (strUrl.Left(6).CompareNoCase(_T("res://")) == 0) return TRUE;
  IInternetSecurityManager* pSecMgr = NULL;
  if (SUCCEEDED(::CoInternetCreateSecurityManager(NULL, &pSecMgr, 0))) {
    DWORD dwZone = URLZONE_INVALID;
    HRESULT hr = pSecMgr->MapUrlToZone(CStringW(strUrl), &dwZone, 0);
    pSecMgr->Release();
    if (SUCCEEDED(hr)) return dwZone == URLZONE_LOCAL_MACHINE;
  }
  // Without a zone, a local file.
  return strUrl.Left(5).CompareNoCase(_T("file:")) == 0;
}

// ----- The browser's questions; the defaults let the browser do its own
// thing, as a dialog: no 3-D border, no scroll bars, no text selection.

STDMETHODIMP CDHtmlDialog::ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) {
  return S_FALSE;
}
STDMETHODIMP CDHtmlDialog::GetHostInfo(DOCHOSTUIINFO* pInfo) {
  if (pInfo == NULL) return E_POINTER;
  pInfo->dwFlags = m_dwHostFlags;
  pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
  return S_OK;
}
STDMETHODIMP CDHtmlDialog::ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*,
                                  IOleInPlaceFrame*, IOleInPlaceUIWindow*) {
  return S_FALSE;
}
STDMETHODIMP CDHtmlDialog::HideUI() { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::UpdateUI() { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::EnableModeless(BOOL) { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::OnDocWindowActivate(BOOL) { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::OnFrameWindowActivate(BOOL) { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) { return E_NOTIMPL; }
STDMETHODIMP CDHtmlDialog::TranslateAccelerator(LPMSG, const GUID*, DWORD) { return S_FALSE; }
STDMETHODIMP CDHtmlDialog::GetOptionKeyPath(LPOLESTR* pchKey, DWORD) {
  if (pchKey != NULL) *pchKey = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP CDHtmlDialog::GetDropTarget(IDropTarget*, IDropTarget** ppDropTarget) {
  if (ppDropTarget != NULL) *ppDropTarget = NULL;
  return E_NOTIMPL;
}
STDMETHODIMP CDHtmlDialog::GetExternal(IDispatch** ppDispatch) {
  if (ppDispatch == NULL) return E_POINTER;
  *ppDispatch = NULL;
  if (m_spExternalDisp == NULL || !CanAccessExternal()) return E_NOTIMPL;
  return m_spExternalDisp.CopyTo(ppDispatch);
}
STDMETHODIMP CDHtmlDialog::TranslateUrl(DWORD, OLECHAR*, OLECHAR** ppchURLOut) {
  if (ppchURLOut != NULL) *ppchURLOut = NULL;
  return S_FALSE;
}
STDMETHODIMP CDHtmlDialog::FilterDataObject(IDataObject*, IDataObject** ppDORet) {
  if (ppDORet != NULL) *ppDORet = NULL;
  return S_FALSE;
}

// ---------------------------------------------------------------------
// CMultiPageDHtmlDialog

IMPLEMENT_DYNAMIC(CMultiPageDHtmlDialog, CDHtmlDialog)

CMultiPageDHtmlDialog::CMultiPageDHtmlDialog() {}

CMultiPageDHtmlDialog::CMultiPageDHtmlDialog(LPCTSTR lpszTemplateName, LPCTSTR szHtmlResID,
                                             CWnd* pParentWnd)
    : CDHtmlDialog(lpszTemplateName, szHtmlResID, pParentWnd) {}

CMultiPageDHtmlDialog::CMultiPageDHtmlDialog(UINT nIDTemplate, UINT nHtmlResID,
                                             CWnd* pParentWnd)
    : CDHtmlDialog(nIDTemplate, nHtmlResID, pParentWnd) {}

CMultiPageDHtmlDialog::~CMultiPageDHtmlDialog() {}

const DHtmlUrlEventMapEntry* CMultiPageDHtmlDialog::GetDHtmlUrlEventMap() {
  static const DHtmlUrlEventMapEntry kNone[] = {{(LPCTSTR)NULL, NULL}};
  return kNone;
}

// A page's URL against an entry's: the whole URL, or its last part (what
// LoadFromResource put after the module), without a #fragment or ?query.
static BOOL UrlMatches(const CString& strUrl, LPCTSTR szEntry) {
  CString strEntry;
  if (IS_INTRESOURCE(szEntry))
    strEntry.Format(_T("%u"), (UINT)(UINT_PTR)szEntry);
  else
    strEntry = szEntry;
  if (strUrl.CompareNoCase(strEntry) == 0) return TRUE;
  CString strLast = strUrl;
  int n = strLast.FindOneOf(_T("#?"));
  if (n >= 0) strLast = strLast.Left(n);
  n = strLast.ReverseFind(_T('/'));
  if (n >= 0) strLast = strLast.Mid(n + 1);
  if (strLast.CompareNoCase(strEntry) == 0) return TRUE;
  // A name given with its path ("pages/one.htm") matches the URL's end.
  return strEntry.GetLength() < strUrl.GetLength() &&
         strUrl.Right(strEntry.GetLength() + 1).CompareNoCase(_T("/") + strEntry) == 0;
}

const DHtmlEventMapEntry* CMultiPageDHtmlDialog::FindPageMap(LPCTSTR szUrl) {
  CString strUrl(szUrl);
  for (const DHtmlUrlEventMapEntry* p = GetDHtmlUrlEventMap(); p != NULL && p->szUrl != NULL;
       p++)
    if (p->pfnGetMap != NULL && UrlMatches(strUrl, p->szUrl)) return p->pfnGetMap();
  return NULL;
}

void CMultiPageDHtmlDialog::OnBeforeNavigate(LPDISPATCH pDisp, LPCTSTR szUrl) {
  CDHtmlDialog::OnBeforeNavigate(pDisp, szUrl);
  if (IsTopLevel(m_pBrowserApp, pDisp)) m_pPageMap = NULL;
}

void CMultiPageDHtmlDialog::OnDocumentComplete(LPDISPATCH pDisp, LPCTSTR szUrl) {
  // The new page's map, before CDHtmlDialog connects the page's elements.
  if (m_pBrowserApp != NULL && IsTopLevel(m_pBrowserApp, pDisp)) m_pPageMap = FindPageMap(szUrl);
  CDHtmlDialog::OnDocumentComplete(pDisp, szUrl);
}
