// cmdcom.cpp - the COM side of CCmdTarget: reference counting, interface
// maps, aggregation, and the IDispatch that a dispatch map drives.
//
// Dispatch IDs follow MFC: an entry without an explicit ID gets its
// position in its class's map plus one in the low word, and in the high
// word how many classes up from the object's own class the map is.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxdisp.h>

// ---------------------------------------------------------------------
// The maps of CCmdTarget itself are empty.

const AFX_DISPMAP* CCmdTarget::GetDispatchMap() const { return GetThisDispatchMap(); }

const AFX_DISPMAP* PASCAL CCmdTarget::GetThisDispatchMap() {
  static const AFX_DISPMAP_ENTRY _dispatchEntries[] = {
      {NULL, DISPID_UNKNOWN, NULL, VT_EMPTY, _AFX_DISP_END, _AFX_DISP_NOFN, _AFX_DISP_NOFN, NULL,
       NULL}};
  static const AFX_DISPMAP dispatchMap = {NULL, &_dispatchEntries[0]};
  return &dispatchMap;
}

const AFX_INTERFACEMAP* CCmdTarget::GetInterfaceMap() const { return GetThisInterfaceMap(); }

const AFX_INTERFACEMAP* PASCAL CCmdTarget::GetThisInterfaceMap() {
  static const AFX_INTERFACEMAP_ENTRY _interfaceEntries[] = {{NULL, (size_t)-1}};
  static const AFX_INTERFACEMAP interfaceMap = {NULL, &_interfaceEntries[0]};
  return &interfaceMap;
}

// ---------------------------------------------------------------------
// Reference counting and interfaces

#define OWNER(member) ((CCmdTarget*)((BYTE*)this - offsetof(CCmdTarget, member)))

void CCmdTarget::OnFinalRelease() { delete this; }

DWORD CCmdTarget::InternalAddRef() { return (DWORD)::InterlockedIncrement(&m_dwRef); }

DWORD CCmdTarget::InternalRelease() {
  LONG n = ::InterlockedDecrement(&m_dwRef);
  if (n == 0) {
    m_dwRef = 1;  // so that a QueryInterface and Release in a destructor do no harm
    OnFinalRelease();
  }
  return (DWORD)n;
}

LPUNKNOWN CCmdTarget::GetInterface(const void* iid) {
  LPUNKNOWN lpUnk = GetInterfaceHook(iid);
  if (lpUnk != NULL) return lpUnk;
  REFIID riid = *(const IID*)iid;
  BOOL bUnknown = ::IsEqualIID(riid, IID_IUnknown);
  // IUnknown is the first interface in the most derived map.
  for (const AFX_INTERFACEMAP* pMap = GetInterfaceMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_INTERFACEMAP_ENTRY* pEntry = pMap->pEntry; pEntry->nOffset != (size_t)-1;
         pEntry++) {
      if (pEntry->piid == NULL) continue;  // an aggregate
      if (bUnknown || ::IsEqualIID(riid, *(const IID*)pEntry->piid))
        return (LPUNKNOWN)((BYTE*)this + pEntry->nOffset);
    }
  }
  if (m_bAutomation) {
    IID iidDispatch;
    if (bUnknown || ::IsEqualIID(riid, IID_IDispatch) ||
        (GetDispatchIID(&iidDispatch) && ::IsEqualIID(riid, iidDispatch)))
      return &m_xDispatch;
  }
  return NULL;
}

LPUNKNOWN CCmdTarget::QueryAggregates(const void* iid) {
  for (const AFX_INTERFACEMAP* pMap = GetInterfaceMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_INTERFACEMAP_ENTRY* pEntry = pMap->pEntry; pEntry->nOffset != (size_t)-1;
         pEntry++) {
      if (pEntry->piid != NULL) continue;
      LPUNKNOWN lpAggr = *(LPUNKNOWN*)((BYTE*)this + pEntry->nOffset);
      LPVOID pv = NULL;
      if (lpAggr != NULL && SUCCEEDED(lpAggr->QueryInterface(*(const IID*)iid, &pv)) && pv != NULL)
        return (LPUNKNOWN)pv;
    }
  }
  return NULL;
}

