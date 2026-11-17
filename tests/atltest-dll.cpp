// atltest-dll.cpp - an in-process COM server written with ATL, as the
// wizard writes one. Its ANSI build is an ATL 3 server (CComModule
// _Module and an object map given to Init); its Unicode build an ATL 7
// one (CAtlDllModuleT and OBJECT_ENTRY_AUTO). It serves:
//   AtlTest.Thing   a dual interface over a type library (IDispatchImpl),
//                   rich errors (ISupportErrorInfo, Error), events (a
//                   connection point), IProvideClassInfo2, a tear-off and
//                   a cached tear-off, and it can be aggregated; it is
//                   registered from an .rgs script with a replacement.
//   AtlTest.Single  one object for every client (a singleton class
//                   factory), registered the old way (DECLARE_REGISTRY).
#include <atlbase.h>
#ifndef _UNICODE
CComModule _Module;
#endif
#include <atlcom.h>
#include "atltest.h"

// The objects alive, for the client to see them go.
static LONG g_nLive;
extern "C" LONG WINAPI AtlTestLiveObjects() { return g_nLive; }

class CAtlThing;

class ATL_NO_VTABLE CRare : public CComTearOffObjectBase<CAtlThing>, public IAtlRare {
public:
  BEGIN_COM_MAP(CRare)
    COM_INTERFACE_ENTRY(IAtlRare)
  END_COM_MAP()
  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    return S_OK;
  }
  void FinalRelease() { InterlockedDecrement(&g_nLive); }
  STDMETHOD(Magic)(long* p) override;
};

class ATL_NO_VTABLE CCached : public CComTearOffObjectBase<CAtlThing>, public IAtlCached {
public:
  BEGIN_COM_MAP(CCached)
    COM_INTERFACE_ENTRY(IAtlCached)
  END_COM_MAP()
  STDMETHOD(Magic)(long* p) override;
};

