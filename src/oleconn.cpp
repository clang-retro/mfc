// oleconn.cpp - connection points (IConnectionPointContainer on every
// CCmdTarget, CConnectionPoint, AfxConnectionAdvise) and type library
// support (CTypeLibCache, AfxOleRegisterTypeLib).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdisp.h>

// ---------------------------------------------------------------------
// The connection map of CCmdTarget itself is empty.

const AFX_CONNECTIONMAP* CCmdTarget::GetConnectionMap() const { return GetThisConnectionMap(); }

const AFX_CONNECTIONMAP* PASCAL CCmdTarget::GetThisConnectionMap() {
  static const AFX_CONNECTIONMAP_ENTRY _connectionEntries[] = {{NULL, (size_t)-1}};
  static const AFX_CONNECTIONMAP connectionMap = {NULL, &_connectionEntries[0]};
  return &connectionMap;
}

static const AFX_CONNECTIONMAP* BaseConnectionMap(const AFX_CONNECTIONMAP* pMap) {
  return pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL;
}

void CCmdTarget::EnableConnections() {
  m_bConnections = TRUE;
  // Each point learns its container.
  for (const AFX_CONNECTIONMAP* pMap = GetConnectionMap(); pMap != NULL;
       pMap = BaseConnectionMap(pMap)) {
    for (const AFX_CONNECTIONMAP_ENTRY* pEntry = pMap->pEntry; pEntry->piid != NULL; pEntry++)
      ((CConnectionPoint*)((BYTE*)this + pEntry->nOffset))->m_pContainer = this;
  }
}

CConnectionPoint* CCmdTarget::FindConnection(REFIID iid) {
  for (const AFX_CONNECTIONMAP* pMap = GetConnectionMap(); pMap != NULL;
       pMap = BaseConnectionMap(pMap)) {
    for (const AFX_CONNECTIONMAP_ENTRY* pEntry = pMap->pEntry; pEntry->piid != NULL; pEntry++) {
      if (::IsEqualIID(iid, *(const IID*)pEntry->piid))
        return (CConnectionPoint*)((BYTE*)this + pEntry->nOffset);
    }
  }
  return NULL;
}

// ---------------------------------------------------------------------
// Enumerators

namespace {

// Enumerates a snapshot of interface pointers, each AddRef'd.
template <class IEnum, class T>
class EnumArray : public IEnum {
public:
  EnumArray(T* pItems, ULONG nCount) : m_nRef(1), m_nPos(0), m_nCount(nCount), m_pItems(pItems) {}
  virtual ~EnumArray() {
    for (ULONG i = 0; i < m_nCount; i++) Free(m_pItems[i]);
    delete[] m_pItems;
  }
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID* ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, __uuidof(IEnum))) {
      *ppv = this;
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
  STDMETHOD(Next)(ULONG celt, T* rgelt, ULONG* pceltFetched) {
    if (rgelt == NULL || (celt != 1 && pceltFetched == NULL)) return E_POINTER;
    ULONG n = 0;
    while (n < celt && m_nPos < m_nCount) {
      rgelt[n] = m_pItems[m_nPos++];
      Copy(rgelt[n]);
      n++;
    }
    if (pceltFetched != NULL) *pceltFetched = n;
    return n == celt ? S_OK : S_FALSE;
  }
  STDMETHOD(Skip)(ULONG celt) {
    while (celt > 0 && m_nPos < m_nCount) {
      m_nPos++;
      celt--;
    }
    return celt == 0 ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() {
    m_nPos = 0;
    return S_OK;
  }
  STDMETHOD(Clone)(IEnum** ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    T* pItems = new T[m_nCount ? m_nCount : 1];
    for (ULONG i = 0; i < m_nCount; i++) {
      pItems[i] = m_pItems[i];
      Copy(pItems[i]);
    }
    EnumArray* pEnum = new EnumArray(pItems, m_nCount);
    pEnum->m_nPos = m_nPos;
    *ppEnum = pEnum;
    return S_OK;
  }

private:
  static void Copy(CONNECTDATA& cd) { cd.pUnk->AddRef(); }
  static void Free(CONNECTDATA& cd) { cd.pUnk->Release(); }
  static void Copy(LPCONNECTIONPOINT& p) { p->AddRef(); }
  static void Free(LPCONNECTIONPOINT& p) { p->Release(); }

  LONG m_nRef;
  ULONG m_nPos;
  ULONG m_nCount;
  T* m_pItems;
};

typedef EnumArray<IEnumConnections, CONNECTDATA> ConnectionEnum;
typedef EnumArray<IEnumConnectionPoints, LPCONNECTIONPOINT> ConnPointEnum;

}  // namespace