DWORD CCmdTarget::InternalQueryInterface(const void* iid, LPVOID* ppvObj) {
  if (ppvObj == NULL) return (DWORD)E_POINTER;
  *ppvObj = NULL;
  if (m_pOuterUnknown != NULL && ::IsEqualIID(*(const IID*)iid, IID_IUnknown)) {
    *ppvObj = &m_xInnerUnknown;
    InternalAddRef();
    return S_OK;
  }
  LPUNKNOWN lpUnk = GetInterface(iid);
  if (lpUnk != NULL) {
    ExternalAddRef();
    *ppvObj = lpUnk;
    return S_OK;
  }
  lpUnk = QueryAggregates(iid);
  if (lpUnk != NULL) {
    *ppvObj = lpUnk;
    return S_OK;
  }
  return (DWORD)E_NOINTERFACE;
}

DWORD CCmdTarget::ExternalAddRef() {
  if (m_pOuterUnknown != NULL) return m_pOuterUnknown->AddRef();
  return InternalAddRef();
}

DWORD CCmdTarget::ExternalRelease() {
  if (m_pOuterUnknown != NULL) return m_pOuterUnknown->Release();
  return InternalRelease();
}

DWORD CCmdTarget::ExternalQueryInterface(const void* iid, LPVOID* ppvObj) {
  if (m_pOuterUnknown != NULL) return (DWORD)m_pOuterUnknown->QueryInterface(*(const IID*)iid, ppvObj);
  return InternalQueryInterface(iid, ppvObj);
}

LPUNKNOWN CCmdTarget::GetControllingUnknown() {
  if (m_pOuterUnknown != NULL) return m_pOuterUnknown;
  return GetInterface(&IID_IUnknown);
}

void CCmdTarget::EnableAutomation() { m_bAutomation = TRUE; }
void CCmdTarget::EnableAggregation() { m_bAggregation = TRUE; }

LPDISPATCH CCmdTarget::GetIDispatch(BOOL bAddRef) {
  if (!m_bAutomation) return NULL;
  if (bAddRef) ExternalAddRef();
  return &m_xDispatch;
}

static void ThrowNotSupported(SCODE sc) {
  COleDispatchException* e = new COleDispatchException(NULL, 0, 0);
  e->m_scError = sc;
  throw e;
}
void CCmdTarget::GetNotSupported() { ThrowNotSupported(CTL_E_GETNOTSUPPORTED); }
void CCmdTarget::SetNotSupported() { ThrowNotSupported(CTL_E_SETNOTSUPPORTED); }

CCmdTarget* PASCAL CCmdTarget::FromIDispatch(LPDISPATCH lpDispatch) {
  // Ours if it has our IDispatch's table of functions.
  static _AfxDispatchImpl sample;
  if (lpDispatch == NULL || *(void**)lpDispatch != *(void**)&sample) return NULL;
  return (CCmdTarget*)((BYTE*)lpDispatch - offsetof(CCmdTarget, m_xDispatch));
}

STDMETHODIMP _AfxInnerUnknownImpl::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  return (HRESULT)OWNER(m_xInnerUnknown)->InternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP_(ULONG) _AfxInnerUnknownImpl::AddRef() {
  return OWNER(m_xInnerUnknown)->InternalAddRef();
}
STDMETHODIMP_(ULONG) _AfxInnerUnknownImpl::Release() {
  return OWNER(m_xInnerUnknown)->InternalRelease();
}

// ---------------------------------------------------------------------
// Finding dispatch map entries

static BOOL IsNumbered(const AFX_DISPMAP_ENTRY* pEntry) {
  return pEntry->lDispID == DISPID_UNKNOWN && pEntry->nKind != _AFX_DISP_DEFVALUE;
}

