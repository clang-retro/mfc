// olefact.cpp - COleObjectFactory, which creates objects for COM clients
// and registers them in the system registry, and AfxOleInit.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxdisp.h>

IMPLEMENT_DYNAMIC(COleObjectFactory, CCmdTarget)

BEGIN_INTERFACE_MAP(COleObjectFactory, CCmdTarget)
  INTERFACE_PART(COleObjectFactory, IID_IClassFactory, ClassFactory)
  INTERFACE_PART(COleObjectFactory, IID_IClassFactory2, ClassFactory)
END_INTERFACE_MAP()

// ---------------------------------------------------------------------
// AfxOleInit

static BOOL g_bOleInit = FALSE;
extern void (AFXAPI* _afxPfnOleTerm)(BOOL);

BOOL AFXAPI AfxOleInit() {
  if (g_bOleInit) return TRUE;
  if (FAILED(::OleInitialize(NULL))) return FALSE;
  g_bOleInit = TRUE;
  _afxPfnOleTerm = AfxOleTerm;  // CWinApp::ExitInstance ends it
  return TRUE;
}

void AFXAPI AfxOleTerm(BOOL bJustRevoke) {
  COleObjectFactory::RevokeAll();
  if (bJustRevoke) return;
  _AfxReleaseTypeLibCaches();
  if (!g_bOleInit) return;
  ::OleUninitialize();
  g_bOleInit = FALSE;
  _afxPfnOleTerm = NULL;
}

// ---------------------------------------------------------------------
// The list of factories

static COleObjectFactory* g_pFirstFactory = NULL;

COleObjectFactory* PASCAL COleObjectFactory::GetFirstFactory() { return g_pFirstFactory; }

COleObjectFactory::COleObjectFactory(REFCLSID clsid, CRuntimeClass* pRuntimeClass,
                                     BOOL bMultiInstance, LPCTSTR lpszProgID)
    : COleObjectFactory(clsid, pRuntimeClass, bMultiInstance, afxRegDefault, lpszProgID) {}

COleObjectFactory::COleObjectFactory(REFCLSID clsid, CRuntimeClass* pRuntimeClass,
                                     BOOL bMultiInstance, int nFlags, LPCTSTR lpszProgID)
    : m_pNextFactory(NULL),
      m_dwRegister(0),
      m_clsid(clsid),
      m_pRuntimeClass(pRuntimeClass),
      m_bMultiInstance(bMultiInstance),
      m_lpszProgID(lpszProgID),
      m_nFlags(nFlags),
      m_bLicenseChecked(FALSE),
      m_bLicenseValid(FALSE) {
  // Keep construction order: the last one goes at the end.
  COleObjectFactory** pp = &g_pFirstFactory;
  while (*pp != NULL) pp = &(*pp)->m_pNextFactory;
  *pp = this;
}

COleObjectFactory::~COleObjectFactory() {
  Revoke();
  for (COleObjectFactory** pp = &g_pFirstFactory; *pp != NULL; pp = &(*pp)->m_pNextFactory) {
    if (*pp == this) {
      *pp = m_pNextFactory;
      break;
    }
  }
}

// ---------------------------------------------------------------------
// Registering with COM

BOOL COleObjectFactory::Register() {
  if (m_dwRegister != 0) return TRUE;
  if (::IsEqualCLSID(m_clsid, CLSID_NULL)) return FALSE;  // a template server not connected yet
  DWORD dwFlags = m_bMultiInstance ? REGCLS_SINGLEUSE : REGCLS_MULTIPLEUSE;
  HRESULT hr = ::CoRegisterClassObject(m_clsid, (LPUNKNOWN)&m_xClassFactory, CLSCTX_LOCAL_SERVER,
                                       dwFlags, &m_dwRegister);
  if (FAILED(hr)) {
    m_dwRegister = 0;
    return FALSE;
  }
  return TRUE;
}

void COleObjectFactory::Revoke() {
  if (m_dwRegister == 0) return;
  ::CoRevokeClassObject(m_dwRegister);
  m_dwRegister = 0;
}

BOOL PASCAL COleObjectFactory::RegisterAll() {
  BOOL bResult = TRUE;
  for (COleObjectFactory* p = g_pFirstFactory; p != NULL; p = p->m_pNextFactory)
    if (!p->Register()) bResult = FALSE;
  return bResult;
}

void PASCAL COleObjectFactory::RevokeAll() {
  for (COleObjectFactory* p = g_pFirstFactory; p != NULL; p = p->m_pNextFactory) p->Revoke();
}

// ---------------------------------------------------------------------
// The system registry