// ---------------------------------------------------------------------
// IConnectionPointContainer

#define CONTAINER() \
  _AFX_NO_OFFSETOF_WARNING_BEGIN \
  CCmdTarget* pThis = (CCmdTarget*)((BYTE*)this - offsetof(CCmdTarget, m_xConnPtContainer)); \
  _AFX_NO_OFFSETOF_WARNING_END

STDMETHODIMP _AfxConnPtContainerImpl::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  CONTAINER();
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}
STDMETHODIMP_(ULONG) _AfxConnPtContainerImpl::AddRef() {
  CONTAINER();
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) _AfxConnPtContainerImpl::Release() {
  CONTAINER();
  return pThis->ExternalRelease();
}

STDMETHODIMP _AfxConnPtContainerImpl::EnumConnectionPoints(IEnumConnectionPoints** ppEnum) {
  if (ppEnum == NULL) return E_POINTER;
  *ppEnum = NULL;
  CONTAINER();
  CPtrArray points;
  for (const AFX_CONNECTIONMAP* pMap = pThis->GetConnectionMap(); pMap != NULL;
       pMap = BaseConnectionMap(pMap)) {
    for (const AFX_CONNECTIONMAP_ENTRY* pEntry = pMap->pEntry; pEntry->piid != NULL; pEntry++) {
      CConnectionPoint* pPoint = (CConnectionPoint*)((BYTE*)pThis + pEntry->nOffset);
      points.Add((LPCONNECTIONPOINT)&pPoint->m_xConnPt);
    }
  }
  // Extra points are IConnectionPoint pointers, not AddRef'd.
  CPtrArray extra;
  if (pThis->GetExtraConnectionPoints(&extra)) points.Append(extra);
  ULONG nCount = (ULONG)points.GetSize();
  LPCONNECTIONPOINT* pItems = new LPCONNECTIONPOINT[nCount ? nCount : 1];
  for (ULONG i = 0; i < nCount; i++) {
    pItems[i] = (LPCONNECTIONPOINT)points[i];
    pItems[i]->AddRef();
  }
  *ppEnum = new ConnPointEnum(pItems, nCount);
  return S_OK;
}

STDMETHODIMP _AfxConnPtContainerImpl::FindConnectionPoint(REFIID iid, IConnectionPoint** ppCP) {
  if (ppCP == NULL) return E_POINTER;
  *ppCP = NULL;
  CONTAINER();
  LPCONNECTIONPOINT lpCP = pThis->GetConnectionHook(iid);
  if (lpCP == NULL) {
    CConnectionPoint* pPoint = pThis->FindConnection(iid);
    if (pPoint != NULL) lpCP = &pPoint->m_xConnPt;
  }
  if (lpCP == NULL) {
    // The extra points, by their interface.
    CPtrArray extra;
    if (pThis->GetExtraConnectionPoints(&extra)) {
      for (INT_PTR i = 0; i < extra.GetSize() && lpCP == NULL; i++) {
        LPCONNECTIONPOINT p = (LPCONNECTIONPOINT)extra[i];
        IID iidPoint;
        if (SUCCEEDED(p->GetConnectionInterface(&iidPoint)) && ::IsEqualIID(iid, iidPoint))
          lpCP = p;
      }
    }
  }
  if (lpCP == NULL) return CONNECT_E_NOCONNECTION;
  lpCP->AddRef();
  *ppCP = lpCP;
  return S_OK;
}