// The DISPID of an entry found in the map nLevel classes up.
static DISPID EntryID(const AFX_DISPMAP_ENTRY* pEntry, const AFX_DISPMAP_ENTRY* pFirst,
                      int nLevel) {
  if (!IsNumbered(pEntry)) return pEntry->lDispID;
  return MAKELONG((WORD)(pEntry - pFirst + 1), (WORD)nLevel);
}

static const AFX_DISPMAP* BaseMap(const AFX_DISPMAP* pMap) {
  return pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL;
}

static const AFX_DISPMAP_ENTRY* FindByName(const AFX_DISPMAP* pMap, LPCOLESTR lpszName,
                                           DISPID* pDispID) {
  for (int nLevel = 0; pMap != NULL; pMap = BaseMap(pMap), nLevel++) {
    for (const AFX_DISPMAP_ENTRY* pEntry = pMap->lpEntries; pEntry->nKind != _AFX_DISP_END;
         pEntry++) {
      if (pEntry->nKind == _AFX_DISP_DEFVALUE || pEntry->lpszName == NULL) continue;
      if (CStringW(pEntry->lpszName).CompareNoCase(lpszName) == 0) {
        if (pDispID != NULL) *pDispID = EntryID(pEntry, pMap->lpEntries, nLevel);
        return pEntry;
      }
    }
  }
  return NULL;
}

const AFX_DISPMAP_ENTRY* CCmdTarget::GetDispEntry(DISPID dispid) {
  const AFX_DISPMAP* pFirstMap = GetDispatchMap();
  // An explicit ID.
  for (const AFX_DISPMAP* pMap = pFirstMap; pMap != NULL; pMap = BaseMap(pMap)) {
    for (const AFX_DISPMAP_ENTRY* pEntry = pMap->lpEntries; pEntry->nKind != _AFX_DISP_END;
         pEntry++) {
      if (pEntry->nKind != _AFX_DISP_DEFVALUE && pEntry->lDispID == dispid &&
          dispid != DISPID_UNKNOWN)
        return pEntry;
    }
  }
  // The default member.
  if (dispid == DISPID_VALUE) {
    for (const AFX_DISPMAP* pMap = pFirstMap; pMap != NULL; pMap = BaseMap(pMap)) {
      for (const AFX_DISPMAP_ENTRY* pEntry = pMap->lpEntries; pEntry->nKind != _AFX_DISP_END;
           pEntry++) {
        if (pEntry->nKind != _AFX_DISP_DEFVALUE) continue;
        if (pEntry->lpszName != NULL)
          return FindByName(pFirstMap, CStringW(pEntry->lpszName), NULL);
        return pEntry->lDispID != DISPID_VALUE ? GetDispEntry(pEntry->lDispID) : NULL;
      }
    }
    return NULL;
  }
  // A position.
  int nIndex = LOWORD(dispid) - 1;
  int nLevel = HIWORD(dispid);
  if (nIndex < 0) return NULL;
  const AFX_DISPMAP* pMap = pFirstMap;
  for (int i = 0; i < nLevel && pMap != NULL; i++) pMap = BaseMap(pMap);
  if (pMap == NULL) return NULL;
  for (int i = 0; i < nIndex; i++)
    if (pMap->lpEntries[i].nKind == _AFX_DISP_END) return NULL;
  const AFX_DISPMAP_ENTRY* pEntry = &pMap->lpEntries[nIndex];
  return pEntry->nKind != _AFX_DISP_END && IsNumbered(pEntry) ? pEntry : NULL;
}

// ---------------------------------------------------------------------
// Converting arguments