static BOOL SetKey(LPCTSTR lpszKey, LPCTSTR lpszValueName, LPCTSTR lpszValue) {
  HKEY hKey;
  if (::RegCreateKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) !=
      ERROR_SUCCESS)
    return FALSE;
  LONG l = ::RegSetValueEx(hKey, lpszValueName, 0, REG_SZ, (const BYTE*)lpszValue,
                           (DWORD)((lstrlen(lpszValue) + 1) * sizeof(TCHAR)));
  ::RegCloseKey(hKey);
  return l == ERROR_SUCCESS;
}

// RegDeleteTree is not on Windows XP.
static void DeleteTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyEx(hParent, lpszKey, 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
  TCHAR szName[256];
  while (::RegEnumKey(hKey, 0, szName, _countof(szName)) == ERROR_SUCCESS) DeleteTree(hKey, szName);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

static CString ClsidString(REFCLSID clsid) {
  LPOLESTR psz = NULL;
  if (FAILED(::StringFromCLSID(clsid, &psz))) return CString();
  CString str(psz);
  ::CoTaskMemFree(psz);
  return str;
}

void COleObjectFactory::UpdateRegistry(LPCTSTR lpszProgID) {
  if (lpszProgID == NULL) lpszProgID = m_lpszProgID;
  CString strClsid = ClsidString(m_clsid);
  if (strClsid.IsEmpty()) return;
  CString strKey = _T("CLSID\\") + strClsid;

  HINSTANCE hInst = AfxGetInstanceHandle();
  if (hInst == NULL) hInst = ::GetModuleHandle(NULL);
  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(hInst, szPath, MAX_PATH);
  CString strPath = szPath;
  if (strPath.Find(_T(' ')) >= 0) strPath = _T("\"") + strPath + _T("\"");

  if (lpszProgID != NULL) {
    SetKey(lpszProgID, NULL, lpszProgID);
    SetKey(CString(lpszProgID) + _T("\\CLSID"), NULL, strClsid);
    SetKey(strKey, NULL, lpszProgID);
    SetKey(strKey + _T("\\ProgID"), NULL, lpszProgID);
  }
  if (hInst != ::GetModuleHandle(NULL)) {
    // A DLL server.
    SetKey(strKey + _T("\\InprocServer32"), NULL, szPath);
    if (m_nFlags & afxRegFreeThreading)
      SetKey(strKey + _T("\\InprocServer32"), _T("ThreadingModel"),
             (m_nFlags & afxRegApartmentThreading) ? _T("Both") : _T("Free"));
    else if (m_nFlags & afxRegApartmentThreading)
      SetKey(strKey + _T("\\InprocServer32"), _T("ThreadingModel"), _T("Apartment"));
  } else {
    SetKey(strKey + _T("\\LocalServer32"), NULL, strPath + _T(" /Automation"));
  }
  if (m_nFlags & afxRegInsertable) {
    SetKey(strKey + _T("\\Insertable"), NULL, _T(""));
    if (lpszProgID != NULL) SetKey(CString(lpszProgID) + _T("\\Insertable"), NULL, _T(""));
  }
}

BOOL COleObjectFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister) {
    UpdateRegistry((LPCTSTR)NULL);
    return TRUE;
  }
  CString strClsid = ClsidString(m_clsid);
  if (strClsid.IsEmpty()) return FALSE;
  DeleteTree(HKEY_CLASSES_ROOT, _T("CLSID\\") + strClsid);
  if (m_lpszProgID != NULL) DeleteTree(HKEY_CLASSES_ROOT, m_lpszProgID);
  return TRUE;
}

BOOL COleObjectFactory::Unregister() { return UpdateRegistry(FALSE); }

BOOL PASCAL COleObjectFactory::UpdateRegistryAll(BOOL bRegister) {
  BOOL bResult = TRUE;
  for (COleObjectFactory* p = g_pFirstFactory; p != NULL; p = p->m_pNextFactory)
    if (!p->UpdateRegistry(bRegister)) bResult = FALSE;
  return bResult;
}

// ---------------------------------------------------------------------
// Creating objects

CCmdTarget* COleObjectFactory::OnCreateObject() {
  if (m_pRuntimeClass == NULL) return NULL;
  CObject* pObject = m_pRuntimeClass->CreateObject();
  if (pObject == NULL) return NULL;
  if (!pObject->IsKindOf(RUNTIME_CLASS(CCmdTarget))) {
    delete pObject;
    return NULL;
  }
  return (CCmdTarget*)pObject;
}

BOOL COleObjectFactory::IsLicenseValid() {
  if (!m_bLicenseChecked) {
    m_bLicenseValid = VerifyUserLicense();
    m_bLicenseChecked = TRUE;
  }
  return m_bLicenseValid;
}

BOOL COleObjectFactory::GetLicenseKey(DWORD, BSTR*) { return FALSE; }