// ---------------------------------------------------------------------
// CConnectionPoint

IMPLEMENT_DYNAMIC(CConnectionPoint, CCmdTarget)

BEGIN_INTERFACE_MAP(CConnectionPoint, CCmdTarget)
  INTERFACE_PART(CConnectionPoint, IID_IConnectionPoint, ConnPt)
END_INTERFACE_MAP()

CConnectionPoint::CConnectionPoint() : m_pContainer(NULL) {}

CConnectionPoint::~CConnectionPoint() {
  for (INT_PTR i = 0; i < m_connections.GetSize(); i++) {
    LPUNKNOWN p = (LPUNKNOWN)m_connections[i];
    if (p != NULL) p->Release();
  }
  m_connections.RemoveAll();
}

// A position is the index of the next connection plus one.
POSITION CConnectionPoint::GetStartPosition() const {
  return m_connections.GetSize() > 0 ? (POSITION)(INT_PTR)1 : NULL;
}

LPUNKNOWN CConnectionPoint::GetNextConnection(POSITION& pos) const {
  INT_PTR i = (INT_PTR)pos - 1;
  LPUNKNOWN p = i >= 0 && i < m_connections.GetSize() ? (LPUNKNOWN)m_connections[i] : NULL;
  pos = i + 1 < m_connections.GetSize() ? (POSITION)(INT_PTR)(i + 2) : NULL;
  return p;
}

const CPtrArray* CConnectionPoint::GetConnections() { return &m_connections; }

LPCONNECTIONPOINTCONTAINER CConnectionPoint::GetContainer() {
  if (m_pContainer == NULL) return NULL;
  return (LPCONNECTIONPOINTCONTAINER)m_pContainer->GetInterface(&IID_IConnectionPointContainer);
}

void CConnectionPoint::OnAdvise(BOOL) {}

int CConnectionPoint::GetMaxConnections() { return -1; }

HRESULT CConnectionPoint::QuerySinkInterface(LPUNKNOWN pUnkSink, void** ppInterface) {
  return pUnkSink->QueryInterface(GetIID(), ppInterface);
}

#define CONNPT() METHOD_PROLOGUE(CConnectionPoint, ConnPt)

// A connection point keeps its container alive.
STDMETHODIMP_(ULONG) CConnectionPoint::XConnPt::AddRef() {
  CONNPT();
  return pThis->m_pContainer != NULL ? pThis->m_pContainer->ExternalAddRef()
                                     : pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CConnectionPoint::XConnPt::Release() {
  CONNPT();
  return pThis->m_pContainer != NULL ? pThis->m_pContainer->ExternalRelease()
                                     : pThis->ExternalRelease();
}
STDMETHODIMP CConnectionPoint::XConnPt::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  if (ppvObj == NULL) return E_POINTER;
  if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IConnectionPoint)) {
    *ppvObj = this;
    AddRef();
    return S_OK;
  }
  *ppvObj = NULL;
  return E_NOINTERFACE;
}

STDMETHODIMP CConnectionPoint::XConnPt::GetConnectionInterface(IID* pIID) {
  if (pIID == NULL) return E_POINTER;
  CONNPT();
  *pIID = pThis->GetIID();
  return S_OK;
}

STDMETHODIMP CConnectionPoint::XConnPt::GetConnectionPointContainer(
    IConnectionPointContainer** ppCPC) {
  if (ppCPC == NULL) return E_POINTER;
  CONNPT();
  *ppCPC = pThis->GetContainer();
  if (*ppCPC == NULL) return E_FAIL;
  (*ppCPC)->AddRef();
  return S_OK;
}