LONGLONG AFXAPI _AfxDispGetInt(const VARIANT& v) {
  switch (v.vt) {
  case VT_I1: return v.cVal;
  case VT_UI1: return v.bVal;
  case VT_I2: return v.iVal;
  case VT_UI2: return v.uiVal;
  case VT_I4: return v.lVal;
  case VT_UI4: return v.ulVal;
  case VT_INT: return v.intVal;
  case VT_UINT: return v.uintVal;
  case VT_I8: return v.llVal;
  case VT_UI8: return (LONGLONG)v.ullVal;
  case VT_BOOL: return v.boolVal != VARIANT_FALSE ? TRUE : FALSE;
  case VT_ERROR: return v.scode;
  case VT_R4: return (LONGLONG)v.fltVal;
  case VT_R8:
  case VT_DATE: return (LONGLONG)v.dblVal;
  case VT_CY: return v.cyVal.int64 / 10000;
  default: return 0;
  }
}

double AFXAPI _AfxDispGetReal(const VARIANT& v) {
  switch (v.vt) {
  case VT_R4: return v.fltVal;
  case VT_R8:
  case VT_DATE: return v.dblVal;
  case VT_CY: return (double)v.cyVal.int64 / 10000;
  case VT_UI8: return (double)v.ullVal;
  default: return (double)_AfxDispGetInt(v);
  }
}

// Converts v to vt. VT_ERROR is not a type VariantChangeType makes.
static HRESULT ChangeType(VARIANT* pDest, const VARIANT* pSrc, VARTYPE vt) {
  const VARIANT* p = pSrc;
  if (p->vt == (VT_BYREF | VT_VARIANT)) p = p->pvarVal;
  if (vt == VT_ERROR) {
    VARIANT t;
    ::VariantInit(&t);
    HRESULT hr = ::VariantChangeType(&t, const_cast<VARIANT*>(p), 0, VT_I4);
    if (FAILED(hr)) {
      if ((p->vt & ~VT_BYREF) != VT_ERROR) return hr;
      t.lVal = (p->vt & VT_BYREF) ? *p->pscode : p->scode;
    }
    ::VariantClear(pDest);
    pDest->vt = VT_ERROR;
    pDest->scode = t.lVal;
    return S_OK;
  }
  return ::VariantChangeType(pDest, const_cast<VARIANT*>(p), 0, vt);
}

// Stores val through the caller's by-reference argument.
static void StoreByRef(VARIANT* pRef, VARIANT& val) {
  if (pRef->vt == (VT_BYREF | VT_VARIANT)) {
    // Keep the type the caller's VARIANT had, when the value converts.
    VARIANT* pDest = pRef->pvarVal;
    VARIANT t;
    ::VariantInit(&t);
    if (pDest->vt == VT_EMPTY || (pDest->vt & VT_BYREF) ||
        FAILED(ChangeType(&t, &val, pDest->vt)))
      ::VariantCopy(&t, &val);
    ::VariantClear(pDest);
    *pDest = t;
    return;
  }
  VARTYPE vt = (VARTYPE)(pRef->vt & ~VT_BYREF);
  VARIANT t;
  ::VariantInit(&t);
  if (FAILED(ChangeType(&t, &val, vt))) return;
  switch (vt) {
  case VT_BSTR:
    ::SysFreeString(*pRef->pbstrVal);
    *pRef->pbstrVal = t.bstrVal;
    return;  // moved
  case VT_DISPATCH:
  case VT_UNKNOWN:
    if (*pRef->ppunkVal != NULL) (*pRef->ppunkVal)->Release();
    *pRef->ppunkVal = t.punkVal;
    return;  // moved
  case VT_I1: case VT_UI1: *(BYTE*)pRef->byref = t.bVal; break;
  case VT_I2: case VT_UI2: case VT_BOOL: *(USHORT*)pRef->byref = t.uiVal; break;
  case VT_I4: case VT_UI4: case VT_INT: case VT_UINT: case VT_ERROR: case VT_R4:
    *(ULONG*)pRef->byref = t.ulVal;
    break;
  case VT_I8: case VT_UI8: case VT_R8: case VT_DATE: case VT_CY:
    *(ULONGLONG*)pRef->byref = t.ullVal;
    break;
  default: break;
  }
  ::VariantClear(&t);
}