class ATL_NO_VTABLE CAtlThing
    : public CComObjectRootEx<CComSingleThreadModel>,
      public CComCoClass<CAtlThing, &CLSID_AtlThing>,
      public IDispatchImpl<IAtlThing, &IID_IAtlThing, &LIBID_AtlTestLib>,
      public ISupportErrorInfoImpl<&IID_IAtlThing>,
      public IConnectionPointContainerImpl<CAtlThing>,
      public IConnectionPointImpl<CAtlThing, &DIID__IAtlThingEvents>,
      public IProvideClassInfo2Impl<&CLSID_AtlThing, &DIID__IAtlThingEvents, &LIBID_AtlTestLib> {
public:
  long m_value;
  IUnknown* m_pCached;

  CAtlThing() : m_value(0), m_pCached(NULL) {}

  // The script, with %FLAGS% replaced.
  static HRESULT WINAPI UpdateRegistry(BOOL bRegister) {
    _ATL_REGMAP_ENTRY aMap[] = {{OLESTR("FLAGS"), OLESTR("7")}, {NULL, NULL}};
    return _pAtlModule->UpdateRegistryFromResource(IDR_ATLTHING, bRegister, aMap);
  }
  DECLARE_PROTECT_FINAL_CONSTRUCT()

  BEGIN_COM_MAP(CAtlThing)
    COM_INTERFACE_ENTRY(IAtlThing)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY(ISupportErrorInfo)
    COM_INTERFACE_ENTRY(IConnectionPointContainer)
    COM_INTERFACE_ENTRY(IProvideClassInfo2)
    COM_INTERFACE_ENTRY(IProvideClassInfo)
    COM_INTERFACE_ENTRY_TEAR_OFF(IID_IAtlRare, CRare)
    COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(IID_IAtlCached, CCached, m_pCached)
  END_COM_MAP()

  BEGIN_CONNECTION_POINT_MAP(CAtlThing)
    CONNECTION_POINT_ENTRY(DIID__IAtlThingEvents)
  END_CONNECTION_POINT_MAP()

  HRESULT FinalConstruct() {
    InterlockedIncrement(&g_nLive);
    return S_OK;
  }
  void FinalRelease() {
    if (m_pCached != NULL) m_pCached->Release();
    InterlockedDecrement(&g_nLive);
  }

  STDMETHOD(get_Value)(long* pVal) override {
    if (pVal == NULL) return E_POINTER;
    *pVal = m_value;
    return S_OK;
  }
  STDMETHOD(put_Value)(long newVal) override {
    m_value = newVal;
    return S_OK;
  }
  STDMETHOD(Add)(long a, long* pResult) override {
    if (pResult == NULL) return E_POINTER;
    m_value += a;
    *pResult = m_value;
    return S_OK;
  }
  STDMETHOD(Fail)(long nKind) override {
    switch (nKind) {
    case 0: return Error(OLESTR("thing failed"), IID_IAtlThing, E_FAIL);
    case 1: return Error(IDS_FAIL, IID_IAtlThing);
    default: return Error("narrow failure");
    }
  }
  // Calls Fired(n) on each sink, as the wizard's proxy class does.
  STDMETHOD(Fire)(long n) override {
    int nConnections = m_vec.GetSize();
    for (int i = 0; i < nConnections; i++) {
      Lock();
      CComPtr<IUnknown> sp = m_vec.GetAt(i);
      Unlock();
      IDispatch* pDispatch = reinterpret_cast<IDispatch*>(sp.p);
      if (pDispatch == NULL) continue;
      CComVariant arg(n);
      DISPPARAMS dp = {&arg, NULL, 1, 0};
      pDispatch->Invoke(1, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    }
    return S_OK;
  }
  STDMETHOD(get_Name)(BSTR* pVal) override {
    if (pVal == NULL) return E_POINTER;
    CComBSTR s(L"Thing ");
    WCHAR sz[16];
    wsprintfW(sz, L"%ld", m_value);
    s += sz;
    *pVal = s.Detach();
    return S_OK;
  }
};

STDMETHODIMP CRare::Magic(long* p) {
  *p = 42 + m_pOwner->m_value;
  return S_OK;
}
STDMETHODIMP CCached::Magic(long* p) {
  *p = 1000 + m_pOwner->m_value;
  return S_OK;
}

class ATL_NO_VTABLE CAtlSingle : public CComObjectRootEx<CComSingleThreadModel>,
                                 public CComCoClass<CAtlSingle, &CLSID_AtlSingle>,
                                 public IAtlRare {
public:
  long m_nCalls;
  CAtlSingle() : m_nCalls(0) {}
  DECLARE_REGISTRY(CAtlSingle, _T("AtlTest.Single.1"), _T("AtlTest.Single"), IDS_SINGLE,
                   THREADFLAGS_APARTMENT)
  DECLARE_NOT_AGGREGATABLE(CAtlSingle)
  DECLARE_CLASSFACTORY_SINGLETON(CAtlSingle)
  BEGIN_COM_MAP(CAtlSingle)
    COM_INTERFACE_ENTRY(IAtlRare)
  END_COM_MAP()
  STDMETHOD(Magic)(long* p) override {
    *p = ++m_nCalls;
    return S_OK;
  }
};

#ifdef _UNICODE
// ATL 7: the module class and the objects that add themselves.
OBJECT_ENTRY_AUTO(CLSID_AtlThing, CAtlThing)
OBJECT_ENTRY_AUTO(CLSID_AtlSingle, CAtlSingle)

class CAtlTestModule : public CAtlDllModuleT<CAtlTestModule> {
public:
  DECLARE_LIBID(LIBID_AtlTestLib)
};
CAtlTestModule _AtlModule;

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD dwReason, LPVOID lpReserved) {
  return _AtlModule.DllMain(dwReason, lpReserved);
}
STDAPI DllCanUnloadNow() { return _AtlModule.DllCanUnloadNow(); }
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}
// The type library is the client's, registered apart.
STDAPI DllRegisterServer() { return _AtlModule.DllRegisterServer(FALSE); }
STDAPI DllUnregisterServer() { return _AtlModule.DllUnregisterServer(FALSE); }
#else
// ATL 3: the object map.
BEGIN_OBJECT_MAP(ObjectMap)
  OBJECT_ENTRY(CLSID_AtlThing, CAtlThing)
  OBJECT_ENTRY(CLSID_AtlSingle, CAtlSingle)
END_OBJECT_MAP()

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
  if (dwReason == DLL_PROCESS_ATTACH) {
    _Module.Init(ObjectMap, hInstance, &LIBID_AtlTestLib);
    DisableThreadLibraryCalls(hInstance);
  } else if (dwReason == DLL_PROCESS_DETACH) {
    _Module.Term();
  }
  return TRUE;
}
STDAPI DllCanUnloadNow() { return _Module.GetLockCount() == 0 ? S_OK : S_FALSE; }
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  return _Module.GetClassObject(rclsid, riid, ppv);
}
STDAPI DllRegisterServer() { return _Module.RegisterServer(FALSE); }
STDAPI DllUnregisterServer() { return _Module.UnregisterServer(FALSE); }
#endif