STDMETHODIMP CConnectionPoint::XConnPt::Advise(LPUNKNOWN pUnkSink, DWORD* pdwCookie) {
  if (pUnkSink == NULL || pdwCookie == NULL) return E_POINTER;
  *pdwCookie = 0;
  CONNPT();
  int nMax = pThis->GetMaxConnections();
  if (nMax >= 0 && pThis->m_connections.GetSize() >= nMax) return CONNECT_E_ADVISELIMIT;
  LPUNKNOWN pSink = NULL;
  if (FAILED(pThis->QuerySinkInterface(pUnkSink, (void**)&pSink)) || pSink == NULL)
    return CONNECT_E_CANNOTCONNECT;
  pThis->m_connections.Add(pSink);
  // The cookie is the sink interface pointer.
  *pdwCookie = (DWORD)(DWORD_PTR)pSink;
  pThis->OnAdvise(TRUE);
  return S_OK;
}

STDMETHODIMP CConnectionPoint::XConnPt::Unadvise(DWORD dwCookie) {
  CONNPT();
  for (INT_PTR i = 0; i < pThis->m_connections.GetSize(); i++) {
    LPUNKNOWN p = (LPUNKNOWN)pThis->m_connections[i];
    if ((DWORD)(DWORD_PTR)p == dwCookie) {
      pThis->m_connections.RemoveAt(i);
      p->Release();
      pThis->OnAdvise(FALSE);
      return S_OK;
    }
  }
  return CONNECT_E_NOCONNECTION;
}

STDMETHODIMP CConnectionPoint::XConnPt::EnumConnections(LPENUMCONNECTIONS* ppEnum) {
  if (ppEnum == NULL) return E_POINTER;
  CONNPT();
  ULONG nCount = (ULONG)pThis->m_connections.GetSize();
  CONNECTDATA* pItems = new CONNECTDATA[nCount ? nCount : 1];
  for (ULONG i = 0; i < nCount; i++) {
    pItems[i].pUnk = (LPUNKNOWN)pThis->m_connections[i];
    pItems[i].pUnk->AddRef();
    pItems[i].dwCookie = (DWORD)(DWORD_PTR)pItems[i].pUnk;
  }
  *ppEnum = new ConnectionEnum(pItems, nCount);
  return S_OK;
}

// ---------------------------------------------------------------------
// AfxConnectionAdvise