// ---------------------------------------------------------------------
// Member variable properties

static void GetMember(void* pm, VARTYPE vt, VARIANT* pResult) {
  ::VariantInit(pResult);
  switch (vt) {
  case VT_I1: pResult->cVal = *(CHAR*)pm; break;
  case VT_UI1: pResult->bVal = *(BYTE*)pm; break;
  case VT_I2: pResult->iVal = *(short*)pm; break;
  case VT_UI2: pResult->uiVal = *(USHORT*)pm; break;
  case VT_I4: case VT_ERROR: pResult->lVal = *(long*)pm; break;
  case VT_UI4: pResult->ulVal = *(ULONG*)pm; break;
  case VT_I8: case VT_UI8: pResult->llVal = *(LONGLONG*)pm; break;
  case VT_R4: pResult->fltVal = *(float*)pm; break;
  case VT_R8: case VT_DATE: pResult->dblVal = *(double*)pm; break;
  case VT_CY: pResult->cyVal = *(CY*)pm; break;
  case VT_BOOL: pResult->boolVal = *(BOOL*)pm ? VARIANT_TRUE : VARIANT_FALSE; break;
  case VT_BSTR: pResult->bstrVal = ::SysAllocString(CStringW(*(CString*)pm)); break;
  case VT_DISPATCH:
  case VT_UNKNOWN:
    pResult->punkVal = *(LPUNKNOWN*)pm;
    if (pResult->punkVal != NULL) pResult->punkVal->AddRef();
    break;
  case VT_VARIANT: ::VariantCopy(pResult, (VARIANT*)pm); return;
  default: return;
  }
  pResult->vt = vt;
}

static void SetMember(void* pm, VARTYPE vt, AFX_DISPARG& a) {
  const VARIANT& v = a.v;
  switch (vt) {
  case VT_I1: case VT_UI1: *(BYTE*)pm = v.bVal; break;
  case VT_I2: case VT_UI2: *(USHORT*)pm = v.uiVal; break;
  case VT_I4: case VT_UI4: case VT_ERROR: *(ULONG*)pm = v.ulVal; break;
  case VT_I8: case VT_UI8: *(LONGLONG*)pm = v.llVal; break;
  case VT_R4: *(float*)pm = v.fltVal; break;
  case VT_R8: case VT_DATE: *(double*)pm = v.dblVal; break;
  case VT_CY: *(CY*)pm = v.cyVal; break;
  case VT_BOOL: *(BOOL*)pm = v.boolVal != VARIANT_FALSE; break;
  case VT_BSTR: *(CString*)pm = v.bstrVal; break;
  case VT_DISPATCH:
  case VT_UNKNOWN: {
    LPUNKNOWN pOld = *(LPUNKNOWN*)pm;
    *(LPUNKNOWN*)pm = v.punkVal;
    if (v.punkVal != NULL) v.punkVal->AddRef();
    if (pOld != NULL) pOld->Release();
    break;
  }
  case VT_VARIANT:
    ::VariantCopy((VARIANT*)pm, a.pVar);
    break;
  default: break;
  }
}

// ---------------------------------------------------------------------
// Invoke