BOOL COleObjectFactory::VerifyLicenseKey(BSTR bstrKey) {
  BSTR bstrMine = NULL;
  if (!GetLicenseKey(0, &bstrMine) || bstrMine == NULL) return FALSE;
  BOOL bMatch = bstrKey != NULL && ::SysStringLen(bstrKey) == ::SysStringLen(bstrMine) &&
                memcmp(bstrKey, bstrMine, ::SysStringByteLen(bstrMine)) == 0;
  ::SysFreeString(bstrMine);
  return bMatch;
}

HRESULT COleObjectFactory::CreateObjectFor(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID* ppvObject,
                                           BSTR bstrKey) {
  if (ppvObject == NULL) return E_POINTER;
  *ppvObject = NULL;
  // An aggregated object hands out its own IUnknown only.
  if (pUnkOuter != NULL && !::IsEqualIID(riid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
  if (bstrKey != NULL ? !VerifyLicenseKey(bstrKey) : !IsLicenseValid()) return CLASS_E_NOTLICENSED;

  CCmdTarget* pTarget = NULL;
  try {
    pTarget = OnCreateObject();
  } catch (CException* e) {
    HRESULT hr = COleException::Process(e);
    e->Delete();
    return hr;
  }
  if (pTarget == NULL) return E_OUTOFMEMORY;
  if (pUnkOuter != NULL) {
    if (!pTarget->m_bAggregation) {
      delete pTarget;
      return CLASS_E_NOAGGREGATION;
    }
    pTarget->m_pOuterUnknown = pUnkOuter;
  }
  if (!pTarget->OnCreateAggregates()) {
    delete pTarget;
    return E_OUTOFMEMORY;
  }
  // The object starts with one reference; the interface takes its own.
  HRESULT hr = (HRESULT)pTarget->InternalQueryInterface(&riid, ppvObject);
  pTarget->InternalRelease();
  return hr;
}

// ---------------------------------------------------------------------
// IClassFactory2

STDMETHODIMP_(ULONG) COleObjectFactory::XClassFactory::AddRef() {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleObjectFactory::XClassFactory::Release() {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleObjectFactory::XClassFactory::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleObjectFactory::XClassFactory::CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid,
                                                              LPVOID* ppvObject) {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  HRESULT hr = pThis->CreateObjectFor(pUnkOuter, riid, ppvObject, NULL);
  // A single-use class makes one object for each copy of the program: the
  // next client starts another. (COM hides a REGCLS_SINGLEUSE class after
  // one object; Wine does not, so the factory takes itself away.)
  if (SUCCEEDED(hr) && pThis->m_bMultiInstance) pThis->Revoke();
  return hr;
}

STDMETHODIMP COleObjectFactory::XClassFactory::LockServer(BOOL fLock) {
  if (fLock)
    AfxOleLockApp();
  else
    AfxOleUnlockApp();
  return S_OK;
}

STDMETHODIMP COleObjectFactory::XClassFactory::GetLicInfo(LPLICINFO pLicInfo) {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  if (pLicInfo == NULL) return E_POINTER;
  BSTR bstr = NULL;
  pLicInfo->cbLicInfo = sizeof(LICINFO);
  pLicInfo->fRuntimeKeyAvail = pThis->GetLicenseKey(0, &bstr);
  if (bstr != NULL) ::SysFreeString(bstr);
  pLicInfo->fLicVerified = pThis->IsLicenseValid();
  return S_OK;
}

STDMETHODIMP COleObjectFactory::XClassFactory::RequestLicKey(DWORD dwReserved, BSTR* pbstrKey) {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  if (pbstrKey == NULL) return E_POINTER;
  *pbstrKey = NULL;
  if (!pThis->IsLicenseValid()) return CLASS_E_NOTLICENSED;
  return pThis->GetLicenseKey(dwReserved, pbstrKey) ? S_OK : E_NOTIMPL;
}

STDMETHODIMP COleObjectFactory::XClassFactory::CreateInstanceLic(LPUNKNOWN pUnkOuter, LPUNKNOWN,
                                                                 REFIID riid, BSTR bstrKey,
                                                                 LPVOID* ppvObject) {
  METHOD_PROLOGUE(COleObjectFactory, ClassFactory)
  HRESULT hr = pThis->CreateObjectFor(pUnkOuter, riid, ppvObject, bstrKey);
  if (SUCCEEDED(hr) && pThis->m_bMultiInstance) pThis->Revoke();
  return hr;
}

// ---------------------------------------------------------------------
// DLL servers

HRESULT AFXAPI AfxDllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  if (ppv == NULL) return E_POINTER;
  *ppv = NULL;
  for (COleObjectFactory* p = g_pFirstFactory; p != NULL; p = p->m_pNextFactory)
    if (::IsEqualCLSID(p->m_clsid, rclsid))
      return (HRESULT)p->InternalQueryInterface(&riid, ppv);
  return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT AFXAPI AfxDllCanUnloadNow() { return AfxOleCanExitApp() ? S_OK : S_FALSE; }