static LPCONNECTIONPOINT FindPoint(LPUNKNOWN pUnkSrc, REFIID iid) {
  if (pUnkSrc == NULL) return NULL;
  LPCONNECTIONPOINTCONTAINER pCPC = NULL;
  if (FAILED(pUnkSrc->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) return NULL;
  LPCONNECTIONPOINT pCP = NULL;
  pCPC->FindConnectionPoint(iid, &pCP);
  pCPC->Release();
  return pCP;
}

BOOL AFXAPI AfxConnectionAdvise(LPUNKNOWN pUnkSrc, REFIID iid, LPUNKNOWN pUnkSink, BOOL bRefCount,
                                DWORD* pdwCookie) {
  LPCONNECTIONPOINT pCP = FindPoint(pUnkSrc, iid);
  if (pCP == NULL) return FALSE;
  BOOL bOK = SUCCEEDED(pCP->Advise(pUnkSink, pdwCookie));
  // Without a reference count the connection does not hold the sink.
  if (bOK && !bRefCount) pUnkSink->Release();
  pCP->Release();
  return bOK;
}

BOOL AFXAPI AfxConnectionUnadvise(LPUNKNOWN pUnkSrc, REFIID iid, LPUNKNOWN pUnkSink,
                                  BOOL bRefCount, DWORD dwCookie) {
  LPCONNECTIONPOINT pCP = FindPoint(pUnkSrc, iid);
  if (pCP == NULL) return FALSE;
  // Give back the reference that Unadvise releases.
  if (!bRefCount) pUnkSink->AddRef();
  BOOL bOK = SUCCEEDED(pCP->Unadvise(dwCookie));
  if (!bOK && !bRefCount) pUnkSink->Release();
  pCP->Release();
  return bOK;
}

// ---------------------------------------------------------------------
// Type libraries

CTypeLibCache::~CTypeLibCache() {
  // The caches live until AfxOleTerm empties them; a pointer left here
  // after OleUninitialize is not released.
}

BOOL CTypeLibCache::Lookup(LCID lcid, LPTYPELIB* pptlib) {
  if (m_ptlib == NULL || m_lcid != lcid) {
    *pptlib = NULL;
    return FALSE;
  }
  m_ptlib->AddRef();
  *pptlib = m_ptlib;
  return TRUE;
}

void CTypeLibCache::Cache(LCID lcid, LPTYPELIB ptlib) {
  if (ptlib != NULL) ptlib->AddRef();
  if (m_ptinfo != NULL) {
    m_ptinfo->Release();
    m_ptinfo = NULL;
  }
  if (m_ptlib != NULL) m_ptlib->Release();
  m_ptlib = ptlib;
  m_lcid = lcid;
}

BOOL CTypeLibCache::LookupTypeInfo(LCID lcid, REFGUID guid, LPTYPEINFO* pptinfo) {
  if (m_ptinfo == NULL || m_lcid != lcid || !::IsEqualGUID(guid, m_guidInfo)) {
    *pptinfo = NULL;
    return FALSE;
  }
  m_ptinfo->AddRef();
  *pptinfo = m_ptinfo;
  return TRUE;
}

void CTypeLibCache::CacheTypeInfo(LCID lcid, REFGUID guid, LPTYPEINFO ptinfo) {
  if (lcid != m_lcid) return;  // only for the cached library's locale
  if (ptinfo != NULL) ptinfo->AddRef();
  if (m_ptinfo != NULL) m_ptinfo->Release();
  m_ptinfo = ptinfo;
  m_guidInfo = guid;
}

namespace {
struct TypeLibCacheEntry {
  GUID tlid;
  CTypeLibCache* pCache;
};
CArray<TypeLibCacheEntry, const TypeLibCacheEntry&>& Caches() {
  static CArray<TypeLibCacheEntry, const TypeLibCacheEntry&> s_caches;
  return s_caches;
}
}  // namespace

CTypeLibCache* AFXAPI AfxGetTypeLibCache(const GUID* pTypeLibID) {
  if (pTypeLibID == NULL) return NULL;
  CArray<TypeLibCacheEntry, const TypeLibCacheEntry&>& caches = Caches();
  for (INT_PTR i = 0; i < caches.GetSize(); i++) {
    if (::IsEqualGUID(caches[i].tlid, *pTypeLibID)) return caches[i].pCache;
  }
  TypeLibCacheEntry entry;
  entry.tlid = *pTypeLibID;
  entry.pCache = new CTypeLibCache;
  entry.pCache->m_pTypeLibID = pTypeLibID;  // the class's own GUID, which stays
  caches.Add(entry);
  return entry.pCache;
}

// Called by AfxOleTerm before OLE goes.
void AFXAPI _AfxReleaseTypeLibCaches() {
  CArray<TypeLibCacheEntry, const TypeLibCacheEntry&>& caches = Caches();
  for (INT_PTR i = 0; i < caches.GetSize(); i++) {
    caches[i].pCache->Cache((LCID)-1, NULL);
    delete caches[i].pCache;
  }
  caches.RemoveAll();
}

HRESULT CCmdTarget::GetTypeInfoOfGuid(LCID lcid, REFGUID guid, LPTYPEINFO* ppTypeInfo) {
  *ppTypeInfo = NULL;
  CTypeLibCache* pCache = GetTypeLibCache();
  if (pCache != NULL && pCache->LookupTypeInfo(lcid, guid, ppTypeInfo)) return S_OK;
  LPTYPELIB ptlib = NULL;
  if (pCache == NULL || !pCache->Lookup(lcid, &ptlib)) {
    HRESULT hr = GetTypeLib(lcid, &ptlib);
    if (FAILED(hr)) return hr;
    if (pCache != NULL) pCache->Cache(lcid, ptlib);
  }
  HRESULT hr = ptlib->GetTypeInfoOfGuid(guid, ppTypeInfo);
  ptlib->Release();
  if (SUCCEEDED(hr) && pCache != NULL) pCache->CacheTypeInfo(lcid, guid, *ppTypeInfo);
  return hr;
}

BOOL AFXAPI AfxOleRegisterTypeLib(HINSTANCE hInstance, REFGUID tlid, LPCTSTR pszFileName,
                                  LPCTSTR pszHelpDir) {
  TCHAR szModule[MAX_PATH];
  if (pszFileName == NULL) {
    if (::GetModuleFileName(hInstance, szModule, MAX_PATH) == 0) return FALSE;
    pszFileName = szModule;
  }
  CStringW strFile(pszFileName);
  LPTYPELIB ptlib = NULL;
  if (FAILED(::LoadTypeLib(strFile, &ptlib))) return FALSE;
  BOOL bOK = FALSE;
  TLIBATTR* pAttr = NULL;
  if (SUCCEEDED(ptlib->GetLibAttr(&pAttr))) {
    bOK = ::IsEqualGUID(pAttr->guid, tlid);
    ptlib->ReleaseTLibAttr(pAttr);
  }
  if (bOK) {
    CStringW strHelp;
    if (pszHelpDir != NULL) strHelp = pszHelpDir;
    bOK = SUCCEEDED(::RegisterTypeLib(ptlib, (LPOLESTR)(LPCWSTR)strFile,
                                      pszHelpDir != NULL ? (LPOLESTR)(LPCWSTR)strHelp : NULL));
  }
  ptlib->Release();
  return bOK;
}

BOOL AFXAPI AfxOleUnregisterTypeLib(REFGUID tlid, WORD wVerMajor, WORD wVerMinor, LCID lcid) {
  if (wVerMajor != 0) {
    // UnRegisterTypeLib wants the library's own system kind.
    return SUCCEEDED(::UnRegisterTypeLib(tlid, wVerMajor, wVerMinor, lcid, SYS_WIN32));
  }
  // Every registered version, from the TypeLib key.
  OLECHAR szGuid[40];
  ::StringFromGUID2(tlid, szGuid, 40);
  CString strKey = CString(_T("TypeLib\\")) + CString(szGuid);
  HKEY hKey = NULL;
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, strKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return FALSE;
  CStringArray versions;
  TCHAR szName[64];
  for (DWORD i = 0; ::RegEnumKey(hKey, i, szName, 64) == ERROR_SUCCESS; i++) versions.Add(szName);
  ::RegCloseKey(hKey);
  BOOL bOK = versions.GetSize() > 0;
  for (INT_PTR i = 0; i < versions.GetSize(); i++) {
    // The key is "major.minor" in hexadecimal.
    unsigned int nMajor = 0, nMinor = 0;
    unsigned int* pn = &nMajor;
    BOOL bDigits = FALSE;
    for (LPCTSTR p = versions[i]; *p != 0; p++) {
      TCHAR c = *p;
      if (c == '.') {
        pn = &nMinor;
        continue;
      }
      int d = c >= '0' && c <= '9'   ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                     : -1;
      if (d < 0) break;
      *pn = *pn * 16 + (unsigned)d;
      bDigits = TRUE;
    }
    if (!bDigits) continue;
    if (FAILED(::UnRegisterTypeLib(tlid, (WORD)nMajor, (WORD)nMinor, lcid, SYS_WIN32)))
      bOK = FALSE;
  }
  return bOK;
}