HRESULT CCmdTarget::InvokeEntry(const AFX_DISPMAP_ENTRY* pEntry, WORD wFlags,
                                DISPPARAMS* pDispParams, VARIANT* pvarResult,
                                EXCEPINFO* pExcepInfo, UINT* puArgErr) {
  BOOL bSet = (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
  if (pEntry->nKind == _AFX_DISP_METHOD) {
    if ((wFlags & (DISPATCH_METHOD | DISPATCH_PROPERTYGET)) == 0) return DISP_E_MEMBERNOTFOUND;
    bSet = FALSE;
  } else if (!bSet && (wFlags & (DISPATCH_METHOD | DISPATCH_PROPERTYGET)) == 0) {
    return DISP_E_MEMBERNOTFOUND;
  }
  if (bSet && pEntry->nKind == _AFX_DISP_PROPERTY_EX && pEntry->pfnSetInvoke == NULL)
    return DISP_E_MEMBERNOTFOUND;

  static DISPPARAMS noParams = {NULL, NULL, 0, 0};
  if (pDispParams == NULL) pDispParams = &noParams;
  // Only the value of a property put may be named.
  if (pDispParams->cNamedArgs > 1 ||
      (pDispParams->cNamedArgs == 1 &&
       (!bSet || pDispParams->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT)))
    return DISP_E_NONAMEDARGS;

  // The parameter types: the entry's, then the value for a put.
  CStringA strTypes;
  if (pEntry->nKind != _AFX_DISP_PROPERTY && pEntry->lpszParams != NULL)
    strTypes = pEntry->lpszParams;
  if (bSet) strTypes += (char)pEntry->vt;
  UINT nArgs = (UINT)strTypes.GetLength();
  if (pDispParams->cArgs != nArgs) return DISP_E_BADPARAMCOUNT;

  AFX_DISPARG* pArgs = nArgs != 0 ? new AFX_DISPARG[nArgs] : NULL;
  HRESULT hr = S_OK;
  for (UINT i = 0; i < nArgs; i++) {
    UINT k = nArgs - 1 - i;  // arguments come last first
    VARIANT* pSrc = &pDispParams->rgvarg[k];
    BYTE code = (BYTE)strTypes[(int)i];
    BOOL bByRef = (code & VT_MFCBYREF) != 0 && code != VT_MFCMARKER;
    VARTYPE vt = (VARTYPE)(bByRef ? code & ~VT_MFCBYREF : code);
    AFX_DISPARG& a = pArgs[i];
    if (vt == VT_VARIANT) {
      a.pVar = pSrc->vt == (VT_BYREF | VT_VARIANT) ? pSrc->pvarVal : pSrc;
      continue;
    }
    if (vt == 14) vt = VT_BSTR;  // VTS_BSTR in an ANSI program: the thunk makes the LPCSTR
    hr = ChangeType(&a.v, pSrc, vt);
    if (FAILED(hr)) {
      if (puArgErr != NULL) *puArgErr = k;
      if (hr != DISP_E_OVERFLOW) hr = DISP_E_TYPEMISMATCH;
      break;
    }
    if (vt == VT_BOOL) a.bVal = a.v.boolVal != VARIANT_FALSE;
    if (bByRef && (pSrc->vt & VT_BYREF)) a.pRef = pSrc;
  }

  VARIANT result;
  ::VariantInit(&result);
  if (SUCCEEDED(hr)) {
    m_bResultExpected = pvarResult != NULL;
    try {
      if (pEntry->nKind == _AFX_DISP_PROPERTY) {
        void* pm = pEntry->pfnMember(this);
        if (bSet) {
          SetMember(pm, pEntry->vt, pArgs[0]);
          if (pEntry->pfnNotify != NULL) pEntry->pfnNotify(this);
        } else {
          GetMember(pm, pEntry->vt, &result);
        }
      } else if (bSet) {
        hr = pEntry->pfnSetInvoke(this, pEntry->pfnSet, pArgs, nArgs, &result);
        ::VariantClear(&result);
      } else {
        hr = pEntry->pfnInvoke(this, pEntry->pfn, pArgs, nArgs, &result);
      }
    } catch (CException* e) {
      if (pExcepInfo != NULL) COleDispatchException::Process(pExcepInfo, e);
      e->Delete();
      hr = DISP_E_EXCEPTION;
    }
    m_bResultExpected = TRUE;
  }

  if (SUCCEEDED(hr)) {
    for (UINT i = 0; i < nArgs; i++) {
      AFX_DISPARG& a = pArgs[i];
      if (a.pRef == NULL) continue;
      if (a.v.vt == VT_BOOL) a.v.boolVal = a.bVal ? VARIANT_TRUE : VARIANT_FALSE;
      StoreByRef(a.pRef, a.v);
    }
    VARTYPE vtResult = bSet ? (VARTYPE)VT_EMPTY : pEntry->vt;
    if (pvarResult != NULL) {
      ::VariantInit(pvarResult);
      if (vtResult == VT_EMPTY || vtResult == VT_VARIANT || result.vt == vtResult) {
        *pvarResult = result;  // moved
        ::VariantInit(&result);
      } else {
        hr = ChangeType(pvarResult, &result, vtResult);
      }
    }
  }
  ::VariantClear(&result);
  delete[] pArgs;
  return hr;
}

// ---------------------------------------------------------------------
// IDispatch

STDMETHODIMP _AfxDispatchImpl::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  return (HRESULT)OWNER(m_xDispatch)->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP_(ULONG) _AfxDispatchImpl::AddRef() { return OWNER(m_xDispatch)->ExternalAddRef(); }
STDMETHODIMP_(ULONG) _AfxDispatchImpl::Release() { return OWNER(m_xDispatch)->ExternalRelease(); }

STDMETHODIMP _AfxDispatchImpl::GetTypeInfoCount(UINT* pctinfo) {
  if (pctinfo == NULL) return E_POINTER;
  *pctinfo = OWNER(m_xDispatch)->GetTypeInfoCount();
  return S_OK;
}

STDMETHODIMP _AfxDispatchImpl::GetTypeInfo(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) {
  if (pptinfo == NULL) return E_POINTER;
  *pptinfo = NULL;
  CCmdTarget* pThis = OWNER(m_xDispatch);
  IID iid;
  if (itinfo != 0 || pThis->GetTypeInfoCount() == 0 || !pThis->GetDispatchIID(&iid))
    return DISP_E_BADINDEX;
  return pThis->GetTypeInfoOfGuid(lcid, iid, pptinfo);
}

STDMETHODIMP _AfxDispatchImpl::GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID,
                                             DISPID* rgdispid) {
  if (!::IsEqualIID(riid, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
  if (cNames == 0 || rgszNames == NULL || rgdispid == NULL) return E_INVALIDARG;
  CCmdTarget* pThis = OWNER(m_xDispatch);
  HRESULT hr = S_OK;
  if (FindByName(pThis->GetDispatchMap(), rgszNames[0], &rgdispid[0]) == NULL) {
    rgdispid[0] = DISPID_UNKNOWN;
    hr = DISP_E_UNKNOWNNAME;
  }
  // Arguments are not named.
  for (UINT i = 1; i < cNames; i++) {
    rgdispid[i] = DISPID_UNKNOWN;
    hr = DISP_E_UNKNOWNNAME;
  }
  return hr;
}

STDMETHODIMP _AfxDispatchImpl::Invoke(DISPID dispid, REFIID riid, LCID, WORD wFlags,
                                      DISPPARAMS* pdispparams, VARIANT* pvarResult,
                                      EXCEPINFO* pexcepinfo, UINT* puArgErr) {
  if (!::IsEqualIID(riid, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
  CCmdTarget* pThis = OWNER(m_xDispatch);
  const AFX_DISPMAP_ENTRY* pEntry = pThis->GetDispEntry(dispid);
  if (pEntry == NULL) return DISP_E_MEMBERNOTFOUND;
  if (!pThis->IsInvokeAllowed(dispid)) return E_UNEXPECTED;
  // Keep the object alive for the call.
  pThis->ExternalAddRef();
  HRESULT hr = pThis->InvokeEntry(pEntry, wFlags, pdispparams, pvarResult, pexcepinfo, puArgErr);
  pThis->ExternalRelease();
  return hr;
}
